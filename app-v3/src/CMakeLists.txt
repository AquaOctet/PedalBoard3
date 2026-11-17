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
# engine first: session PUBLIC-links pb3_engine_core.
add_subdirectory(engine)
add_subdirectory(session)
# control/ (MIDI and OSC) depends only on session/NodeId.h + juce basics.
add_subdirectory(control)

# The icon set (resources/icons/README.md says what every file is for).
set(PB3_ICON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../resources/icons)

# The tray icon's artwork (#147). ICON_BIG/ICON_SMALL further down are a
# build-time conversion into the platform's app icon and leave nothing to draw
# with at runtime, so the notification-area art is embedded separately.
#
# A ladder of sizes rather than one image, because the shell asks for the icon
# at the tray's own pixel size -- 16 at 100% DPI, 20 at 125%, 24 at 150%, 32 at
# 200% -- and JUCE builds the native icon at whatever size the juce::Image
# happens to be, leaving the shell to rescale whatever it is given. Every size
# here is hand-drawn pixel art rather than a downscale, so handing over the
# right one is the difference between the drawing and a smudge of it.
# ui/TrayArt.h picks. These same bytes serve Linux: resources/icons/linux/tray
# holds identical files at the sizes the two sets share.
#
# macOS is the exception and takes TrayIconTemplate.png instead: a menu bar
# extra wants a black-and-transparent silhouette for the system to tint, and
# JUCE marks the second image as a template for us.
juce_add_binary_data(pb3_ui_tray_icons
    HEADER_NAME Pb3TrayIcons.h
    NAMESPACE pb3tray
    SOURCES
        ${PB3_ICON_DIR}/windows/_layers/tray-color-16.png
        ${PB3_ICON_DIR}/windows/_layers/tray-color-20.png
        ${PB3_ICON_DIR}/windows/_layers/tray-color-24.png
        ${PB3_ICON_DIR}/windows/_layers/tray-color-32.png
        ${PB3_ICON_DIR}/windows/_layers/tray-color-40.png
        ${PB3_ICON_DIR}/windows/_layers/tray-color-48.png
        ${PB3_ICON_DIR}/macos/menubar/TrayIconTemplate.png
)

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
    ui/CloseAction.h
    ui/Metrics.h
    ui/CardText.h
    ui/ColourScheme.h
    ui/SystemMeters.cpp
    ui/SystemMeters.h
    ui/DeviceBadge.cpp
    ui/DeviceBadge.h
    ui/IconButton.h
    ui/IoMonitor.cpp
    ui/MeterRow.cpp
    ui/LatencyReadout.cpp
    ui/IoMonitor.h
    ui/MeterRow.h
    ui/LatencyReadout.h
    ui/Alert.cpp
    ui/Alert.h
    ui/AudioSettingsModel.h
    ui/AudioSettingsPanel.cpp
    ui/AudioSettingsPanel.h
    ui/DialogFrame.cpp
    ui/DialogFrame.h
    ui/DialogStyle.h
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
    ui/PortDot.h
    ui/ProcessorControls.cpp
    ui/ProcessorControls.h
    ui/RecorderWindow.cpp
    ui/RecorderWindow.h
    ui/FeaturePrompt.cpp
    ui/FeaturePrompt.h
    ui/FeatureSettingsWindows.cpp
    ui/FeatureSettingsWindows.h
    ui/SegmentedControl.cpp
    ui/SegmentedControl.h
    ui/SettingsWindow.cpp
    ui/SettingsWindow.h
    ui/SignalCanvas.cpp
    ui/SignalCanvas.h
    ui/StatusBar.cpp
    ui/StatusBar.h
    ui/TransportBar.cpp
    ui/TransportBar.h
    ui/TrayArt.h
    ui/TrayIcon.cpp
    ui/TrayIcon.h
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
        pb3_ui_tray_icons
        # juce_animation (JUCE 8): the top bar's folds are eased rather than
        # snapped (#115). Header-and-cpp only, no extra system dependency.
        juce::juce_animation
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
    ui/SystemMeters.cpp
    ui/DeviceBadge.cpp
    ui/IoMonitor.cpp
    ui/MeterRow.cpp
    ui/LatencyReadout.cpp
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
    ui/FeaturePrompt.cpp
    ui/FeatureSettingsWindows.cpp
    ui/SegmentedControl.cpp
    ui/SettingsWindow.cpp
    ui/SignalCanvas.cpp
    ui/StatusBar.cpp
    ui/TransportBar.cpp
    ui/TempoPopover.cpp
    ui/TransportControls.cpp
    ui/TrayIcon.cpp
)

# -----------------------------------------------------------------------------
# Top-level app shell
# -----------------------------------------------------------------------------
# The app icon. JUCE converts what it is given here into the platform's app
# icon at configure time: AppIcon.icns in the bundle on macOS, icon.ico
# compiled into the exe's resources on Windows.
#
# These are the JUCE-facing pair the set ships for exactly this: icon-big.png is
# the full-bleed square at 1024 and icon-small.png the simplified drawing at 64.
# Apple is the exception, because its artwork is -- the Mac tile carries the
# transparent margin macOS expects around it, and a second image would only
# push a stray size into an .icns that is replaced wholesale below anyway.
#
# What JUCE builds out of them is thrown away on the two platforms that have a
# finished icon file: the block after juce_add_gui_app puts Pedalboard3.ico and
# AppIcon.icns in its place, because those hold every hand-drawn size and this
# pair can only hold what JUCE can derive from two pictures. They are still
# passed because JUCE needs them -- given no icon it writes no icon file for us
# to replace at all.
#
# On Linux they do nothing whatever. JUCE's icon conversion has a branch for
# macOS, Windows and iOS and none for Linux, so no window icon is embedded and
# none is generated: a Linux desktop finds the icon through the hicolor theme
# and a .desktop file, which is packaging this repo does not have yet
# (resources/icons/README.md says what it would take, and what the alternative
# costs).
if(APPLE)
    set(PB3_ICON_BIG ${PB3_ICON_DIR}/master/pedalboard3-mac-1024.png)
    set(PB3_ICON_SMALL "")
else()
    set(PB3_ICON_BIG ${PB3_ICON_DIR}/juce/icon-big.png)
    set(PB3_ICON_SMALL ${PB3_ICON_DIR}/juce/icon-small.png)
endif()

juce_add_gui_app(Pedalboard3
    PRODUCT_NAME        "Pedalboard 3"
    COMPANY_NAME        "OpenZephyr LLC"
    COMPANY_COPYRIGHT   "Copyright (c) 2026 OpenZephyr LLC"
    VERSION             "${PROJECT_VERSION}"
    BUNDLE_ID           "com.pedalboard3.app"
    ICON_BIG            "${PB3_ICON_BIG}"
    # Empty on macOS, which juce_add_gui_app treats as "not given".
    ICON_SMALL          "${PB3_ICON_SMALL}"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE
)

# The exe and the bundle get the icon files that were drawn, not the pair of
# images JUCE just converted. juceaide builds its icon from ICON_BIG and
# ICON_SMALL and can only hold what it can derive from two pictures, so its
# 16px is a halved 32 and its 48px is a downscaled 1024. Pedalboard3.ico holds
# ten hand-tuned sizes and AppIcon.icns seven, and the only way in is to put
# them where JUCE has just put its own: it offers no argument for a finished
# icon file, and by the time juce_add_gui_app returns the path is already
# written into the configure-time info file that juceaide reads afterwards.
#
# All of that is JUCE's private arrangement, so nothing here is assumed. The
# generated folder and the file name are both checked, and a JUCE upgrade that
# moves either stops the build rather than quietly shipping the downscales
# again.
function(pb3_use_drawn_icon_file target drawn generated_name)
    get_target_property(generated_dir ${target} JUCE_GENERATED_SOURCES_DIRECTORY)

    if(NOT generated_dir)
        message(FATAL_ERROR
            "${target} has no JUCE_GENERATED_SOURCES_DIRECTORY. JUCE has moved "
            "something this build reaches into; see the icon notes in "
            "app-v3/src/CMakeLists.txt and app-v3/resources/icons/README.md.")
    endif()

    set(generated "${generated_dir}/${generated_name}")

    if(NOT EXISTS "${generated}")
        message(FATAL_ERROR
            "JUCE did not leave a ${generated_name} in '${generated_dir}'. Its "
            "icon conversion has changed shape, so replacing the result with "
            "${drawn} would silently do nothing; see the icon notes in "
            "app-v3/src/CMakeLists.txt.")
    endif()

    # Editing the icon file alone has to re-run configure, or the copy below
    # never gets the chance to happen. Registered before the early return, so
    # it is registered on every configure rather than only on the one that
    # happens to copy.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${drawn}")

    file(SHA256 "${drawn}" want)
    file(SHA256 "${generated}" have)

    if(want STREQUAL have)
        return()
    endif()

    file(COPY_FILE "${drawn}" "${generated}")

    # Windows only: the generated .rc names the icon file, so its text does not
    # change when the icon does, and CMake's Ninja custom commands restat --
    # without this the resource compiler is never re-run and a build that looks
    # entirely successful keeps the previous icon inside the exe.
    set(rc "${generated_dir}/${target}_resources.rc")

    if(EXISTS "${rc}")
        file(TOUCH_NOCREATE "${rc}")
    endif()
endfunction()

if(APPLE)
    # Unverified by eye: nobody on this project has looked at a Dock icon. The
    # file is the one iconutil produced from the same artwork the rest of the
    # set comes from, and CI proves the bundle still builds.
    pb3_use_drawn_icon_file(Pedalboard3 "${PB3_ICON_DIR}/macos/AppIcon.icns" AppIcon.icns)
elseif(WIN32)
    pb3_use_drawn_icon_file(Pedalboard3 "${PB3_ICON_DIR}/windows/Pedalboard3.ico" icon.ico)
endif()

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
    COMPANY_NAME        "OpenZephyr LLC"
    COMPANY_COPYRIGHT   "Copyright (c) 2026 OpenZephyr LLC"
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
