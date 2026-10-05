#!/usr/bin/env python3
"""make_test_models.py -- the glTF test models in tests/gltf/ (docs/18_GLTF.md).

Small, self-contained .gltf files (buffers and textures embedded as data URIs), made by this script so
the tests own every byte (no licence questions) and each one checks one feature:

  skin_tube.gltf      a tube on a 3-joint skin, vertex colours; animations "bend" (linear rotation)
                      and "wave" (CUBICSPLINE rotation + translation)
  morph_cube.gltf     a cube with 2 morph targets ("bulge", "squash"), animated weights (linear + step)
  instancing.gltf     EXT_mesh_gpu_instancing: one box mesh, 100 instances (translation, rotation, scale)
  pbr_materials.gltf  a 5x5 grid of spheres (metallic down, roughness across), an emissive sphere, a
                      textured cube (embedded PNG, KHR_texture_transform), alpha mask and blend quads,
                      an unlit quad (KHR_materials_unlit)

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


if __name__ == '__main__':
    skin_tube()
    morph_cube()
    instancing()
    pbr_materials()
