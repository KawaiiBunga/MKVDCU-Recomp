"""Validate Switch entry point, native hooks, telemetry wrapper and NRO package."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--nm", type=Path, required=True)
parser.add_argument("--deko3d", action="store_true")
args = parser.parse_args()
elf = args.build / "mkvsdcu"
nro = args.build / "mkvsdcu.nro"
result = subprocess.run([str(args.nm), "--defined-only", str(elf)], capture_output=True, text=True, check=True)
symbols = {}
for line in result.stdout.splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = (fields[0], fields[1])
if symbols.get("_start", (None,))[0] != "0000000000000000":
    raise SystemExit("Invalid Switch entry point: _start must be at address zero")
for symbol in ("sub_827E1410", "__wrap___imp__VdSwap"):
    if symbol not in symbols or symbols[symbol][1].upper() in ("W", "V"):
        raise SystemExit("Missing strong hook/wrapper: " + symbol)
data = nro.read_bytes()
if data[0x10:0x14] != b"NRO0":
    raise SystemExit("Invalid NRO header")
image_size = struct.unpack_from("<I", data, 0x18)[0]
if data[image_size:image_size + 4] != b"ASET":
    raise SystemExit("NRO lacks its packaged homebrew assets")
report = {"nro_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
          "entry_point": symbols["_start"][0], "validated_hooks": ["sub_827E1410", "__wrap___imp__VdSwap"],
          "nro_header": "NRO0", "assets_header": "ASET"}
if args.deko3d:
    for symbol in ('dkQueueSubmitCommands', 'dkDeviceCreate', 'dkShaderInitialize'):
        if symbol not in symbols:
            raise SystemExit('Missing native deko3d symbol: '+symbol)
    forbidden = [s for s in symbols if re.match(r'^(vkCreateInstance|vkCreateDevice|nvk_|nvkmd_)', s)]
    if forbidden:
        raise SystemExit('Vulkan driver linked into native build: '+str(forbidden[:10]))
    native_hooks = ['827EB7F8','827EB600','827EB8F0','827EB638','827F23F0',
                    '827F27E0','827F30B0','827F1DE8','827F2350','827F1940','827F1E30']
    for address in native_hooks:
        symbol='sub_'+address
        if symbol not in symbols or symbols[symbol][1].upper() in ('W','V'):
            raise SystemExit('Missing strong native hook: '+symbol)
    direct = json.loads((args.build/'direct-calls/direct-calls-report.json').read_text())
    if not set(native_hooks).issubset(direct['protected_addresses']):
        raise SystemExit('Native hooks were not protected by the direct-call pass')
    bypasses=[]
    for source in (args.build/'direct-calls').glob('mkvsdcu_recomp.*.cpp'):
        for address in re.findall(r'__imp__sub_([0-9A-Fa-f]{8})\(ctx, base\);',source.read_text()):
            if address.upper() in native_hooks:
                bypasses.append((source.name,address))
    if bypasses:
        raise SystemExit('Direct guest calls bypass native hooks: '+str(bypasses[:10]))
    report.update(graphics_api='deko3d', native_shader_hooks=native_hooks,
        direct_call_hook_bypasses=0, hardware_tested=False, native_windows_build=True)
(args.build / "switch-build-verification.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
