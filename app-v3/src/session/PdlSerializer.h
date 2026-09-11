#pragma once

#include "Patch.h"
#include "PatchBank.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace pedalboard3::session::pdl
{

// Read/write Pedalboard 3 .pdl files (master plan §4.5). From schemaMinor 4 a
// .pdl holds a BANK of patches (roadmap M3) in a versioned <PEDALBOARD> root:
//
//   <PEDALBOARD schemaMajor="3" schemaMinor="5" appVersion="1.0.0">
//     <BANK activeIndex="0">
//       <PATCH name="..."> <NODES/> <CONNECTIONS/> <MAPPINGS/> </PATCH>
//       <PATCH .../>
//     </BANK>
//   </PEDALBOARD>
//
// v3.x readers accept schemaMajor == 3 regardless of schemaMinor. A file
// with schemaMajor > 3 was written by a newer app and is rejected with a
// friendly error (Phase 2 MVP: no read-only fallback yet; the AC-FS-7
// preserve-unknown round-trip is deferred). schemaMinor 1 added the optional
// per-patch <MAPPINGS> (MIDI/OSC control bindings); schemaMinor 2 added the
// per-<NODE> kind="internal" type="..." discriminator for built-in nodes;
// schemaMinor 3 added optional per-<NODE> x/y canvas positions; schemaMinor 4
// wraps the patches in <BANK>. A schemaMinor <= 3 file carries a bare <PATCH>
// directly under the root and reads as a one-patch bank with activeIndex 0,
// so older files load unchanged. Writers always emit the <BANK> form, even
// for a single patch. Application mappings and per-patch preferences remain
// out of scope.

inline constexpr int kSchemaMajor = 3;
// schemaMinor 5 (roadmap M4) adds the optional per-<NODE> midiChannel filter
// attribute; a node without it is omni, so schemaMinor 4 files load unchanged.
inline constexpr int kSchemaMinor = 5;

// Atomically write `bank` to `file` (temp file in the same directory +
// rename, so a crash mid-write never corrupts an existing .pdl). Creates
// the parent directory if needed. Returns a failed juce::Result with a
// user-presentable message on any error.
juce::Result writeBank(const PatchBank& bank, const juce::File& file);

// Convenience: write a one-patch bank (activeIndex 0).
juce::Result writePatch(const Patch& patch, const juce::File& file);

struct BankLoadResult
{
    std::unique_ptr<PatchBank> bank; // null on failure
    juce::String error;              // empty on success
};

struct LoadResult
{
    std::unique_ptr<Patch> patch; // null on failure
    juce::String error;           // empty on success
};

// Parse a .pdl into a bank. Every patch is run through validatePatch; an
// invalid one fails the whole load and names its 1-based position. An
// out-of-range activeIndex is clamped, not fatal. On any failure — unreadable,
// not a <PEDALBOARD>, schemaMajor > 3, no patches, malformed patch — returns a
// null bank and a user-presentable error string.
BankLoadResult readBank(const juce::File& file);

// Convenience: readBank, then the bank's active patch.
LoadResult readPatch(const juce::File& file);

} // namespace pedalboard3::session::pdl
