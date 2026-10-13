#include "App.h"

#include "platform/CrashGuard.h"
#include "session/BoardFiles.h"
#include "session/DefaultBank.h"

namespace pedalboard3
{

void App::initialise(const juce::String& /*commandLine*/)
{
    // The plugin crash guard's signal handlers (M5 U5; Linux only, a no-op
    // elsewhere), before any audio or plugin code runs.
    platform::installCrashGuard();

    // Install the Pedalboard 3 LookAndFeel before any Component is built —
    // every dialog launched after this point picks up the new palette.
    lookAndFeel = std::make_unique<ui::Pb3LookAndFeel>();
    juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel.get());

    preferences = std::make_unique<session::Preferences>();

    // Where boards are saved by default (Documents\Pedalboard 3\Patches), made on
    // first run so File > Open and Save As always have somewhere to start.
    session::boardfiles::patchesFolder().createDirectory();

    engine = std::make_unique<engine::AudioEngine>();
    auto savedDeviceState = preferences->getAudioDeviceState();
    const auto result = engine->initialise(savedDeviceState.get());
    if (!result.isOk())
    {
        // Headless dev servers and freshly booted machines with no audio
        // device land here. Window stays open; user can pick a device
        // via Audio Settings once hardware is available.
        juce::Logger::writeToLog("Pedalboard 3: audio init warning — " + result.getErrorMessage());
    }

    pluginHost = std::make_unique<engine::PluginHost>();
    // The crash list and the scan marker live beside the settings file (M5 U7).
    if (auto* store = preferences->getStore())
        pluginHost->setUserDataFolder(store->getFile().getParentDirectory());
    if (auto savedList = preferences->getKnownPluginList())
        pluginHost->loadKnownList(*savedList);

    // Apply the shipping known-problem blacklist BEFORE any scan can
    // happen — in-process (Plugin List dialog) OR out-of-process (Rescan).
    // These plugins crash the host during construction (e.g. Current's
    // pure-virtual license-refresh call) BEFORE any descriptor is ever
    // added, so the only protection is to never open them, by file path,
    // at launch. The out-of-process scanner does NOT replace this: it
    // skips already-blacklisted paths, and a pre-construction crash gives
    // neither scanner a result to act on. See engine/KnownProblemPlugins.h.
    // Persist immediately if any new entries were added so a fresh crash
    // later in this session doesn't lose them.
    if (pluginHost->applyKnownProblemBlacklist())
    {
        if (auto xml = pluginHost->createKnownListXml())
            preferences->setKnownPluginList(*xml);
        preferences->flush();
    }

    session = std::make_unique<session::Session>(*engine, *pluginHost);
    bankDocument = std::make_unique<session::BankDocument>(*session);
    featureGate = std::make_unique<session::FeatureGate>(
        session::FeatureGate::GlobalStore{
            [this](session::Feature feature) { return preferences->getFeatureEnabled(feature); },
            [this](session::Feature feature, bool on) { preferences->setFeatureEnabled(feature, on); }},
        *bankDocument);
    featureNodeGate = std::make_unique<session::FeatureNodeGate>(*featureGate, *session);
    featureNodeGate->apply();
    editHistory = std::make_unique<session::EditHistory>(*session);
    // M6 U3: transport commands for keys, MIDI and OSC, with the MMC switch
    // restored from Preferences.
    transportCommands = std::make_unique<session::TransportCommands>(*session, engine->getTransport());
    transportCommands->setMmcEnabled(preferences->getMmcTransport());

    // M2: ControlHub is the message-thread entry point live MIDI/OSC devices
    // feed (device edges land in App::initialise too, once built); ControlWiring
    // binds its resolver to session and re-feeds mappings on every patch change.
    controlHub = std::make_unique<session::ControlHub>();
    // M4: app-level MIDI/OSC mappings (next / previous patch, ...), restored
    // before the wiring so its first feed already includes them.
    appMappings = std::make_unique<session::AppMappings>();
    if (auto xml = preferences->getAppMappings())
        appMappings->restoreFromXml(*xml);
    controlWiring = std::make_unique<session::ControlWiring>(*session, *controlHub, appMappings.get());
    // M3: every live MIDI message goes through the patch switcher, which hands
    // it to controlHub first and then applies the default Program Change ->
    // patch rule (see session/PatchSwitcher.h).
    patchSwitcher = std::make_unique<session::PatchSwitcher>(*bankDocument, *controlHub);
    patchSwitcher->setEditHistory(editHistory.get());
    patchSwitcher->setTransportCommands(transportCommands.get());
    midiInputManager = std::make_unique<control::MidiInputManager>(
        engine->getDeviceManager(), [this](const juce::MidiMessage& msg) { patchSwitcher->handleMidi(msg); });

    oscServer =
        std::make_unique<control::OscServer>([this](const juce::OSCMessage& msg) { controlHub->handleOsc(msg); });
    // Optional features U2 (KD6): MIDI input runs, and the OSC listener binds,
    // only while their feature is available (on in Settings and for the
    // board). OSC starts off, so nothing listens on the network by default. A
    // bind failure is non-fatal and logged; OSC Settings can retry.
    inputGates = std::make_unique<session::InputGates>(
        *featureGate,
        [this](session::Feature feature) { return featureGate->isAvailable(feature); },
        *midiInputManager,
        *oscServer,
        [this] { return std::make_pair(preferences->getOscPort(), preferences->getOscMulticastAddress()); });
    inputGates->setTransport(transportCommands.get());
    inputGates->apply();

    persister = std::make_unique<session::PreferencesPersister>(
        *preferences, *engine, *pluginHost, *session, *bankDocument, *appMappings);

    context.emplace(AppContext{*engine,
                               *pluginHost,
                               *preferences,
                               *session,
                               *bankDocument,
                               *controlHub,
                               *appMappings,
                               *oscServer,
                               *editHistory,
                               *transportCommands,
                               *featureGate});
    mainWindow = std::make_unique<ui::MainWindow>(getApplicationName(), *context);
    // A settings file that couldn't be read was set aside (M5 U7): say so and
    // offer a rescan, rather than leave an empty plugin list unexplained.
    if (const auto setAside = preferences->getSetAsideSettingsFile(); setAside != juce::File())
        mainWindow->offerRescanAfterReset(setAside);
    // Quit goes through the window's save prompt (finish pass KD4); after Don't
    // Save, shutdown() reverts the persisted session (KD7).
    mainWindow->onQuitConfirmed = [this](bool discardUnsaved)
    {
        discardUnsavedOnShutdown = discardUnsaved;
        quit();
    };

    // Restore the previously-active patch, gated by the KnownPluginList
    // cross-check + crash-loop guard (see restoreActivePatchIfAny).
    restoreActivePatchIfAny();
}

void App::shutdown()
{
    if (preferences != nullptr)
        preferences->flush();

    // Reverse construction order. PreferencesPersister deregisters
    // listeners in its dtor (must drop BEFORE session/host/engine).
    // ControlWiring likewise deregisters from session in its dtor, so it
    // must drop before session too; controlHub outlives it with nothing
    // left referencing it once controlWiring and context (which handed a
    // controlHub& to UI) are both gone.
    // Session's dtor cancels its AsyncUpdater + joins its worker BEFORE
    // PluginHost/AudioEngine teardown (lifetime invariant in Session.h).
    mainWindow.reset();
    context.reset();
    persister.reset();
    // Quit with Don't Save (finish pass KD7): the next launch restores the last
    // saved content, not the discarded edits. Done once the persister is gone,
    // so nothing writes the live bank back afterwards.
    if (discardUnsavedOnShutdown && preferences != nullptr && bankDocument != nullptr)
    {
        session::revertPersistedBank(*preferences, bankDocument->currentFile());
        preferences->flush();
    }
    inputGates.reset(); // listens to featureGate; drives both inputs
    midiInputManager.reset();
    oscServer.reset();
    patchSwitcher.reset(); // after both input edges, before controlHub/bankDocument
    controlWiring.reset();
    appMappings.reset(); // after controlWiring and persister, which listen to it
    controlHub.reset();
    editHistory.reset();       // listens to session; the UI and patch switcher are gone
    transportCommands.reset(); // the UI and patch switcher that call it are gone
    featureNodeGate.reset();   // listens to featureGate and session
    featureGate.reset();       // listens to bankDocument
    bankDocument.reset();
    session.reset();
    pluginHost.reset();
    engine.reset();
    preferences.reset();

    // Drop the default LookAndFeel pointer BEFORE destroying the
    // singleton instance — juce::Desktop holds the pointer; if we
    // destroy the LookAndFeel without unregistering, any latent
    // tooltip/redraw on the way out dereferences a dangling ptr.
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    lookAndFeel.reset();
}

void App::systemRequestedQuit()
{
    if (mainWindow != nullptr)
        mainWindow->requestQuit();
    else
        quit();
}

void App::restoreActivePatchIfAny()
{
    auto saved = preferences->getActiveBank();
    // Nothing to restore (first run, or the last quit discarded an untitled
    // board): open the user's default bank, if they saved one (finish pass KD2).
    const bool fromDefault = saved == nullptr;
    if (fromDefault)
        saved = session::readDefaultBank(session::defaultBankFile(*preferences));
    if (saved == nullptr)
        return;

    // Crash-loop guard (AC-1.D.3-M). If a previous launch crashed mid-restore
    // — inside a plugin's createInstance or setStateInformation, before any
    // signal/SEH handler could catch it — the in-flight marker is still on
    // disk. Retrying the same patch would crash again on every launch with no
    // way out. Quarantine it: drop the saved patch and open clean. The marker
    // is armed below and cleared by PreferencesPersister on a successful Idle.
    if (session::readRestoreInFlightMarker(*preferences).isNotEmpty())
    {
        juce::Logger::writeToLog("Pedalboard 3: previous launch crashed during patch restore — "
                                 "discarding the saved patch to recover.");
        preferences->clearActiveBank();
        session::clearRestoreInFlightMarker(*preferences);
        preferences->flush();
        return;
    }

    // Cross-check against the plugin host (AC-1.D.3-L), per patch — not only the
    // active one: every slot can become live with one Program Change. Plugins
    // are found in the scanned list by ID (M5 U3); one that isn't there, or is
    // an instrument or blacklisted (known crash-on-construct), becomes a
    // pass-through placeholder and never reaches createInstance. Then what
    // can't be built at all is dropped.
    session::resolvePluginNodes(*saved, *pluginHost);
    session::dropUnsafeNodes(*saved, *pluginHost);

    // Arm the crash-loop marker BEFORE handing the active patch to the worker,
    // and flush so it survives a hard crash. replacePatch is async; on success
    // PreferencesPersister clears the marker on the Idle transition.
    session::writeRestoreInFlightMarker(*preferences, session::patchIdentityString(saved->active()));
    preferences->flush();

    bankDocument->loadBank(std::move(*saved));
    // Finish pass KD7: the restored bank keeps its file (quick save still works)
    // and, after a crash or a quit that kept edits, its unsaved marker.
    if (!fromDefault)
        bankDocument->restoreIdentity(preferences->getActiveBankFile(),
                                      preferences->getActiveBankModified(),
                                      preferences->getActiveBankReadOnly());
}

} // namespace pedalboard3
