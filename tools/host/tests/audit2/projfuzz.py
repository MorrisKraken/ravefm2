#!/usr/bin/env python3
"""Audit 2: random sessions with the sound in its own thread (AddressSanitizer + UBSan), saves, loads and new projects
while the music plays; the screen's messages counted as the proof the actions happened."""
import os, random, re, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import run_tests as R
a, b = int(sys.argv[1]), int(sys.argv[2])
bad = 0; cnt = {"SAVED": 0, "LOADED": 0, "NEW": 0}
for sd in range(a, b):
    rnd = random.Random(sd)
    sc = R.random_scenario(sd, 150).replace(" B12 W500", "")
    for _ in range(6):
        sc += " B12 W%d B9 W300 H8 W700 U8 W200 B1 W100 B1 W%d B0 W100 B0 W%d %s W300" % (
            rnd.choice([200, 900]), rnd.choice([50, 400]), rnd.choice([50, 400]), " ".join(f"K{rnd.randrange(27)}" for _ in range(4)))
        sc += " " + " ".join(f"E{rnd.randrange(7)},{rnd.choice([-20, 3, 20])}" for _ in range(6)) + " B8 W200"
    sc += " B12 W500"
    r = subprocess.run([os.path.join(R.ROOT, "build/rfm_tasan"), os.path.join(R.ROOT, "build/sw"), sc], capture_output=True, text=True, errors="replace", timeout=1500)
    for k in cnt:
        cnt[k] += len(re.findall(r"HUD PROJECT \d+ \| " + k, r.stderr))
    errs = [l for l in r.stderr.splitlines() if ("ERROR: AddressSanitizer" in l or "runtime error" in l) and "left shift of negative" not in l]
    if errs:
        bad += 1; print(sd, errs[0][:170])
        open(os.path.join(os.path.dirname(__file__), f"fail_proj_{sd}.txt"), "w").write(sc)
print(f"threaded sessions {b - a}, with errors {bad}; actions done: {cnt}")
