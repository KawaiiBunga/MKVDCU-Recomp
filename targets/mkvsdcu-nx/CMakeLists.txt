# MKvDCU - Mortal Kombat vs. DC Universe Nintendo Switch Recompiled Port
# Built with ReXGlue, devkitA64, libnx, and Mesa/NVK static driver.

cmake_minimum_required(VERSION 3.25)
project(mkvsdcu LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization level for Release build
foreach(_lang C CXX)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_${_lang}_FLAGS_RELEASE MATCHES "-O")
        set(CMAKE_${_lang}_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
    endif()
endforeach()

include(generated/rexglue.cmake)

# Switch target sources
set(MKVSDCU_SOURCES
    src/main.cpp
    src/mkvsdcu_app_switch.cpp
    src/mkvsdcu_espera_anillo.cpp
    src/rust_switch_stubs.c
)

add_executable(mkvsdcu ${MKVSDCU_SOURCES})

# Setup target with Xenos GPU emulation plugin (compiled statically on Switch)
rexglue_setup_target(mkvsdcu GPU_PLUGINS xenos)

# Link FFmpeg decoder from ReXGlue SDK
if(TARGET libavcodec)
    target_link_libraries(mkvsdcu PRIVATE libavcodec)
endif()

if(REXGLUE_PLATFORM_SWITCH)
    # LTO for cross-translation unit optimization and inlining
    option(MKVSDCU_LTO "Link-time optimization for Switch" ON)
    if(MKVSDCU_LTO)
        foreach(target mkvsdcu_recomp mkvsdcu)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE -flto -fno-fat-lto-objects)
            endif()
        endforeach()
        target_link_options(mkvsdcu PRIVATE -flto=2 -flto-partition=balanced)
    endif()

    # Package as Homebrew NRO
    rexglue_switch_add_nro(mkvsdcu
        NAME "Mortal Kombat vs. DC Universe"
        AUTHOR "Midway / ReXGlue"
        VERSION "1.0.0"
    )
endif()
