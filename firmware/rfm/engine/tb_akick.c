/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM: the kicks of the first version (the Atelier, Rave FM on Felucca, October 2026: firmware/src/eng_kick.c,
 * written there by tools/gen_kick.py), brought back as they were: its 22 types, its set-up and its voice, step for step
 * (only renamed into this file: the Felucca DSP blocks they use are below, dsp.c's, MIT, Copyright (C) 2026 Leo
 * Kuroshita, Hugelton Instruments). tb_kick.c plays these when a kick's TYPE is one of the Atelier's (tb_akick.h). */
#include "tb_akick.h"
#include "tb_rave_tab.h"
#include <string.h>

#define CTL 32u
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = RV_SINE[i], b = RV_SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);
    return s < 65536 ? s - 32768 : 98303 - s;
}
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = RV_TANH_Q15[256];
    else
        y = RV_TANH_Q15[i] + (((RV_TANH_Q15[i + 1] - RV_TANH_Q15[i]) * (a & 255)) >> 8);
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

typedef struct {
    uint8_t note;                 /* the note it lands on (MIDI) */
    uint16_t f_sw;                /* the drop above it (Hz Q4) */
    uint32_t sw_us;               /* its time constant */
    uint16_t f_sw2;               /* a second, slower drop (Hz Q4, 0 = none) */
    uint32_t sw2_us;
    uint16_t len_ms;              /* how long it lasts at 200 BPM (the same part of a beat at any tempo) */
    uint8_t env;                  /* 0: held (a 35 % sag), 1: decaying over the length */
    uint8_t rel_ms;               /* the release after it */
    uint8_t wave;                 /* 0: rounded triangle (909), 1: sine (808) */
    uint16_t d1, asym, cut;       /* drive 1 (x Q4), its imbalance (Q8, 0..243), the low cut (Hz) */
    uint16_t mid, mid_hz;         /* the mid bump (x Q8), its centre (Hz) */
    uint16_t d2;                  /* drive 2 (x Q4) */
    uint16_t tone, click;         /* the low-pass (Hz), the tick (Q8) */
    uint16_t hold_ms;             /* the first drop held this long at 200 BPM before it falls (0: at once): the
                                   * uptempo / frenchcore 'tok', a tone held high then dropped */
    uint16_t body;                /* a second layer (Q8, 0: none): a sine on the note itself from the hit, under the
                                   * drop and into the drives with it (the layered uptempo kick: its low end is there
                                   * from the first instant, and the drives mix the two into one broad attack) */
    uint16_t tail_tone;           /* the low-pass after the hold (Hz, 0: TONE's all along): the attack bright, the
                                   * tail rounder, as an uptempo kick is mixed (it closes over ~20 ms; TONE moves both) */
} kick_shape_t;

enum { KT_ROND, KT_CREUX, KT_PROF, KT_NASAL, KT_FUZZ, KT_CASSE, KT_ASYM, KT_GABBR, KT_MOKUM, KT_TOK, KT_TERRO, KT_SPEED, KT_UPTMP, KT_DOEF, KT_EARLY, KT_BELGE, KT_DOOM, KT_909_, KT_BOOM, KT_JUMP, KT_JUMPD, KT_BOUNC, KT_COUNT };
static const char *const N_KICK_TYPE[KT_COUNT] = {"ROND", "CREUX", "PROF", "NASAL", "FUZZ", "CASSE", "ASYM", "GABBR", "MOKUM", "TOK", "TERRO", "SPEED", "UPTMP", "DOEF", "EARLY", "BELGE", "DOOM", "909", "BOOM", "JUMP", "JUMPD", "BOUNC"};
#ifndef KICK_TYPES_CONST
#define KICK_TYPES_CONST const                       /* (a host fit, tests/kick_fit.c, sets its own) */
#endif
static const kick_shape_t KICK_TYPES[KT_COUNT] = {
    /* note, f_sw, sw_us, f_sw2, sw2_us, len_ms, env, rel_ms, wave, d1, asym, cut, mid, mid_hz, d2, tone, click, hold_ms, body, tail_tone */
    [KT_ROND] = {30, 2240, 35000, 0, 0, 300, 0, 12, 0, 128, 0, 25, 0, 800, 24, 2200, 0, 0, 0, 0},   /* F#1 */
    [KT_CREUX] = {31, 2880, 25000, 0, 0, 260, 0, 12, 0, 960, 0, 90, 768, 450, 48, 3500, 0, 0, 0, 0},   /* G1 */
    [KT_PROF] = {30, 2880, 28000, 0, 0, 280, 0, 12, 0, 800, 0, 80, 896, 350, 48, 3000, 0, 0, 0, 0},   /* F#1 */
    [KT_NASAL] = {31, 3200, 24000, 0, 0, 250, 0, 12, 0, 960, 0, 100, 1024, 600, 64, 4000, 51, 0, 0, 0},   /* G1 */
    [KT_FUZZ] = {31, 4000, 22000, 0, 0, 270, 0, 12, 0, 3200, 154, 150, 1024, 3000, 320, 14000, 205, 0, 0, 0},   /* G1 */
    [KT_CASSE] = {31, 4000, 22000, 0, 0, 270, 0, 12, 0, 4800, 230, 200, 1280, 3500, 480, 16000, 256, 0, 0, 0},   /* G1 */
    [KT_ASYM] = {31, 3200, 22000, 0, 0, 270, 0, 12, 0, 960, 205, 70, 384, 1000, 64, 6000, 77, 0, 0, 0},   /* G1 */
    [KT_GABBR] = {33, 5600, 12000, 0, 0, 240, 0, 12, 0, 1280, 77, 120, 640, 1000, 96, 6000, 128, 0, 0, 0},   /* A1 */
    [KT_MOKUM] = {34, 6400, 9000, 0, 0, 150, 1, 10, 0, 2400, 60, 100, 640, 900, 96, 8000, 140, 0, 0, 0},   /* A#1 */
    [KT_TOK] = {36, 4800, 10000, 0, 0, 160, 1, 10, 0, 1280, 0, 150, 1024, 1100, 128, 7000, 100, 0, 0, 0},   /* C2 */
    [KT_TERRO] = {37, 9600, 6000, 0, 0, 110, 1, 6, 0, 4800, 120, 140, 768, 1500, 192, 12000, 200, 0, 0, 0},   /* C#2 */
    [KT_SPEED] = {40, 14400, 4000, 0, 0, 60, 1, 4, 0, 3200, 0, 180, 512, 2000, 128, 14000, 256, 0, 0, 0},   /* E2 */
    [KT_UPTMP] = {37, 8000, 12000, 2400, 45000, 260, 0, 10, 0, 4000, 0, 40, 768, 1100, 320, 12000, 150, 0, 0, 0},   /* C#2 */
    [KT_DOEF] = {30, 2560, 27000, 0, 0, 290, 0, 12, 0, 1280, 0, 80, 0, 1000, 48, 6000, 0, 0, 0, 0},   /* F#1 */
    [KT_EARLY] = {31, 2880, 25000, 0, 0, 280, 0, 12, 0, 1600, 50, 60, 384, 700, 48, 5000, 60, 0, 0, 0},   /* G1 */
    [KT_BELGE] = {31, 3520, 20000, 0, 0, 330, 1, 30, 0, 320, 0, 30, 256, 500, 32, 5000, 80, 0, 0, 0},   /* G1 */
    [KT_DOOM] = {26, 1920, 40000, 0, 0, 520, 1, 80, 1, 192, 0, 15, 0, 1000, 32, 1800, 0, 0, 0, 0},   /* D1 */
    [KT_909_] = {31, 2880, 22000, 0, 0, 300, 0, 12, 0, 32, 0, 20, 0, 1000, 16, 6000, 77, 0, 0, 0},   /* G1 */
    [KT_BOOM] = {28, 1440, 30000, 0, 0, 600, 1, 100, 1, 64, 0, 15, 0, 1000, 16, 3000, 0, 0, 0, 0},   /* E1 */
    [KT_JUMP] = {31, 3680, 25000, 1280, 40000, 270, 0, 12, 0, 640, 0, 4, 0, 1000, 64, 10000, 60, 0, 0, 0},   /* G1 */
    [KT_JUMPD] = {31, 4160, 22000, 1600, 40000, 270, 0, 12, 0, 1920, 0, 5, 384, 1800, 128, 12000, 120, 0, 0, 0},   /* G1 */
    [KT_BOUNC] = {33, 4160, 15000, 960, 30000, 180, 0, 8, 0, 960, 0, 6, 256, 1400, 64, 11000, 90, 0, 0, 0},   /* A1 */
};

/* ------------------------------------------------------- fixed point --- */
/* 2^(x / 65536) in Q16, x in -16..14 octaves (a cubic on the fraction: within 0.01 %) */
static uint32_t kick_exp2(int32_t x)
{
    int32_t n = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu, p;
    p = 65536u + ((f * (45556u + ((f * (14822u + ((f * 5160u) >> 16))) >> 16))) >> 16);   /* 2^f, f Q16 */
    if (n >= 15)
        return 0x7FFFFFFFu;
    if (n >= 0)
        return p << n;
    return n <= -31 ? 0u : p >> -n;
}
/* the per-sample one-pole coefficient exp(-2 pi f / FS), Q15 */
static int32_t kick_pole(uint32_t hz)
{
    return (int32_t)(kick_exp2(-(int32_t)(hz * 13471u / 1000u)) >> 1);
}
/* the per-block decay of a time constant of us microseconds, Q16 (-CTL / (tau FS) / ln 2 octaves) */
static uint32_t kick_blk_decay(uint32_t us)
{
    return kick_exp2(-(int32_t)(68606880u / (us < 500u ? 500u : us)));
}
/* 2 sin(pi f / FS) for the Chamberlin state-variable filter, Q15 */
static int32_t kick_svf_f(uint32_t hz)
{
    return 2 * sine_i(hz * 48696u);
}
/* log2(x / 65536) in Q16, x >= 65536 (a quadratic on the mantissa: within 0.01 octave) */
static int32_t kick_log2(uint32_t x)
{
    int32_t n = 0;
    uint32_t u;
    while (x >= 131072u) {
        x >>= 1;
        n++;
    }
    u = x - 65536u;                                 /* mantissa - 1, Q16 */
    return (n << 16) + (int32_t)((u * (94535u - ((u * 28999u) >> 16))) >> 16);
}
/* the square root of x (integer) */
static uint32_t kick_isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}
/* the longest time constant (us) after which a drop of f_sw above f_end is within an eighth of a semitone (0.72 %) at
 * half of len_ms: 0.5 len / ln(f_sw / 0.0072 f_end) */
static uint32_t kick_land(int32_t f_sw, int32_t f_end, int32_t len_ms)
{
    uint32_t r = (uint32_t)(((int64_t)f_sw * 139 << 16) / (f_end > 0 ? f_end : 1));
    int32_t l2;
    if (r <= 65536u)
        return 0xFFFFFFFFu;
    l2 = kick_log2(r);
    return (uint32_t)(((uint64_t)(uint32_t)len_ms * 47277000u) / (uint32_t)(l2 > 64 ? l2 : 64));
}
/* scale a by 2^(oct65536 / 65536), clamped to lo..hi */
static int32_t kick_scale(int32_t a, int32_t oct65536, int32_t lo, int32_t hi)
{
    return clamp((int32_t)(((int64_t)a * kick_exp2(oct65536)) >> 16), lo, hi);
}

/* ------------------------------------------------------ coefficients --- */
typedef struct {
    uint32_t inc_end, inc_sw, inc_sw2;   /* phase increments of the note and of the two drops */
    uint32_t g_sw, g_sw2;         /* per block: the drops' decays (Q16) */
    uint32_t g_dec, g_rel, g_choke;      /* per block: the decay over the length, the release, the 3 ms choke */
    uint32_t len;                 /* the length, samples */
    uint32_t hold;                /* the first drop held (samples) */
    int32_t body;                 /* the body layer (Q8) */
    int32_t a_tone2;              /* the low-pass after the hold (1 - pole, Q15) */
    uint8_t env, wave, pad[2];
    int32_t d1p, d1n, asg;        /* drive 1 above / below zero (Q4), the level below zero (Q15) */
    int32_t a_cut, a_dc;          /* one-pole high-pass poles (Q15) */
    int32_t f_mid, mid;           /* the mid bump: SVF frequency (Q15), amount (Q8) */
    int32_t d2, n2;               /* drive 2 (Q4), its level normalisation (Q15) */
    int32_t a_tone;               /* the low-pass's 1 - pole (Q15) */
    int32_t f_clk, click;         /* the tick: SVF frequency (Q15), amount (Q8) */
    int32_t sub, subg;            /* the clean sine (Q8, made up for the low cut at the note), the mix's gain with it (Q15) */
} kick_coef_t;

typedef struct {                  /* a hit */
    uint32_t t, ph, sph, sw, sw2; /* samples since the hit, the phases (the kick, the sub), the drops left (Q16) */
    uint32_t bph;                 /* the body layer's phase */
    int32_t at;                   /* the low-pass's coefficient now (0: not started) */
    int32_t lvl;                  /* the decaying level (env 1) or the release's (Q15), 0 before */
    int32_t clk;                  /* the tick's level (Q16) */
    int32_t seed;
    int32_t hx, hy, mlo, mbd, t1, t2, dx, dy, clo, cbd;   /* filter states */
    int32_t shx, shy, sdx, sdy;   /* the sub's low cut and DC blocker (the kick's, for the same phase) */
    uint8_t live, choke, rel, pad;
} kick_hit_t;

__attribute__((noinline)) static void kick_setup(kick_coef_t *c, const int16_t *p, int32_t keys, uint32_t beat)
{
    const kick_shape_t *s = &KICK_TYPES[clamp(p[0], 0, KT_COUNT - 1)];
    int32_t semis = clamp(p[1], 0, 48) - 24 + keys, pun = p[2] - 64, lon = p[3] - 64, dis = p[4] - 64,
            cre = p[5] - 64, ton = p[6] - 64;
    int32_t note = clamp((int32_t)s->note + semis, 12, 72), up = (note - (int32_t)s->note) * 65536 / 12;
    int32_t f_end = kick_scale(440 * 16, (note - 69) * 65536 / 12, 8 * 16, 1100 * 16);   /* the note itself */
    int32_t f_sw = kick_scale(s->f_sw, up + pun * 2048, 0, 3000 * 16);   /* the drops follow the note */
    int32_t f_sw2 = kick_scale(s->f_sw2, up + pun * 2048, 0, 1500 * 16);
    int32_t sw_us = kick_scale((int32_t)s->sw_us, pun * 614, 2000, 120000);
    int32_t sw2_us = kick_scale(s->sw2_us ? (int32_t)s->sw2_us : 10000, pun * 614, 3000, 200000);
    int32_t len = kick_scale((int32_t)((uint32_t)s->len_ms * (beat ? beat : 13230u) / 13230u), lon * 1638, 25, 2000);
    int32_t d1 = kick_scale(s->d1, dis * 4096, 16, 400 * 16);
    int32_t d2 = kick_scale(s->d2, dis * 1638, 16, 40 * 16);
    int32_t as = s->asym ? clamp((int32_t)s->asym + dis * 77 / 128, 0, 243) : 0;   /* (a symmetric type stays so) */
    int32_t mid = clamp((int32_t)s->mid + cre * 16, 0, 1536);
    int32_t tone = kick_scale(s->tone, ton * 2731, 800, 18000);
    int32_t ck = kick_scale(s->click, ton * 1365, 0, 384);
    int32_t sb = clamp(p[7], 0, 127) * 3;           /* SUB: a volume, 0 none .. 127 about 1.5 x */
    uint32_t dc = s->cut < 18u ? s->cut : 18u, fe = (uint32_t)f_end >> 4, mk;
    int32_t ref;
    int32_t hold = (int32_t)((uint32_t)s->hold_ms * (beat ? beat : 13230u) / 13230u);   /* (follows the tempo) */
    {   /* land on the note within what is heard: the length, or the beat when the next hit comes first (the held drop
         * within what is left after its hold, held at most a third of it) */
        int32_t heard = len < (int32_t)((beat ? beat : 13230u) * 10u / 441u) ? len : (int32_t)((beat ? beat : 13230u) * 10u / 441u);
        uint32_t l1, l2 = kick_land(f_sw2, f_end, heard);
        hold = clamp(hold, 0, heard / 3);
        l1 = kick_land(f_sw, f_end, heard - hold);
        sw_us = clamp(sw_us, 2000, (int32_t)(l1 < 120000u ? l1 : 120000u));
        sw2_us = clamp(sw2_us, 2000, (int32_t)(l2 < 200000u ? l2 : 200000u));
    }
    /* the sub's make-up for the low cut and the DC blocker at the note: sqrt(1 + (fc / f)^2), each (Q8; 3 x at most) */
    fe = fe ? fe : 1u;
    mk = kick_isqrt(65536u + ((uint32_t)s->cut * s->cut << 16) / (fe * fe)) * kick_isqrt(65536u + (dc * dc << 16) / (fe * fe)) >> 8;
    c->inc_end = (uint32_t)f_end * 6087u;
    c->inc_sw = (uint32_t)f_sw * 6087u;
    c->inc_sw2 = (uint32_t)f_sw2 * 6087u;
    c->g_sw = kick_blk_decay((uint32_t)sw_us);
    c->g_sw2 = kick_blk_decay((uint32_t)sw2_us);
    c->len = (uint32_t)len * 441u / 10u;
    c->hold = (uint32_t)hold * 441u / 10u;
    c->body = (int32_t)s->body;
    c->env = s->env;
    c->wave = s->wave;
    c->g_dec = kick_blk_decay((uint32_t)len * 500u);              /* a decaying type: e^-2 at the end of LONG */
    c->g_rel = kick_blk_decay((uint32_t)(s->rel_ms ? s->rel_ms : 12u) * 1000u);
    c->g_choke = kick_blk_decay(3000u);
    c->d1p = d1;
    c->d1n = d1 * (256 - as) >> 8;
    c->asg = 32767 - as * 64;                       /* 1 - asym / 2 */
    c->a_cut = kick_pole((uint32_t)s->cut);
    c->a_dc = kick_pole(dc);                        /* (below 18 Hz: a square's flat tops kept) */
    c->f_mid = kick_svf_f(s->mid_hz);
    c->mid = mid;
    c->d2 = d2;
    ref = softclip(clamp(d2 * 2048, 0, 1 << 20));   /* tanh(drive 2): the level at full scale */
    c->n2 = (int32_t)((32767u << 15) / (uint32_t)(ref > 1000 ? ref : 1000));
    c->a_tone = 32767 - kick_pole((uint32_t)tone);
    c->a_tone2 = s->tail_tone ? 32767 - kick_pole((uint32_t)kick_scale(s->tail_tone, ton * 2731, 100, 18000)) : c->a_tone;
    c->f_clk = kick_svf_f(3000u);
    c->click = ck;
    c->sub = clamp((int32_t)((uint32_t)sb * (mk < 768u ? mk : 768u) >> 8), 0, 1200);   /* (make-up: 3 x at most) */
    c->subg = 32767;                                /* (no make-room: the ceiling takes the peaks, so the
                                                     * kick's fundamental only ever grows with SUB) */
}

static void kick_trigger(kick_hit_t *h)
{
    int32_t seed = h->seed ? h->seed : (int32_t)0x9E3779B9;
    memset(h, 0, sizeof *h);
    h->sw = 65536u;
    h->sw2 = 65536u;
    h->clk = 65536;
    h->seed = seed;
    h->live = 1;
}

/* the output's ceiling: straight up to 20 000, then a tanh into the last 11 000 (never past 30 600: no hard clip) */
static inline int32_t kick_ceiling(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= 20000)
        return x;
    a = 20000 + (11000 * softclip(clamp((int32_t)(((int64_t)(a - 20000) << 15) / 11000), 0, 1 << 20)) >> 15);
    return x < 0 ? -a : a;
}

/* n (<= CTL) samples of hit h into y[] (Q15) */
static void kick_run(const kick_coef_t *c, kick_hit_t *h, int32_t *y, uint32_t n)
{
    uint32_t i, inc0, inc1, sw1 = h->t < c->hold ? h->sw : ((h->sw >> 1) * (c->g_sw >> 1)) >> 14,   /* (held, then falls) */
             sw21 = ((h->sw2 >> 1) * (c->g_sw2 >> 1)) >> 14;
    int32_t e0, e1, k0 = h->clk, k1 = (int32_t)(((uint32_t)h->clk >> 4) * 52305u >> 12);   /* the tick: 0.993 / sample */
    if (!h->live) {
        for (i = 0; i < n; i++)
            y[i] = 0;
        return;
    }
    inc0 = c->inc_end + (uint32_t)mulq16((int32_t)(c->inc_sw >> 1), h->sw) * 2u
         + (uint32_t)mulq16((int32_t)(c->inc_sw2 >> 1), h->sw2) * 2u;
    inc1 = c->inc_end + (uint32_t)mulq16((int32_t)(c->inc_sw >> 1), sw1) * 2u
         + (uint32_t)mulq16((int32_t)(c->inc_sw2 >> 1), sw21) * 2u;
    if (h->t < c->len && !h->choke && !h->rel) {     /* the body */
        if (c->env) {                                /* decaying over LONG */
            if (!h->lvl)
                h->lvl = 32767;
            e0 = h->lvl;
            e1 = (int32_t)(((uint32_t)h->lvl * c->g_dec) >> 16);
            h->lvl = e1 > 1 ? e1 : 1;
        } else {                                     /* held: a 35 % sag over LONG */
            e0 = 32767 - (int32_t)((11468u * h->t) / c->len);
            e1 = h->t + n < c->len ? 32767 - (int32_t)((11468u * (h->t + n)) / c->len) : 21299;
        }
    } else {                                          /* the release (or a choke) from where the body was */
        if (!h->rel) {
            h->rel = 1;
            if (!c->env)
                h->lvl = h->t < c->len ? 32767 - (int32_t)((11468u * h->t) / c->len) : 21299;
            else if (!h->lvl)
                h->lvl = 32767;
        }
        e0 = h->lvl;
        e1 = (int32_t)(((uint32_t)h->lvl * (h->choke ? c->g_choke : c->g_rel)) >> 16);
        h->lvl = e1 > 0 ? e1 : 1;
        if (e1 < 33)
            h->live = 0;
    }
    if (!h->at)                                       /* the low-pass: TONE's, then closing towards the tail's */
        h->at = c->a_tone;
    else if (h->t >= c->hold)
        h->at += (c->a_tone2 - h->at) >> 4;           /* (a block of 0.7 ms: ~16 of them, ~11 ms) */
    {
        int32_t dinc = (int32_t)(inc1 - inc0) / (int32_t)n, de = (e1 - e0) / (int32_t)n, dk = (k1 - k0) / (int32_t)n;
        uint32_t inc = inc0;
        int32_t e = e0, k = k0;
        for (i = 0; i < n; i++) {
            int32_t s, x, nz, hi, sb;
            h->ph += inc;
            h->sph += inc;
            inc += (uint32_t)dinc;
            if (c->wave)
                s = sine_i(h->ph);                   /* the 808's sine */
            else {
                s = osc_tri(h->ph + 0x40000000u);    /* (from 0: no click of its own at the start) */
                s = (s * (49152 - (((s * s) >> 15) >> 1))) >> 15;   /* a rounded triangle, the 909's */
            }
            if (c->body) {                           /* the body layer, on the note from the hit (the oscillator
                                                      * and it each at their share, the sum at full scale) */
                s = ((s * (256 - (c->body >> 1))) >> 8) + ((sine_i(h->bph) * (c->body >> 1)) >> 8);
                h->bph += c->inc_end;
            }
            x = (s * e) >> 15;
            sb = (sine_i(h->sph) * e) >> 15;         /* the sub: a sine from 0, on the level */
            e += de;
            if (c->click) {                          /* the tick: noise through a 3 kHz band-pass */
                nz = (int32_t)noise32(&h->seed) >> 17;
                h->clo += (c->f_clk * h->cbd) >> 15;
                hi = nz - h->clo - h->cbd;
                h->cbd += (c->f_clk * hi) >> 15;
                x += (((h->cbd * (k >> 4)) >> 12) * c->click) >> 8;
            }
            k += dk;
            /* drive 1, asymmetric */
            x = x > 0 ? softclip(clamp((x * c->d1p) >> 4, 0, 1 << 20))
                      : (softclip(clamp((x * c->d1n) >> 4, -(1 << 20), 0)) * c->asg) >> 15;
            /* the low cut (pole Q14 against 17-bit signals: no overflow) */
            h->hy = ((c->a_cut >> 1) * (h->hy + x - h->hx)) >> 14;
            h->hx = x;
            x = h->hy;
            /* the mid bump (CREUX): a resonant band-pass added (Q 1.6; states / 4 in the products) */
            if (c->mid) {
                h->mlo = clamp(h->mlo + ((c->f_mid * (h->mbd >> 2)) >> 13), -400000, 400000);
                hi = x - h->mlo - ((h->mbd >> 2) * 20480 >> 13);
                h->mbd = clamp(h->mbd + ((c->f_mid * (hi >> 2)) >> 13), -400000, 400000);
                x += (h->mbd >> 2) * c->mid >> 6;
            }
            /* drive 2, its level kept */
            x = clamp(x, -(1 << 21), 1 << 21);
            x = (softclip(clamp((x * c->d2) >> 4, -(1 << 20), 1 << 20)) * c->n2) >> 15;
            /* TONE: two one-pole low-passes */
            h->t1 += ((h->at >> 1) * (x - h->t1)) >> 14;
            h->t2 += ((h->at >> 1) * (h->t1 - h->t2)) >> 14;
            /* DC */
            h->dy = ((c->a_dc >> 1) * (h->dy + h->t2 - h->dx)) >> 14;
            h->dx = h->t2;
            if (c->sub) {                            /* the sub through the kick's low cut and DC blocker: in phase */
                h->shy = ((c->a_cut >> 1) * (h->shy + sb - h->shx)) >> 14;
                h->shx = sb;
                h->sdy = ((c->a_dc >> 1) * (h->sdy + h->shy - h->sdx)) >> 14;
                h->sdx = h->shy;
                sb = h->sdy;
            }
            x = ((h->dy * 5) >> 3) + ((sb * c->sub) >> 9);   /* the kick at -4 dB, the sub under it */
            if (c->sub)
                x = (x >> 1) * c->subg >> 14;
            y[i] = kick_ceiling(x);
        }
    }
    h->t += n;
    h->sw = sw1;
    h->sw2 = sw21;
    h->clk = k1;
}


/* ------------------------------------------------------------- RAVE FM --- */
_Static_assert(sizeof(kick_coef_t) <= sizeof(((tb_akick_t *)0)->c), "tb_akick_t: its coefficients' room");
_Static_assert(sizeof(kick_hit_t) <= sizeof(((tb_akick_t *)0)->h[0]), "tb_akick_t: a hit's room");
_Static_assert(KT_COUNT == TB_AKICK_NTYPES, "the Atelier's kick types");

int tb_akick_note(const uint8_t *p)                   /* p[0]: the Atelier's type (0..21) */
{
    return clamp((int32_t)KICK_TYPES[clamp(p[0], 0, KT_COUNT - 1)].note + clamp(p[1], 0, 48) - 24, 12, 72);
}
static void refresh(tb_akick_t *k, const uint8_t *p, int semis, uint32_t beat)
{
    if (memcmp(k->key, p, 8) || k->note != semis || k->beat != beat) {
        int16_t q[8];
        unsigned i;
        for (i = 0; i < 8u; i++)
            q[i] = (int16_t)clamp(p[i], 0, 127);
        memcpy(k->key, p, 8);
        k->note = (int8_t)semis;
        k->beat = beat;
        kick_setup((kick_coef_t *)(void *)k->c, q, clamp(semis, -24, 24), beat);
    }
}
void tb_akick_init(tb_akick_t *k)
{
    memset(k, 0, sizeof *k);
    k->key[0] = 0xFF;
}
void tb_akick_hit(tb_akick_t *k, const uint8_t *p, int semis, uint32_t beat, uint32_t seed)
{
    kick_hit_t *h;
    refresh(k, p, semis, beat);
    if (((kick_hit_t *)(void *)k->h[k->cur])->live)   /* the kick is one drum: a new hit chokes the last one */
        ((kick_hit_t *)(void *)k->h[k->cur])->choke = 1;
    k->cur ^= 1u;
    h = (kick_hit_t *)(void *)k->h[k->cur];
    h->seed = (int32_t)(seed | 1u);
    kick_trigger(h);                                   /* (it starts at the next tick: what plays now plays on) */
}
/* n samples added to out (Q15): the Atelier's voice at full level (its amplitude: the kick's own), its control
 * ticks of 32 samples kept across the calls */
void tb_akick_render(tb_akick_t *k, const uint8_t *p, uint32_t beat, int32_t *out, unsigned n)
{
    unsigned i, j;
    refresh(k, p, k->note, beat);
    for (i = 0; i < n; i++) {
        if (!k->pos) {                                /* a new tick: both hits, 32 samples */
            int32_t y[CTL];
            memset(k->buf, 0, sizeof k->buf);
            for (j = 0; j < 2u; j++) {
                kick_hit_t *h = (kick_hit_t *)(void *)k->h[j];
                if (h->live) {
                    unsigned s;
                    kick_run((const kick_coef_t *)(const void *)k->c, h, y, CTL);
                    for (s = 0; s < CTL; s++)
                        k->buf[s] += y[s];
                }
            }
        }
        out[i] += k->buf[k->pos];
        k->pos = (uint8_t)((k->pos + 1u) % CTL);
    }
}
