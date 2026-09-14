# Pedalboard 3 — GUI app target.
#
# Architecture: our code lives in two STATIC libraries so our strict warning
# flags (pb3_warnings) apply only to sources we wrote. `Pedalboard3` itself
# stays thin because JUCE 8 declares each module as an INTERFACE library and
# pipes its sources into this target at link time — applying pb3_warnings
# here would flood the macOS/Clang build with 20k+ old-style-cast warnings
# from JUCE's vendored Harfbuzz.
#
#   pb3_engine_core  — engine/*.cpp   (audio-thread-safe code)
#   pb3_app_core     — App + UI       (message-thread code)
#   Pedalboard3      — juce_add_gui_app shell around main.cpp

# -----------------------------------------------------------------------------
# Subsystems (static libs that carry pb3_warnings on their own sources)
# -----------------------------------------------------------------------------
# engine first; session PUBLIC-links pb3_engine_core for PluginSlot.
add_subdirectory(engine)
add_subdirectory(session)
# control/ (Phase 4 MIDI/OSC) depends only on session/NodeId.h + juce basics.
add_subdirectory(control)

# The mockup's fonts (UI chrome plan 2026-09-13-002, KD1): Barlow Semi Condensed,
# Barlow Condensed and JetBrains Mono, all SIL OFL (licences beside them).
# ui/Fonts.cpp reads them from the generated Pb3Fonts.h.
set(PB3_FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../resources/fonts)
juce_add_binary_data(pb3_ui_fonts
    HEADER_NAME Pb3Fonts.h
    NAMESPACE pb3fonts
    SOURCES
        ${PB3_FONT_DIR}/barlow-semi-condensed/BarlowSemiCondensed-Regular.ttf
        ${PB3_FONT_DIR}/barlow-semi-condensed/BarlowSemiCondensed-Medium.ttf
        ${PB3_FONT_DIR}/barlow-semi-condensed/BarlowSemiCondensed-SemiBold.ttf
        ${PB3_FONT_DIR}/barlow-semi-condensed/BarlowSemiCondensed-Bold.ttf
        ${PB3_FONT_DIR}/barlow-condensed/BarlowCondensed-Medium.ttf
        ${PB3_FONT_DIR}/barlow-condensed/BarlowCondensed-SemiBold.ttf
        ${PB3_FONT_DIR}/barlow-condensed/BarlowCondensed-Bold.ttf
        ${PB3_FONT_DIR}/jetbrains-mono/JetBrainsMono-Regular.ttf
        ${PB3_FONT_DIR}/jetbrains-mono/JetBrainsMono-Medium.ttf
)

add_library(pb3_app_core STATIC
    App.cpp
    App.h
    AppContext.h
    ui/AboutWindow.cpp
    ui/AboutWindow.h
    ui/ActivityLights.cpp
    ui/ActivityLights.h
    ui/AppMappingsWindow.cpp
    ui/AppMappingsWindow.h
    ui/BankRows.h
    ui/BankSidebar.cpp
    ui/BankSidebar.h
    ui/BoardMappingsPanel.cpp
    ui/BoardMappingsPanel.h
    ui/BodyLayout.h
    ui/BrowserModel.h
    ui/CanvasLayout.h
    ui/CardText.h
    ui/ColourScheme.h
    ui/CpuMeter.cpp
    ui/CpuMeter.h
    ui/Alert.cpp
    ui/Alert.h
    ui/AudioSettingsModel.h
    ui/AudioSettingsPanel.cpp
    ui/AudioSettingsPanel.h
    ui/DialogFrame.cpp
    ui/DialogFrame.h
    ui/EditorWindow.cpp
    ui/EditorWindow.h
    ui/FileActions.cpp
    ui/FileActions.h
    ui/FilePlayerWindow.cpp
    ui/FilePlayerWindow.h
    ui/Fonts.cpp
    ui/Fonts.h
    ui/Icons.h
    ui/MappingRowEditors.cpp
    ui/MappingRowEditors.h
    ui/MappingRows.h
    ui/MappingsDialog.cpp
    ui/MappingsDialog.h
    ui/MeterMath.h
    ui/LooperWindow.cpp
    ui/LooperWindow.h
    ui/MetronomeWindow.cpp
    ui/MetronomeWindow.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/Pb3LookAndFeel.cpp
    ui/Pb3LookAndFeel.h
    ui/PatchRail.cpp
    ui/PatchRail.h
    ui/PluginBrowser.cpp
    ui/PluginBrowser.h
    ui/PluginCard.cpp
    ui/PluginCard.h
    ui/ProcessorControls.cpp
    ui/ProcessorControls.h
    ui/RecorderWindow.cpp
    ui/RecorderWindow.h
    ui/FeatureChips.cpp
    ui/FeatureChips.h
    ui/FeaturePrompt.cpp
    ui/FeaturePrompt.h
    ui/FeatureSettingsWindows.cpp
    ui/FeatureSettingsWindows.h
    ui/SettingsWindow.cpp
    ui/SettingsWindow.h
    ui/SignalCanvas.cpp
    ui/SignalCanvas.h
    ui/StatusBar.cpp
    ui/StatusBar.h
    ui/TransportBar.cpp
    ui/TransportBar.h
    ui/TempoPopover.cpp
    ui/TransportControls.cpp
    ui/TempoPopover.h
    ui/TransportControls.h
)

target_include_directories(pb3_app_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# JUCE_APPLICATION_NAME_STRING is referenced from App.h, so any translation
# unit that includes App.h needs it. PUBLIC makes it visible through the
# linkage chain (App.h → pb3_app_core → Pedalboard3/main.cpp). The version is
# the full one with its pre-release label (PB3_VERSION_STRING); JUCE's own
# VERSION is numbers only.
target_compile_definitions(pb3_app_core PUBLIC
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Pedalboard3,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PB3_VERSION_STRING}"
)

target_link_libraries(pb3_app_core
    PUBLIC
        pb3_engine_core
        pb3_session_core
        pb3_control_core
        pb3_ui_fonts
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_osc
    PRIVATE
        pb3_rtsafety
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)

pb3_apply_strict_warnings(pb3_app_core
    App.cpp
    ui/AboutWindow.cpp
    ui/ActivityLights.cpp
    ui/AppMappingsWindow.cpp
    ui/BankSidebar.cpp
    ui/BoardMappingsPanel.cpp
    ui/CpuMeter.cpp
    ui/Alert.cpp
    ui/AudioSettingsPanel.cpp
    ui/DialogFrame.cpp
    ui/EditorWindow.cpp
    ui/FileActions.cpp
    ui/FilePlayerWindow.cpp
    ui/Fonts.cpp
    ui/MappingRowEditors.cpp
    ui/MappingsDialog.cpp
    ui/LooperWindow.cpp
    ui/MetronomeWindow.cpp
    ui/MainWindow.cpp
    ui/Pb3LookAndFeel.cpp
    ui/PatchRail.cpp
    ui/PluginBrowser.cpp
    ui/PluginCard.cpp
    ui/ProcessorControls.cpp
    ui/RecorderWindow.cpp
    ui/FeatureChips.cpp
    ui/FeaturePrompt.cpp
    ui/FeatureSettingsWindows.cpp
    ui/SettingsWindow.cpp
    ui/SignalCanvas.cpp
    ui/StatusBar.cpp
    ui/TransportBar.cpp
    ui/TempoPopover.cpp
    ui/TransportControls.cpp
)

# -----------------------------------------------------------------------------
# Top-level app shell
# -----------------------------------------------------------------------------
# ICON_BIG: JUCE converts icon.png into the platform app icon at build time
# (.icns on macOS, .ico on Windows, embedded BinaryData used for the window /
# taskbar icon on Linux). The 3000x3000 master is downscaled by JUCE for each
# target size; ICON_SMALL is omitted so JUCE derives it from the big one.
juce_add_gui_app(Pedalboard3
    PRODUCT_NAME        "Pedalboard 3"
    COMPANY_NAME        "AquaOctet"
    VERSION             "${PROJECT_VERSION}"
    BUNDLE_ID           "com.pedalboard3.app"
    ICON_BIG            "${CMAKE_SOURCE_DIR}/icon.png"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE
)

target_sources(Pedalboard3 PRIVATE main.cpp)

pb3_apply_strict_warnings(Pedalboard3 main.cpp)

# Module-level JUCE flags (JUCE_WEB_BROWSER=0, JUCE_USE_CURL=0,
# JUCE_PLUGINHOST_*) flow in via pb3_juce_config, propagated through
# pb3_engine_core → pb3_app_core → here.

# Intentionally no pb3_warnings here — see file header. juce_recommended_*
# flags are safe because JUCE authored them for their own code.
target_link_libraries(Pedalboard3 PRIVATE
    pb3_app_core
    pb3_rtsafety
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_recommended_lto_flags
)

if(PB3_ENABLE_CLAP)
    target_link_libraries(Pedalboard3 PRIVATE juce_clap_hosting)
    target_compile_definitions(Pedalboard3 PRIVATE PB3_HAS_CLAP=1)
endif()

# -----------------------------------------------------------------------------
# pb3-scanner-helper — out-of-process plugin descriptor extractor
# -----------------------------------------------------------------------------
# Master plan §4.2.A: each plugin scan runs in its own subprocess so
# crashing plugins cannot kill the host. The parent (engine::PluginScanner)
# spawns this binary per plugin file via juce::ChildProcess, reads the
# resulting <PB3_SCAN_RESULT> XML from stdout, and adds descriptors to
# KnownPluginList. A non-zero exit code (signal, abort, throw) tells the
# parent to blacklist the plugin.
#
# Console app — no GUI dependency. Same plugin-format set as the host.
juce_add_console_app(pb3-scanner-helper
    VERSION             "${PROJECT_VERSION}"
    BUNDLE_ID           "com.pedalboard3.scanner-helper"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE
)

target_sources(pb3-scanner-helper PRIVATE tools/pb3-scanner-helper.cpp)

# So the helper can include the dependency-free "engine/ScanProtocol.h"
# (the shared scan wire-token contract) without linking the engine.
target_include_directories(pb3-scanner-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# juce_gui_basics is required even for the "console" helper because
# AudioUnit plugin scans on macOS need an NSApplication / NSAutoreleasePool
# alive (ScopedJuceInitialiser_GUI handles this). juce_audio_utils pulls
# in the AudioUnit-aware bits — without it, juce_audio_processors.mm
# references AUGenericView from AudioUnit.framework, which the linker
# can't resolve unless something downstream pulls it in. (Linux + Windows
# don't have AudioUnit so the dep is no-op there.)
target_link_libraries(pb3-scanner-helper PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    pb3_juce_config
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_recommended_lto_flags
)

pb3_apply_strict_warnings(pb3-scanner-helper tools/pb3-scanner-helper.cpp)

if(PB3_ENABLE_CLAP)
    target_link_libraries(pb3-scanner-helper PRIVATE juce_clap_hosting)
    target_compile_definitions(pb3-scanner-helper PRIVATE PB3_HAS_CLAP=1)
endif()

# -----------------------------------------------------------------------------
# Place pb3-scanner-helper next to the host executable (all platforms)
# -----------------------------------------------------------------------------
# juce_add_gui_app and juce_add_console_app each emit into their OWN _artefacts
# tree (Pedalboard3_artefacts vs pb3-scanner-helper_artefacts), so the helper
# is NOT a sibling of the host binary by default. At runtime
# PluginScanner::findHelperBinary() prefers the helper as a sibling of the
# host's currentExecutableFile; without this copy that lookup fails and the
# host falls back to the crash-prone in-process scan (most visibly on Windows).
#
# macOS: copy into Pedalboard3.app/Contents/MacOS/ (next to the host binary
# inside the bundle). Windows/Linux: copy next to "Pedalboard 3"(.exe). Both
# make the sibling lookup succeed in dev builds and mirror what a packaged
# install ships.
#
# add_dependencies makes the helper build first so the POST_BUILD copy always
# sees a fresh binary. copy_if_different is a no-op when unchanged, so
# incremental builds aren't penalised.
add_dependencies(Pedalboard3 pb3-scanner-helper)
if(APPLE)
    add_custom_command(TARGET Pedalboard3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pb3-scanner-helper>
            $<TARGET_BUNDLE_CONTENT_DIR:Pedalboard3>/MacOS/$<TARGET_FILE_NAME:pb3-scanner-helper>
        COMMENT "Embedding pb3-scanner-helper in Pedalboard3.app/Contents/MacOS/"
        VERBATIM
    )
else()
    add_custom_command(TARGET Pedalboard3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pb3-scanner-helper>
            $<TARGET_FILE_DIR:Pedalboard3>/$<TARGET_FILE_NAME:pb3-scanner-helper>
        COMMENT "Copying pb3-scanner-helper next to the Pedalboard 3 executable"
        VERBATIM
    )
endif()

# -----------------------------------------------------------------------------
# pb3-ui-snapshots — the app's menus, dialogs and alerts rendered to PNG
# -----------------------------------------------------------------------------
# UI chrome plan 2026-09-13-002, U1 / KD6: every UI change shows its surfaces
# before and after. It draws through the real LookAndFeel with the real menu
# builders and dialog contents. Run it with ./build.ps1 snapshots. A dev tool;
# never installed.
juce_add_console_app(pb3-ui-snapshots
    VERSION             "${PROJECT_VERSION}"
    BUNDLE_ID           "com.pedalboard3.ui-snapshots"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE
)

target_sources(pb3-ui-snapshots PRIVATE tools/pb3-ui-snapshots.cpp)

target_link_libraries(pb3-ui-snapshots PRIVATE
    pb3_app_core
    pb3_rtsafety
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_recommended_lto_flags
)

pb3_apply_strict_warnings(pb3-ui-snapshots tools/pb3-ui-snapshots.cpp)
