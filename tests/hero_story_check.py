"""Exercise the film's synthetic example with the actual engine, retaining inputs.
No real password book, user archive, GUI, recycling or deletion is involved.
"""
import io
import json
from pathlib import Path
import random
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/bin/unpack_cli.exe"
SEVEN = ROOT / "tools/7zip-zs-26.05/7z.exe"


def main():
    folder = ROOT / "test-data" / ("hero-story-" + str(time.time_ns()))
    folder.mkdir(parents=True)
    payload = random.Random(20261117).randbytes(40000)
    inner = folder / "内层.zip"
    with zipfile.ZipFile(inner, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("照片.bin", payload)
        archive.writestr("说明.txt", "public generated hero fixture\n")
    subprocess.run([str(SEVEN), "a", "-t7z", "-mx0", "-v16k", "-mhe=on", "-ppublic-hero-key", str(folder / "项目.7z"), str(inner)], check=True, capture_output=True)
    volumes = sorted(folder.glob("项目.7z.*"))
    assert len(volumes) == 3
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_STORED) as archive:
        for index, part in enumerate(volumes):
            archive.writestr("附件.docx" if index == 2 else part.name, part.read_bytes())
    prefix = b"\x00\x00\x00\x18ftypisom\x00\x00\x02\x00isomiso2" + b"generated illustrative media prefix" * 8
    source = folder / "旅途.mp4"
    source.write_bytes(prefix + stream.getvalue())
    password = folder / "public-password.fixture"
    password.write_text("public-hero-key\n", encoding="utf8")
    book = folder / "isolated.dpapi"
    def call(command, *args):
        result = subprocess.run([str(CLI), command, *map(str,args), "--book", str(book), "--keep"], capture_output=True, timeout=60)
        data = json.loads(result.stdout)
        return result.returncode, data
    status, imported = call("import-passwords", password)
    assert status == 0 and imported["ok"]
    status, result = call("extract", source, "--out", folder / "outputs")
    assert status == 0 and result["ok"], result
    output = Path(result["output"])
    assert any(p.read_bytes() == payload for p in output.rglob("照片.bin"))
    assert result["archives"] >= 3
    assert source.read_bytes() == prefix + stream.getvalue()
    assert all(part.exists() for part in volumes)
    report = {"example": "generated MP4 prefix + ZIP containing encrypted 7z volumes (third renamed .docx) containing inner.zip", "engine_ok": True, "decoded_archives": result["archives"], "payload_matches": True, "source_retained": True, "saved_public_candidate_used": True, "recycle_tested": False, "scope": "One synthetic, uniquely resolvable three-part example. Does not imply all arbitrary renamed volumes or corrupt files can be recovered. Recycling is off; the film presents existing settings-controlled cleanup semantics, not an additional recycle test."}
    (ROOT / "docs/evidence/hero-story-check.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf8")
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__ == "__main__":
    main()
