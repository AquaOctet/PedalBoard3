# Unit tests — Catch2 v3, one executable across the whole suite.
#
# Tests mirror the src/ layout under tests/unit/ and may share helper code
# under tests/support/ (realtime-unsafe helpers live there; they only run in
# tests). Every new test file goes here in the GLOB-free `PB3_TEST_SOURCES`
# list so link errors surface at configure time, not mysterious runtime
# skips.
#
# We link pb3_engine_core rather than re-listing engine .cpp files, so there
# is one source-of-truth for what "the engine" is. Tests stay headless — they
# never pull in juce_gui_extra or pb3_app_core.

set(PB3_TEST_SOURCES
    smoke_test.cpp
    unit/engine/OutputSanitiser_test.cpp
    unit/engine/BypassableInstance_test.cpp
    unit/engine/BypassableInstance_watchdog_test.cpp
    unit/engine/AudioGraphRuntime_test.cpp
    unit/engine/TestToneProcessor_test.cpp
    unit/engine/AudioEngine_test.cpp
    unit/engine/PluginHost_test.cpp
    unit/session/Preferences_test.cpp
    unit/session/PluginSlot_test.cpp
)

add_executable(pb3_smoke_tests ${PB3_TEST_SOURCES})

target_include_directories(pb3_smoke_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # for "support/OfflineRender.h"
)

target_link_libraries(pb3_smoke_tests PRIVATE
    Catch2::Catch2WithMain
    pb3_engine_core
    pb3_session_core
    pb3_rtsafety
    juce::juce_recommended_config_flags
)

pb3_apply_strict_warnings(pb3_smoke_tests ${PB3_TEST_SOURCES})

# JUCE_USE_CURL=0 / JUCE_WEB_BROWSER=0 propagate via pb3_juce_config (linked
# PUBLIC into pb3_engine_core). JUCE_STANDALONE_APPLICATION is test-specific.
target_compile_definitions(pb3_smoke_tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
)

catch_discover_tests(pb3_smoke_tests)
