#pragma once

#include "MidiMapping.h" // ParameterUpdate
#include "OscMapping.h"

#include <juce_osc/juce_osc.h>

#include <optional>
#include <vector>

namespace pedalboard3::control
{

// Turns incoming OSC messages into parameter-update intents per a table of
// OscMappings — the OSC counterpart of MidiRouter. Emits the same
// ParameterUpdate type so the engine sink applies MIDI- and OSC-driven changes
// uniformly. Stateless w.r.t. value (no latch); pure w.r.t. the audio engine.
//
// Mapping addresses are parsed once in setMappings() and cached, so process()
// neither constructs an OSCAddress nor risks throwing per message — invalid
// addresses are simply skipped.
//
// Threading: not internally synchronised — single OSC consumer; setMappings()
// on the message thread while input is quiesced.
class OscRouter
{
public:
    using Update = ParameterUpdate;

    OscRouter() = default;

    void setMappings(std::vector<OscMapping> mappings);
    const std::vector<OscMapping>& mappings() const noexcept { return mappings_; }

    // Compute the updates implied by one incoming OSC message. One message may
    // match several mappings (wildcards / shared addresses), so 0..N updates are
    // returned.
    std::vector<Update> process(const juce::OSCMessage& msg) const;

private:
    std::vector<OscMapping> mappings_;
    std::vector<std::optional<juce::OSCAddress>> parsed_; // parallel; nullopt = invalid address
};

} // namespace pedalboard3::control
