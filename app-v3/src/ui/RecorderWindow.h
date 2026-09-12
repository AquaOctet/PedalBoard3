#pragma once

#include "AppContext.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::engine::processors
{
class AudioRecorderProcessor;
}

namespace pedalboard3::ui
{

// The Audio Recorder's window (M6 U7, plan KD6 / KD7), opened by ED on its
// card: where takes go (Choose Folder..., Open Folder), Sync to Transport, and
// the last take or why one couldn't be written. The card has Record, the time
// and the input level.
class RecorderEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    RecorderEditor(AppContext& ctx, engine::processors::AudioRecorderProcessor& recorder);
    ~RecorderEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFolder();

    AppContext& ctx_;
    engine::processors::AudioRecorderProcessor& recorder_;
    juce::Label folderCaption_;
    juce::Label folderLabel_;
    juce::TextButton chooseButton_{"Choose Folder..."};
    juce::TextButton openButton_{"Open Folder"};
    juce::ToggleButton syncButton_{"Sync to Transport"};
    juce::Label lastTakeLabel_;
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecorderEditor)
};

} // namespace pedalboard3::ui
