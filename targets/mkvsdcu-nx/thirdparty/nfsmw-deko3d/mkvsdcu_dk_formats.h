#pragma once
#include <deko3d.h>
#include <bit>
#include <cstdint>
#include "mkvsdcu_resolve_format.h"

namespace nfsmw::nativo {
inline DkImageFormat NativeRenderFormat(uint32_t format) {
  switch(format) {
    case 0: case 1: return DkImageFormat_RGBA8_Unorm;
    case 2: case 10: return DkImageFormat_RGB10A2_Unorm;
    case 6: return DkImageFormat_RG16_Float;
    case 7: return DkImageFormat_RGBA16_Float;
    case 14: return DkImageFormat_R32_Float;
    case 15: return DkImageFormat_RG32_Float;
    default: return DkImageFormat_None;
  }
}
inline DkImageFormat NativeResolveFormat(uint32_t format) {
  switch(format) {
    case 6: case 14: case 50: return DkImageFormat_RGBA8_Unorm;
    case 7: case 54: return DkImageFormat_RGB10A2_Unorm;
    case 31: return DkImageFormat_RG16_Float;
    case 32: return DkImageFormat_RGBA16_Float;
    case 36: return DkImageFormat_R32_Float;
    case 37: return DkImageFormat_RG32_Float;
    default: return DkImageFormat_None;
  }
}
inline uint32_t NativePixelLog2(DkImageFormat format) {
  return format==DkImageFormat_RGBA16_Float || format==DkImageFormat_RG32_Float ? 3 : 2;
}
inline void NativeClearColor(uint32_t format,uint64_t packed,float color[4]) {
  color[0]=color[1]=color[2]=0; color[3]=1;
  if(format==0 || format==1) {
    for(unsigned c=0;c<4;++c) color[c]=float((packed>>(c*8))&255)/255.0f;
  } else if(format==2 || format==10) {
    for(unsigned c=0;c<3;++c) color[c]=float((packed>>(c*10))&1023)/1023.0f;
    color[3]=float(packed>>30&3)/3.0f;
  } else if(format==6 || format==7) {
    for(unsigned c=0;c<(format==6?2u:4u);++c)
      color[c]=float(std::bit_cast<_Float16>(uint16_t(packed>>(c*16))));
  } else if(format==14 || format==15) {
    color[0]=std::bit_cast<float>(uint32_t(packed));
    if(format==15) color[1]=std::bit_cast<float>(uint32_t(packed>>32));
  }
}
}
