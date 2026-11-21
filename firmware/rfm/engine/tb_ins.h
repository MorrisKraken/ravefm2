/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the inserts, two on every track (FX INSERT A, INSERT B), each one of fifteen effects with its three
 * knobs: drives (OVERDRIVE DISTO FUZZ LO-FI FOLD), modulations (CHORUS FLANGER PHASER TREMOLO RING), filters (LP HP
 * BP WAH) and a compressor. */
#ifndef TB_INS_H
#define TB_INS_H
#include "tb_features.h"
#include <stdint.h>

enum { TB_INS_OFF, TB_INS_OVERDRIVE, TB_INS_DISTO, TB_INS_FUZZ, TB_INS_LOFI, TB_INS_FOLD, TB_INS_CHORUS,
       TB_INS_FLANGER, TB_INS_PHASER, TB_INS_TREMOLO, TB_INS_RING, TB_INS_LP, TB_INS_HP, TB_INS_BP, TB_INS_WAH,
       TB_INS_COMP, TB_NINS };
#if TB_HAS_INS_MOD
#define TB_INS_BUF 1024
#else
#define TB_INS_BUF 1                  /* (no CHORUS, FLANGER: no delay line) */
#endif

typedef struct { uint8_t type, p[3]; } tb_ins_set_t;       /* the effect and its three knobs (0..127) */
typedef struct {
    int16_t buf[TB_INS_BUF];      /* the modulations' delay line */
    uint16_t w;
    uint32_t ph;                  /* their LFO */
    int32_t lo, bd, ap[4], env, hold, lp, fb;
} tb_ins_t;

extern const char *const TB_INS_NAME[TB_NINS];
extern const char *const TB_INS_KNOB[TB_NINS][3];
void tb_ins_init(tb_ins_t *s);
/* n samples in place (Q15); step: a sixteenth in samples (the modulations' rates follow the tempo) */
void tb_ins_run(const tb_ins_set_t *c, tb_ins_t *s, int32_t *x, unsigned n);
#endif
