// Tests for pedalboard3::session::Session -- the Phase 1.D.2 patch-switch
// state machine. Uses the BuildFn-injection ctor so we don't need a real
// VST3 binary or a running audio device.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/Patch.h"
#include "session/PreparedGraph.h"
#include "session/Session.h"
#include "support/OfflineRender.h"
#include "support/TestGainProcessor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{

// Fake whole-patch builder (Phase 1.D.3 BuildFn signature). Returns a
// minimal valid PreparedGraph for any non-empty Patch; returns invalid
// when `failOnEmpty` is set and the first node's fileOrIdentifier is
// empty (lets tests trigger build failure).
class FakeBuilder
{
public:
    struct Call
    {
        session::Patch patch;
    };

    session::Session::BuildFn fn(int delayMs = 0, bool failOnEmpty = false)
    {
        return [this, delayMs, failOnEmpty](
                   const session::Patch& p, double sr, int bs, juce::String& errorOut) -> session::PreparedGraph
        {
            calls_.fetch_add(1, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(callsMutex_);
                lastCalls_.push_back({p});
            }
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

            if (failOnEmpty && !p.nodes.empty() && p.nodes.front().desc.fileOrIdentifier.isEmpty())
            {
                errorOut = "FakeBuilder: empty fileOrIdentifier";
                return {};
            }

            session::PreparedGraph pg;
            pg.graph = std::make_unique<juce::AudioProcessorGraph>();
            pg.graph->setPlayConfigDetails(2, 2, sr, bs);
            auto inNode = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
            auto outNode = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
            // One TestGainProcessor per Patch node. Populate the nodes
            // map so Session can index by NodeId.
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
            {
                for (int ch = 0; ch < 2; ++ch)
                    pg.graph->addConnection({{inNode->nodeID, ch}, {outNode->nodeID, ch}});
            }
            pg.graph->prepareToPlay(sr, bs);
            return pg;
        };
    }

    int callCount() const { return calls_.load(std::memory_order_acquire); }
    std::vector<Call> snapshot() const
    {
        std::lock_guard<std::mutex> lk(callsMutex_);
        return lastCalls_;
    }

private:
    std::atomic<int> calls_{0};
    mutable std::mutex callsMutex_;
    std::vector<Call> lastCalls_;
};

juce::PluginDescription makeDesc(const juce::String& name)
{
    juce::PluginDescription d;
    d.name = name;
    d.pluginFormatName = "VST3";
    d.fileOrIdentifier = "/fake/" + name + ".vst3";
    return d;
}

void pumpUntilIdle(session::Session& s, int timeoutMs = 2000)
{
    // The worker thread fires juce::AsyncUpdater::triggerAsyncUpdate; in a
    // normal app the JUCE event loop dispatches it on the message thread.
    // pb3_juce_config keeps JUCE_MODAL_LOOPS_PERMITTED off, so
    // MessageManager::runDispatchLoopUntil isn't available. Instead drive
    // the AsyncUpdater synchronously via Session::dispatchPendingResult,
    // which calls handleUpdateNowIfNeeded under the hood.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        s.dispatchPendingResult();
        if (s.getState() == session::Session::State::Idle)
        {
            // Brief settle to catch a worker that's mid-publish.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            s.dispatchPendingResult();
            if (s.getState() == session::Session::State::Idle)
                return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// Wait for a state-version increment with bounded patience.
bool waitForVersion(session::Session& s, std::uint64_t target, int timeoutMs = 1000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        s.dispatchPendingResult();
        if (s.getStateVersion() >= target)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

namespace
{

session::Patch makeBogusPatch()
{
    session::Patch p;
    session::PatchNode n;
    n.id = 1;
    n.desc.name = "Bogus";
    n.desc.pluginFormatName = "VST3";
    // fileOrIdentifier intentionally empty -- triggers FakeBuilder failure path.
    p.nodes.push_back(n);
    return p;
}

session::Patch makeOneNodePatch(const juce::String& pluginName)
{
    session::Patch p;
    session::PatchNode n;
    n.id = 1;
    n.desc = makeDesc(pluginName);
    p.nodes.push_back(n);
    return p;
}

} // namespace

TEST_CASE("Session: starts Idle with no nodes", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn());

    REQUIRE(s.getState() == session::Session::State::Idle);
    REQUIRE(s.getNodeCount() == 0);
    REQUIRE(s.getLastError().isEmpty());
    REQUIRE(s.getStateVersion() == 0);
}

TEST_CASE("Session: replacePatch with bogus patch walks Idle->Building->Idle, error set", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn(0, /*failOnEmpty*/ true));

    s.replacePatch(makeBogusPatch());

    pumpUntilIdle(s);

    REQUIRE(s.getState() == session::Session::State::Idle);
    REQUIRE(s.getNodeCount() == 0);
    REQUIRE(s.getLastError().isNotEmpty());
    REQUIRE(s.getStateVersion() >= 2u); // Idle->Building->Idle
}

TEST_CASE("Session: clearPatch from empty transitions through Idle cleanly", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn());

    s.clearPatch();
    pumpUntilIdle(s);

    REQUIRE(s.getState() == session::Session::State::Idle);
    REQUIRE(s.getNodeCount() == 0);
}

TEST_CASE("Session: dtor mid-build joins worker cleanly without crash", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    {
        session::Session s(eng, host, builder.fn(/*delayMs*/ 200));
        s.replacePatch(makeOneNodePatch("Slow"));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    SUCCEED();
}

TEST_CASE("Session: getStatus returns coherent snapshot", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn(0, /*failOnEmpty*/ true));

    s.replacePatch(makeBogusPatch());
    pumpUntilIdle(s);

    auto st = s.getStatus();
    REQUIRE(st.state == session::Session::State::Idle);
    REQUIRE(st.lastError.isNotEmpty());
    REQUIRE(st.version >= 2u);
    REQUIRE(st.nodeCount == 0);
}

TEST_CASE("Session: state version is monotonic across transitions", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn(0, /*failOnEmpty*/ true));

    const auto v0 = s.getStateVersion();
    s.replacePatch(makeBogusPatch());

    REQUIRE(waitForVersion(s, v0 + 1)); // -> Building
    pumpUntilIdle(s);
    REQUIRE(s.getStateVersion() >= v0 + 2u); // -> Idle (with error)
}

TEST_CASE("Session: synchronous setNodeBypass on absent NodeId is a no-op", "[session][incremental]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;
    session::Session s(eng, host, builder.fn());

    const auto v0 = s.getStateVersion();
    s.setNodeBypass(42, true);          // no node 42 exists
    REQUIRE(s.getStateVersion() == v0); // no version bump for no-op
}

TEST_CASE("Session: getNodeIdsInOrder returns empty for fresh Session", "[session][snapshot]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;
    session::Session s(eng, host, builder.fn());
    REQUIRE(s.getNodeIdsInOrder().empty());
    REQUIRE(s.getNodeSnapshots().empty());
}

// -----------------------------------------------------------------------
// liveGraph_ staleness defence (Session.cpp commitAddPlugin +
// applyConnectionsToLiveGraph)
// -----------------------------------------------------------------------
//
// Scenario: anything outside Session that calls AudioEngine::loadGraph
// (the Test Tone diagnostic in TransportBar is the production case)
// invalidates Session's cached liveGraph_ pointer. Without the re-fetch
// fix, the next incremental mutation dereferences a freed graph and
// crashes the audio thread on commit.
//
// Both commitAddPlugin and applyConnectionsToLiveGraph re-derive
// liveGraph_ from engine_.getRuntime()->getLiveGraph() before touching
// it. The tests below drive the second path (applyConnectionsToLiveGraph
// via moveNode) -- commitAddPlugin's rebind branch needs a real plugin
// instance to exercise and is covered by manual smoke until we ship a
// fake-instance test fixture.

namespace
{

// Build a stand-alone graph the test can hand to engine_.loadGraph
// directly, bypassing Session entirely. Mirrors what TransportBar's Test
// Tone path does in production.
std::unique_ptr<juce::AudioProcessorGraph> buildExternalPassthroughGraph(double sr, int bs)
{
    auto g = std::make_unique<juce::AudioProcessorGraph>();
    g->setPlayConfigDetails(2, 2, sr, bs);
    g->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    g->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    g->prepareToPlay(sr, bs);
    return g;
}

session::Patch makeTwoNodePatch()
{
    session::Patch p;
    session::PatchNode a;
    a.id = 1;
    a.desc = makeDesc("A");
    session::PatchNode b;
    b.id = 2;
    b.desc = makeDesc("B");
    p.nodes.push_back(a);
    p.nodes.push_back(b);
    return p;
}

} // namespace

TEST_CASE("Session: removeNode on absent NodeId after external loadGraph(nullptr) is safe", "[session][rebind]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn());

    // Drop the runtime's live graph from underneath Session. Production
    // analogue: TransportBar's Test Tone toggle-off path calls
    // engine.loadGraph(nullptr).
    auto previous = eng.loadGraph(nullptr);
    (void)previous;

    const auto v0 = s.getStateVersion();

    // removeNode on a NodeId that was never in the patch is a no-op --
    // but that no-op MUST short-circuit cleanly without dereferencing
    // the stale liveGraph_ pointer Session cached during ctor.
    s.removeNode(99);

    REQUIRE(s.getStateVersion() == v0); // genuine no-op, no version bump
    REQUIRE(s.getNodeCount() == 0);
}

TEST_CASE("Session: moveNode after external loadGraph re-derives liveGraph_ rather than crashing", "[session][rebind]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn());

    // 1. Build a 2-node patch through the FakeBuilder. After this,
    //    Session's cached liveGraph_ points at FakeBuilder's prepared
    //    graph that engine.loadGraph just installed.
    s.replacePatch(makeTwoNodePatch());
    pumpUntilIdle(s);
    REQUIRE(s.getNodeCount() == 2);

    // 2. Externally swap the runtime to a different graph. Hold the
    //    returned previous graph on the stack so Session's cached
    //    pointer still references valid memory (we want to test the
    //    re-fetch logic, not a use-after-free symptom).
    auto external = buildExternalPassthroughGraph(kDefaultSampleRate, kDefaultBlockSize);
    auto previous = eng.loadGraph(std::move(external));

    // 3. moveNode triggers applyConnectionsToLiveGraph on the message
    //    thread. Without the rebind fix it would mutate `previous`
    //    (Session's stale cached pointer); with the fix it re-derives
    //    liveGraph_ from runtime.getLiveGraph() and operates on the
    //    externally-swapped graph instead. We can only assert "no
    //    crash + version bumped + lastError remains clear" -- the
    //    target graph has no matching node IDs so addConnection
    //    silently fails, but Session-side state stays consistent.
    const auto v0 = s.getStateVersion();
    s.moveNode(1, 1);
    REQUIRE(s.getStateVersion() > v0);
    REQUIRE(s.getLastError().isEmpty());
    REQUIRE(s.getNodeCount() == 2);

    (void)previous;
}

TEST_CASE("Session: destructive mutators are no-ops while a rebuild is in flight", "[session][rebind]")
{
    // Regression guard for the dangling-graph use-after-free: during the async
    // window after clearPatch()/replacePatch() (production trigger: Test Tone
    // toggle, which frees the previous chain graph out-of-band) the cached
    // Node*s in nodes_ can be dangling. removeNode/setNodeBypass/moveNode must
    // short-circuit while Session is not Idle rather than dereference them.
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;

    session::Session s(eng, host, builder.fn());

    s.replacePatch(makeTwoNodePatch());
    pumpUntilIdle(s);
    REQUIRE(s.getNodeCount() == 2);

    // Queue another rebuild but DON'T pump — replacePatch publishes Building
    // synchronously, so Session is now mid-rebuild (the dangling window).
    s.replacePatch(makeTwoNodePatch());
    REQUIRE(s.getState() != session::Session::State::Idle);

    const auto vBuilding = s.getStateVersion();
    s.removeNode(1);
    s.setNodeBypass(1, true);
    s.moveNode(1, 1);

    // All three short-circuited: no state change, no crash.
    REQUIRE(s.getStateVersion() == vBuilding);
    REQUIRE(s.getNodeCount() == 2);

    // Once the rebuild lands the chain is intact and mutable again.
    pumpUntilIdle(s);
    REQUIRE(s.getNodeCount() == 2);
}

// ---- U2: free-routing connection API -------------------------------------

namespace
{
constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

session::Patch makeThreeNodeChain()
{
    session::Patch p;
    for (int i = 1; i <= 3; ++i)
    {
        session::PatchNode n;
        n.id = i;
        n.desc = makeDesc("P" + juce::String(i));
        p.nodes.push_back(n);
    }
    p.connections = session::linearChainConnections(p.nodes);
    return p;
}

bool hasConn(const std::vector<session::PatchConnection>& v, session::NodeId s, int sc, session::NodeId d, int dc)
{
    return std::any_of(v.begin(),
                       v.end(),
                       [&](const session::PatchConnection& c)
                       { return c.src == s && c.srcChannel == sc && c.dst == d && c.dstChannel == dc; });
}

bool touchesNode(const std::vector<session::PatchConnection>& v, session::NodeId id)
{
    return std::any_of(
        v.begin(), v.end(), [&](const session::PatchConnection& c) { return c.src == id || c.dst == id; });
}

// Build a Session sitting Idle on a 3-node linear chain.
struct ChainFixture
{
    engine::AudioEngine eng;
    engine::PluginHost host;
    FakeBuilder builder;
    session::Session s{eng, host, builder.fn()};

    ChainFixture()
    {
        eng.initialise();
        s.replacePatch(makeThreeNodeChain());
        pumpUntilIdle(s);
    }
};
} // namespace

TEST_CASE("Session: addConnection adds a valid parallel/skip edge", "[session][routing]")
{
    ChainFixture f;
    REQUIRE(f.s.getNodeCount() == 3);

    // Skip edge 1 -> 3 (audio L): valid DAG addition.
    REQUIRE(f.s.addConnection(1, 0, 3, 0));
    REQUIRE(hasConn(f.s.getActivePatch().connections, 1, 0, 3, 0));
}

TEST_CASE("Session: addConnection rejects invalid edges", "[session][routing]")
{
    ChainFixture f;
    const auto before = f.s.getActivePatch().connections;

    // Cross-signal: audio out -> MIDI in.
    REQUIRE_FALSE(f.s.addConnection(1, 0, 2, kMidi));
    // Duplicate of an existing chain edge (1 -> 2 audio L).
    REQUIRE_FALSE(f.s.addConnection(1, 0, 2, 0));
    // Cycle: 3 -> 1 closes the loop 1->2->3->1.
    REQUIRE_FALSE(f.s.addConnection(3, 0, 1, 0));
    // Dangling: node 999 does not exist.
    REQUIRE_FALSE(f.s.addConnection(1, 0, 999, 0));

    // None of the rejections mutated the patch.
    REQUIRE(f.s.getActivePatch().connections == before);
}

TEST_CASE("Session: removeConnection drops the edge", "[session][routing]")
{
    ChainFixture f;
    REQUIRE(hasConn(f.s.getActivePatch().connections, 1, 0, 2, 0));

    f.s.removeConnection(1, 0, 2, 0);
    REQUIRE_FALSE(hasConn(f.s.getActivePatch().connections, 1, 0, 2, 0));
    // The R-channel edge is untouched.
    REQUIRE(hasConn(f.s.getActivePatch().connections, 1, 1, 2, 1));
}

TEST_CASE("Session: removeNode prunes its edges and bridges neighbours", "[session][routing]")
{
    ChainFixture f;

    f.s.removeNode(2); // middle of graphIO -> 1 -> 2 -> 3 -> graphIO
    const auto conns = f.s.getActivePatch().connections;

    REQUIRE(f.s.getNodeCount() == 2);
    REQUIRE_FALSE(touchesNode(conns, 2));               // no dangling edges to/from the removed node
    REQUIRE(hasConn(conns, 1, 0, 3, 0));                // bridged: 1 -> 3 (audio L)
    REQUIRE(hasConn(conns, 1, 1, 3, 1));                // bridged: audio R
    REQUIRE(hasConn(conns, 1, kMidi, 3, kMidi));        // bridged: MIDI
    REQUIRE(hasConn(conns, session::graphIO, 0, 1, 0)); // head + tail intact
    REQUIRE(hasConn(conns, 3, 0, session::graphIO, 0));
}

TEST_CASE("Session: setNodePosition updates the snapshot", "[session][routing]")
{
    ChainFixture f;
    f.s.setNodePosition(2, 320.0f, 96.0f);

    const auto p = f.s.getActivePatch();
    const auto* n = p.findNode(2);
    REQUIRE(n != nullptr);
    REQUIRE(n->hasPosition());
    REQUIRE(n->x == 320.0f);
    REQUIRE(n->y == 96.0f);

    // The position also flows through the UI-facing snapshot (U3: SignalCanvas
    // reads positions from getNodeSnapshots to lay out / drag cards).
    const auto snaps = f.s.getNodeSnapshots();
    const auto it = std::find_if(snaps.begin(), snaps.end(), [](const auto& s) { return s.id == 2; });
    REQUIRE(it != snaps.end());
    REQUIRE(it->hasPosition());
    REQUIRE(it->x == 320.0f);
    REQUIRE(it->y == 96.0f);
}

TEST_CASE("Session: seedNodePositions fills only unpositioned nodes", "[session][routing]")
{
    ChainFixture f;

    // Node 2 already has a user-authored position; the seed must not clobber it.
    f.s.setNodePosition(2, 320.0f, 96.0f);

    f.s.seedNodePositions({
        {1, 190.0f, 60.0f},
        {2, 999.0f, 999.0f}, // stale auto-slot for an already-positioned node
        {3, 440.0f, 60.0f},
        {999, 1.0f, 1.0f}, // unknown id: ignored
    });

    const auto p = f.s.getActivePatch();
    REQUIRE(p.findNode(1)->hasPosition());
    REQUIRE(p.findNode(1)->x == 190.0f);
    REQUIRE(p.findNode(1)->y == 60.0f);
    REQUIRE(p.findNode(2)->x == 320.0f); // untouched
    REQUIRE(p.findNode(2)->y == 96.0f);
    REQUIRE(p.findNode(3)->hasPosition());
    REQUIRE(p.findNode(3)->x == 440.0f);

    // Re-seeding after everything is positioned is a no-op (the SignalCanvas
    // relayout cycle relies on this to terminate).
    const auto versionBefore = f.s.getStatus().version;
    f.s.seedNodePositions({{1, 5.0f, 5.0f}, {3, 5.0f, 5.0f}});
    REQUIRE(f.s.getActivePatch().findNode(1)->x == 190.0f);
    REQUIRE(f.s.getStatus().version == versionBefore);
}

// ---- Crash-hunt #7 fixes ---------------------------------------------------

TEST_CASE("Session: two rapid adds both land (worker results queue, not a slot)", "[session][crash-hunt]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    FakeBuilder builder;
    session::Session s(eng, host, builder.fn());

    // Enqueue two internal-node adds back-to-back with no dispatch between:
    // the worker can finish both builds before the message thread consumes
    // either result. With the old single-slot hand-off the second overwrote
    // the first — destroying its live instance on the worker thread and
    // silently losing the add.
    s.addInternalNode("LevelProcessor");
    s.addInternalNode("VuMeterProcessor");

    // Let the worker produce both results before the first dispatch.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    pumpUntilIdle(s);
    REQUIRE(s.getNodeCount() == 2);
}

TEST_CASE("Session: node mutations refuse a live graph swapped outside Session", "[session][crash-hunt]")
{
    ChainFixture f;

    // Swap a foreign graph in directly (Test Tone-style), bypassing Session.
    // Keep the returned chain graph alive so this test never depends on the
    // guard for its own memory safety.
    auto foreign = std::make_unique<juce::AudioProcessorGraph>();
    foreign->setPlayConfigDetails(2, 2, 48000.0, 512);
    foreign->prepareToPlay(48000.0, 512);
    auto previousChainGraph = f.eng.loadGraph(std::move(foreign));

    f.s.removeNode(2);
    REQUIRE(f.s.getNodeCount() == 3); // refused — model and graph untouched

    f.s.setNodeBypass(2, true);
    REQUIRE_FALSE(f.s.isNodeBypassed(2)); // refused

    // A whole-patch swap re-syncs Session with the runtime; mutations work again.
    f.s.replacePatch(makeThreeNodeChain());
    pumpUntilIdle(f.s);
    f.s.removeNode(2);
    REQUIRE(f.s.getNodeCount() == 2);
}

TEST_CASE("Session: getActivePatchWithState quiesces briefly and restores processing", "[session][crash-hunt]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::Session s(eng, host); // production BuildFn — internal nodes build for real

    s.addInternalNode("LevelProcessor");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pumpUntilIdle(s);
    REQUIRE(s.getNodeCount() == 1);

    // The quiesce barrier is bounded: with no audio device running the
    // generation counter never advances, so the wait must give up quickly
    // rather than hang the save path.
    const auto start = std::chrono::steady_clock::now();
    const auto p = s.getActivePatchWithState();
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    REQUIRE(p.nodes.size() == 1);

    // Wrappers are un-suspended afterwards; a second capture behaves identically.
    const auto p2 = s.getActivePatchWithState();
    REQUIRE(p2.nodes.size() == 1);
}

TEST_CASE("Session: patch generation bumps only on whole-patch swaps", "[session][crash-hunt]")
{
    ChainFixture f;
    const auto g0 = f.s.getPatchGeneration();
    REQUIRE(g0 >= 1u); // the fixture's replacePatch already committed once

    f.s.removeNode(2); // incremental mutation: no bump
    REQUIRE(f.s.getPatchGeneration() == g0);

    f.s.replacePatch(makeThreeNodeChain());
    pumpUntilIdle(f.s);
    REQUIRE(f.s.getPatchGeneration() == g0 + 1);
}
