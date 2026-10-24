#!/usr/bin/env python3
"""make_test_models.py -- the glTF test models in tests/gltf/ (docs/engine/08_GLTF.md).

Small, self-contained .gltf files (buffers and textures embedded as data URIs), made by this script so
the tests own every byte (no licence questions) and each one checks one feature:

  skin_tube.gltf      a tube on a 3-joint skin, vertex colours; animations "bend" (linear rotation)
                      and "wave" (CUBICSPLINE rotation + translation)
  morph_cube.gltf     a cube with 2 morph targets ("bulge", "squash"), animated weights (linear + step)
  instancing.gltf     EXT_mesh_gpu_instancing: one box mesh, 100 instances (translation, rotation, scale)
  pbr_materials.gltf  a 5x5 grid of spheres (metallic down, roughness across), an emissive sphere, a
                      textured cube (embedded PNG, KHR_texture_transform), alpha mask and blend quads,
                      an unlit quad (KHR_materials_unlit)
  physics_scene.gltf  KHR_physics_rigid_bodies + KHR_implicit_shapes: a static ground; falling box, bouncy
                      sphere (restitution 0.8), lying capsule, cone (tapered cylinder), a two-box compound, a
                      convex-hull octahedron; a box whose collision filter skips the ground; a kinematic
                      platform moving at 0.5 m/s; a trigger zone the box falls through; a pendulum (a joint with
                      locked linear axes); an animated (kinematic) sweeper

  lights_cameras.gltf KHR_lights_punctual (a red point, a blue spot, a warm sun) and three cameras (front and top
                      perspective, side orthographic) over white shapes: the viewer lights it with the file's lights

  python tools/gltf_viewer/make_test_models.py            writes tests/gltf/*.gltf
"""
import base64
import json
import math
import os
import struct
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tests', 'gltf')


class Builder:
    """Collects buffer views / accessors in one binary buffer (a data URI at the end)."""

    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors = [], []
        self.doc = {'asset': {'version': '2.0', 'generator': 'TOMS make_test_models.py'}}

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target:
            v['target'] = target
        self.bin += data
        self.views.append(v)
        return len(self.views) - 1

    def floats(self, rows, kind, target=34962, minmax=False):
        """rows: list of tuples (or floats for SCALAR)."""
        flat = [x for r in rows for x in (r if isinstance(r, (tuple, list)) else (r,))]
        view = self._view(struct.pack('<%df' % len(flat), *flat), target)
        a = {'bufferView': view, 'componentType': 5126, 'count': len(rows), 'type': kind}
        if minmax:
            n = len(rows[0]) if isinstance(rows[0], (tuple, list)) else 1
            cols = [[(r if n == 1 else r[i]) for r in rows] for i in range(n)]
            a['min'] = [min(c) for c in cols]
            a['max'] = [max(c) for c in cols]
        self.accessors.append(a)
        return len(self.accessors) - 1

    def ubytes(self, rows, kind, target=34962, normalized=False):
        flat = [x for r in rows for x in r]
        view = self._view(struct.pack('<%dB' % len(flat), *flat), target)
        a = {'bufferView': view, 'componentType': 5121, 'count': len(rows), 'type': kind}
        if normalized:
            a['normalized'] = True
        self.accessors.append(a)
        return len(self.accessors) - 1

    def indices(self, idx):
        view = self._view(struct.pack('<%dH' % len(idx), *idx), 34963)
        self.accessors.append({'bufferView': view, 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'})
        return len(self.accessors) - 1

    def anim_input(self, times):
        return self.floats(times, 'SCALAR', target=None, minmax=True)

    def write(self, name):
        self.doc['buffers'] = [{'byteLength': len(self.bin),
                                'uri': 'data:application/octet-stream;base64,' + base64.b64encode(bytes(self.bin)).decode()}]
        self.doc['bufferViews'] = self.views
        self.doc['accessors'] = self.accessors
        os.makedirs(OUT, exist_ok=True)
        path = os.path.join(OUT, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(self.doc, f, indent=1)
            f.write('\n')
        print('wrote', os.path.normpath(path), '(%d bytes)' % os.path.getsize(path))


def quat_axis(axis, deg):
    h = math.radians(deg) / 2
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))


def png_rgba(w, h, pixel):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(pixel(x, y))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


# ---- skin_tube ----------------------------------------------------------------------------------
def skin_tube():
    b = Builder()
    seg, rings, height, radius = 16, 13, 3.0, 0.35
    pos, nrm, col, joints, weights = [], [], [], [], []
    for r in range(rings):
        y = height * r / (rings - 1)
        # 3 joints at y = 0, 1, 2: each vertex weighted between the two nearest
        f = min(y, 1.999)
        j0 = int(f)
        t = f - j0
        for s in range(seg):
            a = 2 * math.pi * s / seg
            pos.append((radius * math.cos(a), y, radius * math.sin(a)))
            nrm.append((math.cos(a), 0.0, math.sin(a)))
            col.append((1.0, 0.45 + 0.5 * y / height, 0.2 + 0.6 * y / height, 1.0))
            joints.append((j0, min(j0 + 1, 2), 0, 0))
            weights.append((1 - t, t, 0.0, 0.0))
    idx = []
    for r in range(rings - 1):
        for s in range(seg):
            a, c = r * seg + s, r * seg + (s + 1) % seg
            idx += [a, a + seg, c, c, a + seg, c + seg]
    prim = {'attributes': {'POSITION': b.floats(pos, 'VEC3', minmax=True), 'NORMAL': b.floats(nrm, 'VEC3'),
                           'COLOR_0': b.floats(col, 'VEC4'), 'JOINTS_0': b.ubytes(joints, 'VEC4'),
                           'WEIGHTS_0': b.floats(weights, 'VEC4')},
            'indices': b.indices(idx), 'material': 0}
    # Inverse bind matrices: joints at y = 0, 1, 2 (translation only) -> inverse = translate(-y).
    ibm = []
    for j in range(3):
        ibm.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -float(j), 0, 1))
    ibm_acc = b.floats(ibm, 'MAT4', target=None)
    # Animations.
    times = [0.0, 0.5, 1.0, 1.5, 2.0]
    bend1 = [quat_axis((0, 0, 1), d) for d in (0, 35, 0, -35, 0)]
    bend2 = [quat_axis((0, 0, 1), d) for d in (0, 45, 0, -45, 0)]
    t_in = b.anim_input(times)
    bend = {'name': 'bend', 'samplers': [{'input': t_in, 'output': b.floats(bend1, 'VEC4', target=None)},
                                         {'input': t_in, 'output': b.floats(bend2, 'VEC4', target=None)}],
            'channels': [{'sampler': 0, 'target': {'node': 2, 'path': 'rotation'}},
                         {'sampler': 1, 'target': {'node': 3, 'path': 'rotation'}}]}
    # wave: CUBICSPLINE (in-tangent, value, out-tangent per key), joint 1 swings about X, the root bobs.
    wt = [0.0, 1.0, 2.0]
    rot = []
    for d in (-30, 30, -30):
        rot += [(0, 0, 0, 0), quat_axis((1, 0, 0), d), (0, 0, 0, 0)]
    tr = []
    for y in (0.0, 0.4, 0.0):
        tr += [(0, 0, 0), (0, y, 0), (0, 0, 0)]
    w_in = b.anim_input(wt)
    wave = {'name': 'wave', 'samplers': [{'input': w_in, 'output': b.floats(rot, 'VEC4', target=None), 'interpolation': 'CUBICSPLINE'},
                                         {'input': w_in, 'output': b.floats(tr, 'VEC3', target=None), 'interpolation': 'CUBICSPLINE'}],
            'channels': [{'sampler': 0, 'target': {'node': 2, 'path': 'rotation'}},
                         {'sampler': 1, 'target': {'node': 1, 'path': 'translation'}}]}
    b.doc.update({
        'scene': 0, 'scenes': [{'name': 'skin', 'nodes': [0, 1]}],
        'nodes': [{'name': 'tube', 'mesh': 0, 'skin': 0},
                  {'name': 'joint0', 'children': [2]},
                  {'name': 'joint1', 'translation': [0, 1, 0], 'children': [3]},
                  {'name': 'joint2', 'translation': [0, 1, 0]}],
        'meshes': [{'name': 'tube', 'primitives': [prim]}],
        'skins': [{'name': 'tube_skin', 'joints': [1, 2, 3], 'inverseBindMatrices': ibm_acc, 'skeleton': 1}],
        'materials': [{'name': 'tube', 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.0,
                                                                 'roughnessFactor': 0.45}}],
        'animations': [bend, wave]})
    b.write('skin_tube.gltf')


# ---- morph_cube ---------------------------------------------------------------------------------
CUBE_FACES = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
              ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
              ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]


def cube(size=1.0):
    pos, nrm, uv, idx = [], [], [], []
    h = size / 2
    for n, u, v in CUBE_FACES:
        base = len(pos)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            pos.append(tuple(h * (n[i] + su * u[i] + sv * v[i]) for i in range(3)))
            nrm.append(n)
            uv.append(((su + 1) / 2, 1 - (sv + 1) / 2))
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, uv, idx


def morph_cube():
    b = Builder()
    pos, nrm, _, idx = cube(1.0)
    bulge = [(0.0, 0.6, 0.0) if p[1] > 0 else (0.0, 0.0, 0.0) for p in pos]   # the top rises
    squash = [(p[0] * 0.6, -p[1] * 0.5, p[2] * 0.6) for p in pos]            # wider and flatter
    prim = {'attributes': {'POSITION': b.floats(pos, 'VEC3', minmax=True), 'NORMAL': b.floats(nrm, 'VEC3')},
            'indices': b.indices(idx), 'material': 0,
            'targets': [{'POSITION': b.floats(bulge, 'VEC3', minmax=True)}, {'POSITION': b.floats(squash, 'VEC3', minmax=True)}]}
    t_lin = b.anim_input([0.0, 1.0, 2.0, 3.0])
    # weights output: keys x 2 targets -- bulge up, then squash, then back
    w_lin = b.floats([0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0], 'SCALAR', target=None)
    t_step = b.anim_input([0.0, 0.5, 1.0, 1.5])
    w_step = b.floats([0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0], 'SCALAR', target=None)
    b.doc.update({
        'scene': 0, 'scenes': [{'nodes': [0]}],
        'nodes': [{'name': 'cube', 'mesh': 0}],
        'meshes': [{'name': 'morph_cube', 'primitives': [prim], 'weights': [0.0, 0.0],
                    'extras': {'targetNames': ['bulge', 'squash']}}],
        'materials': [{'name': 'blue', 'pbrMetallicRoughness': {'baseColorFactor': [0.25, 0.55, 1.0, 1.0],
                                                                 'metallicFactor': 0.1, 'roughnessFactor': 0.4}}],
        'animations': [{'name': 'breathe', 'samplers': [{'input': t_lin, 'output': w_lin}],
                        'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'weights'}}]},
                       {'name': 'steps', 'samplers': [{'input': t_step, 'output': w_step, 'interpolation': 'STEP'}],
                        'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'weights'}}]}]})
    b.write('morph_cube.gltf')


# ---- instancing ---------------------------------------------------------------------------------
def instancing():
    b = Builder()
    pos, nrm, _, idx = cube(0.5)
    prim = {'attributes': {'POSITION': b.floats(pos, 'VEC3', minmax=True), 'NORMAL': b.floats(nrm, 'VEC3')},
            'indices': b.indices(idx), 'material': 0}
    tr, rot, sc = [], [], []
    for i in range(10):
        for j in range(10):
            tr.append(((i - 4.5) * 1.0, 0.25 + 0.15 * math.sin(i * 0.7 + j * 0.5), (j - 4.5) * 1.0))
            rot.append(quat_axis((0, 1, 0), (i * 10 + j) * 9.0))
            s = 0.6 + 0.4 * ((i + j) % 5) / 4
            sc.append((s, s * (1 + 0.5 * ((i * j) % 3)), s))
    b.doc.update({
        'extensionsUsed': ['EXT_mesh_gpu_instancing'], 'extensionsRequired': ['EXT_mesh_gpu_instancing'],
        'scene': 0, 'scenes': [{'nodes': [0]}],
        'nodes': [{'name': 'boxes', 'mesh': 0, 'extensions': {'EXT_mesh_gpu_instancing': {'attributes': {
            'TRANSLATION': b.floats(tr, 'VEC3', target=None), 'ROTATION': b.floats(rot, 'VEC4', target=None),
            'SCALE': b.floats(sc, 'VEC3', target=None)}}}}],
        'meshes': [{'name': 'box', 'primitives': [prim]}],
        'materials': [{'name': 'gold', 'pbrMetallicRoughness': {'baseColorFactor': [1.0, 0.78, 0.34, 1.0],
                                                                 'metallicFactor': 1.0, 'roughnessFactor': 0.3}}]})
    b.write('instancing.gltf')


# ---- pbr_materials ------------------------------------------------------------------------------
def sphere(rings=24, segs=32, r=0.4):
    pos, nrm, uv, idx = [], [], [], []
    for i in range(rings + 1):
        v = i / rings
        th = v * math.pi
        for j in range(segs + 1):
            u = j / segs
            ph = u * 2 * math.pi
            n = (math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph))
            pos.append(tuple(r * c for c in n))
            nrm.append(n)
            uv.append((u, v))
    for i in range(rings):
        for j in range(segs):
            a, c = i * (segs + 1) + j, (i + 1) * (segs + 1) + j
            idx += [a, a + 1, c, c, a + 1, c + 1]
    return pos, nrm, uv, idx


def quad(w=1.0, h=1.0):
    pos = [(-w / 2, -h / 2, 0), (w / 2, -h / 2, 0), (w / 2, h / 2, 0), (-w / 2, h / 2, 0)]
    return pos, [(0, 0, 1)] * 4, [(0, 1), (1, 1), (1, 0), (0, 0)], [0, 1, 2, 0, 2, 3]


def pbr_materials():
    b = Builder()
    meshes, materials, nodes = [], [], []

    shared = {}   # one copy of each geometry's accessors, however many meshes use it

    def add_mesh(name, geo, material):
        if id(geo) not in shared:
            pos, nrm, uv, idx = geo
            # (geo itself is kept too: a freed tuple's id() can come back for the next geometry)
            shared[id(geo)] = (geo, {'POSITION': b.floats(pos, 'VEC3', minmax=True), 'NORMAL': b.floats(nrm, 'VEC3'),
                                     'TEXCOORD_0': b.floats(uv, 'VEC2')}, b.indices(idx))
        _, attrs, ind = shared[id(geo)]
        prim = {'attributes': dict(attrs), 'indices': ind, 'material': material}
        meshes.append({'name': name, 'primitives': [prim]})
        return len(meshes) - 1

    sph = sphere()
    # 5x5 spheres: metallic 0..1 down the rows, roughness 0..1 across.
    for row in range(5):
        for col in range(5):
            materials.append({'name': 'm%d_r%d' % (row, col), 'pbrMetallicRoughness': {
                'baseColorFactor': [0.9, 0.2, 0.15, 1.0], 'metallicFactor': row / 4, 'roughnessFactor': max(0.05, col / 4)}})
            m = add_mesh('sphere_%d_%d' % (row, col), sph, len(materials) - 1)
            nodes.append({'name': 'sphere_%d_%d' % (row, col), 'mesh': m, 'translation': [(col - 2) * 1.0, (2 - row) * 1.0, 0]})
    # Emissive sphere.
    materials.append({'name': 'emissive', 'emissiveFactor': [1.0, 0.6, 0.1],
                      'extensions': {'KHR_materials_emissive_strength': {'emissiveStrength': 2.0}},
                      'pbrMetallicRoughness': {'baseColorFactor': [0.1, 0.1, 0.1, 1], 'metallicFactor': 0, 'roughnessFactor': 0.8}})
    nodes.append({'name': 'emissive', 'mesh': add_mesh('emissive', sph, len(materials) - 1), 'translation': [3.4, 2, 0]})
    # Textured cube: an embedded PNG checker (with a TOMS-blue cross), tiled 2x by KHR_texture_transform.
    png = png_rgba(64, 64, lambda x, y: (40, 120, 255, 255) if (x in (31, 32) or y in (31, 32)) else
                   ((235, 235, 235, 255) if ((x // 8) + (y // 8)) % 2 == 0 else (60, 60, 60, 255)))
    b.doc['images'] = [{'name': 'checker', 'uri': 'data:image/png;base64,' + base64.b64encode(png).decode()}]
    b.doc['samplers'] = [{'magFilter': 9728, 'minFilter': 9986, 'wrapS': 10497, 'wrapT': 10497}]
    b.doc['textures'] = [{'source': 0, 'sampler': 0}]
    materials.append({'name': 'checker', 'pbrMetallicRoughness': {
        'baseColorTexture': {'index': 0, 'extensions': {'KHR_texture_transform': {'scale': [2, 2]}}},
        'metallicFactor': 0.0, 'roughnessFactor': 0.6}})
    nodes.append({'name': 'textured_cube', 'mesh': add_mesh('cube', cube(0.8), len(materials) - 1),
                  'translation': [3.4, 0.8, 0], 'rotation': list(quat_axis((0.577, 0.577, 0.577), 30))})
    # Alpha mask, alpha blend and unlit quads.
    materials.append({'name': 'mask', 'alphaMode': 'MASK', 'alphaCutoff': 0.5, 'doubleSided': True, 'pbrMetallicRoughness': {
        'baseColorTexture': {'index': 0}, 'baseColorFactor': [0.4, 1.0, 0.4, 1.0], 'metallicFactor': 0, 'roughnessFactor': 1}})
    nodes.append({'name': 'mask_quad', 'mesh': add_mesh('mask', quad(), len(materials) - 1), 'translation': [3.4, -0.5, 0]})
    materials.append({'name': 'blend', 'alphaMode': 'BLEND', 'doubleSided': True, 'pbrMetallicRoughness': {
        'baseColorFactor': [0.2, 0.6, 1.0, 0.45], 'metallicFactor': 0, 'roughnessFactor': 0.3}})
    nodes.append({'name': 'blend_quad', 'mesh': add_mesh('blend', quad(5.6, 1.0), len(materials) - 1), 'translation': [0, 0, 0.7]})
    materials.append({'name': 'unlit', 'extensions': {'KHR_materials_unlit': {}}, 'pbrMetallicRoughness': {
        'baseColorFactor': [1.0, 0.85, 0.2, 1.0]}})
    nodes.append({'name': 'unlit_quad', 'mesh': add_mesh('unlit', quad(0.8, 0.8), len(materials) - 1), 'translation': [3.4, -1.8, 0]})
    b.doc.update({'extensionsUsed': ['KHR_texture_transform', 'KHR_materials_emissive_strength', 'KHR_materials_unlit'],
                  'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
                  'nodes': nodes, 'meshes': meshes, 'materials': materials})
    b.write('pbr_materials.gltf')


# ---- physics_scene ------------------------------------------------------------------------------
def box_mesh(sx, sy, sz):
    pos, nrm, uv, idx = cube(1.0)
    return [(p[0] * sx, p[1] * sy, p[2] * sz) for p in pos], nrm, idx


def cylinder_mesh(h, r_top, r_bottom, segs=24):
    pos, nrm, idx = [], [], []
    for i in range(segs):
        a0, a1 = 2 * math.pi * i / segs, 2 * math.pi * (i + 1) / segs
        quad = [(r_bottom * math.cos(a0), -h / 2, r_bottom * math.sin(a0)), (r_bottom * math.cos(a1), -h / 2, r_bottom * math.sin(a1)),
                (r_top * math.cos(a1), h / 2, r_top * math.sin(a1)), (r_top * math.cos(a0), h / 2, r_top * math.sin(a0))]
        base = len(pos)
        slope = (r_bottom - r_top) / h
        for q in quad:
            ang = math.atan2(q[2], q[0])
            n = (math.cos(ang), slope, math.sin(ang))
            l = math.sqrt(sum(c * c for c in n))
            pos.append(q)
            nrm.append(tuple(c / l for c in n))
        idx += [base, base + 2, base + 1, base, base + 3, base + 2]
        for y, r, ny in ((h / 2, r_top, 1.0), (-h / 2, r_bottom, -1.0)):   # caps
            if r <= 0:
                continue
            base = len(pos)
            pos += [(0, y, 0), (r * math.cos(a0), y, r * math.sin(a0)), (r * math.cos(a1), y, r * math.sin(a1))]
            nrm += [(0, ny, 0)] * 3
            idx += [base, base + 2, base + 1] if ny > 0 else [base, base + 1, base + 2]
    return pos, nrm, idx


def capsule_mesh(h, r, rings=8, segs=24):
    pos, nrm, idx = [], [], []
    rows = []
    for half, sign in ((h / 2, 1), (-h / 2, -1)):
        for i in range(rings + 1):
            t = (math.pi / 2) * i / rings
            ring = []
            for j in range(segs + 1):
                a = 2 * math.pi * j / segs
                n = (math.cos(t) * math.cos(a), sign * math.sin(t), math.cos(t) * math.sin(a))
                pos.append((r * n[0], half + r * n[1], r * n[2]))
                nrm.append(n)
                ring.append(len(pos) - 1)
            rows.append((sign, i, ring))
    top = [ring for sign, i, ring in rows if sign > 0][::-1]       # from the pole down to the equator
    bottom = [ring for sign, i, ring in rows if sign < 0]          # from the equator down to the pole
    order = top + bottom
    for k in range(len(order) - 1):
        a, b = order[k], order[k + 1]
        for j in range(segs):
            idx += [a[j], a[j + 1], b[j + 1], a[j], b[j + 1], b[j]]
    return pos, nrm, idx


def octahedron(r):
    v = [(r, 0, 0), (-r, 0, 0), (0, r, 0), (0, -r, 0), (0, 0, r), (0, 0, -r)]
    faces = [(0, 2, 4), (4, 2, 1), (1, 2, 5), (5, 2, 0), (4, 3, 0), (1, 3, 4), (5, 3, 1), (0, 3, 5)]
    pos, nrm, idx = [], [], []
    for f in faces:
        a, b, c = (v[i] for i in f)
        n = [sum(x) / 3 for x in zip(a, b, c)]
        l = math.sqrt(sum(x * x for x in n))
        for q in (a, b, c):
            pos.append(q)
            nrm.append(tuple(x / l for x in n))
        idx += [len(pos) - 3, len(pos) - 1, len(pos) - 2]
    return pos, nrm, idx


def physics_scene():
    b = Builder()
    meshes, materials, nodes, shapes = [], [], [], []

    def material(name, rgba, rough=0.6, metal=0.0, blend=False):
        m = {'name': name, 'pbrMetallicRoughness': {'baseColorFactor': rgba, 'metallicFactor': metal, 'roughnessFactor': rough}}
        if blend:
            m['alphaMode'] = 'BLEND'
            m['doubleSided'] = True
        materials.append(m)
        return len(materials) - 1

    def mesh(name, geo, mat):
        pos, nrm, idx = geo
        meshes.append({'name': name, 'primitives': [{'attributes': {'POSITION': b.floats(pos, 'VEC3', minmax=True),
                                                                   'NORMAL': b.floats(nrm, 'VEC3')},
                                                    'indices': b.indices(idx), 'material': mat}]})
        return len(meshes) - 1

    def shape(kind, **props):
        shapes.append({'type': kind, kind: props})
        return len(shapes) - 1

    def node(name, **kw):
        n = {'name': name}
        n.update({k: v for k, v in kw.items() if k != 'physics'})
        if 'physics' in kw:
            n['extensions'] = {'KHR_physics_rigid_bodies': kw['physics']}
        nodes.append(n)
        return len(nodes) - 1

    def collider(shape_index, mat=0, filt=None):
        c = {'geometry': {'shape': shape_index}, 'physicsMaterial': mat}
        if filt is not None:
            c['collisionFilter'] = filt
        return c

    grey = material('ground', [0.55, 0.57, 0.6, 1.0], rough=0.9)
    orange = material('orange', [1.0, 0.5, 0.15, 1.0])
    blue = material('blue', [0.2, 0.45, 1.0, 1.0], rough=0.3)
    green = material('green', [0.25, 0.8, 0.35, 1.0])
    yellow = material('yellow', [1.0, 0.85, 0.2, 1.0])
    teal = material('teal', [0.1, 0.75, 0.75, 1.0])
    red = material('red', [0.9, 0.2, 0.2, 1.0])
    purple = material('purple', [0.6, 0.3, 0.9, 1.0])
    zone = material('trigger_zone', [1.0, 0.9, 0.2, 0.25], blend=True)
    steel = material('steel', [0.75, 0.75, 0.8, 1.0], rough=0.35, metal=0.8)

    # physics materials: 0 rough (no bounce), 1 bouncy (restitution 0.8, the maximum wins)
    phys_materials = [{'staticFriction': 0.8, 'dynamicFriction': 0.8, 'restitution': 0.0},
                      {'staticFriction': 0.5, 'dynamicFriction': 0.5, 'restitution': 0.8, 'restitutionCombine': 'maximum'}]
    # filters: 0 the world, 1 a ghost that passes through the world
    filters = [{'collisionSystems': ['world']},
               {'collisionSystems': ['ghost'], 'notCollideWithSystems': ['world']}]
    # joint 0: a ball joint (the three linear axes locked, rotation free)
    joints = [{'limits': [{'linearAxes': [0, 1, 2], 'min': 0, 'max': 0}]}]

    roots = []
    roots.append(node('ground', mesh=mesh('ground', box_mesh(12, 0.4, 6), grey), translation=[0, -0.2, 0],
                      physics={'collider': collider(shape('box', size=[12, 0.4, 6]), 0, 0)}))
    roots.append(node('box_drop', mesh=mesh('box', box_mesh(1, 1, 1), orange), translation=[-4, 3, 0],
                      physics={'motion': {'mass': 2}, 'collider': collider(shape('box', size=[1, 1, 1]))}))
    roots.append(node('ball_bouncy', mesh=mesh('ball', (lambda g: (g[0], g[1], g[3]))(sphere(16, 24, 0.5)), blue), translation=[-2, 4, 0],
                      physics={'motion': {'mass': 1}, 'collider': collider(shape('sphere', radius=0.5), 1)}))
    roots.append(node('capsule', mesh=mesh('capsule', capsule_mesh(1.0, 0.3), green), translation=[0, 3, 0],
                      rotation=list(quat_axis((0, 0, 1), 90)),
                      physics={'motion': {'mass': 1}, 'collider': collider(shape('capsule', height=1.0, radiusTop=0.3, radiusBottom=0.3))}))
    roots.append(node('cone', mesh=mesh('cone', cylinder_mesh(1.0, 0.0, 0.5), yellow), translation=[1.6, 3, 0],
                      physics={'motion': {'mass': 1}, 'collider': collider(shape('cylinder', height=1.0, radiusTop=0.0, radiusBottom=0.5))}))
    part = shape('box', size=[0.5, 1.5, 0.5])
    part_mesh = mesh('compound_part', box_mesh(0.5, 1.5, 0.5), teal)
    a = node('compound_a', mesh=part_mesh, translation=[-0.5, 0, 0], physics={'collider': collider(part)})
    c = node('compound_b', mesh=part_mesh, translation=[0.25, 0.5, 0], rotation=list(quat_axis((0, 0, 1), 90)),
             physics={'collider': collider(part)})
    roots.append(node('compound', translation=[3.5, 3.5, 0], children=[a, c], physics={'motion': {'mass': 2}}))
    hull_mesh = mesh('octahedron', octahedron(0.6), purple)
    roots.append(node('convex', mesh=hull_mesh, translation=[5, 3, 0],
                      physics={'motion': {'mass': 1}, 'collider': {'geometry': {'mesh': hull_mesh, 'convexHull': True}, 'physicsMaterial': 0}}))
    roots.append(node('ghost', mesh=mesh('ghost_box', box_mesh(0.8, 0.8, 0.8), red), translation=[-4, 3, 2],
                      physics={'motion': {'mass': 1}, 'collider': collider(shape('box', size=[0.8, 0.8, 0.8]), 0, 1)}))
    roots.append(node('platform', mesh=mesh('platform', box_mesh(1.5, 0.2, 1.5), steel), translation=[-5, 1, -2],
                      physics={'motion': {'isKinematic': True, 'linearVelocity': [0.5, 0, 0]},
                               'collider': collider(shape('box', size=[1.5, 0.2, 1.5]))}))
    roots.append(node('trigger_zone', mesh=mesh('zone', box_mesh(2, 1, 2), zone), translation=[-4, 1.5, 0],
                      physics={'trigger': {'geometry': {'shape': shape('box', size=[2, 1, 2])}}}))
    pivot_a = node('anchor_pivot', physics={'joint': {'connectedNode': len(nodes) + 2, 'joint': 0}})
    roots.append(node('anchor', mesh=mesh('anchor', box_mesh(0.3, 0.3, 0.3), steel), translation=[2.5, 4, -2], children=[pivot_a],
                      physics={'collider': collider(shape('box', size=[0.3, 0.3, 0.3]))}))
    pivot_b = node('bob_pivot', translation=[-1.2, 0, 0])
    assert pivot_b == pivot_a + 2
    roots.append(node('bob', mesh=mesh('bob', box_mesh(0.4, 0.4, 0.4), orange), translation=[3.7, 4, -2], children=[pivot_b],
                      physics={'motion': {'mass': 1}, 'collider': collider(shape('box', size=[0.4, 0.4, 0.4]))}))
    roots.append(node('sweeper', mesh=mesh('sweeper', box_mesh(2, 0.2, 0.2), red), translation=[0, 0.3, 2],
                      physics={'collider': collider(shape('box', size=[2, 0.2, 0.2]))}))
    sweeper = roots[-1]
    t_in = b.anim_input([0.0, 2.0, 4.0])
    r_out = b.floats([quat_axis((0, 1, 0), 0), quat_axis((0, 1, 0), 180), quat_axis((0, 1, 0), 359.9)], 'VEC4', target=None)

    b.doc.update({
        'extensionsUsed': ['KHR_implicit_shapes', 'KHR_physics_rigid_bodies'],
        'extensions': {'KHR_implicit_shapes': {'shapes': shapes},
                       'KHR_physics_rigid_bodies': {'physicsMaterials': phys_materials, 'collisionFilters': filters,
                                                    'physicsJoints': joints}},
        'scene': 0, 'scenes': [{'nodes': roots}],
        'nodes': nodes, 'meshes': meshes, 'materials': materials,
        'animations': [{'name': 'sweep', 'samplers': [{'input': t_in, 'output': r_out}],
                        'channels': [{'sampler': 0, 'target': {'node': sweeper, 'path': 'rotation'}}]}]})
    b.write('physics_scene.gltf')


# ---- lights_cameras -----------------------------------------------------------------------------
def lights_cameras():
    """KHR_lights_punctual and cameras: white shapes on a grey floor, lit only by the file's own lights."""
    b = Builder()
    meshes, materials, nodes = [], [], []

    def add_mesh(name, geo, material):
        pos, nrm, uv, idx = geo
        attrs = {'POSITION': b.floats(pos, 'VEC3', minmax=True), 'NORMAL': b.floats(nrm, 'VEC3'), 'TEXCOORD_0': b.floats(uv, 'VEC2')}
        meshes.append({'name': name, 'primitives': [{'attributes': attrs, 'indices': b.indices(idx), 'material': material}]})
        return len(meshes) - 1

    materials.append({'name': 'floor', 'pbrMetallicRoughness': {'baseColorFactor': [0.7, 0.7, 0.7, 1], 'metallicFactor': 0, 'roughnessFactor': 0.9}})
    materials.append({'name': 'white', 'pbrMetallicRoughness': {'baseColorFactor': [0.95, 0.95, 0.95, 1], 'metallicFactor': 0, 'roughnessFactor': 0.5}})
    fpos, fnrm, fuv, fidx = cube(1.0)
    floor = ([(p[0] * 8, p[1] * 0.1 - 0.05, p[2] * 8) for p in fpos], fnrm, fuv, fidx)
    nodes.append({'name': 'floor', 'mesh': add_mesh('floor', floor, 0)})
    sph = sphere(r=0.5)
    for i, x in enumerate((-1.5, 0.0, 1.5)):
        nodes.append({'name': 'sphere_%d' % i, 'mesh': add_mesh('sphere_%d' % i, sph, 1), 'translation': [x, 0.5, 0]})
    nodes.append({'name': 'box', 'mesh': add_mesh('box', cube(0.8), 1), 'translation': [0, 0.4, -1.6],
                  'rotation': list(quat_axis((0, 1, 0), 30))})

    # Lights: a red point light on the left, a blue spot light above the right sphere aiming down (-Z turned to -Y),
    # a weak warm sun from the front left. Each shines down its node's -Z.
    b.doc['extensions'] = {'KHR_lights_punctual': {'lights': [
        {'name': 'red_point', 'type': 'point', 'color': [1.0, 0.15, 0.1], 'intensity': 12, 'range': 8},
        {'name': 'blue_spot', 'type': 'spot', 'color': [0.2, 0.45, 1.0], 'intensity': 40, 'range': 10,
         'spot': {'innerConeAngle': 0.25, 'outerConeAngle': 0.6}},
        {'name': 'warm_sun', 'type': 'directional', 'color': [1.0, 0.85, 0.6], 'intensity': 0.6}]}}
    light_nodes = [
        {'name': 'red_point', 'translation': [-2.4, 1.4, 0.8], 'extensions': {'KHR_lights_punctual': {'light': 0}}},
        {'name': 'blue_spot', 'translation': [1.5, 3.0, 0], 'rotation': list(quat_axis((1, 0, 0), -90)),
         'extensions': {'KHR_lights_punctual': {'light': 1}}},
        {'name': 'warm_sun', 'rotation': [-0.27, -0.27, -0.08, 0.92], 'extensions': {'KHR_lights_punctual': {'light': 2}}},
    ]
    # Cameras: front (perspective, a little above, looking at the origin), top (straight down), side (orthographic,
    # from +X). Each looks down its node's -Z.
    b.doc['cameras'] = [
        {'name': 'front', 'type': 'perspective', 'perspective': {'yfov': 0.8, 'znear': 0.1, 'zfar': 100}},
        {'name': 'top', 'type': 'perspective', 'perspective': {'yfov': 0.9, 'znear': 0.1, 'zfar': 100}},
        {'name': 'side', 'type': 'orthographic', 'orthographic': {'xmag': 3.6, 'ymag': 2.2, 'znear': 0.1, 'zfar': 100}},
    ]
    cam_nodes = [
        {'name': 'cam_front', 'camera': 0, 'translation': [0, 2.2, 6.5], 'rotation': list(quat_axis((1, 0, 0), -18))},
        {'name': 'cam_top', 'camera': 1, 'translation': [0, 7.5, 0], 'rotation': list(quat_axis((1, 0, 0), -90))},
        {'name': 'cam_side', 'camera': 2, 'translation': [6.0, 1.0, 0], 'rotation': list(quat_axis((0, 1, 0), 90))},
    ]
    nodes += light_nodes + cam_nodes
    b.doc.update({'extensionsUsed': ['KHR_lights_punctual'], 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
                  'nodes': nodes, 'meshes': meshes, 'materials': materials})
    b.write('lights_cameras.gltf')


if __name__ == '__main__':
    skin_tube()
    morph_cube()
    instancing()
    pbr_materials()
    physics_scene()
    lights_cameras()
