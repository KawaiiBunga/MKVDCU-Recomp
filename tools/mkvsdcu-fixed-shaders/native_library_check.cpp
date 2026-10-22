#include "nfsmw_shader_library.h"
#include "nfsmw_nativo_shaders.h"
#include "nfsmw_dk_cache_dksh.h"
#include "mkvsdcu_dk_limits.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <algorithm>

int main(int argc,char** argv) try {
  const bool extended=argc>1 && std::string_view(argv[1])=="--extended";
  if(extended) { --argc; ++argv; }
  if(argc==3 && std::string_view(argv[1])=="--limits-only") {
    std::printf("%u\n",nfsmw::deko::ShaderScratchLimit(argv[2],nfsmw::deko::HuellaCompilador()));
    return 0;
  }
  if(argc==3 && std::string_view(argv[1])=="--cache-only") {
    std::ifstream f(argv[2],std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
    nfsmw::deko::CacheDksh memory,disk; std::string reason;
    if(!memory.Cargar(bytes,reason)||!disk.CargarArchivo(argv[2],reason)||memory.size()!=disk.size())
      throw std::runtime_error("Cache check rejected: "+reason);
    for(const auto& [key,value]:memory.entradas()) {
      const auto* loaded=disk.Buscar(key);
      if(!loaded||*loaded!=value) throw std::runtime_error("Streamed DKSH differs");
    }
    bytes.back()^=1;
    if(memory.Cargar(bytes,reason)) throw std::runtime_error("Corrupted DKSH accepted");
    std::printf("%zu on-demand DKSH reads match; corrupt cache rejected\n",disk.size());
    return 0;
  }
  if(argc<2||argc>3) throw std::runtime_error("Usage: library-check native-library [DKSH-cache]");
  nfsmw::native::BibliotecaShaders library;
  library.Cargar(argv[1]);
  nfsmw::nativo::ShadersNativos renderer;
  if(!renderer.Cargar(argv[1])) throw std::runtime_error("Renderer rejected library");
  size_t vs=0,ps=0,raw=0,bound=0,exports=0;
  for(const auto& shader:library.shaders()) {
    const auto* entry=renderer.IdentificarContenedor(shader.original);
    if(!entry||entry->vertices!=shader.vertices) throw std::runtime_error("Original container identity was lost");
    if(entry->microcodigo.empty()) throw std::runtime_error("Empty decoded microcode");
    const bool without_ps=nfsmw::nativo::ShadersParaDibujo(entry,nullptr,4);
    if(without_ps != bool(shader.vertices && shader.memexport))
      throw std::runtime_error("Memory-export draw lost without a pixel shader");
    if(nfsmw::nativo::ShadersParaDibujo(entry,nullptr,5)!=shader.vertices)
      throw std::runtime_error("Depth-only shader eligibility changed");
    if(shader.vertices) ++vs; else ++ps;
    raw+=bool(shader.raw_fetch[0]||shader.raw_fetch[1]||shader.raw_fetch[2]);
    bound+=bool(shader.bound_fetch);exports+=shader.memexport;
  }
  if(extended ? vs+ps<19340 : vs+ps!=19340) throw std::runtime_error("Incomplete title shader corpus");
  std::printf("Production reader: %zu VS, %zu PS; %zu raw, %zu indexed bound, %zu memory export\n",vs,ps,raw,bound,exports);
  std::printf("Draw eligibility: all %zu memory-export programs accepted without PS; normal color draws require PS\n",exports);
  if(argc==3) {
    std::ifstream f(argv[2],std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
    nfsmw::deko::CacheDksh cache; std::string reason;
    if(!cache.Cargar(bytes,reason)) throw std::runtime_error("DKSH cache rejected: "+reason);
    const auto cache_count=cache.size();
    const uint32_t scratch_limit=nfsmw::deko::ShaderScratchLimit(std::filesystem::path(argv[1]).parent_path(),nfsmw::deko::HuellaCompilador());
    uint32_t max_scratch=0;
    nfsmw::deko::CacheDksh streamed;
    if(!streamed.CargarArchivo(argv[2],reason)||streamed.size()!=cache_count)
      throw std::runtime_error("Streamed native cache rejected: "+reason);
    for(const auto& shader:library.shaders()) {
      nfsmw::deko::CacheDksh::Clave key{nfsmw::deko::HuellaSpirv(shader.spirv),0,shader.vertices?~0u:0u};
      const auto* expected=cache.Buscar(key);
      const auto* actual=streamed.Buscar(key);
      if(!expected||!actual||*expected!=*actual) throw std::runtime_error("Streamed cache differs from native cache");
      max_scratch=std::max(max_scratch,nfsmw::deko::DkshScratch(*actual));
    }
    if(scratch_limit!=std::max(2048u,(max_scratch+65536+511)&~511u)) throw std::runtime_error("Queue scratch metadata mismatch");
    std::printf("Queue scratch: %u bytes per warp covers measured %u\n",scratch_limit,max_scratch);
    bytes.back()^=1;
    if(cache.Cargar(bytes,reason)) throw std::runtime_error("Corrupted DKSH cache was accepted");
    std::printf("Production DKSH cache reader: %zu entries, covers all %zu shaders; tamper rejected\n",cache_count,vs+ps);
  }
  return 0;
} catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
