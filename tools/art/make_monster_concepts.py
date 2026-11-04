#!/usr/bin/env python3
"""make_monster_concepts.py -- concept pictures for a family of cute, colourful new monsters, for the owner to pick from.

    python tools/art/make_monster_concepts.py [--seeds 11,22,33] [--only ember_1,moss_2] [--sheet-only]
    python tools/art/make_monster_concepts.py --teams     # cut the PICKS and lay every team out as in a battle
    python tools/art/make_monster_concepts.py --dark [--only ember_3] [--denoise 0.6]   # the black and white versions (after --teams)
    python tools/art/make_monster_concepts.py --watch     # the 40 x 40 versions of the black and white ones (no ComfyUI)

48 original monsters: 16 teams of three stages each (MONSTERS), blob, small, creature. The pictures are drawn from the
text alone: no reference picture goes in, so nothing is traced from another game's sprites. They go to
Build/art_out/monster_concepts/ (<id>_<seed>.png; sheet_N.png, four teams per sheet with every seed; teams.png).
A picture already there is skipped, unless --only names it.

Needs the ComfyUI server (AIGamestyle/_pipeline/tools/start_comfyui_with_flags.bat) at 127.0.0.1:8188.
"""
import argparse
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_brawler_art import CHECKPOINT, CONTROLNET, run, upload

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'Build' / 'art_out' / 'monster_concepts'
SIZE = 1024
WATCH = 40   # the watch version's size in pixels

STYLE = ('cute fantasy monster mascot character design, one single creature, centered, full body visible, '
         'adorable friendly face, big glossy eyes, soft rounded shapes, simple readable silhouette, '
         'bright saturated colors, clean cel shading, bold clean dark outline, 2D game sticker art, '
         'plain white background')
NEGATIVE = ('photo, realistic, 3d render, blurry, text, watermark, logo, signature, frame, multiple characters, '
            'character sheet, cropped, scary, horror, gore, sharp teeth, monochrome, grayscale, pixel art, '
            'human, person, background scenery, floor shadow')

# id: (name, what it is). A line's stages grow from a small blob to a creature with limbs; the colours stay one family.
MONSTERS = {
    'ember_1': ('Wickling', 'a tiny round blob of warm orange candle wax with a small flame on its head like a hair tuft, '
                            'melted wax drips at the bottom, no arms, no legs'),
    'ember_2': ('Lanternip', 'a small chubby orange and gold lantern creature with stubby feet and two tiny arms, '
                             'a cheerful flame glowing inside its round belly window, a little brass handle on its head'),
    'ember_3': ('Hearthound', 'a stout friendly round stove creature on four chunky iron legs, a body of red bricks and brass, '
                              'a small chimney on its back puffing a heart of smoke, a glowing fire door for a belly, '
                              'not a fox, not a dog, no fur, orange and deep red colors'),
    'moss_1': ('Sproutle', 'a tiny round mint green blob with two small leaves sprouting on its head, '
               'rosy cheeks, no arms, no legs'),
    'moss_2': ('Mossbun', 'a small chubby green rabbit-like creature made of soft moss, long leaf-shaped ears, '
                          'a tiny pink flower on one ear, stubby feet'),
    'moss_3': ('Thicketoad', 'a big friendly round toad covered in moss and ferns, a little mushroom garden and pink flowers '
                             'growing on its back, sturdy legs, green and teal colors'),
    'tide_1': ('Dropsy', 'a tiny round sky blue water droplet creature, translucent jelly body, one small swirl on top, '
                         'no arms, no legs'),
    'tide_2': ('Bubbloon', 'a small chubby blue and aqua jellyfish creature floating, a bubble-like round head, '
                           'short curly ribbon tentacles, tiny pearls on its forehead'),
    'tide_3': ('Coralisk', 'a stout friendly little sea dragon with pale blue skin, coral pink antlers, '
                           'small fin wings, a round belly, a curled tail, aqua and coral colors'),
    'star_1': ('Twinkit', 'a tiny round lilac purple puffball creature with a small golden star floating above its head, '
                          'no arms, no legs'),
    'star_2': ('Runimoth', 'a small chubby lilac moth creature with fluffy collar, big soft wings with glowing golden rune spots, '
                           'feathery antennae, stubby feet'),
    'star_3': ('Astrowl', 'a stout friendly owl sorcerer beast with violet and midnight blue feathers, a wizard hat of '
                          'starry cloth, glowing golden constellation marks on its wings, a crescent moon charm'),
    'frost_1': ('Snowmote', 'a tiny round white and ice blue snowball creature with a little icicle tuft on its head, '
                            'rosy cheeks, no arms, no legs'),
    'frost_2': ('Mittenpip', 'a small chubby ice blue penguin chick wearing a knitted red scarf and mittens, stubby feet'),
    'frost_3': ('Glacibear', 'a big friendly round polar bear cub with a back of pale blue ice crystals, '
                             'a knitted red scarf, chunky paws, white and ice blue colors'),
    'storm_1': ('Puffzap', 'a tiny round pale yellow cloud creature with one small lightning bolt tuft, no arms, no legs'),
    'storm_2': ('Stormlet', 'a small chubby lamb creature with wool of dark blue storm cloud, small yellow horns, stubby hooves, '
                            'tiny sparks around it'),
    'storm_3': ('Thunderam', 'a stout friendly ram with huge curly golden horns, a thick wool coat of indigo storm clouds, '
                             'yellow lightning patterns on its legs'),
    'stone_1': ('Pebbit', 'a tiny round smooth grey pebble creature with a little patch of moss and one small pink gem on top, '
                          'no arms, no legs'),
    'stone_2': ('Geodillo', 'a small chubby armadillo creature with a grey stone shell cracked open to show pink and purple '
                            'crystals, stubby feet'),
    'stone_3': ('Cragtortle', 'a big friendly round tortoise with a shell shaped like a small mountain of pink and teal crystals, '
                              'sturdy stone legs, grey and pink colors'),
    'ghost_1': ('Wispet', 'a tiny round pale mint ghost wisp creature with a wavy tail and a small blue flame on its forehead, '
                          'no arms, no legs'),
    'ghost_2': ('Bonekit', 'a small chubby kitten creature wearing a tiny skull helmet too big for it, pale teal fur, '
                           'a bone-shaped tail, stubby paws'),
    'ghost_3': ('Gravewarden', 'a stout friendly little chibi skeleton knight with a round skull, rusty teal armor, '
                               'a pink ribbon, holding a small lantern, big round eye sockets with glowing kind eyes'),
    'shroom_1': ('Sporeling', 'a tiny round cream colored spore creature with a tiny red spotted mushroom cap on its head, '
                              'no arms, no legs'),
    'shroom_2': ('Capling', 'a small chubby walking mushroom creature with a wide orange cap with yellow spots, '
                            'stubby arms, little feet, holding a small leaf umbrella'),
    'shroom_3': ('Fungolem', 'a big friendly round golem of bark and roots with many red and orange mushrooms growing from '
                             'its shoulders, glowing yellow spots, thick arms'),
    'sweet_1': ('Gumdrop', 'a tiny round glossy pink gumdrop candy creature dusted with sugar, no arms, no legs'),
    'sweet_2': ('Tartling', 'a small chubby strawberry tart creature with cream hair, a strawberry on top, stubby arms and feet'),
    'sweet_3': ('Cakelem', 'a big friendly round golem made of a layered pink and cream cake, cookie hands, '
                           'candy buttons, a cherry crown, a chocolate drip on its head'),
    'book_1': ('Pagelet', 'a tiny round creature made from one curled sheet of old cream paper with a small ink rune on it, '
                          'no arms, no legs'),
    'book_2': ('Tomebat', 'a small chubby flying open book creature, teal leather cover spread as bat wings, '
                          'a red ribbon bookmark tongue, big eyes on the cover'),
    'book_3': ('Grimoox', 'a stout friendly ox beast whose body is a huge teal leather book with brass corners, '
                          'pages for a mane, small horns, floating golden letters around it'),
    'clock_1': ('Cogling', 'a tiny round brass gear creature with a small wind-up key on its back, no arms, no legs'),
    'clock_2': ('Tickbot', 'a small chubby round copper pocket watch robot with a clock face on its belly, '
                           'stubby legs, tiny spring arms'),
    'clock_3': ('Chronoknight', 'a stout friendly clockwork chibi knight of brass and blue enamel, a clock on its chest, '
                                'visible gears in its shoulders, a small round shield, a big wind-up key on its back'),
    'sand_1': ('Dustmote', 'a tiny round sandy gold puff creature with a small swirl of sand on its head, no arms, no legs'),
    'sand_2': ('Scarabee', 'a small chubby beetle creature with a shiny turquoise and gold shell, stubby legs, tiny horn'),
    'sand_3': ('Sphinxie', 'a stout friendly round lion cub sphinx with a turquoise and gold striped headdress, small feathered '
                           'wings, sandy fur, chunky paws'),
    'honey_1': ('Honeybit', 'a tiny round amber honey drop creature with a small white daisy on its head, no arms, no legs'),
    'honey_2': ('Buzzlet', 'a small chubby fluffy bumblebee creature with yellow and brown stripes, tiny clear wings, '
                           'a small honey dipper in its hands'),
    'honey_3': ('Hivebear', 'a big friendly round brown bear cub wearing a honeycomb hive as armor and a beekeeper hat, '
                            'golden honey dripping from its paws, little bees around it'),
    'night_1': ('Inkit', 'a tiny round deep navy ink blob creature with big white eyes, a small crescent moon floating over it, '
                         'no arms, no legs'),
    'night_2': ('Murkitten', 'a small chubby navy blue kitten creature with a smoky tail, a magenta star mark on its forehead, '
                             'stubby paws'),
    'night_3': ('Nocturr', 'a stout friendly round panther beast with midnight blue fur, magenta star spots, '
                           'a long smoky tail, a silver crescent collar'),
    'sky_1': ('Puffeather', 'a tiny round white fluffball creature with one long sky blue feather on its head, no arms, no legs'),
    'sky_2': ('Gustling', 'a small chubby round sky blue bird chick with big wings too large for it, a little aviator cap, stubby feet'),
    'sky_3': ('Galegriff', 'a stout friendly baby griffin with a sky blue and white feathered head and wings, '
                           'a soft cream lion body, chunky paws, a fluffy tail, a cloud-shaped collar'),
}

# The version chosen per monster (filled in after looking at the sheets); --teams reads it.
PICKS = {
    'ember_1': 11, 'ember_2': 11, 'ember_3': 22, 'moss_1': 22, 'moss_2': 33, 'moss_3': 22,
    'tide_1': 33, 'tide_2': 22, 'tide_3': 22, 'star_1': 11, 'star_2': 22, 'star_3': 33,
    'frost_1': 11, 'frost_2': 22, 'frost_3': 11, 'storm_1': 11, 'storm_2': 33, 'storm_3': 33,
    'stone_1': 33, 'stone_2': 11, 'stone_3': 33, 'ghost_1': 22, 'ghost_2': 22, 'ghost_3': 33,
    'shroom_1': 33, 'shroom_2': 11, 'shroom_3': 22, 'sweet_1': 33, 'sweet_2': 22, 'sweet_3': 33,
    'book_1': 11, 'book_2': 33, 'book_3': 33, 'clock_1': 22, 'clock_2': 33, 'clock_3': 22,
    'sand_1': 11, 'sand_2': 11, 'sand_3': 33, 'honey_1': 11, 'honey_2': 33, 'honey_3': 22,
    'night_1': 33, 'night_2': 33, 'night_3': 33, 'sky_1': 22, 'sky_2': 22, 'sky_3': 33,
}


def workflow(prompt, seed, prefix):
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': prompt + ', ' + STYLE, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': NEGATIVE, 'clip': ['1', 1]}},
        '4': {'class_type': 'EmptyLatentImage', 'inputs': {'width': SIZE, 'height': SIZE, 'batch_size': 1}},
        '5': {'class_type': 'KSampler', 'inputs': {'model': ['1', 0], 'positive': ['2', 0], 'negative': ['3', 0],
                                                   'latent_image': ['4', 0], 'seed': seed, 'steps': 30, 'cfg': 6.0,
                                                   'sampler_name': 'dpmpp_2m', 'scheduler': 'karras', 'denoise': 1.0}},
        '6': {'class_type': 'VAEDecode', 'inputs': {'samples': ['5', 0], 'vae': ['1', 2]}},
        '7': {'class_type': 'SaveImage', 'inputs': {'images': ['6', 0], 'filename_prefix': prefix}},
    }


def make_sheets(seeds):
    cell, label, per_sheet = 256, 26, 4
    ids = list(MONSTERS)
    cols = 3 * len(seeds)   # the three stages of a team side by side, once per seed
    font = ImageFont.truetype('arial.ttf', 17)
    teams = [ids[i:i + 3] for i in range(0, len(ids), 3)]
    for n in range(0, len(teams), per_sheet):
        page = teams[n:n + per_sheet]
        sheet = Image.new('RGB', (cols * cell, len(page) * (cell + label)), 'white')
        draw = ImageDraw.Draw(sheet)
        for row, team in enumerate(page):
            for stage, mid in enumerate(team):
                for s, seed in enumerate(seeds):
                    path = WORK / f'{mid}_{seed}.png'
                    if not path.exists():
                        continue
                    x, y = (s * 3 + stage) * cell, row * (cell + label)
                    sheet.paste(Image.open(path).convert('RGB').resize((cell, cell), Image.LANCZOS), (x, y + label))
                    draw.text((x + 6, y + 3), f'{MONSTERS[mid][0]} {seed}', fill='black', font=font)
        out = WORK / f'sheet_{n // per_sheet + 1}.png'
        sheet.save(out)
        print(out)


def cut_workflow(name, prefix):
    return {
        '1': {'class_type': 'LoadImage', 'inputs': {'image': name}},
        '2': {'class_type': 'InspyrenetRembg', 'inputs': {'image': ['1', 0], 'torchscript_jit': 'default'}},
        '3': {'class_type': 'SaveImage', 'inputs': {'images': ['2', 0], 'filename_prefix': prefix}},
    }


DARK_STYLE = ('black and white ink illustration, monochrome, high contrast, deep black shadows, bright white highlights, '
              'dark gothic fairy tale, engraving hatching, bold black outline, plain white background')
DARK_NEGATIVE = 'color, colorful, saturated, photo, realistic, 3d render, blurry, text, watermark, frame, background scenery'


def dark_workflow(name, prompt, denoise, prefix):
    """Repaints a cut monster in ink; ControlNet (its own edges) holds the shape, so its silhouette still fits."""
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': CHECKPOINT}},
        '2': {'class_type': 'CLIPTextEncode', 'inputs': {'text': prompt + ', ' + DARK_STYLE, 'clip': ['1', 1]}},
        '3': {'class_type': 'CLIPTextEncode', 'inputs': {'text': DARK_NEGATIVE, 'clip': ['1', 1]}},
        '4': {'class_type': 'LoadImage', 'inputs': {'image': name}},
        '5': {'class_type': 'Canny', 'inputs': {'image': ['4', 0], 'low_threshold': 0.2, 'high_threshold': 0.6}},
        '6': {'class_type': 'ControlNetLoader', 'inputs': {'control_net_name': CONTROLNET}},
        '7': {'class_type': 'SetUnionControlNetType', 'inputs': {'control_net': ['6', 0], 'type': 'canny/lineart/anime_lineart/mlsd'}},
        '8': {'class_type': 'ControlNetApplyAdvanced',
              'inputs': {'positive': ['2', 0], 'negative': ['3', 0], 'control_net': ['7', 0], 'image': ['5', 0],
                         'strength': 0.8, 'start_percent': 0.0, 'end_percent': 1.0, 'vae': ['1', 2]}},
        '9': {'class_type': 'VAEEncode', 'inputs': {'pixels': ['4', 0], 'vae': ['1', 2]}},
        '10': {'class_type': 'KSampler', 'inputs': {'model': ['1', 0], 'positive': ['8', 0], 'negative': ['8', 1],
                                                    'latent_image': ['9', 0], 'seed': 7, 'steps': 30, 'cfg': 6.0,
                                                    'sampler_name': 'dpmpp_2m', 'scheduler': 'karras', 'denoise': denoise}},
        '11': {'class_type': 'VAEDecode', 'inputs': {'samples': ['10', 0], 'vae': ['1', 2]}},
        '12': {'class_type': 'SaveImage', 'inputs': {'images': ['11', 0], 'filename_prefix': prefix}},
    }


def make_dark(only, denoise):
    """dark/<id>.png: pure greys (no colour survives), with the cut picture's silhouette."""
    out_dir = WORK / 'dark'
    out_dir.mkdir(exist_ok=True)
    for mid, (name, prompt) in MONSTERS.items():
        if (only and mid not in only) or (not only and (out_dir / f'{mid}.png').exists()):
            continue
        t0 = time.time()
        cut = Image.open(WORK / 'cut' / f'{mid}.png').convert('RGBA')
        white = Image.new('RGBA', cut.size, 'white')
        white.alpha_composite(cut)
        painted = run(dark_workflow(upload(white.convert('L').convert('RGB'), f'dark_{mid}.png'), prompt, denoise, 'monster_dark_' + mid))[0]
        grey = ImageOps.autocontrast(painted.convert('L').resize(cut.size, Image.LANCZOS), cutoff=1)
        grey = grey.point(lambda v: int(255 * (v / 255) ** 1.25))   # a little darker in the mid-tones
        Image.merge('LA', (grey, cut.getchannel('A'))).convert('RGBA').save(out_dir / f'{mid}.png')
        print(f'{mid} {name}: {time.time() - t0:.0f} s', flush=True)


def make_watch():
    """watch/<id>.png: exactly WATCH x WATCH from dark/<id>.png, for a small screen: 4 greys, hard edges, a light outline.
    watch_sheet.png shows them all at real size and enlarged (NEAREST), on black, one team per row."""
    out_dir = WORK / 'watch'
    out_dir.mkdir(exist_ok=True)
    levels = (0, 85, 170, 255)
    for mid in MONSTERS:
        img = Image.open(WORK / 'dark' / f'{mid}.png').convert('RGBA')
        img = img.crop(img.getchannel('A').point(lambda a: 255 if a > 40 else 0).getbbox())
        fit = WATCH - 2   # a pixel each side for the outline
        scale = fit / max(img.size)
        size = (max(1, round(img.width * scale)), max(1, round(img.height * scale)))
        small = img.resize(size, Image.LANCZOS)
        grey = ImageOps.autocontrast(small.convert('L'), cutoff=2).filter(ImageFilter.UnsharpMask(1, 120, 0))
        grey = grey.point(lambda v: levels[min(3, v * 4 // 256)])
        solid = small.getchannel('A').point(lambda a: 255 if a >= 128 else 0)
        canvas = Image.new('L', (WATCH, WATCH), 0)
        mask = Image.new('L', (WATCH, WATCH), 0)
        at = ((WATCH - size[0]) // 2, WATCH - 1 - size[1])   # centred, the feet on the last row but one
        canvas.paste(grey, at)
        mask.paste(solid, at)
        outline = mask.filter(ImageFilter.MaxFilter(3))
        canvas = Image.composite(canvas, Image.new('L', (WATCH, WATCH), 255), mask)   # outside the body: the outline's white
        Image.merge('LA', (canvas, outline)).convert('RGBA').save(out_dir / f'{mid}.png')
    ids = list(MONSTERS)
    zoom, gap = 6, 8
    cell = WATCH * zoom + gap
    cols = 4 * 3   # four teams per row, each three monsters
    rows = (len(ids) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * cell + gap, rows * (cell + WATCH + gap)), 'black')
    for i, mid in enumerate(ids):
        img = Image.open(out_dir / f'{mid}.png')
        big = img.resize((WATCH * zoom, WATCH * zoom), Image.NEAREST)
        x, y = gap + (i % cols) * cell, gap + (i // cols) * (cell + WATCH + gap)
        sheet.paste(big, (x, y), big)
        sheet.paste(img, (x, y + WATCH * zoom + gap // 2), img)   # the real size, under it
    sheet.save(WORK / 'watch_sheet.png')
    print(WORK / 'watch_sheet.png')


def make_teams(dark=False):
    """Every team as a battle line-up on a dark floor: the creature in the middle, the two smaller ones in front of it."""
    cut_dir = WORK / ('dark' if dark else 'cut')
    cut_dir.mkdir(exist_ok=True)
    ids = list(MONSTERS)
    cols, cell_w, cell_h = 4, 560, 440
    teams = [ids[i:i + 3] for i in range(0, len(ids), 3)]
    rows = (len(teams) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * cell_w, rows * cell_h), (46, 46, 46) if dark else (44, 42, 58))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype('arial.ttf', 22)
    # stage: (height on the cell, centre x, feet y)
    place = {2: (340, 280, 400), 1: (210, 440, 420), 0: (130, 110, 420)}
    for t, team in enumerate(teams):
        ox, oy = (t % cols) * cell_w, (t // cols) * cell_h
        for stage in (2, 1, 0):   # the biggest first, the small ones stand in front
            mid = team[stage]
            out = cut_dir / f'{mid}.png'
            if not out.exists():
                src = Image.open(WORK / f'{mid}_{PICKS[mid]}.png').convert('RGB')
                run(cut_workflow(upload(src, f'concept_{mid}.png'), 'monster_cut_' + mid))[0].save(out)
            img = Image.open(out).convert('RGBA')
            img = img.crop(img.getchannel('A').point(lambda a: 255 if a > 16 else 0).getbbox())
            h, cx, feet = place[stage]
            w = img.width * h // img.height
            img = img.resize((w, h), Image.LANCZOS)
            sheet.paste(img, (ox + cx - w // 2, oy + feet - h), img)
        draw.text((ox + 12, oy + 8), ' / '.join(MONSTERS[m][0] for m in team), fill='white', font=font)
    out = WORK / ('teams_dark.png' if dark else 'teams.png')
    sheet.save(out)
    print(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seeds', default='11,22,33')
    ap.add_argument('--only', default='', help='comma-separated monster ids')
    ap.add_argument('--sheet-only', action='store_true')
    ap.add_argument('--teams', action='store_true', help='cut the PICKS and lay out the teams')
    ap.add_argument('--dark', action='store_true', help='the black and white versions of the cut PICKS')
    ap.add_argument('--denoise', type=float, default=0.6)
    ap.add_argument('--watch', action='store_true', help='the 40 x 40 versions of the black and white ones')
    a = ap.parse_args()
    seeds = [int(s) for s in a.seeds.split(',')]
    WORK.mkdir(parents=True, exist_ok=True)
    if a.watch:
        make_watch()
        return
    if a.dark:
        make_dark(set(a.only.split(',')) if a.only else set(), a.denoise)
        if not a.only:
            make_teams(dark=True)
        return
    if a.teams:
        make_teams()
        return
    if not a.sheet_only:
        only = set(a.only.split(',')) if a.only else set(MONSTERS)
        for mid, (name, prompt) in MONSTERS.items():
            if mid not in only:
                continue
            for seed in seeds:
                if (WORK / f'{mid}_{seed}.png').exists() and not a.only:
                    continue
                t0 = time.time()
                run(workflow(prompt, seed, 'monster_' + mid))[0].convert('RGB').save(WORK / f'{mid}_{seed}.png')
                print(f'{mid} {name} seed {seed}: {time.time() - t0:.0f} s', flush=True)
    make_sheets(seeds)


if __name__ == '__main__':
    main()
