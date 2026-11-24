"""Build the TUnpack pull-tab icon and its native-size proof sheet with Pillow."""

from __future__ import annotations

import io
import math
import re
import struct
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageColor, ImageDraw, ImageFont


OUT = Path(__file__).resolve().parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


@dataclass(frozen=True)
class Gradient:
    axis: tuple
    stops: tuple


PAINT = {
    "well": Gradient((80, 107, 146, 162), (
        (0, "#424377"), (.4, "#282953"), (1, "#14172F"))),
    "left-wall": Gradient((41, 149, 133, 227), (
        (0, "#6677EC"), (.38, "#4C58C0"), (.8, "#3B419F"), (1, "#47459C"))),
    "right-wall": Gradient((135, 219, 216, 157), (
        (0, "#303477"), (.48, "#252A65"), (1, "#474C92"))),
    "left-wing": Gradient((64, 125, 83, 194), (
        (0, "#CCD2FF"), (.26, "#B4BEFF"), (.62, "#919DF8"), (1, "#7A84EB"))),
    "right-wing": Gradient((180, 131, 191, 178), (
        (0, "#A7AFFF"), (.32, "#8C98FF"), (.73, "#7080F4"), (1, "#6571D7"))),
    "fold-light": Gradient((28, 137, 133, 165), (
        (0, "#CDD5FF"), (.36, "#F0F2FF"), (1, "#ACB5FF"))),
    "front-ridge": Gradient((131, 179, 132, 232), (
        (0, "#585AC4"), (.68, "#7775CB"), (1, "#57589D"))),
    "file-edge": Gradient((116, 112, 201, 129), (
        (0, "#666BA6"), (.72, "#9FA8E4"), (1, "#666DA9"))),
    "strip-face": Gradient((99, 32, 143, 152), (
        (0, "#FFCC80"), (.24, "#FFB45B"), (.52, "#FD924A"),
        (.69, "#F78B49"), (.82, "#FFA461"), (.91, "#FA9957"), (1, "#DB673B"))),
    "strip-crown": Gradient((55, 33, 188, 19), (
        (0, "#FFE3AC"), (.4, "#FFF0CB"), (.76, "#FFD294"), (1, "#FFBC72"))),
    "strip-side": Gradient((139, 49, 152, 144), (
        (0, "#B34D32"), (.35, "#CC5A32"), (.69, "#CA5734"),
        (.86, "#ED8242"), (1, "#FFAF60"))),
    "strip-return": Gradient((125, 140, 167, 131), (
        (0, "#B24E32"), (.47, "#DF6936"), (.86, "#FFB85F"), (1, "#FFCA7C"))),
    "strip-hem": Gradient((51, 41, 183, 27), (
        (0, "#FFE9C2"), (.52, "#FFF4D9"), (1, "#FFD494"))),
    "rolled-edge": Gradient((102, 92, 123, 147), (
        (0, "#FFA45B"), (.66, "#FFBF7B"), (1, "#F49253"))),
}


@dataclass(frozen=True)
class Face:
    name: str
    paint: str
    path: str


def artwork(size):
    """Use the same folds at every scale, with optical cuts for small frames."""
    micro = size <= 32
    faces = []

    def add(name, paint, path):
        faces.append(Face(name, paint, " ".join(path.split())))

    add("archive-body", "right-wall", """
        M 35 130 L 120 91 L 218 121 L 216 188
        Q 216 195 210 198 L 139 234 Q 133 237 127 235
        L 44 204 Q 37 201 37 195 Z""")
    add("near-wall", "left-wall", """
        M 35 130 L 133 163 L 133 236 Q 129 236 125 234
        L 44 204 Q 37 201 37 195 Z""")
    if size >= 48:
        add("pressed-corner", "front-ridge", """
            M 131 168 L 134 166 L 134 231 Q 134 235 132.5 235.8
            Q 131 235 131 231 Z""")
        add("left-flap-shadow", "#4348A7", """
            M 36.5 154 L 119 180 L 133 164 L 133 171
            L 113 192 Q 110 195 105 193 L 36.6 165.8 Z""")
        add("right-flap-shadow", "#2D306F", """
            M 133 164 L 158 184 L 217 154 L 216.7 164
            L 163 192.5 Q 159 195 155.8 192 L 133 169 Z""")

    add("open-mouth", "well", """
        M 35 130 L 116 93 Q 120 91 125 92.5 L 218 121
        L 137 165 Q 133 167 128 165 Z""")
    if size >= 40:
        add("inside-back-rim", "#959CDA", """
            M 35 130 L 116 93 Q 120 91 125 92.5 L 218 121
            L 213 122.6 L 124 95 Q 120 94 117 95.5 L 39 131.5 Z""")
        add("back-file", "file-edge", """
            M 82 129 L 128 107.5 Q 131 106 134 107 L 200 127.3
            L 196 129.2 L 134 110 Q 131 109 129 110 L 86 130.5 Z""")
        add("front-file", "file-edge", """
            M 94 135 L 139 114 Q 142 112.5 145 113.5 L 190 127.5
            Q 192 128.2 190 129.1 L 187.5 130 L 144 116.5
            Q 142 116 140 117 L 98 136.4 Z""")

    # The return folds behind the T. Its last visible tip catches the light.
    add("curled-tip", "strip-return", """
        M 111 135 C 119 139 135 138 145 131 L 154 125
        Q 155 124 157 125 L 167 130 Q 169 131 167 132.5
        L 150 144 C 142 149 132 151 125 148 Z""")
    add("pull-tab-edge", "strip-side", """
        M 112 49 L 148.5 45 L 145.6 103 C 144 119 145 129 155 137
        L 146 145 C 132 145 116 137 111 127 Z""")

    if micro:
        # An extra half-pixel of crossbar depth and a wider stem at 16px.
        add("pull-tab", "strip-face", """
            M 54 33 Q 54 29 59 28 L 190 17 Q 195 17 194 22
            L 190 54 Q 190 58 185 59 L 146 62 L 142 105
            Q 141 130 153 140 L 128 149 Q 122 151 116 146
            Q 99 135 100 112 L 103 66 L 58 70 Q 53 70 52 65 Z""")
    else:
        add("pull-tab", "strip-face", """
            M 52.5 37.5 Q 52.7 33.7 57.5 33 L 189 21.5
            Q 194.5 21.1 194 26.5 L 190.5 50.5 Q 190 54.3 186 55
            L 145.5 58.6 L 141.7 105 C 140.2 121 143.5 133 153.8 139
            L 132 148 Q 126 150.6 120.7 147.2
            C 106 139 100.6 126.5 102 110 L 105.4 62.1 L 58.5 66.2
            Q 53.5 66.6 53.3 62 Z""")
        add("upper-bevel", "strip-crown", """
            M 53 35 Q 53.8 32.9 58 30.6 L 69.5 24.5 Q 72 23.1 76.2 22.7
            L 186.4 13.2 Q 189.5 13 191.6 15.5 L 193.4 19.2
            Q 194.8 22 193.9 25 L 190.7 24 Q 189 23.3 185 23.7
            L 58 35.2 Q 55 35.4 52.6 37.8 Z""")
        add("cap-return", "strip-side", """
            M 194 22 Q 196.5 23 195.9 27 L 192.6 49 Q 192.1 52.5 189.3 54
            Q 190.3 52.6 190.5 50 L 193.6 26 Q 194 23.8 192.6 23 Z""")
        if size >= 64:
            add("rolled-highlight", "strip-hem", """
                M 53 36.5 Q 54.4 34.6 58 34.1 L 184.8 22.7
                Q 190.7 22 192.5 23.4 Q 190 23.3 185 24
                L 58.1 35.6 Q 54.6 35.9 53 38.4 Z""")
            add("ribbon-edge-light", "rolled-edge", """
                M 103.2 96 L 102.8 108 C 101.2 125 106.4 138.7 120.8 147.1
                C 104.7 139 100.8 124.7 102 110 Z""")

    # Broad hinges meet at the front corner; the dark cavity stays open above.
    add("left-flap-edge", "#5058B3", """
        M 7.5 149 L 34 130 L 133 163 L 109 188.2 Q 107 190.5 103.4 189.3
        L 10 155.5 Q 7.3 154.5 7.5 151 Z""")
    add("left-flap", "left-wing", """
        M 8.8 147.8 L 32.4 130.6 Q 34.5 129.1 37 130 L 132.9 163.5
        L 108.2 185 Q 106.1 187.1 103.3 186.1 L 9.4 152.1
        Q 5.9 150.8 8.8 147.8 Z""")
    add("right-flap-edge", "#464D9F", """
        M 132.8 163.4 L 217 121 L 244.7 140.8 L 244.7 144
        Q 244.7 146 242 147.3 L 160.3 189.1 Q 157.5 190.7 155 188.4 Z""")
    add("right-flap", "right-wing", """
        M 132.6 163.4 L 215.8 121.4 Q 217.9 120.2 220 121.7
        L 243.6 139.1 Q 246.3 141.1 243.4 142.8 L 160 185.6
        Q 157.3 187.2 154.8 184.9 Z""")
    if size >= 48:
        add("left-hinge", "fold-light", """
            M 30.5 132 Q 33.5 128.3 37 129.8 L 132.8 163.2 L 131.3 164.5
            L 36.8 131.5 Q 34.1 130.5 30.5 132 Z""")
        add("right-hinge", "#B7C0FF", """
            M 133 163.2 L 215.3 121.3 Q 218.2 119.7 220.6 122.1
            Q 218 121.6 216 122.8 L 134.2 164.5 Z""")
    return faces


def svg_source(faces):
    root = ET.Element("svg", xmlns="http://www.w3.org/2000/svg", width="256",
                      height="256", viewBox="0 0 256 256", role="img",
                      attrib={"aria-labelledby": "title desc"})
    ET.SubElement(root, "title", id="title").text = "TUnpack"
    ET.SubElement(root, "desc", id="desc").text = (
        "An amber T-shaped pull tab lifts out of an open periwinkle archive box.")
    defs = ET.SubElement(root, "defs")
    for name, gradient in PAINT.items():
        x1, y1, x2, y2 = map(str, gradient.axis)
        node = ET.SubElement(defs, "linearGradient", id=name, x1=x1, y1=y1,
                             x2=x2, y2=y2, gradientUnits="userSpaceOnUse")
        for position, color in gradient.stops:
            ET.SubElement(node, "stop", offset=f"{position:g}", **{"stop-color": color})
    for face in faces:
        fill = f"url(#{face.paint})" if face.paint in PAINT else face.paint
        ET.SubElement(root, "path", id=face.name, d=face.path, fill=fill)
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="unicode") + "\n"


def polygon(path, scale):
    """Flatten the absolute SVG Béziers to subpixel accuracy for Pillow."""
    tokens = iter(re.findall(r"[MLQCZ]|-?(?:\d*\.)?\d+", path))
    points = []

    def point():
        return complex(float(next(tokens)), float(next(tokens))) * scale

    for command in tokens:
        if command in ("M", "L"):
            points.append(point())
        elif command in ("Q", "C"):
            controls = [points[-1], point(), point()]
            if command == "C":
                controls.append(point())
            length = sum(abs(b - a) for a, b in zip(controls, controls[1:]))
            steps = max(3, math.ceil(length / 2))
            for i in range(1, steps + 1):
                t = i / steps
                row = controls
                while len(row) > 1:
                    row = [a + (b - a) * t for a, b in zip(row, row[1:])]
                points.append(row[0])
        elif command != "Z":
            raise ValueError(command)
    return [(round(p.real), round(p.imag)) for p in points]


def gradient_patch(gradient, box, scale):
    """Project a 1-D sRGB ramp onto the clipped face in artwork coordinates."""
    left, top, right, bottom = box
    x1, y1, x2, y2 = gradient.axis
    dx, dy = x2 - x1, y2 - y1
    length_squared = dx * dx + dy * dy
    ax, ay = dx / length_squared / scale, dy / length_squared / scale
    intercept = left * ax + top * ay - (x1 * dx + y1 * dy) / length_squared
    width, height = right - left, bottom - top
    corners = (intercept, intercept + ax * width, intercept + ay * height,
               intercept + ax * width + ay * height)
    lo, hi = min(corners) - .01, max(corners) + .01
    density = 1023 / (hi - lo)
    stops = [(pos, ImageColor.getrgb(rgb)) for pos, rgb in gradient.stops]
    colors = []
    segment = 0
    for i in range(1024):
        t = lo + i / density
        while segment + 2 < len(stops) and t > stops[segment + 1][0]:
            segment += 1
        p, a = stops[segment]
        q, b = stops[segment + 1]
        mix = max(0, min(1, (t - p) / (q - p)))
        colors.append(tuple(round(c + (d - c) * mix) for c, d in zip(a, b)))
    ramp = Image.new("RGB", (1024, 1))
    ramp.putdata(colors)
    return ramp.transform((width, height), Image.Transform.AFFINE,
                          (ax * density, ay * density, (intercept - lo) * density + .5,
                           0, 0, .5), Image.Resampling.BILINEAR)


def render(size):
    sample = 8 if size <= 64 else 5
    extent = size * sample
    scale = extent / 256
    canvas = Image.new("RGBA", (extent, extent))
    for face in artwork(size):
        xy = polygon(face.path, scale)
        xs, ys = zip(*xy)
        box = min(xs), min(ys), max(xs) + 1, max(ys) + 1
        mask = Image.new("L", (box[2] - box[0], box[3] - box[1]))
        ImageDraw.Draw(mask).polygon([(x - box[0], y - box[1]) for x, y in xy], fill=255)
        if face.paint in PAINT:
            fill = gradient_patch(PAINT[face.paint], box, scale).convert("RGBA")
        else:
            fill = face.paint
        canvas.paste(fill, box, mask)
    return canvas.resize((size, size), Image.Resampling.LANCZOS)


def preview(frames):
    panel_w, panel_h = 1224, 344
    sheet = Image.new("RGB", (panel_w, panel_h * 2))
    draw = ImageDraw.Draw(sheet)

    def font(filename, size):
        try:
            return ImageFont.truetype(filename, size)
        except OSError:
            return ImageFont.load_default(size=size)

    title_font = font("seguisb.ttf", 21)
    label_font = font("segoeui.ttf", 14)
    scale_font = font("consola.ttf", 13)
    for row, (bg, ink, muted, line) in enumerate((
        ("#F6F6F9", "#212232", "#515569", "#D8D9E1"),
        ("#191A22", "#F2F1F8", "#B0B1C4", "#393B49"),
    )):
        y = row * panel_h
        draw.rectangle((0, y, panel_w, y + panel_h - 1), fill=bg)
        draw.text((30, y + 22), "TUnpack", font=title_font, fill=ink, anchor="lt")
        draw.text((290, y + 28), "256 px", font=scale_font, fill=muted, anchor="rt")
        draw.line((315, y + 24, 315, y + panel_h - 25), fill=line)
        hero = frames[256]
        sheet.paste(hero, (31, y + 63), hero)
        draw.text((342, y + 27), "Native ICO sizes", font=label_font, fill=ink, anchor="lt")
        draw.text((1190, y + 22), "Light" if row == 0 else "Dark", font=label_font,
                  fill=muted, anchor="rt")
        # Actual, unscaled pixels on one common baseline.
        x = 336
        for size in SIZES:
            cell = size + 26
            center = x + cell // 2
            frame = frames[size]
            sheet.paste(frame, (center - size // 2, y + 294 - size), frame)
            draw.text((center, y + 308), str(size), font=scale_font, fill=muted, anchor="mt")
            x += cell
    return sheet


def build():
    svg = OUT / "t-unpack.svg"
    png = OUT / "t-unpack.png"
    ico = OUT / "t-unpack.ico"
    sheet = OUT / "icon-preview.png"
    svg.write_text(svg_source(artwork(256)), encoding="utf-8", newline="\n")
    frames = {size: render(size) for size in SIZES}
    master = frames[256]
    master.save(png, optimize=True)
    master.save(ico, sizes=[(s, s) for s in SIZES],
                append_images=[frames[s] for s in SIZES[:-1]])
    preview(frames).save(sheet, optimize=True)

    # Read each ICO directory entry and payload independently of Pillow's size lookup.
    data = ico.read_bytes()
    assert struct.unpack_from("<HHH", data) == (0, 1, len(SIZES))
    with Image.open(ico) as archive:
        assert archive.ico.sizes() == {(s, s) for s in SIZES}
        for i, size in enumerate(SIZES):
            w, h, _, _, _, depth, length, offset = struct.unpack_from(
                "<BBBBHHII", data, 6 + 16 * i)
            assert (w or 256, h or 256, depth) == (size, size, 32)
            with Image.open(io.BytesIO(data[offset:offset + length])) as embedded:
                assert embedded.size == (size, size) and embedded.mode == "RGBA"
                assert embedded.tobytes() == frames[size].tobytes()
    with Image.open(png) as image:
        assert image.mode == "RGBA" and image.size == (256, 256)
        assert image.getchannel("A").getextrema() == (0, 255)
    assert ET.parse(svg).getroot().get("viewBox") == "0 0 256 256"
    with Image.open(sheet) as image:
        image.verify()
    for path in (svg, png, ico, sheet):
        print(f"{path.name}  {path.stat().st_size:,} bytes")
    print("Verified native ICO frames:", ", ".join(map(str, SIZES)))


if __name__ == "__main__":
    build()
