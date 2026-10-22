"""Run the pinned native XenosRecomp feasibility audit in bounded process batches.

Each batch has a separate log. Runtime cache generation is skipped because it
does not validate translation. The translator must have xenosrecomp-native-windows.patch.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--containers", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--translator", type=Path, default=Path("E:/tools/build/xenos-audit-bin/XenosRecomp.exe"))
    p.add_argument("--common", type=Path, default=Path("E:/tools/src/XenosRecomp-sa2-adf3a4e/XenosRecomp/shader_common.h"))
    p.add_argument("--jobs", type=int, default=6)
    p.add_argument("--batch-size", type=int, default=256)
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--limit", type=int, default=0, help="Optional feasibility sample; zero means all inputs")
    args = p.parse_args()
    if min(args.jobs, args.batch_size, args.timeout) < 1 or args.limit < 0:
        p.error("Invalid jobs, batch size, timeout or limit")
    for path in (args.translator, args.common):
        if not path.is_file():
            p.error("Missing tool input: " + str(path))
    paths = sorted(args.containers.glob("*.bin"))
    if args.limit:
        paths = paths[:args.limit]
    if not paths:
        p.error("No shader containers")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        p.error("Use a fresh output directory so stale SPIR-V cannot pass this audit")
    for folder in ("spirv", "batches", "logs"):
        (output / folder).mkdir()
    batches = []
    manifest = []
    for start in range(0, len(paths), args.batch_size):
        index = len(batches)
        root = output / "batches" / f"{index:04d}"
        inputs = root / "input"
        inputs.mkdir(parents=True)
        # One packed input avoids opening hundreds of small files in each child.
        # The upstream directory scanner expects word-aligned containers.
        with (inputs / "containers.bin").open("wb") as packed:
            for path in paths[start:start + args.batch_size]:
                data = path.read_bytes()
                if len(data) % 4:
                    raise ValueError("Container size is not word-aligned: " + path.name)
                packed.write(data)
                manifest.append({"shader": path.stem, "batch": index})
        batches.append((index, root, len(paths[start:start + args.batch_size])))
    (output / "inputs.json").write_text(json.dumps(manifest, indent=2) + "\n")
    env = os.environ.copy()
    env["MKVSDCU_DEKO3D_ONLY"] = "1"
    env["MKVSDCU_SHADER_AUDIT"] = "1"
    env["BANJO_DEKO3D_SPIRV_DIR"] = str(output / "spirv")
    # Make the MinGW-built fmt dependency available without a system installation.
    compiler_bin = Path("E:/tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin")
    env["PATH"] = str(compiler_bin) + os.pathsep + env.get("PATH", "")
    env["TEMP"] = env["TMP"] = "E:/tools/tmp"

    def one(batch):
        index, root, requested = batch
        started = time.perf_counter()
        result = {"batch": index, "requested": requested}
        try:
            with (output / "logs" / f"{index:04d}.log").open("w") as log:
                proc = subprocess.run([str(args.translator.resolve()), str(root / "input"),
                    str(root / "shader_cache.deko.cpp"), str(args.common.resolve())],
                    stdout=log, stderr=subprocess.STDOUT, env=env, timeout=args.timeout)
                result["exit_code"] = proc.returncode
        except subprocess.TimeoutExpired:
            result.update(exit_code=-1, error="Batch timed out; child terminated")
        result["seconds"] = time.perf_counter() - started
        print(f"Batch {index + 1}/{len(batches)}: exit {result['exit_code']}, {result['seconds']:.1f}s", flush=True)
        return result

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(one, batches))
    produced = sorted(path.stem.lower() for path in (output / "spirv").glob("*.spv"))
    expected = {path.stem.lower() for path in paths}
    missing = sorted(expected - set(produced))
    report = {"requested": len(paths), "produced": len(produced), "missing": missing,
              "sample_only": bool(args.limit), "deko3d_only": True,
              "translator_sha256": hashlib.sha256(args.translator.read_bytes()).hexdigest(),
              "common_sha256": hashlib.sha256(args.common.read_bytes()).hexdigest(), "batches": results}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"SPIR-V: {len(produced)}/{len(paths)}, {len(missing)} missing", flush=True)
    if missing or any(r["exit_code"] != 0 for r in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
