"""Inventory MKvDCU's own XEX/UE3 shaders for the deko3d feasibility gate.

Inputs are read-only. Outputs contain game data and belong in ignored research/.
Python dependencies: pycryptodome, lzokay, xxhash (install outside the repo).
Formats cross-checked against ReXGlue xex_module.cpp and UEViewer's
UnPackage3.cpp / UnCoreCompression.cpp. No shader from another game is used.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct


MAGIC = b"\x9e\x2a\x83\xc1"
SHADER_MAGIC = re.compile(rb"\x10\x2a\x11[\x00\x01]")


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def xex_image(data):
    if len(data) < 24 or data[:4] != b"XEX2":
        raise ValueError("Expected an XEX2 executable")
    count = u32(data, 20)
    if 24 + count * 8 > len(data):
        raise ValueError("Truncated XEX optional-header table")
    headers = dict(struct.iter_unpack(">II", data[24:24 + count * 8]))
    fmt = headers[0x3FF]
    size, encryption, compression = struct.unpack_from(">IHH", data, fmt)
    if compression != 1:
        raise ValueError(f"This reader supports basic-compressed MKvDCU XEXs; type={compression}")
    if size < 8 or (size - 8) % 8 or fmt + size > len(data):
        raise ValueError("Invalid XEX basic block table")
    blocks = list(struct.iter_unpack(">II", data[fmt + 8:fmt + size]))
    if sum(n + z for n, z in blocks) > 512 * 1024 * 1024:
        raise ValueError("Unreasonable XEX image size")
    cipher = None
    if encryption == 1:
        from Crypto.Cipher import AES
        sec = u32(data, 16)
        encrypted_key = data[sec + 0x150:sec + 0x160]
        if len(encrypted_key) != 16:
            raise ValueError("Truncated XEX security header")
        retail_key = bytes.fromhex("20b185a59d28fdc340583fbb0896bf91")
        key = AES.new(retail_key, AES.MODE_CBC, iv=bytes(16)).decrypt(encrypted_key)
        cipher = AES.new(key, AES.MODE_CBC, iv=bytes(16))
    elif encryption != 0:
        raise ValueError(f"Unsupported XEX encryption {encryption}")
    pos = u32(data, 8)
    image = bytearray()
    for length, zeros in blocks:
        block = data[pos:pos + length]
        if len(block) != length or (cipher and length % 16):
            raise ValueError("Truncated or unaligned XEX block")
        image.extend(cipher.decrypt(block) if cipher else block)
        image.extend(bytes(zeros))
        pos += length
    if image[:2] != b"MZ":
        raise ValueError("Decoded XEX has no PE header (wrong key or corrupt input)")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Decoded XEX has an invalid PE signature")
    libraries = []
    if 0x200FF in headers:
        start = headers[0x200FF]
        end = start + u32(data, start)
        if end > len(data) or (end - start - 4) % 16:
            raise ValueError("Invalid XEX library table")
        for at in range(start + 4, end, 16):
            major, minor, build, flags = struct.unpack_from(">HHHH", data, at + 8)
            libraries.append({"name": data[at:at + 8].rstrip(b"\0").decode("ascii"),
                              "version": [major, minor, build], "flags": flags})
    return bytes(image), headers[0x10201], libraries


def compressed_chunk(data, offset):
    """Return (unpacked LZO chunk, end), or None for an ordinary package header."""
    if offset + 16 > len(data):
        return None
    _, block_size, packed_size, unpacked_size = struct.unpack_from(">IIII", data, offset)
    if block_size != 0x20000 or not 0 < unpacked_size <= 256 * 1024 * 1024:
        return None
    count = (unpacked_size + block_size - 1) // block_size
    at = offset + 16 + count * 8
    if at + packed_size > len(data):
        return None
    sizes = list(struct.iter_unpack(">II", data[offset + 16:at]))
    if sum(c for c, _ in sizes) != packed_size or sum(u for _, u in sizes) != unpacked_size:
        return None
    if any(not 0 < c <= block_size * 2 or not 0 < u <= block_size for c, u in sizes):
        return None
    import lzokay
    decoded = bytearray()
    for c, u in sizes:
        block = data[at:at + c]
        # This title's sampled packages, including Startup/Core/Engine, use LZO.
        # Fail explicitly if another codec occurs; never call it an empty scan.
        result = lzokay.decompress(block, u)
        if len(result) != u:
            raise ValueError(f"LZO length mismatch at {at:#x}")
        decoded.extend(result)
        at += c
    return bytes(decoded), at


def package_payloads(data):
    """Reconstruct the logical package, preserving shaders spanning chunk boundaries."""
    if data[:4] != MAGIC:
        yield "raw", data
        return
    # Startup, Core, Engine and seven other packages have a whole-file wrapper.
    wrapped = compressed_chunk(data, 0)
    if wrapped is not None:
        decoded, _ = wrapped
        yield from package_payloads(decoded)
        return
    version = u32(data, 4)
    if version != 0x001E0192:
        raise ValueError(f"Unsupported MKvDCU UE package version {version:#x}")
    # MK8 / UE 402, licensee 30, Midway 30. The group FString is variable length.
    if u32(data, 12) != 0x20384B4D or u32(data, 16) != 30:
        raise ValueError("Unsupported Midway package summary")
    group_length = struct.unpack_from(">i", data, 20)[0]
    at = 24 + (group_length if group_length >= 0 else -2 * group_length)
    at += 4 + 6 * 4 + 2 * 4 + 16  # flags, tables, Midway fields, GUID
    generations = u32(data, at)
    at += 4 + generations * 12 + 8  # generation records, engine and cooker
    if at + 8 > len(data):
        raise ValueError("Truncated package summary")
    flags, count = struct.unpack_from(">II", data, at)
    if flags == 0 and count == 0:
        yield "package", data
        return
    if flags != 2 or count == 0 or count > 16384 or at + 8 + count * 16 > len(data):
        raise ValueError(f"Unsupported package compression {flags:#x}, chunks={count}")
    records = list(struct.iter_unpack(">4I", data[at + 8:at + 8 + count * 16]))
    first = records[0][0]
    if first > len(data):
        raise ValueError("Package prefix exceeds input")
    logical = bytearray(data[:first])
    for uncompressed_offset, uncompressed_size, compressed_offset, compressed_size in records:
        if uncompressed_offset != len(logical):
            raise ValueError("Non-contiguous logical package chunks")
        if uncompressed_offset + uncompressed_size > 512 * 1024 * 1024:
            raise ValueError("Unreasonable logical package size")
        chunk = compressed_chunk(data, compressed_offset)
        if chunk is None:
            raise ValueError(f"Missing compressed chunk at {compressed_offset:#x}")
        decoded, end = chunk
        if len(decoded) != uncompressed_size or end != compressed_offset + compressed_size:
            raise ValueError("Compressed chunk does not agree with package summary")
        logical.extend(decoded)
    yield "package", bytes(logical)


def containers(data):
    # UE serializes byte arrays without requiring the container's file offset
    # to be divisible by four. Only its internal tables have word alignment.
    for match in SHADER_MAGIC.finditer(data):
        at = match.start()
        if at + 0x24 > len(data):
            continue
        flags, virt, phys, _, ct, dt, shader, reserved1, reserved2 = struct.unpack_from(">9I", data, at)
        if not (0x24 <= virt < 0x10000 and 0 < phys < 0x100000 and at + virt + phys <= len(data)):
            continue
        if reserved1 or reserved2 or ct >= virt or dt >= virt or not 0x24 <= shader <= virt - 24:
            continue
        physical_offset, code_size = struct.unpack_from(">II", data, at + shader)
        if not code_size or physical_offset + code_size != phys:
            continue
        yield at, ("vs" if flags & 1 else "ps"), data[at:at + virt + phys], code_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True, help="Folder containing default.xex and Asset/")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--image-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    game = args.game.resolve()
    if output == game or output.is_relative_to(game):
        parser.error("Output must be outside the read-only game directory")
    output.mkdir(parents=True, exist_ok=True)
    dump = output / "containers"
    dump.mkdir(exist_ok=True)
    if any(dump.iterdir()):
        parser.error("Use a fresh output directory so stale containers cannot enter a shader build")
    import xxhash
    image, image_base, libraries = xex_image((game / "default.xex").read_bytes())
    (output / "default_image.bin").write_bytes(image)
    seen = {}
    instances = []
    sources = []
    failures = []

    def scan(source, payload, data):
        n = 0
        for offset, stage, blob, code_size in containers(data):
            hash64 = xxhash.xxh3_64_hexdigest(blob)
            digest = hashlib.sha256(blob).hexdigest()
            key = stage + "_" + hash64
            if key in seen and seen[key] != digest:
                raise ValueError("Container hash collision: " + key)
            if key not in seen:
                seen[key] = digest
                (dump / (key + ".bin")).write_bytes(blob)
            instances.append({"shader": key, "sha256": digest, "source": source,
                              "payload": payload, "offset": offset, "bytes": len(blob),
                              "microcode_bytes": code_size})
            n += 1
        sources.append({"source": source, "payload": payload, "decoded_bytes": len(data), "containers": n})

    scan("default.xex", "image", image)
    print(f"Executable: {len(seen)} unique containers", flush=True)
    assets = sorted((game / "Asset").glob("*.xxx")) if not args.image_only else []
    for index, path in enumerate(assets):
        try:
            for payload, data in package_payloads(path.read_bytes()):
                scan(path.name, payload, data)
        except Exception as exc:
            failures.append({"source": path.name, "error": str(exc)})
            print(f"FAILED {path.name}: {exc}", flush=True)
        if (index + 1) % 25 == 0:
            print(f"Assets {index + 1}/{len(assets)}: {len(seen)} unique containers", flush=True)
    summary = {"game": str(game), "image_base": hex(image_base), "image_bytes": len(image),
               "libraries": libraries, "unique_containers": len(seen),
               "stage_counts": dict(Counter(k[:2] for k in seen)),
               "asset_files_scanned": len(assets), "failures": failures,
               "coverage": "static scan of reconstructed packages; runtime-generated shader coverage unproven"}
    (output / "manifest.json").write_text(json.dumps(instances, indent=2) + "\n")
    (output / "sources.json").write_text(json.dumps(sources, indent=2) + "\n")
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
