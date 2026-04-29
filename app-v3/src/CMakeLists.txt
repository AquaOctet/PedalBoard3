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

add_library(pb3_app_core STATIC
    App.cpp
    App.h
    AppContext.h
    ui/ColourScheme.h
    ui/EditorWindow.cpp
    ui/EditorWindow.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/Pb3LookAndFeel.cpp
    ui/Pb3LookAndFeel.h
    ui/PluginCard.cpp
    ui/PluginCard.h
    ui/SignalCanvas.cpp
    ui/SignalCanvas.h
    ui/StatusBar.cpp
    ui/StatusBar.h
    ui/TransportBar.cpp
    ui/TransportBar.h
)

target_include_directories(pb3_app_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# JUCE_APPLICATION_NAME_STRING is referenced from App.h, so any translation
# unit that includes App.h needs it. PUBLIC makes it visible through the
# linkage chain (App.h → pb3_app_core → Pedalboard3/main.cpp).
target_compile_definitions(pb3_app_core PUBLIC
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Pedalboard3,JUCE_PROJECT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Pedalboard3,JUCE_VERSION>"
)

target_link_libraries(pb3_app_core
    PUBLIC
        pb3_engine_core
        pb3_session_core
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
    ui/EditorWindow.cpp
    ui/MainWindow.cpp
    ui/Pb3LookAndFeel.cpp
    ui/PluginCard.cpp
    ui/SignalCanvas.cpp
    ui/StatusBar.cpp
    ui/TransportBar.cpp
)

# -----------------------------------------------------------------------------
# Top-level app shell
# -----------------------------------------------------------------------------
juce_add_gui_app(Pedalboard3
    PRODUCT_NAME        "Pedalboard 3"
    COMPANY_NAME        "Pedalboard 3 Project"
    VERSION             "${PROJECT_VERSION}"
    BUNDLE_ID           "com.pedalboard3.app"
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
