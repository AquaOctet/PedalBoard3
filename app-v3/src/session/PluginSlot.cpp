#include "PluginSlot.h"

#include "engine/AudioEngine.h"
#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"

namespace pedalboard3::session
{

namespace
{

constexpr int kStereoChannels = 2;
constexpr double kFallbackSampleRate = 48000.0;
constexpr int kFallbackBlockSize = 512;

struct PreparedConfig
{
    double sampleRate;
    int blockSize;
};

PreparedConfig deriveConfig(juce::AudioDeviceManager& dm)
{
    if (auto* dev = dm.getCurrentAudioDevice())
    {
        const auto sr = dev->getCurrentSampleRate();
        const auto bs = dev->getCurrentBufferSizeSamples();
        return {sr > 0.0 ? sr : kFallbackSampleRate, bs > 0 ? bs : kFallbackBlockSize};
    }
    return {kFallbackSampleRate, kFallbackBlockSize};
}

} // namespace

PluginSlot::PluginSlot(engine::AudioEngine& engine, engine::PluginHost& host) : engine_(engine), host_(host) {}

PluginSlot::~PluginSlot()
{
    // Unload before destruction so the engine sees an empty graph and
    // the plugin instance is destroyed on the message thread (where it
    // expects to live, per JUCE convention).
    unload();
}

bool PluginSlot::load(const juce::PluginDescription& desc, juce::String& errorOut)
{
    auto* dm = engine_.getDeviceManager();
    if (dm == nullptr)
    {
        errorOut = "Audio device manager unavailable";
        return false;
    }

    const auto cfg = deriveConfig(*dm);

    auto plugin = host_.createInstance(desc, cfg.sampleRate, cfg.blockSize, errorOut);
    if (plugin == nullptr)
        return false;

    // Cache the raw pointer to the plugin BEFORE the move. unique_ptr's
    // move transfers ownership without changing the underlying object's
    // address, so `hostedPtr` stays valid for the lifetime of the load.
    // Phase 1.D.3 will add proper bus-layout handling for mono / surround
    // plugins; for now we trust the plugin to respond to setPlayConfigDetails
    // on the graph in its default configuration. Plugins that genuinely
    // can't accept 2-in/2-out will fail at prepareToPlay instead of here.
    auto* hostedPtr = plugin.get();

    auto wrapped = std::make_unique<engine::BypassableInstance>(std::move(plugin));
    auto* wrapPtr = wrapped.get();

    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    graph->setPlayConfigDetails(kStereoChannels, kStereoChannels, cfg.sampleRate, cfg.blockSize);

    auto inNode = graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    auto wrapNode = graph->addNode(std::move(wrapped));
    auto outNode = graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    if (inNode == nullptr || wrapNode == nullptr || outNode == nullptr)
    {
        errorOut = "Failed to add graph nodes";
        return false;
    }

    for (int ch = 0; ch < kStereoChannels; ++ch)
    {
        graph->addConnection({{inNode->nodeID, ch}, {wrapNode->nodeID, ch}});
        graph->addConnection({{wrapNode->nodeID, ch}, {outNode->nodeID, ch}});
    }

    graph->prepareToPlay(cfg.sampleRate, cfg.blockSize);

    // Swap. AudioEngine returns the previously-live graph; let RAII drop
    // it (it carries any prior plugin instance, whose dtor will run on
    // this message thread when the local goes out of scope below).
    auto previous = engine_.loadGraph(std::move(graph));
    (void) previous;

    bypassed_ = wrapPtr;
    hostedPlugin_ = hostedPtr;
    loadedName_ = desc.name;

    sendChangeMessage();
    return true;
}

void PluginSlot::unload()
{
    if (hostedPlugin_ == nullptr)
        return;

    // Hand a null graph to the engine — it returns the previously-live
    // graph (the one with our plugin), which falls off scope here and
    // destroys the BypassableInstance + the plugin on the message thread.
    auto previous = engine_.loadGraph(nullptr);
    (void) previous;

    bypassed_ = nullptr;
    hostedPlugin_ = nullptr;
    loadedName_.clear();

    sendChangeMessage();
}

void PluginSlot::setBypassed(bool shouldBypass)
{
    if (bypassed_ != nullptr)
        bypassed_->setBypass(shouldBypass);
}

bool PluginSlot::isBypassed() const
{
    return bypassed_ != nullptr && bypassed_->isBypassed();
}

} // namespace pedalboard3::session
