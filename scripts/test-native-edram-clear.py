"""Check depth-clear alias mapping against Xenos sample/tile conventions."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
reference = (root/'references/rexglue-sdk/include/rex/graphics/xenos.h').read_text()
start=reference.index('constexpr uint32_t GetSurfacePitchTiles(')
end=reference.index('{',start)+1
depth=1
while depth:
    depth+=(reference[end]=='{')-(reference[end]=='}')
    end+=1
pitch=reference[start:end]
source = r'''
#include "mkvsdcu_edram_clear.h"
#include <cassert>
#include <cstdio>
#include <set>
enum class MsaaSamples { k1X, k2X, k4X };
constexpr unsigned kEdramTileWidthSamples=80;
''' + pitch + r'''
using namespace nfsmw::nativo;
std::set<unsigned> samples(EdramSurface surface, EdramClearRect rectangle) {
  std::set<unsigned> result;
  const unsigned sx=surface.samples==2?2:1, sy=surface.samples?2:1;
  const unsigned pitch=GetSurfacePitchTiles(surface.pitch,MsaaSamples(surface.samples),false);
  for(unsigned y=rectangle.y0*sy;y<rectangle.y1*sy;++y)
    for(unsigned x=rectangle.x0*sx;x<rectangle.x1*sx;++x)
      result.insert(((y/16)*pitch+x/80)*1280+(y%16)*80+x%80);
  return result;
}
int main() {
  unsigned tested=0;
  for(unsigned p=1;p<16384;++p) for(unsigned s=0;s<3;++s) {
    assert(EdramPitchTiles({0,p,s})==GetSurfacePitchTiles(p,MsaaSamples(s),false)); ++tested;
  }
  EdramClearRect mapped{};
  assert(MapEdramClear({0,520,2},{0,1040,1},{0,0,520,624},mapped));
  assert(mapped.x0==0 && mapped.y0==0 && mapped.x1==1040 && mapped.y1==624);
  assert(!MapEdramClear({0,520,2},{1,1040,1},{0,0,520,624},mapped));
  assert(!MapEdramClear({0,520,2},{0,1280,1},{0,0,520,624},mapped));
  assert(!MapEdramClear({0,80,0},{0,40,2},{1,1,7,7},mapped));
  for(unsigned from=0;from<3;++from) for(unsigned to=0;to<3;++to) {
    const EdramSurface a{0,from==2?80u:160u,from}, b{0,to==2?80u:160u,to};
    for(unsigned x=0;x<12;x+=2) for(unsigned y=0;y<12;y+=2) {
      const EdramClearRect area{x,y,x+4,y+4};
      assert(MapEdramClear(a,b,area,mapped));
      assert(samples(a,area)==samples(b,mapped)); ++tested;
    }
  }
  std::printf("PASS: %u pitch/physical-sample cases; captured 520px 4x -> 1040px 2x clear\n",tested);
}
'''
env = dict(os.environ, TEMP='E:/tools/tmp', TMP='E:/tools/tmp')
with tempfile.TemporaryDirectory(prefix='mkvsdcu-edram-', dir='E:/tools/tmp') as directory:
    directory=Path(directory)
    (directory/'test.cpp').write_text(source)
    subprocess.run(['E:/tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe',
        '-std=c++20','-O2','-static','-I'+str(root/'targets/mkvsdcu-nx/thirdparty/nfsmw-deko3d'),
        str(directory/'test.cpp'),'-o',str(directory/'test.exe')],check=True,env=env)
    subprocess.run([str(directory/'test.exe')],check=True,env=env)
(root/'research/mkvsdcu-deko3d/hardware-rendering-retry-20261022/edram-clear-regression.json').write_text(
    json.dumps(dict(cases=49473,passed=True,reference='Xenos GetSurfacePitchTiles and 80x16 sample coverage'),indent=2)+'\n')
