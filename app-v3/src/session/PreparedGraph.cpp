#include "PreparedGraph.h"

#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"

namespace pedalboard3::session
{

namespace
{
constexpr int kStereoChannels = 2;
} // namespace

PreparedGraph buildPreparedGraph(engine::PluginHost& host,
                                 const juce::PluginDescription& desc,
                                 double sampleRate,
                                 int blockSize,
                                 juce::String& errorOut)
{
    PreparedGraph result;

    auto plugin = host.createInstance(desc, sampleRate, blockSize, errorOut);
    if (plugin == nullptr)
        return result;

    // Cache raw pointers BEFORE the unique_ptr moves transfer ownership.
    // unique_ptr move keeps the underlying object's address stable, so
    // both pointers stay valid for the lifetime of the prepared graph.
    auto* hostedPtr = plugin.get();
    auto wrapped = std::make_unique<engine::BypassableInstance>(std::move(plugin));
    auto* wrapPtr = wrapped.get();

    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    graph->setPlayConfigDetails(kStereoChannels, kStereoChannels, sampleRate, blockSize);

    auto inNode = graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    auto wrapNode = graph->addNode(std::move(wrapped));
    auto outNode = graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    if (inNode == nullptr || wrapNode == nullptr || outNode == nullptr)
    {
        errorOut = "Failed to add graph nodes";
        return result;
    }

    for (int ch = 0; ch < kStereoChannels; ++ch)
    {
        graph->addConnection({{inNode->nodeID, ch}, {wrapNode->nodeID, ch}});
        graph->addConnection({{wrapNode->nodeID, ch}, {outNode->nodeID, ch}});
    }

    graph->prepareToPlay(sampleRate, blockSize);

    result.graph = std::move(graph);
    result.bypass = wrapPtr;
    result.hosted = hostedPtr;
    result.name = desc.name;
    return result;
}

} // namespace pedalboard3::session
