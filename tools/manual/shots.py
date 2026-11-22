# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's manual: its screenshots, taken in the PC simulator (tools/host/rfm_shot.c) running the firmware's own
code. python3 tools/manual/shots.py OUTDIR  ->  OUTDIR/*.png (480 x 480, the screen at 2x)"""
import os, subprocess, sys, glob
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
B = dict(OCT_DOWN=0, OCT_UP=1, FX=2, SCL=3, ENV=4, LFO=5, EDIT=6, GLO=7, HOME=8, SAVE=9, ARP=10, SEQ=11, PLAY=12, REC=13)
E = dict(SELECT=0, ALGORITHM=1, KNOB1=2, KNOB2=3, KNOB3=4, KNOB4=5, PRESETS=6)
WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]


class T:
    def __init__(s): s.t = []
    def tok(s, *a): s.t += list(a); return s
    def w(s, ms): return s.tok(f"W{int(ms)}")
    def btn(s, b, after=200): return s.tok(f"B{B[b]}").w(after)
    def hold(s, b): return s.tok(f"H{B[b]}")
    def up(s, b): return s.tok(f"U{B[b]}")
    def key(s, k, after=120): return s.tok(f"K{k}").w(after)
    def turn(s, e, n, gap=30):
        for _ in range(abs(n)):
            s.tok(f"E{E[e]},{1 if n > 0 else -1}").w(gap)
        return s
    def shot(s, name, wait=350): return s.w(wait).tok(f"S{name}")
    def pages(s, menu_btn, prefix, n, first_wait=350):
        """a menu opened (its button), its pages one by one with SELECT, each shot"""
        s.btn(menu_btn, 250).turn("SELECT", -20, 15)
        for i in range(n):
            s.shot(f"{prefix}{i + 1:02d}", first_wait)
            if i + 1 < n:
                s.turn("SELECT", 1, 60)
        return s
    def track(s, frm, to): return s.turn("ALGORITHM", to - frm, 80).w(200)


def session():
    s = T()
    s.w(1300).tok("Sboot").w(2000)
    s.shot("home_kick_main")
    s.turn("PRESETS", 11, 40).shot("home_kick_preset", 200)       # ASYM G#1
    s.btn("SEQ").key(WHITE[0]).key(WHITE[4]).key(WHITE[8]).key(WHITE[12]).shot("seq_kick")
    s.turn("KNOB4", 1, 100).shot("seq_kick_grid32").turn("KNOB4", -1, 100)
    s.btn("PLAY", 1500)
    s.btn("HOME").shot("home_kick_main_play", 200).btn("HOME").shot("home_kick_track")
    s.pages("EDIT", "edit_kick_", 3)
    s.btn("ENV").shot("env_kick")
    # break
    s.track(1, 2).btn("HOME").shot("home_break_main")
    s.btn("HOME").shot("home_break_mix").btn("HOME").shot("home_break_track")
    s.pages("EDIT", "edit_break_", 4)
    s.btn("SEQ").key(WHITE[0]).shot("seq_break", 600)
    # 909
    s.track(2, 3).btn("HOME", 300).btn("HOME", 300)
    s.shot("home_909_main")
    s.btn("SEQ").key(BLACK[2]).key(WHITE[0]).key(WHITE[4]).key(WHITE[8]).key(WHITE[12])
    s.key(BLACK[4]).key(WHITE[2]).key(WHITE[6]).key(WHITE[10]).key(WHITE[14]).shot("seq_909", 500)
    s.pages("EDIT", "edit_909_", 4)
    # bass (synth)
    s.track(3, 4).btn("HOME", 300).btn("HOME", 300).shot("home_synth_main")
    s.pages("EDIT", "edit_synth_", 7)
    s.btn("ENV").turn("SELECT", -5, 20).shot("env_synth_amp").turn("SELECT", 1, 60).shot("env_synth_filter")
    # lead: SLIDE V1, a melody step by step
    s.track(4, 5).btn("HOME", 300).btn("HOME", 300).turn("PRESETS", 3, 200).shot("home_rave_main")
    s.pages("EDIT", "edit_rave_", 3)
    s.btn("SEQ")
    for n in (15, 15, 8, 8, 13, 13, 15, 15):
        s.btn("ARP", 60).key(n, 60)
    s.shot("seq_melody", 400)
    s.turn("KNOB2", 1, 150).shot("seq_melody_len")
    s.btn("SEQ").shot("notes_edit", 500)
    s.btn("SEQ").shot("tools_01", 500)
    for i in range(1, 10):
        s.turn("KNOB1", 1, 120).shot(f"tools_{i + 1:02d}", 250)
    s.btn("OCT_DOWN", 300)
    # the pages of the lead
    s.pages("LFO", "lfo_", 2)
    s.btn("ARP", 250).turn("SELECT", -5, 20).shot("arp_01").turn("SELECT", 1, 60).shot("arp_02")
    s.pages("SCL", "scl_", 2)
    s.pages("FX", "fx_", 10)
    s.pages("GLO", "glo_", 11)
    s.btn("HOME", 300)
    # live FX
    s.hold("FX").w(600).shot("livefx", 200).tok(f"P{WHITE[4]}").shot("livefx_lp", 400).tok(f"R{WHITE[4]}").up("FX").w(300)
    # acid on track 6
    s.track(5, 6).hold("EDIT").w(200).key(WHITE[5], 200).up("EDIT").w(300).shot("machine_acid", 100)
    s.btn("HOME", 300).btn("HOME", 300).shot("home_acid_main")
    s.pages("EDIT", "edit_acid_", 3)
    # the projects
    s.hold("HOME").w(700).up("HOME").shot("sys_projects", 500)
    s.turn("SELECT", 1, 80).shot("sys_name", 400).turn("SELECT", -1, 80)
    s.turn("KNOB4", -1, 200).shot("palette_neon", 300)
    for i, n in enumerate(("acide", "toxic", "feu", "spectre", "violet")):
        s.turn("KNOB4", 1, 200).shot(f"palette_{n}", 300)
    s.btn("HOME", 300)
    # REC armed, the mixer as the music plays
    s.btn("REC", 300).shot("rec_armed", 200).btn("REC", 300)
    return s


def main(out):
    os.makedirs(out, exist_ok=True)
    raw = os.path.join(out, "raw")
    os.makedirs(raw, exist_ok=True)
    for f in glob.glob(os.path.join(raw, "*.ppm")):
        os.remove(f)
    s = session()
    subprocess.run([os.path.join(ROOT, "build/rfm_shot"), raw, " ".join(s.t)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    n = 0
    for f in sorted(glob.glob(os.path.join(raw, "*.ppm"))):
        im = Image.open(f).resize((480, 480), Image.NEAREST)
        im.save(os.path.join(out, os.path.basename(f)[:-4] + ".png"), optimize=True)
        n += 1
    print(n, "shots")


if __name__ == "__main__":
    main(sys.argv[1])
