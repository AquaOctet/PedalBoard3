#pragma once

#include <juce_core/juce_core.h>

#include <string_view>

namespace pedalboard3::engine
{

// Plugins that crash the host on scan / construction / destruction.
//
// Any scan must construct the plugin to read its descriptor: the .vst3
// is dlopen'd and its factory's createInstance() is called. Some plugins
// crash during that construction (license-check threads, online-analytics
// modules, pure-virtual calls in their refresh paths) and there's nothing
// we can do once that crash fires — it kills the process doing the load.
//
// This list is the launch-time, pre-construction, PATH-based floor and it
// is NOT made redundant by the out-of-process PluginScanner (master plan
// §4.2.A), which shipped in v3.0. The two cover different failure windows:
//   * The OOP scanner protects against crashes that happen *inside a
//     subprocess we can lose* — but it only runs on the Rescan path and it
//     SKIPS already-blacklisted paths (PluginScanner::scanAllDefaultPaths).
//   * This floor protects against crashes that fire *before any descriptor
//     exists at all*: Current.vst3 aborts during createInstance, so no
//     descriptor is ever added and no post-add safety net (the instrument
//     sweep in PluginHost::changeListenerCallback, JUCE's dead-man's-pedal)
//     can run. The only defence is to never open the path. It is consulted
//     by PluginHost::applyKnownProblemBlacklist at launch, before any scan
//     — in-process (Plugin List dialog) or out-of-process (Rescan) — adds
//     every matching path to the blacklist. JUCE's scanAndAddFile and our
//     scanAllDefaultPaths both honour the blacklist and skip matches.
//
// NOTE: entries here need not be instruments. The current three happen to
// be, so the effects-only instrument sweep double-covers them, but that is
// incidental — a crashing *effect* would be caught only by this path-based
// launch floor. Do not assume the instrument filter makes this list safe
// to delete.
//
// Each entry carries the reason it's listed so future maintainers know
// whether the plugin can be removed (e.g. when a fixed version ships).
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
