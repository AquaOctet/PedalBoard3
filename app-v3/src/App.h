#pragma once

#include "AppContext.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "ui/MainWindow.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <memory>
#include <optional>

namespace pedalboard3
{

// Top-level JUCE application object. Owns every subsystem by unique_ptr
// and exposes them to UI via an AppContext reference bundle (see
// AppContext.h). Phase 1 growth: later subsystems (Session, MidiRouter,
// OscServer, Preferences, Diagnostics::EventLog) will slot into the
// AppContext without widening MainWindow's constructor.
class App : public juce::JUCEApplication
{
public:
    App() = default;

    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<engine::AudioEngine> engine;
    std::unique_ptr<engine::PluginHost>  pluginHost;
    std::optional<AppContext>            context;
    std::unique_ptr<ui::MainWindow>      mainWindow;
};

} // namespace pedalboard3
