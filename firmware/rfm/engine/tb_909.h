/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the 909 track, the user's own TR-909 recordings (tools/gen_909.py). Eleven pieces, each its own voice
 * (a new hit restarts it; the closed hat chokes the open one). The track's knobs move every piece from as recorded
 * (64): TUNE the playback rate (+-1 octave), DECAY a fade (64 and up: the whole recording; lower: shorter), TONE darker
 * below 64, brighter above, SNAP the first 4 ms (softer below 64, harder above). */
#ifndef TB_909_H
#define TB_909_H
#include <stdint.h>
#include "tb_env.h"

enum { TB909_BD, TB909_SD, TB909_CP, TB909_CH, TB909_OH, TB909_MT, TB909_RS, TB909_CR, TB909_LT, TB909_HT,
       TB909_RD, TB909_PIECES };
enum { TB909_TUNE, TB909_DECAY, TB909_TONE, TB909_SNAP, TB909_NPARAM };

typedef struct {
    uint32_t fr, k;               /* the fraction (Q24) past sample k - 5, the next sample to read */
    int32_t pred, idx, lp, q;     /* ADPCM's decoder, TONE's one-pole, the fade (Q30) */
    uint16_t at;                  /* samples played (SNAP's window) */
    uint8_t live, choke;
    uint16_t vel;                 /* the hit's level (Q15) */
    int16_t h[8];                 /* the last 8 samples read: the interpolation's taps */
} tb909_voice_t;

typedef struct {
    tb909_voice_t v[TB909_PIECES];
    tb_env_t env[TB909_PIECES];   /* each piece its envelope (the track's ADSR) */
    tb_envk_t k;
} tb909_t;

extern const char *const TB909_NAME[TB909_PIECES];
void tb909_init(tb909_t *d);
void tb909_hit(tb909_t *d, unsigned piece, unsigned vel);
void tb909_off(tb909_t *d, unsigned piece);
/* n (<= 128) samples, Q15, added to out; adsr: the track's envelope; lfo: a pitch offset (semitones Q16) */
void tb909_render(tb909_t *d, const uint8_t *p, const uint8_t *adsr, int32_t lfo, int32_t *out, unsigned n);
void tb909_render_x(tb909_t *d, const uint8_t *p, const uint8_t *adsr, int32_t lfo, const uint8_t *pp, unsigned mix,
                    int32_t *out, unsigned n);
#endif
