// The transport reaches hosted plugins (M6 U1): AudioGraphRuntime makes it
// the live graph's play head, JUCE's graph hands it to every node each block,
// and BypassableInstance passes it on to the plugin it wraps.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/AudioGraphRuntime.h"
#include "engine/BypassableInstance.h"
#include "support/OfflineRender.h"
#include "support/TestGainProcessor.h"

#include <atomic>
#include <memory>

using namespace pedalboard3;
using Catch::Approx;

namespace
{

// A unity-gain processor that records the play head it sees each block.
class PlayHeadProbe final : public testing::TestGainProcessor
{
public:
    PlayHeadProbe() : TestGainProcessor(1.0f) {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        TestGainProcessor::processBlock(buffer, midi);
        auto* head = getPlayHead();
        sawPlayHead = head != nullptr;
        if (head == nullptr)
            return;
        if (const auto position = head->getPosition())
        {
            bpm = position->getBpm().orFallback(0.0);
            ppq = position->getPpqPosition().orFallback(-1.0);
            playing = position->getIsPlaying();
        }
    }

    std::atomic<bool> sawPlayHead{false};
    std::atomic<double> bpm{0.0};
    std::atomic<double> ppq{-1.0};
    std::atomic<bool> playing{false};
};

} // namespace

TEST_CASE("PlayHead: a plugin inside BypassableInstance sees the transport", "[engine][transport]")
{
    constexpr double rate = testing::kDefaultSampleRate;
    constexpr int block = testing::kDefaultBlockSize;

    auto probeOwned = std::make_unique<PlayHeadProbe>();
    auto* probe = probeOwned.get();

    using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    graph->setPlayConfigDetails(2, 2, rate, block);
    auto in = graph->addNode(std::make_unique<IO>(IO::audioInputNode));
    auto out = graph->addNode(std::make_unique<IO>(IO::audioOutputNode));
    auto node = graph->addNode(std::make_unique<engine::BypassableInstance>(std::move(probeOwned)));
    for (int ch = 0; ch < 2; ++ch)
    {
        graph->addConnection({{in->nodeID, ch}, {node->nodeID, ch}});
        graph->addConnection({{node->nodeID, ch}, {out->nodeID, ch}});
    }
    graph->prepareToPlay(rate, block);

    engine::AudioGraphRuntime runtime;
    runtime.setPlayConfigDetails(2, 2, rate, block);
    runtime.swapPreparedGraph(std::move(graph)); // not prepared yet: a direct swap, no audio-thread wait
    runtime.prepareToPlay(rate, block);

    auto& transport = runtime.getTransport();
    transport.setTempo(100.0);
    transport.play();

    auto buffer = testing::makeTestBuffer(2, block);
    juce::MidiBuffer midi;

    runtime.processBlock(buffer, midi); // the first block starts at 0
    REQUIRE(probe->sawPlayHead.load());
    REQUIRE(probe->bpm.load() == Approx(100.0));
    REQUIRE(probe->playing.load());
    REQUIRE(probe->ppq.load() == Approx(0.0));

    runtime.processBlock(buffer, midi); // the second, one block on
    REQUIRE(probe->ppq.load() == Approx(engine::transport::advancePpq(0.0, block, rate, 100.0)));

    transport.stop();
    runtime.processBlock(buffer, midi);
    REQUIRE_FALSE(probe->playing.load());
    REQUIRE(probe->ppq.load() == Approx(0.0)); // Stop returned it to zero
}
