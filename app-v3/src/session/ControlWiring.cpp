#include "ControlWiring.h"

#include "Session.h"

namespace pedalboard3::session
{

ControlWiring::ControlWiring(Session& session, ControlHub& hub)
    : session_(session), hub_(hub), lastPatchGeneration_(session.getPatchGeneration()),
      lastMappingsRevision_(session.getMappingsRevision())
{
    // Resolve by NodeId per call (session.getHostedPlugin), so this stays
    // correct across every future patch swap without rebinding.
    hub_.setNodeResolver([&session](NodeId id) { return session.getHostedPlugin(id); });

    // Feed once at construction: a patch restored/loaded synchronously during
    // App::initialise (before Session's first change message reaches us) must
    // still be live.
    feedMappings();

    session_.addChangeListener(this);
}

ControlWiring::~ControlWiring()
{
    session_.removeChangeListener(this);
}

void ControlWiring::changeListenerCallback(juce::ChangeBroadcaster*)
{
    const auto generation = session_.getPatchGeneration();
    const auto revision = session_.getMappingsRevision();

    const bool generationChanged = generation != lastPatchGeneration_;
    const bool revisionChanged = revision != lastMappingsRevision_;

    if (!generationChanged && !revisionChanged)
        return; // unrelated Session change (e.g. a node drag) — don't touch the hub

    lastPatchGeneration_ = generation;
    lastMappingsRevision_ = revision;

    feedMappings();

    // Explicit latch reset on a whole-patch swap. Redundant with
    // setMidiMappings' own internal reset today (see ControlHub.cpp), but
    // kept so the intent — "a new patch never carries over a stuck toggle"
    // — reads as a decision here, not an incidental side effect of the
    // mapping call.
    if (generationChanged)
        hub_.resetLatches();
}

void ControlWiring::feedMappings()
{
    const auto patch = session_.getActivePatch();
    hub_.setMidiMappings(patch.midiMappings);
    hub_.setOscMappings(patch.oscMappings);
}

} // namespace pedalboard3::session
