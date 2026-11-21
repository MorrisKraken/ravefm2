#!/usr/bin/env python3
"""Audit 2: the files the FM-1 build compiles (the include tree of each unit), their lines, functions, globals."""
import os, re, sys, json
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
os.chdir(ROOT)
PATHS = ["firmware/src", "firmware/hal", "firmware/rfm/include", "firmware/rfm/engine", "firmware/rfm/gen", "firmware/rfm/jui"]
inc = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
def find(name, here):
    for d in [here] + PATHS:
        p = os.path.normpath(os.path.join(d, name))
        if os.path.isfile(p):
            return p
    return None
seen, order = {}, []
def walk(path, unit):
    if path in seen:
        seen[path]["units"].add(unit); return
    seen[path] = {"units": {unit}}
    order.append(path)
    txt = open(path, errors="replace").read()
    for m in inc.finditer(txt):
        p = find(m.group(1), os.path.dirname(path))
        if p: walk(p, unit)
units = ["firmware/src/ravefm.c"] + sorted("firmware/rfm/engine/" + f for f in os.listdir("firmware/rfm/engine") if f.endswith(".c"))
for u in units:
    walk(u, os.path.basename(u))
for s in ("firmware/rfm_crt0.S", "firmware/hal/fm1_vec.S", "firmware/hal/fm1_isr.S"):
    seen[s] = {"units": {os.path.basename(s)}}; order.append(s)
fdef = re.compile(r'^(?:static\s+|inline\s+|__attribute__\(\([^)]*\)\)\s+|const\s+|volatile\s+|unsigned\s+|signed\s+)*[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(([^;{]*)\)\s*\{?\s*$', re.M)
gdef = re.compile(r'^(?:static\s+|volatile\s+|const\s+)*(?:u?int\d+_t|char|int|unsigned|uint8_t|int8_t|[a-z_]+_t)\s[\w\s\*\[\]]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*(?:=|;)', re.M)
rows, tf, tg, tl = [], 0, 0, 0
for p in order:
    txt = open(p, errors="replace").read()
    lines = txt.count("\n") + 1
    funcs = [] if p.endswith(".S") else [m.group(1) for m in fdef.finditer(txt) if m.group(1) not in ("if", "for", "while", "switch", "return", "sizeof")]
    glob_ = [] if p.endswith(".S") or "/gen/" in p else [m.group(1) for m in gdef.finditer(txt)]
    rows.append({"file": p, "lines": lines, "units": sorted(seen[p]["units"]), "functions": funcs, "globals": len(glob_)})
    tf += len(funcs); tg += len(glob_); tl += lines
json.dump(rows, open("tools/host/tests/audit2/inventory.json", "w"), indent=1)
print(f"files {len(rows)}  lines {tl}  functions {tf}  file-scope variables {tg}")
for r in sorted(rows, key=lambda r: -r["lines"])[:25]:
    print(f"{r['lines']:7d}  {len(r['functions']):4d} fn  {r['file']}")
