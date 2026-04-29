#pragma once

#include <juce_core/juce_core.h>

#include <string_view>

namespace pedalboard3::engine
{

// Plugins that crash the host on scan / construction / destruction.
//
// JUCE's PluginListComponent scans plugins in-process: each .vst3 in
// the user's search path is dlopen'd and its factory's createInstance()
// is called to read the descriptor. Some plugins crash during their
// own construction (license-check threads, online-analytics modules,
// pure-virtual calls in their refresh paths) and there's nothing we
// can do once that crash fires — it kills our process.
//
// Until v3.1's out-of-process PluginScanner (master plan §4.2.A) lands,
// the only way to keep the host alive is to NEVER open these plugins.
// This list is consulted by PluginHost::applyKnownProblemBlacklist
// at app launch — every VST3 path matching one of these basenames is
// added to the KnownPluginList blacklist before the scanner ever sees
// it. JUCE's scanAndAddFile checks the blacklist and skips matches.
//
// Each entry carries the reason it's listed so future maintainers
// know whether the plugin can be removed (e.g. when a fixed version
// ships, or after we move to out-of-process scanning).
//
// Adding entries: only add plugins that *crash the entire host* during
// scan, not plugins that merely fail to load (those are JUCE's failedFiles
// list — already handled). Confirm with a crash log before adding.
//
// Removing entries: if a plugin vendor ships a fix, the user can manually
// remove its blacklist entry through the Plugin List dialog. This list
// is the FLOOR — plugins always start blacklisted; user can opt back in.
struct KnownProblemPlugin
{
    std::string_view bundleBasename; // e.g. "Current.vst3"
    std::string_view reason;
};

inline constexpr KnownProblemPlugin kKnownProblemPlugins[] = {
    // Minimal Audio's "Current" synth. License-refresh path
    // (MKURequestManager::sendEvent → __cxa_pure_virtual) aborts the
    // process during the plugin's construction in scan. Confirmed on
    // macOS 26.4.1 with com.Minimal.current 2.0.1. Reproducible in
    // every host that uses JUCE's in-process scanner.
    {"Current.vst3", "Minimal Audio Current — pure-virtual call during license refresh"},

    // Native Instruments Kontakt 7. Asynchronous BusinessAnalytics
    // deinit on a background thread (ni::uret::HttpClient::cancelAndReset
    // → pplx::Task::Wait throws unhandled exception → std::terminate)
    // aborts the process some time AFTER scanAndAddFile returns
    // successfully. This means JUCE's dead-man's-pedal points at a
    // different plugin by the time the abort fires, so file-based
    // protection can't catch it.
    {"Kontakt.vst3", "Native Instruments Kontakt — async destructor crash on Thread 30"},
    {"Kontakt 7.vst3", "Native Instruments Kontakt 7 — async destructor crash on Thread 30"},
};

} // namespace pedalboard3::engine
