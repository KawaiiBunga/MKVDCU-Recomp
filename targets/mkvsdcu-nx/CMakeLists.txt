# MKvDCU - Mortal Kombat vs. DC Universe Nintendo Switch Recompiled Port
# Built with ReXGlue, devkitA64, libnx, and Mesa/NVK static driver.

cmake_minimum_required(VERSION 3.25)
project(mkvsdcu LANGUAGES C CXX ASM)

option(MKVSDCU_DEKO3D "Native deko3d game renderer" OFF)
if(MKVSDCU_DEKO3D AND NOT REXGLUE_SWITCH_DEKO3D)
    message(FATAL_ERROR "MKVSDCU_DEKO3D requires the SDK's REXGLUE_SWITCH_DEKO3D build")
endif()

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
    src/mkvsdcu_port_menu.cpp
    src/mkvsdcu_frame_telemetry.cpp
)
if(NOT MKVSDCU_DEKO3D)
    list(APPEND MKVSDCU_SOURCES src/rust_switch_stubs.c)
endif()

add_executable(mkvsdcu ${MKVSDCU_SOURCES})

# Setup target with Xenos GPU emulation plugin (compiled statically on Switch)
if(MKVSDCU_DEKO3D)
    rexglue_setup_target(mkvsdcu)
    include(cmake/deko3d.cmake)
else()
    rexglue_setup_target(mkvsdcu GPU_PLUGINS xenos)
endif()

# Link FFmpeg decoder from ReXGlue SDK
if(TARGET libavcodec)
    target_link_libraries(mkvsdcu PRIVATE libavcodec)
endif()

if(REXGLUE_PLATFORM_SWITCH)
    # Weak sub_ aliases deliberately keep hooks overridable, but GCC cannot
    # inline calls through them. Stage direct calls only for unhooked functions.
    option(MKVSDCU_DIRECT_CALLS "Hook-safe direct calls in generated Switch code" ON)
    if(MKVSDCU_DIRECT_CALLS)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(_direct_dir "${CMAKE_CURRENT_BINARY_DIR}/direct-calls")
        set(_direct_script "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/prepare-switch-direct-calls.py")
        set(_sdk_root "${CMAKE_CURRENT_SOURCE_DIR}/../../references/nfsmw-nx-main/sdk")
        if(MKVSDCU_DEKO3D)
            set(_sdk_root "${REXSDK_DIR}")
        endif()
        set(_pc_host "${CMAKE_CURRENT_SOURCE_DIR}/../mkvsdcu/private/rexglue-host")
        file(GLOB_RECURSE _hook_inputs CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
            "${_pc_host}/src/*.cpp" "${_pc_host}/src/*.h" "${_pc_host}/config/*.toml"
            "${_sdk_root}/src/*.cpp" "${_sdk_root}/src/*.h" "${_sdk_root}/include/*.h")
        get_target_property(_original_sources mkvsdcu_recomp SOURCES)
        set(_staged_sources "")
        set(_direct_outputs "")
        set(_direct_inputs "")
        foreach(_source IN LISTS _original_sources)
            get_filename_component(_name "${_source}" NAME)
            if(_name MATCHES "^mkvsdcu_recomp\\.[0-9]+\\.cpp$")
                list(APPEND _direct_inputs "${_source}")
                list(APPEND _direct_outputs "${_direct_dir}/${_name}")
                list(APPEND _staged_sources "${_direct_dir}/${_name}")
            else()
                list(APPEND _staged_sources "${_source}")
            endif()
        endforeach()
        add_custom_command(OUTPUT ${_direct_outputs}
            COMMAND "${Python3_EXECUTABLE}" "${_direct_script}"
                --generated "${CMAKE_CURRENT_SOURCE_DIR}/generated/default"
                --output "${_direct_dir}"
                --hook-root "${CMAKE_CURRENT_SOURCE_DIR}/src"
                --hook-root "${_pc_host}/src" --hook-root "${_pc_host}/config"
                --hook-root "${_sdk_root}/src" --hook-root "${_sdk_root}/include"
            DEPENDS ${_direct_inputs} ${_hook_inputs} "${_direct_script}"
            COMMENT "Staging hook-safe direct calls for Switch LTO" VERBATIM)
        add_custom_target(mkvsdcu_direct_calls DEPENDS ${_direct_outputs})
        add_dependencies(mkvsdcu_direct_calls mkvsdcu_codegen)
        add_dependencies(mkvsdcu_recomp mkvsdcu_direct_calls)
        set_property(TARGET mkvsdcu_recomp PROPERTY SOURCES "${_staged_sources}")
    endif()
    # Use xxHash's memcpy-safe loads; GCC's aliasing assumptions must not
    # turn texture/pipeline keys into unstable cache misses.
    if(TARGET xxhash)
        target_compile_definitions(xxhash PRIVATE XXH_FORCE_MEMORY_ACCESS=0)
    endif()
    # Count the guest's real frame handoff, not UI redraws or vblank ticks.
    target_link_options(mkvsdcu PRIVATE "-Wl,--wrap=__imp__VdSwap")
    # LTO for cross-translation unit optimization and inlining
    option(MKVSDCU_LTO "Link-time optimization for Switch" ON)
    if(MKVSDCU_LTO)
        # devkitA64 GCC 16's Windows host rejects serialization of this PCH
        # with LTO (EINVAL). Compiling its headers normally preserves LTO.
        if(CMAKE_HOST_WIN32 AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
            set_property(TARGET mkvsdcu_recomp PROPERTY DISABLE_PRECOMPILE_HEADERS ON)
        endif()
        foreach(target mkvsdcu_recomp mkvsdcu)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE -flto -fno-fat-lto-objects)
            endif()
        endforeach()
        set(MKVSDCU_LTO_JOBS 2 CACHE STRING "Parallel GCC LTO jobs")
        if(NOT MKVSDCU_LTO_JOBS MATCHES "^[1-9][0-9]*$" OR MKVSDCU_LTO_JOBS GREATER 32)
            message(FATAL_ERROR "MKVSDCU_LTO_JOBS must be between 1 and 32")
        endif()
        target_link_options(mkvsdcu PRIVATE -flto=${MKVSDCU_LTO_JOBS} -flto-partition=balanced)
    endif()

    # Package as Homebrew NRO
    rexglue_switch_add_nro(mkvsdcu
        NAME "Mortal Kombat vs. DC Universe"
        AUTHOR "Midway / ReXGlue"
        VERSION "1.0.0"
    )
endif()
