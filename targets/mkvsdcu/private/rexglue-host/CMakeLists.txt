# mkvsdcu - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(mkvsdcu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(MKVSDCU_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(mkvsdcu WIN32 ${MKVSDCU_SOURCES})
else()
    add_executable(mkvsdcu ${MKVSDCU_SOURCES})
endif()

# This is target-private host plumbing. The plugin remains selectable at
# launch via --gpu_plugin xenos; no UE3 or game code is altered here.
rexglue_setup_target(mkvsdcu GPU_PLUGINS xenos)
