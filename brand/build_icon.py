"""Original layered T-unpack mark. Vector source and optically sized native ICO."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

ROOT=Path(__file__).resolve().parent
SIZES=[16,20,24,32,40,48,64,128,256]

def render(size):
    scale=4 if size>=128 else 12
    side=size*scale
    k=side/256
    canvas=Image.new("RGBA",(side,side))
    def box(values):return tuple(round(v*k) for v in values)
    def mask_rect(rect,radius):
        mask=Image.new("L",(side,side))
        ImageDraw.Draw(mask).rounded_rectangle(box(rect),round(radius*k),fill=255)
        return mask
    def gradient(mask,top,bottom):
        layer=Image.new("RGBA",(side,side))
        draw=ImageDraw.Draw(layer)
        bounds=mask.getbbox()
        if not bounds:return layer
        for y in range(bounds[1],bounds[3]):
            t=(y-bounds[1])/max(1,bounds[3]-bounds[1]-1)
            color=tuple(round(a+(b-a)*t) for a,b in zip(top,bottom))+(255,)
            draw.line((0,y,side,y),fill=color)
        layer.putalpha(mask)
        return layer
    def shadow(mask,dy,blur,alpha):
        moved=Image.new("L",(side,side))
        moved.paste(mask,(0,round(dy*k)))
        moved=moved.filter(ImageFilter.GaussianBlur(blur*k)).point(lambda p:round(p*alpha/255))
        shade=Image.new("RGBA",(side,side),(0,32,43,0));shade.putalpha(moved)
        canvas.alpha_composite(shade)
    def surface(rect,radius,top,bottom,cast=True):
        mask=mask_rect(rect,radius)
        if cast:shadow(mask,5,5,100)
        canvas.alpha_composite(gradient(mask,top,bottom))
        edge=Image.new("RGBA",(side,side))
        ImageDraw.Draw(edge).rounded_rectangle(box(rect),round(radius*k),outline=(225,255,247,95),width=max(1,round(1.2*k)))
        canvas.alpha_composite(edge)
        return mask
    # Staggered archive plates. Dark lower edges give each plate actual thickness.
    surface((76,22,228,196),27,(149,242,215),(44,165,159))
    surface((52,42,211,217),29,(44,188,174),(8,109,123))
    surface((28,73,194,237),31,(5,93,110),(3,54,72))
    front=surface((28,64,194,226),31,(24,153,147),(5,82,105))
    # A soft light along the top lip, kept inside the front plate.
    light=Image.new("RGBA",(side,side));d=ImageDraw.Draw(light)
    d.line(box((59,66,163,66)),fill=(197,255,235,115),width=max(1,round(2*k)))
    canvas.alpha_composite(light)
    mark=Image.new("L",(side,side));d=ImageDraw.Draw(mark)
    d.rounded_rectangle(box((52,91,170,116)),round(6*k),fill=255)
    d.rectangle(box((100,110,123,169)),fill=255)
    d.polygon([tuple(round(v*k) for v in p) for p in [(86,160),(138,160),(112,190)]],fill=255)
    if size>=32:
        for rect in [(106,124,113,131),(113,135,120,142),(106,146,113,153)]:
            d.rounded_rectangle(box(rect),round(k),fill=0)
    shadow(mark,3,1.6,110)
    canvas.alpha_composite(gradient(mark,(255,255,250),(191,238,230)))
    if size>=24:
        tray=Image.new("RGBA",(side,side));d=ImageDraw.Draw(tray)
        d.line([tuple(round(v*k) for v in p) for p in [(65,193),(65,205),(71,211),(153,211),(159,205),(159,193)]],
               fill=(154,239,219,255),width=round(5*k),joint="curve")
        for x,y in [(65,193),(159,193)]:
            d.ellipse(box((x-2.5,y-2.5,x+2.5,y+2.5)),fill=(154,239,219,255))
        canvas.alpha_composite(tray)
    return canvas.resize((size,size),Image.Resampling.LANCZOS)

svg="""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="T解压：层叠压缩包与金属T拉链">
<defs>
<linearGradient id="back" x2="0" y2="1"><stop stop-color="#95f2d7"/><stop offset="1" stop-color="#2ca59f"/></linearGradient>
<linearGradient id="mid" x2="0" y2="1"><stop stop-color="#2cbcae"/><stop offset="1" stop-color="#086d7b"/></linearGradient>
<linearGradient id="base" x2="0" y2="1"><stop stop-color="#055d6e"/><stop offset="1" stop-color="#033648"/></linearGradient>
<linearGradient id="face" x2="0" y2="1"><stop stop-color="#189993"/><stop offset="1" stop-color="#055269"/></linearGradient>
<linearGradient id="metal" x2="0" y2="1"><stop stop-color="#fffffa"/><stop offset="1" stop-color="#bfeee6"/></linearGradient>
<filter id="shadow" x="-30%" y="-30%" width="160%" height="175%"><feDropShadow dx="0" dy="5" stdDeviation="5" flood-color="#00202b" flood-opacity=".4"/></filter>
<filter id="tShadow" x="-20%" y="-20%" width="140%" height="150%"><feDropShadow dx="0" dy="3" stdDeviation="1.6" flood-color="#00202b" flood-opacity=".43"/></filter>
<mask id="t"><rect width="256" height="256" fill="black"/><rect x="52" y="91" width="118" height="25" rx="6" fill="white"/><path d="M100 110h23v50h15l-26 30-26-30h14z" fill="white"/>
<g fill="black"><rect x="106" y="124" width="7" height="7" rx="1"/><rect x="113" y="135" width="7" height="7" rx="1"/><rect x="106" y="146" width="7" height="7" rx="1"/></g></mask>
</defs>
<g stroke="#e1fff7" stroke-opacity=".37" stroke-width="1.2" filter="url(#shadow)">
<rect x="76" y="22" width="152" height="174" rx="27" fill="url(#back)"/>
<rect x="52" y="42" width="159" height="175" rx="29" fill="url(#mid)"/>
<rect x="28" y="73" width="166" height="164" rx="31" fill="url(#base)"/>
<rect x="28" y="64" width="166" height="162" rx="31" fill="url(#face)"/>
</g>
<path d="M59 66h104" stroke="#c5ffeb" stroke-opacity=".45" stroke-width="2"/>
<g filter="url(#tShadow)"><rect width="256" height="256" fill="url(#metal)" mask="url(#t)"/></g>
<path d="M65 193v12l6 6h82l6-6v-12" fill="none" stroke="#9aefdb" stroke-width="5" stroke-linejoin="round" stroke-linecap="round"/>
</svg>"""
(ROOT/"t-unpack.svg").write_text(svg,encoding="utf-8")
icons={n:render(n) for n in SIZES}
icons[256].save(ROOT/"t-unpack.png")
icons[256].save(ROOT/"t-unpack.ico",format="ICO",sizes=[(n,n) for n in SIZES],append_images=[icons[n] for n in SIZES if n!=256])
sheet=Image.new("RGB",(760,420),"#eff3f4");draw=ImageDraw.Draw(sheet)
for i,size in enumerate(SIZES):
    shown=icons[size] if size<=64 else icons[size].resize((64,64),Image.Resampling.LANCZOS)
    x=22+i*82
    sheet.paste(shown,(x+(64-shown.width)//2,24+(64-shown.height)//2),shown)
    draw.text((x+20,102),str(size),fill="#233f46")
sheet.paste(icons[256],(55,145),icons[256])
dark=Image.new("RGB",(270,270),"#172c36");dark.paste(icons[256],(7,7),icons[256]);sheet.paste(dark,(380,135))
sheet.save(ROOT/"icon-preview.png")
print("Created layered SVG, PNG, nine native ICO sizes and preview")
