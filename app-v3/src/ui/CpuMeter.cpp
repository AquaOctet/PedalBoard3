#include "CpuMeter.h"

#include "ColourScheme.h"

namespace pedalboard3::ui
{

namespace
{

constexpr float kCaptionWidth = 34.0f;
constexpr float kNumberWidth = 40.0f;
constexpr float kBarHeight = 10.0f;
constexpr float kSmoothing = 0.35f; // per 100 ms tick

juce::Colour barColour(meter::Zone zone)
{
    switch (zone)
    {
    case meter::Zone::Critical:
        return tokens::accentDestructive;
    case meter::Zone::Warning:
        return tokens::accentWarning;
    case meter::Zone::Normal:
        break;
    }
    return tokens::accentPrimary;
}

juce::Colour numberColour(meter::Zone zone)
{
    return zone == meter::Zone::Normal ? tokens::fgPrimary : barColour(zone);
}

int percent(float load)
{
    return juce::roundToInt(juce::jlimit(0.0f, 1.0f, load) * 100.0f);
}

} // namespace

CpuMeter::CpuMeter(juce::AudioDeviceManager* deviceManager) : deviceManager_(deviceManager)
{
    setInterceptsMouseClicks(true, false); // for the tooltip
    startTimerHz(10);
}

CpuMeter::~CpuMeter()
{
    stopTimer();
}

void CpuMeter::timerCallback()
{
    auto* device = deviceManager_ != nullptr ? deviceManager_->getCurrentAudioDevice() : nullptr;
    hasDevice_ = device != nullptr;

    if (!hasDevice_)
    {
        shown_ = 0.0f;
        setTooltip("Audio CPU load: no audio device");
        repaint();
        return;
    }

    const auto load = static_cast<float>(juce::jlimit(0.0, 1.0, deviceManager_->getCpuUsage()));
    shown_ = meter::smooth(shown_, load, kSmoothing);
    peak_.update(load, juce::Time::getMillisecondCounterHiRes());

    auto tip = "Audio CPU load " + juce::String(percent(shown_)) + "% (peak " + juce::String(percent(peak_.peak())) +
               "%). Amber from 70%, red from 90%.";
    if (const int xruns = device->getXRunCount(); xruns >= 0)
        tip << "\nDropouts since the device started: " << xruns;
    setTooltip(tip);
    repaint();
}

void CpuMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto caption = area.removeFromLeft(kCaptionWidth);
    const auto number = area.removeFromRight(kNumberWidth);
    const auto bar = area.reduced(4.0f, 0.0f).withSizeKeepingCentre(area.getWidth() - 8.0f, kBarHeight);

    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.setColour(tokens::fgMuted);
    g.drawText("CPU", caption, juce::Justification::centredLeft);

    g.setColour(tokens::bgMuted);
    g.fillRoundedRectangle(bar, 3.0f);

    const auto zone = meter::zoneFor(shown_);
    if (hasDevice_)
    {
        g.setColour(barColour(zone));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, shown_)), 3.0f);

        const float peakX = bar.getX() + bar.getWidth() * juce::jlimit(0.0f, 1.0f, peak_.peak());
        g.setColour(tokens::fgMuted);
        g.drawVerticalLine(juce::roundToInt(peakX), bar.getY(), bar.getBottom());
    }

    // The 70% tick.
    const float tickX = bar.getX() + bar.getWidth() * meter::kWarningLoad;
    g.setColour(tokens::borderDefault);
    g.drawVerticalLine(juce::roundToInt(tickX), bar.getY() - 2.0f, bar.getBottom() + 2.0f);

    g.setColour(hasDevice_ ? numberColour(zone) : tokens::fgMuted);
    g.drawText(hasDevice_ ? juce::String(percent(shown_)) + "%" : juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")),
               number,
               juce::Justification::centredRight);
}

} // namespace pedalboard3::ui
