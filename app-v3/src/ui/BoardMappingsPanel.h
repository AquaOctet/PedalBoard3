#pragma once

#include "AppContext.h"
#include "ColourScheme.h"
#include "FeatureChips.h"
#include "MappingRows.h"
#include "session/NodeId.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace pedalboard3::ui
{

// The board mappings panel (UI refresh U9, plan KD4), docked under the canvas.
// It lists every MIDI and OSC mapping on the board in chain order, on MIDI and
// OSC tabs with counts.
//   - The selected card's rows are highlighted, not filtered, and "+ Mapping"
//     adds one to that card. A click on a row's plugin name selects its card.
//   - Rows edit in place with the dialog's row editors, and every edit goes
//     through Session::setNodeMappings, as the dialog's do.
//   - MIDI learn stays in the per-plugin dialog: a row's Learn... opens it.
//   - The board's feature switches sit in its bottom-right corner as chips
//     (optional-features U3), on the header row when it's collapsed.
//   - Collapsed, only the header shows, with the tab counts. Its three-bar
//     toggle (and Plugins > Mappings Panel) opens and collapses it.
//
// Threading: message thread only. Listens to Session. The row editors are
// rebuilt only when the board's mappings change, not when an edit made here
// comes back, so a range being dragged keeps its slider.
class BoardMappingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    static constexpr int kHeaderHeight = 32;
    static constexpr int kOpenHeight = 196;

    explicit BoardMappingsPanel(AppContext& ctx);
    ~BoardMappingsPanel() override;

    // Installed by MainWindow's root component.
    std::function<void()> onToggle;                    // the three-bar toggle, or a tab while collapsed
    std::function<void(session::NodeId)> onOpenDialog; // Learn...: the plugin's mappings dialog
    std::function<void(session::NodeId)> onSelectNode; // a click on a row's plugin name

    void setOpen(bool open);
    void setSelectedNode(std::optional<session::NodeId>); // follows the canvas selection

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    class Content; // the column header and the rows, scrolled by the viewport

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh(); // re-read the board's mappings
    void rebuildRows();
    void layoutRows();
    void showTab(mappingrows::Kind);
    void updateHeader();
    void addMapping();
    void editMidi(size_t row, const control::MidiMapping&);
    void editOsc(size_t row, const control::OscMapping&);
    void removeRow(mappingrows::Kind, size_t row);
    juce::StringArray namesFor(session::NodeId); // parameter names, cached per plugin

    std::vector<mappingrows::Row>& modelsOf(mappingrows::Kind kind)
    {
        return kind == mappingrows::Kind::Midi ? midi_ : osc_;
    }

    AppContext& ctx_;
    bool open_{true};
    mappingrows::Kind tab_{mappingrows::Kind::Midi};
    std::optional<session::NodeId> selected_;
    std::vector<mappingrows::Row> midi_;
    std::vector<mappingrows::Row> osc_;
    std::unordered_map<session::NodeId, juce::String> nodeNames_;
    std::vector<juce::String> nodeKeys_; // which plugins the cached parameter names are for
    std::unordered_map<session::NodeId, juce::StringArray> parameterNames_;

    juce::Rectangle<int> titleArea_;
    juce::ShapeButton toggle_{"Mappings", tokens::fgMuted, tokens::fgPrimary, tokens::accentPrimary};
    juce::TextButton midiTab_;
    juce::TextButton oscTab_;
    juce::TextButton addButton_{"+ Mapping"};
    FeatureChips chips_; // the board's feature switches, bottom right (optional-features U3)
    juce::Viewport viewport_;
    std::unique_ptr<Content> content_;
    std::vector<std::unique_ptr<Row>> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoardMappingsPanel)
};

} // namespace pedalboard3::ui
