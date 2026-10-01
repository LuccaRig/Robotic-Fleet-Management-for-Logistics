cmake_minimum_required(VERSION 3.15)

add_library(core_lib
    src/data_collector.cpp
    src/orchestrator.cpp
    src/robot_agent.cpp
    src/warehouse.cpp
)

target_include_directories(core_lib PUBLIC
    include
)