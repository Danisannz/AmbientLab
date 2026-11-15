# ---- Core tests (no JUCE) ---------------------------------------------------
add_executable(ambient_tests
    test_core.cpp
    test_viz_core.cpp
    test_sidechain.cpp
    test_synth.cpp
)
target_link_libraries(ambient_tests PRIVATE ambient_core Catch2::Catch2WithMain)
find_package(Threads REQUIRED)
target_link_libraries(ambient_tests PRIVATE Threads::Threads)

include(CTest)
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(ambient_tests)

# ---- Visualization tests (juce_dsp, no GUI) ---------------------------------
if(AMBIENT_BUILD_PLUGINS)
    juce_add_console_app(ambient_viz_tests PRODUCT_NAME "ambient_viz_tests")
    target_sources(ambient_viz_tests PRIVATE
        test_spectrum.cpp
        ${PROJECT_SOURCE_DIR}/plugin/viz/SpectrumAnalyser.cpp
        ${PROJECT_SOURCE_DIR}/plugin/viz/VisualizerHub.cpp)
    target_include_directories(ambient_viz_tests PRIVATE ${PROJECT_SOURCE_DIR}/plugin)
    target_compile_definitions(ambient_viz_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(ambient_viz_tests PRIVATE
        ambient_core juce::juce_dsp Catch2::Catch2WithMain
        juce::juce_recommended_config_flags)
    catch_discover_tests(ambient_viz_tests)
endif()
