cmake_minimum_required(VERSION 3.20)
project(cpp_latency LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|armv7)")
    message(STATUS "Targeting 32-bit ARM (Raspberry Pi 2/3/4): Cortex-A7 baseline")
    add_compile_options(-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard)
endif()

add_executable(server src/server.cpp)
add_executable(client src/client.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(server PRIVATE pthread)
    target_link_libraries(client PRIVATE pthread)
endif()
