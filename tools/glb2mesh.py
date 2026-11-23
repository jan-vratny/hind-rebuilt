#!/usr/bin/env python3
"""A model in glTF binary (GLB) to the remake's flat-shaded triangle file (render/model.hpp).

    tools/glb2mesh.py m60 IN.glb data/models/m60.mesh
    tools/glb2mesh.py m72 IN.glb data/models/m72.mesh
    tools/glb2mesh.py m72rocket IN.glb data/models/m72rocket.mesh

Each model has its own recipe below: which parts to leave out, how coarse to make which (vertex
clustering on a grid, so that it looks like the rest of the game), its colours by material (or from its texture), and
where its origin and axes go (metres, -z ahead, +y up).

The file: b"HMSH", uint32 version (1), uint32 triangle count, then per triangle nine float32
(three corners x, y, z) and four bytes of colour (r, g, b, unused); little-endian.
"""
import json
import struct
import sys

import numpy as np


def load(path, local_axes=False):
    """The GLB's triangles: per mesh primitive its node name, material name and corners (in the
    world, or with `local_axes` in its mesh's own axes), its texture coordinates and its base
    colour texture (a PIL image) where it has them."""
    f = open(path, "rb").read()
    clen, _ = struct.unpack("<II", f[12:20])
    j = json.loads(f[20:20 + clen])
    off = 20 + clen
    blen, _ = struct.unpack("<II", f[off:off + 8])
    buf = f[off + 8:off + 8 + blen]

    def accessor(i):
        a = j["accessors"][i]
        bv = j["bufferViews"][a["bufferView"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]]
        start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        size = np.dtype(dt).itemsize * n
        stride = bv.get("byteStride", 0)
        if stride and stride != size:
            raw = np.frombuffer(buf, np.uint8, stride * a["count"], start).reshape(a["count"], stride)[:, :size]
            return np.frombuffer(raw.tobytes(), dt).reshape(a["count"], n)
        return np.frombuffer(buf, dt, a["count"] * n, start).reshape(a["count"], n)

    def local(n):
        if "matrix" in n:
            return np.array(n["matrix"], float).reshape(4, 4).T
        t, r, s = np.eye(4), np.eye(4), np.eye(4)
        if "translation" in n:
            t[:3, 3] = n["translation"]
        if "rotation" in n:
            x, y, z, w = n["rotation"]
            r[:3, :3] = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        if "scale" in n:
            s[:3, :3] = np.diag(n["scale"])
        return t @ r @ s

    images = {}

    def texture(material):
        t = material.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if t is None:
            return None
        src = j["textures"][t["index"]]["source"]
        if src not in images:
            import io
            from PIL import Image
            bv = j["bufferViews"][j["images"][src]["bufferView"]]
            start = bv.get("byteOffset", 0)
            images[src] = Image.open(io.BytesIO(buf[start:start + bv["byteLength"]])).convert("RGB")
        return images[src]

    parts = []

    def walk(i, parent):
        n = j["nodes"][i]
        m = parent @ local(n)
        for p in j["meshes"][n["mesh"]]["primitives"] if "mesh" in n else []:
            pos = accessor(p["attributes"]["POSITION"]).astype(float)
            if not local_axes:
                pos = (m @ np.c_[pos, np.ones(len(pos))].T).T[:, :3]
            idx = accessor(p["indices"]).reshape(-1).astype(int) if "indices" in p else np.arange(len(pos))
            mat = j["materials"][p["material"]]
            uv = accessor(p["attributes"]["TEXCOORD_0"]).astype(float) if "TEXCOORD_0" in p["attributes"] else None
            parts.append({"name": n["name"], "material": mat["name"], "pos": pos, "tris": idx.reshape(-1, 3),
                          "uv": uv, "texture": texture(mat), "double": mat.get("doubleSided", False)})
        for c in n.get("children", []):
            walk(c, m)

    for root in j["scenes"][j.get("scene", 0)]["nodes"]:
        walk(root, np.eye(4))
    return j, parts


def cluster(pos, tris, grid):
    """Simpler: the corners within one cell of the grid made one (their mean), the triangles that
    fall flat left out, and the twice-made ones once. Also which of `tris` are left."""
    if grid <= 0:
        return pos, tris, np.arange(len(tris))
    keys, inv = np.unique(np.round(pos / grid).astype(np.int64), axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    merged = np.zeros((len(keys), 3))
    count = np.zeros(len(keys))
    np.add.at(merged, inv, pos)
    np.add.at(count, inv, 1)
    merged /= count[:, None]
    t = inv[tris]
    kept = np.nonzero((t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2]))[0]
    _, first = np.unique(np.sort(t[kept], axis=1), axis=0, return_index=True)
    kept = kept[np.sort(first)]
    return merged, t[kept], kept


def painted(part, tri, lighter):
    """A triangle's colour from its texture: the median of the texels at its corners pulled a
    little in, its midpoints and its centre (so that a seam's edge doesn't decide it), made
    `lighter` (a factor) as the game's darker colours are."""
    img = part["texture"]
    uv = part["uv"][tri]
    c = uv.mean(0)
    pts = [c] + [c + 0.8 * (q - c) for q in uv] + [(uv[a] + uv[b]) / 2 for a, b in ((0, 1), (1, 2), (2, 0))]
    w, h = img.size
    px = [img.getpixel((int(u % 1.0 * w) % w, int(v % 1.0 * h) % h)) for u, v in pts]
    rgb = np.median(np.array(px, float), axis=0) * lighter
    return tuple(int(min(255, round(x))) for x in rgb)


# M60 (Low Poly) by Jawa (https://sketchfab.com/HELLBRAD), CC BY 4.0:
# https://sketchfab.com/3d-models/m60-low-poly-42b4f0588ef24e62b7b2fe9b9fc413b5
# In centimetres, the muzzle to -z, up +y. Of its two ammunition boxes (each with its belt) the
# one under the gun is kept, the one sticking out to its left side is left out. The belt's
# cartridges coarser than the rest. Its near-black colours lighter, as the game's.
M60 = {
    "leave": ("Cylinder.063", "Cylinder.064"),
    "grid": lambda name: 1.2 if "Cylinder.069" in name else 0.6,
    "colours": {
        "Dark_1": (82, 84, 79), "Dark_2": (62, 64, 60), "Dark_3": (94, 96, 90),
        "Grey": (112, 114, 108), "Grey_2": (126, 128, 122),
        "Dark_Green": (62, 74, 44), "Brass": (184, 140, 52),
    },
    # The top of the pistol grip at the origin; to metres.
    "origin": (0.0, -1.5, 8.0),
    "scale": 0.01,
}

# M72 LAW Rocket Launcher by Rectus (https://sketchfab.com/rectus), CC BY-NC 4.0:
# https://sketchfab.com/3d-models/m72-law-rocket-launcher-b4367ffcf3084d4b9eff8c497bf5c7fd
# Extended, ready to fire; in inches in its mesh's own axes: the muzzle to -y, the sights up +z.
# The loose rocket beside it, the front sight's clear plastic and the rear cover's box on top of
# the inner tube's end (by the eye in his view, filling a corner of it) left out. Painted by its
# texture.
M72 = {
    "local": True,
    "leave": ("rocket", "law_view.001_1"),
    "drop": lambda c: c[1] > 7.5 and c[2] > 1.5,
    "grid": lambda name: 0.3,
    "lighter": 1.15,
    # The tube's axis under the trigger at the origin; to metres, the muzzle to -z, up +y.
    "origin": (0.0, -4.0, 0.0),
    "scale": 0.0254,
    "axes": ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
}
# Its rocket, apart in the same model: the body and the six fins sprung open, in one set of axes
# (the nose to -y). Lighter than its texture, to be seen in the air.
M72_ROCKET = {
    "local": True,
    "leave": ("law_view",),
    "grid": lambda name: 0.0,
    "lighter": 1.5,
    # The nose at the origin (where the game has the rocket); to metres, the nose to -z, up +y.
    "origin": (0.0, -20.05, 0.0),
    "scale": 0.0254,
    "axes": ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
}
RECIPES = {"m60": M60, "m72": M72, "m72rocket": M72_ROCKET}


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in RECIPES:
        sys.exit(__doc__)
    recipe = RECIPES[sys.argv[1]]
    _, parts = load(sys.argv[2], recipe.get("local", False))
    axes = np.array(recipe.get("axes", np.eye(3)), float)
    out = bytearray()
    count = 0
    for p in parts:
        if any(s in p["name"] for s in recipe["leave"]):
            continue
        tris = p["tris"]
        if "drop" in recipe:  # triangles left out by where their centre is (the model's axes)
            tris = tris[[not recipe["drop"](c) for c in p["pos"][tris].mean(1)]]
            p = dict(p, tris=tris)
        pos, tris, kept = cluster(p["pos"], tris, recipe["grid"](p["name"]))
        pos = ((pos - np.array(recipe["origin"])) * recipe["scale"]) @ axes.T
        colours = recipe.get("colours", {})
        for t, k in zip(tris, kept):
            if p["material"] in colours:
                r, g, b = colours[p["material"]]
            else:
                r, g, b = painted(p, p["tris"][k], recipe.get("lighter", 1.0))
            out += struct.pack("<9f", *pos[t].reshape(-1)) + bytes((r, g, b, 0))
            count += 1
            if p["double"]:  # (seen from both sides: once more the other way round)
                out += struct.pack("<9f", *pos[t[::-1]].reshape(-1)) + bytes((r, g, b, 0))
                count += 1
    with open(sys.argv[3], "wb") as f:
        f.write(b"HMSH" + struct.pack("<II", 1, count) + out)
    print(f"{sys.argv[3]}: {count} triangles")


if __name__ == "__main__":
    main()
