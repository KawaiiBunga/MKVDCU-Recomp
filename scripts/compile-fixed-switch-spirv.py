"""Compile MKvDCU's fixed-binding native ABI; reject incomplete corpora."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--dxc', type=Path, default=Path('E:/tools/dxc/bin/x64/dxc.exe'))
    args = p.parse_args()
    if args.jobs < 1 or not args.dxc.is_file(): p.error('Invalid jobs or missing DXC')
    args.output.mkdir(parents=True, exist_ok=True)
    if any(args.output.iterdir()): p.error('Use a fresh output directory')
    spirv = args.output / 'spirv'; spirv.mkdir()
    logs = args.output / 'logs'; logs.mkdir()
    expected = {r['shader'] for r in json.loads(args.manifest.read_text())}
    inputs = sorted(args.input.glob('*.hlsl'))
    env = dict(os.environ, TEMP='E:/tools/tmp', TMP='E:/tools/tmp')
    def one(path):
        stage = 'vs' if path.stem.startswith('vs_') else 'ps'
        command = [str(args.dxc.resolve()), '-spirv', '-T', stage+'_6_6', '-E', 'main',
            '-HV', '2021', '-fspv-target-env=vulkan1.2', '-fvk-use-dx-layout',
            '-DNFSMW_DEKO3D', '-Werror=parameter-usage', '-Fo', str((spirv/(path.stem+'.spv')).resolve()), str(path.resolve())]
        with (logs/(path.stem+'.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=180)
        return {'shader':path.stem, 'exit_code':result.returncode,
            'hlsl_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = []
        for r in pool.map(one, inputs):
            results.append(r)
            if len(results)%256 == 0: print(f'DXC {len(results)}/{len(inputs)}',flush=True)
    produced = {f.stem for f in spirv.glob('*.spv') if f.stat().st_size >= 20}
    missing = sorted(expected-produced)
    report = {'abi':'mkvsdcu-fixed-v4','requested':len(expected),'produced':len(produced),'missing':missing,
        'unexpected':sorted(produced-expected),'dxc_sha256':hashlib.sha256(args.dxc.read_bytes()).hexdigest(),'results':results}
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(produced)}/{len(expected)} SPIR-V; {len(missing)} missing',flush=True)
    if missing or report['unexpected'] or any(r['exit_code'] for r in results): raise SystemExit(1)
if __name__ == '__main__': main()
