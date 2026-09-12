// Tests for session::FeatureGate (optional-features plan U1): a feature is
// available only when it's on in Settings AND on for the board; Settings
// changes save and broadcast; a board switch marks the board unsaved.
// Settings live in memory here, so the tests never touch the real Preferences.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/BankDocument.h"
#include "session/FeatureGate.h"
#include "session/PreparedGraph.h"
#include "session/Session.h"

#include <chrono>
#include <map>
#include <thread>

using namespace pedalboard3;
using session::Feature;

namespace
{

// Patches here have no nodes: the audio input goes straight to the output.
session::Session::BuildFn passThroughBuilder()
{
    return [](const session::Patch&, double sr, int bs, juce::String&) -> session::PreparedGraph
    {
        session::PreparedGraph pg;
        pg.graph = std::make_unique<juce::AudioProcessorGraph>();
        pg.graph->setPlayConfigDetails(2, 2, sr, bs);
        auto in = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
        auto out = pg.graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        for (int ch = 0; ch < 2; ++ch)
            pg.graph->addConnection({{in->nodeID, ch}, {out->nodeID, ch}});
        pg.graph->prepareToPlay(sr, bs);
        return pg;
    };
}

struct Rig
{
    engine::AudioEngine eng;
    engine::PluginHost host;
    session::Session s;
    session::BankDocument doc;
    std::map<Feature, bool> saved; // the "Preferences"
    session::FeatureGate gate;
    int changes{0};

    struct Counter : juce::ChangeListener
    {
        int* n;
        explicit Counter(int* c) : n(c) {}
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++*n; }
    } counter{&changes};

    Rig()
        : s(eng, host, passThroughBuilder()), doc(s),
          gate(session::FeatureGate::GlobalStore{[this](Feature f)
                                                 {
                                                     const auto it = saved.find(f);
                                                     return it != saved.end() ? it->second
                                                                              : session::enabledByDefault(f);
                                                 },
                                                 [this](Feature f, bool on) { saved[f] = on; }},
               doc)
    {
        eng.initialise();
        gate.addChangeListener(&counter);
    }
    ~Rig() { gate.removeChangeListener(&counter); }

    // Deliver the async change messages (the board's first, then the gate's).
    void flush()
    {
        doc.dispatchPendingMessages();
        gate.dispatchPendingMessages();
    }

    void settle(int timeoutMs = 2000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            s.dispatchPendingResult();
            if (s.getState() == session::Session::State::Idle)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        flush();
    }
};

} // namespace

TEST_CASE("FeatureGate: on a fresh install only MIDI is available", "[session][features]")
{
    Rig rig;
    for (const auto feature : session::kAllFeatures)
        REQUIRE(rig.gate.isAvailable(feature) == (feature == Feature::Midi));
}

TEST_CASE("FeatureGate: available only when on in Settings and on for the board", "[session][features]")
{
    Rig rig;
    rig.gate.setEnabled(Feature::Looper, true);
    REQUIRE(rig.gate.isAvailable(Feature::Looper)); // a board has everything on until it says otherwise

    rig.gate.setOnForBoard(Feature::Looper, false);
    REQUIRE_FALSE(rig.gate.isAvailable(Feature::Looper));
    REQUIRE(rig.gate.isEnabled(Feature::Looper)); // Settings keeps its own switch

    rig.gate.setEnabled(Feature::Looper, false);
    rig.gate.setOnForBoard(Feature::Looper, true);
    REQUIRE_FALSE(rig.gate.isAvailable(Feature::Looper)); // the board alone can't switch it on
}

TEST_CASE("FeatureGate: a Settings change is saved and broadcast, once", "[session][features]")
{
    Rig rig;
    rig.flush();
    rig.changes = 0;

    rig.gate.setEnabled(Feature::Tempo, true);
    rig.flush();
    REQUIRE(rig.saved.at(Feature::Tempo));
    REQUIRE(rig.changes == 1);

    rig.gate.setEnabled(Feature::Tempo, true); // no change, no save, no message
    rig.flush();
    REQUIRE(rig.changes == 1);

    // A restart reads it back.
    session::FeatureGate again(
        session::FeatureGate::GlobalStore{
            [&rig](Feature f) { return rig.saved.count(f) != 0 ? rig.saved.at(f) : session::enabledByDefault(f); }, {}},
        rig.doc);
    REQUIRE(again.isEnabled(Feature::Tempo));
}

TEST_CASE("FeatureGate: a board toggle broadcasts only when it changes what's available", "[session][features]")
{
    Rig rig;
    rig.flush();
    rig.changes = 0;

    rig.gate.setOnForBoard(Feature::Metronome, false); // off in Settings anyway: nothing becomes unavailable
    rig.flush();
    REQUIRE(rig.changes == 0);

    rig.gate.setOnForBoard(Feature::Midi, false);
    rig.flush();
    REQUIRE(rig.changes == 1);
    REQUIRE_FALSE(rig.gate.isAvailable(Feature::Midi));
}

TEST_CASE("FeatureGate: a loaded board brings its own switches", "[session][features]")
{
    Rig rig;
    rig.gate.setEnabled(Feature::Osc, true);
    session::PatchBank board;
    board.setFeature(Feature::Osc, false);
    rig.doc.loadBank(board);
    rig.settle();

    REQUIRE_FALSE(rig.gate.isOnForBoard(Feature::Osc));
    REQUIRE_FALSE(rig.gate.isAvailable(Feature::Osc));
    REQUIRE(rig.gate.isEnabled(Feature::Osc));
}

TEST_CASE("BankDocument: a board switch marks the board unsaved, and switching it back clears that",
          "[session][features][bank]")
{
    Rig rig;
    rig.doc.markSaved(juce::File());
    REQUIRE_FALSE(rig.doc.isModified());

    REQUIRE(rig.doc.setBoardFeature(Feature::Looper, false));
    REQUIRE(rig.doc.isModified());
    REQUIRE_FALSE(rig.doc.setBoardFeature(Feature::Looper, false)); // already off: nothing to do

    REQUIRE(rig.doc.setBoardFeature(Feature::Looper, true));
    REQUIRE_FALSE(rig.doc.isModified());
}
