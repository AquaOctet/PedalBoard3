#pragma once

#include "NodeId.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace pedalboard3::session
{

// One slot in a Patch — a plugin description plus per-instance UI state
// (bypass + custom display name). Phase 2 will add a state blob and an
// (x,y) position for the canvas.
struct PatchNode
{
    NodeId id{0};
    juce::PluginDescription desc;
    bool bypass{false};
    juce::String customName;
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

} // namespace pedalboard3::session
