"""Prepare a separate, pinned native SDK without changing the legacy SDK.

Uses the existing dependency checkout and E:/tools sources. No installers,
Docker or WSL. Existing output is verified rather than overwritten.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent
COMMIT = '266f943263993148e49e968208a4f784ddd8d88d'
ARCHIVE = Path('E:/tools/downloads/nfsmw-deko-266f9432.zip')
ARCHIVE_SHA = 'b8aadb65426adece0294192eb2bd4d62b2ef6b08fc20c96465925e126fcc6ecb'

def main():
    sdk = ROOT/'references/mkvsdcu-deko-sdk'
    digest = hashlib.file_digest(ARCHIVE.open('rb'), 'sha256').hexdigest()
    if digest != ARCHIVE_SHA:
        raise ValueError('Pinned SDK archive hash mismatch')
    with zipfile.ZipFile(ARCHIVE) as archive:
        prefix = archive.namelist()[0].split('/')[0]+'/sdk/'
        for member in archive.infolist():
            if not member.filename.startswith(prefix) or member.is_dir():
                continue
            rel = PurePosixPath(member.filename[len(prefix):])
            if '..' in rel.parts or rel.is_absolute():
                raise ValueError('Unsafe archive path')
            # Dependencies are already available locally, shared through junctions.
            if rel.parts[0] == 'thirdparty' and tuple(rel.parts) != ('thirdparty', 'CMakeLists.txt'):
                continue
            destination = sdk.joinpath(*rel.parts)
            if not destination.exists():
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.read(member))
    dependencies = ROOT/'references/nfsmw-nx-main/sdk/thirdparty'
    for source in dependencies.iterdir():
        if not source.is_dir():
            continue
        target = sdk/'thirdparty'/source.name
        if source.name in ('uam', 'SPIRV-Cross'):
            continue
        if not target.exists():
            subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-File',
                str(ROOT/'scripts/deko3d-junction.ps1'), '-Path', str(target), '-Target', str(source)], check=True)
    for name, source in [('uam', Path('E:/tools/src/uam-sa2-adf3a4e')),
                         ('SPIRV-Cross', Path('E:/tools/src/SPIRV-Cross-vulkan-sdk-1.4.363.0'))]:
        if not source.is_dir():
            raise ValueError('Missing pinned tool source '+str(source))
        target = sdk/'thirdparty'/name
        if not target.exists():
            subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-File',
                str(ROOT/'scripts/deko3d-junction.ps1'), '-Path', str(target), '-Target', str(source)], check=True)
    patches = [ROOT/'patches/rexglue-sdk'/name for name in
               ('switch-port-menu.patch', 'audio-client-lifetime.patch')]
    git = ['git', '-c', 'safe.directory='+ROOT.as_posix(), '-C', str(ROOT), 'apply',
           '--directory=references/mkvsdcu-deko-sdk', '--ignore-space-change']
    for patch in patches:
        if subprocess.run(git+['--check', str(patch)], capture_output=True).returncode == 0:
            subprocess.run(git+[str(patch)], check=True)
        elif subprocess.run(git+['--reverse', '--check', str(patch)], capture_output=True).returncode:
            raise ValueError('SDK conflicts with '+patch.name)
    header = sdk/'include/rex/string/buffer.h'
    text = header.read_text()
    if '#include <cstdarg>' not in text:
        header.write_text(text.replace('#include <cstdint>', '#include <cstdint>\n#include <cstdarg>'))
    report = {'commit': COMMIT, 'archive_sha256': digest, 'patches': [p.name for p in patches],
        'gcc16_fix': 'Explicit cstdarg include in rex/string/buffer.h',
        'dependencies': str(dependencies), 'sdk': str(sdk)}
    (sdk/'mkvsdcu-native-sdk.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
