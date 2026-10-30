// Local fixed-step experiment. Shared timing constants remain untouched.
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <chrono>
#include <cmath>
#include <cstring>

REXCVAR_DEFINE_BOOL(mkvsdcu_30fps_timing, false, "MKvDCU",
                    "Experimental 30 Hz engine timing and 30 FPS pacing; save/restart")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(mkvsdcu_timing_diagnostics, false, "MKvDCU",
                    "Log outer engine timing and tick target every 300 frames");

namespace {
constexpr uint32_t kFrameCallReturn = 0x82799294;
constexpr uint32_t kAccumulatedTime = 0x82F84C88;

uint32_t Read32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return __builtin_bswap32(value);
}
double ReadDouble(const uint8_t* base, uint32_t address) {
  uint64_t bits;
  std::memcpy(&bits, base + address, sizeof(bits));
  bits = __builtin_bswap64(bits);
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
void WriteDouble(uint8_t* base, uint32_t address, double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = __builtin_bswap64(bits);
  std::memcpy(base + address, &bits, sizeof(bits));
}

void UpdateFrameTiming(PPCContext& ctx, uint8_t* base) {
  // r29 points to the frame-loop state. +12 holds the previous accumulated
  // double, saved by the guest just before it added one fixed step.
  const double previous = ReadDouble(base, ctx.r29.u32 + 12);
  const double current = ReadDouble(base, kAccumulatedTime);
  const double increment = current - previous;
  const double original_delta = ctx.f31.f64;
  constexpr double original_step = double(1.0f / 60.0f);
  const bool fixed_step = std::isfinite(previous) && std::isfinite(current) &&
      std::abs(original_delta - original_step) < 1e-9 &&
      std::abs(increment - original_step) < 1e-6;
  const bool requested = REXCVAR_GET(mkvsdcu_30fps_timing);
  if (requested && fixed_step) {
    ctx.f31.f64 = original_delta * 2.0;
    WriteDouble(base, kAccumulatedTime, previous + increment * 2.0);
  }
  if (!REXCVAR_GET(mkvsdcu_timing_diagnostics) && !requested) return;
  struct Stats {
    uint64_t frames = 0, applied = 0, rejected = 0;
    double simulation = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  };
  static thread_local Stats stats;
  ++stats.frames;
  stats.simulation += ctx.f31.f64;
  stats.applied += requested && fixed_step;
  stats.rejected += requested && !fixed_step;
  if (stats.frames != 1 && stats.frames % 300 != 0) return;
  const uint32_t engine = Read32(base, 0x82FC2488);
  const uint32_t vtable = engine ? Read32(base, engine) : 0;
  const uint32_t tick = vtable ? Read32(base, vtable + 180) : 0;
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stats.start).count();
  REXLOG_INFO("[frame-timing] mode={} frames={} applied={} rejected={} delta={:.9f} clock_step={:.9f} simulated={:.3f}s wall={:.3f}s engine={:08X} tick={:08X}",
      requested ? "experimental30" : "original60", stats.frames, stats.applied,
      stats.rejected, ctx.f31.f64, ReadDouble(base, kAccumulatedTime) - previous,
      stats.simulation, elapsed, engine, tick);
}
}

REX_EXTERN(__imp__sub_826344D0);
REX_HOOK_RAW(sub_826344D0) {
  // Other callers and alternate/benchmark timing paths retain their timing.
  if (ctx.lr == kFrameCallReturn) UpdateFrameTiming(ctx, base);
  __imp__sub_826344D0(ctx, base);
}
