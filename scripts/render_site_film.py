"""Original physically rendered hero film: Blender/Cycles, no GUI or browser.
Run with Python 3.13 + bpy (or Blender background), then --format wide|portrait.
The old illustrative media URLs remain for cached pages, not as the hero.
"""
import argparse
import math
import json
from pathlib import Path
import shutil
import time

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/evidence/hero-product"
FPS, SECONDS = 24, 20


def smooth(v):
    v = max(0, min(1, v))
    return v * v * (3 - 2 * v)


def material(name, color, metal=0, rough=.3):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    node = mat.node_tree.nodes.get("Principled BSDF")
    node.inputs["Base Color"].default_value = (*color, 1)
    node.inputs["Metallic"].default_value = metal
    node.inputs["Roughness"].default_value = rough
    return mat


def block(name, size, location, mat, radius=.08):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel = obj.modifiers.new("Machined edge", "BEVEL")
    bevel.width = radius
    bevel.segments = 5
    obj.modifiers.new("Weighted normals", "WEIGHTED_NORMAL")
    obj.data.materials.append(mat)
    return obj


def area(name, location, energy, size, color, target=(0,0,0), shape="DISK"):
    data = bpy.data.lights.new(name, "AREA")
    data.energy = energy
    data.color = color
    data.shape = shape
    data.size = size
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    obj.location = location
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    return obj


def label(parent, body, xyz, size, mat, name="Label"):
    curve=bpy.data.curves.new(name,'FONT')
    curve.body=body;curve.size=size;curve.align_x='CENTER';curve.align_y='CENTER'
    curve.font=bpy.data.fonts.load('C:/Windows/Fonts/msyh.ttc',check_existing=True)
    curve.extrude=.001
    obj=bpy.data.objects.new(name,curve);bpy.context.collection.objects.link(obj)
    obj.parent=parent;obj.location=xyz;obj.rotation_euler=(math.pi/2,0,0)
    obj.data.materials.append(mat)
    return obj


def group(name):
    obj=bpy.data.objects.new(name,None);bpy.context.collection.objects.link(obj);return obj


def file_card(parent,name,kind,mat,ink,width=2.4,height=2.8):
    root=group(name);root.parent=parent
    plate=block(name+' body',(width,.17,height),(0,0,0),mat,.12);plate.parent=root
    label(root,kind,(0,-.10,.35),.50,ink)
    label(root,name,(0,-.105,-height*.32),.20 if width<2 else .26,ink)
    return root


def visible(root,on):
    for obj in [root,*root.children_recursive]:obj.hide_render=not on


def check(parent,location,mat):
    root=group('Verified check');root.parent=parent;root.location=location
    for points in [[(-.18,0,0),(0,0,-.15)],[(0,0,-.15),(.30,0,.22)]]:
        start,end=map(Vector,points);direction=end-start
        obj=block('Check stroke',(.075,.045,direction.length),(0,0,0),mat,.025)
        obj.parent=root;obj.location=(start+end)/2
        obj.rotation_euler=direction.to_track_quat('Z','Y').to_euler()
    return root


def scene_setup(portrait=False):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=8
    scene.render.use_persistent_data=True;scene.cycles.use_denoising=True;scene.cycles.device='GPU'
    scene.cycles.max_bounces=4;scene.cycles.diffuse_bounces=1;scene.cycles.glossy_bounces=2
    prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
    for device in prefs.devices:device.use=device.type=='OPTIX'
    scene.render.resolution_x,scene.render.resolution_y=(720,900) if portrait else (1152,720)
    scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB';scene.render.fps=FPS
    scene.view_settings.view_transform='AgX'
    scene.world=bpy.data.worlds.new('Product studio');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.001,.001,.001,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value=.25
    navy=material('MP4 facade',(.035,.075,.105),.35,.29)
    green=material('Archive content',(.035,.22,.14),.55,.27)
    paper=material('Readable file face',(.70,.73,.64),.12,.38)
    gold=material('Content signal',(.78,.48,.13),.6,.28)
    ink=material('Ivory lettering',(.88,.93,.85),0,.7)
    dark=material('Dark ink',(.025,.055,.045),0,.7)
    metal=material('Key and lock',(.52,.59,.58),.85,.25)
    ground=material('Studio ground',(.011,.017,.019),.25,.4)
    block('Ground',(200,200,.1),(0,0,-2.25),ground,.02)
    area('Large key',(-4,-5,7),1800,5,(.88,.95,1))
    area('Warm rim',(4,3,5),2000,4,(1,.86,.66))
    area('Front readability',(0,-6,3),650,4,(.9,1,.95))
    roots={name:group(name) for name in ['identify','volumes','password','nested','result']}
    # The name is retained on the displaced facade; content is recognised, not renamed.
    inner=file_card(roots['identify'],'内容：ZIP','ZIP',green,ink,2.7,3.2)
    inner.location=(.75,.15,0)
    facade=file_card(roots['identify'],'旅途.mp4','.MP4',navy,ink,2.7,3.2)
    facade.location=(.75,-.16,0)
    parts=[]
    for i,name in enumerate(['项目.7z.001','项目.7z.002','附件.docx']):
        part=file_card(roots['volumes'],name,'7Z' if i<2 else '.DOCX',green if i<2 else navy,ink,1.75,1.3 if portrait else 2.25)
        parts.append(part)
    group_ok=check(roots['volumes'],(0,-.5,-1.48),green)
    archive=file_card(roots['password'],'已加密的卷组','7Z',green,ink,2.5,3.0)
    archive.location=(.9,.2,0)
    book=file_card(roots['password'],'已保存候选','密码本',navy,ink,1.5,2.0);book.location=(-1.8,0,0)
    lock=group('Lock');lock.parent=roots['password'];lock.location=(.9,-.30,0)
    body=block('Lock body',(.68,.25,.53),(0,0,0),gold,.09);body.parent=lock
    bpy.ops.mesh.primitive_torus_add(major_radius=.23,minor_radius=.045,location=(0,0,0))
    shackle=bpy.context.object;shackle.parent=lock;shackle.location=(0,0,.38);shackle.rotation_euler.x=math.pi/2;shackle.data.materials.append(metal)
    key=group('Remembered key');key.parent=roots['password']
    bpy.ops.mesh.primitive_torus_add(major_radius=.16,minor_radius=.045)
    ring=bpy.context.object;ring.parent=key;ring.rotation_euler.x=math.pi/2;ring.data.materials.append(gold)
    shaft=block('Key shaft',(.7,.065,.075),(.45,0,0),gold,.025);shaft.parent=key
    tooth=block('Key tooth',(.085,.065,.17),(.7,0,-.06),gold,.02);tooth.parent=key
    password_ok=check(roots['password'],(.9,-.6,1.1),green)
    outer=file_card(roots['nested'],'卷组：外层7Z','7Z',green,ink,2.7,3.2)
    nested=file_card(roots['nested'],'内层.zip','ZIP',navy,ink,2.05,2.5);nested.location=(0,.2,0)
    data_files=[]
    for i,name in enumerate(['照片.jpg','说明.txt']):
        obj=file_card(roots['nested'],name,'JPG' if i==0 else 'TXT',paper,dark,1.25,1.65);obj.location=(0,.4,0);data_files.append(obj)
    # Output remains still while the source alone moves into recycling, after success.
    folder=file_card(roots['result'],'输出目录','文件已就位',green,ink,2.7,2.35);folder.location=(-.55,.2,0)
    output_files=[]
    for i,name in enumerate(['照片.jpg','说明.txt']):
        obj=file_card(roots['result'],name,'JPG' if i==0 else 'TXT',paper,dark,1.1,1.45);obj.location=(-1.05+i*.95,-.18,.18);output_files.append(obj)
    source=file_card(roots['result'],'旅途.mp4','.MP4',navy,ink,1.05,1.4);source.location=(2.4,-.05,1.5)
    bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=.53,depth=.9,end_fill_type='NOTHING')
    bin=bpy.context.object;bin.name='Recycle bin';bin.parent=roots['result'];bin.location=(2.45,.1,-1.15);bin.data.materials.append(metal)
    label(roots['result'],'回收站',(2.45,-.5,-1.85),.20,ink)
    result_ok=check(roots['result'],(-.55,-.5,1.6),green)
    bpy.ops.object.camera_add();camera=bpy.context.object;scene.camera=camera;camera.data.lens=46;camera.data.clip_start=.01
    return dict(scene=scene,camera=camera,roots=roots,facade=facade,parts=parts,group_ok=group_ok,key=key,shackle=shackle,password_ok=password_ok,outer=outer,nested=nested,data_files=data_files,source=source,result_ok=result_ok)


def pose(objects,seconds,portrait):
    scene=objects['scene'];camera=objects['camera']
    chapter=0 if seconds<4.8 else 1 if seconds<8 else 2 if seconds<11 else 3 if seconds<16 else 4
    names=['identify','volumes','password','nested','result']
    for name,root in objects['roots'].items():visible(root,name==names[chapter])
    camera.location=(2.1,-11.4,3.1)
    target=Vector((0,.15,0))
    camera.data.lens=46
    if chapter==0:
        p=smooth((seconds-1.25)/2.1)
        objects['facade'].location=(-2.1*p+.75,-.16-.12*p,-.12*p)
        objects['facade'].rotation_euler.z=-.10*p
        camera.location=(2.6-.7*p,-10.5-.7*p,2.6+.4*p)
    elif chapter==1:
        t=seconds-4.8;fan=smooth(t/.8);join=smooth((t-1.65)/1.4)
        for i,obj in enumerate(objects['parts']):
            if portrait:
                obj.location=((i-1)*.08*join, i*.08*join, (1-i)*(1.6-.1*join)*fan)
            else:
                obj.location=((i-1)*(2.12-.17*join)*fan,i*.08*join,0)
            obj.rotation_euler.z=(i-1)*.035*(1-join)
        visible(objects['group_ok'],t>2.15)
    elif chapter==2:
        t=seconds-8;verified=smooth((t-.8)/1.1)
        objects['key'].location=(-1.65+2.05*verified,-.65,.35)
        objects['shackle'].location.z=.38+.24*smooth((t-1.75)/.55)
        visible(objects['password_ok'],t>1.7)
    elif chapter==3:
        t=seconds-11;first=smooth(t/1.4);second=smooth((t-1.9)/1.3)
        objects['outer'].location=(-2.65*first,0,-.1*first);objects['outer'].rotation_euler.z=-.12*first
        objects['nested'].location=(2.55*second,.2,0);objects['nested'].rotation_euler.z=.1*second
        for i,obj in enumerate(objects['data_files']):
            p=smooth((t-2.5)/1.3);obj.location=((i-.5)*1.42*p,-.15,.02);obj.scale=(p,p,p)
    else:
        t=seconds-16;done=t>.5;recycle=smooth((t-1.4)/1.7)
        visible(objects['result_ok'],done)
        source=objects['source'];source.location=(2.4,.05,1.5-2.6*recycle)
        source.rotation_euler.y=.16*recycle
        source.scale=(1-.62*recycle,)*3
        if recycle>.96:visible(source,False)
    if portrait:
        camera.location.x=.7
        camera.location.z=2.6
    camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    scene.frame_set(int(seconds*FPS)+1)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--format',choices=['wide','portrait'],default='wide')
    parser.add_argument('--stills',action='store_true')
    parser.add_argument('--start',type=int,default=1)
    parser.add_argument('--end',type=int,default=FPS*SECONDS)
    args=parser.parse_args();portrait=args.format=='portrait'
    directory=OUT/args.format;directory.mkdir(parents=True,exist_ok=True)
    objects=scene_setup(portrait)
    frames=[24,84,159,234,331,453] if args.stills else range(args.start,args.end+1)
    poses={};rendered=reused=0
    for frame in frames:
        pose(objects,(frame-1)/FPS,portrait);objects['scene'].render.filepath=str(directory/f'{frame:04d}.png')
        # Holds share exactly the same camera/object state. Reuse their identical
        # rendered pixels instead of spending GPU time on the same still pose.
        state=tuple((obj.name,tuple(obj.location),tuple(obj.rotation_euler),tuple(obj.scale),obj.hide_render) for obj in bpy.context.scene.objects)
        target=directory/f'{frame:04d}.png'
        if state in poses:
            shutil.copyfile(poses[state],target);reused+=1
            print(f'STORY {args.format} {frame} held pose',flush=True)
            continue
        start=time.perf_counter();bpy.ops.render.render(write_still=True)
        poses[state]=target;rendered+=1
        print(f'STORY {args.format} {frame} {time.perf_counter()-start:.2f}s',flush=True)
    if not args.stills:
        (OUT/f'{args.format}-render-stats.json').write_text(json.dumps({'start':args.start,'end':args.end,'unique_rendered_poses':rendered,'held_frames_reused':reused}),encoding='utf8')


if __name__=='__main__':main()
