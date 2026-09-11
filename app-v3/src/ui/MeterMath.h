#pragma once

#include <algorithm>
#include <cstdint>

namespace pedalboard3::ui::meter
{

// Pure maths for the top bar's meters (UI refresh U2 / U3), kept free of
// components so the headless tests can cover it.

enum class Zone
{
    Normal,
    Warning,
    Critical
};

inline constexpr float kWarningLoad = 0.70f; // the tick on the CPU bar
inline constexpr float kCriticalLoad = 0.90f;

inline Zone zoneFor(float load) noexcept
{
    if (load >= kCriticalLoad)
        return Zone::Critical;
    if (load >= kWarningLoad)
        return Zone::Warning;
    return Zone::Normal;
}

// One step of exponential smoothing towards `target`. `amount` (clamped to
// 0..1) is how far to move this step, so it never overshoots.
inline float smooth(float current, float target, float amount) noexcept
{
    return current + (target - current) * std::clamp(amount, 0.0f, 1.0f);
}

// Holds the highest recent value for `holdMs`, then falls to the current one.
class PeakHold
{
public:
    explicit PeakHold(double holdMs = 1000.0) : holdMs_(holdMs) {}

    float update(float value, double nowMs) noexcept
    {
        if (value >= peak_ || nowMs - peakAtMs_ >= holdMs_)
        {
            peak_ = value;
            peakAtMs_ = nowMs;
        }
        return peak_;
    }

    float peak() const noexcept { return peak_; }

private:
    double holdMs_;
    float peak_{0.0f};
    double peakAtMs_{0.0};
};

// A light that stays lit for `litMs` after the counter it watches changes.
// The first look only records the counter, so input from before the light
// existed doesn't flash it.
class ActivityBlink
{
public:
    explicit ActivityBlink(double litMs = 120.0) : litMs_(litMs) {}

    bool update(std::uint64_t counter, double nowMs) noexcept
    {
        if (!seeded_)
        {
            seeded_ = true;
            lastCounter_ = counter;
            return false;
        }
        if (counter != lastCounter_)
        {
            lastCounter_ = counter;
            changedAtMs_ = nowMs;
            changed_ = true;
        }
        return changed_ && nowMs - changedAtMs_ < litMs_;
    }

private:
    double litMs_;
    std::uint64_t lastCounter_{0};
    double changedAtMs_{0.0};
    bool seeded_{false};
    bool changed_{false};
};

} // namespace pedalboard3::ui::meter
