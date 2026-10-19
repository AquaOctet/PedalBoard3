#include "InputMonitor.h"

#include "ColourScheme.h"
#include "engine/AudioEngine.h"
#include "Icons.h"

#include <cmath>

namespace pedalboard3::ui
{

namespace
{

constexpr int kRefreshHz = 30;
constexpr float kIconSize = 13.0f;
constexpr float kIconBarGap = 7.0f;
constexpr float kBarHeight = 10.0f;
// Per tick. Only the fall is smoothed: a meter that eases up as well as down
// lies about transients, and a transient is the thing you are watching for
// when you are asking whether a signal is arriving at all.
constexpr float kFallSmoothing = 0.22f;

// The window the bar covers. Below -60 dB is noise and does not deserve to
// move the needle; the top is full scale.
constexpr float kFloorDb = -60.0f;
constexpr float kClipDb = -3.0f;
constexpr float kWarnDb = -12.0f;

float toDb(float linear)
{
    return linear > 0.0f ? 20.0f * std::log10(linear) : kFloorDb;
}

// dB mapped onto 0..1 across the bar. Linear in dB, not in amplitude: an
// amplitude-linear meter spends almost all of its travel in the top 6 dB and
// shows nothing at all for a quiet but perfectly good signal.
float barPosition(float db)
{
    return juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / (0.0f - kFloorDb));
}

juce::Colour levelColour(float db)
{
    if (db >= kClipDb)
        return tokens::accentDestructive;
    if (db >= kWarnDb)
        return tokens::accentWarning;
    return tokens::accentPrimary;
}

} // namespace

InputMonitor::InputMonitor(engine::AudioEngine& engine) : engine_(engine)
{
    setInterceptsMouseClicks(true, false); // for the tooltip
    setTooltip("Audio input level, measured before the board — so it still reads when the board is empty.");
    startTimerHz(kRefreshHz);
}

InputMonitor::~InputMonitor()
{
    stopTimer();
}

void InputMonitor::timerCallback()
{
    const float level = engine_.readInputPeak();

    // Rise instantly, fall smoothly.
    shown_ = level >= shown_ ? level : meter::smooth(shown_, level, kFallSmoothing);
    peak_.update(level, juce::Time::getMillisecondCounterHiRes());

    repaint();
}

void InputMonitor::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    const float db = toDb(shown_);
    const bool live = shown_ > 0.0f;

    g.setColour(live ? levelColour(db) : tokens::fgSubtle);
    strokeIcon(g, micIcon(), area.removeFromLeft(kIconSize).withSizeKeepingCentre(kIconSize, kIconSize));
    area.removeFromLeft(kIconBarGap);

    const auto bar = area.withSizeKeepingCentre(area.getWidth(), kBarHeight);
    g.setColour(tokens::bgMuted);
    g.fillRoundedRectangle(bar, 3.0f);

    if (live)
    {
        g.setColour(levelColour(db));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * barPosition(db)), 3.0f);
    }

    // The held peak, same treatment as the CPU meter's.
    if (peak_.peak() > 0.0f)
    {
        const float x = bar.getX() + bar.getWidth() * barPosition(toDb(peak_.peak()));
        g.setColour(tokens::fgMuted);
        g.drawVerticalLine(juce::roundToInt(x), bar.getY(), bar.getBottom());
    }
}

} // namespace pedalboard3::ui
