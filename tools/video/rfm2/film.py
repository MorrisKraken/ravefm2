# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's demo film: the PC simulator's recordings (tools/video/rfm2/script.py's sessions, RFM_FILM) drawn as
the FM-1 seen from above - its real screen in the panel and zoomed beside it, what is pressed or turned ringed, the
LEDs as the firmware lights them, English captions and chapter cards from the sessions' marks - with an intro and an
outro. 1920 x 1080, 30 fps.
   python3 film.py WORKDIR OUT.mp4 [--from N --to N] [--preview frame,...]"""
import argparse, json, math, os, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, FPS = 1920, 1080, 30
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
FD = os.path.join(ROOT, "assets/fonts")
def F(name, size): return ImageFont.truetype(os.path.join(FD, name), size)
def FI(size, w=600):
    f = ImageFont.truetype(os.path.join(FD, "InterTight[wght].ttf"), size)
    try: f.set_variation_by_axes([w])
    except Exception: pass
    return f
ACC = (214, 255, 64)          # the acid yellow-green of the default palette
ACC2 = (176, 92, 255)         # the logo's violet
BG = (10, 10, 13)
# ---- the panel (the FM-1's layout, 1000 x 624 units; no maker's marks)
S, OX, OY = 1.12, 40, 336
BODY, TRAY, PART, EDGE, LABEL = (29, 29, 31), (20, 20, 21), (42, 42, 45), (58, 58, 62), (200, 200, 204)
KNOBS = ["MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"]
KPOS = [[105, 100, 24], [215, 100, 24], [105, 205, 24], [215, 205, 24], [610, 100, 22], [714, 100, 22], [818, 100, 22], [922, 100, 22]]
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
def P(x, y): return OX + x * S, OY + y * S
def R(x, y, w, h): a, b = P(x, y); return [a, b, a + w * S, b + h * S]
BTN = {"OCT-": OCT[0], "OCT+": OCT[1]}
for i in range(6):
    BTN[ROW1[i]] = [GRID[0] + i * GRID[4], GRID[1], GRID[2], GRID[3]]
    BTN[ROW2[i]] = [GRID[0] + i * GRID[4], GRID[1] + GRID[5], GRID[2], GRID[3]]
_pitch = (KEYBED[2] - 24) / 16
KW, KH = _pitch * 0.62, KEYBED[3] * 0.40
KEYS = {}
for i, k in enumerate(WHITE): KEYS[k] = (KEYBED[0] + 12 + (i + 0.5) * _pitch, KEYBED[1] + KEYBED[3] * 0.74)
for j, a in enumerate(BLACK_AFTER): KEYS[BLACK[j]] = (KEYBED[0] + 12 + (a + 1) * _pitch, KEYBED[1] + KEYBED[3] * 0.26)
ZX, ZY, ZS = 1262, 120, 600           # the zoomed screen
SX0, SY0 = P(SCREEN[0] + 8, SCREEN[1] + 8); SSZ = int((SCREEN[2] - 16) * S)


def lcd(fr):
    x = fr.astype(np.uint32)
    return np.stack([((x >> 11) & 31) * 255 // 31, ((x >> 5) & 63) * 255 // 63, (x & 31) * 255 // 31], -1).astype(np.uint8)


def base():
    im = Image.new("RGB", (W, H), BG)
    # a soft glow behind the panel and the zoom
    g = Image.new("L", (W, H), 0); d = ImageDraw.Draw(g)
    d.ellipse([200, 420, 1000, 1180], fill=60); d.ellipse([1150, 0, 1990, 820], fill=55)
    g = g.filter(ImageFilter.GaussianBlur(160))
    im = Image.composite(Image.new("RGB", (W, H), (40, 52, 18)), im, g)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(R(4, 4, 992, 616), radius=40 * S, fill=BODY, outline=(46, 46, 50), width=2)
    for t in TRAYS: d.rounded_rectangle(R(*t), radius=14 * S, fill=TRAY)
    sx, sy, ss = SCREEN
    d.rounded_rectangle(R(sx, sy, ss, ss), radius=18 * S, fill=(8, 8, 9))
    f = FI(int(11 * S), 600)
    for n, (x, y, r) in zip(KNOBS, KPOS):
        cx, cy = P(x, y)
        d.ellipse([cx - (r + 5) * S, cy - (r + 5) * S, cx + (r + 5) * S, cy + (r + 5) * S], fill=TRAY)
        d.ellipse([cx - r * S, cy - r * S, cx + r * S, cy + r * S], fill=PART, outline=EDGE, width=2)
        d.text((cx, cy - (r + 11) * S), n, font=f, fill=LABEL, anchor="ms")
    d.rounded_rectangle(R(*KEYBED), radius=18 * S, fill=TRAY)
    # the zoom's bezel
    d.rounded_rectangle([ZX - 22, ZY - 22, ZX + ZS + 22, ZY + ZS + 22], radius=34, fill=(22, 22, 25), outline=(52, 52, 58), width=2)
    d.text((ZX + ZS / 2, ZY - 40), "THE SCREEN, LIVE", font=FI(20, 650), fill=(120, 120, 128), anchor="ms")
    return im


def draw_btn(d, n, lit, glow, ring):
    x, y, w, h = BTN[n]
    fill = (255, 244, 214) if lit else ((66, 64, 56) if glow else PART)
    if n == "PLAY" and lit: fill = (95, 224, 138)
    if n == "REC" and lit: fill = (255, 80, 70)
    d.rounded_rectangle(R(x, y, w, h), radius=8 * S, fill=fill, outline=EDGE, width=2)
    ink = (29, 29, 31) if lit else ((255, 90, 80) if n == "REC" else LABEL)
    cx, cy = P(x + w / 2, y + h / 2)
    d.text((cx, cy + 4 * S), n if n != "PLAY" else "PLAY", font=FI(int(11 * S), 700), fill=ink, anchor="ms")


def draw_key(d, k, lit, glow):
    cx, cy = KEYS[k]
    a, b = P(cx - KW / 2, cy - KH / 2)
    d.rounded_rectangle([a, b, a + KW * S, b + KH * S], radius=KW * S / 2, fill=(12, 12, 13), outline=EDGE, width=2)
    sw = max(3, KW * 0.16)
    a, b = P(cx - sw / 2, cy - KH / 2 + KW * 0.45)
    col = (240, 240, 236) if lit else ((92, 92, 96) if glow else (48, 48, 52))
    d.rounded_rectangle([a, b, a + sw * S, b + (KH - KW * 0.9) * S], radius=sw * S / 2, fill=col)


class Seg:
    """a recording: its screen, its states, its marks"""
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
            s.marks.append((int(f), info[m]))
        s.marks.sort(key=lambda x: x[0])


def wrap(text, font, width):
    out, line = [], ""
    for w in text.split():
        t = (line + " " + w).strip()
        if font.getlength(t) > width and line: out.append(line); line = w
        else: line = t
    if line: out.append(line)
    return out


_capcache = {}
def caption_img(chapter, title, sub):
    k = (chapter, title, sub)
    if k in _capcache: return _capcache[k]
    im = Image.new("RGBA", (1140, 300), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    y = 8
    if chapter:
        d.text((0, y), chapter, font=F("ChakraPetch-SemiBold.ttf", 26), fill=ACC + (255,)); y += 44
    ft = F("Anton-Regular.ttf", 76)
    for l in wrap(title, ft, 1120)[:2]:
        d.text((0, y), l, font=ft, fill=(250, 250, 246, 255)); y += 92
    fs = FI(34, 500)
    for l in wrap(sub, fs, 1100)[:2]:
        d.text((0, y + 4), l, font=fs, fill=(170, 170, 178, 255)); y += 46
    _capcache[k] = im
    return im


_chapcache = {}
def chapter_img(num, title, sub):
    k = (num, title, sub)
    if k in _chapcache: return _chapcache[k]
    im = Image.new("RGBA", (W, H), (6, 6, 8, 236)); d = ImageDraw.Draw(im)
    d.text((W / 2, 400), num, font=F("ChakraPetch-SemiBold.ttf", 40), fill=ACC + (255,), anchor="ms")
    d.text((W / 2, 560), title, font=F("Anton-Regular.ttf", 150), fill=(250, 250, 246, 255), anchor="ms")
    d.rectangle([W / 2 - 160, 600, W / 2 + 160, 606], fill=ACC + (255,))
    d.text((W / 2, 672), sub, font=FI(40, 500), fill=(180, 180, 188, 255), anchor="ms")
    _chapcache[k] = im
    return im


def logo_img(seg, size):
    fr = lcd(seg.scr[45])
    im = Image.fromarray(fr).crop((20, 20, 220, 220)).resize((size, size), Image.LANCZOS)
    return im


def render(segs, plan, out, preview=None, rng=None):
    bg = base()
    total = sum(p["n"] for p in plan)
    frames = preview if preview else list(range(*(rng or (0, total))))
    ff = None
    if not preview:
        ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                               "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
                               "-pix_fmt", "yuv420p", out + ".part.mp4"], stdin=subprocess.PIPE)
    # where each film frame comes from
    index = []
    for pi, p in enumerate(plan):
        for k in range(p["n"]): index.append((pi, k))
    logo = logo_img(segs["a"], 520)
    angle = {}
    cur_seg = None
    for fi in frames:
        pi, k = index[fi]
        p = plan[pi]
        if p["kind"] in ("intro", "outro"):
            im = special(p, k, segs, logo)
        else:
            sg = segs[p["seg"]]
            if cur_seg != p["seg"]:
                cur_seg = p["seg"]; angle = {n: 0.0 for n in KNOBS}; last = {}
                # (the knobs' angles and the rings' fades, as they were up to here)
                for j in range(0, p["k0"] + k):
                    _acc(sg, j, angle, last)
            i = p["k0"] + k
            _acc(sg, i, angle, last)
            im = frame(sg, i, bg, angle, last, p)
        if preview:
            im.save(f"{out}-{fi:05d}.jpg", quality=92)
        else:
            ff.stdin.write(im.tobytes())
    if ff:
        ff.stdin.close(); ff.wait()
        os.replace(out + ".part.mp4", out)


def _acc(sg, i, angle, last):
    b, keys, leds, enc, dirs = sg.st[i]
    for e in range(7):
        if enc >> e & 1:
            n = ENC_KNOB[e]; angle[n] = angle.get(n, 0.0) + (1 if dirs[e] > 0 else -1) * 12
            last[n] = i
    for bi in range(14):
        if b >> bi & 1: last[BNAME[bi]] = i
    for kk in range(27):
        if keys >> kk & 1: last[kk] = i


def frame(sg, i, bg, angle, last, p):
    b, keys, leds, enc, dirs = sg.st[i]
    im = bg.copy(); d = ImageDraw.Draw(im)
    for bi, n in enumerate(BNAME):
        v = int(leds[bi]); draw_btn(d, n, v & 1, v & 2, False)
    for kk in range(27):
        v = int(leds[14 + kk]); draw_key(d, kk, v & 1, v & 2)
    for n, (x, y, r) in zip(KNOBS, KPOS):
        cx, cy = P(x, y)
        a = math.radians(-135 + 270 * 0.68 if n == "MASTER" else angle.get(n, 0.0))
        d.line([cx + math.sin(a) * (r - 5) * S, cy - math.cos(a) * (r - 5) * S, cx + math.sin(a) * (r - 14) * S,
                cy - math.cos(a) * (r - 14) * S], fill=(230, 230, 232), width=4)
    scr = Image.fromarray(lcd(sg.scr[i]))
    im.paste(scr.resize((SSZ, SSZ), Image.NEAREST), (int(SX0), int(SY0)))
    im.paste(scr.resize((ZS, ZS), Image.NEAREST), (ZX, ZY))
    # the rings: what is pressed or turned, fading out after
    for n, t in list(last.items()):
        age = i - t
        if age > 14: continue
        a = 1.0 - age / 15
        col = tuple(int(c * a + bgc * (1 - a)) for c, bgc in zip((255, 206, 60), (60, 60, 60)))
        w = 6 if age < 3 else 4
        if isinstance(n, int):
            cx, cy = P(*KEYS[n]); rr = 26
            d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=col, width=w)
        elif n in BTN:
            x, y, bw, bh = BTN[n]; cx, cy = P(x + bw / 2, y + bh / 2); rr = 40
            d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=col, width=w)
        else:
            x, y, r = KPOS[KNOBS.index(n)]; cx, cy = P(x, y); rr = (r + 12) * S
            d.arc([cx - rr, cy - rr, cx + rr, cy + rr], 0, 360, fill=col, width=w)
    # what was touched last, spelled out under the zoom
    recent = [(t, n) for n, t in last.items() if i - t < 24]
    if recent:
        t, n = max(recent, key=lambda x: x[0])
        lab = ("KEYS" if isinstance(n, int) else n)
        a = max(0.0, 1.0 - max(0, i - t - 10) / 14)
        col = tuple(int(c * a + 10 * (1 - a)) for c in (255, 206, 60))
        d.text((ZX + ZS / 2, ZY + ZS + 92), lab, font=F("ChakraPetch-Bold.ttf", 54), fill=col, anchor="ms")
    # the caption
    cap = None
    for f, m in sg.marks:
        if f <= i and m["kind"] in ("cap", "chap"): cap = (f, m)
    if cap:
        f, m = cap
        chap = p.get("chapter", "")
        img = caption_img(chap, m["title"], m["sub"]) if m["kind"] == "cap" else caption_img(chap, "", "")
        age = i - f
        if age < 8:
            a = age / 8
            im.paste(img, (60, int(60 + 20 * (1 - a))), img.split()[3].point(lambda v: int(v * a)))
        else:
            im.paste(img, (60, 60), img)
        if m["kind"] == "chap" and age < 84:
            ov = chapter_img(m.get("num", ""), m["title"], m["sub"])
            a = min(1.0, age / 8, (84 - age) / 10)
            im = Image.alpha_composite(im.convert("RGBA"), Image.blend(Image.new("RGBA", (W, H), (0, 0, 0, 0)), ov, a)).convert("RGB")
    return im


def special(p, k, segs, logo):
    n = p["n"]; t = k / FPS
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    BG = (0, 0, 0)
    if p["kind"] == "intro":
        # the logo grows out of the dark, the words after it, then the montage's titles on the beat
        a = min(1.0, t / 1.2)
        lg = logo.resize((int(520 * (0.9 + 0.1 * a)),) * 2, Image.LANCZOS)
        if t < 6.0:
            fade = a * min(1.0, (6.0 - t) / 0.6)
            im.paste(Image.blend(Image.new("RGB", lg.size, BG), lg, fade), ((W - lg.width) // 2, 150))
            if t > 1.6:
                b = min(1.0, (t - 1.6) / 0.6) * min(1.0, (6.0 - t) / 0.6)
                col = tuple(int(c * b + BG[j] * (1 - b)) for j, c in enumerate((250, 250, 246)))
                d.text((W / 2, 800), "RAVE FM 2", font=F("Anton-Regular.ttf", 120), fill=col, anchor="ms")
                col2 = tuple(int(c * b + BG[j] * (1 - b)) for j, c in enumerate(ACC))
                d.text((W / 2, 880), "HARDCORE FIRMWARE FOR THE FM-1 SYNTH", font=F("ChakraPetch-SemiBold.ttf", 40), fill=col2, anchor="ms")
        else:
            words = p["words"]
            per = (n / FPS - 6.0) / len(words)
            j = min(len(words) - 1, int((t - 6.0) / per))
            tt = (t - 6.0) - j * per
            w1, w2, shot = words[j]
            sg = segs[shot[0]]
            scr = Image.fromarray(lcd(sg.scr[shot[1]])).resize((600, 600), Image.NEAREST)
            z = 1.0 + 0.04 * tt / per
            scr = scr.resize((int(600 * z), int(600 * z)), Image.NEAREST)
            im.paste(scr, (1180 - (scr.width - 600) // 2, 240 - (scr.height - 600) // 2))
            fl = max(0.0, 1.0 - tt / 0.25)
            d.text((120, 560), w1, font=F("Anton-Regular.ttf", 130), fill=(250, 250, 246), anchor="ls")
            d.text((124, 640), w2, font=FI(44, 500), fill=ACC, anchor="ls")
            if fl > 0:
                im = Image.blend(im, Image.new("RGB", (W, H), (255, 255, 255)), fl * 0.35)
    else:
        a = min(1.0, t / 1.0)
        lg = Image.blend(Image.new("RGB", logo.size, BG), logo, a)
        im.paste(lg, ((W - 520) // 2, 110))
        d.text((W / 2, 760), "RAVE FM 2", font=F("Anton-Regular.ttf", 120), fill=(250, 250, 246), anchor="ms")
        d.text((W / 2, 840), "6 TRACKS · 75 KICKS · REAL BREAKS · THE 909 · RAVE LEADS · LIVE FX", font=F("ChakraPetch-SemiBold.ttf", 34), fill=ACC, anchor="ms")
        d.text((W / 2, 920), "Free firmware for the FM-1 synth", font=FI(34, 500), fill=(170, 170, 178), anchor="ms")
        fo = max(0.0, (t - (n / FPS - 1.2)) / 1.2)
        if fo > 0: im = Image.blend(im, Image.new("RGB", (W, H), (0, 0, 0)), min(1.0, fo))
    return im


def build_plan(work):
    segs = {"a": Seg(work + "/p1", "part1.json")}
    for x in "bcde":
        segs[x] = Seg(work + f"/s{x}", f"show_{x}.json")
    words = [("6 TRACKS", "Kick, break, 909, bass, lead, stabs", ("a", 4300)),
             ("75 KICKS", "Gabber, frenchcore, uptempo, doom...", ("a", 600)),
             ("REAL BREAKS", "Sliced, played on the keys", ("a", 2050)),
             ("RAVE LEADS", "Hoovers, screeches, stabs", ("a", 3160)),
             ("LIVE FX", "Risers, drops, rolls, tape stop", ("a", 4700))]
    plan = [dict(kind="intro", n=12 * FPS, words=words, audio=("a", 4380), fade_in=0.6, fade_out=1.2)]
    chap = {"a": "01 · TRACK CREATION", "b": "02 · SHOWCASE", "c": "02 · SHOWCASE", "d": "02 · SHOWCASE", "e": "02 · SHOWCASE"}
    for x in "abcde":
        plan.append(dict(kind="seg", seg=x, k0=0, n=segs[x].n, chapter=chap[x]))
    plan.append(dict(kind="outro", n=9 * FPS, audio=("b", 120), fade_in=0.3, fade_out=3.0))
    # (the chapter cards' numbers)
    num = {"TRACK CREATION": "01", "PLAY IT LIVE": "01 · LIVE", "SAVE IT": "01 · SAVE", "MORE TRACKS, MORE STYLES": "02",
           "MAKE IT YOURS": "03"}
    for sg in segs.values():
        for f, m in sg.marks:
            if m["kind"] == "chap": m["num"] = num.get(m["title"], "")
    return segs, plan


def audio(segs, plan, out):
    """the sound: each recording's audio under its frames (1470 samples a frame), the intro and the outro over
    excerpts of them, short fades at every cut"""
    def seg_audio(sg):
        a = np.fromfile(os.path.join(sg.d, "audio.raw"), dtype="<i2").reshape(-1, 2).astype(np.float32) / 32768
        n = sg.n * 1470
        return np.pad(a, ((0, max(0, n - len(a))), (0, 0)))[:n]
    parts = []
    for p in plan:
        n = p["n"] * 1470
        if p["kind"] == "seg":
            a = seg_audio(segs[p["seg"]])
        else:
            src, f0 = p["audio"]
            a = seg_audio(segs[src])[f0 * 1470:f0 * 1470 + n].copy()
            a = np.pad(a, ((0, n - len(a)), (0, 0)))
            fi, fo = int(p.get("fade_in", 1.0) * 44100), int(p.get("fade_out", 1.5) * 44100)
            a[:fi] *= np.linspace(0, 1, fi)[:, None]; a[-fo:] *= np.linspace(1, 0, fo)[:, None]
        k = 441
        a[:k] *= np.linspace(0, 1, k)[:, None]; a[-k:] *= np.linspace(1, 0, k)[:, None]
        parts.append(a)
    a = np.concatenate(parts)
    a *= 0.89 / max(1e-6, float(np.abs(a).max()))
    (a * 32767).astype("<i2").tofile(out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("work"); ap.add_argument("out")
    ap.add_argument("--preview"); ap.add_argument("--range"); ap.add_argument("--audio")
    a = ap.parse_args()
    segs, plan = build_plan(a.work)
    print("frames", sum(p["n"] for p in plan), "=", sum(p["n"] for p in plan) / FPS, "s", file=sys.stderr)
    if a.audio:
        audio(segs, plan, a.audio); sys.exit(0)
    render(segs, plan, a.out, preview=[int(x) for x in a.preview.split(",")] if a.preview else None,
           rng=tuple(int(x) for x in a.range.split(",")) if a.range else None)
