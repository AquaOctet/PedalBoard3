#include "SettingsWindow.h"

#include "AppContext.h"
#include "ColourScheme.h"
#include "control/OscMapping.h" // control::isOscMulticastAddress
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "session/Preferences.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace pedalboard3::ui
{

namespace
{

constexpr int kWidth = 520;
constexpr int kDeviceHeight = 430;
constexpr int kLabelWidth = 130;
constexpr int kRowHeight = 28;

class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel(AppContext& ctx)
        : ctx_(ctx), device_(*ctx.audioEngine.getDeviceManager(),
                             0,
                             2,
                             0,
                             2,
                             /*showMidiInputOptions*/ true,
                             /*showMidiOutputSelector*/ false,
                             /*showChannelsAsStereoPairs*/ true,
                             /*hideAdvancedOptionsWithButton*/ false)
    {
        addAndMakeVisible(device_);

        oscHeader_.setText("OSC LISTENER", juce::dontSendNotification);
        oscHeader_.setFont(juce::Font(juce::FontOptions(12.0f)));
        oscHeader_.setColour(juce::Label::textColourId, tokens::fgMuted);
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
                 &oscHeader_, &portLabel_, &portEditor_, &groupLabel_, &groupEditor_, &statusLabel_})
            addAndMakeVisible(c);

        setSize(kWidth, 590);
    }

    ~SettingsPanel() override
    {
        // A focused editor loses focus while the panel is torn down; don't let
        // that run apply() on a half-destroyed panel.
        portEditor_.onFocusLost = nullptr;
        groupEditor_.onFocusLost = nullptr;
    }

    void resized() override
    {
        auto area = getLocalBounds();
        device_.setBounds(area.removeFromTop(kDeviceHeight));

        auto osc = area.reduced(16, 8);
        oscHeader_.setBounds(osc.removeFromTop(22));
        osc.removeFromTop(6);
        auto port = osc.removeFromTop(kRowHeight);
        portLabel_.setBounds(port.removeFromLeft(kLabelWidth));
        portEditor_.setBounds(port.removeFromLeft(80));
        osc.removeFromTop(8);
        auto group = osc.removeFromTop(kRowHeight);
        groupLabel_.setBounds(group.removeFromLeft(kLabelWidth));
        groupEditor_.setBounds(group.removeFromLeft(160));
        osc.removeFromTop(8);
        statusLabel_.setBounds(osc.removeFromTop(40));
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
    juce::AudioDeviceSelectorComponent device_;
    juce::Label oscHeader_;
    juce::Label portLabel_;
    juce::TextEditor portEditor_;
    juce::Label groupLabel_;
    juce::TextEditor groupEditor_;
    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};

} // namespace

void showSettingsWindow(AppContext& ctx)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new SettingsPanel(ctx));
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = tokens::bgPopover;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace pedalboard3::ui
