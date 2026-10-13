#pragma once

#include "MidiMapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace pedalboard3::control
{

// Turns incoming MIDI messages into parameter-update intents according to a
// table of MidiMappings. Owns the per-mapping latch state (momentary→toggle)
// so callers stay stateless. Pure with respect to the audio engine: process()
// only computes WHICH targets should change and to WHAT value — applying those
// onto the live AudioProcessorGraph is the engine layer's job (next increment).
//
// Threading: not internally synchronised. The intended use is a single MIDI
// consumer thread calling process(); setMappings()/reset() happen on the
// message thread while MIDI input is paused, mirroring how the engine swaps
// patches.
class MidiRouter
{
public:
    // A computed change: set `target` to `value` (PluginParameter values are
    // normalized 0..1; PatchSelect/AppCommand carry the value for handlers that
    // want it, e.g. a fader driving a continuous app command). Shared with the
    // OSC router via control::ParameterUpdate (defined in MidiMapping.h).
    using Update = ParameterUpdate;

    MidiRouter() = default;

    // Replace the mapping table. Resets all latch state — a remap shouldn't
    // leave a toggle stuck from the previous patch.
    void setMappings(std::vector<MidiMapping> mappings);

    const std::vector<MidiMapping>& mappings() const noexcept { return mappings_; }

    // Compute the updates implied by one incoming message. A single message may
    // match several mappings (same CC bound to two params), so the result can
    // hold 0..N updates. Advances latch state for matched latch mappings.
    std::vector<Update> process(const juce::MidiMessage& msg);

    // Clear all latch toggles back to released/off (e.g. on patch switch or
    // panic) without dropping the mappings themselves.
    void reset() noexcept;

private:
    // Per-mapping runtime state for latch (momentary→toggle) handling.
    struct LatchState
    {
        bool outputOn{false};   // current toggle output (false → rangeMin side)
        bool buttonHeld{false}; // physical press state, for rising-edge detection
    };

    std::vector<MidiMapping> mappings_;
    std::vector<LatchState> latch_; // parallel to mappings_
};

} // namespace pedalboard3::control
