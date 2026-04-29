#pragma once

#include "AppContext.h"
#include "EditorWindow.h"
#include "PluginCard.h"
#include "session/NodeId.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace pedalboard3::ui
{

// SignalCanvas — read-only signal-flow view for Path γ.
//
// Renders the active patch as a left-to-right sequence of cards
// (virtual "Input" endpoint → user plugin cards → virtual "Output"
// endpoint), with bezier audio + MIDI cables drawn between adjacent
// cards' ports. Path γ ships read-only: ports are drawn but not
// click-to-connect; cards have per-card buttons but are not draggable.
// Phase 3 grows this into the full editor (drag-rearrange, port-click
// connections, lasso, zoom).
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
// Cable derivation (D2, D3):
// --------------------------
// `rebuildCables()` runs once per `layoutCards()` (which runs from
// the listener and from `resized()`), builds two cached
// `juce::Path` members (audioCablesPath_, midiCablesPath_), and
// `paint()` makes 2 stroke calls per material regardless of N. The
// paths are derived from `activePatch_.connections` (not from chain
// adjacency) so Phase 3's arbitrary-topology canvas reuses the same
// rendering.
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

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void layoutCards();
    void rebuildCables();

    std::unique_ptr<PluginCard> makeCard(const PluginCard::NodeSnapshot&);

    // Card-side delegates wired through PluginCard::Callbacks.
    void onCardBypassToggle(NodeId);
    void onCardEdit(NodeId);
    void onCardMoveUp(NodeId);
    void onCardMoveDown(NodeId);
    void onCardRemove(NodeId);

    // Map a graphIO sentinel to either the input or output endpoint
    // bounds; map any other NodeId to its card's index in cards_.
    bool isGraphIO(NodeId id) const noexcept;
    juce::Point<int> portPositionFor(NodeId, int channel, bool asSource) const;

    AppContext& ctx_;

    std::vector<std::unique_ptr<PluginCard>> cards_;
    std::unordered_map<NodeId, std::size_t> idToIndex_;
    std::unordered_map<NodeId, std::unique_ptr<EditorWindow>> editors_;

    juce::Rectangle<int> inputCardBounds_;
    juce::Rectangle<int> outputCardBounds_;

    juce::Path audioCablesPath_;
    juce::Path midiCablesPath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalCanvas)
};

} // namespace pedalboard3::ui
