"""T解压 (TUnpack) Swiss Industrial Icon Builder.

Generates BOTH:
1. 3D Shadowed Edition (t-unpack.png / t-unpack.ico / t-unpack-3d.*)
   Clean alpha-matted cutout from the studio 3D render with subtle grounded shadow.
2. Shadowless Flat Edition (t-unpack-flat.svg / t-unpack.svg / t-unpack-flat.png / t-unpack-flat.ico)
   Pure flat geometric Swiss industrial vector/raster icon with zero shadows.
"""
from collections import deque
from pathlib import Path
import shutil
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SOURCE_3D = ROOT / "t-unpack-3d-source.jpg"


def extract_clean_3d_rgba(src_path: Path) -> Image.Image:
    """Apply an exact mathematical superellipse/squircle mask to the centered 650x650 studio render.
    Avoids color-threshold nibbles on bright studio rim highlights while keeping zero white fringe."""
    rgb = Image.open(src_path).convert("RGB")
    # Exact measured bounding box of the 1024x1024 studio render is [188..837] (650x650).
    # Inset by 2px on each side to eliminate any JPEG white halo around the perimeter.
    crop = rgb.crop((190, 190, 836, 836))
    cw, ch = crop.size  # 646x646

    # Render an oversampled 4x superellipse (squircle, n=4.35) alpha mask matching the exact corner curve.
    s = 2
    mw, mh = cw * s, ch * s
    mask = Image.new("L", (mw, mh), 0)
    mpx = mask.load()
    cx = (mw - 1) / 2.0
    cy = (mh - 1) / 2.0
    a = mw / 2.0 - 0.5
    n = 4.35
    inv_a = 1.0 / a
    for y in range(mh):
        yn = abs(y - cy) * inv_a
        yp = yn ** n
        for x in range(mw):
            xn = abs(x - cx) * inv_a
            val = (xn ** n) + yp
            if val <= 0.985:
                mpx[x, y] = 255
            elif val < 1.015:
                mpx[x, y] = round(255 * (1.015 - val) / 0.030)
    mask = mask.resize((cw, ch), Image.Resampling.LANCZOS)

    rgba = crop.convert("RGBA")
    rgba.putalpha(mask)
    return rgba


def render_3d(cropped_3d: Image.Image, size: int) -> Image.Image:
    """Render optically sized 3D shadowed icon onto a square transparent canvas."""
    scale = 4 if size >= 64 else 8
    side = size * scale
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))

    # Give slightly more breathing room at large sizes for soft drop shadow; fill tightly at <=24px
    margin_ratio = 0.085 if size >= 32 else 0.04
    body_side = round(side * (1.0 - 2 * margin_ratio))
    body = cropped_3d.resize((body_side, body_side), Image.Resampling.LANCZOS)
    x = (side - body_side) // 2
    y = round(side * (0.068 if size >= 32 else 0.04))

    # Soft grounded ambient + contact shadow for the 3D shadowed version
    if size >= 20:
        alpha_ch = body.getchannel("A")
        sh_mask = Image.new("L", (side, side), 0)
        dy = max(1, round(side * 0.028))
        sh_mask.paste(alpha_ch, (x, min(side - body_side, y + dy)))
        sh_mask = sh_mask.filter(ImageFilter.GaussianBlur(max(1.0, side * 0.032)))
        sh_mask = sh_mask.point(lambda p: round(p * 0.34))
        shadow = Image.new("RGBA", (side, side), (18, 20, 24, 0))
        shadow.putalpha(sh_mask)
        canvas.alpha_composite(shadow)

    canvas.alpha_composite(body, (x, y))
    return canvas.resize((size, size), Image.Resampling.LANCZOS)


def render_flat(size: int) -> Image.Image:
    """Render crisp, shadowless flat Swiss Industrial icon matching 2A geometry."""
    scale = 4 if size >= 128 else 12
    side = size * scale
    k = side / 256.0
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)

    def box(r):
        return tuple(round(v * k) for v in r)

    def pt(x, y):
        return (round(x * k), round(y * k))

    # 1. Warm matte ceramic off-white squircle (pure flat, zero shadow)
    pad = 22 if size >= 32 else (14 if size >= 24 else 8)
    rad = 52 if size >= 24 else 46
    d.rounded_rectangle(
        box((pad, pad, 256 - pad, 256 - pad)),
        radius=round(rad * k),
        fill=(236, 234, 226, 255),
        outline=(206, 203, 193, 255),
        width=max(1, round(2.6 * k)),
    )

    # 2. Upper-right Braun micro-indicator dot (dainty hardware proportion, visible at >=24px)
    if size >= 24:
        d.ellipse(box((192, 48, 208, 64)), fill=(212, 209, 199, 255))
        d.ellipse(box((195, 51, 205, 61)), fill=(108, 126, 124, 255))

    # 3. Dark charcoal-graphite precision slot
    slot_rect = (48, 80, 208, 124) if size >= 24 else (38, 72, 218, 122)
    d.rounded_rectangle(box(slot_rect), radius=round(16 * k), fill=(64, 66, 69, 255))
    # Inner dark slit line
    slit_rect = (60, 96, 196, 108) if size >= 24 else (50, 92, 206, 104)
    d.rounded_rectangle(box(slit_rect), radius=round(6 * k), fill=(35, 37, 39, 255))

    # 4. Bold flat international-orange "T" pull-tab with subtle faceted bottom chamfer
    orange = (238, 101, 49, 255)
    groove = (194, 70, 24, 255)
    if size >= 24:
        # Crossbar
        d.rounded_rectangle(box((72, 98, 184, 138)), radius=round(10 * k), fill=orange)
        # Vertical stem with faceted downward chamfer at bottom
        d.polygon(
            [
                pt(108, 132),
                pt(148, 132),
                pt(148, 196),
                pt(138, 206),
                pt(118, 206),
                pt(108, 196),
            ],
            fill=orange,
        )
        # Subtle inner junction fill for clean rounded T neck
        d.rectangle(box((104, 130, 152, 142)), fill=orange)
        # Micro-knurled top grip ticks at >=32px
        if size >= 32:
            for x in range(82, 176, 8):
                d.rounded_rectangle(box((x, 100, x + 4, 109)), radius=round(1.5 * k), fill=groove)
    else:
        # Extra-bold optical silhouette at 16px-20px
        d.rounded_rectangle(box((58, 92, 198, 140)), radius=round(10 * k), fill=orange)
        d.rounded_rectangle(box((104, 128, 152, 214)), radius=round(8 * k), fill=orange)

    return canvas.resize((size, size), Image.Resampling.LANCZOS)


FLAT_SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="T解压：瑞士工业极简平面图标">
  <rect x="22" y="22" width="212" height="212" rx="52" fill="#eceae2" stroke="#cecbc1" stroke-width="2.6"/>
  <circle cx="200" cy="56" r="8" fill="#d4d1c7"/>
  <circle cx="200" cy="56" r="5" fill="#6c7e7c"/>
  <rect x="48" y="80" width="160" height="44" rx="16" fill="#404245"/>
  <rect x="60" y="96" width="136" height="12" rx="6" fill="#232527"/>
  <path d="M82 98h92a10 10 0 0 1 10 10v20a10 10 0 0 1-10 10h-26v58l-10 10h-20l-10-10v-58H82a10 10 0 0 1-10-10v-20a10 10 0 0 1 10-10z" fill="#ee6531"/>
  <g fill="#c24618">
    <rect x="82" y="100" width="4" height="9" rx="1.5"/>
    <rect x="90" y="100" width="4" height="9" rx="1.5"/>
    <rect x="98" y="100" width="4" height="9" rx="1.5"/>
    <rect x="106" y="100" width="4" height="9" rx="1.5"/>
    <rect x="114" y="100" width="4" height="9" rx="1.5"/>
    <rect x="122" y="100" width="4" height="9" rx="1.5"/>
    <rect x="130" y="100" width="4" height="9" rx="1.5"/>
    <rect x="138" y="100" width="4" height="9" rx="1.5"/>
    <rect x="146" y="100" width="4" height="9" rx="1.5"/>
    <rect x="154" y="100" width="4" height="9" rx="1.5"/>
    <rect x="162" y="100" width="4" height="9" rx="1.5"/>
    <rect x="170" y="100" width="4" height="9" rx="1.5"/>
  </g>
</svg>
"""


def save_preview(icons: dict, path: Path):
    sheet = Image.new("RGB", (760, 420), "#eff3f4")
    draw = ImageDraw.Draw(sheet)
    for i, size in enumerate(SIZES):
        shown = icons[size] if size <= 64 else icons[size].resize((64, 64), Image.Resampling.LANCZOS)
        x = 22 + i * 82
        sheet.paste(shown, (x + (64 - shown.width) // 2, 24 + (64 - shown.height) // 2), shown)
        draw.text((x + 20, 102), str(size), fill="#233f46")
    sheet.paste(icons[256], (55, 145), icons[256])
    dark = Image.new("RGB", (270, 270), "#141820")
    dark.paste(icons[256], (7, 7), icons[256])
    sheet.paste(dark, (380, 135))
    sheet.save(path, optimize=True)


def save_both_preview(icons_3d: dict, icons_flat: dict, path: Path):
    W, H = 840, 680
    sheet = Image.new("RGB", (W, H), "#f3f5f8")
    draw = ImageDraw.Draw(sheet)
    draw.rectangle((0, 350, W, H), fill="#131720")

    cols = [
        ("3D Shadowed (t-unpack.ico)", icons_3d, 210),
        ("Flat Shadowless (t-unpack-flat.ico)", icons_flat, 630),
    ]
    for label, icons, cx in cols:
        draw.text((cx - 115, 16), label, fill="#0f172a")
        sheet.paste(icons[256], (cx - 128, 40), icons[256])
        sx = cx - 80
        for sz in (64, 32, 20, 16):
            im = icons[sz]
            sheet.paste(im, (sx, 318 - sz // 2), im)
            sx += sz + 14
        draw.text((cx - 115, 368), label + " (Dark)", fill="#e2e8f0")
        sheet.paste(icons[256], (cx - 128, 396), icons[256])
    sheet.save(path, optimize=True)


def save_ico(icons: dict, path: Path):
    icons[256].save(
        path,
        format="ICO",
        sizes=[(n, n) for n in SIZES],
        append_images=[icons[n] for n in SIZES if n != 256],
    )


def main():
    cropped_3d = extract_clean_3d_rgba(SOURCE_3D)
    icons_3d = {n: render_3d(cropped_3d, n) for n in SIZES}
    icons_flat = {n: render_flat(n) for n in SIZES}

    # Main app icon = 3D Shadowed Edition
    icons_3d[256].save(ROOT / "t-unpack.png", optimize=True)
    save_ico(icons_3d, ROOT / "t-unpack.ico")
    icons_3d[256].save(ROOT / "t-unpack-3d.png", optimize=True)
    save_ico(icons_3d, ROOT / "t-unpack-3d.ico")
    save_preview(icons_3d, ROOT / "icon-preview.png")

    # Flat Shadowless Edition (vector SVG + 256px PNG + 9-size ICO)
    (ROOT / "t-unpack-flat.svg").write_text(FLAT_SVG, encoding="utf-8")
    (ROOT / "t-unpack.svg").write_text(FLAT_SVG, encoding="utf-8")
    icons_flat[256].save(ROOT / "t-unpack-flat.png", optimize=True)
    save_ico(icons_flat, ROOT / "t-unpack-flat.ico")
    save_preview(icons_flat, ROOT / "icon-preview-flat.png")

    save_both_preview(icons_3d, icons_flat, ROOT / "icon-preview-both.png")
    print("Generated 3D shadowed and shadowless flat icon suites (all 9 native ICO sizes).")


if __name__ == "__main__":
    main()
