#pragma once

#include "Patch.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace pedalboard3::session::pdl
{

// Read/write Pedalboard 3 .pdl files (master plan §4.5). A .pdl wraps a
// single <PATCH> (session/Patch.cpp) in a versioned <PEDALBOARD> root:
//
//   <PEDALBOARD schemaMajor="3" schemaMinor="1" appVersion="3.0.0">
//     <PATCH name="..."> <NODES/> <CONNECTIONS/> <MAPPINGS/> </PATCH>
//   </PEDALBOARD>
//
// v3.x readers accept schemaMajor == 3 regardless of schemaMinor. A file
// with schemaMajor > 3 was written by a newer app and is rejected with a
// friendly error (Phase 2 MVP: no read-only fallback yet; the AC-FS-7
// preserve-unknown round-trip is deferred). schemaMinor 1 added the optional
// per-patch <MAPPINGS> (MIDI/OSC control bindings); schemaMinor 2 adds the
// per-<NODE> kind="internal" type="..." discriminator for built-in nodes
// (a <NODE> with no `kind` attribute is a plugin, so older files load
// unchanged). Application mappings and per-patch preferences remain out of scope.

inline constexpr int kSchemaMajor = 3;
inline constexpr int kSchemaMinor = 2;

// Atomically write `patch` to `file` (temp file in the same directory +
// rename, so a crash mid-write never corrupts an existing .pdl). Creates
// the parent directory if needed. Returns a failed juce::Result with a
// user-presentable message on any error.
juce::Result writePatch(const Patch& patch, const juce::File& file);

struct LoadResult
{
    std::unique_ptr<Patch> patch; // null on failure
    juce::String error;           // empty on success
};

// Parse a .pdl. On success returns a structurally-valid Patch (already
// run through validatePatch). On any failure — unreadable, not a
// <PEDALBOARD>, schemaMajor > 3, malformed/inconsistent patch — returns a
// null patch and a user-presentable error string.
LoadResult readPatch(const juce::File& file);

} // namespace pedalboard3::session::pdl
