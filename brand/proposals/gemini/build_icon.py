"""Gemini Proposal: Kinetic Prism T — Modern Fluent/macOS-caliber icon for T解压 (TUnpack)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def lerp_color(c1, c2, t):
    return tuple(round(a + (b - a) * t) for a, b in zip(c1, c2))


def linear_gradient(size, mask, top, bottom, diagonal=False):
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bounds = mask.getbbox()
    if not bounds:
        return layer
    x0, y0, x1, y1 = bounds
    w = max(1, x1 - x0 - 1)
    h = max(1, y1 - y0 - 1)
    if not diagonal:
        draw = ImageDraw.Draw(layer)
        for y in range(y0, y1):
            t = (y - y0) / h
            col = lerp_color(top, bottom, t) + (255,)
            draw.line((x0, y, x1, y), fill=col)
    else:
        px = layer.load()
        for y in range(y0, y1):
            ty = (y - y0) / h
            for x in range(x0, x1):
                tx = (x - x0) / w
                t = max(0.0, min(1.0, 0.55 * ty + 0.45 * tx))
                px[x, y] = lerp_color(top, bottom, t) + (255,)
    layer.putalpha(mask)
    return layer


def radial_glow(size, cx, cy, radius, color, max_alpha):
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = glow.load()
    r2 = radius * radius
    x0 = max(0, int(cx - radius))
    x1 = min(size, int(cx + radius + 1))
    y0 = max(0, int(cy - radius))
    y1 = min(size, int(cy + radius + 1))
    for y in range(y0, y1):
        dy = y - cy
        for x in range(x0, x1):
            dx = x - cx
            d2 = dx * dx + dy * dy
            if d2 < r2:
                f = (1.0 - (d2 / r2) ** 0.5) ** 2.1
                a = round(max_alpha * f)
                if a > 0:
                    px[x, y] = (color[0], color[1], color[2], a)
    return glow


def render(size):
    scale = 4 if size >= 128 else 8
    side = size * scale
    k = side / 256.0
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))

    def pt(x, y):
        return (round(x * k), round(y * k))

    def box(r):
        return tuple(round(v * k) for v in r)

    def add_shadow(mask, dx, dy, blur, alpha, col=(4, 10, 24)):
        sh = Image.new("L", (side, side), 0)
        sh.paste(mask, (round(dx * k), round(dy * k)))
        sh = sh.filter(ImageFilter.GaussianBlur(max(0.5, blur * k)))
        sh = sh.point(lambda p: round(p * alpha / 255))
        layer = Image.new("RGBA", (side, side), col + (0,))
        layer.putalpha(sh)
        canvas.alpha_composite(layer)

    # 1. Outer Obsidian Squircle Base
    base_rect = (18, 18, 238, 238)
    base_r = 54
    base_mask = Image.new("L", (side, side), 0)
    ImageDraw.Draw(base_mask).rounded_rectangle(box(base_rect), round(base_r * k), fill=255)
    add_shadow(base_mask, 0, 7, 7, 130, (2, 6, 18))

    base_grad = linear_gradient(side, base_mask, (20, 34, 62), (9, 15, 30), diagonal=True)
    canvas.alpha_composite(base_grad)

    # Subtle internal aurora glow clipped to squircle
    glow_layer = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    glow_layer.alpha_composite(radial_glow(side, 190 * k, 58 * k, 118 * k, (34, 211, 238), 105))
    glow_layer.alpha_composite(radial_glow(side, 78 * k, 206 * k, 112 * k, (16, 185, 129), 75))
    glow_alpha = Image.composite(glow_layer.getchannel("A"), Image.new("L", (side, side), 0), base_mask)
    glow_layer.putalpha(glow_alpha)
    canvas.alpha_composite(glow_layer)

    # 2. Layered Floating Parallel Archive Plates (diagonal kinetic fan-out)
    if size >= 24:
        # Back-right emerald-cyan translucent plate
        p1_mask = Image.new("L", (side, side), 0)
        ImageDraw.Draw(p1_mask).rounded_rectangle(box((92, 36, 214, 158)), round(24 * k), fill=255)
        p1 = linear_gradient(side, p1_mask, (110, 231, 183), (14, 165, 233), diagonal=True)
        p1.putalpha(p1_mask.point(lambda a: round(a * 0.42)))
        canvas.alpha_composite(p1)

        # Mid glass plate behind hero T
        p2_mask = Image.new("L", (side, side), 0)
        ImageDraw.Draw(p2_mask).rounded_rectangle(box((44, 52, 198, 204)), round(28 * k), fill=255)
        add_shadow(p2_mask, 0, 6, 6, 110, (2, 10, 26))
        p2 = linear_gradient(side, p2_mask, (28, 78, 118), (13, 38, 68), diagonal=True)
        p2.putalpha(p2_mask.point(lambda a: round(a * 0.88)))
        canvas.alpha_composite(p2)

        # Crisp luminous top-edge highlight on mid glass plate
        edge2 = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        ImageDraw.Draw(edge2).rounded_rectangle(
            box((44, 52, 198, 204)), round(28 * k),
            outline=(125, 249, 255, 110), width=max(1, round(1.8 * k))
        )
        canvas.alpha_composite(edge2)

    # 3. Sculpted Hero "T" + Unpack Chevron
    t_mask = Image.new("L", (side, side), 0)
    td = ImageDraw.Draw(t_mask)
    # Top crossbar of T (sleek rounded architectural bar)
    bar_rect = (56, 74, 186, 102) if size >= 24 else (46, 64, 210, 100)
    td.rounded_rectangle(box(bar_rect), round(10 * k), fill=255)
    # Vertical stem of T
    stem_rect = (107, 94, 135, 164) if size >= 24 else (106, 92, 150, 178)
    td.rounded_rectangle(box(stem_rect), round(6 * k), fill=255)
    # Integrated downward kinetic arrowhead / unpack tip
    if size >= 24:
        td.polygon([pt(92, 154), pt(150, 154), pt(121, 185)], fill=255)
    else:
        td.polygon([pt(84, 162), pt(172, 162), pt(128, 206)], fill=255)

    # Parallel multi-lane micro-notches inside stem at >=32px (symbolizing parallel extraction!)
    if size >= 32:
        for ny in (112, 126, 140):
            td.rounded_rectangle(box((115, ny, 127, ny + 6)), round(2.5 * k), fill=0)

    add_shadow(t_mask, 0, 4, 3.5, 145, (2, 12, 28))
    t_grad = linear_gradient(side, t_mask, (255, 255, 255), (186, 238, 255), diagonal=False)
    canvas.alpha_composite(t_grad)

    # 4. Electric Cyan-Emerald Accent Bar / Docking Tray
    if size >= 24:
        tray_mask = Image.new("L", (side, side), 0)
        trd = ImageDraw.Draw(tray_mask)
        trd.rounded_rectangle(box((72, 190, 170, 200)), round(5 * k), fill=255)
        add_shadow(tray_mask, 0, 2, 4, 160, (6, 182, 212))
        tray = linear_gradient(side, tray_mask, (52, 211, 153), (34, 211, 238), diagonal=True)
        canvas.alpha_composite(tray)

        # Parallel speed accent pill on top-right corner
        pill_mask = Image.new("L", (side, side), 0)
        ImageDraw.Draw(pill_mask).rounded_rectangle(box((164, 54, 204, 62)), round(4 * k), fill=255)
        pill = linear_gradient(side, pill_mask, (103, 232, 249), (52, 211, 153), diagonal=True)
        canvas.alpha_composite(pill)

    # 5. Outer Metallic Rim Bevel
    rim = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle(
        box(base_rect), round(base_r * k),
        outline=(148, 226, 255, 88), width=max(1, round(2.2 * k))
    )
    canvas.alpha_composite(rim)

    return canvas.resize((size, size), Image.Resampling.LANCZOS)


SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="T解压：光棱并行展开 T 图标">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#14223e"/>
      <stop offset="100%" stop-color="#090f1e"/>
    </linearGradient>
    <radialGradient id="glowCyan" cx="74%" cy="23%" r="48%">
      <stop offset="0%" stop-color="#22d3ee" stop-opacity="0.45"/>
      <stop offset="100%" stop-color="#22d3ee" stop-opacity="0"/>
    </radialGradient>
    <radialGradient id="glowMint" cx="30%" cy="80%" r="45%">
      <stop offset="0%" stop-color="#10b981" stop-opacity="0.32"/>
      <stop offset="100%" stop-color="#10b981" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="backPlate" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#6ee7b7" stop-opacity="0.45"/>
      <stop offset="100%" stop-color="#0ea5e9" stop-opacity="0.42"/>
    </linearGradient>
    <linearGradient id="midPlate" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#1c4e76" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#0d2644" stop-opacity="0.9"/>
    </linearGradient>
    <linearGradient id="heroT" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#ffffff"/>
      <stop offset="100%" stop-color="#baeeff"/>
    </linearGradient>
    <linearGradient id="accent" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="#34d399"/>
      <stop offset="100%" stop-color="#22d3ee"/>
    </linearGradient>
    <filter id="drop" x="-20%" y="-20%" width="140%" height="145%">
      <feDropShadow dx="0" dy="6" stdDeviation="6" flood-color="#020612" flood-opacity="0.5"/>
    </filter>
    <mask id="tCut">
      <rect width="256" height="256" fill="black"/>
      <rect x="56" y="74" width="130" height="28" rx="10" fill="white"/>
      <rect x="107" y="94" width="28" height="70" rx="6" fill="white"/>
      <polygon points="92,154 150,154 121,185" fill="white"/>
      <rect x="115" y="112" width="12" height="6" rx="2.5" fill="black"/>
      <rect x="115" y="126" width="12" height="6" rx="2.5" fill="black"/>
      <rect x="115" y="140" width="12" height="6" rx="2.5" fill="black"/>
    </mask>
  </defs>
  <rect x="18" y="18" width="220" height="220" rx="54" fill="url(#bg)" stroke="#94e2ff" stroke-opacity="0.35" stroke-width="2.2" filter="url(#drop)"/>
  <rect x="18" y="18" width="220" height="220" rx="54" fill="url(#glowCyan)"/>
  <rect x="18" y="18" width="220" height="220" rx="54" fill="url(#glowMint)"/>
  <rect x="92" y="36" width="122" height="122" rx="24" fill="url(#backPlate)"/>
  <rect x="44" y="52" width="154" height="152" rx="28" fill="url(#midPlate)" stroke="#7df9ff" stroke-opacity="0.45" stroke-width="1.8" filter="url(#drop)"/>
  <rect x="164" y="54" width="40" height="8" rx="4" fill="url(#accent)"/>
  <rect width="256" height="256" fill="url(#heroT)" mask="url(#tCut)" filter="url(#drop)"/>
  <rect x="72" y="190" width="98" height="10" rx="5" fill="url(#accent)"/>
</svg>
"""


def main():
    (ROOT / "t-unpack.svg").write_text(SVG, encoding="utf-8")
    icons = {n: render(n) for n in SIZES}
    icons[256].save(ROOT / "t-unpack.png")
    icons[256].save(
        ROOT / "t-unpack.ico",
        format="ICO",
        sizes=[(n, n) for n in SIZES],
        append_images=[icons[n] for n in SIZES if n != 256],
    )
    sheet = Image.new("RGB", (760, 420), "#f1f5f9")
    draw = ImageDraw.Draw(sheet)
    for i, size in enumerate(SIZES):
        shown = icons[size] if size <= 64 else icons[size].resize((64, 64), Image.Resampling.LANCZOS)
        x = 22 + i * 82
        sheet.paste(shown, (x + (64 - shown.width) // 2, 24 + (64 - shown.height) // 2), shown)
        draw.text((x + 20, 102), str(size), fill="#1e293b")
    sheet.paste(icons[256], (55, 145), icons[256])
    dark = Image.new("RGB", (270, 270), "#0f172a")
    dark.paste(icons[256], (7, 7), icons[256])
    sheet.paste(dark, (380, 135))
    sheet.save(ROOT / "icon-preview.png")
    print("Gemini icon proposal generated successfully.")


if __name__ == "__main__":
    main()
