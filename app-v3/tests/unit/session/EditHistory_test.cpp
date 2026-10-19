// Tests for pedalboard3::session::EditHistory — undo / redo of canvas edits
// with exact inverses (finish pass U7, plan
// docs/plans/2026-09-11-003-feat-finish-pass-core-plan.md, KD5). Uses the
// production Session builder with built-in nodes (LevelProcessor,
// VuMeterProcessor), which instantiate without any scanned plugin. Session's
// change messages are delivered with sendSynchronousChangeMessage (no message
// loop here), which is how the history sees commits and swaps.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/AppCommands.h"
#include "session/BankDocument.h"
#include "session/EditHistory.h"
#include "session/Session.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

using namespace pedalboard3;

namespace
{

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

struct Fixture
{
    engine::AudioEngine eng;
    engine::PluginHost host;
    session::Session s{eng, host}; // production BuildFn: built-in nodes build for real
    session::EditHistory h{s};

    Fixture() { eng.initialise(); }

    // Let the worker land, then tell the history about it.
    void settle()
    {
        pumpUntilIdle(s);
        s.sendSynchronousChangeMessage();
    }

    // Add a built-in through the history; returns the new node's id.
    session::NodeId add(const char* type = "LevelProcessor")
    {
        const auto before = s.getNodeIdsInOrder();
        h.addInternalNode(type);
        settle();
        const auto after = s.getNodeIdsInOrder();
        REQUIRE(after.size() == before.size() + 1);
        for (auto id : after)
            if (std::find(before.begin(), before.end(), id) == before.end())
                return id;
        return session::graphIO;
    }

    bool connected(const session::PatchConnection& c) const
    {
        const auto conns = s.getActivePatch().connections;
        return std::find(conns.begin(), conns.end(), c) != conns.end();
    }
};

} // namespace

TEST_CASE("EditHistory: undoing a remove restores the node, its id, place, name, bypass, channel and wiring",
          "[session][edit-history]")
{
    Fixture f;
    const auto a = f.add();
    const auto b = f.add();
    const auto c = f.add();
    f.s.setNodeCustomName(b, "Mid");
    f.s.setNodeBypass(b, true);
    f.s.setNodeMidiChannel(b, 5);
    const auto before = f.s.getActivePatch();

    f.h.removeNode(b);
    f.settle();
    REQUIRE_FALSE(f.s.hasNode(b));
    REQUIRE(f.s.getActivePatch().connections != before.connections); // Session bridged a -> c

    REQUIRE(f.h.undo());
    f.settle();
    const auto after = f.s.getActivePatch();
    REQUIRE(f.s.hasNode(b));
    REQUIRE(f.s.getNodeIdsInOrder() == std::vector<session::NodeId>{a, b, c});
    REQUIRE(after.findNode(b)->customName == "Mid");
    REQUIRE(after.findNode(b)->midiChannel == 5);
    REQUIRE(f.s.isNodeBypassed(b));
    REQUIRE(after.connections == before.connections); // the original wiring, not the bridge

    // Redo removes it again; the restore was not recorded as an add.
    REQUIRE(f.h.undoDepth() == 3);
    REQUIRE(f.h.redo());
    f.settle();
    REQUIRE_FALSE(f.s.hasNode(b));
}

TEST_CASE("EditHistory: undoing an add removes the node and puts back the wiring from before; redo re-adds it",
          "[session][edit-history]")
{
    Fixture f;
    const auto a = f.add();
    const auto wiring = f.s.getActivePatch().connections;

    const auto b = f.add("VuMeterProcessor");
    REQUIRE(f.h.undoDepth() == 2);

    REQUIRE(f.h.undo());
    f.settle();
    REQUIRE_FALSE(f.s.hasNode(b));
    REQUIRE(f.s.hasNode(a));
    REQUIRE(f.s.getActivePatch().connections == wiring);

    REQUIRE(f.h.redo());
    f.settle();
    REQUIRE(f.s.hasNode(b));
    REQUIRE(f.s.getNodeCount() == 2);
    REQUIRE(f.h.undoDepth() == 2); // the redo's restore is not a new add
}

TEST_CASE("EditHistory: undoing the only add leaves the board as it was before", "[session][edit-history]")
{
    Fixture f;
    const auto wiring = f.s.getActivePatch().connections;
    f.add();
    REQUIRE(f.h.undo());
    f.settle();
    REQUIRE(f.s.getNodeCount() == 0);
    REQUIRE(f.s.getActivePatch().connections == wiring);
}

TEST_CASE("EditHistory: connect, disconnect and rename round-trip through undo and redo", "[session][edit-history]")
{
    Fixture f;
    const auto a = f.add();
    const auto b = f.add();
    const session::PatchConnection ab{a, 0, b, 0};
    REQUIRE(f.connected(ab)); // the second add spliced in after the first

    f.h.removeConnection(a, 0, b, 0);
    REQUIRE_FALSE(f.connected(ab));
    REQUIRE(f.h.undo());
    REQUIRE(f.connected(ab));
    REQUIRE(f.h.redo());
    REQUIRE_FALSE(f.connected(ab));

    REQUIRE(f.h.addConnection(a, 0, b, 0));
    REQUIRE(f.connected(ab));
    REQUIRE(f.h.undo());
    REQUIRE_FALSE(f.connected(ab));
    REQUIRE(f.h.redo());
    REQUIRE(f.connected(ab));

    f.h.setNodeCustomName(a, "Drive");
    REQUIRE(f.s.getActivePatch().findNode(a)->customName == "Drive");
    REQUIRE(f.h.undo());
    REQUIRE(f.s.getActivePatch().findNode(a)->customName.isEmpty());
    REQUIRE(f.h.redo());
    REQUIRE(f.s.getActivePatch().findNode(a)->customName == "Drive");
}

TEST_CASE("EditHistory: a new edit clears redo, a patch swap clears both, and depth is capped",
          "[session][edit-history]")
{
    Fixture f;
    const auto a = f.add();
    f.h.setNodeCustomName(a, "One");
    REQUIRE(f.h.undo());
    REQUIRE(f.h.redoDepth() == 1);
    f.h.setNodeCustomName(a, "Two");
    REQUIRE(f.h.redoDepth() == 0);

    for (int i = 0; i < 40; ++i)
        f.h.setNodeCustomName(a, "Name " + juce::String(i));
    REQUIRE(f.h.undoDepth() == session::EditHistory::kMaxDepth);

    f.s.replacePatch(session::Patch{});
    f.settle();
    REQUIRE(f.h.undoDepth() == 0);
    REQUIRE(f.h.redoDepth() == 0);
    REQUIRE_FALSE(f.h.canUndo());
}

TEST_CASE("EditHistory: the UI hook runs before every removal the history makes", "[session][edit-history]")
{
    Fixture f;
    std::vector<session::NodeId> closed;
    f.h.beforeNodeRemoved = [&closed](session::NodeId id) { closed.push_back(id); };

    const auto a = f.add();
    const auto b = f.add();
    f.h.removeNode(a); // a removal
    f.settle();
    REQUIRE(f.h.undo()); // puts a back: no removal
    f.settle();
    REQUIRE(f.h.undo()); // undoes the add of b: a removal
    f.settle();
    REQUIRE(closed == std::vector<session::NodeId>{a, b});
}

TEST_CASE("EditHistory: edit.undo and edit.redo run through the app commands, not while capture is suspended",
          "[session][edit-history][app-commands]")
{
    Fixture f;
    session::BankDocument doc(f.s);
    doc.setLiveCaptureEnabled(true);
    const auto a = f.add();
    f.h.setNodeCustomName(a, "Drive");

    REQUIRE(session::invokeAppCommand(doc, session::appcommands::kUndo, &f.h));
    REQUIRE(f.s.getActivePatch().findNode(a)->customName.isEmpty());
    REQUIRE_FALSE(session::invokeAppCommand(doc, session::appcommands::kRedo)); // no history given

    doc.setLiveCaptureEnabled(false); // the live graph is not the slot's
    REQUIRE_FALSE(session::invokeAppCommand(doc, session::appcommands::kRedo, &f.h));
    doc.setLiveCaptureEnabled(true);
    REQUIRE(session::invokeAppCommand(doc, session::appcommands::kRedo, &f.h));
    REQUIRE(f.s.getActivePatch().findNode(a)->customName == "Drive");
}

TEST_CASE("EditHistory: an add at a position commits there, and undo removes it", "[session][edit-history]")
{
    Fixture f;
    f.h.addInternalNode("LevelProcessor", juce::Point<float>{320.0f, 180.0f});
    f.settle();
    REQUIRE(f.s.getNodeCount() == 1);
    const auto node = f.s.getActivePatch().nodes.front();
    REQUIRE(node.x == 320.0f);
    REQUIRE(node.y == 180.0f);

    REQUIRE(f.h.undo());
    f.settle();
    REQUIRE(f.s.getNodeCount() == 0);
}

TEST_CASE("EditHistory: a node's note undoes and redoes, and an unchanged note records nothing",
          "[session][edit-history][note]")
{
    // The note is a thing you type, so it is a thing you can mistype (#124).
    // Same treatment as a rename.
    Fixture f;
    const auto a = f.add();

    f.h.setNodeNote(a, "second verse only");
    REQUIRE(f.s.getActivePatch().findNode(a)->note == "second verse only");

    // Setting the same text again is not an edit, so it must not put a
    // no-op on the stack for the user to undo past.
    const auto depth = f.h.undoDepth();
    f.h.setNodeNote(a, "second verse only");
    REQUIRE(f.h.undoDepth() == depth);

    REQUIRE(f.h.undo());
    REQUIRE(f.s.getActivePatch().findNode(a)->note.isEmpty());
    REQUIRE(f.h.redo());
    REQUIRE(f.s.getActivePatch().findNode(a)->note == "second verse only");

    // And clearing it is itself undoable.
    f.h.setNodeNote(a, {});
    REQUIRE(f.s.getActivePatch().findNode(a)->note.isEmpty());
    REQUIRE(f.h.undo());
    REQUIRE(f.s.getActivePatch().findNode(a)->note == "second verse only");
}

TEST_CASE("EditHistory: the note and the custom name are independent", "[session][edit-history][note]")
{
    // They answer different questions -- what the thing IS versus why it is
    // on the board -- so undoing one must not disturb the other.
    Fixture f;
    const auto a = f.add();

    f.h.setNodeCustomName(a, "Punch");
    f.h.setNodeNote(a, "borrowed from the Tuesday board");

    REQUIRE(f.h.undo()); // the note
    REQUIRE(f.s.getActivePatch().findNode(a)->note.isEmpty());
    REQUIRE(f.s.getActivePatch().findNode(a)->customName == "Punch");

    REQUIRE(f.h.undo()); // the name
    REQUIRE(f.s.getActivePatch().findNode(a)->customName.isEmpty());
}
