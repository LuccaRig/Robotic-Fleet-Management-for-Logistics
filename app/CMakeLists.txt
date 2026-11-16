cmake_minimum_required(VERSION 3.15)

add_executable(simulador_exec
    main.cpp
)

target_link_libraries(simulador_exec PRIVATE
    core_lib
)

if(ENABLE_MQTT)
    add_executable(fleet_launcher launcher.cpp)
    target_compile_definitions(fleet_launcher PRIVATE
        FLEET_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
        FLEET_SIMULATOR_EXECUTABLE="$<TARGET_FILE:simulador_exec>"
    )
    add_dependencies(fleet_launcher simulador_exec)
endif()