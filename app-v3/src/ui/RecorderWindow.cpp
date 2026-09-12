#include "RecorderWindow.h"

#include "ColourScheme.h"
#include "engine/processors/AudioRecorderProcessor.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kPad = 16;
constexpr int kRow = 28;

juce::RangedAudioParameter* boolParameter(juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* p : processor.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p); b != nullptr && b->paramID == id)
            return b;
    return nullptr;
}
} // namespace

RecorderEditor::RecorderEditor(AppContext& ctx, engine::processors::AudioRecorderProcessor& recorder)
    : juce::AudioProcessorEditor(recorder), ctx_(ctx), recorder_(recorder)
{
    folderCaption_.setText("Takes go to", juce::dontSendNotification);
    folderCaption_.setColour(juce::Label::textColourId, tokens::fgMuted);
    folderLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    folderLabel_.setMinimumHorizontalScale(0.6f);
    lastTakeLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    lastTakeLabel_.setMinimumHorizontalScale(0.6f);

    chooseButton_.setTooltip("Choose the folder new takes go into");
    chooseButton_.onClick = [this] { chooseFolder(); };
    openButton_.setTooltip("Show the takes folder");
    openButton_.onClick = [this]
    {
        const auto folder = recorder_.getFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };
    syncButton_.setTooltip("Record while the transport plays: every Play starts a new take, Stop ends it");
    if (auto* sync = boolParameter(recorder_, "sync"))
        syncAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*sync, syncButton_);

    for (auto* c : std::initializer_list<juce::Component*>{
             &folderCaption_, &folderLabel_, &chooseButton_, &openButton_, &syncButton_, &lastTakeLabel_})
        addAndMakeVisible(c);

    setSize(480, 190);
    timerCallback();
    startTimerHz(4);
}

RecorderEditor::~RecorderEditor()
{
    stopTimer();
}

void RecorderEditor::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);
}

void RecorderEditor::resized()
{
    auto area = getLocalBounds().reduced(kPad);
    folderCaption_.setBounds(area.removeFromTop(20));
    auto folderRow = area.removeFromTop(kRow);
    openButton_.setBounds(folderRow.removeFromRight(100));
    folderRow.removeFromRight(6);
    chooseButton_.setBounds(folderRow.removeFromRight(130));
    folderRow.removeFromRight(8);
    folderLabel_.setBounds(folderRow);
    area.removeFromTop(10);
    syncButton_.setBounds(area.removeFromTop(kRow).removeFromLeft(260));
    area.removeFromTop(8);
    lastTakeLabel_.setBounds(area.removeFromTop(kRow));
}

void RecorderEditor::timerCallback()
{
    folderLabel_.setText(recorder_.getFolder().getFullPathName(), juce::dontSendNotification);
    juce::String status;
    if (const auto error = recorder_.getError(); error.isNotEmpty())
        status = error;
    else if (const auto last = recorder_.getLastTake(); last != juce::File())
        status = "Last take: " + last.getFileName();
    else
        status = "Each take is a new file named after the patch and the time.";
    if (recorder_.hasDroppedAudio())
        status += "  (The disk fell behind and some audio was dropped.)";
    lastTakeLabel_.setText(status, juce::dontSendNotification);
}

void RecorderEditor::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>("Choose where takes go", recorder_.getFolder());
    juce::Component::SafePointer<RecorderEditor> safeThis(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis](const juce::FileChooser& chooser)
                          {
                              auto* self = safeThis.getComponent();
                              if (self == nullptr || chooser.getResult() == juce::File())
                                  return;
                              self->recorder_.setFolder(chooser.getResult());
                              // A new folder is a state edit: the board shows it's unsaved.
                              self->ctx_.session.parameterEdits().sendChangeMessage();
                              self->timerCallback();
                          });
}

} // namespace pedalboard3::ui
