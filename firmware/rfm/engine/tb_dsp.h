/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: fixed-point sound helpers. Q15 signals (32767 = full scale), 32-bit phases (2^32 = one cycle), the
 * sample rate 44 100 Hz, blocks of TB_BLOCK frames. tb_dsp_init() fills the tables once at power on. */
#ifndef TB_DSP_H
#define TB_DSP_H
#include "tb_features.h"
#include <stdint.h>

#define TB_FS 44100
#define TB_BLOCK 128

extern const int16_t tb_sin_tab[1025];        /* sin over a quarter .. a whole cycle: 1024 + 1 points, Q15 */
extern const int16_t tb_tanh_tab[257];        /* tanh(x) for x = 0 .. 2 in 256 steps, Q15 */
void tb_dsp_init(void);

static inline int32_t tb_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* n / d in Q15 (n, d >= 0, n / d < 2), in 32 bits: no 64-bit division (on the FM-1 a slow routine) in a sample loop */
static inline int32_t tb_divq15(uint32_t n, uint32_t d)
{
    while (n > 65535u || d > 65535u)
        n >>= 1, d >>= 1;
    return d ? (int32_t)((n << 15) / d) : 65535;
}

/* (21 Nov 2026) (x * g) >> 15 as the 64-bit product gives it, in 32 bits: x split into its top and its low 15 bits
 * (x = h 2^15 + l, 0 <= l < 2^15; h g 2^15 a multiple of 2^15: the floor the same). For |g| <= 32768 and |x| < 2^31.
 * No 64-bit multiply: on the FM-1 each of those is a call (the build's helpers, a hardware fault worked round) */
static inline int32_t tb_mul15(int32_t x, int32_t g)
{
    return (x >> 15) * g + (((x & 0x7FFF) * g) >> 15);
}
/* (21 Nov 2026) the same for a shift s of 1..15: (x * g) >> s as the 64-bit product gives it, exact for
 * (2^s - 1) |g| < 2^31 (s 15: |g| <= 65535; s 12: |g| <= 524416) and a result that fits 32 bits (as the 64-bit one's
 * cast to 32 bits needs anyway) */
static inline int32_t tb_mulsh(int32_t x, int32_t g, unsigned s)
{
    return (x >> s) * g + (((x & ((1 << s) - 1)) * g) >> s);
}
/* (21 Nov 2026) (a b + c d) >> 15 as the 64-bit sum gives it, for a and c of 0 .. 32768 (their low products' sum under
 * 2^31) and a result that fits 32 bits: the synth's filter */
static inline int32_t tb_mac15(int32_t a, int32_t b, int32_t c, int32_t d)
{
    return (b >> 15) * a + (d >> 15) * c + (((b & 0x7FFF) * a + (d & 0x7FFF) * c) >> 15);
}

/* (v22) (x * e) >> 30 as the 64-bit product gives it, for an envelope e of 0 .. 2^30 (Q30): at 2^30 x itself;
 * else, for |x| <= 65536, in 32 bits (e split into its top and low 15 bits) */
static inline int32_t tb_mul30(int32_t x, int32_t e)
{
    int32_t h = e >> 15;
    return e == 1 << 30 ? x : (x * h + ((x * (e & 0x7FFF)) >> 15)) >> 15;
}
/* the same for any x (the 64-bit product when e is under 2^30) */
static inline int32_t tb_mul30x(int32_t x, int32_t e) { return e == 1 << 30 ? x : (int32_t)(((int64_t)x * e) >> 30); }

/* sin(2 pi ph / 2^32), Q15, linearly interpolated over 1024 points */
static inline int32_t tb_sin(uint32_t ph)
{
    uint32_t i = ph >> 22, f = (ph >> 6) & 0xFFFFu;
    int32_t a = tb_sin_tab[i], b = tb_sin_tab[i + 1];
    return a + (((b - a) * (int32_t)f) >> 16);
}

/* a triangle, Q15: 0 at phase 0, up to the peak at a quarter */
static inline int32_t tb_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                 /* 0 .. 131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* tanh(x / 32768) x 32767 up to x = 2 (65536), held at tanh(2) above: a soft ceiling at -0.3 dB; linearly
 * interpolated */
static inline int32_t tb_tanh(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = tb_tanh_tab[256];
    else
        y = tb_tanh_tab[i] + (((tb_tanh_tab[i + 1] - tb_tanh_tab[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* 2^(x / 65536), Q16, x from -16 to 14 octaves (a cubic on the fraction: within 0.01 %) */
static inline uint32_t tb_exp2(int32_t x)
{
    int32_t n = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu, p = 65536u + ((f * (45556u + ((f * (14822u + ((f * 5160u) >> 16))) >> 16))) >> 16);
    if (n >= 15)
        return 0x7FFFFFFFu;
    return n >= 0 ? p << n : n <= -31 ? 0u : p >> -n;
}

/* xorshift32 */
static inline uint32_t tb_noise(uint32_t *s)
{
    uint32_t v = *s ? *s : 0x9E3779B9u;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return *s = v;
}

/* a x b >> 16, a and b non-negative up to 2^31 */
static inline uint32_t tb_mulu16(uint32_t a, uint32_t b)
{
    return (a >> 16) * b + (((a & 0xFFFFu) * (b >> 1)) >> 15);
}

/* linear up to k, a soft knee above that never passes full scale (32767) */
static inline int32_t tb_knee(int32_t y, int32_t k)
{
    int32_t a = y < 0 ? -y : y, room = 32767 - k;
    if (a <= k)
        return y;
    a = k + (int32_t)(((int64_t)tb_tanh((int32_t)((int64_t)(a - k) * 32768 / room)) * room) >> 15);
    return y < 0 ? -a : a;
}
#endif
