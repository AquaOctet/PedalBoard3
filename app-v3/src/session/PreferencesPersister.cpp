#include "PreferencesPersister.h"

#include "BankDocument.h"
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
        // Kind/type distinguish internal nodes, whose desc is empty — without
        // this a Level->VU swap at the same position would read as "unchanged"
        // and silently fail to persist.
        if (a.nodes[i].kind != b.nodes[i].kind)
            return false;
        if (a.nodes[i].internalType != b.nodes[i].internalType)
            return false;
        if (a.nodes[i].desc.pluginFormatName != b.nodes[i].desc.pluginFormatName)
            return false;
        if (a.nodes[i].desc.fileOrIdentifier != b.nodes[i].desc.fileOrIdentifier)
            return false;
        if (a.nodes[i].bypass != b.nodes[i].bypass)
            return false;
        if (a.nodes[i].customName != b.nodes[i].customName)
            return false;
        if (a.nodes[i].midiChannel != b.nodes[i].midiChannel)
            return false;
        // Canvas position — a drag must trigger a persist. Two unpositioned
        // nodes (both NaN) compare equal; a positioned/unpositioned mismatch or
        // a moved node compares different.
        if (a.nodes[i].hasPosition() != b.nodes[i].hasPosition())
            return false;
        if (a.nodes[i].hasPosition() && (a.nodes[i].x != b.nodes[i].x || a.nodes[i].y != b.nodes[i].y))
            return false;
    }
    if (a.midiMappings != b.midiMappings || a.oscMappings != b.oscMappings)
        return false;
    return a.connections == b.connections;
}

bool sameBank(const PatchBank& a, const PatchBank& b) noexcept
{
    if (a.activeIndex != b.activeIndex || a.patches.size() != b.patches.size())
        return false;
    for (size_t i = 0; i < a.patches.size(); ++i)
    {
        if (a.patches[i].name != b.patches[i].name)
            return false;
        if (!samePatch(a.patches[i], b.patches[i]))
            return false;
    }
    return true;
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
                                           Session& session,
                                           BankDocument& bankDocument)
    : prefs_(prefs), engine_(engine), host_(host), session_(session), bankDocument_(bankDocument)
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

        // Flush eagerly. The KnownPluginList change message includes
        // dead-man's-pedal blacklist updates, and JUCE's
        // PluginListComponent ctor DELETES the dead-man's-pedal file
        // immediately after applying its contents (juce_PluginListComponent.cpp:444).
        // If we relied on the default lazy save, a crashing third-party
        // plugin scan would delete the dead-man's-pedal file AND lose the
        // in-memory blacklist on the next crash — re-exposing the user
        // to the same crash on next launch. Eager flush makes the blacklist
        // crash-durable.
        prefs_.flush();
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
            // Successful patch state. Persist the whole bank with the active
            // slot refreshed from Session (topology only — no plugin-state
            // capture on every Idle transition; the slot keeps the bank's
            // name). Dirty-write-only: skip the disk write if the persisted
            // bank already matches.
            auto bank = bankDocument_.bank();
            if (bankDocument_.liveMatchesActiveSlot())
            {
                auto slot = active;
                slot.name = bank.active().name;
                bank.active() = std::move(slot);
            }
            const auto current = prefs_.getActiveBank();
            const bool same = current != nullptr && sameBank(*current, bank);
            if (!same)
                prefs_.setActiveBank(bank);

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
            // An empty live patch is still a valid bank state (the user
            // cleared the board, or the bank's active slot is empty): keep
            // the other slots, drop the active slot's nodes. Test Tone
            // (capture disabled) leaves the persisted bank alone.
            if (bankDocument_.liveMatchesActiveSlot())
            {
                auto bank = bankDocument_.bank();
                auto slot = active;
                slot.name = bank.active().name;
                bank.active() = std::move(slot);
                prefs_.setActiveBank(bank);
            }
            clearRestoreInFlightMarker(prefs_);
        }
    }
}

} // namespace pedalboard3::session
