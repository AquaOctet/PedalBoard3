#include "App.h"

#include <algorithm>

namespace pedalboard3
{

void App::initialise(const juce::String& /*commandLine*/)
{
    // Install the Pedalboard 3 LookAndFeel before any Component is built —
    // every dialog launched after this point picks up the new palette.
    lookAndFeel = std::make_unique<ui::Pb3LookAndFeel>();
    juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel.get());

    preferences = std::make_unique<session::Preferences>();

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
    persister = std::make_unique<session::PreferencesPersister>(*preferences, *engine, *pluginHost, *session);

    context.emplace(AppContext{*engine, *pluginHost, *preferences, *session});
    mainWindow = std::make_unique<ui::MainWindow>(getApplicationName(), *context);

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
    // Session's dtor cancels its AsyncUpdater + joins its worker BEFORE
    // PluginHost/AudioEngine teardown (lifetime invariant in Session.h).
    mainWindow.reset();
    context.reset();
    persister.reset();
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

void App::restoreActivePatchIfAny()
{
    if (preferences == nullptr || pluginHost == nullptr || session == nullptr)
        return;

    auto saved = preferences->getActivePatch();
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
        preferences->clearActivePatch();
        session::clearRestoreInFlightMarker(*preferences);
        preferences->flush();
        return;
    }

    // Cross-check against the plugin host (AC-1.D.3-L). The restore path calls
    // createInstance directly, bypassing the instrument sweep + known-problem
    // floor that only guard the scanner. Drop any node that's an instrument or
    // blacklisted (known crash-on-construct) so it never reaches createInstance.
    const auto savedCount = saved->nodes.size();
    saved->nodes.erase(std::remove_if(saved->nodes.begin(),
                                      saved->nodes.end(),
                                      [this](const session::PatchNode& n)
                                      { return !session::isNodeSafeForChain(*pluginHost, n); }),
                       saved->nodes.end());

    if (saved->nodes.size() != savedCount)
    {
        // Dropping nodes left the saved connections dangling — rebuild a clean
        // linear chain over the survivors so the restored graph is valid.
        saved->connections = session::linearChainConnections(saved->nodes);
    }

    // Arm the crash-loop marker BEFORE handing the patch to the worker, and
    // flush so it survives a hard crash. replacePatch is async; on success
    // PreferencesPersister clears the marker on the Idle transition.
    session::writeRestoreInFlightMarker(*preferences, session::patchIdentityString(*saved));
    preferences->flush();

    session->replacePatch(*saved);
}

} // namespace pedalboard3
