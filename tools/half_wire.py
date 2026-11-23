#!/usr/bin/env python3
"""Our Mi-24 (hind --model-sheet mi24.obj) drawn into SVG: the nose flat shaded as in the game,
from a cut plane back a transparent wireframe of the model's own triangles (the logo, the icon).
Painter's algorithm, one flat colour a triangle; needs numpy. A model with texture coordinates
(hind --model-sheet x.obj --sheet-model original, with its x-atlas.ppm) has its nose rasterised
instead, textured, into a picture in the SVG (needs Pillow too), and can wear the Ukrainian
missions' livery as the game paints it (livery=True)."""
import base64
import io

import numpy as np


def load_obj(path, with_groups=False, with_uv=False):
    v, c, vt, f, g = [], [], [], [], []
    group = ''
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == 'v':
            v.append([float(x) for x in p[1:4]])
            c.append([float(x) for x in p[4:7]])
        elif p[0] == 'vt':
            vt.append([float(x) for x in p[1:4]] + [0.0] * (3 - len(p[1:4])))
        elif p[0] == 'g':
            group = p[1]
        elif p[0] == 'f':
            f.append([int(x.split('/')[0]) - 1 for x in p[1:4]])
            g.append(group)
    v, c, f = np.array(v), np.array(c), np.array(f)
    out = [v[f], c[f].mean(axis=1)]  # triangles (n,3,3), colour per triangle
    if with_groups:
        out.append(np.array(g))
    if with_uv:
        # (n,3,3): the atlas u, v of each corner and 1 on a textured face; None without them
        out.append(np.array(vt)[f] if vt else None)
    return tuple(out)


def clip(tris, cols, z0, keep_front):
    """Triangles split at the plane z = z0; keep the front (z < z0) or the rear part. The
    triangles may carry more per corner after x, y, z (texture coordinates): interpolated too."""
    out, oc = [], []
    for t, c in zip(tris, cols):
        d = (t[:, 2] - z0) * (1 if keep_front else -1)  # <= 0: kept
        if (d <= 0).all():
            out.append(t), oc.append(c)
            continue
        if (d > 0).all():
            continue
        poly = []
        for i in range(3):
            a, b = t[i], t[(i + 1) % 3]
            da, db = d[i], d[(i + 1) % 3]
            if da <= 0:
                poly.append(a)
            if (da <= 0) != (db <= 0):
                poly.append(a + (b - a) * (da / (da - db)))
        for i in range(1, len(poly) - 1):
            out.append(np.array([poly[0], poly[i], poly[i + 1]])), oc.append(c)
    if not out:
        return np.zeros((0, 3, tris.shape[2] if tris.ndim == 3 else 3)), np.zeros((0, 3))
    return np.array(out), np.array(oc)


class Camera:
    def __init__(self, eye, target, fov_deg, width, height, scale=1.0):
        self.eye = np.array(eye, float)
        f = np.array(target, float) - self.eye
        f /= np.linalg.norm(f)
        r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r)
        u = np.cross(r, f)
        self.f, self.r, self.u = f, r, u
        self.k = 0.5 * height / np.tan(np.radians(fov_deg) / 2) * scale
        self.cx, self.cy = width / 2, height / 2

    def project(self, p):
        q = p - self.eye
        z = q @ self.f
        x = q @ self.r
        y = q @ self.u
        return np.stack([self.cx + self.k * x / z, self.cy - self.k * y / z], axis=-1), z


def shaded(tris, cols, cam, light, ambient=0.42, diffuse=0.7, tint=(1, 1, 1)):
    if len(tris) == 0:
        return ''
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    centre = tris.mean(axis=1)
    # face the camera
    flip = ((cam.eye - centre) * n).sum(axis=1) < 0
    n[flip] *= -1
    l = np.array(light, float); l /= np.linalg.norm(l)
    lit = ambient + diffuse * np.clip(n @ l, 0, 1)
    rgb = np.clip(cols * lit[:, None] * np.array(tint), 0, 1)
    pts, z = cam.project(tris)
    order = np.argsort(-z.mean(axis=1))
    out = []
    for i in order:
        col = '#%02x%02x%02x' % tuple(int(255 * x) for x in rgb[i])
        p = pts[i]
        out.append(f'<path d="M{p[0,0]:.1f} {p[0,1]:.1f}L{p[1,0]:.1f} {p[1,1]:.1f}L{p[2,0]:.1f} {p[2,1]:.1f}Z" '
                   f'fill="{col}" stroke="{col}" stroke-width="0.6" stroke-linejoin="round"/>')
    return ''.join(out)


BLUE, YELLOW, WHITE = np.array([0.10, 0.36, 0.72]), np.array([0.95, 0.80, 0.12]), np.array([0.86, 0.87, 0.84])


OLIVE, GREY, DARK = np.array([128, 132, 86]) / 255, np.array([188, 190, 172]) / 255, np.array([84, 92, 76]) / 255


def _noise(x, y, seed):
    """Smooth value noise (k), as render/livery.cpp's."""
    i, j = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = x - i, y - j
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

    def h(a, b):
        v = (a * 73856093) ^ (b * 19349663) ^ (seed * 83492791)
        v = v & 0xffffffff
        v ^= v >> 13
        v = (v * 0x5bd1e995) & 0xffffffff
        v ^= v >> 15
        return (v & 0xffff) / 65535.0
    a, b, c, d = h(i, j), h(i + 1, j), h(i, j + 1), h(i + 1, j + 1)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def czech_camouflage(p, m):
    """The Czech three-tone camouflage (olive, light grey, dark grey green; render/livery.cpp's
    colours and blotches) painted by the place on the airframe `p` (k,3) of a face with the
    normal `m`, so that it needs no texture: on the sides as seen from the side, on the top and
    the belly as seen from above."""
    if abs(m[0]) >= max(abs(m[1]), abs(m[2])):
        u, v = p[:, 2], p[:, 1]
    elif abs(m[1]) >= abs(m[2]):
        u, v = p[:, 2], p[:, 0]
    else:
        u, v = p[:, 0], p[:, 1]
    u, v = u / 1.6 + 50, v / 1.6 + 50  # metres to the big patches' cells

    def blotches(seed):
        return 0.75 * _noise(u, v, seed) + 0.25 * _noise(u * 3, v * 3, seed + 7)
    out = np.broadcast_to(OLIVE, (len(p), 3)).copy()
    dark = blotches(2) > 0.58
    out[dark] = DARK
    out[blotches(1) > 0.50] = GREY
    return out


def paint(base, p, m, textured_face):
    """The Ukrainian missions' markings over the colours `base` (k,3) at the places `p` (k,3) on
    the airframe with its face's normal `m`, as the game's scene shader (render/shaders.hpp)."""
    glass = (not textured_face) & (base[:, 2] > base[:, 0] + 0.06)
    ok = ~glass & (textured_face | (base @ np.array([0.30, 0.59, 0.11]) > 0.35))
    out = base.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    side = abs(m[0]) > 0.35
    band = np.where(z < -1.6, -0.62 + (-0.30 + 0.62) * np.clip((z + 6.3) / 4.7, 0, 1), -0.30)
    stripe = ok & side & (((z > -6.3) & (z < -1.6)) | ((z > 0.7) & (z < 3.0))) & (abs(y - band) < 0.24)
    out[stripe] = np.where((y > band)[stripe, None], BLUE, YELLOW)
    rings = ok & (z > 5.6) & (z < 7.15) & (np.mod(z - 5.6, 0.95) < 0.6)
    out[rings] = WHITE
    tips = ok & (abs(x) > 3.15) & (abs(m[0]) > 0.6)
    out[tips] = np.where((y > -0.57)[tips, None], BLUE, YELLOW)
    return out


def bilinear(tex, u, v, tiles):
    """The atlas `tex` (h,w,3) sampled at u, v (k) between its texels, the neighbours taken
    round within the tile the point is in."""
    th, tw = tex.shape[:2]
    ts = tw // tiles
    x, y = u * tw - 0.5, v * th - 0.5
    # the tile of the point (by its texel's centre), and the place within it
    cx = np.clip(np.floor(u * tw).astype(int), 0, tw - 1) // ts * ts
    cy = np.clip(np.floor(v * th).astype(int), 0, th - 1) // ts * ts
    x0, y0 = np.floor(x), np.floor(y)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    x0, y0 = x0.astype(int) - cx, y0.astype(int) - cy
    def at(dx, dy):
        return tex[cy + (y0 + dy) % ts, cx + (x0 + dx) % ts]
    return (at(0, 0) * (1 - fx) * (1 - fy) + at(1, 0) * fx * (1 - fy)
            + at(0, 1) * (1 - fx) * fy + at(1, 1) * fx * fy)


CAMO = (np.array([0.29, 0.35, 0.21]), np.array([0.55, 0.47, 0.31]))  # our model's green and sand


def textured(tris, uvs, cols, cam, light, atlas, size, ambient=0.42, diffuse=0.7, tint=(1, 1, 1), ss=2,
             model=None, two_sided=None, livery=False, smooth=False, tiles=8):
    """The triangles rasterised with a depth buffer, the textured ones (uvs[..., 2] == 1) taking
    their colour from the atlas (Pillow image, nearest texel), one light a face as in the game;
    the faces turned away left out (the original's faces are one-sided, two-sided ones come
    twice) except those `two_sided` (n,) marks (our rotors). With `livery` the Czech camouflage
    (the atlas's last tile) for the original's and Ukraine's colours painted by the places on
    the airframe, `model` (n,3,3), as before the model was turned. An <image> of the frame's
    size with the picture, antialiased by `ss`. `smooth`: the textures filtered (bilinear,
    wrapping round within their tile of the atlas's `tiles` × `tiles`) instead of the game's
    sharp texels."""
    from PIL import Image
    W, H = size
    tex = np.asarray(atlas.convert('RGB'), dtype=np.float32) / 255.0 if atlas is not None else np.zeros((1, 1, 3))
    th, tw = tex.shape[:2]
    rgb = np.zeros((H * ss, W * ss, 3), np.float32)
    zbuf = np.full((H * ss, W * ss), np.inf, np.float32)
    if len(tris):
        n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        facing = ((cam.eye - tris.mean(axis=1)) * n).sum(axis=1) > 0
        if two_sided is not None:
            facing |= two_sided
        if livery:
            mn = np.cross(model[:, 1] - model[:, 0], model[:, 2] - model[:, 0])
            mn /= np.linalg.norm(mn, axis=1, keepdims=True) + 1e-12
        l = np.array(light, float); l /= np.linalg.norm(l)
        lit = (ambient + diffuse * np.clip(n @ l, 0, 1))[:, None] * np.array(tint)
        pts, z = cam.project(tris)
        pts = pts * ss
        for i in np.nonzero(facing & (z.min(axis=1) > 0.1))[0]:
            p, zi = pts[i], z[i]
            # grown by 3/4 of a pixel about its centre: no background showing through the seams
            mid = p.mean(axis=0)
            far = np.linalg.norm(p - mid, axis=1, keepdims=True) + 1e-9
            p = p + (p - mid) / far * 0.75 * ss
            x0, y0 = np.floor(p.min(axis=0)).astype(int)
            x1, y1 = np.ceil(p.max(axis=0)).astype(int)
            x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, W * ss - 1), min(y1, H * ss - 1)
            if x1 < x0 or y1 < y0:
                continue
            ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1] + 0.5
            (ax, ay), (bx, by), (cx, cy) = p
            area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
            if abs(area) < 1e-9:
                continue
            w0 = ((bx - xs) * (cy - ys) - (by - ys) * (cx - xs)) / area
            w1 = ((cx - xs) * (ay - ys) - (cy - ys) * (ax - xs)) / area
            w2 = 1.0 - w0 - w1
            inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
            if not inside.any():
                continue
            # perspective correct: weights over the depth
            q0, q1, q2 = w0 / zi[0], w1 / zi[1], w2 / zi[2]
            qs = q0 + q1 + q2
            depth = 1.0 / qs
            sub = zbuf[y0:y1 + 1, x0:x1 + 1]
            draw = inside & (depth < sub)
            if not draw.any():
                continue
            tex_face = uvs is not None and uvs[i, 0, 2] > 0.5
            if tex_face:
                u = (q0 * uvs[i, 0, 0] + q1 * uvs[i, 1, 0] + q2 * uvs[i, 2, 0]) / qs
                v = (q0 * uvs[i, 0, 1] + q1 * uvs[i, 1, 1] + q2 * uvs[i, 2, 1]) / qs
                if livery and int(uvs[i, :, 0].mean() * 8) == 2 and int(uvs[i, :, 1].mean() * 8) == 0:
                    u, v = u + 5 / 8, v + 7 / 8  # the Czech camouflage for the original's
                if smooth:
                    base = bilinear(tex, u[draw], v[draw], tiles)
                else:
                    tx = np.clip((u * tw).astype(int), 0, tw - 1)
                    ty = np.clip((v * th).astype(int), 0, th - 1)
                    base = tex[ty, tx][draw]
            else:
                base = np.broadcast_to(cols[i], (int(draw.sum()), 3)).astype(np.float32)
            if livery:
                pm = ((q0[..., None] * model[i, 0] + q1[..., None] * model[i, 1] + q2[..., None] * model[i, 2])
                      / qs[..., None])[draw]
                # Our own model's camouflage (green or sand faces): the Czech one in its place.
                camo = not tex_face and any(np.allclose(cols[i], c, atol=0.02) for c in CAMO)
                if camo:
                    base = czech_camouflage(pm, mn[i])
                base = paint(base, pm, mn[i], tex_face or camo)
            sub[draw] = depth[draw]
            rgb[y0:y1 + 1, x0:x1 + 1][draw] = np.clip(base * lit[i], 0, 1)
    alpha = np.isfinite(zbuf).astype(np.float32)
    img = Image.fromarray((np.dstack([rgb, alpha[..., None]]) * 255).astype(np.uint8), 'RGBA')
    img = img.resize((W, H), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, 'PNG')
    data = base64.b64encode(buf.getvalue()).decode()
    return (f'<image xmlns:xlink="http://www.w3.org/1999/xlink" x="0" y="0" width="{W}" height="{H}" '
            f'preserveAspectRatio="none" xlink:href="data:image/png;base64,{data}"/>')


def wire(tris, cam, min_len=1.5, crease_deg=14.0, smooth_only=False):
    """The edges of the triangles as one path: the outline and creases, not the ones across
    a smooth surface (the lofts' diagonals and the faint ring lines)."""
    if len(tris) == 0:
        return ''
    pts, _ = cam.project(tris)
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    edges = {}
    for k, t3 in enumerate(tris):
        for i in range(3):
            a, b = t3[i], t3[(i + 1) % 3]
            key = tuple(sorted((tuple(np.round(a, 3)), tuple(np.round(b, 3)))))
            edges.setdefault(key, []).append((k, i))
    cos_t = np.cos(np.radians(crease_deg))
    lens = np.stack([np.linalg.norm(tris[:, (i + 1) % 3] - tris[:, i], axis=1) for i in range(3)], axis=1)
    longest = lens.argmax(axis=1)
    segs = []
    for key, uses in edges.items():
        if len(uses) == 2 and abs(n[uses[0][0]] @ n[uses[1][0]]) > cos_t:
            # a quad's diagonal: the longest edge of both its triangles
            if all(longest[k] == i for k, i in uses):
                continue
            if smooth_only:
                continue
        k, i = uses[0]
        pa, pb = pts[k][i], pts[k][(i + 1) % 3]
        if np.hypot(*(pa - pb)) < min_len:
            continue
        segs.append(f'M{pa[0]:.1f} {pa[1]:.1f}L{pb[0]:.1f} {pb[1]:.1f}')
    return ''.join(segs)


def cut_outline(tris, cam, z0):
    """The section at the cut, as segments (a bright seam between the halves)."""
    segs = []
    for t in tris:
        d = t[:, 2] - z0
        pts = []
        for i in range(3):
            a, b = t[i], t[(i + 1) % 3]
            da, db = d[i], d[(i + 1) % 3]
            if (da < 0) != (db < 0):
                pts.append(a + (b - a) * (da / (da - db)))
        if len(pts) == 2:
            p, _ = cam.project(np.array(pts))
            segs.append(f'M{p[0,0]:.1f} {p[0,1]:.1f}L{p[1,0]:.1f} {p[1,1]:.1f}')
    return ''.join(segs)


def helicopter(obj, cam, z0, wire_color='#4cff6a', light=(0.5, 0.8, -0.4), tint=(1, 1, 1), wire_opacity=0.6,
               crease_deg=14.0, ambient=0.42, diffuse=0.7, roll_deg=0.0, pitch_deg=0.0, rotor_deg=0.0, rotor_wire=False, wire_width=0.8, seam_width=3.5,
               atlas=None, size=None, livery=False, smooth=False, rotor_colour=None):
    """`atlas` (a Pillow image, the OBJ's x-atlas.ppm) with `size` (the frame, w x h): the nose
    textured and rasterised (textured()), with `livery` in the Ukrainian missions' colours;
    otherwise flat shaded SVG triangles. `rotor_colour` (r, g, b 0..1): the blades in that colour
    (the model's are near black, lost against a night sky)."""
    tris, cols, groups, uvs = load_obj(obj, with_groups=True, with_uv=True)
    if rotor_colour is not None:
        cols = cols.copy()
        cols[np.isin(groups, ('main_rotor', 'tail_rotor'))] = rotor_colour
    if atlas is None:
        # without textures: none (our own model), still rasterised for the livery
        uvs = np.zeros(tris.shape) if livery else None
    if uvs is not None:
        # The texture coordinates, the places on the unturned model (the livery) and whether
        # a face is seen from both sides (the rotors' blades) ride along with the corners
        # through the cut.
        rotor = np.isin(groups, ('main_rotor', 'tail_rotor')).astype(float)
        tris = np.concatenate([tris, uvs, tris, np.repeat(rotor[:, None, None], 3, axis=1)], axis=2)
    if rotor_deg:
        # the main rotor's blades turned about the mast
        sel = groups == 'main_rotor'
        a = np.radians(rotor_deg)
        rot = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
        tris = tris.copy()
        tris[sel, :, :3] = tris[sel, :, :3] @ rot.T
    if roll_deg or pitch_deg:
        # roll about the long axis (positive: the right side down), then pitch (positive: nose down)
        r, p = np.radians(roll_deg), np.radians(pitch_deg)
        roll = np.array([[np.cos(r), np.sin(r), 0], [-np.sin(r), np.cos(r), 0], [0, 0, 1]])
        pitch = np.array([[1, 0, 0], [0, np.cos(p), np.sin(p)], [0, -np.sin(p), np.cos(p)]])
        tris = tris.copy()
        tris[:, :, :3] = tris[:, :, :3] @ (pitch @ roll).T
    if rotor_wire:
        # the main rotor all in wire, a ghost of the turning blades
        blades = groups == 'main_rotor'
        rotor_tris = tris[blades]
        tris, cols = tris[~blades], cols[~blades]
    front, fc = clip(tris, cols, z0, True)
    rear, _ = clip(tris, cols, z0, False)
    if rotor_wire:
        rear = np.concatenate([rear, rotor_tris])
    if uvs is not None:
        solid = textured(front[:, :, :3], front[:, :, 3:6], fc, cam, light, atlas, size, ambient, diffuse, tint,
                         model=front[:, :, 6:9], two_sided=front[:, 0, 9] > 0.5, livery=livery, smooth=smooth)
        front, rear, tris = front[:, :, :3], rear[:, :, :3], tris[:, :, :3]
    else:
        solid = shaded(front, fc, cam, light, ambient, diffuse, tint=tint)
    w = wire(rear, cam, crease_deg=crease_deg)
    seam = cut_outline(tris, cam, z0)
    return (f'<g filter="url(#neon)" opacity="{wire_opacity}"><path d="{w}" stroke="{wire_color}" stroke-width="{wire_width}" fill="none" '
            f'stroke-linecap="round"/></g>'
            f'<path d="{seam}" stroke="{wire_color}" stroke-width="{seam_width}" fill="none" filter="url(#neon)"/>'
            f'{solid}')


NEON = '''<filter id="neon" x="-10%" y="-10%" width="120%" height="120%">
 <feGaussianBlur stdDeviation="1.6" result="b"/>
 <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>'''
