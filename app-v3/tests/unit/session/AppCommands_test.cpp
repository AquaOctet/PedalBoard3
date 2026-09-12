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

TEST_CASE("AppCommands: Undo and Redo resolve both ways", "[session][app-commands]")
{
    for (const auto* id : {session::appcommands::kUndo, session::appcommands::kRedo})
    {
        const auto* c = session::findAppCommand(id);
        REQUIRE(c != nullptr);
        REQUIRE(session::findAppCommandByJuceId(c->juceCommandId) == c);
    }
}

TEST_CASE("AppCommands: the transport commands resolve both ways", "[session][app-commands][transport]")
{
    namespace cmd = pedalboard3::session::appcommands;
    for (const auto* id : {cmd::kPlayPause, cmd::kStop, cmd::kReturnToZero, cmd::kTapTempo})
    {
        const auto* c = pedalboard3::session::findAppCommand(id);
        REQUIRE(c != nullptr);
        REQUIRE(pedalboard3::session::findAppCommandByJuceId(c->juceCommandId) == c);
        REQUIRE(juce::String(c->name).isNotEmpty());
        REQUIRE(juce::String(c->id).startsWith("transport."));
    }
}
