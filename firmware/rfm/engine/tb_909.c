/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the 909 track (tb_909.h). The pieces are stored as IMA ADPCM (the kick, the toms) or BC5 / BC6 blocks
 * (the noisy ones); they play through an 8-tap interpolation (TB909_K), at 44.1 kHz and TUNE 64 its phase 0, the
 * samples as they are. */
#include "tb_909.h"
#include "tb_dsp.h"
#ifdef RFM2
#include "tb_909_slim.h"      /* (RAVE FM 2: every piece, the long ones cut: tools/slim_909.py) */
#else
#include "tb_909_data.h"
#endif
#include <string.h>

const char *const TB909_NAME[TB909_PIECES] = {"BD", "SD", "CP", "CH", "OH", "MT", "RS", "CR", "LT", "HT", "RD"};
/* each piece's level (Q12 on the stored peak), set so that the 909 sits as a drum machine does: the kick and toms
 * up front, the cymbals under */
static const uint16_t GAIN[TB909_PIECES] = {2679, 3545, 3602, 3042, 2394, 3468, 2375, 1719, 3601, 3662, 1950};

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/* sample k of piece pc (0 past its end): BC from its block, ADPCM in order (the voice's decoder) */
static int32_t sample_at(unsigned pc, tb909_voice_t *v, uint32_t k)
{
    const uint8_t *d;
    uint32_t f;
    if (pc >= TB909_N || k >= TB909[pc].len)
        return 0;
    d = &TB909_DATA[TB909[pc].off];
    f = TB909[pc].fmt;
    if (f) {
        const uint8_t *b = d + (k / TB909_BLOCK) * (1u + 2u * f);
        uint32_t bit = (k % TB909_BLOCK) * f, w = (uint32_t)b[1u + (bit >> 3)] | ((uint32_t)b[2u + (bit >> 3)] << 8);
        int32_t m = (int32_t)((w >> (bit & 7u)) & ((1u << f) - 1u));
        if (m >= 1 << (f - 1u))
            m -= 1 << f;
        return tb_clamp((m * (int32_t)TB909_SC[b[0] < 48u ? b[0] : 47u]) >> 6, -32768, 32767);
    } else {
        uint32_t code = (k & 1u) ? (uint32_t)d[k >> 1] >> 4 : d[k >> 1] & 15u;
        int32_t step = IMA_STEP[v->idx], vd = step >> 3;
        if (code & 4u)
            vd += step;
        if (code & 2u)
            vd += step >> 1;
        if (code & 1u)
            vd += step >> 2;
        v->pred = tb_clamp(v->pred + ((code & 8u) ? -vd : vd), -32768, 32767);
        v->idx = tb_clamp(v->idx + IMA_IDX[code & 7u], 0, 88);
        return v->pred;
    }
}

void tb909_init(tb909_t *d) { memset(d, 0, sizeof *d); }

void tb909_hit(tb909_t *d, unsigned piece, unsigned vel)
{
    tb909_voice_t *v;
    uint32_t k;
    if (piece >= TB909_PIECES)
        return;
    v = &d->v[piece];
    memset(v, 0, sizeof *v);
    for (k = 0; k < 5u; k++)                      /* samples 0..4 read, the ones before 0 silent */
        v->h[k] = (int16_t)sample_at(piece, v, k);
    v->k = 5;
    v->q = 1 << 30;
    v->live = 1;
    tb_env_hit(&d->env[piece]);
    v->vel = (uint16_t)(vel > 127u ? 32767u : vel * 258u);
    if (piece == TB909_CH && d->v[TB909_OH].live)
        d->v[TB909_OH].choke = 1;
}

/* 2^(x / 4096), Q16, 0 <= x < 16 x 4096 */
static uint32_t exp2q(uint32_t x)
{
    uint32_t f = x & 4095u, m = 65536u + ((f * (45594u + ((f * (14851u + ((f * 5092u) >> 12))) >> 12))) >> 12);
    return m << (x >> 12);
}

/* per block of 128: e^(-128 / (tau fs)), Q16, tau in us */
static uint32_t blk_decay(uint32_t us)
{
    uint32_t x = 47554467u / (us | 1u), x2, x3;
    if (x > 46000u)
        x = 46000u;
    x2 = (x * x) >> 16;
    x3 = (x2 * x) >> 16;
    return 65536u - x + (x2 >> 1) - x3 / 6u;
}

void tb909_off(tb909_t *d, unsigned piece)
{
    if (piece < TB909_PIECES)
        tb_env_off(&d->env[piece]);
}

void tb909_render(tb909_t *d, const uint8_t *p, const uint8_t *adsr, int32_t lfo, int32_t *out, unsigned n)
{
    tb909_render_x(d, p, adsr, lfo, 0, 128u, out, n);
}

/* (as tb909_render; pp: each piece's own TUNE DECAY - LEVEL, 4 bytes a piece, 64 64 - 100 as recorded, or none; mix:
 * the whole of it, 0 .. 128) */
void tb909_render_x(tb909_t *d, const uint8_t *p, const uint8_t *adsr, int32_t lfo, const uint8_t *pp, unsigned mix,
                    int32_t *out, unsigned n)
{
    unsigned pc, i;
    int32_t tune = (int32_t)p[TB909_TUNE] - 64, tone = p[TB909_TONE], snap = p[TB909_SNAP];
    /* TUNE: the rate x 2^(tune x 3 / 192) (+-1 octave at the ends) */
    uint32_t r = exp2q((uint32_t)(tune * 3 + 192) * 4096u / 192u) >> 1;
    if (lfo)
        r = (uint32_t)(((uint64_t)r * tb_exp2(lfo / 12)) >> 16);
    tb_envk(&d->k, adsr);
    /* TONE: below 64 a low-pass closing to ~400 Hz, above a lift of the top (one-pole split at ~3 kHz) */
    int32_t pole = tone < 64 ? 2000 + tone * 470 : 13000, lift = tone > 64 ? (tone - 64) * 4096 / 63 : 0;
    /* SNAP: the first 4 ms x 0 .. 2 */
    int32_t g0 = snap * 64, gstep = ((g0 - 4096) * 256) / 176;
    for (pc = 0; pc < TB909_PIECES && pc < TB909_N; pc++) {
        tb909_voice_t *v = &d->v[pc];
        uint32_t rr = r, inc, fr = v->fr, k = v->k, len = TB909[pc].len;
        int32_t a, da, g = (GAIN[pc] * v->vel) >> 15, dec = p[TB909_DECAY];
        int32_t *o = out;
        if (!v->live)
            continue;
        if (pp) {                                     /* the piece's own: +-1 octave, its decay, its level */
            const uint8_t *q = pp + pc * 4u;
            rr = (uint32_t)(((uint64_t)r * (exp2q((uint32_t)(4096 + ((int32_t)q[0] - 64) * 64)) >> 1)) >> 16);
            dec = tb_clamp(dec + (int32_t)q[1] - 64, 0, 127);
            g = (int32_t)(((int64_t)g * q[3] * q[3]) / 10000);
        }
        g = (int32_t)(((int64_t)g * (int32_t)mix) >> 7);
        inc = (TB909[pc].inc >> 16) * rr + (((TB909[pc].inc & 0xFFFFu) * (rr >> 1)) >> 15);
        /* the fade: DECAY below 64 (tau from about the recording's length down to 1/30 of it), the choke 1.5 ms */
        a = v->q >> 10;
        if (v->choke || dec < 64) {
            uint32_t len_us = len * (1000000u / 32000u), tau = v->choke ? 1500u : len_us / 30u + len_us * (uint32_t)dec / 64u;
            v->q = (int32_t)(((int64_t)v->q * blk_decay(tau)) >> 16);
        }
        da = ((v->q >> 10) - a) / (int32_t)n;
        for (i = 0; i < n; i++) {
            const int16_t *kt = TB909_K[fr >> 17];
            int16_t *h = v->h;
            int32_t s = fr >> 17 ? (h[k & 7u] * kt[0] + h[(k + 1u) & 7u] * kt[1] + h[(k + 2u) & 7u] * kt[2] +
                         h[(k + 3u) & 7u] * kt[3] + h[(k + 4u) & 7u] * kt[4] + h[(k + 5u) & 7u] * kt[5] +
                         h[(k + 6u) & 7u] * kt[6] + h[(k + 7u) & 7u] * kt[7]) >> 14
                       : h[(k + 3u) & 7u];       /* (v22: on a sample, phase 0, the kernel is that sample: the same) */
            fr += inc;
            while (fr >= 1u << 24) {
                fr -= 1u << 24;
                h[k & 7u] = (int16_t)sample_at(pc, v, k);
                k++;
            }
            if (tone != 64) {
                v->lp += ((s - v->lp) * pole) >> 15;
                s = tone < 64 ? v->lp : s + (((s - v->lp) * lift) >> 12);
            }
            if (v->at < 176u) {
                s = (s * (4096 + ((gstep * (176 - (int32_t)v->at)) >> 8))) >> 12;
                v->at++;
            }
            if (k + 2048u > len)                      /* (the last 64 ms faded: a piece cut short ends clean) */
                s = k >= len ? 0 : (s * (int32_t)(len - k)) >> 11;
            s = tb_clamp(s, -65536, 65535);
            s = tb_mul30(s, tb_env_step(&d->env[pc], &d->k));   /* (|s| <= 65536: in 32 bits, the same) */
            *o++ += (((s * (a >> 6)) >> 14) * g) >> 12;
            a += da;
        }
        v->fr = fr, v->k = k;
        if (k > len + 9u || ((v->choke || dec < 64) && v->q < (1 << 16)) || !d->env[pc].stage)
            v->live = 0;
    }
}
