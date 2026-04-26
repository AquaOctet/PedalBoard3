#include "Preferences.h"

namespace pedalboard3::session
{

namespace
{

constexpr auto kAudioDeviceStateKey = "audioDeviceState";
constexpr auto kKnownPluginListKey = "knownPluginList";

juce::PropertiesFile::Options makeOptions()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Pedalboard 3";
    opts.filenameSuffix = ".settings";
    opts.folderName = "Pedalboard 3";
    // Apple's recommended location since macOS 10.10. JUCE asserts at
    // runtime if this is left empty on macOS.
    opts.osxLibrarySubFolder = "Application Support";
    opts.commonToAllUsers = false;
    opts.ignoreCaseOfKeyNames = false;
    return opts;
}

std::unique_ptr<juce::XmlElement> readXml(const juce::PropertiesFile& f, const juce::String& key)
{
    if (auto element = f.getXmlValue(key))
        return element;
    return nullptr;
}

} // namespace

Preferences::Preferences()
{
    props_.setStorageParameters(makeOptions());
}

Preferences::~Preferences()
{
    // ApplicationProperties' destructor calls saveIfNeeded internally,
    // but we still call flush() explicitly to keep behaviour identical
    // whether or not App::shutdown ran cleanly.
    flush();
}

juce::PropertiesFile* Preferences::getStore() noexcept
{
    return props_.getUserSettings();
}

std::unique_ptr<juce::XmlElement> Preferences::getAudioDeviceState()
{
    if (auto* store = props_.getUserSettings())
        return readXml(*store, kAudioDeviceStateKey);
    return nullptr;
}

void Preferences::setAudioDeviceState(const juce::XmlElement& state)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kAudioDeviceStateKey, &state);
}

std::unique_ptr<juce::XmlElement> Preferences::getKnownPluginList()
{
    if (auto* store = props_.getUserSettings())
        return readXml(*store, kKnownPluginListKey);
    return nullptr;
}

void Preferences::setKnownPluginList(const juce::XmlElement& xml)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kKnownPluginListKey, &xml);
}

void Preferences::flush()
{
    props_.saveIfNeeded();
}

} // namespace pedalboard3::session
