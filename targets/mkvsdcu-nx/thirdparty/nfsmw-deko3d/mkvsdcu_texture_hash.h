// Full-byte fingerprints for transient texture content. Never use these for
// shader identities, on-disk cache keys, or detecting only a sample of a range.
// Inspired by the ARM CRC approach described in SA2's porting guide, section 9.4.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#if defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

namespace nfsmw::nativo::texture_hash {

// Each lane uses two different degree-32 polynomials, rather than treating a
// single 32-bit CRC as a 64-bit hash. Four dependency chains fit the A57 CRC pipe.
template<class Ops>
uint64_t Fingerprint(const uint8_t* data, size_t size, uint64_t seed=0) {
  uint32_t a=uint32_t(seed)^0x9e3779b9u, b=uint32_t(seed>>32)^0x85ebca6bu;
  uint32_t c=~uint32_t(seed), d=~uint32_t(seed>>32);
  const size_t length=size;
  while(size>=16) {
    uint64_t first,second;
    std::memcpy(&first,data,8); std::memcpy(&second,data+8,8);
    a=Ops::C64(a,first); b=Ops::I64(b,first);
    c=Ops::C64(c,second); d=Ops::I64(d,second);
    data+=16; size-=16;
  }
  if(size>=8) {
    uint64_t word;std::memcpy(&word,data,8);
    a=Ops::C64(a,word); b=Ops::I64(b,word);data+=8;size-=8;
  }
  if(size>=4) {
    uint32_t word;std::memcpy(&word,data,4);
    c=Ops::C32(c,word); d=Ops::I32(d,word);data+=4;size-=4;
  }
  while(size) { c=Ops::C8(c,*data); d=Ops::I8(d,*data);++data;--size; }
  uint64_t value=(uint64_t(a)|(uint64_t(b)<<32))*0xd6e8feb86659fd93ull;
  value=std::rotl(value,23)+(uint64_t(c)|(uint64_t(d)<<32));
  value^=uint64_t(length)*0x9e3779b97f4a7c15ull;
  value^=value>>32;value*=0xa24baed4963ee407ull;value^=value>>29;
  return value;
}

// Scalar oracle also provides the startup check on the real Switch CPU.
struct Scalar {
  static uint32_t Byte(uint32_t crc,uint8_t value,uint32_t polynomial) {
    crc^=value;
    for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((0u-(crc&1u))&polynomial);
    return crc;
  }
  static uint32_t Word(uint32_t crc,uint64_t value,unsigned bytes,uint32_t polynomial) {
    while(bytes--) {crc=Byte(crc,uint8_t(value),polynomial);value>>=8;}
    return crc;
  }
  static uint32_t C64(uint32_t s,uint64_t v){return Word(s,v,8,0x82f63b78u);}
  static uint32_t I64(uint32_t s,uint64_t v){return Word(s,v,8,0xedb88320u);}
  static uint32_t C32(uint32_t s,uint32_t v){return Word(s,v,4,0x82f63b78u);}
  static uint32_t I32(uint32_t s,uint32_t v){return Word(s,v,4,0xedb88320u);}
  static uint32_t C8(uint32_t s,uint8_t v){return Byte(s,v,0x82f63b78u);}
  static uint32_t I8(uint32_t s,uint8_t v){return Byte(s,v,0xedb88320u);}
};

#if defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
inline constexpr bool kAvailable=true;
struct Hardware {
  static uint32_t C64(uint32_t s,uint64_t v){return __crc32cd(s,v);}
  static uint32_t I64(uint32_t s,uint64_t v){return __crc32d(s,v);}
  static uint32_t C32(uint32_t s,uint32_t v){return __crc32cw(s,v);}
  static uint32_t I32(uint32_t s,uint32_t v){return __crc32w(s,v);}
  static uint32_t C8(uint32_t s,uint8_t v){return __crc32cb(s,v);}
  static uint32_t I8(uint32_t s,uint8_t v){return __crc32b(s,v);}
};
#else
inline constexpr bool kAvailable=false;
using Hardware=Scalar;
#endif

inline bool SelfTest() {
  uint8_t data[273];
  for(size_t n=0;n<sizeof(data);++n) data[n]=uint8_t(n*131u+17u);
  for(size_t offset=0;offset<8;++offset) for(size_t size: {0u,1u,3u,4u,7u,8u,15u,16u,17u,63u,256u}) {
    const uint64_t seed=0x0123456789abcdefull^size;
    if(Fingerprint<Hardware>(data+offset,size,seed)!=Fingerprint<Scalar>(data+offset,size,seed)) return false;
  }
  return true;
}
} // namespace nfsmw::nativo::texture_hash
