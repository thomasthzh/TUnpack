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
FRAMES = ROOT / "docs/evidence/hero-product"
ASSETS = ROOT / "site/assets"
STEM = "hero-story-20261117"


def score():
    rate, duration = 48000, 20
    t = np.arange(rate * duration) / rate
    mix = np.zeros_like(t)
    # Original synthesis: a quiet material resonance, opening impact, final resolve.
    for start, frequencies in [(0, [55, 82.4069]), (4.8, [55, 110, 164.8138]), (8, [65.4064, 98, 130.8128]), (11, [55, 110]), (16.5, [65.4064, 98, 130.8128])]:
        local = t - start
        envelope = np.maximum(0, np.minimum(local * 18, 1)) * np.exp(-np.maximum(local, 0) / 2.7)
        envelope[local < 0] = 0
        for frequency in frequencies:
            mix += .045 * envelope * np.sin(2 * np.pi * frequency * local)
    for start, frequency in [(2.7, 523.25), (7.0, 659.25), (9.8, 783.99), (14.5, 523.25), (17.5, 659.25)]:
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


def copy_at(t):
    if t<4.8:
        return ('01  /  识别伪装','名字是 MP4。\n内容却是 ZIP。','拖入队列：旅途.mp4\n正在识别内容' if t<2.7 else '认出 ZIP 内容\n原文件名保留')
    if t<8:
        return ('02  /  分卷归组','改名的分卷，\n也能归成一组。','ZIP 内发现三卷' if t<7 else '附件.docx：卷003\n此示例可唯一匹配')
    if t<11:
        return ('03  /  复用密码','用已存密码，\n验证后再解开。','来源：你保存的候选' if t<9.7 else '验证通过 · 自动接着解压')
    if t<16:
        return ('04  /  自动嵌套','里面还有一层？\n继续解开。','这组 7Z → 内层.zip' if t<13.8 else '内层.zip → 文件')
    return ('05  /  结果与善后','文件已就位。\n原包按设置回收。','校验中 · 原包保留' if t<16.5 else '全部校验通过' if t<17.4 else '回收已开启\n没有后续任务依赖')


def font(size):
    return ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',size)


def composite(image,kind,t):
    portrait=kind=='portrait'
    canvas=Image.new('RGB',(720,1100) if portrait else (1536,720),(5,6,7))
    if portrait:
        # Fade the studio horizon into the page; keep the actors fully visible.
        ramp=np.concatenate([np.linspace(0,255,130),np.full(770,255)]).astype('uint8')
        canvas.paste(image,(0,200),Image.fromarray(np.tile(ramp[:,None],(1,720))))
    else:
        ramp=np.concatenate([np.linspace(0,255,96),np.full(1056,255)]).astype('uint8')
        canvas.paste(image,(384,0),Image.fromarray(np.tile(ramp,(720,1))))
    chapter,title,status=copy_at(t)
    draw=ImageDraw.Draw(canvas)
    if portrait:
        draw.text((38,20),'T解压  /  '+chapter,font=font(20),fill=(145,175,155))
        draw.multiline_text((38,62),title,font=font(38),fill=(238,243,234),spacing=10)
        draw.multiline_text((40,172),status,font=font(24),fill=(167,207,177),spacing=7)
        draw.text((40,238),'功能演绎 · 非实际录屏',font=font(14),fill=(117,139,125))
    else:
        draw.text((38,42),'T解压',font=font(23),fill=(221,233,222))
        draw.text((40,182),chapter,font=font(16),fill=(145,175,155))
        draw.multiline_text((36,243),title,font=font(46),fill=(238,243,234),spacing=16)
        draw.multiline_text((40,460),status,font=font(21),fill=(167,207,177),spacing=12)
        draw.text((40,668),'功能演绎 · 非实际录屏',font=font(14),fill=(117,139,125))
    return canvas


def posters(kind):
    # A front-loaded feature frame: the MP4 name and true ZIP content are both visible.
    image=Image.open(FRAMES/kind/'0084.png').convert('RGB')
    image=composite(image,kind,83/24)
    image.save(ASSETS/(STEM+'-'+kind+'.webp'),quality=90)
    if kind=='wide':
        cover=Image.new('RGB',(1200,630),(5,6,7))
        cover.paste(image.resize((1200,562),Image.Resampling.LANCZOS),(0,34))
        cover.save(ASSETS/(STEM+'-cover.jpg'),quality=93)


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
        assert all((source / f"{index:04d}.png").exists() for index in range(1, 481)), kind + " render incomplete"
        target = ASSETS / (STEM + "-" + kind + ".mp4")
        width,height=(720,1100) if kind=='portrait' else (1536,720)
        command=[ffmpeg,'-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{width}x{height}','-r','24','-i','pipe:0','-i',str(audio),'-t','20','-c:v','libx264','-preset','slow','-crf','19','-pix_fmt','yuv420p','-c:a','aac','-b:a','128k','-movflags','+faststart',str(target)]
        process=subprocess.Popen(command,stdin=subprocess.PIPE)
        for index in range(1,481):
            with Image.open(source/f'{index:04d}.png') as image:
                process.stdin.write(composite(image.convert('RGB'),kind,(index-1)/24).tobytes())
        process.stdin.close()
        assert process.wait()==0
        probe = subprocess.run([ffmpeg, "-hide_banner", "-i", str(target), "-f", "null", "-"], capture_output=True, check=True)
        details = probe.stderr.decode("utf8", errors="replace")
        assert "00:00:20.00" in details and "h264" in details and "aac" in details
        stats=json.loads((FRAMES/f'{kind}-render-stats.json').read_text(encoding='utf8'))
        report.append({"format": kind, "source": "Blender 5.3 Cycles / OptiX, original mesh and PBR materials", "sequence_frames": 480, "render_fps": 24, "duration_seconds": 20, "cycles_frames_rendered": stats['start']-1+stats['unique_rendered_poses'], "held_frames_reused": stats['held_frames_reused'], "render_size": [1152, 720] if kind == "wide" else [720, 900], "delivery_size": [1536, 720] if kind == "wide" else [720, 1100], "mp4_bytes": target.stat().st_size, "full_h264_aac_decode": True})
    if report:
        receipt = FRAMES / "media.json"
        previous = json.loads(receipt.read_text(encoding="utf8")) if receipt.exists() else []
        combined = {item["format"]: item for item in previous + report}
        receipt.write_text(json.dumps(list(combined.values()), ensure_ascii=False, indent=2), encoding="utf8")
        print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
