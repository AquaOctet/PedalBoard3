#include "FeatureChips.h"

#include "ColourScheme.h"
#include "session/FeatureGate.h"

#include <numeric>

namespace pedalboard3::ui
{

namespace
{
constexpr int kGap = 4;
constexpr int kChipPadding = 18;
constexpr float kFontSize = 12.0f;
} // namespace

FeatureChips::FeatureChips(AppContext& ctx) : ctx_(ctx)
{
    ctx_.features.addChangeListener(this);
    refresh();
}

FeatureChips::~FeatureChips()
{
    ctx_.features.removeChangeListener(this);
}

int FeatureChips::preferredWidth() const noexcept
{
    if (widths_.empty())
        return 0;
    return std::accumulate(widths_.begin(), widths_.end(), 0) + kGap * static_cast<int>(widths_.size() - 1);
}

void FeatureChips::resized()
{
    // Right-aligned, so the strip hugs the corner.
    auto area = getLocalBounds();
    for (std::size_t i = chips_.size(); i-- > 0;)
    {
        chips_[i]->setBounds(area.removeFromRight(widths_[i]));
        area.removeFromRight(kGap);
    }
}

void FeatureChips::refresh()
{
    std::vector<session::Feature> enabled;
    for (const auto feature : session::kAllFeatures)
        if (ctx_.features.isEnabled(feature))
            enabled.push_back(feature);

    if (enabled != shown_)
    {
        shown_ = enabled;
        chips_.clear();
        widths_.clear();
        const juce::Font font{juce::FontOptions(kFontSize)};
        for (const auto feature : shown_)
        {
            const auto label = session::featureShortName(feature);
            auto chip = std::make_unique<juce::TextButton>(label);
            chip->setClickingTogglesState(false);
            chip->setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
            chip->setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
            chip->setColour(juce::TextButton::textColourOnId, tokens::fgInverse);
            chip->setColour(juce::TextButton::textColourOffId, tokens::fgMuted);
            chip->onClick = [this, feature]
            { ctx_.features.setOnForBoard(feature, !ctx_.features.isOnForBoard(feature)); };
            addAndMakeVisible(*chip);
            widths_.push_back(juce::GlyphArrangement::getStringWidthInt(font, label) + kChipPadding);
            chips_.push_back(std::move(chip));
        }
        resized();
        if (onLayoutChanged)
            onLayoutChanged();
    }

    for (std::size_t i = 0; i < shown_.size(); ++i)
    {
        const bool on = ctx_.features.isOnForBoard(shown_[i]);
        chips_[i]->setToggleState(on, juce::dontSendNotification);
        chips_[i]->setTooltip(session::featureName(shown_[i]) +
                              (on ? " is on for this board. Click to switch it off here."
                                  : " is off for this board. Click to switch it on here."));
    }
}

} // namespace pedalboard3::ui
