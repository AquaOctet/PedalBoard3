# engine/ — real-time audio subsystem.
#
# Every source compiled here is on the hot path. Keep the rules:
#   - No allocation on the audio thread.
#   - No locking on the audio thread.
#   - No I/O on the audio thread.
#   - Every processBlock implementation opens with `juce::ScopedNoDenormals nodn`.
#
# Phase 1.A: graph-swap primitive + BypassableInstance + OutputSanitiser.
# Phase 1.B: SEH/signal crash isolation on top of BypassableInstance.
# Phase 1.C: PluginHost + PluginScanner.

target_sources(Pedalboard3 PRIVATE
    AudioGraphRuntime.cpp
    AudioGraphRuntime.h
    BypassableInstance.cpp
    BypassableInstance.h
    processors/OutputSanitiser.cpp
    processors/OutputSanitiser.h
    processors/TestToneProcessor.cpp
    processors/TestToneProcessor.h
)
