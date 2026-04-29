#include "EditorWindow.h"

namespace pedalboard3::ui
{

EditorWindow::EditorWindow(const juce::String& title,
                           juce::AudioProcessorEditor* editor,
                           std::function<void()> onClose)
    : juce::DocumentWindow(title,
                           juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                               juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::closeButton),
      onClose_(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setResizable(editor->isResizable(), false);
    setContentOwned(editor, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void EditorWindow::closeButtonPressed()
{
    if (onClose_)
        onClose_();
}

} // namespace pedalboard3::ui
