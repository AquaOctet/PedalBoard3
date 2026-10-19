#pragma once

#include "ColourScheme.h"
#include "IconButton.h"
#include "session/NodeId.h"
#include "session/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace pedalboard3::ui
{

// One PluginCard renders a single PatchNode on the SignalCanvas, as the
// mockup's 170 x 92 card (UI refresh U7):
//   - a header with a drag grip, the name, and BYP / M / ED. Remove shows only
//     on hover, so it can't be hit by accident mid-set.
//   - a body with the plugin's category, then its format with the vendor or its
//     mapping count (ui/CardText.h). A VU Meter card shows its live meter.
//   - ports as edge dots; the canvas owns their drag-to-connect via hitTest.
//   - a bottom strip that lights cyan when a mapped control moves the plugin.
// A bypassed card dims and strikes its name; a selected card has a teal edge.
// Double-clicking the name edits it in place (U8): Enter keeps it, Escape
// cancels, and an empty name reverts to the plugin's own. The body is
// draggable to reposition.
//
// Threading: message thread only. SignalCanvas drives every state change:
// setSnapshot on Session change messages, activity and meter levels from its
// timer.
//
// Ownership: SignalCanvas owns the cards and closes the corresponding
// EditorWindow synchronously before a node goes (AC-UI-P).
class PluginCard : public juce::Component
{
public:
    using NodeId = pedalboard3::session::NodeId;
    using NodeSnapshot = pedalboard3::session::Session::NodeSnapshot;

    struct Callbacks
    {
        std::function<void(NodeId)> onBypassToggle;
        std::function<void(NodeId)> onEdit;
        std::function<void(NodeId)> onRemove;
        // Fired continuously while dragging so the canvas can re-stroke cables
        // live; onMoved fires once on release with the final top-left.
        std::function<void(NodeId)> onDragging;
        std::function<void(NodeId, juce::Point<int>)> onMoved;
        // Opens the per-plugin MIDI / OSC mappings dialog (M4 U4), which also
        // owns MIDI learn.
        std::function<void(NodeId)> onOpenMappings;
        // The player's note about this node (#124): empty clears it.
        std::function<void(NodeId, juce::String)> onSetNote;
        // A click on the card (not a drag): the canvas selects it.
        std::function<void(NodeId)> onSelect;
        // The name was edited in place; empty reverts to the plugin's own name.
        std::function<void(NodeId, juce::String)> onRename;
        // A click on a card whose plugin died (M5 U5): bring it back.
        std::function<void(NodeId)> onRearm;
    };

    PluginCard(NodeSnapshot initialSnapshot, Callbacks callbacks);
    ~PluginCard() override;

    NodeId getNodeId() const noexcept { return snapshot_.id; }
    // What the card is showing. The canvas reads it to decide which ports
    // and cables to draw (#123).
    const NodeSnapshot& snapshot() const noexcept { return snapshot_; }

    void setSnapshot(const NodeSnapshot&);
    void setAccentColour(juce::Colour); // teal = effect, amber = instrument
    void setMappingCount(int count);
    void setSelected(bool selected);
    void setActivity(bool lit);                   // a mapped control just moved the plugin
    void setMeterLevels(float left, float right); // linear peaks; VU Meter cards only
    bool isVuMeter() const noexcept;

    // A built-in processor's control row (M6 U5, ui/ProcessorControls.h). The
    // card grows by its height; the ports stay where they are.
    static constexpr int kStandardHeight = 92;
    void setControls(std::unique_ptr<juce::Component> controls);
    int preferredHeight() const noexcept;

    // Port centre in card-local coordinates; SignalCanvas adds getPosition().
    // `channel`: 0 = L, 1 = R, juce::AudioProcessorGraph::midiChannelIndex = MIDI.
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

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    // Right-click: rename, note, bypass, remove (#124).
    void showCardMenu();
    void promptForNote();

private:
    // Hover over the card or any of its buttons, so Remove stays up while the
    // pointer is anywhere on the card. A separate listener, so the card's own
    // mouse handlers never see its children's events.
    struct HoverWatcher : juce::MouseListener
    {
        explicit HoverWatcher(PluginCard& c) : card(c) {}
        void mouseEnter(const juce::MouseEvent&) override { card.updateHover(); }
        void mouseExit(const juce::MouseEvent&) override { card.updateHover(); }
        PluginCard& card;
    };

    juce::String displayName() const;
    void refreshButtons();
    void updateHover();
    void beginRename();
    void endRename(bool commit);

    NodeSnapshot snapshot_;
    Callbacks callbacks_;
    juce::Colour accent_{tokens::accentPrimary};
    int mappingCount_{0};
    bool selected_{false};
    bool active_{false};
    float levelL_{0.0f};
    float levelR_{0.0f};

    juce::ComponentDragger dragger_;
    bool didDrag_{false};
    bool hovered_{false};
    HoverWatcher hoverWatcher_{*this};

    juce::Rectangle<int> nameArea_;
    // Icons, not the letters BYP / M / ED / X (#124). Four abbreviations
    // to learn, on an object meant to be read at a glance from a stage, and
    // two of them ("M", "X") abbreviate nothing obvious. A power symbol, a
    // row of sliders, a window and a cross need no learning.
    // A 22 px button with a 4 px inset: a 14 px icon box, which is the floor
    // in docs/design/2026-10-19-icon-system.md. They were inset 7, leaving
    // 8 px -- half the top bar's 16 and far too small for any of these
    // shapes to survive (#125). The hit target is unchanged; only the
    // drawing grew.
    IconButton bypassButton_{powerIcon(), "Bypass", 4.0f};
    IconButton mappingsButton_{slidersIcon(), "Mappings", 4.0f};
    IconButton editButton_{editorWindowIcon(), "Editor", 4.0f};
    // A bigger inset for the cross: see R6. Its diagonals reach the corners
    // of their box where the others reach only the edges, so at the same
    // inset it reads as the largest thing in the row.
    IconButton removeButton_{closeIcon(), "Remove", 5.5f};
    std::unique_ptr<juce::TextEditor> renameEditor_;
    // The last rename editor, kept alive until its own key or focus callback
    // has returned; freed on the next rename or with the card.
    std::unique_ptr<juce::TextEditor> retiredEditor_;
    std::unique_ptr<juce::Component> controls_;
    int controlsHeight_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCard)
};

} // namespace pedalboard3::ui
