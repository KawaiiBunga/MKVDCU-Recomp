"""Compile deko3d SPIR-V through the pinned SA2 converter and patched uam.

Records failures and warnings, compiler/converter identities and deterministic
recompiles. No default optimization passes are assumed safe for another game.
An offline compile verifies feasibility, not GPU execution or visual parity.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import time


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--spirv", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--converter", type=Path, required=True, help="Pinned deko3d_glsl.py")
    p.add_argument("--spirv-cross", type=Path, default=Path("E:/tools/build/spirv-cross/spirv-cross.exe"))
    p.add_argument("--uam", type=Path, default=Path("E:/tools/build/uam/uam.exe"))
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--allow-warnings", action="store_true")
    args = p.parse_args()
    if args.jobs < 1 or args.timeout < 1:
        p.error("jobs and timeout must be positive")
    for path in (args.converter, args.spirv_cross, args.uam):
        if not path.is_file():
            p.error("Missing tool: " + str(path))
    spec = importlib.util.spec_from_file_location("deko_converter", args.converter)
    converter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(converter)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        p.error("Use a fresh output directory; stale output must not pass this gate")
    paths = sorted(args.spirv.glob("*.spv"))
    if not paths:
        p.error("No SPIR-V input")
    translation_report = args.spirv.parent / "report.json"
    upstream_missing = []
    if translation_report.is_file():
        source_report = json.loads(translation_report.read_text())
        upstream_missing = source_report.get("missing", [])
        if source_report.get("requested", len(paths)) != len(paths) or upstream_missing:
            p.error("The translation report has missing shaders; a successful subset cannot pass the full gate")
    source_failures = sorted(x.name for x in args.spirv.glob("*.fail"))
    for folder in ("vulkan-glsl", "glsl", "dksh", "logs"):
        (output / folder).mkdir()

    def run(cmd):
        r = subprocess.run([str(x) for x in cmd], capture_output=True, text=True,
                           errors="replace", timeout=args.timeout)
        return r.returncode, r.stdout + r.stderr

    def one(path):
        name = path.stem
        stage = "frag" if name.startswith("ps_") else "vert"
        record = {"shader": name, "spirv_sha256": sha(path), "status": "failed"}
        logs = []
        started = time.perf_counter()
        try:
            vk = output / "vulkan-glsl" / (name + ".glsl")
            src = output / "glsl" / (name + ".glsl")
            dest = output / "dksh" / (name + ".dksh")
            code, log = run([args.spirv_cross, path, "--version", "460", "--vulkan-semantics",
                             "--relax-nan-checks", "--output", vk])
            logs.append(log)
            if code:
                raise ValueError("SPIRV-Cross failed")
            text, _ = converter.convert(vk.read_text(), stage, converter.DEFAULT_UBO_MAP, "handle")
            src.write_text(text, encoding="utf-8", newline="\n")
            code, log = run([args.uam, "-s", stage, "-o", dest, src])
            logs.append(log)
            if code or not dest.is_file():
                raise ValueError("uam failed")
            data = dest.read_bytes()
            if data[:4] != b"DKSH":
                raise ValueError("Invalid DKSH header")
            again = dest.with_suffix(".repeat.dksh")
            code, log = run([args.uam, "-s", stage, "-o", again, src])
            logs.append(log)
            if code or not again.exists() or again.read_bytes() != data:
                raise ValueError("DKSH differs across repeated compiles")
            again.unlink()
            record.update(status="compiled", dksh_bytes=len(data), dksh_sha256=sha(dest),
                          deterministic=True)
        except Exception as exc:
            record["error"] = str(exc)
        record["warnings"] = sorted({line.strip() for log in logs for line in log.splitlines()
                                     if "warning" in line.lower()})
        record["seconds"] = time.perf_counter() - started
        (output / "logs" / (name + ".log")).write_text("\n".join(logs), encoding="utf-8")
        return record

    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for r in pool.map(one, paths):
            records.append(r)
            if r["status"] != "compiled" or len(records) % 100 == 0:
                print(f"{len(records)}/{len(paths)} {r['shader']}: {r['status']} {r.get('error', '')}", flush=True)
    failed = sum(r["status"] != "compiled" for r in records)
    warned = sum(bool(r["warnings"]) for r in records)
    report = {"requested": len(records), "compiled": len(records) - failed, "failed": failed,
              "shaders_with_warnings": warned, "upstream_spirv_failures": source_failures,
              "translation_report_sha256": sha(translation_report) if translation_report.is_file() else None,
              "variants": "default specialization values only; observed gameplay variants still required",
              "gpu_tested": False,
              "tools": {"converter_sha256": sha(args.converter), "uam_sha256": sha(args.uam),
                        "spirv_cross_sha256": sha(args.spirv_cross)}, "shaders": records}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "shaders"}, indent=2))
    if failed or source_failures or (warned and not args.allow_warnings):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
