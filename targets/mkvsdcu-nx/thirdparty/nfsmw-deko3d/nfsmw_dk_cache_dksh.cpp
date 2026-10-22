// nfsmw - deko3d renderer: the DKSH cache file. See nfsmw_dk_cache_dksh.h.

#include "nfsmw_dk_cache_dksh.h"

#include "nfsmw_dk_glsl.h"

#include <cstring>
#include <algorithm>
#include <array>
#include <stdexcept>
// The same xxHash setup as nfsmw_shader_library.cpp (reads through memcpy: see the note there).
#if defined(XXH_IMPLEM_13a8737387)
#error "xxhash.h ya se ha incluido con su implementacion antes de este punto: XXH_FORCE_MEMORY_ACCESS 0 llegaria tarde"
#endif
#undef XXH_FORCE_MEMORY_ACCESS
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include <xxhash.h>

namespace nfsmw::deko {
namespace {

constexpr uint32_t kMagia = 0x4B44464Eu;  // "NFDK"
constexpr uint32_t kVersion = 2;
constexpr size_t kCabecera = 32;
constexpr size_t kEntrada = 24;
constexpr uint32_t kMaxEntradas = 1u << 16;
constexpr uint32_t kMaxDksh = 4u << 20;

uint32_t U32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
uint64_t U64(const uint8_t* p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
void Poner(std::vector<uint8_t>& d, size_t at, const void* v, size_t n) { std::memcpy(d.data() + at, v, n); }

uint64_t HuellaId() {
  const std::string id = IdCompilador();
  return XXH3_64bits(id.data(), id.size());
}

}  // namespace

std::string IdCompilador() {
  return "MKvDCU fixed ABI 4; uam SA2 adf3a4eee524dcca5cd7c1b42454ddb8a09124a1 (patches 1-8); SPIRV-Cross vulkan-sdk-1.4.363.0; GLSL revision " +
         std::to_string(kRevisionGlsl);
}
uint64_t HuellaCompilador() { return HuellaId(); }

uint64_t HuellaSpirv(std::span<const uint32_t> espirv) { return XXH3_64bits(espirv.data(), espirv.size_bytes()); }

bool CacheDksh::Cargar(std::span<const uint8_t> archivo, std::string& motivo) {
  entradas_.clear();
  indice_archivo_.clear();
  archivo_.close();
  temporal_archivo_.clear();
  if (archivo.size() < kCabecera || U32(archivo.data()) != kMagia) {
    motivo = "no es una cache de DKSH";
    return false;
  }
  if (U32(archivo.data() + 4) != kVersion) {
    motivo = "otra version del formato";
    return false;
  }
  if (U64(archivo.data() + 8) != HuellaId()) {
    motivo = "hecha por otro compilador (se espera: " + IdCompilador() + ")";
    return false;
  }
  const uint32_t n = U32(archivo.data() + 16);
  if (n > kMaxEntradas || archivo.size() < kCabecera + size_t(n) * kEntrada ||
      XXH3_64bits(archivo.data() + kCabecera, archivo.size() - kCabecera) != U64(archivo.data() + 24)) {
    motivo = "danada";
    return false;
  }
  size_t datos = kCabecera + size_t(n) * kEntrada;
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* e = archivo.data() + kCabecera + size_t(i) * kEntrada;
    const uint32_t bytes = U32(e + 16);
    if (bytes > kMaxDksh || datos + bytes > archivo.size()) {
      entradas_.clear();
      motivo = "danada";
      return false;
    }
    entradas_[{U64(e), U32(e + 8), U32(e + 12)}].assign(archivo.data() + datos, archivo.data() + datos + bytes);
    datos = (datos + bytes + 3) & ~size_t(3);
  }
  return true;
}

bool CacheDksh::CargarArchivo(const std::filesystem::path& ruta, std::string& motivo) {
  entradas_.clear();
  indice_archivo_.clear();
  archivo_.close();
  archivo_.clear();
  temporal_archivo_.clear();
  std::ifstream input(ruta, std::ios::binary | std::ios::ate);
  const auto length = input ? input.tellg() : std::streampos(-1);
  if (length < std::streamoff(kCabecera) || length > std::streamoff(UINT32_MAX)) {
    motivo = "cache ausente o longitud invalida";
    return false;
  }
  input.seekg(0);
  std::array<uint8_t, kCabecera> header{};
  if (!input.read(reinterpret_cast<char*>(header.data()), header.size()) ||
      U32(header.data()) != kMagia || U32(header.data()+4) != kVersion ||
      U64(header.data()+8) != HuellaId() || U32(header.data()+20) != 0) {
    motivo = "cabecera o compilador incompatible";
    return false;
  }
  const uint32_t count = U32(header.data()+16);
  if (!count || count > kMaxEntradas || uint64_t(kCabecera)+uint64_t(count)*kEntrada > uint64_t(length)) {
    motivo = "indice de cache invalido";
    return false;
  }
  // Bound memory use independently of cache size (hundreds of MiB for this title).
  auto hash = XXH3_createState();
  if (!hash) { motivo = "sin memoria para verificar la cache"; return false; }
  XXH3_64bits_reset(hash);
  std::array<uint8_t, 64*1024> block{};
  uint64_t remaining = uint64_t(length)-kCabecera;
  while (remaining) {
    const size_t bytes = size_t(std::min<uint64_t>(remaining, block.size()));
    if (!input.read(reinterpret_cast<char*>(block.data()), bytes)) {
      XXH3_freeState(hash); motivo = "cache truncada"; return false;
    }
    XXH3_64bits_update(hash, block.data(), bytes);
    remaining -= bytes;
  }
  const uint64_t digest = XXH3_64bits_digest(hash);
  XXH3_freeState(hash);
  if (digest != U64(header.data()+24)) { motivo = "cache alterada"; return false; }
  input.clear(); input.seekg(kCabecera);
  std::map<Clave, std::pair<uint64_t, uint32_t>> index;
  uint64_t offset = kCabecera+uint64_t(count)*kEntrada;
  for (uint32_t i=0; i<count; ++i) {
    std::array<uint8_t,kEntrada> row{};
    if (!input.read(reinterpret_cast<char*>(row.data()),row.size())) {
      motivo = "indice truncado"; return false;
    }
    const Clave key{U64(row.data()),U32(row.data()+8),U32(row.data()+12)};
    const uint32_t bytes=U32(row.data()+16);
    if (bytes<32 || bytes>kMaxDksh || U32(row.data()+20) || offset+bytes>uint64_t(length) ||
        (!index.empty() && !(index.rbegin()->first<key))) {
      motivo = "entrada de cache invalida"; return false;
    }
    index.emplace(key,std::pair{offset,bytes});
    offset=(offset+bytes+3)&~uint64_t(3);
  }
  if (offset!=uint64_t(length)) { motivo="longitud de cache incoherente"; return false; }
  indice_archivo_=std::move(index);
  archivo_=std::move(input);
  return true;
}

std::vector<uint8_t> CacheDksh::Serializar() const {
  if (!indice_archivo_.empty()) throw std::logic_error("Immutable offline cache cannot be rewritten");
  size_t total = kCabecera + entradas_.size() * kEntrada;
  for (const auto& [clave, dksh] : entradas_) {
    total = (total + dksh.size() + 3) & ~size_t(3);
  }
  std::vector<uint8_t> d(total, 0);
  const uint32_t cabecera[2] = {kMagia, kVersion};
  Poner(d, 0, cabecera, 8);
  const uint64_t id = HuellaId();
  Poner(d, 8, &id, 8);
  const uint32_t n = uint32_t(entradas_.size());
  Poner(d, 16, &n, 4);
  size_t e = kCabecera;
  size_t datos = kCabecera + entradas_.size() * kEntrada;
  for (const auto& [clave, dksh] : entradas_) {
    const uint32_t bytes = uint32_t(dksh.size());
    Poner(d, e, &clave.huella, 8);
    Poner(d, e + 8, &clave.especializacion, 4);
    Poner(d, e + 12, &clave.variante, 4);
    Poner(d, e + 16, &bytes, 4);
    e += kEntrada;
    if (bytes) {
      Poner(d, datos, dksh.data(), bytes);
    }
    datos = (datos + bytes + 3) & ~size_t(3);
  }
  const uint64_t integridad = XXH3_64bits(d.data() + kCabecera, d.size() - kCabecera);
  Poner(d, 24, &integridad, 8);
  return d;
}

const std::vector<uint8_t>* CacheDksh::Buscar(const Clave& clave) const {
  const auto it = entradas_.find(clave);
  if (it != entradas_.end()) return &it->second;
  const auto disk=indice_archivo_.find(clave);
  if (disk==indice_archivo_.end()) return nullptr;
  temporal_archivo_.resize(disk->second.second);
  archivo_.clear(); archivo_.seekg(std::streamoff(disk->second.first));
  if (!archivo_.read(reinterpret_cast<char*>(temporal_archivo_.data()),temporal_archivo_.size()) ||
      std::memcmp(temporal_archivo_.data(),"DKSH",4)) return nullptr;
  return &temporal_archivo_;
}

void CacheDksh::Anadir(const Clave& clave, std::vector<uint8_t> dksh) {
  indice_archivo_.erase(clave);
  entradas_[clave] = std::move(dksh);
}

}  // namespace nfsmw::deko
