#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "ui/MainWindow.h"

namespace pedalboard3
{

// Top-level JUCE application object. Owns the main window and will later
// own the AppContext (Session, AudioGraphRuntime, MidiRouter, OscServer,
// Preferences). Phase 0: just opens a window.
class App : public juce::JUCEApplication
{
public:
    App() = default;

    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool               moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& /*commandLine*/) override;
    void shutdown() override;

    void systemRequestedQuit() override                    { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<ui::MainWindow> mainWindow;
};

} // namespace pedalboard3
