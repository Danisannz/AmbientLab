cmake_minimum_required(VERSION 3.22)
project(AmbientLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(AMBIENT_BUILD_PLUGINS "Build the JUCE VST3 targets" ON)
option(AMBIENT_BUILD_TESTS   "Build unit tests"             ON)
option(AMBIENT_BUILD_SNAPSHOT "Build the headless editor snapshot tool" OFF)

include(FetchContent)

# ---- Core DSP library (no JUCE dependency) ---------------------------------
add_subdirectory(core)

# ---- Plugins ----------------------------------------------------------------
if(AMBIENT_BUILD_PLUGINS)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
    add_subdirectory(plugin)
    if(AMBIENT_BUILD_SNAPSHOT)
        add_subdirectory(tools/snapshot)
    endif()
endif()

# ---- Tests ------------------------------------------------------------------
if(AMBIENT_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    add_subdirectory(tests)
endif()
