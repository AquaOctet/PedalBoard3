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

int PluginHost::scanDirectory(const juce::FileSearchPath& path)
{
    const juce::ScopedWriteLock sl(listLock_);
    const int beforeCount = knownList_.getNumTypes();
    const juce::File noDeadManPedal{};

    for (int f = 0; f < formats_.getNumFormats(); ++f)
    {
        auto* format = formats_.getFormat(f);
        if (format == nullptr || !format->canScanForPlugins())
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

} // namespace pedalboard3::engine
