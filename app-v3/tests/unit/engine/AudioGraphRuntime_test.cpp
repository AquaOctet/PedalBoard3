// Tests for pedalboard3::engine::AudioGraphRuntime -- the atomic graph-swap
// primitive: null-safety, swap correctness, the generation counter and the
// built-in OutputSanitiser pass. The patch-switch state machine on top of it
// is Session's (Session_test).

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioGraphRuntime.h"
#include "support/OfflineRender.h"
#include "support/TestGainProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <thread>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{

using AudioGraph = juce::AudioProcessorGraph;

// Build a minimal stereo graph: audioInput -> gainProc -> audioOutput.
// Return the graph (ownership moves to caller) and the gain node's id so
// tests can introspect if needed. The graph is NOT prepared yet.
struct BuiltGraph
{
    std::unique_ptr<AudioGraph> graph;
    AudioGraph::NodeID gainNodeId;
};

BuiltGraph buildGainGraph(float gain)
{
    auto g = std::make_unique<AudioGraph>();
    g->setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);

    auto inNode = g->addNode(
        std::make_unique<AudioGraph::AudioGraphIOProcessor>(AudioGraph::AudioGraphIOProcessor::audioInputNode));
    auto outNode = g->addNode(
        std::make_unique<AudioGraph::AudioGraphIOProcessor>(AudioGraph::AudioGraphIOProcessor::audioOutputNode));
    // midiInputNode is required for the graph to route the input MidiBuffer
    // (passed to processBlock) into nodes that acceptsMidi(). Without it,
    // AudioProcessorGraph drops incoming MIDI on the floor -- which broke the
    // all-notes-off flush test that expected the gain processor to see
    // 16ch × 3 events after a swap.
    auto midiInNode = g->addNode(
        std::make_unique<AudioGraph::AudioGraphIOProcessor>(AudioGraph::AudioGraphIOProcessor::midiInputNode));
    auto gainNode = g->addNode(std::make_unique<TestGainProcessor>(gain));

    // Wire stereo input -> gain -> output.
    g->addConnection({{inNode->nodeID, 0}, {gainNode->nodeID, 0}});
    g->addConnection({{inNode->nodeID, 1}, {gainNode->nodeID, 1}});
    g->addConnection({{gainNode->nodeID, 0}, {outNode->nodeID, 0}});
    g->addConnection({{gainNode->nodeID, 1}, {outNode->nodeID, 1}});
    // Wire MIDI: graph's MIDI-in -> gain processor's MIDI-in pin.
    g->addConnection(
        {{midiInNode->nodeID, AudioGraph::midiChannelIndex}, {gainNode->nodeID, AudioGraph::midiChannelIndex}});

    return {std::move(g), gainNode->nodeID};
}

} // namespace

TEST_CASE("AudioGraphRuntime: no live graph produces silence", "[engine][runtime]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.5f);

    renderBlocks(rt, buf);

    REQUIRE(isSilent(buf));
    REQUIRE(rt.getLiveGraph() == nullptr);
}

TEST_CASE("AudioGraphRuntime: swapped-in graph runs and audio flows", "[engine][runtime]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    auto built = buildGainGraph(2.0f);
    built.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    auto* graphRaw = built.graph.get();

    auto prev = rt.swapPreparedGraph(std::move(built.graph));
    REQUIRE(prev == nullptr); // no prior live graph
    REQUIRE(rt.getLiveGraph() == graphRaw);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.25f);

    renderBlocks(rt, buf);

    // Gain of 2 applied: 0.25 -> 0.5. Sanitiser lets it through (well under
    // the 1.5 ceiling).
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            REQUIRE(buf.getReadPointer(ch)[n] == 0.5f);
}

TEST_CASE("AudioGraphRuntime: swapping returns the previously-live graph", "[engine][runtime]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    auto a = buildGainGraph(2.0f);
    a.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    auto* aRaw = a.graph.get();
    auto pre = rt.swapPreparedGraph(std::move(a.graph));
    REQUIRE(pre == nullptr);

    auto b = buildGainGraph(3.0f);
    b.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    auto* bRaw = b.graph.get();
    auto outA = rt.swapPreparedGraph(std::move(b.graph));

    REQUIRE(outA.get() == aRaw);
    REQUIRE(rt.getLiveGraph() == bRaw);

    // Clean up the returned graph on the message thread (we are the
    // message thread for testing purposes). The runtime dtor will dispose
    // of `b` when `rt` goes out of scope.
    outA.reset();
}

TEST_CASE("AudioGraphRuntime: generation counter advances with each processed block", "[engine][runtime]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    const auto startGen = rt.getGenerationAckedByAudio();

    auto buf = makeTestBuffer(2, kDefaultBlockSize * 3);
    renderBlocks(rt, buf);

    REQUIRE(rt.getGenerationAckedByAudio() - startGen == 3u);
}

TEST_CASE("AudioGraphRuntime: swap with no live graph performs no flush, no crash", "[engine][runtime][midi]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    REQUIRE(rt.getLiveGraph() == nullptr);

    auto next = buildGainGraph(1.0f);
    next.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    auto* graphPtr = next.graph.get();

    // Drive processBlock from a separate thread so the audio-thread-side
    // swap path runs (no audio pump -> message-thread-side fallback,
    // which doesn't exercise the flush branch we want to validate).
    std::atomic<bool> stop{false};
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    juce::MidiBuffer midi;
    std::thread pump(
        [&]
        {
            while (!stop.load(std::memory_order_acquire))
            {
                rt.processBlock(buf, midi);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

    auto prev = rt.swapPreparedGraph(std::move(next.graph));
    REQUIRE(prev == nullptr);
    REQUIRE(rt.getLiveGraph() == graphPtr);

    stop.store(true, std::memory_order_release);
    pump.join();
}

TEST_CASE("AudioGraphRuntime: swap flushes all-notes-off on outgoing MIDI receiver", "[engine][runtime][midi]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    // First graph -- the one that will be swapped OUT and should receive
    // the all-notes-off flush. Hold a pointer to its gain processor so
    // we can inspect its last-seen MIDI count after the swap.
    auto outgoing = buildGainGraph(1.0f);
    outgoing.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    auto* outgoingProc =
        dynamic_cast<TestGainProcessor*>(outgoing.graph->getNodeForId(outgoing.gainNodeId)->getProcessor());
    REQUIRE(outgoingProc != nullptr);

    // First swap-in goes via the synchronous fallback (audio not pumping yet).
    rt.swapPreparedGraph(std::move(outgoing.graph));

    // Baseline: render a couple of empty blocks; outgoingProc should record
    // zero MIDI events.
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    juce::MidiBuffer midi;
    rt.processBlock(buf, midi);
    rt.processBlock(buf, midi);
    REQUIRE(outgoingProc->lastMidiEventCount.load() == 0);

    // Now build the incoming graph and start an audio pump on a separate
    // thread so swapPreparedGraph takes the audio-thread-side path.
    auto incoming = buildGainGraph(2.0f);
    incoming.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    std::atomic<bool> stop{false};
    std::thread pump(
        [&]
        {
            auto pumpBuf = makeTestBuffer(2, kDefaultBlockSize);
            juce::MidiBuffer pumpMidi;
            while (!stop.load(std::memory_order_acquire))
            {
                rt.processBlock(pumpBuf, pumpMidi);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

    // Swap. The pump thread will consume pending_, run flushMidiReceivers
    // on outgoing (which sets lastMidiEventCount on outgoingProc), then
    // commit live_.
    auto prev = rt.swapPreparedGraph(std::move(incoming.graph));

    // Wait until the pump thread has actually run the post-swap block that
    // performs the flush, rather than assuming it was scheduled before we stop
    // it -- that assumption races on a loaded CI runner and spuriously fails.
    // Bounded so a genuine regression (flush never happens) still fails fast
    // instead of hanging. Once the flush runs, live_ points to incoming and
    // outgoingProc is never called again, so the count is sticky.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (outgoingProc->lastMidiEventCount.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    stop.store(true, std::memory_order_release);
    pump.join();

    // The outgoing processor should have observed the full 16ch × 3-event
    // flush in its final block. After that, live_ pointed to incoming, so
    // outgoingProc was never called again -- its count is sticky at 48.
    REQUIRE(outgoingProc->lastMidiEventCount.load() == 16 * 3);

    // prev is the graph we built into outgoing -- drop it on the message
    // thread (this thread) so plugin dtors run here.
    prev.reset();
}

TEST_CASE("AudioGraphRuntime: sanitiser pass zeros any NaN/Inf/out-of-range the graph emits",
          "[engine][runtime][sanitiser]")
{
    // Build a graph whose sole effect is to produce 2.0 (over the 1.5 ceiling)
    // regardless of input. The runtime's sanitiser should zero it.
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    auto built = buildGainGraph(10.0f); // input 0.25 * 10 = 2.5 -> clipped
    built.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    rt.swapPreparedGraph(std::move(built.graph));

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.25f); // graph produces 2.5, sanitiser -> 0

    renderBlocks(rt, buf);

    REQUIRE(isSilent(buf));
    REQUIRE(isFiniteEverywhere(buf));
}

// --- The Test Tone as a master stage (#111) -------------------------------
//
// The bug report was "running Test Tone breaks the chain" / "the whole chain
// vanishes". It was accurate: the tone used to be a graph that replaced the
// patch. These pin the replacement design, where the tone is a stage the
// output passes through and the graph underneath is never touched.

TEST_CASE("AudioGraphRuntime: the test tone plays without disturbing the live graph", "[engine][runtime][test-tone]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    auto built = buildGainGraph(2.0f);
    built.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    auto* graphRaw = built.graph.get();
    rt.swapPreparedGraph(std::move(built.graph));

    rt.getTestTone().setEnabled(true);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    for (int i = 0; i < 8; ++i) // past the crossfade
    {
        fillDC(buf, 0.25f);
        renderBlocks(rt, buf);
    }

    // The graph is exactly where it was. This is the whole point: no swap,
    // no rebuild, no plugin destroyed and re-instantiated from its state.
    REQUIRE(rt.getLiveGraph() == graphRaw);

    // And the tone, not the gain stage's 0.5, is what came out.
    float peak = 0.0f;
    for (int n = 0; n < buf.getNumSamples(); ++n)
        peak = juce::jmax(peak, std::abs(buf.getReadPointer(0)[n]));
    REQUIRE(peak > engine::TestTone::kAmplitude * 0.9f);
    REQUIRE(peak <= engine::TestTone::kAmplitude + 1.0e-6f);
}

TEST_CASE("AudioGraphRuntime: switching the tone off returns the board's own output", "[engine][runtime][test-tone]")
{
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    auto built = buildGainGraph(2.0f);
    built.graph->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    auto* graphRaw = built.graph.get();
    rt.swapPreparedGraph(std::move(built.graph));

    auto buf = makeTestBuffer(2, kDefaultBlockSize);

    rt.getTestTone().setEnabled(true);
    for (int i = 0; i < 8; ++i)
    {
        fillDC(buf, 0.25f);
        renderBlocks(rt, buf);
    }

    rt.getTestTone().setEnabled(false);
    for (int i = 0; i < 8; ++i)
    {
        fillDC(buf, 0.25f);
        renderBlocks(rt, buf);
    }

    // Straight back to gain-of-2 on 0.25, with no reload having happened.
    REQUIRE(rt.getLiveGraph() == graphRaw);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int n = 0; n < buf.getNumSamples(); ++n)
            REQUIRE(buf.getReadPointer(ch)[n] == 0.5f);
}

TEST_CASE("AudioGraphRuntime: mute silences the test tone", "[engine][runtime][test-tone]")
{
    // The status bar shows TEST TONE and MUTED together and promises that
    // muted means silent. That only holds if the mute runs after the tone,
    // which is why the order in processBlock is sanitiser, tone, mute.
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    rt.getTestTone().setEnabled(true);
    rt.getMasterMute().setMuted(true);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    for (int i = 0; i < 8; ++i)
    {
        fillDC(buf, 0.0f);
        renderBlocks(rt, buf);
    }

    REQUIRE(isSilent(buf));
}

TEST_CASE("AudioGraphRuntime: the input meter reads the input, not the board", "[engine][runtime][input-level]")
{
    // #117. With no graph at all the output is silence, but the input is
    // still arriving -- and a meter that reads zero because the board is
    // empty answers the wrong question.
    engine::AudioGraphRuntime rt;
    rt.setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    rt.prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);

    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.6f);
    renderBlocks(rt, buf);

    REQUIRE(isSilent(buf)); // no graph: nothing comes out
    REQUIRE(rt.getInputLevel().readAndClear() == 0.6f);
}
