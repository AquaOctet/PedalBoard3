#pragma once

#include "session/NodeId.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <optional>

// Phase 4 control surface — the MIDI mapping data model and its pure
// value-transform logic (master plan §4.2.D, AC-IO-4/5/8/9).
//
// This header is deliberately free of any audio-engine, device-I/O, or UI
// dependency: it describes WHAT a mapping is and computes the parameter value
// an incoming MIDI message implies. MidiRouter (same module) owns the live
// mapping table + latch state; wiring the produced updates onto the running
// graph and onto a MIDI device callback is a later increment. Keeping the
// transform pure makes every range/invert/latch/channel-filter rule unit
// testable headless, which is the only way we can validate it from the Linux
// dev box (real controllers only exist on the user's Windows machine).
namespace pedalboard3::control
{

// The class of incoming MIDI event a mapping listens for.
enum class MidiSourceKind
{
    ControlChange, // CC number `controller`, 7-bit value 0..127
    Note,          // note `controller`; note-on drives it (velocity or latch)
    ProgramChange, // program number is the value; `controller` ignored
    PitchBend      // 14-bit value 0..16383; `controller` ignored
};

// Matches an incoming MIDI message. `channel == 0` is omni (any channel);
// 1..16 selects a specific channel (juce MIDI channels are 1-based). For
// ControlChange/Note, `controller` is the CC/note number; it is ignored for
// ProgramChange/PitchBend (which have no sub-address).
struct MidiSource
{
    MidiSourceKind kind{MidiSourceKind::ControlChange};
    int channel{0};
    int controller{0};

    bool operator==(const MidiSource&) const noexcept = default;
};

// What a mapping drives. v3 starts with plugin-parameter targets; PatchSelect
// and AppCommand are reserved so the model + persistence are forward-compatible
// (their routing lands with the command dispatcher in a later increment).
enum class MidiTargetKind
{
    PluginParameter, // `node` + `parameterIndex`
    PatchSelect,     // `patchIndex`
    AppCommand       // `commandId`
};

struct MidiTarget
{
    MidiTargetKind kind{MidiTargetKind::PluginParameter};
    session::NodeId node{0}; // PluginParameter: which node in the active patch
    int parameterIndex{0};   // PluginParameter: the plugin's parameter index
    int patchIndex{0};       // PatchSelect: 0-based patch index
    juce::String commandId;  // AppCommand: dispatcher command identifier

    bool operator==(const MidiTarget&) const noexcept = default;
};

// A computed change a router emits: set `target` to `value` (PluginParameter
// values are normalized 0..1). Shared by the MIDI and OSC routers so the engine
// sink has one update type to apply regardless of the control source.
struct ParameterUpdate
{
    MidiTarget target;
    float value{0.0f};
};

// One source → target binding with value shaping.
//
//   rangeMin/rangeMax  normalized 0..1 parameter span the source maps onto;
//                      a controller's 0..127 (or PB's 0..16383) maps linearly
//                      onto [rangeMin, rangeMax]. min>max is allowed and is an
//                      alternative way to invert.
//   inverted           flips the source value before scaling (AC-IO-8). Applied
//                      on top of the range, so it composes with min>max.
//   latch              momentary→toggle: each qualifying press flips the target
//                      between rangeMin and rangeMax instead of tracking the
//                      live value (AC-IO-5). For CC the press is value>=64; for
//                      Note it is a note-on with velocity>0.
struct MidiMapping
{
    MidiSource source;
    MidiTarget target;
    float rangeMin{0.0f};
    float rangeMax{1.0f};
    bool inverted{false};
    bool latch{false};

    bool operator==(const MidiMapping&) const noexcept = default;
};

// True when `msg` is the kind/channel/controller this source listens for.
// Does not consider value — a CC mapping matches every value of its CC, a Note
// mapping matches both note-on and note-off for its note (the caller/transform
// decides what a note-off means). PitchBend/ProgramChange match on kind+channel.
bool matches(const MidiSource& source, const juce::MidiMessage& msg) noexcept;

// The raw source value normalized to 0..1, BEFORE range/invert, for a message
// already known to match this kind:
//   ControlChange → value/127      Note → velocity/127 (0 for note-off)
//   ProgramChange → number/127     PitchBend → value/16383
// Returns nullopt when `msg` carries no usable value for `kind` (e.g. a Note
// source handed a non-note message).
std::optional<float> rawNormalized(MidiSourceKind kind, const juce::MidiMessage& msg) noexcept;

// Low-level value shaper shared by MIDI and OSC: clamp `normalizedSource` to
// 0..1, optionally invert, then linearly map onto [rangeMin, rangeMax]
// (rangeMin > rangeMax is allowed and inverts). Pure.
float scaleToRange(float normalizedSource, float rangeMin, float rangeMax, bool inverted) noexcept;

// Map a normalized-0..1 source value through invert + [rangeMin,rangeMax].
// Pure; clamps the input to 0..1 first. This is the continuous (non-latch)
// transform. Thin wrapper over scaleToRange using the mapping's fields.
float applyRange(const MidiMapping& mapping, float normalizedSource) noexcept;

// Build the MidiSource a MIDI-learn flow would capture from `msg`: the kind +
// channel (the message's actual 1..16 channel, not omni) + controller/note that
// identifies this control. Returns nullopt for messages that don't map to a
// learnable source (note-off, aftertouch, clock, transport, etc.) — a learn
// captures the press, not the release, so note-ON is learnable and note-OFF is
// not.
std::optional<MidiSource> sourceFromMessage(const juce::MidiMessage& msg) noexcept;

// Stable string encodings for the mapping enums, used by .pdl persistence so
// files stay human-readable and forward-stable: a reordered or newly-added
// enumerator can never silently re-interpret an old value. The strings are
// frozen — never rename or reorder them; a new enumerator only appends a new
// string. fromString is an exact, case-sensitive match; unknown/empty -> nullopt.
juce::String midiSourceKindToString(MidiSourceKind kind) noexcept;
std::optional<MidiSourceKind> midiSourceKindFromString(const juce::String& s) noexcept;
juce::String midiTargetKindToString(MidiTargetKind kind) noexcept;
std::optional<MidiTargetKind> midiTargetKindFromString(const juce::String& s) noexcept;

} // namespace pedalboard3::control
