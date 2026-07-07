# control/ — Phase 4 control surface (MIDI / OSC / mappings).
#
# Headless control core: MIDI (MidiMapping model + transform, MidiRouter,
# MidiControlSurface incl. learn) and OSC (OscMapping model + address match +
# value, MIDI-over-OSC, multicast validation, OscRouter). M2 added the device
# edge: MidiInputManager (live MIDI input, juce_audio_devices) — the rest
# stays free of audio-engine or device-I/O dependency, so it's unit-testable
# from the Linux dev box.
#
# STATIC library so pb3_apply_strict_warnings() can apply our warning flags
# per-source without touching JUCE's INTERFACE_SOURCES.

add_library(pb3_control_core STATIC
    MidiMapping.cpp
    MidiMapping.h
    MidiRouter.cpp
    MidiRouter.h
    MidiControlSurface.cpp
    MidiControlSurface.h
    MidiInputManager.cpp
    MidiInputManager.h
    OscMapping.cpp
    OscMapping.h
    OscRouter.cpp
    OscRouter.h
)

target_include_directories(pb3_control_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/.. # for "session/NodeId.h" and "control/..."
)

target_link_libraries(pb3_control_core
    PUBLIC
        juce::juce_audio_basics  # juce::MidiMessage
        # MidiInputManager (M2): juce::MidiInputCallback + AudioDeviceManager.
        juce::juce_audio_devices
        juce::juce_osc           # juce::OSCMessage / OSCAddress (pure data use)
        juce::juce_core
        juce::juce_events        # juce::Timer (MidiInputManager's drain timer)
        # JUCE module flags (JUCE_WEB_BROWSER=0 etc.) must reach any JUCE
        # sources compiled into this lib, matching the other pb3_*_core libs.
        pb3_juce_config
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)

pb3_apply_strict_warnings(pb3_control_core
    MidiMapping.cpp
    MidiRouter.cpp
    MidiControlSurface.cpp
    MidiInputManager.cpp
    OscMapping.cpp
    OscRouter.cpp
)
