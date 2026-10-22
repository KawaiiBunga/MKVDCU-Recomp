cmake_minimum_required(VERSION 3.25)
project(mkvsdcu_fixed_shaders LANGUAGES CXX)
set(REXSDK "${CMAKE_CURRENT_SOURCE_DIR}/../../references/nfsmw-nx-main/sdk" CACHE PATH "Existing SDK dependency headers")
add_executable(mkvsdcu-fixed-shaders main.cpp XenosRecomp/shader_recompiler.cpp)
target_compile_features(mkvsdcu-fixed-shaders PRIVATE cxx_std_23)
target_include_directories(mkvsdcu-fixed-shaders PRIVATE . XenosRecomp "${REXSDK}/thirdparty/fmt/include" "${REXSDK}/thirdparty/xxHash")
target_compile_options(mkvsdcu-fixed-shaders PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/XenosRecomp/pch_min.h")
target_compile_definitions(mkvsdcu-fixed-shaders PRIVATE FMT_HEADER_ONLY XXH_INLINE_ALL NFSMW_RECOMP)
target_link_options(mkvsdcu-fixed-shaders PRIVATE -static)

set(SPIRV_CROSS_SOURCE "E:/tools/src/SPIRV-Cross-vulkan-sdk-1.4.363.0" CACHE PATH "Pinned SPIRV-Cross")
set(SPIRV_CROSS_BUILD "E:/tools/build/spirv-cross" CACHE PATH "Existing native SPIRV-Cross libraries")
set(NATIVE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../targets/mkvsdcu-nx/thirdparty/nfsmw-deko3d")
add_executable(mkvsdcu-native-glsl native_glsl.cpp "${NATIVE_SOURCE}/nfsmw_dk_glsl.cpp")
target_compile_features(mkvsdcu-native-glsl PRIVATE cxx_std_23)
target_include_directories(mkvsdcu-native-glsl PRIVATE "${NATIVE_SOURCE}" "${SPIRV_CROSS_SOURCE}")
target_link_libraries(mkvsdcu-native-glsl PRIVATE
    "${SPIRV_CROSS_BUILD}/libspirv-cross-glsl.a" "${SPIRV_CROSS_BUILD}/libspirv-cross-core.a")
target_link_options(mkvsdcu-native-glsl PRIVATE -static)

add_executable(mkvsdcu-native-library-check native_library_check.cpp
    "${NATIVE_SOURCE}/nfsmw_shader_library.cpp" "${NATIVE_SOURCE}/nfsmw_nativo_shaders.cpp"
    "${NATIVE_SOURCE}/nfsmw_dk_cache_dksh.cpp")
target_compile_features(mkvsdcu-native-library-check PRIVATE cxx_std_23)
target_include_directories(mkvsdcu-native-library-check PRIVATE test-shim "${NATIVE_SOURCE}" "${REXSDK}/thirdparty/xxHash" "${REXSDK}/thirdparty/fmt/include")
target_link_options(mkvsdcu-native-library-check PRIVATE -static)
add_executable(mkvsdcu-live-shader-check live_shader_check.cpp
    "${NATIVE_SOURCE}/nfsmw_shader_library.cpp" "${NATIVE_SOURCE}/nfsmw_nativo_shaders.cpp")
target_compile_features(mkvsdcu-live-shader-check PRIVATE cxx_std_23)
target_include_directories(mkvsdcu-live-shader-check PRIVATE test-shim "${NATIVE_SOURCE}"
    "${REXSDK}/thirdparty/xxHash" "${REXSDK}/thirdparty/fmt/include")
target_link_options(mkvsdcu-live-shader-check PRIVATE -static)
add_executable(mkvsdcu-rectangle-glsl rectangle_glsl.cpp)
target_compile_features(mkvsdcu-rectangle-glsl PRIVATE cxx_std_23)
target_include_directories(mkvsdcu-rectangle-glsl PRIVATE "${NATIVE_SOURCE}")
target_link_options(mkvsdcu-rectangle-glsl PRIVATE -static)
