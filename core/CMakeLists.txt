cmake_minimum_required(VERSION 3.15)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

add_library(core_lib
    src/data_collector.cpp
    src/orchestrator.cpp
    src/robot_agent.cpp
    src/robot_worker.cpp
    src/warehouse.cpp
    src/route_planner.cpp
    src/logistics.cpp
    src/hardware.cpp
)

target_include_directories(core_lib PUBLIC include)
target_link_libraries(core_lib PUBLIC SQLite::SQLite3 Threads::Threads communication_lib)