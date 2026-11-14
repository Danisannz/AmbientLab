# Headless editor snapshot tool: renders the real editor to PNG files for visual review.
# Build with -DAMBIENT_BUILD_SNAPSHOT=ON (Linux/Windows). Not shipped.
juce_add_console_app(AmbientSnapshot PRODUCT_NAME "AmbientSnapshot")
target_sources(AmbientSnapshot PRIVATE Main.cpp ${AMBIENT_PLUGIN_SOURCES})
target_include_directories(AmbientSnapshot PRIVATE ${PROJECT_SOURCE_DIR}/plugin)
target_compile_definitions(AmbientSnapshot PRIVATE
    AMBIENT_IS_INSTRUMENT=1
    JucePlugin_Name="AmbientLab"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(AmbientSnapshot PRIVATE
    ambient_core AmbientLabAssets
    juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)
