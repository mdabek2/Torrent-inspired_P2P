cmake_minimum_required(VERSION 3.16)

project(p2p_client C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(client
    src/main.c
    src/tracker_client.c
    src/peer_client.c
    src/peer_server.c
    src/shared_files.c
    ../common/utils.c
)

target_include_directories(client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(client PRIVATE Threads::Threads)

target_compile_options(client PRIVATE -Wall -Wextra -Wpedantic)
