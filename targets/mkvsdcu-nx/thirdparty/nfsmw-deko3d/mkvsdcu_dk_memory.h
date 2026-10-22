// Per-draw raw vertex reads and Xenos memory exports. Guest byte order is
// retained in these buffers; the translated shader performs format/endian work.
#pragma once
#include "nfsmw_dk_nucleo.h"
#include "nfsmw_shader_library.h"
#include <rex/system/xmemory.h>
#include <array>
#include <bit>
#include <cstring>
#include <vector>

namespace nfsmw::deko {
struct MemoriaDibujo {
  struct Range { uint32_t address, bytes, offset; };
  std::vector<Range> reads, writes;
  DkMemBlock exported = nullptr;
  Dispositivo* device = nullptr;
  uint32_t raw_bytes = 0, export_bytes = 0;
  MemoriaDibujo() = default;
  MemoriaDibujo(const MemoriaDibujo&) = delete;
  ~MemoriaDibujo() { if (exported) device->DestruirBloque(exported); }

  static uint32_t ElementBytes(uint32_t f) {
    switch(f) {
      case 6: case 7: case 25: case 31: case 33: case 36: return 4;
      case 26: case 32: case 34: case 37: return 8;
      case 35: case 38: return 16;
      default: return 0;
    }
  }
  bool Prepare(Nucleo& gpu, rex::memory::Memory& memory, const native::Shader& shader,
               const uint32_t* registers, uint32_t* shared) {
    constexpr uint64_t physical_size = 0x20000000;
    auto mask=shader.raw_fetch;
    for(uint32_t element=0;element<32;++element) {
      if(!(shader.bound_fetch&(1u<<element))) continue;
      const uint32_t slot=shared[320+element*4];
      if(slot>=96) return false;
      mask[slot/32]|=1u<<(slot%32);
    }
    for (uint32_t slot=0;slot<96;++slot) {
      if (!(mask[slot/32] & (1u<<(slot%32)))) continue;
      const uint32_t a=registers[0x4800+slot*2], b=registers[0x4801+slot*2];
      const uint32_t address=a&0x1FFFFFFC, bytes=((b>>2)&0xFFFFFF)*4;
      if ((a&3)!=3 || !bytes || uint64_t(address)+bytes>physical_size ||
          uint64_t(raw_bytes)+bytes+32 > 128u*1024*1024) return false;
      shared[124+slot]=raw_bytes|(b&3);
      reads.push_back({address,bytes,raw_bytes});
      raw_bytes=(raw_bytes+bytes+31)&~15u; // guard the final Load4 of each range
    }
    shared[220]=raw_bytes;
    if (!shader.memexport) return true;
    for (uint32_t reg=0;reg<256;++reg) {
      if (!(shader.export_constants[reg/32]&(1u<<(reg%32)))) continue;
      const uint32_t* c=registers+0x4000+reg*4;
      const uint32_t element=ElementBytes((c[2]>>8)&63);
      if ((c[0]&0xC0000000)!=0x40000000 || c[3]<0x4B000000 || !element) return false;
      const uint64_t bytes=uint64_t(c[3]-0x4B000000)*element;
      const uint32_t address=(c[0]&0x3FFFFFFF)*4;
      if (!bytes || uint64_t(address)+bytes>physical_size || bytes>128u*1024*1024) return false;
      bool duplicate=false;
      for (const auto& w:writes) duplicate|=w.address==address && w.bytes==bytes;
      if (duplicate) continue;
      if (writes.size()==4 || uint64_t(export_bytes)+bytes>128u*1024*1024) return false;
      const uint32_t index=uint32_t(writes.size());
      shared[224+index*4]=address;
      shared[225+index*4]=uint32_t(bytes);
      shared[226+index*4]=export_bytes;
      writes.push_back({address,uint32_t(bytes),export_bytes});
      export_bytes=(export_bytes+uint32_t(bytes)+15)&~15u;
    }
    if (writes.empty()) return false;
    export_bytes=(export_bytes+4095)&~4095u;
    device=&gpu.disp();
    exported=device->CrearBloque(export_bytes,DkMemBlockFlags_CpuCached|DkMemBlockFlags_GpuUncached,"MKvDCU memory export");
    auto* cpu=static_cast<uint8_t*>(dkMemBlockGetCpuAddr(exported));
    std::memset(cpu,0,export_bytes);
    for (const auto& w:writes) std::memcpy(cpu+w.offset,memory.TranslatePhysical(w.address),w.bytes);
    dkMemBlockFlushCpuCache(exported,0,export_bytes);
    return true;
  }
  bool Bind(Nucleo& gpu, rex::memory::Memory& memory) {
    if (raw_bytes) {
      const auto upload=gpu.Reservar(raw_bytes,16);
      if (!upload) return false;
      std::memset(upload.cpu,0,raw_bytes);
      for (const auto& r:reads) std::memcpy(static_cast<uint8_t*>(upload.cpu)+r.offset,memory.TranslatePhysical(r.address),r.bytes);
      dkCmdBufBindStorageBuffer(gpu.comandos(),DkStage_Vertex,15,upload.gpu,raw_bytes);
    }
    if (exported)
      dkCmdBufBindStorageBuffer(gpu.comandos(),DkStage_Vertex,14,dkMemBlockGetGpuAddr(exported),export_bytes);
    return true;
  }
  void CopyBack(rex::memory::Memory& memory) {
    if (!exported) return;
    dkMemBlockFlushCpuCache(exported,0,export_bytes);
    const auto* cpu=static_cast<const uint8_t*>(dkMemBlockGetCpuAddr(exported));
    for (const auto& w:writes) std::memcpy(memory.TranslatePhysical(w.address),cpu+w.offset,w.bytes);
  }
};
} // namespace nfsmw::deko
