set(MKVSDCU_PGO "off" CACHE STRING "Switch PGO: off, generate, use")
set_property(CACHE MKVSDCU_PGO PROPERTY STRINGS off generate use)
set(MKVSDCU_PGO_DIR "E:/tools/pgo/mkvsdcu-v15" CACHE PATH "Local compiler profile directory")
if(NOT MKVSDCU_PGO MATCHES "^(off|generate|use)$")
    message(FATAL_ERROR "MKVSDCU_PGO must be off, generate or use")
endif()
if(NOT MKVSDCU_PGO STREQUAL "off")
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "This PGO pipeline requires devkitA64 GCC")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(_pgo_cpu 0)
    if(MKVSDCU_CPU_OPTIMIZATIONS)
        set(_pgo_cpu 1)
    endif()
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/switch-pgo.py" prepare
        --mode "${MKVSDCU_PGO}" --build "${CMAKE_BINARY_DIR}"
        --profile "${MKVSDCU_PGO_DIR}" --compiler "${CMAKE_CXX_COMPILER}"
        --cpu-passes "${_pgo_cpu}"
        RESULT_VARIABLE _pgo_result)
    if(NOT _pgo_result EQUAL 0)
        message(FATAL_ERROR "PGO source/profile validation failed")
    endif()
    file(TO_NATIVE_PATH "${MKVSDCU_PGO_DIR}" _pgo_dir)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" _pgo_prefix)
    set(_pgo_common "-fprofile-prefix-path=${_pgo_prefix}" -fno-profile-values)
    if(MKVSDCU_PGO STREQUAL "generate")
        set(_pgo_flags "-fprofile-generate=${_pgo_dir}" ${_pgo_common}
            -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(mkvsdcu PRIVATE MKVSDCU_PGO_GENERATE=1)
        target_include_directories(mkvsdcu PRIVATE "${CMAKE_BINARY_DIR}/pgo")
        # INSERT scripts must precede libnx's full script. A late -Wl,-T is
        # placed after switch.ld by switch.specs and fails at the final link.
        if(MKVSDCU_PGO_DIR MATCHES "[ \t]")
            message(FATAL_ERROR "The PGO profile directory must have no spaces (GCC specs syntax)")
        endif()
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/pgo.ld"
            "${MKVSDCU_PGO_DIR}/pgo.ld" COPYONLY)
        file(WRITE "${CMAKE_BINARY_DIR}/pgo-link.specs"
            "%rename link mkvsdcu_original_link\n\n*link:\n-T ${MKVSDCU_PGO_DIR}/pgo.ld %(mkvsdcu_original_link)\n\n")
        target_link_options(mkvsdcu PRIVATE
            "-specs=${CMAKE_BINARY_DIR}/pgo-link.specs")
        set_property(TARGET mkvsdcu APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pgo.ld")
    else()
        set(_pgo_flags "-fprofile-use=${_pgo_dir}" ${_pgo_common}
            -fprofile-correction -fprofile-partial-training
            -Wsystem-headers -Werror=coverage-mismatch)
    endif()
    # Renderer sources are part of mkvsdcu. Include the runtime's CPU and UI paths,
    # and the exact texture hash implementation; offline shader tools stay out.
    foreach(_target mkvsdcu mkvsdcu_recomp rexruntime rexcore rexui
                    rexaudio rexinput rexfilesystem xxhash)
        if(TARGET ${_target})
            target_compile_options(${_target} PRIVATE ${_pgo_flags})
        endif()
    endforeach()
    target_link_options(mkvsdcu PRIVATE ${_pgo_flags})
    set_source_files_properties(src/mkvsdcu_pgo.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-profile-generate;-fno-profile-arcs;-fno-profile-use")
    message(STATUS "MKvDCU PGO: ${MKVSDCU_PGO} (${MKVSDCU_PGO_DIR})")
endif()
