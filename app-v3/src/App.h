#pragma once

#include "engine/AudioEngine.h"
#include "ui/MainWindow.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <memory>

namespace pedalboard3
{

// Top-level JUCE application object. Owns the AudioEngine and the main
// window. Phase 1 growth: later subsystems (Session, MidiRouter, OscServer,
// Preferences, Diagnostics::EventLog) will also live here, passed to
// MainWindow via an `AppContext` struct to keep the constructor narrow.
class App : public juce::JUCEApplication
{
public:
    App() = default;

    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool               moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;

    void systemRequestedQuit() override                    { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<engine::AudioEngine> engine;
    std::unique_ptr<ui::MainWindow>      mainWindow;
};

} // namespace pedalboard3
