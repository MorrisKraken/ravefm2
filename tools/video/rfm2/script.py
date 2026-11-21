# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's demo film: the sessions the PC simulator plays (tools/host/rfm_shot.c, RFM_FILM) and their marks.
A session is a list of tokens (rfm_shot's script) and marks (N<id>): "start" (the film starts here), "tag" (a small
title for a moment of the session), "lbl" (what the next key does, shown at the key), "chap" (a chapter title over
the picture), "end". Played as one person playing: no explanations, the hands and the screen tell it."""
import json

B = dict(OCT_DOWN=0, OCT_UP=1, FX=2, SCL=3, ENV=4, LFO=5, EDIT=6, GLO=7, HOME=8, SAVE=9, ARP=10, SEQ=11, PLAY=12, REC=13)
E = dict(SELECT=0, ALGORITHM=1, KNOB1=2, KNOB2=3, KNOB3=4, KNOB4=5, PRESETS=6)
WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]     # the 16 whites: steps 1..16
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]                       # the 11 blacks
DRUM = dict(BD=0, SD=1, CP=2, CH=3, OH=4, MT=5, RS=6, CR=7, LT=8, HT=9, RD=10)
NOTE = {n: i for i, n in enumerate(["F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E"])}
# the live effects (FX held): the whites, the blacks
FXW = dict(RISER=0, DROP=1, LONG_DROP=2, FILTER_LP=4, FILTER_HP=5, KILL_BASS=6, SNARE_BAR=8, LOW_DROP=9)
FXB = dict(K_ROLL=7, S_ROLL=8)
MUTE = ["KICK", "BREAK", "909", "BASS", "LEAD", "STAB"]


def key_of(name):               # "G#4" -> its key (key 0: F3, as the FM-1's keyboard starts on F)
    n, o = name[:-1], int(name[-1])
    semis = (o - 3) * 12 + (NOTE[n] if NOTE[n] < 7 else NOTE[n] - 12)
    if not 0 <= semis <= 26:
        raise ValueError(name)
    return semis


class S:
    def __init__(s, name):
        s.name, s.t, s.marks, s.n = name, [], {}, 0

    def tok(s, *a):
        s.t += list(a)
        return s

    def mark(s, kind, title="", sub="", **kw):
        s.n += 1
        mid = f"{s.name}{s.n}"
        s.marks[mid] = dict(kind=kind, title=title, sub=sub, **kw)
        return s.tok(f"N{mid}")

    def tag(s, title, sub=""):
        return s.mark("tag", title, sub)

    def lbl(s, title):
        return s.mark("lbl", title)

    def w(s, ms):
        return s.tok(f"W{int(ms)}")

    def btn(s, b, after=250):
        return s.tok(f"B{B[b]}").w(after)

    def hold(s, b):
        return s.tok(f"H{B[b]}")

    def up(s, b):
        return s.tok(f"U{B[b]}")

    def key(s, k, after=0):
        s.tok(f"K{k}")
        return s.w(after) if after else s

    def press(s, k, ms, after=0):                      # a key held ms
        s.tok(f"P{k}").w(ms).tok(f"R{k}")
        return s.w(after) if after else s

    def turn(s, e, n, gap=90, step=1):
        d = step if n > 0 else -step
        for _ in range(abs(n)):
            s.tok(f"E{E[e]},{d}").w(gap)
        return s

    def steps(s, ss, gap=60):                          # white keys for steps (1-based)
        for i in ss:
            s.key(WHITE[i - 1], gap)
        return s

    def save(s, path):
        json.dump(dict(tokens=" ".join(s.t), marks=s.marks), open(path, "w"), indent=1)


# the melody (the FL screenshot one octave down): a note on each off-beat eighth, four a bar, eight bars
MELODY = [["G#4"] * 4, ["C#4", "C#4", "F#4", "F#4"], ["G#4"] * 4, ["A#4", "A#4", "E4", "E4"],
          ["B4"] * 4, ["E4", "E4", "C#5", "C#5"], ["A#4"] * 4, ["D#5"] * 4]
LEAD = 4                                               # (the lead's track, from 0)


def fx(s, name, ms=0, after=0):
    """FX held already: a live effect by its name - tapped (ms 0) or held ms"""
    lab = name.replace("_", " ")
    k = WHITE[FXW[name]] if name in FXW else BLACK[FXB[name]]
    s.lbl(lab)
    return s.press(k, ms, after) if ms else s.key(k, after)


def mute(s, t, after=0):
    s.lbl("MUTE " + MUTE[t])
    return s.key(BLACK[t], after)


def part1(lead_wait=0):
    s = S("a")
    s.w(3300).mark("start")
    # ---- the kick: through the presets, each one heard
    s.tag("KICK", "track 1").w(500)
    for i in range(11):
        s.turn("PRESETS", 1, gap=50)
        if i in (0, 2, 4, 6, 8):
            s.key(12, 160)
        else:
            s.w(140)
    s.w(150).tag("KICK", "ASYM G#1").key(12, 220).key(12, 380)
    s.mark("ctx", "steps").btn("SEQ", 350).steps([1, 5, 9, 13], gap=200).w(250)
    s.btn("PLAY", 1900)
    s.mark("ctx", "").btn("HOME", 500).turn("KNOB3", 2, gap=260).w(350).turn("KNOB3", -4, gap=260).w(350).turn("KNOB3", 2, gap=260).w(700)
    s.btn("EDIT", 120).btn("EDIT", 120).btn("EDIT", 700)              # ENVELOPE
    s.turn("KNOB2", -12, gap=80).w(1100).turn("KNOB2", 12, gap=80).w(900)
    # ---- the break: one key on step 1, it loops
    s.tag("BREAK", "track 2").turn("ALGORITHM", 1).w(250).btn("HOME", 450)
    s.turn("KNOB2", 2, gap=380).w(450)
    s.mark("ctx", "steps").btn("SEQ", 400).key(WHITE[0], 2900)
    # ---- the 909, pattern A: clap + open hat
    s.tag("909", "pattern A").mark("ctx", "drum").turn("ALGORITHM", 1).w(450)
    s.key(BLACK[DRUM["CP"]], 280).steps([1, 5, 9, 13], gap=170).w(250)
    s.key(BLACK[DRUM["OH"]], 280).steps([3, 7, 11, 15], gap=170).w(2700)
    # ---- pattern B: kick, break, clap + ride (tuned +9)
    s.tag("909", "pattern B").btn("GLO", 250).btn("GLO", 500).turn("KNOB4", 1).w(1700)
    s.mark("ctx", "steps").turn("ALGORITHM", -2, gap=120).w(150).btn("SEQ", 300).steps([1, 5, 9, 13], gap=150).w(300)
    s.turn("ALGORITHM", 1).w(300).key(WHITE[0], 900)
    s.turn("ALGORITHM", 1).w(350).mark("ctx", "drum")
    s.key(BLACK[DRUM["CP"]], 250).steps([1, 5, 9, 13], gap=150).w(250)
    s.key(BLACK[DRUM["RD"]], 250).steps([1, 5, 9, 13], gap=150).w(1400)
    s.btn("EDIT", 300).turn("SELECT", -4, gap=40).turn("SELECT", 2, gap=250).w(400)
    s.turn("KNOB2", 9, gap=160).w(2200)
    # ---- the lead: SLIDE V1, less reverb, 128 steps, the melody played in (REC)
    s.tag("LEAD", "SLIDE V1").turn("ALGORITHM", 2, gap=150).w(250).btn("HOME", 400)
    s.turn("PRESETS", 3, gap=330).w(300).mark("ctx", "notes")
    for k in ("G#4", "C#4", "F#4", "G#4"):
        s.key(key_of(k), 200)
    s.btn("HOME", 500).turn("KNOB2", -50, gap=22).w(800)                # MIX: REVERB down
    s.btn("SEQ", 450).turn("KNOB3", 14, gap=70, step=8).w(600)          # LOOP 128
    s.btn("HOME", 300)                 # (played from HOME: REC takes it in time - on SEQ, keys write step by step)
    if lead_wait:
        s.w(lead_wait)
    s.btn("REC", 100)
    first = True
    for b, bar in enumerate(MELODY):
        for j, n in enumerate(bar):
            st = 16 * b + 4 * j + 2
            s.tok(f"J{LEAD},{st}")
            if first:
                first = False
            s.press(key_of(n), 120)
    s.tok(f"J{LEAD},0").btn("REC", 2400)
    # (pattern A gets the melody too: COPY, A, PASTE)
    s.btn("HOME", 150).btn("SEQ", 150).btn("SEQ", 150).btn("SEQ", 400).turn("KNOB1", 1, gap=200).w(300).btn("OCT_UP", 600)
    s.btn("GLO", 400).turn("KNOB4", -1).w(1500)
    s.btn("HOME", 150).btn("SEQ", 150).btn("SEQ", 150).btn("SEQ", 400).turn("KNOB1", 1, gap=200).w(300).btn("OCT_UP", 900)
    s.btn("HOME", 3200)
    # ---- live
    s.mark("ctx", "").mark("chap", "PLAY IT LIVE", "Hold FX - the keys become the effects")
    s.w(1500).hold("FX").w(400)
    mute(s, 0, 1000); mute(s, 0, 500)
    mute(s, 1, 300); mute(s, 4, 1000); mute(s, 4, 200); mute(s, 1, 900)
    fx(s, "FILTER_LP", 1700, 400)
    fx(s, "FILTER_HP", 1600, 400)
    fx(s, "KILL_BASS", 1400, 400)
    fx(s, "K_ROLL", 1000, 400)
    fx(s, "S_ROLL", 1000, 500)
    fx(s, "SNARE_BAR", 0, 2300)
    fx(s, "LOW_DROP", 0, 4200)
    s.up("FX").w(200).btn("GLO", 250).turn("KNOB4", 1).w(1500)                 # pattern B: the ride
    # ---- the riser: its sounds mixed - less CLASSIC, WIND, SNARE ROLL, then NOISE
    s.tag("RISER", "Mix its sounds").btn("GLO", 200).turn("SELECT", -12, gap=20).turn("SELECT", 7, gap=90).w(400)
    s.turn("KNOB3", -1, gap=200).w(200)                                         # 2 bars
    s.turn("KNOB2", -12, gap=30).w(250)                                        # CLASSIC down
    s.turn("KNOB1", 2, gap=250).turn("KNOB2", 15, gap=30).w(250)               # WIND
    s.turn("KNOB1", 3, gap=200).turn("KNOB2", 10, gap=30).w(300)               # SNARE ROLL
    s.hold("FX").w(250)
    fx(s, "RISER", 0, 2900)
    fx(s, "DROP", 0, 4300)
    s.up("FX").w(200)
    s.turn("KNOB1", -4, gap=180).turn("KNOB2", 16, gap=30).w(200)              # NOISE in
    s.turn("KNOB1", 1, gap=200).turn("KNOB2", -15, gap=30).w(300)              # WIND out
    s.hold("FX").w(250)
    fx(s, "RISER", 0, 2900)
    fx(s, "LONG_DROP", 0, 6000)
    s.up("FX").w(300)
    # ---- save, with its name
    s.tag("SAVE", "HARDCORE").hold("HOME").w(650).up("HOME").w(800)
    s.turn("SELECT", 1).w(500)
    for i, ch in enumerate("HARDCORE"):
        if i:
            s.turn("KNOB1", 1, gap=110)
        s.turn("KNOB2", " ABCDEFGHIJKLMNOPQRSTUVWXYZ".index(ch), gap=28).w(110)
    s.w(400).btn("SAVE", 1300).turn("SELECT", -1).w(2600)
    s.mark("end")
    return s


def riff(s, notes, gap=40):
    for n in notes.split():
        if n == ".":
            s.btn("ARP", gap)
        else:
            s.key(key_of(n), gap)
    return s


def style(s, n, tempo=True):                           # SEQ x3: the TOOLS; STYLE; its n-th rhythm; OCT+
    s.btn("HOME", 100).btn("SEQ", 100).btn("SEQ", 100).btn("SEQ", 150)
    s.turn("KNOB1", 15, gap=40).turn("KNOB2", n, gap=60)
    if tempo:
        s.turn("KNOB3", 1, gap=60)
    return s.btn("OCT_UP", 200).btn("HOME", 150)


def track(s, frm, to):
    return s.turn("ALGORITHM", to - frm, gap=120)


def showcase():
    out = []
    # ---- GABBER '95
    s = S("b")
    s.tok("O0").w(3000)
    style(s, 0)
    s.turn("PRESETS", 15, gap=40)                      # GABBER A1
    track(s, 1, 6).turn("PRESETS", 16, gap=40)         # MENTASM
    s.btn("SEQ", 150)
    riff(s, "G3 . G3 G4 . G3 F4 . G3 . G3 D#4 . G3 D4 C4")
    s.btn("HOME", 100).btn("PLAY", 1400).tok("O1").mark("start")
    s.mark("chap", "MORE TRACKS", "Made in minutes on RAVE FM 2")
    s.tag("GABBER '95", "180 BPM").w(2600)
    s.btn("EDIT", 2400)
    s.btn("HOME", 300).hold("FX").w(250)
    fx(s, "K_ROLL", 1300, 500)
    s.up("FX").w(300)
    s.mark("end")
    out.append(s)
    # ---- HARDCORE 200 (the first film's frenchcore part, as it was: Maxime's pick)
    s = S("c")
    s.tok("O0").w(2800)
    style(s, 3)
    s.turn("PRESETS", 45, gap=40)
    track(s, 1, 4).turn("PRESETS", 17, gap=40)
    s.btn("SEQ", 150).turn("KNOB3", 16, gap=40).turn("KNOB2", 1, gap=60)
    riff(s, "G4 G4 A#4 G4 D5 C5 A#4 A4 G4 G4 A#4 G4 F4 D#4 D4 F4")
    s.btn("HOME", 100).btn("PLAY", 600).tok("O1").mark("start")
    s.tag("HARDCORE", "200 BPM").w(4000)
    s.tag("FX · INSERTS ON EVERY TRACK", "Drive, chorus, flanger, phaser, tremolo").btn("FX", 600).turn("KNOB1", 2, gap=500).w(2500)
    s.tag("TAPE STOP", "FX + the TAPE key").btn("HOME", 300).hold("FX").w(200).tok(f"P{BLACK[6]}").w(1700).tok(f"R{BLACK[6]}").w(2500)
    s.tag("FILTER HP", "Thin it out, then slam it back").tok(f"P{WHITE[5]}").w(2600).tok(f"R{WHITE[5]}").w(1500).up("FX").w(300)
    s.mark("end")
    out.append(s)
    # ---- EARLY RAVE
    s = S("d")
    s.tok("O0").w(3000)
    style(s, 12)
    track(s, 1, 5).turn("PRESETS", 38, gap=40)         # BELGIAN HOOVER
    s.btn("SEQ", 150).turn("KNOB3", 16, gap=40)
    riff(s, "G3 G3 . G3 A#3 . G3 . C4 . A#3 . G3 . F3 . G3 G3 . G3 A#3 . G3 . D4 . C4 . A#3 . A3 .")
    s.btn("HOME", 100).btn("PLAY", 1400).tok("O1").mark("start")
    s.tag("EARLY RAVE", "BELGIAN HOOVER").w(3200)
    s.btn("EDIT", 2600)
    s.btn("HOME", 300).hold("FX").w(250)
    fx(s, "FILTER_LP", 2200, 500)
    fx(s, "SNARE_BAR", 0, 3200)
    s.up("FX").w(400)
    s.mark("end")
    out.append(s)
    # ---- UPTEMPO, then the looks
    s = S("e")
    s.tok("O0").w(3000)
    style(s, 4)
    s.turn("PRESETS", 25, gap=40)                      # UPTEMPO C#2
    track(s, 1, 5).turn("PRESETS", 11, gap=40)         # UPTEMPO SCREECH
    s.btn("SEQ", 150)
    riff(s, "G4 . G4 . G4 A#4 . G4 . D5 . C5 A#4 . G4 .")
    s.btn("HOME", 100).btn("PLAY", 1400).tok("O1").mark("start")
    s.tag("UPTEMPO", "200 BPM").w(2400)
    s.btn("EDIT", 2400).btn("HOME", 300)
    s.hold("FX").w(250)
    mute(s, 0, 1200); mute(s, 0, 500)
    fx(s, "FILTER_HP", 2400, 600)
    fx(s, "S_ROLL", 1200, 500)
    fx(s, "RISER", 0, 5500)
    fx(s, "DROP", 0, 4500)
    s.up("FX").w(400)
    # ---- make it yours (the same song)
    s.mark("chap", "MAKE IT YOURS", "Colours, lights, characters")
    s.w(1500).hold("HOME").w(650).up("HOME").w(700)
    s.tag("COLOURS")
    for _ in range(6):
        s.turn("KNOB4", 1, gap=850)
    s.tag("KEY LIGHTS", "LUMIERE")
    for _ in range(4):
        s.turn("KNOB3", 1, gap=1300)
    s.btn("HOME", 900)
    s.tag("EVERY TRACK, ITS CHARACTER", "EDIT")
    for t in range(5, 0, -1):
        s.btn("HOME", 100).btn("EDIT", 100).turn("SELECT", -6, gap=25).w(1250)
        if t > 1:
            s.turn("ALGORITHM", -1, gap=150)
    s.btn("HOME", 2200)
    s.mark("end")
    out.append(s)
    return out


if __name__ == "__main__":
    import sys
    lw = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    part1(lw).save(sys.argv[1] + "/part1.json")
    for x in showcase():
        x.save(sys.argv[1] + f"/show_{x.name}.json")
