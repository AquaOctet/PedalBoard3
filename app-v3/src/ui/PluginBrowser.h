#pragma once

#include "AppContext.h"
#include "BrowserModel.h"
#include "ColourScheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pedalboard3::ui
{

// Add the browser entry `id` ("plugin:<identifier>" or "internal:<typeId>")
// through the edit history, so it can be undone, and put it at the front of
// the recents. `position` is where its card goes (model coordinates, see
// ui/CanvasLayout.h), for a drop on the canvas; without one it lands at the
// end of the chain. Returns false (nothing added) when the id is unknown or
// the chain can't take an add right now (Session busy, or Test Tone has the
// graph).
bool addBrowserEntry(AppContext& ctx, const juce::String& id, std::optional<juce::Point<float>> position);

// The browser entry id a drag carries, or empty when the drag isn't from the
// browser.
juce::String browserDragId(const juce::var& dragDescription);

// The plugin browser (UI refresh U5): a 256 px right-hand panel with a
// search box, category chips and the Built-in / Recently used / All effects
// groups. A double-click adds at the end of the chain; dragging a row onto the
// canvas adds it where it lands (SignalCanvas is the drop target). The
// three-bar toggle at the top-right collapses the panel to its strip.
//
// Threading: message thread only. Listens to the known plugin list (a scan
// adds plugins) and to Session (an add refreshes the recents).
class PluginBrowser : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit PluginBrowser(AppContext& ctx);
    ~PluginBrowser() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // Installed by MainWindow's root component.
    std::function<void()> onCollapse; // the three-bar toggle
    std::function<void()> onDismiss;  // an add or Escape: close a slid-open panel

private:
    struct Row
    {
        bool header{false};
        juce::String title; // header rows
        browser::Entry entry;
    };

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void rebuildEntries(); // the known plugins changed
    void rebuildChips();
    void refilter(); // the search, the chip or the recents changed
    int layoutChips(juce::Rectangle<int> area, bool apply);

    AppContext& ctx_;
    std::vector<browser::Entry> entries_;
    std::vector<Row> rows_;
    juce::String chip_{browser::kAllChip};

    juce::ShapeButton toggle_{"Plugin browser", tokens::fgMuted, tokens::fgPrimary, tokens::accentPrimary};
    juce::TextEditor search_;
    std::vector<std::unique_ptr<juce::TextButton>> chips_;
    juce::ListBox list_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowser)
};

// The browser collapsed to a 48 px strip on the right: its three-bar toggle
// opens the panel (docked on a wide window, slid over the canvas on a narrow
// one).
class BrowserRail : public juce::Component
{
public:
    BrowserRail();

    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> onOpenPanel;

private:
    juce::ShapeButton toggle_{"Plugin browser", tokens::fgMuted, tokens::fgPrimary, tokens::accentPrimary};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserRail)
};

} // namespace pedalboard3::ui
