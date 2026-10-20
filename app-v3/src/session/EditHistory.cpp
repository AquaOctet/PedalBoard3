#include "EditHistory.h"

#include "Session.h"

#include <algorithm>
#include <utility>

namespace pedalboard3::session
{

namespace
{
bool contains(const std::vector<PatchConnection>& v, const PatchConnection& c)
{
    return std::find(v.begin(), v.end(), c) != v.end();
}
} // namespace

EditHistory::EditHistory(Session& session)
    : session_(session), seenGeneration_(session.getPatchGeneration()),
      seenAddSequence_(session.getLastCommittedAddSequence())
{
    session_.addChangeListener(this);
}

EditHistory::~EditHistory()
{
    session_.removeChangeListener(this);
}

void EditHistory::addPlugin(juce::PluginDescription desc, std::optional<juce::Point<float>> position)
{
    session_.addPlugin(std::move(desc), position); // recorded when it commits
}

void EditHistory::addInternalNode(juce::String internalType,
                                  std::optional<juce::Point<float>> position,
                                  juce::MemoryBlock initialState)
{
    session_.addInternalNode(std::move(internalType), position, std::move(initialState)); // recorded when it commits
}

void EditHistory::removeNode(NodeId id)
{
    auto edit = captureRemoval(id);
    if (!edit.has_value())
        return;
    removeNow(id);
    if (session_.hasNode(id))
        return; // Session refused (the live graph isn't its own)
    push(std::move(*edit));
}

bool EditHistory::addConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel)
{
    if (!session_.addConnection(src, srcChannel, dst, dstChannel))
        return false;
    Edit edit;
    edit.kind = Kind::Connect;
    edit.connection = {src, srcChannel, dst, dstChannel};
    push(std::move(edit));
    return true;
}

void EditHistory::removeConnection(NodeId src, int srcChannel, NodeId dst, int dstChannel)
{
    const PatchConnection c{src, srcChannel, dst, dstChannel};
    if (!contains(session_.getActivePatch().connections, c))
        return;
    session_.removeConnection(src, srcChannel, dst, dstChannel);
    if (contains(session_.getActivePatch().connections, c))
        return; // refused (Session not Idle)
    Edit edit;
    edit.kind = Kind::Disconnect;
    edit.connection = c;
    push(std::move(edit));
}

void EditHistory::setNodeNote(NodeId id, juce::String note)
{
    const auto patch = session_.getActivePatch();
    const auto* node = patch.findNode(id);
    if (node == nullptr || node->note == note)
        return;
    Edit edit;
    edit.kind = Kind::Note;
    edit.node.id = id;
    edit.nameBefore = node->note;
    edit.nameAfter = note;
    session_.setNodeNote(id, std::move(note));
    push(std::move(edit));
}

void EditHistory::setNodeCustomName(NodeId id, juce::String name)
{
    const auto patch = session_.getActivePatch();
    const auto* node = patch.findNode(id);
    if (node == nullptr || node->customName == name)
        return;
    Edit edit;
    edit.kind = Kind::Rename;
    edit.node.id = id;
    edit.nameBefore = node->customName;
    edit.nameAfter = name;
    session_.setNodeCustomName(id, std::move(name));
    push(std::move(edit));
}

bool EditHistory::canUndo() const noexcept
{
    return !undo_.empty() && session_.getState() == Session::State::Idle;
}

bool EditHistory::canRedo() const noexcept
{
    return !redo_.empty() && session_.getState() == Session::State::Idle;
}

bool EditHistory::undo()
{
    if (!canUndo())
        return false;
    auto edit = std::move(undo_.back());
    undo_.pop_back();

    const auto& c = edit.connection;
    switch (edit.kind)
    {
    case Kind::Add:
        if (auto removal = captureRemoval(edit.node.id))
        {
            // Keep the node as it is now, with its plugin state, for redo.
            edit.node = std::move(removal->node);
            edit.index = removal->index;
            edit.connectionsAfter = std::move(removal->connectionsBefore);
            removeNow(edit.node.id);
            if (session_.hasNode(edit.node.id))
            {
                undo_.push_back(std::move(edit)); // refused; nothing changed
                return false;
            }
            // Session bridged across the gap; put back the wiring from before
            // the add.
            session_.setConnections(edit.connectionsBefore);
            redo_.push_back(std::move(edit));
        }
        break;
    case Kind::Remove:
        session_.restoreNode(edit.node, edit.index, edit.connectionsBefore);
        redo_.push_back(std::move(edit));
        break;
    case Kind::Connect:
        session_.removeConnection(c.src, c.srcChannel, c.dst, c.dstChannel);
        redo_.push_back(std::move(edit));
        break;
    case Kind::Disconnect:
        if (session_.addConnection(c.src, c.srcChannel, c.dst, c.dstChannel))
            redo_.push_back(std::move(edit));
        break;
    case Kind::Rename:
        session_.setNodeCustomName(edit.node.id, edit.nameBefore);
        redo_.push_back(std::move(edit));
        break;
    case Kind::Note:
        session_.setNodeNote(edit.node.id, edit.nameBefore);
        redo_.push_back(std::move(edit));
        break;
    }
    sendChangeMessage();
    return true;
}

bool EditHistory::redo()
{
    if (!canRedo())
        return false;
    auto edit = std::move(redo_.back());
    redo_.pop_back();

    const auto& c = edit.connection;
    switch (edit.kind)
    {
    case Kind::Add:
        // The restore's commit is not recorded as a new add; this entry is it.
        session_.restoreNode(edit.node, edit.index, edit.connectionsAfter);
        undo_.push_back(std::move(edit));
        break;
    case Kind::Remove:
        if (auto removal = captureRemoval(edit.node.id))
        {
            removeNow(edit.node.id);
            if (!session_.hasNode(edit.node.id))
                undo_.push_back(std::move(*removal));
        }
        break;
    case Kind::Connect:
        if (session_.addConnection(c.src, c.srcChannel, c.dst, c.dstChannel))
            undo_.push_back(std::move(edit));
        break;
    case Kind::Disconnect:
        session_.removeConnection(c.src, c.srcChannel, c.dst, c.dstChannel);
        undo_.push_back(std::move(edit));
        break;
    case Kind::Rename:
        session_.setNodeCustomName(edit.node.id, edit.nameAfter);
        undo_.push_back(std::move(edit));
        break;
    case Kind::Note:
        session_.setNodeNote(edit.node.id, edit.nameAfter);
        undo_.push_back(std::move(edit));
        break;
    }
    sendChangeMessage();
    return true;
}

void EditHistory::clear()
{
    if (undo_.empty() && redo_.empty())
        return;
    undo_.clear();
    redo_.clear();
    sendChangeMessage();
}

void EditHistory::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // A whole-patch swap replaced every node: nothing in either stack applies.
    if (const auto generation = session_.getPatchGeneration(); generation != seenGeneration_)
    {
        seenGeneration_ = generation;
        seenAddSequence_ = session_.getLastCommittedAddSequence();
        clear();
        return;
    }

    // Record adds as they commit. A restore is an undo / redo landing, which
    // is already on a stack.
    for (auto& add : session_.getCommittedAddsSince(seenAddSequence_))
    {
        seenAddSequence_ = add.sequence;
        if (add.restored)
            continue;
        Edit edit;
        edit.kind = Kind::Add;
        edit.node.id = add.id;
        edit.connectionsBefore = std::move(add.connectionsBefore);
        push(std::move(edit));
    }
}

void EditHistory::push(Edit edit)
{
    undo_.push_back(std::move(edit));
    if (undo_.size() > kMaxDepth)
        undo_.pop_front();
    redo_.clear();
    sendChangeMessage();
}

std::optional<EditHistory::Edit> EditHistory::captureRemoval(NodeId id) const
{
    if (session_.getState() != Session::State::Idle)
        return std::nullopt;
    auto node = session_.getNodeWithState(id);
    if (!node.has_value())
        return std::nullopt;

    const auto patch = session_.getActivePatch();
    Edit edit;
    edit.kind = Kind::Remove;
    edit.index = patch.indexOfNode(id);
    edit.connectionsBefore = patch.connections;
    edit.node = std::move(*node);
    return edit;
}

void EditHistory::removeNow(NodeId id)
{
    if (beforeNodeRemoved)
        beforeNodeRemoved(id);
    session_.removeNode(id);
}

} // namespace pedalboard3::session
