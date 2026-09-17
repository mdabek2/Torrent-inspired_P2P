cmake_minimum_required(VERSION 3.16)

project(p2p_tracker C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(tracker
    src/main.c
    src/tracker_server.c
    src/client_service.c
    src/resourcelist_handler.c
    ../common/utils.c
)

target_include_directories(tracker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(tracker PRIVATE Threads::Threads)

target_compile_definitions(tracker PRIVATE _POSIX_C_SOURCE=200809L)

target_compile_options(tracker PRIVATE -Wall -Wextra -Wpedantic)
