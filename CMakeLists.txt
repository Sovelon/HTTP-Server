cmake_minimum_required(VERSION 3.20)

project(HTTP-Server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# Итоговый бинарник: ./build/server (требование задания).
add_executable(server
    src/main.cpp
)

target_include_directories(server PRIVATE src)
target_link_libraries(server PRIVATE Threads::Threads)
target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
