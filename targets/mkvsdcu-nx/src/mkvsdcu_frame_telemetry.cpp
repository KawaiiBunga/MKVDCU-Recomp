#include <atomic>
#include <rex/ppc/func.h>

extern std::atomic<uint64_t> g_mkvsdcu_frames_rendered;
#if MKVSDCU_DEKO3D
extern std::atomic<uint64_t> g_nfsmw_fotogramas_juego;
#endif
extern "C" REX_FUNC(__real___imp__VdSwap);

// --wrap preserves the original kernel export. Generated import bindings and
// direct calls both pass through this wrapper; no game address is guessed.
extern "C" REX_FUNC(__wrap___imp__VdSwap) {
  g_mkvsdcu_frames_rendered.fetch_add(1, std::memory_order_relaxed);
#if MKVSDCU_DEKO3D
  g_nfsmw_fotogramas_juego.fetch_add(1, std::memory_order_relaxed);
#endif
  __real___imp__VdSwap(ctx, base);
}
