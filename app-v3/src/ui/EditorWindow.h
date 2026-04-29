#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace pedalboard3::ui
{

// Holds a plugin's AudioProcessorEditor in a juce::DocumentWindow and
// invokes a callback on close so the owner can drop its handle.
//
// Ownership and lifecycle (Path γ contract — AC-UI-P / D1b):
// ----------------------------------------------------------
// SignalCanvas owns these via:
//   std::unordered_map<session::NodeId, std::unique_ptr<EditorWindow>>
//
// `Session::removeNode` and `Session::commitLoadPatch` destroy the
// underlying `BypassableInstance` synchronously inline — by the time
// `sendChangeMessage` (async) fires, the editor's `AudioProcessor*`
// is already a dangling pointer. Therefore: the canvas closes the
// EditorWindow for a NodeId synchronously, on the message thread,
// BEFORE invoking the Session mutator. Listener-side cleanup is
// defensive only.
//
// `setContentOwned(editor, true)` makes this window delete the
// AudioProcessorEditor on destruction. JUCE's editor owns no
// AudioProcessor pointer ownership — that lives in the underlying
// graph node — so this is safe as long as the close-before-call
// discipline above holds.
class EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow(const juce::String& title,
                 juce::AudioProcessorEditor* editor,
                 std::function<void()> onClose);

    void closeButtonPressed() override;

private:
    std::function<void()> onClose_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
};

} // namespace pedalboard3::ui
