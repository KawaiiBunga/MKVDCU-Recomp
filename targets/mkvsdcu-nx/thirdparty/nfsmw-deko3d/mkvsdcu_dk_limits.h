// Small, compiler/library-identified resource limits, read before queue creation.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>

namespace nfsmw::deko {
inline uint32_t LimitU32(const uint8_t* p) {
  return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
}
inline uint64_t LimitU64(const uint8_t* p) {return LimitU32(p)|uint64_t(LimitU32(p+4))<<32;}
inline uint32_t DkshScratch(std::span<const uint8_t> dksh) {
  if(dksh.size()<24 || std::memcmp(dksh.data(),"DKSH",4)) return UINT32_MAX;
  const uint32_t at=LimitU32(dksh.data()+16);
  if(uint64_t(at)+24>dksh.size()) return UINT32_MAX;
  return LimitU32(dksh.data()+at+20);
}
inline uint32_t ShaderScratchLimit(const std::filesystem::path& folder,uint64_t compiler_id) {
  constexpr uint32_t fallback=1u<<20;
  std::array<uint8_t,32> limits{};
  std::array<uint8_t,24> library{};
  std::ifstream a(folder/"mkvsdcu_shader_limits.bin",std::ios::binary);
  std::ifstream b(folder/"mkvsdcu_shaders_dk.nfsp",std::ios::binary);
  if(!a.read(reinterpret_cast<char*>(limits.data()),limits.size()) ||
     !b.read(reinterpret_cast<char*>(library.data()),library.size()) ||
     std::memcmp(limits.data(),"MKVLIMIT",8) || LimitU32(limits.data()+8)!=1 ||
     LimitU64(limits.data()+16)!=compiler_id ||
     std::memcmp(library.data(),"MKVSPV\0\0",8) || LimitU32(library.data()+8)!=4 ||
     LimitU64(limits.data()+24)!=LimitU64(library.data()+16)) return fallback;
  const uint32_t bytes=LimitU32(limits.data()+12);
  return bytes>=2048 && bytes<=4u*1024*1024 && !(bytes&511) ? bytes : fallback;
}
}
