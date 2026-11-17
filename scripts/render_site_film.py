"""Original deterministic product film. Offline Pillow rendering, no browser or GUI.

Run: python -X utf8 scripts/render_site_film.py
Requires Pillow, numpy, imageio-ffmpeg. Fonts are read from Windows, not bundled.
"""
from functools import lru_cache
from pathlib import Path
import math
import argparse
import subprocess
import wave

import imageio_ffmpeg
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "site/assets"
SCRATCH = ROOT / "docs/evidence/site-refinement"
W, H, FPS, DURATION = 1280, 720, 30, 24
PORTRAIT = False
MEDIA = "tunpack-20261117"
MINT = (155, 230, 208)
WHITE = (238, 245, 242)
MUTED = (151, 171, 162)
OUT.mkdir(parents=True, exist_ok=True)
SCRATCH.mkdir(parents=True, exist_ok=True)


def smooth(x):
    x = max(0, min(1, x))
    return x * x * (3 - 2 * x)


def lerp(a, b, p):
    return a + (b - a) * p


@lru_cache(None)
def font(size, bold=False, mono=False):
    name = "consola.ttf" if mono else "msyhbd.ttc" if bold else "msyh.ttc"
    return ImageFont.truetype("C:/Windows/Fonts/" + name, size)


def text(image, xy, value, size=20, color=WHITE, bold=False, anchor=None, mono=False):
    ImageDraw.Draw(image).text(xy, value, font=font(size, bold, mono), fill=color, anchor=anchor, spacing=13)


def line(image, points, color=MINT, width=2):
    ImageDraw.Draw(image).line(points, fill=color, width=width, joint="curve")


def panel(image, box, radius=20, fill=(18, 27, 28), outline=(56, 77, 70)):
    ImageDraw.Draw(image).rounded_rectangle(box, radius, fill=fill, outline=outline, width=1)


def pill(image, x, y, label, color=MINT):
    width = ImageDraw.Draw(image).textlength(label, font=font(15)) + 30
    panel(image, (x, y, x + width, y + 33), 16, (20, 42, 35), (48, 91, 71))
    text(image, (x + 15, y + 7), label, 15, color)


@lru_cache(None)
def background():
    y, x = np.mgrid[0:H, 0:W]
    glow = np.exp(-((x - W * .68) / (W * .43)) ** 2 - ((y - H * .53) / (H * .44)) ** 2)
    floor = np.exp(-((x - W * .60) / (W * .45)) ** 2 - ((y - H * .81) / (H * .09)) ** 2)
    rgb = np.stack([7 + 7 * glow + 3 * floor, 9 + 24 * glow + 8 * floor, 10 + 20 * glow + 7 * floor], axis=2)
    return Image.fromarray(np.uint8(rgb)).convert("RGBA")


def header(image, number, label):
    margin = 42 if PORTRAIT else 84
    text(image, (margin, 42), "T解压", 22 if PORTRAIT else 18, (188, 208, 197), True)
    text(image, (W - margin, 48), number + "  /  " + label, 18 if PORTRAIT else 13, MUTED, anchor="ra")
    line(image, [(margin, 89), (W - margin, 89)], (39, 54, 47), 1)


def heading(image, title, subtitle):
    if PORTRAIT:
        text(image, (W / 2, 143), title, 43, WHITE, True, anchor="ma")
        text(image, (W / 2, 220), subtitle, 24, MUTED, anchor="ma")
    else:
        text(image, (84, 125), title, 45, WHITE, True)
        text(image, (87, 198), subtitle, 18, MUTED)


def footnote(image, value):
    text(image, (W / 2, H - 55), value, 22 if PORTRAIT else 14, MUTED, anchor="ma")


@lru_cache(None)
def file_face(extension, archive=False, label=True):
    width, height = 284, 340
    y = np.arange(height)[:, None, None] / height
    top, bottom = (np.array([49, 116, 100]), np.array([12, 43, 42])) if archive else (np.array([78, 94, 103]), np.array([25, 36, 43]))
    color = np.tile(top[None, None, :] * (1 - y) + bottom[None, None, :] * y, (1, width, 1))
    image = Image.fromarray(np.uint8(color)).convert("RGBA")
    mask = Image.new("L", image.size)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, width - 1, height - 1), 25, fill=255)
    image.putalpha(mask)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((1, 1, width - 2, height - 2), 25, outline=(137, 177, 162), width=1)
    draw.line((26, 2, 253, 2), fill=(212, 231, 219, 175), width=2)
    draw.line((26, 67, 256, 67), fill=(154, 198, 176, 42), width=1)
    text(image, (25, 25), "T  /  CONTENT", 13, (193, 221, 205), mono=True)
    draw.polygon([(232, 0), (284, 52), (232, 52)], fill=(186, 213, 201, 37))
    if archive:
        for index in range(6):
            x = 133 if index % 2 == 0 else 143
            draw.rounded_rectangle((x, 86 + index * 10, x + 11, 93 + index * 10), 2, fill=(188, 235, 215))
        draw.rounded_rectangle((124, 146, 164, 177), 8, outline=(210, 247, 232), width=3)
        draw.rounded_rectangle((137, 155, 152, 170), 3, fill=(17, 51, 43))
    else:
        draw.ellipse((113, 99, 171, 157), outline=(172, 193, 202), width=2)
        draw.polygon([(137, 112), (137, 144), (154, 128)], fill=(218, 230, 234))
    if label:
        text(image, (27, 198), extension, 57, WHITE, True)
    text(image, (29, 289), "旅途记录.mp4" if extension in [".MP4", "ZIP"] else "内容归档", 18, (185, 209, 198))
    return image


def place(image, layer, center, angle=0, scale=1, alpha=1, shadow=True):
    tile = layer.copy()
    if alpha < 1:
        tile.putalpha(tile.getchannel("A").point(lambda p: int(p * alpha)))
    if abs(scale - 1) > 0.002:
        tile = tile.resize((int(tile.width * scale), int(tile.height * scale)), Image.Resampling.BICUBIC)
    if angle:
        tile = tile.rotate(angle, Image.Resampling.BICUBIC, expand=True)
    xy = (int(center[0] - tile.width / 2), int(center[1] - tile.height / 2))
    if shadow:
        mask = Image.new("L", (tile.width + 80, tile.height + 80))
        mask.paste(tile.getchannel("A"), (40, 40))
        mask = mask.point(lambda p: int(p * .38)).filter(ImageFilter.GaussianBlur(17))
        shade = Image.new("RGBA", mask.size)
        shade.putalpha(mask)
        image.alpha_composite(shade, (xy[0] - 32, xy[1] - 19))
    image.alpha_composite(tile, xy)


def reveal_scene(t):
    image = background().copy()
    p = smooth((t - 2.7) / 2.5)
    cx, cy = (360, 548) if PORTRAIT else (850, 380)
    depth = 1 + .28 * math.sin(t * .5)
    for index in [2, 1]:
        place(image, file_face("7Z", True), (cx + 32 * index * depth, cy - 23 * index * depth), -7 - index * 4, .94, .56, False)
    face = file_face(".MP4", False, False).copy()
    mask = Image.new("L", face.size)
    ImageDraw.Draw(mask).rectangle((0, 0, int(face.width * p), face.height), fill=255)
    face = Image.composite(file_face("ZIP", True, False), face, mask)
    if .01 < p < .99:
        line(face, [(int(284 * p), 4), (int(284 * p), 335)], (202, 255, 233), 3)
    extension = Image.new("RGBA", face.size)
    text(extension, (27, 198), ".MP4" if p < .5 else "ZIP", 57, (*WHITE, int(255 * min(1, abs(p - .5) * 6))), True)
    face.alpha_composite(extension)
    place(image, face, (cx - 13 * p, cy + math.sin(t * .7) * 3), -8 + 3 * p, 1.02)
    # The subject camera moves between actions. Copy and diagnostic remain fixed.
    zoom = 1 + (.26 if PORTRAIT else .58) * (smooth((t - 1.3) / 1.3) - smooth((t - 4.8) / 1.9))
    left, top = cx * (1 - 1 / zoom), cy * (1 - 1 / zoom)
    image = image.crop((left, top, left + W / zoom, top + H / zoom)).resize((W, H), Image.Resampling.BICUBIC)
    header(image, "01", "内容识别")
    if PORTRAIT:
        text(image, (360, 139), "名字会伪装。", 46, WHITE, True, anchor="ma")
        text(image, (360, 212), "内容不会。", 46, MINT, True, anchor="ma")
    else:
        text(image, (84, 214), "名字会伪装。", 55, WHITE, True)
        text(image, (84, 296), "内容不会。", 55, MINT, True)
        text(image, (86, 406), "按文件内容，识别真正的格式。", 18, MUTED)
    if t > 1:
        a = smooth((t - 1) / .8)
        info = Image.new("RGBA", (520 if PORTRAIT else 440, 64))
        panel(info, (0, 0, info.width - 2, 62), 14, (13, 24, 23), (65, 98, 81))
        text(info, (21, 13), "50 4B 03 04" if p > .8 else "读取文件内容…", 25 if PORTRAIT else 19, MINT, mono=p > .8)
        text(info, (info.width - 25, 19), "ZIP" if p > .8 else "识别中", 20 if PORTRAIT else 15, MUTED, anchor="ra")
        place(image, info, (360 if PORTRAIT else 858, 788 if PORTRAIT else 663), alpha=a)
    if PORTRAIT:
        footnote(image, "识别受支持的伪装，不修改原文件")
    else:
        text(image, (84, 647), "改名文件 · 部分前缀伪装 · 原文件保留", 14, MUTED)
    return image


def checkmark(image, x, y, active=True):
    d = ImageDraw.Draw(image)
    d.ellipse((x, y, x + 28, y + 28), fill=(33, 76, 58) if active else (34, 44, 42))
    if active:
        line(image, [(x + 7, y + 14), (x + 13, y + 20), (x + 22, y + 9)], MINT, 3)


def password_scene(t):
    image = background().copy()
    header(image, "02", "候选密码")
    heading(image, "先找对钥匙。", "优先候选，通过正文验证再使用。")
    x, y, width, height = (50, 316, 620, 410) if PORTRAIT else (244, 276, 792, 320)
    # A shallow stack establishes depth before the stable validation close-up.
    for i in [2, 1]:
        panel(image, (x + i * 8, y - i * 9, x + width - i * 8, y + height - i * 9), 24, (12, 25, 23), (42, 69, 55))
    panel(image, (x, y, x + width, y + height), 24, (19, 33, 28), (82, 117, 96))
    text(image, (x + 30, y + 27), "本机密码本", 28 if PORTRAIT else 22, WHITE, True)
    text(image, (x + width - 30, y + 34), "WINDOWS DPAPI", 17 if PORTRAIT else 13, MUTED, anchor="ra")
    for i in range(3):
        row_y = y + 98 + i * (88 if PORTRAIT else 62)
        active = t > .85 and i == 0
        panel(image, (x + 26, row_y - 8, x + width - 26, row_y + (57 if PORTRAIT else 39)), 11, (26, 58, 42) if active else (16, 29, 23), (82, 157, 113) if active else (39, 59, 46))
        text(image, (x + 43, row_y + 2), "● ● ● ● ● ●", 26 if PORTRAIT else 21, (178, 210, 190))
        text(image, (x + width - 83, row_y + 6), "已验证" if active else "候选", 24 if PORTRAIT else 17, MINT if active else MUTED, anchor="ra")
        if active:
            checkmark(image, x + width - 68, row_y + 3)
    footnote(image, "来自你保存、导入或提供的密码")
    return image


def volumes_scene(t):
    image = background().copy()
    header(image, "02", "分卷归位")
    heading(image, "各有来路。一起归队。" if not PORTRAIT else "改过名，也能归队。", "识别分卷关系，必要时手工确认。")
    if PORTRAIT:
        for i, name in enumerate(["资料.7z.001", "课程文档.docx", "资料.7z.003"]):
            enter = smooth((t - .18 - i * .26) / .65)
            x, y, width = 52, 328 + i * 133, 616
            layer = Image.new("RGBA", (616, 112))
            panel(layer, (0, 0, 615, 111), 17, (20, 36, 29), (72, 110, 85))
            text(layer, (27, 23), name, 29, WHITE)
            text(layer, (27, 70), f"卷 {i + 1:03d}", 24, MINT if t > 1.3 + i * .35 else MUTED)
            if t > 1.3 + i * .35:
                checkmark(layer, 550, 39)
            place(image, layer, (360, y + 56 + (1 - enter) * 20), alpha=enter)
    else:
        for i, name in enumerate(["资料.7z.001", "课程文档.docx", "资料.7z.003"]):
            enter = smooth((t - .18 - i * .26) / .65)
            layer = Image.new("RGBA", (320, 245))
            panel(layer, (0, 0, 319, 244), 22, (20, 36, 29), (72, 110, 85))
            text(layer, (27, 27), f"{i + 1:02d}", 55, MINT, mono=True)
            text(layer, (27, 125), name, 25, WHITE)
            text(layer, (27, 190), "已确认卷位" if t > 1.3 + i * .35 else "等待确认", 17, MUTED)
            if t > 1.3 + i * .35:
                checkmark(layer, 262, 186)
            place(image, layer, (264 + i * 376, 440 + 24 * (1 - enter)), alpha=enter)
            if i < 2 and enter > .6:
                line(image, [(438 + i * 376, 440), (464 + i * 376, 440)], (98, 165, 123), 2)
    footnote(image, "缺卷或有歧义时，明确提示，不盲目拼接")
    return image


def folder_icon(image, x, y, scale=1):
    d = ImageDraw.Draw(image)
    x2, y2 = x + int(88 * scale), y + int(65 * scale)
    d.rounded_rectangle((x, y, x + int(39 * scale), y + int(26 * scale)), int(7 * scale), fill=(113, 185, 145))
    d.rounded_rectangle((x, y + int(13 * scale), x2, y2), int(7 * scale), fill=(68, 135, 104), outline=(143, 218, 173), width=1)
    line(image, [(x + 8, y + int(25 * scale)), (x2 - 8, y + int(25 * scale))], (165, 235, 196), max(1, int(scale)))


def result_scene(t):
    image = background().copy()
    header(image, "03", "逐层展开")
    heading(image, "解开一层。再下一层。" if not PORTRAIT else "一层，再下一层。", "每一层内容，都有自己的位置。")
    expansion = smooth(t / 2.1)
    for i in [2, 1, 0]:
        focus = smooth((t - i * .65) / .75)
        width, height = (600, 134) if PORTRAIT else (280, 262)
        node = Image.new("RGBA", (width, height))
        panel(node, (0, 0, width - 1, height - 1), 22, (20, 36, 29), (73, 115, 89))
        if PORTRAIT:
            if i < 2:
                place(node, file_face("ZIP" if i == 0 else "7Z", True), (65, 66), -5, .25, shadow=False)
            else:
                folder_icon(node, 30, 38, .8)
            text(node, (133, 27), ["外层.zip", "内层.7z", "文件已就位"][i], 30, WHITE)
            text(node, (135, 79), ["保留外层处理记录", "自动展开嵌套", "同名结果使用新名称"][i], 23, MUTED)
            cx = lerp(360 + i * 18, 360, expansion)
            cy = lerp(470 - i * 19, 350 + i * 169, expansion)
            place(image, node, (cx, cy), alpha=1)
        else:
            if i < 2:
                place(node, file_face("ZIP" if i == 0 else "7Z", True), (140, 103), -5, .35, shadow=False)
            else:
                folder_icon(node, 96, 72)
            text(node, (140, 190), ["外层.zip", "内层.7z", "文件已就位"][i], 23, WHITE, anchor="ma")
            text(node, (140, 229), ["原包旁 · 或指定目录", "自动处理嵌套", "同名结果使用新名称"][i], 14, MUTED, anchor="ma")
            cx = lerp(640 + i * 38, 258 + i * 382, expansion)
            cy = lerp(425 - i * 22, 425, expansion)
            place(image, node, (cx, cy), scale=lerp(.88 - i * .07, 1, expansion), alpha=1)
    if t > 3.1:
        label = "内层失败？单独重试。" if PORTRAIT else "内层遇到问题？单独准备重试，不必重解外层。"
        text(image, (W / 2, H - 126), label, 28 if PORTRAIT else 21, MINT, anchor="ma")
    footnote(image, "原任务仍保留未完成记录，已有内容保留")
    return image


@lru_cache(None)
def logo():
    return Image.open(ROOT / "brand/t-unpack.png").convert("RGBA")


def final_scene(t):
    image = background().copy()
    place(image, logo(), (W / 2, (315 if PORTRAIT else 235) - 12 * smooth(t / 3)), scale=(190 if PORTRAIT else 162) / logo().width)
    text(image, (W / 2, 470 if PORTRAIT else 356), "T解压", 65 if PORTRAIT else 59, WHITE, True, anchor="ma")
    text(image, (W / 2, 590 if PORTRAIT else 458), "把复杂，解开。", 42 if PORTRAIT else 35, MINT, anchor="ma")
    text(image, (W / 2, 746 if PORTRAIT else 568), "WINDOWS / 原生轻量 / 免安装", 24 if PORTRAIT else 15, MUTED, anchor="ma")
    return image


SCENES = [(0, reveal_scene), (7.5, password_scene), (11, volumes_scene), (15, result_scene), (21, final_scene)]


def frame(t):
    index = max(i for i, (start, _) in enumerate(SCENES) if t >= start)
    start, render = SCENES[index]
    image = render(t - start)
    if index and t - start < .18:
        prev_start, prev = SCENES[index - 1]
        image = Image.blend(prev(start - prev_start - .001), image, smooth((t - start) / .18))
    return image.convert("RGB")


def soundtrack():
    rate = 48000
    seconds = np.arange(rate * DURATION) / rate
    audio = np.zeros_like(seconds)
    # Original, restrained synthesized score: no sampled or third-party recordings.
    for start, frequencies in [(0, [130.81, 196, 261.63]), (7.5, [146.83, 220, 293.66]), (15, [164.81, 246.94, 329.63]), (21, [130.81, 196, 261.63])]:
        local = seconds - start
        gate = np.maximum(0, np.minimum(1, local / 1.5)) * np.exp(-np.maximum(local, 0) / 3)
        gate[local < 0] = 0
        for freq in frequencies:
            audio += .027 * gate * (np.sin(2 * np.pi * freq * local) + .2 * np.sin(4 * np.pi * freq * local))
    for start, pitch in [(2.9, 784), (4.9, 1046.5), (8.5, 659.25), (11.7, 783.99), (12.2, 880), (12.7, 1046.5), (15.7, 523.25), (16.5, 659.25), (17.4, 783.99), (21.4, 1046.5)]:
        local = seconds - start
        env = np.exp(-np.maximum(local, 0) * 3.5) * np.minimum(np.maximum(local, 0) * 90, 1)
        env[local < 0] = 0
        audio += .045 * env * np.sin(2 * np.pi * pitch * local)
    audio *= np.minimum(seconds / .6, 1) * np.minimum((DURATION - seconds) / 1.5, 1)
    stereo = np.stack([audio, .96 * audio], axis=1)
    target = SCRATCH / "original-score.wav"
    with wave.open(str(target), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes((np.clip(stereo, -1, 1) * 32767).astype("<i2").tobytes())
    return target


def render(format_name, stills_only=False):
    global W, H, PORTRAIT
    PORTRAIT = format_name == "portrait"
    W, H = (720, 900) if PORTRAIT else (1280, 720)
    background.cache_clear()
    stem = MEDIA + ("-portrait" if PORTRAIT else "-wide")
    poster = frame(6.8)
    poster.save(OUT / (stem + ".webp"), quality=88)
    if not PORTRAIT:
        poster.resize((1200, 675), Image.Resampling.LANCZOS).crop((0, 22, 1200, 652)).save(OUT / (MEDIA + "-cover.jpg"), quality=91)
    moments = [1, 3.8, 6.9, 9.3, 13.7, 15.2, 18.7, 23]
    tw, th = (288, 360) if PORTRAIT else (480, 270)
    sheet = Image.new("RGB", (tw * 2, (th + 27) * 4), (7, 9, 10))
    for index, moment in enumerate(moments):
        sample = frame(moment)
        sample.save(SCRATCH / f"{format_name}-{moment:04.1f}.png")
        x, y = index % 2 * tw, index // 2 * (th + 27)
        sheet.paste(sample.resize((tw, th), Image.Resampling.LANCZOS), (x, y))
        text(sheet, (x + 10, y + th + 4), f"{moment:.1f}s", 13, MUTED)
    sheet.save(SCRATCH / (format_name + "-storyboard.jpg"), quality=94)
    if stills_only:
        return
    audio = soundtrack()
    command = [imageio_ffmpeg.get_ffmpeg_exe(), "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "pipe:0", "-i", str(audio), "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", "-t", str(DURATION), str(OUT / (stem + ".mp4"))]
    process = subprocess.Popen(command, stdin=subprocess.PIPE)
    for index in range(DURATION * FPS):
        process.stdin.write(frame(index / FPS).tobytes())
        if index % (FPS * 6) == 0:
            print(f"{format_name}: {index // FPS}/{DURATION}s", flush=True)
    process.stdin.close()
    if process.wait():
        raise SystemExit("ffmpeg failed")
    print(f"{stem}: {(OUT / (stem + '.mp4')).stat().st_size:,} bytes", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format", choices=["wide", "portrait", "all"], default="all")
    parser.add_argument("--stills-only", action="store_true")
    args = parser.parse_args()
    for format_name in (["wide", "portrait"] if args.format == "all" else [args.format]):
        render(format_name, args.stills_only)


if __name__ == "__main__":
    main()
