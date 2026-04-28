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
        // device land here. Window stays open; user can pick a device
        // via Audio Settings once hardware is available.
        juce::Logger::writeToLog("Pedalboard 3: audio init warning — " + result.getErrorMessage());
    }

    pluginHost = std::make_unique<engine::PluginHost>();
    if (auto savedList = preferences->getKnownPluginList())
        pluginHost->loadKnownList(*savedList);

    session = std::make_unique<session::Session>(*engine, *pluginHost);
    persister = std::make_unique<session::PreferencesPersister>(*preferences, *engine, *pluginHost, *session);

    context.emplace(AppContext{*engine, *pluginHost, *preferences, *session});
    mainWindow = std::make_unique<ui::MainWindow>(getApplicationName(), *context);

    // Phase 1.D.3 Unit 5 wires this up; for now restore is a no-op
    // (function exists so App.h compiles). Implemented in Unit 5.
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
}

void App::restoreActivePatchIfAny()
{
    // Stub — Phase 1.D.3 Unit 5 implements this end-to-end with the
    // KnownPluginList cross-check + crash-loop hash + defer-until-SR.
    if (preferences == nullptr || pluginHost == nullptr || session == nullptr)
        return;
    auto saved = preferences->getActivePatch();
    if (saved == nullptr)
        return;
    session->replacePatch(*saved);
}

} // namespace pedalboard3
