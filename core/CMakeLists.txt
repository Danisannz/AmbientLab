add_library(ambient_core STATIC
    src/TestToneSynth.cpp
)
target_include_directories(ambient_core PUBLIC include)
target_compile_features(ambient_core PUBLIC cxx_std_20)
set_target_properties(ambient_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(ambient_core PRIVATE /W4 /fp:fast)
else()
    target_compile_options(ambient_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
