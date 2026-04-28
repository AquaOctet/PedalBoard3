#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace pedalboard3::engine
{
class BypassableInstance;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{

// Result of a worker-thread-safe graph build. `graph` owns everything;
// `bypass` and `hosted` are non-owning observer pointers into nodes
// inside the graph, used by the message-thread side (see
// PluginSlot::adoptPrepared) to wire UI/bypass without re-walking it.
//
// Empty (graph == nullptr) means the build failed; check isValid().
struct PreparedGraph
{
    std::unique_ptr<juce::AudioProcessorGraph> graph;
    engine::BypassableInstance* bypass{nullptr};
    juce::AudioPluginInstance* hosted{nullptr};
    juce::String name;

    bool isValid() const noexcept { return graph != nullptr; }
};

// Worker-safe. Does NOT touch any AudioEngine, PluginSlot, or Session
// state. Builds a 2-in/2-out graph hosting `desc`, wraps it in a
// BypassableInstance, calls prepareToPlay, returns it ready for the
// message thread to swap into engine::AudioGraphRuntime.
//
// On failure returns an empty PreparedGraph and fills `errorOut`.
PreparedGraph buildPreparedGraph(engine::PluginHost& host,
                                 const juce::PluginDescription& desc,
                                 double sampleRate,
                                 int blockSize,
                                 juce::String& errorOut);

} // namespace pedalboard3::session
