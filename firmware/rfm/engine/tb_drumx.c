/* SPDX-License-Identifier: GPL-3.0-only
 * DRUM-X: Copyright (C) 2026 Juan Ignacio Antonucci (tb_drumx_voice.inc, as it is, GPL-3.0)
 * RAVE FM 2's 909 on it (tb_drumx.h): the eleven pieces on DRUM-X's eight sounds, the kit's knobs and each piece's. */
#include <stdint.h>
#include <string.h>
#include "tb_drumx.h"
#include "tb_rave_tab.h"

/* what DRUM-X takes from its firmware: the rate, the control block, the tables (tb_rave_tab.h's are the same), the
 * fixed-point helpers (as its dsp.c and core.h have them) */
#define FS 44100u
#define CTL 64u
#define SINE RV_SINE
#define TANH_Q15 RV_TANH_Q15
#define SVF_G RV_SVF_G
#define pitch_inc rv_pitch_inc
static inline int64_t mul64(int32_t a, int32_t b) { return (int64_t)a * b; }
static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
static inline uint32_t noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}
typedef struct { int32_t a1, a2, a3; } tsvf_t;
static inline void tsvf_coef_k(tsvf_t *c, int32_t cut, int32_t k)
{
    int32_t i, g, den;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
#include "tb_drumx_voice.inc"

_Static_assert(sizeof(dx_voice_t) <= sizeof(((tb_dx_t *)0)->v[0]), "tb_dx_t: room for a DRUM-X hit");

/* the eleven pieces on the kit's eight sounds: the sound, its tune (1/16 semitones) and decay as moved */
static const struct { uint8_t lane; int16_t tune16; int8_t dofs; } PIECE[TB_DX_PIECES] = {
    {DV_KICK, 0, 0}, {DV_SNARE, 0, 0}, {DV_CLAP, 0, 0}, {DV_HATC, 0, 0}, {DV_HATO, 0, 0}, {DV_TOM, 0, 0},
    {DV_RIM, 0, 0}, {DV_HATO, -80, 34}, {DV_TOM, -80, 6}, {DV_TOM, 80, -4}, {DV_BELL, -32, 20}};
static const char *const KITN[TB_DX_NKIT] = {"909 VRAI", "DRUM-X", "909 CLUB", "808 BOOM", "LOFI", "INDUSTRY", "ACOUSTIC", "DUB",
                                            "TECHNO", "BREAKS", "HIP HOP", "LATIN"};
static int kit_now = -1;

const char *tb_dx_kit_name(unsigned k) { return KITN[k % TB_DX_NKIT]; }
void tb_dx_init(tb_dx_t *d) { memset(d, 0, sizeof *d); }

void tb_dx_defaults(uint8_t *p, uint8_t *d9)
{
    unsigned i;
    p[TBX_TUNE] = 64, p[TBX_DECAY] = 64, p[TBX_TONE] = 64, p[TBX_NOISE] = 64;
    p[TBX_KIT] = 0, p[TBX_MORPH] = 0, p[TBX_FM] = 0, p[TBX_DRIVE] = 0;   /* (909 VRAI: the recordings) */
    for (i = 0; i < TB_DX_PIECES; i++) {
        d9[i * D9_N + D9_TUNE] = 64, d9[i * D9_N + D9_DECAY] = 64;
        d9[i * D9_N + D9_MORPH] = 64, d9[i * D9_N + D9_LEVEL] = 100;
    }
}

void tb_dx_hit(tb_dx_t *d, tb909_t *smp, const uint8_t *p, unsigned piece, unsigned vel)
{
    dx_voice_t *v;
    if (piece >= TB_DX_PIECES)
        return;
    if (p[TBX_KIT] % TB_DX_NKIT == 0u) {               /* the recordings (and DRUM-X under them, for the MORPH) */
        tb909_hit(smp, piece, vel);
        if (!p[TBX_MORPH])
            return;
    }
    v = (dx_voice_t *)(void *)d->v[piece];
    dx_trigger(v);
    v->sv = (uint8_t)(vel > 127u ? 127u : vel);
    d->vel[piece] = (uint8_t)(vel > 127u ? 127u : vel);
    if (piece == 3u)                                       /* the closed hat chokes the open one (and the crash on it) */
        dx_choke((dx_voice_t *)(void *)d->v[4]);
}

static int32_t drive_of(int32_t s, int32_t drive) { return drive ? softclip(s * (32 + drive) / 32) * 3 / 4 : s; }

void tb_dx_render(tb_dx_t *d, tb909_t *smp, const uint8_t *p, const uint8_t *d9, const uint8_t *adsr, int32_t *out,
                  unsigned n)
{
    static int32_t y[CTL];
    unsigned pc, kk = p[TBX_KIT] % TB_DX_NKIT, k, at, i;
    int32_t drive = p[TBX_DRIVE], dxg = 4096, morph_in = p[TBX_MORPH];
    if (kk == 0u) {                                    /* the recordings: TUNE DECAY TONE, NOISE their attack (SNAP) */
        uint8_t sp[TB909_NPARAM];
        unsigned m = (unsigned)p[TBX_MORPH], wet = 128u - (m * 128u + 63u) / 127u;
        sp[TB909_TUNE] = p[TBX_TUNE], sp[TB909_DECAY] = p[TBX_DECAY], sp[TB909_TONE] = p[TBX_TONE];
        sp[TB909_SNAP] = p[TBX_NOISE];
        for (at = 0; at < n; at += CTL) {
            unsigned c = n - at < CTL ? n - at : CTL;
            memset(y, 0, c * sizeof *y);
            tb909_render_x(smp, sp, adsr, 0, d9, wet, y, c);
            for (i = 0; i < c; i++)
                out[at + i] += drive_of(y[i], drive);
        }
        if (!m)
            return;
        k = 1u, dxg = (int32_t)(m * 4096u / 127u), morph_in = 0;   /* (the MORPH: towards DRUM-X's 909 CLUB, its A) */
    } else
        k = kk - 1u;
    if ((int)k != kit_now) {                               /* (the kit: DRUM-X reads it from dx_kit) */
        memcpy(dx_kit, DX_KITS[k].lane, sizeof dx_kit);
        memcpy(dx_lay, DX_KITS[k].lay, sizeof dx_lay);
        dx_tone = 0, dx_hands = 0;
        kit_now = (int)k;
    }
    for (pc = 0; pc < TB_DX_PIECES; pc++) {
        dx_voice_t *v = (dx_voice_t *)(void *)d->v[pc];
        const uint8_t *q = d9 + pc * D9_N;
        int32_t morph, tune16, dofs, lv, g;
        if (!v->live && !v->trig)
            continue;
        morph = clamp(morph_in + ((int32_t)q[D9_MORPH] - 64) * 2, 0, 127);
        tune16 = ((int32_t)p[TBX_TUNE] - 64) * 3 + ((int32_t)q[D9_TUNE] - 64) * 6 + PIECE[pc].tune16;   /* (+-12, +-24 semitones) */
        dofs = clamp(((int32_t)p[TBX_DECAY] - 64) + ((int32_t)q[D9_DECAY] - 64) + PIECE[pc].dofs, -64, 63);
        lv = q[D9_LEVEL];
        g = (lv * lv * 15) / 32 * (64 + d->vel[pc] / 2) / 128;   /* (square law; 100: ~4700, the old 909's level; the touch) */
        g = (g * dxg) >> 12;
        for (at = 0; at < n; at += CTL) {
            unsigned m = n - at < CTL ? n - at : CTL;
            dx_run(&dx_kit[PIECE[pc].lane], v, morph, tune16, dofs, (int32_t)p[TBX_TONE] - 64, (int32_t)p[TBX_NOISE] - 64, 0,
                   p[TBX_FM], y, m);
            for (i = 0; i < m; i++)
                out[at + i] += (drive_of(y[i], drive) * g) >> 12;
        }
    }
}
