"""Render the TUnpack v1.0.0 cinematic Hero Shot promotional film (wide + portrait),
original 48kHz stereo sound design, responsive WebP/JPG posters, and WebVTT captions.
"""
import argparse
import base64
import json
from pathlib import Path
import subprocess
import time
import wave

import imageio_ffmpeg
import numpy as np
from PIL import Image
import playwright.sync_api as pw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "site/assets"
OUT = ROOT / "build/hero_film"
STEM = "hero-cinematic-v100"
FPS = 30
DURATION = 24.0
TOTAL_FRAMES = int(FPS * DURATION)
SIZES = {"wide": (1600, 900), "portrait": (900, 1200)}


def load_icon_data_uri() -> str:
    icon_path = ROOT / "brand/t-unpack-3d.png"
    raw = icon_path.read_bytes()
    return "data:image/png;base64," + base64.b64encode(raw).decode("ascii")


def build_html(icon_uri: str, width: int, height: int, portrait: bool) -> str:
    return f"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<style>
  * {{ box-sizing: border-box; margin: 0; padding: 0; }}
  html, body {{
    width: {width}px; height: {height}px; overflow: hidden;
    background: #06090F; color: #F4F3EE;
    font-family: "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei", system-ui, sans-serif;
    -webkit-font-smoothing: antialiased;
  }}
  #stage {{
    position: relative; width: 100%; height: 100%; overflow: hidden;
    background: radial-gradient(ellipse 85% 75% at 50% 44%, #111824 0%, #070A10 62%, #04060A 100%);
    perspective: 1500px;
  }}

  /* Studio volumetric ambient glow & sweeping light */
  #glow-orange {{
    position: absolute; inset: -20%;
    background: radial-gradient(circle at 50% 48%, rgba(238,101,49,0.28) 0%, rgba(238,101,49,0.08) 28%, rgba(238,101,49,0) 62%);
    mix-blend-mode: screen; pointer-events: none;
  }}
  #glow-cool {{
    position: absolute; inset: -20%;
    background: radial-gradient(circle at 22% 24%, rgba(120,175,230,0.16) 0%, rgba(120,175,230,0) 52%);
    mix-blend-mode: screen; pointer-events: none;
  }}
  #floor-grid {{
    position: absolute; left: -30%; right: -30%; bottom: -18%; height: 58%;
    background-image:
      linear-gradient(rgba(255,255,255,0.045) 1px, transparent 1px),
      linear-gradient(90deg, rgba(255,255,255,0.045) 1px, transparent 1px);
    background-size: 64px 64px;
    transform-origin: center top;
    transform: rotateX(74deg);
    mask-image: radial-gradient(ellipse 60% 70% at 50% 18%, black 0%, transparent 78%);
    -webkit-mask-image: radial-gradient(ellipse 60% 70% at 50% 18%, black 0%, transparent 78%);
  }}
  #flare-bar {{
    position: absolute; left: 50%; top: 47%;
    width: 1400px; height: 3px;
    transform: translate(-50%, -50%);
    background: linear-gradient(90deg, transparent 0%, rgba(238,101,49,0.0) 15%, rgba(255,155,105,0.95) 50%, rgba(238,101,49,0.0) 85%, transparent 100%);
    box-shadow: 0 0 36px 8px rgba(238,101,49,0.45), 0 0 90px 18px rgba(238,101,49,0.22);
    opacity: 0;
  }}
  canvas#particles {{
    position: absolute; inset: 0; width: 100%; height: 100%; pointer-events: none;
  }}
  #vignette {{
    position: absolute; inset: 0; pointer-events: none;
    background: radial-gradient(ellipse 75% 75% at 50% 50%, transparent 52%, rgba(2,4,8,0.68) 100%);
  }}

  /* Shared act layer */
  .act {{
    position: absolute; inset: 0;
    display: flex; flex-direction: column; align-items: center; justify-content: center;
    opacity: 0; pointer-events: none; transform-style: preserve-3d;
  }}

  /* === ACT 1 & ACT 5: MONOLITH HERO === */
  .monolith-wrap {{
    position: relative; display: flex; flex-direction: column; align-items: center;
    transform-style: preserve-3d;
  }}
  .icon-halo {{
    position: absolute; width: {340 if not portrait else 300}px; height: {340 if not portrait else 300}px;
    border-radius: 50%; top: -40px;
    background: radial-gradient(circle, rgba(238,101,49,0.34) 0%, rgba(238,101,49,0.06) 48%, transparent 72%);
    filter: blur(18px);
  }}
  .monolith-icon {{
    position: relative; width: {210 if not portrait else 196}px; height: {210 if not portrait else 196}px;
    border-radius: 44px; overflow: hidden;
    box-shadow: 0 38px 90px rgba(0,0,0,0.78), 0 0 0 1px rgba(255,255,255,0.12), 0 14px 48px rgba(238,101,49,0.26);
  }}
  .monolith-icon img {{ width: 100%; height: 100%; display: block; }}
  .monolith-sheen {{
    position: absolute; inset: -40%;
    background: linear-gradient(115deg, transparent 36%, rgba(255,255,255,0.38) 50%, transparent 64%);
    transform: translateX(-120%);
  }}
  .hero-kicker {{
    margin-top: 34px; font-size: {14 if not portrait else 15}px; letter-spacing: 0.28em;
    color: #EE6531; font-weight: 600; text-transform: uppercase;
  }}
  .hero-title {{
    margin-top: 14px; font-size: {76 if not portrait else 60}px; font-weight: 600;
    letter-spacing: -0.04em; line-height: 1.08; text-align: center;
    background: linear-gradient(180deg, #FFFFFF 0%, #D9D7CE 100%);
    -webkit-background-clip: text; -webkit-text-fill-color: transparent;
  }}
  .hero-sub {{
    margin-top: 18px; font-size: {20 if not portrait else 19}px; color: #9BA5B4;
    letter-spacing: 0.04em; text-align: center;
  }}
  .pill-row {{
    margin-top: 26px; display: flex; gap: 14px; flex-wrap: wrap; justify-content: center;
  }}
  .studio-pill {{
    padding: 8px 18px; border-radius: 999px; font-size: 14px; letter-spacing: 0.04em;
    color: #D7DBE2; background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.13); backdrop-filter: blur(12px);
  }}
  .studio-pill.orange {{
    color: #FFB28F; background: rgba(238,101,49,0.14); border-color: rgba(238,101,49,0.42);
  }}

  /* === ACT 2: X-RAY DISGUISE REVEAL === */
  .act2-stage {{
    position: relative; width: {1160 if not portrait else 780}px;
    display: flex; flex-direction: column; align-items: center;
    transform-style: preserve-3d;
  }}
  .eyebrow {{
    font-size: 14px; letter-spacing: 0.24em; color: #EE6531; font-weight: 600;
    margin-bottom: 10px; text-transform: uppercase;
  }}
  .section-h {{
    font-size: {48 if not portrait else 40}px; font-weight: 600; letter-spacing: -0.03em;
    color: #F6F5F0; text-align: center; margin-bottom: {38 if not portrait else 34}px;
  }}
  .xray-deck {{
    position: relative; width: {740 if not portrait else 680}px; height: {290 if not portrait else 300}px;
    transform-style: preserve-3d;
  }}
  .card-shell, .card-core {{
    position: absolute; inset: 0; border-radius: 26px; padding: 34px 40px;
    display: flex; flex-direction: column; justify-content: space-between;
  }}
  .card-shell {{
    background: linear-gradient(145deg, rgba(28,35,48,0.94), rgba(14,18,26,0.96));
    border: 1px solid rgba(255,255,255,0.14);
    box-shadow: 0 36px 90px rgba(0,0,0,0.72);
  }}
  .card-core {{
    background: linear-gradient(145deg, #1E1410 0%, #141114 55%, #0E131B 100%);
    border: 1.5px solid rgba(238,101,49,0.68);
    box-shadow: 0 38px 95px rgba(0,0,0,0.78), 0 0 55px rgba(238,101,49,0.22);
    clip-path: inset(0 0 100% 0);
  }}
  .file-row {{ display: flex; align-items: center; gap: 22px; }}
  .file-badge {{
    width: 72px; height: 72px; border-radius: 18px; display: flex; align-items: center; justify-content: center;
    font-size: 20px; font-weight: 700; letter-spacing: 0.04em;
  }}
  .badge-mp4 {{ background: rgba(148,163,184,0.16); color: #CBD5E1; border: 1px solid rgba(255,255,255,0.16); }}
  .badge-zip {{ background: linear-gradient(135deg, #EE6531, #C94919); color: #FFF; box-shadow: 0 10px 28px rgba(238,101,49,0.42); }}
  .file-name {{ font-size: 30px; font-weight: 600; color: #F8FAFC; }}
  .file-meta {{ font-size: 16px; color: #94A3B8; margin-top: 4px; }}
  .hex-strip {{
    font-family: Consolas, "Cascadia Mono", monospace; font-size: 14px; letter-spacing: 0.08em;
    padding: 12px 16px; border-radius: 12px; background: rgba(0,0,0,0.38); color: #8B9BB0;
    border: 1px solid rgba(255,255,255,0.08);
  }}
  .hex-strip .hi {{ color: #FF9466; font-weight: 700; }}
  .scan-laser {{
    position: absolute; left: -24px; right: -24px; height: 4px; top: 0;
    background: linear-gradient(90deg, transparent, #FF8E5B 18%, #FFFFFF 50%, #FF8E5B 82%, transparent);
    box-shadow: 0 0 28px 6px rgba(238,101,49,0.85), 0 0 70px 16px rgba(238,101,49,0.35);
    border-radius: 4px; opacity: 0; z-index: 5;
  }}
  .vol-row {{
    margin-top: 28px; display: flex; gap: 16px; justify-content: center; flex-wrap: wrap;
  }}
  .vol-shard {{
    padding: 12px 20px; border-radius: 14px;
    background: rgba(255,255,255,0.05); border: 1px solid rgba(238,101,49,0.38);
    display: flex; align-items: center; gap: 10px; font-size: 15px; color: #F1F5F9;
    box-shadow: 0 14px 32px rgba(0,0,0,0.45);
  }}
  .vol-dot {{ width: 9px; height: 9px; border-radius: 50%; background: #EE6531; box-shadow: 0 0 12px #EE6531; }}

  /* === ACT 3: 160x PASSWORD + PARALLEL LANES === */
  .act3-card {{
    width: {920 if not portrait else 740}px; border-radius: 28px; padding: {36 if not portrait else 30}px;
    background: linear-gradient(160deg, rgba(22,27,36,0.94), rgba(11,14,20,0.97));
    border: 1px solid rgba(255,255,255,0.12);
    box-shadow: 0 42px 100px rgba(0,0,0,0.78), 0 0 70px rgba(238,101,49,0.14);
    position: relative; overflow: hidden;
  }}
  .pw-bar {{
    display: flex; align-items: center; justify-content: space-between; gap: 18px;
    padding: 18px 22px; border-radius: 16px;
    background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.1);
    margin-bottom: 26px;
  }}
  .pw-left {{ display: flex; align-items: center; gap: 14px; }}
  .pw-icon {{
    width: 44px; height: 44px; border-radius: 12px; display: flex; align-items: center; justify-content: center;
    font-size: 20px; font-weight: 700;
    background: rgba(238,101,49,0.18); color: #FF9A6C; border: 1px solid rgba(238,101,49,0.45);
  }}
  .pw-icon.unlocked {{ background: rgba(22,163,74,0.22); color: #4ADE80; border-color: rgba(74,222,128,0.55); }}
  .pw-code {{ font-family: Consolas, monospace; font-size: 18px; color: #E2E8F0; }}
  .pw-badge {{
    padding: 7px 14px; border-radius: 999px; font-size: 13px; font-weight: 600;
    background: rgba(238,101,49,0.16); color: #FFA278; border: 1px solid rgba(238,101,49,0.42);
  }}
  .pw-badge.ok {{ background: rgba(22,163,74,0.2); color: #4ADE80; border-color: rgba(74,222,128,0.45); }}
  .lanes {{ display: flex; flex-direction: column; gap: 16px; }}
  .lane {{ display: grid; grid-template-columns: 112px 1fr 78px; align-items: center; gap: 16px; }}
  .lane-lbl {{ font-size: 14px; color: #94A3B8; font-weight: 600; }}
  .lane-track {{ height: 14px; border-radius: 999px; background: rgba(255,255,255,0.07); overflow: hidden; position: relative; }}
  .lane-fill {{
    height: 100%; width: 0%; border-radius: 999px;
    background: linear-gradient(90deg, #D95320 0%, #EE6531 65%, #FFB18A 100%);
    box-shadow: 0 0 18px rgba(238,101,49,0.65);
  }}
  .lane-fill.done {{ background: linear-gradient(90deg, #15803D, #22C55E, #86EFAC); box-shadow: 0 0 16px rgba(34,197,94,0.55); }}
  .lane-pct {{ font-family: Consolas, monospace; font-size: 15px; color: #E2E8F0; text-align: right; }}
  .speed-foot {{
    margin-top: 26px; padding-top: 20px; border-top: 1px solid rgba(255,255,255,0.09);
    display: flex; justify-content: space-between; align-items: baseline;
  }}
  .speed-num {{ font-size: 38px; font-weight: 700; color: #FFFFFF; letter-spacing: -0.02em; font-variant-numeric: tabular-nums; }}
  .speed-num span {{ font-size: 16px; color: #EE6531; margin-left: 6px; font-weight: 600; }}
  .speed-note {{ font-size: 14px; color: #94A3B8; }}
  #shockwave {{
    position: absolute; left: 50%; top: 50%; width: 120px; height: 120px;
    border-radius: 50%; border: 2px solid rgba(74,222,128,0.8);
    transform: translate(-50%, -50%) scale(0.2); opacity: 0; pointer-events: none;
  }}

  /* === ACT 4: FLOATING DUAL-COLUMN WORKBENCH HERO === */
  .wb-window {{
    width: {1140 if not portrait else 790}px;
    background: #F6F5F0; color: #1F2124; border-radius: 20px;
    border: 1px solid rgba(255,255,255,0.65);
    box-shadow: 0 50px 120px rgba(0,0,0,0.82), 0 0 90px rgba(238,101,49,0.22);
    overflow: hidden; transform-style: preserve-3d;
  }}
  .wb-head {{
    display: flex; align-items: center; justify-content: space-between;
    padding: 18px 24px; border-bottom: 1px solid #E5E2D9;
  }}
  .wb-brand {{ display: flex; align-items: center; gap: 12px; }}
  .wb-brand img {{ width: 38px; height: 38px; border-radius: 9px; }}
  .wb-brand-t {{ font-size: 20px; font-weight: 700; color: #1F2124; }}
  .wb-brand-s {{ font-size: 12px; color: #6C6E73; }}
  .wb-tabs {{ display: flex; gap: 8px; }}
  .wb-tab {{ padding: 7px 16px; border-radius: 9px; font-size: 13px; font-weight: 600; background: #FFF; border: 1px solid #E0DDD4; color: #1F2124; }}
  .wb-tab.active {{ background: #1F2124; color: #FFF; border-color: #1F2124; }}
  .wb-body {{
    display: grid; grid-template-columns: {'1.65fr 1fr' if not portrait else '1.35fr 1fr'};
    gap: 16px; padding: 18px 24px 20px;
  }}
  .wb-card {{
    background: #FFFFFF; border: 1px solid #E0DDD4; border-radius: 14px; padding: 14px 16px;
  }}
  .wb-drop {{
    border: 1.5px dashed #EE6531; background: #FFF3EC; border-radius: 12px;
    padding: 12px 16px; text-align: center; margin-bottom: 12px;
    font-size: 13px; font-weight: 600; color: #C94919;
  }}
  .wb-row {{
    display: grid; grid-template-columns: 1fr 78px 82px; align-items: center; gap: 10px;
    padding: 11px 10px; border-bottom: 1px solid #EFECE4; font-size: 13px;
  }}
  .wb-row:last-child {{ border-bottom: 0; }}
  .wb-row.sel {{ background: #FFF3EC; border-radius: 10px; border-left: 3px solid #EE6531; }}
  .wb-fname {{ font-weight: 600; color: #1F2124; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
  .wb-pill {{
    display: inline-block; text-align: center; padding: 3px 10px; border-radius: 999px;
    font-size: 11px; font-weight: 600;
  }}
  .wb-pill.done {{ background: #DCFCE7; color: #15803D; }}
  .wb-pill.run {{ background: #FFEDD5; color: #C2410C; }}
  .wb-mini-track {{ height: 6px; border-radius: 999px; background: #EEECE5; overflow: hidden; }}
  .wb-mini-fill {{ height: 100%; border-radius: 999px; background: #EE6531; }}
  .wb-metrics {{
    display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 12px;
  }}
  .wb-tile {{ background: #F6F5F0; border-radius: 10px; padding: 10px 12px; }}
  .wb-tile-n {{ font-size: 24px; font-weight: 700; color: #1F2124; }}
  .wb-tile-n.green {{ color: #168A4C; }}
  .wb-tile-n.orange {{ color: #EE6531; }}
  .wb-tile-l {{ font-size: 11px; color: #6C6E73; margin-top: 2px; }}
  .wb-foot {{
    display: flex; align-items: center; justify-content: space-between; gap: 20px;
    padding: 14px 24px 18px; border-top: 1px solid #E5E2D9;
  }}
  .wb-prog-wrap {{ flex: 1; }}
  .wb-prog-t {{ font-size: 12px; color: #52555A; margin-bottom: 8px; }}
  .wb-prog-track {{ height: 7px; border-radius: 999px; background: #E6E3DA; overflow: hidden; }}
  .wb-prog-fill {{ height: 100%; width: 100%; border-radius: 999px; background: #168A4C; }}
  .wb-cta {{
    padding: 10px 24px; border-radius: 11px; background: #EE6531; color: #FFF;
    font-size: 14px; font-weight: 600; box-shadow: 0 8px 20px rgba(238,101,49,0.35);
  }}

  /* Top subtle brand watermark during middle acts */
  #hud-brand {{
    position: absolute; left: 42px; top: 34px; display: flex; align-items: center; gap: 10px;
    opacity: 0; font-size: 15px; font-weight: 600; color: #D8DCE3; letter-spacing: 0.04em;
  }}
  #hud-brand img {{ width: 28px; height: 28px; border-radius: 7px; }}
  #hud-tag {{
    position: absolute; right: 42px; top: 38px; font-size: 12px; letter-spacing: 0.16em;
    color: #8892A2; text-transform: uppercase; opacity: 0;
  }}
</style>
</head>
<body>
<div id="stage">
  <div id="glow-orange"></div>
  <div id="glow-cool"></div>
  <div id="floor-grid"></div>
  <canvas id="particles" width="{width}" height="{height}"></canvas>
  <div id="flare-bar"></div>

  <div id="hud-brand"><img src="{icon_uri}" alt=""><span>T解压</span></div>
  <div id="hud-tag">TUnpack v1.0.0 · Native x64</div>

  <!-- ACT 1: HERO MONOLITH -->
  <section class="act" id="act1">
    <div class="monolith-wrap" id="a1-wrap">
      <div class="icon-halo"></div>
      <div class="monolith-icon" id="a1-icon">
        <img src="{icon_uri}" alt="T解压">
        <div class="monolith-sheen" id="a1-sheen"></div>
      </div>
      <div class="hero-kicker" id="a1-kicker">TUnpack v1.0.0 · Native Windows x64</div>
      <div class="hero-title" id="a1-title">看内容，不看后缀。</div>
      <div class="hero-sub" id="a1-sub">伪装包 · 改名分卷 · 加密嵌套 · 双层并行极速解压</div>
    </div>
  </section>

  <!-- ACT 2: X-RAY REVEAL -->
  <section class="act" id="act2">
    <div class="act2-stage" id="a2-stage">
      <div class="eyebrow">01 · CONTENT-FIRST ENGINE</div>
      <div class="section-h">一眼穿透伪装，分卷自动归位</div>
      <div class="xray-deck" id="a2-deck">
        <div class="card-shell">
          <div class="file-row">
            <div class="file-badge badge-mp4">MP4</div>
            <div>
              <div class="file-name">旅途_4K.mp4</div>
              <div class="file-meta">2.42 GB · 伪装媒体后缀 · 普通解压软件直接报错</div>
            </div>
          </div>
          <div class="hex-strip">00000000: 00 00 00 20 66 74 79 70 69 73 6F 6D ... <span class="hi">50 4B 03 04 [ZIP64]</span></div>
        </div>
        <div class="card-core" id="a2-core">
          <div class="file-row">
            <div class="file-badge badge-zip">ZIP64</div>
            <div>
              <div class="file-name">真实识别：加密多卷归档</div>
              <div class="file-meta">跳过尾部杂数据 · 只读视图直解 · 无需复制整包</div>
            </div>
          </div>
          <div class="hex-strip">DETECTED: <span class="hi">ZIP64 + AES-256 + 3 SPLIT VOLUMES</span> · SMART EXTRACT ON</div>
        </div>
        <div class="scan-laser" id="a2-laser"></div>
      </div>
      <div class="vol-row" id="a2-vols">
        <div class="vol-shard" id="v1"><span class="vol-dot"></span>part1.jpg → 卷 1</div>
        <div class="vol-shard" id="v2"><span class="vol-dot"></span>part2.txt → 卷 2</div>
        <div class="vol-shard" id="v3"><span class="vol-dot"></span>part3.dat → 末卷</div>
      </div>
    </div>
  </section>

  <!-- ACT 3: 160x PASSWORD + PARALLEL IGNITION -->
  <section class="act" id="act3">
    <div class="eyebrow">02 · 160× PROBE &amp; DUAL PARALLEL</div>
    <div class="section-h" style="margin-bottom:26px;">毫秒破密，全核并行拉满</div>
    <div class="act3-card" id="a3-card">
      <div id="shockwave"></div>
      <div class="pw-bar">
        <div class="pw-left">
          <div class="pw-icon" id="a3-lock">🔒</div>
          <div>
            <div class="pw-code" id="a3-pw">PBKDF2-HMAC-SHA1 · 预计算中间态探测…</div>
            <div style="font-size:13px;color:#8A96A8;margin-top:3px;">自动读取同目录提示 · Windows DPAPI 本机加密密码本</div>
          </div>
        </div>
        <div class="pw-badge" id="a3-badge">160× 探测中</div>
      </div>
      <div class="lanes">
        <div class="lane"><div class="lane-lbl">并行通道 01</div><div class="lane-track"><div class="lane-fill" id="l1"></div></div><div class="lane-pct" id="p1">0%</div></div>
        <div class="lane"><div class="lane-lbl">并行通道 02</div><div class="lane-track"><div class="lane-fill" id="l2"></div></div><div class="lane-pct" id="p2">0%</div></div>
        <div class="lane"><div class="lane-lbl">并行通道 03</div><div class="lane-track"><div class="lane-fill" id="l3"></div></div><div class="lane-pct" id="p3">0%</div></div>
        <div class="lane"><div class="lane-lbl">并行通道 04</div><div class="lane-track"><div class="lane-fill" id="l4"></div></div><div class="lane-pct" id="p4">0%</div></div>
      </div>
      <div class="speed-foot">
        <div class="speed-num" id="a3-speed">0 <span>MB/s</span></div>
        <div class="speed-note">单包 LPT 多核负载均衡 + 多任务并发队列 · 完整 CRC 校验</div>
      </div>
    </div>
  </section>

  <!-- ACT 4: DUAL-COLUMN WORKBENCH HERO SHOT -->
  <section class="act" id="act4">
    <div class="eyebrow" style="margin-bottom:8px;">03 · SWISS INDUSTRIAL WORKBENCH</div>
    <div class="section-h" style="font-size:{38 if not portrait else 34}px;margin-bottom:22px;">双列工作台 · 2.13 MB 原生轻量</div>
    <div class="wb-window" id="a4-win">
      <div class="wb-head">
        <div class="wb-brand">
          <img src="{icon_uri}" alt="">
          <div>
            <div class="wb-brand-t">T解压</div>
            <div class="wb-brand-s" id="wb-tag">正在并行加速，喝口水的功夫就好</div>
          </div>
        </div>
        <div class="wb-tabs">
          <div class="wb-tab active">解压队列</div>
          <div class="wb-tab">密码本</div>
          <div class="wb-tab">选项</div>
        </div>
      </div>
      <div class="wb-body">
        <div>
          <div class="wb-drop">把压缩包或文件夹拖到这里 · 任意后缀自动识别 · 智能单根解压</div>
          <div class="wb-card" style="padding:6px 10px;">
            <div class="wb-row sel">
              <div class="wb-fname">旅途_4K.mp4 (ZIP64 · 3卷)</div>
              <div><span class="wb-pill done" id="wb-r1-pill">完成</span></div>
              <div class="wb-mini-track"><div class="wb-mini-fill" id="wb-r1-bar" style="width:100%;background:#168A4C;"></div></div>
            </div>
            <div class="wb-row">
              <div class="wb-fname">设计素材_2026.7z (嵌套)</div>
              <div><span class="wb-pill done" id="wb-r2-pill">完成</span></div>
              <div class="wb-mini-track"><div class="wb-mini-fill" id="wb-r2-bar" style="width:100%;background:#168A4C;"></div></div>
            </div>
            <div class="wb-row">
              <div class="wb-fname">工程备份.part1.rar</div>
              <div><span class="wb-pill run" id="wb-r3-pill">处理中</span></div>
              <div class="wb-mini-track"><div class="wb-mini-fill" id="wb-r3-bar" style="width:62%;"></div></div>
            </div>
            <div class="wb-row">
              <div class="wb-fname">数据集_v2.tar.zst</div>
              <div><span class="wb-pill done">完成</span></div>
              <div class="wb-mini-track"><div class="wb-mini-fill" style="width:100%;background:#168A4C;"></div></div>
            </div>
          </div>
        </div>
        <div class="wb-card">
          <div style="font-size:15px;font-weight:700;color:#1F2124;">工作台概览</div>
          <div style="font-size:12px;color:#6C6E73;margin-top:3px;">智能单根输出 · 零套娃文件夹</div>
          <div class="wb-metrics">
            <div class="wb-tile"><div class="wb-tile-n green" id="wb-done">3</div><div class="wb-tile-l">已完成</div></div>
            <div class="wb-tile"><div class="wb-tile-n orange" id="wb-run">1</div><div class="wb-tile-l">处理中</div></div>
            <div class="wb-tile"><div class="wb-tile-n">0</div><div class="wb-tile-l">需处理</div></div>
            <div class="wb-tile"><div class="wb-tile-n">100%</div><div class="wb-tile-l">CRC 校验</div></div>
          </div>
          <div style="margin-top:14px;padding:10px 12px;border-radius:10px;background:#FFF3EC;font-size:12px;color:#B44012;font-weight:600;">
            ✓ 原包已安全移入回收站 · 结果就位
          </div>
        </div>
      </div>
      <div class="wb-foot">
        <div class="wb-prog-wrap">
          <div class="wb-prog-t" id="wb-foot-txt">并行处理中 · 全部校验通过</div>
          <div class="wb-prog-track"><div class="wb-prog-fill" id="wb-prog" style="width:82%;background:#EE6531;"></div></div>
        </div>
        <div class="wb-cta" id="wb-cta">打开结果</div>
      </div>
    </div>
  </section>

  <!-- ACT 5: FINALE HERO LOCKUP -->
  <section class="act" id="act5">
    <div class="monolith-wrap" id="a5-wrap">
      <div class="icon-halo" style="width:380px;height:380px;"></div>
      <div class="monolith-icon" id="a5-icon">
        <img src="{icon_uri}" alt="T解压">
        <div class="monolith-sheen" id="a5-sheen"></div>
      </div>
      <div class="hero-title" style="margin-top:26px;">T解压</div>
      <div class="hero-sub" style="margin-top:10px;color:#E5E2D8;font-size:{26 if not portrait else 23}px;">看内容，不看后缀。</div>
      <div class="pill-row">
        <div class="studio-pill orange">v1.0.0 正式发布</div>
        <div class="studio-pill">2.13 MB 原生便携</div>
        <div class="studio-pill">免费开源 · MIT</div>
      </div>
      <div style="margin-top:22px;font-size:15px;letter-spacing:0.14em;color:#7C8798;">tunpack.thomasthzh.top</div>
    </div>
  </section>

  <div id="vignette"></div>
</div>

<script>
const W = {width}, H = {height};
const clamp = (v, a=0, b=1) => Math.max(a, Math.min(b, v));
const remap = (t, a, b) => clamp((t - a) / (b - a));
const easeOutExpo = x => x >= 1 ? 1 : 1 - Math.pow(2, -10 * x);
const easeOutCubic = x => 1 - Math.pow(1 - x, 3);
const easeInOutCubic = x => x < 0.5 ? 4*x*x*x : 1 - Math.pow(-2*x + 2, 3) / 2;

// Deterministic floating studio bokeh particles
const pCanvas = document.getElementById('particles');
const pCtx = pCanvas.getContext('2d');
const pts = Array.from({{length: 44}}, (_, i) => {{
  const s = Math.sin(i * 127.1 + 31.7) * 43758.5453;
  const r1 = s - Math.floor(s);
  const s2 = Math.sin(i * 269.5 + 183.3) * 43758.5453;
  const r2 = s2 - Math.floor(s2);
  const s3 = Math.sin(i * 419.2 + 71.1) * 43758.5453;
  const r3 = s3 - Math.floor(s3);
  return {{
    x: r1 * W, y: r2 * H,
    r: 1.2 + r3 * 3.4,
    vx: (r1 - 0.5) * 18,
    vy: -10 - r3 * 22,
    orange: i % 3 === 0,
    phase: r2 * 6.28
  }};
}});

const el = id => document.getElementById(id);

function drawParticles(t) {{
  pCtx.clearRect(0, 0, W, H);
  for (const p of pts) {{
    const x = ((p.x + p.vx * t) % W + W) % W;
    const y = ((p.y + p.vy * t) % H + H) % H;
    const alpha = 0.14 + 0.24 * (0.5 + 0.5 * Math.sin(t * 1.4 + p.phase));
    pCtx.beginPath();
    pCtx.arc(x, y, p.r, 0, Math.PI * 2);
    pCtx.fillStyle = p.orange ? `rgba(238,101,49,${{alpha}})` : `rgba(220,232,248,${{alpha * 0.65}})`;
    pCtx.fill();
  }}
}}

function windowAlpha(t, inA, inB, outA, outB) {{
  const fi = easeOutCubic(remap(t, inA, inB));
  const fo = 1 - easeInOutCubic(remap(t, outA, outB));
  return clamp(fi * fo);
}}

window.renderAt = function(t) {{
  drawParticles(t);

  // Dynamic ambient studio lighting drift
  const gx = 50 + Math.sin(t * 0.45) * 10;
  const gy = 46 + Math.cos(t * 0.35) * 6;
  el('glow-orange').style.background = `radial-gradient(circle at ${{gx}}% ${{gy}}%, rgba(238,101,49,0.30) 0%, rgba(238,101,49,0.08) 30%, transparent 62%)`;
  el('floor-grid').style.transform = `rotateX(74deg) translateY(${{(t * 18) % 64}}px)`;

  // HUD watermark visible during middle acts (4.8s .. 19.6s)
  const hudOp = windowAlpha(t, 4.7, 5.4, 19.2, 19.8);
  el('hud-brand').style.opacity = hudOp;
  el('hud-tag').style.opacity = hudOp;

  // Anamorphic opening flare (0.15s .. 2.4s)
  const fIn = easeOutExpo(remap(t, 0.1, 1.0));
  const fOut = 1 - easeInOutCubic(remap(t, 1.3, 2.5));
  const flare = el('flare-bar');
  flare.style.opacity = clamp(fIn * fOut);
  flare.style.transform = `translate(-50%, -50%) scaleX(${{0.08 + 1.15 * fIn}})`;

  // === ACT 1 (0.0s .. 4.7s) ===
  const a1Op = windowAlpha(t, 0.35, 1.15, 4.1, 4.75);
  el('act1').style.opacity = a1Op;
  if (a1Op > 0.001) {{
    const p = easeOutExpo(remap(t, 0.35, 2.2));
    const exit = easeInOutCubic(remap(t, 4.1, 4.75));
    const rx = 20 * (1 - p) - 6 * exit;
    const sc = 0.80 + 0.20 * p + 0.08 * exit;
    const ty = 48 * (1 - p) - 24 * exit;
    el('a1-wrap').style.transform = `translateY(${{ty}}px) scale(${{sc}}) rotateX(${{rx}}deg)`;
    const sheenP = remap(t, 1.2, 2.6);
    el('a1-sheen').style.transform = `translateX(${{-130 + 260 * easeInOutCubic(sheenP)}}%)`;
    const tIn = easeOutCubic(remap(t, 1.0, 1.9));
    el('a1-title').style.opacity = tIn;
    el('a1-title').style.transform = `translateY(${{24 * (1 - tIn)}}px)`;
    el('a1-title').style.filter = `blur(${{(1 - tIn) * 8}}px)`;
    const sIn = easeOutCubic(remap(t, 1.5, 2.3));
    el('a1-sub').style.opacity = sIn;
    el('a1-kicker').style.opacity = easeOutCubic(remap(t, 0.8, 1.5));
  }}

  // === ACT 2 (4.6s .. 9.7s) ===
  const a2Op = windowAlpha(t, 4.6, 5.2, 9.1, 9.7);
  el('act2').style.opacity = a2Op;
  if (a2Op > 0.001) {{
    const p = easeOutExpo(remap(t, 4.6, 5.7));
    const orbit = remap(t, 4.6, 9.6);
    const ry = -16 * (1 - orbit * 0.45);
    const rx = 11 - orbit * 4;
    el('a2-deck').style.transform = `rotateX(${{rx}}deg) rotateY(${{ry}}deg) scale(${{0.92 + 0.08 * p}})`;

    // X-ray laser sweep from 5.7s to 7.4s
    const scan = easeInOutCubic(remap(t, 5.7, 7.35));
    const laserVis = windowAlpha(t, 5.55, 5.8, 7.2, 7.55);
    el('a2-laser').style.opacity = laserVis;
    el('a2-laser').style.top = `${{scan * 100}}%`;
    el('a2-core').style.clipPath = `inset(0 0 ${{(1 - scan) * 100}}% 0)`;

    // Volume shards fly in and lock (7.2s .. 8.5s)
    ['v1','v2','v3'].forEach((id, idx) => {{
      const vp = easeOutExpo(remap(t, 7.15 + idx * 0.22, 8.15 + idx * 0.22));
      const dx = (idx - 1) * 90 * (1 - vp);
      const dy = 42 * (1 - vp);
      el(id).style.opacity = vp;
      el(id).style.transform = `translate(${{dx}}px, ${{dy}}px) scale(${{0.85 + 0.15 * vp}})`;
    }});
  }}

  // === ACT 3 (9.5s .. 14.9s) ===
  const a3Op = windowAlpha(t, 9.5, 10.1, 14.3, 14.95);
  el('act3').style.opacity = a3Op;
  if (a3Op > 0.001) {{
    const orbit = remap(t, 9.5, 14.9);
    const ry = 13 * (1 - orbit * 0.65);
    const rx = 9 - orbit * 4;
    el('a3-card').style.transform = `rotateX(${{rx}}deg) rotateY(${{ry}}deg)`;

    const unlocked = t >= 11.25;
    const samples = ['Try #0142: summer2025', 'Try #0891: photo_raw', 'Try #2048: trip_4k_key', 'Try #4096: tunpack@2026'];
    if (!unlocked) {{
      const idx = Math.floor((t - 9.5) * 12) % samples.length;
      el('a3-pw').textContent = samples[idx];
      el('a3-lock').textContent = '🔒';
      el('a3-lock').className = 'pw-icon';
      el('a3-badge').textContent = '160× 探测中';
      el('a3-badge').className = 'pw-badge';
    }} else {{
      el('a3-pw').textContent = '密码验证通过 · Windows DPAPI 密码本命中';
      el('a3-lock').textContent = '✓';
      el('a3-lock').className = 'pw-icon unlocked';
      el('a3-badge').textContent = '0.04s 解锁';
      el('a3-badge').className = 'pw-badge ok';
    }}

    // Shockwave ring on unlock (11.25s .. 12.1s)
    const sw = remap(t, 11.25, 12.05);
    el('shockwave').style.opacity = sw > 0 && sw < 1 ? (1 - sw) * 0.85 : 0;
    el('shockwave').style.transform = `translate(-50%, -50%) scale(${{0.3 + sw * 6.5}})`;

    // 4 parallel lanes race after unlock (11.35s .. 13.85s)
    const speeds = [1.05, 0.96, 1.0, 0.92];
    let avg = 0;
    speeds.forEach((sp, i) => {{
      const lp = easeOutCubic(clamp(remap(t, 11.35 + i * 0.08, 13.85) * sp));
      avg += lp;
      const pct = Math.round(lp * 100);
      const bar = el('l' + (i + 1));
      bar.style.width = pct + '%';
      bar.className = pct >= 100 ? 'lane-fill done' : 'lane-fill';
      el('p' + (i + 1)).textContent = pct + '%';
    }});
    avg /= 4;
    const mbps = t < 11.35 ? 0 : Math.round(1280 * Math.sin(clamp(avg) * Math.PI * 0.5) + (avg >= 0.99 ? 0 : Math.sin(t * 20) * 18));
    el('a3-speed').innerHTML = `${{avg >= 0.995 ? '1,280' : mbps.toLocaleString()}} <span>MB/s</span>`;
  }}

  // === ACT 4 (14.7s .. 20.0s) ===
  const a4Op = windowAlpha(t, 14.7, 15.35, 19.3, 20.0);
  el('act4').style.opacity = a4Op;
  if (a4Op > 0.001) {{
    const p = easeOutExpo(remap(t, 14.7, 16.0));
    const orbit = remap(t, 14.7, 19.9);
    const rx = 10 * (1 - orbit * 0.55);
    const ry = -9 + orbit * 14;
    el('a4-win').style.transform = `translateY(${{32 * (1 - p)}}px) scale(${{0.91 + 0.09 * p}}) rotateX(${{rx}}deg) rotateY(${{ry}}deg)`;

    const fin = easeInOutCubic(remap(t, 15.4, 17.5));
    const r3Pct = Math.round(54 + 46 * fin);
    const done = r3Pct >= 100;
    el('wb-r3-bar').style.width = r3Pct + '%';
    el('wb-r3-bar').style.background = done ? '#168A4C' : '#EE6531';
    el('wb-r3-pill').textContent = done ? '完成' : '处理中';
    el('wb-r3-pill').className = done ? 'wb-pill done' : 'wb-pill run';
    el('wb-done').textContent = done ? '4' : '3';
    el('wb-run').textContent = done ? '0' : '1';
    el('wb-prog').style.width = Math.round(82 + 18 * fin) + '%';
    el('wb-prog').style.background = done ? '#168A4C' : '#EE6531';
    el('wb-tag').textContent = done ? '搞定了，干干净净' : '正在并行加速，喝口水的功夫就好';
    el('wb-foot-txt').textContent = done ? '全部 4 项已完成 · 零失败 · 双击直达文件夹' : '并行处理中 · 4 通道全速推进…';
  }}

  // === ACT 5 (19.8s .. 24.0s) ===
  const a5Op = windowAlpha(t, 19.8, 20.7, 23.65, 24.05);
  el('act5').style.opacity = a5Op;
  if (a5Op > 0.001) {{
    const p = easeOutExpo(remap(t, 19.8, 21.6));
    const sc = 0.86 + 0.14 * p;
    const rx = 14 * (1 - p);
    el('a5-wrap').style.transform = `scale(${{sc}}) rotateX(${{rx}}deg)`;
    const sheen = remap(t, 21.0, 22.6);
    el('a5-sheen').style.transform = `translateX(${{-130 + 260 * easeInOutCubic(sheen)}}%)`;
  }}
}};
</script>
</body>
</html>
"""


def build_cinematic_score(wav_path: Path) -> Path:
    """Synthesize a rich 48kHz stereo cinematic hero score (sub-bass swell, air whooshes, mechanical unlock, harmonic finale)."""
    rate = 48000
    n = int(rate * DURATION)
    t = np.arange(n) / rate
    mix_l = np.zeros(n, dtype=np.float64)
    mix_r = np.zeros(n, dtype=np.float64)
    rng = np.random.default_rng(20261126)

    # Filtered studio air noise
    raw_noise = rng.standard_normal(n)
    kernel = np.hanning(31)
    kernel /= kernel.sum()
    smooth_noise = np.convolve(raw_noise, kernel, mode="same")

    # 1. Warm cinematic obsidian sub-pad (D2=73.42, A2=110.0, D3=146.83, F#3=185.0, A3=220.0)
    pad_env = np.clip(t / 1.8, 0, 1) * np.clip((DURATION - t) / 1.8, 0, 1)
    shimmer = 0.85 + 0.15 * np.sin(2 * np.pi * 0.25 * t)
    for freq, amp, detune in [(55.0, 0.14, 0.12), (110.0, 0.07, -0.18), (164.81, 0.045, 0.22), (220.0, 0.032, -0.15)]:
        mix_l += amp * pad_env * shimmer * np.sin(2 * np.pi * (freq + detune) * t)
        mix_r += amp * pad_env * shimmer * np.sin(2 * np.pi * (freq - detune) * t)

    # 2. Deep cinematic hero booms (act transitions & monolith impact)
    for start, gain in [(0.35, 0.34), (4.65, 0.24), (9.55, 0.26), (11.25, 0.30), (14.75, 0.25), (19.85, 0.36)]:
        local = np.maximum(t - start, 0.0)
        active = t >= start
        env = np.exp(-local * 3.4) * active
        phase = 2 * np.pi * (38.0 * local + (95.0 / 12.0) * (1.0 - np.exp(-12.0 * local)))
        boom = gain * env * np.sin(phase)
        mix_l += boom
        mix_r += boom

    # 3. Anamorphic laser / camera whoosh sweeps
    for start, dur, gain in [(0.15, 0.8, 0.11), (4.5, 0.65, 0.09), (5.7, 1.5, 0.12), (9.4, 0.6, 0.09), (14.6, 0.7, 0.10), (19.7, 0.8, 0.11)]:
        local = t - start
        env = np.exp(-((local - dur * 0.45) / (dur * 0.35)) ** 2) * (local >= 0) * (local <= dur * 1.4)
        mix_l += smooth_noise * env * gain
        mix_r += np.roll(smooth_noise, 240) * env * gain

    # 4. Crisp tactile ticks during password probe + volume magnetic locks
    for start in [7.2, 7.42, 7.64, 9.9, 10.15, 10.4, 10.65, 10.9]:
        local = np.maximum(t - start, 0.0)
        env = np.exp(-local * 140.0) * (t >= start)
        tick = 0.08 * env * np.sin(2 * np.pi * 2100.0 * local) + 0.04 * env * raw_noise
        mix_l += tick
        mix_r += tick * 0.92

    # 5. Harmonic crystalline chimes (unlock @11.25s, parallel done @13.85s, workbench done @17.5s, hero finale @20.3s)
    chords = [
        (11.25, [440.0, 554.37, 659.25], 2.8, 0.065),
        (13.85, [523.25, 659.25, 783.99], 2.6, 0.060),
        (17.50, [587.33, 739.99, 880.0], 2.4, 0.055),
        (20.30, [220.0, 329.63, 440.0, 659.25], 1.25, 0.068),
    ]
    for start, freqs, decay, amp in chords:
        local = np.maximum(t - start, 0.0)
        env = np.minimum(local * 55.0, 1.0) * np.exp(-local * decay) * (t >= start)
        for i, f in enumerate(freqs):
            pan_l = 0.85 if i % 2 == 0 else 1.0
            pan_r = 1.0 if i % 2 == 0 else 0.85
            sig = amp * env * np.sin(2 * np.pi * f * local)
            mix_l += sig * pan_l
            mix_r += sig * pan_r

    # Master fade & soft limiter
    master_fade = np.clip(t / 0.06, 0, 1) * np.clip((DURATION - t) / 0.9, 0, 1)
    mix_l = np.tanh(mix_l * master_fade * 1.45) * 0.92
    mix_r = np.tanh(mix_r * master_fade * 1.45) * 0.92
    stereo = np.stack([mix_l, mix_r], axis=1)

    wav_path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(wav_path), "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes((np.clip(stereo, -1, 1) * 32767).astype("<i2").tobytes())
    return wav_path


def write_vtt() -> Path:
    vtt = """WEBVTT

00:00.400 --> 00:04.500
T解压 v1.0.0：看内容，不看后缀。

00:04.700 --> 00:09.500
一眼穿透 MP4 伪装外壳，识别 ZIP64 加密归档并自动归组改名分卷。

00:09.600 --> 00:14.800
预计算 PBKDF2 中间态 160 倍速探测密码本，随即全核双层并行解压。

00:14.900 --> 00:19.800
瑞士工业风双列工作台，智能单根解压，完整 CRC 校验并按设置回收原包。

00:19.900 --> 00:24.000
T解压 v1.0.0 · 2.13 MB 原生便携 · tunpack.thomasthzh.top
"""
    path = ASSETS / f"{STEM}.zh-CN.vtt"
    path.write_text(vtt, encoding="utf-8")
    return path


def render_variant(kind: str, icon_uri: str, audio_path: Path, ffmpeg_exe: str) -> dict:
    w, h = SIZES[kind]
    portrait = kind == "portrait"
    html_path = OUT / f"{kind}.html"
    html_path.write_text(build_html(icon_uri, w, h, portrait), encoding="utf-8")
    mp4_path = ASSETS / f"{STEM}-{kind}.mp4"
    poster_path = ASSETS / f"{STEM}-{kind}.webp"
    cover_path = ASSETS / f"{STEM}-cover.jpg"

    cmd = [
        ffmpeg_exe, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "image2pipe", "-vcodec", "png", "-r", str(FPS), "-i", "pipe:0",
        "-i", str(audio_path),
        "-t", str(DURATION),
        "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "160k", "-movflags", "+faststart",
        str(mp4_path),
    ]
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)

    # Keyframe previews for visual QA
    qa_frames = {int(1.8 * FPS), int(6.8 * FPS), int(12.6 * FPS), int(17.8 * FPS), int(22.0 * FPS)}
    poster_frame = int(22.0 * FPS)

    with pw.sync_playwright() as p:
        browser = p.chromium.launch(channel="msedge")
        page = browser.new_page(viewport={"width": w, "height": h}, device_scale_factor=1)
        page.goto(html_path.as_uri())
        page.wait_for_load_state("networkidle")

        for i in range(TOTAL_FRAMES):
            page.evaluate(f"window.renderAt({i / FPS:.6f})")
            png_bytes = page.screenshot(type="png")
            proc.stdin.write(png_bytes)

            if i in qa_frames and kind == "wide":
                (OUT / f"qa_{i:03d}.png").write_bytes(png_bytes)
            if i == poster_frame:
                tmp_png = OUT / f"poster_{kind}.png"
                tmp_png.write_bytes(png_bytes)
                img = Image.open(tmp_png).convert("RGB")
                img.save(poster_path, quality=92)
                if kind == "wide":
                    img.resize((1200, 675), Image.Resampling.LANCZOS).crop((0, 22, 1200, 652)).save(cover_path, quality=92)
            if i % 120 == 0:
                print(f"[{kind}] {i}/{TOTAL_FRAMES} ({time.time() - t0:.1f}s)", flush=True)

        browser.close()

    proc.stdin.close()
    rc = proc.wait()
    assert rc == 0, f"ffmpeg exited with {rc}"
    elapsed = time.time() - t0
    print(f"[{kind}] DONE in {elapsed:.1f}s -> {mp4_path.name} ({mp4_path.stat().st_size:,} bytes)", flush=True)
    return {
        "format": kind,
        "size": [w, h],
        "fps": FPS,
        "duration": DURATION,
        "mp4": str(mp4_path),
        "mp4_bytes": mp4_path.stat().st_size,
        "poster": str(poster_path),
        "elapsed_sec": round(elapsed, 1),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", choices=["wide", "portrait", "all"], default="all")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    icon_uri = load_icon_data_uri()
    audio_path = build_cinematic_score(OUT / "hero-cinematic-score.wav")
    write_vtt()
    ffmpeg_exe = imageio_ffmpeg.get_ffmpeg_exe()
    kinds = ["wide", "portrait"] if args.format == "all" else [args.format]
    results = [render_variant(k, icon_uri, audio_path, ffmpeg_exe) for k in kinds]
    (OUT / "report.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(results, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
