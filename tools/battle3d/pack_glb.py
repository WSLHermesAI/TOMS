"""pack_glb.py -- one .gltf with its .bin files and textures made into one .glb (glTF's binary form: the JSON, the
buffer and the images in one file). The game reads both forms (docs/engine/08_GLTF.md); one file is simpler to keep
and ship. Used by make_moonlit_ruins.py; also on its own:

    python tools/battle3d/pack_glb.py <model.gltf> [<out.glb>]      (default: beside it, same name)

Every buffer becomes one; every image file (or data: URI) is put in it as a bufferView with its mimeType. Nothing is
re-encoded: the bytes are the same, so the model looks the same.
"""
import base64
import json
import struct
import sys
from pathlib import Path

MIME = {".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png", ".webp": "image/webp", ".ktx2": "image/ktx2"}


def _pad(b: bytes, fill: bytes) -> bytes:
    return b + fill * ((4 - len(b) % 4) % 4)


def _uri_bytes(base: Path, uri: str) -> bytes:
    if uri.startswith("data:"):
        return base64.b64decode(uri.split(",", 1)[1])
    from urllib.parse import unquote
    return (base / unquote(uri)).read_bytes()


def pack(gltf_path: Path, out_path: Path = None) -> Path:
    gltf_path = Path(gltf_path)
    out_path = Path(out_path) if out_path else gltf_path.with_suffix(".glb")
    g = json.loads(gltf_path.read_text(encoding="utf-8"))
    base = gltf_path.parent
    blob = bytearray()
    # the buffers, one after another: each bufferView moves by where its buffer starts now
    starts = []
    for b in g.get("buffers", []):
        while len(blob) % 4:
            blob += b"\x00"
        starts.append(len(blob))
        blob += _uri_bytes(base, b["uri"]) if "uri" in b else b""
    for v in g.get("bufferViews", []):
        v["byteOffset"] = v.get("byteOffset", 0) + starts[v.get("buffer", 0)]
        v["buffer"] = 0
    # the images: their bytes into the buffer, a bufferView each
    for im in g.get("images", []):
        if "uri" not in im:
            continue
        data = _uri_bytes(base, im["uri"])
        while len(blob) % 4:
            blob += b"\x00"
        g.setdefault("bufferViews", []).append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)})
        blob += data
        im["bufferView"] = len(g["bufferViews"]) - 1
        im["mimeType"] = im.get("mimeType") or MIME.get(Path(im["uri"].split("?")[0]).suffix.lower(), "image/png")
        del im["uri"]
    g["buffers"] = [{"byteLength": len(blob)}]
    js = _pad(json.dumps(g, separators=(",", ":")).encode("utf-8"), b" ")
    bin_ = _pad(bytes(blob), b"\x00")
    total = 12 + 8 + len(js) + 8 + len(bin_)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)
        f.write(struct.pack("<I4s", len(bin_), b"BIN\x00") + bin_)
    return out_path


if __name__ == "__main__":
    out = pack(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv) > 2 else None)
    print(f"{out} ({out.stat().st_size / 1e6:.2f} MB)")
