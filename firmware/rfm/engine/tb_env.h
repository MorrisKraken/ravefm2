/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the envelope every track has (ATTACK DECAY SUSTAIN RELEASE, 0..127 each), the same curves on the kick,
 * the 909's pieces, the breaks' slices, the samples and the synths' voices. Exponential segments (as analogue ones
 * are): times from 0 (at once) through 0.5 ms .. about 9 s; SUSTAIN a level, 127 the top. A new note starts the
 * attack from where the level is (no click). On a drum, SUSTAIN 0: DECAY is how long it rings (127: as recorded). */
#ifndef TB_ENV_H
#define TB_ENV_H
#include <stdint.h>
#include "tb_dsp.h"

enum { TB_ENV_A, TB_ENV_D, TB_ENV_S, TB_ENV_R };
typedef struct { int32_t lvl; uint8_t stage; } tb_env_t;          /* lvl Q30; stage 0 off, 1 attack, 2 decay, 3 release */
typedef struct { int32_t ka, kd, kr, sus; uint8_t key[4]; } tb_envk_t;

/* a knob's time: 0 at once, then 0.3 ms + 4 s x (v / 127)^3: the first quarter already heard (16: 8 ms, 32: 64 ms,
 * 64: 0.5 s, 127: 4 s) */
static inline uint32_t tb_env_us(unsigned v)
{
    return v ? 300u + (uint32_t)(4000000ull * v * v * v / (127ull * 127ull * 127ull)) : 0u;
}
/* per sample: 1 - e^(-1 / (tau fs)), Q30 (0: at once) */
static inline int32_t tb_env_k(unsigned v)
{
    uint32_t us;
    uint64_t k;
    if (!v)
        return 1 << 30;
    us = tb_env_us(v);
    k = (1ull << 30) * 1000000ull / ((uint64_t)us * TB_FS + 1u);
    return (int32_t)(k > (1u << 30) ? 1u << 30 : k < 1u ? 1u : k);
}

/* the coefficients from the four knobs (worked out again only when they change) */
static inline void tb_envk(tb_envk_t *k, const uint8_t *adsr)
{
    if (k->ka && adsr[0] == k->key[0] && adsr[1] == k->key[1] && adsr[2] == k->key[2] && adsr[3] == k->key[3])
        return;
    k->key[0] = adsr[0], k->key[1] = adsr[1], k->key[2] = adsr[2], k->key[3] = adsr[3];
    k->ka = tb_env_k(adsr[0]);
    k->kd = tb_env_k(adsr[1]);
    k->kr = tb_env_k(adsr[3]);
    k->sus = adsr[2] >= 127 ? 1 << 30 : (int32_t)adsr[2] << 23;
}

static inline void tb_env_on(tb_env_t *e) { e->stage = 1; }
/* a drum's hit: its attack from silence (the knob heard on every hit, whatever the hit before left) */
static inline void tb_env_hit(tb_env_t *e) { e->lvl = 0, e->stage = 1; }
static inline void tb_env_off(tb_env_t *e) { if (e->stage) e->stage = 3; }

/* one sample: the level, Q30 */
static inline int32_t tb_env_step(tb_env_t *e, const tb_envk_t *k)
{
    switch (e->stage) {
    case 1:                                            /* toward 1.25, stopped at 1: a curve that arrives */
        e->lvl += (int32_t)(((int64_t)((1 << 30) + (1 << 28) - e->lvl) * k->ka) >> 30);
        if (e->lvl >= 1 << 30)
            e->lvl = 1 << 30, e->stage = 2;
        break;
    case 2:                                            /* (v22: held at its sustain, no product: the same) */
        if (k->sus != e->lvl)
            e->lvl += (int32_t)(((int64_t)(k->sus - e->lvl) * k->kd) >> 30);
        break;
    case 3:                                            /* (SUSTAIN 0: the decay goes on if it is the faster) */
        e->lvl -= (int32_t)(((int64_t)e->lvl * (k->sus == 0 && k->kd > k->kr ? k->kd : k->kr)) >> 30);
        if (e->lvl < 1 << 12)
            e->lvl = 0, e->stage = 0;
        break;
    default:
        break;
    }
    return e->lvl;
}
#endif
