#include "App.h"

namespace pedalboard3
{

void App::initialise(const juce::String& /*commandLine*/)
{
    mainWindow = std::make_unique<ui::MainWindow>(getApplicationName());
}

void App::shutdown()
{
    mainWindow.reset();
}

} // namespace pedalboard3
