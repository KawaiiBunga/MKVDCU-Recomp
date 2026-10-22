"""Deploy a verified Switch package with remote backups and hash verification."""
import argparse
from datetime import datetime, timezone
import ftplib
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--host", required=True)
parser.add_argument("--port", type=int, default=5000)
parser.add_argument("--remote", default="/sdmc:/switch/mkvsdcu")
parser.add_argument("--dist", type=Path, required=True)
parser.add_argument("--receipt", type=Path, required=True)
parser.add_argument("--files", nargs="+", default=["mkvsdcu.nro", "mkvsdcu.toml"],
                    help="Package files to deploy; use mkvsdcu.nro for a binary-only update")
args = parser.parse_args()
if any(Path(name).name != name or '/' in name or '\\' in name for name in args.files):
    raise SystemExit("Deployment filenames must be plain basenames")
verification = json.loads((args.dist / "switch-build-verification.json").read_text())
nro = args.dist / "mkvsdcu.nro"
if hashlib.sha256(nro.read_bytes()).hexdigest() != verification["sha256"]:
    raise SystemExit("NRO does not match verified build")
shader_report = args.dist / 'shader-verification.json'
shader_files = json.loads(shader_report.read_text())['files'] if shader_report.exists() else {}
file_hashes = {}
for name in dict.fromkeys(args.files):
    path = args.dist / name
    fingerprint = hashlib.sha256(path.read_bytes()).hexdigest()
    if name in shader_files and (fingerprint != shader_files[name]['sha256'] or
                                path.stat().st_size != shader_files[name]['bytes']):
        raise SystemExit('Shader file does not match verified package: '+name)
    file_hashes[name] = fingerprint
stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
ftp = ftplib.FTP()
ftp.connect(args.host, args.port, timeout=30)
ftp.login()
ftp.cwd(args.remote)
existing = set(ftp.nlst())
existing = {name.rsplit("/", 1)[-1] for name in existing}
installed = []
backups = {}
staged = {}
try:
    for name in dict.fromkeys(args.files):
        path = args.dist / name
        temporary = name + ".upload-" + stamp
        staged[name] = temporary
        total = 0
        last_report = 0
        def progress(block):
            global total, last_report
            total += len(block)
            if total - last_report >= 10 * 1024 * 1024:
                print(f"Uploading {name}: {total / path.stat().st_size:.0%}", flush=True)
                last_report = total
        with path.open("rb") as file:
            ftp.storbinary("STOR " + temporary, file, blocksize=262144, callback=progress)
        actual = hashlib.sha256()
        ftp.retrbinary("RETR " + temporary, actual.update, blocksize=262144)
        expected = file_hashes[name]
        if actual.hexdigest() != expected:
            raise RuntimeError("Uploaded hash mismatch: " + name)
        print("Verified uploaded " + name, flush=True)
    for name, temporary in staged.items():
        if name in existing:
            backup = name + ".before-update-" + stamp
            ftp.rename(name, backup)
            backups[name] = backup
        ftp.rename(temporary, name)
        installed.append(name)
    receipt = {"host": args.host, "port": args.port, "remote": args.remote,
               "installed": installed, "backups": backups, "sha256": verification["sha256"],
               "file_sha256": file_hashes,
               "deployed_utc": stamp}
    args.receipt.write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2), flush=True)
except Exception:
    # Roll back installed files and retain failed uploads for inspection.
    for name in reversed(list(staged)):
        try:
            if name in installed:
                ftp.rename(name, name + ".failed-update-" + stamp)
            if name in backups:
                ftp.rename(backups[name], name)
        except Exception as restore_error:
            print("Restore requires attention: " + name + ": " + str(restore_error), flush=True)
    raise
finally:
    ftp.close()
