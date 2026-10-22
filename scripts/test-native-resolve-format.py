"""Compare native resolve fallback with the SDK's format table and conversion."""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
sdk = root/'references/rexglue-sdk'
enums = (sdk/'include/rex/graphics/xenos.h').read_text()
formats = (sdk/'src/graphics/pipeline/texture/info_formats.cpp').read_text()
conversion = (sdk/'include/rex/graphics/pipeline/texture/info.h').read_text()
declarations = []
for name in ('TextureFormat', 'ColorRenderTargetFormat'):
    start = enums.index('enum class '+name+' : uint32_t {')
    declarations.append(enums[start:enums.index('\n};', start)+3])
start = conversion.index('inline xenos::TextureFormat ColorRenderTargetToTextureFormat(')
function = conversion[start:conversion.index('\n}', start)+2]
entries = re.findall(r'FORMAT_INFO\(\s*(\w+)\s*,\s*(\w+)', formats)
entries = [entry for entry in entries if entry[1] in ('kResolvable', 'kUncompressed', 'kCompressed')]
if len(entries) != 64:
    raise ValueError('SDK texture format table changed')
source = '#include <cstdint>\n#include <cstdio>\n#include <cstdlib>\n'
source += '#include "mkvsdcu_resolve_format.h"\nnamespace xenos {\n'
source += '\n'.join(declarations)+'\n}\n'+function+'\n'
source += 'constexpr bool valid[64] = {'+','.join('true' if t == 'kResolvable' else 'false' for n, t in entries)+'};\n'
source += r'''
int main() {
  unsigned tested = 0;
  for (uint32_t source = 0; source < 16; ++source)
    for (uint32_t requested = 0; requested < 64; ++requested) {
      const uint32_t expected = valid[requested] ? requested :
        uint32_t(ColorRenderTargetToTextureFormat(xenos::ColorRenderTargetFormat(source)));
      if (nfsmw::nativo::ResolveDestinationFormat(requested, source) != expected) {
        std::fprintf(stderr, "Resolve mismatch: destination %u source %u\n", requested, source);
        return 1;
      }
      ++tested;
    }
  std::printf("PASS: %u resolve source/destination combinations match the SDK\n", tested);
}
'''
env = dict(os.environ, TEMP='E:/tools/tmp', TMP='E:/tools/tmp')
with tempfile.TemporaryDirectory(prefix='mkvsdcu-resolve-', dir='E:/tools/tmp') as folder:
    folder = Path(folder)
    (folder/'check.cpp').write_text(source)
    compiler = 'E:/tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'
    subprocess.run([compiler, '-std=c++20', '-O2', '-static',
        '-I'+str(root/'targets/mkvsdcu-nx/thirdparty/nfsmw-deko3d'),
        str(folder/'check.cpp'), '-o', str(folder/'check.exe')], check=True, env=env)
    subprocess.run([str(folder/'check.exe')], check=True, env=env)
(root/'research/mkvsdcu-deko3d/hardware-rendering-20261022/resolve-format-regression.json').write_text(
    json.dumps(dict(cases=1024, passed=True, reference='PC FSI SDK format table and ColorRenderTargetToTextureFormat'), indent=2)+'\n')
