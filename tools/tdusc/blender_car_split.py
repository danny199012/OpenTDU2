import bpy, math, sys
from mathutils import Vector, Matrix
SCALE=float(sys.argv[-1]) if sys.argv[-1].replace('.','').isdigit() else 1.0
OUT=r"C:/temp/otdu2-tdusc-mods/props/"
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=r"E:/TDUSC Rips/Cars/Lambo fixed.fbx")
sc=bpy.context.scene
best={}
for o in sc.objects:
    if o.type!='MESH':continue
    cs=[o.matrix_world@Vector(c) for c in o.bound_box]
    ctr=[sum(c[i] for c in cs)/8 for i in range(3)]
    k=(len(o.data.polygons),round(ctr[0],2),round(ctr[1],2),round(ctr[2],2))
    has=bool(o.data.materials and o.data.materials[0])
    if k not in best or (has and not best[k][1]): best[k]=(o,has)
cls={'body':[],'glass':[],'rim':[],'tire':[]}
for k,(o,has) in best.items():
    m=o.data.materials[0].name if has else ''
    if m in('mat_66','mat_67'):cls['glass'].append(o)
    elif k[0]==5596:cls['rim'].append(o)
    elif k[0]==2464:cls['tire'].append(o)
    else:cls['body'].append(o)
print({c:len(v) for c,v in cls.items()})
allo=[o for v in cls.values() for o in v]
# bake world transforms: delete unused, apply
for o in list(sc.objects):
    if o not in allo: bpy.data.objects.remove(o,do_unlink=True)
for o in allo:
    mw=o.matrix_world.copy();o.parent=None;o.matrix_world=mw
    o.data.transform(o.matrix_world);o.matrix_world=Matrix.Identity(4)
ZS=0.565
for o in allo:o.data.transform(Matrix.Diagonal((1,1,ZS,1)))
# tyre UVs: axle = world Y; tread -> left half of the atlas, sidewall -> right half
import bmesh
for o in cls['tire']:
    me=o.data
    cs=[v.co for v in me.vertices]
    c=Vector([(min(p[i] for p in cs)+max(p[i] for p in cs))/2 for i in range(3)])
    R=max(max(abs(p.x-c.x),abs(p.z-c.z)) for p in cs)
    ys=[p.y-c.y for p in cs];smin,smax=min(ys),max(ys)
    uv=me.uv_layers.new(name="atlas") if "atlas" not in me.uv_layers else me.uv_layers["atlas"]
    for poly in me.polygons:
        nrm=poly.normal
        side=abs(nrm.y)>0.5
        th0=None
        for li in poly.loop_indices:
            p=me.vertices[me.loops[li].vertex_index].co-c
            if side:
                u=0.75+0.24*(p.x/R);v=0.5+0.48*(p.z/R)
            else:
                th=math.atan2(p.z,p.x)
                if th0 is None:th0=th
                while th-th0>math.pi:th-=2*math.pi
                while th-th0<-math.pi:th+=2*math.pi
                u=0.5*(p.y-smin)/(smax-smin);v=th/(2*math.pi)*8
            uv.data[li].uv=(u,v)
    me.uv_layers.active=uv
# global transform: rotate -90 about Z (+X front -> -Y), scale, centre, ground
R=Matrix.Rotation(-math.pi/2,4,'Z')@Matrix.Scale(SCALE,4)
for o in allo:o.data.transform(R)
pts=[v.co for o in allo for v in o.data.vertices]
mn=Vector((min(p[i] for p in pts) for i in range(3)));mx=Vector((max(p[i] for p in pts) for i in range(3)))
rz=min(v.co.z for o in cls['tire'] for v in o.data.vertices)
off=Vector(((mn.x+mx.x)/2,(mn.y+mx.y)/2,rz))
for o in allo:o.data.transform(Matrix.Translation(-off))
print("DIM",tuple(round(x,3) for x in (mx-mn)),"ground z",rz)
# interior: body-class meshes whose whole bbox sits inside the cabin box (car space: x width,
# y length with the nose at -Y, z up from the tyre bottom) become their own dark group [INF heuristic]
cls['interior']=[]
for o in list(cls['body']):
    cs=[v.co for v in o.data.vertices]
    mn=[min(p[i] for p in cs) for i in range(3)];mx=[max(p[i] for p in cs) for i in range(3)]
    if mn[0]>-0.95 and mx[0]<0.95 and mn[1]>-1.15 and mx[1]<1.0 and mn[2]>0.12 and mx[2]<0.98:
        cls['interior'].append(o);cls['body'].remove(o)
print('interior meshes',len(cls['interior']),'body',len(cls['body']))
COL={'interior':(0.08,0.08,0.09),'body':(0.55,0.02,0.02),'glass':(0.03,0.05,0.07),'rim':(0.7,0.7,0.72),'tire':(0.02,0.02,0.02)}
for c,objs in cls.items():
    if not objs:continue
    mat=bpy.data.materials.new("lambo_"+c);mat.diffuse_color=COL[c]+(1,)
    mat.use_nodes=True;mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=COL[c]+(1,)
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.data.materials.clear();o.data.materials.append(mat);o.select_set(True)
    bpy.context.view_layer.objects.active=objs[0]
    bpy.ops.wm.obj_export(filepath=OUT+"tdusc_lambo_"+c+".obj",export_selected_objects=True,export_materials=True,export_normals=True,export_uv=True,forward_axis='NEGATIVE_Z',up_axis='Y')
