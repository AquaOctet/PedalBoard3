#include "PluginHost.h"

#include "KnownProblemPlugins.h"
#include "processors/LevelProcessor.h"
#include "processors/OutputToggleProcessor.h"
#include "processors/VuMeterProcessor.h"

namespace pedalboard3::engine
{

PluginHost::PluginHost()
{
    formats_.addDefaultFormats();

    // Wire the instrument-filter listener BEFORE any code path can mutate
    // knownList_. App::initialise calls loadKnownList right after PluginHost
    // construction; that fires a change message and we sweep any saved
    // instruments out of the persisted list on the very first launch after
    // upgrade.
    knownList_.addChangeListener(this);
}

PluginHost::~PluginHost()
{
    knownList_.removeChangeListener(this);
}

void PluginHost::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source != &knownList_ || sweepingInstruments_)
        return;

    const juce::ScopedWriteLock sl(listLock_);
    sweepingInstruments_ = true;

    // Snapshot getTypes() (returns juce::Array<PluginDescription> by value)
    // so iteration stays stable while removeType mutates the live list.
    // removeType matches by uniqueId; copying descriptors by value is cheap.
    for (const auto& desc : knownList_.getTypes())
    {
        if (!desc.isInstrument)
            continue;

        knownList_.removeType(desc);

        // Blacklist the file so future scans (juce::PluginDirectoryScanner
        // honours the blacklist) skip it entirely. addToBlacklist is a
        // no-op if the path is already present; the !contains guard is
        // belt-and-braces and avoids an extra change-message round trip.
        if (!knownList_.getBlacklistedFiles().contains(desc.fileOrIdentifier))
            knownList_.addToBlacklist(desc.fileOrIdentifier);
    }

    sweepingInstruments_ = false;
}

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

juce::Array<juce::PluginDescription> PluginHost::getKnownEffects() const
{
    const juce::ScopedReadLock sl(listLock_);
    juce::Array<juce::PluginDescription> result;
    for (const auto& desc : knownList_.getTypes())
        if (!desc.isInstrument)
            result.add(desc);
    return result;
}

bool PluginHost::isDescriptorSafeForChain(const juce::PluginDescription& desc) const
{
    // An empty descriptor can never be instantiated (createInstance would fail
    // deep in the add/build worker). Drop it here so it degrades gracefully —
    // the chain is rebuilt over the survivors — instead of aborting the whole
    // load. This also covers a forward-compat hazard: an internal node written
    // by a newer build (kind="internal", no <IDENTITY>) read by an older binary
    // that lacks the kind discriminator deserializes to an empty-desc plugin
    // node; treating it as unsafe drops it cleanly rather than failing the load.
    if (desc.fileOrIdentifier.isEmpty())
        return false;

    // Instruments are out of scope for v3.0 and frequently crash on
    // construction (heavy license/online-refresh ctors) — never restore one.
    if (desc.isInstrument)
        return false;

    // Blacklisted = known crash-on-construct (or a prior scan failure). The
    // restore/load path calls createInstance directly, so we must honour the
    // blacklist here just as the scanner does.
    const juce::ScopedReadLock sl(listLock_);
    if (knownList_.getBlacklistedFiles().contains(desc.fileOrIdentifier))
        return false;

    return true;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance(const juce::PluginDescription& desc,
                                                                      double sampleRate,
                                                                      int blockSize,
                                                                      juce::String& errorOut)
{
    return formats_.createPluginInstance(desc, sampleRate, blockSize, errorOut);
}

juce::Array<InternalNodeType> PluginHost::getInternalNodeTypes() const
{
    // The single source of truth for which built-in nodes exist. Keep in sync
    // with the createInternalInstance switch below. (juce::Array doesn't take a
    // nested brace-init list the way std::vector does, so add() each entry.)
    juce::Array<InternalNodeType> types;
    types.add({"LevelProcessor", "Level"});
    types.add({"VuMeterProcessor", "VU Meter"});
    types.add({"OutputToggleProcessor", "Output Toggle"});
    return types;
}

std::unique_ptr<juce::AudioProcessor> PluginHost::createInternalInstance(const juce::String& type,
                                                                         double sampleRate,
                                                                         int blockSize,
                                                                         juce::String& errorOut)
{
    std::unique_ptr<juce::AudioProcessor> proc;
    if (type == "LevelProcessor")
        proc = std::make_unique<processors::LevelProcessor>();
    else if (type == "VuMeterProcessor")
        proc = std::make_unique<processors::VuMeterProcessor>();
    else if (type == "OutputToggleProcessor")
        proc = std::make_unique<processors::OutputToggleProcessor>();

    if (proc == nullptr)
    {
        errorOut = "Unknown internal node type: " + type;
        return nullptr;
    }

    proc->setRateAndBufferSizeDetails(sampleRate, blockSize);
    return proc;
}

juce::File PluginHost::getCrashedPluginsFile() const
{
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Pedalboard3");
    appDir.createDirectory();
    return appDir.getChildFile("crashed_plugins.xml");
}

bool PluginHost::applyKnownProblemBlacklist()
{
    const juce::ScopedWriteLock sl(listLock_);

    bool anyAdded = false;

    // Walk every registered format (VST3, AU on macOS, etc.) and ask
    // it to enumerate plugin paths under its default search path. For
    // each path, check if the bundle basename matches a known-problem
    // entry and blacklist it.
    for (int i = 0; i < formats_.getNumFormats(); ++i)
    {
        auto* format = formats_.getFormat(i);
        if (format == nullptr)
            continue;

        const auto searchPath = format->getDefaultLocationsToSearch();
        const auto pluginPaths = format->searchPathsForPlugins(searchPath, /*recursive*/ true, /*allowAsync*/ false);

        for (const auto& path : pluginPaths)
        {
            const auto basename = juce::File(path).getFileName();

            for (const auto& entry : kKnownProblemPlugins)
            {
                if (basename == juce::String(entry.bundleBasename.data(), entry.bundleBasename.size()))
                {
                    if (!knownList_.getBlacklistedFiles().contains(path))
                    {
                        knownList_.addToBlacklist(path);
                        anyAdded = true;
                    }
                    break;
                }
            }
        }
    }

    return anyAdded;
}

} // namespace pedalboard3::engine
