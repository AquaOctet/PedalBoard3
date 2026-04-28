# session/ — per-user persistence + (later) Session/Patch state model.
#
# Phase 1.C.2: Preferences (audio-device choice + scanned plugin list).
# Phase 1.D will add Session + Patch types here for the patch-switch
# state machine.
#
# Built as a STATIC library so pb3_warnings can apply per-source via
# pb3_apply_strict_warnings() without touching JUCE INTERFACE_SOURCES.

add_library(pb3_session_core STATIC
    Preferences.cpp
    Preferences.h
    PluginSlot.cpp
    PluginSlot.h
    PreparedGraph.cpp
    PreparedGraph.h
    Session.cpp
    Session.h
    PreferencesPersister.cpp
    PreferencesPersister.h
    Patch.cpp
    Patch.h
    NodeId.h
)

target_include_directories(pb3_session_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(pb3_session_core
    PUBLIC
        # PluginSlot.h forward-declares engine types; PluginSlot.cpp
        # uses them directly. PUBLIC because consumers of session
        # (App, tests) typically use both.
        pb3_engine_core
        juce::juce_data_structures
        juce::juce_core
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_events
        # JUCE_WEB_BROWSER=0 / JUCE_USE_CURL=0 / JUCE_PLUGINHOST_* must
        # also reach JUCE module sources compiled into this lib (juce_core
        # links juce_audio_processors transitively in some configs).
        pb3_juce_config
    PRIVATE
        pb3_rtsafety
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)

pb3_apply_strict_warnings(pb3_session_core
    Preferences.cpp
    PluginSlot.cpp
    PreparedGraph.cpp
    Session.cpp
    PreferencesPersister.cpp
    Patch.cpp
)
