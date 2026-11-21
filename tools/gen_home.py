#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAVE FM 2's HOME: Maxime's two dancing skeletons (assets/maxime/squelettes.jpg), each cut out on its own, its two inks
split, brought down to the screen; as tools/gen_art.py (warm << 2 | cool, 2 bits each, run-length). On the FM-1 the
upper body bobs and leans on the tempo over the legs, the whole one jumps on the kick.
  gen_home.py preview DIR | header OUT.h"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image
from scipy import ndimage as nd

HERE = Path(__file__).resolve().parent.parent
OUT_H = 126                       # their height on the screen
HIP = 0.53                        # where the hips are, of the height (the legs below stay planted)


# the joints on Maxime's drawing (its pixels): the left one faces right, the right one left
RIGS = [
    dict(face=1, pelvis=(240, 606), neck=(336, 340), head=(350, 278), head_r=72,
         hip_f=(285, 622), knee_f=(428, 728), ankle_f=(354, 885), foot_f=(352, 925),
         hip_b=(210, 638), knee_b=(210, 802), ankle_b=(98, 960), foot_b=(105, 1012),
         sh_f=(352, 360), el_f=(477, 348), wr_f=(525, 225), fist_f=(548, 188),
         sh_b=(244, 338), el_b=(120, 300), wr_b=(225, 158), fist_b=(270, 135)),
    dict(face=-1, pelvis=(772, 600), neck=(684, 338), head=(660, 278), head_r=72,
         hip_f=(720, 615), knee_f=(555, 705), ankle_f=(638, 885), foot_f=(645, 918),
         hip_b=(802, 630), knee_b=(720, 795), ankle_b=(848, 998), foot_b=(818, 1035),
         sh_f=(675, 375), el_f=(562, 495), wr_f=(600, 405), fist_f=(600, 380),
         sh_b=(780, 338), el_b=(922, 308), wr_b=(848, 128), fist_b=(818, 135)),
]
BONES = [("thigh_f", "hip_f", "knee_f"), ("shin_f", "knee_f", "ankle_f"), ("thigh_b", "hip_b", "knee_b"),
         ("shin_b", "knee_b", "ankle_b"), ("uarm_f", "sh_f", "el_f"), ("farm_f", "el_f", "wr_f"),
         ("uarm_b", "sh_b", "el_b"), ("farm_b", "el_b", "wr_b")]
PUNCH, PULL, PUMP = (-18, -25), (14, 30), (-30, -10)        # an arm's swing from where it is drawn: (upper, fore), degrees
STAND_F, STAND_B, LIFT, KICK, STAMP = (88, 95), (95, 100), (22, 112), (48, 22), (80, 96)
# the hakken, 8 frames on two beats (facing right; degrees, 0 = forward, 90 = down): a leg's knee lifted, kicked out
# low in front, stamped back, then the other; the fist opposite the kicking leg punching
POSES = [
    dict(leg_f=LIFT, leg_b=STAND_B, arm_f=PULL, arm_b=PUNCH, lean=3),
    dict(leg_f=KICK, leg_b=STAND_B, arm_f=PULL, arm_b=PUNCH, lean=4),
    dict(leg_f=STAMP, leg_b=(98, 104), arm_f=PULL, arm_b=PULL, lean=1),
    dict(leg_f=STAND_F, leg_b=STAND_B, arm_f=PUMP, arm_b=PULL, lean=0),
    dict(leg_f=(90, 97), leg_b=LIFT, arm_f=PUNCH, arm_b=PULL, lean=3),
    dict(leg_f=(92, 99), leg_b=KICK, arm_f=PUNCH, arm_b=PULL, lean=4),
    dict(leg_f=(94, 101), leg_b=STAMP, arm_f=PULL, arm_b=PULL, lean=1),
    dict(leg_f=STAND_F, leg_b=STAND_B, arm_f=PULL, arm_b=PUMP, lean=0),
]
KEYS = POSES


def _mid(a, b, t):
    if isinstance(a, tuple):
        return tuple(x + (y - x) * t for x, y in zip(a, b))
    return a + (b - a) * t


POSES = []                                                 # (in between each two: twice as many, smoother)
for _i, _a in enumerate(KEYS):
    _b = KEYS[(_i + 1) % len(KEYS)]
    POSES.append(_a)
    POSES.append({k: _mid(_a[k], _b[k], 0.5) for k in _a})
NFR = len(POSES)
IDLE = 6                                                   # (standing)


def inks():
    a = np.asarray(Image.open(HERE / "assets" / "maxime" / "squelettes.jpg").convert("RGB"), np.float32)
    R, G, B = a[..., 0], a[..., 1], a[..., 2]
    lum = np.maximum(np.maximum(R, G), B) / 255
    warm = np.clip((R - B) / 120 + 0.15, 0, 1) * lum
    cool = np.clip((B - R) / 120 + 0.15, 0, 1) * lum
    mag = np.clip(1 - np.abs(R - B) / 120, 0, 1) * lum
    warm, cool = np.maximum(warm, mag * 0.6), np.maximum(cool, mag * 0.6)
    ink = np.maximum(warm, cool) > 0.27
    lab, n = nd.label(nd.binary_dilation(ink, iterations=3))
    sizes = nd.sum(ink, lab, range(1, n + 1))
    ids = 1 + np.argsort(sizes)[::-1][:2]
    xs_mean = [np.where(lab == i)[1].mean() for i in ids]
    ids = [i for _, i in sorted(zip(xs_mean, ids))]
    return warm, cool, [lab == i for i in ids]


def seg_dist(px, py, a, b):
    ax, ay = a; bx, by = b
    dx, dy = bx - ax, by - ay
    t = np.clip(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0, 1)
    return np.hypot(px - (ax + t * dx), py - (ay + t * dy))


def ang(a, b):
    return np.degrees(np.arctan2(b[1] - a[1], b[0] - a[0]))


def pose_frames(rig, warm, cool, mask):
    ys, xs = np.where(mask & (np.maximum(warm, cool) > 0.08))
    wv, cv = warm[ys, xs], cool[ys, xs]
    xs, ys = xs.astype(np.float32), ys.astype(np.float32)
    # which part each pixel belongs to: the nearest, the body (spine, skull) wider than the limbs
    parts = ["torso"] + [b[0] for b in BONES]
    pv, nk = rig["pelvis"], rig["neck"]
    up = (pv[0] + (nk[0] - pv[0]) * 0.12, pv[1] + (nk[1] - pv[1]) * 0.12)
    d = [np.minimum(np.minimum(seg_dist(xs, ys, up, nk) - 62, np.hypot(xs - pv[0], ys - pv[1]) - 38),
                    np.hypot(xs - rig["head"][0], ys - rig["head"][1]) - rig["head_r"])]
    for name, j0, j1 in BONES:
        dd = seg_dist(xs, ys, rig[j0], rig[j1]) - 22
        if name.startswith("shin"):
            fo = rig["foot_" + name[-1]]
            dd = np.minimum(dd, np.hypot(xs - fo[0], ys - fo[1]) - 75)
        if name.startswith("farm"):
            fi = rig["fist_" + name[-1]]
            dd = np.minimum(dd, np.hypot(xs - fi[0], ys - fi[1]) - 70)
        d.append(dd)
    part = np.argmin(np.stack(d), axis=0)
    JR = 30                                                # (round each joint: drawn by both parts, no gap when bent)
    parent = {"thigh_f": 0, "thigh_b": 0, "uarm_f": 0, "uarm_b": 0, "shin_f": 1, "shin_b": 3, "farm_f": 5, "farm_b": 7}
    extra = []
    for bi, (name, j0, j1) in enumerate(BONES):
        jx, jy = rig[j0]
        near = (np.hypot(xs - jx, ys - jy) < JR)
        pi_ = parent[name]
        extra.append((np.where(near & (part == pi_))[0], bi + 1))   # the parent's pixels at the joint, also the child's
        extra.append((np.where(near & (part == bi + 1))[0], pi_))   # the child's, also the parent's
    idx = np.concatenate([np.arange(len(xs))] + [e[0] for e in extra])
    part = np.concatenate([part] + [np.full(len(e[0]), e[1]) for e in extra])
    xs, ys, wv, cv = xs[idx], ys[idx], wv[idx], cv[idx]
    face = rig["face"]
    def world(a):                                          # facing right -> this one's way
        return a if face > 0 else 180 - a
    frames = []
    for P in POSES:
        target = {"thigh_f": P["leg_f"][0], "shin_f": P["leg_f"][1], "thigh_b": P["leg_b"][0], "shin_b": P["leg_b"][1],
                  "uarm_f": P["arm_f"][0], "farm_f": P["arm_f"][1], "uarm_b": P["arm_b"][0], "farm_b": P["arm_b"][1]}
        tor = -P["lean"] * face                             # (the body leans forward)
        def rot(p, o, deg):
            r = np.radians(deg)
            x, y = p[0] - o[0], p[1] - o[1]
            return (o[0] + x * np.cos(r) - y * np.sin(r), o[1] + x * np.sin(r) + y * np.cos(r))
        J = {k: rig[k] for k in rig if isinstance(rig[k], tuple)}
        new = {"pelvis": J["pelvis"]}
        for k in ("neck", "head", "hip_f", "hip_b", "sh_f", "sh_b"):
            new[k] = rot(J[k], J["pelvis"], tor)
        delta = {}
        for name, j0, j1 in BONES:
            if name[1:4] == "arm":                         # (the arms: swung from where they are drawn)
                delta[name] = target[name] * face
            else:
                delta[name] = world(target[name]) - ang(J[j0], J[j1])
            delta[name] = (delta[name] + 180) % 360 - 180
            new[j1] = rot(J[j1], J[j0], delta[name])
            new[j1] = (new[j1][0] - J[j0][0] + new[j0][0], new[j1][1] - J[j0][1] + new[j0][1])
        ox, oy = np.zeros_like(xs), np.zeros_like(ys)
        for pi, name in enumerate(parts):
            m = part == pi
            if name == "torso":
                q = rot((xs[m], ys[m]), J["pelvis"], tor)
            else:
                j0 = [b for b in BONES if b[0] == name][0][1]
                q = rot((xs[m], ys[m]), J[j0], delta[name])
                q = (q[0] - J[j0][0] + new[j0][0], q[1] - J[j0][1] + new[j0][1])
            ox[m], oy[m] = q
        tf = [(tor, new["pelvis"])] + [(delta[name], new[j0]) for name, j0, j1 in BONES]
        frames.append((ox, oy, wv, cv, tf))
    pix = [(xs[part == pi], ys[part == pi], wv[part == pi], cv[part == pi]) for pi in range(len(parts))]
    piv = [rig["pelvis"]] + [rig[j0] for name, j0, j1 in BONES]
    return frames, pix, piv


def dancers():
    """the two dancers, each NFR frames: (warm, cool) levels, all on one scale and one floor"""
    warm, cool, masks = inks()
    allf = [pose_frames(r, warm, cool, m)[0] for r, m in zip(RIGS, masks)]
    s = (OUT_H - 1) / max(f[1].max() - f[1].min() for fr in allf for f in fr)
    out = []
    for fr in allf:
        x0 = min(f[0].min() for f in fr)
        x1 = max(f[0].max() for f in fr)
        ow = int(np.ceil((x1 - x0) * s)) + 2
        seq = []
        for ox, oy, wv, cv, _ in fr:
            fl = oy.max()                                  # (the lowest foot on the floor)
            S = 4                                          # (splatted 4 times as fine, then brought down)
            W_, H_ = ow * S, OUT_H * S
            gx = np.clip(((ox - x0) * s * S).astype(int), 0, W_ - 1)
            gy = np.clip(((oy - fl) * s * S + H_ - 1).astype(int), 0, H_ - 1)
            cw = np.zeros((H_, W_), np.float32); cc = np.zeros((H_, W_), np.float32)
            for ddx in (0, 1):
                for ddy in (0, 1):
                    xx_, yy_ = np.clip(gx + ddx, 0, W_ - 1), np.clip(gy + ddy, 0, H_ - 1)
                    np.maximum.at(cw, (yy_, xx_), wv)
                    np.maximum.at(cc, (yy_, xx_), cv)
            def down(c):
                v = c.reshape(OUT_H, S, ow, S).mean(axis=(1, 3))
                v = np.clip(v * 2.8, 0, 1) ** 0.75
                return np.clip(np.round(v * 3), 0, 3).astype(np.uint8)
            seq.append((down(cw), down(cc)))
        out.append(seq)
    return out


ORDER = (7, 8, 3, 4, 0, 1, 2, 5, 6)    # drawn: the back arm, the back leg, the body, the front leg, the front arm


def lift(v):
    v = np.clip(v * 2.8, 0, 1) ** 0.75
    return np.clip(np.round(v * 3), 0, 3).astype(np.uint8)


def rigged():
    """each dancer as its parts (drawn once, at the screen's scale: warm, cool levels; the joint it turns on) and, per
    frame, each part's turn (1/1024 of a turn, clockwise) and where its joint goes (in the dancer's box)"""
    warm, cool, masks = inks()
    res = [pose_frames(r, warm, cool, m) for r, m in zip(RIGS, masks)]
    s = (OUT_H - 1) / max(f[1].max() - f[1].min() for fr, _, _ in res for f in fr)
    out = []
    for frames, pix, piv in res:
        x0 = min(f[0].min() for f in frames)
        x1 = max(f[0].max() for f in frames)
        box_w = int(np.ceil((x1 - x0) * s)) + 2
        parts = []
        for (px, py, pw, pc), (jx, jy) in zip(pix, piv):
            bx0, by0 = px.min() - 2, py.min() - 2
            S = 4
            ow, oh = int(np.ceil((px.max() + 3 - bx0) * s)) + 1, int(np.ceil((py.max() + 3 - by0) * s)) + 1
            gx = np.clip(((px - bx0) * s * S).astype(int), 0, ow * S - 1)
            gy = np.clip(((py - by0) * s * S).astype(int), 0, oh * S - 1)
            cw = np.zeros((oh * S, ow * S), np.float32); cc = np.zeros_like(cw)
            for ddx in (0, 1):
                for ddy in (0, 1):
                    np.maximum.at(cw, (np.clip(gy + ddy, 0, oh * S - 1), np.clip(gx + ddx, 0, ow * S - 1)), pw)
                    np.maximum.at(cc, (np.clip(gy + ddy, 0, oh * S - 1), np.clip(gx + ddx, 0, ow * S - 1)), pc)
            lw = lift(cw.reshape(oh, S, ow, S).mean(axis=(1, 3)))
            lc = lift(cc.reshape(oh, S, ow, S).mean(axis=(1, 3)))
            parts.append(dict(w=ow, h=oh, lw=lw, lc=lc, ox=round((jx - bx0) * s * 16), oy=round((jy - by0) * s * 16)))
        tfs = []
        for ox_, oy_, wv, cv, tf in frames:
            fl = oy_.max()
            tfs.append([(round(deg * 1024 / 360) & 1023, round((pv[0] - x0) * s * 16), round(((pv[1] - fl) * s + OUT_H - 1) * 16))
                        for deg, pv in tf])
        out.append(dict(w=box_w, parts=parts, tf=tfs))
    return out


def simulate(d, f):
    """the FM-1's drawing of dancer d, frame f (as rfm_ui.c's rig_draw: each part turned, sampled back, bilinear)"""
    lw_o = np.zeros((OUT_H, d["w"]), np.uint8); lc_o = np.zeros_like(lw_o)
    for pi in ORDER:
        P, (a, jx, jy) = d["parts"][pi], d["tf"][f][pi]
        c, sn = np.cos(a * 2 * np.pi / 1024), np.sin(a * 2 * np.pi / 1024)
        ys, xs = np.mgrid[0:OUT_H, 0:d["w"]]
        dx, dy = xs - jx / 16, ys - jy / 16
        u = c * dx + sn * dy + P["ox"] / 16
        v = -sn * dx + c * dy + P["oy"] / 16
        ok = (u >= 0) & (v >= 0) & (u < P["w"] - 1) & (v < P["h"] - 1)
        ui, vi = np.floor(u[ok]).astype(int), np.floor(v[ok]).astype(int)
        fu, fv = u[ok] - ui, v[ok] - vi
        for src, dst in ((P["lw"], lw_o), (P["lc"], lc_o)):
            val = (src[vi, ui] * (1 - fu) * (1 - fv) + src[vi, ui + 1] * fu * (1 - fv) + src[vi + 1, ui] * (1 - fu) * fv
                   + src[vi + 1, ui + 1] * fu * fv)
            q = np.round(val).astype(np.uint8)
            cur = dst[ok]
            dst[ok] = np.maximum(cur, q)
    return lw_o, lc_o


def rle(lw, lc):
    flat = list((lw.astype(np.uint8) << 2 | lc).flatten())
    out, i = [], 0
    while i < len(flat):
        v, n = flat[i], 1
        while i + n < len(flat) and flat[i + n] == v and n < 16:
            n += 1
        out.append((n - 1) << 4 | v)
        i += n
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    ds = dancers()
    if sys.argv[1] == "preview":
        sys.path.insert(0, str(Path(__file__).parent))
        import gen_art
        d = Path(sys.argv[2]); d.mkdir(parents=True, exist_ok=True)
        gif = []
        for i in range(NFR * 2):
            im = Image.new("RGB", (240, 140), (0, 0, 0))
            a, b = ds[0][i % NFR], ds[1][(i + 2) % NFR]
            im.paste(gen_art.show(*a, gen_art.INKS["BLOOD"]), (120 - a[0].shape[1], 10))
            im.paste(gen_art.show(*b, gen_art.INKS["BLOOD"]), (120, 10))
            gif.append(im.resize((480, 280), Image.NEAREST))
        gif[0].save(d / "home_dance.gif", save_all=True, append_images=gif[1:], duration=83, loop=0)
        sheet = Image.new("RGB", (NFR * 130, 2 * 132), (0, 0, 0))
        for k in range(2):
            for i in range(NFR):
                sheet.paste(gen_art.show(*ds[k][i], gen_art.INKS["BLOOD"]), (i * 130, k * 132))
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(d / "home_frames.png")
        return
    if sys.argv[1] == "rig":                               # (the parts and turns: drawn by the firmware; heavier)
        sys.exit("rig: not used (the FM-1 draws the frames: 16 drawn here, cheap to send)")
    L = [f"/* generated by tools/gen_home.py: HOME's two skeletons (Maxime's drawing, assets/maxime/squelettes.jpg) dancing"
         f" the hakken: {NFR} frames, two steps of {NFR // 2} (the front leg, then the back one: knee up, kick out, stamp,"
         f" stand), {OUT_H} px high; warm << 2 | cool, run-length as rfm_art.h */",
         f"#define RFM_SKEL_H {OUT_H}", f"#define RFM_SKEL_NFR {NFR}", f"#define RFM_SKEL_IDLE {IDLE}"]
    tot = 0
    offs = []
    for k, seq in enumerate(ds):
        data, o = [], []
        for lw, lc in seq:
            o.append(len(data))
            data += rle(lw, lc)
        offs.append(o)
        tot += len(data)
        L.append(f"static const uint8_t RFM_SKEL_RLE_{k}[{len(data)}] = {{")
        for j in range(0, len(data), 32):
            L.append("    " + ", ".join(str(v) for v in data[j:j + 32]) + ",")
        L.append("};")
    L.append("static const uint8_t *const RFM_SKEL_RLE[2] = {RFM_SKEL_RLE_0, RFM_SKEL_RLE_1};")
    L.append("static const uint16_t RFM_SKEL_OFF[2][RFM_SKEL_NFR] = {" + ", ".join("{" + ", ".join(str(x) for x in o) + "}" for o in offs) + "};")
    L.append("static const uint8_t RFM_SKEL_W[2] = {%d, %d};" % (ds[0][0][0].shape[1], ds[1][0][0].shape[1]))
    L.append(f"/* {tot} bytes */")
    Path(sys.argv[2]).write_text("\n".join(L) + "\n")
    print(tot, "bytes")


if __name__ == "__main__":
    main()
