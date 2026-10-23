// Tests for session::ControlHub — the message-thread seam that unifies the MIDI
// control surface and OSC router into one ControlParameterApplier. These drive
// the WHOLE control chain end to end through a real juce::AudioProcessorGraph
// containing a TestParamProcessor: a juce::MidiMessage / juce::OSCMessage in
// moves a real graph node's parameter out. Headless — no device, no socket.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "session/ControlHub.h"
#include "support/TestParamProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_osc/juce_osc.h>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace pedalboard3;
using Catch::Approx;

namespace
{

using AudioGraph = juce::AudioProcessorGraph;

// A real prepared graph (stereo in -> param node -> out) holding one or two
// TestParamProcessor nodes, plus a ControlHub whose resolver maps NodeId 1 ->
// procA and 2 -> procB. Non-movable (ControlHub captures `this`), so use as a
// local. NodeId 1/2 mirror the production NodeId -> graphNode->getProcessor()
// shape, typed to the juce::AudioProcessor* base.
struct Harness
{
    std::unique_ptr<AudioGraph> graph = std::make_unique<AudioGraph>();
    juce::AudioProcessor* procA = nullptr;
    juce::AudioProcessor* procB = nullptr;
    session::ControlHub hub;

    explicit Harness(bool twoNodes = false)
    {
        graph->setPlayConfigDetails(2, 2, 48000.0, 256);
        auto in = graph->addNode(
            std::make_unique<AudioGraph::AudioGraphIOProcessor>(AudioGraph::AudioGraphIOProcessor::audioInputNode));
        auto out = graph->addNode(
            std::make_unique<AudioGraph::AudioGraphIOProcessor>(AudioGraph::AudioGraphIOProcessor::audioOutputNode));
        auto a = graph->addNode(std::make_unique<testing::TestParamProcessor>());
        procA = a->getProcessor();

        graph->addConnection({{in->nodeID, 0}, {a->nodeID, 0}});
        graph->addConnection({{in->nodeID, 1}, {a->nodeID, 1}});
        graph->addConnection({{a->nodeID, 0}, {out->nodeID, 0}});
        graph->addConnection({{a->nodeID, 1}, {out->nodeID, 1}});

        if (twoNodes)
        {
            auto b = graph->addNode(std::make_unique<testing::TestParamProcessor>());
            procB = b->getProcessor();
        }

        graph->prepareToPlay(48000.0, 256);

        hub.setNodeResolver(
            [this](session::NodeId id) -> juce::AudioProcessor*
            {
                if (id == 1)
                    return procA;
                if (id == 2)
                    return procB;
                return nullptr;
            });
    }

    float gainA() const { return procA->getParameters()[0]->getValue(); }
    float gainB() const { return procB->getParameters()[0]->getValue(); }

    // Forwarders so tests read `h.setMidiMappings(...)` alongside `h.hub.handleMidi(...)`.
    void setMidiMappings(std::vector<control::MidiMapping> m) { hub.setMidiMappings(std::move(m)); }
    void setOscMappings(std::vector<control::OscMapping> m) { hub.setOscMappings(std::move(m)); }
    void resetLatches() noexcept { hub.resetLatches(); }
};

control::MidiMapping ccMap(int channel, int cc, session::NodeId node, int paramIndex)
{
    control::MidiMapping m;
    m.source.kind = control::MidiSourceKind::ControlChange;
    m.source.channel = channel;
    m.source.controller = cc;
    m.target.node = node;
    m.target.parameterIndex = paramIndex;
    return m;
}

control::OscMapping oscMap(const juce::String& address, session::NodeId node, int paramIndex)
{
    control::OscMapping m;
    m.address = address;
    m.argumentIndex = 0;
    m.target.node = node;
    m.target.parameterIndex = paramIndex;
    return m;
}

juce::MidiMessage cc(int channel, int controller, int value)
{
    return juce::MidiMessage::controllerEvent(channel, controller, value);
}

} // namespace

TEST_CASE("ControlHub: MIDI CC moves a real graph-node parameter end to end", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});

    h.hub.handleMidi(cc(1, 7, 64));
    REQUIRE(h.gainA() == Approx(64.0f / 127.0f));
    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleMidi(cc(1, 7, 0));
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: OSC moves a real graph-node parameter end to end", "[session][control][hub]")
{
    Harness h;
    h.setOscMappings({oscMap("/pb3/gain", 1, 0)});

    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.25f));
    REQUIRE(h.gainA() == Approx(0.25f));
    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 1)); // int promoted to 1.0
    REQUIRE(h.gainA() == Approx(1.0f));
}

TEST_CASE("ControlHub: applied value changes audio through the prepared graph", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});

    auto render = [&]
    {
        juce::AudioBuffer<float> buf(2, 256);
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 256; ++n)
                buf.setSample(ch, n, 1.0f);
        juce::MidiBuffer midi;
        h.graph->processBlock(buf, midi);
        return buf.getSample(0, 128);
    };

    h.hub.handleMidi(cc(1, 7, 0));
    REQUIRE(render() == Approx(0.0f));
    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(render() == Approx(1.0f));
}

TEST_CASE("ControlHub: MIDI and OSC to the same target share the one parameter", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});
    h.setOscMappings({oscMap("/pb3/gain", 1, 0)});

    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.3f));
    REQUIRE(h.gainA() == Approx(0.3f)); // last writer wins
    h.hub.handleMidi(cc(1, 7, 0));
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: rebinding the resolver routes input to the new node", "[session][control][hub]")
{
    Harness a;
    Harness b;
    a.setMidiMappings({ccMap(1, 7, 1, 0)});

    a.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(a.gainA() == Approx(1.0f));
    REQUIRE(b.gainA() == Approx(1.0f)); // untouched (default)

    // Repoint a's hub at b's processor.
    a.hub.setNodeResolver([&](session::NodeId id) -> juce::AudioProcessor* { return id == 1 ? b.procA : nullptr; });
    a.hub.handleMidi(cc(1, 7, 0));
    REQUIRE(b.gainA() == Approx(0.0f)); // new node moved
    REQUIRE(a.gainA() == Approx(1.0f)); // old node frozen
}

TEST_CASE("ControlHub: continuous CC range and invert reach the real parameter", "[session][control][hub]")
{
    Harness h;
    auto m = ccMap(1, 7, 1, 0);
    m.rangeMin = 0.2f;
    m.rangeMax = 0.8f;
    h.setMidiMappings({m});

    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(h.gainA() == Approx(0.8f));
    h.hub.handleMidi(cc(1, 7, 0));
    REQUIRE(h.gainA() == Approx(0.2f));
    h.hub.handleMidi(cc(1, 7, 64));
    REQUIRE(h.gainA() == Approx(0.2f + (64.0f / 127.0f) * 0.6f));

    m.inverted = true;
    h.setMidiMappings({m});
    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(h.gainA() == Approx(0.2f));
}

TEST_CASE("ControlHub: latch CC toggles on press edges only", "[session][control][hub]")
{
    Harness h;
    auto m = ccMap(1, 64, 1, 0);
    m.latch = true;
    h.setMidiMappings({m});

    h.hub.handleMidi(cc(1, 64, 127)); // press -> ON
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleMidi(cc(1, 64, 127)); // held -> no re-toggle
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleMidi(cc(1, 64, 0)); // release -> arm, no output
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleMidi(cc(1, 64, 127)); // press -> OFF
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: one CC fans out to two parameters", "[session][control][hub]")
{
    Harness h(true);
    h.setMidiMappings({ccMap(1, 7, 1, 0), ccMap(1, 7, 2, 0)});

    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(h.gainA() == Approx(1.0f));
    REQUIRE(h.gainB() == Approx(1.0f));
}

TEST_CASE("ControlHub: MIDI learn captures, suppresses routing, auto-disarms", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});

    control::MidiSource learned;
    int learnCount = 0;
    h.hub.armMidiLearn(
        [&](const control::MidiSource& src)
        {
            learned = src;
            ++learnCount;
        });
    REQUIRE(h.hub.isLearning());

    h.hub.handleMidi(cc(1, 7, 100)); // captured, NOT routed
    REQUIRE(learnCount == 1);
    REQUIRE_FALSE(h.hub.isLearning());
    REQUIRE(h.gainA() == Approx(1.0f)); // unchanged (default)
    REQUIRE(learned.kind == control::MidiSourceKind::ControlChange);
    REQUIRE(learned.controller == 7);

    h.hub.handleMidi(cc(1, 7, 0)); // routing resumes
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: cancelMidiLearn leaves no stale capture and resumes routing", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});

    int learnCount = 0;
    h.hub.armMidiLearn([&](const control::MidiSource&) { ++learnCount; });
    h.hub.cancelMidiLearn();
    REQUIRE_FALSE(h.hub.isLearning());

    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(learnCount == 0);
    REQUIRE(h.gainA() == Approx(1.0f));
}

TEST_CASE("ControlHub: OSC keeps routing while MIDI learn is armed", "[session][control][hub]")
{
    Harness h;
    h.setOscMappings({oscMap("/pb3/gain", 1, 0)});
    h.hub.armMidiLearn([&](const control::MidiSource&) {});

    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.4f));
    REQUIRE(h.gainA() == Approx(0.4f)); // OSC unaffected by MIDI learn
    REQUIRE(h.hub.isLearning());        // still armed (no MIDI seen)
}

TEST_CASE("ControlHub: channel-filtered and unmapped MIDI leave the parameter alone", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});

    h.hub.handleMidi(cc(2, 7, 0)); // wrong channel
    h.hub.handleMidi(cc(1, 9, 0)); // wrong controller
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleMidi(cc(1, 7, 0));
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: non-matching / argument-short OSC leaves the parameter alone", "[session][control][hub]")
{
    Harness h;
    auto m = oscMap("/pb3/xy", 1, 0);
    m.argumentIndex = 2; // needs a third argument
    h.setOscMappings({oscMap("/pb3/gain", 1, 0), m});

    h.hub.handleOsc(juce::OSCMessage("/pb3/other", 0.0f)); // no address match
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleOsc(juce::OSCMessage("/pb3/xy", 0.1f)); // matches m but arg 2 missing
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.0f));
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: input before a resolver is installed is dropped, no crash", "[session][control][hub]")
{
    session::ControlHub hub; // no resolver, no mappings
    hub.setMidiMappings({ccMap(1, 7, 1, 0)});
    hub.setOscMappings({oscMap("/pb3/gain", 1, 0)});
    REQUIRE_NOTHROW(hub.handleMidi(cc(1, 7, 127)));
    REQUIRE_NOTHROW(hub.handleOsc(juce::OSCMessage("/pb3/gain", 1.0f)));
}

TEST_CASE("ControlHub: unknown node and out-of-range index are safe no-ops", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 99, 0), ccMap(1, 8, 1, 5), ccMap(1, 9, 1, 0)});

    REQUIRE_NOTHROW(h.hub.handleMidi(cc(1, 7, 0))); // unknown node 99
    REQUIRE_NOTHROW(h.hub.handleMidi(cc(1, 8, 0))); // param index 5 out of range
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleMidi(cc(1, 9, 0)); // valid
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: applied value is clamped to 0..1", "[session][control][hub]")
{
    Harness h;
    auto m = ccMap(1, 7, 1, 0);
    m.rangeMin = -1.0f;
    m.rangeMax = 2.0f;
    h.setMidiMappings({m});

    h.hub.handleMidi(cc(1, 7, 127)); // pre-clamp 2.0
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleMidi(cc(1, 7, 0)); // pre-clamp -1.0
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: a NaN OSC argument is dropped, preserving the live value", "[session][control][hub]")
{
    Harness h;
    h.setOscMappings({oscMap("/pb3/gain", 1, 0)});

    // Establish a non-default baseline so a stray write would be observable.
    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.5f));
    REQUIRE(h.gainA() == Approx(0.5f));

    // NaN survives scaleToRange (jlimit passes NaN through) but the hub's
    // finiteness guard drops it before setValueNotifyingHost.
    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(std::isnan(h.gainA()));
    REQUIRE(h.gainA() == Approx(0.5f)); // preserved, not overwritten

    // A real value still lands afterwards.
    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.25f));
    REQUIRE(h.gainA() == Approx(0.25f));
}

TEST_CASE("ControlHub: non-parameter targets go to the unhandled sink, not the graph", "[session][control][hub]")
{
    Harness h;
    auto patchMap = ccMap(1, 7, 1, 0);
    patchMap.target.kind = control::MidiTargetKind::PatchSelect;
    patchMap.target.patchIndex = 3;
    h.setMidiMappings({patchMap});

    int unhandledCount = 0;
    control::MidiTarget seen;
    h.hub.setUnhandledTargetSink(
        [&](const control::ParameterUpdate& u)
        {
            seen = u.target;
            ++unhandledCount;
        });

    h.hub.handleMidi(cc(1, 7, 127));
    REQUIRE(unhandledCount == 1);
    REQUIRE(seen.kind == control::MidiTargetKind::PatchSelect);
    REQUIRE(seen.patchIndex == 3);
    REQUIRE(h.gainA() == Approx(1.0f)); // plugin parameter untouched
}

TEST_CASE("ControlHub: setMidiMappings and resetLatches clear latch state", "[session][control][hub]")
{
    Harness h;
    auto m = ccMap(1, 64, 1, 0);
    m.latch = true;
    h.setMidiMappings({m});

    h.hub.handleMidi(cc(1, 64, 127)); // ON
    REQUIRE(h.gainA() == Approx(1.0f));

    SECTION("setMidiMappings resets latch")
    {
        h.setMidiMappings({m});
        h.hub.handleMidi(cc(1, 64, 127)); // ON again, not OFF
        REQUIRE(h.gainA() == Approx(1.0f));
    }
    SECTION("resetLatches keeps mappings + resolver")
    {
        h.resetLatches();
        h.hub.handleMidi(cc(1, 64, 127)); // ON again
        REQUIRE(h.gainA() == Approx(1.0f));
    }
}

TEST_CASE("ControlHub: malformed OSC mapping address is skipped, sibling applies", "[session][control][hub]")
{
    Harness h;
    h.setOscMappings({oscMap("bad address", 1, 0), oscMap("/pb3/gain", 1, 0)});

    REQUIRE_NOTHROW(h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.0f)));
    REQUIRE(h.gainA() == Approx(0.0f));
}

TEST_CASE("ControlHub: OSC value out of 0..1 is clamped", "[session][control][hub]")
{
    Harness h;
    h.setOscMappings({oscMap("/pb3/gain", 1, 0)});

    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 5.0f));
    REQUIRE(h.gainA() == Approx(1.0f));
}

TEST_CASE("ControlHub: empty mapping tables are safe", "[session][control][hub]")
{
    Harness h;
    REQUIRE_NOTHROW(h.hub.handleMidi(cc(1, 7, 127)));
    REQUIRE_NOTHROW(h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 1.0f)));
    REQUIRE(h.gainA() == Approx(1.0f));
}

TEST_CASE("ControlHub: a single dedicated consumer thread drives the parameter", "[session][control][hub]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});

    int last = 0;
    std::thread consumer(
        [&]
        {
            for (int v = 0; v <= 127; ++v)
            {
                h.hub.handleMidi(cc(1, 7, v));
                last = v;
            }
        });
    consumer.join();

    REQUIRE(h.gainA() == Approx(static_cast<float>(last) / 127.0f));
}

TEST_CASE("ControlHub: the unhandled sink sees each update's press flag", "[session][control][hub]")
{
    Harness h;
    auto m = ccMap(1, 20, 1, 0);
    m.target.kind = control::MidiTargetKind::AppCommand;
    m.target.commandId = "patch.next";
    h.setMidiMappings({m});

    std::vector<bool> presses;
    h.hub.setUnhandledTargetSink([&](const control::ParameterUpdate& u) { presses.push_back(u.press); });

    h.hub.handleMidi(cc(1, 20, 127));
    h.hub.handleMidi(cc(1, 20, 0));
    REQUIRE(presses == std::vector<bool>{true, false});
}

// ---- Activity counters (UI refresh U3) ----------------------------------------

TEST_CASE("ControlHub: MIDI and OSC activity counts every message, mapped or not", "[session][control][activity]")
{
    Harness h;
    REQUIRE(h.hub.midiActivity() == 0);
    REQUIRE(h.hub.oscActivity() == 0);

    h.hub.handleMidi(juce::MidiMessage::controllerEvent(1, 20, 64)); // nothing mapped
    h.hub.handleMidi(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)));
    REQUIRE(h.hub.midiActivity() == 2);

    h.hub.handleOsc(juce::OSCMessage("/nothing/mapped", 0.5f));
    REQUIRE(h.hub.oscActivity() == 1);
    REQUIRE(h.hub.midiActivity() == 2);
}

TEST_CASE("ControlHub: node activity counts parameters actually set, and clears on a re-feed",
          "[session][control][activity]")
{
    Harness h;
    control::MidiMapping m;
    m.source.kind = control::MidiSourceKind::ControlChange;
    m.source.channel = 0; // omni
    m.source.controller = 7;
    m.target.kind = control::MidiTargetKind::PluginParameter;
    m.target.node = 1;
    m.target.parameterIndex = 0;
    m.rangeMin = 0.0f;
    m.rangeMax = 1.0f;
    h.hub.setMidiMappings({m});

    h.hub.handleMidi(juce::MidiMessage::controllerEvent(1, 7, 100));
    REQUIRE(h.hub.nodeActivity(1) == 1);

    h.hub.handleMidi(juce::MidiMessage::controllerEvent(1, 8, 100)); // an unmapped CC moves nothing
    REQUIRE(h.hub.nodeActivity(1) == 1);
    REQUIRE(h.hub.nodeActivity(2) == 0);

    h.hub.setMidiMappings({m}); // a re-feed (e.g. after a whole-patch swap)
    REQUIRE(h.hub.nodeActivity(1) == 0);
}

// -----------------------------------------------------------------------
// MIDI over OSC (#97, AC-IO-6)
// -----------------------------------------------------------------------

TEST_CASE("ControlHub: an OSC message shaped like MIDI drives a MIDI mapping", "[session][control][midi-over-osc]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});

    // 0xB0 = CC on channel 1, controller 7. No OSC mapping exists for this
    // address: the MIDI table is doing the work.
    h.hub.handleOsc(juce::OSCMessage("/midi", 176, 7, 127));
    REQUIRE(h.gainA() == Approx(1.0f));
    h.hub.handleOsc(juce::OSCMessage("/midi", 176, 7, 0));
    REQUIRE(h.gainA() == Approx(0.0f));

    // The blob form reaches the same mapping.
    juce::OSCMessage blob("/midi");
    const juce::uint8 bytes[3] = {176, 7, 127};
    blob.addBlob(juce::MemoryBlock(bytes, 3));
    h.hub.handleOsc(blob);
    REQUIRE(h.gainA() == Approx(1.0f));
}

TEST_CASE("ControlHub: an ordinary OSC message never reaches the MIDI path", "[session][control][midi-over-osc]")
{
    Harness h;
    h.setMidiMappings({ccMap(1, 7, 1, 0)});
    h.setOscMappings({oscMap("/pb3/gain", 1, 0)});

    h.hub.handleOsc(juce::OSCMessage("/pb3/gain", 0.5f)); // one argument
    REQUIRE(h.gainA() == Approx(0.5f));
    REQUIRE(h.hub.midiActivity() == 0);

    // Two normalized floats off an XY pad are numbers, not MIDI bytes.
    h.hub.handleOsc(juce::OSCMessage("/pb3/xy", 0.3f, 0.7f));
    REQUIRE(h.hub.midiActivity() == 0);
    REQUIRE(h.gainA() == Approx(0.5f)); // nothing moved
}

TEST_CASE("ControlHub: a MIDI-shaped message still runs its OSC mapping", "[session][control][midi-over-osc]")
{
    // The two paths are additive. An OSC mapping on an address whose arguments
    // happen to read as MIDI keeps working — which is the reason handleOsc
    // does both rather than stopping at the first one that matches.
    Harness h(/*twoNodes*/ true);

    // The OSC mapping reads argument 2 — the MIDI value byte — and inverts it,
    // so the two paths move their nodes in opposite directions off the one
    // message. Either path going missing is then visible on its own node.
    auto oscToA = oscMap("/both", 1, 0);
    oscToA.argumentIndex = 2;
    oscToA.inverted = true;
    h.setOscMappings({oscToA});
    h.setMidiMappings({ccMap(1, 7, 2, 0)});

    h.hub.handleOsc(juce::OSCMessage("/both", 176, 7, 127));
    REQUIRE(h.gainA() == Approx(0.0f)); // OSC: argument 2 clamped to 1, inverted
    REQUIRE(h.gainB() == Approx(1.0f)); // MIDI: CC 7 value 127

    h.hub.handleOsc(juce::OSCMessage("/both", 176, 7, 0));
    REQUIRE(h.gainA() == Approx(1.0f));
    REQUIRE(h.gainB() == Approx(0.0f));

    REQUIRE(h.hub.midiActivity() == 2);
    REQUIRE(h.hub.oscActivity() == 2);
}

TEST_CASE("ControlHub: a PatchSelect mapping fires over OSC-carried MIDI", "[session][control][midi-over-osc]")
{
    Harness h;
    auto patchMap = ccMap(1, 7, 1, 0);
    patchMap.target.kind = control::MidiTargetKind::PatchSelect;
    patchMap.target.patchIndex = 3;
    h.setMidiMappings({patchMap});

    int unhandledCount = 0;
    control::MidiTarget seen;
    h.hub.setUnhandledTargetSink(
        [&](const control::ParameterUpdate& u)
        {
            seen = u.target;
            ++unhandledCount;
        });

    h.hub.handleOsc(juce::OSCMessage("/midi", 176, 7, 127));
    REQUIRE(unhandledCount == 1);
    REQUIRE(seen.kind == control::MidiTargetKind::PatchSelect);
    REQUIRE(seen.patchIndex == 3);
}

TEST_CASE("ControlHub: MIDI learn captures an OSC-carried MIDI message", "[session][control][midi-over-osc]")
{
    // The MIDI path means the whole MIDI path, learn included: a controller
    // that reaches the app over a socket can be learned like any other.
    Harness h;
    std::optional<control::MidiSource> learned;
    h.hub.armMidiLearn([&](const control::MidiSource& s) { learned = s; });

    h.hub.handleOsc(juce::OSCMessage("/midi", 176, 20, 127));
    REQUIRE(learned.has_value());
    REQUIRE(learned->kind == control::MidiSourceKind::ControlChange);
    REQUIRE(learned->channel == 1);
    REQUIRE(learned->controller == 20);
    REQUIRE_FALSE(h.hub.isLearning());
}
