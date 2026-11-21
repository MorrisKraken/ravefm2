#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2: tools/build.py on Windows, with the JieLi toolchain installed by JieLi's Windows setup (C:\\JL\\pi32,
its tools named clang.exe, pi32v2-ld.exe, llvm-objcopy.exe, llvm-objdump.exe) instead of the Linux one.

The package needs three files of the AC79 SDK (uboot.boot, cfg_tool.bin, cfg/eq_cfg_hw.bin). They are taken here from
M-VAVE's own V15 package (FM-1_015.fwsc), where they sit as M-VAVE shipped them; no vendor file is kept in the tree.

  py tools\\build_win.py [--v15 PATH\\FM-1_015.fwsc] [--pi32 C:\\JL\\pi32] [build.py's own options]"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build          # noqa: E402  (tools/build.py)
import fm1pkg_make    # noqa: E402

TOOLS = {"pi32v2/bin/clang": "clang.exe", "pi32v2/bin/ld": "pi32v2-ld.exe",
         "common/bin/objcopy": "llvm-objcopy.exe", "common/bin/objdump": "llvm-objdump.exe"}


def sdk_from_v15(fwsc, dest):
    """uboot.boot, cfg_tool.bin, cfg/eq_cfg_hw.bin out of M-VAVE's V15 .fwsc, into dest/cpu/wl82/tools/"""
    enc, sfc, chipkey = fm1pkg_make.enc, fm1pkg_make.sfc, fm1pkg_make.chipkey_decode
    raw = Path(fwsc).read_bytes()
    log = bytearray()
    for i in range(20):                                  # the 20 identity marker bytes out
        log += raw[i * 0x30:i * 0x30 + 0x2F]
    log += raw[20 * 0x30:]
    hdr = bytearray(log[:0x40])
    enc(hdr, 0, 0x40)
    nfiles = struct.unpack_from("<H", hdr, 8)[0]
    flash = None
    for i in range(nfiles):
        e = bytearray(log[0x40 + i * 0x50:0x40 + (i + 1) * 0x50])
        enc(e, 0, 0x50)
        off, ln = struct.unpack_from("<II", e, 8)
        if e[0x40:].split(b"\0")[0] == b"flash.bin":
            flash = bytes(log[off:off + ln])
    if not flash:
        raise SystemExit(f"{fwsc}: no flash.bin in it (not an FM-1 package?)")
    head = bytearray(flash[:0x200])
    for o in range(0, 0x200, 32):
        enc(head, o, 32)
    ents = {}
    for o in range(32, 0x200, 32):
        b = head[o:o + 32]
        off, size = struct.unpack_from("<II", b, 4)
        name = b[16:32].split(b"\0")[0]
        if not name or name[0] == 0xFF:
            break
        ents[name.decode("latin-1")] = (off, size)
    spl = flash[ents["uboot.boot"][0]:sum(ents["uboot.boot"])]
    isd = flash[ents["isd_config.ini"][0]:sum(ents["isd_config.ini"])]
    key = chipkey(isd[:32])
    first = bytearray(flash[0x4000:0x4020])
    sfc(first, 0, 32, 0, key)
    blk = struct.unpack_from("<I", first, 8)[0]
    area = bytearray(flash[0x4000:0x4000 + blk + 0x2000])
    sfc(area, 0, len(area), 0, key)
    cfg_tool = None
    for o in range(32, 32 * 8, 32):
        b = area[o:o + 32]
        off, size = struct.unpack_from("<II", b, 4)
        if b[16:32].startswith(b"cfg_tool.bin"):
            cfg_tool = bytes(area[off:off + size])
    cfg = area[blk:]
    off, size = struct.unpack_from("<II", cfg, 32 + 4)
    eq = bytes(cfg[off:off + size])
    if not cfg_tool or len(spl) < 1024 or len(eq) < 64:
        raise SystemExit(f"{fwsc}: its SDK files could not be read")
    out = Path(dest) / "cpu" / "wl82" / "tools"
    (out / "cfg").mkdir(parents=True, exist_ok=True)
    (out / "uboot.boot").write_bytes(spl)
    (out / "cfg_tool.bin").write_bytes(cfg_tool)
    (out / "cfg" / "eq_cfg_hw.bin").write_bytes(eq)
    for n, d in (("uboot.boot", spl), ("cfg_tool.bin", cfg_tool), ("cfg/eq_cfg_hw.bin", eq)):
        print(f"  sdk  {n}: {len(d)} B, sha256 {hashlib.sha256(d).hexdigest()[:16]}")
    return Path(dest)


SDK_RAW = "https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/raw/AC79NN_SDK_V1.2.1_2023-12-13/cpu/wl82/tools/"


def sdk_download(dest):
    """the three SDK files of the tested SDK version (build.SDK_SHA256), from JieLi's own repository; None if any
    cannot be fetched or differs"""
    import urllib.request
    out = Path(dest) / "cpu" / "wl82" / "tools"
    for rel, sha in build.SDK_SHA256.items():
        f = out / rel
        if f.exists() and hashlib.sha256(f.read_bytes()).hexdigest() == sha:
            continue
        try:
            with urllib.request.urlopen(SDK_RAW + rel, timeout=30) as r:
                d = r.read()
        except Exception as e:      # (no network, the site down)
            print(f"  sdk  {rel}: not downloaded ({e})")
            return None
        if hashlib.sha256(d).hexdigest() != sha:
            print(f"  sdk  {rel}: downloaded, but not the tested version")
            return None
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(d)
        print(f"  sdk  {rel}: {len(d)} B, the tested version")
    return Path(dest)


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--v15", default=os.environ.get("FM1_V15"))
    ap.add_argument("--pi32", default=os.environ.get("FM1_PI32", r"C:\JL\pi32"))
    a, rest = ap.parse_known_args()
    bindir = Path(a.pi32) / "bin"
    for exe in TOOLS.values():
        if not (bindir / exe).exists():
            raise SystemExit(f"the JieLi toolchain: {bindir / exe} missing")
    sdk = sdk_download(build.OUT / "sdk")
    if not sdk:                     # (no network: M-VAVE's own copies, from its V15 package)
        if not a.v15 or not Path(a.v15).exists():
            raise SystemExit("the package's SDK files: no download, and no --v15 (M-VAVE's FM-1_015.fwsc) to take them from")
        sdk = sdk_from_v15(a.v15, build.OUT / "sdk_v15")

    def tc(tool, *args):
        rel = [str(Path(x).resolve().relative_to(build.SRC)) if isinstance(x, Path) else x for x in args]
        if tool == "cc":
            tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
        r = subprocess.run([str(bindir / TOOLS[tool]), *rel], cwd=build.SRC, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit(f"build: {tool} failed")
        if r.stderr.strip():
            sys.stderr.write(r.stderr)
        return r.stdout.replace("\r\n", "\n")

    build.tc = tc
    build.use_docker = lambda: False
    build.toolchain = lambda: Path(a.pi32)
    sys.argv = [sys.argv[0], "--sdk", str(sdk), *rest]
    return build.main()


if __name__ == "__main__":
    sys.exit(main())
