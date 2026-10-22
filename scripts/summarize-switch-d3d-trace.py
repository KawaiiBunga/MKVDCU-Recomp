"""Validate a bounded PC original-renderer trace; report observed calls and object pairs.

Object addresses are not shader hashes. This report does not establish shader
coverage or rendering parity. No guest memory or rendering state is modified.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--trace", type=Path, required=True)
    p.add_argument("--map", type=Path, default=Path("targets/mkvsdcu-nx/config/deko3d-d3d-map.json"))
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--containers-manifest", type=Path,
                   help="Optional static shader extraction manifest for runtime hash comparison")
    args = p.parse_args()
    mapping = json.loads(args.map.read_text())
    names = {row["address"][2:].upper(): row["name"] for row in mapping["functions"]}
    counts, threads, pairs = Counter(), Counter(), Counter()
    invalid = without_device = missing_identity = 0
    identities = {}
    seen_identities = set()
    with args.trace.open(encoding="utf-8") as source:
        header = json.loads(next(source))
        if header.get("schema") != "mkvsdcu-d3d-trace-v1":
            p.error("Unsupported trace schema")
        if header.get("expected_xex_sha256") != mapping["xex_sha256"]:
            p.error("Trace and static map target different executables")
        events = 0
        for index, line in enumerate(source):
            row = json.loads(line)
            if row["event"] != index or row["function"] not in names:
                p.error("Nonsequential or unknown trace event")
            if len(row["r3_r10"]) != 8 or any(not re.fullmatch(r"[0-9A-F]{8}", r) for r in row["r3_r10"]):
                p.error("Invalid register capture")
            counts[row["function"]] += 1
            threads[row["thread"]] += 1
            if row["function"] in {"827EB600", "827EB7F8"}:
                stage = "vs" if row["function"] == "827EB7F8" else "ps"
                key = (row["r3_r10"][0], stage)
                identity = row.get("shader_identity")
                identities.pop(key, None)
                if identity is None:
                    missing_identity += 1
                elif identity["stage"] != stage or not re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]):
                    p.error("Invalid shader identity")
                else:
                    identities[key] = identity["sha256"]
                    seen_identities.add((stage, identity["sha256"]))
            device = row["device"]
            if device is None:
                without_device += 1
                if row["function"] not in {"827E1D68", "827EB600", "827EB7F8"}:
                    invalid += 1
            elif row["function"] in {"827F1940", "827F1E30", "827F23F0", "827F27E0", "827F30B0"}:
                vs, ps = device["vertex_shader"], device["pixel_shader"]
                pairs[(vs, ps, identities.get((vs, "vs")), identities.get((ps, "ps")))] += 1
            events += 1
    if events > header["event_limit"]:
        p.error("Trace exceeds declared event limit")
    report = dict(events=events, reached_event_limit=events == header["event_limit"],
                  runtime_verified_executable_hash=False, original_renderer=True,
                  shader_identity="registration container SHA-256 when readable; guest object addresses otherwise",
                  failed_shader_identity_captures=missing_identity,
                  events_without_device_snapshot=without_device, failed_device_snapshots=invalid,
                  functions=[dict(address="0x" + address, name=names[address], calls=count)
                             for address, count in sorted(counts.items())],
                  threads=dict(threads),
                  registered_shader_hashes=[dict(stage=stage, sha256=sha) for stage, sha in sorted(seen_identities)],
                  shader_object_pairs=[dict(vertex_object=vs, pixel_object=ps,
                                            vertex_sha256=vs_sha, pixel_sha256=ps_sha, draws=count)
                                       for (vs, ps, vs_sha, ps_sha), count in pairs.most_common()])
    if args.containers_manifest:
        static = {row["sha256"] for row in json.loads(args.containers_manifest.read_text())}
        report["runtime_hashes_absent_from_static_census"] = sorted(sha for _, sha in seen_identities if sha not in static)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{events} events, {len(counts)} functions, {len(pairs)} shader object pairs")


if __name__ == "__main__":
    main()
