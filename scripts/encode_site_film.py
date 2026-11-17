"""Export the vector icon workbench film, original score and responsive posters."""
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
STEM='hero-icons-polish-20261117'


def score():
    rate=48000;t=np.arange(rate*DURATION)/rate;mix=np.zeros_like(t)
    rng=np.random.default_rng(20261117)
    noise=rng.standard_normal(t.size);noise=np.convolve(noise,np.ones(18)/18,mode='same')
    # Quiet original harmonic bed connects the action accents across the film.
    bed=np.minimum(np.maximum((t-3.6)/1.2,0),1)*np.minimum(np.maximum((DURATION-t)/2,0),1)
    for frequency in [110,164.81,220]:mix+=.007*bed*np.sin(2*np.pi*frequency*t)
    for start in [3.6,4.3,7.2,10.15,15.05,19.6,22.1]:
        local=np.maximum(t-start,0);active=t>=start;env=np.exp(-local*6)*active
        phase=2*np.pi*(46*local+80/14*(1-np.exp(-14*local)))
        mix+=.13*env*np.sin(phase)
    for start in [4.15,6.6,10.0,13.5,15.38,18.0,22.0]:
        local=t-start;air=np.exp(-((local-.22)/.18)**2);air[local<0]=0
        mix+=noise*air*.085
    # Click/drop cues occur on the same clock as visible pointer hits.
    for start in [1.24,2.62,3.6,21.2]:
        local=np.maximum(t-start,0);env=np.exp(-local*130)*(t>=start)
        mix+=.045*env*np.sin(2*np.pi*1800*local)+.035*env*noise
    for start in [9.65,12.75,19.6,23.2]:
        local=np.maximum(t-start,0);env=np.minimum(local*65,1)*np.exp(-local*(1.8 if start==23.2 else 3.5))*(t>=start)
        for freq in [261.63,392,523.25]:mix+=.03*env*np.sin(2*np.pi*freq*local)
    mix*=np.minimum(t/.08,1)*np.minimum((DURATION-t)/.7,1)
    mix*=2.5
    stereo=np.stack([mix,mix*.94+np.roll(mix,288)*.04],axis=1)
    path=OUT/'original-icons-score.wav'
    with wave.open(str(path),'wb') as stream:
        stream.setnchannels(2);stream.setsampwidth(2);stream.setframerate(rate);stream.writeframes((np.clip(stereo,-1,1)*32767).astype('<i2').tobytes())
    return path


def posters(kind):
    image=Image.fromarray(render_frame(24.5,kind)).convert('RGB')
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
        assert '00:00:28.00' in decoded and 'h264' in decoded and 'aac' in decoded
        report.append({'format':kind,'source':'Microsoft Fluent SVG icon paths and native T mark, continuous shared package and output actors, one workbench/cursor/camera timeline; no raster illustration, Blender, GUI or browser','sequence_frames':FPS*DURATION,'fps':FPS,'duration_seconds':DURATION,'delivery_size':[w,h],'mp4_bytes':target.stat().st_size,'full_h264_aac_decode':True,'hero_moves':'3.6s start/full-bleed; 6.6-8.65s ZIP emits volume actors; 10.15-14.4s one package gathers/unlocks/moves; 15.05-16.35s foreground ZIP reveal; output actors persist 15.45-28s through result and brand'})
    if report:
        file=OUT/'media.json';old=json.loads(file.read_text(encoding='utf8')) if file.exists() else [];combined={item['format']:item for item in old+report};file.write_text(json.dumps(list(combined.values()),ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
