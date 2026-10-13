// pb3-scanner-helper — out-of-process plugin descriptor extractor.
//
// Master plan §4.2.A: in-process plugin scanning is dangerous because
// every plugin's constructor runs in the host's address space, and a
// crashing plugin (license-refresh threads calling pure virtuals,
// async destructors that throw, audio-callback init bugs) takes the
// host with it. The host can't catch SIGSEGV/SIGABRT, and the
// dead-man's-pedal heuristic only works after the user has already
// suffered the crash. Production hosts (Reaper, Bitwig, Logic, Ableton)
// all scan in subprocesses.
//
// This binary is the subprocess. Usage:
//
//   pb3-scanner-helper <plugin-file-path> [<result-file>]
//
// Behaviour:
//   - Registers every JUCE-known plugin format (VST3 always, AudioUnit
//     on macOS, etc — same set the parent host uses).
//   - For each format that says canScanForPlugins(), tries
//     findAllTypesForFile(path) (engine/ScanInMessageLoop.h): on the
//     message thread, except an AUv3, which JUCE only scans off it with a
//     message loop running (#3).
//   - Writes the resulting PluginDescriptions as one line of XML wrapped in
//     <PB3_SCAN_RESULT>...</PB3_SCAN_RESULT>, then the completion sentinel,
//     to <result-file> (M5 U6, plan KD8: the parent starts the helper with no
//     pipes, so nothing it or a plugin prints can block it). Without a result
//     file, both go to stdout, for running it by hand.
//   - Exits 0 on a clean scan (even if no descriptors were found —
//     "this file isn't a recognisable plugin" is not a crash).
//   - If the plugin crashes during scan, the OS terminates this helper
//     with SIGSEGV / SIGABRT / SIGILL / etc. The parent observes the
//     crash (a non-zero exit, or no sentinel) and blacklists the path.
//
// A console binary with its own main() and no GUI of its own, built and
// shipped beside the app.

#include "engine/ScanInMessageLoop.h" // the scan, with a message loop running (#3)
#include "engine/ScanProtocol.h"      // kScanOkSentinel — shared with engine::PluginScanner

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <iostream>
#include <string>

namespace
{
constexpr int kExitOk = 0;
constexpr int kExitUsage = 64; // EX_USAGE per sysexits.h
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::fputs("Usage: pb3-scanner-helper <plugin-file-path> [<result-file>]\n", stderr);
        return kExitUsage;
    }

    const juce::String pluginPath{juce::CharPointer_UTF8(argv[1])};
    if (pluginPath.isEmpty())
    {
        std::fputs("pb3-scanner-helper: empty plugin path\n", stderr);
        return kExitUsage;
    }
    const juce::String resultPath = argc == 3 ? juce::String(juce::CharPointer_UTF8(argv[2])) : juce::String();

    std::string serialised;
    {
        // This thread is the message thread. For an AUv3 the scan runs a
        // message loop on it (engine/ScanInMessageLoop.h): an AUv3 is created
        // asynchronously and needs the loop free, or JUCE's AU format silently
        // finds nothing (#3).
        const juce::ScopedJuceInitialiser_GUI scopedJuce;
#if JUCE_MAC
        // Running the loop starts NSApp; don't give the helper a Dock icon.
        juce::Process::setDockIconVisible(false);
#endif

        juce::AudioPluginFormatManager formats;
        formats.addDefaultFormats();

        // findAllTypesForFile is the call that may crash inside the plugin's
        // own constructor (Current.vst3's MKURequestManager pure-virtual call,
        // etc). If it crashes, the OS kills this process with a signal, and
        // the parent sees it.
        juce::OwnedArray<juce::PluginDescription> descriptions;
        pedalboard3::engine::scanFileInMessageLoop(formats.getFormats(), pluginPath, descriptions);

        juce::XmlElement root("PB3_SCAN_RESULT");
        for (const auto* desc : descriptions)
        {
            if (auto descXml = desc->createXml())
                root.addChildElement(descXml.release());
        }
        serialised = root.toString(juce::XmlElement::TextFormat{}.singleLine()).toStdString();

        // Block closes here: ScopedJuceInitialiser_GUI tears JUCE down, and
        // a plugin's lingering worker threads / static destructors often
        // crash in exactly this window (Kontakt's BusinessAnalytics
        // async-deinit abort). On POSIX a signal-kill reports exit code 0
        // to the parent, so the crash would otherwise go unnoticed.
    }

    // Completion sentinel — the parent (PluginScanner::parseHelperOutput)
    // refuses to trust the result without it, even on exit code 0. Written
    // only after JUCE teardown survived. Shared definition (no drift).
    const auto text = serialised + "\n" + pedalboard3::engine::kScanOkSentinel + "\n";
    if (resultPath.isEmpty())
        std::cout << text << std::flush;
    else if (!juce::File(resultPath)
                  .replaceWithText(juce::String::fromUTF8(text.data(), static_cast<int>(text.size()))))
        return kExitUsage; // no result: the parent counts it as a crash, having nothing to trust

    return kExitOk;
}
