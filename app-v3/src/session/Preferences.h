#pragma once

#include "Patch.h"
#include "PatchBank.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace pedalboard3::session
{

// Per-user settings store backed by juce::ApplicationProperties.
//
// One PropertiesFile lives at the OS-conventional path:
//   macOS   ~/Library/Application Support/Pedalboard 3/Preferences.settings
//   Linux   ~/.config/Pedalboard 3/Preferences.settings
//   Windows %APPDATA%\Pedalboard 3\Preferences.settings
//
// Same parent directory as PluginHost::getCrashedPluginsFile()'s xml so
// every per-user artefact lives under one folder.
//
// Carries:
//   - "audioDeviceState" — XML from AudioDeviceManager::createStateXml()
//   - "knownPluginList"  — XML from KnownPluginList::createXml()
//   - JUCE plugin blacklist — managed automatically when a PropertiesFile*
//     is handed to juce::PluginListComponent
//
// Not RT-safe. Message-thread only. ApplicationProperties' default
// behaviour saves dirty values every ~3 seconds; flush() forces an
// immediate write (called from App::shutdown).
class Preferences
{
public:
    Preferences();
    ~Preferences();

    // Hand to JUCE components that take a PropertiesFile* (e.g.
    // juce::AudioDeviceSelectorComponent, juce::PluginListComponent).
    // They read/write keys directly into the same file.
    juce::PropertiesFile* getStore() noexcept;

    // Saved AudioDeviceManager state, or nullptr on first run. Non-const
    // because juce::ApplicationProperties opens the backing file lazily
    // on the first getUserSettings() call.
    std::unique_ptr<juce::XmlElement> getAudioDeviceState();
    void setAudioDeviceState(const juce::XmlElement& state);

    // Saved KnownPluginList XML (includes the plugin blacklist), or
    // nullptr on first run.
    std::unique_ptr<juce::XmlElement> getKnownPluginList();
    void setKnownPluginList(const juce::XmlElement& xml);

    // The plugin description that was active in the host slot when the
    // app last quit cleanly. nullptr on first run, on a malformed
    // settings entry, or after clearActivePluginDescription. Used by
    // App::initialise to restore the user's last loaded plugin via
    // session::Session::queueLoad.
    //
    // Read path is XXE-safe: PropertiesFile::getXmlValue uses the
    // string overload of juce::parseXML, which constructs an
    // XmlDocument with no inputSource and therefore cannot resolve
    // SYSTEM/PUBLIC external entities.
    std::unique_ptr<juce::PluginDescription> getActivePluginDescription();
    void setActivePluginDescription(const juce::PluginDescription& desc);
    void clearActivePluginDescription();

    // The whole active patch (Phase 1.D.3 — replaces ActivePluginDescription
    // for multi-plugin chains). XML root is <ACTIVE_PATCH>; no schemaMajor
    // attribute (the future .pdl format owns version negotiation per master
    // plan §4.5). Round-trip preserves node order, connections,
    // per-node bypass, and customName.
    //
    // Returns nullptr on first run, malformed XML, or a structurally
    // invalid patch (validatePatch != Ok). The caller is responsible for
    // the further KnownPluginList cross-check + crash-loop guard.
    std::unique_ptr<Patch> getActivePatch();
    void setActivePatch(const Patch& patch);
    void clearActivePatch();

    // The whole active patch BANK (roadmap M3 U3) — supersedes the single
    // ActivePatch above for auto-restore. XML root is <BANK> (see
    // session::bankToXml). getActiveBank falls back to a legacy ActivePatch
    // value (as a one-patch bank, index 0) so a settings file written by a
    // pre-bank build still restores; setActiveBank drops that legacy value.
    // Returns nullptr on first run, malformed XML, or any structurally
    // invalid patch.
    std::unique_ptr<PatchBank> getActiveBank();
    void setActiveBank(const PatchBank& bank);
    void clearActiveBank(); // clears the legacy ActivePatch value too

    // OSC listener config (M2, KTD4). Plain scalar values via
    // juce::PropertiesFile's typed getters — no XML needed. Port defaults to
    // 9000 when unset. Multicast address is empty ("") when unset, meaning
    // "unicast only, no group join" — control::OscServer treats an empty
    // string as "don't attempt to join a group" rather than an invalid one.
    static constexpr int kDefaultOscPort = 9000;
    int getOscPort();
    void setOscPort(int port);
    juce::String getOscMulticastAddress();
    void setOscMulticastAddress(const juce::String& address);

    // Force a synchronous write of any dirty values. Safe to call
    // multiple times. ApplicationProperties also auto-saves on
    // destruction, but App::shutdown calls this explicitly so a
    // listener-driven write that happened seconds before quit is
    // guaranteed to land on disk.
    void flush();

private:
    juce::ApplicationProperties props_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Preferences)
};

} // namespace pedalboard3::session
