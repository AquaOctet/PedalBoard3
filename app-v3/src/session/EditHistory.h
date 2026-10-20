#pragma once

#include "NodeId.h"
#include "Patch.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace pedalboard3::session
{

class Session;

// Undo / redo for canvas edits (finish pass U7, plan
// docs/plans/2026-09-11-003-feat-finish-pass-core-plan.md, KD5): a bounded
// history of structural edits, each with an exact inverse.
//
// The undoable edits go through here instead of straight to Session: adding a
// plugin or built-in, removing a node, connecting, disconnecting and renaming.
// A removal captures the node with its plugin state and the connections before
// it; undo puts both back exactly (Session::restoreNode), not the bridge
// Session made across the gap. An add is recorded when it commits, with the
// connections from before it. Both stacks clear on a whole-patch swap (a bank
// switch, Open, New); a new edit clears redo; depth is capped at
// kMaxDepth.
//
// Undo and redo need Session Idle (canUndo / canRedo say so). Putting a node
// back goes through the add worker, so it lands asynchronously.
//
// Threading: message thread only. Broadcasts when either stack changes.
class EditHistory : public juce::ChangeBroadcaster, private juce::ChangeListener
{
public:
    static constexpr std::size_t kMaxDepth = 32;

    explicit EditHistory(Session& session);
    ~EditHistory() override;

    // Called with a node's id right before every removal this history makes (a
    // remove, undoing an add, redoing a remove). The UI binds it to closing
    // that node's editor windows, which must go before the instance does.
    std::function<void(NodeId)> beforeNodeRemoved;

    // The undoable edits: each forwards to Session and records its inverse.
    void addPlugin(juce::PluginDescription desc, std::optional<juce::Point<float>> position = std::nullopt);
    void addInternalNode(juce::String internalType,
                         std::optional<juce::Point<float>> position = std::nullopt,
                         juce::MemoryBlock initialState = {});
    void removeNode(NodeId id);
    bool addConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel);
    void removeConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel);
    void setNodeCustomName(NodeId id, juce::String name);
    // The node's note (#124), undoable the same way a rename is: it is a
    // thing you type, so it is a thing you can mistype.
    void setNodeNote(NodeId id, juce::String note);

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    bool undo(); // false when there is nothing to undo or Session is busy
    bool redo();
    void clear();

    std::size_t undoDepth() const noexcept { return undo_.size(); }
    std::size_t redoDepth() const noexcept { return redo_.size(); }

private:
    enum class Kind
    {
        Add,
        Remove,
        Connect,
        Disconnect,
        Rename,
        Note
    };

    struct Edit
    {
        Kind kind{Kind::Add};
        PatchNode node; // Add / Remove: the node, with its plugin state; Rename: only its id
        int index{-1};  // Add / Remove: its place in the node list
        std::vector<PatchConnection> connectionsBefore; // Add / Remove: the wiring before the edit
        std::vector<PatchConnection> connectionsAfter;  // Add: the wiring after it, for redo
        PatchConnection connection{};                   // Connect / Disconnect
        juce::String nameBefore;                        // Rename / Note
        juce::String nameAfter;
    };

    // Session changed: clear on a whole-patch swap, record committed adds.
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void push(Edit edit); // onto undo, clearing redo
    std::optional<Edit> captureRemoval(NodeId id) const;
    void removeNow(NodeId id); // beforeNodeRemoved, then Session::removeNode

    Session& session_;
    std::deque<Edit> undo_;
    std::deque<Edit> redo_;
    std::uint64_t seenGeneration_{0};
    std::uint64_t seenAddSequence_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditHistory)
};

} // namespace pedalboard3::session
