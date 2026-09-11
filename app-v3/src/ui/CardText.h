#pragma once

#include "BrowserModel.h" // browser::chipForCategory
#include "session/Session.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace pedalboard3::ui::card
{

// What a plugin card says (UI refresh U7), free of components so the headless
// tests can cover it.

inline const juce::String kDot{juce::CharPointer_UTF8(" \xc2\xb7 ")};

struct Lines
{
    juce::String kind; // "EQ", "Built-in"; with " · bypassed" while bypassed
    juce::String meta; // "VST3 · FabFilter", or the mapping count in place of the vendor
};

inline Lines linesFor(const session::Session::NodeSnapshot& node, int mappingCount)
{
    Lines lines;
    lines.kind = node.builtIn ? juce::String("Built-in") : browser::chipForCategory(node.category);
    if (node.bypassed)
        lines.kind += kDot + "bypassed";

    juce::StringArray meta;
    if (node.format.isNotEmpty())
        meta.add(node.format);
    if (mappingCount > 0)
        meta.add(juce::String(mappingCount) + (mappingCount == 1 ? " mapping" : " mappings"));
    else if (node.vendor.isNotEmpty())
        meta.add(node.vendor);
    lines.meta = meta.joinIntoString(kDot);
    return lines;
}

// The VU Meter card's scale: -60 dBFS to 0 dBFS.
inline constexpr float kMeterFloorDb = -60.0f;

// How much of a meter bar a linear peak level fills.
inline float meterFraction(float level)
{
    const float db = juce::Decibels::gainToDecibels(level, kMeterFloorDb);
    return juce::jlimit(0.0f, 1.0f, (db - kMeterFloorDb) / -kMeterFloorDb);
}

// A linear peak level as the card's dBFS readout: "-12.4", or "-inf" at the floor.
inline juce::String dbfsText(float level)
{
    const float db = juce::Decibels::gainToDecibels(level, kMeterFloorDb);
    return db <= kMeterFloorDb ? juce::String("-inf") : juce::String(db, 1);
}

} // namespace pedalboard3::ui::card
