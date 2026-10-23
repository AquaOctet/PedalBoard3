cmake_minimum_required(VERSION 3.25)

# -----------------------------------------------------------------------------
# Pedalboard 3 — root CMake project.
#
# The app (src/), its plugin scanner helper and the unit tests (tests/), on
# Windows, macOS and Linux. Signing is still to come (#40).
# -----------------------------------------------------------------------------

project(Pedalboard3
    VERSION     1.0.0
    DESCRIPTION "A modular, VST plugin host."
    HOMEPAGE_URL "https://gitlab.com/void-realm-solutions/pedalboard-3"
    LANGUAGES   C CXX)

# The release version. project() takes numbers only, and so does JUCE's VERSION
# (the Windows file version, the macOS bundle version), so the pre-release label
# lives here. The app, .pdl files and the packages show PB3_VERSION_STRING; an
# empty suffix makes a plain release. A release is tagged v<PB3_VERSION_STRING>
# (docs/releases/README.md).
set(PB3_VERSION_SUFFIX "beta.1")
if(PB3_VERSION_SUFFIX)
    set(PB3_VERSION_STRING "${PROJECT_VERSION}-${PB3_VERSION_SUFFIX}")
else()
    set(PB3_VERSION_STRING "${PROJECT_VERSION}")
endif()
# Which commit this build came from, for the log header and any crash report
# (crash-visibility plan KD4). Read at configure time, so a rebuild without a
# reconfigure keeps the old value; "unknown" outside a git checkout.
set(PB3_BUILD_ID "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE PB3_GIT_SHA
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                    RESULT_VARIABLE PB3_GIT_RESULT)
    if(PB3_GIT_RESULT EQUAL 0 AND PB3_GIT_SHA)
        set(PB3_BUILD_ID "${PB3_GIT_SHA}")
    endif()
endif()
message(STATUS "Pedalboard3: build id ${PB3_BUILD_ID}")

# For the packaging scripts (tools/package.ps1, tools/package.sh).
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/pb3-version.txt" CONTENT "${PB3_VERSION_STRING}
")

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
option(PB3_ENABLE_VST2    "Enable VST2 hosting (requires user SDK at app-v3/vst2_sdk/)" OFF)
option(PB3_ENABLE_CLAP    "Enable CLAP hosting via vendored module"   ON)
option(PB3_ENABLE_RTSAN   "Enable Clang RealtimeSanitizer (debug)"    OFF)
option(PB3_LINT_ONLY      "Configure for lint only, skip heavy deps"  OFF)

# -----------------------------------------------------------------------------
# Dependencies via CPM
# -----------------------------------------------------------------------------
# Release builds carry the MSVC runtime inside the exe (/MT), so the installer
# and the portable zip start on a PC without the VC++ Redistributable (#88).
# Debug keeps the DLL runtime (/MDd); it only runs where Visual Studio is. Set
# before any dependency so JUCE and every other target take it;
# tools/package.ps1 checks the exes with dumpbin.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")
endif()

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

    # Let CMake own the MSVC debug-info format. JUCE's
    # juce_recommended_config_flags hard-codes "/Od /Zi" for Debug, and that
    # explicit /Zi comes AFTER the /Z7 CMake emits when
    # CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded is requested, so the PDB
    # side-channel wins and ccache reports every JUCE-consuming object as
    # "unsupported compiler option" (CI: 185/294 uncacheable). Strip /Zi here;
    # CMake still defaults to ProgramDatabase (/Zi) when nothing is requested,
    # so local Debug builds are unchanged, while CI passes Embedded (/Z7) and
    # gets cacheable objects.
    if(MSVC AND TARGET juce_recommended_config_flags)
        get_target_property(_pb3_juce_cfg_opts juce_recommended_config_flags INTERFACE_COMPILE_OPTIONS)
        if(_pb3_juce_cfg_opts)
            string(REPLACE "/Od /Zi" "/Od" _pb3_juce_cfg_opts "${_pb3_juce_cfg_opts}")
            string(REPLACE "/Zi" "" _pb3_juce_cfg_opts "${_pb3_juce_cfg_opts}")
            set_target_properties(juce_recommended_config_flags
                PROPERTIES INTERFACE_COMPILE_OPTIONS "${_pb3_juce_cfg_opts}")
        endif()
    endif()

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
    # clap-helpers has no release tags, so this tracks `main`. Pin a commit
    # before CLAP ships (every preset turns PB3_ENABLE_CLAP off).
    CPMAddPackage(
        NAME        clap-helpers
        GITHUB_REPOSITORY free-audio/clap-helpers
        GIT_TAG     main
    )
    # juce_clap_hosting — community module (upstream by Jatin Chowdhury).
    # JUCE's `juce_add_module()` derives the module ID from the source
    # directory's basename, so we can't point it at CPM's `-src` suffixed
    # directory. Symlink to a correctly-named sibling and register from that.
    # Tracks `main` too: pin a commit before CLAP ships.
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
    # Single source of truth for "what version is this app" across every
    # target that links pb3_juce_config (engine, session, control, app,
    # the scanner helper) — so a version bump here is never a second
    # literal to remember to update elsewhere (e.g. .pdl's appVersion attr).
    PB3_APP_VERSION="${PB3_VERSION_STRING}"
    # The commit, so a log or a crash report names the build it came from.
    PB3_BUILD_ID="${PB3_BUILD_ID}"
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
# VST2 hosting (opt-in, SDK-gated)
# -----------------------------------------------------------------------------
# JUCE hosts VST2 when built with JUCE_PLUGINHOST_VST=1 and the VST2 headers on
# the include path: juce_VSTPluginFormat.cpp includes <pluginterfaces/vst2.x/
# aeffect.h> and aeffectx.h. Steinberg closed the VST2 licence programme, so
# the SDK cannot be redistributed and cannot be fetched — it only exists in
# the vstsdk3610_11_06_2018_build_37 (or older) VST3 SDK, or in JUCE 5.3.2.
# Vendor your copy at app-v3/vst2_sdk/ (any layout — we locate the directory
# that CONTAINS pluginterfaces/vst2.x/aeffect.h) and configure with
# -DPB3_ENABLE_VST2=ON. The option is OFF everywhere by default, so CI and
# every preset build configure exactly as before.
#
# Switched on without the SDK this stops the configure instead of warning.
# The option used to be declared and read by nothing at all (#97): you could
# set it, the build would succeed, and VST2 still would not load. A flag that
# lies about what it did is worse than no flag.
if(PB3_ENABLE_VST2 AND NOT PB3_LINT_ONLY)
    file(GLOB_RECURSE _pb3_vst2_iface
         "${CMAKE_CURRENT_SOURCE_DIR}/vst2_sdk/*/pluginterfaces/vst2.x/aeffect.h"
         "${CMAKE_CURRENT_SOURCE_DIR}/vst2_sdk/pluginterfaces/vst2.x/aeffect.h")
    if(NOT _pb3_vst2_iface)
        message(FATAL_ERROR
            "PB3_ENABLE_VST2=ON but no VST2 SDK was found. Expected "
            "pluginterfaces/vst2.x/aeffect.h somewhere under "
            "${CMAKE_CURRENT_SOURCE_DIR}/vst2_sdk/. Take it from the "
            "vstsdk3610_11_06_2018_build_37 (or older) VST3 SDK, or from "
            "JUCE 5.3.2. Configure with -DPB3_ENABLE_VST2=OFF to build "
            "without VST2.")
    endif()
    list(GET _pb3_vst2_iface 0 _pb3_vst2_header)
    # .../pluginterfaces/vst2.x/aeffect.h -> the directory holding pluginterfaces/
    get_filename_component(_pb3_vst2_dir "${_pb3_vst2_header}" DIRECTORY)
    get_filename_component(_pb3_vst2_dir "${_pb3_vst2_dir}" DIRECTORY)
    get_filename_component(_pb3_vst2_dir "${_pb3_vst2_dir}" DIRECTORY)
    if(NOT EXISTS "${_pb3_vst2_dir}/pluginterfaces/vst2.x/aeffectx.h")
        message(FATAL_ERROR
            "VST2 SDK at ${_pb3_vst2_dir} is incomplete: aeffect.h is there but "
            "pluginterfaces/vst2.x/aeffectx.h is not. JUCE needs both.")
    endif()
    message(STATUS "Pedalboard 3: VST2 SDK found at ${_pb3_vst2_dir} — enabling JUCE_PLUGINHOST_VST=1")
    target_compile_definitions(pb3_juce_config INTERFACE JUCE_PLUGINHOST_VST=1)
    target_include_directories(pb3_juce_config INTERFACE "${_pb3_vst2_dir}")
endif()

# -----------------------------------------------------------------------------
# Windows: ASIO low-latency driver support (opt-in, SDK-gated)
# -----------------------------------------------------------------------------
# JUCE only exposes ASIO when built with JUCE_ASIO=1 AND the Steinberg ASIO SDK
# on the include path. The SDK can't be redistributed, so vendor it yourself at
# app-v3/asio_sdk/ (any layout — we locate the dir containing iasiodrv.h) and
# reconfigure; ASIO then appears in Audio Settings. Without the SDK this is a
# no-op, so CI and non-ASIO builds configure unchanged. ASIO relicensed to
# GPLv3 (Oct 2025), compatible with this project. See issue #9 / master plan
# §4.2.E. Untestable on the Linux dev box — verify on Windows hardware.
if(WIN32 AND NOT PB3_LINT_ONLY)
    file(GLOB_RECURSE _pb3_asio_iface "${CMAKE_CURRENT_SOURCE_DIR}/asio_sdk/*/iasiodrv.h"
                                      "${CMAKE_CURRENT_SOURCE_DIR}/asio_sdk/iasiodrv.h")
    if(_pb3_asio_iface)
        list(GET _pb3_asio_iface 0 _pb3_asio_header)
        get_filename_component(_pb3_asio_dir "${_pb3_asio_header}" DIRECTORY)
        message(STATUS "Pedalboard 3: ASIO SDK found at ${_pb3_asio_dir} — enabling JUCE_ASIO=1")
        target_compile_definitions(pb3_juce_config INTERFACE JUCE_ASIO=1)
        target_include_directories(pb3_juce_config INTERFACE "${_pb3_asio_dir}")
    else()
        message(STATUS "Pedalboard 3: no ASIO SDK at app-v3/asio_sdk/ — ASIO disabled "
                       "(WASAPI/DirectSound only). Drop the Steinberg ASIO SDK there to enable it.")
    endif()
endif()

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
