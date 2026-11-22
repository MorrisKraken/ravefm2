# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's manual: its drawings - the panel (tools/video/rfm2/film2.py's FM-1, no maker's marks) and the keyboard's
maps, one per use. python3 tools/manual/diagrams.py SHOTS_DIR OUT_DIR"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../video/rfm2"))
from PIL import Image, ImageDraw
import film2

ACC = (214, 255, 64)
BG = (14, 14, 17)


def panel(shots, out):
    cv = film2.base().copy()
    for n in film2.BNAME:                             # the buttons, unlit (PLAY lit: the music playing)
        sp = film2.SPR[n]
        cv.paste(sp["lit"] if n == "PLAY" else sp["unlit"], sp["pos"], sp["mask"])
    ks = film2.SPR["key"]
    for kk, (kx, ky) in film2.KEYS.items():
        x, y = film2.cp(kx - ks["sw"] / film2.C / 2, ky - film2.KH / 2 + film2.KW * 0.45)
        cv.paste((48, 48, 52), (int(x), int(y)), ks["strip"])
    dr0 = ImageDraw.Draw(cv)
    import math
    for n, (x, y, r) in zip(film2.KNOBS, film2.KPOS):
        cx, cy = film2.cp(x, y); R = r * film2.C; an = math.radians(-60)
        dr0.line([cx + math.sin(an) * R * 0.25, cy - math.cos(an) * R * 0.25, cx + math.sin(an) * R * 0.58,
                  cy - math.cos(an) * R * 0.58], fill=(236, 236, 238), width=7)
    scr = Image.open(os.path.join(shots, "home_kick_preset.png")).resize((720, 720), Image.NEAREST)
    sx, sy, ss = film2.SCREEN
    x0, y0 = film2.cp(sx + 8, sy + 8)
    cv.paste(scr, (int(x0), int(y0)))
    a, b = film2.cp(-6, -6)
    c, d = film2.cp(1006, 630)
    im = cv.crop((int(a), int(b), int(c), int(d)))
    W, H = im.size
    pad = 150
    full = Image.new("RGB", (W, H + pad), BG)
    full.paste(im, (0, 0))
    dr = ImageDraw.Draw(full)
    f = film2.F("ChakraPetch-SemiBold.ttf", 46)
    def callout(ux, uy, text, tx):
        px, py = film2.cp(ux, uy)
        px -= a; py -= b
        dr.line([px, py, tx, H + 30], fill=ACC, width=4)
        dr.ellipse([px - 10, py - 10, px + 10, py + 10], fill=ACC)
        dr.text((tx, H + 40), text, font=f, fill=ACC, anchor="mt")
    callout(420, 300, "SCREEN", 1250)
    callout(140, 520, "27 KEYS: 16 WHITE, 11 BLACK", 520)
    callout(770, 230, "14 BUTTONS", 2280)
    full = full.resize((full.width // 2, full.height // 2), Image.LANCZOS)
    full.save(os.path.join(out, "panel.png"), optimize=True)


WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]


def keymap(name, title, whites, blacks, out, wcol=None, bcol=None, note=""):
    """a map of the 27 keys: whites[16], blacks[11] labels (None: not used)"""
    kw, gap = 118, 8
    W = 16 * kw + 2 * 40
    H = 520 if note else 470
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    ft = film2.F("Anton-Regular.ttf", 54)
    fl = film2.F("ChakraPetch-Bold.ttf", 25)
    fn = film2.F("ChakraPetch-SemiBold.ttf", 22)
    d.text((40, 24), title, font=ft, fill=(250, 250, 246))
    y_b, y_w = 120, 270
    for j, a in enumerate(BLACK_AFTER):
        x = 40 + (a + 1) * kw - kw * 0.42
        lab = blacks[j] if j < len(blacks) else None
        col = (bcol or {}).get(j, ACC) if lab else (60, 60, 64)
        d.rounded_rectangle([x, y_b, x + kw * 0.84, y_b + 130], radius=14, fill=(24, 24, 28), outline=col, width=3)
        if lab:
            for i, line in enumerate(lab.split("\n")):
                d.text((x + kw * 0.42, y_b + 46 + i * 30), line, font=fl, fill=col, anchor="mm")
        d.text((x + kw * 0.42, y_b + 112), f"B{j + 1}", font=fn, fill=(110, 110, 116), anchor="mm")
    for i in range(16):
        x = 40 + i * kw
        lab = whites[i] if i < len(whites) else None
        col = (wcol or {}).get(i, (250, 250, 246)) if lab else (60, 60, 64)
        d.rounded_rectangle([x + gap / 2, y_w, x + kw - gap / 2, y_w + 150], radius=14, fill=(34, 34, 38), outline=col, width=3)
        if lab:
            for k, line in enumerate(lab.split("\n")):
                d.text((x + kw / 2, y_w + 50 + k * 30), line, font=fl, fill=col, anchor="mm")
        d.text((x + kw / 2, y_w + 132), f"W{i + 1}", font=fn, fill=(110, 110, 116), anchor="mm")
    if note:
        d.text((40, H - 50), note, font=fn, fill=(170, 170, 178))
    im.save(os.path.join(out, f"keys_{name}.png"), optimize=True)


def keymaps(out):
    NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    nm = lambda k: f"{NOTES[(53 + k) % 12]}{(53 + k) // 12 - 1}"
    keymap("notes", "PLAYING NOTES (octave 0)", [nm(k) for k in WHITE], [nm(k) for k in BLACK], out,
           note="OCT- / OCT+ move the keyboard by an octave (-3 .. +3), both together: back to 0.")
    keymap("steps", "SEQ - KICK, BREAK, 909: THE STEPS", [str(i + 1) for i in range(16)],
           ["BD", "SD", "CP", "CH", "OH", "MT", "RS", "CR", "LT", "HT", "RD"], out,
           note="White keys: steps 1-16 of the page shown (OCT- / OCT+: the next 16). Black keys (909 only): the piece written.")
    fx_w = ["RISER", "DROP", "LONG\nDROP", "STOP\nDROP", "FILTER\nLP", "FILTER\nHP", "KILL\nBASS", "KICK\nROLL",
            "SNARE\nBAR", "LOW\nDROP", "REVERB\nWASH", "ROLL\n1/8", "ROLL\n1/16", "ROLL\n1/32", "TRACK\n1/16", "TRACK\n1/32"]
    fx_b = ["MUTE\n1", "MUTE\n2", "MUTE\n3", "MUTE\n4", "MUTE\n5", "MUTE\n6", "TAPE\nSTOP", "K\nROLL", "S\nROLL",
            "STUT-\nTER", "ECHO\nOUT"]
    HOT, BLUE, VIO = (255, 120, 90), (110, 190, 255), (190, 130, 255)
    wcol = {0: HOT, 1: HOT, 2: HOT, 3: HOT, 7: HOT, 8: HOT, 9: HOT, 4: BLUE, 5: BLUE, 6: BLUE, 10: BLUE,
            11: VIO, 12: VIO, 13: VIO, 14: VIO, 15: VIO}
    bcol = {i: ACC for i in range(6)}
    bcol.update({6: BLUE, 7: VIO, 8: VIO, 9: VIO, 10: BLUE})
    keymap("livefx", "FX HELD: THE LIVE EFFECTS", fx_w, fx_b, out, wcol=wcol, bcol=bcol,
           note="Red: build-ups and drops (tap: they start on the next bar). Blue / violet: held effects, they stop when you let go.")
    keymap("glo", "GLO HELD: MUTES AND SOLOS", ["MUTE\n1", "MUTE\n2", "MUTE\n3", "MUTE\n4", "MUTE\n5", "MUTE\n6", None, None,
                                                "SOLO\n1", "SOLO\n2", "SOLO\n3", "SOLO\n4", "SOLO\n5", "SOLO\n6", None, None],
           [None] * 11, out, note="Each key toggles. GLO held + PLAY: tap tempo. GLO held + SELECT: the tempo.")
    keymap("edit", "EDIT HELD: THE TRACK'S MACHINE", ["KICK", "BREAK", "909", "SYNTH", None, "ACID"] + [None] * 10,
           [None] * 11, out, note="The selected track becomes that machine (its steps stay). SCL held + any key: the root note.")


if __name__ == "__main__":
    shots, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    panel(shots, out)
    keymaps(out)
    print("ok")


def anatomy(shots, out):
    """the screen's parts: numbered on it, a legend beside"""
    scr = Image.open(os.path.join(shots, "home_kick_main_play.png")).resize((720, 720), Image.NEAREST)
    W, H = 1560, 800
    im = Image.new("RGB", (W, H), BG)
    ox, oy = 40, 40
    im.paste(scr, (ox, oy))
    d = ImageDraw.Draw(im)
    f = film2.F("ChakraPetch-SemiBold.ttf", 30)
    fb = film2.F("ChakraPetch-Bold.ttf", 22)
    s = 3
    items = [(7, 2, "Selected track (1-6) - ALGORITHM changes it"), (23, 2, "Play / stop (a red dot: REC armed)"),
             (48, 2, "Tempo (BPM)"), (100, 2, "Machine · sound (or a message)"), (201, 2, "Audio load"),
             (228, 2, "Pattern playing (A-D; the next one if queued)"), (60, 25, "The page's tabs (the page open lit)"),
             (8, 40, "Four cards: what KNOB 1-4 set, and their values"), (40, 90, "The page's picture"),
             (6, 222, "The selected track's steps and the playhead"), (100, 222, "The page's name"),
             (222, 222, "Page n / N (SELECT moves through them)")]
    for n, (x, y, lab) in enumerate(items, 1):
        px, py = ox + x * s, oy + y * s
        d.ellipse([px, py, px + 34, py + 34], fill=ACC)
        d.text((px + 17, py + 18), str(n), font=fb, fill=(10, 10, 12), anchor="mm")
        ly = 60 + (n - 1) * 58
        d.ellipse([800, ly, 834, ly + 34], fill=ACC)
        d.text((817, ly + 18), str(n), font=fb, fill=(10, 10, 12), anchor="mm")
        d.text((852, ly + 2), lab, font=f, fill=(240, 240, 240))
    im.save(os.path.join(out, "screen_anatomy.png"), optimize=True)

if __name__ == "__main__":
    anatomy(sys.argv[1], sys.argv[2])
