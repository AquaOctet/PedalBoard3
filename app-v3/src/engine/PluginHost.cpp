#include "PluginHost.h"

namespace pedalboard3::engine
{

PluginHost::PluginHost()
{
    formats_.addDefaultFormats();
}

PluginHost::~PluginHost() = default;

int PluginHost::getNumFormats() const noexcept
{
    return formats_.getNumFormats();
}

juce::StringArray PluginHost::getFormatNames() const
{
    juce::StringArray names;
    for (int f = 0; f < formats_.getNumFormats(); ++f)
    {
        if (auto* format = formats_.getFormat(f))
            names.add(format->getName());
    }
    return names;
}

juce::FileSearchPath PluginHost::getDefaultSearchPathForFormat(const juce::String& formatName) const
{
    for (int f = 0; f < formats_.getNumFormats(); ++f)
    {
        auto* format = formats_.getFormat(f);
        if (format != nullptr && format->getName() == formatName)
            return format->getDefaultLocationsToSearch();
    }
    return {};
}

int PluginHost::scanDefaultPaths()
{
    int totalAdded = 0;
    for (int f = 0; f < formats_.getNumFormats(); ++f)
    {
        auto* format = formats_.getFormat(f);
        if (format == nullptr || !format->canScanForPlugins())
            continue;

        const auto path = format->getDefaultLocationsToSearch();
        if (path.getNumPaths() == 0)
            continue;

        // Scan only this format with its own paths — never AU on a VST3
        // path or vice versa. AU's scanner ignores the FileSearchPath, so
        // including it implicitly would walk every installed AU plugin.
        totalAdded += scanDirectory(path, juce::StringArray{format->getName()});
    }
    return totalAdded;
}

int PluginHost::scanDirectory(const juce::FileSearchPath& path, const juce::StringArray& formatNames)
{
    const juce::ScopedWriteLock sl(listLock_);
    const int beforeCount = knownList_.getNumTypes();
    const juce::File noDeadManPedal{};

    for (int f = 0; f < formats_.getNumFormats(); ++f)
    {
        auto* format = formats_.getFormat(f);
        if (format == nullptr || !format->canScanForPlugins())
            continue;

        if (!formatNames.isEmpty() && !formatNames.contains(format->getName()))
            continue;

        juce::PluginDirectoryScanner scanner(knownList_,
                                             *format,
                                             path,
                                             /*recursive*/ true,
                                             noDeadManPedal);

        juce::String currentFile;
        while (scanner.scanNextFile(/*dontRescanIfAlreadyInList*/ true, currentFile))
        {
            // Scanner walks its own queue; we just spin until it returns false.
        }
    }

    return knownList_.getNumTypes() - beforeCount;
}

void PluginHost::loadKnownList(const juce::XmlElement& xml)
{
    const juce::ScopedWriteLock sl(listLock_);
    knownList_.recreateFromXml(xml);
}

std::unique_ptr<juce::XmlElement> PluginHost::createKnownListXml() const
{
    const juce::ScopedReadLock sl(listLock_);
    return knownList_.createXml();
}

void PluginHost::clearKnownPlugins()
{
    const juce::ScopedWriteLock sl(listLock_);
    knownList_.clear();
}

juce::Array<juce::PluginDescription> PluginHost::getKnownPlugins() const
{
    const juce::ScopedReadLock sl(listLock_);
    return knownList_.getTypes();
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance(const juce::PluginDescription& desc,
                                                                      double sampleRate,
                                                                      int blockSize,
                                                                      juce::String& errorOut)
{
    return formats_.createPluginInstance(desc, sampleRate, blockSize, errorOut);
}

juce::File PluginHost::getCrashedPluginsFile() const
{
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Pedalboard3");
    appDir.createDirectory();
    return appDir.getChildFile("crashed_plugins.xml");
}

} // namespace pedalboard3::engine
