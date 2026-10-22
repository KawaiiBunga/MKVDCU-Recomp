// Optional diagnostics for the native renderer migration. Every hook calls the
// original function, with the original context. No rendering is replaced here.
#include <rex/ppc/context.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace {
// The trace is intentionally bounded and synchronous. Use it to establish call
// coverage and state, never to compare renderer performance.
struct Trace {
  std::mutex mutex;
  FILE* file = nullptr;
  uint64_t events = 0;
  BCRYPT_ALG_HANDLE sha256 = nullptr;
  std::filesystem::path shader_directory;
  uint64_t dumped_bytes = 0;
  static constexpr uint64_t kLimit = 50000;

  Trace() {
    const char* path = std::getenv("MKVSDCU_D3D_TRACE_PATH");
    if (!path || !*path) return;
    // Never overwrite an older capture. An explicit new path is required.
    file = std::fopen(path, "wx");
    if (!file) {
      std::fprintf(stderr, "D3D trace could not create a new capture: %s\n", path);
      return;
    }
    BCryptOpenAlgorithmProvider(&sha256, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (const char* directory = std::getenv("MKVSDCU_D3D_TRACE_SHADER_DIR"); directory && *directory) {
      shader_directory = directory;
      std::error_code error;
      std::filesystem::create_directories(shader_directory, error);
      if (error) shader_directory.clear();
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::fprintf(file, "{\"schema\":\"mkvsdcu-d3d-trace-v1\","
                       "\"expected_xex_sha256\":\"2955f2e2be61ec1948cd2fd3538ad45beb04772f5ebd5e0fb1bfde484748e5a7\","
                       "\"original_renderer\":true,\"event_limit\":50000,\"qpc_frequency\":%llu}\n",
                       static_cast<unsigned long long>(frequency.QuadPart));
  }
  ~Trace() {
    if (file) std::fclose(file);
    if (sha256) BCryptCloseAlgorithmProvider(sha256, 0);
  }
};

bool Read(const uint8_t* base, uint32_t address, void* data, size_t size) {
  SIZE_T copied = 0;
  return uint64_t(address) + size <= (uint64_t{1} << 32) &&
      ReadProcessMemory(GetCurrentProcess(), base + address, data, size, &copied) && copied == size;
}

uint32_t BigWord(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool Hash(BCRYPT_ALG_HANDLE algorithm, std::vector<uint8_t>& bytes, std::array<uint8_t, 32>& hash) {
  BCRYPT_HASH_HANDLE state = nullptr;
  if (BCryptCreateHash(algorithm, &state, nullptr, 0, nullptr, 0, 0) < 0) return false;
  const bool ok = BCryptHashData(state, bytes.data(), static_cast<ULONG>(bytes.size()), 0) >= 0 &&
      BCryptFinishHash(state, hash.data(), static_cast<ULONG>(hash.size()), 0) >= 0;
  BCryptDestroyHash(state);
  return ok;
}

void ShaderIdentity(Trace& trace, uint32_t function, uint32_t object, uint32_t physical,
                    const uint8_t* base) {
  if (function != 0x827EB600 && function != 0x827EB7F8) return;
  const bool vertex = function == 0x827EB7F8;
  const uint64_t container = uint64_t(object) + (vertex ? 0x368 : 0x28);
  std::array<uint8_t, 0x24> header{};
  std::fputs(",\"shader_identity\":", trace.file);
  if (!trace.sha256 || container > UINT32_MAX ||
      !Read(base, uint32_t(container), header.data(), header.size())) {
    std::fputs("null", trace.file);
    return;
  }
  const uint32_t flags = BigWord(header.data());
  const uint32_t virtual_size = BigWord(header.data() + 4);
  const uint32_t physical_size = BigWord(header.data() + 8);
  const uint32_t shader_offset = BigWord(header.data() + 24);
  if ((flags & 0xFFFFFF00) != 0x102A1100 || bool(flags & 1) != vertex ||
      virtual_size < 0x24 || virtual_size >= 0x10000 || !physical_size || physical_size >= 0x100000 ||
      !physical ||
      shader_offset < 0x24 || shader_offset > virtual_size - 24 ||
      container + virtual_size > (uint64_t{1} << 32)) {
    std::fputs("null", trace.file);
    return;
  }
  std::vector<uint8_t> bytes(size_t(virtual_size) + physical_size);
  std::array<uint8_t, 32> hash{};
  if (!Read(base, uint32_t(container), bytes.data(), virtual_size) ||
      !Read(base, physical, bytes.data() + virtual_size, physical_size) ||
      uint64_t(BigWord(bytes.data() + shader_offset)) + BigWord(bytes.data() + shader_offset + 4) != physical_size ||
      !Hash(trace.sha256, bytes, hash)) {
    std::fputs("null", trace.file);
    return;
  }
  std::fprintf(trace.file, "{\"stage\":\"%s\",\"sha256\":\"", vertex ? "vs" : "ps");
  for (uint8_t byte : hash) std::fprintf(trace.file, "%02x", unsigned(byte));
  std::fprintf(trace.file, "\",\"virtual_bytes\":%u,\"physical_bytes\":%u}", virtual_size, physical_size);
  if (!trace.shader_directory.empty() && trace.dumped_bytes + bytes.size() <= 128 * 1024 * 1024) {
    char hex[65];
    for (unsigned n = 0; n < hash.size(); ++n) std::snprintf(hex + n * 2, 3, "%02x", unsigned(hash[n]));
    const auto path = trace.shader_directory / (std::string(vertex ? "vs_" : "ps_") + hex + ".bin");
    if (FILE* output = std::fopen(path.string().c_str(), "wbx")) {
      const bool written = std::fwrite(bytes.data(), 1, bytes.size(), output) == bytes.size();
      const bool closed = std::fclose(output) == 0;
      if (written && closed) trace.dumped_bytes += bytes.size();
      else std::fprintf(stderr, "D3D trace shader dump failed: %s\n", path.string().c_str());
    }
  }
}

void Capture(uint32_t function, const PPCContext& ctx, const uint8_t* base, bool has_device) {
  static Trace trace;
  if (!trace.file) return;
  std::lock_guard lock(trace.mutex);
  if (trace.events >= Trace::kLimit) return;
  const std::array<uint32_t, 8> args{ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
                                    ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
  // One checked copy, no direct dereference of a guest pointer. A failed copy is
  // an explicit null snapshot rather than a fabricated set of zero bindings.
  constexpr uint32_t kBegin = 0x2E2C;
  std::array<uint8_t, 0x380> shadow{};
  SIZE_T copied = 0;
  const uint64_t end = uint64_t(args[0]) + kBegin + shadow.size();
  const bool valid = has_device && args[0] && end <= (uint64_t{1} << 32) &&
      ReadProcessMemory(GetCurrentProcess(), base + args[0] + kBegin,
                        shadow.data(), shadow.size(), &copied) && copied == shadow.size();
  const auto word = [&](uint32_t offset) {
    const auto* p = shadow.data() + offset - kBegin;
    return BigWord(p);
  };
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  std::fprintf(trace.file,
      "{\"event\":%llu,\"function\":\"%08X\",\"thread\":%lu,\"qpc\":%llu,"
      "\"lr\":\"%08X\",\"sp\":\"%08X\",\"r3_r10\":[",
      static_cast<unsigned long long>(trace.events++), function,
      static_cast<unsigned long>(GetCurrentThreadId()), static_cast<unsigned long long>(counter.QuadPart),
      static_cast<uint32_t>(ctx.lr), ctx.r1.u32);
  for (size_t n = 0; n < args.size(); ++n)
    std::fprintf(trace.file, "%s\"%08X\"", n ? "," : "", args[n]);
  std::fprintf(trace.file, "],\"f1_bits\":\"%016llX\"",
               static_cast<unsigned long long>(std::bit_cast<uint64_t>(ctx.f1.f64)));
  ShaderIdentity(trace, function, args[0], args[1], base);
  std::fputs(",\"device\":", trace.file);
  if (valid) {
    std::fprintf(trace.file,
        "{\"vertex_declaration\":\"%08X\",\"indices\":\"%08X\",\"color0\":\"%08X\","
        "\"depth\":\"%08X\",\"pixel_shader\":\"%08X\",\"vertex_shader\":\"%08X\"}",
        word(0x2E2C), word(0x3094), word(0x3098), word(0x30A8), word(0x3194), word(0x3198));
  } else {
    std::fputs("null", trace.file);
  }
  std::fputs("}\n", trace.file);
  // Keep the most recent events if a renderer fault terminates the process.
  if ((trace.events & 255) == 0 || trace.events == Trace::kLimit) std::fflush(trace.file);
}
}  // namespace

#define MKV_D3D_TRACE_HOOK(address, has_device)                      \
  REX_EXTERN(__imp__sub_##address);                                 \
  extern "C" REX_FUNC(sub_##address) {                              \
    try { Capture(0x##address, ctx, base, has_device); }              \
    catch (...) { std::fputs("D3D trace capture failed\n", stderr); } \
    __imp__sub_##address(ctx, base);                                \
  }

MKV_D3D_TRACE_HOOK(827E1D68, false)  // CreateDevice
MKV_D3D_TRACE_HOOK(827EB600, false)  // RegisterPixelShader
MKV_D3D_TRACE_HOOK(827EB7F8, false)  // RegisterVertexShader
MKV_D3D_TRACE_HOOK(827F1758, true)   // ClearF
MKV_D3D_TRACE_HOOK(827F1880, true)   // Clear
MKV_D3D_TRACE_HOOK(827F1940, true)   // BeginVertices
MKV_D3D_TRACE_HOOK(827F1DD8, true)   // EndVertices
MKV_D3D_TRACE_HOOK(827F1DE8, true)   // DrawVerticesUP
MKV_D3D_TRACE_HOOK(827F1E30, true)   // BeginIndexedVertices
MKV_D3D_TRACE_HOOK(827F2350, true)   // DrawIndexedVerticesUP
MKV_D3D_TRACE_HOOK(827F23F0, true)   // DrawVertices
MKV_D3D_TRACE_HOOK(827F27E0, true)   // DrawIndexedVertices
MKV_D3D_TRACE_HOOK(827F30B0, true)   // DrawIndexedTessellatedVertices
MKV_D3D_TRACE_HOOK(827F4538, true)   // Resolve
MKV_D3D_TRACE_HOOK(827F7860, true)   // Swap

#undef MKV_D3D_TRACE_HOOK
