#pragma once

#include "MidiMapping.h"
#include "MidiRouter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

namespace pedalboard3::control
{

// The single app-facing entry point for incoming MIDI (Phase 4). Owns a
// MidiRouter and, in normal operation, dispatches each computed parameter
// update to a sink the host installs — the sink is what actually applies the
// value to the live graph / selects a patch / fires an app command. Also
// implements the MIDI-learn capture flow.
//
// Separating the sink from the routing keeps this whole module free of any
// audio-engine or device dependency (so it stays headless-testable): the engine
// provides a concrete sink that resolves a MidiTarget's NodeId to the hosted
// plugin and sets the parameter, and the device layer feeds handleIncomingMidi.
//
// Threading: not internally synchronised — single MIDI consumer. setMappings /
// setParameterSink / armLearn happen on the message thread while input is
// quiesced, matching how the engine swaps patches.
class MidiControlSurface
{
public:
    // Applies one routed update. Installed by the engine layer.
    using ParameterSink = std::function<void(const MidiTarget&, float)>;
    // Receives the captured source when a MIDI-learn completes.
    using LearnCallback = std::function<void(const MidiSource&)>;

    MidiControlSurface() = default;

    void setMappings(std::vector<MidiMapping> mappings) { router_.setMappings(std::move(mappings)); }
    const std::vector<MidiMapping>& mappings() const noexcept { return router_.mappings(); }

    // Install the sink that applies routed updates. A null sink means updates
    // are still computed (advancing latch state) but dropped — e.g. before the
    // engine has a live graph.
    void setParameterSink(ParameterSink sink) { sink_ = std::move(sink); }

    // Feed one incoming MIDI message. In learn mode the first *learnable*
    // message is captured (and NOT routed); otherwise the message is routed and
    // every resulting update is handed to the sink.
    void handleIncomingMidi(const juce::MidiMessage& msg);

    // MIDI learn: capture the next learnable message's source descriptor, then
    // auto-disarm and invoke `onLearned`. While armed, no message is routed.
    void armLearn(LearnCallback onLearned);

    // Disarm learn with no capture — the next CC/note routes normally and no
    // stale mapping is created (AC-IO-4).
    void cancelLearn() noexcept;

    bool isLearning() const noexcept { return learning_; }

    // Clear latch toggles (e.g. on patch switch / panic) without dropping the
    // mappings or sink.
    void reset() noexcept { router_.reset(); }

private:
    MidiRouter router_;
    ParameterSink sink_;
    LearnCallback learnCb_;
    bool learning_{false};
};

} // namespace pedalboard3::control
