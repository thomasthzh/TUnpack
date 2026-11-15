"""Compare real end-to-end candidate verification with the same 7z engine CLI."""
import json
from pathlib import Path
import statistics
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
CLI=ROOT/"build/bin/unpack_cli.exe"
SEVEN=ROOT/"tools/7zip-zs-26.05/7z.exe"
RUN=ROOT/"test-data"/("benchmark-"+time.strftime("%Y%m%d-%H%M%S"))
RUN.mkdir(parents=True)
BOOK=RUN/"book.dpapi"
words=["wrong-%04d" % i for i in range(256)]+["benchmark-password"]
wordfile=RUN/"passwords.txt"
wordfile.write_text("\n".join(words)+"\n",encoding="utf8")
secret=RUN/"sample-password.txt"
secret.write_text(words[-1]+"\n",encoding="utf8")
data=RUN/"sample.txt"
data.write_bytes(b"performance fixture\n"*65536)

def call(command,*args):
    start=time.perf_counter()
    p=subprocess.run([str(CLI),command,*map(str,args),"--book",str(BOOK),"--keep"],capture_output=True,timeout=60)
    result=json.loads(p.stdout.decode())
    if not result["ok"]:raise RuntimeError(result)
    return (time.perf_counter()-start)*1000,result

_,created=call("createzip",RUN/"sample.zip",data,"--password-file",secret)
archive=Path(created["output"])
call("import-passwords",wordfile)
baseline_start=time.perf_counter()
for password in words:
    result=subprocess.run([str(SEVEN),"t",str(archive),"-p"+password,"-y"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=10)
    if result.returncode==0:break
baseline=(time.perf_counter()-baseline_start)*1000
native=[call("extract",archive,"--out",RUN/"outputs") for _ in range(5)]
assert all(result["password_checked"] for _,result in native)
samples=[elapsed for elapsed,_ in native]
median=statistics.median(samples)
report={"candidate_count":len(words),"cli_serial_wall_ms":baseline,"native_wall_ms_samples":samples,"native_median_ms":median,"ratio":baseline/median,"native_password_checked":True,"scope":"ZIP AES256; 256 wrong ASCII candidates then correct; same ZS26.05 engine; CLI full test vs native full extraction with CRC/MAC and output writes; end-to-end process time; synthetic warm-cache fixture; not a claim about all formats"}
(ROOT/"docs/evidence/password-benchmark.json").write_text(json.dumps(report,indent=2),encoding="utf8")
print(json.dumps(report,indent=2))
