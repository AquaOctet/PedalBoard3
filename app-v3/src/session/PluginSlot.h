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

struct PreparedGraph;

// "What plugin is currently loaded?" — the message-thread-only view of
// the active hosting slot. Holds non-owning observer pointers into the
// graph that engine::AudioGraphRuntime owns, plus a display name.
//
// Phase 1.D.1 introduced the synchronous load() that builds the graph
// and swaps it in directly. Phase 1.D.2 keeps load() as a synchronous
// primitive but routes the actual hot path through session::Session,
// which calls session::buildPreparedGraph on a worker thread and then
// commits the result here via adoptPrepared() on the message thread.
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
    //
    // Internally: buildPreparedGraph -> engine.loadGraph -> adoptPrepared.
    bool load(const juce::PluginDescription& desc, juce::String& errorOut);

    // Commits a PreparedGraph as the new live state. Caller must have
    // already swapped `prepared.graph` into the engine via
    // engine::AudioEngine::loadGraph BEFORE calling this — adoptPrepared
    // only updates the observer pointers and fires the change message.
    //
    // Used by load() (synchronous path) and by Session::onBuildResult
    // (queued/coalesced path). Message thread only.
    void adoptPrepared(PreparedGraph&& prepared);

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
