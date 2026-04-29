#include "StatusBar.h"

#include "ColourScheme.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kBarPadding = 12;
constexpr int kDotSize = 8;
constexpr int kDotTextGap = 8;
} // namespace

StatusBar::StatusBar(AppContext& ctx)
    : ctx_(ctx), stateDotColour_(tokens::statusIdle)
{
    setOpaque(true);

    leftLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    leftLabel_.setFont(juce::Font(12.0f));
    leftLabel_.setJustificationType(juce::Justification::centredLeft);
    leftLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(leftLabel_);

    errorLabel_.setColour(juce::Label::textColourId, tokens::accentDestructive);
    errorLabel_.setColour(juce::Label::backgroundColourId, tokens::accentDestructive.withAlpha(0.12f));
    errorLabel_.setFont(juce::Font(12.0f));
    errorLabel_.setJustificationType(juce::Justification::centred);
    errorLabel_.setInterceptsMouseClicks(false, false);
    errorLabel_.setVisible(false);
    addAndMakeVisible(errorLabel_);

    rightLabel_.setText(juce::String("v") + JUCE_APPLICATION_VERSION_STRING + "-pre",
                        juce::dontSendNotification);
    rightLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    rightLabel_.setFont(juce::Font(11.0f));
    rightLabel_.setJustificationType(juce::Justification::centredRight);
    rightLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(rightLabel_);

    ctx_.session.addChangeListener(this);
    refresh();
}

StatusBar::~StatusBar()
{
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
    g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                  static_cast<float>(kDotSize), static_cast<float>(kDotSize));
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

    // Whatever is left of `bounds` is the error pill area.
    errorLabel_.setBounds(bounds.reduced(kBarPadding, 2));
}

void StatusBar::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void StatusBar::refresh()
{
    const auto snaps = ctx_.session.getNodeSnapshots();
    const auto state = ctx_.session.getState();

    juce::String stateText;
    switch (state)
    {
    case session::Session::State::Building:
        stateText = "Loading…";
        stateDotColour_ = tokens::statusBuilding;
        break;
    case session::Session::State::Swapping:
        stateText = "Swapping…";
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

    repaint();
}

} // namespace pedalboard3::ui
