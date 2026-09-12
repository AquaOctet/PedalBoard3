#pragma once

#include "AppContext.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::engine::processors
{
class FilePlayerProcessor;
}

namespace pedalboard3::ui
{

// The File Player's window (M6 U6, plan KD7), opened by ED on its card: the
// file (with Choose File...), Loop and Sync to Transport, and the file's
// length or why it couldn't be read. The card's control row has Play and
// Return to Zero and the waveform; this has the rest.
class FilePlayerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    FilePlayerEditor(AppContext& ctx, engine::processors::FilePlayerProcessor& player);
    ~FilePlayerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile();

    AppContext& ctx_;
    engine::processors::FilePlayerProcessor& player_;
    juce::Label fileLabel_;
    juce::Label infoLabel_;
    juce::TextButton chooseButton_{"Choose File..."};
    juce::ToggleButton loopButton_{"Loop"};
    juce::ToggleButton syncButton_{"Sync to Transport"};
    std::unique_ptr<juce::ButtonParameterAttachment> loopAttachment_;
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilePlayerEditor)
};

} // namespace pedalboard3::ui
