# Headless tools: editor snapshots (PNG), real-time rendering benchmark and a
# processor-level self test with real bus layouts. Build with
# -DAMBIENT_BUILD_SNAPSHOT=ON. Not shipped.
function(ambient_add_tool TARGET IS_INSTRUMENT)
    juce_add_console_app(${TARGET} PRODUCT_NAME "${TARGET}")
    target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp ${AMBIENT_PLUGIN_SOURCES})
    target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/plugin)
    target_compile_definitions(${TARGET} PRIVATE
        AMBIENT_IS_INSTRUMENT=${IS_INSTRUMENT}
        JucePlugin_Name="AmbientLab"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(${TARGET} PRIVATE
        ambient_core AmbientLabAssets
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endfunction()

ambient_add_tool(AmbientSnapshot 1)
ambient_add_tool(AmbientSnapshotFX 0)
