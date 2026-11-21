/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the instrument. Eight tracks, each a machine (KICK, BREAK, 909, a synth, a sample) with its sound, its
 * envelope, its LFO, its chords, its arpeggiator, its mixer channel and, in each of the eight patterns, its steps and
 * its own loop length. One clock (32nds: a track can run at half, normal or double speed), swing, ratchets, chance,
 * gates; the key and scale of the song; the effects (reverb, delay, the live ones: rolls, sweeps, stutter...); the
 * duck under the kick; a clean ceiling. tb_engine_render runs in the audio interrupt; everything else is called from
 * the control task. */
#ifndef TB_ENGINE_H
#define TB_ENGINE_H
#include <stdint.h>
#include <stddef.h>
#include "tb_kick.h"
#include "tb_909.h"
#include "tb_drumx.h"
#include "tb_break.h"
#include "tb_synth.h"
#include "tb_acid.h"
#include "tb_fx.h"
#include "tb_ins.h"
#include "tb_ui.h"

#if TB_LITE || defined(TB_MEL_STEPS)            /* (the FM-1: its RAM; the user's choice, 5 Nov 2026; RAVE FM 2 too) */
#ifndef TB_MEL_STEPS
#define TB_MEL_STEPS 128                       /* the melodies' (7 Nov 2026: 128; 64 if the FM-1's RAM is short) */
#endif
#define TB_STEPS TB_MEL_STEPS                  /* a track's steps at most */
#define TB_DSTEPS 64                           /* tracks 1..3 (the kick, the break, the 909): 64 */
#define TB_OLD_STEPS 64                        /* (version 11 and before: every track 64) */
#define TB_PATTERNS 4
#else
#define TB_STEPS 128
#define TB_DSTEPS 128
#define TB_OLD_STEPS 128
#define TB_PATTERNS 8
#endif
#ifdef RFM2
#define TB_NDRUMT 2                            /* (RAVE FM 2: the kick, the break) */
#else
#define TB_NDRUMT 3
#endif
#define TB_NDRUMT_DOC                            /* the first three tracks: TB_DSTEPS, the others TB_STEPS */
#define TB_TCAP(t) ((unsigned)((t) < TB_NDRUMT ? TB_DSTEPS : TB_STEPS))   /* track t's steps at most */
#define TB_SOFF(t) ((unsigned)((t) < TB_NDRUMT ? (t) * TB_DSTEPS : TB_NDRUMT * TB_DSTEPS + ((t) - TB_NDRUMT) * TB_STEPS))
#define TB_NSTEP (TB_NDRUMT * TB_DSTEPS + (TB_TRACKS - TB_NDRUMT) * TB_STEPS)
#define TB_OLD_NSTEP (TB_TRACKS * TB_OLD_STEPS)
#define TB_SONG 32
/* (the board: the audio interrupt near its time, the synths thin their oscillators until it eases) */
extern volatile uint8_t tb_heavy;
#define TB_P_ENV 24                           /* every machine's envelope: p[24..27] = A D S R */
enum { TB_M_NONE, TB_M_KICK, TB_M_BREAK, TB_M_909, TB_M_SYNTH, TB_M_SAMPLE, TB_M_ACID, TB_NMACHINE };

/* a step: what it plays (a drum track: a bit per piece; the others: 1 = a note), the note, its velocity (0: 100),
 * its length (in eighths of a step; 0: 6, three quarters), its chance, its repeats and whether it skips the
 * arpeggiator (a note the arpeggiator itself played, recorded) */
typedef struct {
    uint16_t hits;
    uint8_t note, vel, gate, flags;
    uint8_t more[3];                          /* (version 11) a chord set by hand: its other notes (0: none) */
    uint8_t spare;
} tb_step_t;
typedef char tb_step_is_10_bytes[sizeof(tb_step_t) == 10 ? 1 : -1];
#define TB_PROB(f) ((f) & 7u)                 /* 0: always, 1..7: 7/8 .. 1/8 of the times */
#define TB_RATCH(f) ((((f) >> 3) & 3u) + 1u)  /* the step played 1..4 times over its length */
#define TB_DIRECT 0x20u                       /* straight to the machine (no chord, no arpeggiator) */
#define TB_ACCENT 0x40u                       /* ACID: accented */
#define TB_SLIDE 0x80u                        /* ACID: held into the next note, which glides in */
/* the step's timing as played (REC, not quantized all the way): hits' top 5 bits, -16..15 32nds of a step, early or
 * late; the hits themselves below (the 909: its 11 pieces) */
#define TB_HITS(h) ((h) & 0x07FFu)
/* every track but the 909: hits 1 = on the step, 2 = a 32nd later (half a step at x1), 3 = both (REC, GRID 1/32) */
#define TB_HALF_ONLY(h) ((TB_HITS(h) & 3u) == 2u)
#define TB_HALF_TOO(h) ((TB_HITS(h) & 3u) == 3u)
#define TB_REC_32 8u
enum { TB_RECG_16, TB_RECG_32, TB_RECG_8, TB_RECG_FREE };   /* rec_quant >> 3: the grid (FREE: none, as played) */
#define TB_MICRO(h) ((int)(((h) >> 11) & 31u) - (((h) & 0x8000u) ? 32 : 0))
#define TB_WITH_MICRO(h, m) ((uint16_t)(TB_HITS(h) | ((((unsigned)(m)) & 31u) << 11)))
#define TB_FLAGS(prob, ratch) ((uint8_t)(((prob) & 7u) | ((((ratch) - 1u) & 3u) << 3)))

/* a parameter lock: on one step of one track, a knob's value while that step plays (ref1: the knob + 1, 0 free;
 * knobs 0..31 the machine's, TB_LK_* the channel's) */
typedef struct { uint8_t track, step, ref1, val; } tb_lock_t;
#define TB_NLOCK 128                          /* a pattern's locks, all tracks */
#define TB_STEP_LOCKS 4                       /* a step's at most */
enum { TB_LK_VOL = 32, TB_LK_FILT, TB_LK_REV, TB_LK_DLY, TB_LK_PAN = 39 };

typedef struct {
    uint8_t len[TB_TRACKS];                   /* each track's loop, 1..128 steps */
    tb_step_t step[TB_NSTEP];                 /* the tracks' one after the other (TB_SOFF, TB_TCAP) */
    tb_lock_t lock[TB_NLOCK];
} tb_pattern_t;

enum { TB_ARP_OFF, TB_ARP_UP, TB_ARP_DOWN, TB_ARP_UPDOWN, TB_ARP_DOWNUP, TB_ARP_UPDOWN2, TB_ARP_RANDOM,
       TB_ARP_PLAYED, TB_ARP_CHORD, TB_ARP_CONVERGE, TB_ARP_DIVERGE, TB_ARP_NMODE };
#define TB_ARP_NRATE 9
#define TB_ARP_NRHYTHM 16
enum { TB_ACC_PLAYED, TB_ACC_FIXED, TB_ACC_BEAT, TB_ACC_RISE, TB_NACC };
enum { TB_SPEED_HALF, TB_SPEED_1, TB_SPEED_2, TB_NSPEED };
enum { TB_LFO_SINE, TB_LFO_TRI, TB_LFO_SQUARE, TB_LFO_RAMPUP, TB_LFO_RAMPDN, TB_LFO_SH, TB_NLFOW };
enum { TB_DEST_VOLUME, TB_DEST_FILTRE, TB_DEST_PITCH, TB_DEST_PAN, TB_DEST_CUTOFF, TB_NDEST };
#define TB_LFO_NSYNC 10
#define TB_NSCALE 16
#define TB_NCHORD 15
#define TB_NVOICING 3

typedef struct {
    uint8_t machine;
    char name[8];
    uint8_t vol, duck;                        /* 0..127 */
    uint8_t filt, rev, dly, pan;              /* the channel: FILTRE (64 open), the sends, the pan (64 centre) */
    uint8_t muted, solo, speed;
    /* the arpeggiator */
    uint8_t arp, arp_rate, arp_gate, arp_oct, arp_latch, arp_rhythm, arp_swing, arp_accent;
    /* the LFO */
    uint8_t lfo_wave, lfo_rate, lfo_depth, lfo_dest, lfo_sync, lfo_retrig;
    /* the chords a key plays (the SCALE screen) */
    uint8_t chord, inversion, voicing;
    int8_t transpose;                         /* its steps transposed (semitones), as they play */
    uint8_t eq[3];                            /* its EQ: bass, mids, treble (64 flat, 0 cut) */
    uint8_t chorus;                           /* its stereo chorus (the hoover's width): 0 none */
    tb_ins_set_t ins[2];                      /* its two inserts (FX INSERT A, B) */
    uint8_t p[32];                            /* the machine's knobs (p[24..27]: its envelope) */
    tb_brk_man_t man;                         /* BREAK: the slices set by hand */
} tb_track_t;

typedef struct {
    uint32_t magic;
    uint16_t version, bpm10;
    uint8_t swing, pattern, palette, root, scale, quantize;
    int8_t transpose;                         /* every melodic track's steps transposed */
    uint8_t pad;
    /* DROP: its length (0..3: 1 2 4 8 bars), the snare roll in its last bar, the filter rising, the crash after */
    uint8_t drop_bars, drop_roll, drop_filter, drop_crash;
    /* MUTATIONS: how much, the fills every 4 / 8 / 16 bars, which tracks (0 all, 1 kick, 2 909, 3 break) */
    uint8_t mut_amount, mut_fills, mut_target;
    /* the click (0 off, 1..4 on at that level, 5..8 only while recording), the count-in (metro_vol: 0 none,
     * 1 1 beat, 2 2 beats, 3 1 bar, 4 2 bars); the clock (0 its own, 1 a MIDI clock's); MIDI clock out */
    uint8_t metro, metro_vol, sync, midi_out;
    /* RISER: its length (0..3: 1 2 4 8 bars), the music filtered up, a beat of silence before, the impact after */
    uint8_t riser_bars, riser_filter, riser_gap, riser_impact;
    /* the SONG: on (the patterns follow its chain), its chain: each place a pattern (TB_PATTERNS: the end) for some bars */
    uint8_t song_on;
    uint8_t rec_quant;                        /* REC's quantize: its strength (bits 0..2: 0 100 %, 1 75 %, 2 50 %, 3 25 %;
                                               * 4: as played, older projects), its grid (bits 3, 4: TB_REC_G*) */
    uint8_t song_pad[2];
    uint8_t song_pat[TB_SONG], song_bars[TB_SONG];
    tb_fx_set_t fx;
    tb_track_t trk[TB_TRACKS];
    tb_pattern_t pat[TB_PATTERNS];
    /* (version 7, after all the rest so a version 6 project loads into the start) each track's stacked effects, all
     * playing at once: FLANGER, PHASER, TREMOLO (TB_STK_*), each its three knobs and its mix (0: off); the stereo
     * chorus's speed and depth */
    tb_ins_set_t stk[TB_TRACKS][3];
    uint8_t stk_mix[TB_TRACKS][3];
    uint8_t cho_rate[TB_TRACKS], cho_depth[TB_TRACKS];
    /* (version 8) each track's effects' order: route_ord[t][place] = the effect (TB_FX_*); route_par: the effects
     * beside the line (bit e: effect e works on the clean sound, its change added at the end); route_send: 1 = the
     * reverb and the delay take the clean sound (beside the effects), 0 = the sound after them */
    uint8_t route_ord[TB_TRACKS][5], route_par[TB_TRACKS], route_send[TB_TRACKS];
    /* (version 9) the RISER: the sound its page shows (TB_RISER_*), each sound's level (0 off .. 100; all 0: the one
     * shown, all of it: older projects); room for what comes next (zeros) */
    uint8_t riser_type;
    uint8_t riser_lvl[6];
    /* (version 9, from its spare room: zeros are the defaults) each DROP kind's settings (TB_DROP_*), two bytes each
     * (low first), every field + 1 (0: the kind's own): bits 0..2 its length (1 2 4 8 bars), 3..5 its roll
     * (TB_DROLL_*), 6..7 its filter (TB_DFILT_*), 8..9 the kick (TB_DKICK_*), 10..12 its end (TB_DEND_*) */
    uint8_t drop_cfg[12];
    /* (version 12, from its spare room) the DROP's and the RISER's own sounds, each its level + 1 (0: 100 %, older
     * projects): TB_FXV_* (FX held: KNOB 1..4) */
    uint8_t fx_vol[4];
    /* (v25, from its spare room: zeros, no name - older projects) the project's name, up to 8 letters (0 ends it) */
    char pname[8];
    uint8_t spare[1];
} tb_project_t;
enum { TB_RISER_CLASSIC, TB_RISER_NOISE, TB_RISER_WIND, TB_RISER_SIREN, TB_RISER_SAW, TB_RISER_SNARE, TB_NRISER };
extern const char *const TB_RISER_NAME[TB_NRISER];
enum { TB_FX_DRIVE, TB_FX_FLANGER, TB_FX_PHASER, TB_FX_TREMOLO, TB_FX_EXTRA, TB_NFX };   /* the effects that can move */
enum { TB_STK_FLANGER, TB_STK_PHASER, TB_STK_TREMOLO, TB_NSTK };
/* the older projects' sizes: their steps 6 bytes (8: version 10), not 10 (tb_engine_widen_steps makes them room) */
/* (version 12, the FM-1: its melodies 128 steps, its patterns bigger; tb_engine_relayout) */
#define TB_RELAYOUT ((uint32_t)TB_PATTERNS * (uint32_t)(TB_NSTEP - TB_OLD_NSTEP) * (uint32_t)sizeof(tb_step_t))
#define TB_STEPS_GROWN ((uint32_t)(TB_PATTERNS * TB_OLD_NSTEP * 4u) + TB_RELAYOUT)
#define TB_V10_BYTES ((uint32_t)sizeof(tb_project_t) - TB_PATTERNS * TB_OLD_NSTEP * 2u - TB_RELAYOUT)
#define TB_V11_BYTES ((uint32_t)sizeof(tb_project_t) - TB_RELAYOUT)
#define TB_V6_BYTES ((uint32_t)offsetof(tb_project_t, stk) - TB_STEPS_GROWN)   /* a version 6 project's size */
#define TB_V7_BYTES ((uint32_t)offsetof(tb_project_t, route_ord) - TB_STEPS_GROWN)   /* a version 7's */
#define TB_V8_BYTES ((uint32_t)offsetof(tb_project_t, riser_type) - TB_STEPS_GROWN)  /* a version 8's */
#define TB_V9_BYTES ((uint32_t)sizeof(tb_project_t) - TB_STEPS_GROWN)               /* a version 9's */
#ifdef RFM2
#define TB_MAGIC 0x36465252u                  /* "RRF6": RAVE FM 2's six tracks (no older layout read) */
#else
#define TB_MAGIC 0x58425454u                  /* "TTBX" */
#endif
#define TB_VERSION 12

extern tb_project_t tb_proj;
/* the current pattern's steps and length of track t */
#define TB_STEP(t, s) (tb_proj.pat[tb_proj.pattern].step[TB_SOFF(t) + (s)])
#define TB_TSTEPS(P, t) ((P)->step + TB_SOFF(t))   /* pattern P's steps of track t */
#define TB_LEN(t) (tb_proj.pat[tb_proj.pattern].len[t])

extern const char *const TB_ARP_NAME[TB_ARP_NMODE];
extern const char *const TB_ARP_RATE_NAME[TB_ARP_NRATE];
extern const char *const TB_ARP_RHYTHM_NAME[TB_ARP_NRHYTHM];
extern const uint8_t TB_ARP_RHYTHM[TB_ARP_NRHYTHM];   /* 8 steps, bit i: step i plays */
extern const char *const TB_ACC_NAME[TB_NACC];
extern const char *const TB_SPEED_NAME[TB_NSPEED];
extern const char *const TB_LFO_WAVE_NAME[TB_NLFOW];
extern const char *const TB_LFO_SYNC_NAME[TB_LFO_NSYNC];
extern const char *const TB_DEST_NAME[TB_NDEST];
extern const char *const TB_SCALE_NAME[TB_NSCALE];
extern const uint16_t TB_SCALE_MASK[TB_NSCALE];       /* bit n: the scale holds the root + n semitones */
extern const char *const TB_CHORD_NAME[TB_NCHORD];
extern const char *const TB_VOICING_NAME[TB_NVOICING];

/* the live effects (FX held + the white keys): bit k, key k + 1 */
enum { TB_PF_ROLL4, TB_PF_ROLL8, TB_PF_ROLL16, TB_PF_ROLL32, TB_PF_TROLL8, TB_PF_TROLL16, TB_PF_TROLL32, TB_PF_BUILD,
       TB_PF_LPSWEEP, TB_PF_HPSWEEP, TB_PF_STUTTER, TB_PF_KILLBASS, TB_PF_ECHO, TB_PF_WASH, TB_PF_NOKICK, TB_PF_KICKONLY,
       TB_PF_STOP, TB_PF_KROLL, TB_PF_SROLL,   /* (RAVE FM 2, the black keys 7..9: everything stops; the kick, the
                                                * 909's snare rolling - the DROP's own drums, heard muted or not) */
       TB_NPF };
#define TB_DROP_BARS(i) (1u << ((i) & 3u))
extern const char *const TB_PF_NAME[TB_NPF];

void tb_engine_init(void);
void tb_engine_default_project(void);
void tb_engine_stack_defaults(void);   /* the stacked effects' and the chorus's starting values (all tracks) */
void tb_engine_route_defaults(void);   /* the effects' order: the pages' order, all in line, the sends after */
void tb_engine_upgrade(unsigned from); /* a project of an older version (6 .. 8) just loaded: to this version */
/* an older project (version < 11) as read in: its steps (6 bytes, 8 in version 10) made 10, the rest moved after */
void tb_engine_widen_steps(unsigned old_step);
void tb_engine_relayout(void);       /* a version 11 project's patterns to version 12's (the FM-1: melodies of 128) */
int tb_engine_fx_on(unsigned t, unsigned e);   /* whether track t's effect e (TB_FX_*) plays */
void tb_engine_default_track(unsigned t);
/* track t becomes another machine (EDIT held + a white key): its sound starts over, its steps stay */
void tb_engine_set_machine(unsigned t, unsigned machine);
/* the break of track t played in order, one slice a step, as pattern pt's steps (PRESETS on BREAK: its loop) */
void tb_engine_break_loop(unsigned pt, unsigned t);
/* the audio interrupt: n frames of stereo signed Q23 */
void tb_engine_render(int32_t *out, unsigned n);
void tb_engine_play(int on);
/* (audit 2) the whole project rewritten from the main loop (a load, a new one): the sound held silent meanwhile, so
 * that the audio interrupt never renders a project half read; tb_engine_hold(1) returns once no render is running */
void tb_engine_hold(int on);
/* (the board's) the FM-1's second core: answering; hand it fn(arg) (1: taken); wait for it until pct % of the block
 * (0 done, 1 late: still at it, untouched; -1 given up: stopped, never again); busy with a job (1), idle (0), given
 * up past 8 ms (-1, stopped) */
int tb_engine_mc_avail(void);
int tb_engine_mc_run(void (*fn)(uint32_t), uint32_t arg);
int tb_engine_mc_wait(unsigned pct);
int tb_engine_mc_busy(void);
extern uint32_t tb_c1_late_count;
/* (21 Nov 2026) the engine's time measured part by part on the FM-1 (TIMER4: the board's tb_engine_ticks, its
 * tb_engine_block_ticks the block's whole time, its tb_engine_c1_ticks the second core's time at its jobs, a running
 * total). tb_engine_prof: each part's busiest block and its mean since the last call, % of a block (0: not measured -
 * the PC's harness has no TIMER4) */
enum { TB_PROF_C0, TB_PROF_C1, TB_PROF_WAIT, TB_PROF_TRK, TB_PROF_SYN, TB_PROF_INS, TB_PROF_PHA, TB_PROF_CHO,
       TB_PROF_DLY, TB_PROF_REV, TB_PROF_CHN, TB_PROF_RIS, TB_PROF_MST, TB_PROF_N };
/* (CHN: the tracks' chains after their sound - inserts, filters, EQ, pan, sends; RIS: the DROP's drums, the RISER, the
 * impact, the click; MST: the master - its EQ, compressor, drive, fuzz, filter, limiter) */
uint32_t tb_engine_ticks(void);
uint32_t tb_engine_block_ticks(void);
uint32_t tb_engine_c1_ticks(void);
void tb_engine_prof(uint8_t *max_pct, uint8_t *avg_pct);
typedef struct { volatile uint32_t syn, dly, rev, trk, pad[12]; } tb_pf_job_t;   /* (a cache line of its own) */
extern tb_pf_job_t tb_pf_job;                       /* the jobs' running totals (on whichever core runs them) */
int tb_engine_playing(void);
/* a key: a note on (vel 1..127) or off (vel 0) on track t (the 909: the piece); through its chords and arpeggiator */
void tb_engine_note(unsigned t, unsigned note, unsigned vel);
/* all of track t's notes off (its sound changed under them) */
void tb_engine_panic(unsigned t);
void tb_engine_preview(unsigned t, unsigned note, unsigned vel, unsigned ms);   /* a note heard for ms (a preset, a step) */
/* where track t is: its step in its loop and how far into it (0..255) */
unsigned tb_engine_step(unsigned t, unsigned *frac);
/* the step the sequencer is about to play on track t should not play this time (it was just played by hand) */
void tb_engine_skip(unsigned t, unsigned step);
/* the pattern to go to at the end of the bar */
void tb_engine_queue_pattern(unsigned p);
int tb_engine_queued_pattern(void);
/* REC: the track whose arpeggiator's notes are written into its steps (-1: none) */
void tb_engine_rec(int track);
/* the live effects held (a mask of TB_PF_*), the track the "piste" ones act on and the 909 piece they roll */
void tb_engine_perform(uint32_t mask, unsigned track, unsigned piece);
/* DROP: the breakdown made for you: from the next bar the kick out for the DROP's bars, the filter rising, a snare
 * roll in the last one, the crash and the kick back on the downbeat (again while it runs: cancelled) */
void tb_engine_drop(void);
/* the DROP's kinds: CLASSIC (the GLO page's settings), KICK ROLL (the kick out, then rolling faster in the last bar),
 * SNARE (one bar, a snare roll all through it), LOW (the music sinking under a low-pass, the snare at the end), STOP
 * (the music on, its last beat stopped for a snare fill), LONG (4 bars, the snare from the third); all: the crash and
 * the kick back on the downbeat, their drums heard whatever the tracks' mutes */
enum { TB_DROP_CLASSIC, TB_DROP_KICKROLL, TB_DROP_SNARE, TB_DROP_LOW, TB_DROP_STOP, TB_DROP_LONG, TB_NDROP };
extern const char *const TB_DROP_NAME[TB_NDROP];
enum { TB_DROLL_OFF, TB_DROLL_SNARE, TB_DROLL_SNARE2, TB_DROLL_KICK, TB_DROLL_FILL, TB_DROLL_BOTH, TB_NDROLL };
enum { TB_DFILT_OFF, TB_DFILT_UP, TB_DFILT_DOWN, TB_NDFILT };
enum { TB_DKICK_OUT, TB_DKICK_IN, TB_NDKICK };
enum { TB_DEND_CRASH, TB_DEND_STOPCRASH, TB_DEND_STOP, TB_DEND_PLAIN, TB_NDEND };
enum { TB_DSET_LEN, TB_DSET_ROLL, TB_DSET_FILT, TB_DSET_KICK, TB_DSET_END, TB_NDSET };
/* the DROP's and the RISER's sounds' levels (tb_proj.fx_vol): the RISER's sound, the DROP's rolls (snare, kick), the
 * crash, the RISER's impact (its boom) */
enum { TB_FXV_RISER, TB_FXV_ROLL, TB_FXV_CRASH, TB_FXV_IMPACT, TB_NFXV };
#define TB_FXVOL(i) (tb_proj.fx_vol[i] ? tb_proj.fx_vol[i] - 1u : 25u)    /* 0..100 % (never set: 25 %) */
extern const char *const TB_DROLL_NAME[TB_NDROLL], *const TB_DFILT_NAME[TB_NDFILT], *const TB_DKICK_NAME[TB_NDKICK],
    *const TB_DEND_NAME[TB_NDEND];
/* a DROP kind's setting (TB_DSET_*; the length 0..3: 1 2 4 8 bars), its own if never set */
unsigned tb_drop_get(unsigned kind, unsigned what);
void tb_drop_set(unsigned kind, unsigned what, unsigned v);
void tb_engine_drop_kind(unsigned kind);
unsigned tb_engine_drop_kind_now(void);   /* the kind asked for (waiting for the bar), else the one running (or last run) */
/* the DROP now: 0 none, 1 waiting for the bar, 2 running; the bars left */
unsigned tb_engine_drop_state(unsigned *bars_left);
/* MUTATIONS on / off: the kick, the 909 and the break vary as they play (extra hits, ghost notes, slices moved,
 * fills every few bars), their steps untouched */
void tb_engine_mutate(int on);
int tb_engine_mutating(void);
/* samples since track t's last hit (the kick designer's playhead) */
uint32_t tb_engine_age(unsigned t);
/* RISER: from the next bar a riser over its bars (noise and a siren climbing), the music filtered up if asked, a
 * beat of silence at its end if asked, then the impact on the downbeat (again while it runs: cancelled) */
void tb_engine_riser(void);
unsigned tb_engine_riser_state(unsigned *bars_left);
/* NOTE REPEAT: track t's keys repeat at its arpeggiator's rate while held (-1: off); rec: the repeats recorded */
void tb_engine_repeat(int track, int rec);
/* the metronome heard now (the app decides: on, or while recording) */
void tb_engine_click(int on);
/* PLAY's count-in: the next start waits that many beats, a click on each */
void tb_engine_countin(unsigned beats);
int tb_engine_counting(void);
/* MIDI clock: pulses (24 a beat) since PLAY */
uint32_t tb_engine_clock24(void);
/* the SONG: the place playing in its chain, the bars it has played there */
unsigned tb_engine_song_pos(unsigned *bar);
/* the locks of pattern pt: the one of track t, step s, knob ref (-1 none); set (a free one taken; -1 full); one or all
 * (ref 255) of a step cleared; how many a step has */
int tb_lock_find(unsigned pt, unsigned t, unsigned s, unsigned ref);
int tb_lock_set(unsigned pt, unsigned t, unsigned s, unsigned ref, unsigned val);
void tb_lock_clear(unsigned pt, unsigned t, unsigned s, unsigned ref);
unsigned tb_lock_count(unsigned pt, unsigned t, unsigned s);
int tb_lock_ok(unsigned ref);
/* the key's notes through the song's scale (snapped to it when the scale is on) */
unsigned tb_scale_snap(unsigned note);
/* the notes a key plays on track t (its chord): how many, into out */
unsigned tb_chord_notes(unsigned t, unsigned note, uint8_t *out);
/* the state the screen draws (lanes, playheads, the beat; the selected track's envelope, LFO, arpeggiator) */
void tb_engine_view(tb_view_t *v, unsigned track);
unsigned tb_engine_cpu(void);
#endif
