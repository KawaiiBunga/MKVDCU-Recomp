#include "mkvsdcu_dk_rectangles.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <array>
int main(int argc,char** argv) {
  if(argc!=2) return 2;
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  std::ofstream manifest(root/"geometry.tsv",std::ios::binary);
  for(uint32_t mask:std::array<uint32_t,15>{0,1,2,4,8,16,32,64,128,255,65536,131072,262144,458752,459007}) {
    const auto name="rectangle_"+std::to_string(mask);
    const auto source=std::filesystem::absolute(root/(name+".glsl"));
    std::ofstream(source,std::ios::binary)<<nfsmw::deko::RectangleGeometryGlsl(mask);
    manifest<<name<<"\tgeom\t"<<source.string()<<'\t'<<std::filesystem::absolute(root/(name+".dksh")).string()<<'\n';
  }
  return manifest ? 0 : 1;
}
