"""Fast full-corpus audit: native SPIRV-Cross C API, Python converter, resident uam.

Each uam child is single-threaded. Multiple independent processes are safe;
concurrent calls into the same uam context are not. Checks default variants only.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


class SpirvCross:
    def __init__(self, path):
        self.dll_paths = []
        if os.name == "nt":
            for directory in (path.parent, Path("E:/tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin")):
                self.dll_paths.append(os.add_dll_directory(str(directory)))
        self.api = C.CDLL(str(path.resolve()))
        ptr = C.c_void_p
        signatures = {
            "context_create": ([C.POINTER(ptr)], C.c_int),
            "context_destroy": ([ptr], None),
            "context_get_last_error_string": ([ptr], C.c_char_p),
            "context_parse_spirv": ([ptr, C.POINTER(C.c_uint32), C.c_size_t, C.POINTER(ptr)], C.c_int),
            "context_create_compiler": ([ptr, C.c_int, ptr, C.c_int, C.POINTER(ptr)], C.c_int),
            "compiler_create_compiler_options": ([ptr, C.POINTER(ptr)], C.c_int),
            "compiler_options_set_uint": ([ptr, C.c_int, C.c_uint], C.c_int),
            "compiler_options_set_bool": ([ptr, C.c_int, C.c_ubyte], C.c_int),
            "compiler_install_compiler_options": ([ptr, ptr], C.c_int),
            "compiler_compile": ([ptr, C.POINTER(C.c_char_p)], C.c_int),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.api, "spvc_" + name)
            fn.argtypes = args
            fn.restype = result

    def compile(self, data):
        if len(data) % 4 or data[:4] != b"\x03\x02\x23\x07":
            raise ValueError("Invalid SPIR-V header/size")
        api = self.api
        context = C.c_void_p()
        if api.spvc_context_create(C.byref(context)):
            raise ValueError("SPIRV-Cross context creation failed")

        def check(result):
            if result:
                reason = api.spvc_context_get_last_error_string(context)
                raise ValueError(reason.decode(errors="replace") if reason else f"SPIRV-Cross error {result}")

        try:
            words = (C.c_uint32 * (len(data) // 4)).from_buffer_copy(data)
            ir, compiler, options = C.c_void_p(), C.c_void_p(), C.c_void_p()
            check(api.spvc_context_parse_spirv(context, words, len(words), C.byref(ir)))
            check(api.spvc_context_create_compiler(context, 1, ir, 1, C.byref(compiler)))
            check(api.spvc_compiler_create_compiler_options(compiler, C.byref(options)))
            # Constants from the pinned spirv_cross_c.h; identical to CLI flags:
            # --version 460 --vulkan-semantics --relax-nan-checks.
            check(api.spvc_compiler_options_set_uint(options, 0x2000000 | 8, 460))
            check(api.spvc_compiler_options_set_bool(options, 0x2000000 | 10, 1))
            check(api.spvc_compiler_options_set_bool(options, 0x1000000 | 78, 1))
            check(api.spvc_compiler_install_compiler_options(compiler, options))
            source = C.c_char_p()
            check(api.spvc_compiler_compile(compiler, C.byref(source)))
            return source.value.decode("utf-8")
        finally:
            api.spvc_context_destroy(context)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--spirv", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--converter", type=Path, required=True)
    p.add_argument("--cross-library", type=Path, default=Path("E:/tools/build/spirv-cross/libspirv-cross-c-shared.dll"))
    p.add_argument("--uam-batch", type=Path, default=Path("E:/tools/build/uam-audit/mkvsdcu-uam-audit.exe"))
    p.add_argument("--jobs", type=int, default=6)
    p.add_argument("--timeout", type=int, default=7200,
                   help="Maximum seconds per resident compiler process")
    p.add_argument("--resume", action="store_true",
                   help="Reuse only successful, hash-verified results from this output's report")
    args = p.parse_args()
    if args.jobs < 1 or args.timeout < 1:
        p.error("Invalid jobs/timeout")
    for path in (args.converter, args.cross_library, args.uam_batch):
        if not path.is_file():
            p.error("Missing tool: " + str(path))
    inputs = sorted(args.spirv.glob("*.spv"))
    if not inputs:
        p.error("No SPIR-V input")
    if any(args.spirv.glob("*.fail")):
        p.error("SPIR-V input contains failed-compilation markers")
    prior = args.spirv.parent / "report.json"
    if prior.is_file():
        report = json.loads(prior.read_text())
        if report.get("missing") or report.get("requested", len(inputs)) != len(inputs):
            p.error("Translation is incomplete; a successful subset cannot pass the gate")
    tool_hashes = {"converter_sha256": digest(args.converter),
                   "spirv_cross_library_sha256": digest(args.cross_library),
                   "uam_batch_sha256": digest(args.uam_batch)}
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    previous = {}
    if args.resume:
        previous_path = output / "report.json"
        if not previous_path.is_file():
            p.error("Resume requires a completed audit report; do not resume an active compiler")
        previous_report = json.loads(previous_path.read_text())
        if previous_report.get("tools") != tool_hashes:
            p.error("Compiler/converter changed; use a fresh output directory")
        if previous_report.get("requested") != len(inputs):
            p.error("Input count changed; use a fresh output directory")
        previous = {r["shader"]: r for r in previous_report["shaders"]}
        if set(previous) != {path.stem for path in inputs}:
            p.error("Input identities changed; use a fresh output directory")
    elif any(output.iterdir()):
        p.error("Use a fresh output directory or --resume with a completed report")
    for directory in ("glsl", "vulkan-glsl", "dksh", "logs"):
        (output / directory).mkdir(exist_ok=args.resume)
    spec = importlib.util.spec_from_file_location("pinned_deko_converter", args.converter)
    converter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(converter)
    cross = SpirvCross(args.cross_library)

    def convert(path):
        name = path.stem
        record = {"shader": name, "status": "failed", "spirv_sha256": digest(path)}
        try:
            if not re.fullmatch(r"(?:vs|ps)_[0-9a-fA-F]{16}", name):
                raise ValueError("Unknown shader stage/name")
            vk = cross.compile(path.read_bytes())
            stage = "frag" if name.startswith("ps_") else "vert"
            glsl, _ = converter.convert(vk, stage, converter.DEFAULT_UBO_MAP, "handle")
            old = previous.get(name)
            if old is not None:
                if old.get("spirv_sha256") != record["spirv_sha256"]:
                    raise ValueError("SPIR-V changed since the previous report")
                if old.get("status") == "compiled" and old.get("deterministic") and not old.get("warnings"):
                    file = output / "dksh" / (name + ".dksh")
                    source = output / "glsl" / (name + ".glsl")
                    if (not file.is_file() or digest(file) != old.get("dksh_sha256") or
                            not source.is_file() or source.read_bytes() != glsl.encode("utf-8")):
                        raise ValueError("Previously compiled output changed; refusing to reuse it")
                    return dict(old, reused=True)
            (output / "vulkan-glsl" / (name + ".glsl")).write_text(vk, encoding="utf-8", newline="\n")
            (output / "glsl" / (name + ".glsl")).write_text(glsl, encoding="utf-8", newline="\n")
            record.update(status="converted", stage=stage)
        except Exception as exc:
            record["error"] = str(exc)
        return record

    converted = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for index, record in enumerate(pool.map(convert, inputs), 1):
            converted.append(record)
            if index % 1000 == 0:
                print(f"GLSL {index}/{len(inputs)}", flush=True)
    groups = [[] for _ in range(args.jobs)]
    for index, record in enumerate(r for r in converted if r["status"] == "converted"):
        groups[index % args.jobs].append(record)

    def compile_group(pair):
        index, records = pair
        if not records:
            return []
        manifest = output / "logs" / f"uam-{index}.tsv"
        result_path = manifest.with_suffix(".jsonl")
        manifest.write_text("".join(
            f"{r['shader']}\t{r['stage']}\t{output / 'glsl' / (r['shader'] + '.glsl')}\t"
            f"{output / 'dksh' / (r['shader'] + '.dksh')}\n" for r in records), encoding="utf-8")
        code = -1
        try:
            env = os.environ.copy()
            env["TEMP"] = env["TMP"] = "E:/tools/tmp"
            with manifest.with_suffix(".log").open("w") as log:
                proc = subprocess.run([str(args.uam_batch.resolve()), str(manifest), str(result_path)],
                    stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, env=env)
                code = proc.returncode
        except subprocess.TimeoutExpired:
            pass
        results = {}
        if result_path.exists():
            for line in result_path.read_text(encoding="utf-8").splitlines():
                row = json.loads(line)
                if row["shader"] in results:
                    raise ValueError("Duplicate resident compiler result")
                results[row["shader"]] = row
        for r in records:
            result = results.get(r["shader"])
            r["status"] = "failed"
            if result is None:
                r["error"] = f"Missing resident compiler result, process exit {code}"
                continue
            r["warnings"] = sorted({line.strip() for line in result["log"].splitlines() if "warning" in line.lower()})
            r["seconds"] = result["seconds"]
            file = output / "dksh" / (r["shader"] + ".dksh")
            if result["ok"] and result["deterministic"] and file.exists() and file.read_bytes()[:4] == b"DKSH":
                r.update(status="compiled", deterministic=True, dksh_sha256=digest(file), dksh_bytes=file.stat().st_size)
            else:
                r["error"] = result["log"]
        if code != 0 and all(r["status"] == "compiled" for r in records):
            records[-1].update(status="failed", error=f"Compiler exited abnormally after emitting results: {code}")
        print(f"uam worker {index}: {len(records)} results, exit {code}", flush=True)
        return records

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(compile_group, enumerate(groups)))
    failed = sum(r["status"] != "compiled" for r in converted)
    warnings = sum(bool(r.get("warnings")) for r in converted)
    report = {"requested": len(inputs), "compiled": len(inputs) - failed, "failed": failed,
              "shaders_with_warnings": warnings, "gpu_tested": False,
              "variants": "default specialization values only",
              "translation_report_sha256": digest(prior) if prior.is_file() else None,
              "reused": sum(bool(r.get("reused")) for r in converted),
              "tools": tool_hashes,
              "shaders": converted}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"DKSH {report['compiled']}/{len(inputs)}, {failed} failures, {warnings} shaders with warnings", flush=True)
    if failed or warnings:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
