// Tests for pedalboard3::session::Session — the Phase 1.D.2 patch-switch
// state machine. Uses the BuildFn-injection ctor so we don't need a real
// VST3 binary or a running audio device.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/Patch.h"
#include "session/PreparedGraph.h"
#include "session/Session.h"
#include "support/TestGainProcessor.h"

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
    // fileOrIdentifier intentionally empty — triggers FakeBuilder failure path.
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

TEST_CASE("Session: replacePatch with bogus patch walks Idle→Building→Idle, error set", "[session][state-machine]")
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
    REQUIRE(s.getStateVersion() >= 2u); // Idle→Building→Idle
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

    REQUIRE(waitForVersion(s, v0 + 1)); // → Building
    pumpUntilIdle(s);
    REQUIRE(s.getStateVersion() >= v0 + 2u); // → Idle (with error)
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
