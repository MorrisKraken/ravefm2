#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's dancers: pixel art, made here from parts (a skeleton posed frame by frame, limbs of a few pixels' width
in the outfit's colours, a dark outline, a light side), one per kind of track, each with its own dance of 8 frames
(two beats). Original characters.
  gen_chars.py preview DIR      PNG sheet + one GIF per dancer (x4)
  gen_chars.py header OUT.h     the C data: per dancer a palette (RGB565) and its frames, 4 bits a pixel, run-length"""
import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw

W, H = 32, 48
OUT = (14, 10, 22)                       # the outline
NFR = 8


def lerp(a, b, t):
    return a + (b - a) * t


class Pose:
    """joints in pixels (feet on y 46): hip, neck, head, l/r elbow, l/r hand, l/r knee, l/r foot"""
    def __init__(self, **k):
        self.__dict__.update(k)


def base_pose():
    return dict(hip=(16, 30), neck=(16, 19), head=(16, 13), le=(12, 25), lh=(11, 30), re=(20, 25), rh=(21, 30),
                lk=(14, 38), lf=(13, 46), rk=(18, 38), rf=(19, 46))


def mv(d, **ch):
    d = dict(d)
    for k, v in ch.items():
        d[k] = v
    return d


def shift(d, dx=0, dy=0, keep=("lf", "rf")):
    return {k: (x + (0 if k in keep else dx), y + (0 if k in keep else dy)) for k, (x, y) in d.items()}


# ---------------------------------------------------------------------------------------------------- the dances ---
def dance_hakken():          # the gabber: legs kicking forward in turn, fists pumping, bouncing
    b = base_pose()
    fr = []
    for i in range(NFR):
        s = 1 if (i // 2) % 2 == 0 else -1       # which leg
        up = i % 2 == 0
        d = shift(b, 0, 1 if up else 0)
        if s > 0:
            d = mv(d, rk=(21, 36), rf=(25, 40) if up else (20, 46), lk=(14, 38), lf=(13, 46))
            d = mv(d, le=(11, 21), lh=(12, 15) if up else (10, 24), re=(20, 26), rh=(22, 31))
        else:
            d = mv(d, lk=(11, 36), lf=(7, 40) if up else (12, 46), rk=(18, 38), rf=(19, 46))
            d = mv(d, re=(21, 21), rh=(20, 15) if up else (22, 24), le=(12, 26), lh=(10, 31))
        fr.append(d)
    return fr


def dance_robot():
    b = base_pose()
    keys = [dict(le=(10, 19), lh=(10, 13), re=(22, 19), rh=(22, 25)),
            dict(le=(10, 19), lh=(10, 25), re=(22, 19), rh=(22, 13)),
            dict(le=(9, 19), lh=(4, 19), re=(23, 19), rh=(28, 19)),
            dict(le=(11, 25), lh=(11, 31), re=(22, 18), rh=(28, 18))]
    fr = []
    for i in range(NFR):
        k = keys[(i // 2) % 4]
        d = mv(b, **k)
        if i % 2:
            d = shift(d, 0, 1)
        fr.append(d)
    return fr


def dance_toprock():         # the b-boy: steps crossing, arms swinging
    b = base_pose()
    fr = []
    for i in range(NFR):
        t = i / NFR * 2 * math.pi
        sx = round(math.sin(t) * 2)
        d = shift(b, sx, 0)
        if i < 4:
            d = mv(d, rk=(15 + sx, 37), rf=(11, 45), lk=(14 + sx, 38), lf=(13, 46))
        else:
            d = mv(d, lk=(17 + sx, 37), lf=(21, 45), rk=(18 + sx, 38), rf=(19, 46))
        d = mv(d, le=(11 + sx, 24), lh=(14 + sx + round(math.cos(t) * 3), 21), re=(21 + sx, 24),
               rh=(18 + sx - round(math.cos(t) * 3), 22))
        fr.append(d)
    return fr


def dance_pump():            # the acid raver: hands up pumping on the beat
    b = base_pose()
    fr = []
    for i in range(NFR):
        up = i % 4 < 2
        d = shift(b, 0, 0 if up else 2)
        d = mv(d, le=(11, 15 if up else 18), lh=(10, 8 if up else 13), re=(21, 15 if up else 18),
               rh=(22, 8 if up else 13))
        if not up:
            d = mv(d, lk=(13, 39), rk=(19, 39))
        fr.append(d)
    return fr


def dance_lean():            # the keytar player: leaning back and forth
    b = base_pose()
    fr = []
    for i in range(NFR):
        t = math.sin(i / NFR * 2 * math.pi)
        dx = round(t * 2)
        d = shift(b, dx, abs(round(t)))
        d = mv(d, le=(12 + dx, 25), lh=(15 + dx, 28), re=(20 + dx, 25), rh=(19 + dx, 29))
        fr.append(d)
    return fr


def dance_mc():              # the MC: bounce, the mic hand at the mouth, the other pointing
    b = base_pose()
    fr = []
    for i in range(NFR):
        down = i % 2
        d = shift(b, 0, down)
        point = (i // 2) % 2
        d = mv(d, re=(20, 18 + down), rh=(17, 15 + down),
               le=(10, 20 + down) if point else (12, 25 + down), lh=(5, 15 + down) if point else (11, 30 + down))
        d = mv(d, lk=(13, 38), rk=(19, 38))
        fr.append(d)
    return fr


def dance_sticks():          # the alien: glowsticks in circles
    b = base_pose()
    fr = []
    for i in range(NFR):
        t = i / NFR * 2 * math.pi
        d = shift(b, round(math.sin(t * 2)), 0)
        d = mv(d, lh=(10 + round(math.cos(t) * 5), 20 + round(math.sin(t) * 5)), le=(11, 23),
               rh=(22 - round(math.cos(t) * 5), 20 + round(math.sin(t + math.pi) * 5)), re=(21, 23))
        fr.append(d)
    return fr


def dance_dj():              # the DJ: nodding, a hand on the headphones, scratching
    b = base_pose()
    fr = []
    for i in range(NFR):
        nod = [0, 1, 2, 1, 0, 1, 2, 1][i]
        d = mv(b, head=(16, 13 + nod), neck=(16, 19 + nod // 2), le=(11, 18), lh=(12, 11 + nod),
               re=(21, 24), rh=(24 + [0, 2, 0, -2, 0, 2, 0, -2][i], 28))
        fr.append(d)
    return fr


def hakken_legs(d, i):
    """the hakken's legs: a kick forward every half beat, the other leg bent, a bounce"""
    s = 1 if (i // 2) % 2 == 0 else -1
    up = i % 2 == 0
    d = shift(d, 0, 1 if up else 0)
    if s > 0:
        d = mv(d, rk=(21, 36), rf=(25, 40) if up else (20, 46), lk=(14, 38), lf=(13, 46))
    else:
        d = mv(d, lk=(11, 36), lf=(7, 40) if up else (12, 46), rk=(18, 38), rf=(19, 46))
    return d


def dance_hakken_pump():     # both fists pumping up on the beat
    fr = []
    for i in range(NFR):
        up = i % 4 < 2
        d = hakken_legs(base_pose(), i)
        d = mv(d, le=(11, 18 if up else 22), lh=(12, 11 if up else 19), re=(21, 18 if up else 22), rh=(20, 11 if up else 19))
        fr.append(d)
    return fr


def dance_hakken_point():    # a finger to the sky, the other hand on the hip
    fr = []
    for i in range(NFR):
        d = hakken_legs(base_pose(), i)
        hi = i % 2 == 0
        d = mv(d, re=(21, 15), rh=(22, 7 if hi else 9), le=(10, 24), lh=(13, 29))
        fr.append(d)
    return fr


def dance_hakken_horn():     # the air horn up, blown on the beat
    fr = []
    for i in range(NFR):
        d = hakken_legs(base_pose(), i)
        d = mv(d, re=(23, 18), rh=(24, 15 if i % 4 < 2 else 17), le=(11, 25), lh=(9, 30))
        fr.append(d)
    return fr


def dance_hakken_mc():       # the mic at the mouth, the other arm pumping
    fr = []
    for i in range(NFR):
        d = hakken_legs(base_pose(), i)
        up = i % 4 < 2
        d = mv(d, re=(20, 19), rh=(17, 15), le=(10, 19 if up else 23), lh=(7, 12 if up else 20))
        fr.append(d)
    return fr


def dance_skull():           # the skull: claws up, hakken
    fr = []
    for i in range(NFR):
        d = hakken_legs(base_pose(), i)
        w = 1 if i % 4 < 2 else -1
        d = mv(d, le=(9, 17), lh=(6 + w, 10), re=(23, 17), rh=(26 - w, 10))
        fr.append(d)
    return fr


def dance_zombie():          # arms out in front, swaying, the hakken under it
    fr = []
    for i in range(NFR):
        d = hakken_legs(base_pose(), i)
        w = 1 if i % 4 < 2 else -1
        d = mv(d, le=(19, 21 + w), lh=(26, 21 + w), re=(21, 22 - w), rh=(28, 22 - w), head=(17, 14))
        fr.append(d)
    return fr


# ------------------------------------------------------------------------------------------------- the dancers ---
SKIN = [(242, 196, 160), (196, 140, 100), (130, 86, 58), (255, 214, 186)]
BLOOD = (150, 0, 0)
CHARS = [
    # one per track (KICK, BREAK, BASS, LEAD, SYNTH, SAMPLE), dark: hardcore's horror side
    dict(name="SKINHEAD", dance=dance_hakken, top=(44, 44, 50), top2=(150, 0, 0), legs=(40, 40, 46), legs2=(150, 0, 0),
         shoes=(230, 230, 230), shoe2=(150, 0, 0), skin=(214, 170, 140), head="skin", eyes=(255, 20, 20), splat=True),
    dict(name="GASMASK", dance=dance_hakken_pump, top=(70, 78, 58), top2=None, legs=(46, 50, 42), legs2=None,
         shoes=(20, 20, 22), shoe2=(150, 0, 0), skin=(60, 64, 58), head="gasmask", eyes=(255, 40, 20)),
    dict(name="ZOMBIE", dance=dance_zombie, top=(70, 60, 52), top2=None, legs=(36, 34, 40), legs2=None,
         shoes=(70, 60, 50), shoe2=(150, 0, 0), skin=(128, 150, 112), head="zombie", ribs=True, splat=True),
    dict(name="BONES", dance=dance_hakken_horn, top=(14, 14, 16), top2=None, legs=(14, 14, 16), legs2=None,
         shoes=(230, 226, 214), shoe2=(150, 0, 0), skin=(230, 226, 214), head="skull", horn=True, skeleton=True),
    dict(name="REAPER", dance=dance_skull, top=(54, 50, 60), top2=(150, 0, 0), legs=(46, 42, 52), legs2=None,
         shoes=(20, 20, 22), shoe2=(150, 0, 0), skin=(232, 228, 214), head="hoodskull"),
    dict(name="DEMON", dance=dance_dj, top=(48, 44, 50), top2=(150, 0, 0), legs=(42, 40, 46), legs2=None,
         shoes=(20, 20, 22), shoe2=(220, 20, 20), skin=(176, 20, 16), head="demon", hat=(40, 40, 46), decks=True),
]


def dark(c, k=0.62):
    return tuple(int(v * k) for v in c)


def light(c, k=0.35):
    return tuple(int(v + (255 - v) * k) for v in c)


def limb(dr, a, b, c, w, stroke=False):
    if stroke:                                             # (a dark edge first: the limb stands out of what is under)
        limb(dr, a, b, OUT, w + 2)
    dr.line([a, b], fill=c, width=w)
    r = w // 2
    for p in (a, b):
        dr.ellipse([p[0] - r, p[1] - r, p[0] + r - (1 - w % 2), p[1] + r - (1 - w % 2)], fill=c)


def draw_frame(ch, d):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dr = ImageDraw.Draw(im)
    top, legs, shoes, skin = ch["top"], ch["legs"], ch["shoes"], ch["skin"]
    # the back leg and arm (darker)
    limb(dr, d["hip"], d["lk"], dark(legs, 0.8), 4)
    limb(dr, d["lk"], d["lf"], dark(legs, 0.8), 4)
    dr.rectangle([d["lf"][0] - 2, d["lf"][1] - 1, d["lf"][0] + 2, d["lf"][1]], fill=dark(shoes, 0.8))
    limb(dr, d["neck"], d["le"], dark(top, 0.8), 3)
    limb(dr, d["le"], d["lh"], dark(top, 0.8), 3)
    dr.rectangle([d["lh"][0] - 1, d["lh"][1] - 1, d["lh"][0] + 1, d["lh"][1] + 1], fill=dark(skin, 0.85))
    # the front leg
    limb(dr, d["hip"], d["rk"], legs, 4, True)
    limb(dr, d["rk"], d["rf"], legs, 4, True)
    limb(dr, d["hip"], d["rk"], legs, 4)
    if ch.get("legs2"):                                    # (tracksuit stripes)
        dr.line([d["hip"][0] + 2, d["hip"][1], d["rk"][0] + 2, d["rk"][1]], fill=ch["legs2"])
        dr.line([d["rk"][0] + 2, d["rk"][1], d["rf"][0] + 2, d["rf"][1] - 2], fill=ch["legs2"])
    dr.rectangle([d["rf"][0] - 2, d["rf"][1] - 1, d["rf"][0] + 3, d["rf"][1]], fill=shoes)
    if ch.get("shoe2"):                                    # (the air bubble / the tick)
        dr.point([(d["rf"][0] + 1, d["rf"][1] - 1)], fill=ch["shoe2"])
    # the torso
    nx, ny = d["neck"]
    hx, hy = d["hip"]
    dr.polygon([(nx - 5, ny), (nx + 5, ny), (hx + 4, hy + 1), (hx - 4, hy + 1)], fill=top)
    if ch.get("shiny"):                                    # (a shell suit's shine)
        dr.line([(nx - 3, ny + 2), (hx - 3, hy - 1)], fill=light(top, 0.45))
    if ch.get("top2"):
        dr.line([(nx + 2, ny + 1), (hx + 2, hy)], fill=ch["top2"])
    if ch.get("bomber"):                                   # a bomber: puffed, its orange lining at the collar
        dr.polygon([(nx - 6, ny + 1), (nx + 6, ny + 1), (hx + 5, hy - 1), (hx - 5, hy - 1)], fill=top)
        dr.rectangle([hx - 5, hy - 1, hx + 5, hy + 1], fill=dark(top, 0.7))
        dr.rectangle([nx - 2, ny, nx + 2, ny + 1], fill=ch["bomber"])
        dr.line([(nx, ny + 2), (hx, hy - 2)], fill=dark(top, 0.6))
    if ch.get("smiley"):                                   # an acid smiley on the shirt
        cx, cy = (nx + hx) // 2, (ny + hy) // 2
        dr.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(255, 229, 59), outline=(20, 20, 20))
        dr.point([(cx - 1, cy - 1), (cx + 1, cy - 1)], fill=(20, 20, 20))
        dr.line([(cx - 2, cy + 1), (cx - 1, cy + 2), (cx + 1, cy + 2), (cx + 2, cy + 1)], fill=(20, 20, 20))
    if ch.get("skeleton"):                                 # bones: the spine, the ribs, white on black
        dr.line([(nx, ny + 1), (hx, hy - 1)], fill=skin)
        for r in range(4):
            y = ny + 2 + r * 2
            dr.line([(nx - 4 + r // 2, y), (nx + 4 - r // 2, y)], fill=skin)
        dr.line([(hx - 3, hy), (hx + 3, hy)], fill=skin)
    if ch.get("ribs"):                                     # the shirt torn open: ribs and blood
        dr.rectangle([nx - 2, ny + 3, nx + 3, ny + 8], fill=(60, 10, 10))
        for r in range(3):
            dr.line([(nx - 2, ny + 4 + r * 2), (nx + 3, ny + 4 + r * 2)], fill=(220, 210, 190))
        dr.line([(nx + 1, ny + 9), (nx + 1, ny + 12)], fill=BLOOD)
    if ch.get("splat"):                                    # blood spattered on it
        dr.point([(nx - 3, ny + 5), (nx - 2, ny + 6), (hx + 2, hy - 3), (hx + 3, hy - 4), (nx + 4, ny + 2)], fill=BLOOD)
    if ch["head"] == "robot":                              # a panel on its chest, its lights
        dr.rectangle([nx - 3, ny + 3, nx + 3, ny + 7], fill=(40, 46, 60))
        dr.point([(nx - 2, ny + 5), (nx + 2, ny + 5)], fill=ch["eye"])
        dr.point([(nx, ny + 5)], fill=(245, 255, 0))
    # the head
    hx, hy = d["head"]
    k = ch["head"]
    if k == "robot":
        dr.rectangle([hx - 5, hy - 5, hx + 5, hy + 4], fill=skin)
        dr.rectangle([hx - 4, hy - 2, hx + 4, hy + 1], fill=(30, 30, 40))
        dr.point([(hx - 2, hy - 1), (hx + 2, hy - 1)], fill=ch["eye"])
        dr.line([(hx, hy - 6), (hx, hy - 9)], fill=dark(skin))
        dr.point([(hx, hy - 10)], fill=ch["eye"])
    elif k == "skin":                                      # shaved, a scar, red eyes, frowning hard
        dr.rounded_rectangle([hx - 4, hy - 5, hx + 4, hy + 4], radius=3, fill=skin)
        dr.line([(hx - 3, hy - 5), (hx + 3, hy - 5)], fill=dark(skin, 0.75))
        dr.line([(hx - 3, hy - 3), (hx - 1, hy - 2)], fill=(30, 10, 10)), dr.line([(hx + 3, hy - 3), (hx + 1, hy - 2)], fill=(30, 10, 10))
        dr.point([(hx - 2, hy - 1), (hx + 2, hy - 1)], fill=ch["eyes"])
        dr.line([(hx - 2, hy + 2), (hx + 2, hy + 2)], fill=(60, 20, 20))
        dr.line([(hx + 2, hy - 4), (hx + 4, hy + 1)], fill=(120, 30, 30))   # (the scar)
    elif k == "gasmask":                                   # a gas mask: dark rubber, two round glowing lenses, the filter
        dr.rounded_rectangle([hx - 5, hy - 6, hx + 5, hy + 4], radius=4, fill=skin)
        dr.ellipse([hx - 4, hy - 3, hx - 1, hy], fill=(20, 20, 20)), dr.ellipse([hx + 1, hy - 3, hx + 4, hy], fill=(20, 20, 20))
        dr.point([(hx - 3, hy - 2), (hx + 2, hy - 2)], fill=ch["eyes"])
        dr.rectangle([hx - 2, hy + 2, hx + 2, hy + 6], fill=(34, 36, 32))
        dr.line([(hx - 2, hy + 4), (hx + 2, hy + 4)], fill=(90, 94, 86))
    elif k == "zombie":                                    # rotting: grey-green, one eye gone, the jaw hanging, blood
        dr.rounded_rectangle([hx - 4, hy - 5, hx + 4, hy + 5], radius=3, fill=skin)
        dr.rectangle([hx - 3, hy - 2, hx - 1, hy], fill=(10, 10, 10))
        dr.point([(hx + 2, hy - 1)], fill=(240, 240, 200))
        dr.rectangle([hx - 2, hy + 2, hx + 2, hy + 4], fill=(40, 6, 6))
        dr.point([(hx - 1, hy + 2), (hx + 1, hy + 2)], fill=(220, 220, 200))
        dr.line([(hx + 1, hy + 5), (hx + 1, hy + 8)], fill=BLOOD)
        dr.point([(hx - 4, hy - 3), (hx + 3, hy - 5)], fill=dark(skin, 0.6))
    elif k == "demon":                                     # a red face, horns, black eyes, fangs
        dr.rounded_rectangle([hx - 4, hy - 5, hx + 4, hy + 4], radius=3, fill=skin)
        dr.polygon([(hx - 4, hy - 4), (hx - 7, hy - 10), (hx - 2, hy - 5)], fill=(230, 220, 200))
        dr.polygon([(hx + 4, hy - 4), (hx + 7, hy - 10), (hx + 2, hy - 5)], fill=(230, 220, 200))
        dr.rectangle([hx - 3, hy - 2, hx - 1, hy - 1], fill=(0, 0, 0)), dr.rectangle([hx + 1, hy - 2, hx + 3, hy - 1], fill=(0, 0, 0))
        dr.point([(hx - 2, hy - 2), (hx + 2, hy - 2)], fill=(255, 220, 0))
        dr.line([(hx - 2, hy + 2), (hx + 2, hy + 2)], fill=(20, 0, 0))
        dr.point([(hx - 2, hy + 3), (hx + 2, hy + 3)], fill=(250, 250, 250))
    elif k == "cube":                                      # a big yellow block of a head, shades, a red mohawk, a grin
        dr.rectangle([hx - 6, hy - 6, hx + 6, hy + 5], fill=skin)
        dr.rectangle([hx - 6, hy + 4, hx + 6, hy + 5], fill=dark(skin, 0.8))
        for x in range(-2, 3):
            dr.line([(hx + x, hy - 7), (hx + x, hy - 10 + abs(x))], fill=ch["hair"])
        dr.rectangle([hx - 5, hy - 3, hx + 5, hy - 1], fill=(16, 16, 20))
        dr.point([(hx - 4, hy - 3), (hx + 1, hy - 3)], fill=(250, 250, 250))
        dr.line([(hx - 3, hy + 2), (hx - 2, hy + 3), (hx + 2, hy + 3), (hx + 3, hy + 2)], fill=(60, 20, 20))
    elif k == "alien":
        dr.ellipse([hx - 6, hy - 7, hx + 6, hy + 4], fill=skin)
        dr.polygon([(hx - 5, hy - 2), (hx - 1, hy), (hx - 4, hy + 1)], fill=(10, 10, 10))
        dr.polygon([(hx + 5, hy - 2), (hx + 1, hy), (hx + 4, hy + 1)], fill=(10, 10, 10))
    elif k in ("skull", "hoodskull"):
        if k == "hoodskull":                               # (a black hood round it)
            dr.rounded_rectangle([hx - 6, hy - 7, hx + 6, hy + 5], radius=4, fill=(70, 70, 80))
        dr.rounded_rectangle([hx - 5, hy - 6, hx + 5, hy + 2], radius=4, fill=skin)
        dr.rectangle([hx - 3, hy + 2, hx + 3, hy + 4], fill=skin)
        eye = (255, 20, 20)
        dr.rectangle([hx - 4, hy - 2, hx - 1, hy], fill=(10, 10, 12))
        dr.rectangle([hx + 1, hy - 2, hx + 4, hy], fill=(10, 10, 12))
        dr.point([(hx - 3, hy - 1), (hx + 2, hy - 1)], fill=eye)
        dr.point([(hx, hy + 1)], fill=(10, 10, 12))
        for x in (-2, 0, 2):
            dr.point([(hx + x, hy + 3)], fill=(10, 10, 12))
    else:
        dr.rounded_rectangle([hx - 4, hy - 5, hx + 4, hy + 4], radius=3, fill=skin)
        dr.point([(hx - 2, hy - 1), (hx + 2, hy - 1)], fill=(20, 16, 24))
        dr.line([(hx - 1, hy + 2), (hx + 1, hy + 2)], fill=dark(skin, 0.6))
        if k in ("bald", "phones"):                        # (shaved: the stubble, a shine)
            dr.line([(hx - 3, hy - 5), (hx + 3, hy - 5)], fill=dark(skin, 0.78))
            dr.point([(hx - 2, hy - 4)], fill=light(skin, 0.6))
            dr.line([(hx - 2, hy - 3), (hx - 1, hy - 3)], fill=dark(skin, 0.5))   # (the brows: frowning)
            dr.line([(hx + 1, hy - 3), (hx + 2, hy - 3)], fill=dark(skin, 0.5))
        elif k == "ponytail":                              # pulled tight, high, the tail flying
            dr.rectangle([hx - 4, hy - 6, hx + 4, hy - 4], fill=ch["hair"])
            dr.rectangle([hx - 1, hy - 8, hx + 1, hy - 6], fill=(255, 60, 160))
            dr.line([(hx + 1, hy - 9), (hx + 6, hy - 11), (hx + 8, hy - 7)], fill=ch["hair"], width=2)
        elif k == "spiky":
            for x in range(-4, 5, 2):
                dr.line([(hx + x, hy - 5), (hx + x + (1 if x > 0 else -1 if x < 0 else 0), hy - 9)], fill=ch["hair"])
            dr.line([(hx - 4, hy - 5), (hx + 4, hy - 5)], fill=ch["hair"])
        elif k == "long":
            dr.rectangle([hx - 5, hy - 6, hx + 5, hy - 4], fill=ch["hair"])
            dr.rectangle([hx - 5, hy - 4, hx - 4, hy + 5], fill=ch["hair"])
            dr.rectangle([hx + 4, hy - 4, hx + 5, hy + 5], fill=ch["hair"])
        elif k == "cap":
            dr.rectangle([hx - 5, hy - 7, hx + 4, hy - 4], fill=ch["hat"])
            dr.rectangle([hx + 2, hy - 4, hx + 8, hy - 3], fill=dark(ch["hat"]))
        elif k == "bucket":
            dr.rectangle([hx - 4, hy - 8, hx + 4, hy - 4], fill=ch["hat"])
            dr.rectangle([hx - 6, hy - 4, hx + 6, hy - 3], fill=dark(ch["hat"], 0.8))
            dr.point([(hx - 2, hy - 1), (hx + 2, hy - 1)], fill=(20, 16, 24))
        if ch.get("visor"):                                # a gamer's visor over the eyes
            dr.rectangle([hx - 4, hy - 2, hx + 4, hy], fill=ch["visor"])
            dr.point([(hx - 3, hy - 2)], fill=(250, 250, 250))
        if k == "phones":
            dr.arc([hx - 6, hy - 9, hx + 6, hy + 3], 180, 360, fill=(240, 240, 240))
            dr.rectangle([hx - 7, hy - 2, hx - 5, hy + 2], fill=(240, 240, 240))
            dr.rectangle([hx + 5, hy - 2, hx + 7, hy + 2], fill=(240, 240, 240))
    if ch.get("skeleton"):                                 # (the bones of the legs and the back arm)
        for a_, b_ in (("hip", "rk"), ("rk", "rf"), ("hip", "lk"), ("lk", "lf"), ("neck", "le"), ("le", "lh")):
            dr.line([d[a_], d[b_]], fill=dark(skin, 0.85))
    # the front arm
    limb(dr, d["neck"], d["re"], top, 3, True)
    limb(dr, d["re"], d["rh"], top, 3, True)
    limb(dr, d["neck"], d["re"], light(top, 0.15), 3)
    limb(dr, d["re"], d["rh"], light(top, 0.15), 3)
    if ch.get("skeleton"):
        dr.line([d["neck"], d["re"]], fill=skin), dr.line([d["re"], d["rh"]], fill=skin)
    dr.rectangle([d["rh"][0] - 1, d["rh"][1] - 1, d["rh"][0] + 1, d["rh"][1] + 1], fill=skin)
    # props
    if ch.get("keytar"):
        a = (d["lh"][0] - 4, d["lh"][1] + 1)
        b = (d["rh"][0] + 5, d["rh"][1] - 2)
        dr.line([a, b], fill=(255, 61, 154), width=3)
        for t in range(1, 5):
            x = round(lerp(a[0], b[0], t / 5))
            y = round(lerp(a[1], b[1], t / 5))
            dr.point([(x, y)], fill=(250, 250, 250))
    if ch.get("horn"):                                     # an air horn raised high: the red can in the fist, its white
        x, y = d["rh"]                                     # trumpet straight up, flaring to a wide rim; blasts on the beat
        dr.rectangle([x - 1, y - 1, x + 1, y + 4], fill=(225, 10, 10))
        dr.line([(x - 1, y - 1), (x - 1, y + 4)], fill=(255, 120, 110))
        dr.line([(x - 1, y - 2), (x + 1, y - 2)], fill=(20, 20, 24))
        for k, half in enumerate((0, 0, 1, 1, 1, 2, 2, 3)):   # the trumpet, from the cap up
            dr.line([(x - half, y - 3 - k), (x + half, y - 3 - k)], fill=(250, 250, 250))
            if half:
                dr.point([(x + half, y - 3 - k)], fill=(176, 176, 186))
        dr.line([(x - 4, y - 11), (x + 4, y - 11)], fill=(250, 250, 250))   # the rim
        if ch.get("blow"):
            dr.line([(x - 4, y - 13), (x - 6, y - 15)], fill=(245, 255, 0))
            dr.line([(x, y - 13), (x, y - 16)], fill=(245, 255, 0))
            dr.line([(x + 4, y - 13), (x + 6, y - 15)], fill=(245, 255, 0))
    if ch.get("mic"):
        x, y = d["rh"]
        dr.rectangle([x - 1, y - 3, x + 1, y - 1], fill=(60, 60, 70))
        dr.point([(x, y - 3)], fill=(200, 200, 210))
    if ch.get("sticks"):
        for (hand, elb), c in zip(((d["lh"], d["le"]), (d["rh"], d["re"])), ch["sticks"]):
            vx, vy = hand[0] - elb[0], hand[1] - elb[1]
            n = max(1.0, math.hypot(vx, vy))
            e = (round(hand[0] + vx / n * 6), round(hand[1] + vy / n * 6))
            dr.line([hand, e], fill=c, width=2)
    if ch.get("decks"):
        dr.rectangle([5, 31, 27, 37], fill=(40, 40, 52))
        dr.rectangle([5, 31, 27, 32], fill=(90, 90, 110))
        dr.ellipse([7, 32, 13, 36], fill=(16, 16, 20))
        dr.ellipse([19, 32, 25, 36], fill=(16, 16, 20))
        dr.point([(10, 34), (22, 34)], fill=(255, 61, 154))
    # the outline: every empty pixel touching the figure
    px = im.load()
    solid = [[px[x, y][3] > 0 for x in range(W)] for y in range(H)]
    for y in range(H):
        for x in range(W):
            if solid[y][x]:
                continue
            if any(0 <= x + dx < W and 0 <= y + dy < H and solid[y + dy][x + dx]
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                px[x, y] = OUT + (255,)
    return im


def frames(ch):
    out = []
    for i, d in enumerate(ch["dance"]()):
        c = dict(ch, blow=ch.get("horn") and i % 4 < 2)
        out.append(draw_frame(c, d))
    return out


def preview(dirp):
    dirp = Path(dirp)
    dirp.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGB", (W * NFR * 3 + 8, H * len(CHARS) * 3 + 8), (8, 6, 14))
    for r, ch in enumerate(CHARS):
        fr = frames(ch)
        for i, f in enumerate(fr):
            sheet.paste(f.resize((W * 3, H * 3), Image.NEAREST), (4 + i * W * 3, 4 + r * H * 3), f.resize((W * 3, H * 3), Image.NEAREST))
    sheet.save(dirp / "chars_sheet.png")
    # a dance floor GIF: all eight, in two rows, x3
    gif = []
    for i in range(NFR * 3):
        im = Image.new("RGB", (240, 240), (8, 6, 14))
        for k, ch in enumerate(CHARS):
            f = frames(ch)[i % NFR] if i < NFR else None
            gif.append(None) if False else None
        gif.append(im)
    fr_all = [frames(ch) for ch in CHARS]
    gif = []
    for i in range(NFR * 4):
        im = Image.new("RGB", (240, 240), (8, 6, 14))
        for k in range(len(CHARS)):
            f = fr_all[k][i % NFR].resize((W * 2, H * 2), Image.NEAREST)
            x = 6 + (k % 3) * 78
            y = 14 + (k // 3) * 112
            im.paste(f, (x, y), f)
        gif.append(im.resize((480, 480), Image.NEAREST))
    gif[0].save(dirp / "chars_floor.gif", save_all=True, append_images=gif[1:], duration=120, loop=0)


def header(out):
    L = ["/* generated by tools/gen_chars.py: RAVE FM 2's dancers, %d x %d, %d frames each (TB_ICON_* order) */" % (W, H, NFR),
         f"#define RFM_CHAR_W {W}", f"#define RFM_CHAR_H {H}", f"#define RFM_CHAR_NFR {NFR}", f"#define RFM_NCHAR {len(CHARS)}"]
    total = 0
    names = []
    for k, ch in enumerate(CHARS):
        fr = frames(ch)
        cols = []
        for f in fr:
            for px in (f.get_flattened_data() if hasattr(f, 'get_flattened_data') else f.getdata()):
                if px[3] and px[:3] not in cols:
                    cols.append(px[:3])
        remap = {c: c for c in cols}
        while len(cols) > 15:                             # (too many: the two nearest made one)
            best = None
            for i in range(len(cols)):
                for j in range(i + 1, len(cols)):
                    dd = sum((a - b) ** 2 for a, b in zip(cols[i], cols[j]))
                    if best is None or dd < best[0]:
                        best = (dd, i, j)
            _, i, j = best
            gone = cols.pop(j)
            for c in remap:
                if remap[c] == gone:
                    remap[c] = cols[i]
        def at(px):
            return cols.index(remap[px[:3]]) + 1
        pal = [0] + [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r, g, b in cols]
        pal += [0] * (16 - len(pal))
        L.append(f"static const uint16_t RFM_CHAR_PAL_{k}[16] = {{{', '.join(f'0x{c:04X}' for c in pal)}}};")
        offs, blob = [], bytearray()
        for f in fr:
            idx = [at(px) if px[3] else 0 for px in (f.get_flattened_data() if hasattr(f, 'get_flattened_data') else f.getdata())]
            offs.append(len(blob))
            i = 0
            while i < len(idx):
                c, n = idx[i], 1
                while n < 16 and i + n < len(idx) and idx[i + n] == c:
                    n += 1
                blob.append((n - 1) << 4 | c)
                i += n
        L.append(f"static const uint8_t RFM_CHAR_RLE_{k}[{len(blob)}] = {{")
        for q in range(0, len(blob), 32):
            L.append("    " + ", ".join(str(b) for b in blob[q:q + 32]) + ",")
        L.append("};")
        L.append(f"static const uint16_t RFM_CHAR_OFF_{k}[{NFR}] = {{{', '.join(map(str, offs))}}};")
        names.append(k)
        total += len(blob) + 32 + NFR * 2
    L.append("static const uint8_t *const RFM_CHAR_RLE[%d] = {%s};" % (len(CHARS), ", ".join(f"RFM_CHAR_RLE_{k}" for k in names)))
    L.append("static const uint16_t *const RFM_CHAR_OFF[%d] = {%s};" % (len(CHARS), ", ".join(f"RFM_CHAR_OFF_{k}" for k in names)))
    L.append("static const uint16_t *const RFM_CHAR_PAL[%d] = {%s};" % (len(CHARS), ", ".join(f"RFM_CHAR_PAL_{k}" for k in names)))
    L.append("static const char *const RFM_CHAR_NAME[%d] = {%s};" % (len(CHARS), ", ".join(f'"{c["name"]}"' for c in CHARS)))
    Path(out).write_text("\n".join(L) + "\n")
    print(f"dancers: {len(CHARS)} x {NFR} frames, {total} B")


if __name__ == "__main__":
    if sys.argv[1] == "preview":
        preview(sys.argv[2])
    else:
        header(sys.argv[2])
