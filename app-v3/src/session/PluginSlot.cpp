#include "PluginSlot.h"

#include "engine/AudioEngine.h"
#include "engine/BypassableInstance.h"
#include "engine/PluginHost.h"
#include "PreparedGraph.h"

namespace pedalboard3::session
{

namespace
{

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

    auto prepared = buildPreparedGraph(host_, desc, cfg.sampleRate, cfg.blockSize, errorOut);
    if (!prepared.isValid())
        return false;

    // Swap. AudioEngine returns the previously-live graph; let RAII drop
    // it (it carries any prior plugin instance, whose dtor will run on
    // this message thread when the local goes out of scope below).
    auto previous = engine_.loadGraph(std::move(prepared.graph));
    (void)previous;

    adoptPrepared(std::move(prepared));
    return true;
}

void PluginSlot::adoptPrepared(PreparedGraph&& prepared)
{
    // Caller is responsible for having moved prepared.graph into the
    // engine; we only update observer pointers + name. Both pointers
    // are guaranteed valid because unique_ptr::move kept their target
    // addresses stable.
    //
    // Phase-1.D.3 transitional shape: PreparedGraph now exposes a
    // NodeRefs map keyed by NodeId. PluginSlot is single-slot, so we
    // pull the first (and only) entry. PluginSlot itself is retired in
    // Unit 5; this adapter exists to keep the tree buildable through
    // Phase A.
    if (!prepared.nodes.empty())
    {
        const auto& refs = prepared.nodes.begin()->second;
        bypassed_ = refs.bypass;
        hostedPlugin_ = refs.hosted;
        loadedName_ = refs.hosted != nullptr ? refs.hosted->getName() : juce::String();
    }
    else
    {
        bypassed_ = nullptr;
        hostedPlugin_ = nullptr;
        loadedName_.clear();
    }

    sendChangeMessage();
}

void PluginSlot::unload()
{
    if (hostedPlugin_ == nullptr)
        return;

    // Hand a null graph to the engine — it returns the previously-live
    // graph (the one with our plugin), which falls off scope here and
    // destroys the BypassableInstance + the plugin on the message thread.
    auto previous = engine_.loadGraph(nullptr);
    (void)previous;

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
