cmake_minimum_required(VERSION 3.28)

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
add_library(pb3_warnings INTERFACE)
if(MSVC)
    target_compile_options(pb3_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(pb3_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wconversion -Wsign-conversion
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
    )
endif()

add_library(pb3_rtsafety INTERFACE)
if(PB3_ENABLE_RTSAN)
    target_compile_options(pb3_rtsafety INTERFACE -fsanitize=realtime)
    target_link_options(pb3_rtsafety INTERFACE   -fsanitize=realtime)
    message(STATUS "Pedalboard3: RealtimeSanitizer ENABLED")
endif()

# -----------------------------------------------------------------------------
# Plugin-host format selection.
#
# juce::AudioPluginFormatManager::addDefaultFormats() registers formats only
# when JUCE_PLUGINHOST_* macros are defined to 1 at the compilation of
# juce_AudioPluginFormatManager.cpp. JUCE modules are INTERFACE libraries
# whose sources are re-compiled per consumer target, so these macros must be
# visible to EVERY target that links juce::juce_audio_processors — otherwise
# different translation units see different formats and addDefaultFormats()
# silently registers zero of them. Wire this INTERFACE target into the first
# target in the chain (pb3_engine_core) with PUBLIC linkage; transitive
# propagation reaches pb3_app_core, Pedalboard3, and pb3_smoke_tests.
add_library(pb3_plugin_formats INTERFACE)
target_compile_definitions(pb3_plugin_formats INTERFACE
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LV2=1>
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_LV2=1>
)

# -----------------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------------
if(NOT PB3_LINT_ONLY)
    add_subdirectory(src)
endif()

if(PB3_BUILD_TESTS AND NOT PB3_LINT_ONLY)
    add_subdirectory(tests)
endif()
