"""Export illustrated layer animation, original score and responsive posters."""
import argparse
from concurrent.futures import ProcessPoolExecutor
import json
from pathlib import Path
import subprocess
import wave

import imageio_ffmpeg
import numpy as np
from PIL import Image
from render_site_film import render_frame,SIZES,FPS,DURATION,OUT,ROOT

ASSETS=ROOT/'site/assets'
STEM='hero-art-20261117'


def score():
    rate=48000;t=np.arange(rate*DURATION)/rate;mix=np.zeros_like(t)
    # Original sound design: low impacts at reveals, a short air sweep at moves,
    # and a resolved chord at the final pullback. No third-party music.
    rng=np.random.default_rng(20261117)
    noise=rng.standard_normal(t.size);noise=np.convolve(noise,np.ones(18)/18,mode='same')
    for start in [0,2.2,2.7,5.7,9.45,12.8,18.0,21.2]:
        local=t-start;env=np.where(local>=0,np.exp(-np.maximum(local,0)*5),0)
        freq=48+60*np.exp(-np.maximum(local,0)*12)
        mix+=.16*env*np.sin(2*np.pi*freq*local)
        air=np.exp(-((local-.16)/.17)**2);air[local<0]=0
        mix+=noise*air*.12
    for start in [8.75,12.05,19.3,22.1]:
        local=t-start;env=np.where(local>=0,np.minimum(np.maximum(local,0)*50,1)*np.exp(-np.maximum(local,0)*2.7),0)
        for freq in [261.63,392,523.25]:mix+=.025*env*np.sin(2*np.pi*freq*local)
    mix*=np.minimum(t/.08,1)*np.minimum((DURATION-t)/.7,1)
    stereo=np.stack([mix,mix*.96],axis=1)
    path=OUT/'original-art-score.wav'
    with wave.open(str(path),'wb') as stream:
        stream.setnchannels(2);stream.setsampwidth(2);stream.setframerate(rate);stream.writeframes((np.clip(stereo,-1,1)*32767).astype('<i2').tobytes())
    return path


def posters(kind):
    image=Image.fromarray(render_frame(23.2,kind)).convert('RGB')
    image.save(ASSETS/f'{STEM}-{kind}.webp',quality=91)
    if kind=='wide':
        image.resize((1200,675),Image.Resampling.LANCZOS).crop((0,22,1200,652)).save(ASSETS/f'{STEM}-cover.jpg',quality=92)


def frame_bytes(args):
    index,kind=args
    return render_frame(index/FPS,kind).tobytes()


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--format',choices=['wide','portrait','all'],default='all');parser.add_argument('--posters-only',action='store_true');args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);audio=score();ffmpeg=imageio_ffmpeg.get_ffmpeg_exe();report=[]
    for kind in (['wide','portrait'] if args.format=='all' else [args.format]):
        posters(kind)
        if args.posters_only:continue
        w,h=SIZES[kind];target=ASSETS/f'{STEM}-{kind}.mp4'
        cmd=[ffmpeg,'-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgba','-s',f'{w}x{h}','-r',str(FPS),'-i','pipe:0','-i',str(audio),'-t',str(DURATION),'-c:v','libx264','-preset','medium','-crf','20','-pix_fmt','yuv420p','-c:a','aac','-b:a','128k','-movflags','+faststart',str(target)]
        process=subprocess.Popen(cmd,stdin=subprocess.PIPE)
        with ProcessPoolExecutor(max_workers=4) as pool:
            # Bound completed image buffers while rendering independent frames.
            for start in range(0,FPS*DURATION,12):
                batch=[(i,kind) for i in range(start,min(start+12,FPS*DURATION))]
                for pixels in pool.map(frame_bytes,batch):process.stdin.write(pixels)
                if start%120==0:print(f'{kind}: {start}/{FPS*DURATION}',flush=True)
        process.stdin.close();assert process.wait()==0
        decoded=subprocess.run([ffmpeg,'-hide_banner','-i',str(target),'-f','null','-'],capture_output=True,check=True).stderr.decode('utf8',errors='replace')
        assert '00:00:24.00' in decoded and 'h264' in decoded and 'aac' in decoded
        report.append({'format':kind,'source':'Imagegen illustrated shell/content sprites, independently animated layers, Skia camera and paths; no Blender, GUI or browser','sequence_frames':FPS*DURATION,'fps':FPS,'duration_seconds':DURATION,'delivery_size':[w,h],'mp4_bytes':target.stat().st_size,'full_h264_aac_decode':True,'hero_moves':'2.15-2.55s macro zipper push; 2.65-4.00s opaque shell halves peel independently; 3.80-5.25s pullback reveals nested contents; 21.30-22.15s full illustration settles with brand'})
    if report:
        file=OUT/'media.json';old=json.loads(file.read_text(encoding='utf8')) if file.exists() else [];combined={item['format']:item for item in old+report};file.write_text(json.dumps(list(combined.values()),ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
