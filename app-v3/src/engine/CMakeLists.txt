# engine/ — real-time audio subsystem.
#
# Every source compiled here is on the hot path. Keep the rules:
#   - No allocation on the audio thread.
#   - No locking on the audio thread.
#   - No I/O on the audio thread.
#   - Every processBlock implementation opens with `juce::ScopedNoDenormals nodn`.
#
# Here: the graph-swap runtime, BypassableInstance and its crash isolation,
# OutputSanitiser, PluginHost and PluginScanner, the transport, and the
# built-in processors.
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
    PeakLevel.h
    MasterMute.h
    TestTone.h
    Transport.cpp
    Transport.h
    TransportMath.h
    DiskThread.h
    Recordings.h
    TakeWriter.cpp
    TakeWriter.h
    BypassableInstance.cpp
    BypassableInstance.h
    KnownProblemPlugins.h
    PluginHost.cpp
    PluginHost.h
    PluginScanner.cpp
    PluginScanner.h
    ScanInMessageLoop.h
    ScanProtocol.h
    processors/LevelProcessor.cpp
    processors/OutputToggleProcessor.cpp
    processors/FilePlayerProcessor.cpp
    processors/AudioRecorderProcessor.cpp
    processors/MetronomeProcessor.cpp
    processors/LooperProcessor.cpp
    processors/ProcessorBase.cpp
    processors/LevelProcessor.h
    processors/OutputToggleProcessor.h
    processors/FilePlayerProcessor.h
    processors/AudioRecorderProcessor.h
    processors/MetronomeProcessor.h
    processors/LooperProcessor.h
    processors/LoopBuffer.h
    processors/ProcessorBase.h
    processors/OutputSanitiser.cpp
    processors/OutputSanitiser.h
    processors/TestToneProcessor.cpp
    processors/TestToneProcessor.h
    processors/VuMeterProcessor.cpp
    processors/VuMeterProcessor.h
    processors/MissingPluginProcessor.cpp
    processors/MissingPluginProcessor.h
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
        # PUBLIC so JUCE_PLUGINHOST_* / JUCE_WEB_BROWSER / JUCE_USE_CURL
        # reach every downstream consumer that re-compiles JUCE INTERFACE
        # sources (pb3_app_core, Pedalboard3, pb3_smoke_tests).
        pb3_juce_config
    PRIVATE
        pb3_rtsafety
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)

pb3_apply_strict_warnings(pb3_engine_core
    AudioEngine.cpp
    AudioGraphRuntime.cpp
    BypassableInstance.cpp
    Transport.cpp
    TakeWriter.cpp
    PluginHost.cpp
    PluginScanner.cpp
    processors/LevelProcessor.cpp
    processors/OutputToggleProcessor.cpp
    processors/FilePlayerProcessor.cpp
    processors/AudioRecorderProcessor.cpp
    processors/MetronomeProcessor.cpp
    processors/LooperProcessor.cpp
    processors/ProcessorBase.cpp
    processors/OutputSanitiser.cpp
    processors/TestToneProcessor.cpp
    processors/VuMeterProcessor.cpp
    processors/MissingPluginProcessor.cpp
)

# The crash guard's platform half (M5 U5, plan KD1): structured exception
# handling on Windows, signals on Linux, and no guard on macOS.
target_sources(pb3_engine_core PRIVATE ../platform/CrashGuard.h)
if(WIN32)
    target_sources(pb3_engine_core PRIVATE ../platform/CrashGuard_windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pb3_engine_core PRIVATE ../platform/CrashGuard_posix.cpp)
else()
    target_sources(pb3_engine_core PRIVATE ../platform/CrashGuard_none.cpp)
endif()

# The memory readout's platform half (#118): the process resident set, asked
# for once a second by the top bar's MEM meter. It lives with the engine
# rather than the UI so the smoke tests can link it without a window.
target_sources(pb3_engine_core PRIVATE ../platform/ProcessMemory.h)
if(WIN32)
    target_sources(pb3_engine_core PRIVATE ../platform/ProcessMemory_windows.cpp)
    target_link_libraries(pb3_engine_core PRIVATE psapi)
elseif(APPLE)
    target_sources(pb3_engine_core PRIVATE ../platform/ProcessMemory_mac.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(pb3_engine_core PRIVATE ../platform/ProcessMemory_linux.cpp)
else()
    target_sources(pb3_engine_core PRIVATE ../platform/ProcessMemory_none.cpp)
endif()
