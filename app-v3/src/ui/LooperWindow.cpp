#include "LooperWindow.h"

#include "ColourScheme.h"
#include "engine/processors/LooperProcessor.h"
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

void styleLevel(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, kRow - 4);
    slider.setTextValueSuffix(" dB");
}
} // namespace

LooperEditor::LooperEditor(AppContext& ctx, engine::processors::LooperProcessor& looper)
    : juce::AudioProcessorEditor(looper), ctx_(ctx), looper_(looper)
{
    syncButton_.setTooltip(
        "Play the loop while the transport plays, at the transport's position counted from where it was recorded");
    autoPlayButton_.setTooltip("Play the loop as soon as its recording ends");
    stopAfterBarButton_.setTooltip("End the recording by itself after one bar, at the transport's tempo");
    styleCaption(beatsLabel_, "Beats per bar");
    beats_.setSliderStyle(juce::Slider::IncDecButtons);
    beats_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, kRow - 4);
    styleCaption(noteLabel_, "Note value");
    for (const auto* value : {"1", "2", "4", "8", "16"})
        note_.addItem(value, note_.getNumItems() + 1);
    styleCaption(inputLabel_, "Input level");
    styleLevel(input_);
    input_.setTooltip("The input passing through the Looper (-60 dB is silent: hear only the loop)");
    styleCaption(loopLabel_, "Loop level");
    styleLevel(loop_);

    if (auto* p = parameterOf<juce::AudioParameterBool>(looper_, "sync"))
        syncAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*p, syncButton_);
    if (auto* p = parameterOf<juce::AudioParameterBool>(looper_, "autoPlay"))
        autoPlayAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*p, autoPlayButton_);
    if (auto* p = parameterOf<juce::AudioParameterBool>(looper_, "stopAfterBar"))
        stopAfterBarAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*p, stopAfterBarButton_);
    if (auto* p = parameterOf<juce::AudioParameterInt>(looper_, "numerator"))
        beatsAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*p, beats_);
    if (auto* p = parameterOf<juce::AudioParameterChoice>(looper_, "denominator"))
        noteAttachment_ = std::make_unique<juce::ComboBoxParameterAttachment>(*p, note_);
    if (auto* p = parameterOf<juce::AudioParameterFloat>(looper_, "inputLevel"))
        inputAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*p, input_);
    if (auto* p = parameterOf<juce::AudioParameterFloat>(looper_, "loopLevel"))
        loopAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*p, loop_);

    styleCaption(folderCaption_, "Loops are saved as takes in");
    folderLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    folderLabel_.setMinimumHorizontalScale(0.6f);
    chooseFolderButton_.setTooltip("Choose the folder loops are saved into");
    chooseFolderButton_.onClick = [this] { chooseFolder(); };
    openFolderButton_.setTooltip("Show the takes folder");
    openFolderButton_.onClick = [this]
    {
        const auto folder = looper_.getFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };
    fileLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    fileLabel_.setMinimumHorizontalScale(0.6f);
    loadButton_.setTooltip("Load an audio file as the loop");
    loadButton_.onClick = [this] { chooseLoopFile(); };
    status_.setColour(juce::Label::textColourId, tokens::fgMuted);
    status_.setMinimumHorizontalScale(0.6f);

    for (auto* c : std::initializer_list<juce::Component*>{&syncButton_,
                                                           &autoPlayButton_,
                                                           &stopAfterBarButton_,
                                                           &beatsLabel_,
                                                           &beats_,
                                                           &noteLabel_,
                                                           &note_,
                                                           &inputLabel_,
                                                           &input_,
                                                           &loopLabel_,
                                                           &loop_,
                                                           &folderCaption_,
                                                           &folderLabel_,
                                                           &chooseFolderButton_,
                                                           &openFolderButton_,
                                                           &fileLabel_,
                                                           &loadButton_,
                                                           &status_})
        addAndMakeVisible(c);

    setSize(520, 350);
    timerCallback();
    startTimerHz(4);
}

LooperEditor::~LooperEditor()
{
    stopTimer();
}

void LooperEditor::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);
}

void LooperEditor::resized()
{
    auto area = getLocalBounds().reduced(kPad);
    auto toggles = area.removeFromTop(kRow);
    syncButton_.setBounds(toggles.removeFromLeft(180));
    autoPlayButton_.setBounds(toggles.removeFromLeft(110));
    stopAfterBarButton_.setBounds(toggles.removeFromLeft(150));
    area.removeFromTop(8);

    const auto row = [&area](juce::Label& label)
    {
        auto r = area.removeFromTop(kRow);
        label.setBounds(r.removeFromLeft(kLabelWidth));
        area.removeFromTop(6);
        return r;
    };
    beats_.setBounds(row(beatsLabel_).removeFromLeft(130));
    note_.setBounds(row(noteLabel_).removeFromLeft(90));
    input_.setBounds(row(inputLabel_));
    loop_.setBounds(row(loopLabel_));
    area.removeFromTop(6);

    folderCaption_.setBounds(area.removeFromTop(20));
    auto folderRow = area.removeFromTop(kRow);
    openFolderButton_.setBounds(folderRow.removeFromRight(100));
    folderRow.removeFromRight(6);
    chooseFolderButton_.setBounds(folderRow.removeFromRight(130));
    folderRow.removeFromRight(8);
    folderLabel_.setBounds(folderRow);
    area.removeFromTop(8);

    auto fileRow = area.removeFromTop(kRow);
    loadButton_.setBounds(fileRow.removeFromRight(120));
    fileRow.removeFromRight(8);
    fileLabel_.setBounds(fileRow);
    area.removeFromTop(6);
    status_.setBounds(area.removeFromTop(kRow));
}

void LooperEditor::timerCallback()
{
    folderLabel_.setText(looper_.getFolder().getFullPathName(), juce::dontSendNotification);
    const auto file = looper_.getLoopFile();
    fileLabel_.setText(file == juce::File() ? juce::String("No loop saved yet") : "Loop: " + file.getFileName(),
                       juce::dontSendNotification);

    juce::String status;
    if (looper_.isLoading())
        status = "Loading the loop...";
    else if (const auto error = looper_.getError(); error.isNotEmpty())
        status = error;
    else if (looper_.isRecording())
        status = "Recording. Press Record (or Play) on the card to end the loop.";
    else if (looper_.ranOutOfMemory())
        status = "The last recording filled the loop memory (10 minutes) and ended there.";
    else if (syncButton_.getToggleState())
        status = "Synced: the loop plays while the transport does, in step with it.";
    else
        status = "Record on the card starts a loop; press it again to end it.";
    status_.setText(status, juce::dontSendNotification);
}

void LooperEditor::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>("Choose where loops are saved", looper_.getFolder());
    juce::Component::SafePointer<LooperEditor> safeThis(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis](const juce::FileChooser& chooser)
                          {
                              auto* self = safeThis.getComponent();
                              if (self == nullptr || chooser.getResult() == juce::File())
                                  return;
                              self->looper_.setFolder(chooser.getResult());
                              // A new folder is a state edit: the board shows it's unsaved.
                              self->ctx_.session.parameterEdits().sendChangeMessage();
                              self->timerCallback();
                          });
}

void LooperEditor::chooseLoopFile()
{
    const auto current = looper_.getLoopFile();
    chooser_ = std::make_unique<juce::FileChooser>("Load a loop",
                                                   current != juce::File() ? current.getParentDirectory()
                                                                           : looper_.getFolder(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<LooperEditor> safeThis(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis](const juce::FileChooser& chooser)
                          {
                              auto* self = safeThis.getComponent();
                              if (self == nullptr || chooser.getResult() == juce::File())
                                  return;
                              self->looper_.loadLoop(chooser.getResult());
                              self->ctx_.session.parameterEdits().sendChangeMessage();
                              self->timerCallback();
                          });
}

} // namespace pedalboard3::ui
