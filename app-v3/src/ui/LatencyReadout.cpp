#include "LatencyReadout.h"

#include "ColourScheme.h"
#include "Fonts.h"
#include "Icons.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kIconSize = 14;
constexpr int kIconTextGap = 6;
constexpr float kTextSize = 11.0f;

juce::Font textFont()
{
    return fonts::mono(kTextSize);
}
} // namespace

LatencyReadout::LatencyReadout(juce::AudioDeviceManager* deviceManager) : deviceManager_(deviceManager)
{
    if (deviceManager_ != nullptr)
        deviceManager_->addChangeListener(this);
    refresh();
}

LatencyReadout::~LatencyReadout()
{
    if (deviceManager_ != nullptr)
        deviceManager_->removeChangeListener(this);
}

void LatencyReadout::refresh()
{
    auto* dev = deviceManager_ != nullptr ? deviceManager_->getCurrentAudioDevice() : nullptr;
    const double hz = dev != nullptr ? dev->getCurrentSampleRate() : 0.0;
    const int buffer = dev != nullptr ? dev->getCurrentBufferSizeSamples() : 0;

    hasDevice_ = dev != nullptr && hz > 0.0;
    if (hasDevice_)
    {
        const double ms = buffer / hz * 2000.0; // in and out, as the demo counts it
        text_ = juce::String(ms, 1) + "ms";
        setTooltip("Round-trip latency: " + juce::String(buffer) + " samples at " + juce::String(static_cast<int>(hz)) +
                   " Hz, in and out.");
    }
    else
    {
        text_ = "—";
        setTooltip("No audio device, so there is no latency to report.");
    }

    const int want = kIconSize + kIconTextGap + juce::GlyphArrangement::getStringWidthInt(textFont(), text_);
    if (want != preferredWidth_)
    {
        preferredWidth_ = want;
        if (onLayoutChanged)
            onLayoutChanged();
    }
    repaint();
}

void LatencyReadout::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour(tokens::fgMuted);
    strokeIcon(g, activityIcon(), area.removeFromLeft(kIconSize).toFloat());
    area.removeFromLeft(kIconTextGap);
    g.setFont(textFont());
    g.setColour(hasDevice_ ? tokens::fgMuted : tokens::fgSubtle);
    g.drawText(text_, area, juce::Justification::centredLeft);
}

} // namespace pedalboard3::ui
