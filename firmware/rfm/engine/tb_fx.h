/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the channel strip's filter and the two send effects. FILTRE is one knob, as on a DJ mixer: 64 open, to
 * the left a low-pass closing to 80 Hz, to the right a high-pass opening to 6 kHz. DELAY: a ping-pong on a dotted
 * eighth of the tempo, its repeats darkening. REVERB: a hall of four lines, about 2.5 s, its tail in stereo. */
#ifndef TB_FX_H
#define TB_FX_H
#include "tb_features.h"
#include <stdint.h>

typedef struct { int32_t lo, bd; int32_t f, q; uint8_t knob, mode; } tb_filter_t;

void tb_filter_set(tb_filter_t *f, unsigned knob);          /* 0..127 */
void tb_filter_run(tb_filter_t *f, int32_t *x, unsigned n);  /* in place, Q15 */

/* the effects' settings (the FX pages), 0..127 each (EQ, WIDTH: 64 = flat); the types index the names below */
typedef struct {
    uint8_t rev_type, rev_size, rev_damp, rev_pre, rev_level;
    uint8_t dly_type, dly_time, dly_fb, dly_tone, dly_level;
    uint8_t eq_low, eq_mid, eq_high, comp, drive, width;
    uint8_t dist_type, dist_gain, dist_tone, dist_mix;   /* the master's distortion (DISTO MASTER) */
} tb_fx_set_t;
#define TB_FX_NDIST 5
extern const char *const TB_FX_DIST_NAME[TB_FX_NDIST];
#define TB_FX_NDLY 7
#define TB_FX_NREV 5
#define TB_FX_NDLYT 5
extern const char *const TB_FX_DLY_NAME[TB_FX_NDLY];
extern const char *const TB_FX_REV_NAME[TB_FX_NREV];
extern const char *const TB_FX_DLYT_NAME[TB_FX_NDLYT];

void tb_fx_init(void);
/* the sends of a block (mono Q15) -> added into l and r; step: a sixteenth in samples; wash: the reverb longer (the
 * REVERB WASH effect); hold: the delay's repeats held, nothing new in (ECHO OUT) */
void tb_fx_run(const tb_fx_set_t *fx, const int32_t *dly, const int32_t *rev, int32_t *l, int32_t *r, unsigned n,
               uint32_t step, int wash, int hold);
/* the master's chain: EQ, compressor, width, drive */
void tb_fx_master(const tb_fx_set_t *fx, int32_t *l, int32_t *r, unsigned n);
/* the master's distortion: CHAUD (warm), GABBER (the kick's hard clip), ROTTERDAM (heavier, the bass pushed),
 * TERROR (a square, crushed) */
void tb_fx_dist(const tb_fx_set_t *fx, int32_t *l, int32_t *r, unsigned n);
#endif
