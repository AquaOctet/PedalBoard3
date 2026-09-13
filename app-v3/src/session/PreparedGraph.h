#pragma once

#include "NodeId.h"
#include "Patch.h"
#include "PatchBank.h"

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

    // The plugin nodes built as pass-through placeholders, and why (M5 U3).
    std::unordered_map<NodeId, juce::String> missing;

    bool isValid() const noexcept { return graph != nullptr; }
};

// Why a plugin node is a pass-through placeholder (M5 U3, plan KD4), as its
// card says it.
namespace missingreason
{
inline juce::String notFound()
{
    return "Not found: install it, or scan for plugins";
}
inline juce::String instrument()
{
    return "Instruments aren't supported yet";
}
inline juce::String blocked()
{
    return "Blocked: it crashed a plugin scan";
}
inline juce::String failedToLoad(const juce::String& why)
{
    return why.isNotEmpty() ? "Couldn't load: " + why : juce::String("Couldn't load");
}
inline juce::String settingsFailed()
{
    return "Its saved settings wouldn't load";
}
inline juce::String crashedLoading(const juce::String& fault)
{
    return "It crashed loading its settings (" + fault + ")";
}
} // namespace missingreason

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
// A plugin node that's missing (PatchNode::missingReason, from
// resolvePluginNodes), fails to load, or throws on its saved state becomes a
// pass-through placeholder, listed in `missing`, and the rest of the graph
// still builds (M5 U3, plan KD4). A built-in of an unknown type still fails the
// build: an empty PreparedGraph (graph == nullptr), with `errorOut` carrying
// "<type>: <reason>". isNodeSafeForChain filters those out beforehand.
//
// Does NOT touch any AudioEngine, PluginSlot, or Session state. Safe to
// call from a worker thread. The graph is fully prepared (prepareToPlay
// run) before return; the caller commits it to engine::AudioGraphRuntime
// from the message thread via swapPreparedGraph or by direct ownership.
PreparedGraph buildPreparedGraph(
    engine::PluginHost& host, const Patch& patch, double sampleRate, int blockSize, juce::String& errorOut);

// One plugin node's processor, as the build and the add worker make it (M5 U3).
// `loaded` comes from host.createInstance (nullptr when that failed with
// `loadError`); the node's saved state is applied to it before prepareToPlay.
// When the node is missing, the plugin didn't load, or its setStateInformation
// throws or crashes (through the crash guard, M5 U5), the result is instead a pass-through
// engine::processors::MissingPluginProcessor holding the node's identity and
// saved state verbatim, and `missingReason` says why. Worker-thread safe.
struct PluginBuild
{
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::String missingReason; // empty when `processor` is the plugin itself
};
PluginBuild pluginOrPlaceholder(std::unique_ptr<juce::AudioProcessor> loaded,
                                const juce::String& loadError,
                                const PatchNode& node);

// True if `node` can be safely materialized into a live chain: a plugin
// placeholder (PatchNode::missingReason set) always, since it loads nothing; any
// other plugin via PluginHost::isDescriptorSafeForChain; an internal node iff
// its `internalType` is a recognized built-in. A plugin node with no identity
// at all, or an unrecognized internal type (e.g. a newer schema's node opened by
// an older binary), returns false, so restore/open paths DROP it instead of
// letting buildPreparedGraph abort the whole patch. Message-thread only.
bool isNodeSafeForChain(const engine::PluginHost& host, const PatchNode& node);

// Find every plugin node of every patch in `bank` in the scanned list (M5 U3,
// plan KD3), by ID through PluginHost::resolve, and adopt the scanned
// description. A node that isn't there, or is an instrument or blocked, gets a
// missingReason instead: it builds as a pass-through placeholder that keeps its
// identity and state, and it's never loaded from a path. The restore and open
// paths run this, then dropUnsafeNodes, before any patch can go live (every
// slot is one switch away). Returns the number of missing nodes. Message-thread
// only.
int resolvePluginNodes(PatchBank& bank, const engine::PluginHost& host);

// Drop every node isNodeSafeForChain rejects from every patch in `bank` (each
// slot is one switch away from live), rebuilding a linear chain over the
// survivors of any patch that lost one so its connections stay valid. Run
// after resolvePluginNodes, it drops only what can't be built or shown at all.
// Returns the number of nodes dropped. Message-thread only.
int dropUnsafeNodes(PatchBank& bank, const engine::PluginHost& host);

} // namespace pedalboard3::session
