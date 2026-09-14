#include "FilePlayerWindow.h"

#include "CardText.h"
#include "ColourScheme.h"
#include "engine/processors/FilePlayerProcessor.h"
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

FilePlayerEditor::FilePlayerEditor(AppContext& ctx, engine::processors::FilePlayerProcessor& player)
    : juce::AudioProcessorEditor(player), ctx_(ctx), player_(player)
{
    fileLabel_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    fileLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    infoLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);

    chooseButton_.setTooltip("Choose a WAV, AIFF, FLAC, Ogg or MP3 file (up to 30 minutes)");
    chooseButton_.onClick = [this] { chooseFile(); };
    loopButton_.setTooltip("At the end of the file, start again from the top");
    syncButton_.setTooltip("Play from the transport's position: Play, Stop and Return to Zero on the transport "
                           "run this player too");

    if (auto* loop = boolParameter(player_, "looping"))
        loopAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*loop, loopButton_);
    if (auto* sync = boolParameter(player_, "sync"))
        syncAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*sync, syncButton_);

    for (auto* c :
         std::initializer_list<juce::Component*>{&fileLabel_, &infoLabel_, &chooseButton_, &loopButton_, &syncButton_})
        addAndMakeVisible(c);

    setSize(460, 170);
    timerCallback();
    startTimerHz(10);
}

FilePlayerEditor::~FilePlayerEditor()
{
    stopTimer();
}

void FilePlayerEditor::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);
}

void FilePlayerEditor::resized()
{
    auto area = getLocalBounds().reduced(kPad);
    auto top = area.removeFromTop(kRow);
    chooseButton_.setBounds(top.removeFromRight(130));
    top.removeFromRight(8);
    fileLabel_.setBounds(top);
    area.removeFromTop(4);
    infoLabel_.setBounds(area.removeFromTop(kRow));
    area.removeFromTop(8);
    loopButton_.setBounds(area.removeFromTop(kRow).removeFromLeft(200));
    syncButton_.setBounds(area.removeFromTop(kRow).removeFromLeft(240));
}

void FilePlayerEditor::timerCallback()
{
    const auto file = player_.getFile();
    fileLabel_.setText(file == juce::File() ? juce::String("No file") : file.getFileName(), juce::dontSendNotification);

    juce::String info;
    if (player_.isLoading())
        info = "Loading...";
    else if (const auto error = player_.getLoadError(); error.isNotEmpty())
        info = error;
    else if (file == juce::File())
        info = "Choose a file, or drop one on the canvas.";
    else
        info =
            card::minutesAndSeconds(player_.getLengthSeconds()) + "   " + file.getParentDirectory().getFullPathName();
    infoLabel_.setText(info, juce::dontSendNotification);
}

void FilePlayerEditor::chooseFile()
{
    const auto current = player_.getFile();
    const auto start = current != juce::File() ? current.getParentDirectory()
                                               : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    chooser_ =
        std::make_unique<juce::FileChooser>("Choose a file to play", start, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<FilePlayerEditor> safeThis(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis](const juce::FileChooser& chooser)
                          {
                              auto* self = safeThis.getComponent();
                              if (self == nullptr || chooser.getResult() == juce::File())
                                  return;
                              self->player_.setFile(chooser.getResult());
                              // A new file is a state edit, like turning a plugin's knob: the
                              // board shows it's unsaved.
                              self->ctx_.session.parameterEdits().sendChangeMessage();
                              self->timerCallback();
                          });
}

} // namespace pedalboard3::ui
