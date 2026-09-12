#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <thread>

namespace pedalboard3::engine
{

// Find every plugin type in `path`, with each format that can scan it, while a
// message loop runs: the way JUCE's own out-of-process scanner works
// (extras/AudioPluginHost, HostStartup.cpp). Call it on the message thread
// with no loop running; it runs the loop until the scan is done.
//
//   - A format that needs the message thread free while an instance is
//     created (AUv3) is scanned on a worker thread. JUCE's
//     AudioUnitPluginFormat::findAllTypesForFile returns nothing for an AUv3
//     on the message thread, which is how the helper silently skipped every
//     AUv3 before (#3).
//   - Every other format (VST3, and CLAP when it's built) is scanned on the
//     message thread as before, one format at a time, in order.
//
// `results` is only ever touched by one thread at a time: the worker waits
// while a message-thread scan runs. A plugin that crashes still takes the
// whole process down, which is what the parent watches for.
//
// Header-only so pb3-scanner-helper can use it without linking the engine.
inline void scanFileInMessageLoop(const juce::Array<juce::AudioPluginFormat*>& formats,
                                  const juce::String& path,
                                  juce::OwnedArray<juce::PluginDescription>& results)
{
    auto* messageManager = juce::MessageManager::getInstance();
    jassert(messageManager->isThisTheMessageThread());

    struct Job
    {
        juce::AudioPluginFormat& format;
        const juce::String& path;
        juce::OwnedArray<juce::PluginDescription>& results;

        static void* run(void* job)
        {
            auto& self = *static_cast<Job*>(job);
            self.format.findAllTypesForFile(self.results, self.path);
            return nullptr;
        }
    };

    std::thread worker(
        [&formats, &path, &results, messageManager]
        {
            for (auto* format : formats)
            {
                if (format == nullptr || !format->canScanForPlugins() || !format->fileMightContainThisPluginType(path))
                    continue;

                juce::PluginDescription probe;
                probe.fileOrIdentifier = path;
                probe.uniqueId = probe.deprecatedUid = 0;
                if (format->requiresUnblockedMessageThreadDuringCreation(probe))
                {
                    format->findAllTypesForFile(results, path); // here, with the message thread free
                }
                else
                {
                    Job job{*format, path, results};
                    messageManager->callFunctionOnMessageThread(&Job::run, &job);
                }
            }
            messageManager->stopDispatchLoop(); // safe from any thread: it posts the quit
        });

    messageManager->runDispatchLoop();
    worker.join();
}

} // namespace pedalboard3::engine
