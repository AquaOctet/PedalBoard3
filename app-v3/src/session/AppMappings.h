#pragma once

#include "control/MidiMapping.h"
#include "control/OscMapping.h"

#include <juce_events/juce_events.h>

#include <memory>
#include <vector>

namespace pedalboard3::session
{

// App-level MIDI and OSC mappings (roadmap M4 U3, plan
// docs/plans/2026-09-11-002-feat-mappings-ui-plan.md, KD5 / KTD5): bindings
// that work in every patch of the bank, typically AppCommand targets
// ("patch.next"), though any target kind is accepted. PreferencesPersister
// writes them to Preferences on every change; ControlWiring feeds them to
// ControlHub together with the active patch's mappings, so a patch switch
// never drops them.
//
// Broadcasts a change message after every mutation that changes a table.
// Message-thread only.
class AppMappings : public juce::ChangeBroadcaster
{
public:
    const std::vector<control::MidiMapping>& midi() const noexcept { return midi_; }
    const std::vector<control::OscMapping>& osc() const noexcept { return osc_; }

    // Replace a table. Setting an identical table is a no-op (no broadcast),
    // so re-applying unchanged mappings doesn't reset the hub's latch state.
    void setMidi(std::vector<control::MidiMapping> midi);
    void setOsc(std::vector<control::OscMapping> osc);

    // <APP_MAPPINGS> holding the same <MAPPINGS> element a patch uses.
    std::unique_ptr<juce::XmlElement> toXml() const;

    // Replace both tables from toXml()'s output, without broadcasting (this
    // is the launch-time restore, before any listener attaches). Returns
    // false, changing nothing, when `xml` is not an <APP_MAPPINGS> element.
    bool restoreFromXml(const juce::XmlElement& xml);

private:
    std::vector<control::MidiMapping> midi_;
    std::vector<control::OscMapping> osc_;
};

} // namespace pedalboard3::session
