add_executable(ambient_tests
    test_core.cpp
)
target_link_libraries(ambient_tests PRIVATE ambient_core Catch2::Catch2WithMain)

include(CTest)
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(ambient_tests)
