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
#
# Built as a STATIC library (pb3_engine_core) so our strict warning flags
# apply only to our own sources. JUCE 8 declares modules as INTERFACE
# libraries and pipes their sources into the consumer target — if this
# target were folded directly into `Pedalboard3`, -Wold-style-cast etc.
# would reach JUCE's vendored Harfbuzz and emit 20k+ warnings on Clang.

add_library(pb3_engine_core STATIC
    AudioEngine.cpp
    AudioEngine.h
    AudioGraphRuntime.cpp
    AudioGraphRuntime.h
    BypassableInstance.cpp
    BypassableInstance.h
    PluginHost.cpp
    PluginHost.h
    processors/OutputSanitiser.cpp
    processors/OutputSanitiser.h
    processors/TestToneProcessor.cpp
    processors/TestToneProcessor.h
)

target_include_directories(pb3_engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(pb3_engine_core
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_events
    PRIVATE
        pb3_warnings
        pb3_rtsafety
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)
