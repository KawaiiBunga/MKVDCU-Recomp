# Native Windows shader tools

All tool installations, sources used to build tools, downloads, package caches
and installation temporary files are under `E:/tools`. No Docker, WSL, MSYS
shell, Visual Studio installation or reboot was used. The existing bundled
Python is used as an interpreter; its added packages are in
`E:/tools/python-packages`.

## Pinned inputs

* devkitA64 GCC 16.1.0 and binutils 2.46.0 (release 30), newlib
  4.6.0.20260123-4, rules 1.1.1, libnx 4.12.0, deko3d 0.5.0 and switch-tools
  1.13.1. The portable root is `E:/tools/devkitPro`. Exact downloaded archive
  hashes are recorded in its `portable-packages.json`. devkitPro's package host
  returned HTTP 403 here; these unmodified packages were retrieved from the
  Leseratte devkitPro archive. That file records hashes, not signature checks.
* CMake 4.4.4 and Ninja 1.13.2: official Windows release ZIPs under `E:/tools`.
* llvm-mingw 20261006 UCRT x86_64 (Clang 23.1.3), from its official GitHub
  release. Native host compiler under `E:/tools/llvm-mingw`.
* DXC v1.9.2609: Microsoft's official Windows release, `E:/tools/dxc`. Its
  `lib/x64/dxcompiler.lib` and `bin/x64/dxcompiler.dll` are copied into the
  translator's `thirdparty/dxc-bin` directories before building.
* SPIRV-Cross `vulkan-sdk-1.4.363.0`: official Khronos source ZIP, built at
  `E:/tools/build/spirv-cross`.
* XenosRecomp and patched uam: the `app/thirdparty/reblue-XenosRecomp` and
  `app/thirdparty/uam` directories from
  [SA2 adf3a4e](https://github.com/ChanseyIsTheBest/sa2-nx/tree/adf3a4eee524dcca5cd7c1b42454ddb8a09124a1/app/thirdparty),
  copied into `E:/tools/src`. uam includes all eight of the pinned source's
  patches, including deterministic scheduling, no dual issue and the
  block-transition barrier stall fix. Built at `E:/tools/build/uam`.

The converter used by `compile-switch-dksh.py` is the pinned SA2
`tools/deko3d_glsl.py`. The script records its SHA-256 together with those of
uam and SPIRV-Cross in every output report. It does not silently apply SA2's
game-specific alpha, precision, early-depth or shader whitelist passes.

## Translator patch

`xenosrecomp-native-windows.patch` is against that exact SA2 XenosRecomp source.
It makes Clang's DXC UUID handling work with MinGW headers, separates DXC
arguments, reports failed COM calls, logs the shader being translated, and
adds an opt-in deko3d-only audit mode. Build the MKvDCU tool with
`-DCMAKE_CXX_FLAGS=-DMKVSDCU_RECOMP=1`: it adds **COLOR2 to both stage
interfaces**, required by MKvDCU's asset shaders. Substituting another color
or declaring only the pixel input would produce an incorrect interface.

Normal translator behavior is preserved unless the MKvDCU build define or
the `MKVSDCU_DEKO3D_ONLY` environment variable is set. The latter's generated
cache sources contain deko3d SPIR-V and are audit outputs, not PC Vulkan
libraries. `translate-switch-shaders.py` sets it only for its child processes.
It also sets `MKVSDCU_SHADER_AUDIT=1`, which skips runtime-cache generation and
its expensive maximum-level compression; both stage translation and DXC errors
are still checked. The default translator still builds its normal cache.

Native builds use CMake/Ninja from PowerShell with clang/clang++ on PATH:

```powershell
$env:TEMP = $env:TMP = 'E:/tools/tmp'
$env:Path = 'E:/tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin;E:/tools/ninja;' + $env:Path
$cmake = 'E:/tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
& $cmake -S E:/tools/src/XenosRecomp-sa2-adf3a4e -B E:/tools/build/xenos-recomp-mkvsdcu -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_CXX_FLAGS=-DMKVSDCU_RECOMP=1 -DXENOS_RECOMP_RENUT=ON -DXENOS_RECOMP_DXIL=OFF `
  -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DCMAKE_EXE_LINKER_FLAGS=-static `
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=E:/tools/build/xenos-audit-bin `
  -DCMAKE_INSTALL_PREFIX=E:/tools/install/xenos-recomp-mkvsdcu
& $cmake --build E:/tools/build/xenos-recomp-mkvsdcu --target XenosRecomp --parallel 5
```

## Audit workflow

Use fresh output directories, so a shader from an older run cannot make a
failed run appear successful. Set `PYTHONPATH=E:/tools/python-packages` for
the extraction dependencies (`pycryptodome`, `lzokay`, `xxhash`).

1. `scripts/audit-switch-shaders.py --game <own-game-folder> --output <audit>`
   reads the XEX and LZO-compressed UE3 assets, writes containers and records
   source offsets and hashes. It refuses output inside the input game folder.
2. `scripts/translate-switch-shaders.py --containers <audit>/containers --output <spirv-audit>`
   batches the native translator, checks every requested container has an
   output and reports failed/timed-out batches. This is a translation audit,
   not yet a merged runtime shader package.
3. `scripts/compile-switch-dksh.py --spirv <spirv-audit>/spirv --output <dksh-audit> --converter <pinned-deko3d_glsl.py>`
   converts to GLSL, compiles DKSH, checks headers and recompiles every shader
   to check deterministic bytes. Failures and warnings make the gate fail.

For a large corpus, `compile-switch-dksh-batch.py` uses the SPIRV-Cross C API
and `tools/deko3d-shader-audit`'s resident uam host tool. A compiler context is
never used from concurrent threads; separate compiler processes may run in
parallel. Build the host tool into `E:/tools/build/uam-audit` with the same
llvm-mingw as `uamlib`, and build SPIRV-Cross with `SPIRV_CROSS_SHARED=ON`.
The independent CLI and resident paths were compared on the 68 executable
shaders: converted GLSL and DKSH are identical in all 68. The resident tool's
invalid-shader recovery and repeated-compile checks also passed. Every report
pins the actual compiler and converter binaries by hash.

The resident audit supports `--resume` after a completed/failed audit report.
It requires identical tool hashes and input identities, rechecks SPIR-V and
converted GLSL, and reuses only successful deterministic DKSH whose SHA-256
still matches the report. Changed outputs fail validation. A 68-shader resume
and a modified-DKSH rejection test passed. The process timeout is configurable;
the default is two hours per resident worker because some full-output MKvDCU
vertex shaders take several minutes for repeated host compilation. A timeout
is reported as a failure, not a successful subset.

The static D3D catalog and optional original-renderer PC trace are documented
in [D3D_TRACE.md](D3D_TRACE.md).

This tests **default specialization values only**. Shader behavior, every
observed gameplay variant, pixel identity and GPU performance need subsequent
renderer and console checks. All extracted and translated shader data belongs
in ignored local output, not in public source or the platform probe NRO.

## Credits and licenses

XenosRecomp (hedge-dev, reblue/reNut contributors and the port authors): MIT;
SPIRV-Cross (Khronos): Apache-2.0; DXC (Microsoft): its distributed licenses;
uam (devkitPro/fincs): zlib, its Mesa files MIT, SwitchWakerHD patches MPL-2.0.
SA2/banjo-nx's converter and optimization scripts: GPL-3.0. See the pinned
sources' own license files and uam's `PATCHES.md`. No reNut renderer code has
been imported into the game target by this shader/toolchain work.

## Native game integration (2026-10-21)

The reNut P0 compiler reports above remain feasibility evidence. The game uses
a separately implemented fixed ABI and a pinned GPL PM4/deko3d frontend, not
the P0 blobs. See `targets/mkvsdcu-nx/DEKO3D.md`, the translator's
`tools/mkvsdcu-fixed-shaders/UPSTREAM.json`, and
`scripts/build-switch-deko3d.ps1`. The runtime's exact GLSL converter is also
compiled as a host tool, and the production container/cache readers validate
the packaged output. The immutable offline cache is indexed on disk and read
on demand, while runtime variants use a separate writable cache.

The PC diagnostic build reuses a portable MSVC 14.44.35207 CRT extracted from
Microsoft VSIX packages, plus Windows SDK NuGet 10.0.26100.9169 (headers/libs
10.0.26100.0). They live at `E:/tools/msvc` and `E:/tools/windows-sdk` with
provenance manifests; no Visual Studio installer or registry setup was used.
Native Clang/lld link the PC executable against those headers/libraries.

The fixed-ABI production reader accepts a bounded 768 MiB library for this
larger corpus; per-shader SPIR-V and DKSH are bounded at 4 MiB. The immutable
DKSH cache is checksum-verified in 64 KiB chunks and holds an on-disk index,
not every compiled blob in RAM. The native GLSL revision is 3, including a
selective explicit-temporary fallback for six SPIRV-Cross loop regressions.
