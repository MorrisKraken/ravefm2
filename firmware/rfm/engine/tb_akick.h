/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM: the kicks of the first version (the Atelier: RTD 93, MOKUM, ABYSSE...), its own engine (tb_akick.c).
 * Its eight knobs are a kick's (TYPE PITCH PUNCH LONG DISTO CREUX TONE SUB), TYPE one of its 22 (0..21 here; on a kick
 * track they come after RAVE FM's own types, named *ROND, *CREUX ...). */
#ifndef TB_AKICK_H
#define TB_AKICK_H
#include <stdint.h>
#define TB_AKICK_NTYPES 22
typedef struct {
    uint8_t key[8];
    int8_t note;
    uint8_t cur, pos;
    uint32_t beat;
    uint32_t c[32];               /* its coefficients (tb_akick.c kick_coef_t) */
    uint32_t h[2][26];            /* the hit and the one it chokes (kick_hit_t) */
    int32_t buf[32];              /* the tick being played out */
} tb_akick_t;
void tb_akick_init(tb_akick_t *k);
void tb_akick_hit(tb_akick_t *k, const uint8_t *p, int semis, uint32_t beat, uint32_t seed);
void tb_akick_render(tb_akick_t *k, const uint8_t *p, uint32_t beat, int32_t *out, unsigned n);
int tb_akick_note(const uint8_t *p);
#endif
