# Unit tests — Catch2 v3, one executable across the whole suite.
#
# Tests mirror the src/ layout under tests/unit/ and may share helper code
# under tests/support/ (realtime-unsafe helpers live there; they only run in
# tests). Every new test file goes here in the GLOB-free `PB3_TEST_SOURCES`
# list so link errors surface at configure time, not mysterious runtime
# skips.

set(PB3_TEST_SOURCES
    smoke_test.cpp
    unit/engine/OutputSanitiser_test.cpp
    unit/engine/BypassableInstance_test.cpp
    unit/engine/BypassableInstance_watchdog_test.cpp
    unit/engine/AudioGraphRuntime_test.cpp
    unit/engine/TestToneProcessor_test.cpp
    unit/engine/AudioEngine_test.cpp
)

add_executable(pb3_smoke_tests ${PB3_TEST_SOURCES})

# Tests link against the engine sources directly (rather than the full
# Pedalboard3 GUI target) so they can run headless without a display server.
# Add source files here as they become testable. Keep this minimal — a unit
# test for Foo should only pull Foo's .cpp + its direct deps.
target_sources(pb3_smoke_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine/AudioEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/AudioGraphRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/BypassableInstance.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/processors/OutputSanitiser.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/processors/TestToneProcessor.cpp
)

target_include_directories(pb3_smoke_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}           # for "support/OfflineRender.h"
)

target_link_libraries(pb3_smoke_tests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    pb3_warnings
)

target_compile_definitions(pb3_smoke_tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

catch_discover_tests(pb3_smoke_tests)
