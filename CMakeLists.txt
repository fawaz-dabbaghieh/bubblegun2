cmake_minimum_required(VERSION 3.20)
project(bubblegun_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUBBLEGUN_BUILD_CLI "Build the bubblegun_cpp command-line tool" ON)
option(BUBBLEGUN_WITH_GFA_READER "Read GFA files (needs zlib); off leaves only PackedGraph::from_edges" ON)

if(BUBBLEGUN_BUILD_CLI AND NOT BUBBLEGUN_WITH_GFA_READER)
    message(FATAL_ERROR "BUBBLEGUN_BUILD_CLI needs BUBBLEGUN_WITH_GFA_READER")
endif()

add_library(bubblegun STATIC
    src/bubbles/bubbles.cpp
    src/bchains/bchains.cpp
    src/graph/packed_graph.cpp
)

target_include_directories(bubblegun
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(bubblegun
    PRIVATE
        -O3
        -Wall
        -Wextra
        -Wpedantic
)

if(BUBBLEGUN_WITH_GFA_READER)
    find_package(ZLIB REQUIRED)
    target_sources(bubblegun
        PRIVATE
            src/fs/Reader.cpp
            src/fs/gfa_line_parsers.cpp
            src/graph/packed_graph_gfa.cpp
    )
    target_compile_definitions(bubblegun PUBLIC BUBBLEGUN_WITH_GFA_READER)
    target_link_libraries(bubblegun PUBLIC ZLIB::ZLIB)
endif()

if(BUBBLEGUN_BUILD_CLI)
    set(ARGPARSE_SUBMODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/argparse")

    if(NOT EXISTS "${ARGPARSE_SUBMODULE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Could not find vendored argparse at ${ARGPARSE_SUBMODULE_DIR}. "
            "Initialize submodules with: git submodule update --init --recursive"
        )
    endif()

    add_subdirectory(external/argparse)

    add_executable(bubblegun_cpp src/main.cpp)

    target_compile_options(bubblegun_cpp
        PRIVATE
            -O3
            -Wall
            -Wextra
            -Wpedantic
    )

    target_link_libraries(bubblegun_cpp
        PRIVATE
            bubblegun
            argparse::argparse
    )
endif()
