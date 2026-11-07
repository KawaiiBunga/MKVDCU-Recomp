"""Package the verified render30 candidate for an existing complete installation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tomllib
import zipfile

root = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--release', type=Path, required=True)
p.add_argument('--config', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

research = a.release.parent
verification = json.loads((a.release/'switch-build-verification.json').read_text())
loops = json.loads((research/'self-loop-comparison.json').read_text())
audit = json.loads((research/'executable-audit.json').read_text())
expected_hooks = {'8262F430', '823D4E70', '826318F0', '826037D8'}
if (verification.get('graphics_api') != 'deko3d' or
    verification.get('direct_call_hook_bypasses') != 0 or
    set(verification.get('native_render_cadence_hooks', [])) != expected_hooks or
    not verification.get('cpu_passes', {}).get('compiler_semantics_checked') or
    not loops['passed'] or len(audit['checks']) != 12 or
    sha(a.release/'mkvsdcu.nro') != verification['sha256']):
    raise SystemExit('Candidate has not passed required executable checks')

text = a.config.read_text()
before = tomllib.loads(text)
changes = {'mkvsdcu_30fps_render': True, 'mkvsdcu_30fps_timing': False,
           'mkvsdcu_fps_limit': 60, 'mkvsdcu_show_fps': True}
for key, value in changes.items():
    literal = str(value).lower() if isinstance(value, bool) else str(value)
    line = f'{key} = {literal}'
    if key in before:
        text, count = re.subn(r'(?m)^'+re.escape(key)+r'\s*=.*$', line, text)
        if count != 1:
            raise SystemExit('Ambiguous configuration key: '+key)
    else:
        text = line+'\n'+text  # Root scope even if the original ends in a table.
after = tomllib.loads(text)
assert after == before | changes, 'Unrelated configuration changed'
(a.release/'mkvsdcu.toml').write_text(text)

readme = '''MKvDCU v18: experimental full-speed 30 FPS rendering candidate

INSTALL INTO YOUR EXISTING WORKING GAME FOLDER
Copy mkvsdcu/mkvsdcu.nro and mkvsdcu/mkvsdcu.toml from this ZIP to:
    sdmc:/switch/mkvsdcu/
Replace the existing files. Do not create a second nested mkvsdcu folder.
Keep the existing shader files, cache, game_root and saves. This is an update;
the NRO alone cannot replace the full native shader installation.
Keep a backup of your previous NRO and TOML on your PC.

The supplied TOML is based on the config retrieved from your Switch before
this build: 75% scene scale, FPS counter, existing controls and system clocks.
If you changed personal settings after that capture, keep your own TOML and
apply these four root-level values instead:
    mkvsdcu_30fps_render = true
    mkvsdcu_30fps_timing = false
    mkvsdcu_fps_limit = 60
    mkvsdcu_show_fps = true

The 60 FPS cap is intentional: the new mode owns pacing and draws at 30 FPS
while retaining the original 60 simulation updates/s. That cap is only used
when the new path is inactive or disabled. The old half-speed delta experiment
is off. New mode is also selectable in the + and - port menu; save and restart.

This candidate has passed host and executable checks, but has not been tested
in gameplay on the Switch. It needs enough CPU time for both simulation ticks.
Try a complete fight: check that ten round-clock seconds take about ten real
seconds, movement/attacks/audio feel normal, and all graphics are visible.
Then reopen FTP or provide the latest logs/mkvsdcu_XXX.log. The new [render30]
lines report actual simulation rate separately from rendered FPS.

ROLLBACK: set mkvsdcu_30fps_render = false, save, and fully restart. Leave
mkvsdcu_30fps_timing = false. Or restore your previous NRO and TOML together.
No PGO training instrumentation or new shader package is required.
'''
(a.release/'README.txt').write_text(readme)
shutil.copyfile(root/'docs/SWITCH_RENDER30_20261029.md', a.release/'RENDER30.md')
source_map = {
    'mkvsdcu/mkvsdcu.nro': a.release/'mkvsdcu.nro',
    'mkvsdcu/mkvsdcu.toml': a.release/'mkvsdcu.toml',
    'README.txt': a.release/'README.txt',
    'RENDER30.md': a.release/'RENDER30.md',
    'verification/build.json': a.release/'switch-build-verification.json',
    'verification/self-loops.json': research/'self-loop-comparison.json',
    'verification/executable-audit.json': research/'executable-audit.json',
    'LICENSE-GPL-3.0.txt': root/'targets/mkvsdcu-nx/thirdparty/nfsmw-deko3d/LICENSE',
}
manifest = dict(build='v18-render30-candidate', hardware_tested=False, deployed=False,
                config_source_sha256=sha(a.config), files={})
a.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(a.output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=1) as z:
    for name, path in source_map.items():
        z.write(path, name)
        manifest['files'][name] = dict(bytes=path.stat().st_size, sha256=sha(path))
    z.writestr('manifest.json', json.dumps(manifest, indent=2)+'\n')
with zipfile.ZipFile(a.output) as z:
    if z.testzip():
        raise SystemExit('ZIP CRC failure')
    for name, record in manifest['files'].items():
        data = z.read(name)
        if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise SystemExit('ZIP content differs: '+name)
result = dict(path=str(a.output.resolve()), bytes=a.output.stat().st_size, sha256=sha(a.output),
              nro_sha256=verification['sha256'], hardware_tested=False, deployed=False)
(research/'update-package.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
