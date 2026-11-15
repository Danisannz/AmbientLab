# Shared plugin code compiled into both targets.
set(AMBIENT_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StateMigration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Fonts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Backdrop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/AmbientLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Components.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/HeaderBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Mascot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/MascotArt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SidechainPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/AnalyzerView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ScopeView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/StereoView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SpectrumView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/AnalyzerPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/viz/SpectrumAnalyser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/viz/VisualizerHub.cpp
)
set(AMBIENT_PLUGIN_SOURCES ${AMBIENT_PLUGIN_SOURCES} PARENT_SCOPE)

juce_add_binary_data(AmbientLabAssets SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/SpaceGrotesk-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/SpaceGrotesk-Bold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/IBMPlexSans-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/IBMPlexSans-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/IBMPlexSans-SemiBold.ttf)
set_target_properties(AmbientLabAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(ambient_add_plugin TARGET PRODUCT CODE IS_SYNTH)
    juce_add_plugin(${TARGET}
        COMPANY_NAME            "AmbientLab"
        PRODUCT_NAME            "${PRODUCT}"
        PLUGIN_MANUFACTURER_CODE Ambl
        PLUGIN_CODE             ${CODE}
        FORMATS                 VST3
        IS_SYNTH                ${IS_SYNTH}
        NEEDS_MIDI_INPUT        TRUE
        NEEDS_MIDI_OUTPUT       FALSE
        IS_MIDI_EFFECT          FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES         $<IF:$<BOOL:${IS_SYNTH}>,Instrument Synth,Fx Reverb Delay>)

    target_sources(${TARGET} PRIVATE ${AMBIENT_PLUGIN_SOURCES})
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${TARGET} PUBLIC
        AMBIENT_IS_INSTRUMENT=$<IF:$<BOOL:${IS_SYNTH}>,1,0>
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${TARGET}
        PRIVATE ambient_core
                AmbientLabAssets
                juce::juce_audio_utils
                juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
endfunction()

ambient_add_plugin(AmbientLabInstrument "AmbientLab"    Amb1 TRUE)
ambient_add_plugin(AmbientLabFX         "AmbientLab FX" Amb2 FALSE)
