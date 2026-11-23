#!/usr/bin/env python3
"""The night sky's stars for the game: src/render/stars.inc from the Yale Bright Star Catalogue.

Usage: python3 tools/stars.py bsc5.dat.gz

The catalogue (5th revised edition, Hoffleit & Warren 1991, public) is at
http://tdc-www.harvard.edu/catalogs/bsc5.dat.gz. Writes every star down to magnitude 5.5
(about what the eye sees from a dark place) as its J2000 right ascension and declination in
radians, its visual magnitude and its B-V colour index.
"""
import gzip
import math
import os
import sys

LIMIT = 5.5


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    opener = gzip.open if path.endswith('.gz') else open
    stars = []
    with opener(path, 'rt', encoding='latin-1') as f:
        for line in f:
            line = line.rstrip('\n').ljust(197)
            ra, de, vmag, bv = line[75:83], line[83:90], line[102:107], line[109:114]
            if not ra.strip() or not vmag.strip():
                continue  # novae and other objects without a position or a magnitude
            mag = float(vmag)
            if mag > LIMIT:
                continue
            ra_h = int(ra[0:2]) + int(ra[2:4]) / 60 + float(ra[4:8]) / 3600
            dec = int(de[1:3]) + int(de[3:5]) / 60 + int(de[5:7]) / 3600
            if de[0] == '-':
                dec = -dec
            colour = float(bv) if bv.strip() else 0.6
            stars.append((math.radians(ra_h * 15), math.radians(dec), mag, colour))
    stars.sort(key=lambda s: s[2])
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'render', 'stars.inc')
    with open(out, 'w') as f:
        f.write('// Written by tools/stars.py from the Yale Bright Star Catalogue: the stars down to\n')
        f.write(f'// magnitude {LIMIT}, brightest first. Right ascension and declination (J2000, radians),\n')
        f.write('// visual magnitude, B-V colour index.\n')
        for s in stars:
            f.write('{%.5ff, %.5ff, %.2ff, %.2ff},\n' % s)
    print(f'{len(stars)} stars to {os.path.normpath(out)}')


if __name__ == '__main__':
    main()
