/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: ACID, the 303 of the hardcore and acid tracks, its own machine. One saw or square oscillator, a
 * four-pole ladder filter that screams when pushed (RESO), its envelope (ENV MOD, DECAY), and the two things a 303's
 * steps carry: ACCENT (louder, the filter opened further and quicker; accents in a row climb, as the 303's do) and
 * SLIDE (the note held into the next one, which glides in without starting over).
 *   p: WAVE CUTOFF RESO ENVMOD DECAY ACCENT SLIDE DRIVE TUNE; the amp's ADSR p[24..27] (a 303's: a gate) */
#ifndef TB_ACID_H
#define TB_ACID_H
#include "tb_features.h"
#include <stdint.h>
#include "tb_env.h"

enum { TBA_WAVE, TBA_CUTOFF, TBA_RESO, TBA_ENVMOD, TBA_DECAY, TBA_ACCENT, TBA_SLIDE, TBA_DRIVE, TBA_TUNE, TBA_NPARAM };
#define TBA_ACCENT_VEL 118        /* a note this loud is accented */

typedef struct {
    uint32_t ph;
    int32_t pitch, target;        /* semitones Q16 */
    int32_t y[4], fb;             /* the ladder's stages */
    int32_t fenv, aenv, sweep;    /* the filter's envelope, the accent's and its climb (Q15) */
    uint8_t gate, note, accent, live, sliding;
    tb_env_t amp;
    tb_envk_t ka;
    int32_t blep_last;
} tb_acid_t;

typedef struct { const char *name; uint8_t p[TBA_NPARAM]; } tb_acid_sound_t;
extern const tb_acid_sound_t TBA_SOUND[];
extern const unsigned TBA_NSOUND;
extern const char *const TBA_WAVE_NAME[2];

void tb_acid_init(tb_acid_t *a);
/* a sound's knobs into p (and its gate envelope) */
void tb_acid_sound(uint8_t *p, unsigned i);
/* a note on (vel >= TBA_ACCENT_VEL: accented); on while another is held: it slides in */
void tb_acid_on(tb_acid_t *a, const uint8_t *p, unsigned note, unsigned vel);
void tb_acid_off(tb_acid_t *a, unsigned note);
/* n (<= 128) samples, Q15, added; the LFO's pitch (semitones Q16) and cutoff (octaves Q16) */
void tb_acid_render(tb_acid_t *a, const uint8_t *p, int32_t *out, unsigned n, int32_t lfo_pitch, int32_t lfo_cut);
int32_t tb_acid_env(const tb_acid_t *a, unsigned *stage);
#endif
