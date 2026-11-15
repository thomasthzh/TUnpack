"""Build the local portable artifact; does not upload or remove files."""
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"build/bin"
TARGET=ROOT/"dist/T解压"
TARGET.mkdir(parents=True,exist_ok=True)
NAMES=["T解压.exe","7z.dll","7-Zip-License.txt","bit7z-LICENSE.txt"]
for name in NAMES:shutil.copyfile(SOURCE/name,TARGET/name)
check=subprocess.run([str(SOURCE/"resource_checks.exe"),str(TARGET/"T解压.exe")],capture_output=True,check=True)
bundle=ROOT/"dist/T解压-portable.zip"
with zipfile.ZipFile(bundle,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
    for name in NAMES:archive.write(TARGET/name,"T解压/"+name)
with zipfile.ZipFile(bundle) as archive:
    assert archive.testzip() is None
    assert archive.namelist()==["T解压/"+name for name in NAMES]
report={"files":[{"name":name,"bytes":(TARGET/name).stat().st_size} for name in NAMES],
        "total_bytes":sum((TARGET/name).stat().st_size for name in NAMES),"zip_bytes":bundle.stat().st_size,
        "resource_check":check.stdout.decode("utf-8").strip(),"zip_crc_check":True,"published":False}
(ROOT/"docs/evidence/portable-tunpack.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(report,ensure_ascii=False,indent=2))
