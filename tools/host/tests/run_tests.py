#!/usr/bin/env python3
"""RAVE FM 2's tests on the PC (tools/host/rfm_shot.c runs the firmware's own code: the engine, the screen, the app).

    python3 tools/host/tests/run_tests.py [--fuzz N] [--seed S] [--perf] [--only PREFIX]

Every test has an id, its parameters (the seed for the random ones), and a result: PASS, WARN or FAIL. The results
go to tools/host/tests/results/<date>.json; a failed random scenario is kept in results/ with its seed, to replay.
What runs here is the SIMULATION: the FM-1's own processor, its screen's timing, its flash and its LEDs are not in it
(the LEDs and the screen are the firmware's calls, caught). A PASS here is not a test on the device.

The builds: rfm_shot (as tools/host/build.sh), rfm_asan (AddressSanitizer + UndefinedBehaviorSanitizer), rfm_truth
(every screen cell sent each frame: what the screen should show, to check the cells the firmware skips)."""
import argparse, json, os, random, re, subprocess, sys, time
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
B = os.path.join(ROOT, "build")
T = os.path.join(ROOT, "tools", "host", "tests")
FS = 44100
W = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]   # the white keys
BLK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]                     # the black keys
FLAGS = ("-w -std=gnu99 -DTB_LITE=0 -DTB_DANCE=0 -DTB_MEL_STEPS=128 -DTB_HAS_USER_BRK=1 -DTB_HAS_DLY_HIFI=0 -DTB_BRK3 "
         "-DRFM2 -DRFM_HOST -DTB_SMP_LITE -Ifirmware/rfm/engine -Ifirmware/rfm/gen -iquote firmware/rfm/include")
SONG = ("W2800 B11 K0 K7 K14 K21 E1,1 K0 K9 K16 E1,1 K0 K4 K7 K11 K14 K18 K21 K25 E1,1 K0 K7 K14 K21 K4 K18 E1,1 "
        "K2 K6 K11 K18 K23 E1,1 K0 K4 K7 K12 K19 E1,-1 E1,-1 E1,-1 E1,-1 E1,-1 B8 W200 B12 W1500")   # six tracks playing
IGNORE = ("left shift of negative",)   # (UBSan: the engine's arithmetic shifts, as the FM-1's compiler does them)

results = []


def build():
    srcs = " ".join(sorted(os.path.join("firmware/rfm/engine", f) for f in os.listdir(os.path.join(ROOT, "firmware/rfm/engine"))
                           if f.endswith(".c") and f != "rfm_libc.c"))
    os.makedirs(B, exist_ok=True)
    for name, extra in (("rfm_shot", "-O1 -g"), ("rfm_asan", "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"),
                        ("rfm_truth", "-O1 -DDC_FAST=2")):
        cmd = f"gcc {extra} {FLAGS} -o build/{name} tools/host/rfm_shot.c {srcs} -lm"
        r = subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            print(r.stderr[-2000:])
            sys.exit(f"build {name}: failed")


def run(binary, script, wav=False, shots=None):
    out = shots or os.path.join(B, "t_shots")
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ)
    raw = os.path.join(B, "t_audio.raw")
    if wav:
        env["RFM_WAV"] = raw
    elif "RFM_WAV" in env:
        del env["RFM_WAV"]
    r = subprocess.run([os.path.join(B, binary), out, script], env=env, capture_output=True, text=True, errors="replace", timeout=900)
    a = np.fromfile(raw, dtype=np.int16).reshape(-1, 2).astype(float) if wav else None
    return r.stderr, a


def record(tid, status, detail, **params):
    results.append({"id": tid, "status": status, "detail": detail, **params})
    print(f"  {status:4s} {tid:22s} {detail}")


def db(x):
    return 20 * np.log10(np.sqrt((np.asarray(x, float) ** 2).mean()) / 32768 + 1e-9)


def sanitizer_errors(err):
    return [l for l in err.splitlines() if ("ERROR: AddressSanitizer" in l or "runtime error" in l)
            and not any(i in l for i in IGNORE)]


def random_scenario(seed, n=300):
    """a reproducible session: any button, held or pressed, any encoder any amount, keys pressed and held, palettes"""
    rnd = random.Random(seed)
    s = ["W2800"]
    for _ in range(n):
        k = rnd.random()
        if k < 0.22:
            s.append(f"B{rnd.randrange(14)}")
        elif k < 0.27:
            b = rnd.choice([2, 3, 6, 7, 10, 11])                            # a button held with a key
            s += [f"H{b}", "W40", f"K{rnd.randrange(27)}", "W40", f"U{b}"]
        elif k < 0.55:
            s.append(f"E{rnd.randrange(7)},{rnd.choice([-60, -9, -3, -1, 1, 2, 5, 13, 60])}")
        elif k < 0.75:
            s.append(f"K{rnd.randrange(27)}")
        elif k < 0.80:
            key = rnd.randrange(27)
            s += [f"P{key}", f"W{rnd.choice([100, 400, 1200])}", f"R{key}"]
        elif k < 0.82:
            s.append(f"C{rnd.randrange(6)}")
        else:
            s.append(f"W{rnd.choice([30, 100, 300, 800])}")
    s += ["B12", "W500"]
    return " ".join(s)


def t_boot_led():
    err, _ = run("rfm_shot", "W3000 L W2000 L")
    lit = [int(m) for m in re.findall(r"glow: (\d+) of 41", err)]
    record("BOOT-LED", "PASS" if lit and all(x == 41 for x in lit) else "FAIL", f"backlight at boot: {lit} of 41")


def t_fuzz(n, seed0):
    clips = 0
    cuts = set()
    for i in range(n):
        seed = seed0 + i
        sc = random_scenario(seed)
        err, a = run("rfm_asan", sc, wav=True)
        bad = sanitizer_errors(err)
        clips += int((np.abs(a) >= 32767).sum()) if a is not None else 0
        if bad:
            path = os.path.join(T, "results", f"fail_seed{seed}.txt")
            open(path, "w").write(sc)
            record(f"FUZZ-{seed}", "FAIL", bad[0][:150], seed=seed, replay=path)
        else:
            record(f"FUZZ-{seed}", "PASS", f"{len(sc.split())} actions, no memory or arithmetic error", seed=seed)
        cuts |= set(re.findall(r"CUT (\S+)", run("rfm_shot", sc)[0]))
    record("FUZZ-CLIP", "PASS" if clips == 0 else "WARN", f"samples at full scale over the {n} sessions: {clips}")
    record("UI-CUT", "PASS" if not cuts else "WARN", f"card texts still cut (rolled instead): {sorted(cuts)[:12]}")


def t_scenarios():
    d = os.path.join(T, "scenarios")
    for f in sorted(os.listdir(d)):
        err, _ = run("rfm_asan", open(os.path.join(d, f)).read())
        bad = sanitizer_errors(err)
        record(f"SCEN-{f[:-4]}", "FAIL" if bad else "PASS", bad[0][:150] if bad else "no memory or arithmetic error")


def t_audio():
    # each FX key, the song playing: it sounds, nothing at full scale, back to the song after
    base_err, base = run("rfm_shot", SONG + " W4000 B12 W200", wav=True)
    ref = db(base[int(5 * FS):int(8 * FS)])
    for i in range(16):
        _, a = run("rfm_shot", SONG + f" H2 W60 K{W[i]} W60 U2 W5000 B12 W200", wav=True)
        seg = a[int(4.4 * FS):]
        clip = int((np.abs(seg) >= 32767).sum())
        jump = np.abs(np.diff(seg.mean(1))).max() / 32768
        ok = clip == 0 and db(seg) > -60
        record(f"AUDIO-FX{i + 1:02d}", "PASS" if ok else "FAIL",
               f"level {db(seg):.1f} dB (song {ref:.1f}), full-scale samples {clip}, largest step {jump:.2f}", key=W[i])
    # the 909 track: its eleven pieces, the recordings
    names = "BD SD CP CH OH MT RS CR LT HT RD".split()
    _, a = run("rfm_shot", "W2800 E1,1 E1,1 W700 " + " ".join(f"K{W[i]} W440" for i in range(11)) + " W500", wav=True)
    t0 = 2.8 + 0.08 + 0.7
    for i in range(11):
        seg = a[int((t0 + i * 0.6 - 0.02) * FS):int((t0 + i * 0.6 + 0.55) * FS)]
        pk = np.abs(seg).max() / 32768
        record(f"AUDIO-909-{names[i]}", "PASS" if 0.02 < pk < 1.0 else "FAIL", f"peak {pk:.2f}")
    # PAN: full left and full right
    for d, side in ((-1, "L"), (1, "R")):
        _, a = run("rfm_shot", "W2800 E1,1 E1,1 E1,1 E1,1 W100 E0,1 W200 " + " ".join([f"E5,{d}"] * 70) + " W300 P12 W1000 R12 W300", wav=True)
        seg = a[int(6.2 * FS):int(7.2 * FS)]
        l, r = db(seg[:, 0]), db(seg[:, 1])
        far = r if side == "L" else l
        near = l if side == "L" else r
        record(f"AUDIO-PAN-{side}", "PASS" if near - far > 10 else "FAIL", f"near {near:.1f} dB, far {far:.1f} dB (the far side: the reverb's and the delay's)")
    # the filter on the bass, the reverb send on the lead
    _, a = run("rfm_shot", "W2800 E1,1 E1,1 E1,1 B6 W300 E0,1 W200 P12 W1500 " + " ".join(["E2,-1"] * 60) + " W1500 R12 W800", wav=True)
    t1 = 2.8 + 0.12 + 0.12 + 0.3 + 0.04 + 0.2
    before, after = db(a[int((t1 + 0.8) * FS):int((t1 + 1.4) * FS)]), db(a[int((t1 + 1.5 + 2.4 + 0.5) * FS):int((t1 + 1.5 + 2.4 + 1.4) * FS)])
    record("AUDIO-FILTER", "PASS" if after < before - 6 else "FAIL", f"bass {before:.1f} dB, cutoff down: {after:.1f} dB")
    tails = []
    for knob in ("", " ".join(["E3,1"] * 100)):
        _, a = run("rfm_shot", "W2800 E1,1 E1,1 E1,1 E1,1 W100 E0,1 W200 " + knob + " W300 K12 W3000", wav=True)
        t = 2.8 + 0.16 + 0.1 + 0.04 + 0.2 + 0.04 * len(knob.split()) + 0.3
        tails.append(db(a[int((t + 0.8) * FS):int((t + 2.5) * FS)]))
    record("AUDIO-REVERB", "PASS" if tails[1] > tails[0] + 6 else "FAIL", f"lead's tail {tails[0]:.1f} dB, reverb up: {tails[1]:.1f} dB")
    # the break's DISQUE: the key starts the loop, it keeps going, the key stops it
    _, a = run("rfm_shot", "W2800 E1,1 B6 W200 E4,1 E4,1 E4,1 E4,1 W300 B8 W200 K0 W6000 K0 W3000", wav=True)
    on, off = db(a[int(6 * FS):int(9 * FS)]), db(a[int(11.5 * FS):int(12.5 * FS)])
    record("AUDIO-DISQUE", "PASS" if on > -40 and off < on - 20 else "FAIL", f"looping {on:.1f} dB, stopped {off:.1f} dB")


def t_screen():
    # the cells the firmware skips (unchanged, by their hash): the screen as if every one had been sent
    d = os.path.join(T, "scenarios")
    for f in ("sweep.txt", "sw909.txt", "sw_scl.txt", "sw_fx.txt"):
        t = open(os.path.join(d, f)).read().split()
        sc = " ".join(x + (f" Sx{i}" if i % 25 == 24 else "") for i, x in enumerate(t))
        a, b = os.path.join(B, "t_scr_a"), os.path.join(B, "t_scr_b")
        for p in (a, b):
            subprocess.run(["rm", "-rf", p])
        run("rfm_shot", sc, shots=a)
        run("rfm_truth", sc, shots=b)
        names = sorted(os.listdir(b))
        diff = sum(open(os.path.join(a, n), "rb").read() != open(os.path.join(b, n), "rb").read() for n in names)
        record(f"SCREEN-{f[:-4]}", "PASS" if diff <= 1 else "FAIL", f"{diff} of {len(names)} captures differ from a full redraw (1: a 16-bit hash's chance)")


def t_perf():
    base_path = os.path.join(T, "perf_baseline.json")
    base = json.load(open(base_path)) if os.path.exists(base_path) else {}
    prof = os.path.join(B, "rfm_prof")
    srcs = " ".join(sorted(os.path.join("firmware/rfm/engine", f) for f in os.listdir(os.path.join(ROOT, "firmware/rfm/engine"))
                           if f.endswith(".c") and f != "rfm_libc.c"))
    subprocess.run(f"gcc -O2 {FLAGS.replace('-DTB_SMP_LITE', '')} -o {prof} tools/host/rfm_shot.c {srcs} -lm", shell=True, cwd=ROOT, check=True)
    sc = SONG + " W4500 P12 P16 P19 W2000 R12 R16 R19 W2000"
    now = {}
    for fn in ("tb_engine_render", "tb_app_tick"):
        out = os.path.join(B, "t_cg.out")
        r = subprocess.run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={out}", f"--toggle-collect={fn}", prof, os.path.join(B, "t_shots"), sc],
                           capture_output=True, text=True, timeout=1800)
        m = re.search(r"refs:\s+([\d,]+)", r.stderr)
        now[fn] = int(m.group(1).replace(",", "")) if m else 0
    for fn, label in (("tb_engine_render", "PERF-AUDIO"), ("tb_app_tick", "PERF-SCREEN")):
        b = base.get(fn)
        st = "PASS" if not b or now[fn] <= b * 1.10 else "FAIL"
        record(label, st, f"{now[fn] / 1e6:.0f} M instructions (PC, -O2) for the six-track song" + (f", baseline {b / 1e6:.0f} M ({100 * (now[fn] - b) / b:+.1f} %)" if b else ", baseline written"))
    if not base:
        json.dump(now, open(base_path, "w"), indent=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fuzz", type=int, default=30)
    ap.add_argument("--seed", type=int, default=1000)
    ap.add_argument("--perf", action="store_true")
    ap.add_argument("--only", default="")
    o = ap.parse_args()
    os.makedirs(os.path.join(T, "results"), exist_ok=True)
    t0 = time.time()
    print("building ...")
    build()
    for name, fn in (("BOOT", t_boot_led), ("SCEN", t_scenarios), ("FUZZ", lambda: t_fuzz(o.fuzz, o.seed)),
                     ("AUDIO", t_audio), ("SCREEN", t_screen)) + ((("PERF", t_perf),) if o.perf else ()):
        if o.only and not name.startswith(o.only):
            continue
        print(name)
        fn()
    n = {s: sum(r["status"] == s for r in results) for s in ("PASS", "WARN", "FAIL")}
    path = os.path.join(T, "results", time.strftime("%Y%m%d-%H%M%S") + ".json")
    json.dump({"when": time.ctime(), "seconds": round(time.time() - t0), "counts": n, "simulation_only": True,
               "results": results}, open(path, "w"), indent=1)
    print(f"\n{len(results)} tests: {n['PASS']} PASS, {n['WARN']} WARN, {n['FAIL']} FAIL ({time.time() - t0:.0f} s) -> {path}")
    sys.exit(1 if n["FAIL"] else 0)


if __name__ == "__main__":
    main()
