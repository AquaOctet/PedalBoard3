#include "AppLog.h"

namespace pedalboard3::session::applog
{

juce::File logFileFor(const juce::File& settingsFile)
{
    return settingsFile.getParentDirectory().getChildFile("logs").getChildFile("Pedalboard 3.log");
}

juce::String welcomeMessage()
{
    juce::String message;
    message << "Pedalboard 3 " << PB3_APP_VERSION << "  (build " << PB3_BUILD_ID << ")" << juce::newLine
            << juce::SystemStats::getOperatingSystemName() << ", " << juce::SystemStats::getCpuModel() << ", "
            << juce::SystemStats::getNumCpus() << " cores, "
            << juce::String(juce::SystemStats::getMemorySizeInMegabytes()) << " MB" << juce::newLine;
    return message;
}

std::unique_ptr<juce::FileLogger> createLogger(const juce::File& settingsFile)
{
    return std::make_unique<juce::FileLogger>(logFileFor(settingsFile), welcomeMessage(), kMaxBytes);
}

} // namespace pedalboard3::session::applog
