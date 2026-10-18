cmake_minimum_required(VERSION 3.15)

add_library(communication_lib src/mqtt_client.cpp src/topics.cpp src/protocol.cpp src/ipc_channel.cpp)
target_include_directories(communication_lib PUBLIC include ${PROJECT_SOURCE_DIR}/core/include)
find_package(Threads REQUIRED)
target_link_libraries(communication_lib PUBLIC Threads::Threads)
