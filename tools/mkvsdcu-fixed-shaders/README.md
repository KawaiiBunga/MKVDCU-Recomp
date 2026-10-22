# MKvDCU fixed-binding shader tools

`UPSTREAM.json` pins the imported MIT XenosRecomp sources and their original
hashes. `XenosRecomp/LICENSE.md` covers that translator and the SA2 raw fetch /
memory export helpers. GPL helper inputs retained from NFSMW are covered by
`LICENSE`. The production GLSL converter and library/cache readers are compiled
directly from the GPL renderer sources, so offline validation uses the runtime
implementation.

Build with native llvm-mingw Clang and CMake/Ninja under `E:/tools`; point
`SPIRV_CROSS_SOURCE` and `SPIRV_CROSS_BUILD` at the pinned native build. No
restricted reNut renderer code is imported.

The fixed ABI uses stage UBO bindings 0/1, shared UBO 2 (112 uint4), read-only
raw vertex data at SSBO 15 and memory export at SSBO 14. Logical texture
instructions receive unsigned/signed bindings 2N/2N+1, with sampler 2N. The
library's version-4 metadata records the guest fetch descriptor and instruction
filter overrides. The shared UBO carries texture sign/exponent controls and
indexed-fetch descriptors. This ABI is distinct from the earlier reNut P0
feasibility audit; their SPIR-V/DKSH files cannot be interchanged.

`mkvsdcu-fixed-shaders` translates original containers and emits HLSL plus
metadata. It rejects unknown instructions rather than silently dropping them.
`mkvsdcu-native-glsl` invokes the runtime converter and rejects its known uam
hazard warnings. `mkvsdcu-native-library-check` loads the final library and
cache through production readers, checks all original identities and verifies
cache coverage and tamper rejection.

The runtime GLSL converter revision is 3. Six DXC-produced pixel shaders exposed
SPIRV-Cross expression-forwarding recompile loops. Only that failure retries
with explicit temporaries; all six then compile twice identically through uam.
The default fast output stays unchanged for the other shaders. The full native
pass must be rerun when this converter changes, even when DXC's SPIR-V is reused.
