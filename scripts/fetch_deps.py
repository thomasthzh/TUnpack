"""获取构建所需的第三方依赖。

不修改系统环境，也不写入仓库之外的路径：
只把依赖放进仓库内的 tools/ 目录（该目录已在 .gitignore 中）。

用法：
    python scripts/fetch_deps.py
    python scripts/fetch_deps.py --skip-engine   # 只取 bit7z 源码
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"

# 7-Zip ZS 官方包：x64 版本，含 7z.dll 与许可文件
ENGINE_VERSION = "26.05"
ENGINE_URL = f"https://www.7-zip.org/a/7z{ENGINE_VERSION}-zstd-x64.exe"

# bit7z 上游仓库
BIT7Z_REPO = "https://github.com/rikyoz/bit7z.git"
BIT7Z_COMMIT = "0f03717"


def run(*args, cwd=None):
    print("  $", " ".join(str(a) for a in args))
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def fetch_bit7z():
    target = TOOLS / "bit7z"
    if target.exists():
        print(f"bit7z 已存在，跳过：{target}")
        return
    target.mkdir(parents=True, exist_ok=True)
    run("git", "clone", "--depth", "1", BIT7Z_REPO, str(target))
    run("git", "-C", str(target), "fetch", "--depth", "1", "origin", BIT7Z_COMMIT)
    run("git", "-C", str(target), "checkout", BIT7Z_COMMIT)
    print("bit7z 就绪")


def fetch_engine():
    target = TOOLS / f"7zip-zs-{ENGINE_VERSION}"
    if target.exists():
        print(f"7-Zip ZS 已存在，跳过：{target}")
        return
    target.mkdir(parents=True, exist_ok=True)
    archive = target / "7z-installer.exe"
    print(f"下载 7-Zip ZS {ENGINE_VERSION} …")
    urllib.request.urlretrieve(ENGINE_URL, archive)
    # 官方安装包是自解压程序；用 7z 自身解出，避免运行安装流程
    seven_zip = TOOLS / "7z.exe"
    if not seven_zip.exists():
        raise SystemExit(
            "需要 7z.exe 才能展开官方包：请先安装 7-Zip 并把 7z.exe 放到 tools/ 下，"
            "或手动解出 7zip-zs-%s 目录。" % ENGINE_VERSION
        )
    run(seven_zip, "x", archive, f"-o{target}", "-y")
    archive.unlink(missing_ok=True)
    print("7-Zip ZS 就绪")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-engine", action="store_true", help="只获取 bit7z 源码")
    parser.add_argument("--skip-bit7z", action="store_true", help="只获取 7-Zip ZS 引擎")
    args = parser.parse_args()

    TOOLS.mkdir(parents=True, exist_ok=True)
    report = {"bit7z": BIT7Z_COMMIT, "engine": ENGINE_VERSION}
    if not args.skip_bit7z:
        fetch_bit7z()
    if not args.skip_engine:
        fetch_engine()
    (TOOLS / "deps.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
