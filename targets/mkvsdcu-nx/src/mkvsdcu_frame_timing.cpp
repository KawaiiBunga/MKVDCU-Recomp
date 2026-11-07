// Local fixed-step experiment. Shared timing constants remain untouched.
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <atomic>
#include <thread>
#include "mkvsdcu_render_cadence.h"

REXCVAR_DEFINE_BOOL(mkvsdcu_30fps_timing, false, "MKvDCU",
                    "Experimental 30 Hz engine timing and 30 FPS pacing; save/restart")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(mkvsdcu_timing_diagnostics, false, "MKvDCU",
                    "Log outer engine timing and tick target every 300 frames");
REXCVAR_DEFINE_BOOL(mkvsdcu_30fps_render, false, "MKvDCU",
                    "Experimental 30 FPS rendering with original 60 Hz simulation; save/restart")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// VdSwap may execute on the render thread. Only disable its frame cap after
// the game thread has actually entered the validated 60-tick/30-render path.
std::atomic<bool> g_mkvsdcu_render_cadence_active{false};

namespace {
constexpr uint32_t kFrameCallReturn = 0x82799294;
constexpr uint32_t kAccumulatedTime = 0x82F84C88;
using CadenceClock = std::chrono::steady_clock;
struct CadenceState {
  MkvsdcuRenderCadence scheduler;
  bool active = false, draw = true, in_engine = false;
  uint64_t ticks = 0, rendered = 0, skipped = 0, combat_ticks = 0, waits_bypassed = 0;
  double engine_ms = 0, viewport_ms = 0;
  CadenceClock::time_point report = CadenceClock::now();
};
thread_local CadenceState cadence;

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
  const bool render30 = REXCVAR_GET(mkvsdcu_30fps_render);
  const uint32_t engine = render30 ? Read32(base, 0x82FC2488) : 0;
  const uint32_t table = engine ? Read32(base, engine) : 0;
  const bool eligible = render30 && fixed_step && table &&
      Read32(base, table + 180) == 0x8262F430 &&
      Read32(base, table + 208) == 0x824A8A18;
  const auto now = CadenceClock::now();
  const auto frame = cadence.scheduler.Begin(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(), eligible);
  if (frame.active && !cadence.active) {
    cadence.ticks = cadence.rendered = cadence.skipped = cadence.combat_ticks = cadence.waits_bypassed = 0;
    cadence.engine_ms = cadence.viewport_ms = 0;
    cadence.report = now;
    REXLOG_INFO("[render30] enabled: original 1/60 ticks, alternate viewport draws, paired 30 Hz pacing");
  }
  cadence.active = frame.active;
  cadence.draw = frame.draw;
  g_mkvsdcu_render_cadence_active.store(frame.active, std::memory_order_relaxed);
  if (frame.wait_ns) std::this_thread::sleep_until(now + std::chrono::nanoseconds(frame.wait_ns));
  // The failed delta-doubling experiment cannot combine with render decimation.
  const bool requested = REXCVAR_GET(mkvsdcu_30fps_timing) && !render30;
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
  const uint32_t logged_engine = Read32(base, 0x82FC2488);
  const uint32_t vtable = logged_engine ? Read32(base, logged_engine) : 0;
  const uint32_t tick = vtable ? Read32(base, vtable + 180) : 0;
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stats.start).count();
  REXLOG_INFO("[frame-timing] mode={} frames={} applied={} rejected={} delta={:.9f} clock_step={:.9f} simulated={:.3f}s wall={:.3f}s engine={:08X} tick={:08X}",
      requested ? "experimental30" : "original60", stats.frames, stats.applied,
      stats.rejected, ctx.f31.f64, ReadDouble(base, kAccumulatedTime) - previous,
      stats.simulation, elapsed, logged_engine, tick);
}
}

REX_EXTERN(__imp__sub_826344D0);
REX_HOOK_RAW(sub_826344D0) {
  // Other callers and alternate/benchmark timing paths retain their timing.
  if (ctx.lr == kFrameCallReturn) UpdateFrameTiming(ctx, base);
  __imp__sub_826344D0(ctx, base);
}

REX_EXTERN(__imp__sub_8262F430);
REX_HOOK_RAW(sub_8262F430) {
  // Engine tick from the outer loop. Never invoke it twice or change its delta.
  const bool scoped = cadence.active && ctx.lr == 0x8279939C &&
      std::abs(ctx.f1.f64 - double(1.0f / 60.0f)) < 1e-9 && !cadence.in_engine;
  if (!scoped) { __imp__sub_8262F430(ctx, base); return; }
  cadence.in_engine = true;
  const auto start = CadenceClock::now();
  __imp__sub_8262F430(ctx, base);
  cadence.in_engine = false;
  cadence.engine_ms += std::chrono::duration<double, std::milli>(CadenceClock::now() - start).count();
  if (++cadence.ticks % 600 == 0) {
    const auto now = CadenceClock::now();
    const double seconds = std::chrono::duration<double>(now - cadence.report).count();
    REXLOG_INFO("[render30] ticks=600 rate={:.2f}/s simulation={:.3f}s wall={:.3f}s viewport_draws={} skipped={} combat_ticks={} engine={:.2f}ms/tick viewport={:.2f}ms/draw waits_bypassed={}",
        600.0 / seconds, 600.0 * double(1.0f / 60.0f), seconds,
        cadence.rendered, cadence.skipped, cadence.combat_ticks, cadence.engine_ms / 600,
        cadence.rendered ? cadence.viewport_ms / cadence.rendered : 0, cadence.waits_bypassed);
    cadence.rendered = cadence.skipped = cadence.combat_ticks = cadence.waits_bypassed = 0;
    cadence.engine_ms = cadence.viewport_ms = 0;
    cadence.report = now;
  }
}

REX_EXTERN(__imp__sub_823D4E70);
REX_HOOK_RAW(sub_823D4E70) {
  // FViewport draw from the verified engine redraw method. Other callers
  // (loading, explicit redraws, etc.) retain the complete original behavior.
  const bool scoped = cadence.in_engine && ctx.lr == 0x824A8A6C;
  if (scoped && !cadence.draw) { ++cadence.skipped; return; }
  const auto start = scoped ? CadenceClock::now() : CadenceClock::time_point{};
  __imp__sub_823D4E70(ctx, base);
  if (scoped) {
    ++cadence.rendered;
    cadence.viewport_ms += std::chrono::duration<double, std::milli>(CadenceClock::now() - start).count();
  }
}

REX_EXTERN(__imp__sub_826318F0);
REX_HOOK_RAW(sub_826318F0) {
  // World tick calls the MK combat update before viewport drawing. Count it,
  // but leave every call and all original registers/deltas intact.
  if (cadence.in_engine && ctx.lr == 0x824D078C) ++cadence.combat_ticks;
  __imp__sub_826318F0(ctx, base);
}

REX_EXTERN(__imp__sub_826037D8);
REX_HOOK_RAW(sub_826037D8) {
  // The original per-iteration engine limiter would add a 60 Hz sleep to the
  // undrawn iteration. The pair scheduler already budgets two ticks at 30 Hz.
  // All loading, worker and other sleep call sites remain intact.
  if (cadence.active && ctx.lr == 0x82799434) { ++cadence.waits_bypassed; return; }
  __imp__sub_826037D8(ctx, base);
}
