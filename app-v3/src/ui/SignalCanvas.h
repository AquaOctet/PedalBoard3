#pragma once

#include "AppContext.h"
#include "EditorWindow.h"
#include "MappingsDialog.h"
#include "MeterMath.h"
#include "PluginCard.h"
#include "session/NodeId.h"

#include "session/Patch.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pedalboard3::ui
{

// SignalCanvas — read-only signal-flow view for Path γ.
//
// Renders the active patch as a free 2D graph of cards (virtual "Input"
// endpoint → user plugin cards → virtual "Output" endpoint), with bezier
// audio + MIDI cables drawn between ports. Cards are draggable: each carries a
// persisted (x, y); an unpositioned card auto-lays-out left-to-right until
// dragged. Ports are still drawn-only here — port drag-to-connect lands in the
// next unit (U4); zoom/pan follows.
//
// Threading: message thread only.
//
// Listener strategy (D1, D1b):
// ----------------------------
// SignalCanvas listens on `Session` (which is a juce::ChangeBroadcaster).
// On each callback: build oldIds/newIds sets keyed by NodeId, reuse
// existing PluginCards by NodeId so visual state survives reorder/
// bypass, drop departed cards, append new cards, reorder vector to
// match snapshot order, then re-layout.
//
// EditorWindow lifecycle is CALLER-MANAGED, NOT listener-managed:
// `Session::removeNode` and `Session::commitLoadPatch` destroy the
// underlying `BypassableInstance` synchronously inline BEFORE firing
// the async change message. By the time the listener runs, the
// editor's `AudioProcessor*` is dangling. Therefore the canvas
// closes `editors_[id]` synchronously, on the message thread, BEFORE
// invoking the corresponding Session mutator. Listener-side cleanup
// is defensive only (catches restore-on-launch and other UI-bypassing
// paths).
//
// Layout:
// -------
// Cards keep their saved (x, y) in model coordinates and draw at model + a view
// offset. The Input / Output endpoints follow the cards (one gap either side,
// vertically centred on them), and the whole group is centred in the visible
// area, re-centring when the window resizes or cards come and go
// (ui/CanvasLayout.h).
//
// Cable rendering:
// ----------------
// `paint()` draws one stroke per connection (from the cached `connections_`
// vector + live card positions) so each cable can carry its own amber->teal
// gradient (audio), cyan (MIDI), dimming (bypassed source), or selection
// highlight. Derived from `connections_` (arbitrary topology), not chain
// adjacency.
class SignalCanvas : public juce::Component,
                     public juce::DragAndDropTarget,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    using NodeId = pedalboard3::session::NodeId;

    explicit SignalCanvas(AppContext& ctx);
    ~SignalCanvas() override;

    // Public API the TransportBar calls before invoking any Session
    // mutator that destroys plugin instances (e.g. clearPatch from
    // Test Tone). Closes every EditorWindow synchronously, on the
    // message thread, BEFORE the caller calls into Session.
    void closeAllEditors();

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;

    // Port drag-to-connect / cable selection live on the canvas (cards delegate
    // their port pixels here via hitTest).
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Drops from the plugin browser (UI refresh U5): the plugin is added where
    // it lands, its card centred on the pointer.
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    // An audio file dropped on the canvas adds a File Player with it, where it
    // lands (M6 U6, AC-FP-6). A .pdl goes on up to the window, which opens it.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // The selected card (UI refresh U7): set by a click on a card, cleared by a
    // click on empty canvas. The board mappings panel (U9) highlights its rows.
    std::optional<NodeId> selectedNode() const noexcept { return selectedNode_; }

    // Select a card from outside the canvas (a row in the board mappings panel)
    // or clear the selection. onSelectionChanged fires on every change, from
    // either side.
    void selectNode(std::optional<NodeId> id) { setSelectedNode(id); }
    std::function<void(std::optional<NodeId>)> onSelectionChanged;

    // The per-plugin mappings dialog (M4 U4): at most one open; opening another
    // node's replaces it. The board mappings panel's Learn... opens it too.
    void openMappings(NodeId);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override; // the cards' activity strips and VU meters
    void setSelectedNode(std::optional<NodeId> id);

    void layoutCards();
    // Stroke one cable (gradient for audio, cyan for MIDI; dashed when its
    // source node is bypassed; highlighted when selected).
    void strokeCable(
        juce::Graphics&, juce::Point<int> from, juce::Point<int> to, bool midi, bool bypassed, bool selected) const;

    std::unique_ptr<PluginCard> makeCard(const PluginCard::NodeSnapshot&);

    // Card-side delegates wired through PluginCard::Callbacks.
    void onCardBypassToggle(NodeId);
    void onCardEdit(NodeId);
    void onCardRemove(NodeId);
    void onCardRearm(NodeId); // a dead plugin's card was clicked (M5 U5)
    // Close one node's editor and mappings windows, before it goes away.
    void closeEditorsFor(NodeId);
    void onCardMoved(NodeId, juce::Point<int> newPos);
    void onCardDragging(NodeId);
    // Destroys the mappings dialog, cancelling any learn it armed.
    void closeMappings();

    // Map a graphIO sentinel to either the input or output endpoint
    // bounds; map any other NodeId to its card's index in cards_.
    bool isGraphIO(NodeId id) const noexcept;
    juce::Point<int> portPositionFor(NodeId, int channel, bool asSource) const;

    // A single pin on a node or the graphIO endpoints. `isSource` = it emits
    // signal (a node output, or the Input endpoint); a connection is always
    // source -> dest.
    struct PortRef
    {
        NodeId node{session::graphIO};
        int channel{0};
        bool isSource{false};
        bool valid{false};
    };
    PortRef portAt(juce::Point<int>) const;
    std::optional<session::PatchConnection> cableAt(juce::Point<int>) const;
    // The connection feeding a destination port (for grab-to-detach), if any.
    std::optional<session::PatchConnection> connectionInto(const PortRef&) const;

    AppContext& ctx_;

    std::vector<std::unique_ptr<PluginCard>> cards_;
    std::unordered_map<NodeId, std::size_t> idToIndex_;
    std::unordered_map<NodeId, std::unique_ptr<EditorWindow>> editors_;

    // Last Session patch generation seen; a change means a whole-patch swap
    // replaced every processor, so all editors close (ids may be reused).
    std::uint64_t lastPatchGeneration_{0};

    // BypassableInstance::deathCount() when the cards last looked (M5 U5).
    std::uint64_t lastDeathCount_{0};

    juce::Rectangle<int> inputCardBounds_;
    juce::Rectangle<int> outputCardBounds_;

    // The view offset (canvas position = saved card position + offset) and
    // what it was computed for; see layoutCards.
    std::optional<juce::Point<int>> viewOffset_;
    juce::Point<int> lastViewport_;
    std::vector<NodeId> lastCardIds_;

    // Cached on each Session change so paint() (which redraws cables per
    // drag-frame to keep them glued to a moving card) never re-copies the whole
    // patch. bypassedNodes_ drives dashed cables for bypassed sources.
    std::vector<session::PatchConnection> connections_;
    std::unordered_set<NodeId> bypassedNodes_;

    // Interaction state (U4).
    bool dragging_{false};
    PortRef dragAnchor_;                                    // the fixed end of a connection drag
    juce::Point<int> dragCursor_;                           // current cursor in canvas space
    std::optional<session::PatchConnection> selectedCable_; // for click-select + Delete

    // The per-plugin mappings dialog (M4 U4); it owns MIDI learn.
    std::unique_ptr<MappingsWindow> mappingsWindow_;

    // The selected card, and each card's activity blink (UI refresh U7).
    std::optional<NodeId> selectedNode_;
    std::unordered_map<NodeId, meter::ActivityBlink> activityBlinks_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalCanvas)
};

} // namespace pedalboard3::ui
