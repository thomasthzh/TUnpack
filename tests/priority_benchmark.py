"""Measure current ZIP priority and bulk-tail paths with isolated public fixtures."""
import json
import re
from pathlib import Path
import shutil
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/bin/unpack_cli.exe"
RUN = ROOT / "test-data" / ("priority-benchmark-" + time.strftime("%Y%m%d-%H%M%S"))
RUN.mkdir(parents=True)
payload = b"public priority benchmark\n" * 512
data = RUN / "payload.bin"
data.write_bytes(payload)
secret = RUN / "public-key.fixture"
secret.write_text("public-benchmark-key\n", encoding="utf8")
candidates = RUN / "public-candidates.fixture"
candidates.write_text("\n".join([f"public-wrong-{i:04d}" for i in range(512)] + ["public-benchmark-key"]) + "\n", encoding="utf8")


def call(command, book, *args):
    start = time.perf_counter()
    process = subprocess.run([str(CLI), command, *map(str, args), "--book", str(book), "--keep"], capture_output=True, timeout=60)
    wall_ms = (time.perf_counter() - start) * 1000
    result = json.loads(process.stdout)
    assert process.returncode == 0 and result["ok"], result
    progress = [json.loads(line) for line in process.stderr.splitlines()]
    return result, wall_ms, progress


master = RUN / "master.dpapi"
archive = Path(call("createzip", master, RUN / "sample.zip", data, "--password-file", secret)[0]["output"])
call("import-passwords", master, candidates)
samples = {"preferred": [], "bulk_tail": []}
for index in range(5):
    for mode in (["preferred", "bulk_tail"] if index % 2 == 0 else ["bulk_tail", "preferred"]):
        book = RUN / f"{mode}-{index}.dpapi"
        shutil.copyfile(master, book)
        args = [archive, "--out", RUN / "outputs", "--no-nested", "--progress"]
        if mode == "preferred":
            args.extend(["--password-file", secret])
        result, wall_ms, progress = call("extract", book, *args)
        assert result["password_checked"] and (Path(result["output"]) / "payload.bin").read_bytes() == payload
        totals = [event["total"] for event in progress if event["phase"] == "密码头部预检"]
        assert totals and (max(totals) <= 8 if mode == "preferred" else max(totals) > 500)
        samples[mode].append({"wall_ms": wall_ms, "engine_operation_ms": result["ms"], "max_header_candidates": max(totals), "password_checked": True, "payload_matches": True})
report = {
    "version": re.search(r"project\(TUnpack VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf8"))[1],
    "cli_bytes": CLI.stat().st_size,
    "cli_mtime_ns": CLI.stat().st_mtime_ns,
    "saved_candidates": 513,
    "payload_bytes": len(payload),
    "samples": samples,
    "median_wall_ms": {mode: statistics.median(item["wall_ms"] for item in rows) for mode, rows in samples.items()},
    "scope": "Same current native CLI and AES ZIP, small single entry. Preferred key versus correct key late in ranked saved candidates; five interleaved samples per mode, independent DPAPI book copied before each run so success history cannot improve later runs. Wall time includes process launch, book load, ranking, filtering, SDK validation, output writes and success persistence; operation ms excludes initial CLI setup. Warm OS cache, uncontrolled CPU scheduling, no prior-version comparison or general format speed claim. All output bytes checked; no GUI, real passwords, deletion or recycling.",
}
(ROOT / "docs/evidence/zip-priority-performance.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf8")
print(json.dumps({"median_wall_ms": report["median_wall_ms"], "header_totals": {mode: [item["max_header_candidates"] for item in rows] for mode, rows in samples.items()}}, indent=2))
