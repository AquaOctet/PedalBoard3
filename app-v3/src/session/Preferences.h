#pragma once

#include "Patch.h"
#include "PatchBank.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>
#include <utility>

namespace pedalboard3::session
{

// Per-user settings store backed by juce::ApplicationProperties.
//
// One PropertiesFile lives at the OS-conventional path (JUCE's
// PropertiesFile::Options::getDefaultFile):
//   macOS   ~/Library/Application Support/Pedalboard 3/Pedalboard 3.settings
//   Linux   ~/Pedalboard 3/Pedalboard 3.settings
//   Windows %APPDATA%\Pedalboard 3\Pedalboard 3.settings
//
// App points PluginHost's crash list and scan marker at the same folder
// (PluginHost::setUserDataFolder), so every per-user file lives in one place.
//
// A file that exists but can't be read is moved aside as "<name>.corrupt" and
// Preferences starts empty, rather than letting the next save overwrite it
// (M5 U7, plan KD8, master plan AC-SE-24); getSetAsideSettingsFile() tells the
// app, which says so and offers a plugin rescan. Saves go through the atomic
// writer, owner-only (0600) on POSIX.
//
// Carries:
//   - "audioDeviceState" — XML from AudioDeviceManager::createStateXml()
//   - "knownPluginList"  — XML from KnownPluginList::createXml()
//   - JUCE plugin blacklist — managed automatically when a PropertiesFile*
//     is handed to juce::PluginListComponent
//
// Not RT-safe. Message-thread only. Dirty values are saved about 3 seconds
// after the last change; flush() forces an immediate write (called from
// App::shutdown).
class Preferences : private juce::ChangeListener, private juce::Timer
{
public:
    Preferences();
    // The settings live in `settingsFile` rather than the user's (tests).
    explicit Preferences(juce::File settingsFile);

    // The user's settings file, which Preferences() opens (%APPDATA%\Pedalboard
    // 3\Pedalboard 3.settings on Windows). Tests never open it: they pass a file of
    // their own, and tests/support/RealSettingsGuard fails a run that changes it.
    static juce::File defaultSettingsFile();
    ~Preferences() override;

    // Hand to JUCE components that take a PropertiesFile* (e.g.
    // juce::AudioDeviceSelectorComponent, juce::PluginListComponent).
    // They read/write keys directly into the same file.
    juce::PropertiesFile* getStore() noexcept;

    // A settings file that couldn't be read when Preferences opened it (M5 U7):
    // where it was moved aside, "<name>.corrupt". Empty when the file was fine
    // or absent.
    juce::File getSetAsideSettingsFile() const noexcept { return setAside_; }

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

    // The file the persisted bank belongs to (empty = untitled) and whether it
    // had unsaved changes when last persisted (finish pass KD7): a relaunch
    // restores both, so quick save still knows its file and a crash does not
    // hide unsaved work. Cleared by clearActiveBank. `readOnly`: the board was
    // written by a newer major version (M5 U4), and stays read-only after a
    // relaunch.
    void setActiveBankIdentity(const juce::File& file, bool modified, bool readOnly = false);
    juce::File getActiveBankFile();
    bool getActiveBankModified();
    bool getActiveBankReadOnly();

    // App-level MIDI/OSC mappings (roadmap M4 U3): the <APP_MAPPINGS> XML that
    // session::AppMappings writes. nullptr on first run.
    std::unique_ptr<juce::XmlElement> getAppMappings();
    void setAppMappings(const juce::XmlElement& xml);

    // Keyboard shortcuts for app commands (roadmap M4 U5): the XML
    // juce::KeyPressMappingSet::createXml(true) writes, i.e. only the
    // differences from the default keys. nullptr on first run.
    std::unique_ptr<juce::XmlElement> getKeyMappings();
    void setKeyMappings(const juce::XmlElement& xml);

    // The main window's position and size (finish pass U6): the string
    // juce::ResizableWindow::getWindowStateAsString writes. Empty on first run.
    juce::String getWindowState();
    void setWindowState(const juce::String& state);

    // Whether a main-window panel is shown (UI refresh U6): "patches" today,
    // "browser" and "mappings" later. `fallback` until the user toggles it.
    bool getPanelShown(const juce::String& panel, bool fallback);
    void setPanelShown(const juce::String& panel, bool shown);

    // MIDI Machine Control drives the transport (M6 U3, plan KD5); off by default.
    bool getMmcTransport();
    void setMmcTransport(bool enabled);

    // The Settings switch for each optional feature (plan
    // docs/plans/2026-09-12-002-feat-optional-features-plan.md, KD2 / KD3):
    // "feature.<key>", session::enabledByDefault until the user sets it.
    bool getFeatureEnabled(Feature feature);
    void setFeatureEnabled(Feature feature, bool enabled);

    // What the feature Settings sections keep (optional-features U2, KD7):
    //   - the tempo and time signature new patches start with (120 BPM in 4/4
    //     until set); applyNewPatchDefaults writes them into a new patch
    //   - whether the transport shows the Metronome's click button (yes)
    //   - the level new Metronomes start at (-6 dB)
    //   - the folder new Loopers save loops in (File() = the Recordings folder)
    double getNewPatchTempo();
    void setNewPatchTempo(double bpm);
    std::pair<int, int> getNewPatchTimeSignature();
    void setNewPatchTimeSignature(int numerator, int denominator);
    void applyNewPatchDefaults(Patch& patch);
    bool getMetronomeClickButton();
    void setMetronomeClickButton(bool shown);
    float getNewMetronomeLevel();
    void setNewMetronomeLevel(float db);
    juce::File getLooperFolder();
    void setLooperFolder(const juce::File& folder);

    // The plugin browser's recently used entries, newest first (UI refresh U5):
    // browser ids, "plugin:<identifier>" or "internal:<typeId>".
    juce::StringArray getRecentPlugins();
    void setRecentPlugins(const juce::StringArray& ids);

    // File > Open Recent (#30): full paths of the banks last opened or saved,
    // newest first (session/BoardFiles.h keeps the list).
    juce::StringArray getRecentFiles();
    void setRecentFiles(const juce::StringArray& paths);

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

    // Write any dirty values now, atomically (a crash mid-save leaves the old
    // settings whole) and owner-only on POSIX. Safe to call multiple times. The
    // destructor flushes too, but App::shutdown calls this explicitly so a
    // listener-driven write that happened seconds before quit is guaranteed to
    // land on disk.
    void flush();

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // a value changed: save soon
    void timerCallback() override;                                  // ... now

    juce::File file_;
    juce::File setAside_;

    // The store. Its own PropertiesFile, not juce::ApplicationProperties, so
    // Preferences decides how the file is read and written (M5 U7).
    struct Store
    {
        std::unique_ptr<juce::PropertiesFile> file;
        juce::PropertiesFile* getUserSettings() const noexcept { return file.get(); }
    } props_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Preferences)
};

} // namespace pedalboard3::session
