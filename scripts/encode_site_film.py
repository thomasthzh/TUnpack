"""Encode the actual Cycles frame sequence and original score for the website."""
import argparse
import json
from pathlib import Path
import subprocess
import wave

import imageio_ffmpeg
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FRAMES = ROOT / "docs/evidence/hero-cinema"
ASSETS = ROOT / "site/assets"
STEM = "hero-3d-20261117"


def score():
    rate, duration = 48000, 12
    t = np.arange(rate * duration) / rate
    mix = np.zeros_like(t)
    # Original synthesis: a quiet material resonance, opening impact, final resolve.
    for start, frequencies in [(0, [55, 82.4069]), (3.2, [55, 110, 164.8138]), (8.2, [65.4064, 98, 130.8128])]:
        local = t - start
        envelope = np.maximum(0, np.minimum(local * 18, 1)) * np.exp(-np.maximum(local, 0) / 2.7)
        envelope[local < 0] = 0
        for frequency in frequencies:
            mix += .045 * envelope * np.sin(2 * np.pi * frequency * local)
    for start, frequency in [(3.9, 523.25), (4.8, 659.25), (5.8, 783.99), (8.6, 523.25)]:
        local = t - start
        envelope = np.maximum(0, np.minimum(local * 30, 1)) * np.exp(-np.maximum(local, 0) * 3)
        envelope[local < 0] = 0
        mix += .018 * envelope * np.sin(2 * np.pi * frequency * local + .18 * np.sin(2 * np.pi * frequency * 2.001 * local))
    mix *= np.minimum(t / .8, 1) * np.minimum((duration - t) / 1.3, 1)
    audio = np.stack([mix, .96 * mix], axis=1)
    path = FRAMES / "original-hero-score.wav"
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes((np.clip(audio, -1, 1) * 32767).astype("<i2").tobytes())
    return path


def wide_composite(image):
    canvas = Image.new("RGB", (1920, 900), (5, 6, 7))
    # Blend the studio's edge into the reserved dark field instead of a hard seam.
    ramp = np.concatenate([np.linspace(0, 255, 180), np.full(1260, 255)]).astype("uint8")
    mask = Image.fromarray(np.tile(ramp, (900, 1)))
    canvas.paste(image, (480, 0), mask)
    return canvas


def posters(kind):
    image = Image.open(FRAMES / kind / "0268.png").convert("RGB")
    if kind == "wide":
        # Deliberate dark typography field; the 3D subject retains its perspective.
        image = wide_composite(image)
        cover = Image.new("RGB", (1200, 630), (5, 6, 7))
        cover.paste(image.resize((1200, 562), Image.Resampling.LANCZOS), (0, 34))
        draw = ImageDraw.Draw(cover)
        draw.text((63, 210), "T解压", font=ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 76), fill=(236, 239, 231))
        draw.text((66, 323), "解开，不止一层。", font=ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 24), fill=(172, 192, 177))
        cover.save(ASSETS / (STEM + "-cover.jpg"), quality=93)
    image.save(ASSETS / (STEM + "-" + kind + ".webp"), quality=90)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--posters-only", action="store_true")
    parser.add_argument("--format", choices=["wide", "portrait", "all"], default="all")
    args = parser.parse_args()
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    audio = score()
    report = []
    for kind in (["wide", "portrait"] if args.format == "all" else [args.format]):
        posters(kind)
        if args.posters_only:
            continue
        source = FRAMES / kind
        assert all((source / f"{index:04d}.png").exists() for index in range(1, 289)), kind + " render incomplete"
        target = ASSETS / (STEM + "-" + kind + ".mp4")
        command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y"]
        if kind == "wide":
            command += ["-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1920x900", "-r", "24", "-i", "pipe:0"]
        else:
            command += ["-framerate", "24", "-start_number", "1", "-i", str(source / "%04d.png")]
        command += ["-i", str(audio)]
        command += ["-t", "12", "-c:v", "libx264", "-preset", "slow", "-crf", "19", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", str(target)]
        if kind == "wide":
            process = subprocess.Popen(command, stdin=subprocess.PIPE)
            for index in range(1, 289):
                with Image.open(source / f"{index:04d}.png") as image:
                    process.stdin.write(wide_composite(image.convert("RGB")).tobytes())
            process.stdin.close()
            assert process.wait() == 0
        else:
            subprocess.run(command, check=True)
        probe = subprocess.run([ffmpeg, "-hide_banner", "-i", str(target), "-f", "null", "-"], capture_output=True, check=True)
        details = probe.stderr.decode("utf8", errors="replace")
        assert "00:00:12.00" in details and "h264" in details and "aac" in details
        report.append({"format": kind, "source": "Blender 5.3 Cycles / OptiX, original mesh and PBR materials", "render_frames": 288, "render_fps": 24, "duration_seconds": 12, "render_size": [1440, 900] if kind == "wide" else [864, 1080], "delivery_size": [1920, 900] if kind == "wide" else [864, 1080], "mp4_bytes": target.stat().st_size, "full_h264_aac_decode": True})
    if report:
        receipt = FRAMES / "media.json"
        previous = json.loads(receipt.read_text(encoding="utf8")) if receipt.exists() else []
        combined = {item["format"]: item for item in previous + report}
        receipt.write_text(json.dumps(list(combined.values()), ensure_ascii=False, indent=2), encoding="utf8")
        print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
