# control/ — Phase 4 control surface (MIDI / OSC / mappings).
#
# This increment lands the headless MIDI mapping core: the MidiMapping data
# model + pure value transform, and MidiRouter (MIDI message -> parameter-update
# intents, owning latch state). No audio-engine or device-I/O dependency yet —
# that wiring is a later increment — so the whole thing is unit-testable from
# the Linux dev box.
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
)

target_include_directories(pb3_control_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/.. # for "session/NodeId.h" and "control/..."
)

target_link_libraries(pb3_control_core
    PUBLIC
        juce::juce_audio_basics # juce::MidiMessage
        juce::juce_core
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
)
