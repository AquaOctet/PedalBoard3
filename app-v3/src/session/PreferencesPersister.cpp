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

bool samePatch(const Patch& a, const Patch& b) noexcept
{
    if (a.nodes.size() != b.nodes.size())
        return false;
    for (size_t i = 0; i < a.nodes.size(); ++i)
    {
        if (a.nodes[i].id != b.nodes[i].id)
            return false;
        if (a.nodes[i].desc.pluginFormatName != b.nodes[i].desc.pluginFormatName)
            return false;
        if (a.nodes[i].desc.fileOrIdentifier != b.nodes[i].desc.fileOrIdentifier)
            return false;
        if (a.nodes[i].bypass != b.nodes[i].bypass)
            return false;
        if (a.nodes[i].customName != b.nodes[i].customName)
            return false;
    }
    return a.connections == b.connections;
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

        const auto active = session_.getActivePatch();
        const bool isEmpty = active.nodes.empty();

        if (!isEmpty)
        {
            // Successful patch state. Dirty-write-only: skip the disk
            // write if the persisted patch already matches.
            const auto current = prefs_.getActivePatch();
            const bool same = current != nullptr && samePatch(*current, active);
            if (!same)
                prefs_.setActivePatch(active);

            clearRestoreInFlightMarker(prefs_);
            return;
        }

        // Empty active patch — user explicitly cleared, OR a failed
        // restore landed in the empty state. Distinguish via lastError:
        // present means a failure happened, so leave the persisted
        // patch alone (the user keeps trying next launch with the right
        // plugins). Empty error means an explicit clearPatch.
        const auto err = session_.getLastError();
        if (err.isEmpty())
        {
            prefs_.clearActivePatch();
            clearRestoreInFlightMarker(prefs_);
        }
    }
}

} // namespace pedalboard3::session
