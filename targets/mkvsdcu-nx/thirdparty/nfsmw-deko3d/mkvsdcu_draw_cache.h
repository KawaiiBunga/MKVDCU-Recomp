// Complete-key, bounded draw caches. Hashes select slots; equality proves hits.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mkvsdcu {
struct TextureBinding {
  uint32_t image=0, sampler=0, width=0, height=0, control=0;
  uint32_t inverse_width=0, inverse_height=0;
  uint64_t valid_until=0;
  bool operator==(const TextureBinding&) const = default;
};
template<size_t Slots=512> class TextureBindingCache {
  static_assert(Slots && !(Slots & (Slots-1)));
  struct Entry {
    std::array<uint32_t,6> fetch{};
    uint32_t description=0;
    uint64_t generation=0, work=0;
    bool valid=false;
    TextureBinding value;
  };
  std::array<Entry,Slots> entries_{};
  static size_t Index(const uint32_t* fetch,uint32_t description) {
    uint32_t h=description^0x9E3779B9u;
    for(unsigned i=0;i<6;++i) h=((h<<5)|(h>>27))^(fetch[i]*0x85EBCA6Bu);
    h^=h>>16;h*=0x7FEB352Du;h^=h>>15;h*=0x846CA68Bu;h^=h>>16;
    return h&(Slots-1);
  }
 public:
  const TextureBinding* Find(const uint32_t* fetch,uint32_t description,
      uint64_t frame,uint64_t generation,uint64_t work) const {
    const auto& e=entries_[Index(fetch,description)];
    return e.valid && e.description==description && e.generation==generation && e.work==work &&
      frame<=e.value.valid_until && !std::memcmp(e.fetch.data(),fetch,24) ? &e.value : nullptr;
  }
  void Store(const uint32_t* fetch,uint32_t description,uint64_t generation,
      uint64_t work,const TextureBinding& value) {
    auto& e=entries_[Index(fetch,description)];
    std::memcpy(e.fetch.data(),fetch,24);e.description=description;
    e.generation=generation;e.work=work;e.value=value;e.valid=true;
  }
};

// Irregular selection avoids repeatedly timing the same draw of a periodic scene.
inline bool SampleDraw(uint32_t sequence,uint32_t interval) {
  if(!interval) return false;
  if(interval==1) return true;
  uint32_t h=sequence*0x9E3779B9u;h^=h>>16;h*=0x85EBCA6Bu;h^=h>>13;
  return uint64_t(h)<(uint64_t(1)<<32)/interval;
}
}
