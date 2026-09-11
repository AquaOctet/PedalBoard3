// Tests for pedalboard3::session::ControlWiring — the listener that feeds
// session::ControlHub from Session's active patch (roadmap M2, plan
// docs/plans/2026-07-04-001-feat-live-midi-osc-control-plan.md, U1).
//
// Uses the BuildFn-injection Session ctor with a local FakeBuilder that
// wires TestParamProcessor (a real AudioParameterFloat, unlike
// Session_test.cpp's TestGainProcessor) so control updates are observable
// via getParameters()[0]->getValue(), mirroring ControlHub_test.cpp.
//
// Catch2 + JUCE timing: Session's own change notifications go through
// juce::ChangeBroadcaster::sendChangeMessage() (async — requires a running
// MessageManager). These headless tests drive delivery deterministically
// via sess.sendSynchronousChangeMessage(), the same pattern
// PreferencesPersister_test.cpp uses.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/ControlHub.h"
#include "session/ControlWiring.h"
#include "session/Patch.h"
#include "session/PreparedGraph.h"
#include "session/Session.h"
#include "session/v2_import/PdlV2Importer.h"
#include "support/TestParamProcessor.h"

#include <juce_osc/juce_osc.h>

#include <chrono>
#include <thread>

using namespace pedalboard3;
using Catch::Approx;

namespace
{

// Builds a real PreparedGraph with one TestParamProcessor per Patch node, so
// ControlWiring's resolver (session.getHostedPlugin) resolves to a processor
// with a genuine, observable parameter — unlike Session_test.cpp's
// FakeBuilder, which uses TestGainProcessor (no juce parameters at all).
session::Session::BuildFn testParamBuilder()
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
            auto procNode = pg.graph->addNode(std::make_unique<testing::TestParamProcessor>());
            // `hosted` (not just `graphNode`) must be set: Session::getHostedPlugin
            // reads it directly, and that's the resolver ControlWiring binds.
            pg.nodes[patchNode.id] =
                session::PreparedGraph::NodeRefs{nullptr, procNode->getProcessor(), procNode.get()};
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

juce::PluginDescription makeDesc(const juce::String& name)
{
    juce::PluginDescription d;
    d.name = name;
    d.pluginFormatName = "VST3";
    d.fileOrIdentifier = "/fake/" + name + ".vst3";
    return d;
}

// Same polling idiom as Session_test.cpp's pumpUntilIdle: pb3_juce_config
// keeps JUCE_MODAL_LOOPS_PERMITTED off, so tests drive the worker's
// AsyncUpdater synchronously instead of running a real dispatch loop.
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

// A one-node patch with no mappings yet (node id 1 -- the target every
// ccMapping() in this file addresses).
session::Patch makeOneNodePatch()
{
    session::Patch p;
    session::PatchNode n;
    n.id = 1;
    n.desc = makeDesc("Param");
    p.nodes.push_back(n);
    return p;
}

// A one-node patch whose node carries `mapping` (typically targeting node id
// 1, parameterIndex 0 — the TestParamProcessor's sole "gain" parameter).
session::Patch makeOneNodePatchWithMapping(control::MidiMapping mapping)
{
    session::Patch p = makeOneNodePatch();
    p.midiMappings.push_back(std::move(mapping));
    return p;
}

control::MidiMapping ccMapping(int cc, bool latch = false)
{
    control::MidiMapping m;
    m.source.kind = control::MidiSourceKind::ControlChange;
    m.source.channel = 0; // omni
    m.source.controller = cc;
    m.target.kind = control::MidiTargetKind::PluginParameter;
    m.target.node = 1;
    m.target.parameterIndex = 0;
    m.rangeMin = 0.0f;
    m.rangeMax = 1.0f;
    m.latch = latch;
    return m;
}

// CC value 127 ("press") / 0 ("release") on the mapped controller/channel.
// A latch mapping only toggles on the rising edge of a press (per
// MidiRouter::process's readPress: ControlChange >= 64 is Down, < 64 is Up),
// so exercising a latch through two presses requires a release in between —
// otherwise the second press reads as "button already held" and is a no-op
// by design, mirroring a real footswitch's press/release cycle.
juce::MidiMessage ccOn(int cc)
{
    return juce::MidiMessage::controllerEvent(1, cc, 127);
}

juce::MidiMessage ccOff(int cc)
{
    return juce::MidiMessage::controllerEvent(1, cc, 0);
}

struct Fixture
{
    engine::AudioEngine eng;
    engine::PluginHost host;
    session::Session sess{eng, host, testParamBuilder()};
    session::ControlHub hub;

    Fixture() { eng.initialise(); }

    float paramValue(session::NodeId id = 1) const
    {
        auto* proc = sess.getHostedPlugin(id);
        REQUIRE(proc != nullptr);
        return proc->getParameters()[0]->getValue();
    }
};

} // namespace

TEST_CASE("ControlWiring: construction feeds mappings already active on the Session", "[session][control-wiring]")
{
    // Covers R9: a patch restored/loaded BEFORE ControlWiring exists (e.g.
    // App::restoreActivePatchIfAny running synchronously during App::initialise)
    // must still be live once ControlWiring is constructed — it feeds once at
    // construction rather than waiting for the next change message.
    Fixture f;
    f.sess.replacePatch(makeOneNodePatchWithMapping(ccMapping(74)));
    pumpUntilIdle(f.sess);
    REQUIRE(f.sess.getNodeCount() == 1);

    session::ControlWiring wiring(f.sess, f.hub);

    f.hub.handleMidi(ccOn(74));
    REQUIRE(f.paramValue() == Approx(1.0f));
}

TEST_CASE("ControlWiring: a whole-patch swap re-feeds mappings and resets latch state (R9)",
          "[session][control-wiring]")
{
    Fixture f;
    session::ControlWiring wiring(f.sess, f.hub);

    f.sess.replacePatch(makeOneNodePatchWithMapping(ccMapping(1, /*latch*/ true)));
    pumpUntilIdle(f.sess);
    f.sess.sendSynchronousChangeMessage();

    // Toggle the latch ON.
    f.hub.handleMidi(ccOn(1));
    REQUIRE(f.paramValue() == Approx(1.0f));

    // Release before the swap — otherwise the pre-swap "button held" state
    // would itself suppress the post-swap press regardless of latch reset.
    f.hub.handleMidi(ccOff(1));

    // Whole-patch swap to a fresh patch carrying the SAME mapping shape.
    f.sess.replacePatch(makeOneNodePatchWithMapping(ccMapping(1, /*latch*/ true)));
    pumpUntilIdle(f.sess);
    f.sess.sendSynchronousChangeMessage();

    // If latch state carried over, this press would be the second toggle
    // (on -> off, rangeMin). A reset patch reads this as a fresh rising
    // edge (off -> on, rangeMax) instead.
    f.hub.handleMidi(ccOn(1));
    REQUIRE(f.paramValue() == Approx(1.0f));
}

TEST_CASE("ControlWiring: an unrelated Session change does not re-feed or reset latch state",
          "[session][control-wiring]")
{
    Fixture f;
    session::ControlWiring wiring(f.sess, f.hub);

    f.sess.replacePatch(makeOneNodePatchWithMapping(ccMapping(1, /*latch*/ true)));
    pumpUntilIdle(f.sess);
    f.sess.sendSynchronousChangeMessage();

    // Toggle the latch ON, then release (arms the next press to toggle again).
    f.hub.handleMidi(ccOn(1));
    REQUIRE(f.paramValue() == Approx(1.0f));
    f.hub.handleMidi(ccOff(1));

    // A mutation that bumps stateVersion_ but neither getPatchGeneration()
    // nor getMappingsRevision() — the node-position/bypass/rename family.
    f.sess.setNodeCustomName(1, "renamed");
    f.sess.sendSynchronousChangeMessage();

    // Latch preserved: the next press is the second toggle (on -> off). If
    // the unrelated change had incorrectly re-fed/reset the hub, this would
    // instead read as a fresh rising edge (on, not off).
    f.hub.handleMidi(ccOn(1));
    REQUIRE(f.paramValue() == Approx(0.0f));
}

TEST_CASE("ControlWiring: resolver stays correct across a whole-patch swap", "[session][control-wiring]")
{
    // session.getHostedPlugin(id) resolves per call, not a cached pointer, so
    // the resolver bound once at construction is still correct after the old
    // graph (and its TestParamProcessor instance) is torn down.
    Fixture f;
    session::ControlWiring wiring(f.sess, f.hub);

    f.sess.replacePatch(makeOneNodePatchWithMapping(ccMapping(20)));
    pumpUntilIdle(f.sess);
    f.sess.sendSynchronousChangeMessage();
    auto* firstProc = f.sess.getHostedPlugin(1);

    f.sess.replacePatch(makeOneNodePatchWithMapping(ccMapping(20)));
    pumpUntilIdle(f.sess);
    f.sess.sendSynchronousChangeMessage();
    auto* secondProc = f.sess.getHostedPlugin(1);

    REQUIRE(firstProc != secondProc); // the old instance was torn down, not reused

    f.hub.handleMidi(ccOn(20));
    REQUIRE(f.paramValue() == Approx(1.0f)); // resolves + drives the NEW instance
}

TEST_CASE("ControlWiring: Session::addMidiMapping (MIDI learn's persist step) is live immediately",
          "[session][control-wiring]")
{
    // Covers R8's integration scenario: learn appends via Session::addMidiMapping
    // rather than a whole-patch swap; ControlWiring must re-feed on that too
    // (it bumps mappingsRevision_, same as a swap) so the mapping controls the
    // parameter with no re-arm and no patch reload.
    Fixture f;
    f.sess.replacePatch(makeOneNodePatch());
    pumpUntilIdle(f.sess);

    session::ControlWiring wiring(f.sess, f.hub);

    f.sess.addMidiMapping(ccMapping(74));
    f.sess.sendSynchronousChangeMessage();

    f.hub.handleMidi(ccOn(74));
    REQUIRE(f.paramValue() == Approx(1.0f));
}

TEST_CASE("ControlWiring: a v3 patch with MIDI+OSC mappings drives parameters with no manual step (R9)",
          "[session][control-wiring]")
{
    // ControlWiring is constructed first (mirroring App::initialise's order),
    // THEN the patch loads -- the plan's actual "no manual mapping step"
    // scenario: replacePatch's change message alone re-feeds the hub.
    Fixture f;
    session::ControlWiring wiring(f.sess, f.hub);

    session::Patch p;
    session::PatchNode midiNode;
    midiNode.id = 1;
    midiNode.desc = makeDesc("MidiTarget");
    session::PatchNode oscNode;
    oscNode.id = 2;
    oscNode.desc = makeDesc("OscTarget");
    p.nodes.push_back(midiNode);
    p.nodes.push_back(oscNode);
    p.midiMappings.push_back(ccMapping(74)); // targets node 1, parameter 0

    control::OscMapping osc;
    osc.address = "/pb3/gain";
    osc.argumentIndex = 0;
    osc.target.kind = control::MidiTargetKind::PluginParameter;
    osc.target.node = 2;
    osc.target.parameterIndex = 0;
    osc.rangeMin = 0.0f;
    osc.rangeMax = 1.0f;
    p.oscMappings.push_back(osc);

    f.sess.replacePatch(p);
    pumpUntilIdle(f.sess);
    f.sess.sendSynchronousChangeMessage();

    f.hub.handleMidi(ccOn(74));
    REQUIRE(f.paramValue(1) == Approx(1.0f));

    f.hub.handleOsc(juce::OSCMessage(juce::OSCAddressPattern("/pb3/gain"), 1.0f));
    REQUIRE(f.paramValue(2) == Approx(1.0f));
}

TEST_CASE("ControlWiring: a v2-imported patch's MIDI mapping reaches the hub after load (R9)",
          "[session][control-wiring]")
{
    // Minimal v2 fixture: Input(1) -> EQ(8) -> Output(3), one CC mapping on
    // EQ's parameter 0 -- mirrors PdlV2Importer_test.cpp's fixture shape. The
    // Audio Input/Output FILTERs become the graphIO sentinel (not a PatchNode),
    // so EQ is the importer's only real node.
    constexpr auto kV2Xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Pedalboard2PatchFile>
  <Patch tempo="120" name="Board">
    <FILTERGRAPH>
      <FILTER uid="1" x="0" y="0">
        <PLUGIN name="Audio Input" format="Internal" manufacturer="Raw Material Software"
                uid="246006c0" isInstrument="0" numInputs="0" numOutputs="4"/>
      </FILTER>
      <FILTER uid="3" x="900" y="0">
        <PLUGIN name="Audio Output" format="Internal" manufacturer="Raw Material Software"
                uid="724248cb" isInstrument="0" numInputs="2" numOutputs="0"/>
      </FILTER>
      <FILTER uid="8" x="200" y="0">
        <PLUGIN name="EQ" format="VST" category="EQ" manufacturer="Acme" version="2.0"
                file="C:\VST\EQ.dll" uid="33334444" isInstrument="0" numInputs="2" numOutputs="2"/>
      </FILTER>
      <CONNECTION srcFilter="1" srcChannel="0" dstFilter="8" dstChannel="0"/>
      <CONNECTION srcFilter="1" srcChannel="1" dstFilter="8" dstChannel="1"/>
      <CONNECTION srcFilter="8" srcChannel="0" dstFilter="3" dstChannel="0"/>
      <CONNECTION srcFilter="8" srcChannel="1" dstFilter="3" dstChannel="1"/>
    </FILTERGRAPH>
    <Mappings>
      <MidiMapping pluginId="8" parameter="0" cc="74" latch="0" channel="0" lowerBound="0.0" upperBound="1.0"/>
    </Mappings>
  </Patch>
</Pedalboard2PatchFile>)";

    auto xml = juce::parseXML(juce::String(kV2Xml));
    REQUIRE(xml != nullptr);
    auto imported = session::v2::importPedalboard2Patch(*xml);
    REQUIRE(imported.patch != nullptr);
    REQUIRE(imported.patch->midiMappings.size() == 1);

    Fixture f;
    session::ControlWiring wiring(f.sess, f.hub);

    f.sess.replacePatch(*imported.patch);
    pumpUntilIdle(f.sess);
    f.sess.sendSynchronousChangeMessage();

    const auto targetNode = imported.patch->midiMappings[0].target.node;
    f.hub.handleMidi(ccOn(74));
    REQUIRE(f.paramValue(targetNode) == Approx(1.0f));
}

TEST_CASE("ControlWiring: lifetime -- outliving the wiring with a Session change is safe",
          "[session][control-wiring][lifetime]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::Session sess(eng, host, testParamBuilder());
    session::ControlHub hub;

    {
        session::ControlWiring wiring(sess, hub);
    } // dtor deregisters from sess

    // After dtor, a synchronous change must not re-enter the freed wiring.
    sess.sendSynchronousChangeMessage();
    SUCCEED();
}

TEST_CASE("ControlWiring: Session::setNodeMappings (the Mappings dialog's edit) is live immediately",
          "[session][control-wiring]")
{
    Fixture f;
    f.sess.replacePatch(makeOneNodePatch());
    pumpUntilIdle(f.sess);

    session::ControlWiring wiring(f.sess, f.hub);

    auto narrowed = ccMapping(21);
    narrowed.rangeMax = 0.5f;
    REQUIRE(f.sess.setNodeMappings(1, {narrowed}, {}));
    f.sess.sendSynchronousChangeMessage();

    f.hub.handleMidi(ccOn(21));
    REQUIRE(f.paramValue() == Approx(0.5f));
}
