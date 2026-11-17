"""Refresh full upstream license notices for the pinned binary; no user data."""
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
ZS = "07f03a46ef7642bf9ace70070d8874d2b92b4e70"
GHC = "983650f374699e3979f9cdefe13ddff60bd4ac68"


def main():
    sections = ["""T解压 0.13.0 — Third-party notices and source locations

The application is MIT licensed; see LICENSE.txt. Third-party licenses remain
in effect for their respective components.

bit7z v4.0.10, Riccardo Ostani and contributors — MPL-2.0.
Full license: bit7z-LICENSE.txt
Corresponding Source:
https://github.com/rikyoz/bit7z/archive/0f03717920442abcdba80183534931c867feb9c0.zip
The local modifications to include/bit7z/bit7zlibrary.hpp and
src/internal/opencallback.cpp are available under MPL-2.0:
https://github.com/thomasthzh/TUnpack/blob/v0.13.0/patches/bit7z-volumes.patch
Apply this patch to the specified source. No other bit7z modifications are used.

7-Zip ZS 26.05, Igor Pavlov, Tino Reichardt and contributors.
DLL distributed unmodified from:
https://github.com/mcmilk/7-Zip-zstd/releases/tag/v26.05-v1.5.7-R1
Corresponding DLL source:
https://github.com/mcmilk/7-Zip-zstd/archive/07f03a46ef7642bf9ace70070d8874d2b92b4e70.zip
The separate DarkMode GUI submodule is not linked into this DLL.
See 7-Zip-License.txt and the complete upstream license notices below.

ghc::filesystem, Steffen Schümann — MIT, full license below.
Corresponding Source:
https://github.com/gulrak/filesystem/tree/983650f374699e3979f9cdefe13ddff60bd4ac68
"""]
    paths = ["COPYING", "DOC/copying.txt", "C/brotli/LICENSE", "C/fast-lzma2/LICENSE", "C/lizard/LICENSE", "C/lz4/LICENSE", "C/lz5/LICENSE", "C/zstd/LICENSE", "C/zstdmt/LICENSE"]
    sources = [(f"7-Zip ZS: {path}", f"https://raw.githubusercontent.com/mcmilk/7-Zip-zstd/{ZS}/{path}") for path in paths]
    sources.append(("ghc::filesystem", f"https://raw.githubusercontent.com/gulrak/filesystem/{GHC}/LICENSE"))
    for name, url in sources:
        content = urllib.request.urlopen(url, timeout=60).read().decode("utf-8-sig")
        sections.append("\n" + "=" * 72 + "\n" + name + "\n" + url + "\n" + "=" * 72 + "\n" + content)
        print("Retrieved upstream notice:", name, flush=True)
    (ROOT / "THIRD-PARTY-NOTICES.txt").write_text("\n".join(line.rstrip() for line in "\n".join(sections).splitlines()) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
