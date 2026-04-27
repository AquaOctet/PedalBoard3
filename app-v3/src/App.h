#pragma once

#include "AppContext.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/PluginSlot.h"
#include "session/Preferences.h"
#include "session/PreferencesPersister.h"
#include "session/Session.h"
#include "ui/MainWindow.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <memory>
#include <optional>

namespace pedalboard3
{

// Top-level JUCE application object. Owns every subsystem by unique_ptr
// and exposes them to UI via an AppContext reference bundle (see
// AppContext.h). Phase 1.D.2 extracted listener fan-out into
// session::PreferencesPersister so App is a pure composition root.
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
    // Try to restore the previously-active plugin per AC-1.D.2-F, gated
    // by the KnownPluginList cross-check (AC-1.D.2-L) and the
    // restore-in-flight crash-loop guard (AC-1.D.2-N).
    void restoreActivePluginIfAny();

    // Construction order (also reverse destruction order). Each member's
    // dtor depends on the ones below it being still alive.
    std::unique_ptr<session::Preferences> preferences;
    std::unique_ptr<engine::AudioEngine> engine;
    std::unique_ptr<engine::PluginHost> pluginHost;
    std::unique_ptr<session::PluginSlot> pluginSlot;
    std::unique_ptr<session::Session> session;
    std::unique_ptr<session::PreferencesPersister> persister;
    std::optional<AppContext> context;
    std::unique_ptr<ui::MainWindow> mainWindow;
};

} // namespace pedalboard3
