#!/usr/bin/env python3
"""The manual's chart of the collective for level flight, measured on the simulation.

    tools/collective_chart.py     (after any change to the flight physics; build/hind_tests built)

Runs build/hind_tests --trim-table and writes docs/collective.csv (hind_tests compares the
simulation with it and fails when they part), docs/collective.cs.svg, docs/collective.en.svg and
the table between the collective markers in README.md and README.cs.md.
"""
import csv
import io
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEEDS = [0, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300, 325, 350]
SERIES = [  # mass, height, colour slot, dashed
    (9850, 450, 1, False),
    (11000, 450, 2, False),
    (11700, 450, 3, False),
    (11000, 1500, 4, True),
]
TEXT = {
    "cs": {
        "title": "Kolektiv pro vodorovný let",
        "sub": "režim Stable a Realistic, výška letu nad mořem v popisku",
        "x": "rychlost (km/h)",
        "y": "kolektiv (%)",
        "limit": "100 %: výš svítí TORQ",
        "names": ["9,85 t: drony a GŠG, 450 m", "11 t: 4 gondoly, 450 m", "11,7 t: nejtěžší, 450 m", "11 t, 1500 m"],
        "head": "| km/h | 9,85 t | 11 t | 11,7 t | 11 t, 1500 m | sklon nosu (11 t) |",
    },
    "en": {
        "title": "Collective for level flight",
        "sub": "Stable and Realistic models, height above sea level in the label",
        "x": "speed (km/h)",
        "y": "collective (%)",
        "limit": "100 %: TORQ lights above it",
        "names": ["9.85 t: drones and GShG, 450 m", "11 t: 4 pods, 450 m", "11.7 t: heaviest, 450 m", "11 t, 1500 m"],
        "head": "| km/h | 9.85 t | 11 t | 11.7 t | 11 t, 1500 m | nose attitude (11 t) |",
    },
}


def measure():
    out = subprocess.run([os.path.join(ROOT, "build", "hind_tests"), "--trim-table"], check=True, capture_output=True, text=True).stdout
    rows = list(csv.DictReader(io.StringIO(out)))
    curves = {}
    for r in rows:
        c = float(r["collective"])
        if c >= 129.5:  # the lever at its stop: not level flight any more
            continue
        curves.setdefault((int(r["mass"]), int(r["height"])), []).append((float(r["kmh"]), c, float(r["pitch"])))
    for k in curves:
        curves[k].sort()
    return curves


def at(curve, kmh, col):
    if kmh > curve[-1][0]:
        return None
    for a, b in zip(curve, curve[1:]):
        if a[0] <= kmh <= b[0]:
            t = (kmh - a[0]) / max(b[0] - a[0], 1e-6)
            return a[col] + (b[col] - a[col]) * t
    return curve[0][col]


def svg(curves, lang):
    t = TEXT[lang]
    w, h = 760, 470
    l, r, top, bottom = 62, 24, 62, 104
    x0, x1, y0, y1 = 0, 350, 50, 130
    px = lambda v: l + (v - x0) / (x1 - x0) * (w - l - r)
    py = lambda v: h - bottom - (v - y0) / (y1 - y0) * (h - top - bottom)
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" role="img" aria-label="{t["title"]}" font-family="system-ui,-apple-system,Segoe UI,Helvetica,Arial,sans-serif">']
    o.append("""<style>
:root{--bg:#fcfcfb;--ink:#0b0b0b;--ink2:#52514e;--grid:#e4e3df;--s1:#2a78d6;--s2:#eb6834;--s3:#1baf7a;--s4:#eda100}
@media (prefers-color-scheme:dark){:root{--bg:#1a1a19;--ink:#f0efec;--ink2:#c3c2b7;--grid:#3a3a37;--s1:#3987e5;--s2:#d95926;--s3:#199e70;--s4:#c98500}}
text{fill:var(--ink2);font-size:13px}.t{fill:var(--ink);font-size:17px;font-weight:600}
</style>""")
    o.append(f'<rect width="{w}" height="{h}" fill="var(--bg)"/>')
    o.append(f'<text x="{l}" y="26" class="t">{t["title"]}</text><text x="{l}" y="46">{t["sub"]}</text>')
    for v in range(y0, y1 + 1, 10):
        o.append(f'<line x1="{l}" x2="{w-r}" y1="{py(v):.1f}" y2="{py(v):.1f}" stroke="var(--grid)"/><text x="{l-8}" y="{py(v)+4:.1f}" text-anchor="end">{v}</text>')
    for v in range(x0, x1 + 1, 50):
        o.append(f'<line x1="{px(v):.1f}" x2="{px(v):.1f}" y1="{top}" y2="{h-bottom}" stroke="var(--grid)"/><text x="{px(v):.1f}" y="{h-bottom+18}" text-anchor="middle">{v}</text>')
    o.append(f'<line x1="{l}" x2="{w-r}" y1="{py(100):.1f}" y2="{py(100):.1f}" stroke="var(--ink2)" stroke-dasharray="2 3"/><text x="{l+8}" y="{py(100)-6:.1f}">{t["limit"]}</text>')
    o.append(f'<line x1="{l}" x2="{w-r}" y1="{h-bottom}" y2="{h-bottom}" stroke="var(--ink2)"/>')
    o.append(f'<text x="{(l+w-r)/2}" y="{h-bottom+38}" text-anchor="middle">{t["x"]}</text>')
    o.append(f'<text transform="translate(16 {(top+h-bottom)/2}) rotate(-90)" text-anchor="middle">{t["y"]}</text>')
    for i, (mass, height, slot, dashed) in enumerate(SERIES):
        pts = [(k, c) for k, c, _ in curves[(mass, height)] if c <= y1 and k <= x1]
        d = " ".join(f"{px(k):.1f},{py(c):.1f}" for k, c in pts)
        dash = ' stroke-dasharray="7 5"' if dashed else ""
        o.append(f'<polyline fill="none" stroke-width="2" stroke-linejoin="round" stroke-linecap="round" points="{d}" stroke="var(--s{slot})"{dash}/>')
    # The legend: two rows under the plot.
    for i, (mass, height, slot, dashed) in enumerate(SERIES):
        lx = l + (i % 2) * 330
        ly = h - 30 + (i // 2) * 20 - 8
        dash = ' stroke-dasharray="7 5"' if dashed else ""
        o.append(f'<line x1="{lx}" x2="{lx+26}" y1="{ly-4}" y2="{ly-4}" stroke="var(--s{slot})" stroke-width="2"{dash}/><text x="{lx+34}" y="{ly}">{t["names"][i]}</text>')
    o.append("</svg>")
    return "\n".join(o) + "\n"


def table(curves, lang):
    rows = [TEXT[lang]["head"], "|---:|---:|---:|---:|---:|---:|"]
    fmt = lambda v: "–" if v is None or v > 129 else f"{v:.0f} %"
    for kmh in SPEEDS:
        cols = [fmt(at(curves[(m, h)], kmh, 1)) for m, h, _, _ in SERIES]
        p = at(curves[(11000, 450)], kmh, 2)
        rows.append(f"| {kmh} | " + " | ".join(cols) + " | " + ("–" if p is None else f"{p:.1f}°".replace(".", "," if lang == "cs" else ".")) + " |")
    return "\n".join(rows)


def main():
    curves = measure()
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    with open(os.path.join(ROOT, "docs", "collective.csv"), "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["mass", "height", "kmh", "collective"])
        for (m, h), c in sorted(curves.items()):
            for k, v, _ in c:
                w.writerow([m, h, k, v])
    for lang, md in (("cs", "README.cs.md"), ("en", "README.md")):
        with open(os.path.join(ROOT, "docs", f"collective.{lang}.svg"), "w", encoding="utf-8") as f:
            f.write(svg(curves, lang))
        path = os.path.join(ROOT, md)
        s = open(path, encoding="utf-8").read()
        s, n = re.subn(r"(<!-- collective:begin -->\n).*?(<!-- collective:end -->)", lambda m: m.group(1) + "\n" + table(curves, lang) + "\n\n" + m.group(2), s, flags=re.S)
        if n != 1:
            raise SystemExit(f"{md}: no collective markers")
        open(path, "w", encoding="utf-8").write(s)


if __name__ == "__main__":
    main()
