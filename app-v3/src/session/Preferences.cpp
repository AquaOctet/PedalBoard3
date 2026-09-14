#include "Preferences.h"

#include "AtomicFileWriter.h"
#include "engine/TransportMath.h"

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
constexpr auto kActiveBankReadOnlyKey = "activeBankReadOnly";
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

// Dirty values are saved this long after the last change, as JUCE's own
// auto-save did.
constexpr int kSaveDelayMs = 3000;

// The settings as JUCE's PropertiesFile writes them (saveAsXml), so either can
// read what the other wrote: one <VALUE name="..."> per key, holding its value
// as XML when it is XML, else as a val="..." attribute.
std::unique_ptr<juce::XmlElement> settingsXml(juce::PropertiesFile& store)
{
    auto doc = std::make_unique<juce::XmlElement>("PROPERTIES");
    const auto& props = store.getAllProperties();
    for (int i = 0; i < props.size(); ++i)
    {
        auto* value = doc->createNewChildElement("VALUE");
        value->setAttribute("name", props.getAllKeys()[i]);
        if (auto child = juce::parseXML(props.getAllValues()[i]))
            value->addChildElement(child.release());
        else
            value->setAttribute("val", props.getAllValues()[i]);
    }
    return doc;
}

} // namespace

juce::File Preferences::defaultSettingsFile()
{
    return makeOptions().getDefaultFile();
}

Preferences::Preferences() : Preferences(defaultSettingsFile()) {}

Preferences::Preferences(juce::File settingsFile) : file_(std::move(settingsFile))
{
    auto options = makeOptions();
    // Saved only by flush(), through the atomic writer (M5 U7): JUCE never
    // writes the file itself.
    options.doNotSave = true;
    options.millisecondsBeforeSaving = -1;

    props_.file = std::make_unique<juce::PropertiesFile>(file_, options);
    if (!props_.file->isValidFile())
    {
        // It's there, but it couldn't be read (M5 U7, plan KD8, AC-SE-24).
        // Move it aside rather than let the next save overwrite it, start
        // empty, and keep where it went so the app can say so.
        props_.file.reset();
        const auto aside = file_.getParentDirectory().getNonexistentChildFile(file_.getFileName(), ".corrupt", false);
        if (file_.moveFileTo(aside) || file_.copyFileTo(aside))
            setAside_ = aside;
        props_.file = std::make_unique<juce::PropertiesFile>(file_, options);
    }
    props_.file->addChangeListener(this);
}

Preferences::~Preferences()
{
    stopTimer();
    props_.file->removeChangeListener(this);
    flush();
}

juce::PropertiesFile* Preferences::getStore() noexcept
{
    return props_.getUserSettings();
}

void Preferences::changeListenerCallback(juce::ChangeBroadcaster*)
{
    startTimer(kSaveDelayMs);
}

void Preferences::timerCallback()
{
    stopTimer();
    flush();
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
        store->removeValue(kActiveBankReadOnlyKey);
    }
}

void Preferences::setActiveBankIdentity(const juce::File& file, bool modified, bool readOnly)
{
    if (auto* store = props_.getUserSettings())
    {
        store->setValue(kActiveBankFileKey, file == juce::File() ? juce::String() : file.getFullPathName());
        store->setValue(kActiveBankModifiedKey, modified);
        store->setValue(kActiveBankReadOnlyKey, readOnly);
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

bool Preferences::getActiveBankReadOnly()
{
    if (auto* store = props_.getUserSettings())
        return store->getBoolValue(kActiveBankReadOnlyKey, false);
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

bool Preferences::getMmcTransport()
{
    if (auto* store = props_.getUserSettings())
        return store->getBoolValue("mmcTransport", false);
    return false;
}

void Preferences::setMmcTransport(bool enabled)
{
    if (auto* store = props_.getUserSettings())
        store->setValue("mmcTransport", enabled);
}

bool Preferences::getFeatureEnabled(Feature feature)
{
    if (auto* store = props_.getUserSettings())
        return store->getBoolValue(juce::String("feature.") + featureKey(feature), enabledByDefault(feature));
    return enabledByDefault(feature);
}

void Preferences::setFeatureEnabled(Feature feature, bool enabled)
{
    if (auto* store = props_.getUserSettings())
        store->setValue(juce::String("feature.") + featureKey(feature), enabled);
}

double Preferences::getNewPatchTempo()
{
    if (auto* store = props_.getUserSettings())
        return engine::transport::clampTempo(
            store->getDoubleValue("tempo.newPatchTempo", engine::transport::kDefaultTempo));
    return engine::transport::kDefaultTempo;
}

void Preferences::setNewPatchTempo(double bpm)
{
    if (auto* store = props_.getUserSettings())
        store->setValue("tempo.newPatchTempo", engine::transport::clampTempo(bpm));
}

std::pair<int, int> Preferences::getNewPatchTimeSignature()
{
    int numerator = 4;
    int denominator = 4;
    if (auto* store = props_.getUserSettings())
    {
        numerator = store->getIntValue("tempo.newPatchNumerator", 4);
        denominator = store->getIntValue("tempo.newPatchDenominator", 4);
    }
    if (!engine::transport::isValidTimeSignature(numerator, denominator))
        return {4, 4};
    return {numerator, denominator};
}

void Preferences::setNewPatchTimeSignature(int numerator, int denominator)
{
    if (!engine::transport::isValidTimeSignature(numerator, denominator))
        return;
    if (auto* store = props_.getUserSettings())
    {
        store->setValue("tempo.newPatchNumerator", numerator);
        store->setValue("tempo.newPatchDenominator", denominator);
    }
}

void Preferences::applyNewPatchDefaults(Patch& patch)
{
    patch.tempo = getNewPatchTempo();
    const auto [numerator, denominator] = getNewPatchTimeSignature();
    patch.timeSigNumerator = numerator;
    patch.timeSigDenominator = denominator;
}

bool Preferences::getMetronomeClickButton()
{
    if (auto* store = props_.getUserSettings())
        return store->getBoolValue("metronome.clickButton", true);
    return true;
}

void Preferences::setMetronomeClickButton(bool shown)
{
    if (auto* store = props_.getUserSettings())
        store->setValue("metronome.clickButton", shown);
}

float Preferences::getNewMetronomeLevel()
{
    double db = -6.0;
    if (auto* store = props_.getUserSettings())
        db = store->getDoubleValue("metronome.newLevel", -6.0);
    return static_cast<float>(juce::jlimit(-40.0, 6.0, db));
}

void Preferences::setNewMetronomeLevel(float db)
{
    if (auto* store = props_.getUserSettings())
        store->setValue("metronome.newLevel", static_cast<double>(juce::jlimit(-40.0f, 6.0f, db)));
}

juce::File Preferences::getLooperFolder()
{
    if (auto* store = props_.getUserSettings())
        if (const auto path = store->getValue("looper.folder"); juce::File::isAbsolutePath(path))
            return juce::File(path);
    return {};
}

void Preferences::setLooperFolder(const juce::File& folder)
{
    if (auto* store = props_.getUserSettings())
        store->setValue("looper.folder", folder == juce::File() ? juce::String() : folder.getFullPathName());
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
    auto* store = props_.getUserSettings();
    if (store == nullptr || !store->needsToBeSaved())
        return;
    // Atomic (M5 U2) and owner-only on POSIX (M5 U7, AC-SE-24): a crash
    // mid-save leaves the old settings whole, and no other user can read them.
    if (AtomicFileWriter::writeText(file_, settingsXml(*store)->toString(), {}, /*ownerOnly*/ true).wasOk())
        store->setNeedsToBeSaved(false);
}

} // namespace pedalboard3::session
