#include "SettingsWindow.h"

#include "AppContext.h"
#include "ColourScheme.h"
#include "control/OscMapping.h" // control::isOscMulticastAddress
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "session/Preferences.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

namespace pedalboard3::ui
{

namespace
{

constexpr int kPad = 16;
constexpr int kLabelWidth = 130;
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

// ---- Audio Devices ------------------------------------------------------------

class AudioDevicesPanel : public juce::Component
{
public:
    AudioDevicesPanel(AppContext& ctx, juce::Value& testTone)
        : device_(*ctx.audioEngine.getDeviceManager(),
                  0,
                  2,
                  0,
                  2,
                  /*showMidiInputOptions*/ false, // MIDI Settings has those
                  /*showMidiOutputSelector*/ false,
                  /*showChannelsAsStereoPairs*/ true,
                  /*hideAdvancedOptionsWithButton*/ false)
    {
        addAndMakeVisible(device_);

        styleCaption(toneHeader_, "TEST TONE");
        toneButton_.getToggleStateValue().referTo(testTone); // shared with the Settings menu
        toneButton_.setTooltip("Play a 440 Hz tone straight to the output, bypassing the chain");
        styleHint(toneHint_, "Checks the output device without the board. Switching patch turns it off.");
        for (auto* c : std::initializer_list<juce::Component*>{&toneHeader_, &toneButton_, &toneHint_})
            addAndMakeVisible(c);

        setSize(520, 510);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        device_.setBounds(area.removeFromTop(420));
        auto tone = area.reduced(kPad, 6);
        toneHeader_.setBounds(tone.removeFromTop(22));
        toneButton_.setBounds(tone.removeFromTop(kRowHeight).removeFromLeft(160));
        toneHint_.setBounds(tone.removeFromTop(22));
    }

private:
    juce::AudioDeviceSelectorComponent device_;
    juce::Label toneHeader_;
    juce::ToggleButton toneButton_{"Test Tone"};
    juce::Label toneHint_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDevicesPanel)
};

// ---- MIDI Settings --------------------------------------------------------------

class MidiSettingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit MidiSettingsPanel(AppContext& ctx) : devices_(*ctx.audioEngine.getDeviceManager())
    {
        styleCaption(header_, "MIDI INPUTS");
        styleHint(hint_, "Enabled inputs drive mapped controls and Program Change patch switching.");
        styleHint(empty_, "No MIDI inputs found. Connect a controller; it appears here.");
        for (auto* c : std::initializer_list<juce::Component*>{&header_, &hint_, &empty_})
            addAndMakeVisible(c);

        devices_.addChangeListener(this); // inputs come and go
        rebuild();
    }

    ~MidiSettingsPanel() override { devices_.removeChangeListener(this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kPad, 12);
        header_.setBounds(area.removeFromTop(22));
        hint_.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        empty_.setBounds(area.removeFromTop(kRowHeight));
        for (auto& toggle : toggles_)
            toggle->setBounds(area.removeFromTop(kRowHeight));
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { rebuild(); }

    void rebuild()
    {
        toggles_.clear();
        for (const auto& input : juce::MidiInput::getAvailableDevices())
        {
            auto toggle = std::make_unique<juce::ToggleButton>(input.name);
            toggle->setToggleState(devices_.isMidiInputDeviceEnabled(input.identifier), juce::dontSendNotification);
            auto* raw = toggle.get();
            // The device manager's change message (which rebuilds this list)
            // arrives asynchronously, so the button outlives its own click.
            toggle->onClick = [this, raw, id = input.identifier]
            { devices_.setMidiInputDeviceEnabled(id, raw->getToggleState()); };
            addAndMakeVisible(*toggle);
            toggles_.push_back(std::move(toggle));
        }
        empty_.setVisible(toggles_.empty());
        setSize(440, juce::jmax(170, 100 + kRowHeight * static_cast<int>(toggles_.size())));
        resized();
    }

    juce::AudioDeviceManager& devices_;
    juce::Label header_;
    juce::Label hint_;
    juce::Label empty_;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiSettingsPanel)
};

// ---- OSC Settings -----------------------------------------------------------------

class OscSettingsPanel : public juce::Component
{
public:
    explicit OscSettingsPanel(AppContext& ctx) : ctx_(ctx)
    {
        styleCaption(header_, "OSC LISTENER");
        portLabel_.setText("Port", juce::dontSendNotification);
        groupLabel_.setText("Multicast group", juce::dontSendNotification);

        portEditor_.setInputRestrictions(5, "0123456789");
        portEditor_.setText(juce::String(ctx_.preferences.getOscPort()), juce::dontSendNotification);
        portEditor_.setTooltip("The UDP port OSC controllers send to (1-65535)");
        groupEditor_.setInputRestrictions(15, "0123456789.");
        groupEditor_.setText(ctx_.preferences.getOscMulticastAddress(), juce::dontSendNotification);
        groupEditor_.setTextToShowWhenEmpty("none (unicast)", tokens::fgMuted);
        groupEditor_.setTooltip("Optional IPv4 multicast group to join, 224.0.0.0 to 239.255.255.255");

        // Enter always applies (so a port that was busy can be retried); leaving
        // a field applies only a change, so clicking around doesn't restart the
        // listener.
        for (auto* editor : {&portEditor_, &groupEditor_})
        {
            editor->onReturnKey = [this] { apply(/*force*/ true); };
            editor->onFocusLost = [this] { apply(/*force*/ false); };
        }

        showStatus("Press Enter, or leave a field, to apply.", false);

        for (auto* c : std::initializer_list<juce::Component*>{
                 &header_, &portLabel_, &portEditor_, &groupLabel_, &groupEditor_, &statusLabel_})
            addAndMakeVisible(c);

        setSize(440, 200);
    }

    ~OscSettingsPanel() override
    {
        // A focused editor loses focus while the panel is torn down; don't let
        // that run apply() on a half-destroyed panel.
        portEditor_.onFocusLost = nullptr;
        groupEditor_.onFocusLost = nullptr;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kPad, 12);
        header_.setBounds(area.removeFromTop(22));
        area.removeFromTop(6);
        auto port = area.removeFromTop(kRowHeight);
        portLabel_.setBounds(port.removeFromLeft(kLabelWidth));
        portEditor_.setBounds(port.removeFromLeft(80));
        area.removeFromTop(8);
        auto group = area.removeFromTop(kRowHeight);
        groupLabel_.setBounds(group.removeFromLeft(kLabelWidth));
        groupEditor_.setBounds(group.removeFromLeft(160));
        area.removeFromTop(8);
        statusLabel_.setBounds(area.removeFromTop(40));
    }

private:
    void apply(bool force)
    {
        auto& prefs = ctx_.preferences;
        const int port = portEditor_.getText().getIntValue();
        const auto group = groupEditor_.getText().trim();

        if (port <= 0 || port > 65535)
        {
            // Back to the last-good saved value rather than binding with nonsense.
            portEditor_.setText(juce::String(prefs.getOscPort()), juce::dontSendNotification);
            showStatus("The port must be between 1 and 65535.", true);
            return;
        }
        if (group.isNotEmpty() && !control::isOscMulticastAddress(group))
        {
            showStatus("A multicast group is an IPv4 address from 224.0.0.0 to 239.255.255.255, or empty.", true);
            return;
        }
        if (!force && port == prefs.getOscPort() && group == prefs.getOscMulticastAddress())
            return;

        // rebind() drops the previous (working) binding before trying the new
        // one, so a failure leaves OSC off either way; save only on success, so
        // a rejected value never overwrites the last-good one. A failure is
        // shown here, not in a blocking dialog: the rest of the control layer
        // keeps working without OSC (App::initialise treats it the same way).
        const auto error = ctx_.oscServer.rebind(port, group);
        if (error.isNotEmpty())
        {
            showStatus(error, true);
            return;
        }
        prefs.setOscPort(port);
        prefs.setOscMulticastAddress(group);
        prefs.flush();
        showStatus("Listening on port " + juce::String(port) +
                       (group.isNotEmpty() ? ", group " + group : juce::String()) + ".",
                   false);
    }

    void showStatus(const juce::String& text, bool isError)
    {
        statusLabel_.setText(text, juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, isError ? tokens::accentDestructive : tokens::fgMuted);
        const auto textColour = isError ? tokens::accentDestructive : tokens::fgPrimary;
        portEditor_.applyColourToAllText(textColour);
        groupEditor_.applyColourToAllText(textColour);
    }

    AppContext& ctx_;
    juce::Label header_;
    juce::Label portLabel_;
    juce::TextEditor portEditor_;
    juce::Label groupLabel_;
    juce::TextEditor groupEditor_;
    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscSettingsPanel)
};

} // namespace

void showAudioDevicesWindow(AppContext& ctx, juce::Value& testTone)
{
    launch(new AudioDevicesPanel(ctx, testTone), "Audio Devices");
}

void showMidiSettingsWindow(AppContext& ctx)
{
    launch(new MidiSettingsPanel(ctx), "MIDI Settings");
}

void showOscSettingsWindow(AppContext& ctx)
{
    launch(new OscSettingsPanel(ctx), "OSC Settings");
}

} // namespace pedalboard3::ui
