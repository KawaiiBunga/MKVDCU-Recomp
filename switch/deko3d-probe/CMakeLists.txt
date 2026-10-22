cmake_minimum_required(VERSION 3.25)
project(mkvsdcu_deko3d_probe LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(UAM "E:/tools/build/uam/uam.exe" CACHE FILEPATH "Patched native Windows uam")
find_program(NACPTOOL nacptool HINTS "${DEVKITPRO}/tools/bin" NO_DEFAULT_PATH REQUIRED)
find_program(ELF2NRO elf2nro HINTS "${DEVKITPRO}/tools/bin" NO_DEFAULT_PATH REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
option(DEKO3D_DEBUG "Use deko3d's diagnostic library for bring-up" ON)
foreach(_stage vert frag)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/probe.${_stage}.dksh"
        COMMAND "${UAM}" -s ${_stage} -o "${CMAKE_CURRENT_BINARY_DIR}/probe.${_stage}.dksh"
            "${CMAKE_CURRENT_SOURCE_DIR}/probe.${_stage}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/probe.${_stage}" "${UAM}" VERBATIM)
endforeach()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/probe_shaders.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_shaders.py"
        "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/probe.vert.dksh" "${CMAKE_CURRENT_BINARY_DIR}/probe.frag.dksh"
        embed_shaders.py VERBATIM)
add_executable(mkvsdcu-deko3d-probe main.cpp "${CMAKE_CURRENT_BINARY_DIR}/probe_shaders.h")
target_include_directories(mkvsdcu-deko3d-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(mkvsdcu-deko3d-probe PRIVATE -O2 -g1)
if(DEKO3D_DEBUG)
    target_link_libraries(mkvsdcu-deko3d-probe PRIVATE deko3dd nx m)
else()
    target_link_libraries(mkvsdcu-deko3d-probe PRIVATE deko3d nx m)
endif()
add_custom_command(TARGET mkvsdcu-deko3d-probe POST_BUILD
    COMMAND "${NACPTOOL}" --create "MKvDCU deko3d platform probe" "MKvDCU-Recomp" "0.1.0"
        "${CMAKE_CURRENT_BINARY_DIR}/mkvsdcu-deko3d-probe.nacp"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:mkvsdcu-deko3d-probe>"
        "${CMAKE_CURRENT_BINARY_DIR}/probe.stripped.elf"
    COMMAND "${CMAKE_STRIP}" --strip-all "${CMAKE_CURRENT_BINARY_DIR}/probe.stripped.elf"
    COMMAND "${ELF2NRO}" "${CMAKE_CURRENT_BINARY_DIR}/probe.stripped.elf"
        "${CMAKE_CURRENT_BINARY_DIR}/mkvsdcu-deko3d-probe.nro"
        "--nacp=${CMAKE_CURRENT_BINARY_DIR}/mkvsdcu-deko3d-probe.nacp"
        "--icon=${DEVKITPRO}/libnx/default_icon.jpg" VERBATIM)
