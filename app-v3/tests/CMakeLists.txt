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
    unit/engine/LevelProcessor_test.cpp
    unit/engine/VuMeterProcessor_test.cpp
    unit/engine/AudioEngine_test.cpp
    unit/engine/PluginHost_test.cpp
    unit/engine/PluginScanner_test.cpp
    unit/engine/AudioRecorderProcessor_test.cpp
    unit/engine/FilePlayerProcessor_test.cpp
    unit/engine/LoopBuffer_test.cpp
    unit/engine/LooperProcessor_test.cpp
    unit/engine/MetronomeProcessor_test.cpp
    unit/engine/OutputToggleProcessor_test.cpp
    unit/engine/PlayHead_test.cpp
    unit/engine/Recordings_test.cpp
    unit/engine/Transport_test.cpp
    unit/engine/TransportMath_test.cpp
    unit/engine/ScanInMessageLoop_test.cpp
    unit/session/Preferences_test.cpp
    unit/session/PreferencesPersister_test.cpp
    unit/session/PreparedGraph_test.cpp
    unit/session/Session_test.cpp
    unit/session/Patch_test.cpp
    unit/session/PatchBank_test.cpp
    unit/session/BoardFiles_test.cpp
    unit/session/Mmc_test.cpp
    unit/session/TapTempo_test.cpp
    unit/session/TransportCommands_test.cpp
    unit/session/BankDocument_test.cpp
    unit/session/FeatureGate_test.cpp
    unit/session/InputGates_test.cpp
    unit/session/Features_test.cpp
    unit/session/EditHistory_test.cpp
    unit/session/PatchSwitcher_test.cpp
    unit/session/AppCommands_test.cpp
    unit/session/PdlSerializer_test.cpp
    unit/session/SafeXml_test.cpp
    unit/session/AtomicFileWriter_test.cpp
    unit/session/PdlV2Importer_test.cpp
    unit/session/ControlParameterApplier_test.cpp
    unit/session/ControlHub_test.cpp
    unit/session/ControlWiring_test.cpp
    unit/session/AppMappings_test.cpp
    unit/ui/CanvasLayout_test.cpp
    unit/ui/MeterMath_test.cpp
    unit/ui/BankRows_test.cpp
    unit/ui/BodyLayout_test.cpp
    unit/ui/BrowserModel_test.cpp
    unit/ui/CardText_test.cpp
    unit/ui/MappingRows_test.cpp
    unit/control/MidiMapping_test.cpp
    unit/control/MidiRouter_test.cpp
    unit/control/MidiControlSurface_test.cpp
    unit/control/MidiInputManager_test.cpp
    unit/control/OscMapping_test.cpp
    unit/control/OscRouter_test.cpp
    unit/control/OscServer_test.cpp
)

add_executable(pb3_smoke_tests ${PB3_TEST_SOURCES})

target_include_directories(pb3_smoke_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # for "support/OfflineRender.h"
)

target_link_libraries(pb3_smoke_tests PRIVATE
    Catch2::Catch2WithMain
    pb3_engine_core
    pb3_session_core
    pb3_control_core
    pb3_rtsafety
    juce::juce_recommended_config_flags
)

pb3_apply_strict_warnings(pb3_smoke_tests ${PB3_TEST_SOURCES})

# JUCE_USE_CURL=0 / JUCE_WEB_BROWSER=0 propagate via pb3_juce_config (linked
# PUBLIC into pb3_engine_core). JUCE_STANDALONE_APPLICATION is test-specific.
target_compile_definitions(pb3_smoke_tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
)

# Wire the pb3-scanner-helper binary into the test target so PluginScanner
# integration cases can invoke a real subprocess. We can't pass the helper
# path directly via target_compile_definitions: $<TARGET_FILE:...> resolves
# at build time and on Windows yields backslashes, which then collide with
# C++ string-escape parsing if embedded in a string literal (e.g. "\p" is
# undefined, "\b" becomes 0x08).
#
# Indirection: file(GENERATE) writes a tiny text file containing the
# resolved helper path; the test reads it at runtime via juce::File which
# handles both slash directions on every platform. The locator file's own
# path is set at CMake configure time using CMAKE_CURRENT_BINARY_DIR
# (always forward slashes), so the macro embedded in source is safe.
#
# Tests gate the integration cases on the helper existing — a build with
# PB3_LINT_ONLY (no helper target) still compiles and gated cases SKIP.
if(TARGET pb3-scanner-helper)
    add_dependencies(pb3_smoke_tests pb3-scanner-helper)
    file(GENERATE
        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/scanner_helper_path.txt"
        CONTENT "$<TARGET_FILE:pb3-scanner-helper>"
    )
    target_compile_definitions(pb3_smoke_tests PRIVATE
        "PB3_TEST_SCANNER_HELPER_LOCATOR=\"${CMAKE_CURRENT_BINARY_DIR}/scanner_helper_path.txt\""
    )
endif()

catch_discover_tests(pb3_smoke_tests)
