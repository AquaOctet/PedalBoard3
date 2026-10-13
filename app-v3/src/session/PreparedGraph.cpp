#include "PreparedGraph.h"

#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"
#include "engine/processors/MissingPluginProcessor.h"
#include "platform/CrashGuard.h"

#include <algorithm>

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

// True if a plugin node names anything to find or to show: a name, an ID, or
// (from the scanned list) a path.
bool hasPluginIdentity(const PatchNode& node) noexcept
{
    const auto& d = node.desc;
    return d.name.isNotEmpty() || d.uniqueId != 0 || d.deprecatedUid != 0 || d.fileOrIdentifier.isNotEmpty();
}

// setStateInformation as the crash guard takes it: a C++ exception out of the
// plugin is caught here, below the guard.
struct SetStateCall
{
    juce::AudioProcessor* processor;
    const juce::MemoryBlock* state;
    bool threw;
    void operator()() noexcept
    {
        try
        {
            processor->setStateInformation(state->getData(), static_cast<int>(state->getSize()));
        }
        catch (...)
        {
            threw = true;
        }
    }
};

} // namespace

PluginBuild pluginOrPlaceholder(std::unique_ptr<juce::AudioProcessor> loaded,
                                const juce::String& loadError,
                                const PatchNode& node)
{
    const auto placeholder = [&node](juce::String reason)
    {
        PluginBuild built;
        built.processor = std::make_unique<engine::processors::MissingPluginProcessor>(node.desc, node.state);
        built.missingReason = std::move(reason);
        return built;
    };

    if (node.missingReason.isNotEmpty())
        return placeholder(node.missingReason);
    if (loaded == nullptr)
        return placeholder(missingreason::failedToLoad(loadError));

    // The saved state (knob settings) before prepareToPlay, through the crash
    // guard (M5 U5). A plugin that rejects a stale blob quietly starts at its
    // defaults; one that throws or crashes becomes a placeholder that keeps the
    // blob, so a save doesn't lose it.
    if (node.state.getSize() > 0)
    {
        SetStateCall call{loaded.get(), &node.state, false};
        if (const auto fault = platform::guarded(call); fault != platform::Fault::None)
        {
            // A crash can leave a plugin's locks held, so it's never touched
            // again, not even to be deleted.
            if (dynamic_cast<juce::AudioPluginInstance*>(loaded.get()) != nullptr)
                (void)loaded.release();
            else
                loaded.reset();
            return placeholder(missingreason::crashedLoading(platform::faultName(fault)));
        }
        if (call.threw)
        {
            loaded.reset();
            return placeholder(missingreason::settingsFailed());
        }
    }
    return {std::move(loaded), {}};
}

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

    // Plugin + internal nodes. Both end up wrapped in a BypassableInstance; the
    // only difference is how the processor is constructed (host.createInstance
    // for a scanned plugin vs host.createInternalInstance for a built-in node).
    // A plugin that's missing or fails to load becomes a pass-through
    // placeholder, so the rest of the graph still builds (M5 U3, plan KD4).
    for (const auto& patchNode : patch.nodes)
    {
        const auto nodeLabel = (patchNode.kind == NodeKind::Internal) ? patchNode.internalType : patchNode.desc.name;

        std::unique_ptr<juce::AudioProcessor> proc;
        if (patchNode.kind == NodeKind::Internal)
        {
            proc = host.createInternalInstance(patchNode.internalType, sampleRate, blockSize, errorOut);
            if (proc == nullptr)
            {
                errorOut = nodeLabel + ": " + errorOut;
                return result;
            }

            // Restore the saved state before prepareToPlay. Best-effort — a node
            // that rejects a stale blob simply starts at its defaults.
            if (patchNode.state.getSize() > 0)
                proc->setStateInformation(patchNode.state.getData(), static_cast<int>(patchNode.state.getSize()));
        }
        else
        {
            // A node already known to be missing is never loaded.
            juce::String loadError;
            if (patchNode.missingReason.isEmpty())
                proc = host.createInstance(patchNode.desc, sampleRate, blockSize, loadError);
            auto built = pluginOrPlaceholder(std::move(proc), loadError, patchNode);
            proc = std::move(built.processor);
            if (built.missingReason.isNotEmpty())
                result.missing[patchNode.id] = std::move(built.missingReason);
        }

        auto* hostedPtr = proc.get();
        auto wrapped = std::make_unique<engine::BypassableInstance>(std::move(proc));
        if (patchNode.bypass)
            wrapped->setBypass(true);
        if (patchNode.midiChannel != 0)
            wrapped->setMidiChannel(patchNode.midiChannel);
        auto* wrapPtr = wrapped.get();

        auto graphNode = graph->addNode(std::move(wrapped), {}, UpdateKind::async);
        if (graphNode == nullptr)
        {
            errorOut = nodeLabel + ": failed to add node to graph";
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

bool isNodeSafeForChain(const engine::PluginHost& host, const PatchNode& node)
{
    if (node.kind == NodeKind::Internal)
    {
        for (const auto& t : host.getInternalNodeTypes())
            if (t.typeId == node.internalType)
                return true;
        return false; // unrecognized internal type — drop rather than abort the build
    }
    if (!hasPluginIdentity(node))
        return false; // nothing to find or to show
    if (node.missingReason.isNotEmpty())
        return true; // a placeholder loads nothing
    return host.isDescriptorSafeForChain(node.desc);
}

int resolvePluginNodes(PatchBank& bank, const engine::PluginHost& host)
{
    int missing = 0;
    for (auto& patch : bank.patches)
    {
        for (auto& node : patch.nodes)
        {
            if (node.kind != NodeKind::Plugin || !hasPluginIdentity(node))
                continue; // built-ins, and plugin nodes dropUnsafeNodes removes

            node.missingReason.clear();
            const auto& hint = node.fileHint.isNotEmpty() ? node.fileHint : node.desc.fileOrIdentifier;
            if (node.desc.isInstrument)
            {
                node.missingReason = missingreason::instrument();
            }
            else if (const auto found = host.resolve(node.desc, hint))
            {
                if (found->isInstrument)
                    node.missingReason = missingreason::instrument();
                else if (!host.isDescriptorSafeForChain(*found))
                    node.missingReason = missingreason::blocked();
                else
                    node.desc = *found;
            }
            else
            {
                node.missingReason = missingreason::notFound();
            }

            if (node.missingReason.isNotEmpty())
                ++missing;
        }
    }
    return missing;
}

int unresolvedNodeCount(const PatchBank& bank)
{
    int unresolved = 0;
    for (const auto& patch : bank.patches)
        for (const auto& node : patch.nodes)
            if (node.kind == NodeKind::Plugin && hasPluginIdentity(node) && node.desc.fileOrIdentifier.isEmpty())
                ++unresolved;
    return unresolved;
}

int dropUnsafeNodes(PatchBank& bank, const engine::PluginHost& host)
{
    int dropped = 0;
    for (auto& patch : bank.patches)
    {
        const auto before = patch.nodes.size();
        patch.nodes.erase(std::remove_if(patch.nodes.begin(),
                                         patch.nodes.end(),
                                         [&host](const PatchNode& n) { return !isNodeSafeForChain(host, n); }),
                          patch.nodes.end());
        if (patch.nodes.size() != before)
        {
            dropped += static_cast<int>(before - patch.nodes.size());
            patch.connections = linearChainConnections(patch.nodes);
        }
    }
    return dropped;
}

} // namespace pedalboard3::session
