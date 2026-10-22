"""Catalog MKvDCU's D3D code and verify recorded static identifications.

Consumes the locally decoded image from audit-switch-shaders.py and the game's
generated C++. It never patches game code or enables hooks. Requires capstone.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--generated", type=Path, required=True)
    p.add_argument("--map", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--base", type=lambda s: int(s, 0), default=0x82000000)
    p.add_argument("--start", type=lambda s: int(s, 0), default=0x827D0000)
    p.add_argument("--end", type=lambda s: int(s, 0), default=0x82810000)
    args = p.parse_args()
    image = args.image.read_bytes()
    mapping = json.loads(args.map.read_text())
    known = {int(row["address"], 16): row for row in mapping["functions"]}
    starts = set()
    imports = {}
    for path in sorted(args.generated.glob("*_recomp.*.cpp")):
        source = path.read_text(encoding="utf-8")
        starts.update(int(x, 16) for x in re.findall(r"DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)", source))
        for address, name in re.findall(r"ctx\.lr = (0x[0-9A-Fa-f]+);\s*__imp__(?!sub_)(\w+)\(ctx, base\);", source):
            imports[int(address, 16) - 4] = name
    starts = sorted(starts)
    if not starts:
        p.error("No generated function starts")
    for address, row in known.items():
        offset = address - args.base
        body = image[offset:offset + row["bytes"]] if offset >= 0 else b""
        if hashlib.sha256(body).hexdigest() != row["body_sha256"]:
            p.error(f"Static function map does not match the image: {row['name']} at {address:#x}")
    import capstone
    disassembler = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    # Capstone does not decode every Xbox VMX128 instruction. Preserve those
    # words as data and continue, rather than silently dropping the function tail.
    disassembler.skipdata = True
    functions = []
    lines = []
    for n, address in enumerate(starts):
        if not args.start <= address < args.end:
            continue
        end = starts[n + 1] if n + 1 < len(starts) else address + 4
        offset = address - args.base
        if offset < 0 or end - args.base > len(image):
            p.error("Function range exceeds decoded image")
        code = image[offset:end - args.base]
        named = known.get(address, {}).get("name", f"sub_{address:08X}")
        lines.append(f"\n{named} (sub_{address:08X}):")
        calls = []
        undecoded = 0
        for instruction in disassembler.disasm(code, address):
            if instruction.mnemonic == ".byte":
                undecoded += instruction.size // 4
            annotation = " ; " + imports[instruction.address] if instruction.address in imports else ""
            lines.append(f"{instruction.address:08X} {instruction.mnemonic:12} {instruction.op_str}{annotation}")
            if instruction.mnemonic == "bl":
                calls.append({"site": hex(instruction.address), "target": instruction.op_str,
                              "import": imports.get(instruction.address)})
        functions.append({"address": hex(address), "name": named, "bytes": len(code), "calls": calls,
                          "undecoded_instruction_words": undecoded,
                          "body_sha256": hashlib.sha256(code).hexdigest()})
    kernel_calls = []
    for site, name in sorted(imports.items()):
        if not name.startswith("Vd"):
            continue
        parent = bisect.bisect_right(starts, site) - 1
        kernel_calls.append({"site": hex(site), "function": hex(starts[parent]) if parent >= 0 else None,
                             "import": name})
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "d3d-disassembly.txt").write_text("\n".join(lines) + "\n")
    report = {"image_sha256": hashlib.sha256(image).hexdigest(), "functions": functions,
              "verified_static_map_entries": len(known), "runtime_verified": False,
              "hooks_enabled": False, "video_kernel_calls": kernel_calls}
    (args.output / "d3d-audit.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(functions)} functions catalogued, {len(known)} static body hashes verified; no hooks enabled")


if __name__ == "__main__":
    main()
