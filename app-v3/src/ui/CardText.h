#pragma once

#include "BrowserModel.h" // browser::chipForCategory
#include "session/Session.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

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

// The card's title: its custom name, else the plugin's; "Missing: <name>" while
// the plugin is a pass-through placeholder (M5 U3).
inline juce::String titleFor(const session::Session::NodeSnapshot& node)
{
    const auto name = node.customName.isNotEmpty() ? node.customName : node.name;
    return node.isMissing() ? "Missing: " + name : name;
}

inline Lines linesFor(const session::Session::NodeSnapshot& node, int mappingCount)
{
    Lines lines;
    if (node.isMissing())
        lines.kind = node.missingReason; // why, and how to fix it (M5 U3)
    else
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

// The Metronome card's readout (optional-features U5): "4/4 · synced" while it
// follows the transport, or its own count's "3/4 · 96 BPM" (a whole tempo
// without decimals).
inline juce::String metronomeReadout(int beats, int noteValue, bool synced, double bpm)
{
    const auto signature = juce::String(beats) + "/" + juce::String(noteValue);
    if (synced)
        return signature + kDot + "synced";
    const double rounded = std::round(bpm * 10.0) / 10.0;
    const bool whole = juce::approximatelyEqual(rounded, std::round(rounded));
    return signature + kDot + (whole ? juce::String(juce::roundToInt(rounded)) : juce::String(rounded, 1)) + " BPM";
}

// The Looper card's status tag (optional-features U5): REC while recording,
// PLAY while a loop plays, a dash otherwise.
inline juce::String looperStatus(bool recording, bool playing, bool hasLoop)
{
    if (recording)
        return "REC";
    if (playing && hasLoop)
        return "PLAY";
    return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));
}

} // namespace pedalboard3::ui::card
