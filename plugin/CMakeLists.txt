# Shared plugin code compiled into both targets.
set(AMBIENT_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StateMigration.cpp
)

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
                juce::juce_audio_utils
                juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
endfunction()

ambient_add_plugin(AmbientLabInstrument "AmbientLab"    Amb1 TRUE)
ambient_add_plugin(AmbientLabFX         "AmbientLab FX" Amb2 FALSE)
