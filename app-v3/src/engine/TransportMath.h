#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdint>

namespace pedalboard3::engine::transport
{

// The transport's position maths (M6 U1), free of the audio engine so the
// headless tests cover it. Beats are quarter notes (PPQ), the unit
// juce::AudioPlayHead reports. A bar holds numerator * 4 / denominator of
// them, and one beat is one denominator note.

inline constexpr double kDefaultTempo = 120.0;
inline constexpr double kMinTempo = 20.0;
inline constexpr double kMaxTempo = 300.0;

// The PPQ position after `numSamples` more samples at `bpm`. Advancing block
// by block, rather than recomputing from the sample count, keeps the position
// continuous when the tempo changes while playing.
inline double advancePpq(double ppq, int numSamples, double sampleRate, double bpm) noexcept
{
    if (sampleRate <= 0.0)
        return ppq;
    return ppq + static_cast<double>(numSamples) / sampleRate * bpm / 60.0;
}

inline double quarterNotesPerBar(int numerator, int denominator) noexcept
{
    return denominator > 0 ? numerator * 4.0 / denominator : 4.0;
}

// Whole bars before `ppq`: 0 in the first bar. A hair of tolerance counts a
// position that lands on a bar line after floating-point accumulation as that
// bar.
inline std::int64_t barsBefore(double ppq, int numerator, int denominator) noexcept
{
    const double bar = quarterNotesPerBar(numerator, denominator);
    return bar > 0.0 ? static_cast<std::int64_t>(std::floor(ppq / bar + 1e-9)) : 0;
}

inline double barStartPpq(double ppq, int numerator, int denominator) noexcept
{
    return static_cast<double>(barsBefore(ppq, numerator, denominator)) * quarterNotesPerBar(numerator, denominator);
}

// "bar.beat", both counted from 1, for the top bar's readout: 5 quarter notes
// into 4/4 is "2.2".
inline juce::String barsBeatsText(double ppq, int numerator, int denominator)
{
    const auto bar = barsBefore(ppq, numerator, denominator);
    const double beatLength = denominator > 0 ? 4.0 / denominator : 1.0;
    const double intoBar = ppq - barStartPpq(ppq, numerator, denominator);
    const int beat =
        juce::jlimit(0, juce::jmax(numerator - 1, 0), static_cast<int>(std::floor(intoBar / beatLength + 1e-9)));
    return juce::String(bar + 1) + "." + juce::String(beat + 1);
}

inline double clampTempo(double bpm) noexcept
{
    return juce::jlimit(kMinTempo, kMaxTempo, bpm);
}

// Time signatures the transport accepts: 1 to 16 beats of a whole, half,
// quarter, eighth or sixteenth note.
inline bool isValidTimeSignature(int numerator, int denominator) noexcept
{
    return numerator >= 1 && numerator <= 16 &&
           (denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16);
}

} // namespace pedalboard3::engine::transport
