#!/usr/bin/env python3
"""make_brawler_art.py -- the brawler showcase character's art, from ComfyUI to atlas parts (docs/engine/18_NODE_EFFECTS.md).

    python tools/art/make_brawler_art.py pose                 # the OpenPose skeleton the picture is drawn on
    python tools/art/make_brawler_art.py concept [--seeds 1,2,3,4]
    python tools/art/make_brawler_art.py parts --pick N       # cut concept N into parts -> assets/media/brawler/parts/

The character is drawn ONCE, whole, in a rig-friendly stance: the skeleton (JOINTS) is drawn as an OpenPose picture and
ControlNet (union, openpose mode) makes ComfyUI put the body's joints there. The parts are then cut along the same
bones: every visible pixel goes to the bone segment nearest to it (with a little overlap at the joints, so a bent
elbow shows no gap), and each part keeps its joint as its pivot. The rig in tools/make_brawler_anim.py uses the same
JOINTS, so the parts sit together at rest exactly as in the picture.

Needs the ComfyUI server (AIGamestyle/_pipeline/tools/start_comfyui_with_flags.bat) at 127.0.0.1:8188.
"""
import argparse
import io
import json
import math
import time
import urllib.request
import uuid
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'Build' / 'art_out' / 'brawler'   # ComfyUI pictures (Build/ is not in git)
PARTS = ROOT / 'Build' / 'art_out' / 'brawler' / 'parts'   # packed into assets/media/atlas/brawler.atlas
SERVER = '127.0.0.1:8188'
CHECKPOINT = 'Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors'
CONTROLNET = 'controlnet-union-sdxl-promax.safetensors'
W, H = 832, 1216

PROMPT = ('full body character concept art of a short stocky young male martial artist brawler, a man, heroic chibi proportions, '
          'big head, huge muscular forearms and big clenched fists, broad shoulders, '
          'spiky silver white hair tied in a short ponytail, tan skin, determined grin, '
          'sleeveless crimson vest with gold trim, black wide martial arts trousers, white cloth wraps on the forearms, '
          'brown leather boots, three-quarter view facing right, A-pose, both arms spread out away from the body, fists far from the hips, legs apart, '
          '2D fighting game character art, clean cel shading, bold clean lineart, hand painted, vivid colors, '
          'plain white background, full body visible, centered')
NEGATIVE = ('photo, realistic, 3d render, blurry, text, watermark, logo, frame, multiple characters, cropped, '
            'red hair, prayer beads, rope belt, blue gi, headband, weapon, extra arms, extra fingers, '
            'arms crossed, hands behind back, background scenery, floor shadow')

# The skeleton, in picture pixels (W x H), OpenPose COCO-18 order. Three-quarter view facing right: the near side is
# the character's left (screen right). Arms held out and down, legs apart: the parts overlap as little as possible.
JOINTS = {
    'nose': (452, 262), 'neck': (420, 400),
    'r_shoulder': (318, 420), 'r_elbow': (206, 530), 'r_wrist': (120, 650),
    'l_shoulder': (528, 420), 'l_elbow': (640, 530), 'l_wrist': (726, 650),
    'r_hip': (366, 720), 'r_knee': (300, 900), 'r_ankle': (270, 1100),
    'l_hip': (478, 720), 'l_knee': (548, 900), 'l_ankle': (584, 1100),
    'r_eye': (432, 236), 'l_eye': (480, 240), 'r_ear': (380, 246), 'l_ear': (520, 250),
}
ORDER = ['nose', 'neck', 'r_shoulder', 'r_elbow', 'r_wrist', 'l_shoulder', 'l_elbow', 'l_wrist', 'r_hip', 'r_knee',
         'r_ankle', 'l_hip', 'l_knee', 'l_ankle', 'r_eye', 'l_eye', 'r_ear', 'l_ear']
LIMBS = [(1, 2), (1, 5), (2, 3), (3, 4), (5, 6), (6, 7), (1, 8), (8, 9), (9, 10), (1, 11), (11, 12), (12, 13),
         (1, 0), (0, 14), (14, 16), (0, 15), (15, 17)]
COLORS = [(255, 0, 0), (255, 85, 0), (255, 170, 0), (255, 255, 0), (170, 255, 0), (85, 255, 0), (0, 255, 0),
          (0, 255, 85), (0, 255, 170), (0, 255, 255), (0, 170, 255), (0, 85, 255), (0, 0, 255), (85, 0, 255),
          (170, 0, 255), (255, 0, 255), (255, 0, 170), (255, 0, 85)]


def pose_image():
    img = Image.new('RGB', (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img, 'RGBA')
    for i, (a, b) in enumerate(LIMBS):
        pa, pb = JOINTS[ORDER[a]], JOINTS[ORDER[b]]
        c = COLORS[i % len(COLORS)]
        # OpenPose draws each limb as a filled ellipse along the segment, 60% bright
        mx, my = (pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2
        length = math.hypot(pb[0] - pa[0], pb[1] - pa[1]) / 2
        ang = math.atan2(pb[1] - pa[1], pb[0] - pa[0])
        pts = []
        for k in range(36):
            t = 2 * math.pi * k / 36
            x, y = length * math.cos(t), 6 * math.sin(t)
            pts.append((mx + x * math.cos(ang) - y * math.sin(ang), my + x * math.sin(ang) + y * math.cos(ang)))
        d.polygon(pts, fill=(int(c[0] * 0.6), int(c[1] * 0.6), int(c[2] * 0.6), 255))
    for i, name in enumerate(ORDER):
        x, y = JOINTS[name]
        d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=COLORS[i])
    return img


def http(path, data=None, ctype='application/json'):
    req = urllib.request.Request(f'http://{SERVER}{path}', data=data, headers={'Content-Type': ctype} if data else {})
    with urllib.request.urlopen(req, timeout=1800) as r:
        return r.read()


def upload(img, name):
    buf = io.BytesIO()
    img.save(buf, 'PNG')
    boundary = uuid.uuid4().hex
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="image"; filename="{name}"\r\n'
            f'Content-Type: image/png\r\n\r\n').encode() + buf.getvalue() + \
           f'\r\n--{boundary}\r\nContent-Disposition: form-data; name="overwrite"\r\n\r\ntrue\r\n--{boundary}--\r\n'.encode()
    out = json.loads(http('/upload/image', body, f'multipart/form-data; boundary={boundary}'))
    return out['name']


def run(wf):
    pid = json.loads(http('/prompt', json.dumps({'prompt': wf, 'client_id': uuid.uuid4().hex}).encode()))['prompt_id']
    while True:
        hist = json.loads(http(f'/history/{pid}'))
        if pid in hist:
            h = hist[pid]
            if h.get('status', {}).get('status_str') == 'error':
                raise RuntimeError(json.dumps(h['status'], indent=1)[:2000])
            imgs = []
            for node in h['outputs'].values():
                for im in node.get('images', []):
                    q = f"/view?filename={im['filename']}&subfolder={im['subfolder']}&type={im['type']}"
                    imgs.append(Image.open(io.BytesIO(http(q))).copy())
            return imgs
        time.sleep(1.0)


def concept_workflow(pose_name, seed):
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': PROMPT, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': NEGATIVE, 'clip': ['1', 1]}},
        '4': {'class_type': 'LoadImage', 'inputs': {'image': pose_name}},
        '5': {'class_type': 'ControlNetLoader', 'inputs': {'control_net_name': CONTROLNET}},
        '6': {'class_type': 'SetUnionControlNetType', 'inputs': {'control_net': ['5', 0], 'type': 'openpose'}},
        '7': {'class_type': 'ControlNetApplyAdvanced',
              'inputs': {'positive': ['2', 0], 'negative': ['3', 0], 'control_net': ['6', 0], 'image': ['4', 0],
                         'strength': 1.0, 'start_percent': 0.0, 'end_percent': 1.0, 'vae': ['1', 2]}},
        '8': {'class_type': 'EmptyLatentImage', 'inputs': {'width': W, 'height': H, 'batch_size': 1}},
        '9': {'class_type': 'KSampler', 'inputs': {'model': ['1', 0], 'positive': ['7', 0], 'negative': ['7', 1],
                                                   'latent_image': ['8', 0], 'seed': seed, 'steps': 32, 'cfg': 6.0,
                                                   'sampler_name': 'dpmpp_2m', 'scheduler': 'karras', 'denoise': 1.0}},
        '10': {'class_type': 'VAEDecode', 'inputs': {'samples': ['9', 0], 'vae': ['1', 2]}},
        '11': {'class_type': 'SaveImage', 'inputs': {'images': ['10', 0], 'filename_prefix': 'brawler_concept'}},
        '12': {'class_type': 'InspyrenetRembg', 'inputs': {'image': ['10', 0], 'torchscript_jit': 'default'}},
        '13': {'class_type': 'SaveImage', 'inputs': {'images': ['12', 0], 'filename_prefix': 'brawler_cut'}},
    }


def cmd_pose(_):
    WORK.mkdir(parents=True, exist_ok=True)
    pose_image().save(WORK / 'pose.png')
    print(WORK / 'pose.png')


def cmd_concept(a):
    WORK.mkdir(parents=True, exist_ok=True)
    img = pose_image()
    img.save(WORK / 'pose.png')
    name = upload(img, 'brawler_pose.png')
    for seed in [int(s) for s in a.seeds.split(',')]:
        t0 = time.time()
        imgs = run(concept_workflow(name, seed))
        imgs[0].convert('RGB').save(WORK / f'concept_{seed}.png')
        imgs[1].save(WORK / f'concept_{seed}_cut.png')
        print(f'seed {seed}: {time.time() - t0:.0f} s -> {WORK / f"concept_{seed}_cut.png"}')


# ---- parts -----------------------------------------------------------------------------------------------------------
#
# The joints as drawn in the chosen picture (measured on it; picture pixels). "back" is the character's far side (screen
# left), "front" the near side (screen right, the way it faces).
RIG = {
    'neck': (440, 380), 'tie': (400, 135), 'waist': (425, 630), 'pelvis': (420, 690),
    'back_shoulder': (305, 430), 'back_elbow': (215, 530), 'back_wrist': (140, 650), 'back_fist': (140, 708),
    'front_shoulder': (548, 430), 'front_elbow': (650, 522), 'front_wrist': (712, 642), 'front_fist': (714, 702),
    'back_hip': (362, 715), 'back_knee': (300, 930), 'back_ankle': (262, 1100), 'back_toe': (180, 1160),
    'front_hip': (482, 715), 'front_knee': (540, 930), 'front_ankle': (592, 1100), 'front_toe': (680, 1160),
    'sash_knot': (400, 650), 'ground': (425, 1175),
}
# part: (bone segment from, to, the joint it turns on, its parent part). Each opaque pixel goes to the nearest segment.
PART_BONES = {
    'head': ('neck', (450, 150), 'neck', 'torso'),
    'torso': ((440, 400), 'waist', 'waist', None),
    'pelvis': ((372, 690), (470, 690), 'pelvis', 'torso'),
    'back_upper': ('back_shoulder', 'back_elbow', 'back_shoulder', 'torso'),
    'back_forearm': ('back_elbow', 'back_wrist', 'back_elbow', 'back_upper'),
    'back_fist': ('back_wrist', 'back_fist', 'back_wrist', 'back_forearm'),
    'front_upper': ('front_shoulder', 'front_elbow', 'front_shoulder', 'torso'),
    'front_forearm': ('front_elbow', 'front_wrist', 'front_elbow', 'front_upper'),
    'front_fist': ('front_wrist', 'front_fist', 'front_wrist', 'front_forearm'),
    'back_leg': ('back_hip', 'back_ankle', 'back_hip', 'pelvis'),
    'front_leg': ('front_hip', 'front_ankle', 'front_hip', 'pelvis'),
}
# parts made by rules, not by a bone: (joint, parent)
RULE_PARTS = {'ponytail': ('tie', 'head'), 'sash_tail': ('sash_knot', 'pelvis')}
# Bone segments cut together into one part, a skinned mesh: an arm (upper arm + forearm) bends at the elbow; a leg
# (trousers + boot, its segment hip..ankle) at the knee and the ankle. Two rigid parts turned far apart show their cut
# ends at the joint; one bent mesh does not. part: (joint, parent)
MERGED = {'back_arm': (('back_upper', 'back_forearm'), 'back_shoulder', 'torso'),
          'front_arm': (('front_upper', 'front_forearm'), 'front_shoulder', 'torso')}
FIST_PARENT = {'back_fist': 'back_arm', 'front_fist': 'front_arm'}
THICK = {'torso': 70, 'head': 30, 'pelvis': 20}   # half widths (picture pixels) of the capsule parts
SCALE = 0.5          # the parts' size: the figure about 565 px tall (an HD 2D fighter on a 1080p screen)
OVERLAP = 9          # picture pixels a part reaches into its parent, so a turned joint shows no gap
HIDDEN = {}          # part -> (pixels, colours) painted in under the sash (cmd_parts)


def joint(v):
    return np.array(RIG[v] if isinstance(v, str) else v, dtype=np.float32)


def segment_distance(px, a, b):
    ab = b - a
    t = np.clip(((px - a) @ ab) / max(float(ab @ ab), 1e-6), 0.0, 1.0)
    return np.linalg.norm(px - (a + t[:, None] * ab), axis=1)


def cmd_parts(a):
    src = Image.open(WORK / f'concept_{a.pick}_cut.png').convert('RGBA')
    rgba = np.asarray(src).copy()
    alpha = rgba[..., 3]
    # The cut-out's soft edge still holds the light background's colour (a pale halo where parts overlap): every
    # pixel that is not fully solid takes the colour of the nearest solid one; its alpha stays.
    from scipy import ndimage
    solid = alpha >= 250
    _, (iy, ix) = ndimage.distance_transform_edt(~solid, return_indices=True)
    edge = ~solid
    rgba[edge, :3] = rgba[iy[edge], ix[edge], :3]
    alpha = rgba[..., 3]
    ys, xs = np.nonzero(alpha > 12)
    px = np.stack([xs, ys], axis=1).astype(np.float32)
    names = list(PART_BONES)
    # Thick parts are capsules: the distance to the bone minus their half width, so a broad chest stays the torso's.
    dist = np.stack([segment_distance(px, joint(f), joint(t)) - THICK.get(n, 0)
                     for n, (f, t, _, _) in PART_BONES.items()], axis=1)
    label = np.array(names, dtype=object)[np.argmin(dist, axis=1)]
    r, g, b = (rgba[ys, xs, k].astype(int) for k in range(3))
    label[(xs < 355) & (ys < 350) | (xs < 395) & (ys < 125)] = 'ponytail'
    # The sash's tail: red, in a band along it (knot to tip), its small holes closed.
    band = (segment_distance(px, joint((392, 668)), joint((188, 872))) < 40) & (ys > 668)
    red = np.zeros(alpha.shape, np.uint8)
    red[ys, xs] = ((r > 85) & (r - g > 50) & (r - b > 40) & band) * 255
    red = np.asarray(Image.fromarray(red).filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.MinFilter(7)))
    label[(red[ys, xs] > 0) & band] = 'sash_tail'
    for merged, (pieces, _, _) in MERGED.items():
        label[np.isin(label, pieces)] = merged
    lab = np.full(alpha.shape, '', dtype=object)
    lab[ys, xs] = label
    # What the sash's tail hides: the trousers under it (inside the legs' and pelvis's closed outline) are painted in from
    # around (OpenCV Telea) and given to the part under it, so the leg shows no hole when the sash swings.
    import cv2
    sash = lab == 'sash_tail'
    under = np.isin(lab, ['back_leg', 'pelvis', 'front_leg'])
    closed = cv2.morphologyEx(under.astype(np.uint8) * 255, cv2.MORPH_CLOSE, np.ones((45, 45), np.uint8)) > 0
    hidden = sash & closed
    if hidden.any():
        bgr = cv2.cvtColor(rgba[..., :3], cv2.COLOR_RGB2BGR)
        mask = cv2.dilate(hidden.astype(np.uint8) * 255, np.ones((5, 5), np.uint8))
        filled = cv2.cvtColor(cv2.inpaint(bgr, mask, 9, cv2.INPAINT_TELEA), cv2.COLOR_BGR2RGB)
        hy, hx = np.nonzero(hidden)
        nearest = {}
        for y, x in zip(hy, hx):   # the part under: the nearest leg / pelvis pixel's
            for rad in range(1, 60):
                y0, y1, x0, x1 = max(y - rad, 0), y + rad + 1, max(x - rad, 0), x + rad + 1
                win = lab[y0:y1, x0:x1]
                hit = np.isin(win, ['back_leg', 'pelvis', 'front_leg'])
                if hit.any():
                    wy, wx = np.argwhere(hit)[0]
                    nearest[(y, x)] = win[wy, wx]
                    break
        under_px = {}
        for (y, x), part in nearest.items():
            under_px.setdefault(part, []).append((y, x))
        HIDDEN.clear()
        for part, pts in under_px.items():
            arr = np.array(pts)
            HIDDEN[part] = (arr, filled[arr[:, 0], arr[:, 1]])

    pieces = {p for v in MERGED.values() for p in v[0]}
    parents = {k: v[3] for k, v in PART_BONES.items() if k not in pieces} | {k: v[1] for k, v in RULE_PARTS.items()}
    joints = {k: v[2] for k, v in PART_BONES.items() if k not in pieces} | {k: v[0] for k, v in RULE_PARTS.items()}
    for k, (_, j, parent) in MERGED.items():
        parents[k], joints[k] = parent, j
    parents.update(FIST_PARENT)
    PARTS.mkdir(parents=True, exist_ok=True)
    for old in PARTS.glob('*.png'):
        old.unlink()
    rig = {'scale': SCALE, 'ground': RIG['ground'], 'joints': {k: list(v) for k, v in RIG.items()}, 'parts': {}}
    k = OVERLAP * 2 + 1
    for name in parents:
        mask = lab == name
        if not mask.any():
            raise SystemExit(f'part {name}: no pixels')
        parent = parents[name]
        if parent:   # reach into the parent: the joint is covered when it turns
            grown = np.asarray(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(k))) > 0
            mask |= grown & (lab == parent)
        part = rgba.copy()
        part[..., 3] = np.where(mask, part[..., 3], 0)
        if name in HIDDEN:   # the painted pixels under the sash
            pts, colours = HIDDEN[name]
            part[pts[:, 0], pts[:, 1], :3] = colours
            part[pts[:, 0], pts[:, 1], 3] = 255
        img = Image.fromarray(part, 'RGBA')
        x0, y0, x1, y1 = img.getbbox()
        x0, y0, x1, y1 = max(x0 - 4, 0), max(y0 - 4, 0), min(x1 + 4, W), min(y1 + 4, H)
        img = img.crop((x0, y0, x1, y1))
        w, h = max(1, round(img.width * SCALE)), max(1, round(img.height * SCALE))
        img = img.convert('RGBa').resize((w, h), Image.LANCZOS).convert('RGBA')   # premultiplied: no dark fringes
        img.save(PARTS / f'{name}.png')
        jx, jy = RIG[joints[name]]
        rig['parts'][name] = {'joint': joints[name], 'parent': parent, 'size': [w, h],
                              'pivot': [round((jx - x0) / (x1 - x0), 4), round((jy - y0) / (y1 - y0), 4)]}
        print(f'{name:14s} {w:4d} x {h:4d}  pivot {rig["parts"][name]["pivot"]}')
    out = ROOT / 'tools' / 'art' / 'brawler_rig.json'
    out.write_text(json.dumps(rig, indent=1) + '\n', encoding='utf-8')
    # a check picture: every part in its own colour
    check = np.zeros_like(rgba)
    rng = np.random.default_rng(3)
    for name in parents:
        check[lab == name] = list(rng.integers(60, 255, 3)) + [255]
    Image.fromarray(check, 'RGBA').save(WORK / 'parts_labels.png')
    print(out)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('pose').set_defaults(fn=cmd_pose)
    c = sub.add_parser('concept')
    c.add_argument('--seeds', default='11,22,33,44')
    c.set_defaults(fn=cmd_concept)
    pp = sub.add_parser('parts')
    pp.add_argument('--pick', default='55', help='the concept seed to cut')
    pp.set_defaults(fn=cmd_parts)
    args = ap.parse_args()
    args.fn(args)
