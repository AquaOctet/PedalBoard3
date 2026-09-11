// Tests for pedalboard3::session::BankDocument — the patch-bank document that
// switches the live patch through Session::replacePatch (roadmap M3 U3).
// Uses Session's BuildFn-injection ctor with a fake builder, so no plugin
// binaries or audio device are needed.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/BankDocument.h"
#include "session/PreparedGraph.h"
#include "session/Session.h"
#include "support/TestGainProcessor.h"

#include <chrono>
#include <thread>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{

// Minimal valid PreparedGraph for any Patch: one TestGainProcessor per node.
session::Session::BuildFn fakeBuilder()
{
    return [](const session::Patch& p, double sr, int bs, juce::String& errorOut) -> session::PreparedGraph
    {
        // A node whose identifier starts with /fail/ makes the whole build fail,
        // the way a missing plugin does in production.
        for (const auto& n : p.nodes)
            if (n.desc.fileOrIdentifier.startsWith("/fail/"))
            {
                errorOut = "FakeBuilder: unbuildable node";
                return {};
            }

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

session::Patch namedPatch(const juce::String& name, int nodeCount)
{
    session::Patch p;
    p.name = name;
    for (int i = 0; i < nodeCount; ++i)
    {
        session::PatchNode n;
        n.id = static_cast<session::NodeId>(i + 1);
        n.desc.name = name + juce::String(i);
        n.desc.pluginFormatName = "VST3";
        n.desc.fileOrIdentifier = "/fake/" + name + juce::String(i) + ".vst3";
        p.nodes.push_back(n);
    }
    p.connections = session::linearChainConnections(p.nodes);
    return p;
}

session::PatchBank threeBank()
{
    session::PatchBank b;
    b.patches = {namedPatch("A", 1), namedPatch("B", 2), namedPatch("C", 3)};
    b.activeIndex = 0;
    return b;
}

struct Fixture
{
    engine::AudioEngine eng;
    engine::PluginHost host;
    session::Session s;
    session::BankDocument doc;
    int changes{0};

    struct Counter : juce::ChangeListener
    {
        int* n;
        explicit Counter(int* c) : n(c) {}
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++*n; }
    } counter{&changes};

    Fixture() : s(eng, host, fakeBuilder()), doc(s)
    {
        eng.initialise();
        doc.addChangeListener(&counter);
    }
    ~Fixture() { doc.removeChangeListener(&counter); }

    void settle() { pumpUntilIdle(s); }
    void flushChanges() { doc.dispatchPendingMessages(); }
};

} // namespace

TEST_CASE("BankDocument: default holds one Untitled patch and issues nothing until loadBank", "[session][bank]")
{
    Fixture f;
    REQUIRE(f.doc.size() == 1);
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.doc.activePatch().name == "Untitled");
    REQUIRE(f.s.getNodeCount() == 0);
}

TEST_CASE("BankDocument: loadBank makes the stored active index live", "[session][bank]")
{
    Fixture f;
    auto bank = threeBank();
    bank.activeIndex = 1; // "B", 2 nodes
    f.doc.loadBank(bank);
    f.settle();

    REQUIRE(f.doc.activeIndex() == 1);
    REQUIRE(f.s.getNodeCount() == 2);
    REQUIRE(f.s.getActivePatch().name == "B");
    f.flushChanges();
    REQUIRE(f.changes == 1);
}

TEST_CASE("BankDocument: select captures the outgoing patch and swaps in the incoming one", "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();
    REQUIRE(f.s.getNodeCount() == 1); // "A"

    // Edit the live patch so the capture has something to store. (Custom name,
    // not bypass: the fake builder provides no BypassableInstance, so
    // setNodeBypass is a no-op under this fixture.)
    f.s.setNodeCustomName(1, "edited");

    REQUIRE(f.doc.select(2)); // -> "C"
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
    REQUIRE(f.s.getNodeCount() == 3);
    REQUIRE(f.s.getActivePatch().name == "C");

    // The outgoing patch's live edit landed in its slot.
    REQUIRE(f.doc.bank().patches[0].nodes[0].customName == "edited");
    REQUIRE(f.doc.bank().patches[0].name == "A");

    // Switching back restores it, edit included.
    REQUIRE(f.doc.select(0));
    f.settle();
    REQUIRE(f.s.getNodeCount() == 1);
    REQUIRE(f.s.getActivePatch().nodes[0].customName == "edited");
}

TEST_CASE("BankDocument: select of the active or an invalid index is a no-op", "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();
    f.flushChanges();
    const int before = f.changes;
    const auto gen = f.s.getPatchGeneration();

    REQUIRE_FALSE(f.doc.select(0));
    REQUIRE_FALSE(f.doc.select(3));
    REQUIRE_FALSE(f.doc.select(-1));
    f.settle();
    f.flushChanges();
    REQUIRE(f.changes == before);
    REQUIRE(f.s.getPatchGeneration() == gen);
}

TEST_CASE("BankDocument: next and previous wrap", "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();

    f.doc.previous(); // 0 -> 2
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
    REQUIRE(f.s.getActivePatch().name == "C");

    f.doc.next(); // 2 -> 0
    f.settle();
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.s.getActivePatch().name == "A");
}

TEST_CASE("BankDocument: rapid selects land on the last one", "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();

    f.doc.select(1);
    f.doc.select(2);
    f.settle();
    REQUIRE(f.doc.activeIndex() == 2);
    REQUIRE(f.s.getActivePatch().name == "C");
    REQUIRE(f.s.getState() == session::Session::State::Idle);
}

TEST_CASE("BankDocument: rename never touches the live patch; storeActive keeps the bank's name", "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();

    REQUIRE(f.doc.rename(0, "Lead"));
    REQUIRE(f.doc.activePatch().name == "Lead");
    REQUIRE(f.s.getActivePatch().name == "A"); // Session's copy is stale by design

    f.doc.storeActive();
    REQUIRE(f.doc.activePatch().name == "Lead");
    REQUIRE(f.doc.snapshotForSave().patches[0].name == "Lead");
}

TEST_CASE("BankDocument: addPatch and duplicate do not switch; duplicating the active captures first",
          "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();
    f.s.setNodeCustomName(1, "live");

    REQUIRE(f.doc.addPatch() == 3);
    REQUIRE(f.doc.size() == 4);
    REQUIRE(f.doc.activeIndex() == 0);

    REQUIRE(f.doc.duplicate(0) == 1);
    REQUIRE(f.doc.size() == 5);
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.doc.bank().patches[1].name == "A copy");
    REQUIRE(f.doc.bank().patches[1].nodes[0].customName == "live"); // live edit captured into the copy
    f.settle();
    REQUIRE(f.s.getActivePatch().name == "A"); // still live, no swap issued
}

TEST_CASE("BankDocument: removing a non-active patch keeps the live patch; removing the active swaps",
          "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();
    const auto gen = f.s.getPatchGeneration();

    REQUIRE(f.doc.remove(2)); // "C"
    f.settle();
    REQUIRE(f.doc.size() == 2);
    REQUIRE(f.s.getPatchGeneration() == gen);

    REQUIRE(f.doc.remove(0)); // the active "A" -> "B" becomes active and live
    f.settle();
    REQUIRE(f.doc.size() == 1);
    REQUIRE(f.doc.activeIndex() == 0);
    REQUIRE(f.s.getActivePatch().name == "B");
    REQUIRE(f.s.getNodeCount() == 2);

    REQUIRE(f.doc.remove(0)); // last one -> Untitled, empty, live
    f.settle();
    REQUIRE(f.doc.size() == 1);
    REQUIRE(f.doc.activePatch().name == "Untitled");
    REQUIRE(f.s.getNodeCount() == 0);
}

TEST_CASE("BankDocument: live capture can be suspended and a swap re-enables it", "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();

    // Simulate Test Tone: capture, suspend, then the live graph becomes empty.
    f.doc.storeActive();
    f.doc.setLiveCaptureEnabled(false);
    f.s.clearPatch();
    f.settle();
    REQUIRE(f.s.getNodeCount() == 0);

    // A save snapshot while suspended keeps the slot, not the empty live patch.
    REQUIRE(f.doc.snapshotForSave().patches[0].nodes.size() == 1);

    // reloadActive brings the slot back and re-enables capture.
    f.doc.reloadActive();
    f.settle();
    REQUIRE(f.s.getNodeCount() == 1);
    REQUIRE(f.doc.isLiveCaptureEnabled());

    // Same for a switch issued while suspended: the slot survives, capture is back on.
    f.doc.setLiveCaptureEnabled(false);
    f.s.clearPatch();
    f.settle();
    REQUIRE(f.doc.select(1));
    f.settle();
    REQUIRE(f.doc.bank().patches[0].nodes.size() == 1);
    REQUIRE(f.doc.isLiveCaptureEnabled());
    REQUIRE(f.s.getNodeCount() == 2);
}

TEST_CASE("BankDocument: beforeSwap runs before every swap it issues", "[session][bank]")
{
    Fixture f;
    int calls = 0;
    f.doc.beforeSwap = [&calls] { ++calls; };

    f.doc.loadBank(threeBank()); // 1
    f.settle();
    f.doc.select(1); // 2
    f.settle();
    f.doc.rename(1, "x"); // no swap
    f.doc.addPatch();     // no swap
    f.doc.remove(1);      // active removed -> 3
    f.settle();
    f.doc.reloadActive(); // 4
    f.settle();
    REQUIRE(calls == 4);
}

TEST_CASE("BankDocument: a second select while the first swap is building leaves the in-flight slot intact",
          "[session][bank]")
{
    Fixture f;
    f.doc.loadBank(threeBank());
    f.settle();

    f.doc.select(1); // Session is Building "B" and still holds "A"
    f.doc.select(2); // must NOT capture "A" into slot 1
    f.settle();

    REQUIRE(f.doc.activeIndex() == 2);
    REQUIRE(f.s.getActivePatch().name == "C");
    REQUIRE(f.doc.bank().patches[1].nodes.size() == 2);
    REQUIRE(f.doc.bank().patches[1].nodes[0].desc.name == "B0");
    REQUIRE(f.doc.bank().patches[0].nodes.size() == 1);
    REQUIRE(f.doc.bank().patches[0].nodes[0].desc.name == "A0");
}

TEST_CASE("BankDocument: after a failed switch, neither save nor the next switch overwrites the slot",
          "[session][bank]")
{
    Fixture f;
    auto bank = threeBank();
    bank.patches[1].nodes[0].desc.fileOrIdentifier = "/fail/B0.vst3";
    f.doc.loadBank(bank);
    f.settle();

    REQUIRE(f.doc.select(1));
    f.settle();
    REQUIRE(f.s.getLastError().isNotEmpty());
    REQUIRE(f.s.getActivePatch().name == "A"); // Session kept the previous patch
    REQUIRE_FALSE(f.doc.liveMatchesActiveSlot());

    const auto saved = f.doc.snapshotForSave();
    REQUIRE(saved.patches[1].nodes.size() == 2);
    REQUIRE(saved.patches[1].nodes[0].desc.fileOrIdentifier == "/fail/B0.vst3");

    REQUIRE(f.doc.select(2));
    f.settle();
    REQUIRE(f.doc.bank().patches[1].nodes[0].desc.fileOrIdentifier == "/fail/B0.vst3");
    REQUIRE(f.doc.bank().patches[1].name == "B");
    REQUIRE(f.s.getActivePatch().name == "C");
    REQUIRE(f.doc.liveMatchesActiveSlot());
}
