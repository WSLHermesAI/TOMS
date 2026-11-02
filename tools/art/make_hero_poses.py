#!/usr/bin/env python3
"""make_hero_poses.py -- the player's battle key poses, painted one by one (docs/gameplay/17 Q58, docs/engine/20_CHARACTER_ART.md).

    python tools/art/make_hero_poses.py --weapon bow [--seeds 11,22] [--pose charge] [--ref Build/art_out/hero_m/concept_606.png]

Each pose of a weapon is its own picture: an OpenPose skeleton drawn for that pose (POSES) makes ControlNet put the body
there, and IP-Adapter (the owner's pick, hero_m 606, as the reference picture) keeps it the same person in the same
clothes. The weapon is painted in the hands, so a pose can turn, lunge and foreshorten freely -- what one picture cut
into parts cannot (option B of Q58). The anim editor then plays the poses as sprite keys with tweens and effects.

Pictures go to Build/art_out/hero_<weapon>/<pose>_<seed>.png and <pose>_<seed>_cut.png (background removed), and a
contact sheet to Build/art_out/hero_<weapon>/sheet.png.

Needs the ComfyUI server at 127.0.0.1:8188 with the models of docs/engine/20_CHARACTER_ART.md.
"""
import argparse
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_brawler_art as base   # noqa: E402  (the ComfyUI helpers and the OpenPose drawing)

ROOT = base.ROOT
IPADAPTER = 'ip-adapter-plus_sdxl_vit-h.safetensors'
CLIP_VISION = 'CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors'

HERO = ('an adult man in his twenties, an adventurer hero, messy brown hair, determined face, blue tunic with white '
        'sleeves, brown leather armor pieces and bracers, a long brown leather cape, leather belt with pouches, dark '
        'trousers, brown boots')
STYLE = ('side view, his body turned to face right, looking to the right, three-quarter profile, '
         'digital painting, painterly dark fantasy game character art, dynamic action pose, visible brush strokes, soft rim '
         'light, clean silhouette, full body visible, plain white background, centered')
NEGATIVE = ('photo, 3d render, blurry, text, watermark, logo, frame, multiple characters, cropped, extra arms, extra '
            'fingers, extra limbs, two bows, background scenery, floor shadow, chibi, child, big head, front view, '
            'facing the viewer, looking at the viewer, symmetrical pose, standing still')


def head(nose, facing=1):
    """The face's points around the nose (eyes, ears), for a head turned right."""
    x, y = nose
    return {'nose': (x, y), 'r_eye': (x - 12 * facing, y - 22), 'l_eye': (x + 18 * facing, y - 20),
            'r_ear': (x - 52 * facing, y - 10), 'l_ear': (x + 30 * facing, y - 8)}


def pose(nose, neck, rs, re, rw, ls, le, lw, rh, rk, ra, lh, lk, la):
    """A skeleton (picture pixels, 832 x 1216). r_ = the far side (screen left), l_ = the near side (it faces right)."""
    j = head(nose)
    j.update({'neck': neck, 'r_shoulder': rs, 'r_elbow': re, 'r_wrist': rw, 'l_shoulder': ls, 'l_elbow': le, 'l_wrist': lw,
              'r_hip': rh, 'r_knee': rk, 'r_ankle': ra, 'l_hip': lh, 'l_knee': lk, 'l_ankle': la})
    return j


# weapon -> pose -> (skeleton, what the pose words say). Adult proportions: a small head, long legs.
POSES = {
    'bow': {
        'idle': (pose((455, 190), (425, 300), (370, 330), (340, 455), (345, 575), (480, 330), (540, 440), (590, 540),
                      (395, 640), (340, 870), (300, 1105), (460, 640), (520, 870), (565, 1105)),
                 'standing ready, holding a wooden recurve bow lowered in his left hand, right hand relaxed, calm stance'),
        'charge': (pose((470, 195), (430, 305), (345, 335), (270, 305), (420, 250), (515, 330), (635, 330), (755, 330),
                        (380, 640), (300, 860), (245, 1105), (475, 640), (565, 850), (630, 1105)),
                   'drawing a wooden recurve bow at full draw, left arm straight forward to the right holding the bow, '
                   'right hand pulling the string back to his cheek, an arrow nocked, aiming right, wide archer stance'),
        'attack': (pose((480, 195), (440, 305), (380, 335), (300, 290), (215, 250), (500, 330), (630, 322), (760, 315),
                        (395, 640), (300, 860), (240, 1105), (465, 640), (565, 850), (640, 1105)),
                   'just released an arrow from a wooden recurve bow, the bow held in his forward left hand on the right '
                   'side of the picture, arm straight toward the right, right hand flung back open behind his head after '
                   'the release, the arrow flying away to the right'),
        'miss': (pose((400, 205), (390, 315), (305, 345), (255, 435), (195, 500), (470, 330), (585, 255), (665, 170),
                      (360, 650), (300, 880), (215, 1100), (450, 650), (520, 880), (585, 1105)),
                 'stumbling backwards off balance, the wooden recurve bow swinging up in his left hand, right arm '
                 'flailing back, a surprised face'),
        'hurt': (pose((340, 250), (360, 350), (310, 380), (300, 490), (360, 545), (420, 375), (470, 480), (430, 560),
                      (380, 670), (320, 880), (250, 1105), (445, 670), (540, 860), (600, 1105)),
                 'staggering backwards from a heavy hit, body bent and leaning back in pain, one knee buckling, '
                 'arms pulled in to protect his chest, holding the wooden recurve bow close, grimacing, eyes shut'),
        'attack_big': (pose((455, 120), (425, 230), (340, 260), (280, 280), (420, 175), (505, 255), (630, 300), (760, 345),
                            (380, 560), (300, 760), (345, 905), (470, 560), (530, 700), (470, 880)),
                       'leaping high in mid air at full draw of a wooden recurve bow, bow arm stretched toward the lower '
                       'right, the string pulled back to his cheek, a glowing magic arrow nocked, the cape flying behind, '
                       'knees tucked, dynamic heroic action shot'),
    },
}


def workflow(pose_name, ref_name, prompt, seed, prefix):
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': base.CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': prompt, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': NEGATIVE, 'clip': ['1', 1]}},
        '4': {'class_type': 'LoadImage', 'inputs': {'image': pose_name}},
        '5': {'class_type': 'ControlNetLoader', 'inputs': {'control_net_name': base.CONTROLNET}},
        '6': {'class_type': 'SetUnionControlNetType', 'inputs': {'control_net': ['5', 0], 'type': 'openpose'}},
        '7': {'class_type': 'ControlNetApplyAdvanced',
              'inputs': {'positive': ['2', 0], 'negative': ['3', 0], 'control_net': ['6', 0], 'image': ['4', 0],
                         'strength': 1.0, 'start_percent': 0.0, 'end_percent': 0.9, 'vae': ['1', 2]}},
        # the same person: IP-Adapter Plus with the chosen concept as the reference
        '20': {'class_type': 'LoadImage', 'inputs': {'image': ref_name}},
        '21': {'class_type': 'IPAdapterModelLoader', 'inputs': {'ipadapter_file': IPADAPTER}},
        '22': {'class_type': 'CLIPVisionLoader', 'inputs': {'clip_name': CLIP_VISION}},
        '23': {'class_type': 'IPAdapterAdvanced',
               'inputs': {'model': ['1', 0], 'ipadapter': ['21', 0], 'image': ['20', 0], 'clip_vision': ['22', 0],
                          'weight': 0.7, 'weight_type': 'linear', 'combine_embeds': 'concat', 'start_at': 0.0,
                          'end_at': 0.85, 'embeds_scaling': 'V only'}},
        '8': {'class_type': 'EmptyLatentImage', 'inputs': {'width': base.W, 'height': base.H, 'batch_size': 1}},
        '9': {'class_type': 'KSampler', 'inputs': {'model': ['23', 0], 'positive': ['7', 0], 'negative': ['7', 1],
                                                   'latent_image': ['8', 0], 'seed': seed, 'steps': 32, 'cfg': 6.0,
                                                   'sampler_name': 'dpmpp_2m', 'scheduler': 'karras', 'denoise': 1.0}},
        '10': {'class_type': 'VAEDecode', 'inputs': {'samples': ['9', 0], 'vae': ['1', 2]}},
        '11': {'class_type': 'SaveImage', 'inputs': {'images': ['10', 0], 'filename_prefix': prefix}},
        '12': {'class_type': 'InspyrenetRembg', 'inputs': {'image': ['10', 0], 'torchscript_jit': 'default'}},
        '13': {'class_type': 'SaveImage', 'inputs': {'images': ['12', 0], 'filename_prefix': prefix + '_cut'}},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--weapon', default='bow', choices=sorted(POSES))
    ap.add_argument('--seeds', default='11,22')
    ap.add_argument('--pose', default='', help='one pose only (default: all of the weapon)')
    ap.add_argument('--ref', default=str(ROOT / 'Build' / 'art_out' / 'hero_m' / 'concept_606.png'))
    a = ap.parse_args()
    work = ROOT / 'Build' / 'art_out' / f'hero_{a.weapon}'
    work.mkdir(parents=True, exist_ok=True)
    ref_name = base.upload(Image.open(a.ref).convert('RGB'), 'hero_ref.png')
    names = [a.pose] if a.pose else list(POSES[a.weapon])
    seeds = [int(s) for s in a.seeds.split(',')]
    for name in names:
        joints, words = POSES[a.weapon][name]
        base.JOINTS = joints
        img = base.pose_image()
        img.save(work / f'{name}_pose.png')
        pose_name = base.upload(img, f'hero_{a.weapon}_{name}_pose.png')
        prompt = f'full body character art of {HERO}, {words}, {STYLE}'
        for seed in seeds:
            t0 = time.time()
            imgs = base.run(workflow(pose_name, ref_name, prompt, seed, f'hero_{a.weapon}_{name}'))
            imgs[0].convert('RGB').save(work / f'{name}_{seed}.png')
            imgs[1].save(work / f'{name}_{seed}_cut.png')
            print(f'{name} seed {seed}: {time.time() - t0:.0f} s')
    # a contact sheet: a row per pose (its skeleton, then each seed)
    w, h = 220, 322
    rows = [n for n in POSES[a.weapon] if (work / f'{n}_{seeds[0]}.png').exists()]
    sheet = Image.new('RGB', (w * (len(seeds) + 1), (h + 22) * len(rows)), (30, 30, 34))
    d = ImageDraw.Draw(sheet)
    for r, name in enumerate(rows):
        y = r * (h + 22)
        cells = [work / f'{name}_pose.png'] + [work / f'{name}_{s}.png' for s in seeds]
        for c, f in enumerate(cells):
            if f.exists():
                sheet.paste(Image.open(f).convert('RGB').resize((w, h)), (c * w, y + 22))
        d.text((6, y + 4), f'{name}   (skeleton, then seeds {", ".join(map(str, seeds))})', fill=(255, 230, 150))
    sheet.save(work / 'sheet.png')
    print(work / 'sheet.png')


if __name__ == '__main__':
    main()
