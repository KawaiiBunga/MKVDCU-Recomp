"""Package the complete, verified MKvDCU native shader library and DKSH cache.

These files contain shaders from the user's game and must not be redistributed.
Requires xxhash (install into E:/tools/python-packages for this workspace).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import xxhash

COMPILER_ID = ('MKvDCU fixed ABI 4; uam SA2 '
    'adf3a4eee524dcca5cd7c1b42454ddb8a09124a1 (patches 1-8); '
    'SPIRV-Cross vulkan-sdk-1.4.363.0; GLSL revision 3')

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(condition, message):
    if not condition:
        raise ValueError(message)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--containers', type=Path, required=True)
    p.add_argument('--metadata', type=Path, required=True)
    p.add_argument('--spirv', type=Path, required=True)
    p.add_argument('--native', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--expected-count', type=int, default=19340,
                   help='Explicit count for a separately captured supplemental corpus')
    args = p.parse_args()
    manifest = {r['shader']: r for r in json.loads(args.manifest.read_text())}
    require(1 <= args.expected_count <= 32768 and len(manifest) == args.expected_count,
            'Shader count differs from the explicitly expected corpus')
    metadata = [json.loads(line) for line in args.metadata.read_text().splitlines()]
    require(len(metadata) == len(manifest), 'Incomplete or duplicate metadata')
    metadata = {r['shader']: r for r in metadata}
    require(set(metadata) == set(manifest), 'Metadata differs from title manifest')
    spv_report = json.loads((args.spirv.parent/'report.json').read_text())
    dk_report = json.loads((args.native/'report.json').read_text())
    require(spv_report['abi'] == dk_report['abi'] == 'mkvsdcu-fixed-v4', 'Wrong shader ABI')
    require(not spv_report['missing'] and not spv_report['unexpected'] and
        spv_report['produced'] == len(manifest) and
        all(r['exit_code'] == 0 for r in spv_report['results']), 'Incomplete SPIR-V pass')
    require(not dk_report['failures'] and dk_report['compiled'] == len(manifest), 'Incomplete native pass')
    entries = {r['shader']: r for r in dk_report['entries']}
    require(set(entries) == set(manifest), 'Native report differs from title manifest')
    originals = {}
    for batch in sorted(args.containers.rglob('*.bin')):
        data = batch.read_bytes()
        pos = 0
        while pos < len(data):
            require(len(data)-pos >= 36, 'Truncated container batch')
            flags, virtual, physical = struct.unpack_from('>III', data, pos)
            size = virtual + physical
            require(flags & ~1 == 0x102a1100 and virtual >= 36 and physical > 0 and
                size % 4 == 0 and size <= len(data)-pos, 'Invalid container batch')
            original = data[pos:pos+size]
            name = ('vs_' if flags & 1 else 'ps_') + f'{xxhash.xxh3_64_intdigest(original):016x}'
            require(name in manifest and name not in originals, 'Unexpected or duplicate original '+name)
            require(sha(original) == manifest[name]['sha256'], 'Original identity mismatch '+name)
            originals[name] = original
            pos += size
    require(set(originals) == set(manifest), 'Original corpus is incomplete')
    library_body = bytearray()
    cache_entries = []
    count_vs = 0
    max_scratch = 0
    for name in sorted(originals, key=lambda n: (int(n[3:], 16), originals[n])):
        original = originals[name]
        spv = (args.spirv/(name+'.spv')).read_bytes()
        glsl = (args.native/'glsl'/(name+'.glsl')).read_bytes()
        dksh = (args.native/'dksh'/(name+'.dksh')).read_bytes()
        entry = entries[name]
        require(entry.get('ok') and entry.get('deterministic') and not entry.get('log', '').strip(),
            'Unverified native shader '+name)
        for label, data in [('spirv', spv), ('glsl', glsl), ('dksh', dksh)]:
            require(sha(data) == entry[label+'_sha256'], 'Changed '+label+' '+name)
        require(len(spv) >= 20 and len(spv)%4 == 0 and spv[:4] == b'\x03\x02\x23\x07', 'Bad SPIR-V')
        require(32 <= len(dksh) <= 4*1024*1024 and dksh[:4] == b'DKSH', 'Bad DKSH')
        program = struct.unpack_from('<I', dksh, 16)[0]
        require(program+24 <= len(dksh), 'Truncated DKSH program')
        max_scratch = max(max_scratch,struct.unpack_from('<I',dksh,program+20)[0])
        m = metadata[name]
        require(len(m['raw_fetch']) == 3 and len(m['export_constants']) == 8 and len(m['textures']) == 32,
            'Bad metadata shape')
        library_body += struct.pack('<IIQI3I8I32II', len(original), len(spv)//4,
            int(name[3:],16), int(m['memexport']), *m['raw_fetch'], *m['export_constants'],
            *m['textures'], m['bound_fetch'])
        library_body += original + spv
        vertex = name.startswith('vs_')
        count_vs += vertex
        cache_entries.append((xxhash.xxh3_64_intdigest(spv), 0, 0xffffffff if vertex else 0, dksh))
    require(len(library_body)+24 <= 768*1024*1024, 'Shader library exceeds runtime limit')
    library = struct.pack('<8sIIQ', b'MKVSPV\0\0', 4, len(manifest),
        xxhash.xxh3_64_intdigest(library_body)) + library_body
    unique_cache = {}
    for fingerprint, spec, variant, dksh in cache_entries:
        key = (fingerprint, spec, variant)
        require(key not in unique_cache or unique_cache[key] == dksh, 'Native cache key collision')
        unique_cache[key] = dksh
    cache_entries = [(*key, value) for key, value in sorted(unique_cache.items())]
    cache_index = bytearray()
    cache_data = bytearray()
    for fingerprint, spec, variant, dksh in cache_entries:
        cache_index += struct.pack('<QIIII', fingerprint, spec, variant, len(dksh), 0)
        cache_data += dksh
        cache_data += bytes((-len(cache_data))%4)
    body = cache_index + cache_data
    cache = struct.pack('<IIQIIQ', 0x4b44464e, 2,
        xxhash.xxh3_64_intdigest(COMPILER_ID.encode()), len(cache_entries), 0,
        xxhash.xxh3_64_intdigest(body)) + body
    # Margin for runtime alpha/output-pruned variants; queue creation reads this
    # small identity-checked file before loading the full shader library.
    queue_scratch = max(2048,(max_scratch+65536+511)&~511)
    require(queue_scratch <= 4*1024*1024, 'Shader scratch exceeds the supported queue budget')
    limits = struct.pack('<8sIIQQ',b'MKVLIMIT',1,queue_scratch,
        xxhash.xxh3_64_intdigest(COMPILER_ID.encode()),xxhash.xxh3_64_intdigest(library_body))
    args.output.mkdir(parents=True, exist_ok=True)
    outputs = {'mkvsdcu_shaders_dk.nfsp': library, 'mkvsdcu_shadercache_dksh.bin': cache,
        'mkvsdcu_shader_limits.bin': limits}
    report = {'abi': 'mkvsdcu-fixed-v4', 'shaders': len(manifest), 'vertex': count_vs,
        'pixel': len(manifest)-count_vs, 'cache_entries': len(cache_entries), 'compiler_id': COMPILER_ID,
        'max_per_warp_scratch': max_scratch, 'queue_per_warp_scratch': queue_scratch,
        'hardware_tested': False, 'files': {}}
    for name, data in outputs.items():
        path = args.output/name
        require(not path.exists(), 'Refusing to overwrite '+str(path))
        path.write_bytes(data)
        report['files'][name] = {'bytes': len(data), 'sha256': sha(data)}
    (args.output/'shader-verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
