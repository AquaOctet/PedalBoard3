#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace pedalboard3::session::applog
{

// The app's log file (crash-visibility plan U2). Until this existed, every
// juce::Logger::writeToLog call in the app went nowhere, because nothing ever
// installed a logger.
//
// One rolling file beside the settings, so a user can find both in the same
// folder and send them together. JUCE trims it from the front at startup when
// it has grown past kMaxBytes, so it can't fill a disk.

inline constexpr int kMaxBytes = 512 * 1024;

// <settings folder>/logs/Pedalboard 3.log
juce::File logFileFor(const juce::File& settingsFile);

// The log's first lines: which build this is and what it's running on, so a
// log someone sends can be matched to a release and its symbols (KD4).
juce::String welcomeMessage();

// A logger writing to logFileFor(settingsFile). The caller installs it with
// juce::Logger::setCurrentLogger and owns it for the life of the app.
std::unique_ptr<juce::FileLogger> createLogger(const juce::File& settingsFile);

} // namespace pedalboard3::session::applog
