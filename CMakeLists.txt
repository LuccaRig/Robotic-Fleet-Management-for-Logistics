cmake_minimum_required(VERSION 3.15)
project(SimuladorMultirobos VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Preparar os testes automatizados" OFF)

add_subdirectory(communication)
add_subdirectory(core)
add_subdirectory(app)

if(BUILD_TESTING)
    enable_testing()
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    add_executable(unit_tests
        tests/test_warehouse.cpp tests/test_robot.cpp tests/test_ipc.cpp
        tests/test_logistics.cpp tests/test_passages_hardware.cpp
        tests/test_data_collector.cpp tests/test_buffer_data_collector.cpp tests/test_storage_migration.cpp)
    target_link_libraries(unit_tests PRIVATE core_lib GTest::gtest_main)
    target_compile_definitions(unit_tests PRIVATE TEST_DB_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/test_databases")
    add_test(NAME unidade COMMAND unit_tests)
    set_tests_properties(unidade PROPERTIES TIMEOUT 30)
    add_executable(etapa1_integration tests/test_integration.cpp)
    target_link_libraries(etapa1_integration PRIVATE core_lib GTest::gtest_main)
    target_compile_definitions(etapa1_integration PRIVATE
        TEST_DB_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/test_databases"
        SIMULATOR_EXECUTABLE_PATH="$<TARGET_FILE:simulador_exec>")
    add_dependencies(etapa1_integration simulador_exec)
    add_test(NAME integracao COMMAND etapa1_integration)
    set_tests_properties(integracao PROPERTIES TIMEOUT 1020)
endif()
