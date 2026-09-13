#include "PreferencesPersister.h"
#include "AtomicFileWriter.h"

#include "AppMappings.h"
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
        // By ID and name (M5 U3): a plugin's path isn't part of a board.
        if (a.nodes[i].desc.uniqueId != b.nodes[i].desc.uniqueId || a.nodes[i].desc.name != b.nodes[i].desc.name)
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
        if (a.nodes[i].unknown != b.nodes[i].unknown) // what a newer version wrote (M5 U4)
            return false;
    }
    if (a.midiMappings != b.midiMappings || a.oscMappings != b.oscMappings)
        return false;
    if (a.unknown != b.unknown || a.unknownMappings != b.unknownMappings)
        return false;
    return a.connections == b.connections;
}

bool sameBank(const PatchBank& a, const PatchBank& b) noexcept
{
    if (a.activeIndex != b.activeIndex || a.patches.size() != b.patches.size())
        return false;
    if (a.features != b.features) // a board switch must persist too
        return false;
    if (a.unknown != b.unknown || a.fileUnknown != b.fileUnknown) // what a newer version wrote (M5 U4)
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
    AtomicFileWriter::writeText(f, fileOrIdentifier); // M5 U2: never a torn marker
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
                                           BankDocument& bankDocument,
                                           AppMappings& appMappings)
    : prefs_(prefs), engine_(engine), host_(host), session_(session), bankDocument_(bankDocument),
      appMappings_(appMappings)
{
    if (auto* dm = engine_.getDeviceManager())
        dm->addChangeListener(this);
    host_.getKnownPluginList().addChangeListener(this);
    session_.addChangeListener(this);
    appMappings_.addChangeListener(this);
    bankDocument_.addChangeListener(this);
}

PreferencesPersister::~PreferencesPersister()
{
    bankDocument_.removeChangeListener(this);
    appMappings_.removeChangeListener(this);
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

    if (source == &bankDocument_)
    {
        // Bank-only edits (patch rename / add / remove, a switch) and the file
        // identity: persisted here, since they don't always move Session.
        persistBank();
        prefs_.setActiveBankIdentity(
            bankDocument_.currentFile(), bankDocument_.isModified(), bankDocument_.isReadOnly());
        return;
    }

    if (source == &appMappings_)
    {
        if (auto xml = appMappings_.toXml())
            prefs_.setAppMappings(*xml);
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
            // Successful patch state: persist the whole bank (dirty-write-only).
            persistBank();
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

void PreferencesPersister::persistBank()
{
    auto bank = bankDocument_.bank();
    if (bankDocument_.liveMatchesActiveSlot())
    {
        auto slot = session_.getActivePatch();
        slot.name = bank.active().name;
        bank.active() = std::move(slot);
    }
    const auto current = prefs_.getActiveBank();
    if (current == nullptr || !sameBank(*current, bank))
        prefs_.setActiveBank(bank);
}

} // namespace pedalboard3::session
