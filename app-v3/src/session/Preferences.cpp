#include "Preferences.h"

namespace pedalboard3::session
{

namespace
{

constexpr auto kAudioDeviceStateKey = "audioDeviceState";
constexpr auto kKnownPluginListKey = "knownPluginList";
constexpr auto kActivePluginDescriptionKey = "activePluginDescription";
constexpr auto kActivePatchKey = "activePatch";

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

namespace
{

juce::String channelAttr(int channel)
{
    return juce::String(channel);
}

std::unique_ptr<juce::XmlElement> serialisePatch(const Patch& p)
{
    auto root = std::make_unique<juce::XmlElement>("ACTIVE_PATCH");
    root->setAttribute("name", p.name);

    auto* nodesEl = root->createNewChildElement("NODES");
    for (const auto& n : p.nodes)
    {
        auto* nodeEl = nodesEl->createNewChildElement("NODE");
        nodeEl->setAttribute("id", n.id);
        nodeEl->setAttribute("bypass", n.bypass);
        nodeEl->setAttribute("customName", n.customName);
        if (auto descXml = n.desc.createXml())
        {
            // Re-tag from <PLUGIN> to <IDENTITY> so the schema reads
            // cleanly (master plan §4.5 idiom).
            descXml->setTagName("IDENTITY");
            nodeEl->addChildElement(descXml.release());
        }
    }

    auto* connsEl = root->createNewChildElement("CONNECTIONS");
    for (const auto& c : p.connections)
    {
        auto* connEl = connsEl->createNewChildElement("CONN");
        connEl->setAttribute("src", c.src);
        connEl->setAttribute("srcChan", channelAttr(c.srcChannel));
        connEl->setAttribute("dst", c.dst);
        connEl->setAttribute("dstChan", channelAttr(c.dstChannel));
    }

    return root;
}

std::unique_ptr<Patch> deserialisePatch(const juce::XmlElement& root)
{
    if (!root.hasTagName("ACTIVE_PATCH"))
        return nullptr;

    auto patch = std::make_unique<Patch>();
    patch->name = root.getStringAttribute("name", "Untitled");

    if (auto* nodesEl = root.getChildByName("NODES"))
    {
        for (auto* nodeEl : nodesEl->getChildIterator())
        {
            if (!nodeEl->hasTagName("NODE"))
                continue;
            PatchNode n;
            n.id = nodeEl->getIntAttribute("id");
            n.bypass = nodeEl->getBoolAttribute("bypass");
            n.customName = nodeEl->getStringAttribute("customName");
            if (auto* identityEl = nodeEl->getChildByName("IDENTITY"))
            {
                // Restore expects a <PLUGIN> root for loadFromXml.
                juce::XmlElement asPlugin(*identityEl);
                asPlugin.setTagName("PLUGIN");
                if (!n.desc.loadFromXml(asPlugin))
                    return nullptr;
            }
            patch->nodes.push_back(std::move(n));
        }
    }

    if (auto* connsEl = root.getChildByName("CONNECTIONS"))
    {
        for (auto* connEl : connsEl->getChildIterator())
        {
            if (!connEl->hasTagName("CONN"))
                continue;
            PatchConnection c;
            c.src = connEl->getIntAttribute("src");
            c.srcChannel = connEl->getIntAttribute("srcChan");
            c.dst = connEl->getIntAttribute("dst");
            c.dstChannel = connEl->getIntAttribute("dstChan");
            patch->connections.push_back(c);
        }
    }

    return patch;
}

} // namespace

std::unique_ptr<Patch> Preferences::getActivePatch()
{
    auto* store = props_.getUserSettings();
    if (store == nullptr)
        return nullptr;

    auto xml = readXml(*store, kActivePatchKey);
    if (xml == nullptr)
        return nullptr;

    auto patch = deserialisePatch(*xml);
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

    if (auto xml = serialisePatch(patch))
        store->setValue(kActivePatchKey, xml.get());
}

void Preferences::clearActivePatch()
{
    if (auto* store = props_.getUserSettings())
        store->removeValue(kActivePatchKey);
}

void Preferences::flush()
{
    props_.saveIfNeeded();
}

} // namespace pedalboard3::session
