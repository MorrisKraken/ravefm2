/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM: the Atelier's leads (tb_rave.h). The code below is the Atelier's own (Rave FM on Felucca, October 2026:
 * firmware/src/eng_rave.c, its chain 1, and firmware/src/voice.c's envelope, glide and mono / poly voices), with the
 * Felucca engines' DSP blocks it uses (firmware/engines/src/dsp.c, MIT, Copyright (C) 2026 Leo Kuroshita, Hugelton
 * Instruments), kept step for step: only what the leads use, renamed into this file. */
#include "tb_rave.h"
#include "tb_rave_tab.h"
#include "tb_synth.h"
#include "tb_dsp.h"
#include <string.h>

#define CTL 32u                       /* the Atelier's control tick (envelopes, glide, filters' coefficients) */
#define FS 44100u
#define VOICE_FS 24000
/* the Atelier's level to RAVE FM's synth level (measured: as loud as the library's leads) */
#define RV_OUT 2896                   /* (+3 dB: its peaks as the library's loud leads) */

/* ----------------------------------------------------------- dsp.c (MIT) --- */
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t rblep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline int32_t osc_saw(uint32_t ph, uint32_t inc) { return ((int32_t)(ph >> 16) - 32768) - rblep(ph, inc); }
static inline int32_t osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw) { return (osc_saw(ph, inc) - osc_saw(ph + pw, inc)) >> 1; }
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = RV_TANH_Q15[256];
    else
        y = RV_TANH_Q15[i] + (((RV_TANH_Q15[i + 1] - RV_TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
typedef struct { int32_t a1, a2, a3; } tsvf_t;
static inline void tsvf_coef_k(tsvf_t *c, int32_t cut, int32_t k)
{
    int32_t i, g, den;
    cut = clampi(cut, 0, 127 << 8);
    i = cut >> 8;
    g = RV_SVF_G[i];
    if (i < 127)
        g += ((RV_SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static inline void tsvf_coef(tsvf_t *c, int32_t cut, int32_t reso) { tsvf_coef_k(c, cut, 8192 - reso * 7600 / 127); }
static inline int32_t tsvf_lp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clampi(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clampi(2 * v2 - *ic2, -150000, 150000);
    return v2;
}
typedef struct { uint32_t inc; int32_t pitch16, amp0, amp1, cutoff, fine; } vmod_t;
static inline int32_t amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (int32_t)(CTL - 1))) >> 5);
}
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i) { return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS); }
static inline int32_t soft_knee(int32_t y, int32_t k)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}
static inline uint32_t cents_inc(int32_t pitch16, int32_t ct, int32_t fine)
{
    int32_t d16 = ct * 16 / 100, rem = ct * 16 - d16 * 100;
    uint32_t inc = rv_pitch_inc((uint32_t)clampi(pitch16 + d16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (rem * 2367 / 16000 + fine));
}

/* ---------------------------------------------- eng_kick.c's fixed point --- */
static uint32_t kick_exp2(int32_t x)
{
    int32_t n = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu, p;
    p = 65536u + ((f * (45556u + ((f * (14822u + ((f * 5160u) >> 16))) >> 16))) >> 16);
    if (n >= 15)
        return 0x7FFFFFFFu;
    if (n >= 0)
        return p << n;
    return n <= -31 ? 0u : p >> -n;
}
static int32_t kick_pole(uint32_t hz) { return (int32_t)(kick_exp2(-(int32_t)(hz * 13471u / 1000u)) >> 1); }
static uint32_t kick_blk_decay(uint32_t us) { return kick_exp2(-(int32_t)(68606880u / (us < 500u ? 500u : us))); }
static int32_t kick_scale(int32_t a, int32_t oct65536, int32_t lo, int32_t hi)
{
    return clampi((int32_t)(((int64_t)a * kick_exp2(oct65536)) >> 16), lo, hi);
}

/* ------------------------------------------------- eng_rave.c, chain 1 --- */
enum { RO_SAW, RO_SAWOCT };
typedef struct {
    uint8_t osc, nosc;
    uint16_t spread;
    int16_t bend;
    uint16_t bend_ms;
    uint8_t cut, reso;
    int8_t fenv;
    uint16_t fenv_ms;
    uint8_t cut2, post;
    uint16_t drv;
    int16_t bias;
    uint8_t lvl;
} rave_type_t;
/* ANGER: a clean supersaw lead (six saws tight, the octave pair, two low-passes, a light tube); V1: the first ANGER,
 * fitted on the references, wider and rawer; SUPA: the tracker's three saws (the 'Supa Dupa' sound) */
static const rave_type_t RAVE_TYPES[TBR_NTYPE] = {
    /*            osc        n  spr  bend ms  cut res fenv ms   cut2 post drv  bias  lvl */
    [TBR_ANGER] = {RO_SAWOCT, 8, 32,  16, 25, 106, 20, 30, 180, 114, 118, 9,  1500, 32},
    [TBR_V1]    = {RO_SAWOCT, 6, 142, 16, 25, 110, 100, 30, 180, 113, 121, 9, 4606, 32},
    [TBR_SUPA]  = {RO_SAW,    3, 30,  16, 25, 104, 20, 30, 180, 112, 112, 24, 1500, 32},
};

static int32_t rave_lncosh(int32_t x5)                /* x5 = |x| >> 5, 0..32768 */
{
    int32_t i, f, y0, y1;
    if (x5 >= 2048)
        return RV_RAVE_LNCOSH[256] + RV_TANH_Q15[256] * (x5 - 2048);
    i = x5 >> 3;
    f = x5 & 7;
    y0 = RV_TANH_Q15[i];
    y1 = RV_TANH_Q15[i + 1];
    return RV_RAVE_LNCOSH[i] + y0 * f + (((y1 - y0) * f * f) >> 4);
}
static int32_t tube_aa(int32_t x, int32_t *px)
{
    int32_t a = x >> 5, b = *px >> 5, d = a - b, y;
    *px = x;
    if (d > -4 && d < 4)
        return softclip((x + (b << 5)) >> 1);
    y = (rave_lncosh(a < 0 ? -a : a) - rave_lncosh(b < 0 ? -b : b)) / d;
    return y;
}
static uint32_t rave_blk(uint32_t ms) { return ms ? kick_blk_decay(ms * 1000u) : 0u; }

/* the Atelier's E0..E7 from a synth sound */
#define E_TYPE(p) ((p)[TBS_SUB] < TBR_NTYPE ? (p)[TBS_SUB] : 0u)
#define E_SPRD(p) ((int32_t)(p)[TBS_DETUNE])
#define E_CUT(p) ((int32_t)(p)[TBS_CUTOFF])
#define E_RESO(p) ((int32_t)(p)[TBS_RESO])
#define E_DISTO(p) ((int32_t)(p)[TBS_DRIVE])
#define E_ENV(p) ((int32_t)(p)[TBS_ENV])
#define E_BEND(p) ((int32_t)(p)[TBS_BEND])
#define E_COLOR(p) ((int32_t)(p)[TBS_MACRO])

static void rave_note_on(tb_rave_voice_t *v)
{
    uint32_t k;
    v->fe = 65536u;
    v->pe = 65536u;
    if (v->env > 0 || v->env_out > 0)                 /* a sounding voice retriggered: its phases and filters go on */
        return;
    for (k = 0; k < 9u; k++)                          /* stacked oscillators start apart: no phasing at the hit */
        v->ph[k] = (v->age + k) * 0x9E3779B9u;
    v->hx = v->hy = 0;
    v->f2a = v->f2b = v->p1 = v->p2 = 0;
    v->tx = 0;
}

static void rave_render(const uint8_t *p, tb_rave_voice_t *r, int32_t *out, uint32_t n, const vmod_t *m)
{
    const rave_type_t *ty = &RAVE_TYPES[E_TYPE(p)];
    int32_t spr = E_SPRD(p), cutk = E_CUT(p) - 64, resk = E_RESO(p) - 64, dis = E_DISTO(p) - 64, envk = E_ENV(p),
            bendk = E_BEND(p), col = E_COLOR(p);
    uint32_t inc[8], i, k, nosc = ty->nosc, nup = ty->osc == RO_SAWOCT ? 2u : 0u;
    int32_t cut, reso, bend16, ref, n2;
    int32_t ic1 = r->s0, ic2 = r->s1;
    tsvf_t flt, flt2, fpost;
    int32_t drv, bias, boff, bmk;
    /* envelopes, once a block */
    bend16 = ty->bend * bendk / 64 * (int32_t)(r->pe >> 4) >> 12;
    r->pe = ty->bend_ms ? (uint32_t)(((uint64_t)r->pe * rave_blk(ty->bend_ms)) >> 16) : 0u;
    cut = clampi(((ty->cut + cutk) << 8) + m->cutoff + (ty->fenv * envk / 64 * (int32_t)(r->fe >> 8)), 0, 127 << 8);
    r->fe = ty->fenv_ms ? (uint32_t)(((uint64_t)r->fe * rave_blk(ty->fenv_ms)) >> 16) : 0u;
    reso = clampi(ty->reso + resk, 0, 127);
    tsvf_coef(&flt, cut, reso);
    tsvf_coef(&flt2, clampi(((ty->cut2 + cutk) << 8) + m->cutoff, 0, 127 << 8), 40);
    tsvf_coef(&fpost, (ty->post - (dis > 0 ? dis / 6 : 0)) << 8, 23);
    drv = kick_scale(ty->drv, dis * 6144, 4, 64 * 16);
    bias = clampi(ty->bias + dis * 40, 0, 6000);
    boff = softclip(clampi((bias * drv) >> 4, -(1 << 20), 1 << 20));
    ref = softclip(clampi(((32767 + bias) * drv) >> 4, -(1 << 20), 1 << 20)) - boff;
    n2 = boff - softclip(clampi(((-32767 + bias) * drv) >> 4, -(1 << 20), 1 << 20));
    if (n2 > ref)
        ref = n2;
    bmk = (int32_t)((32767u << 15) / (uint32_t)(ref > 2000 ? ref : 2000));
    if (bmk > 4 * 32767)
        bmk = 4 * 32767;
    {                                                 /* the stack: the last two (ANGER, V1) an octave up */
        int32_t sp = (int32_t)ty->spread * spr / 64;
        for (k = 0; k < nosc; k++) {
            uint32_t nb = nosc - nup;
            int32_t ct, up = 0;
            if (k >= nb) {
                ct = nup > 1u ? sp / 3 * (2 * (int32_t)(k - nb) - (int32_t)(nup - 1u)) / (int32_t)(nup - 1u) : 0;
                up = 192;
            } else
                ct = nb > 1u ? sp * (2 * (int32_t)k - (int32_t)(nb - 1u)) / (2 * (int32_t)(nb - 1u)) : 0;
            inc[k] = cents_inc(clampi(m->pitch16 + bend16 + up, 0, 2047), ct, m->fine);
        }
    }
    for (i = 0; i < n; i++) {
        int32_t s = 0, y;
        for (k = 0; k < nosc; k++) {
            uint32_t ph = r->ph[k], in = inc[k];
            int32_t o = osc_saw(ph, in);
            if (ty->osc == RO_SAWOCT) {
                if (k >= nosc - nup)
                    o = (o * col) >> 6;
            } else if (col > 64 && (k & 1u))
                o = o + (((osc_pulse(ph, in, 0x80000000u) - o) * (col - 64)) >> 6);
            s += o;
            r->ph[k] = ph + in;
        }
        s = s / (int32_t)(nosc - nup);
        y = tsvf_lp(&flt, clampi(s, -150000, 150000) >> 1, &ic1, &ic2);
        y = tsvf_lp(&flt2, y, &r->f2a, &r->f2b) << 1;
        y = tube_aa(clampi(((clampi(y, -65536, 65536) + bias) * drv) >> 4, -(1 << 20), 1 << 20), &r->tx) - boff;
        y = (int32_t)(((int64_t)y * bmk) >> 15);
        y = tsvf_lp(&fpost, y, &r->p1, &r->p2);
        r->hy = ((kick_pole(20) >> 1) * (r->hy + y - r->hx)) >> 14;
        r->hx = y;
        out[i] += voice_amp(soft_knee((r->hy * ty->lvl) >> 5, 22000), m, i) << 1;
    }
    r->s0 = ic1;
    r->s1 = ic2;
}

/* --------------------------------------------- voice.c: voices, envelope --- */
#define P_ATK(p) ((p)[TBS_AMP + 0] & 127u)
#define P_DEC(p) ((p)[TBS_AMP + 1] & 127u)
#define P_SUS(p) ((p)[TBS_AMP + 2] & 127u)
#define P_REL(p) ((p)[TBS_AMP + 3] & 127u)
#define POLY(p) ((p)[TBS_MODE] == TBS_POLY)

static void glide_set(const uint8_t *p, tb_rave_voice_t *v, int glide)
{
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->gstep = 0;
        return;
    }
    {                                                 /* TIME: the same time for any interval */
        int32_t d = v->pitch16 - v->pitch_cur, ticks = 1 + p[TBS_GLIDE] * p[TBS_GLIDE] / 8;
        d = d < 0 ? -d : d;
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    }
}
static void voice_start(tb_rave_t *t, const uint8_t *p, tb_rave_voice_t *v, unsigned note, unsigned vel, int glide)
{
    int sounding = v->active && v->stage != 0;
    int32_t s0 = v->s0, s1 = v->s1;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++t->vage;
    v->pitch16 = (int32_t)note * 16;
    glide_set(p, v, glide);
    if (!sounding)
        v->env = v->env_out = 0;
    rave_note_on(v);
    if (sounding)                                     /* a retrigger keeps the filter */
        v->s0 = s0, v->s1 = s1;
}
/* POLY: the same note's voice; else a free one (GLIDE: the one whose last pitch is closest, else round-robin); else
 * the oldest released, else the oldest held but the lowest (the bass) */
static tb_rave_voice_t *voice_alloc(tb_rave_t *t, const uint8_t *p, unsigned note)
{
    unsigned i, np = TBR_VOICES, best = np, low = np;
    int32_t bd = 0x7FFFFFFF;
    for (i = 0; i < np; i++)
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    if (p[TBS_GLIDE]) {
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;
            if (d < bd)
                bd = d, best = i;
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            unsigned k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)(((best == np ? t->rr : best) + 1u) % np);
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)
        if (!t->v[i].gate && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)
            if (i != low && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    return &t->v[best == np ? 0 : best];
}
static void mono_play(tb_rave_t *t, const uint8_t *p, unsigned note, unsigned vel, int retrig, int glide)
{
    tb_rave_voice_t *v = &t->v[0];
    if (retrig || !v->active || !v->gate)
        voice_start(t, p, v, note, vel, glide);
    else {                                            /* legato: the new pitch, the same envelope */
        v->note = (uint8_t)note;
        v->pitch16 = (int32_t)note * 16;
        if (vel > v->vel)
            v->vel = (uint8_t)vel;
        glide_set(p, v, glide);
    }
    t->mono_note = (uint8_t)note;
}
static void mono_remove(tb_rave_t *t, unsigned note)
{
    unsigned j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

void tb_rave_on(tb_rave_t *t, const uint8_t *p, unsigned note, unsigned vel)
{
    if (note > 127u)
        return;
    if (POLY(p)) {
        t->nmono = 0, t->mono_note = 0;
        voice_start(t, p, voice_alloc(t, p, note), note, vel, p[TBS_GLIDE] != 0);
        return;
    }
    {                                                 /* LEGATO (MONO alike): glide only between held notes */
        unsigned sounding = t->mono_note && t->v[0].active && t->v[0].gate;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        if (p[TBS_MODE] == TBS_MONO)
            mono_play(t, p, note, vel, 1, p[TBS_GLIDE] != 0);
        else
            mono_play(t, p, note, vel, !sounding, sounding && p[TBS_GLIDE] != 0);
    }
}
void tb_rave_off(tb_rave_t *t, const uint8_t *p, unsigned note)
{
    unsigned i;
    mono_remove(t, note);
    if (!POLY(p) && t->mono_note == note) {
        if (t->nmono)                                 /* back to a held note, legato */
            mono_play(t, p, t->mono_stack[t->nmono - 1u], t->v[0].vel, 0, p[TBS_GLIDE] != 0);
        else {
            if (t->v[0].gate)
                t->v[0].gate = 0, t->v[0].stage = 3;
            t->mono_note = 0;
        }
    }
    for (i = 0; i < TBR_VOICES; i++)
        if (t->v[i].note == note && t->v[i].gate)
            t->v[i].gate = 0, t->v[i].stage = 3;
}
void tb_rave_all_off(tb_rave_t *t)
{
    unsigned i;
    for (i = 0; i < TBR_VOICES; i++) {
        t->v[i].gate = 0;
        t->v[i].stage = t->v[i].active ? 3 : 0;
    }
    t->nmono = 0, t->mono_note = 0;
}

static int32_t env_tick(const uint8_t *p, tb_rave_voice_t *v)
{
    int32_t sus = (int32_t)P_SUS(p) << 17;
    switch (v->stage) {
    case 1:
        v->env += (int32_t)RV_ENV_LIN[P_ATK(p)];
        if (v->env >= (1 << 24))
            v->env = 1 << 24, v->stage = 2;
        break;
    case 2:
        v->env += mulq16(sus - v->env, RV_ENV_EXP[P_DEC(p)]);
        break;
    case 3:
        v->env -= mulq16(v->env, RV_ENV_EXP[P_REL(p)]);
        if (v->env < (1 << 12))
            v->env = 0, v->stage = 0, v->active = 0;
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* one control tick (CTL samples) of every voice: the Atelier's track_render, for these voices */
static void tick(tb_rave_t *t, const uint8_t *p, int32_t *out, int32_t pitch_lfo, int32_t cut_lfo)
{
    unsigned i;
    for (i = 0; i < TBR_VOICES; i++) {
        tb_rave_voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
        env = env_tick(p, v);
        m.amp1 = mulq15(env, v->vel * 258);
        m.amp0 = v->env_out;
        v->env_out = m.amp1;
        if (v->pitch_cur != v->pitch16) {             /* glide */
            int32_t d = v->pitch16 - v->pitch_cur;
            int32_t st = v->gstep ? v->gstep : (int32_t)(RV_ENV_LIN[(p[TBS_GLIDE] ? p[TBS_GLIDE] : 40) & 127] >> 14);
            if (st < 1)
                st = 1;
            v->pitch_cur += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
        }
        if (!env && !m.amp0 && v->stage == 2)
            continue;
        pitch = v->pitch_cur + pitch_lfo;
        m.pitch16 = clampi(pitch, 0, 2047);
        m.inc = rv_pitch_inc((uint32_t)m.pitch16);
        m.fine = 0;
        m.cutoff = cut_lfo;
        if (v->vel > 110)                             /* the accent opens the filter with the envelope */
            m.cutoff += (env * 24) >> 7;
        rave_render(p, v, out, CTL, &m);
    }
}

void tb_rave_render(tb_rave_t *t, const uint8_t *p, int32_t *out, unsigned n, int32_t lfo_pitch, int32_t lfo_cut)
{
    int32_t pl = lfo_pitch >> 12, cl = (int32_t)(((int64_t)lfo_cut * 3584) >> 16);   /* (semitones Q16 -> 1/16; octaves
                                                                                     * Q16 -> the cutoff's 14 steps of 256 an octave) */
    unsigned i;
    for (i = 0; i < n; i++) {                         /* (the engine's blocks cut at its ticks: any length; the
                                                       *  Atelier's ticks of 32 run on whole, 0.7 ms late at most) */
        if (!t->pos) {
            memset(t->buf, 0, sizeof t->buf);
            tick(t, p, t->buf, pl, cl);
        }
        out[i] += (int32_t)(((int64_t)t->buf[t->pos] * RV_OUT) >> 12);
        t->pos = (uint8_t)((t->pos + 1u) % CTL);
    }
}
