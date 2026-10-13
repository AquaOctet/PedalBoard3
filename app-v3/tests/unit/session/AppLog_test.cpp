// Tests for session::applog (session/AppLog.h): where the log goes, what it
// starts with, and that writing through juce::Logger reaches it.

#include <catch2/catch_test_macros.hpp>

#include "session/AppLog.h"
#include "support/TempSettings.h"

using namespace pedalboard3;

TEST_CASE("AppLog: the log sits in logs/ beside the settings", "[session][log]")
{
    pb3test::TempSettings settings;
    const auto log = session::applog::logFileFor(settings.file());

    REQUIRE(log.getFileName() == "Pedalboard 3.log");
    REQUIRE(log.getParentDirectory().getFileName() == "logs");
    REQUIRE(log.getParentDirectory().getParentDirectory() == settings.file().getParentDirectory());
}

TEST_CASE("AppLog: the welcome line names the build and the machine", "[session][log]")
{
    const auto welcome = session::applog::welcomeMessage();

    // The version and commit are what match a log to a release and its symbols.
    REQUIRE(welcome.contains(PB3_APP_VERSION));
    REQUIRE(welcome.contains(PB3_BUILD_ID));
    REQUIRE(welcome.contains(juce::SystemStats::getOperatingSystemName()));
}

TEST_CASE("AppLog: what is logged reaches the file", "[session][log]")
{
    pb3test::TempSettings settings;
    const auto log = session::applog::logFileFor(settings.file());
    REQUIRE_FALSE(log.existsAsFile());

    {
        auto logger = session::applog::createLogger(settings.file());
        REQUIRE(logger != nullptr);
        juce::Logger::setCurrentLogger(logger.get());
        juce::Logger::writeToLog("a line the app wrote");
        juce::Logger::setCurrentLogger(nullptr);
    }

    REQUIRE(log.existsAsFile());
    const auto text = log.loadFileAsString();
    REQUIRE(text.contains("Pedalboard 3 " PB3_APP_VERSION));
    REQUIRE(text.contains("a line the app wrote"));
}
