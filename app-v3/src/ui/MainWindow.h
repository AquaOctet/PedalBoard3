#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace pedalboard3::ui
{

// Phase 0 placeholder window. Phase 3 grows this into the real main window
// that hosts the plugin-field canvas, transport bar, patch bar, and menus.
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace pedalboard3::ui
