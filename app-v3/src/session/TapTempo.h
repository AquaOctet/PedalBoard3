#pragma once

#include "engine/TransportMath.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace pedalboard3::session
{

// Tap tempo (M6 U3, plan KD3, AC-TR-2): the tempo is the average gap between
// the last four taps. A gap of more than 2 s starts again. The result is
// clamped to 20-300 BPM and rounded to 0.1 BPM. Pure: the caller supplies the
// time, so keys, MIDI, OSC and the Tap button can share one instance.
class TapTempo
{
public:
    static constexpr double kResetGapMs = 2000.0;
    static constexpr std::size_t kTaps = 4;

    // A tap at `nowMs` on a monotonic clock. Returns the tempo once two or more
    // taps are in the window.
    std::optional<double> tap(double nowMs) noexcept
    {
        if (count_ > 0)
        {
            const double last = taps_[count_ - 1];
            if (nowMs - last > kResetGapMs || nowMs <= last)
                count_ = 0; // too long a gap, or a clock that went backwards: start again
        }
        if (count_ == kTaps)
        {
            for (std::size_t i = 1; i < kTaps; ++i)
                taps_[i - 1] = taps_[i];
            --count_;
        }
        taps_[count_++] = nowMs;
        if (count_ < 2)
            return std::nullopt;

        const double gapMs = (taps_[count_ - 1] - taps_[0]) / static_cast<double>(count_ - 1);
        const double bpm = engine::transport::clampTempo(60000.0 / gapMs);
        return std::round(bpm * 10.0) / 10.0;
    }

    void reset() noexcept { count_ = 0; }

private:
    std::array<double, kTaps> taps_{};
    std::size_t count_{0};
};

} // namespace pedalboard3::session
