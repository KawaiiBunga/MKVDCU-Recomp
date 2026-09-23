# mkvsdcu - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(mkvsdcu LANGUAGES CXX)

set(MKVSDCU_CODEGEN_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/mkvsdcu_manifest.toml"
    CACHE FILEPATH "Manifest for the validated local game folder")

# A configure that fails early can leave empty per-config flags in the cache,
# which silently builds the whole recompiled game at -O0 (several times slower).
foreach(_mkvsdcu_lang CXX)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_${_mkvsdcu_lang}_FLAGS_RELEASE MATCHES "-O")
        message(WARNING "CMAKE_${_mkvsdcu_lang}_FLAGS_RELEASE was empty; restoring -O3 -DNDEBUG")
        set(CMAKE_${_mkvsdcu_lang}_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
    endif()
endforeach()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(MKVSDCU_SOURCES
    src/main.cpp
    src/mkvsdcu_app.cpp
    src/port_menu/port_menu.cpp
    src/port_menu/port_config.cpp
    src/port_menu/settings_catalog.cpp
    src/host_tweaks.cpp
    src/input/controller_filter.cpp
    src/mods/mod_overlay.cpp
    src/telemetry/frame_telemetry.cpp
    src/telemetry/system_telemetry.cpp
    src/telemetry/perf_overlay.cpp
    src/telemetry/guest_profiler.cpp
)

if(WIN32)
    add_executable(mkvsdcu WIN32 ${MKVSDCU_SOURCES})
else()
    add_executable(mkvsdcu ${MKVSDCU_SOURCES})
endif()

# This is target-private host plumbing. The plugin remains selectable at
# launch via --gpu_plugin xenos; no UE3 or game code is altered here.
rexglue_setup_target(mkvsdcu GPU_PLUGINS xenos)

if(WIN32)
    # Performance overlay: GPU engine counters and video memory queries.
    target_link_libraries(mkvsdcu PRIVATE pdh dxgi winmm dbghelp)

    # rexglue_setup_target copies the FidelityFX DLLs only for in-tree target
    # names; an installed SDK exports them as rex::amd_fidelityfx_*, so the
    # runtime would fail to load without this.
    foreach(_mkvsdcu_fx rex::amd_fidelityfx_dx12 rex::amd_fidelityfx_vk)
        if(TARGET ${_mkvsdcu_fx})
            add_custom_command(TARGET mkvsdcu POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${_mkvsdcu_fx}> $<TARGET_FILE_DIR:mkvsdcu>
                VERBATIM)
        endif()
    endforeach()
endif()
