#include <atomic>
#include <chrono>
#include <thread>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
REXCVAR_DECLARE(int32_t, mkvsdcu_fps_limit);

extern std::atomic<uint64_t> g_mkvsdcu_frames_rendered;
#if MKVSDCU_DEKO3D
extern std::atomic<uint64_t> g_nfsmw_fotogramas_juego;
REXCVAR_DECLARE(bool, mkvsdcu_30fps_timing);
#endif
extern "C" REX_FUNC(__real___imp__VdSwap);

// --wrap preserves the original kernel export. Generated import bindings and
// direct calls both pass through this wrapper; no game address is guessed.
extern "C" REX_FUNC(__wrap___imp__VdSwap) {
  // This path is used by the native backend as well as the SDK GPU. Keep the
  // guest vblank clock at 60 Hz; pacing rendered frames is a separate choice.
  using Clock = std::chrono::steady_clock;
  static thread_local Clock::time_point deadline{};
  static thread_local int previous_cap = -1;
  int cap = REXCVAR_GET(mkvsdcu_fps_limit);
#if MKVSDCU_DEKO3D
  if (REXCVAR_GET(mkvsdcu_30fps_timing)) cap = 30;
#endif
  auto now = Clock::now();
  if (cap > 0) {
    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / cap));
    if (cap != previous_cap || deadline < now - period) deadline = now;
    if (deadline > now) std::this_thread::sleep_until(deadline);
    deadline += period;
  } else {
    deadline = now;
  }
  previous_cap = cap;
  g_mkvsdcu_frames_rendered.fetch_add(1, std::memory_order_relaxed);
#if MKVSDCU_DEKO3D
  g_nfsmw_fotogramas_juego.fetch_add(1, std::memory_order_relaxed);
#endif
  __real___imp__VdSwap(ctx, base);
}
