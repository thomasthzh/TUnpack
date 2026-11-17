"""Static publishing checks only; does not launch or pretend to render a browser."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit, unquote
import re

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.refs = []
        self.elements = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            assert attrs["id"] not in self.ids, "Duplicate id: " + attrs["id"]
            self.ids.add(attrs["id"])
        self.elements.append((tag, attrs))
        if tag == "meta" and attrs.get("property") == "og:image":
            prefix = "https://thomasthzh.github.io/TUnpack/"
            assert attrs.get("content", "").startswith(prefix)
            self.refs.append(attrs["content"][len(prefix):])
        for name in ["href", "src", "poster", "data-src", "data-src-portrait", "data-poster-wide", "data-poster-portrait", "srcset"]:
            if attrs.get(name):
                self.refs.append(attrs[name])


def main():
    html = (SITE / "index.html").read_text(encoding="utf8")
    page = Page()
    page.feed(html)
    for ref in page.refs:
        parts = urlsplit(ref)
        if parts.scheme:
            assert parts.scheme == "https", ref
            assert parts.hostname in {"github.com", "thomasthzh.github.io"}, ref
        elif ref.startswith("#"):
            assert ref[1:] in page.ids, ref
        else:
            assert not ref.startswith("/"), "Root-relative URL breaks project Pages: " + ref
            assert (SITE / unquote(parts.path)).exists(), "Missing local resource: " + ref
    assert len([tag for tag, _ in page.elements if tag == "h1"]) == 1
    videos = [attrs for tag, attrs in page.elements if tag == "video"]
    assert len(videos) == 1 and "src" not in videos[0] and videos[0]["preload"] == "none"
    assert all(attr in videos[0] for attr in ["controls", "muted", "playsinline", "aria-describedby", "hidden", "data-src-portrait", "data-poster-portrait"])
    chapters = [attrs for tag, attrs in page.elements if tag == "button" and "data-time" in attrs]
    assert [float(item["data-time"]) for item in chapters] == [0, 7.5, 15]
    assert all("disabled" in item for item in chapters), "No-JS chapter buttons must not pretend to seek"
    assert "非实际录屏" in html and "尚未进行数字签名" in html and "不进行无边界" in html
    assert any("releases/download/v0.13.0/TUnpack-0.13.0-windows-x64.zip" in ref for ref in page.refs)
    css = (SITE / "styles.css").read_text(encoding="utf8")
    assert "max-width:700px" in css and "prefers-reduced-motion" in css and "focus-visible" in css
    assert not re.search(r"@import|https?://", css), "External font or CSS dependency"
    for shape in ["wide", "portrait"]:
        assert (SITE / ("assets/tunpack-20261117-" + shape + ".webp")).stat().st_size < 150_000
        assert (SITE / ("assets/tunpack-20261117-" + shape + ".mp4")).stat().st_size < 4_000_000
    assert videos[0]["data-src-portrait"] != videos[0]["data-src"]
    assert any(tag == "source" and attrs.get("media") == "(max-width: 700px)" and "portrait" in attrs.get("srcset", "") for tag, attrs in page.elements)
    assert any(ref.endswith("/v0.13.0/SHA256SUMS.txt") for ref in page.refs)
    assert "默认：每个任务及其嵌套包全部解压成功后" in html and "可在开始前关闭" in html and "当前 Windows 用户目录" in html
    assert any(tag == "input" and "readonly" in attrs and attrs.get("value", "").endswith("/v0.13.0/TUnpack-0.13.0-windows-x64.zip") for tag, attrs in page.elements)
    captions = (SITE / "assets/film.zh-CN.vtt").read_text(encoding="utf8")
    assert captions.startswith("WEBVTT") and "00:24.000" in captions
    print(f"Site static checks passed: {len(page.refs)} links/media paths, 3 chapter targets, accessibility/fallback and asset budgets. No browser run.")


if __name__ == "__main__":
    main()
