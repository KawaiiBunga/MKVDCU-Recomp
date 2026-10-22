// Uses precisely the renderer's SPIR-V conversion code for the offline cache.
#include "nfsmw_dk_glsl.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main(int argc,char** argv) try {
  if(argc!=3) throw std::runtime_error("Usage: mkvsdcu-native-glsl spirv-root fresh-glsl-root");
  const std::filesystem::path output(argv[2]);
  if(std::filesystem::exists(output)&&!std::filesystem::is_empty(output)) throw std::runtime_error("Output must be empty");
  std::filesystem::create_directories(output);
  std::ofstream report(output.parent_path()/"glsl-report.tsv",std::ios::binary);
  size_t total=0,failed=0;
  for(const auto& file:std::filesystem::directory_iterator(argv[1])) {
    if(!file.is_regular_file()||file.path().extension()!=".spv") continue;
    ++total;
    try {
      std::ifstream input(file.path(),std::ios::binary|std::ios::ate);
      const auto size=input.tellg();
      if(size<20||size%4) throw std::runtime_error("Invalid SPIR-V size");
      std::vector<uint32_t> words(size_t(size)/4);
      input.seekg(0);
      if(!input.read(reinterpret_cast<char*>(words.data()),size)) throw std::runtime_error("SPIR-V read failed");
      const auto result=nfsmw::deko::SpirvAGlsl(words,0);
      if(result.glsl.empty()) throw std::runtime_error(result.error);
      if(!result.avisos.empty()) throw std::runtime_error(result.avisos.front());
      std::ofstream glsl(output/(file.path().stem().string()+".glsl"),std::ios::binary);
      if(!glsl.write(result.glsl.data(),result.glsl.size())) throw std::runtime_error("GLSL write failed");
      report<<file.path().stem().string()<<'\t'<<(result.vertices?"vert":"frag")<<'\t'
            <<result.ubos<<'\t'<<result.texturas<<'\t'<<result.interpoladores<<'\n';
    } catch(const std::exception& error) {
      ++failed; std::fprintf(stderr,"%s: %s\n",file.path().filename().string().c_str(),error.what());
    }
    if(total%256==0) {std::printf("GLSL %zu (%zu failed)\n",total,failed);std::fflush(stdout);}
  }
  if(!report) throw std::runtime_error("Report write failed");
  std::printf("GLSL %zu, %zu failed\n",total,failed);
  return total&&!failed?0:1;
} catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
