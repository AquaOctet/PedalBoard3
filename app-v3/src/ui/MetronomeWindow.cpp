#include "MetronomeWindow.h"

#include "ColourScheme.h"
#include "engine/processors/MetronomeProcessor.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kPad = 16;
constexpr int kRow = 28;
constexpr int kLabelWidth = 110;

template <typename Parameter>
Parameter* parameterOf(juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* p : processor.getParameters())
        if (auto* typed = dynamic_cast<Parameter*>(p); typed != nullptr && typed->paramID == id)
            return typed;
    return nullptr;
}

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, tokens::fgMuted);
}

juce::String sampleText(const juce::File& file)
{
    return file == juce::File() ? juce::String("Built-in click") : file.getFileName();
}
} // namespace

MetronomeEditor::MetronomeEditor(AppContext& ctx, engine::processors::MetronomeProcessor& metronome)
    : juce::AudioProcessorEditor(metronome), ctx_(ctx), metronome_(metronome)
{
    styleCaption(beatsLabel_, "Beats per bar");
    beats_.setSliderStyle(juce::Slider::IncDecButtons);
    beats_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, kRow - 4);
    styleCaption(noteLabel_, "Note value");
    for (const auto* value : {"1", "2", "4", "8", "16"})
        note_.addItem(value, note_.getNumItems() + 1);
    styleCaption(levelLabel_, "Level");
    level_.setSliderStyle(juce::Slider::LinearHorizontal);
    level_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, kRow - 4);
    level_.setTextValueSuffix(" dB");
    syncButton_.setTooltip("Click on the transport's beats, in the patch's time signature, while it plays");

    if (auto* p = parameterOf<juce::AudioParameterInt>(metronome_, "numerator"))
        beatsAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*p, beats_);
    if (auto* p = parameterOf<juce::AudioParameterChoice>(metronome_, "denominator"))
        noteAttachment_ = std::make_unique<juce::ComboBoxParameterAttachment>(*p, note_);
    if (auto* p = parameterOf<juce::AudioParameterFloat>(metronome_, "level"))
        levelAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*p, level_);
    if (auto* p = parameterOf<juce::AudioParameterBool>(metronome_, "sync"))
        syncAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*p, syncButton_);

    accentChoose_.onClick = [this] { chooseSample(true); };
    clickChoose_.onClick = [this] { chooseSample(false); };
    accentClear_.setTooltip("Use the built-in accent click");
    clickClear_.setTooltip("Use the built-in click");
    accentClear_.onClick = [this]
    {
        metronome_.setSampleFiles({}, metronome_.getClickFile());
        ctx_.session.parameterEdits().sendChangeMessage();
    };
    clickClear_.onClick = [this]
    {
        metronome_.setSampleFiles(metronome_.getAccentFile(), {});
        ctx_.session.parameterEdits().sendChangeMessage();
    };
    hint_.setColour(juce::Label::textColourId, tokens::fgMuted);

    for (auto* c : std::initializer_list<juce::Component*>{&beatsLabel_,
                                                           &beats_,
                                                           &noteLabel_,
                                                           &note_,
                                                           &levelLabel_,
                                                           &level_,
                                                           &syncButton_,
                                                           &accentLabel_,
                                                           &accentChoose_,
                                                           &accentClear_,
                                                           &clickLabel_,
                                                           &clickChoose_,
                                                           &clickClear_,
                                                           &hint_})
        addAndMakeVisible(c);

    setSize(460, 290);
    timerCallback();
    startTimerHz(4);
}

MetronomeEditor::~MetronomeEditor()
{
    stopTimer();
}

void MetronomeEditor::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);
}

void MetronomeEditor::resized()
{
    auto area = getLocalBounds().reduced(kPad);
    const auto row = [&area](juce::Label& label)
    {
        auto r = area.removeFromTop(kRow);
        label.setBounds(r.removeFromLeft(kLabelWidth));
        area.removeFromTop(6);
        return r;
    };
    beats_.setBounds(row(beatsLabel_).removeFromLeft(130));
    note_.setBounds(row(noteLabel_).removeFromLeft(90));
    level_.setBounds(row(levelLabel_));
    syncButton_.setBounds(area.removeFromTop(kRow).removeFromLeft(240));
    area.removeFromTop(8);

    auto accent = area.removeFromTop(kRow);
    accentClear_.setBounds(accent.removeFromRight(80));
    accent.removeFromRight(6);
    accentChoose_.setBounds(accent.removeFromRight(90));
    accentLabel_.setBounds(accent);
    area.removeFromTop(6);
    auto click = area.removeFromTop(kRow);
    clickClear_.setBounds(click.removeFromRight(80));
    click.removeFromRight(6);
    clickChoose_.setBounds(click.removeFromRight(90));
    clickLabel_.setBounds(click);
    area.removeFromTop(6);
    hint_.setBounds(area.removeFromTop(kRow));
}

void MetronomeEditor::timerCallback()
{
    accentLabel_.setText("Accent: " + sampleText(metronome_.getAccentFile()), juce::dontSendNotification);
    clickLabel_.setText("Click: " + sampleText(metronome_.getClickFile()), juce::dontSendNotification);
    juce::String hint;
    if (metronome_.isLoading())
        hint = "Loading...";
    else if (const auto error = metronome_.getLoadError(); error.isNotEmpty())
        hint = error;
    else if (syncButton_.getToggleState())
        hint = "Synced: it counts in the patch's time signature, from the transport.";
    else
        hint = "Play on its card starts it on beat 1 at the patch tempo.";
    hint_.setText(hint, juce::dontSendNotification);
}

void MetronomeEditor::chooseSample(bool accent)
{
    const auto current = accent ? metronome_.getAccentFile() : metronome_.getClickFile();
    chooser_ =
        std::make_unique<juce::FileChooser>(accent ? "Choose the accent sample" : "Choose the click sample",
                                            current != juce::File() ? current.getParentDirectory() : juce::File(),
                                            "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<MetronomeEditor> safeThis(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis, accent](const juce::FileChooser& chooser)
                          {
                              auto* self = safeThis.getComponent();
                              if (self == nullptr || chooser.getResult() == juce::File())
                                  return;
                              auto& m = self->metronome_;
                              if (accent)
                                  m.setSampleFiles(chooser.getResult(), m.getClickFile());
                              else
                                  m.setSampleFiles(m.getAccentFile(), chooser.getResult());
                              self->ctx_.session.parameterEdits().sendChangeMessage();
                              self->timerCallback();
                          });
}

} // namespace pedalboard3::ui
