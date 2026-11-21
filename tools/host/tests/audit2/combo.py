#!/usr/bin/env python3
"""Audit 2: covering arrays over the engine's factors (combo.c): every pair of levels of every two factors (pairwise),
then every triple over the six heaviest factors (3-way); each configuration run plain (sound, time) and under
AddressSanitizer + UBSan. Results: combo_results.json, combo_report.txt."""
import itertools, json, os, random, subprocess, sys
D = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(D, "..", "..", "..", ".."))
NAMES = ["DRIVE", "STACK", "CHORUS", "REVERB", "DELAY", "CHORD", "ARP", "FX KEY", "EVENT", "BPM", "EQ", "909"]
LEVELS = [["OFF", "OVERDRIVE", "DIST", "FUZZ", "LO-FI", "FOLD"], ["none", "FLANGER", "PHASER", "TREMOLO"], ["0", "MAX"],
          ["20", "MAX"], ["0", "MAX"], ["OFF", "AUTO 9"], ["off", "on"], ["none", "WASH", "ROLL32", "KILLBASS", "ECHO"],
          ["none", "DROP", "RISER"], ["140", "260"], ["flat", "MAX"], ["VRAI", "DRUM-X+MORPH+DRIVE"]]
def covering(strength, factors, seed):
    rnd = random.Random(seed)
    need = set()
    for fs in itertools.combinations(factors, strength):
        for lv in itertools.product(*[range(len(LEVELS[f])) for f in fs]):
            need.add((fs, lv))
    rows = []
    while need:
        best, gain = None, -1
        for _ in range(60):
            r = [rnd.randrange(len(L)) for L in LEVELS]
            fs, lv = next(iter(need))
            for f, l in zip(fs, lv): r[f] = l           # (seeded on a tuple still missing: always progress)
            g = sum(1 for fs, lv in itertools.combinations(factors, strength) and [] or [])
            g = sum(((fs2, tuple(r[f] for f in fs2)) in need) for fs2 in itertools.combinations(factors, strength))
            if g > gain: best, gain = r, g
        rows.append(best)
        for fs2 in itertools.combinations(factors, strength):
            need.discard((fs2, tuple(best[f] for f in fs2)))
    return rows
def run(binary, r):
    p = subprocess.run([os.path.join(ROOT, "build", binary)] + [str(x) for x in r], capture_output=True, text=True, errors="replace", timeout=600)
    return p
pw = covering(2, range(12), 1)
tw = covering(3, [0, 1, 2, 3, 4, 5], 2)                   # (the heaviest: drive, stack, chorus, sends, chords)
ext = [[len(L) - 1 for L in LEVELS], [0] * 12]            # (all at their last level; all off)
configs = [("PAIR", r) for r in pw] + [("3WAY", r) for r in tw] + [("EDGE", r) for r in ext]
print(f"pairwise {len(pw)} configs, 3-way {len(tw)}, edges {len(ext)}", flush=True)
base = json.loads(run("combo", [0] * 12).stdout)["us_mean"]
out, flagged, asan_bad = [], [], 0
for kind, r in configs:
    m = json.loads(run("combo", r).stdout)
    a = run("combo_asan", r)
    errs = [l for l in a.stderr.splitlines() if ("ERROR: AddressSanitizer" in l or "runtime error" in l) and "left shift of negative" not in l]
    m.update(kind=kind, config={NAMES[i]: LEVELS[i][r[i]] for i in range(12)}, levels=r, cpu_x=round(m["us_mean"] / base, 2), sanitizer=errs[:2])
    asan_bad += bool(errs)
    if m["clips"] or m["peak"] >= 0.999 or abs(m["dc"]) > 0.02 or errs:
        flagged.append(m)
    out.append(m)
json.dump(out, open(os.path.join(D, "combo_results.json"), "w"), indent=1)
out.sort(key=lambda m: -m["cpu_x"])
with open(os.path.join(D, "combo_report.txt"), "w") as f:
    def w(s): print(s); f.write(s + "\n")
    w(f"configurations run: {len(out)} (pairwise {len(pw)}, 3-way {len(tw)}, edges {len(ext)}); sanitizer errors: {asan_bad}; flagged (clip, peak >= 0.999, |dc| > 2 %, sanitizer): {len(flagged)}")
    w(f"CPU (time per block on this PC, x the all-off configuration {base:.1f} us): min {out[-1]['cpu_x']}  median {out[len(out)//2]['cpu_x']}  max {out[0]['cpu_x']}")
    w("heaviest five:")
    for m in out[:5]: w(f"  x{m['cpu_x']}  peak {m['peak']:.3f}  {m['config']}")
    for m in flagged: w(f"FLAG {m['kind']} clips {m['clips']} peak {m['peak']:.3f} dc {m['dc']:.4f} {m['sanitizer']} {m['config']}")
    # each factor's own cost: mean cpu_x by level
    w("mean CPU by factor level:")
    for i, n in enumerate(NAMES):
        w("  " + n + ": " + ", ".join(f"{LEVELS[i][l]} x{sum(m['cpu_x'] for m in out if m['levels'][i] == l) / max(1, sum(1 for m in out if m['levels'][i] == l)):.2f}" for l in range(len(LEVELS[i]))))
