#pragma once

#include "AppContext.h"
#include "control/MidiInputManager.h"
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/ControlHub.h"
#include "session/ControlWiring.h"
#include "session/Preferences.h"
#include "session/PreferencesPersister.h"
#include "session/Session.h"
#include "ui/MainWindow.h"
#include "ui/Pb3LookAndFeel.h"

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
// Phase 1.D.3 retired PluginSlot — Session now owns the multi-node
// patch directly.
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
    // Try to restore the previously-active patch per AC-1.D.3-G, gated by
    // the KnownPluginList cross-check (AC-1.D.3-L) and the restore-in-flight
    // crash-loop guard (AC-1.D.3-M). Implemented in Unit 5.
    void restoreActivePatchIfAny();

    // Construction order (also reverse destruction order). Each member's
    // dtor depends on the ones below it being still alive.
    //
    // lookAndFeel is constructed first and destroyed last: every other
    // UI element (MainWindow, dialogs, alerts) reads juce::Desktop's
    // default LookAndFeel pointer during draw, so the pointer must
    // outlive any visible Component.
    std::unique_ptr<ui::Pb3LookAndFeel> lookAndFeel;
    std::unique_ptr<session::Preferences> preferences;
    std::unique_ptr<engine::AudioEngine> engine;
    std::unique_ptr<engine::PluginHost> pluginHost;
    std::unique_ptr<session::Session> session;
    // M2: constructed after session (ControlWiring observes it), before
    // AppContext (which hands controlHub out to UI). Destroyed before
    // session in shutdown() — see the reverse-order comment there.
    std::unique_ptr<session::ControlHub> controlHub;
    std::unique_ptr<session::ControlWiring> controlWiring;
    // M2: feeds ControlHub::handleMidi from live MIDI input devices. Captures
    // controlHub by reference in its sink, so must be destroyed before it.
    std::unique_ptr<control::MidiInputManager> midiInputManager;
    // M2: feeds ControlHub::handleOsc from a live OSC listener. Same
    // capture-by-reference lifetime constraint as midiInputManager.
    std::unique_ptr<control::OscServer> oscServer;
    std::unique_ptr<session::PreferencesPersister> persister;
    std::optional<AppContext> context;
    std::unique_ptr<ui::MainWindow> mainWindow;
};

} // namespace pedalboard3
