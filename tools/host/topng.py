#!/usr/bin/env python3
"""build/shots/*.ppm -> .png (x2), and a contact sheet; gif NAME: NAME_*.ppm as an animated GIF"""
import sys, glob
from pathlib import Path
from PIL import Image
d = Path(sys.argv[1])
if len(sys.argv) > 3 and sys.argv[2] == "gif":
    fr = [Image.open(p).convert("RGB").resize((480, 480), Image.NEAREST) for p in sorted(d.glob(sys.argv[3] + "_*.ppm"))]
    ms = int(sys.argv[4]) if len(sys.argv) > 4 else 66
    fr[0].save(d / (sys.argv[3] + ".gif"), save_all=True, append_images=fr[1:], duration=ms, loop=0)
    sys.exit(0)
for p in sorted(d.glob("*.ppm")):
    Image.open(p).convert("RGB").resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
