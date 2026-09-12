#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace pedalboard3::session
{

struct PatchBank;

// The optional features (plan docs/plans/2026-09-12-002-feat-optional-features-plan.md,
// KD1). Each is switched on in its own Settings section, for the whole app,
// and then per board (FeatureGate). Tempo / BPM, the Metronome and the Looper
// start off; MIDI starts on and OSC off (KD2, the user's decision).
enum class Feature
{
    Tempo, // the transport: the pill, Space / Home, transport commands and MMC
    Metronome,
    Looper,
    Midi, // all MIDI input
    Osc,  // the OSC server and OSC mappings
};

inline constexpr std::size_t kFeatureCount = 5;
inline constexpr std::array<Feature, kFeatureCount> kAllFeatures{
    Feature::Tempo, Feature::Metronome, Feature::Looper, Feature::Midi, Feature::Osc};

constexpr std::size_t indexOf(Feature feature) noexcept
{
    return static_cast<std::size_t>(feature);
}

// "tempo", "metronome", "looper", "midi", "osc": the Preferences keys
// ("feature.<key>") and the board's <BANK> attributes.
const char* featureKey(Feature feature) noexcept;

// What the UI calls it: "Tempo / BPM", "Metronome", "Looper", "MIDI", "OSC".
juce::String featureName(Feature feature);

// On in Settings on a fresh install (KD2).
bool enabledByDefault(Feature feature) noexcept;

// Whether a board uses a feature (KD5), which is what the question on opening a
// board asks about:
//   - Tempo: a patch whose tempo or time signature isn't the default 120 BPM in 4/4
//   - Metronome / Looper: a node of that type in any patch
//   - MIDI / OSC: MIDI / OSC mappings in any patch
bool boardUses(const PatchBank& bank, Feature feature);

// The short label on a chip: "Tempo", "Metronome", "Looper", "MIDI", "OSC".
juce::String featureShortName(Feature feature);

// What opening `bank` asks to turn on (KD5, U3): the features it uses and keeps
// on for itself that are off in Settings (`enabledInSettings`), in the order
// of kAllFeatures.
std::vector<Feature> featuresToAsk(const PatchBank& bank, const std::function<bool(Feature)>& enabledInSettings);

// The features as a sentence would list them: "Looper", "Looper and OSC",
// "Tempo / BPM, Looper and OSC".
juce::String featureListText(const std::vector<Feature>& features);

} // namespace pedalboard3::session
