// Tests for session::Features and the board's feature switches on PatchBank
// (optional-features plan U1): the defaults, "does this board use it", and the
// switches round-tripping through <BANK>.

#include <catch2/catch_test_macros.hpp>

#include "session/Features.h"
#include "session/PatchBank.h"

#include <set>

using namespace pedalboard3;
using session::Feature;

namespace
{

session::Patch patchWithNode(const juce::String& internalType)
{
    session::Patch p;
    session::PatchNode n;
    n.id = 1;
    n.kind = session::NodeKind::Internal;
    n.internalType = internalType;
    p.nodes.push_back(n);
    return p;
}

bool usesOnly(const session::PatchBank& bank, Feature expected)
{
    for (const auto feature : session::kAllFeatures)
        if (session::boardUses(bank, feature) != (feature == expected))
            return false;
    return true;
}

} // namespace

TEST_CASE("Features: Tempo, Metronome, Looper and OSC start off; MIDI starts on", "[session][features]")
{
    REQUIRE_FALSE(session::enabledByDefault(Feature::Tempo));
    REQUIRE_FALSE(session::enabledByDefault(Feature::Metronome));
    REQUIRE_FALSE(session::enabledByDefault(Feature::Looper));
    REQUIRE(session::enabledByDefault(Feature::Midi));
    REQUIRE_FALSE(session::enabledByDefault(Feature::Osc));
}

TEST_CASE("Features: every feature has its own key and a name", "[session][features]")
{
    std::set<juce::String> keys;
    for (const auto feature : session::kAllFeatures)
    {
        keys.insert(session::featureKey(feature));
        REQUIRE(session::featureName(feature).isNotEmpty());
    }
    REQUIRE(keys.size() == session::kFeatureCount);
    REQUIRE(session::featureName(Feature::Tempo) == "Tempo / BPM");
}

TEST_CASE("Features: a new board uses nothing optional", "[session][features]")
{
    const session::PatchBank bank;
    for (const auto feature : session::kAllFeatures)
        REQUIRE_FALSE(session::boardUses(bank, feature));
}

TEST_CASE("Features: what each feature's use looks like in a board", "[session][features]")
{
    session::PatchBank bank;

    SECTION("a tempo other than 120 in any patch")
    {
        session::Patch fast;
        fast.tempo = 96.0;
        bank.patches.push_back(fast);
        REQUIRE(usesOnly(bank, Feature::Tempo));
    }
    SECTION("a time signature other than 4/4")
    {
        session::Patch waltz;
        waltz.timeSigNumerator = 3;
        bank.patches.push_back(waltz);
        REQUIRE(usesOnly(bank, Feature::Tempo));
    }
    SECTION("a Metronome node")
    {
        bank.patches.push_back(patchWithNode("MetronomeProcessor"));
        REQUIRE(usesOnly(bank, Feature::Metronome));
    }
    SECTION("a Looper node, in the second patch")
    {
        bank.patches.push_back(patchWithNode("LooperProcessor"));
        REQUIRE(usesOnly(bank, Feature::Looper));
    }
    SECTION("MIDI mappings")
    {
        session::Patch mapped;
        mapped.midiMappings.push_back(control::MidiMapping{});
        bank.patches.push_back(mapped);
        REQUIRE(usesOnly(bank, Feature::Midi));
    }
    SECTION("OSC mappings")
    {
        session::Patch mapped;
        mapped.oscMappings.push_back(control::OscMapping{});
        bank.patches.push_back(mapped);
        REQUIRE(usesOnly(bank, Feature::Osc));
    }
}

TEST_CASE("PatchBank: every feature is on for a board until it turns one off", "[session][features][bank]")
{
    session::PatchBank bank;
    for (const auto feature : session::kAllFeatures)
        REQUIRE(bank.featureOn(feature));
    bank.setFeature(Feature::Looper, false);
    REQUIRE_FALSE(bank.featureOn(Feature::Looper));
    REQUIRE(bank.featureOn(Feature::Metronome));
}

TEST_CASE("PatchBank: the board's switches round-trip through <BANK>, and a file without them has everything on",
          "[session][features][bank]")
{
    session::PatchBank bank;
    bank.setFeature(Feature::Tempo, false);
    bank.setFeature(Feature::Osc, false);
    const auto xml = session::bankToXml(bank);
    REQUIRE(xml != nullptr);
    REQUIRE(xml->getStringAttribute("tempo") == "0");
    REQUIRE_FALSE(xml->hasAttribute("looper")); // only a switch that's off is written

    const auto back = session::bankFromXml(*xml);
    REQUIRE(back != nullptr);
    for (const auto feature : session::kAllFeatures)
        REQUIRE(back->featureOn(feature) == (feature != Feature::Tempo && feature != Feature::Osc));

    xml->removeAttribute("tempo");
    xml->removeAttribute("osc");
    const auto older = session::bankFromXml(*xml);
    REQUIRE(older != nullptr);
    for (const auto feature : session::kAllFeatures)
        REQUIRE(older->featureOn(feature));
}
