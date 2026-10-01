cmake_minimum_required(VERSION 3.15)

add_library(communication_lib
    src/mqtt_client.cpp
    src/topics.cpp
)

target_include_directories(communication_lib PUBLIC
    include
)