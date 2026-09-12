#include "Preferences.h"

namespace pedalboard3::session
{

namespace
{

constexpr auto kAudioDeviceStateKey = "audioDeviceState";
constexpr auto kKnownPluginListKey = "knownPluginList";
constexpr auto kActivePluginDescriptionKey = "activePluginDescription";
constexpr auto kActivePatchKey = "activePatch";
constexpr auto kActiveBankKey = "activeBank";
constexpr auto kActiveBankFileKey = "activeBankFile";
constexpr auto kActiveBankModifiedKey = "activeBankModified";
constexpr auto kAppMappingsKey = "appMappings";
constexpr auto kKeyMappingsKey = "keyMappings";
constexpr auto kWindowStateKey = "windowState";
constexpr auto kRecentPluginsKey = "recentPlugins";
constexpr auto kRecentFilesKey = "recentFiles";
constexpr auto kOscPortKey = "oscPort";
constexpr auto kOscMulticastAddressKey = "oscMulticastAddress";

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

std::unique_ptr<juce::PluginDescription> Preferences::getActivePluginDescription()
{
    auto* store = props_.getUserSettings();
    if (store == nullptr)
        return nullptr;

    auto xml = readXml(*store, kActivePluginDescriptionKey);
    if (xml == nullptr)
        return nullptr;

    auto desc = std::make_unique<juce::PluginDescription>();
    if (!desc->loadFromXml(*xml))
        return nullptr;

    // Treat a fully-empty record (no format, no identifier) the same as
    // "no value stored" so callers don't need to defend against the
    // round-trip-of-an-empty-element edge case.
    if (desc->pluginFormatName.isEmpty() && desc->fileOrIdentifier.isEmpty())
        return nullptr;

    return desc;
}

void Preferences::setActivePluginDescription(const juce::PluginDescription& desc)
{
    auto* store = props_.getUserSettings();
    if (store == nullptr)
        return;

    auto xml = desc.createXml();
    if (xml != nullptr)
        store->setValue(kActivePluginDescriptionKey, xml.get());
}

void Preferences::clearActivePluginDescription()
{
    if (auto* store = props_.getUserSettings())
        store->removeValue(kActivePluginDescriptionKey);
}

std::unique_ptr<Patch> Preferences::getActivePatch()
{
    auto* store = props_.getUserSettings();
    if (store == nullptr)
        return nullptr;

    auto xml = readXml(*store, kActivePatchKey);
    if (xml == nullptr)
        return nullptr;

    // patchFromXml accepts both the current <PATCH> tag and the legacy
    // <ACTIVE_PATCH> tag older builds wrote here, and round-trips the
    // per-node plugin state blob (see session/Patch.cpp).
    auto patch = patchFromXml(*xml);
    if (patch == nullptr)
        return nullptr;

    if (validatePatch(*patch) != PatchValidation::Ok)
        return nullptr;

    return patch;
}

void Preferences::setActivePatch(const Patch& patch)
{
    auto* store = props_.getUserSettings();
    if (store == nullptr)
        return;

    if (auto xml = patchToXml(patch))
        store->setValue(kActivePatchKey, xml.get());
}

void Preferences::clearActivePatch()
{
    if (auto* store = props_.getUserSettings())
        store->removeValue(kActivePatchKey);
}

std::unique_ptr<PatchBank> Preferences::getActiveBank()
{
    auto* store = props_.getUserSettings();
    if (store == nullptr)
        return nullptr;

    if (auto xml = readXml(*store, kActiveBankKey))
        return bankFromXml(*xml);

    // Legacy: a pre-bank build persisted one patch under activePatch.
    if (auto legacy = getActivePatch())
    {
        auto bank = std::make_unique<PatchBank>();
        bank->patches = {std::move(*legacy)};
        bank->activeIndex = 0;
        return bank;
    }
    return nullptr;
}

void Preferences::setActiveBank(const PatchBank& bank)
{
    auto* store = props_.getUserSettings();
    if (store == nullptr)
        return;

    if (auto xml = bankToXml(bank))
    {
        store->setValue(kActiveBankKey, xml.get());
        store->removeValue(kActivePatchKey); // one source of truth from here on
    }
}

void Preferences::clearActiveBank()
{
    if (auto* store = props_.getUserSettings())
    {
        store->removeValue(kActiveBankKey);
        store->removeValue(kActivePatchKey);
        store->removeValue(kActiveBankFileKey);
        store->removeValue(kActiveBankModifiedKey);
    }
}

void Preferences::setActiveBankIdentity(const juce::File& file, bool modified)
{
    if (auto* store = props_.getUserSettings())
    {
        store->setValue(kActiveBankFileKey, file == juce::File() ? juce::String() : file.getFullPathName());
        store->setValue(kActiveBankModifiedKey, modified);
    }
}

juce::File Preferences::getActiveBankFile()
{
    if (auto* store = props_.getUserSettings())
    {
        const auto path = store->getValue(kActiveBankFileKey);
        if (juce::File::isAbsolutePath(path))
            return juce::File(path);
    }
    return {};
}

bool Preferences::getActiveBankModified()
{
    if (auto* store = props_.getUserSettings())
        return store->getBoolValue(kActiveBankModifiedKey, false);
    return false;
}

std::unique_ptr<juce::XmlElement> Preferences::getAppMappings()
{
    if (auto* store = props_.getUserSettings())
        return readXml(*store, kAppMappingsKey);
    return nullptr;
}

void Preferences::setAppMappings(const juce::XmlElement& xml)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kAppMappingsKey, &xml);
}

std::unique_ptr<juce::XmlElement> Preferences::getKeyMappings()
{
    if (auto* store = props_.getUserSettings())
        return readXml(*store, kKeyMappingsKey);
    return nullptr;
}

void Preferences::setKeyMappings(const juce::XmlElement& xml)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kKeyMappingsKey, &xml);
}

juce::String Preferences::getWindowState()
{
    if (auto* store = props_.getUserSettings())
        return store->getValue(kWindowStateKey);
    return {};
}

void Preferences::setWindowState(const juce::String& state)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kWindowStateKey, state);
}

bool Preferences::getPanelShown(const juce::String& panel, bool fallback)
{
    if (auto* store = props_.getUserSettings())
        return store->getBoolValue("panel." + panel, fallback);
    return fallback;
}

void Preferences::setPanelShown(const juce::String& panel, bool shown)
{
    if (auto* store = props_.getUserSettings())
        store->setValue("panel." + panel, shown);
}

juce::StringArray Preferences::getRecentPlugins()
{
    juce::StringArray ids;
    if (auto* store = props_.getUserSettings())
        ids = juce::StringArray::fromLines(store->getValue(kRecentPluginsKey));
    ids.removeEmptyStrings();
    return ids;
}

void Preferences::setRecentPlugins(const juce::StringArray& ids)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kRecentPluginsKey, ids.joinIntoString("\n"));
}

juce::StringArray Preferences::getRecentFiles()
{
    juce::StringArray paths;
    if (auto* store = props_.getUserSettings())
        paths = juce::StringArray::fromLines(store->getValue(kRecentFilesKey));
    paths.removeEmptyStrings();
    return paths;
}

void Preferences::setRecentFiles(const juce::StringArray& paths)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kRecentFilesKey, paths.joinIntoString("\n"));
}

int Preferences::getOscPort()
{
    if (auto* store = props_.getUserSettings())
        return store->getIntValue(kOscPortKey, kDefaultOscPort);
    return kDefaultOscPort;
}

void Preferences::setOscPort(int port)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kOscPortKey, port);
}

juce::String Preferences::getOscMulticastAddress()
{
    if (auto* store = props_.getUserSettings())
        return store->getValue(kOscMulticastAddressKey, {});
    return {};
}

void Preferences::setOscMulticastAddress(const juce::String& address)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(kOscMulticastAddressKey, address);
}

void Preferences::flush()
{
    props_.saveIfNeeded();
}

} // namespace pedalboard3::session
