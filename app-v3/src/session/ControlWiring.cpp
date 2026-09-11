#include "ControlWiring.h"

#include "AppMappings.h"
#include "Session.h"

namespace pedalboard3::session
{

ControlWiring::ControlWiring(Session& session, ControlHub& hub, AppMappings* appMappings)
    : session_(session), hub_(hub), appMappings_(appMappings), lastPatchGeneration_(session.getPatchGeneration()),
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
    if (appMappings_ != nullptr)
        appMappings_->addChangeListener(this);
}

ControlWiring::~ControlWiring()
{
    if (appMappings_ != nullptr)
        appMappings_->removeChangeListener(this);
    session_.removeChangeListener(this);
}

void ControlWiring::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (appMappings_ != nullptr && source == appMappings_)
    {
        feedMappings(); // the app half changed; the patch did not
        return;
    }

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
    auto patch = session_.getActivePatch();
    auto midi = std::move(patch.midiMappings);
    auto osc = std::move(patch.oscMappings);
    if (appMappings_ != nullptr)
    {
        midi.insert(midi.end(), appMappings_->midi().begin(), appMappings_->midi().end());
        osc.insert(osc.end(), appMappings_->osc().begin(), appMappings_->osc().end());
    }
    hub_.setMidiMappings(std::move(midi));
    hub_.setOscMappings(std::move(osc));
}

} // namespace pedalboard3::session
