/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the synth of the melodic tracks (BASS, LEAD, STAB, HOOVER): one engine, the classic way, so that it
 * reads as every synth does.
 *   OSC     WAVE (SCIE, CARRE, PWM, SUPER: a stack of saws, HOOVER: the rave's moving saws, SINUS, ORGUE: drawbars,
 *           FM: two operators, CLOCHE: FM at 1:3.5), DETUNE (the oscillators spread apart; ORGUE: the upper
 *           drawbars, FM CLOCHE: the index), SUB (a square an octave down), NOISE
 *   FILTRE  CUTOFF, RESO, ENV (how far the filter's envelope opens it), SUIVI (the filter follows the keys)
 *   JEU     MODE (MONO, LEGATO: the envelopes go on and the note glides, POLY: 4 voices), GLIDE, BEND (the note starts
 *           up to 12 semitones under or over and slides in: the hoover's dive), DRIVE (a warm saturation)
 *   ENV     the amp's ADSR (p[24..27]) and the filter's ADSR (p[28..31]), drawn on the ENV screen
 * Chords come from the track (the SCALE screen): a key gives the voices all its notes at once. The oscillators are
 * band-limited (polyBLEP), the filter a zero-delay state-variable one (stable however far it is pushed). */
#ifndef TB_SYNTH_H
#define TB_SYNTH_H
#include "tb_features.h"
#include <stdint.h>
#include "tb_env.h"
#include "tb_rave.h"

enum { TBS_WAVE, TBS_DETUNE, TBS_SUB, TBS_GLIDE, TBS_CUTOFF, TBS_RESO, TBS_ENV, TBS_DRIVE, TBS_MODE, TBS_BEND,
       TBS_NOISE, TBS_KTRK, TBS_MACRO, TBS_MTYPE, TBS_SPEED, TBS_CHORUS, TBS_WHOOP,
       /* the hardcore stage: DIST (after the filter, up to x60: the resonance's peak crushed, the screech), its TYPE,
        * the filter's TYPE (low-, band-, high-pass), HPF (the low end cut after it, 20 .. 800 Hz), VIB (a fast
        * vibrato, up to a semitone) */
       TBS_DIST, TBS_DTYPE, TBS_FTYPE, TBS_HPF, TBS_VIB,
       /* GATE: the note chopped on the beat (1/8, 1/16, 1/32, 1/16 triplets, 1/16 swung); PTIME: BEND's time (0: 120 ms;
        * else 10 ms .. 2.5 s, and BEND up to two octaves: the laser, the dive) */
       TBS_GATE, TBS_PTIME, TBS_AMP = 24, TBS_FILT = 28, TBS_NPARAM = 32 };
enum { TBS_G_OFF, TBS_G_8, TBS_G_16, TBS_G_32, TBS_G_16T, TBS_G_SWING, TBS_NG };
extern const char *const TBS_GATE_NAME[TBS_NG];
extern uint32_t tb_syn_step;     /* a sixteenth (samples): the engine sets it */
enum { TBS_D_SOFT, TBS_D_HARD, TBS_D_FOLD, TBS_D_ASYM, TBS_ND };
enum { TBS_F_LP, TBS_F_BP, TBS_F_HP, TBS_NF };
extern const char *const TBS_DTYPE_NAME[TBS_ND];
extern const char *const TBS_FTYPE_NAME[TBS_NF];
/* MACRO: one knob for a sound played by hand (TYPE: SCREECH the frenchcore's scream: the filter and its resonance up,
 * a fast vibrato; REESE: the saws apart and the filter swaying; WOBBLE: the filter wobbling; GROWL: FM and drive) */
enum { TBS_M_OFF, TBS_M_SCREECH, TBS_M_REESE, TBS_M_WOBBLE, TBS_M_GROWL, TBS_NMTYPE };
extern const char *const TBS_MTYPE_NAME[TBS_NMTYPE];
enum { TBS_SAW, TBS_SQUARE, TBS_PWM, TBS_SUPER, TBS_HOOVER, TBS_SINE, TBS_ORGAN, TBS_FM, TBS_BELL,
       /* SYNC: a saw synced to the note (DETUNE: how far up, the filter's envelope sweeps it: the screaming lead);
        * VOIX: a voice through two formants (DETUNE: the vowel OU O A E I, the envelope opens it: the rave's "wooh");
        * RING: a saw ring-modulated (DETUNE: the ratio: metal, uptempo's screech); STAB: two squares and a saw an
        * octave up, apart (the rave stab); M1: drawbars of a digital organ, its top ringing off (DETUNE: the timbre);
        * FOLD: a sine folded on itself (DETUNE and the envelope: how far: the growl) */
       TBS_SYNC, TBS_VOX, TBS_RING, TBS_STAB, TBS_M1, TBS_FOLD,
       /* HOOVER 2: the Alpha Juno's PWM saw (a saw chopped by a pulse at twice the note, its width sweeping) and a PWM
        * pulse, three apart; WHOOP its rise at the attack (up to 7 semitones) */
       TBS_HOOV2,
       /* RAVE: the first version's leads (the Atelier's RAVE engine, tb_rave.h: ANGER, V1, SUPA), played by it */
       TBS_RAVE, TBS_NWAVE };
enum { TBS_MONO, TBS_LEGATO, TBS_POLY, TBS_NMODE };
#define TBS_VOICES 4
#define TBS_OSC 5

typedef struct {
    uint32_t ph[TBS_OSC], sub;   /* the oscillators' phases, the sub's */
    int32_t pitch, target, bend; /* the note now (gliding), where it goes, the bend left (semitones Q16) */
    int32_t wh1, wh2;            /* HOOVER's WHOOP: the pitch up then back (two falls, semitones Q16) */
    tb_env_t amp, flt;
    int32_t ic1, ic2;            /* the filter's state (held inside +-5 x 2^20) */
    uint32_t c_hz;               /* (the filter's coefficients as last worked out: for this cutoff, this damping) */
    int32_t c_k, c_a1, c_a2, c_a3, c_tk1, c_tk4;
    int32_t f1lo, f1bd, f2lo, f2bd;  /* VOIX: its two formants' */
    int32_t dc;                  /* the new waves: their offset, taken off after the drive */
    int32_t hp, tame, dprev, t4; /* the hardcore stage: its high-pass, its top's low-pass, its input before, its 4x low-pass */
    uint32_t gpos;               /* GATE: samples since the note began */
    int32_t glvl, bendk;         /* .. its level (Q15, smoothed); BEND's fall per sub-block (Q16) */
    uint8_t note, gate, live, key;     /* key: the note of the key that started it (a chord's notes go off with it) */
    uint8_t shed;                /* (the FM-1 out of time, tb_heavy 2: this voice fading out in ~5 ms) */
    uint16_t vel;
    uint32_t age;
} tb_syn_voice_t;

typedef struct {
    union {                      /* (a sound plays one or the other: the WAVE RAVE's voices are the Atelier's) */
        tb_syn_voice_t v[TBS_VOICES];
        tb_rave_t rave;
    };
    uint8_t rmode;               /* the voices hold the RAVE's (1) or the synth's (0) */
    uint8_t held[8], nheld;      /* the keys down, in order (MONO, LEGATO: the last one plays, then the one before) */
    int8_t iv[8][4];             /* .. each one's chord */
    uint8_t niv[8];
    uint32_t lfo, age, noise, mlfo, vlfo, hl1, hl2;
    tb_envk_t ka, kf;
    uint8_t kg_key;              /* (the glide's step, as worked out for this GLIDE) */
    int32_t kg_val;
} tb_syn_t;

extern const char *const TBS_WAVE_NAME[TBS_NWAVE];
extern const char *const TBS_MODE_NAME[TBS_NMODE];

/* the sounds PRESETS goes through on a synth track: a library by family (BASS, LEAD, STAB, HOOVER, PAD, PLUCK,
 * ORGUE, FX); chord: the chords the sound asks of its track (255: leave them) */
typedef struct { const char *name, *family; uint8_t chord; uint8_t p[TBS_NPARAM]; } tb_syn_sound_t;
extern const tb_syn_sound_t TBS_SOUND[];
extern const unsigned TBS_NSOUND;
/* the sound each synth slot starts with (BASS, LEAD, STAB, HOOVER) */
unsigned tb_syn_default(unsigned slot);

void tb_syn_init(tb_syn_t *s);
/* a key: its note and its chord (iv: the intervals, iv[0] = 0; n: how many, 1..4) */
void tb_syn_on(tb_syn_t *s, const uint8_t *p, unsigned note, unsigned vel, const int8_t *iv, unsigned n);
void tb_syn_off(tb_syn_t *s, const uint8_t *p, unsigned note);
void tb_syn_all_off(tb_syn_t *s);
/* n (<= 128) samples, Q15, added; the LFO's pitch (semitones Q16) and cutoff (octaves Q16) */
void tb_syn_render(tb_syn_t *s, const uint8_t *p, int32_t *out, unsigned n, int32_t lfo_pitch, int32_t lfo_cut);
/* (21 Nov 2026) the same in two (the two cores sharing a synth's voices): tb_syn_begin once a block (0: the RAVE's
 * voices, by one tb_syn_voices with bit 0), then tb_syn_voices for the voices in vmask, on either core, each voice by
 * one; a sound with NOISE shares its noise's generator: its voices all by one (c.noise); tb_syn_live: the live ones */
typedef struct {
    const uint8_t *p;
    unsigned n, wave, nosc, ftype, dtype, gate, rave;
    uint32_t gate_len, pw, hw1, hw2;
    int32_t kg, noise, res_k, drive, sub, vgain, vib, dist, hpk, foldg, m_oct, m_semi, m_spread, lfo_pitch, lfo_cut;
    int32_t k8a[3], k8f[3];      /* (the envelopes' 8-sample coefficients) */
} tb_syn_ctx_t;
int tb_syn_begin(tb_syn_t *s, const uint8_t *p, unsigned n, int32_t lfo_pitch, int32_t lfo_cut, tb_syn_ctx_t *c);
void tb_syn_voices(tb_syn_t *s, const tb_syn_ctx_t *c, int32_t *out, unsigned vmask);
unsigned tb_syn_live(const tb_syn_t *s);
/* the level of the loudest voice's amp envelope (Q30) and its stage, for the ENV screen */
int32_t tb_syn_env(const tb_syn_t *s, int filter, unsigned *stage);
#endif
