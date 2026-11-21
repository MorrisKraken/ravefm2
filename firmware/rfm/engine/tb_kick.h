/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: KICK, the hardcore kick (gabber, Rotterdam, terror, uptempo, early, doomcore, jumpstyle).
 * Eight knobs: TYPE PITCH PUNCH LONG DISTO CREUX TONE SUB (0..127, 64 = the type as designed; PITCH 24 = its note,
 * SUB 0 = none). The kick follows the tempo (LONG is a part of the beat) and lands on its note within what is heard.
 * Mono: a new hit chokes the last in 3 ms. */
#ifndef TB_KICK_H
#define TB_KICK_H
#include "tb_features.h"
#include <stdint.h>
#include "tb_akick.h"

enum { TB_KICK_TYPE, TB_KICK_PITCH, TB_KICK_PUNCH, TB_KICK_LONG, TB_KICK_DISTO, TB_KICK_CREUX, TB_KICK_TONE,
       TB_KICK_SUB, TB_KICK_NPARAM };

typedef struct {
    uint32_t inc_end, inc_sw, inc_sw2, g_sw, g_sw2, g_dec, g_rel, g_choke, len, hold;
    int32_t body, a_tone2;
    uint8_t env, wave, pad[2];
    int32_t d1p, d1n, asg, a_cut, a_dc, f_mid, mid, d2, n2, a_tone, f_clk, click, sub;
    /* the tail (a second voice after the punch: raw, frenchcore, uptempo): its wave (0: none), pitch, bend, timing,
     * envelope, filter, drive, level */
    uint8_t t_wave, t_env, t_bp, t_ratio;
    uint32_t t_inc, t_at, t_len, t_mod, t_lfo;
    int32_t t_bend, t_g_bend, t_g_dec, t_g_rel, t_g_fe, t_fenv, t_cut, t_q, t_drive, t_mix, t_wob;
    /* (Nov 2026) drive 1's curve, the tick (its decay per block, its kind, its pitch), the body's FM, its grit, the
     * moving mid, the push of the highs into drive 1, the tail's second saw */
    uint8_t dmode, ck_kind, fm_ratio, pad3;
    int32_t ck_g, ck_inc, fm, grit, mid_f0, mid_f1, mid_g, a_pre, pre, t_det;
    /* (Nov 2026) a sampled kick (0: none, else 1 + its index): its step (Q16), its end, PUNCH's gain over its first
     * 25 ms, DISTO's drive, TONE's low-pass, SUB's level */
    uint32_t smp, s_inc, s_end, s_len;
    int32_t s_pk, s_drv, s_norm, s_a, s_sub;
} tb_kick_coef_t;

typedef struct {
    uint32_t t, ph, sph, sw, sw2, bph, seed;
    int32_t at, lvl, clk;
    int32_t hx, hy, mlo, mbd, t1, t2, dx, dy, clo, cbd, shx, shy, sdx, sdy;
    uint8_t live, choke, rel, pad;
    uint32_t tph, tmph, tlfo, tt;
    int32_t tl, tbend, tfe, tlo, tbd, tdx, tdy, tinc, tf, tgate;
    uint8_t tdone, pdead, pad2[2];
    uint32_t cph, mph, ckinc, tph2;   /* (Nov 2026) the tick's sine, the body's modulator, the chirp, the second saw */
    int32_t mf, plo;                  /* .. the moving mid's place (Hz), the pre-emphasis's low part */
    int32_t og;                       /* .. its release after the drive (Q15; 0: not released): it fades out, not cut */
    uint32_t spos, sidx;              /* .. the sampled kick: where it is (Q16), which (1 + its index; 0: none) .. */
    int32_t slo;                      /* .. its low-pass */
} tb_kick_hit_t;

typedef struct {
    union {                       /* (a kick plays RAVE FM's engine or the first version's: tb_akick.h) */
        struct {
            uint8_t key[TB_KICK_NPARAM];  /* the knobs the coefficients were worked out from */
            int8_t note;                  /* .. the semitones played */
            uint32_t beat;                /* .. the beat (samples) */
            tb_kick_coef_t c;
            tb_kick_hit_t h[2];           /* the hit and the one it chokes */
            uint8_t cur;
        };
        tb_akick_t a;
    };
    uint8_t amode;                /* 1: the union holds the first version's kick */
} tb_kick_t;

typedef struct { const char *name; uint8_t p[TB_KICK_NPARAM]; } tb_kick_preset_t;
extern const tb_kick_preset_t TB_KICK_PRESETS[];
extern const unsigned TB_KICK_NPRESETS;
extern const char *const TB_KICK_TYPE_NAME[];
extern const unsigned TB_KICK_NTYPES;   /* (RAVE FM's types, then the first version's 22) */

void tb_kick_init(tb_kick_t *k);
/* a hit: semitones from the type's note (the keys), the knobs, the beat in samples */
void tb_kick_hit(tb_kick_t *k, const uint8_t *p, int semis, uint32_t beat, uint32_t seed);
/* n (<= 128) samples, Q15, added to out */
void tb_kick_render(tb_kick_t *k, const uint8_t *p, uint32_t beat, int32_t *out, unsigned n);
/* the note a type lands on (MIDI), for the screen */
int tb_kick_note(const uint8_t *p);
#endif
