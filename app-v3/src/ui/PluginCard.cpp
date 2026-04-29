#include "PluginCard.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace pedalboard3::ui
{

namespace
{
constexpr float kCardCornerRadius = 6.0f;
constexpr float kCardBorderThickness = 1.0f;
constexpr float kAccentBorderWidth = 4.0f;

// Layout constants (the card is a fixed 170 × 140 from SignalCanvas's
// layoutCards; rows here add up to that). Defined privately rather
// than at file scope so they're only addressable from PluginCard
// (architecture-review preference; matches Session::kAddQueueMax).
constexpr int kHeaderHeight = 28;
constexpr int kPortRowHeight = 22;
constexpr int kMidiRowHeight = 18;
constexpr int kMoveRowHeight = 28;

constexpr int kPortRadius = 5;
constexpr int kPortStripeInset = 12; // distance from L/R edge to port centre

constexpr int kPaddingX = 8;
} // namespace

PluginCard::PluginCard(NodeSnapshot initialSnapshot, Callbacks callbacks)
    : snapshot_(std::move(initialSnapshot)), callbacks_(std::move(callbacks))
{
    nameLabel_.setText(snapshot_.name, juce::dontSendNotification);
    nameLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    nameLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    nameLabel_.setMinimumHorizontalScale(0.7f);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    auto wireSmallButton = [this](juce::TextButton& b)
    {
        b.setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
        b.setColour(juce::TextButton::textColourOffId, tokens::fgPrimary);
        addAndMakeVisible(b);
    };
    wireSmallButton(bypassButton_);
    wireSmallButton(editButton_);
    wireSmallButton(removeButton_);
    wireSmallButton(moveUpButton_);
    wireSmallButton(moveDownButton_);

    removeButton_.setColour(juce::TextButton::textColourOffId, tokens::accentDestructive);

    bypassButton_.onClick = [this]
    {
        if (callbacks_.onBypassToggle)
            callbacks_.onBypassToggle(snapshot_.id);
    };
    editButton_.onClick = [this]
    {
        if (callbacks_.onEdit)
            callbacks_.onEdit(snapshot_.id);
    };
    removeButton_.onClick = [this]
    {
        if (callbacks_.onRemove)
            callbacks_.onRemove(snapshot_.id);
    };
    moveUpButton_.onClick = [this]
    {
        if (callbacks_.onMoveUp)
            callbacks_.onMoveUp(snapshot_.id);
    };
    moveDownButton_.onClick = [this]
    {
        if (callbacks_.onMoveDown)
            callbacks_.onMoveDown(snapshot_.id);
    };

    refreshFromSnapshot();
}

void PluginCard::setSnapshot(const NodeSnapshot& s)
{
    const bool nameChanged = (s.name != snapshot_.name) || (s.customName != snapshot_.customName);
    const bool bypassChanged = (s.bypassed != snapshot_.bypassed);
    snapshot_ = s;

    if (nameChanged)
        nameLabel_.setText(snapshot_.customName.isNotEmpty() ? snapshot_.customName : snapshot_.name,
                           juce::dontSendNotification);
    if (bypassChanged)
        refreshFromSnapshot();

    if (nameChanged || bypassChanged)
        repaint();
}

void PluginCard::setAccentColour(juce::Colour c)
{
    if (c == accent_)
        return;
    accent_ = c;
    repaint();
}

void PluginCard::setMovableUp(bool ok)
{
    moveUpButton_.setEnabled(ok);
}

void PluginCard::setMovableDown(bool ok)
{
    moveDownButton_.setEnabled(ok);
}

void PluginCard::refreshFromSnapshot()
{
    // Bypass button visually flips: when bypassed, button is dimmed
    // (foreground muted) so a glance reveals "this plugin is OFF".
    bypassButton_.setColour(juce::TextButton::textColourOffId,
                            snapshot_.bypassed ? tokens::fgMuted : tokens::accentPrimary);
}

juce::Point<int> PluginCard::getPortCentre(bool isInput, int channel) const noexcept
{
    const int leftPortX = static_cast<int>(kAccentBorderWidth) + kPortStripeInset;
    const int rightPortX = getWidth() - kPortStripeInset;
    const int x = isInput ? leftPortX : rightPortX;

    const int portsTopY = kHeaderHeight + 4;
    const int rowL = portsTopY + kPortRowHeight / 2;
    const int rowR = portsTopY + kPortRowHeight + kPortRowHeight / 2;
    const int midiY = kHeaderHeight + 2 * kPortRowHeight + 4 + kMidiRowHeight / 2;

    if (channel == juce::AudioProcessorGraph::midiChannelIndex)
        return {x, midiY};
    return {x, channel == 0 ? rowL : rowR};
}

void PluginCard::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    // Card body fill — slightly dimmed when bypassed so the whole
    // tile reads as "off" at a glance (matches mockup `opacity-50`).
    auto bodyFill = tokens::bgCard;
    if (snapshot_.bypassed)
        bodyFill = bodyFill.withMultipliedAlpha(0.65f);
    g.setColour(bodyFill);
    g.fillRoundedRectangle(bounds, kCardCornerRadius);

    // Accent stripe on the left edge by plugin type (teal = effect,
    // amber = instrument; canvas decides via setAccentColour).
    g.setColour(accent_);
    juce::Path accentStripe;
    accentStripe.addRoundedRectangle(bounds.getX(),
                                     bounds.getY(),
                                     kAccentBorderWidth,
                                     bounds.getHeight(),
                                     kCardCornerRadius,
                                     kCardCornerRadius,
                                     true,
                                     false,
                                     true,
                                     false);
    g.fillPath(accentStripe);

    // Outer border.
    g.setColour(tokens::borderDefault);
    g.drawRoundedRectangle(bounds, kCardCornerRadius, kCardBorderThickness);

    // Header divider line below the title row.
    const auto y1 = static_cast<float>(kHeaderHeight);
    g.setColour(tokens::borderMuted);
    g.drawHorizontalLine(static_cast<int>(y1), bounds.getX() + kAccentBorderWidth, bounds.getRight());

    // Port circles + labels (Path γ is read-only — drawn but no
    // mouseDown handler; Phase 3 wires interactivity).
    const auto portFontHeight = 10.0f;
    g.setFont(juce::Font(portFontHeight));
    g.setColour(tokens::fgMuted);

    const int portsTopY = kHeaderHeight + 4;
    const int portRowL = portsTopY + kPortRowHeight / 2;
    const int portRowR = portsTopY + kPortRowHeight + kPortRowHeight / 2;

    auto drawPort = [&](int cx, int cy, juce::Colour c)
    {
        auto box = juce::Rectangle<float>(0.0f, 0.0f, kPortRadius * 2.0f, kPortRadius * 2.0f)
                       .withCentre({static_cast<float>(cx), static_cast<float>(cy)});
        g.setColour(tokens::bgWindow);
        g.fillEllipse(box);
        g.setColour(c);
        g.drawEllipse(box, 1.5f);
    };

    const int leftPortX = static_cast<int>(bounds.getX()) + kAccentBorderWidth + kPortStripeInset;
    const int rightPortX = static_cast<int>(bounds.getRight()) - kPortStripeInset;

    // Audio L (input + output) — input port colour = primary teal,
    // output port colour = chart-3 amber per the mockup.
    drawPort(leftPortX, portRowL, tokens::accentPrimary);
    drawPort(rightPortX, portRowL, tokens::accentWarning);
    drawPort(leftPortX, portRowR, tokens::accentPrimary);
    drawPort(rightPortX, portRowR, tokens::accentWarning);

    g.setColour(tokens::fgMuted);
    g.drawText("In L", leftPortX + kPortRadius + 4, portRowL - 7, 30, 14, juce::Justification::centredLeft);
    g.drawText("Out L", rightPortX - kPortRadius - 34, portRowL - 7, 30, 14, juce::Justification::centredRight);
    g.drawText("In R", leftPortX + kPortRadius + 4, portRowR - 7, 30, 14, juce::Justification::centredLeft);
    g.drawText("Out R", rightPortX - kPortRadius - 34, portRowR - 7, 30, 14, juce::Justification::centredRight);

    // MIDI activity strip — placeholder (no router yet).
    const int midiY = kHeaderHeight + 2 * kPortRowHeight + 4;
    g.setColour(tokens::borderMuted);
    g.drawHorizontalLine(midiY, bounds.getX() + kAccentBorderWidth, bounds.getRight());
    g.setColour(tokens::fgMuted);
    g.drawText("MIDI",
               static_cast<int>(bounds.getX()) + kAccentBorderWidth + kPaddingX,
               midiY + 2,
               40,
               kMidiRowHeight - 4,
               juce::Justification::centredLeft);
    drawPort(static_cast<int>(bounds.getX()) + kAccentBorderWidth + kPaddingX + 36,
             midiY + kMidiRowHeight / 2,
             tokens::accentInfo);

    // Bottom divider.
    const int moveRowTop = midiY + kMidiRowHeight;
    g.setColour(tokens::borderMuted);
    g.drawHorizontalLine(moveRowTop, bounds.getX() + kAccentBorderWidth, bounds.getRight());
}

void PluginCard::resized()
{
    auto bounds = getLocalBounds().reduced(0).withTrimmedLeft(static_cast<int>(kAccentBorderWidth));
    auto header = bounds.removeFromTop(kHeaderHeight).reduced(kPaddingX, 4);

    // Title fills left, then 3 buttons on the right (Bypass | Edit | Remove).
    auto buttonStrip = header.removeFromRight(20 * 3 + 6);
    nameLabel_.setBounds(header.withTrimmedRight(4));

    const int btnW = 20;
    const int btnGap = 3;
    bypassButton_.setBounds(buttonStrip.removeFromLeft(btnW));
    buttonStrip.removeFromLeft(btnGap);
    editButton_.setBounds(buttonStrip.removeFromLeft(btnW + 8)); // "Edit" wider
    buttonStrip.removeFromLeft(btnGap);
    removeButton_.setBounds(buttonStrip.removeFromLeft(btnW));

    // Port rows + MIDI row are paint-only; skip vertical space.
    bounds.removeFromTop(2 * kPortRowHeight + kMidiRowHeight + 4);

    // Move row — two buttons centred.
    auto moveRow = bounds.removeFromTop(kMoveRowHeight).reduced(kPaddingX, 4);
    const int moveBtnW = 28;
    const int spacing = 8;
    auto moveLeft = moveRow.removeFromLeft((moveRow.getWidth() - 2 * moveBtnW - spacing) / 2 + moveBtnW);
    moveUpButton_.setBounds(moveLeft.removeFromRight(moveBtnW));
    moveRow.removeFromLeft(spacing);
    moveDownButton_.setBounds(moveRow.removeFromLeft(moveBtnW));
}

} // namespace pedalboard3::ui
