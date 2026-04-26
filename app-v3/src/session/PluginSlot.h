#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <memory>

namespace pedalboard3::engine
{
class AudioEngine;
class BypassableInstance;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{

// "What plugin is currently loaded?" — Phase 1.D.1's minimum viable
// hosting state. Builds a 2-in/2-out audio-processor graph that wraps
// the active plugin instance with a BypassableInstance and routes
// input → plugin → output. Synchronous load/unload; the audio thread
// sees a graph swap via AudioEngine::loadGraph ->
// AudioGraphRuntime::swapPreparedGraph.
//
// Phase 1.D.2 will add the queued/coalescing patch-switch state
// machine on top of this synchronous load() — load() becomes its
// blocking back-end, run on a worker thread.
//
// Phase 1.D.3 grows this into a multi-plugin chain.
//
// Not RT-safe. Message-thread only. juce::ChangeBroadcaster so UI can
// observe load/unload transitions without owning the slot.
class PluginSlot : public juce::ChangeBroadcaster
{
public:
    PluginSlot(engine::AudioEngine& engine, engine::PluginHost& host);
    ~PluginSlot() override;

    // Synchronously instantiate `desc`, wrap in BypassableInstance,
    // build a 2-in/2-out graph, swap into the engine. Returns true on
    // success. On failure returns false, fills `errorOut`, and leaves
    // any previously-loaded plugin intact.
    bool load(const juce::PluginDescription& desc, juce::String& errorOut);

    // Swap a null graph in; engine goes silent. Idempotent.
    void unload();

    juce::String getLoadedName() const { return loadedName_; }
    bool isLoaded() const noexcept { return hostedPlugin_ != nullptr; }

    // Bypass control. No-op when nothing is loaded.
    void setBypassed(bool shouldBypass);
    bool isBypassed() const;

    // The plugin instance the graph is hosting, or nullptr when nothing
    // is loaded. Lifetime tracks the load — invalidated by unload(),
    // load(), or dtor. UI uses this to call createEditor().
    juce::AudioPluginInstance* getHostedPlugin() const noexcept { return hostedPlugin_; }

private:
    engine::AudioEngine& engine_;
    engine::PluginHost& host_;

    // Non-owning observer pointers into the live graph, which is owned
    // by AudioGraphRuntime once we hand it off via engine_.loadGraph().
    // Both pointers are null exactly when isLoaded() is false.
    engine::BypassableInstance* bypassed_{nullptr};
    juce::AudioPluginInstance* hostedPlugin_{nullptr};

    juce::String loadedName_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSlot)
};

} // namespace pedalboard3::session
