#pragma once

#include "AppContext.h"
#include "EditorWindow.h"
#include "PluginCard.h"
#include "session/NodeId.h"

#include "session/Patch.h"

#include <juce_gui_basics/juce_gui_basics.h>

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
// Cable rendering:
// ----------------
// `paint()` draws one stroke per connection (from the cached `connections_`
// vector + live card positions) so each cable can carry its own amber->teal
// gradient (audio), cyan (MIDI), dimming (bypassed source), or selection
// highlight. Derived from `connections_` (arbitrary topology), not chain
// adjacency.
class SignalCanvas : public juce::Component, private juce::ChangeListener
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

    // Port drag-to-connect / cable selection live on the canvas (cards delegate
    // their port pixels here via hitTest).
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

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
    void onCardMoved(NodeId, juce::Point<int> newPos);
    void onCardDragging(NodeId);
    // MIDI learn (M2 U4): opens a parameter picker, then arms ControlHub;
    // clicking again on the already-armed card cancels it.
    void onCardMidiLearn(NodeId);
    // Clears midiLearnArmedNode_ and its card's visual, if any is armed.
    void cancelMidiLearnIfArmed();

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

    juce::Rectangle<int> inputCardBounds_;
    juce::Rectangle<int> outputCardBounds_;

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

    // MIDI learn (M2 U4): at most one card armed at a time, mirroring
    // ControlHub::isLearning()'s single-slot policy.
    std::optional<NodeId> midiLearnArmedNode_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalCanvas)
};

} // namespace pedalboard3::ui
