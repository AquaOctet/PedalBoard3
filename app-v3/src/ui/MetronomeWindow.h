#pragma once

#include "AppContext.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::engine::processors
{
class MetronomeProcessor;
}

namespace pedalboard3::ui
{

// The Metronome's window (M6 U9, plan KD7 / KD11), opened by ED on its card:
//   - beats per bar and the note value (its own time signature, used unsynced)
//   - the level
//   - Sync to Transport
//   - optional accent and click samples in place of the synthesized clicks
// The card has Play and the beat readout.
class MetronomeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    MetronomeEditor(AppContext& ctx, engine::processors::MetronomeProcessor& metronome);
    ~MetronomeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseSample(bool accent);

    AppContext& ctx_;
    engine::processors::MetronomeProcessor& metronome_;

    juce::Label beatsLabel_;
    juce::Slider beats_;
    juce::Label noteLabel_;
    juce::ComboBox note_;
    juce::Label levelLabel_;
    juce::Slider level_;
    juce::ToggleButton syncButton_{"Sync to Transport"};
    juce::Label accentLabel_;
    juce::TextButton accentChoose_{"Choose..."};
    juce::TextButton accentClear_{"Built-in"};
    juce::Label clickLabel_;
    juce::TextButton clickChoose_{"Choose..."};
    juce::TextButton clickClear_{"Built-in"};
    juce::Label hint_;

    std::unique_ptr<juce::SliderParameterAttachment> beatsAttachment_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> noteAttachment_;
    std::unique_ptr<juce::SliderParameterAttachment> levelAttachment_;
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetronomeEditor)
};

} // namespace pedalboard3::ui
