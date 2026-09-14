#pragma once

#include <memory>

namespace juce
{
class Component;
class Value;
} // namespace juce

namespace pedalboard3
{
struct AppContext;
} // namespace pedalboard3

namespace pedalboard3::ui
{

// The Settings dialogs (UI refresh U10, plan KD10), opened from the Settings
// menu on the left of the top bar (on macOS, the menu bar's Settings menu).
// Each opens as a dialog that owns its content.

// The audio device and its channels (ui::AudioSettingsPanel), and the Test
// Tone toggle. `testTone` is the top bar's shared on / off value, so
// the dialog, the Settings menu and the tone itself stay in step.
void showAudioDevicesWindow(AppContext& ctx, juce::Value& testTone);

// Which MIDI inputs are enabled: they feed mapped controls and Program Change.
void showMidiSettingsWindow(AppContext& ctx);

// The OSC listener's port and multicast group.
void showOscSettingsWindow(AppContext& ctx);

// Each dialog's content on its own, as the windows above open it (the UI
// snapshots tool renders them: UI chrome plan U1).
std::unique_ptr<juce::Component> makeAudioDevicesPanel(AppContext& ctx, juce::Value& testTone);
std::unique_ptr<juce::Component> makeMidiSettingsPanel(AppContext& ctx);
std::unique_ptr<juce::Component> makeOscSettingsPanel(AppContext& ctx);

} // namespace pedalboard3::ui
