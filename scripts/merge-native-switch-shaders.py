"""Merge verified live shaders while preserving every shader in the static corpus."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import xxhash


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verified(folder):
    report = json.loads((folder/'shader-verification.json').read_text())
    require(report['abi'] == 'mkvsdcu-fixed-v4', 'Shader ABI differs')
    for name, record in report['files'].items():
        require(Path(name).name == name, 'Invalid package filename')
        data = (folder/name).read_bytes()
        require(len(data) == record['bytes'] and digest(data) == record['sha256'],
                'Package file changed: '+name)
    return report


def library(path):
    data = path.read_bytes()
    magic, version, count, fingerprint = struct.unpack_from('<8sIIQ', data)
    require(magic == b'MKVSPV\0\0' and version == 4 and 0 < count <= 32768,
            'Invalid library header')
    require(xxhash.xxh3_64_intdigest(data[24:]) == fingerprint, 'Library checksum differs')
    records, position = {}, 24
    for _ in range(count):
        require(position+196 <= len(data), 'Truncated library metadata')
        size, words, key = struct.unpack_from('<IIQ', data, position)
        end = position+196+size+words*4
        require(36 <= size <= 1024*1024 and 5 <= words <= 1024*1024 and end <= len(data),
                'Invalid library entry range')
        original = data[position+196:position+196+size]
        require(xxhash.xxh3_64_intdigest(original) == key, 'Original shader checksum differs')
        identity = key, original
        require(identity not in records, 'Duplicate original shader')
        records[identity] = data[position:end]
        position = end
    require(position == len(data), 'Trailing library data')
    return records


def cache(path):
    data = path.read_bytes()
    magic, version, compiler, count, reserved, checksum = struct.unpack_from('<IIQIIQ', data)
    require(magic == 0x4b44464e and version == 2 and reserved == 0, 'Invalid cache header')
    require(xxhash.xxh3_64_intdigest(data[32:]) == checksum, 'Cache checksum differs')
    require(32+count*24 <= len(data), 'Truncated cache index')
    position, records, scratch = 32+count*24, {}, 0
    for i in range(count):
        fingerprint, spec, variant, size, reserved = struct.unpack_from('<QIIII', data, 32+i*24)
        require(reserved == 0 and 32 <= size <= 4*1024*1024 and position+size <= len(data),
                'Invalid cache entry range')
        blob = data[position:position+size]
        require(blob[:4] == b'DKSH', 'Invalid native shader')
        program = struct.unpack_from('<I', blob, 16)[0]
        require(program+24 <= size, 'Truncated native program header')
        scratch = max(scratch, struct.unpack_from('<I', blob, program+20)[0])
        key = fingerprint, spec, variant
        require(key not in records, 'Duplicate native cache key')
        records[key] = blob
        position += (size+3) & ~3
    require(position == len(data), 'Trailing native cache data')
    return compiler, records, scratch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--extra', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base, extra = verified(args.base), verified(args.extra)
    static = base.get('base_static_shaders', base['shaders'])
    require(static == 19340 and base['shaders'] >= static, 'Static title corpus is incomplete')
    require(base['compiler_id'] == extra['compiler_id'], 'Native compiler identities differ')
    records = library(args.base/'mkvsdcu_shaders_dk.nfsp')
    require(len(records) == base['shaders'], 'Base library count differs')
    additions = library(args.extra/'mkvsdcu_shaders_dk.nfsp')
    require(len(additions) == extra['shaders'], 'Supplemental library count differs')
    original_count = len(records)
    for key, value in additions.items():
        require(key not in records or records[key] == value, 'Original shader translation changed')
        records[key] = value
    require(len(records) <= 32768, 'Merged library exceeds entry limit')
    body = b''.join(value for key, value in sorted(records.items()))
    require(len(body)+24 <= 768*1024*1024, 'Merged library exceeds runtime size limit')
    fingerprint = xxhash.xxh3_64_intdigest(body)
    lib = struct.pack('<8sIIQ', b'MKVSPV\0\0', 4, len(records), fingerprint)+body
    compiler, blobs, scratch = cache(args.base/'mkvsdcu_shadercache_dksh.bin')
    extra_compiler, extra_blobs, extra_scratch = cache(args.extra/'mkvsdcu_shadercache_dksh.bin')
    require(compiler == extra_compiler == xxhash.xxh3_64_intdigest(base['compiler_id'].encode()),
            'Cache compiler fingerprint differs')
    for key, value in extra_blobs.items():
        require(key not in blobs or blobs[key] == value, 'Native cache key collision')
        blobs[key] = value
    index, payload = bytearray(), bytearray()
    for (key, spec, variant), blob in sorted(blobs.items()):
        index += struct.pack('<QIIII', key, spec, variant, len(blob), 0)
        payload += blob+bytes((-len(blob)) % 4)
    cache_body = index+payload
    dksh = struct.pack('<IIQIIQ', 0x4b44464e, 2, compiler, len(blobs), 0,
                       xxhash.xxh3_64_intdigest(cache_body))+cache_body
    maximum = max(scratch, extra_scratch)
    queue = max(2048, (maximum+65536+511) & ~511)
    require(queue <= 4*1024*1024, 'Merged scratch requirement exceeds runtime limit')
    limits = struct.pack('<8sIIQQ', b'MKVLIMIT', 1, queue, compiler, fingerprint)
    vertex = sum(bool(struct.unpack_from('>I', original)[0] & 1) for _, original in records)
    report = dict(abi='mkvsdcu-fixed-v4', shaders=len(records), vertex=vertex,
        pixel=len(records)-vertex, base_static_shaders=static,
        supplemental_shaders=len(records)-static, newly_added_shaders=len(records)-original_count,
        compiler_id=base['compiler_id'], cache_entries=len(blobs), max_per_warp_scratch=maximum,
        queue_per_warp_scratch=queue, hardware_tested=False, files={})
    args.output.mkdir(parents=True, exist_ok=True)
    require(not any(args.output.iterdir()), 'Use a fresh merge output directory')
    for name, data in [('mkvsdcu_shaders_dk.nfsp', lib), ('mkvsdcu_shadercache_dksh.bin', dksh),
                       ('mkvsdcu_shader_limits.bin', limits)]:
        (args.output/name).write_bytes(data)
        report['files'][name] = dict(bytes=len(data), sha256=digest(data))
    (args.output/'shader-verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
