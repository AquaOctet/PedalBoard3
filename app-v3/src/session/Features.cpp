#include "Features.h"

#include "PatchBank.h"

#include <algorithm>

namespace pedalboard3::session
{

namespace
{

bool hasNodeOfType(const PatchBank& bank, const juce::String& internalType)
{
    return std::any_of(bank.patches.begin(),
                       bank.patches.end(),
                       [&internalType](const Patch& patch)
                       {
                           return std::any_of(patch.nodes.begin(),
                                              patch.nodes.end(),
                                              [&internalType](const PatchNode& node)
                                              { return node.internalType == internalType; });
                       });
}

} // namespace

const char* featureKey(Feature feature) noexcept
{
    switch (feature)
    {
    case Feature::Tempo:
        return "tempo";
    case Feature::Metronome:
        return "metronome";
    case Feature::Looper:
        return "looper";
    case Feature::Midi:
        return "midi";
    case Feature::Osc:
        return "osc";
    }
    return "";
}

juce::String featureName(Feature feature)
{
    switch (feature)
    {
    case Feature::Tempo:
        return "Tempo / BPM";
    case Feature::Metronome:
        return "Metronome";
    case Feature::Looper:
        return "Looper";
    case Feature::Midi:
        return "MIDI";
    case Feature::Osc:
        return "OSC";
    }
    return {};
}

bool enabledByDefault(Feature feature) noexcept
{
    return feature == Feature::Midi;
}

bool boardUses(const PatchBank& bank, Feature feature)
{
    switch (feature)
    {
    case Feature::Tempo:
    {
        const Patch defaults;
        return std::any_of(bank.patches.begin(),
                           bank.patches.end(),
                           [&defaults](const Patch& patch)
                           {
                               return !juce::approximatelyEqual(patch.tempo, defaults.tempo) ||
                                      patch.timeSigNumerator != defaults.timeSigNumerator ||
                                      patch.timeSigDenominator != defaults.timeSigDenominator;
                           });
    }
    case Feature::Metronome:
        return hasNodeOfType(bank, "MetronomeProcessor");
    case Feature::Looper:
        return hasNodeOfType(bank, "LooperProcessor");
    case Feature::Midi:
        return std::any_of(
            bank.patches.begin(), bank.patches.end(), [](const Patch& patch) { return !patch.midiMappings.empty(); });
    case Feature::Osc:
        return std::any_of(
            bank.patches.begin(), bank.patches.end(), [](const Patch& patch) { return !patch.oscMappings.empty(); });
    }
    return false;
}

} // namespace pedalboard3::session
