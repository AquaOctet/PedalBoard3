#pragma once

#include "AppContext.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::engine::processors
{
class LooperProcessor;
}

namespace pedalboard3::ui
{

// The Looper's window (M6 U8, plan KD7 / KD11), opened by ED on its card:
//   - Sync to Transport, Auto-play and Stop After Bar
//   - the bar (beats and note value) Stop After Bar counts
//   - the input and loop levels
//   - where loops are saved as takes, and loading a loop from a file
// The card has Record, Play / Pause, Return to Zero and the loop's waveform.
class LooperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    LooperEditor(AppContext& ctx, engine::processors::LooperProcessor& looper);
    ~LooperEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFolder();
    void chooseLoopFile();

    AppContext& ctx_;
    engine::processors::LooperProcessor& looper_;

    juce::ToggleButton syncButton_{"Sync to Transport"};
    juce::ToggleButton autoPlayButton_{"Auto-play"};
    juce::ToggleButton stopAfterBarButton_{"Stop After Bar"};
    juce::Label beatsLabel_;
    juce::Slider beats_;
    juce::Label noteLabel_;
    juce::ComboBox note_;
    juce::Label inputLabel_;
    juce::Slider input_;
    juce::Label loopLabel_;
    juce::Slider loop_;
    juce::Label folderCaption_;
    juce::Label folderLabel_;
    juce::TextButton chooseFolderButton_{"Choose Folder..."};
    juce::TextButton openFolderButton_{"Show Takes"};
    juce::Label fileLabel_;
    juce::TextButton loadButton_{"Load Loop..."};
    juce::Label status_;

    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment_;
    std::unique_ptr<juce::ButtonParameterAttachment> autoPlayAttachment_;
    std::unique_ptr<juce::ButtonParameterAttachment> stopAfterBarAttachment_;
    std::unique_ptr<juce::SliderParameterAttachment> beatsAttachment_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> noteAttachment_;
    std::unique_ptr<juce::SliderParameterAttachment> inputAttachment_;
    std::unique_ptr<juce::SliderParameterAttachment> loopAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperEditor)
};

} // namespace pedalboard3::ui
