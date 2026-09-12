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
//   pb3-scanner-helper <plugin-file-path>
//
// Behaviour:
//   - Registers every JUCE-known plugin format (VST3 always, AudioUnit
//     on macOS, etc — same set the parent host uses).
//   - For each format that says canScanForPlugins(), tries
//     findAllTypesForFile(path), with a message loop running
//     (engine/ScanInMessageLoop.h): an AUv3 is scanned off the message
//     thread, which JUCE requires, and every other format on it (#3).
//   - Writes the resulting PluginDescriptions to stdout as a concatenated
//     XML stream wrapped in <PB3_SCAN_RESULT>...</PB3_SCAN_RESULT>.
//   - Exits 0 on a clean scan (even if no descriptors were found —
//     "this file isn't a recognisable plugin" is not a crash).
//   - If the plugin crashes during scan, the OS terminates this helper
//     with SIGSEGV / SIGABRT / SIGILL / etc. The parent observes the
//     non-zero exit and treats the path as crashed (adds to blacklist).
//
// This file deliberately uses JUCE_CONSOLE_APPLICATION_MAIN so we don't
// pull in any GUI dependency. Helper builds as a console binary that
// ships alongside the main app.

#include "engine/ScanInMessageLoop.h" // the scan, with a message loop running (#3)
#include "engine/ScanProtocol.h"      // kScanOkSentinel — shared with engine::PluginScanner

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <iostream>

namespace
{
constexpr int kExitOk = 0;
constexpr int kExitUsage = 64; // EX_USAGE per sysexits.h
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: pb3-scanner-helper <plugin-file-path>\n", stderr);
        return kExitUsage;
    }

    const juce::String pluginPath{juce::CharPointer_UTF8(argv[1])};
    if (pluginPath.isEmpty())
    {
        std::fputs("pb3-scanner-helper: empty plugin path\n", stderr);
        return kExitUsage;
    }

    {
        // This thread is the message thread, and the scan runs a message
        // loop on it until it's done (engine/ScanInMessageLoop.h): an AUv3 is
        // created asynchronously and needs the loop free, or JUCE's AU format
        // silently finds nothing (#3).
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
        // the parent sees it in the exit code.
        juce::OwnedArray<juce::PluginDescription> descriptions;
        pedalboard3::engine::scanFileInMessageLoop(formats.getFormats(), pluginPath, descriptions);

        juce::XmlElement root("PB3_SCAN_RESULT");
        for (const auto* desc : descriptions)
        {
            if (auto descXml = desc->createXml())
                root.addChildElement(descXml.release());
        }

        // Print the XML to stdout. The parent reads this stream verbatim
        // and parses with juce::parseXML.
        const auto serialised = root.toString(juce::XmlElement::TextFormat{}.singleLine());
        std::cout << serialised.toStdString() << std::endl;

        // Block closes here: ScopedJuceInitialiser_GUI tears JUCE down, and
        // a plugin's lingering worker threads / static destructors often
        // crash in exactly this window (Kontakt's BusinessAnalytics
        // async-deinit abort). On POSIX a signal-kill reports exit code 0
        // to the parent, so the crash would otherwise go unnoticed.
    }

    // Completion sentinel — the parent (PluginScanner::parseHelperOutput)
    // refuses to trust the result without it, even on exit code 0. Printed
    // only after JUCE teardown survived. Shared definition (no drift).
    std::cout << pedalboard3::engine::kScanOkSentinel << std::endl;

    return kExitOk;
}
