# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's demo film, take 2: the PC simulator's recordings (script.py's sessions, RFM_FILM) filmed as one take -
a camera that follows the hands over the FM-1 (drawn, no maker's marks), a pop-up circle on every key, button and
knob touched with what it is, the LEDs as the firmware lights them, few words (a small title per moment, a chapter
title over the picture), cross-fades between the sessions, an intro and an outro. 1920 x 1080, 30 fps.
   python3 film2.py WORKDIR OUT.mp4 [--range a,b] [--preview f,f,...] [--audio OUT.raw]"""
import argparse, json, math, os, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

W, H, FPS = 1920, 1080, 30
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
FD = os.path.join(ROOT, "assets/fonts")
_fc = {}
def F(name, size):
    k = (name, size)
    if k not in _fc: _fc[k] = ImageFont.truetype(os.path.join(FD, name), size)
    return _fc[k]
def FI(size, w=600):
    k = ("inter", size, w)
    if k not in _fc:
        f = ImageFont.truetype(os.path.join(FD, "InterTight[wght].ttf"), size)
        try: f.set_variation_by_axes([w])
        except Exception: pass
        _fc[k] = f
    return _fc[k]
ACC = (214, 255, 64)
WHITE_T = (250, 250, 246)

# ---- the FM-1's layout in units (1000 x 624), as film.py
KNOBS = ["MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"]
KPOS = [[105, 100, 24], [215, 100, 24], [105, 205, 24], [215, 205, 24], [610, 100, 22], [714, 100, 22], [818, 100, 22], [922, 100, 22]]
KNOB_SAY = {"SELECT": "SELECT", "ALGORITHM": "TRACK", "PRESETS": "PRESETS"}
ENC_KNOB = {0: "SELECT", 1: "ALGORITHM", 2: "KNOB 1", 3: "KNOB 2", 4: "KNOB 3", 5: "KNOB 4", 6: "PRESETS"}
BNAME = ["OCT-", "OCT+", "FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
ROW1, ROW2 = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO"], ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
TRAYS = [[50, 250, 220, 50], [568, 160, 400, 140]]
OCT = [[60, 258, 90, 34], [170, 258, 90, 34]]
GRID = [582, 174, 54, 48, 63, 62]
KEYBED = [36, 332, 928, 256]
SCREEN = [292, 44, 256]
WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]
DRUMS = ["BD", "SD", "CP", "CH", "OH", "MT", "RS", "CR", "LT", "HT", "RD"]
NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
BTN = {"OCT-": OCT[0], "OCT+": OCT[1]}
for i in range(6):
    BTN[ROW1[i]] = [GRID[0] + i * GRID[4], GRID[1], GRID[2], GRID[3]]
    BTN[ROW2[i]] = [GRID[0] + i * GRID[4], GRID[1] + GRID[5], GRID[2], GRID[3]]
_pitch = (KEYBED[2] - 24) / 16
KW, KH = _pitch * 0.62, KEYBED[3] * 0.40
KEYS = {}
for i, k in enumerate(WHITE): KEYS[k] = (KEYBED[0] + 12 + (i + 0.5) * _pitch, KEYBED[1] + KEYBED[3] * 0.74)
for j, a in enumerate(BLACK_AFTER): KEYS[BLACK[j]] = (KEYBED[0] + 12 + (a + 1) * _pitch, KEYBED[1] + KEYBED[3] * 0.26)

# ---- the canvas: the panel drawn at C px a unit, M px around it
C, M = 3.0, 260
CW, CH = int(1000 * C + 2 * M), int(624 * C + 2 * M)
def cp(x, y): return M + x * C, M + y * C
def crect(x, y, w, h): a, b = cp(x, y); return [a, b, a + w * C, b + h * C]


def lcd(fr):
    x = fr.astype(np.uint32)
    return np.stack([((x >> 11) & 31) * 255 // 31, ((x >> 5) & 63) * 255 // 63, (x & 31) * 255 // 31], -1).astype(np.uint8)


def vgrad(w, h, top, bot):
    t = np.linspace(0, 1, h)[:, None, None]
    a = np.array(top, float)[None, None] * (1 - t) + np.array(bot, float)[None, None] * t
    return Image.fromarray(np.repeat(a, w, 1).astype(np.uint8))


def rr_mask(w, h, r, ss=3):
    m = Image.new("L", (w * ss, h * ss), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w * ss - 1, h * ss - 1], radius=r * ss, fill=255)
    return m.resize((w, h), Image.LANCZOS)


def circle_mask(d, ss=3):
    m = Image.new("L", (d * ss, d * ss), 0)
    ImageDraw.Draw(m).ellipse([0, 0, d * ss - 1, d * ss - 1], fill=255)
    return m.resize((d, d), Image.LANCZOS)


def shadow(im, box, radius, blur, alpha, off=(0, 0)):
    x0, y0, x1, y1 = [int(v) for v in box]
    pad = blur * 3
    m = Image.new("L", (x1 - x0 + 2 * pad, y1 - y0 + 2 * pad), 0)
    ImageDraw.Draw(m).rounded_rectangle([pad, pad, pad + x1 - x0, pad + y1 - y0], radius=radius, fill=int(255 * alpha))
    m = m.filter(ImageFilter.GaussianBlur(blur))
    im.paste((0, 0, 0), (x0 - pad + off[0], y0 - pad + off[1]), m)


# ---- the sprites
SPR = {}


def button_sprites(n):
    x, y, w, h = BTN[n]
    pw, ph = int(w * C), int(h * C)
    r = int(9 * C)
    mask = rr_mask(pw, ph, r)
    f = FI(int(12 * C), 700)
    def make(top, bot, ink, hi):
        im = vgrad(pw, ph, top, bot)
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([1, 1, pw - 2, ph - 2], radius=r, outline=hi, width=3)
        d.text((pw / 2, ph / 2 + 2), n, font=f, fill=ink, anchor="mm")
        return im
    lit_col = (255, 240, 205)
    if n == "PLAY": lit_col = (96, 232, 140)
    if n == "REC": lit_col = (255, 84, 72)
    unlit = make((66, 66, 71), (44, 44, 48), (255, 96, 86) if n == "REC" else (200, 200, 205), (84, 84, 90))
    lit = make(tuple(min(255, int(c * 1.0)) for c in lit_col), tuple(int(c * 0.82) for c in lit_col), (28, 28, 30),
               tuple(min(255, c + 30) for c in lit_col))
    pad = 60
    g = Image.new("L", (pw + 2 * pad, ph + 2 * pad), 0)
    ImageDraw.Draw(g).rounded_rectangle([pad, pad, pad + pw, pad + ph], radius=r, fill=255)
    g = g.filter(ImageFilter.GaussianBlur(26))
    SPR[n] = dict(pos=(int(cp(x, y)[0]), int(cp(x, y)[1])), mask=mask, unlit=unlit, lit=lit, col=lit_col, glow=g, pad=pad)


def key_sprites():
    pw, ph = int(KW * C), int(KH * C)
    slot = Image.new("RGB", (pw, ph), (12, 12, 14))
    m = rr_mask(pw, ph, pw // 2)
    sw = max(6, int(pw * 0.17))
    sh = int((KH - KW * 0.9) * C)
    strip_m = rr_mask(sw, sh, sw // 2)
    pad = 40
    g = Image.new("L", (sw + 2 * pad, sh + 2 * pad), 0)
    ImageDraw.Draw(g).rounded_rectangle([pad, pad, pad + sw, pad + sh], radius=sw // 2, fill=255)
    g = g.filter(ImageFilter.GaussianBlur(14))
    SPR["key"] = dict(pw=pw, ph=ph, sw=sw, sh=sh, strip=strip_m, glow=g, pad=pad)


def base():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".panel_cache.png")
    for n in BNAME: button_sprites(n)
    key_sprites()
    if os.path.exists(path):
        return Image.open(path).convert("RGB")
    # the desk: dark, a light from above
    yy, xx = np.mgrid[0:CH, 0:CW]
    r = np.sqrt(((xx - CW * 0.5) / (CW * 0.62)) ** 2 + ((yy - CH * 0.42) / (CH * 0.70)) ** 2)
    v = np.clip(1 - r, 0, 1) ** 1.6
    bg = np.stack([8 + 22 * v, 8 + 22 * v, 10 + 25 * v], -1)
    rng = np.random.default_rng(1)
    bg += rng.normal(0, 1.6, bg.shape)
    im = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8))
    # the body: its shadow, a gradient, an edge of light
    body = crect(4, 4, 992, 616)
    shadow(im, body, int(40 * C), 70, 0.85, (0, 40))
    bw, bh = int(body[2] - body[0]), int(body[3] - body[1])
    im.paste(vgrad(bw, bh, (46, 46, 50), (25, 25, 28)), (int(body[0]), int(body[1])), rr_mask(bw, bh, int(40 * C)))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(body, radius=40 * C, outline=(70, 70, 76), width=3)
    d.rounded_rectangle([body[0] + 4, body[1] + 4, body[2] - 4, body[3] - 4], radius=40 * C - 4, outline=(34, 34, 37), width=2)
    for t in TRAYS + [KEYBED]:
        b = crect(*t)
        tw, th = int(b[2] - b[0]), int(b[3] - b[1])
        im.paste(vgrad(tw, th, (14, 14, 15), (22, 22, 24)), (int(b[0]), int(b[1])), rr_mask(tw, th, int(16 * C)))
        d.rounded_rectangle(b, radius=16 * C, outline=(52, 52, 56), width=2)
    # the screen's bezel
    sx, sy, ss = SCREEN
    b = crect(sx, sy, ss, ss)
    shadow(im, b, int(20 * C), 14, 0.7, (0, 8))
    d.rounded_rectangle(b, radius=20 * C, fill=(6, 6, 7), outline=(60, 60, 66), width=3)
    # the knobs
    fl = FI(int(11 * C), 600)
    for n, (x, y, r) in zip(KNOBS, KPOS):
        cx, cy = cp(x, y)
        R = r * C
        d.ellipse([cx - R - 6 * C, cy - R - 6 * C, cx + R + 6 * C, cy + R + 6 * C], fill=(15, 15, 16))
        sh = Image.new("L", (int(2 * R + 80), int(2 * R + 80)), 0)
        ImageDraw.Draw(sh).ellipse([40, 40, 40 + 2 * R, 40 + 2 * R], fill=200)
        sh = sh.filter(ImageFilter.GaussianBlur(10))
        im.paste((0, 0, 0), (int(cx - R - 40), int(cy - R - 40 + 10)), sh)
        D = int(2 * R)
        k = vgrad(D, D, (92, 92, 98), (36, 36, 40))
        kd = ImageDraw.Draw(k)
        for a in range(0, 360, 10):          # the knurl
            ra = math.radians(a)
            kd.line([D / 2 + math.cos(ra) * (R - 2), D / 2 + math.sin(ra) * (R - 2), D / 2 + math.cos(ra) * (R - 9),
                     D / 2 + math.sin(ra) * (R - 9)], fill=(24, 24, 27), width=3)
        cap = int(R * 1.3)
        k.paste(vgrad(cap, cap, (40, 40, 44), (70, 70, 76)), ((D - cap) // 2, (D - cap) // 2), circle_mask(cap))
        im.paste(k, (int(cx - R), int(cy - R)), circle_mask(D))
        d.text((cx, cy - (r + 13) * C), n, font=fl, fill=(170, 170, 176), anchor="ms")
    # the keys' slots
    ks = SPR["key"]
    for kk, (kx, ky) in KEYS.items():
        a, b = cp(kx - KW / 2, ky - KH / 2)
        im.paste((10, 10, 11), (int(a), int(b)), rr_mask(ks["pw"], ks["ph"], ks["pw"] // 2))
        d.rounded_rectangle([a, b, a + ks["pw"], b + ks["ph"]], radius=ks["pw"] // 2, outline=(56, 56, 60), width=2)
    im.save(path)
    return im


def draw_panel(cv, sg, i, angle):
    """the frame's live parts on a copy of the canvas: the screen, the LEDs, the knobs' marks"""
    b, keys, leds, enc, dirs = sg.st[i]
    sx, sy, ss = SCREEN
    x0, y0 = cp(sx + 8, sy + 8)
    scr = np.repeat(np.repeat(lcd(sg.scr[i]), 3, 0), 3, 1)
    cv.paste(Image.fromarray(scr), (int(x0), int(y0)))
    for bi, n in enumerate(BNAME):
        v = int(leds[bi]); s = SPR[n]
        if v & 1:
            cv.paste(s["col"], (s["pos"][0] - s["pad"], s["pos"][1] - s["pad"]), s["glow"].point(lambda q: q * 0.75))
            cv.paste(s["lit"], s["pos"], s["mask"])
        else:
            cv.paste(s["unlit"], s["pos"], s["mask"])
            if v & 2:
                cv.paste(s["col"], (s["pos"][0] - s["pad"], s["pos"][1] - s["pad"]), s["glow"].point(lambda q: q * 0.22))
    ks = SPR["key"]
    for kk in range(27):
        v = int(leds[14 + kk])
        kx, ky = KEYS[kk]
        a, bb = cp(kx - ks["sw"] / C / 2, ky - KH / 2 + KW * 0.45)
        if v & 1:
            cv.paste((255, 255, 246), (int(a) - ks["pad"], int(bb) - ks["pad"]), ks["glow"].point(lambda q: q * 0.8))
            cv.paste((255, 255, 250), (int(a), int(bb)), ks["strip"])
        elif v & 2:
            cv.paste((120, 120, 118), (int(a), int(bb)), ks["strip"])
        else:
            cv.paste((44, 44, 48), (int(a), int(bb)), ks["strip"])
    d = ImageDraw.Draw(cv)
    for n, (x, y, r) in zip(KNOBS, KPOS):
        cx, cy = cp(x, y)
        a = math.radians(-135 + 270 * 0.68 if n == "MASTER" else angle.get(n, 0.0))
        R = r * C
        d.line([cx + math.sin(a) * R * 0.25, cy - math.cos(a) * R * 0.25, cx + math.sin(a) * R * 0.58,
                cy - math.cos(a) * R * 0.58], fill=(236, 236, 238), width=7)


# ---- a recording
class Seg:
    def __init__(s, d, js):
        s.d = d
        s.scr = np.memmap(os.path.join(d, "screen.raw"), dtype="<u2", mode="r").reshape(-1, 240, 240)
        s.st = []
        for l in open(os.path.join(d, "state.txt")):
            v = l.split()
            s.st.append((int(v[1], 16), int(v[2], 16), v[3], int(v[4], 16), [int(x) for x in v[5:12]]))
        s.n = min(len(s.scr), len(s.st))
        info = json.load(open(os.path.join(d, js)))["marks"]
        s.marks = []
        for l in open(os.path.join(d, "marks.txt")):
            f, m = l.split()
            s.marks.append((int(f), dict(info[m])))
        s.marks.sort(key=lambda x: x[0])
        s.events()

    def events(s):
        """the touches: (what, start, end, label) - buttons, keys, knobs (a knob's turns close together: one)"""
        ev, held = [], {}
        lbl_pending = None
        ctx = ""
        mi = 0
        s.angles = []
        ang = {n: 0.0 for n in KNOBS}
        kn_run = {}
        for i in range(s.n):
            while mi < len(s.marks) and s.marks[mi][0] <= i:
                m = s.marks[mi][1]
                if m["kind"] == "lbl": lbl_pending = (i, m["title"])
                if m["kind"] == "ctx": ctx = m["title"]
                mi += 1
            b, keys, leds, enc, dirs = s.st[i]
            now = {}
            for bi in range(14):
                if b >> bi & 1: now[("b", BNAME[bi])] = 1
            for kk in range(27):
                if keys >> kk & 1: now[("k", kk)] = 1
            if i < 2:                     # (what was done before the film went on: not a touch seen)
                for k in now: held.setdefault(k, None)
                now = {}
            for k in now:
                if k not in held:
                    lab = None
                    if k[0] == "b": lab = k[1]
                    else:
                        kk = k[1]
                        fx = b >> 2 & 1
                        if lbl_pending and i - lbl_pending[0] < 25: lab = lbl_pending[1]; lbl_pending = None
                        elif fx: lab = None
                        elif ctx == "steps": lab = f"STEP {WHITE.index(kk) + 1}" if kk in WHITE else None
                        elif ctx == "drum": lab = f"STEP {WHITE.index(kk) + 1}" if kk in WHITE else DRUMS[BLACK.index(kk)]
                        elif ctx == "notes": m = 53 + kk; lab = f"{NOTES[m % 12]}{m // 12 - 1}"
                    held[k] = [k, i, None, lab]
            for k in list(held):
                if k not in now:
                    if held[k] is not None: held[k][2] = i; ev.append(tuple(held[k]))
                    del held[k]
            for e in range(7):
                if enc >> e & 1 and i >= 2:
                    n = ENC_KNOB[e]
                    ang[n] += (1 if dirs[e] > 0 else -1) * 14
                    r = kn_run.get(n)
                    if r and i - r[2] <= 8: r[2] = i; r[4] = dirs[e]
                    else:
                        if r: ev.append((("n", n), r[1], r[2] + 1, KNOB_SAY.get(n, n), r[4]))
                        kn_run[n] = [("n", n), i, i, None, dirs[e]]
            s.angles.append(dict(ang))
        for k, v in held.items():
            if v is not None: v[2] = s.n; ev.append(tuple(v))
        for n, r in kn_run.items(): ev.append((("n", n), r[1], r[2] + 1, KNOB_SAY.get(n, n), r[4]))
        s.ev = sorted(ev, key=lambda e: e[1])


def ctl_rect(k):
    """a control's place in units (x0, y0, x1, y1)"""
    t, n = k
    if t == "b":
        x, y, w, h = BTN[n]; return (x, y, x + w, y + h)
    if t == "n":
        x, y, r = KPOS[KNOBS.index(n)]; return (x - r - 10, y - r - 30, x + r + 10, y + r + 10)
    return (KEYBED[0], KEYBED[1], KEYBED[0] + KEYBED[2], KEYBED[1] + KEYBED[3])


def ctl_center(k):
    t, n = k
    if t == "b":
        x, y, w, h = BTN[n]; return x + w / 2, y + h / 2, max(w, h) * 0.62
    if t == "n":
        x, y, r = KPOS[KNOBS.index(n)]; return x, y, r + 14
    x, y = KEYS[n]; return x, y, 24


FULL = (500, 312, 1150)                         # the whole panel: its centre, the camera's width (units)


def camera_path(sg, k0, n, init=None):
    """the camera: where it looks (units: centre x, y, width) for each frame - the screen and what the hands do"""
    path = []
    x, y, w = init or FULL
    vx = vy = vw = 0.0
    om, omw = 4.2, 3.4
    dt = 1.0 / FPS
    act = [e for e in sg.ev]
    ei = 0
    live = []
    idle = 0
    for i in range(k0, k0 + n):
        while ei < len(act) and act[ei][1] <= i:
            live.append(act[ei]); ei += 1
        live = [e for e in live if (e[2] is None or e[2] + 40 > i)]
        b = sg.st[i][0]
        fx = b >> 2 & 1
        rects = [ctl_rect(e[0]) for e in live]
        if fx: rects.append(ctl_rect(("k", 0))); rects.append(ctl_rect(("b", "FX")))
        if rects:
            idle = 0
            sx, sy, ss = SCREEN
            r = [sx - 30, sy - 25, sx + ss + 30, sy + ss + 25]
            for q in rects:
                r = [min(r[0], q[0] - 40), min(r[1], q[1] - 30), max(r[2], q[2] + 40), max(r[3], q[3] + 30)]
            tw = max(r[2] - r[0], (r[3] - r[1]) * 16 / 9, 640)
            tx, ty = (r[0] + r[2]) / 2, (r[1] + r[3]) / 2
        else:
            idle += 1
            tx, ty, tw = (x, y, w) if idle < 50 else FULL
        tw = min(tw, FULL[2])
        th = tw * 9 / 16
        tx = min(max(tx, FULL[0] - (FULL[2] - tw) / 2), FULL[0] + (FULL[2] - tw) / 2)
        ty = min(max(ty, -40 + th / 2), 664 - th / 2) if th < 704 else FULL[1]
        ax = om * om * (tx - x) - 2 * om * vx; vx += ax * dt; x += vx * dt
        ay = om * om * (ty - y) - 2 * om * vy; vy += ay * dt; y += vy * dt
        aw = omw * omw * (tw - w) - 2 * omw * vw; vw += aw * dt; w += vw * dt
        t = i / FPS     # (a hand-held camera's breathing)
        path.append((x + 2.0 * math.sin(t * 0.7), y + 1.5 * math.sin(t * 0.53 + 1), w * (1 + 0.004 * math.sin(t * 0.31))))
    return path


def cam_box(c):
    x, y, w = c
    h = w * 9 / 16
    a, b = cp(x - w / 2, y - h / 2)
    return (a, b, a + w * C, b + h * C)


def to_out(c, ux, uy):
    x, y, w = c
    s = W / w
    return (ux - (x - w / 2)) * s, (uy - (y - w * 9 / 32)) * s, s


# ---- pop-ups
def ease_back(t):
    t = min(1.0, max(0.0, t)); c = 1.9
    return 1 + (c + 1) * (t - 1) ** 3 + c * (t - 1) ** 2


_pill = {}
def pill(text):
    if text in _pill: return _pill[text]
    f = F("ChakraPetch-Bold.ttf", 38)
    tw = int(f.getlength(text))
    w, h = tw + 44, 62
    ss = 3
    im = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * ss - 1, h * ss - 1], radius=h * ss // 2, fill=(14, 14, 16, 235), outline=ACC + (255,), width=3 * ss)
    im = im.resize((w, h), Image.LANCZOS)
    ImageDraw.Draw(im).text((w / 2, h / 2 + 1), text, font=f, fill=ACC + (255,), anchor="mm")
    _pill[text] = im
    return im


def paste_rgba(out, im, x, y, alpha=1.0):
    if alpha <= 0.01: return
    if alpha < 0.999:
        a = im.split()[3].point(lambda v: int(v * alpha))
        im = im.copy(); im.putalpha(a)
    out.paste(im, (int(round(x)), int(round(y))), im)


def ring_sprite(R, disc_a, ring_w, ring_a, rip_r, rip_w, rip_a, arc=None):
    """a pop-up: a disc, its ring, a ripple going out (R the sprite's half size, px)"""
    ss = 3
    D = int(2 * R) + 8
    im = Image.new("RGBA", (D * ss, D * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = D * ss / 2
    return im, d, c, ss, D


KEEPOUT = [0, 0]                                # (a title on screen: the pop-ups' labels keep off it)


def draw_touch(out, c, e, i):
    k, t0, t1, lab = e[0], e[1], e[2], e[3]
    ux, uy, ur = ctl_center(k)
    ox, oy, s = to_out(c, ux, uy)
    r = max(26.0, ur * s)
    age = i - t0
    held = t1 is None or i < t1
    rel = 0 if held else i - t1
    fade = 1.0 if held else max(0.0, 1 - rel / 10)
    if fade <= 0 and age > 16: return False
    pop = ease_back(age / 7)
    rip = min(1.0, age / 16)
    Rmax = r * 2.5
    ss = 3
    D = int(2 * Rmax) + 10
    im = Image.new("RGBA", (D * ss, D * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cc = D * ss / 2
    if k[0] == "n":
        # a knob: an arc around it, its way shown
        rr = r * pop * ss
        d.ellipse([cc - rr, cc - rr, cc + rr, cc + rr], outline=ACC + (int(90 * fade),), width=int(4 * ss))
        a0 = (i * 9) % 360 if e[4] > 0 else -(i * 9) % 360
        d.arc([cc - rr, cc - rr, cc + rr, cc + rr], a0, a0 + 110, fill=ACC + (int(255 * fade),), width=int(8 * ss))
        a = math.radians(a0 + 110 if e[4] > 0 else a0)
        hx, hy = cc + math.cos(a) * rr, cc + math.sin(a) * rr
        tng = a + (math.pi / 2 if e[4] > 0 else -math.pi / 2)
        L = 18 * ss
        p1 = (hx + math.cos(tng) * L, hy + math.sin(tng) * L)
        p2 = (hx + math.cos(a) * L * 0.6, hy + math.sin(a) * L * 0.6)
        p3 = (hx - math.cos(a) * L * 0.6, hy - math.sin(a) * L * 0.6)
        d.polygon([p1, p2, p3], fill=ACC + (int(255 * fade),))
    else:
        rr = r * pop * ss
        d.ellipse([cc - rr, cc - rr, cc + rr, cc + rr], fill=ACC + (int(70 * fade),), outline=ACC + (int(255 * fade),), width=int(5 * ss))
        if rip < 1:
            r2 = r * (1 + 1.3 * rip) * ss
            d.ellipse([cc - r2, cc - r2, cc + r2, cc + r2], outline=(255, 255, 255, int(200 * (1 - rip))), width=max(1, int(4 * ss * (1 - rip))))
    im = im.resize((D, D), Image.LANCZOS)
    out.paste(im, (int(ox - D / 2), int(oy - D / 2)), im)
    if lab:
        p = pill(lab)
        sc = ease_back(age / 6)
        pw, ph = max(1, int(p.width * sc)), max(1, int(p.height * sc))
        pp = p.resize((pw, ph), Image.BILINEAR) if (pw, ph) != p.size else p
        yy = oy - r - 22 - ph
        px = min(max(ox - pw / 2, 10), W - pw - 10)
        if yy < 20 or (KEEPOUT[0] and px < KEEPOUT[1] and yy < 300): yy = oy + r + 22
        paste_rgba(out, pp, px, yy, fade)
    return True


# ---- titles
_tag = {}
def tag_img(title, sub):
    k = (title, sub)
    if k in _tag: return _tag[k]
    ft, fs = F("Anton-Regular.ttf", 92), F("ChakraPetch-SemiBold.ttf", 36)
    tw = max(ft.getlength(title), fs.getlength(sub.upper()) if sub else 0)
    w, h = int(tw + 140), 220
    im = Image.new("RGBA", (w + 300, h), (0, 0, 0, 0))
    g = np.zeros((h, w + 300), np.uint8)
    g[:] = (np.clip(1 - np.linspace(0, 1, w + 300) ** 1.5, 0, 1) * 150).astype(np.uint8)[None]
    im.putalpha(Image.fromarray(g))
    d = ImageDraw.Draw(im)
    d.rectangle([56, 40, 64, 40 + (178 if sub else 120)], fill=ACC + (255,))
    d.text((90, 30), title, font=ft, fill=WHITE_T + (255,))
    if sub: d.text((92, 148), sub.upper(), font=fs, fill=ACC + (255,))
    _tag[k] = im
    return im


_chap = {}
def chap_img(title, sub):
    k = (title, sub)
    if k in _chap: return _chap[k]
    im = Image.new("RGBA", (W, H), (4, 4, 6, 175))
    d = ImageDraw.Draw(im)
    d.text((W / 2, 585), title, font=F("Anton-Regular.ttf", 170), fill=WHITE_T + (255,), anchor="ms")
    d.rectangle([W / 2 - 150, 622, W / 2 + 150, 629], fill=ACC + (255,))
    d.text((W / 2, 700), sub, font=FI(44, 500), fill=(200, 200, 206, 255), anchor="ms")
    _chap[k] = im
    return im


_vig = None
def vignette():
    global _vig
    if _vig is None:
        yy, xx = np.mgrid[0:H, 0:W]
        r = np.sqrt(((xx - W / 2) / (W * 0.62)) ** 2 + ((yy - H / 2) / (H * 0.62)) ** 2)
        v = np.clip(1.12 - 0.42 * r ** 2.2, 0.45, 1.0)
        _vig = Image.fromarray((np.stack([v, v, v], -1) * 255).astype(np.uint8))
    return _vig


XF = 18                                         # (the cross-fades, frames)


# ---- a clip's frame
class Clip:
    def __init__(s, kind, seg=None, k0=0, n=0, **kw):
        s.kind, s.seg, s.k0, s.n = kind, seg, k0, n
        s.__dict__.update(kw)
        if seg is not None and kind == "seg":
            pc = kw.get("prev_cam")       # (through the cross-fade: the last clip's camera, the pictures one on the other)
            if pc:
                s.cam = list(pc[-XF:]) + camera_path(seg, k0 + XF, n - XF, pc[-1])
            else:
                s.cam = camera_path(seg, k0, n)
            chf = [f for f, m in seg.marks if m["kind"] == "chap"]
            s.tags = []
            for f, m in seg.marks:          # (a title: after the chapter's)
                if m["kind"] in ("tag", "cap") and k0 <= f < k0 + n:
                    f2 = max([f] + [c + 72 for c in chf if c <= f < c + 72])
                    s.tags.append((f2, m))
            s.chaps = [(f, m) for f, m in seg.marks if m["kind"] == "chap" and k0 - 5 <= f < k0 + n]


def seg_frame(cv0, sg, i, cam, overlay=True, clip=None):
    cv = cv0.copy()
    draw_panel(cv, sg, i, sg.angles[i])
    out = cv.resize((W, H), Image.BILINEAR, box=cam_box(cam))
    out = ImageChops.multiply(out, vignette())
    if not overlay: return out
    out = out.convert("RGB")
    tagnow = None
    if clip is not None:
        cur = None
        for j, (f, m) in enumerate(clip.tags):
            if f <= i: cur = (f, m, clip.tags[j + 1][0] if j + 1 < len(clip.tags) else 10 ** 9)
        if cur and i < min(cur[0] + 120, cur[2]) + 12:
            tagnow = cur
    KEEPOUT[0] = 1 if tagnow else 0
    KEEPOUT[1] = tag_img(tagnow[1]["title"], tagnow[1].get("sub", "")).width - 250 if tagnow else 0
    for e in sg.ev:
        if e[1] > i: break
        if e[1] + 400 < i: continue
        if e[2] is not None and e[2] + 12 < i and e[1] + 18 < i: continue
        draw_touch(out, cam, e, i)
    if clip is not None:
        # the moment's title
        if tagnow:
            f, m, nxt = tagnow
            age = i - f
            end = min(f + 120, nxt)
            if i < end + 12:
                a = min(1.0, age / 9) * min(1.0, max(0.0, (end + 12 - i) / 12))
                im = tag_img(m["title"], m.get("sub", ""))
                paste_rgba(out, im, -30 * (1 - min(1.0, age / 9)) ** 2 + 0, 40, a)
        for f, m in clip.chaps:
            age = i - f
            if 0 <= age < 80:
                a = min(1.0, age / 9, (80 - age) / 12)
                ov = chap_img(m["title"], m.get("sub", ""))
                out = Image.alpha_composite(out.convert("RGBA"), Image.blend(Image.new("RGBA", (W, H), (0, 0, 0, 0)), ov, a)).convert("RGB")
    return out


def logo_img(sg, size):
    fr = lcd(sg.scr[45])
    return Image.fromarray(fr).crop((20, 20, 220, 220)).resize((size, size), Image.LANCZOS)


def special(clip, k, cv0, segs):
    t = k / FPS
    out = Image.new("RGB", (W, H), (0, 0, 0))
    if clip.kind == "intro":
        if t < clip.logo_s:
            lg = clip.logo
            a = min(1.0, t / 0.9) * min(1.0, (clip.logo_s - t) / 0.35)
            z = 0.9 + 0.1 * (1 - (1 - min(1.0, t / 1.4)) ** 3) + 0.02 * t
            l2 = lg.resize((int(lg.width * z), int(lg.height * z)), Image.LANCZOS)
            glow = l2.filter(ImageFilter.GaussianBlur(40))
            x, y = (W - l2.width) // 2, 60 - (l2.height - lg.height) // 2
            out.paste(ImageChops.multiply(glow, Image.new("RGB", glow.size, (int(255 * a * 0.9),) * 3)), (x, y))
            out = ImageChops.lighter(out, Image.new("RGB", (W, H)))
            o2 = out.copy(); o2.paste(l2, (x, y))
            out = Image.blend(out, ImageChops.lighter(out, o2), a)
            d = ImageDraw.Draw(out)
            if t > 1.3:
                b = min(1.0, (t - 1.3) / 0.5) * min(1.0, (clip.logo_s - t) / 0.35)
                dy = 20 * (1 - min(1.0, (t - 1.3) / 0.5)) ** 2
                d.text((W / 2, 800 + dy), "RAVE FM 2", font=F("Anton-Regular.ttf", 140), fill=tuple(int(c * b) for c in WHITE_T), anchor="ms")
            if t > 1.9:
                b = min(1.0, (t - 1.9) / 0.5) * min(1.0, (clip.logo_s - t) / 0.35)
                d.text((W / 2, 880), "HARDCORE FIRMWARE FOR THE FM-1", font=F("ChakraPetch-SemiBold.ttf", 42), fill=tuple(int(c * b) for c in ACC), anchor="ms")
        else:
            per = (clip.n / FPS - clip.logo_s) / len(clip.shots)
            j = min(len(clip.shots) - 1, int((t - clip.logo_s) / per))
            tt = (t - clip.logo_s) - j * per
            w1, w2, seg, fi, cam = clip.shots[j]
            sg = segs[seg]
            x, y, w = cam
            cam2 = (x, y, w * (1 - 0.06 * tt / per))
            out = seg_frame(cv0, sg, fi + int(tt * FPS), cam2, overlay=False)
            out = Image.blend(out, Image.new("RGB", (W, H), (0, 0, 0)), 0.35)
            d = ImageDraw.Draw(out)
            sl = 40 * (1 - min(1.0, tt / 0.18))
            d.text((110 - sl, 900), w1, font=F("Anton-Regular.ttf", 170), fill=WHITE_T, anchor="ls")
            d.text((116 - sl, 970), w2, font=F("ChakraPetch-SemiBold.ttf", 44), fill=ACC, anchor="ls")
            fl = max(0.0, 1.0 - tt / 0.12)
            if fl > 0: out = Image.blend(out, Image.new("RGB", (W, H), (255, 255, 255)), fl * 0.5)
    else:
        lg = clip.logo
        a = min(1.0, t / 0.8)
        glow = lg.filter(ImageFilter.GaussianBlur(40))
        x, y = (W - lg.width) // 2, 90
        out.paste(ImageChops.multiply(glow, Image.new("RGB", glow.size, (int(230 * a),) * 3)), (x, y))
        o2 = out.copy(); o2.paste(lg, (x, y))
        out = Image.blend(out, ImageChops.lighter(out, o2), a)
        d = ImageDraw.Draw(out)
        b = min(1.0, max(0.0, (t - 0.4) / 0.6))
        d.text((W / 2, 740), "RAVE FM 2", font=F("Anton-Regular.ttf", 140), fill=tuple(int(c * b) for c in WHITE_T), anchor="ms")
        d.text((W / 2, 820), "6 TRACKS · 75 KICKS · REAL BREAKS · THE 909 · RAVE LEADS · LIVE FX", font=F("ChakraPetch-SemiBold.ttf", 36), fill=tuple(int(c * b) for c in ACC), anchor="ms")
        d.text((W / 2, 900), "Free firmware for the FM-1 synth", font=FI(36, 500), fill=tuple(int(c * b) for c in (175, 175, 182)), anchor="ms")
        fo = max(0.0, (t - (clip.n / FPS - 1.5)) / 1.5)
        if fo > 0: out = Image.blend(out, Image.new("RGB", (W, H), (0, 0, 0)), min(1.0, fo))
    return out


def clip_frame(clip, k, cv0, segs):
    if clip.kind == "seg":
        i = clip.k0 + k
        return seg_frame(cv0, clip.seg, i, clip.cam[k], clip=clip)
    return special(clip, k, cv0, segs)


# ---- the plan
def first_mark(sg, kind):
    for f, m in sg.marks:
        if m["kind"] == kind: return f
    return None


def build(work):
    segs = {"a": Seg(work + "/p1", "part1.json")}
    for x in "bcde":
        segs[x] = Seg(work + f"/s{x}", f"show_{x}.json")
    clips = []
    sa = segs["a"]
    shots = [("75 KICKS", "GABBER · HARDCORE · UPTEMPO · DOOM", "a", 175, (430, 160, 700)),
             ("REAL BREAKS", "SLICED · LOOPED · IN TIME", "a", 840, (420, 170, 720)),
             ("THE 909", "EVERY PIECE, EVERY STEP", "a", 1075, (420, 170, 700)),
             ("LIVE FX", "RISERS · DROPS · ROLLS · FILTERS", "a", 3420, (500, 312, 1150))]
    intro = Clip("intro", n=int(8.6 * FPS), logo=logo_img(sa, 500), logo_s=4.6, shots=shots, audio=("a", 4150), afade=(0.4, 1.0))
    clips.append(intro)
    prev = None
    for x in "abcde":
        sg = segs[x]
        k0 = first_mark(sg, "start") or 0
        e = first_mark(sg, "end") or sg.n
        n = min(sg.n, e + 15) - k0
        c = Clip("seg", sg, k0, n, prev_cam=prev)    # (the camera goes on where the last one was)
        clips.append(c)
        prev = c.cam
    outro = Clip("outro", n=int(7.5 * FPS), logo=logo_img(sa, 480), audio=("c", 220), afade=(0.2, 3.5))
    clips.append(outro)
    t = 0
    for j, c in enumerate(clips):
        c.t0 = t if j == 0 else t - XF
        t = c.t0 + c.n
    total = t
    return segs, clips, total, XF


def frame_at(T, clips, XF, cv0, segs):
    on = [c for c in clips if c.t0 <= T < c.t0 + c.n]
    if len(on) == 1:
        c = on[0]; return clip_frame(c, T - c.t0, cv0, segs)
    a, b = on[0], on[1]
    fa, fb = clip_frame(a, T - a.t0, cv0, segs), clip_frame(b, T - b.t0, cv0, segs)
    x = (T - b.t0 + 1) / (XF + 1)
    x = x * x * (3 - 2 * x)
    return Image.blend(fa, fb, x)


def audio(segs, clips, total, XF, out):
    def seg_audio(sg):
        a = np.fromfile(os.path.join(sg.d, "audio.raw"), dtype="<i2").reshape(-1, 2).astype(np.float32) / 32768
        # (audio.raw: from the film's first frame, 1470 samples a frame)
        return a
    N = total * 1470
    mix = np.zeros((N + 1470, 2), np.float32)
    xs = XF * 1470
    for c in clips:
        n = c.n * 1470
        if c.kind == "seg":
            a = seg_audio(c.seg)[c.k0 * 1470:c.k0 * 1470 + n]
        else:
            src, f0 = c.audio
            a = seg_audio(segs[src])[f0 * 1470:f0 * 1470 + n].copy()
            fi, fo = int(c.afade[0] * 44100), int(c.afade[1] * 44100)
            a[:fi] *= np.linspace(0, 1, fi)[:, None]; a[-fo:] *= np.linspace(1, 0, fo)[:, None]
        a = np.pad(a, ((0, n - len(a)), (0, 0)))
        if c.t0 > 0: a[:xs] *= np.linspace(0, 1, xs)[:, None]
        if c.t0 + c.n < total: a[-xs:] *= np.linspace(1, 0, xs)[:, None]
        mix[c.t0 * 1470:c.t0 * 1470 + n] += a
    mix = mix[:N]
    mix *= 0.89 / max(1e-6, float(np.abs(mix).max()))
    (mix * 32767).astype("<i2").tofile(out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("work"); ap.add_argument("out")
    ap.add_argument("--preview"); ap.add_argument("--range"); ap.add_argument("--audio", action="store_true")
    a = ap.parse_args()
    segs, clips, total, XF = build(a.work)
    print("frames", total, "=", round(total / FPS, 1), "s", file=sys.stderr)
    if a.audio:
        audio(segs, clips, total, XF, a.out); sys.exit(0)
    cv0 = base()
    if a.preview:
        for T in [int(x) for x in a.preview.split(",")]:
            frame_at(T, clips, XF, cv0, segs).save(f"{a.out}-{T:05d}.jpg", quality=92)
        sys.exit(0)
    r0, r1 = (int(x) for x in a.range.split(",")) if a.range else (0, total)
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
                           "-pix_fmt", "yuv420p", a.out + ".part.mp4"], stdin=subprocess.PIPE)
    for T in range(r0, r1):
        ff.stdin.write(frame_at(T, clips, XF, cv0, segs).convert("RGB").tobytes())
    ff.stdin.close()
    if ff.wait() == 0:
        os.replace(a.out + ".part.mp4", a.out)
