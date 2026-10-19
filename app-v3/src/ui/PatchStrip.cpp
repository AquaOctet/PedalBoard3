#include "PatchStrip.h"

#include "ColourScheme.h"
#include "Fonts.h"
#include "Icons.h"
#include "session/BankDocument.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kArrowWidth = 28;
constexpr int kNameMinWidth = 120;
constexpr int kNameMaxWidth = 300;
// JUCE ellipsises on "does not fit", and a width computed to the exact glyph
// run is right on that boundary — the name lost its last letters at every
// window size. A few pixels of slack keeps it off the edge.
constexpr int kNameSlack = 10;
constexpr int kNamePadding = 12; // either side of the name block
constexpr int kIndexGap = 8;     // "2/6" to the name
constexpr int kCaretWidth = 14;
constexpr float kRadius = 4.0f;
constexpr float kIndexSize = 11.0f;
constexpr float kNameSize = 14.0f;

juce::Font indexFont()
{
    return fonts::mono(kIndexSize);
}
juce::Font nameFont()
{
    return fonts::ui(kNameSize, fonts::Weight::Medium);
}
} // namespace

// A transparent hit target. It paints nothing: the strip paints everything, so
// that a hover wash can be clipped to the container's rounded corners.
class PatchStrip::Segment : public juce::Button
{
public:
    explicit Segment(const juce::String& name) : juce::Button(name) {}

private:
    void paintButton(juce::Graphics&, bool, bool) override {}
};

PatchStrip::PatchStrip(AppContext& ctx) : ctx_(ctx)
{
    prev_ = std::make_unique<Segment>("Previous patch");
    name_button_ = std::make_unique<Segment>("Patch");
    next_ = std::make_unique<Segment>("Next patch");

    prev_->setTooltip("Previous patch");
    next_->setTooltip("Next patch");
    name_button_->setTooltip("The playing patch. Click for the bank: new, duplicate, rename, remove, go to.");

    prev_->onClick = [this] { ctx_.bankDocument.previous(); };
    next_->onClick = [this] { ctx_.bankDocument.next(); };
    name_button_->onClick = [this]
    {
        if (onOpenMenu)
            onOpenMenu();
    };

    addAndMakeVisible(*prev_);
    addAndMakeVisible(*name_button_);
    addAndMakeVisible(*next_);

    ctx_.bankDocument.addChangeListener(this);
    refresh();
}

PatchStrip::~PatchStrip()
{
    ctx_.bankDocument.removeChangeListener(this);
}

void PatchStrip::refresh()
{
    const auto& doc = ctx_.bankDocument;
    const int count = doc.size();

    index_ = juce::String(doc.activeIndex() + 1);
    count_ = "/" + juce::String(count);
    name_ = doc.activePatch().name;

    prev_->setEnabled(count > 1);
    next_->setEnabled(count > 1);

    const int indexWidth = juce::GlyphArrangement::getStringWidthInt(indexFont(), index_ + count_);
    const int nameWidth = juce::GlyphArrangement::getStringWidthInt(nameFont(), name_);
    const int wanted =
        juce::jlimit(kNameMinWidth,
                     kNameMaxWidth,
                     kNamePadding * 2 + indexWidth + kIndexGap + nameWidth + kIndexGap + kCaretWidth + kNameSlack);
    const int want = kArrowWidth * 2 + wanted;
    static_assert(kArrowWidth * 2 + kNameMinWidth == PatchStrip::kMinimumWidth,
                  "PatchStrip::kMinimumWidth is what the bar reserves; keep it with the floor it names");
    if (want != preferredWidth_)
    {
        preferredWidth_ = want;
        if (onLayoutChanged)
            onLayoutChanged();
    }
    repaint();
}

void PatchStrip::resized()
{
    auto area = getLocalBounds();
    prev_->setBounds(area.removeFromLeft(kArrowWidth));
    next_->setBounds(area.removeFromRight(kArrowWidth));
    name_button_->setBounds(area);
}

void PatchStrip::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    juce::Path container;
    container.addRoundedRectangle(bounds, kRadius);

    g.setColour(tokens::bgSecondary.withAlpha(0.45f));
    g.fillPath(container);

    // Hover and press washes, clipped to the container so a corner segment
    // cannot paint outside the rounded edge.
    const auto wash = [&](const Segment& seg)
    {
        if (!seg.isEnabled() || !(seg.isOver() || seg.isDown()))
            return;
        g.saveState();
        g.reduceClipRegion(container);
        g.setColour(tokens::fgPrimary.withAlpha(seg.isDown() ? 0.12f : 0.06f));
        g.fillRect(seg.getBounds());
        g.restoreState();
    };
    wash(*prev_);
    wash(*name_button_);
    wash(*next_);

    g.setColour(tokens::borderDefault);
    g.strokePath(container, juce::PathStrokeType(1.0f));

    // The two dividers: one control made of three parts, not three controls.
    const auto divider = [&](int x)
    {
        g.setColour(tokens::borderDefault.withAlpha(0.7f));
        g.fillRect(
            juce::Rectangle<float>(static_cast<float>(x), bounds.getY() + 5.0f, 1.0f, bounds.getHeight() - 10.0f));
    };
    divider(prev_->getRight());
    divider(next_->getX() - 1);

    const auto arrow = [&](const Segment& seg, const juce::Path& icon)
    {
        g.setColour(seg.isEnabled() ? tokens::fgMenu : tokens::fgSubtle);
        strokeIcon(g, icon, seg.getBounds().toFloat().reduced(9.0f));
    };
    arrow(*prev_, chevronLeftIcon());
    arrow(*next_, chevronRightIcon());

    // "2/6  Verse Clean  ▾": the index muted so the name is what you read,
    // and the caret because the name opens a menu.
    auto text = name_button_->getBounds().reduced(kNamePadding, 0);
    auto caret = text.removeFromRight(kCaretWidth);
    g.setColour(tokens::fgSubtle);
    strokeIcon(g, chevronDownIcon(), caret.toFloat().reduced(2.0f, 0.0f));
    text.removeFromRight(kIndexGap);

    g.setFont(indexFont());
    const int indexWidth = juce::GlyphArrangement::getStringWidthInt(indexFont(), index_);
    const int countWidth = juce::GlyphArrangement::getStringWidthInt(indexFont(), count_);
    g.setColour(tokens::fgMuted);
    g.drawText(index_, text.removeFromLeft(indexWidth), juce::Justification::centredLeft);
    g.setColour(tokens::fgSubtle);
    g.drawText(count_, text.removeFromLeft(countWidth), juce::Justification::centredLeft);
    text.removeFromLeft(kIndexGap);

    g.setFont(nameFont());
    g.setColour(tokens::fgPrimary);
    g.drawText(name_, text, juce::Justification::centredLeft, true);
}

} // namespace pedalboard3::ui
