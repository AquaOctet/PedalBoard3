#pragma once

#include "control/MidiControlSurface.h" // control::MidiControlSurface (MidiRouter + learn)
#include "control/MidiMapping.h"        // control::MidiMapping / MidiTarget
#include "control/OscMapping.h"         // control::OscMapping
#include "control/OscRouter.h"          // control::OscRouter
#include "ControlParameterApplier.h"    // session::ControlParameterApplier, NodeResolver

#include <juce_audio_basics/juce_audio_basics.h> // juce::MidiMessage
#include <juce_osc/juce_osc.h>                   // juce::OSCMessage

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace pedalboard3::session
{

// The single message-thread entry point the app feeds control input into. Owns
// the MIDI control surface (MidiRouter + learn), the OSC router, and the
// ControlParameterApplier, and funnels every computed update through one applier
// whose NodeResolver is bound to the active live graph. The net effect: a
// juce::MidiMessage or juce::OSCMessage in moves a real graph node's plugin
// parameter out.
//
// The actual MIDI device callback, OSC socket bind, and mapping UI are out of
// scope; handleMidi()/handleOsc() are the seam they plug into.
//
// Threading: message-thread only. setNodeResolver / setMidiMappings /
// setOscMappings / handleMidi / handleOsc / armMidiLearn / cancelMidiLearn /
// resetLatches must all run on the JUCE message thread. The composed
// routers/surface/applier are not internally synchronised (single consumer;
// mappings/resolver swapped while input is quiesced), and the resolver typically
// reaches into Session under its status lock — so a real MIDI/OSC device thread
// must marshal onto the message thread (juce::MessageManager::callAsync or a
// lock-free queue drained on a timer) rather than calling these directly.
class ControlHub
{
public:
    using NodeResolver = ControlParameterApplier::NodeResolver;       // std::function<juce::AudioProcessor*(NodeId)>
    using LearnCallback = control::MidiControlSurface::LearnCallback; // std::function<void(const MidiSource&)>
    // Receives PatchSelect / AppCommand updates the applier does not apply to a
    // plugin parameter, whole (with the press flag), so a command can fire on
    // a press only. session::PatchSwitcher installs it.
    using UnhandledTargetSink = std::function<void(const control::ParameterUpdate&)>;

    ControlHub();
    ~ControlHub() = default;

    // The MIDI sink and (in tests) resolvers capture `this`, so the hub must not
    // be copied or moved.
    ControlHub(const ControlHub&) = delete;
    ControlHub& operator=(const ControlHub&) = delete;
    ControlHub(ControlHub&&) = delete;
    ControlHub& operator=(ControlHub&&) = delete;

    // Bind the applier to the live graph (the one seam the app drives): resolves
    // a target's NodeId to the hosted processor whose parameter will be set.
    void setNodeResolver(NodeResolver resolver);

    void setMidiMappings(std::vector<control::MidiMapping> mappings);
    void setOscMappings(std::vector<control::OscMapping> mappings);
    const std::vector<control::MidiMapping>& midiMappings() const noexcept;
    const std::vector<control::OscMapping>& oscMappings() const noexcept;

    // Input entry points — the seam for the device / socket layer.
    void handleMidi(const juce::MidiMessage& msg);

    // Runs the OSC mappings, and — when the message reads as MIDI bytes
    // (control::midiFromOsc) — the MIDI path too, with the same mappings, the
    // same latches and the same learn capture a DIN cable would get (#97,
    // AC-IO-6). Both, not one or the other: see the comment on the definition.
    //
    // Two MIDI behaviours are deliberately NOT reached this way, because they
    // belong to session::PatchSwitcher, which sits on the MIDI *device* drain
    // rather than here: MIDI Machine Control, and the default rule that a
    // Program Change selects the bank slot of that number. An explicit
    // PatchSelect or AppCommand mapping does work over OSC, because that runs
    // through the mapping tables.
    void handleOsc(const juce::OSCMessage& msg);

    // MIDI learn (delegates to the surface flow). OSC has no learn/suppression
    // and keeps routing even while MIDI learn is armed.
    void armMidiLearn(LearnCallback onLearned);
    void cancelMidiLearn() noexcept;
    bool isLearning() const noexcept;

    void setUnhandledTargetSink(UnhandledTargetSink sink);

    // Clear MIDI latch toggles (patch swap / panic) without dropping mappings or
    // the resolver.
    void resetLatches() noexcept;

    // Activity counters for the UI's lights (UI refresh U3, plan KD8): every
    // MIDI or OSC message in, mapped or not, and per node each parameter a
    // mapped control actually set. Message-thread only like the rest of the
    // hub; the UI polls them on a timer. Node counts clear on a mapping
    // re-feed, because after a whole-patch swap an id can mean another plugin.
    std::uint64_t midiActivity() const noexcept { return midiActivity_; }
    std::uint64_t oscActivity() const noexcept { return oscActivity_; }
    std::uint64_t nodeActivity(NodeId node) const noexcept;

private:
    // Single fan-in for both MIDI and OSC updates. Drops non-finite values before
    // they can reach setValueNotifyingHost; routes non-parameter targets to the
    // unhandled sink.
    void applyUpdate(const control::ParameterUpdate& update);

    ControlParameterApplier applier_;
    control::MidiControlSurface midiSurface_; // owns MidiRouter + learn
    control::OscRouter oscRouter_;
    UnhandledTargetSink unhandled_;

    std::uint64_t midiActivity_{0};
    std::uint64_t oscActivity_{0};
    std::unordered_map<NodeId, std::uint64_t> nodeActivity_;
};

} // namespace pedalboard3::session
