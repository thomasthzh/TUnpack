"""Original deterministic product film. Offline Pillow rendering, no browser or GUI.

Run: python -X utf8 scripts/render_site_film.py
Requires Pillow, numpy, imageio-ffmpeg. Fonts are read from Windows, not bundled.
"""
from functools import lru_cache
from pathlib import Path
import math
import subprocess
import wave

import imageio_ffmpeg
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "site/assets"
SCRATCH = ROOT / "docs/evidence/site-film"
W, H, FPS, DURATION = 1280, 720, 30, 24
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
    glow = np.exp(-((x - 870) / 530) ** 2 - ((y - 360) / 300) ** 2)
    floor = np.exp(-((x - 740) / 500) ** 2 - ((y - 580) / 65) ** 2)
    rgb = np.stack([7 + 8 * glow + 3 * floor, 9 + 23 * glow + 9 * floor, 10 + 19 * glow + 7 * floor], axis=2)
    return Image.fromarray(np.uint8(rgb)).convert("RGBA")


def header(image, number, label):
    text(image, (84, 47), "T解压", 18, (188, 208, 197), True)
    text(image, (1197, 53), number + "  /  " + label, 13, MUTED, anchor="ra")
    line(image, [(84, 89), (1196, 89)], (39, 54, 47), 1)


def copy(image, first, second, detail, x=84, y=204):
    text(image, (x, y), first, 55, WHITE, True)
    text(image, (x, y + 82), second, 55, MINT, True)
    text(image, (x + 2, y + 192), detail, 18, MUTED)


@lru_cache(None)
def file_face(extension, archive=False):
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
        shade = Image.new("RGBA", tile.size, (0, 0, 0, 0))
        shade.putalpha(tile.getchannel("A").point(lambda p: int(p * 0.40)))
        shade = shade.filter(ImageFilter.GaussianBlur(17))
        image.alpha_composite(shade, (xy[0] + 7, xy[1] + 23))
    image.alpha_composite(tile, xy)


def reveal_scene(t):
    image = background().copy()
    p = smooth((t - 2.7) / 2.5)
    sway = math.sin(t * .7) * 3
    for index in [2, 1]:
        layer = file_face("7Z", True).copy()
        place(image, layer, (875 + 35 * index, 355 - 28 * index), -8 - index * 3, .98, .60, False)
    face = file_face(".MP4").copy()
    mask = Image.new("L", face.size)
    ImageDraw.Draw(mask).rectangle((0, 0, int(face.width * p), face.height), fill=255)
    face = Image.composite(file_face("ZIP", True), face, mask)
    if .01 < p < .99:
        line(face, [(int(284 * p), 4), (int(284 * p), 335)], (202, 255, 233), 3)
    place(image, face, (850 - 16 * p, 379 + sway), -8 + 3 * p, 1.06 + .06 * math.sin(t * .3))
    # An authored wide -> 1.52x macro -> wide camera. The file scan is held
    # inside the close-up; copy and controls are composited after the camera.
    zoom = 1 + .52 * (smooth((t - 1.3) / 1.3) - smooth((t - 4.8) / 1.9))
    left, top = 850 * (1 - 1 / zoom), 379 * (1 - 1 / zoom)
    image = image.crop((left, top, left + W / zoom, top + H / zoom)).resize((W, H), Image.Resampling.BICUBIC)
    header(image, "01", "内容识别")
    copy(image, "名字会伪装。", "内容不会。", "识别压缩内容，而不只看文件后缀。")
    if t > 1:
        a = smooth((t - 1) / .8)
        info = Image.new("RGBA", (440, 64))
        panel(info, (0, 0, 438, 62), 14, (13, 24, 23), (65, 98, 81))
        text(info, (21, 13), "50 4B 03 04" if p > .8 else "读取文件内容…", 19, MINT, mono=p > .8)
        text(info, (410, 20), "ZIP" if p > .8 else "识别中", 15, MUTED, anchor="ra")
        place(image, info, (858, 663 + 12 * (1 - a)), alpha=a)
    text(image, (84, 647), "改名文件 · 部分前缀伪装 · 不修改原文件", 14, MUTED)
    return image


def checkmark(image, x, y, active=True):
    d = ImageDraw.Draw(image)
    d.ellipse((x, y, x + 24, y + 24), fill=(33, 76, 58) if active else (34, 44, 42))
    if active:
        line(image, [(x + 6, y + 12), (x + 11, y + 17), (x + 19, y + 8)], MINT, 2)


def logic_scene(t):
    image = background().copy()
    header(image, "02", "密码与分卷")
    text(image, (84, 123), "各有来路，也能归队。", 45, WHITE, True)
    text(image, (87, 193), "优先验证候选密码。分卷关系，清楚呈现。", 18, MUTED)
    # One fixed work surface: attention moves from saved candidates to volume slots.
    panel(image, (84, 260, 1196, 602), 23, (14, 23, 22), (63, 85, 73))
    line(image, [(640, 261), (640, 601)], (51, 69, 58), 1)
    text(image, (116, 288), "密码本", 23, WHITE, True)
    text(image, (116, 329), "保存的候选，优先尝试", 14, MUTED)
    for i in range(3):
        y = 389 + i * 53
        panel(image, (115, y - 8, 603, y + 34), 9, (20, 33, 29), (39, 60, 47))
        text(image, (133, y + 1), "● ● ● ● ● ● ● ●", 17, (137, 168, 150))
        selected = t > 1.0 and i == 0
        text(image, (552, y + 3), "已验证" if selected else "候选", 13, MINT if selected else MUTED, anchor="ra")
        if selected:
            checkmark(image, 566, y, True)
    text(image, (674, 288), "分卷识别", 23, WHITE, True)
    text(image, (674, 329), "改过名字，也可手工补齐顺序", 14, MUTED)
    for i, name in enumerate(["资料.7z.001", "课程文档.docx", "资料.7z.003"]):
        y = 389 + i * 53
        selected = t > 3.3 + i * .45
        panel(image, (674, y - 8, 1164, y + 34), 9, (20, 33, 29), (39, 60, 47))
        text(image, (691, y + 1), name, 17, (191, 211, 198))
        text(image, (1110, y + 3), f"卷 {i + 1:03d}" if selected else "待确认", 13, MINT if selected else MUTED, anchor="ra")
        if selected:
            checkmark(image, 1127, y)
    progress = smooth((t - .4) / 1.4)
    line(image, [(116, 571), (116 + 487 * progress, 571)], MINT, 2)
    if t > 4:
        line(image, [(675, 571), (675 + 488 * smooth((t - 4) / 2), 571)], MINT, 2)
    text(image, (84, 647), "密码由 Windows 加密保存 · 有缺卷或歧义时，明确提示", 14, MUTED)
    # Smooth macro push without moving controls relative to each other.
    zoom = 1 + .028 * math.sin(min(t / 7.5, 1) * math.pi)
    if zoom > 1.001:
        crop_w, crop_h = int(W / zoom), int(H / zoom)
        image = image.crop(((W - crop_w) // 2, (H - crop_h) // 2, (W + crop_w) // 2, (H + crop_h) // 2)).resize((W, H), Image.Resampling.BICUBIC)
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
    text(image, (84, 137), "解开一层。再下一层。", 47, WHITE, True)
    text(image, (87, 211), "嵌套自动展开，结果留在你选的位置。", 18, MUTED)
    # A single flowing archive hierarchy, no unrelated UI card wall.
    xs = [118, 478, 838]
    for i, x in enumerate(xs):
        enter = smooth((t - i * .75) / .9)
        node = Image.new("RGBA", (280, 260))
        panel(node, (0, 0, 279, 258), 22, (19, 34, 28), (77, 107, 86))
        if i < 2:
            icon = file_face("ZIP" if i == 0 else "7Z", True)
            place(node, icon, (140, 103), -5, .35, shadow=False)
        else:
            folder_icon(node, 96, 72)
        text(node, (140, 190), ["外层.zip", "内层.7z", "文件已就位"][i], 21, WHITE, anchor="ma")
        text(node, (140, 224), ["原包旁 · 或指定目录", "自动处理嵌套", "同名结果使用新名称"][i], 13, MUTED, anchor="ma")
        place(image, node, (x + 140, 425 + 25 * (1 - enter)), alpha=enter)
        if i < 2 and t > (i + 1) * .75:
            line(image, [(x + 298, 420), (x + 339, 420)], (103, 167, 129), 2)
            line(image, [(x + 332, 414), (x + 339, 420), (x + 332, 426)], (103, 167, 129), 2)
    if t > 3.3:
        text(image, (84, 605), "内层遇到问题？单独准备重试，不必重解外层。", 19, MINT)
    text(image, (84, 656), "原任务保留未完成记录 · 已解内容保留 · 不重复覆盖", 14, MUTED)
    return image


@lru_cache(None)
def logo():
    return Image.open(ROOT / "brand/t-unpack.png").convert("RGBA")


def final_scene(t):
    image = background().copy()
    place(image, logo(), (640, 235 - 12 * smooth(t / 3)), scale=162 / logo().width)
    text(image, (640, 356), "T解压", 59, WHITE, True, anchor="ma")
    text(image, (640, 458), "把复杂，解开。", 35, MINT, anchor="ma")
    text(image, (640, 568), "WINDOWS  /  原生轻量  /  免安装", 15, MUTED, anchor="ma")
    return image


SCENES = [(0, reveal_scene), (7.5, logic_scene), (15, result_scene), (21, final_scene)]


def frame(t):
    index = max(i for i, (start, _) in enumerate(SCENES) if t >= start)
    start, render = SCENES[index]
    image = render(t - start)
    if index and t - start < .35:
        prev_start, prev = SCENES[index - 1]
        image = Image.blend(prev(start - prev_start - .001), image, smooth((t - start) / .35))
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


def main():
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    poster = frame(5.7)
    poster.save(OUT / "hero-poster.webp", quality=88)
    cover = poster.copy().resize((1200, 675), Image.Resampling.LANCZOS).crop((0, 22, 1200, 652))
    cover.save(OUT / "cover.jpg", quality=91)
    moments = [1, 4.0, 6.9, 13.5, 16.2, 19.2, 21.8, 23.5]
    sheet = Image.new("RGB", (960, 4 * 297), (7, 9, 10))
    for index, moment in enumerate(moments):
        sample = frame(moment)
        sample.save(SCRATCH / f"frame-{moment:04.1f}.png")
        thumb = sample.resize((480, 270), Image.Resampling.LANCZOS)
        x, y = index % 2 * 480, index // 2 * 297
        sheet.paste(thumb, (x, y))
        text(sheet, (x + 10, y + 274), f"{moment:.1f}s", 13, MUTED)
    sheet.save(SCRATCH / "storyboard.jpg", quality=94)
    audio = soundtrack()
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "pipe:0", "-i", str(audio), "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", "-t", str(DURATION), str(OUT / "tunpack-film.mp4")]
    process = subprocess.Popen(command, stdin=subprocess.PIPE)
    for index in range(DURATION * FPS):
        process.stdin.write(frame(index / FPS).tobytes())
        if index % (FPS * 6) == 0:
            print(f"Rendered {index // FPS}/{DURATION}s", flush=True)
    process.stdin.close()
    if process.wait():
        raise SystemExit("ffmpeg failed")
    print(f"Original {DURATION}s film: {(OUT / 'tunpack-film.mp4').stat().st_size:,} bytes", flush=True)


if __name__ == "__main__":
    main()
