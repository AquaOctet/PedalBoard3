cmake_minimum_required(VERSION 3.25)

# -----------------------------------------------------------------------------
# Pedalboard 3 — root CMake project.
#
# Phase 0: buildable Linux hello-world JUCE 8 GUI app + Catch2 unit test.
# Subsequent phases enrich this with engine / session / UI / diagnostics.
# Signing, cross-platform release, and auto-update are Phase 6.
# -----------------------------------------------------------------------------

project(Pedalboard3
    VERSION     3.0.0
    DESCRIPTION "A modular, live-performance VST plugin host."
    HOMEPAGE_URL "https://gitlab.com/void-realm-solutions/pedalboard-3"
    LANGUAGES   C CXX)

# -----------------------------------------------------------------------------
# Language settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd / clang-tidy

# Prefer ccache / sccache as compiler launcher if present
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Pedalboard3: using ccache at ${CCACHE_PROGRAM}")
endif()

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(PB3_BUILD_TESTS    "Build unit tests"                          ON)
option(PB3_ENABLE_VST2    "Enable VST2 hosting (requires user SDK)"   OFF)
option(PB3_ENABLE_CLAP    "Enable CLAP hosting via vendored module"   ON)
option(PB3_ENABLE_RTSAN   "Enable Clang RealtimeSanitizer (debug)"    OFF)
option(PB3_LINT_ONLY      "Configure for lint only, skip heavy deps"  OFF)

# -----------------------------------------------------------------------------
# Dependencies via CPM
# -----------------------------------------------------------------------------
include(cmake/CPM.cmake)

if(NOT PB3_LINT_ONLY)
    # JUCE 8 — pinned to a stable release. Bump in a dedicated PR.
    CPMAddPackage(
        NAME        JUCE
        GITHUB_REPOSITORY juce-framework/JUCE
        GIT_TAG     8.0.4
        OPTIONS
            "JUCE_BUILD_EXTRAS OFF"
            "JUCE_BUILD_EXAMPLES OFF"
    )

    # Mark every JUCE module's INTERFACE_INCLUDE_DIRECTORIES as SYSTEM so
    # GCC/Clang treat JUCE headers as system headers (-isystem) and don't
    # emit warnings from them. This is the second half of the warning-
    # scoping fix:
    #   - pb3_apply_strict_warnings (per-source) handles the case where
    #     JUCE's own .cpp files compile inside our static libs as
    #     INTERFACE_SOURCES — they don't get -Wold-style-cast etc.
    #   - This block handles the case where our own .cpp files include
    #     a JUCE header that itself contains old-style casts (e.g.
    #     juce_Memory.h, juce_AudioSampleBuffer.h) — without SYSTEM,
    #     GCC still flags those.
    # Hardcoded list = the JUCE modules we actually link. Add new modules
    # here if/when our targets pick them up.
    foreach(_juce_mod IN ITEMS
            juce_core juce_events juce_data_structures juce_graphics
            juce_gui_basics juce_gui_extra juce_audio_basics juce_audio_devices
            juce_audio_formats juce_audio_processors juce_audio_utils
            juce_osc)
        if(TARGET ${_juce_mod})
            get_target_property(_juce_includes ${_juce_mod} INTERFACE_INCLUDE_DIRECTORIES)
            if(_juce_includes)
                set_target_properties(${_juce_mod} PROPERTIES
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_juce_includes}")
            endif()
        endif()
    endforeach()
endif()

# CLAP SDK + helpers + hosting module — Phase 1+. Fetched only when enabled
# so the first-ever configure stays fast.
if(PB3_ENABLE_CLAP AND NOT PB3_LINT_ONLY)
    CPMAddPackage(
        NAME        clap
        GITHUB_REPOSITORY free-audio/clap
        GIT_TAG     1.2.7
    )
    # clap-helpers has no release tags; track `main` and re-pin to a SHA
    # once we have a known-good commit for the fork work in Phase 1.
    CPMAddPackage(
        NAME        clap-helpers
        GITHUB_REPOSITORY free-audio/clap-helpers
        GIT_TAG     main
    )
    # juce_clap_hosting — community module (upstream by Jatin Chowdhury).
    # JUCE's `juce_add_module()` derives the module ID from the source
    # directory's basename, so we can't point it at CPM's `-src` suffixed
    # directory. Symlink to a correctly-named sibling and register from that.
    # Ship-in-v3.0 decision per 2026-04-12 /ce:work kickoff; pin to a SHA
    # in Phase 1 once we exercise hosting end-to-end.
    CPMAddPackage(
        NAME           juce_clap_hosting
        GITHUB_REPOSITORY jatinchowdhury18/juce_clap_hosting
        GIT_TAG        main
        DOWNLOAD_ONLY  YES
    )
    if(juce_clap_hosting_ADDED)
        set(PB3_CLAP_HOSTING_LINK "${CMAKE_BINARY_DIR}/_deps/juce_clap_hosting")
        if(NOT EXISTS "${PB3_CLAP_HOSTING_LINK}")
            file(CREATE_LINK "${juce_clap_hosting_SOURCE_DIR}"
                             "${PB3_CLAP_HOSTING_LINK}" SYMBOLIC)
        endif()
        juce_add_module("${PB3_CLAP_HOSTING_LINK}")
    endif()
endif()

# Catch2 v3 — unit tests
if(PB3_BUILD_TESTS AND NOT PB3_LINT_ONLY)
    CPMAddPackage(
        NAME        Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG     v3.7.1
    )
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
endif()

# -----------------------------------------------------------------------------
# Warnings / sanitizers
# -----------------------------------------------------------------------------
# Strict warnings get applied per-source-file via pb3_apply_strict_warnings(),
# NOT via INTERFACE library. JUCE 8 modules declare INTERFACE_SOURCES
# (JUCEModuleSupport.cmake:100), so any consumer that links a JUCE module
# pulls JUCE .cpp files into its own compile unit list — and a target-level
# INTERFACE warning library would flood the build with ~24k -Wold-style-cast
# warnings from JUCE's vendored Harfbuzz / juce_core. Per-source COMPILE_OPTIONS
# attach to specific TUs only, leaving JUCE's INTERFACE_SOURCES untouched.
if(MSVC)
    set(PB3_STRICT_WARNING_FLAGS /W4 /permissive-)
else()
    set(PB3_STRICT_WARNING_FLAGS
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wconversion -Wsign-conversion
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
    )
endif()

function(pb3_apply_strict_warnings target)
    foreach(src IN LISTS ARGN)
        set_source_files_properties("${src}"
            TARGET_DIRECTORY "${target}"
            PROPERTIES COMPILE_OPTIONS "${PB3_STRICT_WARNING_FLAGS}"
        )
    endforeach()
endfunction()

add_library(pb3_rtsafety INTERFACE)
if(PB3_ENABLE_RTSAN)
    target_compile_options(pb3_rtsafety INTERFACE -fsanitize=realtime)
    target_link_options(pb3_rtsafety INTERFACE   -fsanitize=realtime)
    message(STATUS "Pedalboard3: RealtimeSanitizer ENABLED")
endif()

# -----------------------------------------------------------------------------
# JUCE module configuration shared across every consumer.
#
# JUCE modules are INTERFACE libraries (JUCEModuleSupport.cmake:100) whose
# .cpp files get re-compiled per consumer target. Any preprocessor flag that
# the JUCE module sources read (JUCE_PLUGINHOST_*, JUCE_WEB_BROWSER,
# JUCE_USE_CURL, ...) must therefore be visible to EVERY target that links a
# JUCE module — otherwise translation units silently disagree (formats fail
# to register, gtk/gtk.h gets demanded by a JUCE_WEB_BROWSER=0 consumer that
# transitively pulled juce_audio_utils → juce_gui_extra, etc.).
#
# Wire this INTERFACE target PUBLIC into the first target in the chain
# (pb3_engine_core); transitive propagation reaches pb3_app_core,
# Pedalboard3, and pb3_smoke_tests.
add_library(pb3_juce_config INTERFACE)
target_compile_definitions(pb3_juce_config INTERFACE
    # Plugin-host format selection — drives juce::AudioPluginFormatManager::addDefaultFormats().
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LV2=1>
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_LV2=1>
    # Disable optional JUCE features uniformly. juce_gui_extra's webview path
    # and juce_core's curl path both get gated on these; without them on
    # every JUCE-compiling target, juce_gui_extra.cpp tries to include
    # <gtk/gtk.h> and the build fails on runners without GTK include paths.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# -----------------------------------------------------------------------------
# Windows: delay-load dcomp.dll
# -----------------------------------------------------------------------------
# JUCE 8's juce_graphics statically imports dcomp.dll (DirectComposition)
# via its juce_graphics.cpp:94 `#pragma comment(lib, "DComp.lib")`. On
# saas-windows-medium-amd64 (Server Core variant) dcomp.dll is missing
# from System32, and the loader fails 0xC0000135 (STATUS_DLL_NOT_FOUND)
# on every binary that pulls juce_graphics — including our test exe
# which Catch2's catch_discover_tests invokes at build time.
#
# Marking dcomp.dll as delay-loaded keeps the import in the table but
# defers resolution until something actually calls a DCompositionXxx
# function. juceaide / pb3_smoke_tests / pb3-scanner-helper / Pedalboard3
# all run their startup code (including Catch2 test-list enumeration)
# without needing dcomp.dll. Real Windows desktops have dcomp.dll
# present, so the delay-load is a no-op there — the same DCompositionXxx
# call resolves on first invocation as it would have at static-import
# time.
#
# Applied at directory scope so it propagates to every executable
# defined under src/ and tests/. JUCE's juceaide is patched separately
# in .gitlab-ci.yml's windows-setup since it's built by JUCE's recursive
# sub-cmake outside our project.
if(MSVC)
    add_link_options("/DELAYLOAD:dcomp.dll")
    link_libraries(delayimp)
endif()

# -----------------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------------
if(NOT PB3_LINT_ONLY)
    add_subdirectory(src)
endif()

if(PB3_BUILD_TESTS AND NOT PB3_LINT_ONLY)
    add_subdirectory(tests)
endif()
