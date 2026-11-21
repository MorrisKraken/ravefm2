#!/usr/bin/env python3
"""Audit 2: the sound in its own thread (as the FM-1's interrupt), random sessions, ThreadSanitizer; the races where
the main loop WRITES what the sound reads or writes, grouped by the pair of functions (both stacks' first app frame)."""
import os, re, subprocess, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import run_tests as R
ROOT = R.ROOT
seeds = range(int(sys.argv[1]), int(sys.argv[2]))
pairs = collections.Counter(); ex = {}
for sd in seeds:
    sc = R.random_scenario(sd, 250)
    r = subprocess.run([os.path.join(ROOT, "build/rfm_tsan"), os.path.join(ROOT, "build/sw"), sc], capture_output=True, text=True,
                       errors="replace", env={**os.environ, "TSAN_OPTIONS": "report_signal_unsafe=0 history_size=3"}, timeout=1800)
    for rep in r.stderr.split("WARNING: ThreadSanitizer")[1:]:
        st = re.findall(r"(Write|Read|Atomic \w+|Previous write|Previous read|Previous atomic \w+) of size \d+ at 0x[0-9a-f]+ by (main thread|thread T\d+)[^\n]*:\n((?:\s+#\d+[^\n]*\n)+)", rep)
        if len(st) < 2: continue
        side = {}
        for kind, who, frames in st:
            fns = [m for m in re.findall(r"#\d+ (\w+) (\S+)", frames) if "rfm_shot.c" not in m[1]]
            side["main" if who == "main thread" else "audio"] = (kind.replace("Previous ", "").lower(), " < ".join(f[0] for f in fns[:3]), fns[0][1] if fns else "")
        if "main" in side and "audio" in side and side["main"][0].startswith("write"):
            k = (side["main"][1], side["main"][2], side["audio"][0], side["audio"][1])
            pairs[k] += 1
    print(f"seed {sd}: {len(pairs)} distinct main-write races so far", flush=True)
with open(os.path.join(os.path.dirname(__file__), "races_main_writes.txt"), "w") as f:
    for (mw, loc, ak, af), n in pairs.most_common():
        line = f"{n:4d}  MAIN WRITES {mw}  [{loc}]   while AUDIO {ak}s {af}"
        print(line); f.write(line + "\n")
