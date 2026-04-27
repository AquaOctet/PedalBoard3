#include "PreferencesPersister.h"

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "Preferences.h"
#include "Session.h"

namespace pedalboard3::session
{

namespace
{

constexpr auto kInFlightFileName = "restore_in_flight.txt";

juce::File restoreInFlightFile(Preferences& prefs)
{
    if (auto* store = prefs.getStore())
        return store->getFile().getParentDirectory().getChildFile(kInFlightFileName);
    return {};
}

bool sameDescription(const juce::PluginDescription& a, const juce::PluginDescription& b) noexcept
{
    return a.pluginFormatName == b.pluginFormatName && a.fileOrIdentifier == b.fileOrIdentifier &&
           a.deprecatedUid == b.deprecatedUid;
}

} // namespace

juce::String readRestoreInFlightMarker(Preferences& prefs)
{
    const auto f = restoreInFlightFile(prefs);
    if (f != juce::File{} && f.existsAsFile())
        return f.loadFileAsString().trim();
    return {};
}

void writeRestoreInFlightMarker(Preferences& prefs, const juce::String& fileOrIdentifier)
{
    const auto f = restoreInFlightFile(prefs);
    if (f == juce::File{})
        return;
    f.getParentDirectory().createDirectory();
    f.replaceWithText(fileOrIdentifier);
}

void clearRestoreInFlightMarker(Preferences& prefs)
{
    const auto f = restoreInFlightFile(prefs);
    if (f != juce::File{} && f.existsAsFile())
        f.deleteFile();
}

PreferencesPersister::PreferencesPersister(Preferences& prefs,
                                           engine::AudioEngine& engine,
                                           engine::PluginHost& host,
                                           Session& session)
    : prefs_(prefs), engine_(engine), host_(host), session_(session)
{
    if (auto* dm = engine_.getDeviceManager())
        dm->addChangeListener(this);
    host_.getKnownPluginList().addChangeListener(this);
    session_.addChangeListener(this);
}

PreferencesPersister::~PreferencesPersister()
{
    session_.removeChangeListener(this);
    host_.getKnownPluginList().removeChangeListener(this);
    if (auto* dm = engine_.getDeviceManager())
        dm->removeChangeListener(this);
}

void PreferencesPersister::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (auto* dm = engine_.getDeviceManager(); source == dm)
    {
        if (auto state = dm->createStateXml())
            prefs_.setAudioDeviceState(*state);
        return;
    }

    if (source == &host_.getKnownPluginList())
    {
        if (auto xml = host_.createKnownListXml())
            prefs_.setKnownPluginList(*xml);
        return;
    }

    if (source == &session_)
    {
        // Only act on Idle transitions — Building/Swapping are transient.
        if (session_.getState() != Session::State::Idle)
            return;

        const auto active = session_.getActiveDescription();
        if (active)
        {
            // Successful load. Dirty-write-only: skip the disk write if
            // the persisted description already matches.
            const auto current = prefs_.getActivePluginDescription();
            const bool same = current != nullptr && sameDescription(*current, *active);
            if (!same)
                prefs_.setActivePluginDescription(*active);

            // Crash-loop guard satisfied: load completed without crash.
            clearRestoreInFlightMarker(prefs_);
            return;
        }

        if (!session_.isLoaded())
        {
            // User-initiated unload — Session::onBuildResult sets
            // activeDescription_ to nullopt only on explicit unload. A
            // failed load also lands here with activeDescription_ unset
            // and isLoaded() == false, but in that case we must NOT
            // clear the persisted description (would lose the user's
            // restore state on a missing-plugin failure).
            const auto err = session_.getLastError();
            if (err.isEmpty())
            {
                prefs_.clearActivePluginDescription();
                clearRestoreInFlightMarker(prefs_);
            }
        }
    }
}

} // namespace pedalboard3::session
