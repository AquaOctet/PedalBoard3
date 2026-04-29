#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace pedalboard3
{
struct AppContext;
}

namespace pedalboard3::ui
{

// Path γ MainWindow — a thin juce::DocumentWindow shell whose content
// is a vertical stack of TransportBar (h:48), juce::Viewport wrapping
// SignalCanvas (flex:1), and StatusBar (h:32). All UI orchestration
// (Add Plugin entry, Test Tone, Audio Settings dialog, plugin-list
// dialog, signal canvas, editor windows) is owned by those child
// components — MainWindow itself only handles close → quit.
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, AppContext& context);
    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    class RootComponent;
    std::unique_ptr<RootComponent> root_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace pedalboard3::ui
