#pragma once

#include "ControlHub.h"

#include <juce_events/juce_events.h>

#include <cstdint>

namespace pedalboard3::session
{

class Session;

// Feeds ControlHub from the active Session patch (roadmap M2, plan
// docs/plans/2026-07-04-001-feat-live-midi-osc-control-plan.md, KTD2). Session
// stays unaware of ControlHub — this is the one-way listener that keeps them
// in sync, so control/ never gains an upward dependency on session/'s app-level
// wiring.
//
// Binds the hub's node resolver once at construction (stable across patch
// swaps: Session::getHostedPlugin resolves by NodeId per call, not a cached
// pointer). Re-feeds MIDI/OSC mappings only when the active patch's mapping
// content could actually have changed — gated on two independent Session
// counters:
//   - getPatchGeneration(): a whole-patch swap (replacePatch/clearPatch).
//     Mappings are replaced wholesale and latch state is explicitly reset —
//     a toggle from the previous patch must not carry into the new one.
//   - getMappingsRevision(): any change to the mapping tables, including a
//     whole-patch swap. A revision-only change (e.g. MIDI learn appending
//     one mapping mid-patch, U4) re-feeds without an explicit resetLatches()
//     call. Note: ControlHub::setMidiMappings itself unconditionally resets
//     MidiRouter's latch state as a documented side effect (see
//     ControlHub.cpp), so a revision-only re-feed still clears latch toggles
//     for every mapping today — this wiring avoids adding a REDUNDANT extra
//     reset on top of that, and is positioned to stop clearing them for free
//     if MidiRouter ever grows latch-preserving re-mapping. What it reliably
//     buys today is skipping the re-feed entirely on an UNRELATED Session
//     change (e.g. a node drag), which would otherwise reset every latch on
//     every canvas interaction.
//
// Threading: message-thread only, matching ControlHub and Session's own
// contract — Session's change messages already dispatch there.
class ControlWiring : private juce::ChangeListener
{
public:
    ControlWiring(Session& session, ControlHub& hub);
    ~ControlWiring() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void feedMappings();

    Session& session_;
    ControlHub& hub_;

    std::uint64_t lastPatchGeneration_;
    std::uint64_t lastMappingsRevision_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlWiring)
};

} // namespace pedalboard3::session
