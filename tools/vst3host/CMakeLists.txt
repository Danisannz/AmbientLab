# End-to-end VST3 test: loads the BUILT .vst3 bundles through JUCE's VST3 host
# (the same code path a DAW uses), routes sidechain audio and checks that it
# reaches the detector, never leaks, and ducks. Built with -DAMBIENT_BUILD_SNAPSHOT=ON.
juce_add_console_app(AmbientVst3HostTest PRODUCT_NAME "AmbientVst3HostTest")
target_sources(AmbientVst3HostTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp)
target_compile_definitions(AmbientVst3HostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(AmbientVst3HostTest PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
