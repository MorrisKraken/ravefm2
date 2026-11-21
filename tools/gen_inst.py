#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's instruments: the picture on a track's EDIT page, one per kind of sound, dark (hardcore's horror side),
pixel art 64 x 64 shown twice as big. Two frames each: at rest, and hit (the sound playing). Original drawings.
  gen_inst.py preview DIR      PNG sheet (x4) + the pictures one by one
  gen_inst.py header OUT.h     the C data: per picture a palette (RGB565) and its 2 frames, 4 bits a pixel, run-length"""
import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw

W = H = 64
OUT = (8, 6, 10)
BONE, BONE_D, BONE_L = (226, 220, 200), (150, 142, 124), (250, 248, 236)
RED, RED_D, RED_L, GLOW = (190, 0, 0), (100, 0, 0), (255, 70, 60), (255, 30, 10)
BLK, DARK = (14, 12, 16), (30, 28, 34)
ST, ST_D, ST_L = (104, 104, 116), (56, 56, 66), (176, 176, 188)
WOOD, WOOD_D = (84, 46, 30), (50, 26, 18)
YEL, YEL_D = (246, 206, 24), (170, 120, 0)
PURP, PURP_D = (96, 44, 120), (52, 20, 70)
MOSS = (70, 96, 60)


def skull(dr, cx, cy, s=1.0, eye=GLOW, jaw=0, col=BONE):
    """a skull, centred on cx, cy (the eyes' row); s: its scale; jaw: how far the jaw drops"""
    r = 9 * s
    dr.ellipse([cx - r, cy - r * 1.05, cx + r, cy + r * 0.75], fill=col)
    dr.rectangle([cx - r * 0.62, cy + r * 0.3, cx + r * 0.62, cy + r * 0.9], fill=col)   # the cheekbones down
    dr.ellipse([cx - r * 0.75, cy - r * 0.35, cx - r * 0.12, cy + r * 0.3], fill=BLK)       # the eyes
    dr.ellipse([cx + r * 0.12, cy - r * 0.35, cx + r * 0.75, cy + r * 0.3], fill=BLK)
    if eye:
        dr.point([(round(cx - r * 0.42), round(cy - r * 0.02)), (round(cx + r * 0.44), round(cy - r * 0.02))], fill=eye)
        if s >= 1.4:
            dr.point([(round(cx - r * 0.42) + 1, round(cy - r * 0.02)), (round(cx + r * 0.44) - 1, round(cy - r * 0.02))], fill=eye)
    dr.polygon([(cx, cy + r * 0.32), (cx - r * 0.16, cy + r * 0.58), (cx + r * 0.16, cy + r * 0.58)], fill=BLK)   # the nose
    jy = cy + r * 0.9 + jaw
    dr.rectangle([cx - r * 0.55, jy, cx + r * 0.55, jy + r * 0.38], fill=col)               # the jaw
    for k in range(-2, 3):                                                                  # the teeth
        x = round(cx + k * r * 0.22)
        dr.line([(x, round(cy + r * 0.7)), (x, round(cy + r * 0.9))], fill=BLK)
        if jaw:
            dr.line([(x, round(jy)), (x, round(jy + r * 0.2))], fill=BLK)
    if jaw >= 2:
        dr.rectangle([cx - r * 0.5, cy + r * 0.92, cx + r * 0.5, jy - 1], fill=BLK)
    dr.arc([cx - r * 0.9, cy - r * 1.0, cx + r * 0.3, cy - r * 0.1], 200, 260, fill=BONE_L)   # a shine


def drips(dr, x0, x1, y, col, hit, seed):
    for i, x in enumerate(range(x0, x1, 3)):
        n = (seed * 7 + i * 5) % 6
        if n < 3:
            dr.line([(x, y), (x, y + n + 1 + (2 if hit else 0))], fill=col)


# ------------------------------------------------------------------------------------------------ the pictures ---
def kick(dr, hit):            # KICK: a bass drum, a skull painted on its skin; hit: the skull's eyes burn, the skin waves
    cx, cy, r = 32, 30, 25
    dr.ellipse([cx - r, cy - r, cx + r, cy + r], fill=RED_D)                  # the shell
    dr.ellipse([cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3], fill=BLK)     # the hoop
    dr.ellipse([cx - r + 5, cy - r + 5, cx + r - 5, cy + r - 5], fill=DARK if not hit else (44, 10, 10))   # the skin
    for k in range(10):                                                       # the lugs
        a = k * math.tau / 10
        x, y = cx + math.cos(a) * (r - 1.5), cy + math.sin(a) * (r - 1.5)
        dr.rectangle([x - 1, y - 1, x + 1, y + 1], fill=ST_L)
    if hit:                                                                   # the skin's waves
        for rr in (17, 13):
            dr.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=RED)
    skull(dr, cx, cy - 2, 1.25, GLOW if hit else RED_D, jaw=2 if hit else 0)
    dr.rectangle([cx - 14, cy + r + 1, cx - 11, 62], fill=ST_D)                # the legs
    dr.rectangle([cx + 11, cy + r + 1, cx + 14, 62], fill=ST_D)
    bx = cx + 18 if not hit else cx + 12                                      # the beater: back, then on the skin
    dr.line([(cx + 22, 62), (bx, cy + 12)], fill=ST_L, width=2)
    dr.ellipse([bx - 3, cy + 9, bx + 2, cy + 14], fill=BONE if not hit else BONE_L)
    if hit:
        for a in (-60, -30, 0):
            t = math.radians(a)
            dr.line([(cx + math.cos(t) * 28, cy + math.sin(t) * 28), (cx + math.cos(t) * 31, cy + math.sin(t) * 31)], fill=GLOW)


def brk(dr, hit):             # BREAK: a skeleton at its kit, sticks up, then down on the snare and the hat
    sx, sy = 32, 15
    skull(dr, sx, sy, 0.9, GLOW if hit else RED_D, jaw=1 if hit else 0)
    dr.line([(sx, 24), (sx, 40)], fill=BONE, width=2)                          # the spine
    for k in range(4):                                                        # the ribs
        y = 26 + k * 3
        dr.arc([sx - 8 + k, y - 2, sx + 8 - k, y + 3], 180, 360, fill=BONE)
    dr.line([(sx - 6, 40), (sx + 6, 40)], fill=BONE, width=2)                  # the pelvis
    if hit:                                                                   # the arms: down, sticks on the drums
        arms = [((sx - 7, 25), (sx - 12, 33), (sx - 16, 37), (sx - 26, 38)), ((sx + 7, 25), (sx + 13, 31), (sx + 17, 34), (sx + 26, 31))]
    else:                                                                     # up, sticks raised
        arms = [((sx - 7, 25), (sx - 13, 21), (sx - 15, 13), (sx - 20, 4)), ((sx + 7, 25), (sx + 13, 21), (sx + 15, 13), (sx + 20, 4))]
    dr.line([(sx - 7, 25), (sx + 7, 25)], fill=BONE, width=2)                  # the shoulders
    for sh, el, ha, tip in arms:
        dr.line([sh, el], fill=BONE, width=2)
        dr.line([el, ha], fill=BONE, width=2)
        dr.line([ha, tip], fill=WOOD if not hit else BONE_L, width=2)
    # the kit, in front: the kick in the middle, the snare left, the hi-hat far left, the crash right
    dr.ellipse([20, 40, 44, 62], fill=RED_D)
    dr.ellipse([23, 43, 41, 59], fill=BLK)
    dr.ellipse([28, 48, 36, 54], fill=RED)
    dr.rectangle([5, 38 - (1 if hit else 0), 21, 43 - (1 if hit else 0)], fill=ST)   # the snare
    dr.line([(5, 38 - (1 if hit else 0)), (21, 38 - (1 if hit else 0))], fill=ST_L)
    dr.line([(13, 44), (13, 62)], fill=ST_D)
    j = 2 if hit else 0                                                       # the crash, shaking
    dr.line([(44, 30 - j), (62, 33 + j)], fill=YEL, width=2)
    dr.line([(53, 32), (53, 62)], fill=ST_D)
    dr.line([(0, 34), (8, 34)], fill=YEL_D, width=1)                           # the hi-hat
    dr.line([(0, 36), (8, 36)], fill=YEL_D)
    if hit:
        dr.point([(57, 26), (60, 28), (55, 24), (3, 31), (6, 30)], fill=YEL)


def bass(dr, hit):            # BASS: a speaker cabinet with a demon's face: the woofer its mouth, fangs, tweeters for eyes
    x0, y0, x1, y1 = 12, 10, 52, 62
    dr.polygon([(x0 + 4, y0), (x0 - 1, y0 - 9), (x0 + 10, y0)], fill=BONE_D)  # the horns
    dr.polygon([(x1 - 4, y0), (x1 + 1, y0 - 9), (x1 - 10, y0)], fill=BONE_D)
    dr.rectangle([x0, y0, x1, y1], fill=DARK)
    dr.rectangle([x0 + 2, y0 + 2, x1 - 2, y1 - 2], fill=BLK)
    for (x, y) in ((x0 + 2, y0 + 2), (x1 - 4, y0 + 2), (x0 + 2, y1 - 4), (x1 - 4, y1 - 4)):
        dr.rectangle([x, y, x + 2, y + 2], fill=ST)                            # the corners
    for ex in (24, 40):                                                        # the tweeters: eyes
        dr.ellipse([ex - 5, 14, ex + 5, 24], fill=ST_D)
        dr.ellipse([ex - 3, 16, ex + 3, 22], fill=GLOW if hit else RED_D)
        dr.point([(ex - 1, 18)], fill=BONE_L if hit else RED)
    dr.line([(18, 12), (29, 15)], fill=ST_L), dr.line([(46, 12), (35, 15)], fill=ST_L)   # frowning
    p = 3 if hit else 0                                                        # the woofer, pumping out
    cx, cy = 32, 43
    dr.ellipse([cx - 15 - p, cy - 15 - p, cx + 15 + p, cy + 15 + p], fill=ST_D)
    dr.ellipse([cx - 12 - p, cy - 12 - p, cx + 12 + p, cy + 12 + p], fill=(40, 6, 6))   # the mouth
    for k in range(-2, 3):                                                     # the fangs, top and bottom
        x = cx + k * 5
        dr.polygon([(x - 2, cy - 11 - p), (x + 2, cy - 11 - p), (x, cy - 5 - p + (2 if abs(k) == 1 else 0))], fill=BONE)
        dr.polygon([(x - 2, cy + 11 + p), (x + 2, cy + 11 + p), (x, cy + 6 + p - (2 if abs(k) == 1 else 0))], fill=BONE)
    dr.ellipse([cx - 4, cy - 3, cx + 4, cy + 3], fill=RED if hit else RED_D)  # the tongue / dust cap
    if hit:
        for k in range(3):
            dr.arc([cx - 24 - k * 4, cy - 14 - k * 2, cx - 16 - k * 4, cy + 14 + k * 2], 120, 240, fill=RED_L)
            dr.arc([cx + 16 + k * 4, cy - 14 - k * 2, cx + 24 + k * 4, cy + 14 + k * 2], -60, 60, fill=RED_L)


def acid(dr, hit):            # ACID (the 303): the acid smiley gone bad: an X for an eye, a grin with fangs, melting
    cx, cy, r = 32, 28, 22
    dr.ellipse([cx - r, cy - r, cx + r, cy + r], fill=YEL)
    dr.arc([cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2], 200, 280, fill=(255, 240, 140))
    dr.arc([cx - r, cy - r, cx + r, cy + r], 20, 160, fill=YEL_D)
    for k, (x, n) in enumerate(((14, 6), (20, 11), (27, 4), (35, 9), (42, 13), (49, 5))):   # melting
        n += 4 if hit else 0
        top = cy + round(math.sqrt(max(0, r * r - (x - cx) ** 2))) - 2
        dr.rectangle([x - 1, top, x + 1, top + n], fill=YEL)
        dr.ellipse([x - 2, top + n - 1, x + 2, top + n + 2], fill=YEL)
    dr.line([(19, 17), (27, 25)], fill=BLK, width=2)                            # the X eye
    dr.line([(27, 17), (19, 25)], fill=BLK, width=2)
    dr.ellipse([36, 16, 44, 26], fill=BLK)                                     # the other: open
    dr.point([(40, 21)], fill=GLOW if hit else RED)
    if hit:
        dr.point([(41, 21), (40, 20)], fill=GLOW)
    m = 4 if hit else 0                                                        # the grin, the fangs
    dr.chord([16, 22 - m // 2, 48, 42 + m], 20, 160, fill=BLK)
    for x in (22, 28, 36, 42):
        dr.polygon([(x - 2, 32 + (2 if x in (22, 42) else 0)), (x + 2, 32 + (2 if x in (22, 42) else 0)), (x, 36 + m // 2)], fill=BONE_L)
    dr.line([(30, 6), (34, 10)], fill=RED_D), dr.line([(34, 10), (31, 12)], fill=RED_D)   # a crack
    dr.line([(12, 16), (16, 18)], fill=RED_D)


def lead(dr, hit):            # LEAD: a killer clown's head; hit: it screams
    cx = 32
    for k, x in enumerate((8, 12, 52, 56)):                                    # the hair, red tufts
        dr.ellipse([x - 7, 10 + (k % 2) * 4, x + 7, 26 + (k % 2) * 4], fill=RED)
    dr.ellipse([cx - 18, 4, cx + 18, 52], fill=BONE_L)                         # the face
    dr.polygon([(cx - 14, 7), (cx, 0), (cx + 14, 7)], fill=PURP)                # a little hat
    dr.ellipse([cx - 2, -2, cx + 2, 2], fill=YEL)
    for s in (-1, 1):                                                          # the eyes: black diamonds, thin brows
        ex = cx + s * 9
        dr.polygon([(ex, 14), (ex + 5, 22), (ex, 30), (ex - 5, 22)], fill=PURP_D)
        dr.ellipse([ex - 2, 19, ex + 2, 24], fill=BLK)
        dr.point([(ex, 21)], fill=GLOW if hit else YEL)
        dr.line([(ex - 5 * s, 12), (ex + 4 * s, 16)], fill=BLK, width=2)
    dr.ellipse([cx - 4, 25, cx + 4, 32], fill=RED)                             # the nose
    dr.point([(cx - 1, 27)], fill=RED_L)
    if hit:                                                                    # the mouth: a scream, rows of teeth
        dr.polygon([(cx - 15, 36), (cx + 15, 36), (cx + 10, 51), (cx - 10, 51)], fill=RED)
        dr.polygon([(cx - 13, 37), (cx + 13, 37), (cx + 8, 49), (cx - 8, 49)], fill=(40, 0, 0))
        for k in range(-3, 4):
            x = cx + k * 3.6
            dr.polygon([(x - 1.6, 37), (x + 1.6, 37), (x, 41)], fill=BONE)
            dr.polygon([(x - 1.4, 49), (x + 1.4, 49), (x, 46)], fill=BONE)
    else:                                                                      # a wide grin, sharp teeth
        dr.chord([cx - 16, 26, cx + 16, 46], 10, 170, fill=RED)
        dr.chord([cx - 14, 30, cx + 14, 44], 10, 170, fill=(40, 0, 0))
        for k in range(-3, 4):
            x = cx + k * 3.6
            dr.polygon([(x - 1.6, 37), (x + 1.6, 37), (x, 41)], fill=BONE)
    dr.rectangle([cx - 12, 54, cx + 12, 63], fill=PURP)                         # the ruff
    for x in range(cx - 12, cx + 13, 6):
        dr.polygon([(x, 52), (x + 3, 58), (x - 3, 58)], fill=BONE_D)
    drips(dr, cx - 10, cx + 11, 51, RED, hit, 3)


def stab(dr, hit):            # STAB: a dagger stabbed into a keyboard; hit: in deeper, blood
    y0 = 36
    dr.rectangle([2, y0, 61, 62], fill=DARK)
    for k in range(8):                                                         # white keys
        x = 4 + k * 7
        dr.rectangle([x, y0 + 4, x + 5, 60], fill=BONE)
    for k in (0, 1, 3, 4, 5):                                                  # black keys
        x = 8 + k * 7
        dr.rectangle([x, y0 + 4, x + 3, 50], fill=BLK)
    d = 5 if hit else 0                                                        # the dagger
    tip = (33, 46 + d)
    dr.polygon([(29, 14 + d), (37, 14 + d), tip], fill=ST_L)                    # the blade
    dr.line([(33, 14 + d), tip], fill=ST)
    dr.rectangle([23, 10 + d, 43, 13 + d], fill=YEL_D)                          # the guard
    dr.rectangle([30, -1 + d, 36, 9 + d], fill=WOOD)                            # the grip
    for y in range(1 + d, 9 + d, 2):
        dr.line([(30, y), (36, y)], fill=WOOD_D)
    skull(dr, 33, -4 + d, 0.42, None, col=BONE)                                # a little skull on the pommel
    dr.rectangle([26, y0 + 3, 40, y0 + 6 + d], fill=RED)                        # the blood
    for x, n in ((27, 6), (31, 12), (35, 8), (39, 4)):
        dr.line([(x, y0 + 6), (x, y0 + 6 + n + d)], fill=RED)
    if hit:
        for (x, y) in ((20, 30), (46, 28), (18, 26), (49, 33), (24, 25), (43, 24)):
            dr.rectangle([x, y, x + 1, y + 1], fill=RED)


def hoover(dr, hit):          # HOOVER: a vampire bat, wings out; hit: the wings beat down
    cx, cy = 32, 30
    up = not hit
    for s in (-1, 1):
        tip = (cx + s * 31, cy - 18 if up else cy + 14)
        mid = (cx + s * 18, cy - 14 if up else cy + 4)
        pts = [(cx + s * 4, cy - 4), mid, tip]
        lowy = cy + 8 if up else cy + 20
        scal = [(cx + s * 26, lowy), (cx + s * 20, lowy - 4), (cx + s * 14, lowy + 1), (cx + s * 8, lowy - 3), (cx + s * 4, cy + 6)]
        dr.polygon(pts + scal, fill=PURP_D)
        dr.line([(cx + s * 4, cy - 4), mid, tip], fill=PURP, width=2)          # the arm bone
        for p in scal[:-1:2]:                                                  # the fingers
            dr.line([mid, p], fill=PURP)
    dr.ellipse([cx - 6, cy - 6, cx + 6, cy + 12], fill=(40, 24, 44))            # the body
    dr.ellipse([cx - 6, cy - 14, cx + 6, cy - 2], fill=(52, 30, 58))            # the head
    dr.polygon([(cx - 6, cy - 10), (cx - 7, cy - 20), (cx - 2, cy - 13)], fill=(52, 30, 58))   # the ears
    dr.polygon([(cx + 6, cy - 10), (cx + 7, cy - 20), (cx + 2, cy - 13)], fill=(52, 30, 58))
    dr.point([(cx - 3, cy - 9), (cx + 3, cy - 9)], fill=GLOW)
    if hit:
        dr.point([(cx - 2, cy - 9), (cx + 2, cy - 9)], fill=GLOW)
    dr.line([(cx - 3, cy - 5), (cx + 3, cy - 5)], fill=BLK)
    dr.polygon([(cx - 2, cy - 5), (cx - 1, cy - 2), (cx, cy - 5)], fill=BONE_L)   # fangs
    dr.polygon([(cx, cy - 5), (cx + 1, cy - 2), (cx + 2, cy - 5)], fill=BONE_L)
    dr.line([(cx - 3, cy + 12), (cx - 4, cy + 16)], fill=(52, 30, 58))         # the feet
    dr.line([(cx + 3, cy + 12), (cx + 4, cy + 16)], fill=(52, 30, 58))
    if hit:
        dr.point([(cx, cy - 1)], fill=RED)


def pad(dr, hit):             # PAD: a graveyard under a full moon, the fog; hit: a hand comes out of the grave
    dr.ellipse([38, 2, 58, 22], fill=(200, 196, 180))                          # the moon
    dr.ellipse([42, 6, 47, 10], fill=(170, 166, 150)), dr.ellipse([49, 13, 53, 16], fill=(170, 166, 150))
    dr.line([(4, 30), (14, 18)], fill=BLK, width=2)                             # a dead tree
    dr.line([(14, 18), (14, 4)], fill=BLK, width=2)
    dr.line([(14, 10), (22, 4)], fill=BLK), dr.line([(14, 14), (8, 8)], fill=BLK)
    dr.rectangle([0, 50, 63, 63], fill=(26, 24, 30))                            # the ground
    dr.rounded_rectangle([20, 22, 40, 54], radius=8, fill=ST)                  # the big stone
    dr.rounded_rectangle([22, 24, 38, 54], radius=7, fill=ST_D)
    for i, ln in enumerate(("R", "I", "P")):                                   # R.I.P.
        x = 25 + i * 5
        if ln == "R":
            dr.line([(x, 31), (x, 37)], fill=BONE_D), dr.line([(x, 31), (x + 2, 31)], fill=BONE_D)
            dr.line([(x + 2, 31), (x + 2, 33)], fill=BONE_D), dr.line([(x, 34), (x + 2, 34)], fill=BONE_D)
            dr.line([(x + 1, 35), (x + 2, 37)], fill=BONE_D)
        elif ln == "I":
            dr.line([(x + 1, 31), (x + 1, 37)], fill=BONE_D)
        else:
            dr.line([(x, 31), (x, 37)], fill=BONE_D), dr.line([(x, 31), (x + 2, 31)], fill=BONE_D)
            dr.line([(x + 2, 31), (x + 2, 34)], fill=BONE_D), dr.line([(x, 34), (x + 2, 34)], fill=BONE_D)
    dr.rectangle([4, 40, 7, 54], fill=ST_D), dr.rectangle([1, 43, 10, 46], fill=ST_D)     # a cross
    dr.rounded_rectangle([46, 36, 58, 54], radius=5, fill=ST_D)                 # a small stone
    dr.line([(49, 41), (55, 47)], fill=BLK)
    if hit:                                                                    # the hand, out of the ground
        dr.rectangle([10, 41, 14, 52], fill=MOSS)
        for k in range(4):
            dr.line([(10 + k * 1.5, 41), (9 + k * 2, 35 + abs(k - 1.5))], fill=MOSS, width=1)
        dr.line([(14, 44), (17, 40)], fill=MOSS)
        dr.point([(12, 47)], fill=RED)
    for k in range(5):                                                         # the fog
        y = 50 + (k % 2) * 4 - (2 if hit else 0)
        dr.ellipse([k * 14 - 6, y, k * 14 + 12, y + 7], fill=(64, 60, 72))


def pluck(dr, hit):           # PLUCK: a spider on its web; hit: it drops on its thread, the web shakes
    cx, cy = 32, 22
    sh = 1 if hit else 0
    for k in range(8):                                                         # the spokes
        a = k * math.tau / 8 + 0.2
        dr.line([(cx, cy), (cx + math.cos(a) * 40, cy + math.sin(a) * 40 + (sh if k % 2 else -sh))], fill=ST)
    for rr in (6, 12, 19, 27):                                                 # the rings
        pts = []
        for k in range(9):
            a = k * math.tau / 8 + 0.2
            pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr + (sh if k % 2 else -sh)))
        dr.line(pts, fill=ST_D if rr > 6 else ST)
    sy = 46 if hit else 30                                                     # the spider
    dr.line([(cx, cy), (cx, sy - 6)], fill=BONE_D)
    for s in (-1, 1):
        for k in range(4):
            y = sy - 3 + k * 2
            kx, ky = cx + s * (7 + (k % 2)), y - 4 + k * 1
            dr.line([(cx, y), (kx, ky)], fill=(74, 68, 82), width=2)
            dr.line([(kx, ky), (kx + s * 3, ky + 6 - k)], fill=(74, 68, 82), width=2)
    dr.ellipse([cx - 5, sy - 2, cx + 5, sy + 9], fill=(50, 44, 56))                     # the abdomen, a red mark
    dr.polygon([(cx, sy + 1), (cx - 2, sy + 4), (cx, sy + 7), (cx + 2, sy + 4)], fill=RED)
    dr.ellipse([cx - 3, sy - 6, cx + 3, sy], fill=(50, 44, 56))                          # the head, its eyes
    dr.point([(cx - 1, sy - 4), (cx + 1, sy - 4)], fill=GLOW)
    dr.point([(cx - 2, sy - 3), (cx + 2, sy - 3)], fill=RED)
    dr.ellipse([6, 50, 12, 56], fill=BONE), dr.rectangle([7, 55, 11, 57], fill=BONE)   # a fly's bones in the web
    dr.point([(8, 52), (10, 52)], fill=BLK)


def organ(dr, hit):           # ORGAN: a haunted church organ, its pipes, candles; hit: the flames flare
    dr.rectangle([4, 44, 59, 63], fill=WOOD_D)                                  # the console
    dr.rectangle([6, 46, 57, 50], fill=WOOD)
    for k in range(10):
        dr.rectangle([8 + k * 5, 51, 11 + k * 5, 56], fill=BONE if k % 3 else BONE_D)
    hs = (30, 36, 42, 38, 34, 40, 44, 40, 34, 38, 42, 36, 30)                   # the pipes
    for k, h in enumerate(hs):
        x = 5 + k * 4
        top = 44 - h + (0 if not hit else -1 * (k % 2))
        dr.rectangle([x, top, x + 3, 44], fill=ST if k % 2 else ST_L)
        dr.line([(x, top), (x, 44)], fill=ST_D)
        dr.rectangle([x, top + 4, x + 3, top + 6], fill=BLK)                    # the mouth of the pipe
    skull(dr, 32, 36, 0.62, GLOW if hit else RED_D)                             # a skull on the console
    for x in (2, 60):                                                          # candles
        dr.rectangle([x - 1, 40, x + 1, 52], fill=BONE)
        drips(dr, x - 1, x + 2, 40, BONE_L, False, x)
        f = 6 if hit else 3
        dr.polygon([(x - 2, 39), (x + 2, 39), (x, 39 - f)], fill=YEL)
        dr.point([(x, 38)], fill=BONE_L)
    if hit:
        for (x, y) in ((10, 6), (50, 8), (30, 2)):                              # notes / sparks in the air
            dr.point([(x, y), (x + 1, y - 1)], fill=RED_L)


def fx(dr, hit):              # FX: an air-raid siren on a pole; hit: it wails, red light sweeps out
    cx, cy = 32, 24
    if hit:
        dr.polygon([(cx, cy), (0, cy - 22), (0, cy + 6)], fill=(70, 0, 0))
        dr.polygon([(cx, cy), (63, cy - 8), (63, cy + 22)], fill=(70, 0, 0))
    dr.rectangle([cx - 2, cy + 14, cx + 2, 60], fill=ST_D)                       # the pole
    dr.rectangle([cx - 10, 59, cx + 10, 63], fill=ST_D)
    dr.ellipse([cx - 16, cy - 8, cx + 16, cy + 16], fill=ST)                    # the body
    dr.rectangle([cx - 16, cy - 2, cx + 16, cy + 10], fill=ST)
    for k in range(-3, 4):                                                     # the slots
        x = cx + k * 4
        dr.rectangle([x - 1, cy - 2, x + 1, cy + 10], fill=BLK)
    dr.rectangle([cx - 18, cy - 4, cx + 18, cy - 2], fill=ST_L)
    dr.rectangle([cx - 18, cy + 10, cx + 18, cy + 12], fill=ST_L)
    dr.ellipse([cx - 7, cy - 18, cx + 7, cy - 4], fill=GLOW if hit else RED_D)   # the light on top
    dr.ellipse([cx - 4, cy - 15, cx, cy - 11], fill=RED_L if hit else RED)
    dr.rectangle([cx - 8, cy - 6, cx + 8, cy - 4], fill=ST_D)
    if hit:
        for k in range(3):
            dr.arc([cx - 30 - k * 5, cy - 14 - k * 4, cx - 18 - k * 5, cy + 22 + k * 4], 120, 240, fill=BONE_L)
            dr.arc([cx + 18 + k * 5, cy - 14 - k * 4, cx + 30 + k * 5, cy + 22 + k * 4], -60, 60, fill=BONE_L)
    skull(dr, cx, 48, 0.5, None)                                               # a danger plate on the pole
    dr.line([(cx - 6, 56), (cx + 6, 60)], fill=BONE), dr.line([(cx + 6, 56), (cx - 6, 60)], fill=BONE)


def sample(dr, hit):          # SAMPLE: a cassette, a skull on its label; hit: its reels turn, the tape glows
    x0, y0, x1, y1 = 4, 12, 59, 50
    dr.rounded_rectangle([x0, y0, x1, y1], radius=3, fill=DARK)
    dr.rounded_rectangle([x0 + 4, y0 + 3, x1 - 4, y1 - 12], radius=2, fill=RED_D)   # the label
    dr.rectangle([x0 + 4, y0 + 3, x1 - 4, y0 + 6], fill=BONE_D)
    dr.rounded_rectangle([18, 24, 45, 34], radius=4, fill=BLK)                  # the window
    a = 0.6 if hit else 0.0
    for rx in (24, 39):                                                        # the reels
        dr.ellipse([rx - 4, 25, rx + 4, 33], fill=BONE)
        dr.ellipse([rx - 2, 27, rx + 2, 31], fill=BLK)
        for k in range(3):
            t = a + k * math.tau / 3
            dr.line([(rx, 29), (rx + math.cos(t) * 3, 29 + math.sin(t) * 3)], fill=BONE)
    dr.rectangle([29, 27, 34, 31], fill=WOOD if not hit else (150, 60, 20))     # the tape between
    skull(dr, 12, 23, 0.42, GLOW if hit else None)                              # the label's skull, crossed bones
    skull(dr, 52, 23, 0.42, GLOW if hit else None)
    dr.polygon([(16, y1), (20, y1 - 8), (43, y1 - 8), (47, y1)], fill=ST_D)     # the bottom edge
    for x in (22, 28, 35, 41):
        dr.ellipse([x - 1, y1 - 5, x + 1, y1 - 3], fill=BLK)
    if hit:
        for k in range(4):                                                     # sound coming out
            dr.line([(10 + k * 14, 58), (14 + k * 14, 54)], fill=RED_L)
            dr.line([(14 + k * 14, 54), (18 + k * 14, 60)], fill=RED_L)


PICS = [("KICK", "LA GROSSE CAISSE", kick), ("BREAK", "LE BATTEUR", brk), ("BASS", "L'ENCEINTE", bass),
        ("ACID", "LE SMILEY", acid), ("LEAD", "LE CLOWN", lead), ("STAB", "LE POIGNARD", stab),
        ("HOOVER", "LA CHAUVE-SOURIS", hoover), ("PAD", "LE CIMETIERE", pad), ("PLUCK", "L'ARAIGNEE", pluck),
        ("ORGAN", "L'ORGUE", organ), ("FX", "LA SIRENE", fx), ("SAMPLE", "LA CASSETTE", sample)]


def render(fn, hit):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    fn(ImageDraw.Draw(im), hit)
    px = im.load()                                                             # the outline round the drawing
    solid = [[px[x, y][3] > 0 for x in range(W)] for y in range(H)]
    for y in range(H):
        for x in range(W):
            if not solid[y][x] and any(0 <= x + dx < W and 0 <= y + dy < H and solid[y + dy][x + dx]
                                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                px[x, y] = OUT + (255,)
    return im


def quant(frames):
    """both frames on one palette of 15 colours (+ clear)"""
    sheet = Image.new("RGB", (W * len(frames), H), (0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f.convert("RGB"), (i * W, 0), f)
    q = sheet.quantize(15, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = (q.getpalette()[:45] + [0] * 45)[:45]
    out = []
    for i, f in enumerate(frames):
        a = f.load()
        qq = q.crop((i * W, 0, (i + 1) * W, H)).load()
        out.append([[0 if a[x, y][3] == 0 else qq[x, y] + 1 for x in range(W)] for y in range(H)])
    return [(0, 0, 0)] + [tuple(pal[i:i + 3]) for i in range(0, 45, 3)], out


def rle(idx):
    flat = [c for row in idx for c in row]
    out, i = [], 0
    while i < len(flat):
        c, n = flat[i], 1
        while i + n < len(flat) and flat[i + n] == c and n < 16:
            n += 1
        out.append(((n - 1) << 4) | c)
        i += n
    return out


def rgb565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    if sys.argv[1] == "preview":
        d = Path(sys.argv[2])
        d.mkdir(parents=True, exist_ok=True)
        z = 3
        cols = 4
        cw, ch = W * z * 2 + 30, H * z + 40
        rows = (len(PICS) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cw + 10, rows * ch + 10), (0, 0, 0))
        dr = ImageDraw.Draw(sheet)
        for i, (key, name, fn) in enumerate(PICS):
            x, y = 10 + (i % cols) * cw, 10 + (i // cols) * ch
            for h in (0, 1):
                im = render(fn, h)
                sheet.paste(im.resize((W * z, H * z), Image.NEAREST), (x + h * (W * z + 6), y), im.resize((W * z, H * z), Image.NEAREST))
            dr.text((x, y + H * z + 6), f"{key}  -  {name}", fill=(220, 220, 220))
            dr.text((x + W * z + 6, y + H * z + 22), "(joue)", fill=(150, 150, 150))
            dr.text((x, y + H * z + 22), "(repos)", fill=(150, 150, 150))
        sheet.save(d / "instruments.png")
        return
    if sys.argv[1] == "header":
        lines = [f"/* generated by tools/gen_inst.py: RAVE FM 2's instruments (the EDIT page's picture), {W} x {H}, 2 frames"
                 " (at rest, hit), shown x2 */", f"#define RFM_INST_W {W}", f"#define RFM_INST_H {H}",
                 f"#define RFM_NINST {len(PICS)}"]
        total = 0
        for i, (key, name, fn) in enumerate(PICS):
            lines.append(f"#define RFM_INST_{key} {i}")
        offs = []
        for i, (key, name, fn) in enumerate(PICS):
            pal, idx = quant([render(fn, 0), render(fn, 1)])
            r0, r1 = rle(idx[0]), rle(idx[1])
            offs.append((0, len(r0)))
            data = r0 + r1
            total += len(data) + 32
            lines.append(f"static const uint16_t RFM_INST_PAL_{i}[16] = {{{', '.join(f'0x{rgb565(c):04X}' for c in pal)}}};")
            lines.append(f"static const uint8_t RFM_INST_RLE_{i}[{len(data)}] = {{")
            for k in range(0, len(data), 32):
                lines.append("    " + ", ".join(str(v) for v in data[k:k + 32]) + ",")
            lines.append("};")
        lines.append("static const uint16_t *const RFM_INST_PAL[RFM_NINST] = {" + ", ".join(f"RFM_INST_PAL_{i}" for i in range(len(PICS))) + "};")
        lines.append("static const uint8_t *const RFM_INST_RLE[RFM_NINST] = {" + ", ".join(f"RFM_INST_RLE_{i}" for i in range(len(PICS))) + "};")
        lines.append("static const uint16_t RFM_INST_OFF[RFM_NINST][2] = {" + ", ".join(f"{{{a}, {b}}}" for a, b in offs) + "};")
        lines.append(f"/* {total} bytes */")
        Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
        print(total, "bytes")


if __name__ == "__main__":
    main()
