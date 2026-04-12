# Pedalboard 3 main app target.
#
# Phase 0: a minimal JUCE 8 GUI app that opens a window saying "Pedalboard 3".
# Later phases expand this target with engine / session / control / ui / diagnostics.

juce_add_gui_app(Pedalboard3
    PRODUCT_NAME        "Pedalboard 3"
    COMPANY_NAME        "Pedalboard 3 Project"
    VERSION             "${PROJECT_VERSION}"
    BUNDLE_ID           "com.pedalboard3.app"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE
)

target_sources(Pedalboard3 PRIVATE
    main.cpp
    App.cpp
    App.h
    ui/MainWindow.cpp
    ui/MainWindow.h
)

target_compile_definitions(Pedalboard3 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Pedalboard3,JUCE_PROJECT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Pedalboard3,JUCE_VERSION>"
)

target_link_libraries(Pedalboard3 PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_osc
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_recommended_lto_flags
    pb3_warnings
    pb3_rtsafety
)

# CLAP hosting module (vendored fork). Off by default for -DPB3_ENABLE_CLAP=OFF
# builds; in Phase 0 CLAP code is not yet called but the module builds.
if(PB3_ENABLE_CLAP)
    target_link_libraries(Pedalboard3 PRIVATE juce_clap_hosting)
    target_compile_definitions(Pedalboard3 PRIVATE PB3_HAS_CLAP=1)
endif()

target_include_directories(Pedalboard3 PRIVATE .)
