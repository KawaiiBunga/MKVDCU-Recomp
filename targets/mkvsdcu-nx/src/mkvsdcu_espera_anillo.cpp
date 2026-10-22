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
#if MKVSDCU_DEKO3D
#include <condition_variable>
#include <mutex>
namespace nfsmw::nativo {
namespace {
std::mutex progreso_mutex;
std::condition_variable progreso_cv;
std::atomic<uint32_t> progreso{0};
unsigned esperando = 0;
}
void AvisarProgresoAnillo() {
  bool avisar;
  {
    std::lock_guard lock(progreso_mutex);
    progreso.fetch_add(1, std::memory_order_release);
    avisar = esperando != 0;
  }
  if (avisar) progreso_cv.notify_all();
}
uint32_t ProgresoAnillo() { return progreso.load(std::memory_order_acquire); }
void EsperarProgresoAnillo(uint32_t visto, int32_t us) {
  std::unique_lock lock(progreso_mutex);
  ++esperando;
  progreso_cv.wait_for(lock, std::chrono::microseconds(us), [visto] {
    return progreso.load(std::memory_order_acquire) != visto;
  });
  --esperando;
}
}
#endif

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(mkvsdcu_espera_anillo_bloqueante, true, "MKvDCU",
                    "D3D ring wait: sleep/yield when waiting for GPU ring buffer in sub_827E1410 "
                    "instead of burning 100% CPU in spin loops");
REXCVAR_DEFINE_INT32(mkvsdcu_espera_anillo_us, 200, "MKvDCU",
                     "Max sleep duration per iteration waiting for GPU ring, in microseconds (0 = yield only)")
    .range(0, 10000);

REXCVAR_DEFINE_BOOL(mkvsdcu_ring_wait_diagnostics, false, "MKvDCU",
                    "Measure/log ring waits (adds clock reads; leave off for normal play)");

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

struct RingStats {
  uint64_t checks = 0, sleeps = 0, sleep_ns = 0;
  int64_t next_report_ms = 0;
};
// This function can be called by more than one guest thread. Each thread owns
// its counters, avoiding ARMv8 atomic read/modify/write traffic in the hot loop.
thread_local RingStats stats;

void ReportRingStats() {
  if ((stats.checks & 511) != 0) return;
  using namespace std::chrono;
  const int64_t now_ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  if (now_ms < stats.next_report_ms) return;
  if (stats.next_report_ms != 0) {
    REXLOG_INFO("[espera_anillo] per-thread interval: {} checks, {} sleeps ({:.2f} ms asleep)",
                stats.checks, stats.sleeps, double(stats.sleep_ns) / 1e6);
  }
  stats = {0, 0, 0, now_ms + 10000};
}

}  // namespace

REX_EXTERN(__imp__sub_827E1410);

REX_HOOK_RAW(sub_827E1410) {
  const bool diagnostics = REXCVAR_GET(mkvsdcu_ring_wait_diagnostics);
  if (diagnostics) ++stats.checks;

  if (REXCVAR_GET(mkvsdcu_espera_anillo_bloqueante)) {
#if MKVSDCU_DEKO3D
    const uint32_t progreso = nfsmw::nativo::ProgresoAnillo();
#endif
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
              if (diagnostics) ++stats.sleeps;
              const auto start = diagnostics ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};

              const int32_t sleep_us = REXCVAR_GET(mkvsdcu_espera_anillo_us);
#if MKVSDCU_DEKO3D
              if (sleep_us <= 0) std::this_thread::yield();
              else nfsmw::nativo::EsperarProgresoAnillo(progreso, sleep_us);
#elif defined(__SWITCH__)
              if (sleep_us <= 0) {
                svcSleepThread(-1); // YieldWithoutCoreMigration; positive values request a timed sleep.
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
              if (diagnostics) {
                stats.sleep_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
              }
            }
          }
        }
      }
    }
  }

  if (diagnostics) ReportRingStats();

  // Call original function to maintain exact state transitions and return values
  __imp__sub_827E1410(ctx, base);
}
