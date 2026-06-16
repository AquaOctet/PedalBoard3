#pragma once

#include "control/MidiMapping.h"
#include "control/OscMapping.h"
#include "NodeId.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace pedalboard3::session
{

// Whether a PatchNode hosts a scanned plugin or a built-in (internal)
// processor. `Plugin` is the default so the field is additive and
// back-compatible: a node deserialized from a pre-discriminator file has no
// `kind` attribute and reads back as `Plugin`.
enum class NodeKind
{
    Plugin,  // identity is `desc` (juce::PluginDescription); instantiated via host.createInstance
    Internal // identity is `internalType`; instantiated via host.createInternalInstance
};

// One slot in a Patch — a plugin OR a built-in node, plus per-instance state
// (bypass + custom display name + the plugin's own opaque state blob).
// Phase 3 will add an (x,y) position for the canvas.
struct PatchNode
{
    NodeId id{0};

    // For kind == Plugin, `desc` is the node's identity; for kind == Internal it
    // is unused and `internalType` (e.g. "LevelProcessor") is the identity.
    NodeKind kind{NodeKind::Plugin};
    juce::PluginDescription desc;
    juce::String internalType;

    bool bypass{false};
    juce::String customName;

    // The plugin's getStateInformation() blob (knob settings, presets,
    // etc.). Empty for a freshly-added plugin, an internal node (no state
    // today), or a patch built before state capture. buildPreparedGraph
    // applies it via setStateInformation after instantiation;
    // getActivePatchWithState() captures it on save.
    juce::MemoryBlock state;
};

// Directed connection between two pins. `src == graphIO` means the
// connection comes from the graph's audio/midi input; `dst == graphIO`
// means it goes to the graph's output. Channel 0/1 is stereo audio;
// channel 0x1000 (== juce::AudioProcessorGraph::midiChannelIndex) is
// the MIDI pin.
struct PatchConnection
{
    NodeId src{0};
    int srcChannel{0};
    NodeId dst{0};
    int dstChannel{0};

    bool operator==(const PatchConnection&) const noexcept = default;
};

// Pure topology record — the *spec* the user has expressed. Distinct
// from the live AudioProcessorGraph that engine::AudioGraphRuntime
// owns. session::Session keeps a Patch alongside its PreparedGraph and
// maintains the invariant that the live graph's nodes ≡ patch.nodes
// keys.
//
// Linear-chain helpers live in MainWindow.cpp (UI side); the data type
// stays free of layout assumptions so Phase 3's drag-to-connect canvas
// can mutate `connections` directly.
struct Patch
{
    std::vector<PatchNode> nodes;
    std::vector<PatchConnection> connections;

    // Per-patch control bindings (Phase 4). Persisted alongside the topology;
    // a mapping whose target NodeId is absent from `nodes` is a harmless
    // runtime no-op (not a validation error), so they are intentionally not
    // cross-checked against the node table.
    std::vector<control::MidiMapping> midiMappings;
    std::vector<control::OscMapping> oscMappings;

    juce::String name{"Untitled"};

    // max(existing node.id) + 1, or 1 if empty. Never reuses an id
    // observed in this Patch's lifetime, even post-remove.
    NodeId nextNodeId() const noexcept;

    // Find a node by id; returns nullptr if absent.
    const PatchNode* findNode(NodeId) const noexcept;
    PatchNode* findNode(NodeId) noexcept;

    int indexOfNode(NodeId) const noexcept; // -1 if absent
};

// Reasons a Patch can fail structural validation on restore. The
// concrete reason is logged so the user has a forensics trail when a
// settings file becomes corrupt or a future-Phase-3 file is loaded by
// an older binary.
enum class PatchValidation
{
    Ok,
    DuplicateNodeId,
    DanglingConnection, // CONN references a NodeId not in nodes
    Cycle,              // connection graph has a cycle
    InvalidChannel,     // negative channel index
};

// Verify the patch is internally consistent. Used by App::restoreActivePatchIfAny
// before handing a deserialised patch to Session.
PatchValidation validatePatch(const Patch&) noexcept;

// Concatenate every PatchNode's pluginFormatName + fileOrIdentifier
// into a stable identity string suitable for hashing into the
// crash-loop marker. Order-sensitive (matches the user's chain shape).
juce::String patchIdentityString(const Patch&);

// Patch <-> XML. Produces a <PATCH> element carrying NODES (each with an
// <IDENTITY> and, when non-empty, a base64 <STATE>), CONNECTIONS, and — when
// any exist — a <MAPPINGS> child of <MIDI_MAP>/<OSC_MAP> entries (Phase 4).
// Shared by Preferences (auto-restore) and PdlSerializer (.pdl files) so
// both round-trip plugin state and control bindings identically. patchFromXml
// also accepts the legacy <ACTIVE_PATCH> tag written by pre-Phase-2 builds, and
// older files with no <MAPPINGS> load cleanly (empty mapping tables). A single
// <MIDI_MAP> with an unknown source kind is dropped, not fatal — the rest of the
// patch still loads. Returns nullptr only if the element is not a recognised
// patch root or an <IDENTITY> fails to parse. Neither validates topology — call
// validatePatch() afterwards.
std::unique_ptr<juce::XmlElement> patchToXml(const Patch&);
std::unique_ptr<Patch> patchFromXml(const juce::XmlElement&);

// Build the canonical linear-chain connection set for `nodes`: stereo audio
// + MIDI threaded graphIO -> node[0] -> ... -> node[N-1] -> graphIO. For an
// empty node list, returns the audio + MIDI passthrough. This is the
// topology the app wires for add / remove / restore; living here (rather
// than only inside Session) makes it reusable by App's saved-patch restore
// — which must rebuild connections after dropping unsafe nodes — and
// unit-testable in isolation.
std::vector<PatchConnection> linearChainConnections(const std::vector<PatchNode>& nodes);

} // namespace pedalboard3::session
