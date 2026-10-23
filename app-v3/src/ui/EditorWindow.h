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
//
// Clearing the active-editor slots (JUCE 8.0.16):
// ------------------------------------------------
// ~AudioProcessorEditor does NOT tell its processor it is going; JUCE puts
// that on the host, and asserts the host did it. An uncleared slot used to
// be invisible — createEditorIfNeeded just handed the freed editor back —
// but 8.0.16's createEditorAndMakeActive refuses to build a second editor
// while one is registered, so a window that skipped this would open once
// and never again. There are two slots to clear, because a hosted plugin's
// editor is made by the inner processor while engine::BypassableInstance,
// the wrapper the canvas asked, records it as well.
// Releases `editor` from every processor holding it in an active-editor slot:
// `hostProcessor`, the one the editor was asked of, and the processor the
// editor itself points at, which for a hosted plugin is the one inside
// engine::BypassableInstance rather than the wrapper. The two coincide for a
// built-in processor and the second release is then a no-op.
//
// EditorWindow calls this before deleting the editor. It is a free function so
// the contract can be tested without a desktop window (the suite is headless).
void releaseEditorFromProcessors(juce::AudioProcessor& hostProcessor, juce::AudioProcessorEditor* editor);

class EditorWindow : public juce::DocumentWindow
{
public:
    // `hostProcessor` is the processor the editor was asked of, which for a
    // hosted plugin is the wrapper rather than the processor the editor
    // itself points at. Both are cleared on the way out.
    EditorWindow(const juce::String& title,
                 juce::AudioProcessor& hostProcessor,
                 juce::AudioProcessorEditor* editor,
                 std::function<void()> onClose);
    ~EditorWindow() override;

    void closeButtonPressed() override;

private:
    juce::AudioProcessor& hostProcessor_;
    std::function<void()> onClose_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
};

} // namespace pedalboard3::ui
