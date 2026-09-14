#pragma once

#include "AppContext.h"
#include "control/MidiInputManager.h"
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/AppMappings.h"
#include "session/BankDocument.h"
#include "session/ControlHub.h"
#include "session/ControlWiring.h"
#include "session/EditHistory.h"
#include "session/FeatureGate.h"
#include "session/FeatureNodeGate.h"
#include "session/InputGates.h"
#include "session/PatchSwitcher.h"
#include "session/Preferences.h"
#include "session/PreferencesPersister.h"
#include "session/Session.h"
#include "session/TransportCommands.h"
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
// AppContext.h). Listener fan-out lives in session::PreferencesPersister,
// so App is a pure composition root.
class App : public juce::JUCEApplication
{
public:
    App() = default;

    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;

    // Asks the main window first, so unsaved changes get the save prompt
    // (finish pass KD4).
    void systemRequestedQuit() override;
    void anotherInstanceStarted(const juce::String&) override {}

private:
    // Try to restore the previously-active patch BANK per AC-1.D.3-G, gated
    // by the KnownPluginList cross-check (AC-1.D.3-L) and the
    // restore-in-flight crash-loop guard (AC-1.D.3-M) on its active patch.
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
    // M3: the patch-bank document that drives session's whole-patch swaps.
    // Constructed right after session; destroyed before it (issues no swap
    // in its dtor, but PreferencesPersister and UI hold a reference).
    std::unique_ptr<session::BankDocument> bankDocument;
    // Finish pass U8: undo / redo for canvas edits. Listens to session; the
    // UI and the patch switcher hold it, so it goes after them, before session.
    std::unique_ptr<session::EditHistory> editHistory;
    // M6 U3: the transport's commands (one tap tempo for every trigger) and the MMC switch.
    std::unique_ptr<session::TransportCommands> transportCommands;
    // Optional features (plan 2026-09-12-002 U1): Settings switches in
    // Preferences, board switches in the open bank. Listens to bankDocument.
    std::unique_ptr<session::FeatureGate> featureGate;
    // Optional features U4: switches the Metronome and Looper nodes to
    // pass-through while their feature is off. Listens to featureGate and session.
    std::unique_ptr<session::FeatureNodeGate> featureNodeGate;
    // M2: constructed after session (ControlWiring observes it), before
    // AppContext (which hands controlHub out to UI). Destroyed before
    // session in shutdown() — see the reverse-order comment there.
    std::unique_ptr<session::ControlHub> controlHub;
    // M4: app-level MIDI/OSC mappings. Restored before controlWiring so its
    // first feed includes them; destroyed after controlWiring and persister,
    // which both listen to it.
    std::unique_ptr<session::AppMappings> appMappings;
    std::unique_ptr<session::ControlWiring> controlWiring;
    // M3: Program Change / PatchSelect -> bank switch. Installs controlHub's
    // unhandled-target sink and is the MIDI drain sink below, so it must
    // outlive both input edges and die before controlHub and bankDocument.
    std::unique_ptr<session::PatchSwitcher> patchSwitcher;
    // M2: feeds ControlHub::handleMidi from live MIDI input devices. Captures
    // controlHub by reference in its sink, so must be destroyed before it.
    std::unique_ptr<control::MidiInputManager> midiInputManager;
    // M2: feeds ControlHub::handleOsc from a live OSC listener. Same
    // capture-by-reference lifetime constraint as midiInputManager.
    std::unique_ptr<control::OscServer> oscServer;
    // Optional features U2: MIDI input and the OSC listener run only while
    // their feature is available. Listens to featureGate.
    std::unique_ptr<session::InputGates> inputGates;
    std::unique_ptr<session::PreferencesPersister> persister;
    std::optional<AppContext> context;
    std::unique_ptr<ui::MainWindow> mainWindow;

    // Set when the user quits with Don't Save: shutdown() then reverts the
    // persisted session to the last saved content (finish pass KD7).
    bool discardUnsavedOnShutdown{false};
};

} // namespace pedalboard3
