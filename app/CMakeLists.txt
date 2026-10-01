cmake_minimum_required(VERSION 3.15)

add_executable(simulador_exec
    main.cpp
)

target_link_libraries(simulador_exec PRIVATE
    communication_lib
    core_lib
)