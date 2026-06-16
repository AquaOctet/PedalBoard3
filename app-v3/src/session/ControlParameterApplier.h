#pragma once

#include "control/MidiMapping.h" // control::MidiTarget / MidiTargetKind
#include "NodeId.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace pedalboard3::session
{

// Bridges the control surface (MIDI / OSC routers) to live audio processors:
// turns a computed control update (a MidiTarget + normalized value) into an
// actual parameter set on the processor that the target's NodeId resolves to.
//
// Decoupled from how the graph is stored via a NodeResolver, so it is unit
// testable against a plain juce::AudioProcessor and the engine/UI can wire it to
// the active PreparedGraph (NodeId -> NodeRefs.graphNode->getProcessor()) without
// this class knowing about Session or PreparedGraph. This is the seam the live
// MIDI/OSC device input plugs into to actually move a plugin's knob.
//
// Only PluginParameter targets are applied here; PatchSelect / AppCommand are the
// command dispatcher's job (a later increment).
//
// Threading: apply() sets the parameter on the calling thread via
// setValueNotifyingHost, the same call host automation uses. The caller decides
// which thread feeds it (the device-input increment marshals to the right one).
class ControlParameterApplier
{
public:
    using NodeResolver = std::function<juce::AudioProcessor*(NodeId)>;

    void setResolver(NodeResolver resolver) { resolve_ = std::move(resolver); }

    // Apply one update. Returns true iff it resolved to a real parameter that was
    // set; false if the target is not a plugin parameter, the resolver is unset,
    // the node/processor is not found, or the parameter index is out of range.
    bool apply(const control::MidiTarget& target, float value) const;

private:
    NodeResolver resolve_;
};

} // namespace pedalboard3::session
