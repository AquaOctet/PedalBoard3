#include "FeatureSettingsWindows.h"

#include "ColourScheme.h"
#include "engine/Recordings.h"
#include "session/FeatureGate.h"
#include "session/Preferences.h"
#include "session/TransportCommands.h"

#include <memory>
#include <vector>

namespace pedalboard3::ui
{

namespace
{

constexpr int kPad = 16;
constexpr int kLabelWidth = 170;
constexpr int kRowHeight = 28;

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    label.setColour(juce::Label::textColourId, tokens::fgMuted);
}

void styleHint(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(12.0f)));
    label.setColour(juce::Label::textColourId, tokens::fgMuted);
    label.setJustificationType(juce::Justification::topLeft);
}

void launch(juce::Component* content, const juce::String& title)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = title;
    options.dialogBackgroundColour = tokens::bgPopover;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

// The top every feature window shares: "Enable <feature>" and what it does,
// with the options below greyed out while it's off. The switch follows
// changes made elsewhere (another window, the menu bar).
class FeaturePanel : public juce::Component, private juce::ChangeListener
{
public:
    FeaturePanel(AppContext& ctx, session::Feature feature, const juce::String& description)
        : ctx_(ctx), feature_(feature)
    {
        enable_.setButtonText("Enable " + session::featureName(feature_));
        enable_.setToggleState(ctx_.features.isEnabled(feature_), juce::dontSendNotification);
        enable_.onClick = [this]
        {
            ctx_.features.setEnabled(feature_, enable_.getToggleState());
            refreshOptions();
        };
        styleHint(description_, description);
        addAndMakeVisible(enable_);
        addAndMakeVisible(description_);
        ctx_.features.addChangeListener(this);
    }

    ~FeaturePanel() override { ctx_.features.removeChangeListener(this); }

protected:
    // Lays out the top; returns the area below it.
    juce::Rectangle<int> layoutTop()
    {
        auto area = getLocalBounds().reduced(kPad, 12);
        enable_.setBounds(area.removeFromTop(kRowHeight).removeFromLeft(280));
        description_.setBounds(area.removeFromTop(40));
        area.removeFromTop(8);
        return area;
    }

    // The components greyed out while the feature is off.
    void setOptions(std::initializer_list<juce::Component*> options)
    {
        options_.assign(options.begin(), options.end());
        refreshOptions();
    }

    AppContext& ctx_;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        enable_.setToggleState(ctx_.features.isEnabled(feature_), juce::dontSendNotification);
        refreshOptions();
    }

    void refreshOptions()
    {
        const bool on = enable_.getToggleState();
        for (auto* option : options_)
            option->setEnabled(on);
    }

    const session::Feature feature_;
    juce::ToggleButton enable_;
    juce::Label description_;
    std::vector<juce::Component*> options_;
};

// ---- Tempo / BPM -------------------------------------------------------------

const juce::StringArray kSignatures{"2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "12/8"};

class TempoPanel final : public FeaturePanel
{
public:
    explicit TempoPanel(AppContext& ctx)
        : FeaturePanel(ctx,
                       session::Feature::Tempo,
                       "Shows the transport (play, tempo, time signature and position) and lets keys, "
                       "mappings and MMC run it. Each patch keeps its own tempo.")
    {
        styleCaption(newHeader_, "NEW PATCHES START AT");
        tempoLabel_.setText("Tempo", juce::dontSendNotification);
        tempo_.setSliderStyle(juce::Slider::IncDecButtons);
        tempo_.setRange(20.0, 300.0, 0.1);
        tempo_.setNumDecimalPlacesToDisplay(1);
        tempo_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 80, kRowHeight - 4);
        tempo_.setTextValueSuffix(" BPM");
        tempo_.setValue(ctx_.preferences.getNewPatchTempo(), juce::dontSendNotification);
        tempo_.onValueChange = [this] { ctx_.preferences.setNewPatchTempo(tempo_.getValue()); };

        signatureLabel_.setText("Time signature", juce::dontSendNotification);
        signature_.addItemList(kSignatures, 1);
        const auto [numerator, denominator] = ctx_.preferences.getNewPatchTimeSignature();
        const auto current = juce::String(numerator) + "/" + juce::String(denominator);
        if (!kSignatures.contains(current))
            signature_.addItem(current, kSignatures.size() + 1);
        signature_.setText(current, juce::dontSendNotification);
        signature_.onChange = [this]
        {
            const auto text = signature_.getText();
            ctx_.preferences.setNewPatchTimeSignature(text.upToFirstOccurrenceOf("/", false, false).getIntValue(),
                                                      text.fromFirstOccurrenceOf("/", false, false).getIntValue());
        };

        styleCaption(transportHeader_, "TRANSPORT");
        mmc_.setToggleState(ctx_.transportCommands.isMmcEnabled(), juce::dontSendNotification);
        mmc_.setTooltip("Play, Stop, Pause and Rewind from a controller or another app run the transport");
        mmc_.onClick = [this]
        {
            ctx_.transportCommands.setMmcEnabled(mmc_.getToggleState());
            ctx_.preferences.setMmcTransport(mmc_.getToggleState());
        };

        for (auto* c : std::initializer_list<juce::Component*>{
                 &newHeader_, &tempoLabel_, &tempo_, &signatureLabel_, &signature_, &transportHeader_, &mmc_})
            addAndMakeVisible(c);
        setOptions({&tempoLabel_, &tempo_, &signatureLabel_, &signature_, &mmc_});
        setSize(480, 262);
    }

    void resized() override
    {
        auto area = layoutTop();
        newHeader_.setBounds(area.removeFromTop(22));
        auto row = area.removeFromTop(kRowHeight);
        tempoLabel_.setBounds(row.removeFromLeft(kLabelWidth));
        tempo_.setBounds(row.removeFromLeft(170));
        area.removeFromTop(6);
        row = area.removeFromTop(kRowHeight);
        signatureLabel_.setBounds(row.removeFromLeft(kLabelWidth));
        signature_.setBounds(row.removeFromLeft(90));
        area.removeFromTop(14);
        transportHeader_.setBounds(area.removeFromTop(22));
        mmc_.setBounds(area.removeFromTop(kRowHeight));
    }

private:
    juce::Label newHeader_;
    juce::Label tempoLabel_;
    juce::Slider tempo_;
    juce::Label signatureLabel_;
    juce::ComboBox signature_;
    juce::Label transportHeader_;
    juce::ToggleButton mmc_{"MIDI Machine Control (MMC) runs the transport"};
};

// ---- Metronome -----------------------------------------------------------------

class MetronomePanel final : public FeaturePanel
{
public:
    explicit MetronomePanel(AppContext& ctx)
        : FeaturePanel(ctx,
                       session::Feature::Metronome,
                       "Adds the Metronome to Add Plugin and the plugin browser: an accented click on beat 1, "
                       "synced to the transport or on its own.")
    {
        click_.setButtonText(
            juce::String(juce::CharPointer_UTF8("Show the \xe2\x99\xa9 click button in the transport")));
        click_.setToggleState(ctx_.preferences.getMetronomeClickButton(), juce::dontSendNotification);
        click_.setTooltip("One click starts or stops the board's Metronome");
        click_.onClick = [this] { ctx_.preferences.setMetronomeClickButton(click_.getToggleState()); };

        levelLabel_.setText("Level for new Metronomes", juce::dontSendNotification);
        level_.setSliderStyle(juce::Slider::LinearHorizontal);
        level_.setRange(-40.0, 6.0, 0.5);
        level_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, kRowHeight - 4);
        level_.setTextValueSuffix(" dB");
        level_.setValue(ctx_.preferences.getNewMetronomeLevel(), juce::dontSendNotification);
        level_.onValueChange = [this] { ctx_.preferences.setNewMetronomeLevel(static_cast<float>(level_.getValue())); };

        for (auto* c : std::initializer_list<juce::Component*>{&click_, &levelLabel_, &level_})
            addAndMakeVisible(c);
        setOptions({&click_, &levelLabel_, &level_});
        setSize(480, 172);
    }

    void resized() override
    {
        auto area = layoutTop();
        click_.setBounds(area.removeFromTop(kRowHeight));
        area.removeFromTop(6);
        auto row = area.removeFromTop(kRowHeight);
        levelLabel_.setBounds(row.removeFromLeft(kLabelWidth));
        level_.setBounds(row);
    }

private:
    juce::ToggleButton click_;
    juce::Label levelLabel_;
    juce::Slider level_;
};

// ---- Looper ----------------------------------------------------------------------

class LooperPanel final : public FeaturePanel
{
public:
    explicit LooperPanel(AppContext& ctx)
        : FeaturePanel(ctx,
                       session::Feature::Looper,
                       "Adds the Looper to Add Plugin and the plugin browser: record a phrase and play it back "
                       "as a loop. Each loop is also saved as a take.")
    {
        styleCaption(folderCaption_, "NEW LOOPERS SAVE LOOPS IN");
        folder_.setColour(juce::Label::textColourId, tokens::fgPrimary);
        folder_.setMinimumHorizontalScale(0.6f);
        choose_.setTooltip("Choose the folder new Loopers save loops into");
        choose_.onClick = [this] { chooseFolder(); };
        reset_.setTooltip("Documents/Pedalboard 3/Recordings");
        reset_.onClick = [this]
        {
            ctx_.preferences.setLooperFolder({});
            refreshFolder();
        };
        for (auto* c : std::initializer_list<juce::Component*>{&folderCaption_, &folder_, &choose_, &reset_})
            addAndMakeVisible(c);
        setOptions({&folder_, &choose_, &reset_});
        refreshFolder();
        setSize(520, 158);
    }

    void resized() override
    {
        auto area = layoutTop();
        folderCaption_.setBounds(area.removeFromTop(22));
        auto row = area.removeFromTop(kRowHeight);
        reset_.setBounds(row.removeFromRight(80));
        row.removeFromRight(6);
        choose_.setBounds(row.removeFromRight(100));
        row.removeFromRight(8);
        folder_.setBounds(row);
    }

private:
    void refreshFolder()
    {
        const auto chosen = ctx_.preferences.getLooperFolder();
        const auto folder = chosen != juce::File() ? chosen : engine::recordings::defaultFolder();
        folder_.setText(folder.getFullPathName(), juce::dontSendNotification);
    }

    void chooseFolder()
    {
        const auto chosen = ctx_.preferences.getLooperFolder();
        chooser_ =
            std::make_unique<juce::FileChooser>("Choose where new Loopers save loops",
                                                chosen != juce::File() ? chosen : engine::recordings::defaultFolder());
        juce::Component::SafePointer<LooperPanel> safeThis(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis](const juce::FileChooser& chooser)
                              {
                                  auto* self = safeThis.getComponent();
                                  if (self == nullptr || chooser.getResult() == juce::File())
                                      return;
                                  self->ctx_.preferences.setLooperFolder(chooser.getResult());
                                  self->refreshFolder();
                              });
    }

    juce::Label folderCaption_;
    juce::Label folder_;
    juce::TextButton choose_{"Choose..."};
    juce::TextButton reset_{"Default"};
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace

std::unique_ptr<juce::Component> makeTempoSettingsPanel(AppContext& ctx)
{
    return std::make_unique<TempoPanel>(ctx);
}

std::unique_ptr<juce::Component> makeMetronomeSettingsPanel(AppContext& ctx)
{
    return std::make_unique<MetronomePanel>(ctx);
}

std::unique_ptr<juce::Component> makeLooperSettingsPanel(AppContext& ctx)
{
    return std::make_unique<LooperPanel>(ctx);
}

void showTempoSettingsWindow(AppContext& ctx)
{
    launch(makeTempoSettingsPanel(ctx).release(), "Tempo / BPM Settings");
}

void showMetronomeSettingsWindow(AppContext& ctx)
{
    launch(makeMetronomeSettingsPanel(ctx).release(), "Metronome Settings");
}

void showLooperSettingsWindow(AppContext& ctx)
{
    launch(makeLooperSettingsPanel(ctx).release(), "Looper Settings");
}

} // namespace pedalboard3::ui
