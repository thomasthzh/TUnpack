"""Fetch the pinned sources and engine used by TUnpack. Downloads are retained.

python scripts/fetch_deps.py --seven-zip "C:/Program Files/7-Zip/7z.exe"
No installer is executed, no system settings are changed, no files are deleted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BIT7Z_COMMIT = "0f03717920442abcdba80183534931c867feb9c0"
ENGINE_VERSION = "26.05"
ENGINE_TAG = "v26.05-v1.5.7-R1"
ENGINE_URL = f"https://github.com/mcmilk/7-Zip-zstd/releases/download/{ENGINE_TAG}/7z26.05-zstd-x64.exe"
ENGINE_SHA256 = "430e398037520a52a35dedf45472eb3ea62073b7bf53bfdd37d2b663f50f5530"


def run(*args, cwd=None, capture=False, check=True):
    return subprocess.run([str(arg) for arg in args], cwd=cwd, check=check, capture_output=capture)


def download(url, target):
    if not target.exists():
        print("Download:", url, flush=True)
        with urllib.request.urlopen(url, timeout=120) as source, target.open("wb") as output:
            shutil.copyfileobj(source, output)


def bit7z(tools):
    target = tools / "bit7z"
    archive = tools / f"bit7z-{BIT7Z_COMMIT}.zip"
    marker = target / ".tunpack-source"
    if not target.exists():
        download(f"https://github.com/rikyoz/bit7z/archive/{BIT7Z_COMMIT}.zip", archive)
        with zipfile.ZipFile(archive) as source:
            prefix = f"bit7z-{BIT7Z_COMMIT}/"
            for member in source.infolist():
                if member.is_dir():
                    continue
                if not member.filename.startswith(prefix):
                    raise RuntimeError("Unexpected bit7z source archive")
                relative = Path(member.filename[len(prefix):])
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError("Invalid source path")
                output = target / relative
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(source.read(member))
        marker.write_text(BIT7Z_COMMIT, encoding="ascii")
    else:
        revision = marker.read_text(encoding="ascii").strip() if marker.exists() else run("git", "rev-parse", "HEAD", cwd=target, capture=True).stdout.decode().strip()
        if revision != BIT7Z_COMMIT:
            raise RuntimeError("Existing bit7z revision differs; use a fresh checkout or a separate --tools-dir")
    patch = ROOT / "patches/bit7z-volumes.patch"
    # A ZIP source folder may sit inside the TUnpack Git checkout. Stop Git's
    # upward discovery so apply cannot silently skip paths as outside its prefix.
    patch_env = {**os.environ, "GIT_CEILING_DIRECTORIES": str(target.resolve().parent)}
    patch_bytes = patch.read_bytes().replace(b"\r\n", b"\n")
    def apply(*options, check=True):
        return subprocess.run(["git", "apply", "--ignore-whitespace", *options, "-"], input=patch_bytes, cwd=target, env=patch_env, capture_output=True, check=check)
    applied = apply("--reverse", "--check", check=False).returncode == 0
    if not applied:
        apply("--check")
        apply()
    if b"setVolumeResolver" not in (target / "include/bit7z/bit7zlibrary.hpp").read_bytes():
        raise RuntimeError("Volume patch was not applied to bit7z")
    print("bit7z v4.0.10 + volume patch ready", flush=True)


def engine(tools, explicit):
    candidates = [explicit, shutil.which("7z"), tools / "7z.exe", Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "7-Zip/7z.exe"]
    seven = next((Path(item) for item in candidates if item and Path(item).is_file()), None)
    if not seven:
        raise RuntimeError("Install 7-Zip or pass --seven-zip with the path to 7z.exe; the SFX installer is never run")
    archive = tools / f"7z26.05-zstd-x64.exe"
    download(ENGINE_URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ENGINE_SHA256:
        raise RuntimeError("7-Zip ZS archive differs from the official release checksum; retained for inspection")
    target = tools / f"7zip-zs-{ENGINE_VERSION}"
    target.mkdir(exist_ok=True)
    run(seven, "x", archive, f"-o{target}", "7z.dll", "7z.exe", "License.txt", "-y")
    if not all((target / name).is_file() for name in ["7z.dll", "7z.exe", "License.txt"]):
        raise RuntimeError("Expected engine files were not extracted")
    print("7-Zip ZS 26.05 ready; installer retained", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seven-zip", type=Path)
    parser.add_argument("--tools-dir", type=Path, default=ROOT / "tools")
    parser.add_argument("--skip-engine", action="store_true")
    parser.add_argument("--skip-bit7z", action="store_true")
    args = parser.parse_args()
    tools = args.tools_dir.resolve()
    tools.mkdir(parents=True, exist_ok=True)
    if not args.skip_bit7z:
        bit7z(tools)
    if not args.skip_engine:
        engine(tools, args.seven_zip)
    (tools / "deps.json").write_text(json.dumps({"bit7z": BIT7Z_COMMIT, "bit7z_patch": "patches/bit7z-volumes.patch", "engine": ENGINE_TAG, "engine_sha256": ENGINE_SHA256}, indent=2), encoding="utf8")


if __name__ == "__main__":
    main()
