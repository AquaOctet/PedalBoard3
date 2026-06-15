#pragma once

#include "../Patch.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace pedalboard3::session::v2
{

// Result of importing a Pedalboard2 patch. `patch` is null on failure (see
// `error`); `warnings` collects non-fatal issues (skipped nodes, dropped
// connections, multi-patch files, dropped plugin state) to surface to the user.
struct ImportResult
{
    std::unique_ptr<Patch> patch;
    juce::StringArray warnings;
    juce::String error;
};

// True if `root` is a Pedalboard2 patch file (root tag <Pedalboard2PatchFile>).
bool isPedalboard2Patch(const juce::XmlElement& root);

// Parse a Pedalboard2 <Pedalboard2PatchFile> into a v3 Patch (master plan
// §4.2.B.1 + Appendix D). PURE DATA TRANSFORM — it does NOT resolve plugins
// against the host's installed plugin list (the caller matches by identity and
// drops/substitutes unmatched nodes) and does NOT validate topology (call
// validatePatch afterwards).
//
// Mapping: each real <FILTER>/<PLUGIN> becomes a PatchNode carrying the v2
// plugin identity (name, manufacturer, format, file, uid). The internal
// "Audio Input"/"Audio Output" (and MIDI I/O) FILTERs become the graphIO
// endpoint. <CONNECTION>s are translated (v2's magic MIDI channel 0x1000 ==
// juce::AudioProcessorGraph::midiChannelIndex, so it carries over unchanged).
// Nodes are returned in signal-chain order (walked from the input endpoint).
//
// Plugin STATE is intentionally NOT migrated: v2 stores VST2 state, and a
// faithful replacement matches to the installed VST3/AU build whose state
// format is incompatible (master plan: "lossy-with-warning on plugin state").
// A warning is emitted per node that had state.
ImportResult importPedalboard2Patch(const juce::XmlElement& root);

} // namespace pedalboard3::session::v2
