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
OUT=ROOT/'docs/evidence/hero-icons-polish'
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
 # A short lower lip and a directional face give each icon a readable edge.
 c.drawRoundRect(skia.Rect.MakeXYWH(48,41,416,416),94,94,paint(bottom))
 p=skia.Paint(AntiAlias=True);p.setShader(skia.GradientShader.MakeLinear([(72,24),(424,454)],[color(top),color(bottom)]))
 c.drawRoundRect(skia.Rect.MakeXYWH(48,28,416,416),94,94,p)
 c.drawRoundRect(skia.Rect.MakeXYWH(50,30,412,412),92,92,paint('#ffffff',.26,2))
 c.save();c.clipRRect(skia.RRect.MakeRectXY(skia.Rect.MakeXYWH(50,30,412,410),92,92))
 sheen=skia.Paint(AntiAlias=True);sheen.setShader(skia.GradientShader.MakeLinear([(70,36),(355,360)],[color('#ffffff',.18),color('#ffffff',0)]))
 c.drawRect(skia.Rect.MakeXYWH(50,30,412,410),sheen);c.restore()
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
 w,h=SIZES['portrait' if p else 'wide'];fx=w*.5;fy=h*.49;z=1.
 if 4.15<=t<7.1:
  q=at(t,4.15,4.51)*(1-at(t,5.85,7.1));z=1+(.28 if p else .45)*q
  fx+=w*.055*q;fy-=h*.015*q
 elif 8.2<=t<10.9:
  q=at(t,8.2,8.6)*(1-at(t,9.85,10.9));z=1+.12*q;fy-=h*.035*q
 elif 11.9<=t<14.2:
  q=at(t,11.9,12.26)*(1-at(t,13.2,14.2));z=1+(.14 if p else .2)*q
  fx+=w*.055*q;fy-=h*.025*q
 elif 14.8<=t<18.35:
  q=at(t,14.8,15.16)*(1-at(t,16.25,18.35));z=1+(.27 if p else .48)*q
  fx+=w*.045*q;fy-=h*.015*q
 return(fx,fy,z)


def chrome(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];c.clear(color('#edf3f7'))
 x,y,ww,hh=surface_rect(t,p)
 box(c,x,y+5,ww,hh,'#b8cad7',30,a=.18)
 box(c,x,y,ww,hh,'#ffffff',28*(1-at(t,3.6,4.12)))
 icon(c,'brand',x+53,y+47,42)
 text(c,'T解压',x+88,y+57,26,INK,align='left',bold=True)
 bx,by=start_pos(t,p)
 label='开始全部' if t<3.6 else '处理中' if t<19.6 else '已完成'
 box(c,bx-63,by-23,126,46,TEAL if t<3.6 else '#edf7f5',14)
 if t<3.6:glyph(c,'play',bx-42,by,18)
 text(c,label,bx+12 if t<3.6 else bx,by+8,20,'#ffffff' if t<3.6 else TEAL)
 if t>=2.62:
  rowy=y+hh-78
  box(c,x+22,rowy-32,ww-44,66,'#f4f8fb',16)
  # The input identity stays in the queue while previews show derived content.
  text(c,'旅途.mp4',x+94,rowy+8,22,INK,align='left')
  states=['待开始','识别内容','分卷归组','验证已存候选','展开内层.zip','全部校验通过']
  i=0 if t<3.6 else 1 if t<7 else 2 if t<10.6 else 3 if t<14 else 4 if t<19.6 else 5
  text(c,states[i],x+ww-40,rowy+8,20,TEAL if i==5 else MUTED,align='right')
  if t>=19.6:stamp(c,x+ww-42,rowy-18,.55)
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


def recognition_pose(p):
 return(270,480,603,636,210) if p else (520,410,1050,410,300)

def zip_anchor(p):return(450,300) if p else (800,250)
def group_center(p):return(450,540) if p else (800,440)
def package_target(p):return(540,470,220) if p else (1040,420,310)
def inner_pos(p):return(550,585) if p else (840,430)


def identify(c,t,p):
 ax,ay,bx,by,size=recognition_pose(p);arrive=at(t,3.6,4.12);handoff=at(t,6.6,7.4)
 x,y,ww,hh=surface_rect(t,p);sx,sy=x+58,y+hh-78
 vx=lerp(sx,ax,arrive);vy=lerp(sy,ay,arrive);vs=lerp(44,size,arrive)
 # The source preview returns to its queue identity while its contents remain.
 back=at(t,6.6,7.15)
 vx=lerp(vx,sx,back);vy=lerp(vy,sy,back);vs=lerp(vs,44,back)
 icon(c,'video_clip',vx,vy,vs,a=1-at(t,6.95,7.18),badge='MP4' if back<.55 else None)
 text(c,'旅途.mp4',vx,vy+vs*.58,27,INK,a=arrive*(1-back))
 text(c,'文件名保留',vx,vy+vs*.58+38,22,MUTED,a=arrive*(1-back))
 reveal=at(t,4.3,5.4)
 if reveal:
  zx,zy=zip_anchor(p);lift=at(t,6.6,7.4)
  ex=lerp(lerp(ax,bx,reveal),zx,lift);ey=lerp(lerp(ay,by,reveal),zy,lift)
  archive_size=lerp(size*.94,110 if p else 125,lift)
  curve(c,(ax+size*.35,ay,bx-size*.7,ay,bx-size*.6,by,bx-size*.42,by),'#b8dcd4',3,a=1-back)
  parent_alpha=1-at(t,8.05,8.65)
  icon(c,'archive',ex,ey,archive_size,angle=lerp(-12,0,reveal),a=parent_alpha)
  # The parent identity moves above the exit before its children pass below it.
  badge_y=ey+archive_size*lerp(.32,-.78,lift)
  box(c,ex-29,badge_y,58,30,'#ffffff',10,a=parent_alpha)
  text(c,'ZIP',ex,badge_y+22,17,INK,a=parent_alpha,bold=True)
  if reveal>.85:
   c.saveLayerAlpha(None,round((1-back)*255));stamp(c,ex+archive_size*.32,ey-archive_size*.30,.85);c.restore()
   text(c,'内容：ZIP',ex,ey+archive_size*.58,29,TEAL,a=1-back)
 if t<5.0:
  xx=lerp(ax-size*.30,ax+size*.30,at(t,3.8,4.8));line(c,xx,ay-size*.31,xx,ay+size*.31,'#85ddcb',3,.75*arrive)


def volumes(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];zx,zy=zip_anchor(p);gx,gy=group_center(p)
 centers=[(225,535),(450,535),(675,710)] if p else [(450,465),(800,465),(1150,560)]
 join=at(t,8.35,9.6);pack=at(t,10.0,10.9);px,py,ps=package_target(p)
 for i in range(3):
  q=at(t,7.0+i*.12,7.85+i*.12)
  if q<=0:continue
  tx,ty=centers[i];x=lerp(zx,tx,q);y=lerp(zy,ty,q)-math.sin(q*math.pi)*45
  x=lerp(x,gx+(i-1)*(100 if p else 135),join);y=lerp(y,gy+abs(i-1)*18,join)
  x=lerp(x,px+(i-1)*20,pack);y=lerp(y,py-(i-1)*16,pack)
  label=['001','002','DOCX' if t<9.65 else '003'][i]
  icon(c,'document_text' if i==2 else 'archive',x,y,lerp(lerp(75,170 if p else 225,q),80,pack),angle=(i-1)*7*(1-join),a=1-at(t,10.6,10.95),badge=label)
  a=at(t,7.75+i*.12,8.05+i*.12)*(1-at(t,8.7,9.2));text(c,['项目.7z.001','项目.7z.002','附件.docx'][i],x,y+(140 if p else 160),22,INK,a=a)
 if t>=9.65:
  a=1-at(t,10.55,10.95);text(c,'附件.docx → 卷003',gx,gy+225,28,INK,a=a);text(c,'唯一匹配，归为一组',gx,gy+266,23,TEAL,a=a)
 else:text(c,'ZIP 中的三个分卷',w*.5,195 if p else 135,29,INK,a=at(t,7.5,7.8))


def package_pose(t,p):
 gx,gy=group_center(p);ax,ay,size=package_target(p);gather=at(t,10.15,10.9);leave=at(t,13.5,14.4)
 x=lerp(gx,ax,gather);y=lerp(gy,ay,gather);size=lerp(100,size,gather)
 x=lerp(x,250 if p else 390,leave);y=lerp(y,420 if p else 430,leave)
 size=lerp(size,250 if p else 300,leave)
 retire=at(t,15.0,16.3);x=lerp(x,235 if p else 430,retire);y=lerp(y,330 if p else 350,retire);size=lerp(size,120 if p else 145,retire)
 return x,y,size


def package_actor(c,t,p):
 x,y,size=package_pose(t,p);a=at(t,10.15,10.55)*(1-at(t,17.4,18.35))
 # The visible rear layers carry the established three-volume identity.
 for i in [2,1]:icon(c,'archive',x-i*15,y-i*12,size*.93,a=a*.68)
 icon(c,'archive',x,y,size,a=a,badge='7Z')
 lock_alpha=at(t,10.65,10.95)*(1-at(t,14.2,14.6))
 if lock_alpha:icon(c,'lock_open' if t>=12.95 else 'lock_closed',x+size*.28,y+size*.09,78 if p else 96,a=lock_alpha)
 if t>=14.4:text(c,'外层卷组',x,y+size*.64+12,22,MUTED,a=a)


def password(c,t,p):
 bx,by=(245,710) if p else (430,480);ax,ay,size=package_pose(t,p)
 a=at(t,10.65,11.1)*(1-at(t,13.25,14.0))
 icon(c,'book',bx,by,165 if p else 215,a=a)
 text(c,'已存候选',bx,by+130,25,INK,a=a)
 box(c,bx-65,by+145,130,38,'#f3effb',12,a=a);text(c,'••••',bx,by+173,24,'#7e65b4',a=a)
 lx=ax+size*.28;ly=ay+size*.09;pts=(bx+50,by-35,bx+170,by-180,ax-100,ay-130,lx-28,ly)
 q=at(t,11.2,12.7)
 if q>0:
  curve(c,pts,'#e7dcc2',3,a=a);kx,ky=point(pts,q);withdraw=at(t,12.95,13.35)
  icon(c,'key',kx-120*withdraw,ky+45*withdraw,lerp(76 if p else 92,55,withdraw),angle=lerp(-18,0,q),a=a)
 if t>=12.75:
  stamp(c,ax+size*.35,ay-size*.28,.85) if a>.5 else None
  text(c,'验证通过',ax,ay+size*.60,28,TEAL,a=a)


def nested(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];ax,ay,size=package_pose(t,p);ix,iy=inner_pos(p)
 reveal=at(t,14.2,15.05);surge=at(t,15.05,15.42);passage=at(t,15.38,16.35)
 x=lerp(ax,ix,reveal);y=lerp(ay,iy,reveal)
 x=lerp(x,ix if p else -900,passage);y=lerp(y,-1000 if p else 170,passage)
 span=lerp(lerp(70,220 if p else 260,reveal),850 if p else 1000,surge)
 span=lerp(span,1300,passage)
 icon(c,'folder_zip',x,y,span,angle=-12*passage,a=reveal,badge='ZIP')
 if 14.7<=t<15.22:text(c,'内层.zip',ix,iy+165,25,TEAL,a=1-at(t,15.05,15.22))
 if t>=17.1:text(c,'还有内层，就接着解开。',w*.5,h*.78,29,INK,a=1-at(t,18.0,18.45))


def output_pose(i,t,p):
 ix,iy=inner_pos(p);q=at(t,15.45+i*.15,16.8+i*.12)
 tx,ty=([(225,680),(450,760),(675,680)] if p else [(1120,285),(1290,450),(1120,615)])[i]
 x=lerp(ix,tx,q);y=lerp(iy,ty,q)-math.sin(q*math.pi)*42
 gather=at(t,18.0,19.15);rx=(i-1)*225+450 if p else 560+i*240;ry=780 if p else 625
 x,y=point((x,y,x,lerp(y,ry,.8),rx,ry-20,rx,ry),gather)
 size=lerp(90,145 if p else 160,q);size=lerp(size,150 if p else 170,gather)
 opened=at(t,21.2,21.6);fx,fy=result_pos(p)
 x=lerp(x,fx+(i-1)*(225 if p else 245),opened);y=lerp(y,fy+25,opened)
 final=at(t,22,23.2+i*.1)
 dx,dy=([(720,400),(720,580),(720,760)] if p else [(1250,285),(1250,450),(1250,615)])[i]
 x,y=point((x,y,x+100,y-65,dx-60,dy,dx,dy),final);size=lerp(size,125 if p else 150,final)
 return x,y,size,q


def outputs(c,t,p):
 for i,name in enumerate(['image','document','music_note_2']):
  x,y,size,q=output_pose(i,t,p)
  if q<=0:continue
  icon(c,name,x,y,size,angle=(i-1)*4*(1-q))
  if t>=19.1+i*.16 and t<22.4:
   a=1-at(t,22.0,22.4)
   c.saveLayerAlpha(None,round(a*255));stamp(c,x+size*.25,y-size*.24,.65);c.restore()
  opened=at(t,21.2,21.6);a=at(t,21.45,21.65)*(1-at(t,22.0,22.25))
  if a:text(c,['照片.jpg','说明.txt','音频.wav'][i],x,y+105,23 if p else 21,INK,a=a)


def result(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];fx,fy=result_pos(p);opened=at(t,21.2,21.6);a=at(t,18.0,18.65)*(1-at(t,22.0,22.25))
 if opened:
  pw=760 if p else 940;box(c,fx-pw/2,fy-155,pw,325,'#eef8f5',24,a=a*opened)
  text(c,'解压结果',fx,fy-98,27,TEAL,a=a*opened,bold=True)
 icon(c,'folder',lerp(fx,fx-280 if p else fx-375,opened),lerp(fy,fy-115,opened),lerp(lerp(90,200 if p else 245,at(t,18,18.8)),65,opened),a=a)
 text(c,'输出目录',fx-(140 if p else 180),fy+10,26,INK,a=a*max(0,1-opened*4),align='right')
 if 19.6<=t<22.65:
  c.saveLayerAlpha(None,round(a*255));stamp(c,fx+70,fy-65,.9);c.restore()
  text(c,'全部校验通过',fx,fy-180 if p else fy-170,31,TEAL,a=a)
 if t>=19.6:
  rx,ry=recycle_pos(p);icon(c,'delete',rx,ry,84,a=a)
  text(c,'回收站',rx,ry+68,20,MUTED,a=a)
  text(c,'回收已开启 · 无后续依赖',w*.5,h*.86,22,MUTED,a=a)


def queued_source(c,t,p):
 if t<3.0 or t>=20.65:return
 x,y,ww,hh=surface_rect(t,p);sx,sy=x+58,y+hh-78;rx,ry=recycle_pos(p);q=at(t,20.0,20.65)
 ix,iy=point((sx,sy,sx+150,sy-160,rx-110,ry+100,rx,ry),q)
 icon(c,'video_clip',ix,iy,lerp(44,26,q),a=1-q)


def finale(c,t,p):
 w,h=SIZES['portrait' if p else 'wide'];cx=w*.5;cy=h*.45
 left=[(180,400),(180,580),(180,760)] if p else [(350,285),(350,450),(350,615)]
 for i,fmt in enumerate(['ZIP','7Z','RAR']):
  q=at(t,22.0+i*.12,23.2+i*.12);x,y=left[i]
  icon(c,'archive',lerp(-150,x,q),y+math.sin(q*math.pi)*35,125 if p else 150,a=q,badge=fmt)
 q=at(t,22.1,23.2);size=(270 if p else 370)*out(q)+18*math.sin(q*math.pi)
 icon(c,'brand',cx,cy,size,a=q)
 a=at(t,23.15,23.65);bs=270 if p else 370
 text(c,'T解压',cx,cy+bs*.61+20,65 if p else 76,INK,a=a,bold=True)
 text(c,'看内容，不看后缀。',cx,cy+bs*.61+85,32 if p else 38,TEAL,a=a)
 text(c,'tunpack.thomasthzh.top',cx,cy+bs*.61+135,23,MUTED,a=a)


def draw_content(c,t,p):
 # Shared actors cross chapter boundaries; no whole-scene crossfade/reset.
 if t<3.6:drag(c,t,p);return
 if t<8.65:identify(c,t,p)
 if 7<=t<10.95:volumes(c,t,p)
 if 10.15<=t<18.35:package_actor(c,t,p)
 if 10.65<=t<14:password(c,t,p)
 if 18<=t<22.65:result(c,t,p)
 if t>=22:finale(c,t,p)
 if t>=15.45:outputs(c,t,p)
 if 14.2<=t<18.45:nested(c,t,p)


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
