cmake_minimum_required(VERSION 3.20)

project(HTTP-Server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(http_server
    src/main.cpp
)

target_include_directories(http_server PRIVATE src)