#include "SettingsWindow.h"

#include "AppContext.h"
#include "AudioSettingsPanel.h"
#include "ColourScheme.h"
#include "control/OscMapping.h" // control::isOscMulticastAddress
#include "control/OscServer.h"
#include "DialogFrame.h"
#include "DialogStyle.h"
#include "engine/AudioEngine.h"
#include "Fonts.h"
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
constexpr int kLabelWidth = 130;
constexpr int kRowHeight = 28;

// ---- MIDI Settings --------------------------------------------------------------

// "Enable MIDI" sits on top (optional-features U2, KD6 / KD7): off stops all
// MIDI input. The devices below are what's read while it's on. MMC moved to
// Tempo / BPM Settings.
class MidiSettingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit MidiSettingsPanel(AppContext& ctx) : devices_(*ctx.audioEngine.getDeviceManager()), ctx_(ctx)
    {
        enable_.setToggleState(ctx_.features.isEnabled(session::Feature::Midi), juce::dontSendNotification);
        enable_.setTooltip("Off stops all MIDI input: mappings, Program Change patch switching, MMC and MIDI learn");
        enable_.onClick = [this]
        {
            ctx_.features.setEnabled(session::Feature::Midi, enable_.getToggleState());
            refreshEnabled();
        };
        dialogstyle::caption(header_, "MIDI INPUTS");
        dialogstyle::hint(empty_, "No MIDI inputs found. Connect a controller; it appears here.");
        for (auto* c : std::initializer_list<juce::Component*>{&enable_, &header_, &hint_, &empty_})
            addAndMakeVisible(c);

        devices_.addChangeListener(this); // inputs come and go
        rebuild();
    }

    ~MidiSettingsPanel() override { devices_.removeChangeListener(this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kPad, 12);
        enable_.setBounds(area.removeFromTop(kRowHeight).removeFromLeft(220));
        area.removeFromTop(10);
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
        setSize(440, 142 + kRowHeight * juce::jmax(1, static_cast<int>(toggles_.size())));
        refreshEnabled();
        resized();
    }

    void refreshEnabled()
    {
        const bool on = enable_.getToggleState();
        dialogstyle::hint(hint_,
                          on ? "Enabled inputs drive mapped controls and Program Change patch switching."
                             : "MIDI is off: no MIDI input is read.");
        for (auto& toggle : toggles_)
            toggle->setEnabled(on);
    }

    juce::AudioDeviceManager& devices_;
    AppContext& ctx_;
    juce::ToggleButton enable_{"Enable MIDI"};
    juce::Label header_;
    juce::Label hint_;
    juce::Label empty_;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiSettingsPanel)
};

// ---- OSC Settings -----------------------------------------------------------------

// "Enable OSC" sits on top (optional-features U2, KD6 / KD7). Off, which is the
// default, no port is open. The port and group apply straight away while OSC
// is on for the board; otherwise they're saved for when it is.
class OscSettingsPanel : public juce::Component, private juce::Timer
{
public:
    explicit OscSettingsPanel(AppContext& ctx) : ctx_(ctx)
    {
        enable_.setToggleState(ctx_.features.isEnabled(session::Feature::Osc), juce::dontSendNotification);
        enable_.setTooltip("Listen for OSC on the port below. Off, no network port is open.");
        enable_.onClick = [this]
        {
            ctx_.features.setEnabled(session::Feature::Osc, enable_.getToggleState());
            error_.clear();
            refreshEnabled();
        };

        dialogstyle::caption(header_, "OSC LISTENER");
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

        for (auto* c : std::initializer_list<juce::Component*>{
                 &enable_, &header_, &portLabel_, &portEditor_, &groupLabel_, &groupEditor_, &statusLabel_})
            addAndMakeVisible(c);

        setSize(440, 238);
        refreshEnabled();
        startTimerHz(4); // the listener binds asynchronously, after the switch
    }

    ~OscSettingsPanel() override
    {
        stopTimer();
        // A focused editor loses focus while the panel is torn down; don't let
        // that run apply() on a half-destroyed panel.
        portEditor_.onFocusLost = nullptr;
        groupEditor_.onFocusLost = nullptr;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kPad, 12);
        enable_.setBounds(area.removeFromTop(kRowHeight).removeFromLeft(220));
        area.removeFromTop(10);
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
            error_ = "The port must be between 1 and 65535.";
            timerCallback();
            return;
        }
        if (group.isNotEmpty() && !control::isOscMulticastAddress(group))
        {
            error_ = "A multicast group is an IPv4 address from 224.0.0.0 to 239.255.255.255, or empty.";
            timerCallback();
            return;
        }
        if (!force && port == prefs.getOscPort() && group == prefs.getOscMulticastAddress())
            return;

        if (!ctx_.features.isAvailable(session::Feature::Osc))
        {
            // Nothing is listening: just keep the values for when OSC is on.
            prefs.setOscPort(port);
            prefs.setOscMulticastAddress(group);
            prefs.flush();
            error_.clear();
            timerCallback();
            return;
        }

        // rebind() drops the previous (working) binding before trying the new
        // one, so a failure leaves OSC off either way; save only on success, so
        // a rejected value never overwrites the last-good one. A failure is
        // shown here, not in a blocking dialog: the rest of the control layer
        // keeps working without OSC.
        if (const auto error = ctx_.oscServer.rebind(port, group); error.isNotEmpty())
        {
            error_ = error;
            timerCallback();
            return;
        }
        prefs.setOscPort(port);
        prefs.setOscMulticastAddress(group);
        prefs.flush();
        error_.clear();
        timerCallback();
    }

    void refreshEnabled()
    {
        const bool on = enable_.getToggleState();
        portEditor_.setEnabled(on);
        groupEditor_.setEnabled(on);
        timerCallback();
    }

    void timerCallback() override
    {
        auto& features = ctx_.features;
        if (enable_.getToggleState() != features.isEnabled(session::Feature::Osc))
        {
            enable_.setToggleState(features.isEnabled(session::Feature::Osc), juce::dontSendNotification);
            refreshEnabled();
            return;
        }
        // The panel's own error is from a port you just typed. With none, fall
        // back to the server's, which is how a bind that failed at startup or
        // when OSC was switched on finally reaches a human (#96) — before
        // this, the dialog said "Not listening", which is the symptom rather
        // than the reason.
        const auto& standingError = ctx_.oscServer.lastError();
        juce::String text;
        bool isError = false;
        if (error_.isNotEmpty())
        {
            text = error_;
            isError = true;
        }
        else if (!features.isEnabled(session::Feature::Osc))
            text = "OSC is off: no network port is open.";
        else if (!features.isAvailable(session::Feature::Osc))
            text = "On in Settings, but off for this board.";
        else if (ctx_.oscServer.isConnected())
            text = "Listening on port " + juce::String(ctx_.preferences.getOscPort()) +
                   (ctx_.preferences.getOscMulticastAddress().isNotEmpty()
                        ? ", group " + ctx_.preferences.getOscMulticastAddress()
                        : juce::String()) +
                   ".";
        else if (standingError.isNotEmpty())
        {
            text = standingError + ". Press Enter in a field to try again.";
            isError = true;
        }
        else
            text = "Not listening. Press Enter in a field to try again.";
        showStatus(text, isError);
    }

    void showStatus(const juce::String& text, bool isError)
    {
        if (statusLabel_.getText() == text && shownError_ == isError)
            return;
        shownError_ = isError;
        statusLabel_.setText(text, juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, isError ? tokens::accentDestructive : tokens::fgMuted);
        const auto textColour = isError ? tokens::accentDestructive : tokens::fgPrimary;
        portEditor_.applyColourToAllText(textColour);
        groupEditor_.applyColourToAllText(textColour);
    }

    AppContext& ctx_;
    juce::ToggleButton enable_{"Enable OSC"};
    juce::Label header_;
    juce::Label portLabel_;
    juce::TextEditor portEditor_;
    juce::Label groupLabel_;
    juce::TextEditor groupEditor_;
    juce::Label statusLabel_;
    juce::String error_;
    bool shownError_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscSettingsPanel)
};

} // namespace

std::unique_ptr<juce::Component> makeAudioDevicesPanel(AppContext& ctx, juce::Value& testTone)
{
    // Our own panel on the device manager (UI chrome U6), not JUCE's selector.
    return std::make_unique<AudioSettingsPanel>(*ctx.audioEngine.getDeviceManager(), testTone);
}

std::unique_ptr<juce::Component> makeMidiSettingsPanel(AppContext& ctx)
{
    return std::make_unique<MidiSettingsPanel>(ctx);
}

std::unique_ptr<juce::Component> makeOscSettingsPanel(AppContext& ctx)
{
    return std::make_unique<OscSettingsPanel>(ctx);
}

void showAudioDevicesWindow(AppContext& ctx, juce::Value& testTone)
{
    dialogstyle::launch(makeAudioDevicesPanel(ctx, testTone).release(), "Audio Devices");
}

void showMidiSettingsWindow(AppContext& ctx)
{
    dialogstyle::launch(makeMidiSettingsPanel(ctx).release(), "MIDI Settings");
}

void showOscSettingsWindow(AppContext& ctx)
{
    dialogstyle::launch(makeOscSettingsPanel(ctx).release(), "OSC Settings");
}

} // namespace pedalboard3::ui
