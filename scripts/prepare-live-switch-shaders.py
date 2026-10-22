"""Wrap explicitly selected live utility programs for the fixed native translator.

Utility programs use register-number interpolators rather than asset CTAB
semantics. Register i is mapped to TEXCOORDi at both ends; vertex fetches stay
raw GPU fetches. The original instructions are preserved byte-for-byte.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import xxhash


EXEC = {1, 2, 3, 4, 5, 6, 13, 14}


def blocks(words):
    limit, offset, instructions = len(words)//3, 0, []
    while offset < limit:
        a, b, c = words[offset*3:offset*3+3]
        for cf in (a | ((b & 65535) << 32), (b >> 16) | (c << 16)):
            opcode = (cf >> 44) & 15
            if opcode not in EXEC:
                if opcode not in {0, 12, 15}:
                    raise ValueError('Utility wrapper requires straight-line control flow')
                continue
            address, count, sequence = cf & 4095, (cf >> 12) & 7, (cf >> 16) & 4095
            if not address or address+count > len(words)//3:
                raise ValueError('Invalid utility EXEC range')
            limit = min(limit, address)
            if offset >= limit:
                raise ValueError('Utility control flow overlaps instructions')
            for i in range(count):
                instructions.append((bool((sequence >> (i*2)) & 1),
                                     words[(address+i)*3:(address+i+1)*3]))
        offset += 1
    return instructions


def container(raw, vertex):
    words = struct.unpack('<'+'I'*(len(raw)//4), raw)
    instructions = blocks(words)
    outputs = 0
    for fetch, (a, b, c) in instructions:
        if fetch or not (a & (1 << 15)) or not ((a >> 16) & 255):
            continue
        register = a & 63
        if register < 4:
            outputs |= 1 << register
        elif register == 61:
            outputs |= 16
    if not vertex and not outputs:
        raise ValueError('Pixel utility has no supported output')
    # A single complete float bank prevents absent CTAB names from reducing
    # uploads. There are no baked definition-table constants in a live program.
    count = 256 if vertex else 224
    ctab = struct.pack('>7I', 28, 0, 0, 1, 28, 0, 0)
    ctab += struct.pack('>IHHHHII', 64, 2, 0, count, 0, 48, 0)
    ctab += struct.pack('>6HI', 1, 3, 1, 4, count, 0, 0)
    ctab += b'LiveFloat\0'
    ctab += bytes((-len(ctab)) % 4)
    ctab = struct.pack('>I', len(ctab))+ctab
    shader_offset = 36+len(ctab)
    shader = struct.pack('>6I', 0, len(raw), 0, 0xFF00, 0, 16 << 5)
    shader += struct.pack('>3I', 0, 0, 0) if vertex else struct.pack('>2I', 0, outputs)
    shader += b''.join(struct.pack('>I', i | (5 << 4) | (i << 8)) for i in range(16))
    virtual_size = shader_offset+len(shader)
    header = struct.pack('>9I', 0x102a1101 if vertex else 0x102a1100,
                         virtual_size, len(raw), 0, 36, 0, shader_offset, 0, 0)
    return header+ctab+shader+struct.pack('>'+'I'*len(words), *words), outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--names', nargs='+', required=True,
                        help='Explicit utility program stems observed in the failing draws')
    parser.add_argument('--template', action='append', default=[], metavar='NAME=FILE',
                        help='Preserve captured constants/interpolators; use exact live code and raw vertex fetches')
    args = parser.parse_args()
    templates = {}
    for item in args.template:
        name, filename = item.split('=', 1)
        if name not in args.names or name in templates:
            parser.error('Each template must name one selected live program once')
        templates[name] = Path(filename)
    args.output.mkdir(parents=True, exist_ok=True)
    if any(args.output.iterdir()):
        parser.error('Use a fresh output directory')
    packed = args.output/'packed'
    packed.mkdir()
    manifest, provenance = [], []
    for name in sorted(set(args.names)):
        if len(name) != 19 or name[:3] not in ('vs_', 'ps_'):
            raise ValueError('Invalid utility program name')
        source = args.capture/(name+'.ucode')
        data = source.read_bytes()
        magic, stage, count = struct.unpack_from('<8sII', data)
        if magic != b'MKVCODE1' or stage not in (0, 1) or len(data) != 16+count*4 or count % 3:
            raise ValueError('Invalid live program capture')
        if not 0 < count <= 96:
            raise ValueError('Only the observed small utility programs belong in this wrapper')
        vertex, raw = stage == 0, data[16:]
        if vertex != name.startswith('vs_') or xxhash.xxh3_64_intdigest(raw) != int(name[3:], 16):
            raise ValueError('Live program fingerprint differs')
        original, outputs = container(raw, vertex)
        template_digest = None
        if name in templates:
            template = templates[name].read_bytes()
            template_digest = hashlib.sha256(template).hexdigest()
            header = struct.unpack_from('>9I', template)
            flags, virtual_size, physical_size = header[:3]
            if flags != (0x102a1101 if vertex else 0x102a1100) or len(template) != virtual_size+physical_size:
                raise ValueError('Invalid template stage or container size')
            offset, size = struct.unpack_from('>2I', template, header[6])
            if size != len(raw) or offset+size != physical_size:
                raise ValueError('Template instruction range differs')
            if vertex:
                # GPU utility draws may bypass the D3D object queue. Read their
                # actual patched fetch constants rather than binding a CPU
                # declaration, while retaining baked constants and varyings.
                shader = header[6]
                previous, elements = struct.unpack_from('>2I', template, shader+24)
                declaration = shader+36
                end = declaration+4*(previous+elements)
                if end > virtual_size:
                    raise ValueError('Invalid template vertex declaration')
                removed = end-declaration
                updated = bytearray(template[:declaration]+template[end:])
                struct.pack_into('>I', updated, 4, virtual_size-removed)
                struct.pack_into('>2I', updated, shader+24, 0, 0)
                template = bytes(updated)
                virtual_size -= removed
            start = virtual_size+offset
            encoded = struct.pack('>'+'I'*count, *struct.unpack('<'+'I'*count, raw))
            original = template[:start]+encoded
        key = ('vs_' if vertex else 'ps_')+f'{xxhash.xxh3_64_intdigest(original):016x}'
        (packed/(key+'.bin')).write_bytes(original)
        manifest.append(dict(shader=key, sha256=hashlib.sha256(original).hexdigest()))
        provenance.append(dict(shader=key, live_program=name, words=count, pixel_outputs=outputs,
                               capture_sha256=hashlib.sha256(data).hexdigest(),
                               template_sha256=template_digest))
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (args.output/'live-programs.json').write_text(json.dumps(provenance, indent=2)+'\n')
    print(f'Prepared {len(manifest)} live utility programs with unchanged instruction bytes')


if __name__ == '__main__':
    main()
