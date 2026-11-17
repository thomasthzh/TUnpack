"""Icon-based product motion on one workbench. No Blender or GUI/browser."""
import argparse
from functools import lru_cache
import math
from pathlib import Path
import re
import time

from PIL import Image
import skia

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/evidence/hero-icons'
ICONS=ROOT/'brand/film-icons/fluent'
FPS,DURATION=60,28
SIZES={'wide':(1600,900),'portrait':(900,1200)}
INK='#18303f';MUTED='#738798';TEAL='#008c83';LINE='#dce6ed'
COLORS={'video_clip':('#56c3f6','#3274df'),'archive':('#38c9b6','#087c94'),'folder_zip':('#51cdb7','#107c93'),'folder':('#ffc65e','#e89528'),'document':('#8c9cf7','#5e62cc'),'document_text':('#a696f7','#7660ce'),'book':('#c0a6fa','#8362d0'),'lock_closed':('#ffd681','#e0a23d'),'lock_open':('#76d4b1','#29947d'),'key':('#ffd36f','#dda02e'),'image':('#65d4c4','#238d99'),'music_note_2':('#ec9fce','#bd5fa9'),'delete':('#b0c2d0','#6d879b'),'checkmark_circle':('#54ccb0','#189b83')}


def clamp(v):return max(0.,min(1.,v))
def ease(v):
 v=clamp(v);return v*v*(3-2*v)
def out(v):return 1-(1-clamp(v))**3
def lerp(a,b,v):return a+(b-a)*v
def at(t,a,b):return ease((t-a)/(b-a))
def color(s,a=1):return skia.ColorSetARGB(round(255*clamp(a)),*[int(s[i:i+2],16) for i in (1,3,5)])
def paint(s=INK,a=1,stroke=0):
 p=skia.Paint(Color=color(s,a),AntiAlias=True)
 if stroke:p.setStyle(skia.Paint.kStroke_Style);p.setStrokeWidth(stroke);p.setStrokeCap(skia.Paint.kRound_Cap)
 return p
@lru_cache(maxsize=80)
def font(n,bold=False):return skia.Font(skia.Typeface.MakeFromFile('C:/Windows/Fonts/'+('msyhbd.ttc' if bold else 'msyh.ttc')),n)
def text(c,s,x,y,n=25,fill=INK,a=1,align='center',bold=False):
 f=font(round(n),bold);w=f.measureText(s)
 if align=='center':x-=w/2
 elif align=='right':x-=w
 c.drawString(s,x,y,f,paint(fill,a))
def box(c,x,y,w,h,fill='#ffffff',r=20,a=1,stroke=0):c.drawRoundRect(skia.Rect.MakeXYWH(x,y,w,h),r,r,paint(fill,a,stroke))
def line(c,x1,y1,x2,y2,fill=LINE,w=2,a=1):c.drawLine(x1,y1,x2,y2,paint(fill,a,w))
def path(c,pts,fill=TEAL,w=2,a=1,closed=False):
 q=skia.Path();q.moveTo(*pts[0])
 for xy in pts[1:]:q.lineTo(*xy)
 if closed:q.close()
 c.drawPath(q,paint(fill,a,w))
def check(c,x,y,s=1,a=1):path(c,[(x-15*s,y),(x-3*s,y+12*s),(x+23*s,y-19*s)],TEAL,4*s,a)
def curve(c,pts,fill=LINE,w=2,a=1):
 q=skia.Path();q.moveTo(*pts[:2]);q.cubicTo(*pts[2:]);c.drawPath(q,paint(fill,a,w))
def point(pts,q):
 x1,y1,x2,y2,x3,y3,x4,y4=pts;v=1-q
 return(v**3*x1+3*v*v*q*x2+3*v*q*q*x3+q**3*x4,v**3*y1+3*v*v*q*y2+3*v*q*q*y3+q**3*y4)

@lru_cache(maxsize=70)
def svg(name,ink='#ffffff'):
 raw=(ICONS/(name+'.svg')).read_text(encoding='utf-8-sig')
 raw=re.sub(r'fill="#[0-9a-fA-F]{6}"',f'fill="{ink}"',raw)
 dom=skia.SVGDOM.MakeFromStream(skia.MemoryStream(raw.encode()))
 dom.setContainerSize(skia.Size(24,24));return dom

def glyph(c,name,x,y,size,ink='#ffffff',a=1):
 if a<1:c.saveLayerAlpha(None,round(255*clamp(a)))
 else:c.save()
 c.translate(x-size/2,y-size/2);c.scale(size/24,size/24);svg(name,ink).render(c);c.restore()

@lru_cache(maxsize=30)
def tile(name):
 recording=skia.PictureRecorder();c=recording.beginRecording(skia.Rect.MakeWH(512,512))
 p=paint('#36566c',.15);p.setMaskFilter(skia.MaskFilter.MakeBlur(skia.BlurStyle.kNormal_BlurStyle,17))
 c.drawRoundRect(skia.Rect.MakeXYWH(48,49,416,416),96,96,p)
 top,bottom=COLORS.get(name,('#8da8bb','#4c6d89'))
 p=skia.Paint(AntiAlias=True);p.setShader(skia.GradientShader.MakeLinear([(72,24),(424,454)],[color(top),color(bottom)]))
 c.drawRoundRect(skia.Rect.MakeXYWH(48,28,416,416),94,94,p)
 c.drawRoundRect(skia.Rect.MakeXYWH(50,30,412,412),92,92,paint('#ffffff',.26,2))
 glyph(c,name,256,234,282)
 return recording.finishRecordingAsPicture()

@lru_cache(maxsize=1)
def brand():
 recording=skia.PictureRecorder();c=recording.beginRecording(skia.Rect.MakeWH(512,512))
 raw=(ROOT/'brand/t-unpack.svg').read_bytes();dom=skia.SVGDOM.MakeFromStream(skia.MemoryStream(raw));dom.setContainerSize(skia.Size(256,256));c.scale(2,2);dom.render(c);return recording.finishRecordingAsPicture()
def icon(c,name,x,y,size,angle=0,a=1,badge=None):
 if a<=0:return
 c.save();c.translate(x,y);c.rotate(angle)
 if a<1:c.saveLayerAlpha(skia.Rect.MakeXYWH(-size*.7,-size*.65,size*1.4,size*1.4),round(a*255))
 c.save();c.translate(-size/2,-size/2);c.scale(size/512,size/512)
 c.drawPicture(brand() if name=='brand' else tile(name));c.restore()
 if badge:
  ww=max(52,len(badge)*13+20)
  box(c,-ww/2,size*.32,ww,30,'#ffffff',10)
  text(c,badge,0,size*.32+22,17,INK,bold=True)
 if a<1:c.restore()
 c.restore()

def stamp(c,x,y,n=1):
 c.drawCircle(x,y,20*n,paint('#ffffff'));glyph(c,'checkmark_circle',x,y,34*n,TEAL)


def surface_rect(t,p):
 q=at(t,3.6,4.12)
 return (lerp(28 if p else 84,0,q),lerp(120 if p else 110,0,q),lerp(844 if p else 1432,900 if p else 1600,q),lerp(970 if p else 680,1200 if p else 900,q))
def start_pos(t,p):
 x,y,w,h=surface_rect(t,p);return(x+w-104,y+47)
def source_pos(p):return(235,470) if p else (360,450)
def drop_pos(p):return(450,620) if p else (870,450)
def result_pos(p):return(450,490) if p else (800,380)
def recycle_pos(p):return(740,920) if p else (1390,425)

def pointer_state(t,p):
 w,h=SIZES['portrait' if p else 'wide'];sx,sy=source_pos(p);dx,dy=drop_pos(p);bx,by=start_pos(3.6,p)
 if t<1.22:
  pts=(w+35,h*.84,w*.8,h*.7,sx-80,sy+90,sx,sy);q=at(t,0,1.22);xy=point(pts,q)
 elif t<1.38:xy=(sx,sy)
 elif t<2.62:xy=point((sx,sy,sx+90,sy-110,dx-90,dy-70,dx,dy),at(t,1.38,2.62))
 elif t<3.5:xy=point((dx,dy,dx+180,dy-90,bx-100,by+50,bx,by),at(t,2.72,3.5))
 elif t<3.68:xy=(bx,by)
 elif t<4.3:xy=point((bx,by,w-20,by+60,w+30,h*.72,w+35,h*.84),at(t,3.68,4.3))
 elif t<20.55:return(w+35,h*.84,False,False)
 elif t<21.2:
  rx,ry=result_pos(p);xy=point((w+35,h*.84,w*.88,h*.60,rx+110,ry+60,rx,ry),at(t,20.55,21.2))
 elif t<21.38:xy=result_pos(p)
 else:
  rx,ry=result_pos(p);xy=point((rx,ry,rx-80,ry+130,w-60,h+30,w+35,h+35),at(t,21.38,22.3))
 down=(1.24<=t<1.38) or (3.50<=t<3.65) or (21.2<=t<21.36)
 return(*xy,True,down)

def cursor(c,t,p):
 x,y,visible,down=pointer_state(t,p)
 if not visible:return
 c.save();c.translate(x,y)
 q=skia.Path();q.moveTo(0,0);q.lineTo(1,30);q.lineTo(8,23);q.lineTo(16,40);q.lineTo(24,36);q.lineTo(16,19);q.lineTo(29,19);q.close()
 c.drawPath(q,paint('#ffffff'));c.drawPath(q,paint('#223d4f',1,1.8))
 if down:c.drawCircle(2,2,17,paint(TEAL,.38,2))
 c.restore()


def camera(t,p):
 w,h=SIZES['portrait' if p else 'wide'];z=1.;fx=w/2;fy=h*.49
 if 4.2<=t<7:
  push=at(t,4.2,4.56);back=at(t,6.2,7)
  z=1+.4*push-.3*at(t,4.6,6.2)-.1*back
  fx=lerp(w*.5,w*.58,push*(1-back))
 elif 7.7<=t<10.6:
  z=1+.22*at(t,7.7,8.1)-.22*at(t,9.7,10.5)
 elif 11.9<=t<14:
  z=1+.22*at(t,11.9,12.25)-.22*at(t,13.1,14)
 elif 14.6<=t<18.5:
  z=1+.32*at(t,14.6,14.96)-.32*at(t,16.5,18.2)
 return(fx,fy,z)

def chrome(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];c.clear(color('#edf3f7'))
 x,y,ww,hh=surface_rect(t,p)
 box(c,x,y+5,ww,hh,'#b8cad7',30,a=.18)
 box(c,x,y,ww,hh,'#ffffff',28*(1-at(t,3.6,4.12)))
 icon(c,'brand',x+53,y+47,42)
 text(c,'T解压',x+88,y+57,26,INK,align='left',bold=True)
 bx,by=start_pos(t,p)
 label='开始全部' if t<3.6 else '处理中' if t<19.4 else '已完成'
 box(c,bx-63,by-23,126,46,TEAL if t<3.6 else '#edf7f5',14)
 if t<3.6:glyph(c,'play',bx-42,by,18)
 text(c,label,bx+12 if t<3.6 else bx,by+8,20,'#ffffff' if t<3.6 else TEAL)
 if t>=2.62:
  rowy=y+hh-78
  box(c,x+22,rowy-32,ww-44,66,'#f4f8fb',16)
  # The input identity stays in the queue while previews show derived content.
  text(c,'旅途.mp4',x+94,rowy+8,22,INK,align='left')
  states=['待开始','识别内容','分卷归组','验证已存候选','展开内层.zip','全部校验通过']
  i=0 if t<3.6 else 1 if t<7 else 2 if t<10.6 else 3 if t<14 else 4 if t<19.4 else 5
  text(c,states[i],x+ww-40,rowy+8,20,TEAL if i==5 else MUTED,align='right')
  if t>=19.4:stamp(c,x+ww-42,rowy-18,.55)
 # This is an illustration of controls, not a synthetic recording of the native app.
 text(c,'功能动画 · 非软件录屏',w-30,h-17,16,MUTED,align='right')


def drag(c,t,p):
 sx,sy=source_pos(p);dx,dy=drop_pos(p)
 dw=560 if p else 500
 box(c,dx-dw/2,dy-115,dw,230,'#f1faf8',24)
 c.drawRoundRect(skia.Rect.MakeXYWH(dx-dw/2,dy-115,dw,230),24,24,paint('#acd9d0',1,2))
 hint=1-at(t,1.9,2.35)
 glyph(c,'arrow_down',dx,dy-10,44,TEAL,.35*hint)
 text(c,'拖入文件，加入队列',dx,dy+69,27,TEAL,a=hint)
 q=at(t,1.38,2.62);fx,fy=point((sx,sy,sx+90,sy-110,dx-90,dy-70,dx,dy),q)
 join=at(t,2.62,3.0);xx,yy,ww,hh=surface_rect(t,p)
 if t<3.0:
  icon(c,'video_clip',lerp(fx,xx+58,join),lerp(fy,yy+hh-78,join),lerp(150 if p else 170,44,join),badge='MP4' if join<.4 else None)
 text(c,'旅途.mp4',fx,fy+110,25,INK,a=1-join)
 if t>=2.62:text(c,'已加入，等待开始',dx,dy-150,27,TEAL)


def identify(c,t,p):
 w,h=SIZES['portrait' if p else 'wide']
 if p:ax,ay=w*.30,h*.40;bx,by=w*.67,h*.53;size=210
 else:ax,ay=520,410;bx,by=1050,410;size=300
 icon(c,'video_clip',ax,ay,size,badge='MP4')
 text(c,'旅途.mp4',ax,ay+size*.58,27,INK)
 text(c,'文件名保留',ax,ay+size*.58+38,22,MUTED)
 reveal=at(t,4.3,5.4)
 if reveal:
  ex=lerp(ax,bx,reveal);ey=lerp(ay,by,reveal)
  curve(c,(ax+size*.35,ay,bx-size*.7,ay,bx-size*.6,by,bx-size*.42,by),'#b8dcd4',3)
  icon(c,'archive',ex,ey,size*.94,angle=lerp(-12,0,reveal),badge='ZIP')
  if reveal>.85:
   stamp(c,bx+size*.32,by-size*.30,.85)
   text(c,'内容：ZIP',bx,by+size*.58,29,TEAL)
 # Scan and recognition have their own clock; source extension never morphs.
 if t<5.0:
  xx=lerp(ax-size*.30,ax+size*.30,at(t,3.8,4.8))
  line(c,xx,ay-size*.31,xx,ay+size*.31,'#85ddcb',3,.75)


def volumes(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];join=at(t,8.35,9.6)
 centers=[(w*.25,h*.42),(w*.50,h*.42),(w*.75,h*.58)] if p else [(420,420),(800,420),(1180,560)]
 for i in range(3):
  sx,sy=centers[i];tx=(i-1)*(96 if p else 135)+w*.5;ty=h*.44+abs(i-1)*10
  x=lerp(sx,tx,join);y=lerp(sy,ty,join)
  name='document_text' if i==2 else 'archive';label=['001','002','DOCX'][i]
  icon(c,name,x,y,170 if p else 225,angle=(i-1)*lerp(8,0,join),badge=label)
  if join<.8:text(c,['项目.7z.001','项目.7z.002','附件.docx'][i],x,y+140 if p else y+160,22,INK,a=1-join)
 if t>=9.65:
  stamp(c,w*.5,h*.44+175,.85)
  text(c,'附件.docx → 卷003',w*.5,h*.44+240,28,INK)
  text(c,'唯一匹配，归为一组',w*.5,h*.44+281,23,TEAL)
 else:text(c,'ZIP 中的三个分卷',w*.5,h*.24,29,INK)


def password(c,t,p):
 w,h=SIZES['portrait' if p else 'wide']
 if p:bx,by=245,710;ax,ay=540,470
 else:bx,by=430,480;ax,ay=1040,420
 size=220 if p else 310
 icon(c,'book',bx,by,165 if p else 215)
 text(c,'已存候选',bx,by+130,25,INK)
 box(c,bx-65,by+145,130,38,'#f3effb',12);text(c,'••••',bx,by+173,24,'#7e65b4')
 icon(c,'archive',ax,ay,size,badge='7Z')
 unlocked=t>=12.95;lx=ax+size*.28;ly=ay+size*.09
 icon(c,'lock_open' if unlocked else 'lock_closed',lx,ly,78 if p else 96)
 pts=(bx+50,by-35,bx+170,by-180,ax-100,ay-130,lx-28,ly)
 q=at(t,11.15,12.7)
 if q>0:
  curve(c,pts,'#e7dcc2',3)
  kx,ky=point(pts,q);withdraw=at(t,12.95,13.4)
  icon(c,'key',kx-120*withdraw,ky+45*withdraw,lerp(76 if p else 92,55,withdraw),angle=lerp(-18,0,q))
 if t>=12.75:
  stamp(c,ax+size*.35,ay-size*.28,.85)
  text(c,'验证通过',ax,ay+size*.60,28,TEAL)


def nested(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];reveal=at(t,14.1,15.1);passage=at(t,15.1,16.25)
 if p:ax,ay=280,400;ix,iy=560,590
 else:ax,ay=390,430;ix,iy=850,430
 icon(c,'archive',ax,ay,250 if p else 300,badge='7Z')
 text(c,'外层卷组',ax,ay+165 if p else ay+190,23,MUTED)
 x=lerp(ax,ix,reveal);y=lerp(ay,iy,reveal)
 # Content emerges behind the same nested archive icon as it passes the frame.
 for i,name in enumerate(['image','document','music_note_2']):
  q=at(t,15.35+i*.17,16.8+i*.12)
  if q<=0:continue
  if p:tx=(i-1)*230+450;ty=670+(i%2)*100
  else:tx=1180;ty=260+i*170
  px=lerp(ix,tx,q);py=lerp(iy,ty,q)-math.sin(q*math.pi)*55
  icon(c,name,px,py,lerp(50,145 if p else 160,q),angle=(i-1)*4*(1-q))
 fx=lerp(x,ix if p else -850,passage);fy=lerp(y,-950 if p else 160,passage)
 icon(c,'folder_zip',fx,fy,lerp(lerp(85,220 if p else 260,reveal),900,passage),angle=-14*passage,badge='ZIP')
 if reveal>.8 and passage<.15:text(c,'内层.zip',ix,iy+165,25,TEAL)
 if t>=17.2:text(c,'还有内层，就接着解开。',w*.5,h*.78,29,INK)


def result(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];fx,fy=result_pos(p);opened=at(t,21.2,21.6)
 if opened:
  pw=760 if p else 940
  box(c,fx-pw/2,fy-155,pw,325,'#eef8f5',24,a=opened)
  text(c,'解压结果',fx,fy-98,27,TEAL,a=opened,bold=True)
 icon(c,'folder',lerp(fx,fx-280 if p else fx-375,opened),lerp(fy,fy-115,opened),lerp(200 if p else 245,65,opened))
 text(c,'输出目录',fx,fy+145 if p else fy+170,26,INK,a=max(0,1-opened*4))
 for i,name in enumerate(['image','document','music_note_2']):
  if p:tx=(i-1)*225+450;ty=780
  else:tx=560+i*240;ty=625
  tx=lerp(tx,fx+(i-1)*(225 if p else 245),opened);ty=lerp(ty,fy+25,opened)
  q=at(t,18.7+i*.12,19.35+i*.10)
  icon(c,name,lerp(fx,tx,q),lerp(fy,ty,q),lerp(55,150 if p else 170,q))
  if t>=19.65:stamp(c,tx+42,ty-40,.65)
  if opened>.7:text(c,['照片.jpg','说明.txt','音频.wav'][i],tx,ty+105,21,INK,a=out((opened-.7)/.3))
 if t>=19.4:
  stamp(c,fx+70,fy-65,.9)
  text(c,'全部校验通过',fx,fy-180 if p else fy-170,31,TEAL)
 # The source file remains an MP4-named input; it alone goes to recycling.
 recycle=at(t,20.0,20.65)
 rx,ry=recycle_pos(p)
 if t>=19.6:
  icon(c,'delete',rx,ry,84)
  text(c,'回收站',rx,ry+68,20,MUTED)
  text(c,'回收已开启 · 无后续依赖',w*.5,h*.86,22,MUTED)

def queued_source(c,t,p):
 if t<3.0 or t>=20.65:return
 x,y,ww,hh=surface_rect(t,p);sx,sy=x+58,y+hh-78
 rx,ry=recycle_pos(p)
 q=at(t,20.0,20.65)
 ix,iy=point((sx,sy,sx+150,sy-160,rx-110,ry+100,rx,ry),q)
 icon(c,'video_clip',ix,iy,lerp(44,26,q),a=1-q)


def finale(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];q=at(t,22,23.1)
 cx=w*.5;cy=h*.45
 if p:left=[(180,400),(180,580),(180,760)];right=[(720,400),(720,580),(720,760)];size=125;bs=270
 else:left=[(350,285),(350,450),(350,615)];right=[(1250,285),(1250,450),(1250,615)];size=150;bs=370
 for i,fmt in enumerate(['ZIP','7Z','RAR']):
  x,y=left[i];icon(c,'archive',lerp(cx,x,q),lerp(cy,y,q),size,badge=fmt)
 for i,name in enumerate(['image','document','music_note_2']):
  x,y=right[i];icon(c,name,lerp(cx,x,q),lerp(cy,y,q),size)
 icon(c,'brand',cx,cy,bs+18*math.sin(clamp((t-22)/1.1)*math.pi))
 # Relationships settle around the app. No decorative orbits or random shaking.
 if t>=23.1:
  text(c,'T解压',cx,cy+bs*.61+20,65 if p else 76,INK,bold=True)
  text(c,'看内容，不看后缀。',cx,cy+bs*.61+85,32 if p else 38,TEAL)
  text(c,'tunpack.thomasthzh.top',cx,cy+bs*.61+135,23,MUTED)


def draw_content(c,t,p):
 chapters=[(0,drag),(3.6,identify),(7,volumes),(10.6,password),(14,nested),(18.5,result),(22,finale)]
 index=max(i for i,(start,fn) in enumerate(chapters) if t>=start)
 start,fn=chapters[index]
 if index and t<start+.16:
  q=at(t,start,start+.16)
  c.saveLayerAlpha(None,round((1-q)*255));chapters[index-1][1](c,t,p);c.restore()
  c.saveLayerAlpha(None,round(q*255));fn(c,t,p);c.restore()
 else:fn(c,t,p)


def render_frame(t,kind='wide'):
 p=kind=='portrait';w,h=SIZES[kind];surface=skia.Surface(w,h);c=surface.getCanvas();chrome(c,t,p)
 x,y,ww,hh=surface_rect(t,p);c.save();c.clipRRect(skia.RRect.MakeRectXY(skia.Rect.MakeXYWH(x+2,y+85,ww-4,hh-190),22,22))
 fx,fy,z=camera(t,p);c.translate(w/2,h*.49);c.scale(z,z);c.translate(-fx,-fy)
 draw_content(c,t,p);c.restore();queued_source(c,t,p);cursor(c,t,p)
 return surface.toarray(colorType=skia.ColorType.kRGBA_8888_ColorType)


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--format',choices=['wide','portrait','all'],default='all');args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
 for kind in (['wide','portrait'] if args.format=='all' else [args.format]):
  for t in [.9,2.65,3.55,5.5,9.8,13.2,16.0,17.6,20.8,24.5]:
   start=time.perf_counter();Image.fromarray(render_frame(t,kind)).save(OUT/f'{kind}-{t:.2f}.png');print(kind,t,round(time.perf_counter()-start,3),flush=True)

if __name__=='__main__':main()
