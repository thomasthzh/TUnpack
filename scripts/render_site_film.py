"""Deterministic vector motion film. Skia paths/type, no Blender or browser."""
import argparse
from functools import lru_cache
import math
from pathlib import Path
import time

import numpy as np
from PIL import Image
import skia

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/evidence/hero-motion'
FPS,DURATION=60,22
SIZES={'wide':(1600,900),'portrait':(900,1200)}
BG='#080d0b';INK='#f4f5eb';LIME='#ceff68';DIM='#91a998';LINE='#344a3d'
FONTDIR=Path('C:/Windows/Fonts')


def clamp(x):return max(0.,min(1.,x))
def ease(x):
    x=clamp(x);return x*x*(3-2*x)
def out(x):return 1-(1-clamp(x))**3
def lerp(a,b,t):return a+(b-a)*t
def between(t,a,b):return ease((t-a)/(b-a))
def color(value,alpha=1):
    rgb=tuple(int(value[i:i+2],16) for i in (1,3,5));return skia.ColorSetARGB(round(255*clamp(alpha)),*rgb)
def mix(a,b,t):
    return '#'+''.join(f'{round(lerp(int(a[i:i+2],16),int(b[i:i+2],16),t)):02x}' for i in (1,3,5))
def paint(c=INK,alpha=1,stroke=0):
    p=skia.Paint(Color=color(c,alpha),AntiAlias=True)
    if stroke:p.setStyle(skia.Paint.kStroke_Style);p.setStrokeWidth(stroke);p.setStrokeCap(skia.Paint.kRound_Cap)
    return p
@lru_cache(maxsize=160)
def font(size,bold=False,latin=False):
    name='bahnschrift.ttf' if latin else 'msyhbd.ttc' if bold else 'msyh.ttc'
    f=skia.Font(skia.Typeface.MakeFromFile(str(FONTDIR/name)),size)
    f.setSubpixel(True);return f

def text(c,s,x,y,size=30,fill=INK,alpha=1,align='left',bold=False,latin=False):
    f=font(round(size),bold,latin);w=f.measureText(s)
    if align=='center':x-=w/2
    if align=='right':x-=w
    c.drawString(s,x,y,f,paint(fill,alpha))
def line(c,x1,y1,x2,y2,fill=LINE,width=2,alpha=1):
    c.drawLine(x1,y1,x2,y2,paint(fill,alpha,width))
def rect(c,x,y,w,h,fill=LINE,stroke=0,alpha=1):
    c.drawRect(skia.Rect.MakeXYWH(x,y,w,h),paint(fill,alpha,stroke))
def circle(c,x,y,r,fill=LIME,stroke=0,alpha=1):c.drawCircle(x,y,r,paint(fill,alpha,stroke))
def curve(c,coords,fill=LINE,width=2,alpha=1):
    p=skia.Path();p.moveTo(*coords[:2]);p.cubicTo(*coords[2:]);c.drawPath(p,paint(fill,alpha,width))
def check(c,x,y,scale=1,alpha=1):
    line(c,x-16*scale,y,x-3*scale,y+13*scale,LIME,5*scale,alpha)
    line(c,x-3*scale,y+13*scale,x+25*scale,y-20*scale,LIME,5*scale,alpha)
def partial_line(c,x1,y1,x2,y2,p,fill=LIME,width=3):line(c,x1,y1,lerp(x1,x2,p),lerp(y1,y2,p),fill,width)


def camera(t,portrait):
    # One world and clock. Fast reveal, slower settle, lateral/vertical handoffs.
    phase=0
    for a,b,src,dst in [(4.05,4.7,0,1),(7.55,8.15,1,2),(10.75,11.45,2,3),(15.4,16.1,3,4)]:
        if t>=a:phase=lerp(src,dst,between(t,a,b))
    spacing=1650 if portrait else 1800
    zoom=1+.24*between(t,1.8,2.7)+.90*between(t,3.0,3.36)-1.14*between(t,3.6,4.08)
    if 11.5<t<15.4:zoom=1+.22*between(t,11.5,11.9)-.22*between(t,12.7,13.7)
    end=between(t,19.0,20.05)
    focus=lerp(phase*spacing,2*spacing,end)
    zoom=lerp(zoom,.135 if portrait else .17,end)
    return (0,focus,zoom) if portrait else (focus,0,zoom)


def ribbon(c,t,portrait):
    # The content stream is the cross-scene anchor, not an unrelated particle field.
    spacing=1650 if portrait else 1800
    for i in range(4):
        pos=i*spacing
        for j in range(5):
            offset=(j-2)*14
            if portrait:
                curve(c,(offset,pos+330,offset+80,pos+700,offset-80,pos+1000,offset,pos+spacing-330),LINE,1.5,.7)
            else:
                curve(c,(pos+480,offset,pos+850,offset+100,pos+1050,offset-100,pos+spacing-480,offset),LINE,1.5,.7)
        for j in range(14):
            travel=((t*.48+j/14)%1)
            distance=lerp(420,spacing-420,travel)
            fade=math.sin(math.pi*travel)*.7
            if portrait:line(c,0,pos+distance-12,0,pos+distance+12,LIME,3,fade)
            else:line(c,pos+distance-12,0,pos+distance+12,0,LIME,3,fade)


def facade(c,t,p):
    split=between(t,1.6,2.7);peak=between(t,2.55,2.85)*(1-between(t,3.35,3.75))
    size=210 if p else 300
    dark=mix(INK,BG,peak)
    # Compressed stripes behind the extension shell, readable only as content.
    for j in range(9):
        yy=-165+j*40
        xx=math.sin(j*.7+t*.9)*50
        line(c,-500+xx,yy,500+xx,yy,LIME,1,.10*split)
    text(c,'ZIP',0,85,size+15,mix(LIME,BG,peak),split,'center',True,True)
    for top in [True,False]:
        c.save();dy=(-1 if top else 1)*150*out(split)
        c.translate((-1 if top else 1)*60*split,dy)
        c.clipRect(skia.Rect.MakeXYWH(-900,-500 if top else 0,1800,500))
        text(c,'.MP4',0,85,size,dark,1-split,'center',True,True);c.restore()
    text(c,'旅途.mp4',0,-210 if p else -230,36 if p else 40,dark,1,'center')
    line(c,-80,-172,80,-172,mix(DIM,BG,peak),2)
    if split>.15:
        reading=1-between(t,3.0,3.25)
        text(c,'看内容，不看后缀。',0,225,42 if p else 50,dark,out((split-.15)/.7)*reading,'center',True)
        text(c,'识别为 ZIP · 原文件名保留',0,280,24 if p else 27,mix(DIM,BG,peak),split*reading,'center')
    # Small zip teeth create a graphic aperture which the first hero push enters.
    for i in range(12):
        xx=-210+i*38;yy=140
        rect(c,xx,yy,21,7,mix(LIME,BG,peak),alpha=split*.65)


def volumes(c,t,p):
    arrival=out((t-4.55)/.55);match=between(t,5.7,6.75);verified=t>=6.9
    left=-355 if p else -580;right=260 if p else 495
    title='ZIP 内，发现三卷。'
    text(c,title,0,-350 if p else -310,42 if p else 48,INK,arrival,'center',True)
    rows=[-195,-35,125] if p else [-140,20,180]
    for i,(name,yy) in enumerate(zip(['项目.7z.001','项目.7z.002','附件.docx'],rows)):
        alpha=arrival if i<2 else out((t-5.2)/.5)
        dx=(1-arrival)*420 if i<2 else (1-match)*520
        dy=0 if i<2 else (1-match)*110
        text(c,f'{i+1:02}',left,yy,45 if p else 66,LIME,arrival,latin=True)
        line(c,left+82,yy+26,right,yy+26,LINE,2,arrival)
        for j in range(24):
            xx=left+86+j*(19 if p else 33)
            line(c,xx,yy+26,xx,yy+34+(j%3)*4,LIME if verified else DIM,1,alpha*.5)
        if i==2 and match<1:
            text(c,'等待匹配',left+92,yy-15,25,DIM,arrival*(1-match))
        text(c,name,left+92+dx,yy-14+dy,31 if p else 43,INK,alpha)
        if i==2 and verified:
            text(c,'卷003',right-10,yy-17,23,LIME,1,'right')
    if p:
        for yy in rows:curve(c,(right+5,yy+26,right+100,yy+26,right+75,320,70,320),LIME if verified else LINE,2,arrival)
        text(c,'7Z',0,350,78,LIME if verified else DIM,arrival,'center',True,True)
        if verified:check(c,108,325,.7)
    else:
        for yy in rows:curve(c,(right+5,yy+26,right+100,yy+26,right+100,20,right+180,20),LIME if verified else LINE,2,arrival)
        text(c,'7Z',right+225,48,94,LIME if verified else DIM,arrival,'center',True,True)
        if verified:check(c,right+225,115,.8)
    text(c,'唯一匹配，自动归组。' if verified else '先找到关联，再确定卷序。',0,435 if p else 355,28 if p else 32,LIME if verified else DIM,arrival,'center')


def password(c,t,p):
    enter=out((t-7.9)/.55);move=between(t,8.5,9.3);verified=t>=9.45;unlock=between(t,9.62,10.35)
    text(c,'记住的密码，',0,-330 if p else -310,44 if p else 51,INK,enter,'center',True)
    text(c,'自动接着用。',0,-268 if p else -242,44 if p else 51,INK,enter,'center',True)
    text(c,'7Z',0,70,210 if p else 260,INK,enter,'center',True,True)
    # A mask candidate travels through verification; the lock moves afterwards.
    origin_y=285;yy=lerp(origin_y,108,move)
    text(c,'已存候选',0,origin_y+75,25,DIM,enter,'center')
    line(c,-110,origin_y+30,110,origin_y+30,LINE,2,enter)
    if not verified:
        for i in range(4):circle(c,(i-1.5)*22,yy,5,LIME,alpha=enter)
    circle(c,0,108,38,LINE,2,enter)
    if verified:check(c,0,105,1,1-unlock*.5)
    for side in [-1,1]:
        x=side*(110+230*unlock)
        line(c,x,-130,x,130,LIME,5,enter*(1-unlock*.65))
        line(c,x,-130,x-side*28,-130,LIME,5,enter*(1-unlock*.65))
        line(c,x,130,x-side*28,130,LIME,5,enter*(1-unlock*.65))
    if verified:
        text(c,'验证通过',0,210,30,LIME,1,'center')
        circle(c,0,0,170+300*unlock,LIME,2,(1-unlock)*.5)


def file_result(c,name,mime,x,y,amount,p):
    text(c,mime,x,y,72 if p else 88,INK,amount,'center',True,True)
    line(c,x-105,y+28,x+105,y+28,LIME,3,amount)
    text(c,name,x,y+80,27 if p else 32,INK,amount,'center')


def nested(c,t,p):
    reveal=between(t,11.45,12.3);opened=between(t,12.6,14.4);settle=between(t,14.4,15.1)
    text(c,'这组 7Z → 内层.zip',0,-370 if p else -330,32,DIM,1,'center')
    if p:
        oy=-170;ox=0;w=650;h=330
    else:oy=-25;ox=-230;w=760;h=470
    scale=1+1.4*reveal
    alpha=1-reveal
    rect(c,ox-w*scale/2,oy-h*scale/2,w*scale,h*scale,LIME,3,alpha)
    text(c,'7Z',ox-w*scale/2+30,oy-h*scale/2+65,46,LIME,alpha,latin=True)
    inner_w=w*.62;inner_h=h*.64
    rect(c,ox-inner_w/2,oy-inner_h/2,inner_w,inner_h,INK,2,1-opened*.75)
    text(c,'ZIP',ox,oy+lerp(45,20,opened),lerp(128 if p else 170,58,opened),LIME,1,'center',True,True)
    # The nested aperture splits into two output routes, not another card cut.
    for i,(name,mime) in enumerate([('照片.jpg','JPG'),('说明.txt','TXT')]):
        if p:tx=(i-.5)*330;ty=220
        else:tx=420;ty=-100+i*270
        x=lerp(ox,tx,opened);y=lerp(oy,ty,opened)
        if opened>.05:
            curve(c,(ox,oy+55,ox+70,oy+95,tx-70,ty-95,tx,ty-95),LIME,2,opened*.6)
            file_result(c,name,mime,x,y,opened,p)
    if settle:text(c,'里面还有一层，就继续解开。',0,420 if p else 365,30,INK,settle,'center')


def recycle(c,x,y,alpha=1):
    line(c,x-29,y-26,x+29,y-26,DIM,3,alpha)
    line(c,x-22,y-19,x-17,y+31,DIM,3,alpha);line(c,x+22,y-19,x+17,y+31,DIM,3,alpha)
    line(c,x-17,y+31,x+17,y+31,DIM,3,alpha)
    line(c,x-10,y-36,x+10,y-36,DIM,3,alpha)


def result(c,t,p):
    enter=out((t-15.8)/.6);verified=t>=16.8;cleanup=between(t,17.65,18.8)
    text(c,'文件，已经就位。',0,-315 if p else -285,48 if p else 64,INK,enter,'center',True)
    text(c,'旅途 /',-290 if p else -490,-180,30,DIM,enter)
    for i,(name,mime) in enumerate([('照片.jpg','JPG'),('说明.txt','TXT')]):
        x=(i-.5)*(300 if p else 430);file_result(c,name,mime,x,-30,enter,p)
    if verified:
        check(c,0,155,1.2)
        text(c,'全部校验通过',0,220,30,LIME,1,'center')
    rx=250 if p else 520;ry=340 if p else 240
    recycle(c,rx,ry,enter)
    if cleanup<.98:text(c,'旅途.mp4',rx,lerp(ry-125,ry-5,cleanup),24,DIM,enter*(1-cleanup),'center')
    text(c,'回收站',rx,ry+70,23,DIM,enter,'center')
    text(c,'回收已开启 · 没有后续任务依赖' if cleanup else '校验期间，原包保留。',0,455 if p else 370,24,DIM,enter,'center')


def render_frame(t,kind='wide'):
    p=kind=='portrait';w,h=SIZES[kind]
    surface=skia.Surface(w,h);c=surface.getCanvas()
    flash=between(t,2.55,2.85)*(1-between(t,3.35,3.75))
    c.clear(color(mix(BG,LIME,flash)))
    # Letterbox opens into a full-bleed hero on the reveal beat.
    aperture=between(t,2.45,2.95)
    c.save();margin=lerp(50,0,aperture)
    c.clipRect(skia.Rect.MakeXYWH(0,margin,w,h-margin*2))
    cam_x,cam_y,zoom=camera(t,p)
    c.save();c.translate(w/2,h/2);c.scale(zoom,zoom);c.translate(-cam_x,-cam_y)
    end=between(t,19.25,20.1)
    # Dim the route map only as the final brand arrives. Scene identities persist.
    c.saveLayerAlpha(None,round(255*(1-.82*end)))
    ribbon(c,t,p)
    spacing=1650 if p else 1800
    for i,fn in enumerate([facade,volumes,password,nested,result]):
        # Cull only out-of-view nodes, never restart their clocks at a cut.
        center=i*spacing
        if abs(center-(cam_y if p else cam_x))*zoom>max(w,h)+1100*zoom:continue
        c.save();c.translate(0 if p else center,center if p else 0);fn(c,t,p);c.restore()
    c.restore();c.restore();c.restore()
    # Screen typography has its own timing; it does not zoom with the content.
    overlay=mix(INK,BG,flash);subtle=mix(DIM,BG,flash)
    text(c,'T解压',38,56,27,overlay,bold=True)
    chapter=0 if t<4.1 else 1 if t<7.8 else 2 if t<11.1 else 3 if t<15.8 else 4
    text(c,['识别伪装','改名分卷','密码复用','自动嵌套','结果与善后'][chapter],w-38,55,22,subtle,1,'right')
    text(c,'源：旅途.mp4',38,h-45,22,subtle,1-end)
    text(c,'功能动画 · 非软件录屏',w-38,h-45,18,subtle,1,'right')
    brand=out((t-20.0)/.55)
    if brand:
        text(c,'T解压',w/2,h*.45,lerp(145 if p else 190,158 if p else 210,brand),INK,brand,'center',True)
        text(c,'看内容，不看后缀。',w/2,h*.57,42 if p else 50,LIME,brand,'center',True)
        text(c,'Windows 原生 · 3.63 MB · 免费开源',w/2,h*.66,25 if p else 29,DIM,brand,'center')
    return surface.toarray(colorType=skia.ColorType.kRGBA_8888_ColorType)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--format',choices=['wide','portrait','all'],default='all');args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    for kind in (['wide','portrait'] if args.format=='all' else [args.format]):
        for t in [0,2.9,3.3,6.9,9.9,14.8,18.8,21.5]:
            started=time.perf_counter();Image.fromarray(render_frame(t,kind)).save(OUT/f'{kind}-{t:.1f}.png');print(kind,t,round(time.perf_counter()-started,3),flush=True)

if __name__=='__main__':main()
