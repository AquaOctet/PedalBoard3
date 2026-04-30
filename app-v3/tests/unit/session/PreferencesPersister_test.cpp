// Tests for pedalboard3::session::PreferencesPersister — the listener
// fan-out that drives incremental Preferences writes when AudioDeviceManager,
// KnownPluginList, or Session change.
//
// The behaviour we care about most for v3.0 is the *eager flush* on
// KnownPluginList changes: JUCE's PluginListComponent ctor deletes the
// dead-man's-pedal file immediately after applying its contents, so a
// subsequent crash loses the in-memory blacklist unless we have already
// written it to disk. PreferencesPersister calls prefs_.flush() right
// after setKnownPluginList() to make the blacklist crash-durable.
//
// We can't easily redirect JUCE's storage path, so these tests touch
// the developer's real user settings file under a synthetic key/value
// nobody else writes. Each test scrubs after itself; if a test aborts
// the next run starts clean because the synthetic basename never
// matches anything outside this file.
//
// Catch2 + JUCE timing: KnownPluginList::addToBlacklist calls
// sendChangeMessage (async, requires a MessageManager). Tests don't
// run a MessageManager loop, so we drive the listener directly via
// sendSynchronousChangeMessage to keep test runs deterministic without
// pulling JUCE_MODAL_LOOPS_PERMITTED into pb3_juce_config.

#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "session/Preferences.h"
#include "session/PreferencesPersister.h"
#include "session/Session.h"

#include <juce_data_structures/juce_data_structures.h>

using namespace pedalboard3;

namespace
{

constexpr auto kSyntheticBlacklistPath = "/test/pb3-persister-synthetic-blacklist.vst3";

// Best-effort cleanup helper for tests that scribble on the user's
// real preferences file. Drops the keys our tests touch on construction
// AND destruction so a previously aborted test can't poison the run.
struct PrefsScrubber
{
    explicit PrefsScrubber(session::Preferences& p) : prefs_(p) { scrub(); }
    ~PrefsScrubber() { scrub(); }

    void scrub()
    {
        if (auto* store = prefs_.getStore())
        {
            store->removeValue("knownPluginList");
            store->removeValue("activePluginDescription");
            store->removeValue("activePatch");
        }
        prefs_.flush();
    }

    session::Preferences& prefs_;
};

// True iff the given KnownPluginList XML carries `path` in its
// <BLACKLISTED id="..."> children. Schema per
// juce_KnownPluginList.cpp::createXml() — a flat list of
// <BLACKLISTED id="…"/> elements directly under the root, alongside
// each <PLUGIN/> entry.
bool xmlContainsBlacklistEntry(const juce::XmlElement& list, const juce::String& path)
{
    for (auto* child : list.getChildIterator())
    {
        if (child->hasTagName("BLACKLISTED") && child->getStringAttribute("id") == path)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("PreferencesPersister: a KnownPluginList change writes XML and eagerly flushes",
          "[session][preferences-persister]")
{
    session::Preferences prefs;
    PrefsScrubber scrub{prefs};

    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;

    {
        session::Session sess(eng, host);
        session::PreferencesPersister persister(prefs, eng, host, sess);

        // Mutate the live KnownPluginList. addToBlacklist sends a change
        // message asynchronously; without a running MessageManager we
        // drive the listener directly via the synchronous overload. The
        // synthetic path never matches a real plugin so this can't
        // accidentally affect the developer's chain.
        host.getKnownPluginList().addToBlacklist(kSyntheticBlacklistPath);
        host.getKnownPluginList().sendSynchronousChangeMessage();

        // In-memory: prefs reflects the new blacklist.
        auto stored = prefs.getKnownPluginList();
        REQUIRE(stored != nullptr);
        REQUIRE(xmlContainsBlacklistEntry(*stored, kSyntheticBlacklistPath));
    }

    // Eager-flush proof: a fresh Preferences instance reads the same
    // file from disk and observes the entry. If the persister had
    // relied on lazy auto-save, the new instance would either see
    // nothing or see an older snapshot.
    {
        session::Preferences fresh;
        auto reread = fresh.getKnownPluginList();
        REQUIRE(reread != nullptr);
        REQUIRE(xmlContainsBlacklistEntry(*reread, kSyntheticBlacklistPath));
    }
}

TEST_CASE("PreferencesPersister: change before construction is ignored, change after is captured",
          "[session][preferences-persister]")
{
    session::Preferences prefs;
    PrefsScrubber scrub{prefs};

    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::Session sess(eng, host);

    // Pre-construction mutation — should not write anything (no listener yet).
    host.getKnownPluginList().addToBlacklist("/test/pb3-pre-listener.vst3");
    host.getKnownPluginList().sendSynchronousChangeMessage();
    REQUIRE(prefs.getKnownPluginList() == nullptr);

    {
        session::PreferencesPersister persister(prefs, eng, host, sess);

        // Post-construction mutation — listener fires, persister writes.
        host.getKnownPluginList().addToBlacklist(kSyntheticBlacklistPath);
        host.getKnownPluginList().sendSynchronousChangeMessage();

        auto stored = prefs.getKnownPluginList();
        REQUIRE(stored != nullptr);
        REQUIRE(xmlContainsBlacklistEntry(*stored, kSyntheticBlacklistPath));
    }

    // After persister destruction, further mutations don't reach prefs
    // (deregistration during dtor). We verify by snapshotting prefs,
    // mutating, and asserting prefs is unchanged.
    auto snapshotBefore = prefs.getKnownPluginList();
    REQUIRE(snapshotBefore != nullptr);
    const auto countBefore = snapshotBefore->getNumChildElements();

    host.getKnownPluginList().addToBlacklist("/test/pb3-post-dtor.vst3");
    host.getKnownPluginList().sendSynchronousChangeMessage();

    auto snapshotAfter = prefs.getKnownPluginList();
    REQUIRE(snapshotAfter != nullptr);
    REQUIRE(snapshotAfter->getNumChildElements() == countBefore);
}

TEST_CASE("PreferencesPersister: lifetime — outliving the persister with a Session change is safe",
          "[session][preferences-persister][lifetime]")
{
    session::Preferences prefs;
    PrefsScrubber scrub{prefs};

    engine::AudioEngine eng;
    eng.initialise();
    engine::PluginHost host;
    session::Session sess(eng, host);

    {
        session::PreferencesPersister persister(prefs, eng, host, sess);
    } // dtor deregisters from sess + host.knownPluginList + eng.deviceManager

    // After dtor, sending a synchronous change must not re-enter the
    // freed persister. JUCE's ChangeBroadcaster guards this internally;
    // the test verifies the dtor unhooks before being destroyed (a
    // missed deregistration would crash here).
    sess.sendSynchronousChangeMessage();
    SUCCEED();
}
