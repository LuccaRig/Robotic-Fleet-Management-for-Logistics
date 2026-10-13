cmake_minimum_required(VERSION 3.15)
project(SimuladorMultirobos VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Habilita testes no CMake
enable_testing()

# 1. Adiciona suas bibliotecas existentes (ajuste os caminhos conforme sua árvore)
add_subdirectory(core)
add_subdirectory(communication)
add_subdirectory(app)

# 2. Configura o Google Test via FetchContent
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# Para evitar warnings do GTest no MSVC/GCC se necessário
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests 
    tests/test_warehouse.cpp
    tests/test_robot.cpp
)

target_link_libraries(unit_tests 
    core_lib             
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests)