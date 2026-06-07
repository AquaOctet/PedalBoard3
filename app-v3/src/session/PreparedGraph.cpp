#include "PreparedGraph.h"

#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"

namespace pedalboard3::session
{

namespace
{
constexpr int kStereoChannels = 2;
using AudioGraph = juce::AudioProcessorGraph;
using IOProc = AudioGraph::AudioGraphIOProcessor;
using UpdateKind = AudioGraph::UpdateKind;

// Resolve a PatchConnection endpoint to a graph NodeID. graphIO src
// resolves to the audio-/midi-input node; graphIO dst resolves to the
// audio-/midi-output node. Channel == midiChannelIndex distinguishes
// MIDI from audio.
AudioGraph::NodeID resolveEndpoint(NodeId patchNodeId,
                                   int channel,
                                   bool isSrc,
                                   AudioGraph::NodeID audioInId,
                                   AudioGraph::NodeID audioOutId,
                                   AudioGraph::NodeID midiInId,
                                   AudioGraph::NodeID midiOutId,
                                   const std::unordered_map<NodeId, PreparedGraph::NodeRefs>& nodes) noexcept
{
    if (patchNodeId == graphIO)
    {
        const bool isMidi = (channel == AudioGraph::midiChannelIndex);
        if (isSrc)
            return isMidi ? midiInId : audioInId;
        return isMidi ? midiOutId : audioOutId;
    }
    auto it = nodes.find(patchNodeId);
    if (it == nodes.end() || it->second.graphNode == nullptr)
        return AudioGraph::NodeID();
    return it->second.graphNode->nodeID;
}

} // namespace

PreparedGraph buildPreparedGraph(
    engine::PluginHost& host, const Patch& patch, double sampleRate, int blockSize, juce::String& errorOut)
{
    PreparedGraph result;

    auto graph = std::make_unique<AudioGraph>();
    graph->setPlayConfigDetails(kStereoChannels, kStereoChannels, sampleRate, blockSize);

    // I/O processor nodes — endpoints for PatchConnections referencing graphIO.
    // UpdateKind::async on intermediate calls; the final addConnection call
    // (or graph->rebuild()) commits the topology in a single render-sequence
    // rebuild. JUCE collapses the intermediate topologyChanged() calls.
    auto audioInNode = graph->addNode(std::make_unique<IOProc>(IOProc::audioInputNode), {}, UpdateKind::async);
    auto audioOutNode = graph->addNode(std::make_unique<IOProc>(IOProc::audioOutputNode), {}, UpdateKind::async);
    auto midiInNode = graph->addNode(std::make_unique<IOProc>(IOProc::midiInputNode), {}, UpdateKind::async);
    auto midiOutNode = graph->addNode(std::make_unique<IOProc>(IOProc::midiOutputNode), {}, UpdateKind::async);

    if (audioInNode == nullptr || audioOutNode == nullptr || midiInNode == nullptr || midiOutNode == nullptr)
    {
        errorOut = "Failed to add I/O nodes to graph";
        return result;
    }

    // Plugin nodes.
    for (const auto& patchNode : patch.nodes)
    {
        auto plugin = host.createInstance(patchNode.desc, sampleRate, blockSize, errorOut);
        if (plugin == nullptr)
        {
            errorOut = patchNode.desc.name + ": " + errorOut;
            return result;
        }

        // Phase 2: restore the plugin's saved state (knob settings) before
        // prepareToPlay. Best-effort — a plugin that rejects a stale blob
        // simply starts at its defaults; we don't fail the whole load.
        if (patchNode.state.getSize() > 0)
            plugin->setStateInformation(patchNode.state.getData(),
                                        static_cast<int>(patchNode.state.getSize()));

        auto* hostedPtr = plugin.get();
        auto wrapped = std::make_unique<engine::BypassableInstance>(std::move(plugin));
        if (patchNode.bypass)
            wrapped->setBypass(true);
        auto* wrapPtr = wrapped.get();

        auto graphNode = graph->addNode(std::move(wrapped), {}, UpdateKind::async);
        if (graphNode == nullptr)
        {
            errorOut = patchNode.desc.name + ": failed to add node to graph";
            return result;
        }

        result.nodes[patchNode.id] = PreparedGraph::NodeRefs{wrapPtr, hostedPtr, graphNode.get()};
    }

    // Connections from patch.connections — explicit, no implicit broadcast.
    // The last addConnection call commits the topology (UpdateKind::sync).
    const auto& conns = patch.connections;
    for (size_t i = 0; i < conns.size(); ++i)
    {
        const auto& c = conns[i];
        const bool isLast = (i == conns.size() - 1);

        auto srcId = resolveEndpoint(c.src,
                                     c.srcChannel,
                                     /*isSrc*/ true,
                                     audioInNode->nodeID,
                                     audioOutNode->nodeID,
                                     midiInNode->nodeID,
                                     midiOutNode->nodeID,
                                     result.nodes);
        auto dstId = resolveEndpoint(c.dst,
                                     c.dstChannel,
                                     /*isSrc*/ false,
                                     audioInNode->nodeID,
                                     audioOutNode->nodeID,
                                     midiInNode->nodeID,
                                     midiOutNode->nodeID,
                                     result.nodes);

        graph->addConnection({{srcId, c.srcChannel}, {dstId, c.dstChannel}},
                             isLast ? UpdateKind::sync : UpdateKind::async);
    }

    // For empty patches with no plugin nodes, ensure at least the
    // audio + midi passthrough connections exist so a cleared chain
    // still routes input straight to output rather than silencing.
    if (patch.nodes.empty() && conns.empty())
    {
        for (int ch = 0; ch < kStereoChannels; ++ch)
            graph->addConnection({{audioInNode->nodeID, ch}, {audioOutNode->nodeID, ch}}, UpdateKind::async);
        graph->addConnection(
            {{midiInNode->nodeID, AudioGraph::midiChannelIndex}, {midiOutNode->nodeID, AudioGraph::midiChannelIndex}},
            UpdateKind::sync);
    }
    else if (conns.empty())
    {
        // Patch has nodes but no connections — odd but valid; force the
        // final rebuild so the audio thread sees the new render plan.
        graph->rebuild();
    }

    graph->prepareToPlay(sampleRate, blockSize);

    result.graph = std::move(graph);
    return result;
}

} // namespace pedalboard3::session
