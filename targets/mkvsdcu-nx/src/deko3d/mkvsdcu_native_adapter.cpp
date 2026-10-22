// MKvDCU adapters for the generic GPL deko3d renderer. NFSMW gameplay,
// material replacements, scenery LOD and automatic quality changes are unused.
#include "nfsmw_ajustes_graficos.h"
#include "nfsmw_nativo_ganchos.h"
#include "nfsmw_nativo_shaders.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <atomic>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <unordered_set>
REXCVAR_DECLARE(bool, mkvsdcu_capture_unknown_shaders);

std::atomic<uint64_t> g_nfsmw_fotogramas_juego{0};
namespace nfsmw::ajustes {
Posproceso LeerPosproceso() { return {}; }
uint64_t VersionPosproceso() { return 0; }
bool AntialiasingFxaa() { return false; }
int ResplandorCielo() { return 0; }
int TratamientoVisual() { return 0; }
}
namespace nfsmw::escenario_lod {
std::string Resumen() { return "MKvDCU original scene LOD"; }
}
namespace nfsmw::guardia30 {
void Latir(double) {}
void Informe() {}
}

namespace nfsmw::nativo::ganchos_detalle {
void RecordarCreacion(uint32_t objeto, const EntradaShader* entrada, bool vertices);
}
namespace nfsmw::nativo {
// MKvDCU retains its original FlushState PM4 packets. No compound markers are
// emitted, so the NFSMW-only producer's diagnostic callbacks are inactive.
void ActivarConsumidorMarcadores(bool) {}
void AnotarComprobacionMarcador(bool, uint32_t, uint32_t, uint32_t, uint32_t) {}
}
namespace {
uint32_t BE(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void Remember(const uint8_t* base, uint32_t object, bool vertex, bool registration = false) {
  using namespace nfsmw::nativo;
  const auto* library = BibliotecaActiva();
  if (!library || !object || object > UINT32_MAX - 0x400) return;
  if (!registration && ShaderDeObjeto(object)) return;
  const auto* container = base + object + (vertex ? 0x368 : 0x28);
  const uint32_t flags = BE(container), virtual_bytes = BE(container+4), physical_bytes = BE(container+8);
  const uint32_t physical = BE(base + object + (vertex ? 0x20 : 0x18));
  const EntradaShader* entry = nullptr;
  if (flags == (vertex ? 0x102A1101u : 0x102A1100u) && virtual_bytes >= 36 &&
      virtual_bytes < 65536 && physical_bytes && physical_bytes < 1024*1024 && physical &&
      uint64_t(object) + (vertex ? 0x368 : 0x28) + virtual_bytes <= (uint64_t(1)<<32) &&
      uint64_t(physical)+physical_bytes <= (uint64_t(1)<<32)) {
    std::vector<uint8_t> bytes(size_t(virtual_bytes)+physical_bytes);
    std::memcpy(bytes.data(),container,virtual_bytes);
    std::memcpy(bytes.data()+virtual_bytes,base+physical,physical_bytes);
    entry = library->IdentificarContenedor(bytes);
    if (!entry && REXCVAR_GET(mkvsdcu_capture_unknown_shaders)) {
      static std::mutex capture_mutex;
      static std::unordered_set<uint32_t> captured;
      std::lock_guard lock(capture_mutex);
      if (captured.size() < 64 && captured.insert(object).second) {
        std::error_code ec;
        const auto folder = rex::filesystem::GetExecutableFolder() / "shader_capture";
        std::filesystem::create_directories(folder, ec);
        const auto path = folder / ((vertex ? "container-vs-" : "container-ps-") +
                                   std::to_string(object) + ".bin");
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      }
    }
  }
  // A registration may precede the caller's physical copy. Retry an unknown
  // object on SetShader / its first draw, before FlushState patches the fetches.
  if (entry || registration)
    ganchos_detalle::RecordarCreacion(object, entry, vertex);
}
thread_local unsigned draw_depth = 0;
void Record(const uint8_t* base, const PPCContext& ctx, nfsmw::nativo::FuncionDibujo function,
            uint32_t count_override = 0) {
  using namespace nfsmw::nativo;
  if (!ctx.r3.u32) return;
  Remember(base,BE(base+ctx.r3.u32+0x3198),true);
  Remember(base,BE(base+ctx.r3.u32+0x3194),false);
  AnotarDibujo(function,base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,
               count_override ? count_override : ctx.r7.u32);
}
}

#define MKV_REGISTER(address, vertex) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    const uint32_t object = ctx.r3.u32; \
    __imp__sub_##address(ctx,base); \
    Remember(base,object,vertex,true); \
  }
MKV_REGISTER(827EB7F8, true)
MKV_REGISTER(827EB600, false)
#undef MKV_REGISTER

#define MKV_BIND(address, vertex) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    const uint32_t object = ctx.r4.u32; \
    Remember(base,object,vertex); \
    __imp__sub_##address(ctx,base); \
  }
MKV_BIND(827EB8F0, true)
MKV_BIND(827EB638, false)
#undef MKV_BIND

#define MKV_DRAW(address, kind) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    const bool outer = draw_depth++ == 0; \
    if (outer) Record(base,ctx,nfsmw::nativo::FuncionDibujo::kind); \
    __imp__sub_##address(ctx,base); \
    if (outer) nfsmw::nativo::TerminarDibujo(); \
    --draw_depth; \
  }
MKV_DRAW(827F23F0, kVertices)
MKV_DRAW(827F27E0, kIndexados)
MKV_DRAW(827F30B0, kIndexados)
MKV_DRAW(827F1DE8, kVerticesUP)
MKV_DRAW(827F2350, kIndexadosUP)
MKV_DRAW(827F1940, kVerticesUP)
MKV_DRAW(827F1E30, kIndexadosUP)
#undef MKV_DRAW
