#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

// File > About (UI refresh U11, #24): the app's name, the build's version and
// the Pedalboard2 / JUCE credit, with Close. No license text and no
// source-code link until the license is chosen and the repository is public
// (#47, plan KD6). Opens as a dialog that owns its content.
void showAboutWindow();

// The dialog's content on its own (the UI snapshots tool renders it: UI chrome
// plan U1).
std::unique_ptr<juce::Component> makeAboutPanel();

} // namespace pedalboard3::ui
