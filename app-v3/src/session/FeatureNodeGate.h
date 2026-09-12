#pragma once

#include <juce_events/juce_events.h>

namespace pedalboard3::session
{

class FeatureGate;
class Session;

// Switches the board's Metronome and Looper nodes to pass-through while their
// feature isn't available (plan docs/plans/2026-09-12-002-feat-optional-features-plan.md,
// KD6). A switched-off node passes its input through untouched. That's a
// runtime flag on the processor (ProcessorBase::setFeatureOff), never saved,
// and not the node's bypass, so nothing in the board changes.
//
// It applies again whenever the features or the live patch change, so a node a
// rebuild creates is caught as soon as the patch settles.
//
// Message thread only.
class FeatureNodeGate : private juce::ChangeListener
{
public:
    FeatureNodeGate(FeatureGate& features, Session& session);
    ~FeatureNodeGate() override;

    void apply();

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { apply(); }

    FeatureGate& features_;
    Session& session_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeatureNodeGate)
};

} // namespace pedalboard3::session
