"""Stage a verified native NRO and its complete private shader package."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--shaders',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    root=Path(__file__).resolve().parent.parent
    build=json.loads((args.build/'switch-build-verification.json').read_text())
    shaders=json.loads((args.shaders/'shader-verification.json').read_text())
    if build.get('graphics_api')!='deko3d' or build.get('direct_call_hook_bypasses')!=0:
        raise ValueError('Native executable verification is incomplete')
    static_count=shaders.get('base_static_shaders',shaders.get('shaders'))
    if static_count!=19340 or shaders.get('shaders',0)<19340 or shaders.get('abi')!='mkvsdcu-fixed-v4':
        raise ValueError('Native shader corpus is incomplete')
    nro=args.build/'mkvsdcu.nro'
    if digest(nro)!=build['sha256']:
        raise ValueError('NRO changed after verification')
    for name,record in shaders['files'].items():
        path=args.shaders/name
        if path.name!=name or digest(path)!=record['sha256'] or path.stat().st_size!=record['bytes']:
            raise ValueError('Shader package changed after verification: '+name)
    args.output.mkdir(parents=True,exist_ok=True)
    sources={
        'mkvsdcu.nro':nro,
        'mkvsdcu.toml':root/'targets/mkvsdcu-nx/config/deko3d-defaults.toml',
        'README.md':root/'targets/mkvsdcu-nx/DEKO3D.md',
        'switch-build-verification.json':args.build/'switch-build-verification.json',
        'shader-verification.json':args.shaders/'shader-verification.json',
        'licenses/renderer-GPL-3.0.txt':root/'targets/mkvsdcu-nx/thirdparty/nfsmw-deko3d/LICENSE',
        'licenses/XenosRecomp-MIT.txt':root/'tools/mkvsdcu-fixed-shaders/XenosRecomp/LICENSE.md',
        'licenses/ReXGlue.txt':root/'references/mkvsdcu-deko-sdk/LICENSE',
    }
    sources.update({name:args.shaders/name for name in shaders['files']})
    files={}
    for name,source in sources.items():
        destination=args.output/name
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,destination)
        files[name]={'bytes':destination.stat().st_size,'sha256':digest(destination)}
    report={'graphics_api':'deko3d','hardware_tested':False,'shader_containers':shaders['shaders'],
        'base_static_shaders':static_count,
        'native_cache_entries':shaders['cache_entries'],'files':files,
        'warning':'Private shaders from the user game; no redistribution. Current rendering correction awaits console visual/performance verification.'}
    (args.output/'package-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    archive=args.output.parent/'mkvsdcu-deko3d.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=1,allowZip64=True) as z:
        for name in sorted([*sources,'package-verification.json']):
            z.write(args.output/name,'mkvsdcu/'+name)
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise ValueError('Package ZIP CRC check failed')
    print(json.dumps({'package':str(archive.resolve()),'bytes':archive.stat().st_size,
        'sha256':digest(archive),'nro_sha256':build['sha256'],'hardware_tested':False},indent=2))

if __name__=='__main__':
    main()
