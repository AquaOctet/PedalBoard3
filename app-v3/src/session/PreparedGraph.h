#pragma once

#include "NodeId.h"
#include "Patch.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <unordered_map>

namespace pedalboard3::engine
{
class BypassableInstance;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{

// Result of a worker-thread-safe graph build for a multi-plugin Patch.
// `graph` owns every node (the audio/midi I/O processors and one
// BypassableInstance per PatchNode). `nodes` maps each PatchNode's
// NodeId to non-owning observers used by the message-thread side
// (Session, MainWindow) to drive bypass / open the editor / route
// incremental mutations without re-walking the graph.
//
// PreparedGraph deliberately does NOT store its own copy of the Patch.
// Session is the single source of truth for the active Patch; the
// invariant "graph nodes ≡ activePatch_.nodes ids" is maintained by
// Session on every mutation.
//
// `graphIO` (NodeId 0) is reserved for the audio/midi I/O endpoints
// (PatchConnection's `src == graphIO` or `dst == graphIO`); it is NOT
// in the `nodes` map.
//
// Empty (graph == nullptr) means the build failed.
struct PreparedGraph
{
    std::unique_ptr<juce::AudioProcessorGraph> graph;

    struct NodeRefs
    {
        engine::BypassableInstance* bypass{nullptr};
        // The processor inside the BypassableInstance. juce::AudioProcessor (not
        // AudioPluginInstance) so it covers both hosted plugins and built-in
        // internal nodes; consumers that want plugin-only APIs must dynamic_cast.
        juce::AudioProcessor* hosted{nullptr};
        juce::AudioProcessorGraph::Node* graphNode{nullptr};
    };
    std::unordered_map<NodeId, NodeRefs> nodes;

    bool isValid() const noexcept { return graph != nullptr; }
};

// Worker-thread-safe builder. Iterates `patch.nodes`, instantiates each
// plugin via `host.createInstance`, wraps in BypassableInstance,
// addNode, then iterates `patch.connections` and addConnection per the
// patch's topology. Adds audio in/out + midi in/out IO processors so
// PatchConnections that reference `graphIO` resolve to the runtime's
// hardware endpoints. Single graph->prepareToPlay at the end.
//
// Empty patch builds an audio-passthrough graph (input → output direct
// per stereo channel) plus a MIDI passthrough (midiInputNode →
// midiOutputNode). Matches Pedalboard 2's empty-FILTERGRAPH behaviour.
//
// On any plugin instantiation failure, returns an empty PreparedGraph
// (graph == nullptr) and `errorOut` carries a "<plugin name>: <reason>"
// line for the offending node.
//
// Does NOT touch any AudioEngine, PluginSlot, or Session state. Safe to
// call from a worker thread. The graph is fully prepared (prepareToPlay
// run) before return; the caller commits it to engine::AudioGraphRuntime
// from the message thread via swapPreparedGraph or by direct ownership.
PreparedGraph buildPreparedGraph(
    engine::PluginHost& host, const Patch& patch, double sampleRate, int blockSize, juce::String& errorOut);

// True if `node` can be safely materialized into a live chain: a plugin via
// PluginHost::isDescriptorSafeForChain (drops instruments / blacklisted files);
// an internal node iff its `internalType` is a recognized built-in. An
// unrecognized internal type (e.g. a newer schema's node opened by an older
// binary) returns false, so restore/open paths DROP it instead of letting
// buildPreparedGraph abort the whole patch. Message-thread only.
bool isNodeSafeForChain(const engine::PluginHost& host, const PatchNode& node);

} // namespace pedalboard3::session
