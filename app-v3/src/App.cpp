#include "App.h"

namespace pedalboard3
{

void App::initialise(const juce::String& /*commandLine*/)
{
    engine = std::make_unique<engine::AudioEngine>();
    const auto result = engine->initialise();
    if (!result.isOk())
    {
        // Headless dev servers and freshly booted machines with no audio
        // device will land here. We keep the window open so the user can
        // pick a device via Audio Settings once hardware is available.
        juce::Logger::writeToLog("Pedalboard 3: audio init warning — " + result.getErrorMessage());
    }

    pluginHost = std::make_unique<engine::PluginHost>();

    context.emplace(AppContext{*engine, *pluginHost});
    mainWindow = std::make_unique<ui::MainWindow>(getApplicationName(), *context);
}

void App::shutdown()
{
    // Close the window first so no UI callback can try to touch the engine
    // during its teardown.
    mainWindow.reset();
    context.reset();
    pluginHost.reset();
    engine.reset();
}

} // namespace pedalboard3
