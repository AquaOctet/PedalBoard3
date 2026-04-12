#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace pedalboard3::engine
{
class AudioEngine;
}

namespace pedalboard3::ui
{

// Phase 0/1 placeholder window. Phase 3 grows this into the real main
// window that hosts the plugin-field canvas, transport bar, patch bar, and
// menus. For now it provides two diagnostic affordances so the developer
// and QA operator can verify audio round-trip on real hardware: an Audio
// Settings dialog (JUCE's standard selector) and a Test Tone toggle.
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, engine::AudioEngine& audioEngine);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace pedalboard3::ui
