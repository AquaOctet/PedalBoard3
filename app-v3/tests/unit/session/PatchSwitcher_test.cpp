// Tests for pedalboard3::session::PatchSwitcher — Program Change and
// PatchSelect routing into the patch bank (roadmap M3 U4). Drives a real
// ControlHub + BankDocument + Session (fake BuildFn), so the whole path from
// a MIDI/OSC message to the live patch is exercised without a device.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/AppCommands.h"
#include "session/BankDocument.h"
#include "session/ControlHub.h"
#include "session/PatchSwitcher.h"
#include "session/PreparedGraph.h"
#include "session/Session.h"
#include "support/TestGainProcessor.h"

#include <juce_osc/juce_osc.h>

#include <chrono>
#include <memory>
#include <thread>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{

session::Session::BuildFn fakeBuilder()
{
    return [](const session::Patch& p, double sr, int bs, juce::String&) -> session::PreparedGraph
    {
        session::PreparedGraph pg;
        pg.graph = std::make_unique<juce::AudioProcessorGraph>();
        pg.graph->setPlayConfigDetails(2, 2, sr, bs);
        auto inNode = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
        auto outNode = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        for (const auto& patchNode : p.nodes)
        {
            auto procNode = pg.graph->addNode(std::make_unique<TestGainProcessor>(1.0f));
            pg.nodes[patchNode.id] = session::PreparedGraph::NodeRefs{nullptr, nullptr, procNode.get()};
            for (int ch = 0; ch < 2; ++ch)
            {
                pg.graph->addConnection({{inNode->nodeID, ch}, {procNode->nodeID, ch}});
                pg.graph->addConnection({{procNode->nodeID, ch}, {outNode->nodeID, ch}});
            }
        }
        if (p.nodes.empty())
            for (int ch = 0; ch < 2; ++ch)
                pg.graph->addConnection({{inNode->nodeID, ch}, {outNode->nodeID, ch}});
        pg.graph->prepareToPlay(sr, bs);
        return pg;
    };
}

void pumpUntilIdle(session::Session& s, int timeoutMs = 2000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        s.dispatchPendingResult();
        if (s.getState() == session::Session::State::Idle)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            s.dispatchPendingResult();
            if (s.getState() == session::Session::State::Idle)
                return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// Five one-node patches P0..P4, each with a distinct plugin identity.
session::PatchBank fiveBank()
{
    session::PatchBank b;
    b.patches.clear();
    for (int i = 0; i < 5; ++i)
    {
        session::Patch p;
        p.name = "P" + juce::String(i);
        session::PatchNode n;
        n.id = 1;
        n.desc.name = p.name + " FX";
        n.desc.pluginFormatName = "VST3";
        n.desc.fileOrIdentifier = "/fake/" + p.name + ".vst3";
        p.nodes.push_back(n);
        p.connections = session::linearChainConnections(p.nodes);
        b.patches.push_back(p);
    }
    b.activeIndex = 0;
    return b;
}

control::MidiMapping midiToPatch(control::MidiSourceKind kind, int controller, int patchIndex)
{
    control::MidiMapping m;
    m.source.kind = kind;
    m.source.channel = 0; // omni
    m.source.controller = controller;
    m.target.kind = control::MidiTargetKind::PatchSelect;
    m.target.patchIndex = patchIndex;
    return m;
}

control::OscMapping oscToPatch(const juce::String& address, int patchIndex)
{
    control::OscMapping m;
    m.address = address;
    m.target.kind = control::MidiTargetKind::PatchSelect;
    m.target.patchIndex = patchIndex;
    return m;
}

struct Fixture
{
    engine::AudioEngine eng;
    engine::PluginHost host;
    session::Session s;
    session::BankDocument doc;
    session::ControlHub hub;
    std::unique_ptr<session::PatchSwitcher> sw; // declared last: destroyed first

    Fixture() : s(eng, host, fakeBuilder()), doc(s)
    {
        eng.initialise();
        sw = std::make_unique<session::PatchSwitcher>(doc, hub);
        doc.loadBank(fiveBank());
        settle();
    }

    void settle() { pumpUntilIdle(s); }
};

} // namespace

TEST_CASE("PatchSwitcher: Program Change n selects bank index n on any channel", "[session][patch-switcher]")
{
    Fixture f;

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 3));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 3);
    REQUIRE(f.s.getActivePatch().name == "P3");

    f.sw->handleMidi(juce::MidiMessage::programChange(16, 0));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.s.getActivePatch().name == "P0");
}

TEST_CASE("PatchSwitcher: an out-of-range Program Change changes nothing", "[session][patch-switcher]")
{
    Fixture f;
    const auto gen = f.s.getPatchGeneration();

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 9));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.s.getPatchGeneration() == gen);
}

TEST_CASE("PatchSwitcher: non-Program-Change messages never switch by default", "[session][patch-switcher]")
{
    Fixture f;
    const auto gen = f.s.getPatchGeneration();

    f.sw->handleMidi(juce::MidiMessage::controllerEvent(1, 3, 127));
    f.sw->handleMidi(juce::MidiMessage::noteOn(1, 3, 0.8f));
    f.sw->handleMidi(juce::MidiMessage::pitchWheel(1, 16000));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.s.getPatchGeneration() == gen);
}

TEST_CASE("PatchSwitcher: a PatchSelect mapping on Program Change overrides the default rule",
          "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setMidiMappings({midiToPatch(control::MidiSourceKind::ProgramChange, 0, 4)});

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 1));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 4); // the mapping's target, not program 1
}

TEST_CASE("PatchSwitcher: a matching PatchSelect mapping owns the message even when its index is out of range",
          "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setMidiMappings({midiToPatch(control::MidiSourceKind::ProgramChange, 0, 9)});

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 2));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 0);
}

TEST_CASE("PatchSwitcher: a CC PatchSelect mapping switches on press; the release is harmless",
          "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setMidiMappings({midiToPatch(control::MidiSourceKind::ControlChange, 20, 2)});

    f.sw->handleMidi(juce::MidiMessage::controllerEvent(1, 20, 127));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
    const auto gen = f.s.getPatchGeneration();

    f.sw->handleMidi(juce::MidiMessage::controllerEvent(1, 20, 0));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
    REQUIRE(f.s.getPatchGeneration() == gen);
}

TEST_CASE("PatchSwitcher: an OSC PatchSelect mapping switches the bank", "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setOscMappings({oscToPatch("/pb3/patch", 3)});

    f.hub.handleOsc(juce::OSCMessage("/pb3/patch", 1.0f));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 3);
    REQUIRE(f.s.getActivePatch().name == "P3");
}

TEST_CASE("PatchSwitcher: while MIDI learn is armed, a Program Change is learned, not followed",
          "[session][patch-switcher]")
{
    Fixture f;
    bool learnedProgramChange = false;
    f.hub.armMidiLearn([&](const control::MidiSource& src)
                       { learnedProgramChange = src.kind == control::MidiSourceKind::ProgramChange; });

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 2));
    f.settle();
    REQUIRE(learnedProgramChange);
    REQUIRE(f.doc.activeIndex() == 0);

    // Learn disarmed itself on capture; the next Program Change switches.
    f.sw->handleMidi(juce::MidiMessage::programChange(1, 2));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
}

TEST_CASE("PatchSwitcher: a rapid Program Change sweep lands on the last program with every slot intact",
          "[session][patch-switcher]")
{
    Fixture f;

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 1));
    f.sw->handleMidi(juce::MidiMessage::programChange(1, 2));
    f.sw->handleMidi(juce::MidiMessage::programChange(1, 3));
    f.settle();

    REQUIRE(f.doc.activeIndex() == 3);
    REQUIRE(f.s.getActivePatch().name == "P3");
    for (int i = 0; i < 5; ++i)
    {
        const auto& p = f.doc.bank().patches[static_cast<size_t>(i)];
        REQUIRE(p.name == "P" + juce::String(i));
        REQUIRE(p.nodes.size() == 1);
        REQUIRE(p.nodes[0].desc.fileOrIdentifier == "/fake/P" + juce::String(i) + ".vst3");
    }
}

TEST_CASE("PatchSwitcher: destroying the switcher uninstalls the hub sink", "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setOscMappings({oscToPatch("/pb3/patch", 3)});

    f.sw.reset();
    f.hub.handleOsc(juce::OSCMessage("/pb3/patch", 1.0f)); // must not reach a dead switcher
    f.settle();
    REQUIRE(f.doc.activeIndex() == 0);
}

// ---- App commands (roadmap M4 U1) ------------------------------------------

namespace
{

control::MidiMapping midiToCommand(control::MidiSourceKind kind, int controller, const char* commandId)
{
    control::MidiMapping m;
    m.source.kind = kind;
    m.source.channel = 0; // omni
    m.source.controller = controller;
    m.target.kind = control::MidiTargetKind::AppCommand;
    m.target.commandId = commandId;
    return m;
}

} // namespace

TEST_CASE("PatchSwitcher: a CC mapped to patch.next steps once per press, not on release", "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setMidiMappings(
        {midiToCommand(control::MidiSourceKind::ControlChange, 64, session::appcommands::kNextPatch)});

    f.sw->handleMidi(juce::MidiMessage::controllerEvent(1, 64, 127));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 1);

    f.sw->handleMidi(juce::MidiMessage::controllerEvent(1, 64, 0));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 1);

    f.sw->handleMidi(juce::MidiMessage::controllerEvent(1, 64, 127));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
}

TEST_CASE("PatchSwitcher: a note mapped to patch.previous wraps and ignores the note-off", "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setMidiMappings({midiToCommand(control::MidiSourceKind::Note, 36, session::appcommands::kPreviousPatch)});

    f.sw->handleMidi(juce::MidiMessage::noteOn(1, 36, 0.9f));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 4); // wrapped from 0

    f.sw->handleMidi(juce::MidiMessage::noteOff(1, 36));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 4);
}

TEST_CASE("PatchSwitcher: a Program Change mapped to an app command owns the message, PC 0 included",
          "[session][patch-switcher]")
{
    Fixture f;
    f.hub.setMidiMappings({midiToCommand(control::MidiSourceKind::ProgramChange, 0, session::appcommands::kNextPatch)});

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 3));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 1); // next, not program 3

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 0));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
}

TEST_CASE("PatchSwitcher: an OSC patch.next mapping steps on 1.0 and ignores 0.0", "[session][patch-switcher]")
{
    Fixture f;
    control::OscMapping m;
    m.address = "/pb3/next";
    m.target.kind = control::MidiTargetKind::AppCommand;
    m.target.commandId = session::appcommands::kNextPatch;
    f.hub.setOscMappings({m});

    f.hub.handleOsc(juce::OSCMessage("/pb3/next", 1.0f));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 1);

    f.hub.handleOsc(juce::OSCMessage("/pb3/next", 0.0f));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 1);
}

TEST_CASE("PatchSwitcher: an unknown app command does nothing and still owns its Program Change",
          "[session][patch-switcher]")
{
    Fixture f;
    const auto gen = f.s.getPatchGeneration();
    f.hub.setMidiMappings({midiToCommand(control::MidiSourceKind::ProgramChange, 0, "patch.nope")});

    f.sw->handleMidi(juce::MidiMessage::programChange(1, 2));
    f.settle();
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.s.getPatchGeneration() == gen);
}

#include "engine/Transport.h"
#include "session/TransportCommands.h"

TEST_CASE("PatchSwitcher: MMC reaches the transport first, only while MMC is switched on",
          "[session][patch-switcher][mmc]")
{
    Fixture f;
    session::TransportCommands commands(f.s, f.eng.getTransport());
    f.sw->setTransportCommands(&commands);
    const auto play = juce::MidiMessage::midiMachineControlCommand(juce::MidiMessage::mmc_play);

    f.sw->handleMidi(play);
    REQUIRE_FALSE(f.eng.getTransport().isPlaying());

    commands.setMmcEnabled(true);
    f.sw->handleMidi(play);
    REQUIRE(f.eng.getTransport().isPlaying());
    f.sw->handleMidi(juce::MidiMessage::midiMachineControlCommand(juce::MidiMessage::mmc_stop));
    REQUIRE_FALSE(f.eng.getTransport().isPlaying());
    REQUIRE(f.doc.activeIndex() == 0); // MMC never switches patches

    f.sw->setTransportCommands(nullptr); // before `commands` goes
}
