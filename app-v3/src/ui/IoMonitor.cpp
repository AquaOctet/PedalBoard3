#include "IoMonitor.h"

#include "ColourScheme.h"
#include "engine/AudioEngine.h"
#include "MeterRow.h"

#include <cmath>

namespace pedalboard3::ui
{

namespace
{

constexpr int kRefreshHz = 30;
constexpr int kCaptionWidth = 26; // "OUT" at 10pt, with room to breathe

// Per tick. Only the fall is smoothed: see the note on the class.
constexpr float kFallSmoothing = 0.22f;

// The window the track covers. Below -60 dB is noise and does not deserve to
// move anything; the top is full scale.
constexpr float kFloorDb = -60.0f;
constexpr float kClipDb = -3.0f;
constexpr float kWarnDb = -12.0f;

float toDb(float linear)
{
    return linear > 0.0f ? 20.0f * std::log10(linear) : kFloorDb;
}

// dB mapped onto 0..1 across the track, linear in dB rather than amplitude.
float trackPosition(float linear)
{
    return juce::jlimit(0.0f, 1.0f, (toDb(linear) - kFloorDb) / (0.0f - kFloorDb));
}

juce::Colour levelColour(float linear)
{
    const float db = toDb(linear);
    if (db >= kClipDb)
        return tokens::accentDestructive;
    if (db >= kWarnDb)
        return tokens::accentWarning;
    return tokens::accentPrimary;
}

juce::String describe(const char* name, float linear)
{
    if (linear <= 0.0f)
        return juce::String(name) + ": silent";
    return juce::String(name) + ": " + juce::String(toDb(linear), 1) + " dBFS";
}

} // namespace

IoMonitor::IoMonitor(engine::AudioEngine& engine) : engine_(engine)
{
    setInterceptsMouseClicks(true, false); // for the tooltip
    startTimerHz(kRefreshHz);
}

IoMonitor::~IoMonitor()
{
    stopTimer();
}

void IoMonitor::advance(Channel& channel, float level, double nowMs)
{
    channel.shown = level >= channel.shown ? level : meter::smooth(channel.shown, level, kFallSmoothing);
    channel.peak.update(level, nowMs);
}

void IoMonitor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    advance(in_, engine_.readInputPeak(), now);
    advance(out_, engine_.readOutputPeak(), now);

    // The tooltip carries the figures the bar has no room for, and names the
    // two places they are taken -- which is the part that makes the pair
    // diagnostic rather than decorative.
    setTooltip(describe("IN", in_.shown) + ", measured before the board\n" + describe("OUT", out_.shown) +
               ", measured after the master mute");

    repaint();
}

void IoMonitor::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().withSizeKeepingCentre(getWidth(), meter::kStackHeight);

    const auto row = [](const char* caption, const Channel& c)
    {
        meter::Row r;
        r.caption = caption;
        r.fill = trackPosition(c.shown);
        r.peak = c.peak.peak() > 0.0f ? trackPosition(c.peak.peak()) : 0.0f;
        r.bar = levelColour(c.shown);
        r.active = true;
        return r;
    };

    meter::drawRow(g, area.removeFromTop(meter::kRowHeight), kCaptionWidth, 0, row("IN", in_));
    area.removeFromTop(meter::kRowGap);
    meter::drawRow(g, area.removeFromTop(meter::kRowHeight), kCaptionWidth, 0, row("OUT", out_));
}

} // namespace pedalboard3::ui
