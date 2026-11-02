"""hero_rig_blender.py -- run INSIDE Blender by tools/art/make_hero_rig.py (docs/engine/20_CHARACTER_ART.md §4):

    blender -b --factory-startup -P tools/art/hero_rig_blender.py -- --model M.glb --joints joints.json --tris N --out O.glb

The coloured hero (make_hero_3d.py) -> fewer triangles -> a skeleton on the concept's joints -> automatic weights ->
a bow in the left hand and its string -> the clips -> a skinned GLB with the clips.
The colours are baked into a texture (_colour.png beside the GLB, packed into it).

Blender space: z up, the character's front toward -y, its left hand toward +x (the glTF file: y up, the front +z).
The bow shoots toward +x: the chest faces the camera and the arms work side to side, the archer's stance for a battle
seen from the front with the enemy on the right.
"""
import json
import math
import sys

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

FPS = 30
TEXTURE = 2048   # the baked colour texture's size
VOXEL = 0.006   # the remesh grid (model units; the hero is about 2 tall): finer keeps the hair and fingers


def log(*a):
    print('[rig]', *a, flush=True)


def args():
    argv = sys.argv[sys.argv.index('--') + 1:]
    out = {}
    for i in range(0, len(argv), 2):
        out[argv[i].lstrip('-')] = argv[i + 1]
    return out


def B(p):
    """glTF (x, y up, z front) -> Blender (x, -z, y)."""
    return Vector((p[0], -p[2], p[1]))


# ---- the mesh ----

def drop_crumbs(mesh, min_verts):
    """Delete the loose pieces smaller than min_verts (the remesh's crumbs at the hair tips and fingers): the
    automatic weights find no solution at all with them."""
    bm = bmesh.new()
    bm.from_mesh(mesh.data)
    seen, crumbs = set(), []
    for v in bm.verts:
        if v in seen:
            continue
        part, stack = [], [v]
        seen.add(v)
        while stack:
            x = stack.pop()
            part.append(x)
            for e in x.link_edges:
                o = e.other_vert(x)
                if o not in seen:
                    seen.add(o)
                    stack.append(o)
        if len(part) < min_verts:
            crumbs += part
    bmesh.ops.delete(bm, geom=crumbs, context='VERTS')
    bm.to_mesh(mesh.data)
    bm.free()
    log(f'crumbs: {len(crumbs)} vertices in small loose pieces deleted')

def load_mesh(path, tris):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = FPS   # (the empty scene runs at 24: the clips would play slow)
    bpy.ops.import_scene.gltf(filepath=path)
    mesh = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
    mesh.name = 'hero'
    bpy.context.view_layer.objects.active = mesh
    mesh.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # the colours per vertex (the importer puts them on the face corners: every corner a seam the decimation keeps)
    col = mesh.data.color_attributes.active_color
    if col is not None and col.domain != 'POINT':
        bpy.ops.geometry.color_attribute_convert(domain='POINT', data_type='FLOAT_COLOR')
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.0005)
    bpy.ops.mesh.dissolve_degenerate()
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    n0 = len(mesh.data.polygons)
    # The surface net leaves edges shared by 3-4 faces (the thin cape), which the decimation cannot collapse: rebuild
    # a clean closed surface (voxel remesh), copy the colours back from the original, then cut it down.
    src = mesh.copy()
    src.data = mesh.data.copy()
    bpy.context.scene.collection.objects.link(src)
    mesh.data.remesh_voxel_size = VOXEL
    bpy.ops.object.voxel_remesh()
    if not mesh.data.color_attributes:
        mesh.data.color_attributes.new('Color', 'FLOAT_COLOR', 'POINT')
    mesh.data.color_attributes.active_color = mesh.data.color_attributes[0]
    t = mesh.modifiers.new('colours', 'DATA_TRANSFER')
    t.object = src
    t.use_vert_data = True
    t.data_types_verts = {'COLOR_VERTEX'}
    t.vert_mapping = 'POLYINTERP_NEAREST'
    bpy.ops.object.modifier_apply(modifier=t.name)
    log(f'remesh: {n0} -> {len(mesh.data.polygons)} faces (voxel {VOXEL})')
    n0 = len(mesh.data.polygons)
    d = mesh.modifiers.new('decimate', 'DECIMATE')
    d.ratio = min(1.0, tris / max(n0, 1))
    bpy.ops.object.modifier_apply(modifier=d.name)
    drop_crumbs(mesh, 300)
    bpy.ops.object.shade_smooth()
    log(f'mesh: {n0} -> {len(mesh.data.polygons)} faces, colours: {[a.name for a in mesh.data.color_attributes]}')
    return mesh, src


def bake_texture(mesh, src, png, size=TEXTURE):
    """The colours as a texture: vertex colours on the cut-down mesh are too coarse for the face. The cut-down mesh is
    unwrapped, and the full mesh's vertex colours (src) are baked onto it (Cycles, emission, selected to active)."""
    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = mesh
    mesh.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.002)
    bpy.ops.object.mode_set(mode='OBJECT')
    img = bpy.data.images.new('hero_colour', size, size)
    # the source shows its vertex colours as light
    ms = bpy.data.materials.new('source')
    ms.use_nodes = True
    nt = ms.node_tree
    nt.nodes.clear()
    vc = nt.nodes.new('ShaderNodeVertexColor')
    vc.layer_name = src.data.color_attributes.active_color.name
    em = nt.nodes.new('ShaderNodeEmission')
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    nt.links.new(vc.outputs['Color'], em.inputs['Color'])
    nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
    src.data.materials.clear()
    src.data.materials.append(ms)
    # the hero: the baked texture as the base colour (matte: a painting, not a photo)
    mh = bpy.data.materials.new('painted')
    mh.use_nodes = True
    nt = mh.node_tree
    bsdf = nt.nodes['Principled BSDF']
    bsdf.inputs['Roughness'].default_value = 0.9
    bsdf.inputs['Metallic'].default_value = 0.0
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = img
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    nt.nodes.active = tex                                 # the bake writes into the active image node
    mesh.data.materials.clear()
    mesh.data.materials.append(mh)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = 1
    src.hide_render = False
    src.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=True, cage_extrusion=0.03, max_ray_distance=0.08,
                        margin=8)
    img.filepath_raw = png
    img.file_format = 'PNG'
    img.save()
    bpy.data.objects.remove(src)
    # the colours now live in the texture
    while mesh.data.color_attributes:
        mesh.data.color_attributes.remove(mesh.data.color_attributes[0])
    log(f'texture: {size} x {size} baked -> {png}')


# ---- the skeleton ----

def build_armature(j):
    """Bones on the joints. Names: <part>.L / .R = the character's left / right (OpenPose l_ / r_)."""
    arm_data = bpy.data.armatures.new('rig')
    arm = bpy.data.objects.new('rig', arm_data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm_data.edit_bones

    def bone(name, head, tail, parent=None, connect=False, deform=True):
        b = eb.new(name)
        b.head, b.tail = head, tail
        b.roll = 0.0
        if parent:
            b.parent = eb[parent]
            b.use_connect = connect
        b.use_deform = deform
        return b

    P = {k: B(v) for k, v in j.items() if k not in ('cape', 'ground')}
    ground = j['ground']
    hips = (P['r_hip'] + P['l_hip']) / 2
    neck = P['neck']
    chest = hips.lerp(neck, 0.55)
    head_base = neck.lerp(P['nose'], 0.35)
    bone('root', Vector((hips.x, hips.y, ground)), Vector((hips.x, hips.y, ground + 0.15)), deform=False)
    bone('hips', hips, hips.lerp(chest, 0.5), 'root')
    bone('spine', hips.lerp(chest, 0.5), chest, 'hips', True)
    bone('chest', chest, neck, 'spine', True)
    bone('neck', neck, head_base, 'chest', True)
    bone('head', head_base, Vector((head_base.x, head_base.y, P['head_top'].z)), 'neck', True)
    for s, side in (('l', 'L'), ('r', 'R')):
        sh, el, wr = P[f'{s}_shoulder'], P[f'{s}_elbow'], P[f'{s}_wrist']
        bone(f'shoulder.{side}', neck.lerp(sh, 0.15), sh, 'chest')
        bone(f'upper_arm.{side}', sh, el, f'shoulder.{side}', True)
        bone(f'forearm.{side}', el, wr, f'upper_arm.{side}', True)
        bone(f'hand.{side}', wr, wr + (wr - el).normalized() * 0.09, f'forearm.{side}', True)
        hp, kn, an = P[f'{s}_hip'], P[f'{s}_knee'], P[f'{s}_ankle']
        bone(f'thigh.{side}', hp, kn, 'hips')
        bone(f'shin.{side}', kn, an, f'thigh.{side}', True)
        bone(f'foot.{side}', an, Vector((an.x, an.y - 0.10, ground + 0.01)), f'shin.{side}', True)
    cape = [B(p) for p in j['cape']]
    for i in range(len(cape) - 1):
        bone(f'cape.{i}', cape[i], cape[i + 1], 'chest' if i == 0 else f'cape.{i - 1}', i > 0)
    # the string's middle: in the bow's plane at rest, pulled to the right hand while drawing (a constraint, baked)
    bone('string', Vector((0, 0, 0)), Vector((0, 0, 0.05)), 'hand.L')
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    log(f'skeleton: {len(arm_data.bones)} bones')
    return arm


def skin(mesh, arm):
    bpy.ops.object.select_all(action='DESELECT')
    mesh.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    unweighted = sum(1 for v in mesh.data.vertices if not v.groups)
    log(f'weights: {len(mesh.vertex_groups)} groups, {unweighted} of {len(mesh.data.vertices)} vertices unweighted')


# ---- posing: aim bones at directions (armature space), parents first ----

def update():
    bpy.context.view_layer.update()


def base_matrix(pb):
    """The bone's armature-space matrix with no rotation of its own (its parent as posed)."""
    if pb.parent:
        return pb.parent.matrix @ pb.parent.bone.matrix_local.inverted() @ pb.bone.matrix_local
    return pb.bone.matrix_local.copy()


def aim(arm, name, direction):
    pb = arm.pose.bones[name]
    m0 = base_matrix(pb).to_3x3()
    d0 = (m0 @ Vector((0, 1, 0))).normalized()
    q = d0.rotation_difference(Vector(direction).normalized())
    pb.rotation_quaternion = (m0.inverted() @ q.to_matrix() @ m0).to_quaternion()
    update()


def turn(arm, name, axis, degrees):
    """Rotate a bone about an armature-space axis."""
    pb = arm.pose.bones[name]
    m0 = base_matrix(pb).to_3x3()
    q = Quaternion(Vector(axis), math.radians(degrees))
    pb.rotation_quaternion = (m0.inverted() @ q.to_matrix() @ m0).to_quaternion()
    update()


def reach(arm, upper, fore, target, pole):
    """Two-bone IK by hand: the elbow bends toward `pole`; the hand at `target` (or as near as the arm reaches)."""
    pu, pf = arm.pose.bones[upper], arm.pose.bones[fore]
    a, b = pu.bone.length, pf.bone.length
    s = pu.head.copy()
    t = Vector(target)
    d = min((t - s).length, a + b - 1e-4)
    axis = (t - s).normalized()
    x = (a * a - b * b + d * d) / (2 * d)
    h = math.sqrt(max(a * a - x * x, 0.0))
    side = (Vector(pole) - axis * Vector(pole).dot(axis)).normalized()
    elbow = s + axis * x + side * h
    aim(arm, upper, elbow - s)
    aim(arm, fore, (s + axis * d) - elbow)


def rest(arm):
    for pb in arm.pose.bones:
        pb.rotation_quaternion = Quaternion()
        pb.location = Vector()
    update()


def key_all(arm, frame):
    for pb in arm.pose.bones:
        pb.keyframe_insert('rotation_quaternion', frame=frame)
        pb.keyframe_insert('location', frame=frame)


# ---- the poses ----

def pose_idle(arm, breathe=0.0):
    """Arms down at the sides, the bow in the left hand; `breathe` 0..1 lifts the chest a little."""
    rest(arm)
    turn(arm, 'chest', (1, 0, 0), 2.0 * breathe)
    turn(arm, 'head', (1, 0, 0), -1.5 * breathe)
    for side, sx in (('L', 1), ('R', -1)):
        aim(arm, f'upper_arm.{side}', (0.28 * sx, -0.05, -1))
        aim(arm, f'forearm.{side}', (0.12 * sx, -0.35, -1))
        aim(arm, f'hand.{side}', (0.05 * sx, -0.4, -1))
    turn(arm, 'cape.0', (1, 0, 0), -3.0 * breathe)


def pose_raise(arm):
    """The bow up toward the enemy (+x), the right hand on the string at the bow."""
    rest(arm)
    turn(arm, 'head', (0, 0, 1), 60)
    turn(arm, 'chest', (0, 0, 1), 8)
    aim(arm, 'upper_arm.L', (1, -0.15, 0.05))
    aim(arm, 'forearm.L', (1, -0.12, 0.08))
    aim(arm, 'hand.L', (1, -0.1, 0.1))
    grip = arm.pose.bones['hand.L'].head
    reach(arm, 'upper_arm.R', 'forearm.R', grip + Vector((-0.12, -0.05, 0.02)), (0, 1, -1))
    aim(arm, 'hand.R', (1, 0, 0))


def pose_draw(arm, tremble=0.0):
    """Full draw: the left arm straight at the enemy, the right hand at the chin, the elbow back and up."""
    pose_raise(arm)
    turn(arm, 'head', (0, 0, 1), 70)
    turn(arm, 'chest', (0, 0, 1), 12)
    turn(arm, 'spine', (0, 1, 0), -3)
    aim(arm, 'upper_arm.L', (1, -0.12, 0.08 + tremble))
    aim(arm, 'forearm.L', (1, -0.1, 0.08 + tremble))
    aim(arm, 'hand.L', (1, -0.1, 0.1))
    chin = arm.pose.bones['head'].head + Vector((0.07, -0.06, -0.01))
    reach(arm, 'upper_arm.R', 'forearm.R', chin, (-1, 0.3, 0.6))
    aim(arm, 'hand.R', (1, -0.2, 0))
    turn(arm, 'cape.0', (1, 0, 0), -6)


def pose_release(arm):
    """Just after the release: the right hand flung back past the ear, open."""
    pose_draw(arm)
    reach(arm, 'upper_arm.R', 'forearm.R', arm.pose.bones['head'].head + Vector((-0.22, 0.05, 0.02)), (-1, 0.3, 0.8))
    aim(arm, 'hand.R', (-1, 0, 0.3))
    turn(arm, 'cape.0', (1, 0, 0), -12)


# ---- the bow (rigid on hand.L) and its string (the middle on the 'string' bone) ----

def add_bow(mesh, arm):
    """Built where it must be in the full draw, then moved back to the rest pose through the bones that carry it, so
    the skinning puts it there again."""
    pose_draw(arm)
    hand_l, string_b = arm.pose.bones['hand.L'], arm.pose.bones['string']
    grip = hand_l.head.lerp(hand_l.tail, 0.5)
    to_rest_l = hand_l.bone.matrix_local @ hand_l.matrix.inverted()
    R, half = 0.55, math.radians(42)
    arc = [grip + Vector((R * (math.cos(t) - 1), 0, R * math.sin(t))) for t in [half * (i / 12.0 - 1) for i in range(25)]]
    top, bottom = arc[-1], arc[0]
    # the string bone sits at the string's rest middle (between the tips), in the bow's plane
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    sb = arm.data.edit_bones['string']
    mid_rest = to_rest_l @ ((top + bottom) / 2)
    sb.head, sb.tail = mid_rest, mid_rest + Vector((0, 0, 0.04))
    bpy.ops.object.mode_set(mode='OBJECT')

    bm = bmesh.new()
    bm.from_mesh(mesh.data)
    deform = bm.verts.layers.deform.verify()
    gl, gs = mesh.vertex_groups.get('hand.L') or mesh.vertex_groups.new(name='hand.L'), \
        mesh.vertex_groups.get('string') or mesh.vertex_groups.new(name='string')
    def tube(points, radius, groups):
        faces = []
        rings = []
        for i, p in enumerate(points):
            t = (points[min(i + 1, len(points) - 1)] - points[max(i - 1, 0)]).normalized()
            u = t.cross(Vector((0, 1, 0)))
            if u.length < 1e-3:
                u = t.cross(Vector((1, 0, 0)))
            u.normalize()
            v = t.cross(u)
            ring = []
            for k in range(6):
                a = 2 * math.pi * k / 6
                vert = bm.verts.new(p + (u * math.cos(a) + v * math.sin(a)) * radius)
                for g, w in groups(i):
                    vert[deform][g.index] = w
                ring.append(vert)
            rings.append(ring)
        for r0, r1 in zip(rings, rings[1:]):
            for k in range(6):
                faces.append(bm.faces.new((r0[k], r0[(k + 1) % 6], r1[(k + 1) % 6], r1[k])))
        return faces

    # the bow: rigid on the left hand, placed back at the rest pose
    bow_faces = tube([to_rest_l @ p for p in arc], 0.012, lambda i: [(gl, 1.0)])
    # the string: the tips on the left hand, the middle on the string bone (rest: between the tips)
    n = 8
    pts, groups = [], []
    for i in range(2 * n + 1):
        w = 1.0 - abs(i - n) / n                         # 0 at the tips, 1 at the middle
        pts.append(to_rest_l @ top.lerp(bottom, i / (2 * n)))
        groups.append([(gl, 1.0 - w), (gs, w)] if 0 < w < 1 else [(gs, 1.0)] if w == 1 else [(gl, 1.0)])
    string_faces = tube(pts, 0.003, lambda i: groups[i])
    # plain materials: the bow dark wood, the string pale (the body has the baked texture, material 0)
    for faces, name, c in ((bow_faces, 'bow', (0.10, 0.045, 0.02, 1.0)), (string_faces, 'string', (0.6, 0.55, 0.45, 1.0))):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = c
        m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.7
        mesh.data.materials.append(m)
        for f in faces:
            f.material_index = len(mesh.data.materials) - 1
    bm.to_mesh(mesh.data)
    bm.free()
    rest(arm)
    log(f'bow: {len(bow_faces)} + {len(string_faces)} faces')


def string_follow(arm):
    """The string's middle follows the right hand while the constraint's influence is 1 (keyed per clip)."""
    c = arm.pose.bones['string'].constraints.new('COPY_LOCATION')
    c.target, c.subtarget, c.head_tail = arm, 'hand.R', 0.3
    c.influence = 0.0
    return c


# ---- the clips ----

def clip(arm, name, keys, follow, length):
    """keys: [(time s, pose function, string influence)]. Baked to plain keys (the constraint included) and put on
    an NLA track of its own: one glTF animation per track."""
    act = bpy.data.actions.new(name)
    arm.animation_data_create()
    arm.animation_data.action = act
    for t, fn, infl in keys:
        fn(arm)
        f = round(t * FPS) + 1
        key_all(arm, f)
        follow.influence = infl
        follow.keyframe_insert('influence', frame=f)
    bpy.context.scene.frame_start, bpy.context.scene.frame_end = 1, round(length * FPS) + 1
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='POSE')
    bpy.ops.pose.select_all(action='SELECT')
    bpy.ops.nla.bake(frame_start=1, frame_end=round(length * FPS) + 1, only_selected=False, visual_keying=True,
                     clear_constraints=False, use_current_action=True, bake_types={'POSE'})
    bpy.ops.object.mode_set(mode='OBJECT')
    # the baked action holds the constraint's result: drop the influence keys from it
    baked = arm.animation_data.action
    track = arm.animation_data.nla_tracks.new()
    track.name = name
    track.strips.new(name, 1, baked)
    arm.animation_data.action = None
    log(f'clip {name}: {length:.2f} s')


def main():
    a = args()
    j = json.load(open(a['joints'], encoding='utf-8'))
    mesh, src = load_mesh(a['model'], int(a['tris']))
    bake_texture(mesh, src, a['out'].replace('.glb', '_colour.png'))
    arm = build_armature(j)
    skin(mesh, arm)
    add_bow(mesh, arm)
    follow = string_follow(arm)

    clip(arm, 'idle', [(0.0, lambda r: pose_idle(r, 0.0), 0.0), (1.0, lambda r: pose_idle(r, 1.0), 0.0),
                       (2.0, lambda r: pose_idle(r, 0.0), 0.0)], follow, 2.0)
    clip(arm, 'draw', [(0.0, lambda r: pose_idle(r, 0.0), 0.0), (0.30, pose_raise, 1.0), (0.75, pose_draw, 1.0),
                       (0.95, lambda r: pose_draw(r, 0.01), 1.0), (1.10, pose_draw, 1.0),
                       (1.13, pose_release, 0.0), (1.30, pose_release, 0.0), (1.80, lambda r: pose_idle(r, 0.0), 0.0)],
         follow, 1.8)
    clip(arm, 'charge', [(0.0, pose_draw, 1.0), (0.6, lambda r: pose_draw(r, 0.012), 1.0), (1.2, pose_draw, 1.0)],
         follow, 1.2)
    arm.pose.bones['string'].constraints.remove(follow)   # baked into the clips

    bpy.ops.object.select_all(action='SELECT')
    opts = dict(filepath=a['out'], export_format='GLB', export_skins=True, export_animations=True,
                export_animation_mode='NLA_TRACKS', export_force_sampling=True, export_frame_range=False,
                export_def_bones=False, export_apply=False)
    try:
        bpy.ops.export_scene.gltf(**opts, export_vertex_color='NONE')
    except TypeError:
        bpy.ops.export_scene.gltf(**opts)
    log('exported', a['out'])


main()
