#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

// Settings > Tempo / BPM, Metronome and Looper (optional-features plan U2,
// KD7). Each window opens with "Enable <feature>"; the options below it are
// greyed out while the feature is off.
//   - Tempo / BPM: MIDI Machine Control, and the tempo and time signature new
//     patches start with
//   - Metronome: whether the transport shows the click button, and the level
//     new Metronomes start at
//   - Looper: the folder new Loopers save loops in
void showTempoSettingsWindow(AppContext& ctx);
void showMetronomeSettingsWindow(AppContext& ctx);
void showLooperSettingsWindow(AppContext& ctx);

// Each window's content on its own (the UI snapshots tool renders them: UI
// chrome plan U1).
std::unique_ptr<juce::Component> makeTempoSettingsPanel(AppContext& ctx);
std::unique_ptr<juce::Component> makeMetronomeSettingsPanel(AppContext& ctx);
std::unique_ptr<juce::Component> makeLooperSettingsPanel(AppContext& ctx);

} // namespace pedalboard3::ui
