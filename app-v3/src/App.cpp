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

    // Listener-driven incremental save: every device change or plugin-list
    // edit fires changeListenerCallback on the message thread, which writes
    // a fresh XML snapshot to preferences. ApplicationProperties batches
    // disk writes, so this is cheap.
    if (auto* dm = engine->getDeviceManager())
        dm->addChangeListener(this);
    pluginHost->getKnownPluginList().addChangeListener(this);

    context.emplace(AppContext{*engine, *pluginHost, *preferences, *pluginSlot});
    mainWindow = std::make_unique<ui::MainWindow>(getApplicationName(), *context);
}

void App::shutdown()
{
    // Deregister listeners FIRST so a late device change can't fire a
    // write into preferences while we're flushing.
    if (engine != nullptr)
        if (auto* dm = engine->getDeviceManager())
            dm->removeChangeListener(this);
    if (pluginHost != nullptr)
        pluginHost->getKnownPluginList().removeChangeListener(this);

    // Force a final synchronous save before subsystems are torn down.
    if (preferences != nullptr)
        preferences->flush();

    // Close the window first so no UI callback can try to touch the engine
    // during its teardown.
    mainWindow.reset();
    context.reset();
    // pluginSlot owns the live audio graph (via the engine); drop it
    // BEFORE the engine + pluginHost go away.
    pluginSlot.reset();
    pluginHost.reset();
    engine.reset();
    preferences.reset();
}

void App::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (preferences == nullptr)
        return;

    if (engine != nullptr && source == engine->getDeviceManager())
    {
        if (auto state = engine->getDeviceManager()->createStateXml())
            preferences->setAudioDeviceState(*state);
        return;
    }

    if (pluginHost != nullptr && source == &pluginHost->getKnownPluginList())
    {
        if (auto xml = pluginHost->createKnownListXml())
            preferences->setKnownPluginList(*xml);
        return;
    }
}

} // namespace pedalboard3
