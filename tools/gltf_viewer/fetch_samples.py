#!/usr/bin/env python3
"""fetch_samples.py -- download Khronos glTF sample models to try in gltf_viewer (docs/18_GLTF.md).

They go to Build/gltf_samples/ (not committed: each model has its own licence, see its README at
https://github.com/KhronosGroup/glTF-Sample-Assets). Already downloaded files are skipped.

  python tools/gltf_viewer/fetch_samples.py           the default set below
  python tools/gltf_viewer/fetch_samples.py Fox Duck  only these (any model name of the repo)
"""
import os
import sys
import urllib.request

BASE = 'https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/{0}/glTF-Binary/{0}.glb'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Build', 'gltf_samples')

# name: what it checks
DEFAULT = {
    'Fox': 'skinning, 3 animations (Survey, Walk, Run)',
    'CesiumMan': 'skinning, textured',
    'RiggedFigure': 'skinning, simple',
    'BrainStem': 'skinning, many joints and meshes',
    'AnimatedMorphCube': 'morph targets, animated weights',
    'MorphStressTest': 'morph targets, many, with a texture',
    'SimpleInstancing': 'EXT_mesh_gpu_instancing',
    'DamagedHelmet': 'PBR: every texture (base, metal/rough, normal, occlusion, emissive)',
    'MetalRoughSpheres': 'PBR: metallic / roughness grid',
    'AlphaBlendModeTest': 'alpha opaque / mask / blend',
    'BoxAnimated': 'node animation (translation, rotation)',
    'InterpolationTest': 'STEP / LINEAR / CUBICSPLINE',
}


def main():
    names = sys.argv[1:] or list(DEFAULT)
    os.makedirs(OUT, exist_ok=True)
    failed = 0
    for name in names:
        path = os.path.join(OUT, name + '.glb')
        if os.path.exists(path):
            print('have  %-20s %s' % (name, DEFAULT.get(name, '')))
            continue
        url = BASE.format(name)
        try:
            with urllib.request.urlopen(url, timeout=60) as r, open(path + '.part', 'wb') as f:
                f.write(r.read())
            os.replace(path + '.part', path)
            print('got   %-20s %s (%d KB)' % (name, DEFAULT.get(name, ''), os.path.getsize(path) // 1024))
        except Exception as e:   # noqa: BLE001 -- report and go on with the rest
            failed += 1
            print('FAIL  %-20s %s: %s' % (name, url, e))
            if os.path.exists(path + '.part'):
                os.remove(path + '.part')
    print('\n%s\nOpen one: gltf_viewer %s' % (os.path.normpath(OUT), os.path.normpath(os.path.join(OUT, 'Fox.glb'))))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
