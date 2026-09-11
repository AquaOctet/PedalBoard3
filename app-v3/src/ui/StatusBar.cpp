#include "StatusBar.h"

#include "ColourScheme.h"
#include "engine/AudioEngine.h"
#include "session/Session.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace pedalboard3::ui
{

namespace
{
constexpr int kBarPadding = 12;
constexpr int kDotSize = 8;
constexpr int kDotTextGap = 8;
} // namespace

StatusBar::StatusBar(AppContext& ctx) : ctx_(ctx), stateDotColour_(tokens::statusIdle)
{
    setOpaque(true);

    leftLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    leftLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    leftLabel_.setJustificationType(juce::Justification::centredLeft);
    leftLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(leftLabel_);

    errorLabel_.setColour(juce::Label::textColourId, tokens::accentDestructive);
    errorLabel_.setColour(juce::Label::backgroundColourId, tokens::accentDestructive.withAlpha(0.12f));
    errorLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    errorLabel_.setJustificationType(juce::Justification::centred);
    errorLabel_.setInterceptsMouseClicks(false, false);
    errorLabel_.setVisible(false);
    addAndMakeVisible(errorLabel_);

    deviceLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    deviceLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    deviceLabel_.setJustificationType(juce::Justification::centredRight);
    deviceLabel_.setMinimumHorizontalScale(0.85f);
    deviceLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(deviceLabel_);

    rightLabel_.setText(juce::String("v") + JUCE_APPLICATION_VERSION_STRING + "-pre", juce::dontSendNotification);
    rightLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    rightLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    rightLabel_.setJustificationType(juce::Justification::centredRight);
    rightLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(rightLabel_);

    ctx_.session.addChangeListener(this);
    if (auto* dm = ctx_.audioEngine.getDeviceManager())
        dm->addChangeListener(this);
    refreshDeviceInfo();
    refresh();
}

StatusBar::~StatusBar()
{
    if (auto* dm = ctx_.audioEngine.getDeviceManager())
        dm->removeChangeListener(this);
    ctx_.session.removeChangeListener(this);
}

void StatusBar::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));

    // Status dot at the left edge — coloured per Session::State.
    const int dotX = kBarPadding;
    const int dotY = (getHeight() - kDotSize) / 2;
    g.setColour(stateDotColour_);
    g.fillEllipse(
        static_cast<float>(dotX), static_cast<float>(dotY), static_cast<float>(kDotSize), static_cast<float>(kDotSize));
}

void StatusBar::resized()
{
    auto bounds = getLocalBounds().reduced(kBarPadding, 4);
    bounds.removeFromLeft(kDotSize + kDotTextGap); // space for the painted dot

    auto right = bounds.removeFromRight(140);
    rightLabel_.setBounds(right);
    bounds.removeFromRight(kBarPadding);

    auto left = bounds.removeFromLeft(280);
    leftLabel_.setBounds(left);

    // Middle: the device readout sits right-aligned; an error pill, when
    // shown, takes the left half so both stay readable.
    if (errorLabel_.isVisible())
    {
        deviceLabel_.setBounds(bounds.removeFromRight(bounds.getWidth() / 2));
        errorLabel_.setBounds(bounds.reduced(kBarPadding, 2));
    }
    else
    {
        deviceLabel_.setBounds(bounds);
    }
}

void StatusBar::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &ctx_.session)
        refresh();
    else
        refreshDeviceInfo(); // the AudioDeviceManager
}

void StatusBar::refreshDeviceInfo()
{
    auto* dm = ctx_.audioEngine.getDeviceManager();
    auto* dev = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;

    if (dev == nullptr)
    {
        // Surface the actual reason audio is silent (e.g. a driver that
        // failed to open) rather than a generic placeholder.
        const auto& err = ctx_.audioEngine.getLastInitError();
        deviceLabel_.setColour(juce::Label::textColourId, tokens::accentWarning);
        deviceLabel_.setText(dm == nullptr      ? juce::String("No audio device")
                             : err.isNotEmpty() ? "Audio device error: " + err + " — open Settings > Audio Devices"
                                                : juce::String("No audio device — open Settings > Audio Devices"),
                             juce::dontSendNotification);
        return;
    }

    deviceLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    deviceLabel_.setText(dev->getName() + "  •  " + juce::String(static_cast<int>(dev->getCurrentSampleRate())) +
                             " Hz  /  " + juce::String(dev->getCurrentBufferSizeSamples()) + " samples",
                         juce::dontSendNotification);
}

void StatusBar::refresh()
{
    const auto snaps = ctx_.session.getNodeSnapshots();
    const auto state = ctx_.session.getState();

    juce::String stateText;
    switch (state)
    {
    case session::Session::State::Building:
        stateText = "Loading...";
        stateDotColour_ = tokens::statusBuilding;
        break;
    case session::Session::State::Swapping:
        stateText = "Swapping...";
        stateDotColour_ = tokens::statusSwapping;
        break;
    case session::Session::State::Idle:
    default:
        stateText = "Idle";
        stateDotColour_ = tokens::statusIdle;
        break;
    }

    juce::String chainText = "Chain: " + juce::String(static_cast<int>(snaps.size())) + " plugin";
    if (snaps.size() != 1)
        chainText += "s";
    leftLabel_.setText(chainText + "  •  " + stateText, juce::dontSendNotification);

    // Error pill — shown when state is Idle and lastError is non-empty.
    // (During Building/Swapping the previous error from a prior run
    // stays hidden so the user isn't shown stale text mid-op.)
    const auto err = ctx_.session.getLastError();
    if (state == session::Session::State::Idle && err.isNotEmpty())
    {
        errorLabel_.setText("⚠  " + err, juce::dontSendNotification);
        errorLabel_.setVisible(true);
    }
    else
    {
        errorLabel_.setVisible(false);
    }

    resized(); // the middle split depends on whether the error pill is showing
    repaint();
}

} // namespace pedalboard3::ui
