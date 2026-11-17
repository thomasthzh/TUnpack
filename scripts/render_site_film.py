"""Original physically rendered hero film: Blender/Cycles, no GUI or browser.
Run with Python 3.13 + bpy (or Blender background), then --format wide|portrait.
The old illustrative media URLs remain for cached pages, not as the hero.
"""
import argparse
import math
from pathlib import Path
import time

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/evidence/hero-cinema"
FPS, SECONDS = 24, 12


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


def tmark(mat):
    outline = [(-1.11, .93), (1.11, .93), (1.11, .55), (.22, .55), (.22, -.92), (-.22, -.92), (-.22, .55), (-1.11, .55)]
    vertices = [(x, y, z) for y in [-.495, -.60] for x, z in outline]
    n = len(outline)
    faces = [tuple(range(n-1, -1, -1)), tuple(range(n, 2*n))]
    faces += [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
    mesh = bpy.data.meshes.new("Solid T mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("Solid platinum T", mesh)
    bpy.context.collection.objects.link(obj)
    bevel = obj.modifiers.new("Precision bevel", "BEVEL")
    bevel.width = .035
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


def scene_setup(portrait=False):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 12 if portrait else 16
    scene.render.use_persistent_data = True
    scene.cycles.use_denoising = True
    scene.cycles.device = 'GPU'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'OPTIX'
    prefs.get_devices()
    for device in prefs.devices:
        device.use = device.type == 'OPTIX'
    scene.render.resolution_x,scene.render.resolution_y = (864,1080) if portrait else (1440,900)
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.film_transparent = False
    scene.render.fps = FPS
    scene.view_settings.view_transform = 'AgX'
    scene.world = bpy.data.worlds.new("Black studio")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (.025,.03,.035,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value = .16
    shell = material("Graphite titanium", (.022,.03,.032), .88, .29)
    inner = material("Deep mineral green", (.035,.16,.125), .72, .24)
    silver = material("Satin platinum", (.63,.68,.66), .97, .23)
    paper = material("Ivory content layers", (.72,.76,.68), .28, .30)
    dark = material("Dark polished ground", (.009,.012,.013), .55, .24)
    # A real PBR microtexture, not a painted surrounding glow.
    tree = shell.node_tree
    noise = tree.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=180
    bump = tree.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.04;bump.inputs['Distance'].default_value=.003
    tree.links.new(noise.outputs['Fac'],bump.inputs['Height'])
    tree.links.new(bump.outputs['Normal'],tree.nodes['Principled BSDF'].inputs['Normal'])
    plates=[]
    for index in range(3):
        plate=block(f"Shell {index}",(3.22-index*.06,.24,3.62-index*.05),(0,index*.28,0),shell if index==0 else inner,.23)
        plates.append(plate)
    front=tmark(silver)
    front.parent=plates[0]
    # Modelled details inherit the same shell: physically continuous through shots.
    for i in range(10):
        tooth=block(f"Zipper tooth {i}",(.075,.045,.085),(-.068 if i%2 else .068,-.625,.30-i*.115),inner,.012)
        tooth.parent=plates[0]
    sill=block("Lower seam",(2.3,.06,.035),(0,-.42,-1.42),silver,.015);sill.parent=plates[0]
    contents=[]
    for i in range(6):
        obj=block(f"Unfolding content {i}",(2.70,.028,3.05),(0,.14+i*.035,0),paper,.09)
        contents.append(obj)
    block("Studio floor",(200,200,.12),(0,0,-2.32),dark,.02)
    key=area("Long white key",(-3.4,-4.5,6.8),1600,5,(.88,.94,1))
    rim=area("Warm edge",(4,2.5,4),2100,3,(1,.84,.63))
    area("Soft frontal fill",(-1,-6,1),60,4,(.78,.86,1))
    area("Narrow back rim",(-4,3,1),1200,2,(.72,1,.91))
    bpy.ops.object.camera_add()
    camera=bpy.context.object
    scene.camera=camera
    camera.data.lens=58
    camera.data.dof.use_dof=True
    camera.data.dof.aperture_fstop=5.6
    camera.data.clip_start=.01
    return scene, camera, plates, contents, key, rim


def pose(objects, seconds, portrait):
    scene,camera,plates,contents,key,rim=objects
    # Three authored shots: hidden detail -> decisive opening -> complete form.
    if seconds<3.2:
        p=smooth(seconds/3.2)
        camera.location=(1.65+1.0*p,-3.25-.65*p,.90+.8*p)
        target=Vector((.35,-.33,.52))
        camera.data.lens=75
        camera.data.dof.aperture_fstop=3.5
    elif seconds<8.2:
        p=smooth((seconds-3.2)/5)
        camera.location=(5.4-2.0*p,-9.4,3.6-.3*p)
        target=Vector((0,.45,.03))
        camera.data.lens=62 if portrait else 43
        camera.data.dof.aperture_fstop=8
    else:
        p=smooth((seconds-8.2)/3.8)
        camera.location=(3.4-7.0*p,-9.4,3.3-1.3*p)
        target=Vector((0,.55,.05))
        camera.data.lens=62 if portrait else 43
        camera.data.dof.aperture_fstop=8
    # Strong speed contrast around the 4-second opening; quiet readable finish.
    opening=smooth((seconds-3.75)/1.35)
    settle=smooth((seconds-9.2)/1.9)
    for i,obj in enumerate(plates):
        obj.location=(0, i*.28 + opening*(i-1)*.82, opening*(i-1)*.12)
        obj.rotation_euler=(0,0,math.radians(opening*(i-1)*-6))
    for i,obj in enumerate(contents):
        reveal=smooth((seconds-4.45-i*.07)/1.8)
        obj.location=((i-2.5)*.085*reveal,.12+i*.035+(i-2.5)*.23*reveal,.09*math.sin(i)*reveal)
        obj.rotation_euler=(0,0,math.radians((i-2.5)*3.8*reveal))
    key.location.x=-4+7*smooth(seconds/5)
    key.data.energy=50+1900*smooth((seconds-.5)/2.0)
    rim.data.energy=150+2150*smooth((seconds-1.5)/2.0)
    if portrait:
        # Reframe the same physical subject, not a crop of the desktop pixels.
        if seconds<3.2:
            camera.data.lens=60
        target.z+=.03
    camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.dof.focus_distance=(target-camera.location).length
    scene.frame_set(int(seconds*FPS)+1)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--format',choices=['wide','portrait'],default='wide')
    parser.add_argument('--stills',action='store_true')
    parser.add_argument('--start',type=int,default=1)
    parser.add_argument('--end',type=int,default=FPS*SECONDS)
    args=parser.parse_args()
    portrait=args.format=='portrait'
    directory=OUT/args.format
    directory.mkdir(parents=True,exist_ok=True)
    objects=scene_setup(portrait)
    frames=[12,90,160,268] if args.stills else range(args.start,args.end+1)
    for frame in frames:
        pose(objects,(frame-1)/FPS,portrait)
        objects[0].render.filepath=str(directory/f'{frame:04d}.png')
        started=time.perf_counter()
        bpy.ops.render.render(write_still=True)
        print(f'HERO {args.format} frame {frame} {time.perf_counter()-started:.2f}s',flush=True)


if __name__=='__main__':
    main()
