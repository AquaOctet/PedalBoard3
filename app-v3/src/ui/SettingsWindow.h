#pragma once

namespace pedalboard3
{
struct AppContext;
} // namespace pedalboard3

namespace pedalboard3::ui
{

// Settings (UI refresh U10, plan KD10): the audio device and MIDI inputs
// (juce::AudioDeviceSelectorComponent) above the OSC listener's port and
// multicast group, which moved here from the top bar. Opens as a dialog that
// owns its content.
void showSettingsWindow(AppContext& ctx);

} // namespace pedalboard3::ui
