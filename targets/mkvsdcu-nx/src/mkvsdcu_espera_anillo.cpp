// mkvsdcu-nx - D3D ring buffer wait hook for Nintendo Switch
//
// In MKvDCU, the Direct3D rendering thread spends up to 95% of its time spinning
// in sub_827E1410 waiting for the GPU ring buffer read pointer to advance.
// On the Switch's 3 available CPU cores (1020 MHz), this CPU spin starves the
// GPU submission thread, the audio pump, and game logic threads.
//
// This hook intercepts sub_827E1410:
// When the current GPU read pointer matches the previous read pointer, we yield
// or sleep for a short bounded duration rather than spinning the CPU in db16cyc loops.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(mkvsdcu_espera_anillo_bloqueante, true, "MKvDCU",
                    "D3D ring wait: sleep/yield when waiting for GPU ring buffer in sub_827E1410 "
                    "instead of burning 100% CPU in spin loops");
REXCVAR_DEFINE_INT32(mkvsdcu_espera_anillo_us, 20, "MKvDCU",
                     "Max sleep duration per iteration waiting for GPU ring, in microseconds (0 = yield only)")
    .range(0, 10000);

namespace {

constexpr uint32_t kOffReadPtrPtr = 10896;  // Offset in D3D device to pointer to GPU read pointer
constexpr uint32_t kOffDeviceState = 10941; // State byte (bit 1: exit)

inline uint32_t ReadU32(const uint8_t* base, uint32_t addr) {
  uint32_t v = 0;
  std::memcpy(&v, base + addr, sizeof(v));
  return __builtin_bswap32(v);
}

inline uint8_t ReadU8(const uint8_t* base, uint32_t addr) {
  return base[addr];
}

std::atomic<uint64_t> g_ring_checks{0};
std::atomic<uint64_t> g_ring_sleeps{0};
std::atomic<uint64_t> g_ring_sleep_ns{0};
std::atomic<int64_t> g_next_report_ms{0};

void ReportRingStats() {
  using namespace std::chrono;
  const int64_t now_ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  int64_t next_ms = g_next_report_ms.load(std::memory_order_relaxed);
  if (now_ms < next_ms) return;
  if (next_ms == 0) {
    g_next_report_ms.store(now_ms + 10000, std::memory_order_relaxed);
    return;
  }
  if (!g_next_report_ms.compare_exchange_strong(next_ms, now_ms + 10000)) return;

  const uint64_t checks = g_ring_checks.exchange(0, std::memory_order_relaxed);
  const uint64_t sleeps = g_ring_sleeps.exchange(0, std::memory_order_relaxed);
  const uint64_t ns = g_ring_sleep_ns.exchange(0, std::memory_order_relaxed);

  REXLOG_INFO("[espera_anillo] 10s window: {} checks, {} blocking sleeps ({:.2f} ms total asleep)",
              checks, sleeps, double(ns) / 1e6);
}

}  // namespace

REX_EXTERN(__imp__sub_827E1410);

REX_HOOK_RAW(sub_827E1410) {
  g_ring_checks.fetch_add(1, std::memory_order_relaxed);

  if (REXCVAR_GET(mkvsdcu_espera_anillo_bloqueante)) {
    const uint32_t wait_struct = ctx.r3.u32;
    if (wait_struct != 0) {
      const uint32_t device = ReadU32(base, wait_struct);
      if (device != 0) {
        const uint8_t state = ReadU8(base, device + kOffDeviceState);
        if ((state & 0x02) == 0) {
          const uint32_t read_ptr_addr = ReadU32(base, device + kOffReadPtrPtr);
          if (read_ptr_addr != 0) {
            const uint32_t current_gpu_read = ReadU32(base, read_ptr_addr);
            const uint32_t prev_read = ReadU32(base, wait_struct + 8);

            if (current_gpu_read == prev_read) {
              // GPU hasn't caught up yet; yield/sleep to allow PM4 and audio threads to run.
              g_ring_sleeps.fetch_add(1, std::memory_order_relaxed);
              const auto start = std::chrono::steady_clock::now();

              const int32_t sleep_us = REXCVAR_GET(mkvsdcu_espera_anillo_us);
#if defined(__SWITCH__)
              if (sleep_us <= 0) {
                svcSleepThread(1); // 1ns = yield quantum in Horizon OS
              } else {
                svcSleepThread(static_cast<s64>(sleep_us) * 1000LL);
              }
#else
              if (sleep_us <= 0) {
                std::this_thread::yield();
              } else {
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
              }
#endif
              const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - start).count();
              g_ring_sleep_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
              ReportRingStats();
            }
          }
        }
      }
    }
  }

  // Call original function to maintain exact state transitions and return values
  __imp__sub_827E1410(ctx, base);
}
