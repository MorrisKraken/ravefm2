#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2: build the app (firmware/src/ravefm.c, the platform, + firmware/rfm/engine, RAVE FM), the update loader and
the installable .fwsc package, with JieLi's toolchain (Windows: C:\\JL\\pi32, or the Linux one in $JIELI_TOOLCHAIN).

  py tools\\build_ravefm.py [--v15 PATH\\FM-1_015.fwsc] [--pi32 C:\\JL\\pi32]

How the app is made:
  1. every unit to LLVM IR, unoptimised (RAVE FM's units with -fno-common; their zeroed globals moved to .pool:
     the sound's state is ~290 KB, the 96 KB RAM region holds the platform's and the DMA buffers);
  2. llvm-link into one module; what an interrupt can reach (tools/size_fns.py isr_reach) keeps -Os, the rest
     (the UI, the stores) gets minsize;
  3. optimised (-Os) as one program;
  4. in the optimised IR: every 64-bit multiply moved into a helper of its own (the FM-1's CPU now and then leaves a
     register off when a 32x32->64 multiply runs beside a load: the platform's src/core.h mul64), what an interrupt
     reaches into .text.a.* (first in the image: one block for the I-cache, and in reach of the RAM code), the sound's
     hottest functions into .hot_text (RAM), as many as fit;
  5. the code generator, the link (firmware/rfm.ld), the RAM sections packed (LZ4, unpacked by fm1_cstart), the checks,
     the package (tools/fm1pkg_make.py).
The full sample data is tried first; if the app does not fit its slot, the lighter one (TB_SMP_LITE)."""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build          # noqa: E402  (tools/build.py: paths, the loader, the LZ4 packing's helpers, mmio_check)
import fm1pkg_make    # noqa: E402
import lz4blk         # noqa: E402
import size_fns       # noqa: E402
import build_win      # noqa: E402  (the SDK files: downloaded, or out of M-VAVE's V15 package)

SRC, FW, OUT = build.SRC, build.FW, build.SRC / "build" / "ravefm"
APP_XIP, APP_SLOT = build.APP_XIP, build.APP_SLOT
RAMTEXT_LO, RAMTEXT_END = 0x01C69800, build.RAMTEXT_END   # (rfm.ld: 16 KiB lower than the platform's, the sound's code)
POOL_END = RAMTEXT_LO
PRODUCT = "FM-1_180"     # (its own identity, for the Firmware Hub: a number of its own; FM-1_900 is the shared loader name)
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
ENGINE_FLAGS = ["-DTB_LITE=0", "-DTB_DANCE=0", "-DTB_MEL_STEPS=128", "-DTB_HAS_USER_BRK=1", "-DTB_HAS_DLY_HIFI=0",
                "-DTB_BRK3", "-DRFM2", "-fno-common"]
# the sound's hottest code, in this order, into RAM as long as it fits (a name the optimiser inlined away is skipped)
HOT_FNS = ("tb_engine_render", "render_tracks",
           # (21 Nov 2026: the synth's voices next - the FM-1's measure put them first, 70-76 % of a block)
           "tb_syn_voices", "tb_fx_run", "tb_hall_run", "tb_filter_run", "tb_ins_run",
           # (in the order a six-track song's profile gave: the kicks, the break, the stab, the 909)
           "tb_kick_render", "k_run", "tb_brk_render", "tb_akick_render", "kick_run",
           # (v24: the 909's before the first-version leads - its hats and ride on most songs; v22/v23 left it out)
           "tb_dx_render", "tb_rave_render", "rave_render", "tb909_render_x", "tb909_render", "tb_fx_master",
           "dc_block", "lfo_run", "tb_fx_dist", "dx_run_fx", "dx_run", "tb_acid_render")
RAM_SPARE = 2048 + 128                  # RAMTEXT kept free (the check wants 2 KiB; v24: 128 over it)
FAST = ("lcd_", "fm1_screen_", "rfm_scr_", "gfx_", "ui_px", "ui_blit")   # main-loop code kept -Os (the screen's loops)
TOOLS_WIN = {"clang": "clang.exe", "ld": "pi32v2-ld.exe", "objcopy": "llvm-objcopy.exe", "objdump": "llvm-objdump.exe",
             "link": "llvm-link.exe"}
TOOLS_LNX = {"clang": "pi32v2/bin/clang", "ld": "pi32v2/bin/ld", "objcopy": "common/bin/objcopy",
             "objdump": "common/bin/objdump", "link": "pi32v2/bin/llvm-link"}
BIN = {}


def tool(name, *args):
    rel = [str(Path(x).resolve().relative_to(SRC)) if isinstance(x, Path) else x for x in args]
    if name == "clang":
        rel = ["-target", "pi32v2", *rel]
    r = subprocess.run([BIN[name], *rel], cwd=SRC, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {name} failed ({' '.join(map(str, args))[:200]})")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout.replace("\r\n", "\n")


def build_tc(t, *args):
    """tools/build.py's tc (its loader build): its tool names to ours"""
    name = {"cc": "clang", "pi32v2/bin/ld": "ld", "common/bin/objcopy": "objcopy", "common/bin/objdump": "objdump"}[t]
    return tool(name, *args)


# ------------------------------------------------------------------------------------------------ the IR passes ---
GLOBAL_ZERO = re.compile(r'^(@"?[\w.$-]+"? = (?:internal |private )?(?:unnamed_addr )?global [^\n]*?zeroinitializer)'
                         r'(, align \d+)?$', re.M)


def to_pool(ir):
    """RAVE FM's zeroed globals (no section of their own) into .pool (zeroed by fm1_cstart, not in the RAM region)"""
    n = 0

    def put(m):
        nonlocal n
        if " section " in m.group(0):
            return m.group(0)
        n += 1
        return f'{m.group(1)}, section ".pool"{m.group(2) or ""}'
    return GLOBAL_ZERO.sub(put, ir), n


IR_DEF = re.compile(r'^(define [^\n]*?@"?([\w.$]+)"?\([^\n]*\)[^\n{]*?)\s*\{$', re.M)


def mark_size(ir, isr):
    """minsize on what no interrupt reaches (but the screen's loops, FAST): as tools/size_fns.py marks them"""
    n = 0

    def put(m):
        nonlocal n
        name = m.group(2)
        if name in isr or name.startswith(FAST) or "minsize" in m.group(0) or "optnone" in m.group(0):
            return m.group(0)
        n += 1
        return m.group(1) + " minsize" + m.group(3)
    return size_fns.IR_DEF.sub(put, ir), n


MUL = re.compile(r"^(\s*)(%[\w.]+) = mul (?:nuw )?(?:nsw )?(?:nuw )?i64 ([^,]+), (.+)$")
EXT = re.compile(r"^\s*(%[\w.]+) = (sext|zext)( nneg)? i(8|16|32) ([^\s,]+) to i64")
HELPERS = """
define internal i64 @rfm_smul64(i32 %a, i32 %b) noinline nounwind section ".hot_text.rfm_smul64" {
  %x = sext i32 %a to i64
  %y = sext i32 %b to i64
  %p = mul nsw i64 %x, %y
  ret i64 %p
}
define internal i64 @rfm_umul64(i32 %a, i32 %b) noinline nounwind section ".hot_text.rfm_umul64" {
  %x = zext i32 %a to i64
  %y = zext i32 %b to i64
  %p = mul nuw i64 %x, %y
  ret i64 %p
}
define internal i64 @rfm_mul64(i64 %a, i64 %b) noinline nounwind section ".hot_text.rfm_mul64" {
  %p = mul i64 %a, %b
  ret i64 %p
}
"""


def mul64_out(ir):
    """every 'mul i64' into a call of a helper (32x32 signed, unsigned, or 64x64): the multiply alone in its function,
    its arguments in registers, no load beside it. An operand extended from 32 bits or fewer, or a constant, takes the
    32-bit helpers (a narrower one widened to 32 bits first)"""
    out, ext, counts, tmp = [], {}, {"s": 0, "u": 0, "w": 0}, 0
    for ln in ir.split("\n"):
        if ln.startswith("define "):
            ext = {}
        m = EXT.match(ln)
        if m:
            op, nneg, bits, src = m.group(2), m.group(3), int(m.group(4)), m.group(5)
            can = {"s"} if op == "sext" else {"s", "z"} if (nneg or bits < 32) else {"z"}
            ext[m.group(1)] = (can, op, bits, src)
        m = MUL.match(ln)
        if m:
            a, b = m.group(3).strip(), m.group(4).strip()

            def kind(v):
                if v in ext:
                    return ext[v]
                if re.fullmatch(r"-?\d+", v):
                    c = int(v)
                    can = ({"s"} if -(1 << 31) <= c < (1 << 31) else set()) | ({"z"} if 0 <= c < (1 << 32) else set())
                    return (can, "c", 32, str(c if c < (1 << 31) else c - (1 << 32)))
                return None
            ka, kb = kind(a), kind(b)
            both = (ka[0] & kb[0]) if ka and kb and not (ka[1] == kb[1] == "c") else set()
            if both:
                h = "rfm_smul64" if "s" in both else "rfm_umul64"
                args = []
                for k in (ka, kb):
                    if k[1] != "c" and k[2] < 32:          # (an i8 / i16: widened to i32 as it was to i64)
                        tmp += 1
                        out.append(f"{m.group(1)}%rfm.w{tmp} = {k[1]} i{k[2]} {k[3]} to i32")
                        args.append(f"%rfm.w{tmp}")
                    else:
                        args.append(k[3])
                ln = f"{m.group(1)}{m.group(2)} = call i64 @{h}(i32 {args[0]}, i32 {args[1]})"
                counts["s" if h == "rfm_smul64" else "u"] += 1
            else:
                ln = f"{m.group(1)}{m.group(2)} = call i64 @rfm_mul64(i64 {a}, i64 {b})"
                counts["w"] += 1
        out.append(ln)
    return "\n".join(out) + HELPERS, counts


def sectioned(ir, names, prefix):
    """the definitions of names given section prefix+name (not those with a section already)"""
    hit = []

    def put(m):
        if m.group(2) not in names or " section " in m.group(1):
            return m.group(0)
        hit.append(m.group(2))
        return f'{m.group(1)} section "{prefix}{m.group(2)}" {{'
    return IR_DEF.sub(put, ir), hit


def fn_sizes(syms):
    """{function: bytes} from objdump -t"""
    out = {}
    for ln in syms.splitlines():
        p = ln.split()
        if len(p) >= 5 and "F" in p[1:3]:
            try:
                out[p[-1]] = int(p[-2], 16)
            except ValueError:
                pass
    return out


# ---------------------------------------------------------------------------------------------------- the app ---
def build_app(lite):
    OUT.mkdir(parents=True, exist_ok=True)
    plat = ["-Ifirmware/hal", "-Ifirmware/src", f'-DFELUCCA_ID="{PRODUCT}"']
    eng = ["-Ifirmware/rfm/include", "-Ifirmware/rfm/engine", "-Ifirmware/rfm/gen", *ENGINE_FLAGS,
           *(["-DTB_SMP_LITE"] if lite else [])]
    ir_flags = ["-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c"]
    tool("clang", "-c", FW / "rfm_crt0.S", "-o", OUT / "crt0.o")
    tool("clang", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o")
    tool("clang", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o")
    mods = [OUT / "ravefm.ll"]
    tool("clang", *CFLAGS, *plat, *ir_flags, FW / "src" / "ravefm.c", "-o", OUT / "ravefm.ll")
    pooled = 0
    for c in sorted((FW / "rfm" / "engine").glob("*.c")):
        ll = OUT / f"{c.stem}.ll"
        tool("clang", *CFLAGS, *eng, *ir_flags, c, "-o", ll)
        ir, n = to_pool(ll.read_text())
        ll.write_text(ir)
        pooled += n
        mods.append(ll)
    print(f"units: {len(mods)}; RAVE FM's zeroed globals into the pool: {pooled}", flush=True)
    tool("link", "-S", *mods, "-o", OUT / "linked.ll")
    ir = (OUT / "linked.ll").read_text()
    isr = size_fns.isr_reach(ir)
    ir, n = mark_size(ir, isr)
    (OUT / "sized.ll").write_text(ir)
    print(f"size: {len(isr)} functions an interrupt reaches (-Os), {n} others built for size", flush=True)
    cf = [f for f in CFLAGS if not f.startswith("-W")]
    tool("clang", *cf, "-S", "-emit-llvm", "-c", OUT / "sized.ll", "-o", OUT / "opt.ll")
    ir = (OUT / "opt.ll").read_text()
    if os.environ.get("RFM_MUL64") != "0":
        ir, k = mul64_out(ir)
        print(f"64-bit multiplies into helpers: {k['s']} signed 32x32, {k['u']} unsigned, {k['w']} 64x64", flush=True)
    isr_opt = size_fns.isr_reach(ir) - {"rfm_smul64", "rfm_umul64", "rfm_mul64"}

    def codegen(hot, measure=False):
        x, _ = sectioned(ir, set(hot), ".hot_text.")
        x, a = sectioned(x, isr_opt - set(hot), ".text.a.")
        (OUT / "final.ll").write_text(x)
        tool("clang", *cf, "-Xclang", "-disable-llvm-optzns", "-c", OUT / "final.ll", "-o", OUT / "app.o")
        elf = OUT / "ravefm.elf"
        ld = FW / "rfm.ld"
        if measure:                                   # (21 Nov 2026) the first link only measures the functions:
            # every one of them still in XIP then, the slot may not hold them all - a linker script with a larger
            # XIP for it alone (never the image: the second link, the real one, keeps the slot's own size)
            ld = OUT / "rfm_measure.ld"
            t = (FW / "rfm.ld").read_text()
            t2 = re.sub(r"(XIP\s*\(rx\)\s*:\s*ORIGIN = 0x[0-9A-Fa-f]+, LENGTH = )0x([0-9A-Fa-f]+)",
                        lambda m: m.group(1) + hex(int(m.group(2), 16) + 0x20000), t, count=1)
            if t2 == t:
                raise SystemExit("build: the XIP line of rfm.ld not found (measuring link)")
            ld.write_text(t2)
        tool("ld", "-T", ld, OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o", OUT / "app.o",
             "-o", elf, "-Map", OUT / "ravefm.map")
        return elf, tool("objdump", "-t", elf), len(a)

    elf, syms, na = codegen((), measure=True)
    sizes = fn_sizes(syms)
    sym = {ln.split()[-1]: int(ln.split()[0], 16) for ln in syms.splitlines() if len(ln.split()) >= 4 and
           re.fullmatch(r"[0-9a-f]{8}", ln.split()[0])}
    room = RAMTEXT_END - (sym.get("_rt_end", RAMTEXT_LO)) - RAM_SPARE
    hot, used = [], 0
    for f in HOT_FNS:
        names = [s for s in sizes if s == f or s.startswith(f + ".")]
        for s in names:
            if s in isr_opt and used + sizes[s] + 4 <= room:
                hot.append(s)
                used += sizes[s] + 4
    print(f"hot RAM code: {len(hot)} functions, {used} B of {room} B ({', '.join(hot)})", flush=True)
    elf, syms, na = codegen(hot)
    print(f"interrupt code first in the image: {na} functions", flush=True)

    for f in ("text.bin", "data.bin", "ramtext.bin", "hottext.bin"):
        (OUT / f).unlink(missing_ok=True)
    tool("objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin")
    tool("objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin")
    tool("objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin")
    tool("objcopy", "-O", "binary", "-j", ".hot_text", elf, OUT / "hottext.bin")
    dis = tool("objdump", "-d", elf)
    rt = tool("objdump", "-d", "-j", ".ram_text", elf)
    (OUT / "ravefm.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + re.escape(name) + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    load = symv("_etext")
    if load % 4 or load - APP_XIP != len(img):
        raise SystemExit(f"_etext {load:#x}: not word aligned or not the end of .text")
    stream, raw = bytearray(), 0
    for f, lo, hi in (("ramtext.bin", "_rt_start", "_rt_end"), ("hottext.bin", "_hot_start", "_hot_end"),
                      ("data.bin", "_data_start", "_data_end")):     # (rfm_main.c fm1_cstart's order)
        blob = (OUT / f).read_bytes() if (OUT / f).exists() else b""
        if len(blob) != symv(hi) - symv(lo):
            raise SystemExit(f"{f}: {len(blob)} B, its section {symv(hi) - symv(lo)} B")
        at = len(stream)
        stream += lz4blk.compress_small(blob) if blob else b""
        back, end = lz4blk.boot_unpack(bytes(stream), at, len(blob))
        if back != blob or end != len(stream):
            raise SystemExit(f"{f}: its LZ4 block does not unpack to it")
        raw += len(blob)
    img += stream
    img += b"\xff" * (-len(img) % 4)
    print(f"boot: RAM code and data {raw} B packed to {len(stream)} B", flush=True)
    (OUT / "ravefm.bin").write_bytes(img)
    return bytes(img), syms, dis, rt


def check(img, syms, dis, rt):
    errors, notes = [], []
    mul = build.mul64_near_loads(dis)
    notes.append(f"64-bit multiplies left beside a load: {len(mul)} functions" +
                 (f" ({', '.join(f for f, *_ in mul[:6])})" if mul else ""))
    dbl = [n for n in build.SOFT_DOUBLE if re.search(r"\s" + n + r"$", syms, re.M)]
    if dbl:
        errors.append(f"soft-double routines linked (a double crept in): {dbl}")
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    if [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]:
        errors.append(".ram_text contains calls (RAM code runs with the flash off)")
    for ln in dis.splitlines():
        mm = build.LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot ({APP_SLOT} B)")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    notes.append(f"image {len(img)} B of the {APP_SLOT} B slot ({APP_SLOT - len(img)} B free); RAM .data+.bss {bss} B "
                 f"of 98304; pool {pool} B, spare {POOL_END - sym('_pool_end')} B; LOWBUF {sym('_low_end') - sym('_low_start')} B")
    if bss > 96 * 1024:
        errors.append("RAM region overflow")
    if POOL_END - sym("_pool_end") < 8192:
        errors.append(f"pool headroom {POOL_END - sym('_pool_end')} B < 8192 B")
    targets, sect = [], None
    for ln in dis.splitlines():
        mm = re.match(r"Disassembly of section (\S+):", ln)
        if mm:
            sect = mm.group(1)
            continue
        if sect in (".ram_text", ".hot_text"):
            mm = re.search(r"\bcall\b[^<]*<([\w.]+)\s*:\s*([0-9a-f]+)\s*>", ln)
            if mm and int(mm.group(2), 16) >= APP_XIP:
                targets.append((int(mm.group(2), 16), mm.group(1)))
    far = max(targets) if targets else (APP_XIP, "-")
    reach = sym("_rt_start") + 0x400000 - far[0]
    notes.append(f"RAM code {sym('_hot_end') - sym('_rt_start')} B of {RAMTEXT_END - sym('_rt_start')}; "
                 f"{len(targets)} calls into the image, the farthest {far[1]} with {reach} B of reach to spare")
    if sym("_hot_end") > sym("_hot_start") and not targets and "call" in dis:
        errors.append("no call from RAM code found in the disassembly: the reach check cannot read it")
    if reach < 12288:
        errors.append(f"RAM code's farthest call ({far[1]}) has only {reach} B of reach to spare")
    if RAMTEXT_END - sym("_hot_end") < 2048:
        errors.append(f"RAM code headroom {RAMTEXT_END - sym('_hot_end')} B < 2048 B")
    for n in ("fm1_cstart", "fm1_alnk0_irq", "fm1_timer5_irq", "tb_engine_render", "tb_app_tick", "fm1_fault_c"):
        if not sym(n):
            errors.append(f"{n} missing from the image")
    return errors, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--v15", default=os.environ.get("FM1_V15"))
    ap.add_argument("--pi32", default=os.environ.get("FM1_PI32", r"C:\JL\pi32"))
    ap.add_argument("--lite", action="store_true", help="the lighter sample data from the start")
    a = ap.parse_args()
    if os.name == "nt":
        for k, exe in TOOLS_WIN.items():
            BIN[k] = str(Path(a.pi32) / "bin" / exe)
    else:
        root = Path(os.environ.get("JIELI_TOOLCHAIN", "/opt/jieli"))
        for k, exe in TOOLS_LNX.items():
            BIN[k] = str(root / exe)
    for k, p in BIN.items():
        if not Path(p).exists():
            raise SystemExit(f"the JieLi toolchain: {p} missing")
    OUT.mkdir(parents=True, exist_ok=True)
    sdk = build_win.sdk_download(OUT / "sdk")
    if not sdk:
        if not a.v15 or not Path(a.v15).exists():
            raise SystemExit("the package's SDK files: no download, and no --v15 (M-VAVE's FM-1_015.fwsc)")
        sdk = build_win.sdk_from_v15(a.v15, OUT / "sdk_v15")
    fm1pkg_make.SDK = sdk
    build.tc = build_tc
    build.LDR = OUT / "loader"
    ota = build.build_loader()
    lite = a.lite
    for attempt in (0, 1):
        print(f"--- the app ({'lighter' if lite else 'full'} sample data)", flush=True)
        img, syms, dis, rt = build_app(lite)
        if len(img) <= APP_SLOT or lite:
            break
        print(f"  {len(img)} B: {len(img) - APP_SLOT} B over the slot; again with the lighter sample data", flush=True)
        lite = True
    errors, notes = check(img, syms, dis, rt)
    hal = build.mmio_check()
    errors += hal
    if not hal:
        notes.append("register access: hal/ only")
    notes.append("samples: " + ("the lighter set (LT HT from the mid tom, DIST from LOUD)" if lite else "the full set"))
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    name = OUT / "RAVEFM2.fwsc"
    name.write_bytes(pkg)
    print(f"app      {OUT / 'ravefm.bin'}  {len(img)} B")
    print(f"loader   {OUT / 'loader' / 'ota.bin'}  {len(ota)} B")
    print(f"package  {name}  {len(pkg)} B, identity {PRODUCT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
