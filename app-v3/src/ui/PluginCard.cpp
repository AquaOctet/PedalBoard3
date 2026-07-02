#include "PluginCard.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace pedalboard3::ui
{

namespace
{
constexpr float kCardCornerRadius = 6.0f;
constexpr float kCardBorderThickness = 1.0f;
constexpr float kAccentBorderWidth = 4.0f;
constexpr int kAccentInset = 4; // integer form of kAccentBorderWidth for pixel layout

// Layout constants (the card is a fixed 170 × 140 from SignalCanvas's
// layoutCards; rows here add up to that). Defined privately rather
// than at file scope so they're only addressable from PluginCard
// (architecture-review preference; matches Session::kAddQueueMax).
constexpr int kHeaderHeight = 28;
constexpr int kPortRowHeight = 22;
constexpr int kMidiRowHeight = 18;

constexpr int kPortRadius = 6;
constexpr int kPortStripeInset = 12; // distance from L/R edge to port centre

constexpr int kPaddingX = 8;
} // namespace

PluginCard::PluginCard(NodeSnapshot initialSnapshot, Callbacks callbacks)
    : snapshot_(std::move(initialSnapshot)), callbacks_(std::move(callbacks))
{
    nameLabel_.setText(snapshot_.name, juce::dontSendNotification);
    nameLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    nameLabel_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
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

    // Outer border — brightens to the accent on hover for a "liftable" feel.
    g.setColour(hovered_ ? accent_.withAlpha(0.8f) : tokens::borderDefault);
    g.drawRoundedRectangle(bounds, kCardCornerRadius, hovered_ ? 1.5f : kCardBorderThickness);

    // Header divider line below the title row.
    const auto y1 = static_cast<float>(kHeaderHeight);
    g.setColour(tokens::borderMuted);
    g.drawHorizontalLine(static_cast<int>(y1), bounds.getX() + kAccentBorderWidth, bounds.getRight());

    // Port circles + labels (Path γ is read-only — drawn but no
    // mouseDown handler; Phase 3 wires interactivity).
    const auto portFontHeight = 10.0f;
    g.setFont(juce::Font(juce::FontOptions(portFontHeight)));
    g.setColour(tokens::fgMuted);

    const int portsTopY = kHeaderHeight + 4;
    const int portRowL = portsTopY + kPortRowHeight / 2;
    const int portRowR = portsTopY + kPortRowHeight + kPortRowHeight / 2;

    // Solid coloured dots so it's obvious where to grab a cable (a dark ring
    // separates the dot from the card body).
    auto drawPort = [&](int cx, int cy, juce::Colour c)
    {
        auto box = juce::Rectangle<float>(0.0f, 0.0f, kPortRadius * 2.0f, kPortRadius * 2.0f)
                       .withCentre({static_cast<float>(cx), static_cast<float>(cy)});
        g.setColour(c);
        g.fillEllipse(box);
        g.setColour(tokens::bgWindow);
        g.drawEllipse(box, 1.5f);
    };

    const int leftPortX = static_cast<int>(bounds.getX()) + kAccentInset + kPortStripeInset;
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

    // MIDI row: in/out ports at the card edges — the same anchors
    // getPortCentre reports, so the visible dot is exactly where a cable
    // terminates and where the canvas's grab hotspot lives. (The old single
    // mid-card dot sat ~30px away from the interaction point.)
    const int midiY = kHeaderHeight + 2 * kPortRowHeight + 4;
    g.setColour(tokens::borderMuted);
    g.drawHorizontalLine(midiY, bounds.getX() + kAccentBorderWidth, bounds.getRight());
    g.setColour(tokens::fgMuted);
    g.drawText("MIDI",
               static_cast<int>(bounds.getX()) + kAccentInset + kPaddingX + kPortRadius * 2,
               midiY + 2,
               40,
               kMidiRowHeight - 4,
               juce::Justification::centredLeft);
    const int midiPortY = midiY + kMidiRowHeight / 2;
    drawPort(leftPortX, midiPortY, tokens::accentInfo);
    drawPort(rightPortX, midiPortY, tokens::accentInfo);
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

    // Port rows + MIDI row are paint-only; no further child layout (the old
    // Move ↑/↓ row was removed — dragging the card body repositions it).
}

bool PluginCard::hitTest(int x, int y)
{
    // Port hotspots are owned by SignalCanvas (drag-to-connect): report them as
    // "not on the card" so the click falls through to the canvas behind. The
    // card still paints the ports; only the interaction is delegated.
    // Generous so a port grab reliably wins over a card-body drag.
    constexpr int kPortHotspot = 16;
    const juce::Point<int> p{x, y};
    // midiChannelIndex is an unnamed enum, so cast it to match the int literals
    // in the braced-init list (otherwise the list's element type can't deduce).
    for (const bool isInput : {true, false})
        for (const int ch : {0, 1, static_cast<int>(juce::AudioProcessorGraph::midiChannelIndex)})
            if (p.getDistanceFrom(getPortCentre(isInput, ch)) <= kPortHotspot)
                return false;
    return true;
}

// ----- Drag-to-move ----------------------------------------------------------
// The card body is draggable; the child TextButtons consume their own clicks
// and nameLabel_ has intercepts disabled, so a drag that starts on the body
// (or a port) repositions the card. SignalCanvas persists the new position on
// release via Session::setNodePosition.

void PluginCard::mouseDown(const juce::MouseEvent& e)
{
    didDrag_ = false;
    dragger_.startDraggingComponent(this, e);
}

void PluginCard::mouseDrag(const juce::MouseEvent& e)
{
    // Only treat it as a drag once the pointer has actually moved, so a plain
    // click on the body doesn't nudge the card.
    if (e.getDistanceFromDragStart() > 3)
    {
        didDrag_ = true;
        dragger_.dragComponent(this, e, nullptr);
        if (callbacks_.onDragging)
            callbacks_.onDragging(snapshot_.id); // canvas re-strokes cables live
    }
}

void PluginCard::mouseUp(const juce::MouseEvent&)
{
    if (didDrag_ && callbacks_.onMoved)
        callbacks_.onMoved(snapshot_.id, getPosition());
    didDrag_ = false;
}

void PluginCard::mouseEnter(const juce::MouseEvent&)
{
    hovered_ = true;
    repaint();
}

void PluginCard::mouseExit(const juce::MouseEvent&)
{
    hovered_ = false;
    repaint();
}

} // namespace pedalboard3::ui
