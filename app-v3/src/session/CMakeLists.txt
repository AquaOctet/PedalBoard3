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
    PreparedGraph.cpp
    PreparedGraph.h
    Session.cpp
    Session.h
    PreferencesPersister.cpp
    PreferencesPersister.h
    Patch.cpp
    Patch.h
    PatchBank.cpp
    PatchBank.h
    BoardFiles.h
    FeatureGate.h
    FeatureNodeGate.h
    InputGates.h
    Features.h
    BankDocument.cpp
    FeatureGate.cpp
    FeatureNodeGate.cpp
    InputGates.cpp
    Features.cpp
    BankDocument.h
    DefaultBank.cpp
    DefaultBank.h
    EditHistory.cpp
    EditHistory.h
    PatchSwitcher.cpp
    PatchSwitcher.h
    AppCommands.cpp
    AppCommands.h
    Mmc.h
    TapTempo.h
    TransportCommands.cpp
    TransportCommands.h
    PdlSerializer.cpp
    PdlSerializer.h
    SafeXml.cpp
    SafeXml.h
    AtomicFileWriter.cpp
    AtomicFileWriter.h
    AtomicFileWriterPlatform.h
    v2_import/PdlV2Importer.cpp
    v2_import/PdlV2Importer.h
    ControlParameterApplier.cpp
    ControlParameterApplier.h
    ControlHub.cpp
    ControlHub.h
    ControlWiring.cpp
    ControlWiring.h
    AppMappings.cpp
    AppMappings.h
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
        # ControlParameterApplier.h exposes control::MidiTarget; PUBLIC so
        # consumers get the control headers. (control depends on session only
        # via the NodeId.h include path, so there is no link cycle.)
        pb3_control_core
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
    PreparedGraph.cpp
    Session.cpp
    PreferencesPersister.cpp
    Patch.cpp
    PatchBank.cpp
    BankDocument.cpp
    FeatureGate.cpp
    FeatureNodeGate.cpp
    InputGates.cpp
    Features.cpp
    DefaultBank.cpp
    EditHistory.cpp
    PatchSwitcher.cpp
    AppCommands.cpp
    TransportCommands.cpp
    PdlSerializer.cpp
    SafeXml.cpp
    AtomicFileWriter.cpp
    v2_import/PdlV2Importer.cpp
    ControlParameterApplier.cpp
    ControlHub.cpp
    ControlWiring.cpp
    AppMappings.cpp
)

# AtomicFileWriter's platform half (M5 U2), apart from JUCE's headers.
if(WIN32)
    target_sources(pb3_session_core PRIVATE AtomicFileWriter_windows.cpp)
else()
    target_sources(pb3_session_core PRIVATE AtomicFileWriter_posix.cpp)
endif()
