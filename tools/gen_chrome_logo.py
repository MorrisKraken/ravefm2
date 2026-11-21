#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's name on the boot screen, lettered after the 90s hardcore chrome wordmarks: tall condensed capitals,
thick main strokes and hairline ones, in 80s chrome (a blue sky down to a white horizon, a dark line, warm ground
under it), a dark edge. Original letters drawn here (no font file, no logo copied).
  gen_chrome_logo.py preview DIR      PNG previews
  gen_chrome_logo.py header OUT.h     the C data: 16 colours, run-length (as the boot picture)"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
import numpy as np

Z = 8                 # drawn 8 times as big, then brought down
CAP = 100             # the capitals' height, in design units


ST = 11              # the stroke: one weight for all (the deco letters' even strokes)


def disk(X, Y, cx, cy, r):
    return (X - cx) ** 2 + (Y - cy) ** 2 <= r * r


def ell(X, Y, x0, y0, x1, y1):
    cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    return ((X - cx) / rx) ** 2 + ((Y - cy) / ry) ** 2 <= 1


def rect(X, Y, x0, y0, x1, y1):
    return (X >= x0) & (X < x1) & (Y >= y0) & (Y < y1)


def letter(ch, X, Y):
    """the letter as a mask over the design grid X, Y (units: the cap height is 100); and its width"""
    if ch == "R":                                     # a stem, a round bowl, the leg kicked out in a curve
        w = 46
        bowl = (rect(X, Y, 0, 0, 22, 56) | ell(X, Y, 0, 0, 44, 56)) & ~((rect(X, Y, ST, ST, 22, 56 - ST) | ell(X, Y, ST - 8, ST, 44 - ST, 56 - ST)) & (X >= ST))
        stem = rect(X, Y, 0, 0, ST, 100)
        t = np.clip((Y - 44) / 56, 0, 1)              # the leg: its middle runs (22, 44) -> (38, 100), bowing out
        mid = 22 + 16 * t + 6 * np.sin(t * np.pi)
        leg = (Y >= 44) & (Y < 100) & (np.abs(X - mid) <= ST / 2 + 0.5)
        return bowl | stem | leg, w
    if ch in "AV":                                    # A: shoulders rounding up to a flat top; V: the same upside down
        w = 46
        yy = Y if ch == "A" else 100 - Y
        sh = 34                                       # (the shoulders' height)
        half = 7 + (w / 2 - 7) * np.sqrt(np.clip(yy / sh, 0, 1))
        hi = (w / 2 - ST) * np.sqrt(np.clip((yy - ST) / (sh - 4), 0, 1))
        outer = (np.abs(X - w / 2) <= half) & (Y >= 0) & (Y < 100)
        inner = (np.abs(X - w / 2) < hi) & (yy > ST)
        if ch == "A":
            inner &= ~rect(X, Y, 0, 64, w, 64 + ST - 2)
        return outer & ~inner, w
    if ch in "EF":                                    # a stem, short arms, the middle one shorter
        w = 34
        m = rect(X, Y, 0, 0, ST, 100) | rect(X, Y, 0, 0, w, ST - 2) | rect(X, Y, 0, 44, w - 6, 44 + ST - 2)
        if ch == "E":
            m |= rect(X, Y, 0, 100 - ST + 2, w, 100)
        return m, w
    if ch == "M":                                     # flat on top, round shoulders, a stem down the middle
        w = 58
        rr = 20
        def rtop(x0, x1, y0, r):                      # a rectangle whose top corners are rounded
            m = rect(X, Y, x0, y0, x1, 100)
            m &= ~((X < x0 + r) & (Y < y0 + r) & ~disk(X, Y, x0 + r, y0 + r, r))
            m &= ~((X > x1 - r) & (Y < y0 + r) & ~disk(X, Y, x1 - r, y0 + r, r))
            return m
        outer = rtop(0, w, 0, rr)
        inner = rtop(ST, w - ST, ST, rr - ST + 2)
        mid = rect(X, Y, w / 2 - ST / 2, 0, w / 2 + ST / 2, 100)
        return (outer & ~inner) | mid, w
    return np.zeros_like(X, bool), 22


def draw_word(word, gap=6):
    widths = [letter(ch, np.zeros((1, 1)), np.zeros((1, 1)))[1] for ch in word]
    total = sum(widths) + gap * (len(word) - 1)
    pad = 8
    Wd, Hd = total + 2 * pad, 100 + 2 * pad
    ys, xs = np.mgrid[0:Hd * Z, 0:Wd * Z].astype(np.float32) / Z - pad
    m = np.zeros(ys.shape, bool)
    x = 0
    for ch, w in zip(word, widths):
        mm, _ = letter(ch, xs - x, ys)
        m |= mm & (xs - x >= -4) & (xs - x < w + 4)
        x += w + gap
    return Image.fromarray((m * 255).astype(np.uint8)), pad


def chrome(mask_img, pad, out_h):
    """the chrome of the 90s wordmark, in the bat's violets: a sky down to a white horizon, a ridge of dark hills under
    it, then deep violet up to a lighter foot; a thin light edge, a glowing magenta-violet rim on the right"""
    from scipy import ndimage as nd
    m = np.asarray(mask_img, np.float32) / 255
    H, W = m.shape
    top, bot = pad * Z, (pad + 100) * Z
    yy = np.arange(H)[:, None].astype(np.float32)
    xx = np.arange(W)[None, :].astype(np.float32)
    y = np.broadcast_to((yy - top) / (bot - top), (H, W))
    def mix(a, b, t):
        t = np.clip(t, 0, 1)[..., None]
        return np.array(a, np.float32) * (1 - t) + np.array(b, np.float32) * t
    hor = 0.48 + 0.012 * np.sin(xx / (Z * 6.0)) + 0.008 * np.sin(xx / (Z * 2.3) + 1)   # the hills' ridge
    hor = np.broadcast_to(hor, (H, W))
    sky = mix((90, 40, 170), (196, 160, 255), y / 0.3)
    sky = np.where((y > 0.3)[..., None], mix((196, 160, 255), (252, 248, 255), (y - 0.3) / (hor - 0.3)), sky)
    ground = mix((22, 6, 44), (70, 26, 130), (y - hor) / 0.3)
    ground = np.where((y > hor + 0.3)[..., None], mix((70, 26, 130), (168, 120, 245), (y - hor - 0.3) / (1 - hor - 0.3)), ground)
    col = np.where((y < hor)[..., None], sky, ground)
    inside = m > 0.5
    edge = nd.binary_dilation(inside, iterations=int(Z * 0.9)) & ~inside
    rim = nd.binary_dilation(inside, structure=np.ones((1, 3)), iterations=int(Z * 1.4))
    rim = np.roll(rim, int(Z * 1.4), axis=1) & ~inside & ~edge
    rim = rim | (np.roll(edge, int(Z * 1.2), axis=1) & ~inside & ~edge)
    rgb = np.zeros((H, W, 3), np.float32)
    rgb = np.where(rim[..., None], np.array((200, 50, 230), np.float32), rgb)
    rgb = np.where(edge[..., None], np.array((236, 222, 255), np.float32), rgb)
    rgb = np.where(inside[..., None], col, rgb)
    a = (inside | edge | rim).astype(np.float32)
    im = Image.fromarray(rgb.astype(np.uint8))
    al = Image.fromarray((a * 255).astype(np.uint8))
    w = round(W * out_h / H)
    return im.resize((w, out_h), Image.LANCZOS), al.resize((w, out_h), Image.LANCZOS)


def logo(out_h=72):
    m, pad = draw_word("RAVE FM")
    return chrome(m, pad, out_h)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    if sys.argv[1] == "preview":
        d = Path(sys.argv[2]); d.mkdir(parents=True, exist_ok=True)
        rgb, al = logo(60)
        bg = Image.new("RGB", rgb.size, (0, 0, 0)); bg.paste(rgb, (0, 0), al)
        bg.save(d / "logo.png")
        bg.resize((bg.width * 4, bg.height * 4), Image.NEAREST).save(d / "logo_x4.png")


if __name__ == "__main__":
    main()
