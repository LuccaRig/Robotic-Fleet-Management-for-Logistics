cmake_minimum_required(VERSION 3.15)

find_package(SQLite3 3.24 REQUIRED)
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
    src/passages.cpp
    src/event_recorder.cpp
    src/runtime.cpp
    src/scenarios.cpp
)

target_include_directories(core_lib PUBLIC include)
if(TARGET SQLite3::SQLite3)
    set(ETAPA1_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(ETAPA1_SQLITE_TARGET SQLite::SQLite3)
endif()
target_link_libraries(core_lib PUBLIC ${ETAPA1_SQLITE_TARGET} Threads::Threads communication_lib)