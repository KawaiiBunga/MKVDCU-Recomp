"""Find likely function entries the recompiler has not seen yet.

The recompiler discovers functions by following direct calls, so functions
only reached through vtables, UE3 native-function tables or callback pointers
are missing until the game calls one and aborts with "Call to invalid or
unregistered function". This scans the loaded image for such pointers and
prints the entries the generated code does not register.

Usage:
  1. set MKVDCU_DUMP_IMAGE=<file> and start the game once (it writes the
     loaded image and can be closed right away)
  2. python scripts/find-function-seeds.py <image.bin> [--append]
     --append adds the new addresses to config/mkvsdcu_functions.toml
  3. rerun codegen (scripts/build-pc.ps1)
"""

import argparse
import bisect
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "targets" / "mkvsdcu" / "private" / "rexglue-host"
REGISTER = HOST / "generated" / "default" / "mkvsdcu_register.cpp"
CONFIG = HOST / "config" / "mkvsdcu_functions.toml"

IMAGE_BASE = 0x82000000
CODE_START = 0x82250000
CODE_END = 0x82F3AEC4

BLR = 0x4E800020
BCTR = 0x4E800420


def word(image, address):
    return struct.unpack_from(">I", image, address - IMAGE_BASE)[0]


def previous_ends_flow(image, address):
    """The instruction before an entry ends straight-line code."""
    previous = word(image, address - 4)
    return previous in (BLR, BCTR, 0) or (previous >> 26) == 18  # blr, bctr, padding, b


def is_instruction_start(image, address):
    """Inline switch tables also follow a bctr; their words are code
    addresses or small offsets, not instructions."""
    for value in (word(image, address), word(image, address + 4)):
        if value >> 16 in (0, 0xFFFF) or CODE_START <= value < CODE_END:
            return False
    return True


def table_runs(image):
    """Runs of consecutive words outside the code section that all point into
    code."""
    code_lo, code_hi = CODE_START - IMAGE_BASE, CODE_END - IMAGE_BASE
    run = []
    for offset in range(0, len(image) - 3, 4):
        value = struct.unpack_from(">I", image, offset)[0]
        if not (code_lo <= offset < code_hi) and CODE_START <= value < CODE_END and value % 4 == 0:
            run.append((offset + IMAGE_BASE, value))
            continue
        if run:
            yield run
            run = []
    if run:
        yield run


def code_built_pointers(image):
    """Code addresses built with lis rD,hi / addi rX,rD,lo: callbacks and
    thread entry points handed to other code."""
    for address in range(CODE_START, CODE_END, 4):
        first = word(image, address)
        if first >> 26 != 15 or (first >> 16) & 31 != 0:
            continue  # not lis
        register = (first >> 21) & 31
        high = (first & 0xFFFF) << 16
        for follow in range(address + 4, min(address + 40, CODE_END), 4):
            second = word(image, follow)
            if second >> 26 == 14 and (second >> 16) & 31 == register:  # addi
                low = second & 0xFFFF
                value = (high + (low - 0x10000 if low & 0x8000 else low)) & 0xFFFFFFFF
                if CODE_START <= value < CODE_END and value % 4 == 0:
                    yield address, value
                break
            if (second >> 21) & 31 == register and second >> 26 not in (36, 32):
                break  # register overwritten (stores and loads excepted)


def find_candidates(image, known):
    """Returns {entry: where it is referenced} for unregistered entries."""
    def plausible(target):
        return (target not in known and previous_ends_flow(image, target)
                and is_instruction_start(image, target))

    candidates = {}
    # A vtable or native-function table is mostly entries the recompiler
    # already knows as functions. A switch table points into the middle of one
    # function instead, so its targets are almost never known starts.
    for run in table_runs(image):
        if len(run) >= 2 and sum(1 for _, t in run if t in known) * 2 >= len(run):
            for where, target in run:
                if plausible(target):
                    candidates.setdefault(target, where)
    # A pointer built inside the same function it points into is a local
    # label (exception landing pad, computed jump), not a callback.
    starts = sorted(known)

    def owner(address):
        index = bisect.bisect_right(starts, address) - 1
        return starts[index] if index >= 0 else None

    for where, target in code_built_pointers(image):
        if plausible(target) and owner(where) != owner(target):
            candidates.setdefault(target, where)
    return candidates


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("--append", action="store_true")
    args = parser.parse_args()

    image = Path(args.image).read_bytes()
    known = {int(m, 16) for m in re.findall(r"SetFunction\(0x([0-9A-Fa-f]+)", REGISTER.read_text())}
    known |= {int(m, 16) for m in re.findall(r'"0x([0-9A-Fa-f]{8})"', CONFIG.read_text())}
    candidates = find_candidates(image, known)
    print(f"{len(known)} known functions, {len(candidates)} unregistered entries", file=sys.stderr)
    for address in sorted(candidates):
        print(f"0x{address:08X}  referenced from 0x{candidates[address]:08X}")

    if args.append and candidates:
        with CONFIG.open("a", newline="\r\n") as config:
            config.write("\n# Found by scripts/find-function-seeds.py (tables and callback pointers).\n")
            for address in sorted(candidates):
                config.write(f'"0x{address:08X}" = {{}}\n')


if __name__ == "__main__":
    main()
