#include "EditorWindow.h"

namespace pedalboard3::ui
{

void releaseEditorFromProcessors(juce::AudioProcessor& hostProcessor, juce::AudioProcessorEditor* editor)
{
    if (editor == nullptr)
        return;

    hostProcessor.editorBeingDeleted(editor);

    if (auto* owner = editor->getAudioProcessor(); owner != nullptr && owner != &hostProcessor)
        owner->editorBeingDeleted(editor);
}

EditorWindow::EditorWindow(const juce::String& title,
                           juce::AudioProcessor& hostProcessor,
                           juce::AudioProcessorEditor* editor,
                           std::function<void()> onClose)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::closeButton),
      hostProcessor_(hostProcessor), onClose_(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setResizable(editor->isResizable(), false);
    setContentOwned(editor, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

EditorWindow::~EditorWindow()
{
    // Release the editor from both processors before the base class deletes
    // it: ~AudioProcessorEditor asserts its processor has already let go, and
    // an un-released slot makes createEditorAndMakeActive refuse the next
    // editor, so the window would never reopen.
    //
    // Two calls, because the hosted case has two registrations: the canvas
    // asked `hostProcessor_` (the BypassableInstance wrapper), which filled its
    // own slot, while the editor itself was built by — and points at — the
    // plugin inside. For a built-in processor the two are the same object and
    // the second call finds nothing to do, which is a no-op by design.
    releaseEditorFromProcessors(hostProcessor_, dynamic_cast<juce::AudioProcessorEditor*>(getContentComponent()));
}

void EditorWindow::closeButtonPressed()
{
    if (onClose_)
        onClose_();
}

} // namespace pedalboard3::ui
