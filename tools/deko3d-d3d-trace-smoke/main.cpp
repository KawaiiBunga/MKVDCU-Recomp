// Tests the real diagnostic hooks against synthetic guest memory and original
// function stubs. It does not run or emulate the game.
#include <rex/ppc/context.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
thread_local PPCContext expected{};
std::atomic<unsigned> calls{0};
void Require(bool condition) { if (!condition) std::abort(); }
void Original(PPCContext& ctx) {
  Require(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
  ++calls;
  ctx.r3.u64 = 0x123456789ABCDEF0;
}
}

#define ORIGINAL(address) \
  extern "C" REX_FUNC(__imp__sub_##address) { (void)base; Original(ctx); }
ORIGINAL(827E1D68)
ORIGINAL(827EB600)
ORIGINAL(827EB7F8)
ORIGINAL(827F1758)
ORIGINAL(827F1880)
ORIGINAL(827F1940)
ORIGINAL(827F1DD8)
ORIGINAL(827F1DE8)
ORIGINAL(827F1E30)
ORIGINAL(827F2350)
ORIGINAL(827F23F0)
ORIGINAL(827F27E0)
ORIGINAL(827F30B0)
ORIGINAL(827F4538)
ORIGINAL(827F7860)
#undef ORIGINAL

REX_EXTERN(sub_827F23F0);
REX_EXTERN(sub_827E1D68);
REX_EXTERN(sub_827F7860);
REX_EXTERN(sub_827EB7F8);

int main(int argc, char** argv) {
  if (argc != 2 || _putenv_s("MKVSDCU_D3D_TRACE_PATH", argv[1])) return 2;
  const std::string shader_directory = std::string(argv[1]) + ".shaders";
  if (_putenv_s("MKVSDCU_D3D_TRACE_SHADER_DIR", shader_directory.c_str())) return 2;
  std::vector<uint8_t> memory(0x10000);
  constexpr uint32_t device = 0x1000;
  for (uint32_t offset : {0x2E2Cu, 0x3094u, 0x3098u, 0x30A8u, 0x3194u, 0x3198u}) {
    const uint32_t value = 0xA1230000u | offset;
    for (unsigned n = 0; n < 4; ++n)
      memory[device + offset + n] = uint8_t(value >> (24 - n * 8));
  }
  const auto put = [&](uint32_t offset, uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) memory[offset + n] = uint8_t(value >> (24 - n * 8));
  };
  constexpr uint32_t shader_object = 0x2000, container = shader_object + 0x368;
  put(container, 0x102A1101);
  put(container + 4, 0x3C);
  put(container + 8, 24);
  put(container + 24, 0x24);
  put(container + 0x28, 24);
  for (unsigned n = 0; n < 24; ++n) memory[0x8000 + n] = uint8_t(n);
  const auto call = [&](PPCFunc* hook, uint32_t pointer) {
    PPCContext ctx{};
    ctx.r3.u32 = pointer;
    ctx.r4.u64 = 0xAABBCCDD12345678;
    if (hook == sub_827EB7F8) ctx.r4.u64 = 0x8000;
    ctx.r7.u64 = 0x8877665544332211;
    ctx.r1.u32 = 0xF000;
    ctx.lr = 0x82250004;
    ctx.f1.u64 = 0x7FF8000000000055;
    expected = ctx;
    hook(ctx, memory.data());
    expected.r3.u64 = 0x123456789ABCDEF0;
    Require(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
  };
  call(sub_827F23F0, device);
  call(sub_827E1D68, device);
  call(sub_827F23F0, 0xFFFFF000);  // The diagnostic must not dereference it.
  call(sub_827EB7F8, shader_object);
  call(sub_827EB7F8, 0xFFFFF000);
  std::array<std::thread, 4> threads;
  for (auto& thread : threads) thread = std::thread([&] {
    for (unsigned n = 0; n < 100; ++n) call(sub_827F7860, device);
  });
  for (auto& thread : threads) thread.join();
  Require(calls == 405);
  std::printf("405 original calls preserved, including shader identity, concurrent and invalid-pointer captures\n");
}
