"""Exercise production XDK clear recognition/replay with captured microcode."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parent.parent
native=root/'targets/mkvsdcu-nx/thirdparty/nfsmw-deko3d'
def function(text, signature):
    start=text.index(signature)
    end=text.index('{',start)+1
    depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}')
        end+=1
    return text[start:end]
draw=(native/'nfsmw_dk_dibujos.cpp').read_text()
targets=(native/'nfsmw_dk_destinos.cpp').read_text()
xenos=(root/'references/rexglue-sdk/src/graphics/xenos.cpp').read_text()
source=r'''
#include "mkvsdcu_edram_clear.h"
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#undef assert
#define assert(value) do { if(!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::exit(1); } } while(0)
bool aliases=true;
#define REXCVAR_GET(...) aliases
#define REXLOG_INFO(...) ((void)0)
namespace rex { unsigned lzcnt(unsigned x) { return std::countl_zero(x); }
namespace memory { template<class T, class U> T Reinterpret(U value) { return std::bit_cast<T>(value); } } }
namespace nfsmw::nativo {
namespace xenos {
enum class SourceSelect { kDMA=0, kAutoIndex=2 };
enum class Endian { kNone, k8in16, k8in32, k16in32 };
uint32_t GpuSwap(uint32_t v,Endian e) {
  if(e==Endian::k8in32) return std::byteswap(v);
  if(e==Endian::k8in16) return ((v&0xff00ff)<<8)|((v>>8)&0xff00ff);
  if(e==Endian::k16in32) return (v<<16)|(v>>16);
  return v;
}
''' + function(xenos,'uint32_t Float32To20e4(')+function(xenos,'float Float20e4To32(')+r'''
}
namespace gr { enum { XE_GPU_REG_VGT_DRAW_INITIATOR, XE_GPU_REG_RB_DEPTHCONTROL,
  XE_GPU_REG_RB_COLORCONTROL,XE_GPU_REG_RB_STENCILREFMASK,XE_GPU_REG_PA_SU_VTX_CNTL,
  XE_GPU_REG_PA_CL_CLIP_CNTL }; }
constexpr unsigned kRegFetch=0x4800,kMemoriaFisica=0x20000000;
enum { DkImageFormat_Z24S8=1,DkImageFormat_ZF32_X24S8=2 };
struct DkScissor { uint32_t x,y,width,height; };
using DkCmdBuf=void*;
struct Image { EdramSurface edram{}; uint64_t edram_clear_serial=0,version=0;
  uint32_t ancho=520,alto=720,formato=DkImageFormat_ZF32_X24S8,imagen=0,serie=0; };
unsigned gpu_clears=0,bound_image=0; float cleared_depth=-1; unsigned cleared_stencil_mask=0;
DkScissor cleared_scissor{};
unsigned VistaCompleta(unsigned image) { return image; }
void dkCmdBufBindRenderTargets(DkCmdBuf,void*,unsigned,const unsigned* image) { bound_image=*image; }
void dkCmdBufSetScissors(DkCmdBuf,unsigned,const DkScissor* s,unsigned) { cleared_scissor=*s; }
void dkCmdBufClearDepthStencil(DkCmdBuf,bool depth,float z,uint8_t mask,uint8_t) {
  assert(depth); ++gpu_clears; cleared_depth=z; cleared_stencil_mask=mask;
}
struct Shader { std::vector<uint32_t> microcodigo; };
struct PeticionDibujo { const uint32_t* registros; Shader* vs; Shader* ps; };
struct Memory {
  std::array<uint8_t,65536> data{};
  uint8_t* TranslatePhysical(uint32_t address) { assert(address<data.size());return data.data()+address; }
};
struct RenderDk {
  struct DepthAliasClear { EdramSurface surface; EdramClearRect rectangle; uint64_t serial;
    uint32_t depth_bits; uint8_t stencil,stencil_mask; bool depth,valid; };
  std::array<Image*,5> imagenes_pase_{};
  std::vector<DepthAliasClear> depth_alias_clears_;
  uint64_t depth_alias_serial_=0,depth_alias_applied_=0;
  std::unordered_map<uint32_t,uint32_t> borrado_previo_;
  Memory* memoria_;
  struct { void* comandos() { return nullptr; } } nucleo_;
  bool RecordDepthClear(const PeticionDibujo&,uint32_t,uint32_t,const float*,const float*,const DkScissor&);
  void ApplyDepthAliasClears(Image&);
  void InvalidateDepthAliasClears(uint32_t);
  void AntesDeCambiarDestino(Image*) {}
  void HacerBorradoPendiente(Image&) {}
  void CompletarBanda(Image&,uint32_t,bool,uint32_t) {}
  void AntesDe3D() {}
  void OlvidarEstado() {}
};
using ImagenDk=Image;
''' + function(draw,'bool RenderDk::RecordDepthClear(') + function(targets,'void RenderDk::InvalidateDepthAliasClears(') + function(targets,'void RenderDk::ApplyDepthAliasClears(')+r'''
std::vector<uint32_t> capture(const char* name) {
  std::ifstream f(name,std::ios::binary);std::vector<char> data{std::istreambuf_iterator<char>(f),{}};
  assert(data.size()>16);std::vector<uint32_t> code((data.size()-16)/4);
  std::memcpy(code.data(),data.data()+16,data.size()-16);return code;
}
}
int main(int argc,char** argv) {
  using namespace nfsmw::nativo;
  assert(argc==3); Shader vs{capture(argv[1])},ps{capture(argv[2])};
  std::array<uint32_t,0x5000> regs{}; Memory memory;
  regs[gr::XE_GPU_REG_VGT_DRAW_INITIATOR]=(3u<<16)|(2<<6)|8;
  regs[gr::XE_GPU_REG_RB_DEPTHCONTROL]=0x8777;
  regs[gr::XE_GPU_REG_RB_STENCILREFMASK]=0xff0000;
  regs[gr::XE_GPU_REG_PA_CL_CLIP_CNTL]=1<<16;
  regs[gr::XE_GPU_REG_PA_SU_VTX_CNTL]=1; // captured clears have integral host edges
  regs[kRegFetch]=0x1003;regs[kRegFetch+1]=(21<<2)|2;
  const float coordinates[3][3]={{0,0,0},{520,0,0},{0,624,0}};
  auto upload=[&](const auto& coords) {
    for(unsigned v=0;v<3;++v) for(unsigned c=0;c<3;++c) {
      const auto word=std::byteswap(std::bit_cast<uint32_t>(coords[v][c]));
      std::memcpy(memory.data.data()+0x1000+v*28+c*4,&word,4);
    }
  };upload(coordinates);
  Image clear_image;clear_image.edram={0,520,2};RenderDk renderer;renderer.memoria_=&memory;
  renderer.imagenes_pase_[4]=&clear_image;
  const float viewport[]={0,0,520,720,0,0.5f},ndc[]={2.0f/520,-2.0f/720,-1,1};
  // Hardware log 009: the scissor includes padding below the actual clear.
  const DkScissor scissor{0,0,520,720};PeticionDibujo request{regs.data(),&vs,&ps};
  assert(renderer.RecordDepthClear(request,0,2,viewport,ndc,scissor));
  assert(renderer.depth_alias_clears_.back().depth_bits==0);
  Image scene;scene.edram={0,1040,1};scene.ancho=1040;scene.alto=1040;scene.imagen=1;
  renderer.borrado_previo_[scene.serie]=1;
  renderer.ApplyDepthAliasClears(scene);
  assert(renderer.borrado_previo_.empty());
  assert(gpu_clears==1 && bound_image==1 && cleared_depth==0 && cleared_stencil_mask==255);
  assert(cleared_scissor.width==1040 && cleared_scissor.height==624);
  renderer.ApplyDepthAliasClears(scene);assert(gpu_clears==1);
  renderer.InvalidateDepthAliasClears(0);
  Image late=scene;late.edram_clear_serial=0;renderer.ApplyDepthAliasClears(late);assert(gpu_clears==1);
  regs[gr::XE_GPU_REG_RB_DEPTHCONTROL]&=~(7<<8);
  assert(!renderer.RecordDepthClear(request,0,2,viewport,ndc,scissor));
  regs[gr::XE_GPU_REG_RB_DEPTHCONTROL]=0x8777;regs[gr::XE_GPU_REG_RB_COLORCONTROL]=8;
  assert(!renderer.RecordDepthClear(request,0,2,viewport,ndc,scissor));
  regs[gr::XE_GPU_REG_RB_COLORCONTROL]=0;
  float partial[3][3];std::memcpy(partial,coordinates,sizeof(partial));partial[1][0]=250;upload(partial);
  assert(renderer.RecordDepthClear(request,0,2,viewport,ndc,scissor));
  assert(renderer.depth_alias_clears_.back().rectangle.x1==250);
  assert(renderer.depth_alias_clears_.back().rectangle.y1==624);
  renderer.ApplyDepthAliasClears(scene);
  assert(gpu_clears==2 && cleared_scissor.width==500 && cleared_scissor.height==624);
  // A partial clear must not erase the rest of the target or a clipped border.
  const DkScissor clipped{10,20,230,580};
  assert(renderer.RecordDepthClear(request,0,2,viewport,ndc,clipped));
  const auto& clipped_rect=renderer.depth_alias_clears_.back().rectangle;
  assert(clipped_rect.x0==10 && clipped_rect.y0==20 && clipped_rect.x1==240 && clipped_rect.y1==600);
  // Fractional edges could cover only some MSAA samples: leave those alone.
  partial[1][0]=250.25f;upload(partial);
  assert(!renderer.RecordDepthClear(request,0,2,viewport,ndc,scissor));
  // Hardware's second scene alias clear is 520x312, not 520x720.
  partial[1][0]=520;partial[2][1]=312;upload(partial);
  assert(renderer.RecordDepthClear(request,0,2,viewport,ndc,scissor));
  Image single=scene;single.edram.samples=0;single.edram_clear_serial=0;
  renderer.ApplyDepthAliasClears(single);
  assert(cleared_scissor.width==1040 && cleared_scissor.height==624);
  upload(coordinates);
  aliases=false;assert(!renderer.RecordDepthClear(request,0,2,viewport,ndc,scissor));aliases=true;
  std::puts("PASS: captured XDK clear recognized with hardware's padded scissor; zero depth/stencil replayed to 2x/1x aliases; partial/scissored bounds preserved; stale/fractional/discarded clears rejected");
}
'''
env=dict(os.environ,TEMP='E:/tools/tmp',TMP='E:/tools/tmp')
with tempfile.TemporaryDirectory(prefix='mkvsdcu-clear-production-',dir='E:/tools/tmp') as directory:
    directory=Path(directory);(directory/'test.cpp').write_text(source)
    subprocess.run(['E:/tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe','-std=c++23',
        '-O2','-static','-I'+str(native),str(directory/'test.cpp'),'-o',str(directory/'test.exe')],check=True,env=env)
    capture_root=root/'research/mkvsdcu-deko3d/hardware-rendering-20261022/capture'
    subprocess.run([str(directory/'test.exe'),str(capture_root/'vs_6d38cfa50aa12e5f.ucode'),
                    str(capture_root/'ps_d40dc7ffe00e211a.ucode')],check=True,env=env,timeout=30)
