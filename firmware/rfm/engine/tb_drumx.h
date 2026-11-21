/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2's 909: a drum machine of synthesized sounds (tb_drumx.c: DRUM-X, GPL, a tuned oscillator with its pitch
 * envelope and FM, a filtered noise, two patches A and B and a MORPH between them), playing the 909 track's eleven
 * pieces (BD SD CP CH OH MT RS CR LT HT RD) on the 909's steps and keys.
 * The track's knobs (tb_track_t p[]): TUNE DECAY TONE NOISE (the whole kit, 64: as the kit has it), KIT (one of the
 * factory kits), MORPH (A .. B, every piece), FM (the attack's metal edge), DRIVE. Each piece its own (D9_* in
 * tb_drumx_piece(), kept in the track's spare bytes): TUNE (+-24 semitones), DECAY, MORPH (64: the kit's), LEVEL. */
#ifndef TB_DRUMX_H
#define TB_DRUMX_H
#include <stdint.h>
#include "tb_909.h"

#define TB_DX_PIECES 11
enum { TBX_TUNE, TBX_DECAY, TBX_TONE, TBX_NOISE, TBX_KIT, TBX_MORPH, TBX_FM, TBX_DRIVE, TBX_NPARAM };
enum { D9_TUNE, D9_DECAY, D9_MORPH, D9_LEVEL, D9_N };   /* a piece's own: d9[piece * D9_N + D9_*] */
#define TB_DX_NKIT 12     /* 0: 909 VRAI (the recordings, tb_909.c), then DRUM-X's kits */

typedef struct {
    uint8_t v[TB_DX_PIECES][96];  /* a hit per piece (dx_voice_t, kept opaque here) */
    uint8_t vel[TB_DX_PIECES];
} tb_dx_t;

void tb_dx_init(tb_dx_t *d);
void tb_dx_hit(tb_dx_t *d, tb909_t *smp, const uint8_t *p, unsigned piece, unsigned vel);
/* KIT 0: the recordings (smp), their MORPH towards DRUM-X's 909 CLUB; the other kits: DRUM-X's */
void tb_dx_render(tb_dx_t *d, tb909_t *smp, const uint8_t *p, const uint8_t *d9, const uint8_t *adsr, int32_t *out,
                  unsigned n);
void tb_dx_defaults(uint8_t *p, uint8_t *d9);           /* a track become a 909: its knobs as the kit has them */
const char *tb_dx_kit_name(unsigned k);
#endif
