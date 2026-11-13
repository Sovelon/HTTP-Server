cmake_minimum_required(VERSION 3.20)

project(HTTP-Server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Тесты по умолчанию выключены, чтобы стандартная сборка не требовала сети:
#   cmake -B build -DBUILD_TESTS=ON
option(BUILD_TESTS "Build unit tests (downloads GoogleTest)" OFF)

find_package(Threads REQUIRED)

# Логика сервера собирается в библиотеку, её используют и сервер, и тесты.
add_library(http_core STATIC
    src/http_parser.cpp
)
target_include_directories(http_core PUBLIC src)
target_compile_options(http_core PRIVATE -Wall -Wextra -Wpedantic)

# Итоговый бинарник: ./build/server (требование задания).
add_executable(server
    src/main.cpp
)
target_link_libraries(server PRIVATE http_core Threads::Threads)
target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(unit_tests
        tests/test_http_parser.cpp
    )
    target_link_libraries(unit_tests PRIVATE http_core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()
