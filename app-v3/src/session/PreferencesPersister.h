#pragma once

#include <juce_events/juce_events.h>

namespace pedalboard3::engine
{
class AudioEngine;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{

class AppMappings;
class BankDocument;
class Preferences;
class Session;

// Single-responsibility owner of the three change-listener subscriptions
// that drive incremental Preferences writes. Pulls the listener fan-out
// off App so App stays a pure composition root.
//
// On construction registers itself against:
//   - juce::AudioDeviceManager (writes audioDeviceState XML on change)
//   - juce::KnownPluginList    (writes knownPluginList XML on change)
//   - session::Session         (writes/clears the active patch BANK on
//                               every Idle transition, the active slot
//                               refreshed from Session; clears the
//                               restore-in-flight marker on success)
//
//   - session::AppMappings     (writes the app-level MIDI/OSC mappings on
//                               every change)
//
// On destruction deregisters all of them. Lifetime invariant: must be
// destroyed BEFORE the AudioEngine, PluginHost, and Session it observes.
class PreferencesPersister : private juce::ChangeListener
{
public:
    PreferencesPersister(Preferences& prefs,
                         engine::AudioEngine& engine,
                         engine::PluginHost& host,
                         Session& session,
                         BankDocument& bankDocument,
                         AppMappings& appMappings);
    ~PreferencesPersister() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    Preferences& prefs_;
    engine::AudioEngine& engine_;
    engine::PluginHost& host_;
    Session& session_;
    BankDocument& bankDocument_;
    AppMappings& appMappings_;

    // Write the bank (its active slot refreshed from Session when that is the
    // live patch) unless the stored one already matches.
    void persistBank();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesPersister)
};

// Restore-in-flight crash-loop guard helpers (AC-1.D.2-N). The marker
// file lives next to Pedalboard 3.settings; it carries the
// `fileOrIdentifier` of the plugin description we are currently
// attempting to restore-load. Set by App::initialise BEFORE queueLoad,
// cleared by PreferencesPersister on Session-Idle-with-success.
//
// If the app crashes inside createInstance (before SEH/signal handlers
// catch it), the marker remains; next launch reads it and refuses to
// retry the same plugin, breaking the crash loop.
juce::String readRestoreInFlightMarker(Preferences& prefs);
void writeRestoreInFlightMarker(Preferences& prefs, const juce::String& fileOrIdentifier);
void clearRestoreInFlightMarker(Preferences& prefs);

} // namespace pedalboard3::session
