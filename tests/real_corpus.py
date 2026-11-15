"""Explicit local-corpus acceptance: cover every top-level download, retain all sources."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
CLI=ROOT/"build/bin/unpack_cli.exe"
RUN=Path(r"D:\T解压测试\20261111-real")
REPORT=ROOT/"docs/evidence/real-corpus.json"
SOURCE=Path(r"D:\Download")
DAMAGED="社会运行的经济视角-S01E16_50100487.7z"

def stat(path):
    s=path.stat()
    return {"path":str(path),"size":s.st_size,"mtime_ns":s.st_mtime_ns}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode",choices=["inspect","extract","verify"])
    args=parser.parse_args()
    RUN.mkdir(parents=True,exist_ok=True)
    book=RUN/"passwords.dpapi"
    if not book.exists():shutil.copyfile(Path(os.environ["LOCALAPPDATA"])/"T解压/passwords.dpapi",book)
    state=json.loads(REPORT.read_text(encoding="utf-8")) if REPORT.exists() else {"run":str(RUN),"jobs":{}}
    binary=stat(CLI)
    inventory=json.loads((ROOT/"docs/evidence/download-inventory.json").read_text(encoding="utf-8"))["files"]
    expected={p["path"]:p for p in inventory if Path(p["path"]).parent==SOURCE and Path(p["path"]).name!="Thumbs.db"}
    before=[stat(Path(p)) for p in sorted(expected)]
    for p in before:
        assert p["size"]==expected[p["path"]]["size"] and p["mtime_ns"]==expected[p["path"]]["mtime_ns"],"Source changed since inventory"
    state["inputs_before"]=before
    state["binary"]=binary
    state["run"]=str(RUN)
    def save():
        REPORT.write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding="utf-8")
    if args.mode in ("inspect","verify"):
        command=[str(CLI),"scan",str(SOURCE),"--flat","--keep","--book",str(book),"--progress"]
        with (RUN/"scan-progress.jsonl").open("wb") as progress:
            proc=subprocess.run(command,stdout=subprocess.PIPE,stderr=progress)
        scan=json.loads(proc.stdout.decode("utf-8"))
        assert proc.returncode==0 and scan["ok"],scan
        covered={p for entry in scan["entries"] for p in entry["files"]}
        assert covered==set(expected),{"missed":sorted(set(expected)-covered),"unexpected":sorted(covered-set(expected))}
        previous={record["files"][0]:record for record in state.get("jobs",{}).values()}
        jobs={}
        for entry in scan["entries"]:
            name=Path(entry["files"][0]).name
            record={"files":entry["files"],"inspect":{"binary":binary,"result":entry}}
            old=previous.get(entry["files"][0],{})
            if "extract" in old:record["extract"]=old["extract"]
            jobs[name]=record
        state["jobs"]=jobs
        state["scan"]={"binary":binary,"result":scan}
        state["coverage"]={"download_files":len(expected),"recognized_files":len(covered),"archive_groups":len(jobs)}
        print("Covered",len(covered),"download files in",len(jobs),"groups",flush=True)
    if args.mode=="verify":
        seen=set();checks=[];failures=[]
        for index,(name,record) in enumerate(state["jobs"].items()):
            metadata=record["inspect"]["result"]
            if not metadata["ok"]:
                assert metadata["status"] in ("password_required","missing_volume"),metadata
                continue
            todo=[record["files"][0],*record.get("extract",{}).get("result",{}).get("sources",[])]
            for path in todo:
                if str(Path(path)).casefold() in seen:continue
                print("VERIFY",Path(path).name,flush=True)
                command=[str(CLI),"inspect",path,"--verify","--keep","--book",str(book),"--progress"]
                with (RUN/("verify-"+str(len(checks))+"-progress.jsonl")).open("wb") as progress:
                    proc=subprocess.run(command,stdout=subprocess.PIPE,stderr=progress)
                result=json.loads(proc.stdout.decode("utf-8"))
                expected=name==DAMAGED
                if (not expected and not result["ok"]) or (expected and (result["ok"] or result["status"]!="verification_failed")):failures.append(path)
                checks.append({"source":path,"known_failure":expected,"result":result})
                seen.add(str(Path(path)).casefold())
                seen.update(str(Path(p)).casefold() for p in result["sources"])
                state["final_integrity"]={"binary":binary,"checks":checks,"unexpected_failures":failures}
                save()
        state["inputs_after"]=[stat(Path(p["path"])) for p in before]
        assert state["inputs_after"]==before
        state["final_integrity"]["verified_archives"]=sum(c["result"]["ok"] for c in checks)
        save()
        print("Verified",state["final_integrity"]["verified_archives"],"archives; unexpected failures",len(failures),flush=True)
        return 1 if failures else 0
    if args.mode=="extract":
        assert state.get("scan",{}).get("binary")==binary,"Inspect current binary before extraction"
        failures=[]
        for index,(name,record) in enumerate(state["jobs"].items()):
            metadata=record["inspect"]["result"]
            if not metadata["ok"]:
                assert metadata["status"] in ("password_required","missing_volume"),metadata
                print("REQUIRES INPUT",name,metadata["status"],flush=True)
                continue
            if record.get("extract",{}).get("binary")==binary:
                print("RESUME",name,flush=True)
                continue
            assert shutil.disk_usage(RUN).free>metadata["bytes"]+2*1024**3,"Insufficient output space"
            command=[str(CLI),"extract",*record["files"],"--out",str(RUN/("final-"+str(index))),"--book",str(book),"--keep","--progress"]
            record["running"]={"mode":"extract","started":time.strftime("%Y-%m-%d %H:%M:%S")}
            save()
            print("START",name,flush=True)
            started=time.perf_counter()
            with (RUN/("final-"+str(index)+"-progress.jsonl")).open("wb") as progress:
                proc=subprocess.run(command,stdout=subprocess.PIPE,stderr=progress)
            result=json.loads(proc.stdout.decode("utf-8"))
            detail={"binary":binary,"wall_seconds":round(time.perf_counter()-started,3),"result":result}
            if result["ok"]:
                assert proc.returncode==0
                files=[p for p in Path(result["output"]).rglob("*") if p.is_file()]
                detail["output_files"]=len(files)
                detail["output_bytes"]=sum(p.stat().st_size for p in files)
                assert detail["output_bytes"]>=metadata["bytes"],"Output size mismatch"
                assert all(Path(p).is_file() for p in result["sources"]),"Source not retained"
            elif name==DAMAGED:
                assert result["status"]=="verification_failed" and result["password_checked"],result
                detail["known_failure"]="independent native/Python body CRC mismatch, source and partial output retained"
            else:failures.append(name)
            record["extract"]=detail
            record.pop("running",None)
            after=[stat(Path(p["path"])) for p in before]
            assert after==before,"Source size or modification time changed"
            state["inputs_after"]=after
            save()
            print("DONE",name,"ok="+str(result["ok"]),detail["wall_seconds"],"seconds",flush=True)
        state["unexpected_failures"]=failures
        good=[j["extract"] for j in state["jobs"].values() if j.get("extract",{}).get("binary")==binary and j["extract"]["result"]["ok"]]
        state["summary"]={"verified_groups":len(good),"archives":sum(j["result"]["archives"] for j in good),
                          "output_files":sum(j["output_files"] for j in good),"output_bytes":sum(j["output_bytes"] for j in good),
                          "source_files":len(before),"sources_unchanged":[stat(Path(p["path"])) for p in before]==before}
        save()
        print(json.dumps(state["summary"],ensure_ascii=False),flush=True)
        return 1 if failures else 0
    state["inputs_after"]=[stat(Path(p["path"])) for p in before]
    assert state["inputs_after"]==before
    save()
    return 0

if __name__=="__main__":sys.exit(main())
