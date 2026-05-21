cmake_minimum_required(VERSION 3.20)
project(bubblegun_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(ARGPARSE_SUBMODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/argparse")

if(NOT EXISTS "${ARGPARSE_SUBMODULE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Could not find vendored argparse at ${ARGPARSE_SUBMODULE_DIR}. "
        "Initialize submodules with: git submodule update --init --recursive"
    )
endif()

find_package(ZLIB REQUIRED)
add_subdirectory(external/argparse)

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
        argparse::argparse
        ZLIB::ZLIB
)
