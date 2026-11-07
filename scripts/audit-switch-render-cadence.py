"""Check render30 hook boundaries against this game's mapped PPC executable."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser()
p.add_argument('--image', type=Path, default=root/'research/mkvsdcu-deko3d/default_image.bin')
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
image = args.image.read_bytes()
base = 0x82000000

def word(address):
    return struct.unpack_from('>I', image, address-base)[0]

checks = []
def check(address, expected, description):
    actual = word(address)
    if actual != expected:
        raise SystemExit(f'{description}: {address:08X} expected {expected:08X}, found {actual:08X}')
    checks.append(dict(address=f'{address:08X}', word=f'{actual:08X}', purpose=description))

def branch(pc, target, description):
    check(pc, 0x48000001 | ((target-pc) & 0x03FFFFFC), description)

check(0x820D4158+180, 0x8262F430, 'engine tick vtable slot')
check(0x820D4158+208, 0x824A8A18, 'engine redraw vtable slot')
check(0x8204A090, struct.unpack('>I', struct.pack('>f', 1/60))[0], 'original fixed timestep')
branch(0x82799290, 0x826344D0, 'outer frame hook')
check(0x82799398, 0x4E800421, 'outer engine virtual dispatch')
branch(0x8262F4C0, 0x824ADA70, 'MK engine calls base engine tick')
branch(0x824ADC38, 0x824D0408, 'base engine calls world update')
branch(0x824D0788, 0x826318F0, 'world update calls MK update before redraw')
check(0x824ADD7C, 0x4E800421, 'engine virtual redraw after world update')
branch(0x824A8A54, 0x824A8748, 'preserved viewport-client callback')
branch(0x824A8A68, 0x823D4E70, 'only viewport-draw call eligible for decimation')
branch(0x82799430, 0x826037D8, 'only engine-limiter sleep eligible for bypass')
report = dict(image_sha256=hashlib.sha256(image).hexdigest(),
              mapping='mapped image; file offset = guest VA - 0x82000000',
              checks=checks, hardware_full_speed_validated=False)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2)+'\n')
print(f'PASS: {len(checks)} executable checks; world/MK update before separate viewport draw; {args.output}')
