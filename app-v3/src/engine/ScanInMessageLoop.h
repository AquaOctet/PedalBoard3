#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <thread>
#include <vector>

namespace pedalboard3::engine
{

// Find every plugin type in `path`, with each format that can scan it. Call it
// on the message thread, with no message loop running.
//
//   - Most formats (VST3, and CLAP when it's built) are scanned right here on
//     the message thread, exactly as the helper always did.
//   - A format that needs the message thread free while an instance is created
//     (AUv3) is scanned on a worker thread while this thread runs the message
//     loop, the way JUCE's own out-of-process scanner works (extras/
//     AudioPluginHost, HostStartup.cpp). JUCE's
//     AudioUnitPluginFormat::findAllTypesForFile returns nothing for an AUv3
//     on the message thread, which is how the helper silently skipped every
//     AUv3 (#3).
//
// The loop runs at most once per call, since a stopped loop can't restart: for
// all such formats together, and only when one of them might contain the file.
// A VST3-only scan never starts it. The worker never waits on the message
// thread, so a loop that fails to start can't deadlock the scan. On macOS the
// loop needs NSApp, which ScopedJuceInitialiser_GUI provides.
//
// Header-only so pb3-scanner-helper can use it without linking the engine.
inline void scanFileInMessageLoop(const juce::Array<juce::AudioPluginFormat*>& formats,
                                  const juce::String& path,
                                  juce::OwnedArray<juce::PluginDescription>& results)
{
    auto* messageManager = juce::MessageManager::getInstance();
    jassert(messageManager->isThisTheMessageThread());

    juce::PluginDescription probe;
    probe.fileOrIdentifier = path;
    probe.uniqueId = probe.deprecatedUid = 0;

    std::vector<juce::AudioPluginFormat*> needFreeMessageThread;
    for (auto* format : formats)
    {
        if (format == nullptr || !format->canScanForPlugins() || !format->fileMightContainThisPluginType(path))
            continue;
        if (format->requiresUnblockedMessageThreadDuringCreation(probe))
            needFreeMessageThread.push_back(format);
        else
            format->findAllTypesForFile(results, path); // here, on the message thread
    }

    if (needFreeMessageThread.empty())
        return;

    // From here only the worker touches `results`; this thread runs the loop
    // until the worker posts the quit.
    std::thread worker(
        [&needFreeMessageThread, &path, &results, messageManager]
        {
            for (auto* format : needFreeMessageThread)
                format->findAllTypesForFile(results, path);
            messageManager->stopDispatchLoop(); // safe from any thread: it posts the quit
        });
    messageManager->runDispatchLoop();
    worker.join();
}

} // namespace pedalboard3::engine
