"""Package only the native portable program and its licenses; no upload or removal."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-bin", type=Path, default=ROOT / "build/bin")
    args = parser.parse_args()
    source = args.source_bin.resolve()
    if not (source / "T解压.exe").exists() and (source / "Release/T解压.exe").exists():
        source = source / "Release"
    version = re.search(r"project\(TUnpack VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf8"))[1]
    target = ROOT / "dist/T解压"
    target.mkdir(parents=True, exist_ok=True)
    files = {"T解压.exe": source / "T解压.exe"}
    for name in ["7z.dll", "7-Zip-License.txt", "bit7z-LICENSE.txt"]:
        path = source / name
        files[name] = path if path.is_file() or source.name != "Release" else source.parent / name
    files.update({"LICENSE.txt": ROOT / "LICENSE", "README.txt": ROOT / "release-README.txt", "THIRD-PARTY-NOTICES.txt": ROOT / "THIRD-PARTY-NOTICES.txt"})
    for name, path in files.items():
        shutil.copyfile(path, target / name)
    check = subprocess.run([str(source / "resource_checks.exe"), str(target / "T解压.exe")], capture_output=True, check=True)
    bundle = ROOT / f"dist/TUnpack-{version}-windows-x64.zip"
    with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in files:
            archive.write(target / name, "T解压/" + name)
    with zipfile.ZipFile(bundle) as archive:
        assert archive.testzip() is None
        assert archive.namelist() == ["T解压/" + name for name in files]
    digest = hashlib.sha256(bundle.read_bytes()).hexdigest()
    checksum = ROOT / "dist/SHA256SUMS.txt"
    checksum.write_text(digest + "  " + bundle.name + "\n", encoding="ascii")
    report = {"version": version, "files": [{"name": name, "bytes": (target / name).stat().st_size} for name in files], "total_bytes": sum((target / name).stat().st_size for name in files), "zip_name": bundle.name, "zip_bytes": bundle.stat().st_size, "sha256": digest, "resource_check": check.stdout.decode("utf8").strip(), "zip_crc_check": True, "published": False}
    (ROOT / "docs/evidence/portable-tunpack.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
