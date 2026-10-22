# Native Windows MSVC ABI using portable Clang + Microsoft headers/libraries.
# The invoking script sets INCLUDE/LIB/PATH; no VS installation is registered.
set(MKVSDCU_TOOLS_ROOT "E:/tools" CACHE PATH "Portable tools root")
set(_llvm "${MKVSDCU_TOOLS_ROOT}/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin")
set(CMAKE_C_COMPILER "${_llvm}/clang.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_llvm}/clang++.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc19.44.0)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc19.44.0)
set(CMAKE_LINKER "${MKVSDCU_TOOLS_ROOT}/msvc-bin/lld-link.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER "${MKVSDCU_TOOLS_ROOT}/windows-sdk/bin/10.0.26100.0/x64/rc.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_MT "${MKVSDCU_TOOLS_ROOT}/windows-sdk/bin/10.0.26100.0/x64/mt.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
