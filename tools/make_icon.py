#!/usr/bin/env python3
"""The game's icon from our own Mi-24 (not from the original's data), as the HIND REBUILT logo:
the nose in the Ukrainian missions' colours (the Czech camouflage with smooth patches, Ukraine's
blue and yellow, half_wire.py livery=True), from the rotor back a wireframe of the model's own
triangles, banked towards us over the night sky with the sight's ring.

  ./build/hind --model-sheet mi24.obj
  python3 tools/make_icon.py mi24.obj

Writes data/hind.svg (the drawing), data/hind.png (256 px), data/hind.ico (16 to 256 px, for the
Windows .exe) and src/render/icon.inc (64 x 64 RGBA, the window's icon set at run time). Needs
numpy, Pillow and Inkscape (to turn the SVG into pixels).
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import half_wire as hw  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 1024
CREAM = '#e8dfbf'


def drawing(obj):
    cam = hw.Camera(eye=(27, 1.8, -14.4), target=(0, 0.6, 1.8), fov_deg=19, width=SIZE, height=900, scale=0.68)
    cam.cx, cam.cy = 490, 490
    heli = hw.helicopter(obj, cam, z0=0.3, wire_color=CREAM, wire_opacity=0.65, ambient=0.6, diffuse=0.7,
                         tint=(0.92, 0.96, 1.05), roll_deg=30, rotor_deg=69, wire_width=1.6, seam_width=5,
                         size=(SIZE, SIZE), livery=True, rotor_colour=(0.42, 0.44, 0.45))
    c = SIZE / 2
    r = 0.43 * SIZE
    ticks = ''.join(f'<path d="M{c} {c - r - 30} L{c} {c - r + 30}" transform="rotate({a} {c} {c})"/>' for a in (0, 90, 180, 270))
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}" width="{SIZE}" height="{SIZE}">
<defs>
 <linearGradient id="night" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#050912"/><stop offset="1" stop-color="#16263a"/></linearGradient>
 <clipPath id="tile"><rect x="24" y="24" width="{SIZE - 48}" height="{SIZE - 48}" rx="190"/></clipPath>
 {hw.NEON}
</defs>
<g clip-path="url(#tile)">
 <rect width="{SIZE}" height="{SIZE}" fill="url(#night)"/>
 <g stroke="{CREAM}" fill="none" opacity="0.32"><circle cx="{c}" cy="{c}" r="{r}" stroke-width="12"/>
  <g stroke-width="10">{ticks}</g></g>
 <g transform="rotate(-9 {cam.cx} {cam.cy})">{heli}</g>
</g>
<rect x="24" y="24" width="{SIZE - 48}" height="{SIZE - 48}" rx="190" fill="none" stroke="#2b3a4e" stroke-width="10"/>
</svg>
'''


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    data = os.path.join(ROOT, 'data')
    svg = os.path.join(data, 'hind.svg')
    with open(svg, 'w') as f:
        f.write(drawing(sys.argv[1]))
    with tempfile.TemporaryDirectory() as tmp:
        big = os.path.join(tmp, 'icon.png')
        subprocess.run(['inkscape', svg, '--export-type=png', '--export-filename=' + big, '-w', str(SIZE)],
                       check=True, capture_output=True)
        icon = Image.open(big).convert('RGBA')
    icon.resize((256, 256), Image.LANCZOS).save(os.path.join(data, 'hind.png'))
    icon.save(os.path.join(data, 'hind.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    small = icon.resize((64, 64), Image.LANCZOS)
    with open(os.path.join(ROOT, 'src', 'render', 'icon.inc'), 'w') as f:
        f.write('// Written by tools/make_icon.py: the window icon, 64 x 64 RGBA.\n')
        raw = small.tobytes()
        for i in range(0, len(raw), 32):
            f.write(','.join(str(b) for b in raw[i:i + 32]) + ',\n')
    print('data/hind.svg, data/hind.png, data/hind.ico, src/render/icon.inc')


if __name__ == '__main__':
    main()
