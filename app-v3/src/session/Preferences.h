#pragma once

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
