# SPDX-License-Identifier: GPL-3.0-only
"""Stage hook-safe direct guest calls for GCC/LTO without editing generated inputs.

Adapted from StevensND/nfsmw-nx tools/llamadas_directas.py (GPL-3.0-only).
Dispatch tables stay untouched. Every address mentioned by app/SDK/config sources
is conservatively treated as hooked, including macro-built hook names.
"""
import argparse
import json
from pathlib import Path
import re

ADDRESS = re.compile(r"(?<![0-9A-Fa-f])(8[0-9A-Fa-f]{7})(?![0-9A-Fa-f])")
CALL = re.compile(r"(?<![\w])sub_([0-9A-Fa-f]{8})\(ctx, base\);")
DIRECT_CALL = re.compile(r"__imp__sub_([0-9A-Fa-f]{8})\(ctx, base\);")
SOURCE_EXTENSIONS = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".inc", ".inl", ".toml", ".cmake"}


def find_hooks(roots):
    hooks = set()
    for root in roots:
        files = [root] if root.is_file() else root.rglob("*")
        for source in files:
            if source.is_file() and source.suffix in SOURCE_EXTENSIONS:
                hooks.update(x.upper() for x in ADDRESS.findall(source.read_text(encoding="utf-8", errors="replace")))
    return hooks


def transform(text, hooks):
    # Generated inputs may already contain a prior direct-call optimization.
    # Newly installed native hooks must intercept those calls too.
    text = DIRECT_CALL.sub(lambda m: 'sub_'+m[1]+'(ctx, base);'
        if m[1].upper() in hooks else m.group(0), text)
    def replace(match):
        return match.group(0) if match[1].upper() in hooks else "__imp__sub_" + match[1] + "(ctx, base);"
    return CALL.sub(replace, text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--hook-root", type=Path, action="append", default=[])
    args = parser.parse_args()
    hooks = find_hooks(args.hook_root)
    args.output.mkdir(parents=True, exist_ok=True)
    changed_calls = kept_calls = existing_direct_calls = restored_calls = files = 0
    for source in sorted(args.generated.glob("mkvsdcu_recomp.*.cpp")):
        original = source.read_text(encoding="utf-8")
        existing_direct_calls += len(re.findall(r"__imp__sub_[0-9A-Fa-f]{8}\(ctx, base\);", original))
        restored_calls += sum(m[1].upper() in hooks for m in DIRECT_CALL.finditer(original))
        staged = transform(original, hooks)
        for call in CALL.finditer(original):
            if call[1].upper() in hooks:
                kept_calls += 1
            else:
                changed_calls += 1
        # Catch accidental hook bypass before any staged file reaches the compiler.
        direct = set(re.findall(r"__imp__sub_([0-9A-Fa-f]{8})\(ctx, base\);", staged))
        assert not ({x.upper() for x in direct} & hooks)
        destination = args.output / source.name
        data = staged.replace("\r\n", "\n").encode("utf-8")
        if not destination.exists() or destination.read_bytes() != data:
            destination.write_bytes(data)
        files += 1
    report = {"files": files, "direct_calls_added": changed_calls, "direct_calls_existing": existing_direct_calls,
              "direct_calls_restored_for_hooks": restored_calls, "hooked_calls_preserved": kept_calls + restored_calls,
              "protected_addresses": sorted(hooks), "generated_inputs_modified": False}
    (args.output / "direct-calls-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Direct calls added: {changed_calls}; already direct: {existing_direct_calls}; protected alias calls: {kept_calls}; staged files: {files}")


if __name__ == "__main__":
    main()
