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
        // Fired continuously while dragging so the canvas can re-stroke cables
        // live; onMoved fires once on release with the final top-left.
        std::function<void(NodeId)> onDragging;
        std::function<void(NodeId, juce::Point<int>)> onMoved;
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

    // Port centre in card-local coordinates. SignalCanvas adds
    // getPosition() to convert into canvas-global coordinates.
    // `channel`: 0 = L, 1 = R, `juce::AudioProcessorGraph::midiChannelIndex`
    // (0x1000) = MIDI.
    juce::Point<int> getPortCentre(bool isInput, int channel) const noexcept;

    // Stored canvas position from the snapshot (NaN = unpositioned).
    bool hasStoredPosition() const noexcept { return snapshot_.hasPosition(); }
    juce::Point<int> storedPosition() const noexcept
    {
        return {static_cast<int>(snapshot_.x), static_cast<int>(snapshot_.y)};
    }

    void paint(juce::Graphics&) override;
    void resized() override;

    // Port pixels fall through to SignalCanvas (which owns drag-to-connect);
    // the rest of the card stays interactive (body drag + buttons).
    bool hitTest(int x, int y) override;

    // Drag-to-move on the card body (buttons consume their own clicks).
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    NodeSnapshot snapshot_;
    Callbacks callbacks_;
    juce::Colour accent_{tokens::accentPrimary};

    juce::ComponentDragger dragger_;
    bool didDrag_{false};

    juce::Label nameLabel_;
    juce::TextButton bypassButton_{"BYP"};
    juce::TextButton editButton_{"Edit"};
    juce::TextButton removeButton_{"X"};
    juce::TextButton moveUpButton_{"Up"};
    juce::TextButton moveDownButton_{"Dn"};

    void refreshFromSnapshot();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCard)
};

} // namespace pedalboard3::ui
