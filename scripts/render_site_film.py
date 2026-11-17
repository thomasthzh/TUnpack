"""Pictorial layered hero animation: illustrated assets, paths and one clock.
No Blender, GUI, browser recording or text-slide renderer.
"""
import argparse
from functools import lru_cache
import math
from pathlib import Path
import time

from PIL import Image
import skia

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/evidence/hero-art'
ART=ROOT/'brand/film-art'
FPS,DURATION=60,24
SIZES={'wide':(1600,900),'portrait':(900,1200)}
BG='#071019';INK='#f2efe7';JADE='#7cddc6';DIM='#9aaeb5';COPPER='#cba176'


def clamp(x):return max(0.,min(1.,x))
def ease(x):
 x=clamp(x);return x*x*(3-2*x)
def out(x):return 1-(1-clamp(x))**3
def lerp(a,b,t):return a+(b-a)*t
def at(t,a,b):return ease((t-a)/(b-a))
def color(s,a=1):return skia.ColorSetARGB(round(clamp(a)*255),*[int(s[i:i+2],16) for i in (1,3,5)])
def paint(s=INK,a=1,stroke=0):
 p=skia.Paint(Color=color(s,a),AntiAlias=True)
 if stroke:p.setStyle(skia.Paint.kStroke_Style);p.setStrokeWidth(stroke);p.setStrokeCap(skia.Paint.kRound_Cap)
 return p
@lru_cache(maxsize=100)
def font(n,bold=False):return skia.Font(skia.Typeface.MakeFromFile('C:/Windows/Fonts/'+('msyhbd.ttc' if bold else 'msyh.ttc')),n)
def text(c,s,x,y,size=26,fill=INK,a=1,align='center',bold=False):
 f=font(round(size),bold);w=f.measureText(s)
 if align=='center':x-=w/2
 elif align=='right':x-=w
 c.drawString(s,x,y,f,paint(fill,a))
def line(c,x1,y1,x2,y2,fill=JADE,w=2,a=1):c.drawLine(x1,y1,x2,y2,paint(fill,a,w))
def path(c,points,fill=JADE,w=2,a=1,close=False):
 p=skia.Path();p.moveTo(*points[0])
 for xy in points[1:]:p.lineTo(*xy)
 if close:p.close()
 c.drawPath(p,paint(fill,a,w))
def curve(c,pts,fill=JADE,w=2,a=1):
 p=skia.Path();p.moveTo(*pts[:2]);p.cubicTo(*pts[2:]);c.drawPath(p,paint(fill,a,w))
def rect(c,x,y,w,h,fill=INK,a=1,stroke=0):c.drawRect(skia.Rect.MakeXYWH(x,y,w,h),paint(fill,a,stroke))
def check(c,x,y,s=1,a=1):path(c,[(x-16*s,y),(x-3*s,y+13*s),(x+25*s,y-20*s)],JADE,4*s,a)
def glow(c,x,y,r,fill=JADE,a=.25):
 p=skia.Paint(AntiAlias=True);p.setShader(skia.GradientShader.MakeRadial((x,y),r,[color(fill,a),color(fill,0)]));c.drawCircle(x,y,r,p)

@lru_cache(maxsize=2)
def bitmap(name):return skia.Image.MakeFromEncoded(skia.Data.MakeFromFileName(str(ART/name)))
SAMPLING=skia.SamplingOptions(skia.FilterMode.kLinear,skia.MipmapMode.kLinear)


def sprite(c,item,x,y,w,angle=0,a=1):
 """Sample the original atlas at draw time; no edited derivative image files."""
 if a<=0:return
 im=bitmap('contents-atlas.png');half=im.width()/2
 col,row=item%2,item//2
 src=skia.Rect.MakeXYWH(col*half,row*half,half,half)
 c.save();c.translate(x,y);c.rotate(angle)
 c.drawImageRect(im,src,skia.Rect.MakeXYWH(-w/2,-w/2,w,w),SAMPLING,paint(INK,a));c.restore()


def sleeve(c,x,y,w,angle=0,a=1,split=0,label=None):
 if a<=0:return
 im=bitmap('media-sleeve.png');h=w*im.height()/im.width()
 c.save();c.translate(x,y);c.rotate(angle)
 for side in [-1,1]:
  c.save();c.translate(side*split*w*.53,split*20);c.rotate(side*split*15)
  c.clipRect(skia.Rect.MakeXYWH(-w if side<0 else 0,-h,w,h*2))
  c.drawImageRect(im,skia.Rect.MakeWH(im.width(),im.height()),skia.Rect.MakeXYWH(-w/2,-h/2,w,h),SAMPLING,paint(INK,a))
  if label:
   # Provenance belongs to the right shell, and never morphs into a ZIP suffix.
   if side>0:
    rect(c,w*.025,h*.31,w*.40,32,'#111f28',a*.85)
    text(c,label,w*.22,h*.31+23,max(14,w*.047),INK,a)
  c.restore()
 c.restore()


def paper(c,x,y,w,angle=0,a=1):
 c.save();c.translate(x,y);c.rotate(angle)
 rect(c,-w/2,-w*.35,w,w*.70,'#c7d3cc',a)
 line(c,-w/2,-w*.35,w/2,-w*.35,INK,1,a)
 c.restore()


def fan(c,t,x,y,size,spread=1,a=1):
 # Individual content objects move at different speeds and occlude one another.
 for i in range(6):
  paper(c,x+(i-2.5)*size*.055*spread,y+size*.22+i*5,size*.73,(-15+i*6)*spread,a*.7)
 sprite(c,2,x-size*.29*spread,y+size*.06,size*.82,-13*spread,a)
 sprite(c,1,x+size*.25*spread,y-size*.11,size*.90,12*spread,a)
 sprite(c,0,x-size*.06*spread,y-size*.01,size,-5*spread,a)


def lock(c,x,y,s=1,opened=0,a=1):
 c.save();c.translate(x,y);c.scale(s,s)
 p=skia.Paint(AntiAlias=True);p.setShader(skia.GradientShader.MakeLinear([(-40,-20),(40,60)],[color('#e4d3ac',a),color('#9d7350',a)]))
 c.drawRoundRect(skia.Rect.MakeXYWH(-38,-16,76,65),12,12,p)
 # The shackle opens only after the verification mark has appeared.
 c.save();c.translate(0,-32*opened);c.rotate(-24*opened)
 q=skia.Path();q.moveTo(-24,-16);q.lineTo(-24,-38);q.cubicTo(-24,-76,24,-76,24,-38);q.lineTo(24,-16)
 c.drawPath(q,paint('#d9e4df',a,9));c.restore()
 c.drawCircle(0,10,7,paint('#4e493d',a));rect(c,-3,11,6,16,'#4e493d',a)
 c.restore()


def key(c,x,y,angle=0,s=1,a=1):
 c.save();c.translate(x,y);c.rotate(angle);c.scale(s,s)
 c.drawCircle(-28,0,19,paint(COPPER,a,8));line(c,-9,0,51,0,COPPER,8,a)
 line(c,30,0,30,16,COPPER,7,a);line(c,47,0,47,13,COPPER,7,a)
 line(c,-8,-3,50,-3,INK,1,a*.7);c.restore()


def book(c,x,y,w,a=1):
 c.save();c.translate(x,y)
 # Two drawn paper wings, distinct from a file card or UI rectangle.
 for side in [-1,1]:
  p=skia.Path();p.moveTo(0,w*.25);p.cubicTo(side*w*.18,w*.05,side*w*.42,w*.08,side*w*.48,w*.02);p.lineTo(side*w*.48,-w*.50);p.cubicTo(side*w*.30,-w*.57,side*w*.12,-w*.48,0,-w*.36);p.close()
  c.drawPath(p,paint('#d7ddd2' if side<0 else '#eef0e4',a))
  for i in range(5):line(c,side*w*.10,-w*.26+i*w*.066,side*w*.39,-w*.30+i*w*.065,'#729187',1.5,a*.65)
 line(c,0,-w*.36,0,w*.25,'#3c695e',2,a)
 for i in range(4):c.drawCircle((i-1.5)*14,-w*.08,3.8,paint('#244c46',a))
 c.restore()


def bezier(points,q):
 x1,y1,x2,y2,x3,y3,x4,y4=points;v=1-q
 return (v**3*x1+3*v*v*q*x2+3*v*q*q*x3+q**3*x4,v**3*y1+3*v*v*q*y2+3*v*q*q*y3+q**3*y4)


def camera(t,p):
 phase=0
 for a,b,i in [(5.7,6.35,0),(9.45,10.1,1),(12.8,13.45,2),(18.0,18.65,3)]:
  if t>=a:phase=lerp(i,i+1,at(t,a,b))
 spacing=1500 if p else 1900
 # Macro seam shot, rapid release into separated material, then a readable fan.
 z=1+.24*at(t,0,1.9)+.78*at(t,2.15,2.55)-1.02*at(t,3.8,5.25)
 if 13.5<t<18:z=1+.18*at(t,13.5,14.0)-.18*at(t,15.4,16.7)
 if t>21.2:z=lerp(1,.9,at(t,21.2,22.1))
 return (0,phase*spacing,z) if p else (phase*spacing,0,z)


def backdrop(c,t,w,h):
 c.clear(color(BG))
 glow(c,w*.53,h*.48,min(w,h)*.72,'#306771',.40)
 glow(c,w*.82,h*.24,min(w,h)*.5,'#1c4e63',.25)
 # Broad paper-ribbon curves suggest continuous unwrapping, not a particle field.
 for i in range(6):
  yy=h*.84+i*17
  curve(c,(-200,yy,w*.30,yy-h*.55,w*.77,yy+h*.1,w+150,yy-h*.43),'#32586c',1,.10)


def bridge(c,t,p):
 step=1500 if p else 1900
 for i in range(4):
  for j in range(4):
   d=(j-1.5)*13
   if p:curve(c,(d,i*step+400,d+130,i*step+650,d-130,(i+1)*step-650,d,(i+1)*step-400),'#6da7a1',1.5,.22)
   else:curve(c,(i*step+620,d,i*step+850,d+100,(i+1)*step-850,d-100,(i+1)*step-620,d),'#6da7a1',1.5,.22)


def opening(c,t,p):
 split=at(t,2.65,4.0);reveal=at(t,3.0,4.7)
 size=425 if p else 470
 c.save()
 opening_width=lerp(0,1100,reveal)
 c.clipRect(skia.Rect.MakeXYWH(-opening_width/2,-600,opening_width,1200))
 # The outer ZIP contains encrypted volumes; do not preview their plaintext.
 sprite(c,3,0,35,420 if p else 500,0,1)
 for i in [0,2,1]:
  x=(i-1)*lerp(8,120,reveal);y=lerp(10,-20,reveal)+abs(i-1)*30
  sleeve(c,x,y,lerp(220,235,reveal),(i-1)*lerp(2,14,reveal),1,label=['001','002','DOCX'][i])
 c.restore()
 sleeve(c,0,0,size,-3+3*at(t,0,2),1,split,label='旅途.mp4')
 # A moving seam light triggers the physical peel rather than a text change.
 sweep=at(t,1.45,2.75);glow(c,0,lerp(-210,250,sweep),70,JADE,(1-split)*.9)
 if reveal>.75:
  text(c,'ZIP 内容',0,310,24,JADE,reveal)
  text(c,'原文件名保留',0,345,21,DIM,reveal)


def volumes(c,t,p):
 spread=at(t,6.2,7.15);fit=at(t,7.45,8.65);verified=t>=8.75
 gap=190 if p else 300
 for i in [0,2,1]:
  x=(i-1)*lerp(gap,85,fit)
  y=(abs(i-1)*40)+(1-fit)*(80 if i==2 else 0)
  tilt=(i-1)*lerp(14,2,fit)
  sleeve(c,x,y,235 if p else 280,tilt,spread,label=['001','002','DOCX'][i])
  text(c,['项目.7z.001','项目.7z.002','附件.docx'][i],x,y-205 if p else y-235,22,DIM,spread*(1-fit))
  if i==2 and not verified:
   # The third piece exists from the start; it is never synthesized from a gap.
   glow(c,x,y,160,'#599eae',.1)
 if verified:
  check(c,0,275,.9)
  text(c,'附件.docx → 卷003 · 唯一匹配',0,335,24,JADE)
 else:text(c,'ZIP 内的三卷',0,345,25,DIM,spread)


def password(c,t,p):
 enter=at(t,9.9,10.4);verified=t>=12.05;unlock=at(t,12.22,12.75)
 ax=0 if p else 150;ay=-80 if p else -15
 sleeve(c,ax,ay,360 if p else 410,-3,enter,label='7Z 卷组')
 lock(c,ax,ay+15,1.0,unlock,enter)
 bx=-220 if p else -440;by=270 if p else 150
 book(c,bx,by,165 if p else 200,enter)
 text(c,'已存候选',bx,by+92,23,DIM,enter)
 points=(bx,by-15,bx+100,by-230,ax-160,ay-180,ax,ay+15)
 q=at(t,10.6,12.05)
 if q>0:
  curve(c,points,JADE,1.3,.26)
  kx,ky=bezier(points,q*.82 if not verified else lerp(.82,1,at(t,12.05,12.3)))
  key(c,kx,ky,-10,1.0,1-unlock*.75)
 if verified:
  check(c,ax+150,ay-140,.8)
  text(c,'验证通过',ax,ay+310 if p else ay+340,27,JADE)


def nested(c,t,p):
 inner=at(t,13.35,14.55);flight=at(t,14.7,16.5);settled=at(t,16.5,17.7)
 if p:ox,oy=-100,-100;ix,iy=115,100
 else:ox,oy=-300,10;ix,iy=100,15
 sprite(c,3,ox,oy,520 if not p else 430,-10,1)
 sweep=at(t,14.55,15.45)
 # Same mountain photograph is exposed, extracted and then kept in the result fan.
 for i,item in enumerate([2,1,0]):
  if p:tx=(i-1)*220;ty=-190+(i%2)*75
  else:tx=390+(i-1)*130;ty=(i-1)*150
  q=at(t,14.7+i*.17,16.4+i*.12)
  x=lerp(ix,tx,q);y=lerp(iy,ty,q)-math.sin(q*math.pi)*100
  if q>0:
   sprite(c,item,x,y,lerp(90,295 if p else 340,q),(i-1)*lerp(0,12,q),1)
 # The same inner sleeve grows into the foreground and passes the lens. Content
 # is revealed by its trailing edge, so this is an occlusion change, not a zoom
 # applied to a complete still illustration.
 fx=lerp(lerp(ox,ix,inner),ix if p else -1700,sweep)
 fy=lerp(lerp(oy,iy,inner),-1850 if p else -160,sweep)
 fw=lerp(lerp(170,390 if p else 430,inner),1650,sweep)
 sprite(c,3,fx,fy,fw,lerp(8,-18,sweep),1)
 if inner>.8 and sweep<.25:text(c,'内层.zip',ix,iy+190,23,JADE,inner*(1-sweep*4))
 text(c,'7Z 卷组',ox,oy-270 if not p else oy-255,22,DIM)
 if settled:text(c,'继续解开，直到文件。',0,420 if p else 360,28,INK,settled)


def bin_icon(c,x,y,s=1):
 path(c,[(x-24*s,y-20*s),(x-18*s,y+34*s),(x+18*s,y+34*s),(x+24*s,y-20*s)],DIM,3)
 line(c,x-30*s,y-28*s,x+30*s,y-28*s,DIM,3);line(c,x-11*s,y-38*s,x+11*s,y-38*s,DIM,3)


def result(c,t,p):
 enter=at(t,18.4,19.05);done=t>=19.3;cleanup=at(t,20.05,21.05);ending=at(t,21.3,22.15)
 if p:cx,cy=0,0
 else:cx,cy=lerp(0,300,ending),15
 sprite(c,3,cx,cy+175,580 if p else 650,2,enter)
 fan(c,t,cx,cy-20,470 if p else 570,lerp(.7,1.1,ending),enter)
 if done:
  check(c,cx,cy+370 if p else cy+330,1.05)
  if not ending:text(c,'全部校验通过',cx,cy+425 if p else cy+380,24,JADE,1-ending)
 # Source alone moves into recycling after success. Output remains in place.
 rx=310 if p else 650;ry=340 if p else 210
 if ending<.98:
  bin_icon(c,rx,ry,.7)
  if cleanup<.98:sleeve(c,rx,lerp(ry-200,ry,cleanup),lerp(88,30,cleanup),-8,1-cleanup,label='旅途.mp4')
 if cleanup>0 and not ending:text(c,'按设置回收 · 无后续依赖',0,490 if p else 425,21,DIM,1-ending)
 if ending:
  if p:
   text(c,'T解压',0,-365,86,INK,ending,bold=True)
   text(c,'看内容，不看后缀。',0,-290,33,JADE,ending)
  else:
   text(c,'T解压',-520,-80,112,INK,ending,align='left',bold=True)
   text(c,'看内容，',-510,10,38,JADE,ending,align='left')
   text(c,'不看后缀。',-510,65,38,JADE,ending,align='left')
   text(c,'Windows 原生 · 3.63 MB',-510,145,24,DIM,ending,align='left')


def render_frame(t,kind='wide'):
 p=kind=='portrait';w,h=SIZES[kind];surface=skia.Surface(w,h);c=surface.getCanvas();backdrop(c,t,w,h)
 x,y,z=camera(t,p);c.save();c.translate(w/2,h/2);c.scale(z,z);c.translate(-x,-y)
 bridge(c,t,p);spacing=1500 if p else 1900
 for i,fn in enumerate([opening,volumes,password,nested,result]):
  center=i*spacing
  if abs(center-(y if p else x))*z>(h/2 if p else w/2)+800*z:continue
  c.save();c.translate(0 if p else center,center if p else 0);fn(c,t,p);c.restore()
 c.restore()
 text(c,'T解压',38,54,25,INK,align='left',bold=True)
 text(c,'功能动画 · 非软件录屏',w-38,h-38,17,DIM,align='right')
 return surface.toarray(colorType=skia.ColorType.kRGBA_8888_ColorType)


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--format',choices=['wide','portrait','all'],default='all');args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
 for kind in (['wide','portrait'] if args.format=='all' else [args.format]):
  for t in [0.7,2.45,3.5,4.8,8.8,12.4,16.8,20.5,23.2]:
   start=time.perf_counter();Image.fromarray(render_frame(t,kind)).save(OUT/f'{kind}-{t:.2f}.png');print(kind,t,round(time.perf_counter()-start,3),flush=True)

if __name__=='__main__':main()
