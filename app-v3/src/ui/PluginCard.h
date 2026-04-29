#pragma once

#include "ColourScheme.h"
#include "session/NodeId.h"
#include "session/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace pedalboard3::ui
{

// One PluginCard renders a single PatchNode in the SignalCanvas:
// title row (name + Bypass/Edit/Remove), input/output ports (drawn but
// non-interactive in Path γ — Phase 3 wires drag-to-connect), MIDI
// activity placeholder, and a Move ↑/↓ row.
//
// Threading: message thread only. SignalCanvas drives every state
// change via setSnapshot() in response to Session change messages.
//
// Ownership: SignalCanvas owns std::vector<unique_ptr<PluginCard>>;
// the canvas closes the corresponding EditorWindow synchronously
// before invoking Session::removeNode (per AC-UI-P).
//
// CardCallbacks is set at construction; the architecture review's
// "audit at construction" guidance — passing the whole bundle keeps
// 'forgot to wire one' obvious in the call site.
class PluginCard : public juce::Component
{
public:
    using NodeId = pedalboard3::session::NodeId;
    using NodeSnapshot = pedalboard3::session::Session::NodeSnapshot;

    struct Callbacks
    {
        std::function<void(NodeId)> onBypassToggle;
        std::function<void(NodeId)> onEdit;
        std::function<void(NodeId)> onMoveUp;
        std::function<void(NodeId)> onMoveDown;
        std::function<void(NodeId)> onRemove;
    };

    PluginCard(NodeSnapshot initialSnapshot, Callbacks callbacks);
    ~PluginCard() override = default;

    NodeId getNodeId() const noexcept { return snapshot_.id; }

    // SignalCanvas calls this from changeListenerCallback when the
    // chain's snapshot vector reports a change for this NodeId. Cheap
    // path: no Component destruction; just relabels and repaints.
    void setSnapshot(const NodeSnapshot&);

    // Left-edge accent colour driven by canvas (instrument vs effect).
    void setAccentColour(juce::Colour);

    // Move-row buttons disabled at chain endpoints. Canvas knows the
    // index, so it tells us.
    void setMovableUp(bool);
    void setMovableDown(bool);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    NodeSnapshot snapshot_;
    Callbacks callbacks_;
    juce::Colour accent_ {tokens::accentPrimary};

    juce::Label nameLabel_;
    juce::TextButton bypassButton_ {"BYP"};
    juce::TextButton editButton_   {"Edit"};
    juce::TextButton removeButton_ {"X"};
    juce::TextButton moveUpButton_   {"Up"};
    juce::TextButton moveDownButton_ {"Dn"};

    void refreshFromSnapshot();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCard)
};

} // namespace pedalboard3::ui
