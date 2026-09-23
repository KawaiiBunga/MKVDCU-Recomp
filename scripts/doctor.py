#!/usr/bin/env python3
"""Non-mutating PC or Switch prerequisite report for MKVDCU-Recomp."""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FAILURES = 0
EXPECTED_XEX_SHA256 = "2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7"

def result(level: str, label: str, detail: str) -> None:
    global FAILURES
    if level == "FAIL":
        FAILURES += 1
    print(f"[{level}] {label}: {detail}")

def command(name: str) -> None:
    path = shutil.which(name)
    if not path:
        result("FAIL", name, "not found on PATH")
        return
    try:
        out = subprocess.check_output([name, "--version"], text=True, stderr=subprocess.STDOUT).splitlines()[0]
    except (OSError, subprocess.CalledProcessError):
        out = path
    result("OK", name, out)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("pc", "switch"), default="pc")
    mode = parser.parse_args().mode
    print(f"Workspace: {ROOT}")
    print(f"Mode: {mode}")
    for name in ("git", "cmake", "ninja", "python"):
        command(name)
    if sys.version_info >= (3, 10):
        result("OK", "Python version", sys.version.split()[0])
    else:
        result("FAIL", "Python version", "3.10+ required")

    if mode == "switch":
        devkit = Path(os.environ.get("DEVKITPRO", r"C:\devkitPro"))
        if not devkit.exists() and Path(r"C:\devkitPro").exists():
            devkit = Path(r"C:\devkitPro")
            result("WARN", "DEVKITPRO", "MSYS-style value ignored; using C:\\devkitPro")
        elif devkit.exists():
            result("OK", "DEVKITPRO", str(devkit))
        else:
            result("FAIL", "DEVKITPRO", "devkitPro root was not found")

        checks = {
            "devkitA64 compiler": devkit / "devkitA64/bin/aarch64-none-elf-g++.exe",
            "libnx headers": devkit / "libnx/include/switch.h",
            "libnx linker spec": devkit / "libnx/switch.specs",
            "elf2nro": devkit / "tools/bin/elf2nro.exe",
            "nacptool": devkit / "tools/bin/nacptool.exe",
            "Switch portlibs": devkit / "portlibs/switch",
        }
        for label, path in checks.items():
            result("OK" if path.exists() else "FAIL", label, str(path) if path.exists() else "missing")

    sdk = ROOT / "references/rexglue-sdk"
    if sdk.exists() and (sdk / ".git").exists():
        try:
            sha = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "--short", "HEAD"], text=True).strip()
            result("OK", "ReXGlue reference", sha)
        except subprocess.CalledProcessError:
            result("WARN", "ReXGlue reference", "present but revision unavailable")
    else:
        result("WARN", "ReXGlue reference", "not cloned")

    clang = shutil.which("clang") or shutil.which("clang-cl")
    bundled_clang = Path(r"C:\\Program Files\\LLVM\\bin\\clang.exe")
    if clang:
        result("OK", "Host LLVM", clang)
    elif bundled_clang.exists():
        result("OK", "Host LLVM", f"{bundled_clang} (use scripts/enter-dev-env.ps1 to add it for this session)")
    else:
        result("WARN", "Host LLVM", "needed to build official host SDK; not on PATH")
    native_cmake = Path(r"C:\\Program Files\\CMake\\bin\\cmake.exe")
    result("OK" if native_cmake.exists() else "WARN", "Native CMake", str(native_cmake) if native_cmake.exists() else "MSYS CMake may mishandle workspace paths with spaces")
    if mode == "pc":
        game_xex = ROOT / "user-game-files/work/mkvsdcu/default.xex"
        host_project = ROOT / "targets/mkvsdcu/private/rexglue-host"
        for label, path in {
            "Staged retail XEX": game_xex,
            "Local function config": host_project / "config/mkvsdcu_functions.toml",
            "Installed ReXGlue CLI": sdk / "out/install/win-amd64/bin/rexglue.exe",
        }.items():
            result("OK" if path.is_file() else "FAIL", label, str(path) if path.is_file() else "missing; see docs/PC_BUILD.md")
        if game_xex.is_file():
            with game_xex.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest().upper()
            result("OK" if digest == EXPECTED_XEX_SHA256 else "FAIL", "XEX SHA-256", digest)
        game_exe = host_project / "out/build/win-amd64-release/mkvsdcu.exe"
        result("OK" if game_exe.is_file() else "WARN", "PC game executable", str(game_exe) if game_exe.is_file() else "not built yet")
    else:
        result("WARN", "Switch game runtime", "not implemented; this mode checks the toolchain only")
    return 1 if FAILURES else 0

if __name__ == "__main__":
    raise SystemExit(main())
