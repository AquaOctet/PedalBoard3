// Tests for pedalboard3::session::AppCommands — the app-command registry shared
// by key, MIDI and OSC triggers (roadmap M4 U1). Dispatch through a real bank
// is covered in PatchSwitcher_test.cpp.

#include <catch2/catch_test_macros.hpp>

#include "session/AppCommands.h"

#include <set>

using namespace pedalboard3;

TEST_CASE("AppCommands: every command has a unique id, a name and a unique non-zero JUCE id", "[session][app-commands]")
{
    std::set<juce::String> ids;
    std::set<int> juceIds;
    for (const auto& c : session::kAppCommands)
    {
        REQUIRE(juce::String(c.id).isNotEmpty());
        REQUIRE(juce::String(c.name).isNotEmpty());
        REQUIRE(c.juceCommandId != 0);
        ids.insert(c.id);
        juceIds.insert(c.juceCommandId);
    }
    REQUIRE(ids.size() == session::kAppCommands.size());
    REQUIRE(juceIds.size() == session::kAppCommands.size());
}

TEST_CASE("AppCommands: next and previous patch resolve both ways; unknown ids do not", "[session][app-commands]")
{
    const auto* next = session::findAppCommand(session::appcommands::kNextPatch);
    const auto* previous = session::findAppCommand(session::appcommands::kPreviousPatch);
    REQUIRE(next != nullptr);
    REQUIRE(previous != nullptr);
    REQUIRE(session::findAppCommandByJuceId(next->juceCommandId) == next);
    REQUIRE(session::findAppCommandByJuceId(previous->juceCommandId) == previous);

    REQUIRE(session::findAppCommand("patch.nope") == nullptr);
    REQUIRE(session::findAppCommand("") == nullptr);
    REQUIRE(session::findAppCommandByJuceId(0) == nullptr);
}

TEST_CASE("AppCommands: Save resolves both ways", "[session][app-commands]")
{
    const auto* save = session::findAppCommand(session::appcommands::kSave);
    REQUIRE(save != nullptr);
    REQUIRE(session::findAppCommandByJuceId(save->juceCommandId) == save);
}
