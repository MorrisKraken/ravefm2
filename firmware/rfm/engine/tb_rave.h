/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM: the leads of the first version (the Atelier, Rave FM on Felucca, October 2026), brought back as they were:
 * its RAVE engine's millennium-lead chain (ANGER, V1, SUPA) and its voices' envelope and glide, the same fixed-point
 * steps, so that ANGER, ANGER V1, SUPA DUPA... sound as in the video. A synth sound plays it when its WAVE is RAVE
 * (tb_synth.c hands the notes and the render over); the sound's values then mean:
 *   SUB the TYPE (0 ANGER, 1 V1, 2 SUPA), DETUNE SPRD, CUTOFF CUT, RESO RESO, DRIVE DISTO, ENV ENV, BEND BEND,
 *   MACRO COLOR (the octave pair's level), MODE (LEGATO or POLY), GLIDE the Atelier's (TIME: 1 + g^2 / 8 ticks),
 *   the amp's A D S R the Atelier's (its tables), CHORUS the Atelier's chorus send (kept, not played here). */
#ifndef TB_RAVE_H
#define TB_RAVE_H
#include <stdint.h>

#define TBR_VOICES 6                     /* (as the Atelier's RAVE: its poly cap) */
enum { TBR_ANGER, TBR_V1, TBR_SUPA, TBR_NTYPE };

typedef struct {
    /* the Atelier's voice (voice.c): its envelope, its pitch and glide */
    uint8_t note, vel, gate, active, stage;
    int32_t env, env_out;            /* Q24, the last amplitude (Q15) */
    int32_t pitch16, pitch_cur, gstep;
    int32_t s0, s1;                  /* the first filter's state (kept over a retrigger) */
    uint32_t age;
    /* the RAVE engine's voice (eng_rave.c rave_voice_t, chain 1) */
    uint32_t ph[9];
    uint32_t fe, pe;
    int32_t hx, hy, f2a, f2b, p1, p2, tx;
} tb_rave_voice_t;

typedef struct {
    tb_rave_voice_t v[TBR_VOICES];
    uint8_t mono_stack[8], nmono, mono_note, rr, pos;
    uint32_t vage;
    int32_t buf[32];             /* the control tick being played out (the Atelier's ticks of 32 kept across calls) */
} tb_rave_t;

void tb_rave_on(tb_rave_t *r, const uint8_t *p, unsigned note, unsigned vel);
void tb_rave_off(tb_rave_t *r, const uint8_t *p, unsigned note);
void tb_rave_all_off(tb_rave_t *r);
/* n samples (any number), Q15, added; the LFO's pitch (semitones Q16) and cutoff (octaves Q16) */
void tb_rave_render(tb_rave_t *r, const uint8_t *p, int32_t *out, unsigned n, int32_t lfo_pitch, int32_t lfo_cut);
#endif
