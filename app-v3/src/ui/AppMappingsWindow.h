#pragma once

#include "AppContext.h"
#include "DialogFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace pedalboard3::ui
{

// The App Mappings window's content on its own: its Keyboard and MIDI / OSC
// tabs (the UI snapshots tool renders it: UI chrome U7). `initialTabIndex`
// picks which tab is showing — 0 Keyboard, 1 MIDI / OSC — so the snapshot
// tool can photograph the second one; the window itself always opens on the
// first.
std::unique_ptr<juce::Component> makeAppMappingsContent(AppContext& ctx,
                                                        juce::KeyPressMappingSet& keyMappings,
                                                        int initialTabIndex = 0);

// App Mappings window (roadmap M4 U5, plan
// docs/plans/2026-09-11-002-feat-mappings-ui-plan.md, KD2 / KD3 / KTD6): one
// place to bind app commands (next / previous patch) to triggers.
//
//   Keyboard   juce::KeyMappingEditorComponent over MainWindow's
//              ApplicationCommandManager key mappings; MainWindow persists
//              every change to Preferences.
//   MIDI / OSC the app-level mappings in session::AppMappings (command,
//              source, learn, delete); edits apply live and persist through
//              PreferencesPersister.
//
// In the dialog frame (UI chrome U4). MainWindow owns at most one and drops it
// from `onClose`, the EditorWindow contract. Closing the window cancels a learn
// it armed.
class AppMappingsWindow : public DialogFrame
{
public:
    AppMappingsWindow(AppContext& ctx, juce::KeyPressMappingSet& keyMappings, std::function<void()> onClose);

    void closeButtonPressed() override;

private:
    std::function<void()> onClose_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppMappingsWindow)
};

} // namespace pedalboard3::ui
