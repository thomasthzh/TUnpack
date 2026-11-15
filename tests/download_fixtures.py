"""Download pinned upstream archive test fixtures; does not execute or delete them."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import urllib.request

TARGET = Path(__file__).resolve().parent / "fixtures"
BASE = "https://raw.githubusercontent.com/rikyoz/bit7z-test-data/077e407b1c07b7443626b5902eeb4819388bf656/test_archives/extraction/"
FILES = ["single_file/clouds.jpg.rar4.rar", "encrypted/encrypted.rar4.rar", "encrypted/encrypted.rar5.rar", "header_encrypted/header_encrypted.rar5.rar"]
FILES += ["split/clouds.jpg.part%d.rar" % i for i in (1, 2, 3)]
FILES += ["split/clouds.jpg." + ext for ext in ("rar", "r00", "r01")]


def download(relative):
    target = TARGET / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        urllib.request.urlretrieve(BASE + relative, target)
    return str(target.relative_to(TARGET)), target.stat().st_size


if __name__ == "__main__":
    with ThreadPoolExecutor(max_workers=4) as pool:
        for result in pool.map(download, FILES):
            print(*result)
