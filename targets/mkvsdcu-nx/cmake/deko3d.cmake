set(_native "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nfsmw-deko3d")
file(GLOB _native_sources CONFIGURE_DEPENDS "${_native}/nfsmw_dk_*.cpp")
target_sources(mkvsdcu PRIVATE ${_native_sources}
    "${_native}/nfsmw_nativo_sistema.cpp"
    "${_native}/nfsmw_nativo_shaders.cpp"
    "${_native}/nfsmw_nativo_ganchos.cpp"
    "${_native}/nfsmw_shader_library.cpp"
    src/deko3d/mkvsdcu_native_adapter.cpp)
target_include_directories(mkvsdcu PRIVATE "${_native}"
    "${REXSDK_DIR}/src/core" "${REXSDK_DIR}/thirdparty/xxHash")
target_compile_definitions(mkvsdcu PRIVATE MKVSDCU_DEKO3D=1 NFSMW_DEKO3D=1)
add_subdirectory("${REXSDK_DIR}/thirdparty/uam" uam EXCLUDE_FROM_ALL)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${REXSDK_DIR}/thirdparty/SPIRV-Cross" spirv-cross EXCLUDE_FROM_ALL)
target_link_libraries(mkvsdcu PRIVATE uamlib spirv-cross-glsl spirv-cross-core)
