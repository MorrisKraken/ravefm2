/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the BREAK track, Rave FM's slicer rebuilt. The user's breaks (tools/gen_breaks.py) cut in slices:
 *   SLICES  4 / 8 / 16 / 32 equal ones, AUTO one per hit (found when the firmware is built), MAN the ones set by hand
 *   MODE    ONE the slice to its end (a key let go does not stop it), GATE until the key is let go, LOOP the slice again
 *           and again while the key is held, FULL from the slice to the end of the break (REV: to its start)
 *   PITCH   +-24 semitones, faster is higher (a record's speed)
 *   SYNC    ON: the break follows the tempo as a record on a deck does (a 140 Amen at 180 plays 180 and higher)
 *   DECAY   a fade over each slice (127: none)   TONE a low-pass (127: open)
 *   RELEASE the last slice played does not stop dry at its end: it rings on into what follows it, fading out (0: dry)
 *   DISQUE  (RAVE FM 2) the whole break looped, as a record turning: its speed (and pitch) the tempo's, started
 *           again at its first beat every time round (PLAY: on the bars); a key starts it, the same key again stops it
 *   REV     the slices backwards     START which slice the lowest key (and step note 60) plays
 * Note n plays slice (n - 60 + START) mod the slice count. A new slice takes over from the one before (2 ms), a tail
 * already ringing fades on under it. */
#ifndef TB_BREAK_H
#define TB_BREAK_H
#include "tb_features.h"
#include <stdint.h>
#include "tb_env.h"

enum { TBB_SRC, TBB_DIV, TBB_MODE, TBB_PITCH, TBB_DECAY, TBB_TONE, TBB_REL, TBB_REV, TBB_START, TBB_SYNC, TBB_NPARAM };
enum { TBB_ONE, TBB_GATE, TBB_LOOP, TBB_FULL, TBB_DISC };   /* (RAVE FM 2) DISC: the whole break round and round, on the tempo */
enum { TBB_D4, TBB_D8, TBB_D16, TBB_D32, TBB_AUTO, TBB_MAN };
#define TBB_MAXSLICE 32
#define TBB_VOICES 3
extern uint8_t tb_brk_transport;  /* (the engine: PLAY on) */

typedef struct {
    uint32_t fr, k, stop, a, b;  /* the fraction (Q24), the next sample to read, where it stops; the slice [a, b) */
    int32_t q;                   /* the fade (DECAY, the release), Q30 */
    int32_t lp;
    uint16_t in, vel, w;         /* the fade in (samples played, up to 32); the level (Q15); the taps' ring */
    uint8_t live, rev, loop, gate, note, tail, out;   /* out: fading out (1 taken over, 2 let go), tail: ringing on */
    uint8_t disc;                /* (DISC: it loops on with the key let go) */
    int8_t semi;                 /* its own pitch (SAMPLE: the key's), semitones */
    int16_t h[8];
    tb_env_t env;                /* the track's ADSR */
} tb_brk_voice_t;

typedef struct {
    tb_brk_voice_t v[TBB_VOICES];
    uint32_t age;
    tb_envk_t k;
} tb_brk_t;

/* the manual slices of a track: starts in 4-sample units, n of them (0: none, AUTO plays) */
typedef struct { uint16_t at[TBB_MAXSLICE]; uint8_t n; } tb_brk_man_t;

/* the user's own samples, after the built-in breaks (16 kHz mono, kept companded: 8 bits a sample) */
#define TBB_USER 4
#ifdef RFM2
#define TBB_POOL (16000u * 6u)    /* (RAVE FM 2: 6 s in all, kept in flash and played from it) */
#else
#define TBB_POOL (16000u * 12u)   /* 12 s in all */
#endif
typedef struct {
    uint32_t magic, n;
    struct { char name[12]; uint32_t off, len; uint8_t nauto, pad[3]; uint32_t at[TBB_MAXSLICE]; } s[TBB_USER];
} tb_brk_user_t;
extern tb_brk_user_t tb_brk_user;
#if TB_HAS_USER_BRK && defined(RFM2)
extern const uint8_t *tb_brk_rom;  /* the samples' bytes in flash (tb_store.c), 0: none */
#elif TB_HAS_USER_BRK
extern uint8_t tb_brk_pool[TBB_POOL];
#endif
/* a sample coming in (16 kHz, mu-law bytes, as the phone and the page send it): user slot u (past the last: a new
 * one; the ones after it are dropped), its name, its length (cut to the room left): 0 ready, -1 no room; its bytes at
 * off; done: its hits found (the AUTO slices) */
int tb_brk_user_begin(unsigned u, const char *name, uint32_t n);
void tb_brk_user_write(uint32_t off, const uint8_t *ulaw, uint32_t n);
void tb_brk_user_end(void);
/* only the first n of the user's samples kept */
void tb_brk_user_keep(unsigned n);

/* all the sources: the built-in breaks, then the user's */
unsigned tb_brk_count(void);
const char *tb_brk_name(unsigned b);
unsigned tb_brk_default_div(unsigned b);
unsigned tb_brk_bars(unsigned b);
/* the slices of break b cut as p says: how many; the start and end (samples) of slice i into *a, *b */
unsigned tb_brk_slices(const uint8_t *p, const tb_brk_man_t *m, unsigned i, uint32_t *a, uint32_t *b);
uint32_t tb_brk_len(unsigned b);
/* break b's level at sample k (for the screen), 0..32767 */
int32_t tb_brk_peek(unsigned b, uint32_t k);

void tb_brk_init(tb_brk_t *s);
void tb_brk_on(tb_brk_t *s, const uint8_t *p, const tb_brk_man_t *m, unsigned note, unsigned vel);
/* a region [a, b) of the material at its own pitch (the SAMPLE track) */
void tb_brk_play(tb_brk_t *s, const uint8_t *p, uint32_t a, uint32_t b, unsigned note, unsigned vel, int semi);
void tb_brk_off(tb_brk_t *s, const uint8_t *p, unsigned note);
/* n (<= 128) samples, Q15, added; bpm10: the tempo (SYNC); adsr: the track's envelope; lfo: a pitch offset (Q16) */
void tb_brk_release(tb_brk_t *s, unsigned note);
void tb_brk_render(tb_brk_t *s, const uint8_t *p, unsigned bpm10, const uint8_t *adsr, int32_t lfo, int32_t *out, unsigned n);
/* the newest voice's envelope (Q30) and its stage, for the ENV screen */
int32_t tb_brk_env(const tb_brk_t *s, unsigned *stage);
#endif
