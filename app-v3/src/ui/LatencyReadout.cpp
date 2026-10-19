#include "LatencyReadout.h"

#include "ColourScheme.h"
#include "Fonts.h"
#include "Icons.h"
#include "MeterRow.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kIconSize = 13;
constexpr int kIconTextGap = 5;
constexpr float kCaptionSize = 10.0f;
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
        setTooltip("Round-trip delay: " + juce::String(buffer) + " samples at " + juce::String(static_cast<int>(hz)) +
                   " Hz, in and out.");
    }
    else
    {
        text_ = juce::CharPointer_UTF8("\xe2\x80\x94");
        setTooltip("No audio device, so there is no delay to report.");
    }

    const int want = juce::jmax(kIconSize + kIconTextGap + juce::GlyphArrangement::getStringWidthInt(textFont(), text_),
                                juce::GlyphArrangement::getStringWidthInt(captionFont(), "DELAY"));
    if (want != preferredWidth_)
    {
        preferredWidth_ = want;
        if (onLayoutChanged)
            onLayoutChanged();
    }
    repaint();
}

juce::Font LatencyReadout::captionFont()
{
    return fonts::ui(kCaptionSize, fonts::Weight::Medium);
}

void LatencyReadout::paint(juce::Graphics& g)
{
    // Two rows, captioned, because its neighbours are: IN / OUT on one side
    // and CPU / MEM on the other both say what they are, and a lone number
    // with a pulse icon between them did not (#122). "10.7ms" is only
    // obviously a latency if you already knew that is what lives there.
    auto area = getLocalBounds().withSizeKeepingCentre(getWidth(), meter::kStackHeight);

    auto caption = area.removeFromTop(meter::kRowHeight);
    g.setFont(captionFont());
    g.setColour(hasDevice_ ? tokens::fgMuted : tokens::fgSubtle);
    g.drawText("DELAY", caption, juce::Justification::centredLeft);

    area.removeFromTop(meter::kRowGap);
    auto value = area.removeFromTop(meter::kRowHeight);
    g.setColour(tokens::fgMuted);
    strokeIcon(g, activityIcon(), value.removeFromLeft(kIconSize).toFloat());
    value.removeFromLeft(kIconTextGap);
    g.setFont(textFont());
    g.setColour(hasDevice_ ? tokens::fgPrimary : tokens::fgSubtle);
    g.drawText(text_, value, juce::Justification::centredLeft);
}

} // namespace pedalboard3::ui
