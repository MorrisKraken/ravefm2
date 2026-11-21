/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the inserts (tb_ins.h). */
#include "tb_ins.h"
#include "tb_dsp.h"
#include <string.h>

const char *const TB_INS_NAME[TB_NINS] = {"OFF", "OVERDRIVE", "DIST", "FUZZ", "LO-FI", "FOLD", "CHORUS", "FLANGER",
                                          "PHASER", "TREMOLO", "RING MOD", "FILTER LP", "FILTER HP", "FILTER BP",
                                          "AUTO-WAH", "COMPRESS."};
const char *const TB_INS_KNOB[TB_NINS][3] = {
    {0, 0, 0}, {"GAIN", "TONE", "MIX"}, {"GAIN", "TONE", "MIX"}, {"GAIN", "TONE", "MIX"}, {"BITS", "RATE", "MIX"},
    {"FOLD", "SYMM", "MIX"}, {"SPEED", "AMOUNT", "MIX"}, {"SPEED", "AMOUNT", "RETURN"},
    {"SPEED", "AMOUNT", "RETURN"}, {"SPEED", "AMOUNT", "SHAPE"}, {"FREQ", "SPEED", "MIX"},
    {"CUTOFF", "RESO", "DRIVE"}, {"CUTOFF", "RESO", "DRIVE"}, {"CUTOFF", "RESO", "DRIVE"}, {"DIR", "RESO", "MIX"},
    {"AMOUNT", "SPEED", "GAIN"}};

void tb_ins_init(tb_ins_t *s) { memset(s, 0, sizeof *s); }

/* a free LFO's step: 0.1 .. ~19 Hz from 0..127 */
static uint32_t lfo_inc(unsigned p)
{
    uint32_t mhz = (uint32_t)((100ull * tb_exp2((int32_t)p * 65536 * 76 / 1270)) >> 16);
    return (uint32_t)((uint64_t)mhz * 97391u / 1000u);
}
/* a one-pole's coefficient (Q15) for 40 Hz .. 16 kHz from 0..127 */
static int32_t pole(unsigned p)
{
    uint32_t hz = (uint32_t)((40ull * tb_exp2((int32_t)((int64_t)p * 65536 * 864 / 12700))) >> 16);
    int32_t c = (int32_t)(hz * 467u / 100u);           /* ~ 2 pi fc / fs, Q15 */
    return c > 32000 ? 32000 : c;
}
/* the state-variable filter, run twice a sample; f from 40 Hz .. 16 kHz */
static int32_t svf_f(unsigned p)
{
    uint32_t hz = (uint32_t)((40ull * tb_exp2((int32_t)((int64_t)p * 65536 * 864 / 12700))) >> 16);
    return 2 * tb_sin(hz * 24348u);
}
static int32_t svf(tb_ins_t *s, int32_t f, int32_t q, int32_t x, int mode)
{
    int32_t hi = 0, k;
    for (k = 0; k < 2; k++) {
        s->lo += (f * s->bd) >> 15;
        hi = x - s->lo - ((q * s->bd) >> 15);
        s->bd += (f * hi) >> 15;
        s->lo = tb_clamp(s->lo, -(1 << 20), 1 << 20);
        s->bd = tb_clamp(s->bd, -(1 << 20), 1 << 20);
    }
    return mode == 0 ? s->lo : mode == 1 ? hi : s->bd;
}
#if TB_HAS_INS_MOD
/* the delay line read d samples back (Q8: a fraction), linearly */
static int32_t tap(const tb_ins_t *s, uint32_t d8)
{
    uint32_t d = d8 >> 8, fr = d8 & 255u;
    int32_t a = s->buf[(s->w - d) & (TB_INS_BUF - 1u)], b = s->buf[(s->w - d - 1u) & (TB_INS_BUF - 1u)];
    return a + (((b - a) * (int32_t)fr) >> 8);
}
#endif

void tb_ins_run(const tb_ins_set_t *c, tb_ins_t *s, int32_t *x, unsigned n)
{
    unsigned i, t = c->type < TB_NINS ? c->type : 0u, p1 = c->p[0], p2 = c->p[1], p3 = c->p[2];
    int32_t mix = (int32_t)p3 * 258;
    if (!t)
        return;
    switch (t) {
    case TB_INS_OVERDRIVE: case TB_INS_DISTO: case TB_INS_FUZZ: {
        int32_t g = 256 + (int32_t)p1 * (t == TB_INS_OVERDRIVE ? 24 : t == TB_INS_DISTO ? 48 : 96), tc = pole(40u + p2 * 87u / 127u);
        for (i = 0; i < n; i++) {
            int32_t d = tb_clamp(x[i], -65535, 65535) * g >> 8, y;
            if (t == TB_INS_OVERDRIVE)
                y = tb_tanh(d);
            else if (t == TB_INS_DISTO)                 /* hard and uneven: the top clipped harder */
                y = d > 0 ? tb_clamp(d, 0, 20000) + (tb_tanh(d - 20000 > 0 ? d - 20000 : 0) >> 2) : tb_tanh(d);
            else                                        /* FUZZ: driven into a square */
                y = tb_tanh(tb_tanh(d) * 3);
            s->lp += ((y - s->lp) * tc) >> 15;
            y = s->lp * 3 / 4;
            x[i] += (int32_t)(((int64_t)(y - x[i]) * mix) >> 15);
        }
        break;
    }
#if TB_HAS_INS_LOFI
    case TB_INS_LOFI: {
        unsigned bits = 16u - p1 * 14u / 127u, hold = 1u + p2 * 31u / 127u;
        int32_t mask = ~((1 << (16u - bits)) - 1);
        for (i = 0; i < n; i++) {
            if (++s->hold >= (int32_t)hold)
                s->hold = 0, s->lp = tb_clamp(x[i], -32768, 32767) & mask;
            x[i] += (int32_t)(((int64_t)(s->lp - x[i]) * mix) >> 15);
        }
        break;
    }
    case TB_INS_FOLD: {
        int32_t g = 256 + (int32_t)p1 * 14, bias = ((int32_t)p2 - 64) * 256;
        for (i = 0; i < n; i++) {
            int32_t v = (tb_clamp(x[i], -65535, 65535) * g >> 8) + bias, y;
            uint32_t u = (uint32_t)(v + 32768) & 131071u;    /* a triangle: the signal folded back at the edges */
            y = u < 65536u ? (int32_t)u - 32768 : 98303 - (int32_t)u;
            x[i] += (int32_t)(((int64_t)(y * 3 / 4 - x[i]) * mix) >> 15);
        }
        break;
    }
#endif
#if TB_HAS_INS_MOD
    case TB_INS_CHORUS: case TB_INS_FLANGER: {
        uint32_t inc = lfo_inc(p1 / 2u);
        uint32_t base = t == TB_INS_CHORUS ? 529u * 256u : 44u * 256u;          /* 12 ms, 1 ms */
        uint32_t dep = (t == TB_INS_CHORUS ? 264u : 176u) * 256u * p2 / 127u;  /* +- 6 ms, +- 4 ms */
        int32_t fb = t == TB_INS_FLANGER ? (int32_t)p3 * 230 : 0, m = t == TB_INS_CHORUS ? mix : 16384;
        for (i = 0; i < n; i++) {
            uint32_t d = base + (uint32_t)(((int64_t)(tb_sin(s->ph) + 32768) * dep) >> 16);
            int32_t y = tap(s, d);
            s->ph += inc;
            s->buf[s->w & (TB_INS_BUF - 1u)] = (int16_t)tb_clamp(x[i] + ((y * fb) >> 15), -32768, 32767);
            s->w++;
            x[i] += ((y - x[i]) * m) >> 15;
        }
        break;
    }
#endif
    case TB_INS_PHASER: {
        /* four first-order all-passes swept together from 150 Hz up to DEPTH's 5 octaves (the notches where one
         * hears them; their coefficient a = (t - 1) / (t + 1), t = tan(pi f / fs) ~ pi f / fs), every 8 samples */
        uint32_t inc = lfo_inc(p1 / 2u);
        int32_t fb = (int32_t)p3 * 170, a = s->lp, da = 0;   /* (a glides to each new value: no steps heard) */
        for (i = 0; i < n; i++) {
            int32_t v, k;
            if (!(i & 7u)) {
                int32_t l = tb_sin(s->ph) + 32768, want;
                uint32_t hz = (uint32_t)((150ull * tb_exp2(l * (int32_t)p2 * 5 / 127)) >> 16);   /* (l x p2 x 5 < 2^26:
                                                       * a 32-bit division, the same - the FM-1's 64-bit one is a loop) */
                int32_t tq = (int32_t)(hz * 2334u / 1000u);           /* pi f / fs, Q15 */
                want = (tq - 32768) * 32768 / (tq + 32768);
                if (a == 0)
                    a = want;
                da = (want - a) / 8;
            }
            a += da;
            s->ph += inc;
            v = x[i] + ((s->fb * fb) >> 15);
            for (k = 0; k < 4; k++) {
                int32_t y = tb_mul15(v, a) + s->ap[k];      /* (32-bit, the same: tb_dsp.h) */
                s->ap[k] = tb_clamp(v - tb_mul15(y, a), -(1 << 18), 1 << 18);
                v = y;
            }
            {                                        /* (the return bent softly, never clipped: no crackle when it rings) */
                int32_t av = v < 0 ? -v : v;
                s->fb = v < 0 ? -tb_divq15((uint32_t)av, 32768u + (uint32_t)av / 2u) : tb_divq15((uint32_t)av, 32768u + (uint32_t)av / 2u);
            }
            x[i] = (x[i] + v) / 2;
        }
        s->lp = a;
        break;
    }
#if TB_HAS_INS_MOD
    case TB_INS_TREMOLO: {
        uint32_t inc = lfo_inc(p1);
        for (i = 0; i < n; i++) {
            int32_t sn = tb_sin(s->ph), sq = s->ph < 0x80000000u ? 32767 : -32767;
            int32_t w = sn + (((sq - sn) * (int32_t)p3) >> 7), g = 32767 - (((32767 - w) / 2) * (int32_t)p2 >> 7);
            s->ph += inc;
            x[i] = (int32_t)(((int64_t)x[i] * g) >> 15);
        }
        break;
    }
    case TB_INS_RING: {                                 /* a sine times the sound; VITESSE sways its pitch */
        uint32_t hz = (uint32_t)((20ull * tb_exp2((int32_t)((int64_t)p1 * 65536 * 664 / 12700))) >> 16);
        uint32_t inc = hz * 97391u, li = lfo_inc(p2 / 2u), swq = (uint32_t)((uint64_t)(inc / 4u) * p2 / 127u);
        for (i = 0; i < n; i++) {
            int32_t m = tb_sin(s->ph), y;
            uint32_t sway = (uint32_t)(((uint64_t)(uint32_t)(tb_sin((uint32_t)s->fb) + 32768) * swq) >> 16);
            s->ph += inc + sway;
            s->fb = (int32_t)((uint32_t)s->fb + li);
            y = (int32_t)(((int64_t)x[i] * m) >> 15);
            x[i] += (int32_t)(((int64_t)(y - x[i]) * mix) >> 15);
        }
        break;
    }
#endif
    case TB_INS_LP: case TB_INS_HP: case TB_INS_BP: {
        /* a zero-delay state-variable filter (trapezoidal, A. Simper's): stable however far the resonance is pushed,
         * at any cutoff (the old one ran away at its top). g = tan(pi f / fs) from a sine and a cosine, per block */
        uint32_t hz = (uint32_t)((40ull * tb_exp2((int32_t)((int64_t)p1 * 65536 * 864 / 12700))) >> 16), ph;
        int32_t sn, cs, gq, k = 8192 - (int32_t)p2 * 7600 / 127, den, a1, a2, a3, g = 4096 + (int32_t)p3 * 96;
        int mode = t == TB_INS_LP ? 0 : t == TB_INS_HP ? 1 : 2;
        if (hz > 18000u)
            hz = 18000u;
        ph = hz * 48696u;                             /* (pi hz / fs, as a phase: 2^31 / 44100 = 48696) */
        sn = tb_sin(ph), cs = tb_sin(ph + 0x40000000u);
        gq = (int32_t)(((int64_t)sn << 12) / (cs > 400 ? cs : 400));   /* Q12 */
        den = 4096 + (int32_t)(((int64_t)gq * (gq + k)) >> 12);
        a1 = (int32_t)((4096u << 13) / (uint32_t)den);
        a2 = (int32_t)(((int64_t)a1 * gq) >> 12);
        a3 = (int32_t)(((int64_t)a2 * gq) >> 12);
        for (i = 0; i < n; i++) {
            int32_t in = p3 ? tb_tanh((tb_clamp(x[i], -65535, 65535) * g) >> 12) : x[i], v1, v2, v3;
            in = tb_clamp(in, -100000, 100000);
            v3 = in - s->bd;
            v1 = (int32_t)(((int64_t)a1 * s->lo + (int64_t)a2 * v3) >> 13);
            v2 = s->bd + (int32_t)(((int64_t)a2 * s->lo + (int64_t)a3 * v3) >> 13);
            s->lo = tb_clamp(2 * v1 - s->lo, -(1 << 21), 1 << 21);
            s->bd = tb_clamp(2 * v2 - s->bd, -(1 << 21), 1 << 21);
            x[i] = mode == 0 ? v2 : mode == 2 ? v1 : in - (int32_t)(((int64_t)k * v1) >> 12) - v2;
        }
        break;
    }
#if TB_HAS_INS_LOFI
    case TB_INS_WAH: {
        int32_t q = 30000 - (int32_t)p2 * 220;
        for (i = 0; i < n; i++) {
            int32_t a = x[i] < 0 ? -x[i] : x[i], f, y;
            s->env += a > s->env ? (a - s->env) >> 5 : -((s->env - a) >> 11);
            f = svf_f(30u + (uint32_t)tb_clamp((s->env >> 7) * (int32_t)p1 / 64, 0, 90));
            y = svf(s, f, q, x[i], 2);
            x[i] += (int32_t)(((int64_t)(y * 2 - x[i]) * mix) >> 15);
        }
        break;
    }
#endif
    case TB_INS_COMP: {
        int32_t th = (int32_t)((32767ull * tb_exp2(-(int32_t)p1 * 65536 * 5 / 127)) >> 16);
        int32_t rel = 4 + (int32_t)(127 - p2) / 8;
        /* GAIN: up to what the compression took off a full-scale sound (no more: louder than before is clipping) */
        int32_t top = th + (32767 - th) / 4, mk = 4096 + (int32_t)(((int64_t)(32767 - top) * 4096 / (top | 1)) * p3 / 127);
        for (i = 0; i < n; i++) {
            int32_t a = x[i] < 0 ? -x[i] : x[i], g = 32767;
            s->env += a > s->env ? (a - s->env) >> 2 : -((s->env - a) >> rel);
            if (s->env > th)                             /* 4:1 above the threshold */
                g = tb_divq15((uint32_t)(th + (s->env - th) / 4), (uint32_t)(s->env | 1));
            x[i] = (int32_t)(((int64_t)x[i] * g >> 15) * mk >> 12);
        }
        break;
    }
    default:
        break;
    }
}
