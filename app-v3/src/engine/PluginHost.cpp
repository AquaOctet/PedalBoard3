#include "PluginHost.h"

#include "KnownProblemPlugins.h"
#include "processors/AudioRecorderProcessor.h"
#include "processors/FilePlayerProcessor.h"
#include "processors/LevelProcessor.h"
#include "processors/LooperProcessor.h"
#include "processors/MetronomeProcessor.h"
#include "processors/OutputToggleProcessor.h"
#include "processors/VuMeterProcessor.h"

namespace pedalboard3::engine
{

PluginHost::PluginHost()
{
    juce::addDefaultFormatsToManager(formats_);
}

PluginHost::~PluginHost() = default;

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

std::optional<juce::PluginDescription> PluginHost::resolve(const juce::PluginDescription& saved,
                                                           const juce::String& fileHint) const
{
    const auto sameId = [&saved](const juce::PluginDescription& d)
    {
        return (saved.uniqueId != 0 && d.uniqueId == saved.uniqueId) ||
               (saved.deprecatedUid != 0 && d.deprecatedUid == saved.deprecatedUid);
    };
    const auto sameName = [&saved](const juce::PluginDescription& d)
    {
        return saved.name.isNotEmpty() && d.name == saved.name &&
               (saved.manufacturerName.isEmpty() || d.manufacturerName == saved.manufacturerName);
    };

    // The best match wins: the ID counts most, then the name, and the path hint
    // only breaks a tie. Neither the ID nor the name matching: not this plugin.
    const juce::ScopedReadLock sl(listLock_);
    const auto types = knownList_.getTypes();
    int best = -1;
    int bestScore = 0;
    for (int i = 0; i < types.size(); ++i)
    {
        const auto& d = types.getReference(i);
        if (saved.pluginFormatName.isNotEmpty() && d.pluginFormatName != saved.pluginFormatName)
            continue;
        const bool id = sameId(d);
        const bool name = sameName(d);
        if (!id && !name)
            continue;
        const bool path = fileHint.isNotEmpty() && d.fileOrIdentifier == fileHint;
        const int score = (id ? 4 : 0) + (name ? 2 : 0) + (path ? 1 : 0);
        if (score > bestScore)
        {
            best = i;
            bestScore = score;
        }
    }
    if (best < 0)
        return std::nullopt;
    return types.getReference(best);
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
    types.add({"FilePlayerProcessor", "File Player"});
    types.add({"AudioRecorderProcessor", "Audio Recorder"});
    types.add({"MetronomeProcessor", "Metronome"});
    types.add({"LooperProcessor", "Looper"});
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
    else if (type == "FilePlayerProcessor")
        proc = std::make_unique<processors::FilePlayerProcessor>(diskThread_);
    else if (type == "AudioRecorderProcessor")
        proc = std::make_unique<processors::AudioRecorderProcessor>(diskThread_);
    else if (type == "MetronomeProcessor")
        proc = std::make_unique<processors::MetronomeProcessor>(diskThread_);
    else if (type == "LooperProcessor")
        proc = std::make_unique<processors::LooperProcessor>(diskThread_);

    if (proc == nullptr)
    {
        errorOut = "Unknown internal node type: " + type;
        return nullptr;
    }

    proc->setRateAndBufferSizeDetails(sampleRate, blockSize);
    return proc;
}

void PluginHost::setUserDataFolder(juce::File folder)
{
    userDataFolder_ = std::move(folder);
}

juce::File PluginHost::getCrashedPluginsFile() const
{
    auto folder =
        userDataFolder_ != juce::File()
            ? userDataFolder_
            : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Pedalboard 3");
    folder.createDirectory();
    return folder.getChildFile("crashed_plugins.xml");
}

PluginHost::CrashList PluginHost::readCrashList() const
{
    constexpr juce::int64 kMaxBytes = 1024 * 1024;
    constexpr int kMaxLineLength = 4096;

    // A plugin as JUCE names it: a file, or an identifier such as an Audio
    // Unit's "AudioUnit:Effects/aufx,dely,appl".
    const auto namesAPlugin = [](const juce::String& line)
    {
        if (line.length() > kMaxLineLength)
            return false;
        for (auto p = line.getCharPointer(); !p.isEmpty(); ++p)
            if (*p < 0x20)
                return false;
        if (juce::File::isAbsolutePath(line))
            return true;
        const auto format = line.upToFirstOccurrenceOf(":", false, false);
        return line.containsChar(':') && format.isNotEmpty() &&
               format.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    };

    CrashList list;
    const auto file = getCrashedPluginsFile();
    if (!file.existsAsFile())
        return list;
    if (file.getSize() > kMaxBytes)
    {
        list.error = "it's larger than 1 MB";
        return list;
    }

    int lineNumber = 0;
    for (const auto& line : juce::StringArray::fromLines(file.loadFileAsString()))
    {
        ++lineNumber;
        const auto entry = line.trim();
        if (entry.isEmpty())
            continue;
        if (!namesAPlugin(entry))
        {
            list.paths.clear();
            list.error = "line " + juce::String(lineNumber) + " doesn't name a plugin";
            return list;
        }
        list.paths.addIfNotAlreadyThere(entry);
    }
    return list;
}

juce::File PluginHost::getScanInFlightFile() const
{
    return getCrashedPluginsFile().getSiblingFile("scan_in_flight.txt");
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
