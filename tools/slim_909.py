#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's 909 track: Maxime's own TR-909 recordings, every piece, out of the full data
(firmware/rfm/gen/tb_909_data.h); the long ones (the cymbals, the snare's tail) cut at MAXLEN so that the app fits
its slot (the player fades the last 64 ms out).   slim_909.py OUT.h"""
import re
import sys
from pathlib import Path

GEN = Path(__file__).resolve().parent.parent / "firmware" / "rfm" / "gen"
KEEP = ("BD", "SD", "CP", "CH", "OH", "MT", "RS", "CR", "LT", "HT", "RD")
MAXLEN = {"OH": 11000, "CR": 9000, "RD": 15000}    # samples at 32 kHz (the rest whole; 19 Nov 2026: the ride
#   longer, as hardcore and jump play it: 0.47 s (21 Nov: what the FM-1 has room for) - the crash shorter for its room)
BLOCK = 16


def main(out):
    s = (GEN / "tb_909_data.h").read_text()
    full = s.split("#else", 1)[1]                                   # (the full table and data: after the lite ones)
    rows = re.findall(r"\{(\d+), (\d+), (\d+), (\d+)\},\s*/\* (\w+)", full.split("};", 1)[0])
    # (v22: the full data is the one after the second #else - the first TB909_DATA after the table's #else is the
    #  lighter set's: its bytes read with the full table's places gave the ride (and the toms) as noise)
    data_txt = s[s.index("#else", s.index("static const uint8_t TB909_DATA[")):]
    data_txt = data_txt[data_txt.index("TB909_DATA["):]
    data = [int(x) for x in re.findall(r"\d+", data_txt[data_txt.index("{"):data_txt.index("};")])]
    names = [r[4] for r in rows]
    ends = sorted(set(int(r[0]) for r in rows)) + [len(data)]
    out_data, table = [], []
    # (the toms: the mid tom pitched, as the lighter set has them - the room in the flash)
    TOM = {"LT": 5256961, "HT": 9702848}
    mt = next(r for r in rows if r[4] == "MT")
    rows = [(mt[0], mt[1], str(TOM[r[4]]), mt[3], r[4]) if r[4] in TOM else r for r in rows]
    placed = {}
    for off, ln, inc, fmt, name in rows:
        off, ln = int(off), int(ln)
        if off in placed:                                           # (the same recording: its data once)
            table.append((placed[off][0], placed[off][1], int(inc), int(fmt), name))
            continue
        if name in KEEP:
            end = min(e for e in ends if e > off)
            f = int(fmt)
            if ln > MAXLEN.get(name, ln):
                ln = MAXLEN[name]
                end = off + (-(-ln // BLOCK) * (1 + 2 * f) if f else (ln + 1) // 2)
            if f:                                                   # (no more than the recording's data holds)
                ln = min(ln, (end - off) // (1 + 2 * f) * BLOCK)
                end = off + (-(-ln // BLOCK) * (1 + 2 * f))
            else:
                ln = min(ln, (end - off) * 2)
            placed[off] = (len(out_data), ln)
            table.append((len(out_data), ln, int(inc), f, name))
            out_data += data[off:end]
        else:
            table.append((0, 0, int(inc), int(fmt), name))
    head = s[:s.index("static const struct")]
    sc = s[s.index("/* BC's scales"):s.index("#if TB_LITE || defined(TB_SMP_LITE)\nstatic const uint8_t TB909_DATA")]
    L = [head.replace("#ifndef TB_909_DATA_H\n#define TB_909_DATA_H", "#ifndef TB_909_DATA_H\n#define TB_909_DATA_H\n"
                      "/* (RAVE FM 2, tools/slim_909.py: every piece, the long ones cut) */"),
         "static const struct { uint32_t off, len, inc, fmt; } TB909[TB909_N] = {"]
    for off, ln, inc, fmt, name in table:
        L.append(f"    {{{off}, {ln}, {inc}, {fmt}}},   /* {name} */")
    out_data += [0, 0]                                              # (the BC reader takes 2 bytes: none past the end)
    L += ["};", sc, f"static const uint8_t TB909_DATA[{len(out_data)}] = {{"]
    for j in range(0, len(out_data), 32):
        L.append("    " + ", ".join(str(v) for v in out_data[j:j + 32]) + ",")
    L += ["};", "#endif"]
    Path(out).write_text("\n".join(L) + "\n")
    print(f"909 slim: {len(out_data)} B ({', '.join(KEEP)})")


if __name__ == "__main__":
    main(sys.argv[1])
