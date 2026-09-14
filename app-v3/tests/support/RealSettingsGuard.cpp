// Fails a test run that changed the user's real per-user files: the settings,
// the default board and the crash list beside them. The Preferences tests once
// opened the real settings and wiped the developer's plugin list and session on
// every run; tests give Preferences a file of their own (support/TempSettings.h).
//
// ctest runs each test case in its own process, so a failure here names the
// test that did it. It reports rather than restores: the app, if it's running,
// may save its settings while tests run, and putting the old bytes back would
// undo that.

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "session/Preferences.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace
{

struct Snapshot
{
    bool exists{false};
    juce::MemoryBlock bytes;
};

Snapshot snapshotOf(const juce::File& file)
{
    Snapshot snapshot;
    snapshot.exists = file.existsAsFile();
    if (snapshot.exists)
        file.loadFileAsData(snapshot.bytes);
    return snapshot;
}

class RealSettingsGuard final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override
    {
        const auto settings = pedalboard3::session::Preferences::defaultSettingsFile();
        for (const auto& file :
             {settings, settings.getSiblingFile("default.pdl"), settings.getSiblingFile("crashed_plugins.xml")})
            watched_.emplace_back(file, snapshotOf(file));
    }

    void testRunEnded(const Catch::TestRunStats&) override
    {
        bool changed = false;
        for (const auto& [file, before] : watched_)
        {
            const auto after = snapshotOf(file);
            if (after.exists == before.exists && after.bytes == before.bytes)
                continue;
            changed = true;
            std::fprintf(stderr,
                         "RealSettingsGuard: %s changed during this test run. Tests must not touch the user's "
                         "real files: give Preferences a file of its own (support/TempSettings.h). (Or the app "
                         "saved it while tests ran.)\n",
                         file.getFullPathName().toRawUTF8());
        }
        if (changed)
        {
            std::fflush(stderr);
            std::exit(3);
        }
    }

private:
    std::vector<std::pair<juce::File, Snapshot>> watched_;
};

} // namespace

CATCH_REGISTER_LISTENER(RealSettingsGuard)
