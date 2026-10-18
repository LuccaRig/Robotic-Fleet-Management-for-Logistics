cmake_minimum_required(VERSION 3.15)
project(SimuladorMultirobos VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()
add_subdirectory(core)
add_subdirectory(communication)
add_subdirectory(app)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests
    tests/test_warehouse.cpp
    tests/test_robot.cpp
    tests/test_data_collector.cpp
    tests/test_buffer_data_collector.cpp
)
target_link_libraries(unit_tests PRIVATE core_lib GTest::gtest_main)
target_compile_definitions(unit_tests PRIVATE
    TEST_DB_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/test_databases"
)

include(GoogleTest)
gtest_discover_tests(unit_tests)