#!/usr/bin/env python3
"""make_hero_3d.py -- option C: a 3D model of the player from the chosen concept (docs/engine/20_CHARACTER_ART.md §4).

    python tools/art/make_hero_3d.py [--ref Build/art_out/hero_m/concept_606.png] [--seed 1] [--octree 256]
                                     [--views back,left,right,back_left] [--view-seeds 9,9,9,8] [--repaint] [--recolour]

1. The concept's figure (its _cut.png, background removed) is centred on a white square, as Hunyuan3D-2 wants it.
2. ComfyUI's built-in Hunyuan3D-2 nodes make the shape (a mesh, no texture) and save it as a GLB.
3. The other sides are painted: the mesh's depth seen from the back and each side (VIEWS) makes ControlNet (depth)
   paint that view on the same body, and IP-Adapter (the concept) keeps the person and the clothes.
4. Every vertex takes the colours of the views that see it (the concept for the front), weighted by how squarely each
   view sees it (COLOR_0); a vertex no view sees takes its nearest coloured neighbour's.

Output in Build/art_out/hero_3d/: hero_<seed>_shape.glb (the raw mesh), view_<name>_depth.png and
view_<name>_<seed>.png (the painted views; delete one or pass --repaint to paint again), and hero_<seed>.glb
(coloured), for Build/windows-release/bin/gltf_viewer.exe. --recolour skips the shape (uses the saved one).

Needs the ComfyUI server at 127.0.0.1:8188 and hunyuan3d-dit-v2-0.fp16.safetensors in its checkpoints folder
(docs/engine/20_CHARACTER_ART.md §4; tools\\setup_ai_art.cmd).
"""
import argparse
import json
import struct
import sys
import time
import uuid
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage, sparse
from scipy.spatial import cKDTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_brawler_art as base   # noqa: E402  (the ComfyUI helpers)
from make_hero_poses import CLIP_VISION, HERO, IPADAPTER   # noqa: E402  (the same person and clothes)

ROOT = base.ROOT
MODEL = 'hunyuan3d-dit-v2-0.fp16.safetensors'
SMOOTH = 2   # rounds of colour averaging with the neighbours


def square_input(cut):
    """The figure centred on a white square, filling 90% of its height (Hunyuan3D-2's own preprocessing)."""
    a = np.asarray(cut.getchannel('A'))
    x0, y0, x1, y1 = Image.fromarray(((a > 16) * 255).astype(np.uint8)).getbbox()
    fig = cut.crop((x0, y0, x1, y1))
    side = int((y1 - y0) / 0.9)
    sq = Image.new('RGB', (side, side), (255, 255, 255))
    sq.paste(fig, ((side - fig.width) // 2, (side - fig.height) // 2), fig)
    return sq.resize((1024, 1024), Image.LANCZOS)


def workflow(image_name, seed, octree, prefix):
    return {
        '1': {'class_type': 'ImageOnlyCheckpointLoader', 'inputs': {'ckpt_name': MODEL}},
        '2': {'class_type': 'ModelSamplingAuraFlow', 'inputs': {'model': ['1', 0], 'shift': 1.0}},
        '3': {'class_type': 'LoadImage', 'inputs': {'image': image_name}},
        '4': {'class_type': 'CLIPVisionEncode', 'inputs': {'clip_vision': ['1', 1], 'image': ['3', 0], 'crop': 'none'}},
        '5': {'class_type': 'Hunyuan3Dv2Conditioning', 'inputs': {'clip_vision_output': ['4', 0]}},
        '6': {'class_type': 'EmptyLatentHunyuan3Dv2', 'inputs': {'resolution': 3072, 'batch_size': 1}},
        '7': {'class_type': 'KSampler', 'inputs': {'model': ['2', 0], 'positive': ['5', 0], 'negative': ['5', 1],
                                                   'latent_image': ['6', 0], 'seed': seed, 'steps': 30, 'cfg': 5.0,
                                                   'sampler_name': 'euler', 'scheduler': 'normal', 'denoise': 1.0}},
        '8': {'class_type': 'VAEDecodeHunyuan3D', 'inputs': {'samples': ['7', 0], 'vae': ['1', 2], 'num_chunks': 8000,
                                                            'octree_resolution': octree}},
        '9': {'class_type': 'VoxelToMesh', 'inputs': {'voxel': ['8', 0], 'algorithm': 'surface net', 'threshold': 0.6}},
        '10': {'class_type': 'SaveGLB', 'inputs': {'mesh': ['9', 0], 'filename_prefix': prefix}},
    }


def run_for_file(wf):
    """Like base.run, but returns the bytes of the first file the prompt saved (the GLB)."""
    pid = json.loads(base.http('/prompt', json.dumps({'prompt': wf, 'client_id': uuid.uuid4().hex}).encode()))['prompt_id']
    while True:
        hist = json.loads(base.http(f'/history/{pid}'))
        if pid in hist:
            h = hist[pid]
            if h.get('status', {}).get('status_str') == 'error':
                raise RuntimeError(json.dumps(h['status'], indent=1)[:2000])
            for out in h['outputs'].values():
                for items in out.values():
                    for f in items if isinstance(items, list) else []:
                        if isinstance(f, dict) and str(f.get('filename', '')).endswith('.glb'):
                            q = f"/view?filename={f['filename']}&subfolder={f.get('subfolder', '')}&type={f.get('type', 'output')}"
                            return base.http(q)
            raise RuntimeError('the prompt saved no GLB: ' + json.dumps(h['outputs'])[:500])
        time.sleep(2.0)


# ---- a minimal GLB reader / writer (one mesh, triangles) ----

def read_glb(data):
    n = struct.unpack_from('<I', data, 12)[0]
    gltf = json.loads(data[20:20 + n])
    binc = data[20 + n + 8:]
    def acc(i):
        a = gltf['accessors'][i]
        bv = gltf['bufferViews'][a['bufferView']]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16}[a['componentType']]
        k = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        return np.frombuffer(binc, dt, a['count'] * k, off).reshape(-1, k) if k > 1 else \
            np.frombuffer(binc, dt, a['count'], off)
    prim = gltf['meshes'][0]['primitives'][0]
    return acc(prim['attributes']['POSITION']).astype(np.float32), acc(prim['indices']).astype(np.uint32)


def write_glb(path, pos, nrm, col, idx):
    chunks, views, accs = [], [], []
    def add(arr, target, ctype, typ, minmax=False):
        raw = arr.tobytes()
        off = sum(len(c) for c in chunks)
        chunks.append(raw + b'\0' * (-len(raw) % 4))
        views.append({'buffer': 0, 'byteOffset': off, 'byteLength': len(raw), 'target': target})
        a = {'bufferView': len(views) - 1, 'componentType': ctype, 'count': len(arr), 'type': typ}
        if minmax:
            a['min'], a['max'] = arr.min(0).tolist(), arr.max(0).tolist()
        accs.append(a)
        return len(accs) - 1
    p = add(pos.astype(np.float32), 34962, 5126, 'VEC3', True)
    n = add(nrm.astype(np.float32), 34962, 5126, 'VEC3')
    c = add(col.astype(np.float32), 34962, 5126, 'VEC4')
    i = add(idx.astype(np.uint32).reshape(-1), 34963, 5125, 'SCALAR')
    binc = b''.join(chunks)
    gltf = {'asset': {'version': '2.0', 'generator': 'TOMS make_hero_3d.py'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
            'nodes': [{'mesh': 0, 'name': 'hero'}],
            'meshes': [{'name': 'hero', 'primitives': [{'attributes': {'POSITION': p, 'NORMAL': n, 'COLOR_0': c},
                                                        'indices': i, 'material': 0}]}],
            'materials': [{'name': 'painted', 'doubleSided': True,   # the cape is a sheet thinner than the grid: open in places
                           'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.0,
                                                    'roughnessFactor': 0.9}}],
            'buffers': [{'byteLength': len(binc)}], 'bufferViews': views, 'accessors': accs}
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binc))
    out += struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(binc), 0x004E4942) + binc
    Path(path).write_bytes(out)


def neighbour_mean(tri, n):
    """The matrix that averages each vertex with its mesh neighbours (itself included)."""
    e = np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]])
    adj = sparse.coo_matrix((np.ones(len(e) * 2), (np.r_[e[:, 0], e[:, 1]], np.r_[e[:, 1], e[:, 0]])), (n, n)).tocsr()
    adj.data[:] = 1.0
    adj = adj + sparse.identity(n, format='csr')
    return sparse.diags(1.0 / np.asarray(adj.sum(1)).ravel()) @ adj


def taubin(pos, tri, rounds):
    """Taubin smoothing: the voxel steps of the surface net (the cape's ripples) go, the volume stays."""
    m = neighbour_mean(tri, len(pos))
    p = pos.astype(np.float64)
    for _ in range(rounds):
        p = p + 0.5 * (m @ p - p)
        p = p - 0.53 * (m @ p - p)
    return p.astype(np.float32)


def vertex_normals(pos, tri):
    """Normals that ignore the winding: where the cape is thinner than the shape's grid the surface net leaves faces
    wound both ways (and edges shared by 3-4 faces), so the plain sum of face normals cancels and the lighting speckles.
    Each vertex takes the main axis of its faces' normals (the largest eigenvector of their sum of outer products),
    turned outward from the body's up axis; then every face is rewound to agree. Returns (normals, faces)."""
    fn = np.cross(pos[tri[:, 1]] - pos[tri[:, 0]], pos[tri[:, 2]] - pos[tri[:, 0]]).astype(np.float64)
    cov = np.zeros((len(pos), 3, 3))
    outer = fn[:, :, None] * fn[:, None, :]
    for k in range(3):
        np.add.at(cov, tri[:, k], outer)
    _, vecs = np.linalg.eigh(cov)
    vn = vecs[:, :, 2]
    axis = pos.mean(0)
    out = pos - axis
    out[:, 1] = 0.0                                      # outward = away from the vertical line through the middle
    vn *= np.where((vn * out).sum(1) < 0, -1.0, 1.0)[:, None]
    flip = (fn * vn[tri].sum(1)).sum(1) < 0
    tri = tri.copy()
    tri[flip] = tri[flip][:, [0, 2, 1]]
    return vn.astype(np.float32), tri


def filled_rgb(rgb_img, cut):
    """The picture's colours, with every pixel outside the figure taking the nearest figure pixel's colour (so a vertex
    on the mesh's edge does not pick up the background)."""
    a = np.asarray(cut.getchannel('A')) > 16
    rgb = np.asarray(rgb_img.convert('RGB')).astype(np.float32) / 255.0
    _, (iy, ix) = ndimage.distance_transform_edt(~a, return_indices=True)
    return rgb[iy, ix], a


# The painted views: the camera's turn about the up axis (degrees; 0 = the front, the concept), and the pose words.
VIEWS = {
    'back': (180, 'back view, seen from behind, his back to the viewer, the back of his head, no face visible'),
    'left': (90, 'side view, full profile facing left'),
    'right': (270, 'side view, full profile facing right'),
    'back_left': (135, 'three-quarter back view, seen from behind, looking away to the left, face hidden'),
    'back_right': (225, 'three-quarter back view, seen from behind, looking away to the right, face hidden'),
}
VIEW_W, VIEW_H = base.W, base.H
VIEW_NEGATIVE = ('photo, 3d render, blurry, text, watermark, frame, multiple characters, cropped, extra arms, extra limbs, '
                 'background scenery, floor shadow, chibi, big head, holding a weapon, bow, sword, blue cape, white cape')
# The cape: without the weight the views paint the concept's blue tunic on it (IP-Adapter pulls the colours in).
CAPE = '(a long brown leather cape covering his back:1.5), the same brown as the shoulder capelet'


def view_coords(pos, yaw, lo, hi):
    """Picture x, y (pixels, VIEW_W x VIEW_H, the figure 90% of the height) and depth (larger = nearer) of each vertex
    seen orthographically from the camera turned `yaw` degrees about the up axis (0 = in front, on +Z)."""
    t = np.radians(yaw)
    right = np.array([np.cos(t), 0.0, -np.sin(t)])
    toward = np.array([np.sin(t), 0.0, np.cos(t)])        # from the model to the camera
    c = (lo + hi) / 2
    s = VIEW_H * 0.9 / (hi[1] - lo[1])
    p = pos - c
    return VIEW_W / 2 + (p @ right) * s, VIEW_H / 2 - p[:, 1] * s, p @ toward, toward


def depth_buffer(pos, tri, yaw, lo, hi):
    """The nearest depth per pixel: the vertices and the triangles' centres splatted 2 px around (the mesh is dense)."""
    pts = np.concatenate([pos, pos[tri].mean(1)])
    x, y, d, _ = view_coords(pts, yaw, lo, hi)
    zb = np.full((VIEW_H, VIEW_W), -np.inf, np.float32)
    xi, yi = x.round().astype(int), y.round().astype(int)
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            xx, yy = np.clip(xi + dx, 0, VIEW_W - 1), np.clip(yi + dy, 0, VIEW_H - 1)
            np.maximum.at(zb, (yy, xx), d.astype(np.float32))
    return zb


def depth_picture(zb):
    """ControlNet's depth: near white, far dark, the background black."""
    m = np.isfinite(zb)
    lo, hi = zb[m].min(), zb[m].max()
    v = np.zeros_like(zb)
    v[m] = 0.25 + 0.75 * (zb[m] - lo) / max(hi - lo, 1e-6)
    return Image.fromarray((v * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5)).convert('RGB')


def paint_workflow(depth_name, ref_name, words, seed, prefix):
    prompt = (f'full body character art of {HERO}, standing in an A pose, {words}, {CAPE}, painterly fantasy game character art, '
              f'soft even light, clean silhouette, plain white background')
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': base.CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': prompt, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': VIEW_NEGATIVE, 'clip': ['1', 1]}},
        '4': {'class_type': 'LoadImage', 'inputs': {'image': depth_name}},
        '5': {'class_type': 'ControlNetLoader', 'inputs': {'control_net_name': base.CONTROLNET}},
        '6': {'class_type': 'SetUnionControlNetType', 'inputs': {'control_net': ['5', 0], 'type': 'depth'}},
        '7': {'class_type': 'ControlNetApplyAdvanced',
              'inputs': {'positive': ['2', 0], 'negative': ['3', 0], 'control_net': ['6', 0], 'image': ['4', 0],
                         'strength': 1.0, 'start_percent': 0.0, 'end_percent': 1.0, 'vae': ['1', 2]}},
        '20': {'class_type': 'LoadImage', 'inputs': {'image': ref_name}},
        '21': {'class_type': 'IPAdapterModelLoader', 'inputs': {'ipadapter_file': IPADAPTER}},
        '22': {'class_type': 'CLIPVisionLoader', 'inputs': {'clip_name': CLIP_VISION}},
        '23': {'class_type': 'IPAdapterAdvanced',
               'inputs': {'model': ['1', 0], 'ipadapter': ['21', 0], 'image': ['20', 0], 'clip_vision': ['22', 0],
                          'weight': 0.6, 'weight_type': 'linear', 'combine_embeds': 'concat', 'start_at': 0.0,
                          'end_at': 1.0, 'embeds_scaling': 'V only'}},
        '8': {'class_type': 'EmptyLatentImage', 'inputs': {'width': VIEW_W, 'height': VIEW_H, 'batch_size': 1}},
        '9': {'class_type': 'KSampler', 'inputs': {'model': ['23', 0], 'positive': ['7', 0], 'negative': ['7', 1],
                                                   'latent_image': ['8', 0], 'seed': seed, 'steps': 32, 'cfg': 6.0,
                                                   'sampler_name': 'dpmpp_2m', 'scheduler': 'karras', 'denoise': 1.0}},
        '10': {'class_type': 'VAEDecode', 'inputs': {'samples': ['9', 0], 'vae': ['1', 2]}},
        '11': {'class_type': 'SaveImage', 'inputs': {'images': ['10', 0], 'filename_prefix': prefix}},
        '12': {'class_type': 'InspyrenetRembg', 'inputs': {'image': ['10', 0], 'torchscript_jit': 'default'}},
        '13': {'class_type': 'SaveImage', 'inputs': {'images': ['12', 0], 'filename_prefix': prefix + '_cut'}},
    }


def front_samples(pos, ref, cut, lo, hi):
    """The concept seen as the front view: the mesh's height spans the figure's height (Hunyuan3D-2 made it so)."""
    rgb, a = filled_rgb(ref, cut)
    ys, xs = np.nonzero(a)
    fx0, fx1, fy0, fy1 = xs.min(), xs.max(), ys.min(), ys.max()
    s = (fy1 - fy0) / (hi[1] - lo[1])                    # picture pixels per model unit
    px = np.clip(((pos[:, 0] - (lo[0] + hi[0]) / 2) * s + (fx0 + fx1) / 2).round().astype(int), 0, rgb.shape[1] - 1)
    py = np.clip(((hi[1] - pos[:, 1]) * s + fy0).round().astype(int), 0, rgb.shape[0] - 1)
    return rgb[py, px]


def colour_vertices(pos, nrm, tri, views, ref, cut):
    """Blend the views that see each vertex: weight = (how squarely it faces the camera)^3, only where the vertex is
    the nearest surface at its pixel. `views`: [(yaw, rgb picture, cut picture)], the front (yaw 0) is the concept."""
    lo, hi = pos.min(0), pos.max(0)
    acc = np.zeros((len(pos), 3), np.float64)
    wsum = np.zeros(len(pos), np.float64)
    eps = 0.015 * (hi[1] - lo[1])
    for yaw, img, vcut in [(0, ref, cut)] + views:
        x, y, d, toward = view_coords(pos, yaw, lo, hi)
        zb = depth_buffer(pos, tri, yaw, lo, hi)
        xi, yi = np.clip(x.round().astype(int), 0, VIEW_W - 1), np.clip(y.round().astype(int), 0, VIEW_H - 1)
        seen = d >= zb[yi, xi] - eps
        # |n.v|: a sheet thinner than eps (the cape) passes the depth test on both of its faces; both take this view's
        # colour, else the two faces differ and flicker against each other. A thick part's far side fails the test.
        w = np.abs(nrm @ toward) ** 3 * seen
        if yaw == 0:
            rgb = front_samples(pos, img, vcut, lo, hi)
        else:
            pic, _ = filled_rgb(img.resize((VIEW_W, VIEW_H)), vcut.resize((VIEW_W, VIEW_H)))
            rgb = pic[yi, xi]
            w = w * 0.8                                    # the concept is the most faithful: it wins where it sees
        acc += rgb * w[:, None]
        wsum += w
    col = np.ones((len(pos), 4), np.float32)
    have = wsum > 1e-4
    acc[have] /= wsum[have, None]
    if (~have).any():                                    # unseen (under the arms, between the legs): the nearest seen
        _, j = cKDTree(pos[have]).query(pos[~have])
        acc[~have] = acc[have][j]
    # a few rounds of each vertex averaged with its neighbours: the views differ a little where they meet (speckles)
    adj = neighbour_mean(tri, len(pos))
    for _ in range(SMOOTH):
        acc = adj @ acc
    col[:, :3] = acc ** 2.2                              # COLOR_0 is linear
    return col, int((~have).sum())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ref', default=str(ROOT / 'Build' / 'art_out' / 'hero_m' / 'concept_606.png'))
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--octree', type=int, default=256, help='the shape decoder\'s grid (higher: finer, slower)')
    ap.add_argument('--views', default='back,left,right,back_left', help='the painted views (VIEWS)')
    ap.add_argument('--view-seeds', default='9,9,9,8',   # the picks for concept 606 (brown cape all round)
                    help='the views\' seeds: one, or one per view')
    ap.add_argument('--repaint', action='store_true', help='paint the views again (else the saved pictures are used)')
    ap.add_argument('--smooth', type=int, default=10, help='rounds of shape smoothing (0: the raw surface)')
    ap.add_argument('--recolour', action='store_true', help='use the saved _shape.glb (no new shape)')
    a = ap.parse_args()
    work = ROOT / 'Build' / 'art_out' / 'hero_3d'
    work.mkdir(parents=True, exist_ok=True)
    ref = Image.open(a.ref)
    cut = Image.open(a.ref.replace('.png', '_cut.png')).convert('RGBA')
    shape = work / f'hero_{a.seed}_shape.glb'
    if not a.recolour:
        sq = square_input(cut)
        sq.save(work / 'input.png')
        t0 = time.time()
        data = run_for_file(workflow(base.upload(sq, 'hero_3d_input.png'), a.seed, a.octree, 'hero_3d/hero'))
        shape.write_bytes(data)
        print(f'{shape}: {time.time() - t0:.0f} s')
    pos, idx = read_glb(shape.read_bytes())
    tri = idx.reshape(-1, 3)
    pos = taubin(pos, tri, a.smooth)
    nrm, tri = vertex_normals(pos, tri)
    lo, hi = pos.min(0), pos.max(0)

    # the other sides, painted on the mesh's depth
    names = [v for v in a.views.split(',') if v]
    seeds = [int(s) for s in a.view_seeds.split(',')]
    ref_name = None
    views = []
    for i, name in enumerate(names):
        yaw, words = VIEWS[name]
        seed = seeds[min(i, len(seeds) - 1)]
        pic, pcut = work / f'view_{name}_{seed}.png', work / f'view_{name}_{seed}_cut.png'
        if a.repaint or not pic.exists():
            depth = depth_picture(depth_buffer(pos, tri, yaw, lo, hi))
            depth.save(work / f'view_{name}_depth.png')
            ref_name = ref_name or base.upload(ref.convert('RGB'), 'hero_ref.png')
            t0 = time.time()
            imgs = base.run(paint_workflow(base.upload(depth, f'hero_3d_{name}_depth.png'), ref_name, words, seed,
                                           f'hero_3d/view_{name}'))
            imgs[0].convert('RGB').save(pic)
            imgs[1].save(pcut)
            print(f'{pic}: {time.time() - t0:.0f} s')
        views.append((yaw, Image.open(pic), Image.open(pcut).convert('RGBA')))

    col, unseen = colour_vertices(pos, nrm, tri, views, ref, cut)
    out = work / f'hero_{a.seed}.glb'
    write_glb(out, pos, nrm, col, tri)
    print(f'{out}: {len(pos)} vertices ({unseen} seen by no view), {len(tri)} triangles, '
          f'size {np.round(hi - lo, 3).tolist()}')


if __name__ == '__main__':
    main()
