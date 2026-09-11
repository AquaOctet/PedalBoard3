#pragma once

#include "AppContext.h"
#include "BankRows.h"
#include "ColourScheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace pedalboard3::ui
{

// The patch bank sidebar (UI refresh U4): the board's name and patch count
// over one row per patch (Program Change number, name, plugin count), the
// playing patch highlighted. A click switches to a patch, a double-click
// renames it, a right-click offers rename / duplicate / remove; "+ New" and
// Duplicate sit at the foot. Every switch goes through BankDocument, like the
// top bar's patch strip.
//
// Threading: message thread only. Listens to BankDocument and Session.
class BankSidebar : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit BankSidebar(AppContext& ctx);
    ~BankSidebar() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // Installed by MainWindow's root component.
    std::function<void()> onRenameActive; // the top bar's rename prompt, for the active patch
    std::function<void()> onRemoveActive; // the top bar's remove confirmation
    std::function<void()> onCollapse;     // the three-bar toggle: collapse to the strip
    std::function<void()> onDismiss;      // a pick or Escape: close a slid-open panel

private:
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void refresh();
    void showRowMenu(int row);

    AppContext& ctx_;
    std::vector<bank::Row> rows_;
    juce::ListBox list_;
    juce::ShapeButton collapseButton_{"Patch bank", tokens::fgMuted, tokens::fgPrimary, tokens::accentPrimary};
    juce::TextButton newButton_{"+ New"};
    juce::TextButton duplicateButton_{"Duplicate"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BankSidebar)
};

} // namespace pedalboard3::ui
