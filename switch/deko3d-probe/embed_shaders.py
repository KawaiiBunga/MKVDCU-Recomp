from pathlib import Path
import sys

root = Path(sys.argv[1])
lines = ["#pragma once", "#include <cstdint>"]
for stage in ("vert", "frag"):
    data = (root / f"probe.{stage}.dksh").read_bytes()
    if data[:4] != b"DKSH":
        raise SystemExit("uam did not produce DKSH: " + stage)
    lines.append(f"alignas(256) static const uint8_t shader_{stage}[] = {{")
    lines.extend(",".join(str(v) for v in data[i:i + 32]) + "," for i in range(0, len(data), 32))
    lines.append("};")
(root / "probe_shaders.h").write_text("\n".join(lines) + "\n")
