#include "App.h"

namespace pedalboard3
{

void App::initialise(const juce::String& /*commandLine*/)
{
    preferences = std::make_unique<session::Preferences>();

    engine = std::make_unique<engine::AudioEngine>();
    auto savedDeviceState = preferences->getAudioDeviceState();
    const auto result = engine->initialise(savedDeviceState.get());
    if (!result.isOk())
    {
        // Headless dev servers and freshly booted machines with no audio
        // device will land here. We keep the window open so the user can
        // pick a device via Audio Settings once hardware is available.
        juce::Logger::writeToLog("Pedalboard 3: audio init warning — " + result.getErrorMessage());
    }

    pluginHost = std::make_unique<engine::PluginHost>();
    if (auto savedList = preferences->getKnownPluginList())
        pluginHost->loadKnownList(*savedList);

    pluginSlot = std::make_unique<session::PluginSlot>(*engine, *pluginHost);
    session = std::make_unique<session::Session>(*pluginSlot, *engine, *pluginHost);

    // PreferencesPersister registers the three change-listeners. Construct
    // it AFTER session so it can subscribe; destroy it BEFORE session.
    persister = std::make_unique<session::PreferencesPersister>(*preferences, *engine, *pluginHost, *session);

    context.emplace(AppContext{*engine, *pluginHost, *preferences, *pluginSlot, *session});
    mainWindow = std::make_unique<ui::MainWindow>(getApplicationName(), *context);

    // Restore-on-launch is dispatched AFTER MainWindow exists, so any
    // change-message Session fires during the build is observed by the
    // window's listener (which Unit 7 will register).
    restoreActivePluginIfAny();
}

void App::shutdown()
{
    // Force a final synchronous save before subsystems are torn down.
    if (preferences != nullptr)
        preferences->flush();

    // Reverse construction order. PreferencesPersister deregisters its
    // listeners in its dtor, so it must drop BEFORE session/host/engine.
    // Session's dtor cancels its AsyncUpdater + joins its worker BEFORE
    // PluginSlot/AudioEngine teardown (lifetime invariant in Session.h).
    mainWindow.reset();
    context.reset();
    persister.reset();
    session.reset();
    pluginSlot.reset();
    pluginHost.reset();
    engine.reset();
    preferences.reset();
}

void App::restoreActivePluginIfAny()
{
    if (preferences == nullptr || pluginHost == nullptr || session == nullptr)
        return;

    auto savedDesc = preferences->getActivePluginDescription();
    if (savedDesc == nullptr)
        return;

    // Cross-check 1 (AC-1.D.2-L): refuse to load a description whose
    // plugin is not in KnownPluginList. Raises the bar from "edit one
    // settings key" to "edit two correlated settings keys".
    const auto known = pluginHost->getKnownPlugins();
    bool foundInKnown = false;
    for (const auto& k : known)
    {
        if (k.pluginFormatName == savedDesc->pluginFormatName
            && k.fileOrIdentifier == savedDesc->fileOrIdentifier)
        {
            foundInKnown = true;
            break;
        }
    }
    if (!foundInKnown)
    {
        preferences->clearActivePluginDescription();
        preferences->flush();
        return;
    }

    // Cross-check 2 (AC-1.D.2-N): if a previous launch set the in-flight
    // marker to this plugin and never cleared it, that previous launch
    // crashed inside the load. Skip + clear to break the crash loop.
    const auto inFlight = session::readRestoreInFlightMarker(*preferences);
    if (inFlight.isNotEmpty() && inFlight == savedDesc->fileOrIdentifier)
    {
        preferences->clearActivePluginDescription();
        session::clearRestoreInFlightMarker(*preferences);
        preferences->flush();
        juce::Logger::writeToLog(
            "Pedalboard 3: refusing to restore '" + savedDesc->name +
            "' — previous launch crashed during instantiation. Active plugin cleared.");
        return;
    }

    // Tag in-flight, then dispatch. PreferencesPersister will clear the
    // marker when Session transitions Idle with a successful load.
    session::writeRestoreInFlightMarker(*preferences, savedDesc->fileOrIdentifier);
    session->queueLoad(*savedDesc);
}

} // namespace pedalboard3
