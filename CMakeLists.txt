cmake_minimum_required(VERSION 3.20)
project(bubblegun_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(DEFAULT_ARGPARSE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../gfaidx/external/argparse/include")
set(ARGPARSE_INCLUDE_DIR "${DEFAULT_ARGPARSE_INCLUDE_DIR}" CACHE PATH "Path to the argparse include directory")

if(NOT EXISTS "${ARGPARSE_INCLUDE_DIR}/argparse/argparse.hpp")
    message(FATAL_ERROR "Could not find argparse.hpp. Set ARGPARSE_INCLUDE_DIR to the directory that contains argparse/argparse.hpp")
endif()

find_package(ZLIB REQUIRED)

add_executable(bubblegun_cpp
    src/main.cpp
    src/bchains/bchains.cpp
    src/fs/Reader.cpp
    src/fs/gfa_line_parsers.cpp
    src/graph/packed_graph.cpp
)

target_include_directories(bubblegun_cpp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ARGPARSE_INCLUDE_DIR}
)

target_compile_options(bubblegun_cpp
    PRIVATE
        -O3
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(bubblegun_cpp
    PRIVATE
        ZLIB::ZLIB
)
