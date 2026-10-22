"""Compile and verify the renderer's fixed-binding GLSL with resident patched uam."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess

def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--spirv',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--glsl-tool',type=Path,default=Path('E:/tools/build/mkvsdcu-fixed-shaders/mkvsdcu-native-glsl.exe'))
    p.add_argument('--uam',type=Path,default=Path('E:/tools/build/uam-audit/mkvsdcu-uam-audit.exe'))
    p.add_argument('--jobs',type=int,default=6)
    args=p.parse_args()
    if args.jobs<1: p.error('Invalid job count')
    source_report=json.loads((args.spirv.parent/'report.json').read_text())
    if source_report['missing'] or any(r['exit_code'] for r in source_report['results']): p.error('SPIR-V is incomplete')
    inputs=sorted(args.spirv.glob('*.spv'))
    if len(inputs)!=source_report['requested']: p.error('SPIR-V count differs from report')
    args.output.mkdir(parents=True,exist_ok=True)
    if any(args.output.iterdir()): p.error('Use a fresh output directory')
    glsl=args.output/'glsl'; dksh=args.output/'dksh'; dksh.mkdir()
    env=dict(os.environ,TEMP='E:/tools/tmp',TMP='E:/tools/tmp')
    with (args.output/'glsl.log').open('w') as log:
        subprocess.run([str(args.glsl_tool.resolve()),str(args.spirv.resolve()),str(glsl.resolve())],stdout=log,stderr=subprocess.STDOUT,env=env,check=True)
    rows={line.split('\t')[0]:line.split('\t') for line in (args.output/'glsl-report.tsv').read_text().splitlines()}
    if set(rows)!={f.stem for f in inputs}: raise ValueError('GLSL conversion is incomplete')
    manifests=[]
    for worker in range(args.jobs):
        path=args.output/f'uam-{worker}.tsv'
        path.write_text(''.join(f'{f.stem}\t{rows[f.stem][1]}\t{(glsl/(f.stem+".glsl")).resolve()}\t{(dksh/(f.stem+".dksh")).resolve()}\n' for f in inputs[worker::args.jobs]),encoding='utf-8')
        manifests.append((worker,path))
    def compile_one(item):
        worker,path=item
        report=args.output/f'uam-{worker}.jsonl'
        with (args.output/f'uam-{worker}.log').open('w') as log:
            result=subprocess.run([str(args.uam.resolve()),str(path.resolve()),str(report.resolve())],stdout=log,stderr=subprocess.STDOUT,env=env,timeout=14400)
        return result.returncode,report
    results=[]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for code,report in pool.map(compile_one,manifests):
            print(f'{report.name}: exit {code}',flush=True)
            results.extend(json.loads(line) for line in report.read_text().splitlines())
    by_name={r['shader']:r for r in results}
    failures=[]
    entries=[]
    for f in inputs:
        name=f.stem; result=by_name.get(name,{})
        good=result.get('ok') and result.get('deterministic') and not result.get('log','').strip()
        if not good: failures.append(name)
        entry=dict(result,shader=name,spirv_sha256=digest(f),glsl_sha256=digest(glsl/(name+'.glsl')))
        if good: entry['dksh_sha256']=digest(dksh/(name+'.dksh'))
        entries.append(entry)
    report={'abi':source_report['abi'],'requested':len(inputs),'compiled':len(inputs)-len(failures),'failures':failures,
        'glsl_tool_sha256':digest(args.glsl_tool),'uam_sha256':digest(args.uam),'entries':entries}
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{report["compiled"]}/{len(inputs)} deterministic native DKSH, {len(failures)} failures',flush=True)
    if failures: raise SystemExit(1)
if __name__=='__main__': main()
