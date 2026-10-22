#include "nfsmw_nativo_shaders.h"
#include "nfsmw_shader_library.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

int main(int argc, char** argv) try {
  if (argc < 4) throw std::runtime_error("Usage: live-check library capture-folder program-stems...");
  nfsmw::nativo::ShadersNativos shaders;
  if (!shaders.Cargar(argv[1])) throw std::runtime_error("Library rejected");
  unsigned exports = 0;
  for (int i = 3; i < argc; ++i) {
    const auto path = std::filesystem::path(argv[2]) / (std::string(argv[i])+".ucode");
    std::ifstream file(path, std::ios::binary);
    std::vector<char> data{std::istreambuf_iterator<char>(file), {}};
    if (data.size() < 16 || std::memcmp(data.data(), "MKVCODE1", 8))
      throw std::runtime_error("Invalid capture header");
    uint32_t stage, count;
    std::memcpy(&stage, data.data()+8, 4);
    std::memcpy(&count, data.data()+12, 4);
    if (stage > 1 || data.size() != 16+uint64_t(count)*4)
      throw std::runtime_error("Invalid capture length");
    std::vector<uint32_t> code(count);
    std::memcpy(code.data(), data.data()+16, count*4);
    const auto* entry = shaders.Identificar(stage == 0, code);
    if (!entry || entry->microcodigo != code)
      throw std::runtime_error("Live program still has no exact runtime identity: "+std::string(argv[i]));
    if (entry->shader->memexport) {
      ++exports;
      if (!nfsmw::nativo::ShadersParaDibujo(entry, nullptr, 4))
        throw std::runtime_error("Live memory export still filtered out");
    }
    std::printf("%s -> shader %u; memory export %s; raw fetch %08x/%08x/%08x\n",
                argv[i], entry->numero, entry->shader->memexport ? "yes" : "no",
                entry->shader->raw_fetch[0], entry->shader->raw_fetch[1], entry->shader->raw_fetch[2]);
  }
  std::printf("All %d captured utility identities resolve exactly; %u memory-export programs\n", argc-3, exports);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
