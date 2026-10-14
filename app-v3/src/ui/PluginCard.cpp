#include "PluginCard.h"

#include "CardText.h"
#include "ProcessorControls.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

namespace pedalboard3::ui
{

namespace
{
constexpr float kRadius = 6.0f;
constexpr float kAccentWidth = 4.0f;
constexpr int kAccentInset = 4; // integer form of kAccentWidth for layout
constexpr int kHeaderHeight = 32;
constexpr int kStripHeight = 3;
constexpr int kGripWidth = 14;
constexpr int kButtonGap = 3;

// Ports sit at the card's edges, clear of the body text.
constexpr int kPortRadius = 5;
constexpr int kInPortX = 12;
constexpr int kOutPortInset = 10;
constexpr int kPortYL = 46;
constexpr int kPortYR = 60;
constexpr int kPortYMidi = 76;

constexpr int kTextLeft = 24;
constexpr int kTextRightInset = 20;

const juce::String kVuMeterType{"VuMeterProcessor"};

juce::Font nameFont()
{
    return juce::Font(juce::FontOptions(14.0f, juce::Font::bold));
}

juce::Font monoFont(float height)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}
} // namespace

PluginCard::PluginCard(NodeSnapshot initialSnapshot, Callbacks callbacks)
    : snapshot_(std::move(initialSnapshot)), callbacks_(std::move(callbacks))
{
    for (auto* button : {&bypassButton_, &mappingsButton_, &editButton_, &removeButton_})
    {
        button->setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
        button->setColour(juce::TextButton::textColourOffId, tokens::fgPrimary);
        addAndMakeVisible(button);
    }
    removeButton_.setColour(juce::TextButton::textColourOffId, tokens::accentDestructive);
    removeButton_.setVisible(false); // shown on hover

    bypassButton_.setTooltip("Bypass");
    mappingsButton_.setTooltip("MIDI / OSC mappings");
    editButton_.setTooltip("Open the plugin's editor");
    removeButton_.setTooltip("Remove the plugin (Ctrl+Z brings it back)");

    bypassButton_.onClick = [this]
    {
        if (callbacks_.onBypassToggle)
            callbacks_.onBypassToggle(snapshot_.id);
    };
    mappingsButton_.onClick = [this]
    {
        if (callbacks_.onOpenMappings)
            callbacks_.onOpenMappings(snapshot_.id);
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

    addMouseListener(&hoverWatcher_, /*wantsEventsForAllNestedChildComponents*/ true);
    refreshButtons();
}

PluginCard::~PluginCard()
{
    removeMouseListener(&hoverWatcher_);
    // A focused rename editor loses focus as the card goes; don't commit from a
    // half-destroyed card.
    if (renameEditor_ != nullptr)
        renameEditor_->onFocusLost = nullptr;
}

juce::String PluginCard::displayName() const
{
    return snapshot_.customName.isNotEmpty() ? snapshot_.customName : snapshot_.name;
}

bool PluginCard::isVuMeter() const noexcept
{
    return snapshot_.builtIn && snapshot_.internalType == kVuMeterType;
}

void PluginCard::setSnapshot(const NodeSnapshot& s)
{
    snapshot_ = s;
    refreshButtons();
    resized();
    repaint();
}

void PluginCard::setAccentColour(juce::Colour c)
{
    if (c == accent_)
        return;
    accent_ = c;
    repaint();
}

void PluginCard::setMappingCount(int count)
{
    if (count == mappingCount_)
        return;
    mappingCount_ = count;
    repaint();
}

void PluginCard::setSelected(bool selected)
{
    if (selected == selected_)
        return;
    selected_ = selected;
    repaint();
}

void PluginCard::setActivity(bool lit)
{
    if (lit == active_)
        return;
    active_ = lit;
    repaint();
}

void PluginCard::setMeterLevels(float left, float right)
{
    if (std::abs(left - levelL_) < 0.0005f && std::abs(right - levelR_) < 0.0005f)
        return;
    levelL_ = left;
    levelR_ = right;
    repaint();
}

void PluginCard::refreshButtons()
{
    // BYP lights amber while bypassed. ED is for plugins (built-ins have no
    // editor, nor does a missing or dead one); M is for everything but the VU
    // Meter, which has nothing to map.
    bypassButton_.setColour(juce::TextButton::buttonColourId,
                            snapshot_.bypassed ? tokens::accentWarning.withAlpha(0.18f) : tokens::bgSecondary);
    bypassButton_.setColour(juce::TextButton::textColourOffId,
                            snapshot_.bypassed ? tokens::accentWarning : tokens::fgPrimary);
    editButton_.setVisible(!snapshot_.isMissing() && !snapshot_.isDead() &&
                           (!snapshot_.builtIn || hasProcessorEditor(snapshot_.internalType)));
    mappingsButton_.setVisible(!isVuMeter());
    removeButton_.setVisible(hovered_);
}

void PluginCard::updateHover()
{
    const bool over = isMouseOver(/*includeChildren*/ true);
    if (over == hovered_)
        return;
    hovered_ = over;
    removeButton_.setVisible(hovered_);
    resized();
    repaint();
}

juce::Point<int> PluginCard::getPortCentre(bool isInput, int channel) const noexcept
{
    const int x = isInput ? kInPortX : getWidth() - kOutPortInset;
    if (channel == juce::AudioProcessorGraph::midiChannelIndex)
        return {x, kPortYMidi};
    return {x, channel == 0 ? kPortYL : kPortYR};
}

void PluginCard::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    juce::Path shape;
    shape.addRoundedRectangle(bounds, kRadius);

    // Body: dimmed while bypassed so the whole card reads as "off".
    g.setColour(snapshot_.bypassed ? tokens::bgCard.withMultipliedAlpha(0.6f) : tokens::bgCard);
    g.fillPath(shape);

    {
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(shape);
        g.setColour(snapshot_.bypassed ? accent_.withMultipliedAlpha(0.5f) : accent_);
        g.fillRect(bounds.withWidth(kAccentWidth));
        // The activity strip: cyan while a mapped control moves the plugin.
        g.setColour(active_ ? tokens::accentInfo : tokens::bgMuted);
        g.fillRect(bounds.withTrimmedLeft(kAccentWidth).removeFromBottom(static_cast<float>(kStripHeight)));
    }

    g.setColour(tokens::borderMuted);
    g.drawHorizontalLine(kHeaderHeight, bounds.getX() + kAccentWidth, bounds.getRight());

    // The drag grip: two columns of three dots.
    g.setColour(tokens::fgMuted.withAlpha(0.7f));
    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse(10.0f + 4.0f * static_cast<float>(col), 11.0f + 4.5f * static_cast<float>(row), 2.0f, 2.0f);

    // The name, struck through while bypassed (hidden while it's being edited),
    // and red while the plugin is missing (M5 U3) or dead (M5 U5).
    if (renameEditor_ == nullptr)
    {
        const auto name = card::titleFor(snapshot_);
        const auto font = nameFont();
        g.setFont(font);
        g.setColour((snapshot_.isMissing() || snapshot_.isDead()) ? tokens::accentDestructive
                    : snapshot_.bypassed                          ? tokens::fgMuted
                                                                  : tokens::fgPrimary);
        g.drawText(name, nameArea_, juce::Justification::centredLeft, true);
        if (snapshot_.bypassed)
        {
            const float width = juce::jmin(juce::GlyphArrangement::getStringWidth(font, name),
                                           static_cast<float>(nameArea_.getWidth()));
            g.setColour(tokens::fgPrimary.withAlpha(0.3f));
            g.drawHorizontalLine(nameArea_.getCentreY(),
                                 static_cast<float>(nameArea_.getX()),
                                 static_cast<float>(nameArea_.getX()) + width);
        }
    }

    const int textWidth = getWidth() - kTextLeft - kTextRightInset;
    if (isVuMeter())
    {
        // Two peak bars: teal, amber from 70%, red from 88% of the -60..0 dBFS scale.
        const auto bar = [&g, textWidth](int y, float level)
        {
            const juce::Rectangle<float> r(
                static_cast<float>(kTextLeft), static_cast<float>(y), static_cast<float>(textWidth), 7.0f);
            g.setColour(tokens::bgMuted);
            g.fillRoundedRectangle(r, 2.0f);
            const float filled = r.getWidth() * card::meterFraction(level);
            const float warnAt = r.getWidth() * 0.70f;
            const float hotAt = r.getWidth() * 0.88f;
            g.setColour(tokens::accentPrimary);
            g.fillRect(r.withWidth(juce::jmin(filled, warnAt)));
            if (filled > warnAt)
            {
                g.setColour(tokens::accentWarning);
                g.fillRect(r.withX(r.getX() + warnAt).withWidth(juce::jmin(filled, hotAt) - warnAt));
            }
            if (filled > hotAt)
            {
                g.setColour(tokens::accentDestructive);
                g.fillRect(r.withX(r.getX() + hotAt).withWidth(filled - hotAt));
            }
        };
        bar(40, levelL_);
        bar(50, levelR_);
        g.setFont(monoFont(10.0f));
        g.setColour(tokens::fgMuted);
        g.drawText(card::dbfsText(levelL_), kTextLeft, 60, textWidth / 2, 12, juce::Justification::centredLeft);
        g.drawText(card::dbfsText(levelR_) + " dBFS",
                   kTextLeft + textWidth / 2,
                   60,
                   textWidth - textWidth / 2,
                   12,
                   juce::Justification::centredRight);
    }
    else
    {
        const auto lines = card::linesFor(snapshot_, mappingCount_);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(tokens::fgMuted);
        g.drawText(lines.kind, kTextLeft, 37, textWidth, 16, juce::Justification::centredLeft, true);
        g.setFont(monoFont(10.0f));
        g.setColour(tokens::fgMuted.withAlpha(0.8f));
        g.drawText(lines.meta, kTextLeft, 54, textWidth, 14, juce::Justification::centredLeft, true);
    }

    // Ports: audio in teal, audio out amber, MIDI cyan, each ringed so it reads
    // against the card.
    const auto drawPort = [&g](int cx, int cy, juce::Colour c)
    {
        const auto box = juce::Rectangle<float>(0.0f, 0.0f, kPortRadius * 2.0f, kPortRadius * 2.0f)
                             .withCentre({static_cast<float>(cx), static_cast<float>(cy)});
        g.setColour(c);
        g.fillEllipse(box);
        g.setColour(tokens::bgWindow);
        g.drawEllipse(box, 1.5f);
    };
    const int outX = getWidth() - kOutPortInset;
    drawPort(kInPortX, kPortYL, tokens::accentPrimary);
    drawPort(kInPortX, kPortYR, tokens::accentPrimary);
    drawPort(kInPortX, kPortYMidi, tokens::accentInfo);
    drawPort(outX, kPortYL, tokens::accentWarning);
    drawPort(outX, kPortYR, tokens::accentWarning);
    drawPort(outX, kPortYMidi, tokens::accentInfo);

    // The edge: the "chosen" accent when selected, brighter on hover. Selected
    // is pink rather than the running cyan-green on purpose (#105): on this
    // canvas a card you have clicked and a chain that is playing are different
    // things, and under the old single accent they looked the same.
    const auto edge = selected_  ? tokens::accentSelected
                      : hovered_ ? tokens::borderDefault.brighter(0.35f)
                                 : tokens::borderDefault;
    g.setColour(edge);
    g.strokePath(shape, juce::PathStrokeType(selected_ ? 1.5f : 1.0f));
}

void PluginCard::resized()
{
    auto header = getLocalBounds().removeFromTop(kHeaderHeight).withTrimmedLeft(kAccentInset).reduced(6, 5);

    // From the right: Remove (on hover), ED, M, BYP; the name takes the rest.
    const auto place = [&header](juce::Button& button, int width)
    {
        if (!button.isVisible())
            return;
        button.setBounds(header.removeFromRight(width));
        header.removeFromRight(kButtonGap);
    };
    place(removeButton_, 22);
    place(editButton_, 26);
    place(mappingsButton_, 22);
    place(bypassButton_, 32);
    nameArea_ = header.withTrimmedLeft(kGripWidth);

    if (renameEditor_ != nullptr)
        renameEditor_->setBounds(nameArea_.expanded(2, 1));

    // The control row sits below the MIDI port, above the activity strip.
    if (controls_ != nullptr)
        controls_->setBounds(
            kTextLeft - 8, kStandardHeight - 6, getWidth() - (kTextLeft - 8) - kOutPortInset - 6, controlsHeight_ - 2);
}

bool PluginCard::hitTest(int x, int y)
{
    // Port hotspots are owned by SignalCanvas (drag-to-connect): report them as
    // "not on the card" so the click falls through to the canvas behind.
    constexpr int kPortHotspot = 12;
    const juce::Point<int> p{x, y};
    // midiChannelIndex is an unnamed enum, so cast it to match the int literals.
    for (const bool isInput : {true, false})
        for (const int ch : {0, 1, static_cast<int>(juce::AudioProcessorGraph::midiChannelIndex)})
            if (p.getDistanceFrom(getPortCentre(isInput, ch)) <= kPortHotspot)
                return false;
    return true;
}

// ----- Drag to move, click to select, double-click the name to rename ---------

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
            callbacks_.onDragging(snapshot_.id); // the canvas re-strokes cables live
    }
}

void PluginCard::mouseUp(const juce::MouseEvent&)
{
    if (didDrag_)
    {
        if (callbacks_.onMoved)
            callbacks_.onMoved(snapshot_.id, getPosition());
    }
    else
    {
        if (callbacks_.onSelect)
            callbacks_.onSelect(snapshot_.id);
        // A click on a card whose plugin died brings it back (M5 U5).
        if (snapshot_.isDead() && callbacks_.onRearm)
            callbacks_.onRearm(snapshot_.id);
    }
    didDrag_ = false;
}

void PluginCard::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (nameArea_.contains(e.getPosition()))
        beginRename();
}

void PluginCard::beginRename()
{
    if (renameEditor_ != nullptr)
        return;
    renameEditor_ = std::make_unique<juce::TextEditor>();
    auto& editor = *renameEditor_;
    editor.setFont(nameFont());
    editor.setText(displayName(), juce::dontSendNotification);
    editor.setColour(juce::TextEditor::outlineColourId, tokens::accentPrimary);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, tokens::accentPrimary);
    editor.onReturnKey = [this] { endRename(/*commit*/ true); };
    editor.onEscapeKey = [this] { endRename(/*commit*/ false); };
    editor.onFocusLost = [this] { endRename(/*commit*/ true); };
    addAndMakeVisible(editor);
    editor.setBounds(nameArea_.expanded(2, 1));
    editor.grabKeyboardFocus();
    editor.selectAll();
    repaint();
}

void PluginCard::endRename(bool commit)
{
    if (renameEditor_ == nullptr)
        return;

    // This runs inside one of the editor's own callbacks, so it can't be
    // deleted here: park it, and free it on the next rename or with the card.
    // The early return above makes any callback it fires from now on a no-op.
    retiredEditor_ = std::move(renameEditor_);
    const auto text = retiredEditor_->getText().trim();
    removeChildComponent(retiredEditor_.get());

    if (commit && callbacks_.onRename)
    {
        // Typing the plugin's own name, or nothing, clears the custom name.
        const auto custom = text == snapshot_.name ? juce::String() : text;
        if (custom != snapshot_.customName)
            callbacks_.onRename(snapshot_.id, custom);
    }
    repaint();
}

void PluginCard::setControls(std::unique_ptr<juce::Component> controls)
{
    if (controls_ != nullptr)
        removeChildComponent(controls_.get());
    controls_ = std::move(controls);
    controlsHeight_ = controls_ != nullptr ? controls_->getHeight() : 0;
    if (controls_ != nullptr)
        addAndMakeVisible(*controls_);
    resized();
}

int PluginCard::preferredHeight() const noexcept
{
    return kStandardHeight + controlsHeight_;
}

} // namespace pedalboard3::ui
