// Tests for pedalboard3::session::Session — the Phase 1.D.2 patch-switch
// state machine. Uses the BuildFn-injection ctor so we don't need a real
// VST3 binary or a running audio device.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/PluginSlot.h"
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

// Fake builder that returns a real (in-memory) graph wrapping a
// TestGainProcessor — mirrors the production buildPreparedGraph minus
// the BypassableInstance/PluginHost dependency. Records every invocation
// so tests can assert coalescing behaviour.
class FakeBuilder
{
public:
    struct Call
    {
        juce::PluginDescription desc;
    };

    session::Session::BuildFn fn(int delayMs = 0, bool failOnEmpty = false)
    {
        return [this, delayMs, failOnEmpty](const juce::PluginDescription& desc,
                                            double sr,
                                            int bs,
                                            juce::String& errorOut) -> session::PreparedGraph
        {
            calls_.fetch_add(1, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(callsMutex_);
                lastCalls_.push_back({desc});
            }
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

            if (failOnEmpty && desc.fileOrIdentifier.isEmpty())
            {
                errorOut = "FakeBuilder: empty fileOrIdentifier";
                return {};
            }

            session::PreparedGraph pg;
            pg.graph = std::make_unique<juce::AudioProcessorGraph>();
            pg.graph->setPlayConfigDetails(2, 2, sr, bs);
            auto inNode = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
            auto procNode = pg.graph->addNode(std::make_unique<TestGainProcessor>(1.0f));
            auto outNode = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
            for (int ch = 0; ch < 2; ++ch)
            {
                pg.graph->addConnection({{inNode->nodeID, ch}, {procNode->nodeID, ch}});
                pg.graph->addConnection({{procNode->nodeID, ch}, {outNode->nodeID, ch}});
            }
            pg.graph->prepareToPlay(sr, bs);
            // 1.D.3 PreparedGraph shape: nodes map keyed by NodeId.
            // For the fake we leave the map empty — Session's slot
            // adapter handles an empty map by clearing observer
            // pointers. The existing tests only check state-machine
            // transitions, not hosted-plugin pointer identity.
            (void)desc;
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

TEST_CASE("Session: starts Idle, no plugin loaded", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);
    FakeBuilder builder;

    session::Session s(slot, eng, builder.fn());

    REQUIRE(s.getState() == session::Session::State::Idle);
    REQUIRE_FALSE(s.isLoaded());
    REQUIRE(s.getLoadedName().isEmpty());
    REQUIRE(s.getLastError().isEmpty());
    REQUIRE(s.getStateVersion() == 0);
}

TEST_CASE("Session: queueLoad with bogus desc walks Idle→Building→Idle, error set", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);
    FakeBuilder builder;

    session::Session s(slot, eng, builder.fn(0, /*failOnEmpty*/ true));

    juce::PluginDescription bogus;
    bogus.name = "Bogus";
    bogus.pluginFormatName = "VST3";
    // fileOrIdentifier intentionally empty → builder fails

    s.queueLoad(bogus);

    pumpUntilIdle(s);

    REQUIRE(s.getState() == session::Session::State::Idle);
    REQUIRE_FALSE(s.isLoaded());
    REQUIRE(s.getLastError().isNotEmpty());
    REQUIRE(s.getStateVersion() >= 2u); // at least Idle→Building→Idle
}

TEST_CASE("Session: queueUnload on an empty slot transitions through Idle cleanly", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);
    FakeBuilder builder;

    session::Session s(slot, eng, builder.fn());

    s.queueUnload();
    pumpUntilIdle(s);

    REQUIRE(s.getState() == session::Session::State::Idle);
    REQUIRE_FALSE(s.isLoaded());
    REQUIRE_FALSE(s.getActiveDescription().has_value());
}

TEST_CASE("Session: dtor mid-build joins worker cleanly without crash", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);
    FakeBuilder builder;

    {
        session::Session s(slot, eng, builder.fn(/*delayMs*/ 200));
        s.queueLoad(makeDesc("Slow"));
        // Don't wait for completion — destruct immediately.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    SUCCEED();
}

TEST_CASE("Session: getStatus returns coherent snapshot", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);
    FakeBuilder builder;

    session::Session s(slot, eng, builder.fn(0, /*failOnEmpty*/ true));

    juce::PluginDescription bogus;
    bogus.name = "Bogus2";
    bogus.pluginFormatName = "VST3";
    s.queueLoad(bogus);
    pumpUntilIdle(s);

    auto st = s.getStatus();
    REQUIRE(st.state == session::Session::State::Idle);
    REQUIRE(st.lastError.isNotEmpty());
    REQUIRE(st.version >= 2u);
    REQUIRE_FALSE(st.activeDescription.has_value());
}

TEST_CASE("Session: state version is monotonic across transitions", "[session][state-machine]")
{
    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::PluginSlot slot(eng, host);
    FakeBuilder builder;

    session::Session s(slot, eng, builder.fn(0, /*failOnEmpty*/ true));

    const auto v0 = s.getStateVersion();

    juce::PluginDescription bogus;
    bogus.name = "Bogus3";
    bogus.pluginFormatName = "VST3";
    s.queueLoad(bogus);

    REQUIRE(waitForVersion(s, v0 + 1)); // → Building
    pumpUntilIdle(s);
    REQUIRE(s.getStateVersion() >= v0 + 2u); // → Idle (with error)
}
