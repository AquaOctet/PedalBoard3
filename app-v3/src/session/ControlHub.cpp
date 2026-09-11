#include "ControlHub.h"

#include <cmath>
#include <utility>

namespace pedalboard3::session
{

ControlHub::ControlHub()
{
    // Wire the MIDI surface's sink into the single apply path. Installed once;
    // the lambda captures `this`, which is why the hub is non-movable.
    midiSurface_.setParameterSink([this](const control::ParameterUpdate& update) { applyUpdate(update); });
}

void ControlHub::setNodeResolver(NodeResolver resolver)
{
    applier_.setResolver(std::move(resolver));
}

void ControlHub::setMidiMappings(std::vector<control::MidiMapping> mappings)
{
    midiSurface_.setMappings(std::move(mappings)); // also resets MidiRouter latch state
}

void ControlHub::setOscMappings(std::vector<control::OscMapping> mappings)
{
    oscRouter_.setMappings(std::move(mappings));
}

const std::vector<control::MidiMapping>& ControlHub::midiMappings() const noexcept
{
    return midiSurface_.mappings();
}

const std::vector<control::OscMapping>& ControlHub::oscMappings() const noexcept
{
    return oscRouter_.mappings();
}

void ControlHub::handleMidi(const juce::MidiMessage& msg)
{
    // Reuses the surface's learn capture/suppression: while armed it captures the
    // first learnable message and does not route, so nothing reaches the applier.
    midiSurface_.handleIncomingMidi(msg);
}

void ControlHub::handleOsc(const juce::OSCMessage& msg)
{
    // OSC has no learn flow; every matching mapping routes immediately, even
    // while MIDI learn is armed (the two surfaces are independent).
    for (const auto& update : oscRouter_.process(msg))
        applyUpdate(update);
}

void ControlHub::armMidiLearn(LearnCallback onLearned)
{
    midiSurface_.armLearn(std::move(onLearned));
}

void ControlHub::cancelMidiLearn() noexcept
{
    midiSurface_.cancelLearn();
}

bool ControlHub::isLearning() const noexcept
{
    return midiSurface_.isLearning();
}

void ControlHub::setUnhandledTargetSink(UnhandledTargetSink sink)
{
    unhandled_ = std::move(sink);
}

void ControlHub::resetLatches() noexcept
{
    midiSurface_.reset();
}

void ControlHub::applyUpdate(const control::ParameterUpdate& update)
{
    if (update.target.kind == control::MidiTargetKind::PluginParameter)
    {
        // Drop NaN/inf before it can reach setValueNotifyingHost (jlimit does not
        // sanitise non-finite input). OSC arguments are the realistic source.
        if (!std::isfinite(update.value))
            return;
        applier_.apply(update.target, update.value); // false on no-resolver / unknown-node / bad-index — ignored
    }
    else if (unhandled_)
    {
        unhandled_(update); // PatchSelect / AppCommand seam
    }
}

} // namespace pedalboard3::session
