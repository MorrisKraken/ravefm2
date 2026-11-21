/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the instrument (tb_engine.h).
 *
 * The clock runs in 32nds (ticks): a step is 2 ticks at normal speed, 4 at half, 1 at double. Everything a step
 * plays goes through one queue of timed events (sample-exact: a block is cut wherever an event, a tick or an
 * arpeggiator's beat falls): its swing (odd steps late), its repeats (ratchets), its note-offs (gates).
 * A note reaching a track goes: chord (the track's, in the song's scale) -> arpeggiator (if on) -> the machine. */
#include "tb_engine.h"
#include "tb_dsp.h"
#include "tb_env.h"
#include <string.h>

tb_project_t tb_proj;

const char *const TB_ARP_NAME[TB_ARP_NMODE] = {"OFF", "UP", "DOWN", "UP-DOWN", "DOWN-UP", "H&B", "RANDOM", "PLAYED",
                                               "CHORD", "CONVERGE", "DIVERGE"};
const char *const TB_ARP_RATE_NAME[TB_ARP_NRATE] = {"1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/8.", "1/16."};
static const uint8_t ARP_RATE_Q[TB_ARP_NRATE] = {48, 24, 12, 6, 32, 16, 8, 36, 18};   /* the beat, in twelfths of a step */
const char *const TB_ARP_RHYTHM_NAME[TB_ARP_NRHYTHM] = {"ALL", "1 IN 2", "GALLOP", "x.xx", "xx.x", "x..x", "xxx.",
                                                        "3-3-2", ".x.x", "x.x..x.x", "xx..xx..", "GABBER", "x...x...",
                                                        "TRIPLE", "SKIP", "RANDOM"};
const uint8_t TB_ARP_RHYTHM[TB_ARP_NRHYTHM] = {0xFF, 0x55, 0xDB, 0xDD, 0xBB, 0x99, 0x77, 0x49, 0xAA, 0xA5, 0x33,
                                               0x6D, 0x11, 0xB6, 0xED, 0x00};
const char *const TB_ACC_NAME[TB_NACC] = {"PLAYED", "FIXED", "BEAT", "RISE"};
const char *const TB_SPEED_NAME[TB_NSPEED] = {"x1/2", "x1", "x2"};
const char *const TB_LFO_WAVE_NAME[TB_NLFOW] = {"SINE", "TRIANGLE", "SQUARE", "RAMP +", "RAMP -", "RANDOM"};
const char *const TB_LFO_SYNC_NAME[TB_LFO_NSYNC] = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/16T"};
static const uint16_t LFO_SYNC_Q[TB_LFO_NSYNC] = {768, 384, 192, 96, 48, 24, 12, 32, 16, 8};   /* a cycle, in twelfths of a step */
const char *const TB_DEST_NAME[TB_NDEST] = {"VOLUME", "FILTER", "PITCH", "PAN", "CUTOFF"};
const char *const TB_SCALE_NAME[TB_NSCALE] = {"CHROMA", "MAJOR", "MINOR", "HARM MIN", "MELO MIN", "DORIAN",
                                              "PHRYGIAN", "LYDIAN", "MIXOLYD", "LOCRIAN", "PENTA MAJ", "PENTA MIN",
                                              "BLUES", "HUNGARIAN", "PHRYG DOM", "JAPANESE"};
const uint16_t TB_SCALE_MASK[TB_NSCALE] = {0xFFF, 0xAB5, 0x5AD, 0x9AD, 0xAAD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B,
                                           0x295, 0x4A9, 0x4E9, 0x9CD, 0x5B3, 0x4A3};
const char *const TB_CHORD_NAME[TB_NCHORD] = {"OFF", "AUTO", "AUTO 7", "AUTO 9", "MIN", "MAJ", "MIN7", "MAJ7", "7",
                                              "DIM", "AUG", "SUS2", "SUS4", "FIFTH", "OCTAVE"};
static const int8_t CHORD_IV[TB_NCHORD][4] = {
    {0, -1, -1, -1}, {0, -1, -1, -1}, {0, -1, -1, -1}, {0, -1, -1, -1},            /* (AUTO: from the scale) */
    {0, 3, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 4, 7, 10}, {0, 3, 6, -1}, {0, 4, 8, -1},
    {0, 2, 7, -1}, {0, 5, 7, -1}, {0, 7, 12, -1}, {0, 12, -1, -1}};
const char *const TB_VOICING_NAME[TB_NVOICING] = {"CLOSE", "OPEN", "WIDE"};
const char *const TB_PF_NAME[TB_NPF] = {"ROLL 1/4", "ROLL 1/8", "ROLL 1/16", "ROLL 1/32", "TRACK 1/8", "TRACK 1/16",
                                        "TRACK 1/32", "BUILD", "FILTER LP", "FILTER HP", "STUTTER", "KILL BASS",
                                        "ECHO OUT", "REVERB", "NO KICK", "KICK ONLY", "TAPE STOP", "KICK ROLL",
                                        "SNARE ROLL"};

typedef struct {
    tb_kick_t kick;
    tb909_t d909;
#ifdef RFM2
    tb_dx_t dx;                          /* (RAVE FM 2) the 909: DRUM-X's sounds (tb_drumx.h) */
#endif
    tb_brk_t brk;
    tb_syn_t syn;
    tb_acid_t acid;
    tb_env_t env;                        /* the kick's envelope (the others: one a voice, in the machine) */
    tb_envk_t envk;
} tb_voices_t;

static tb_voices_t V[TB_TRACKS];
static tb_filter_t F[TB_TRACKS], MF[2];
static tb_ins_t INS[TB_TRACKS][2];   /* the inserts' states */
#if TB_HAS_STACK
static tb_ins_t STK[TB_TRACKS][TB_NSTK];   /* the stacked effects' */
static int32_t stk_dry[TB_BLOCK];
#endif
static int32_t fx_cln[TB_BLOCK], fx_tmp[TB_BLOCK], fx_sum[TB_BLOCK];
static int32_t band_lo[TB_TRACKS], band_hi[TB_TRACKS];   /* the lanes' colours: each track's bass and treble */  /* each track's FILTRE; the master's (the sweeps, KILL BASS) */
static volatile uint8_t playing, play_req;
static volatile int8_t queued = -1, rec_track = -1;
static volatile uint32_t pf_mask;
static volatile uint8_t pf_track, pf_piece;
static uint32_t now;                     /* samples since power on */
static uint32_t tick_pos, gtick;         /* samples into the current tick, ticks since PLAY */
static uint8_t tstep[TB_TRACKS];         /* each track's step in its loop */
static int16_t skip_step[TB_TRACKS];
static uint32_t seed = 0x1234567u;
/* the SIDECHAIN: each kick (heard) starts it again: the other tracks pushed down at once (in 1 ms), back up over
 * about half a beat (a curve, the pump); each track by its own SIDECHAIN (duck) amount */
static uint32_t sc_age = 0xFFFFFFFFu;    /* samples since the last kick */
static int32_t sc_dip[TB_BLOCK];         /* this block: how far down (Q15, 32767: all the way) */

/* the arpeggiator: per track, the notes held (in the order played) and where it is */
typedef struct {
    uint8_t held[8], vel[8], n, down;    /* down: keys still down (LATCH: a new chord once all are up) */
    uint8_t idx, on, rstep;              /* the next note, the note sounding (+1), the rhythm's step */
    uint32_t pos, beat_no;               /* samples into its beat, beats since it started */
    uint8_t chord_on[4], nchord_on;      /* ACCORD: the notes sounding */
} arp_t;
static arp_t A[TB_TRACKS];

/* the LFOs */
typedef struct { uint32_t ph; int32_t v, sh; } lfo_t;
static lfo_t L[TB_TRACKS];

/* the live effects: the roll's start, the steps it repeats, the sweeps' positions */
static uint32_t roll_tick0, pf_was;
static uint8_t roll_step[TB_TRACKS], roll_active;
static int32_t sweep;                    /* 0..65536: how far the held sweep has gone */
static volatile uint8_t drop_req, mut_on;
/* the DROP's kind (TB_DROP_*): the one asked for, the one running */
static volatile uint8_t drop_kind_req;
static uint8_t drop_kind;
/* the DROP's and the RISER's own drums (a 909 and a kick of their own, straight to the master: heard whatever the
 * tracks' mutes, the crash its whole recording) */
static tb909_t FXD;
static int8_t fxk_track = -1;
static uint32_t fxk_gate, fxk_since;                   /* (v25) the roll's hit held a step; the time since its last hit */
static uint8_t trk_mt[TB_TRACKS];                      /* (v24) a track muted this block (its gains then 0) */
static int32_t go_l[TB_TRACKS], go_r[TB_TRACKS];       /* (v24) its gains as if it were not: the rolls through a muted kick */
static const uint8_t FXD_P[TB909_NPARAM] = {64, 64, 64, 64}, FXD_ENV[4] = {0, 127, 127, 127};
/* the crash's tail: the 909's recording rings ~1 s; after it, a noise of its colour (3 .. 11 kHz), each side its own,
 * in over 250 ms and out over ~4.5 s, so it rings on as a crash does */
static uint32_t crash_age = 0xFFFFFFFFu;
static int32_t ct_hp[2], ct_hx[2], ct_lp[2];
static uint32_t ct_seed[2] = {0x2468ACE1u, 0x13579BDFu};
static uint8_t drop_on;
static uint32_t bar_count, drop_end, drop_start;  /* bars since PLAY; where the DROP ends, began */
static uint32_t hit_at[TB_TRACKS];       /* when each track last struck (now) */
static uint32_t pf_at;                   /* samples since the effect started */
static uint32_t ptick;                   /* ticks since PLAY (never reset: the MIDI clock) */
static uint8_t song_pos;                 /* the SONG: the place playing, the bars it has played */
static uint32_t song_bar;
/* the parameter locks sounding: per track, those of the last step it played */
static uint8_t alk_n[TB_TRACKS], alk_ref[TB_TRACKS][TB_STEP_LOCKS], alk_val[TB_TRACKS][TB_STEP_LOCKS];
static uint8_t PE[TB_TRACKS][32];
/* each track's EQ: its bass (~250 Hz) and its top (~2.5 kHz) split off */
static int32_t eq_lo[TB_TRACKS], eq_hi[TB_TRACKS];
/* each track's stereo chorus: a line of 1024 samples read twice, its LFO moving the two reads opposite ways (the
 * Juno's: ~0.5 Hz, 2.5 .. 9 ms) */
#if TB_HAS_CHORUS
static int16_t cho[TB_TRACKS][1024];
static uint16_t cho_w[TB_TRACKS];
static uint32_t cho_ph[TB_TRACKS];
/* (v22) a b >> 16 exactly for a <= 65536 (b split into its top and low 16 bits) - no 64-bit product */
static inline uint32_t mulu_16(uint32_t a, uint32_t b) { return a * (b >> 16) + ((a * (b & 0xFFFFu)) >> 16); }
static int32_t cho_tap(unsigned t, uint32_t d8)
{
    uint32_t d = d8 >> 8, fr = d8 & 255u;
    int32_t a = cho[t][(cho_w[t] - d) & 1023u], b = cho[t][(cho_w[t] - d - 1u) & 1023u];
    return a + (((b - a) * (int32_t)fr) >> 8);
}
#endif
/* RISER */
static volatile uint8_t riser_req;
static uint8_t riser_on;
static uint32_t riser_start, riser_end, impact_at = 0x80000000u, riser_ph;
typedef struct { int32_t lo, bd, lo2, hp, env, sm[4]; uint32_t osc[6], ph, tick; } rs_t;   /* a riser sound's state */
static rs_t RS[TB_NRISER];
const char *const TB_RISER_NAME[TB_NRISER] = {"CLASSIC", "NOISE", "WIND", "SIREN", "SAW UP", "SNARE ROLL"};
/* the metronome */
static volatile uint8_t click_on;
static uint32_t click_at = 0x80000000u;
static uint8_t click_bar;
/* NOTE REPEAT */
static volatile int8_t rep_track = -1;
static volatile uint8_t rep_rec;
static struct { uint8_t note[4], vel[4], n, track; uint32_t pos; } RP;

/* the screen's view of the sound: each lane's loop as levels, its playhead, its level */
static uint8_t wave[TB_TRACKS][TB_WAVE_W], heat[TB_TRACKS][TB_WAVE_W];
static uint16_t wcol[TB_TRACKS], wpos[TB_TRACKS];
static uint8_t level[TB_TRACKS], active[TB_TRACKS], tone_now[TB_TRACKS];
static uint16_t hit_count[TB_TRACKS];               /* (RAVE FM 2) its notes so far: the screen sees every one */
volatile uint8_t tb_heavy;
static uint32_t cpu_busy, cpu_total;
unsigned tb_engine_cpu(void) { return cpu_total ? (unsigned)(cpu_busy * 100u / cpu_total) : 0; }

/* the keys, from the control task: a small single-producer queue */
typedef struct { uint8_t track, note, vel, hold; } key_t;   /* hold: a preview's length, 10 ms (0: a key) */
static key_t kq[64];
static volatile uint8_t kw, kr;
/* (audit 2) the slot written before the index that hands it over, read before the index that frees it: kq is not
 * volatile, so without a barrier the compiler may move those stores and loads across kw / kr, and the audio interrupt
 * (or the main loop) take a key half written */
#define KQ_BARRIER() __asm__ volatile("" ::: "memory")

void tb_engine_note(unsigned t, unsigned note, unsigned vel)
{
    uint8_t n = (uint8_t)((kw + 1u) & 63u);
    if (n == kr || t >= TB_TRACKS)
        return;
    kq[kw].track = (uint8_t)t, kq[kw].note = (uint8_t)note, kq[kw].vel = (uint8_t)vel, kq[kw].hold = 0;
    KQ_BARRIER();
    kw = n;
}
void tb_engine_preview(unsigned t, unsigned note, unsigned vel, unsigned ms)
{
    uint8_t n = (uint8_t)((kw + 1u) & 63u);
    if (n == kr || t >= TB_TRACKS || !vel)
        return;
    kq[kw].track = (uint8_t)t, kq[kw].note = (uint8_t)note, kq[kw].vel = (uint8_t)vel;
    kq[kw].hold = (uint8_t)(ms / 10u < 1u ? 1u : ms / 10u > 255u ? 255u : ms / 10u);
    KQ_BARRIER();
    kw = n;
}
void tb_engine_panic(unsigned t) { tb_engine_note(t, 255, 0); }
#ifdef RFM_HOST
/* (audit 2, the PC's tests: what still runs in the engine, outside the project) */
#include <stdio.h>
char *getenv(const char *);
void tb_engine_debug(void);
#endif
static volatile uint8_t held_req, rendering;
void tb_engine_hold(int on)
{
    held_req = (uint8_t)(on != 0);
    __asm__ volatile("" ::: "memory");
    while (on && rendering)                          /* (a render under way, cut into: let it end) */
        ;
}
void tb_engine_play(int on) { play_req = (uint8_t)(on ? 1 : 2); }
int tb_engine_playing(void) { return playing; }
void tb_engine_queue_pattern(unsigned p) { queued = (int8_t)(p < TB_PATTERNS ? (int)p : -1); }
int tb_engine_queued_pattern(void) { return queued; }
void tb_engine_rec(int track) { rec_track = (int8_t)(TB_HAS_REC && track >= 0 && track < TB_TRACKS ? track : -1); }
void tb_engine_perform(uint32_t mask, unsigned track, unsigned piece)
{
    pf_track = (uint8_t)(track < TB_TRACKS ? track : 0);
    pf_piece = (uint8_t)piece;
    pf_mask = TB_HAS_LIVEFX ? mask : 0u;
#ifdef RFM_HOST
    {                                                 /* (the PC tool's Y: live effects held from a session) */
        extern uint32_t rfm_pf_force;
        pf_mask |= rfm_pf_force;
    }
#endif
}
void tb_engine_drop(void) { tb_engine_drop_kind(TB_DROP_CLASSIC); }
void tb_engine_drop_kind(unsigned kind) { drop_kind_req = (uint8_t)(TB_HAS_DROPS && kind < TB_NDROP ? kind : 0u); drop_req = 1; }
unsigned tb_engine_drop_kind_now(void) { return drop_req ? drop_kind_req : drop_kind; }
const char *const TB_DROP_NAME[TB_NDROP] = {"DROP", "KICK ROLL", "SNARE BAR", "LOW DROP", "STOP", "LONG DROP"};
const char *const TB_DROLL_NAME[TB_NDROLL] = {"NONE", "SNARE", "SNARE x2", "KICK", "FILL", "KICK+SN"};
const char *const TB_DFILT_NAME[TB_NDFILT] = {"NONE", "RISING", "SINKING"};
const char *const TB_DKICK_NAME[TB_NDKICK] = {"OUT", "IN"};
const char *const TB_DEND_NAME[TB_NDEND] = {"CRASH", "CUT+CRASH", "CUT", "NONE"};   /* (CUT: its last beat silent) */
/* each kind as made (the DROP itself: its older GLO settings) */
static unsigned drop_own(unsigned kind, unsigned what)
{
    static const uint8_t OWN[TB_NDROP][TB_NDSET] = {
        {2, TB_DROLL_SNARE, TB_DFILT_UP, TB_DKICK_OUT, TB_DEND_CRASH},       /* (CLASSIC: below) */
        {1, TB_DROLL_KICK, TB_DFILT_UP, TB_DKICK_OUT, TB_DEND_CRASH},
        {0, TB_DROLL_SNARE, TB_DFILT_UP, TB_DKICK_OUT, TB_DEND_CRASH},
        {1, TB_DROLL_SNARE, TB_DFILT_DOWN, TB_DKICK_OUT, TB_DEND_CRASH},
        {1, TB_DROLL_FILL, TB_DFILT_OFF, TB_DKICK_IN, TB_DEND_STOPCRASH},
        {2, TB_DROLL_SNARE2, TB_DFILT_UP, TB_DKICK_OUT, TB_DEND_CRASH}};
    if (kind == TB_DROP_CLASSIC)
        switch (what) {
        case TB_DSET_LEN: return tb_proj.drop_bars & 3u;
        case TB_DSET_ROLL: return tb_proj.drop_roll ? TB_DROLL_SNARE : TB_DROLL_OFF;
        case TB_DSET_FILT: return tb_proj.drop_filter ? TB_DFILT_UP : TB_DFILT_OFF;
        case TB_DSET_END: return tb_proj.drop_crash ? TB_DEND_CRASH : TB_DEND_PLAIN;
        default: break;
        }
    return OWN[kind % TB_NDROP][what % TB_NDSET];
}
static const uint8_t DSET_SHIFT[TB_NDSET] = {0, 3, 6, 8, 10}, DSET_BITS[TB_NDSET] = {7, 7, 3, 3, 7};
unsigned tb_drop_get(unsigned kind, unsigned what)
{
    unsigned c, f;
    kind %= TB_NDROP, what %= TB_NDSET;
    c = tb_proj.drop_cfg[2u * kind] | (unsigned)tb_proj.drop_cfg[2u * kind + 1u] << 8;
    f = (c >> DSET_SHIFT[what]) & DSET_BITS[what];
    return f ? f - 1u : drop_own(kind, what);
}
void tb_drop_set(unsigned kind, unsigned what, unsigned v)
{
    unsigned c;
    kind %= TB_NDROP, what %= TB_NDSET;
    c = tb_proj.drop_cfg[2u * kind] | (unsigned)tb_proj.drop_cfg[2u * kind + 1u] << 8;
    c = (c & ~((unsigned)DSET_BITS[what] << DSET_SHIFT[what])) | (((v + 1u) & DSET_BITS[what]) << DSET_SHIFT[what]);
    tb_proj.drop_cfg[2u * kind] = (uint8_t)c, tb_proj.drop_cfg[2u * kind + 1u] = (uint8_t)(c >> 8);
}
/* the DROP running: its settings, read as it starts */
static uint8_t dc_roll, dc_filt, dc_kick, dc_end;
unsigned tb_engine_drop_state(unsigned *left)
{
    if (left)
        *left = drop_on && drop_end > bar_count ? drop_end - bar_count : 0;
    return drop_on ? 2u : drop_req ? 1u : 0u;
}
void tb_engine_mutate(int on) { mut_on = (uint8_t)(TB_HAS_MUTATE && on != 0); }
int tb_engine_mutating(void) { return mut_on; }
uint32_t tb_engine_age(unsigned t) { return t < TB_TRACKS ? now - hit_at[t] : 0; }
void tb_engine_riser(void) { riser_req = TB_HAS_RISER; }
unsigned tb_engine_riser_state(unsigned *left)
{
    if (left)
        *left = riser_on && riser_end > bar_count ? riser_end - bar_count : 0;
    return riser_on ? 2u : riser_req ? 1u : 0u;
}
void tb_engine_repeat(int track, int rec) { rep_rec = (uint8_t)(rec != 0); rep_track = (int8_t)(TB_HAS_ROLL && track >= 0 && track < TB_TRACKS ? track : -1); }
void tb_engine_click(int on) { click_on = (uint8_t)(TB_HAS_METRO && on != 0); }
/* the count-in: PLAY waits this many beats, a click on each (the first of each bar higher), then starts */
static volatile uint8_t ci_req;
static uint32_t ci_left, ci_pos;
void tb_engine_countin(unsigned beats) { ci_req = (uint8_t)(beats > 16u ? 16u : beats); }
int tb_engine_counting(void) { return ci_left != 0; }

/* ------------------------------------------------------------ parameter locks --- */
int tb_lock_ok(unsigned ref) { return ref < 32u || (ref >= TB_LK_VOL && ref <= TB_LK_DLY) || ref == TB_LK_PAN; }
int tb_lock_find(unsigned pt, unsigned t, unsigned s, unsigned ref)
{
    const tb_lock_t *L_ = tb_proj.pat[pt % TB_PATTERNS].lock;
    unsigned i;
    for (i = 0; i < TB_NLOCK; i++)
        if (L_[i].ref1 == ref + 1u && L_[i].track == t && L_[i].step == s)
            return (int)i;
    return -1;
}
unsigned tb_lock_count(unsigned pt, unsigned t, unsigned s)
{
    const tb_lock_t *L_ = tb_proj.pat[pt % TB_PATTERNS].lock;
    unsigned i, n = 0;
    for (i = 0; i < TB_NLOCK; i++)
        n += L_[i].ref1 && L_[i].track == t && L_[i].step == s;
    return n;
}
int tb_lock_set(unsigned pt, unsigned t, unsigned s, unsigned ref, unsigned val)
{
    tb_lock_t *L_ = tb_proj.pat[pt % TB_PATTERNS].lock;
    int i = tb_lock_find(pt, t, s, ref);
    if (!TB_HAS_PLOCK || !tb_lock_ok(ref))
        return -1;
    if (i < 0) {
        if (tb_lock_count(pt, t, s) >= TB_STEP_LOCKS)
            return -1;
        for (i = 0; i < TB_NLOCK && L_[i].ref1; i++)
            ;
        if (i >= TB_NLOCK)
            return -1;
    }
    L_[i].track = (uint8_t)t, L_[i].step = (uint8_t)s, L_[i].ref1 = (uint8_t)(ref + 1u), L_[i].val = (uint8_t)val;
    return i;
}
void tb_lock_clear(unsigned pt, unsigned t, unsigned s, unsigned ref)
{
    tb_lock_t *L_ = tb_proj.pat[pt % TB_PATTERNS].lock;
    unsigned i;
    for (i = 0; i < TB_NLOCK; i++)
        if (L_[i].ref1 && L_[i].track == t && (s == 255u || L_[i].step == s) && (ref == 255u || L_[i].ref1 == ref + 1u))
            L_[i].ref1 = 0;
}
/* the step's locks become track t's (a step with none: the knobs as they are) */
static void apply_locks(unsigned t, unsigned s)
{
    if (!TB_HAS_PLOCK)
        return;
    const tb_lock_t *L_ = tb_proj.pat[tb_proj.pattern].lock;
    unsigned i, n = 0;
    for (i = 0; i < TB_NLOCK && n < TB_STEP_LOCKS; i++)
        if (L_[i].ref1 && L_[i].track == t && L_[i].step == s)
            alk_ref[t][n] = (uint8_t)(L_[i].ref1 - 1u), alk_val[t][n] = L_[i].val, n++;
    alk_n[t] = (uint8_t)n;
}
/* track t's machine knobs as they sound now (its locks over them) */
static const uint8_t *prm(unsigned t)
{
    unsigned i;
    if (!alk_n[t])
        return tb_proj.trk[t].p;
    memcpy(PE[t], tb_proj.trk[t].p, 32);
    for (i = 0; i < alk_n[t]; i++)
        if (alk_ref[t][i] < 32u)
            PE[t][alk_ref[t][i]] = alk_val[t][i];
    return PE[t];
}
/* a channel's value as it sounds now */
static unsigned chv(unsigned t, unsigned ref, unsigned base)
{
    unsigned i;
    for (i = 0; i < alk_n[t]; i++)
        if (alk_ref[t][i] == ref)
            return alk_val[t][i];
    return base;
}

void tb_engine_skip(unsigned t, unsigned step)
{
    if (t < TB_TRACKS)
        skip_step[t] = (int16_t)step;
}

static uint32_t step_samples(void)       /* a sixteenth: 60 / bpm / 4 s */
{
    uint32_t bpm10 = tb_proj.bpm10 < 300 ? 300 : tb_proj.bpm10 > 3000 ? 3000 : tb_proj.bpm10;
    return (uint32_t)(TB_FS * 150u / bpm10);
}
static unsigned ticks_per_step(unsigned t) { static const uint8_t T[TB_NSPEED] = {4, 2, 1}; if (!TB_HAS_SPEED) return 2u; return T[tb_proj.trk[t].speed < TB_NSPEED ? tb_proj.trk[t].speed : 1]; }
static unsigned track_len(unsigned t) { unsigned l = TB_LEN(t); return l ? (l > TB_TCAP(t) ? TB_TCAP(t) : l) : 16u; }
static int melodic(unsigned t) { unsigned m = tb_proj.trk[t].machine; return m == TB_M_SYNTH || m == TB_M_BREAK || m == TB_M_SAMPLE || m == TB_M_ACID; }

/* ------------------------------------------------------------- scale and chords --- */
unsigned tb_scale_snap(unsigned note)
{
    unsigned m = TB_SCALE_MASK[tb_proj.scale < TB_NSCALE ? tb_proj.scale : 0], r = tb_proj.root % 12u, d;
    for (d = 0; d < 12u; d++) {                       /* the scale's note at or under it */
        unsigned n = note >= d ? note - d : 0u;
        if ((m >> ((n + 12u - r) % 12u)) & 1u)
            return n;
    }
    return note;
}

/* the k-th note of the scale above (or at) note (k = 0: note itself, snapped) */
static int scale_up(int note, unsigned k)
{
    unsigned m = TB_SCALE_MASK[tb_proj.scale < TB_NSCALE ? tb_proj.scale : 0], r = tb_proj.root % 12u;
    int n = (int)tb_scale_snap((unsigned)note);
    while (k) {
        n++;
        if ((m >> ((unsigned)(n + 120 - (int)r) % 12u)) & 1u)
            k--;
    }
    return n;
}

unsigned tb_chord_notes(unsigned t, unsigned note, uint8_t *out)
{
    const tb_track_t *tr = &tb_proj.trk[t];
    unsigned c = tr->chord < TB_NCHORD ? tr->chord : 0u, n = 0, i, inv;
    int x[4];
    if (!TB_HAS_SCALE || !c || tr->machine == TB_M_909 || tr->machine == TB_M_KICK || tr->machine == TB_M_ACID) {
        out[0] = (uint8_t)note;
        return 1;
    }
    if (c <= 3u) {                                    /* AUTO: stacked thirds of the song's scale */
        int base = (int)note;
        if (!tb_proj.scale) {                         /* (no scale: a minor triad, the hardcore's) */
            static const int8_t M[3][4] = {{0, 3, 7, -1}, {0, 3, 7, 10}, {0, 3, 7, 14}};
            for (i = 0; i < 4u && M[c - 1u][i] >= 0; i++)
                x[n++] = base + M[c - 1u][i];
        } else {
            x[n++] = scale_up(base, 0), x[n++] = scale_up(base, 2), x[n++] = scale_up(base, 4);
            if (c == 2u)
                x[n++] = scale_up(base, 6);
            else if (c == 3u)
                x[n++] = scale_up(base, 8);
        }
    } else
        for (i = 0; i < 4u && CHORD_IV[c][i] >= 0; i++)
            x[n++] = (int)note + CHORD_IV[c][i];
    /* the inversion: the lowest notes up an octave, one by one */
    for (inv = 0; inv < tr->inversion % 4u && n > 1u; inv++) {
        int lo = x[0];
        for (i = 0; i + 1u < n; i++)
            x[i] = x[i + 1u];
        x[n - 1u] = lo + 12;
    }
    if (tr->voicing == 1 && n >= 3u)                  /* OUVERT: the second highest down an octave */
        x[n - 2u] -= 12;
    else if (tr->voicing == 2 && n >= 3u)             /* LARGE: the middle up an octave */
        x[1] += 12;
    for (i = 0; i < n; i++)
        out[i] = (uint8_t)(x[i] < 0 ? 0 : x[i] > 127 ? 127 : x[i]);
    return n;
}

/* ------------------------------------------------------------------ the LFOs --- */
static int32_t lfo_pitch(unsigned t)           /* semitones Q16 */
{
    const tb_track_t *tr = &tb_proj.trk[t];
    int32_t d = tr->lfo_depth;
    if (!TB_HAS_LFO || tr->lfo_dest != TB_DEST_PITCH || !d)
        return 0;
    return (int32_t)(((int64_t)L[t].v * d * d / (127 * 127)) * 12 * 65536 / 32768);
}
static int32_t lfo_cutoff(unsigned t)          /* octaves Q16 (the synth's own filter) */
{
    const tb_track_t *tr = &tb_proj.trk[t];
    if (!TB_HAS_LFO || tr->lfo_dest != TB_DEST_CUTOFF || !tr->lfo_depth)
        return 0;
    return (int32_t)((int64_t)L[t].v * tr->lfo_depth * 4 * 65536 / (127 * 32768));
}

static void lfo_run(unsigned t, unsigned n)
{
    const tb_track_t *tr = &tb_proj.trk[t];
    lfo_t *l = &L[t];
    uint32_t inc, old = l->ph;
    int32_t v;
    if (tr->lfo_sync) {                               /* a cycle a number of steps */
        uint32_t cyc = step_samples() * LFO_SYNC_Q[tr->lfo_rate % TB_LFO_NSYNC] / 12u;
        inc = (uint32_t)(0x100000000ull / (cyc ? cyc : 1u));
    } else {                                          /* 0.05 .. 30 Hz */
        uint32_t mhz = (uint32_t)((50ull * tb_exp2((int32_t)tr->lfo_rate * 65536 / 14)) >> 16);
        inc = (uint32_t)((uint64_t)mhz * 97391u / 1000u);   /* mHz x 2^32 / 44100 / 1000 */
    }
    l->ph += inc * n;
    if (l->ph < old)                                  /* a new cycle: the next random step */
        l->sh = (int32_t)(tb_noise(&seed) >> 17) - 16384, l->sh *= 2;
    switch (tr->lfo_wave) {
    case TB_LFO_TRI: v = tb_tri(l->ph + 0x40000000u); break;
    case TB_LFO_SQUARE: v = l->ph < 0x80000000u ? 32767 : -32767; break;
    case TB_LFO_RAMPUP: v = (int32_t)(l->ph >> 16) - 32768; break;
    case TB_LFO_RAMPDN: v = 32767 - (int32_t)(l->ph >> 16); break;
    case TB_LFO_SH: v = l->sh; break;
    default: v = tb_sin(l->ph);
    }
    l->v += (v - l->v) / 2;                           /* (a little smoothing: the square and S&H without clicks) */
}

/* ------------------------------------------------------------------ the machines --- */
#if TB_HAS_SAMPLE
static void sample_region(const uint8_t *p, uint32_t *a, uint32_t *b)
{
    uint32_t len = tb_brk_len(p[TBB_SRC]);
    *a = (uint32_t)((uint64_t)len * p[TBB_START] / 128u);
    *b = *a + (uint32_t)((uint64_t)len * (p[10] + 1u) / 128u);
}
#endif

/* the machine itself: a note on (vel > 0) or off; iv/niv: the chord the synth plays as one (NULL: one note) */
static void voice(unsigned t, unsigned note, unsigned vel, const int8_t *iv, unsigned niv)
{
    tb_track_t *tr = &tb_proj.trk[t];
    const uint8_t *P = prm(t);
    if (vel && tr->lfo_retrig)
        L[t].ph = 0;
    switch (tr->machine) {
    case TB_M_KICK:
        if ((int)t == fxk_track)                      /* (v25) a KICK ROLL playing: it is the kick - the pattern's hits
                                                       *  (and their ends) left out, no two kicks at once */
            break;
        if (vel) {
            tb_kick_hit(&V[t].kick, P, (int)note - 60 + (lfo_pitch(t) >> 16), step_samples() * 4u, tb_noise(&seed));
            tb_env_hit(&V[t].env);
            if (!tr->muted)
                sc_age = 0;                           /* (the SIDECHAIN: from this kick) */
        } else
            tb_env_off(&V[t].env);
        break;
    case TB_M_909:
#ifdef RFM2
        if (vel)
            tb_dx_hit(&V[t].dx, &V[t].d909, P, note, vel);
#else
        vel ? tb909_hit(&V[t].d909, note, vel) : tb909_off(&V[t].d909, note);
#endif
        break;
    case TB_M_BREAK:
        vel ? tb_brk_on(&V[t].brk, P, &tr->man, note, vel) : tb_brk_off(&V[t].brk, P, note);
        break;
#if TB_HAS_SAMPLE
    case TB_M_SAMPLE:
        if (vel) {
            uint32_t a, b;
            sample_region(P, &a, &b);
            tb_brk_play(&V[t].brk, P, a, b, note, vel, (int)note - 60);
        } else
            tb_brk_off(&V[t].brk, P, note);
        break;
#endif
#if TB_HAS_SYNTH
    case TB_M_SYNTH:
        vel ? tb_syn_on(&V[t].syn, P, note, vel, iv, niv) : tb_syn_off(&V[t].syn, P, note);
        break;
#endif
#if TB_HAS_ACID
    case TB_M_ACID:
        vel ? tb_acid_on(&V[t].acid, P, note, vel) : tb_acid_off(&V[t].acid, note);
        break;
#endif
    default:
        break;
    }
    if (vel)
        active[t] = 8, hit_at[t] = now, hit_count[t]++;
}

/* ------------------------------------------------------------ the arpeggiator --- */
static uint32_t arp_beat(unsigned t)
{
    unsigned r = tb_proj.trk[t].arp_rate < TB_ARP_NRATE ? tb_proj.trk[t].arp_rate : 2u;
    return step_samples() * ARP_RATE_Q[r] / 12u;
}
static int arp_on(unsigned t) { return TB_HAS_ARP && melodic(t) && tb_proj.trk[t].arp != TB_ARP_OFF; }

static void arp_hold(unsigned t, unsigned note, unsigned vel)
{
    arp_t *a = &A[t];
    unsigned i, j;
    for (i = 0; i < a->n && a->held[i] != note; i++)
        ;
    if (vel) {
        if (tb_proj.trk[t].arp_latch && !a->down && a->n)
            a->n = 0;                                 /* LATCH: a new chord after all the keys were up */
        a->down++;
        if (i < a->n || a->n >= 8u)
            return;
        if (!a->n) {
            a->idx = 0, a->rstep = 0, a->beat_no = 0;
            if (!playing)
                a->pos = arp_beat(t);                 /* stopped: it starts at once */
        }
        a->held[a->n] = (uint8_t)note, a->vel[a->n] = (uint8_t)vel;
        a->n++;
    } else {
        if (a->down)
            a->down--;
        if (tb_proj.trk[t].arp_latch || i >= a->n)
            return;
        for (j = i; j + 1u < a->n; j++)
            a->held[j] = a->held[j + 1u], a->vel[j] = a->vel[j + 1u];
        a->n--;
    }
}

/* the arpeggiator's order for the notes held: a cycle of up to 16 notes */
static unsigned arp_order(unsigned t, uint8_t *seq)
{
    const tb_track_t *tr = &tb_proj.trk[t];
    const arp_t *a = &A[t];
    unsigned oct = tr->arp_oct ? (tr->arp_oct > 4u ? 4u : tr->arp_oct) : 1u, n = 0, i, k, m = 0;
    uint8_t s[32];
    if (!a->n)
        return 0;
    for (k = 0; k < oct; k++)                         /* the notes over the octaves, sorted or as played */
        for (i = 0; i < a->n && m < 32u; i++)
            s[m++] = (uint8_t)(a->held[i] + 12u * k > 127u ? 127u : a->held[i] + 12u * k);
    if (tr->arp != TB_ARP_PLAYED)
        for (i = 1; i < m; i++) {
            uint8_t x = s[i];
            unsigned j = i;
            while (j && s[j - 1u] > x)
                s[j] = s[j - 1u], j--;
            s[j] = x;
        }
    switch (tr->arp) {
    case TB_ARP_DOWN:
        for (i = 0; i < m && n < 16u; i++)
            seq[n++] = s[m - 1u - i];
        break;
    case TB_ARP_UPDOWN:
    case TB_ARP_DOWNUP:
        for (i = 0; i < m && n < 16u; i++)
            seq[n++] = s[tr->arp == TB_ARP_UPDOWN ? i : m - 1u - i];
        for (i = 1; i + 1u < m && n < 16u; i++)
            seq[n++] = s[tr->arp == TB_ARP_UPDOWN ? m - 1u - i : i];
        break;
    case TB_ARP_UPDOWN2:
        for (i = 0; i < m && n < 16u; i++)
            seq[n++] = s[i];
        for (i = 0; i < m && n < 16u; i++)
            seq[n++] = s[m - 1u - i];
        break;
    case TB_ARP_CONVERGE:
    case TB_ARP_DIVERGE: {
        unsigned lo = 0, hi = m - 1u;
        uint8_t c[32];
        unsigned nc = 0;
        while (lo <= hi && nc < 32u) {
            c[nc++] = s[lo];
            if (hi != lo)
                c[nc++] = s[hi];
            lo++;
            if (!hi)
                break;
            hi--;
        }
        for (i = 0; i < nc && n < 16u; i++)
            seq[n++] = c[tr->arp == TB_ARP_CONVERGE ? i : nc - 1u - i];
        break;
    }
    default:                                          /* HAUT, JOUE, HASARD (drawn as HAUT), ACCORD */
        for (i = 0; i < m && n < 16u; i++)
            seq[n++] = s[i];
    }
    return n;
}

static void rec_arp(unsigned t, unsigned note, unsigned vel, uint32_t len)
{
    unsigned L_ = track_len(t), s = tstep[t], g;
    uint32_t sl = step_samples() * ticks_per_step(t) / 2u, into = (gtick % ticks_per_step(t)) * (step_samples() / 2u) + tick_pos;
    tb_step_t *st;
    if (into * 2u >= sl) {
        s = (s + 1u) % L_;
        skip_step[t] = (int16_t)s;
    }
    st = &TB_STEP(t, s);
    g = (unsigned)((uint64_t)len * 8u / (sl ? sl : 1u));
    st->hits = 1, st->note = (uint8_t)note, st->vel = (uint8_t)vel;
    st->gate = (uint8_t)(g < 1u ? 1u : g > 255u ? 255u : g);
    st->flags = TB_DIRECT;
}

static void schedule(uint32_t at, unsigned t, unsigned note, unsigned vel);

static void arp_tick(unsigned t)
{
    arp_t *a = &A[t];
    tb_track_t *tr = &tb_proj.trk[t];
    uint32_t beat = arp_beat(t), len;
    uint8_t seq[16];
    unsigned n, i, rhythm, vel, play_it;
    if (a->on) {
        voice(t, a->on - 1u, 0, 0, 0);
        a->on = 0;
    }
    for (i = 0; i < a->nchord_on; i++)
        voice(t, a->chord_on[i], 0, 0, 0);
    a->nchord_on = 0;
    if (!a->n)
        return;
    rhythm = TB_ARP_RHYTHM[tr->arp_rhythm % TB_ARP_NRHYTHM];
    if (tr->arp_rhythm % TB_ARP_NRHYTHM == TB_ARP_NRHYTHM - 1u)
        rhythm = tb_noise(&seed) & 0xFFu;             /* HASARD: a new rhythm each time */
    play_it = (rhythm >> (a->rstep & 7u)) & 1u;
    a->rstep = (uint8_t)((a->rstep + 1u) & 7u);
    a->beat_no++;
    if (!play_it)
        return;
    n = arp_order(t, seq);
    switch (tr->arp_accent) {                         /* the velocity */
    case TB_ACC_FIXED: vel = 110; break;
    case TB_ACC_BEAT: vel = (a->beat_no - 1u) % 4u == 0 ? 127u : 80u; break;
    case TB_ACC_RISE: vel = 60u + ((a->idx % (n ? n : 1u)) * 67u) / (n ? n : 1u); break;
    default: vel = a->vel[0] ? a->vel[0] : 100u;
    }
    len = (uint32_t)((uint64_t)beat * (6u + tr->arp_gate * 94u / 127u) / 100u);
    if (tr->arp == TB_ARP_CHORD) {                    /* ACCORD: all of them, each beat */
        for (i = 0; i < a->n && i < 4u; i++) {
            voice(t, a->held[i], vel, 0, 0);
            a->chord_on[a->nchord_on++] = a->held[i];
            if (rec_track == (int8_t)t && playing && !i)
                rec_arp(t, a->held[i], vel, len);
        }
    } else {
        unsigned nn = tr->arp == TB_ARP_RANDOM ? seq[tb_noise(&seed) % n] : seq[a->idx % n];
        a->idx++;
        voice(t, nn, vel, 0, 0);
        a->on = (uint8_t)(nn + 1u);
        if (rec_track == (int8_t)t && playing)
            rec_arp(t, nn, vel, len);
    }
    schedule(now + len, t, 254, 0);                   /* its gate: 5 .. 100 % of the beat */
}

/* ------------------------------------------------------------------ the events --- */
typedef struct { uint32_t at; uint8_t t, note, vel, flags; } ev_t;
#define EV_SEQ 0x08u                     /* a step's note: played as written (one note: one note; not the track's chord) */
#define EV_LETGO 0x10u                   /* a preview's end: the break let go (FULL takes no note-off) */
#define EV_REPEAT 0x40u                  /* a note repeat's: past the arpeggiator */
#define EV_HAND 0x80u                    /* a chord set by hand on the step: its notes in hand_iv[t] (not the track's chord) */
static int8_t hand_iv[TB_TRACKS][4];
static uint8_t hand_n[TB_TRACKS];
#define NEV 256
static ev_t ev[NEV];
static unsigned nev;

static unsigned ev_lost, ev_peak;        /* (the checks: note-ons that found no room, the most events at once) */
#ifdef TB_TEST
unsigned tb_engine_test_events(unsigned *peak) { if (peak) *peak = ev_peak; return ev_lost; }
#endif
static void note(unsigned t, unsigned n, unsigned vel, unsigned flags);
/* an event kept for its time. The list full: a note-on is not played; a note-off is never lost (a note would hang):
 * it takes the place of the note-on furthest ahead, or (none) lets go now */
static void schedule_f(uint32_t at, unsigned t, unsigned note_, unsigned vel, unsigned flags)
{
    if (nev + 1u > ev_peak)
        ev_peak = nev + 1u;
    if (nev >= NEV) {
        unsigned i;
        int far = -1;
        ev_lost++;
        if (vel)
            return;
        for (i = 0; i < nev; i++)
            if (ev[i].vel && (far < 0 || (int32_t)(ev[i].at - ev[far].at) > 0))
                far = (int)i;
        if (far < 0) {
            note(t, note_, 0, flags);
            return;
        }
        ev[far].at = at, ev[far].t = (uint8_t)t, ev[far].note = (uint8_t)note_, ev[far].vel = 0, ev[far].flags = (uint8_t)flags;
        return;
    }
    ev[nev].at = at, ev[nev].t = (uint8_t)t, ev[nev].note = (uint8_t)note_, ev[nev].vel = (uint8_t)vel;
    ev[nev].flags = (uint8_t)flags;
    nev++;
}
static void schedule(uint32_t at, unsigned t, unsigned note, unsigned vel) { schedule_f(at, t, note, vel, 0); }

/* a note reaching a track: its chord, then its arpeggiator, or the machine */
static void note(unsigned t, unsigned n, unsigned vel, unsigned flags)
{
    uint8_t cn[4];
    unsigned k, nc;
    if (n == 254) {                                   /* the end of an arpeggiator's note */
        arp_t *a = &A[t];
        if (a->on)
            voice(t, a->on - 1u, 0, 0, 0), a->on = 0;
        for (k = 0; k < a->nchord_on; k++)
            voice(t, a->chord_on[k], 0, 0, 0);
        a->nchord_on = 0;
        return;
    }
    if ((flags & EV_LETGO) && !vel && (tb_proj.trk[t].machine == TB_M_BREAK || tb_proj.trk[t].machine == TB_M_SAMPLE)) {
        tb_brk_release(&V[t].brk, n);
        return;
    }
    if (flags & TB_DIRECT) {
        voice(t, n, vel, 0, 0);
        return;
    }
    if (flags & EV_HAND) {                            /* a chord set by hand: its notes, not the track's chord */
        nc = hand_n[t] > 4u ? 4u : hand_n[t];
        for (k = 0; k < nc; k++) {
            int m = (int)n + hand_iv[t][k];
            cn[k] = (uint8_t)(m < 0 ? 0 : m > 127 ? 127 : m);
        }
    } else if (flags & EV_SEQ)                        /* a step: as written, the chord of SCL not added */
        cn[0] = (uint8_t)n, nc = 1;
    else
        nc = tb_chord_notes(t, n, cn);
    if (arp_on(t) && !(flags & EV_REPEAT)) {
        for (k = 0; k < nc; k++)
            arp_hold(t, cn[k], vel);
        return;
    }
    if (tb_proj.trk[t].machine == TB_M_SYNTH) {       /* the synth: the chord as one (its voices, MONO or not) */
        int8_t iv[4];
        for (k = 0; k < nc; k++)
            iv[k] = (int8_t)((int)cn[k] - (int)n);
        voice(t, n, vel, iv, nc);
        return;
    }
    for (k = 0; k < nc; k++)
        voice(t, cn[k], vel, 0, 0);
}

static void fire_due(void)
{
    unsigned i, pass;
    for (pass = 0; pass < 2u; pass++)                 /* the offs first, then the ons */
        for (i = 0; i < nev;) {
            if ((int32_t)(ev[i].at - now) <= 0 && (pass ? ev[i].vel != 0 : ev[i].vel == 0)) {
                ev_t e = ev[i];
                ev[i] = ev[--nev];
                note(e.t, e.note, e.vel, e.flags);
            } else
                i++;
        }
}

/* ------------------------------------------------------------------- the steps --- */
static void play_step_as(unsigned t, const tb_step_t *st, unsigned s, uint32_t sl, unsigned vel_override, uint32_t delay)
{
    tb_track_t *tr = &tb_proj.trk[t];
    unsigned vel = vel_override ? vel_override : st->vel ? st->vel : 100u, r, nr, b;
    if (tr->machine == TB_M_ACID)                     /* the 303: an accent is its loudest, the others under it */
        vel = (st->flags & TB_ACCENT) ? 127u : vel >= TBA_ACCENT_VEL ? TBA_ACCENT_VEL - 1u : vel;
    if (TB_HAS_CHANCE && TB_PROB(st->flags) && (tb_noise(&seed) & 7u) < TB_PROB(st->flags))
        return;
    if (TB_HAS_SWING && (s & 1u) && tb_proj.swing)    /* swing: the odd steps late, up to a third of a step */
        delay += (uint32_t)((uint64_t)sl * tb_proj.swing / 381u);
    nr = TB_HAS_RATCHET ? TB_RATCH(st->flags) : 1u;
    if (tr->machine != TB_M_909) {                    /* (a 32nd: half a step later, or the step and its half) */
        if (TB_HALF_ONLY(st->hits))
            delay += sl / 2u;
        else if (TB_HALF_TOO(st->hits) && nr == 1u)
            nr = 2u;
    }
    for (r = 0; r < nr; r++) {
        uint32_t at = now + delay + sl * r / nr;
        unsigned g = st->gate ? st->gate : 6u;
        uint32_t len = nr > 1u ? sl / nr / 2u : (uint32_t)((uint64_t)sl * g / 8u);
        if (len < 48u)
            len = 48u;
        if (tr->machine == TB_M_ACID && (st->flags & TB_SLIDE) && nr == 1u)
            len = sl + sl / 4u;                       /* SLIDE: into the next step's note */
        if (tr->machine == TB_M_909) {
            for (b = 0; b < TB909_PIECES; b++)
                if (TB_HITS(st->hits) & (1u << b)) {
                    schedule(at, t, b, vel);
                    schedule(at + len - 16u, t, b, 0);
                }
        } else {
            int nn = st->note ? st->note : 60;
            unsigned n;
            if (tr->machine != TB_M_BREAK)               /* TRANSPO: the track's and the song's */
                nn += tr->transpose + (tr->machine != TB_M_KICK ? tb_proj.transpose : 0);
            n = (unsigned)(nn < 0 ? 0 : nn > 127 ? 127 : nn);
            if (st->more[0] && tr->machine != TB_M_KICK && tr->machine != TB_M_ACID) {   /* a chord set by hand */
                unsigned k, nh = 1;
                hand_iv[t][0] = 0;
                for (k = 0; k < 3u; k++)
                    if (st->more[k])
                        hand_iv[t][nh++] = (int8_t)((int)st->more[k] - (int)(st->note ? st->note : 60));
                hand_n[t] = (uint8_t)nh;
                schedule_f(at, t, n, vel, EV_HAND);
                if (tr->machine == TB_M_SYNTH)        /* (the synth: its voices let go with the root) */
                    schedule_f(at + len - 16u, t, n, 0, EV_HAND);
                else
                    for (k = 0; k < nh; k++) {
                        int m = (int)n + hand_iv[t][k];
                        schedule_f(at + len - 16u, t, (unsigned)(m < 0 ? 0 : m > 127 ? 127 : m), 0, TB_DIRECT);
                    }
            } else {
                schedule_f(at, t, n, vel, (st->flags & TB_DIRECT) | EV_SEQ);
                schedule_f(at + len - 16u, t, n, 0, (st->flags & TB_DIRECT) | EV_SEQ);
            }
        }
    }
}

/* MUTATIONS: the step as it plays this time (its own steps untouched); 0: nothing to play */
#if TB_HAS_MUTATE
static int mutate(unsigned t, unsigned s, tb_step_t *st)
{
    const tb_track_t *tr = &tb_proj.trk[t];
    unsigned m = tr->machine, tg = tb_proj.mut_target, a = tb_proj.mut_amount, pos = (gtick % 32u) / 2u;
    unsigned fills = 4u << (tb_proj.mut_fills % 3u), fill = (bar_count % fills) == fills - 1u && pos >= 12u;
    uint32_t r = tb_noise(&seed);
    unsigned p = r & 127u;
    if (!mut_on || !a || (tg == 1 && m != TB_M_KICK) || (tg == 2 && m != TB_M_909) || (tg == 3 && m != TB_M_BREAK) ||
        (m != TB_M_KICK && m != TB_M_909 && m != TB_M_BREAK))
        return st->hits != 0;
    switch (m) {
    case TB_M_KICK:
        if (fill && pos >= 14u && p < a)                     /* the fill: the last beat's kicks rolled (gabber) */
            st->hits = 1, st->flags = TB_FLAGS(0, pos == 15u ? 4u : 2u), st->vel = 110;
        else if (!st->hits && pos % 4u == 3u && p < a / 4u)  /* a kick pushed before the beat */
            st->hits = 1, st->note = 60, st->vel = 90, st->gate = 4;
        break;
    case TB_M_909:
        if (fill && p < a) {                                 /* the fill: a snare roll, or the toms down */
            if ((r >> 8) & 1u)
                st->hits |= (uint16_t)(1u << TB909_SD), st->flags = TB_FLAGS(0, pos >= 14u ? 4u : 2u);
            else
                st->hits |= (uint16_t)(1u << (pos <= 12u ? TB909_HT : pos <= 13u ? TB909_MT : TB909_LT));
            st->vel = (uint8_t)(80u + pos * 3u);
        } else if (p < a / 5u)                               /* ghost hats */
            st->hits |= (uint16_t)(1u << ((r >> 9) % 5u ? TB909_CH : TB909_OH)), st->vel = 55;
        break;
    case TB_M_BREAK:
        if (!st->hits)
            break;
        if (fill && p < a)                                   /* the fill: the slice stuttered */
            st->flags = TB_FLAGS(0, pos >= 14u ? 4u : 2u);
        else if (pos % 4u && p < a / 3u)                     /* a slice moved (the downbeats stay) */
            st->note = (uint8_t)(60u + (r >> 10) % 32u);
        else if (p < a / 8u)
            st->flags = TB_FLAGS(0, 2u);
        break;
    default:
        break;
    }
    (void)s;
    return st->hits != 0;
}
#else
static int mutate(unsigned t, unsigned s, tb_step_t *st) { (void)t, (void)s; return st->hits != 0; }
#endif

/* a step at its tick: late ones delayed; early ones (played before the beat) were started at the step before */
static void play_track_step(unsigned t, unsigned s)
{
    tb_step_t st = TB_STEP(t, s);
    uint32_t sl = step_samples() * ticks_per_step(t) / 2u;
    unsigned nx = (s + 1u) % track_len(t);
    int m = TB_MICRO(st.hits);
    if (skip_step[t] == (int16_t)s)
        skip_step[t] = -1;
    else if (m >= 0 && mutate(t, s, &st)) {
        apply_locks(t, s);
        play_step_as(t, &st, s, sl, 0, (uint32_t)m * sl / 32u);
    }
    st = TB_STEP(t, nx);                              /* the next one, if early: from here */
    m = TB_MICRO(st.hits);
    if (m < 0 && TB_HITS(st.hits) && skip_step[t] != (int16_t)nx) {
        apply_locks(t, nx);
        play_step_as(t, &st, nx, sl, 0, sl - (uint32_t)(-m) * sl / 32u);
    }
}

static int first_of(unsigned machine)
{
    unsigned t;
    for (t = 0; t < TB_TRACKS; t++)
        if (tb_proj.trk[t].machine == machine)
            return (int)t;
    return -1;
}

/* the DROP's own hits: a 909 piece (no note off: the crash its whole recording), a kick (the first kick track's sound) */
static void fx_hit(unsigned piece, unsigned vel)
{
    vel = vel * TB_FXVOL(piece == TB909_CR ? TB_FXV_CRASH : TB_FXV_ROLL) / 100u;   /* (FX held: KNOB 2, 3) */
    if (!vel)
        return;
    tb909_hit(&FXD, piece, vel);
    if (piece == TB909_CR)
        crash_age = 0;
}
/* the crash's tail's level at a (samples after the hit), Q15: up over 250 ms, then down, 0.8 s a time constant */
static uint32_t ct_q, ct_r;                    /* (21 Nov 2026) crash_age x 65536 / 24451: quotient, remainder, carried */
static int32_t crash_tail_env(uint32_t a)
{
    int32_t up = a >= 11025u ? 32767 : (int32_t)(a * 32767u / 11025u);
    uint32_t dn;
    if (!a)
        ct_q = ct_r = 0;
    else if (ct_r += 16634u, ct_q += 2u, ct_r >= 24451u)   /* (65536 = 2 x 24451 + 16634: a sample on, no division) */
        ct_r -= 24451u, ct_q++;
    dn = tb_exp2(-(int32_t)ct_q);                     /* e^(-a / 0.8 s), Q16 */
    return (int32_t)(((int64_t)up * dn) >> 16);
}
static void fx_kick(void)
{
    int k = first_of(TB_M_KICK);
    if (k < 0)
        return;
    fxk_track = (int8_t)k;                            /* (v25) the kick track's own voice: the roll replaces its kick */
    tb_kick_hit(&V[k].kick, prm((unsigned)k), lfo_pitch((unsigned)k) >> 16, step_samples() * 4u, tb_noise(&seed));
    tb_env_hit(&V[k].env), fxk_gate = step_samples(), fxk_since = 0;
    sc_age = 0;                                       /* (the SIDECHAIN pumps on the roll's kicks too) */
}
/* how many bars a DROP of this kind lasts */
static uint32_t drop_bars_of(unsigned kind) { return TB_DROP_BARS(tb_drop_get(kind, TB_DSET_LEN)); }
/* the accelerating roll over a bar: 1/8 for half of it, 1/16 for a quarter, then 1/32 */
static int roll_hit(unsigned t32) { unsigned every = t32 < 16u ? 4u : t32 < 24u ? 2u : 1u; return t32 % every == 0; }
/* the DROP, at every tick: its start at the bar, its rolls, the crash and the end */
static void drop_tick(void)
{
    unsigned t32 = gtick % 32u;
    int last = 0, before = 0;
    if (t32 == 0) {
        if (drop_req && !drop_on) {
            drop_on = 1, drop_kind = drop_kind_req, drop_start = bar_count, drop_end = bar_count + drop_bars_of(drop_kind);
            dc_roll = (uint8_t)tb_drop_get(drop_kind, TB_DSET_ROLL), dc_filt = (uint8_t)tb_drop_get(drop_kind, TB_DSET_FILT);
            dc_kick = (uint8_t)tb_drop_get(drop_kind, TB_DSET_KICK), dc_end = (uint8_t)tb_drop_get(drop_kind, TB_DSET_END);
        } else if (drop_req && drop_on)
            drop_on = 0;                                     /* (asked again: cancelled) */
        drop_req = 0;
        if (drop_on && bar_count >= drop_end) {              /* the end: the crash and the kick, on the one */
            drop_on = 0;
            if (dc_end == TB_DEND_CRASH || dc_end == TB_DEND_STOPCRASH)
                fx_hit(TB909_CR, 127);
        }
    }
    if (!drop_on)
        return;
    last = bar_count == drop_end - 1u, before = drop_end - drop_start >= 2u && bar_count == drop_end - 2u;
    switch (dc_roll) {
    case TB_DROLL_SNARE:                                     /* the last bar: the snare, faster and louder */
        if (last && roll_hit(t32))
            fx_hit(TB909_SD, 60u + t32 * 2u);
        break;
    case TB_DROLL_SNARE2:                                    /* two bars: eighths, then faster to the end */
        if (before && t32 % 4u == 0)
            fx_hit(TB909_SD, 50u + t32);
        else if (last && roll_hit(t32))
            fx_hit(TB909_SD, 80u + t32 * 3u / 2u);
        break;
    case TB_DROLL_KICK:                                      /* the last bar: the kick itself rolling faster */
        if (last && roll_hit(t32))
            fx_kick();
        break;
    case TB_DROLL_BOTH:
        if (last && roll_hit(t32))
            fx_kick(), fx_hit(TB909_SD, 60u + t32 * 2u);
        break;
    case TB_DROLL_FILL:                                      /* the last beat: a snare fill */
        if (last && t32 >= 24u)
            fx_hit(TB909_SD, 70u + (t32 - 24u) * 7u);
        break;
    default: break;
    }
}

static int pf(unsigned k) { return TB_HAS_LIVEFX && ((pf_mask >> k) & 1u); }

/* a roll: the steps under the playhead when it began, again and again (all tracks, or the "piste" one: on the
 * 909 its piece, on another track the step or, if empty, its last note) */
static int rolling(unsigned *ticks, int *one_track, unsigned *vel)
{
    static const uint8_t R[7] = {8, 4, 2, 1, 4, 2, 1};
    unsigned k;
    *vel = 0;
    for (k = 0; k < 7u; k++)
        if (pf(k)) {
            *ticks = R[k];
            *one_track = k >= 4u;
            return 1;
        }
    if (pf(TB_PF_BUILD)) {                            /* BUILD: 1/8 for half a bar, 1/16, then 1/32; louder */
        uint32_t el = gtick - roll_tick0;
        *ticks = el < 16u ? 4u : el < 24u ? 2u : 1u;
        *one_track = 1;
        *vel = 60u + (el > 32u ? 67u : el * 67u / 32u);
        return 1;
    }
    return 0;
}

unsigned tb_engine_song_pos(unsigned *bar)
{
    if (bar)
        *bar = song_bar;
    return song_pos;
}
static unsigned song_bars(unsigned i) { return tb_proj.song_bars[i] ? tb_proj.song_bars[i] : 1u; }

static void do_tick(void)
{
    unsigned t, rt = 0, rv = 0;
    int one = 0, roll = rolling(&rt, &one, &rv);
    if (TB_HAS_SONG && gtick % 32u == 0 && tb_proj.song_on && playing) {   /* the SONG: its place's bars played, the next place */
        if (++song_bar >= song_bars(song_pos)) {
            unsigned nx = song_pos + 1u;
            if (nx >= TB_SONG || tb_proj.song_pat[nx] >= TB_PATTERNS)
                nx = 0;
            song_pos = (uint8_t)nx, song_bar = 0;
            if (tb_proj.song_pat[nx] < TB_PATTERNS)
                queued = (int8_t)tb_proj.song_pat[nx];
        }
    }
    if (gtick % 32u == 0 && queued >= 0) {            /* a new pattern: at the bar */
        tb_proj.pattern = (uint8_t)queued;
        queued = -1;
        for (t = 0; t < TB_TRACKS; t++)
            tstep[t] = 0;
        gtick = 0;
    }
    if (gtick % 32u == 0) {
        bar_count++;
        for (t = 0; t < TB_TRACKS; t++) {            /* (RAVE FM 2) a break turning as a record (DISC): on its bars, from its start */
            tb_track_t *tr = &tb_proj.trk[t];
            if (tr->machine == TB_M_BREAK && tr->p[TBB_MODE] == TBB_DISC && !tr->muted && playing &&
                bar_count % tb_brk_bars(tr->p[TBB_SRC]) == 0u)
                voice(t, 60u, 110u, 0, 0);
        }
    }
    drop_tick();
    {                                                 /* (RAVE FM 2) the live KICK ROLL and SNARE ROLL, held: the DROP's
                                                       * own kick and 909 (heard whatever is muted), from where they began:
                                                       * 1/16; the snare 1/16 for a bar, then 1/32, climbing */
        static uint32_t k0, s0;
        static uint8_t kon, son;
        if (pf(TB_PF_KROLL)) {
            if (!kon)
                kon = 1, k0 = gtick;
            if ((gtick - k0) % 2u == 0)
                fx_kick();
        } else
            kon = 0;
        if (pf(TB_PF_SROLL)) {
            uint32_t el;
            if (!son)
                son = 1, s0 = gtick;
            el = gtick - s0;
            if (el % (el < 32u ? 2u : 1u) == 0)
                fx_hit(TB909_SD, 70u + (el > 48u ? 57u : el * 57u / 48u));
        } else
            son = 0;
    }
    if (gtick % 32u == 0) {                           /* the RISER: its start at the bar, its end and the impact */
        if (TB_HAS_RISER && riser_req && !riser_on) {
            riser_on = 1, riser_start = bar_count, riser_end = bar_count + TB_DROP_BARS(tb_proj.riser_bars), riser_ph = 0;
            {
                unsigned k;
                memset(RS, 0, sizeof RS);
                for (k = 0; k < TB_NRISER; k++)
                    RS[k].tick = 0xFFFFFFFFu;
            }
        } else if (riser_req && riser_on)
            riser_on = 0;
        riser_req = 0;
        if (riser_on && bar_count >= riser_end) {
            riser_on = 0;
            if (tb_proj.riser_impact) {                /* the impact: its boom, and the 909's crash, all of it */
                impact_at = now;
                fx_hit(TB909_CR, 127);
            }
        }
    }
    if (click_on && gtick % 8u == 0)                  /* the metronome: each beat, the bar's first higher */
        click_at = now, click_bar = gtick % 32u == 0;
    if (roll && !roll_active) {                       /* the roll begins: the steps it repeats */
        roll_active = 1, roll_tick0 = gtick;
        for (t = 0; t < TB_TRACKS; t++)
            roll_step[t] = tstep[t];
    } else if (!roll)
        roll_active = 0;
    for (t = 0; t < TB_TRACKS; t++) {
        unsigned tps = ticks_per_step(t);
        int in_roll = roll && (!one || t == pf_track);
        if (gtick % tps == 0)
            tstep[t] = (uint8_t)((gtick / tps) % track_len(t));
        if (in_roll) {
            if ((gtick - roll_tick0) % rt == 0) {
                tb_step_t st = TB_STEP(t, roll_step[t]);
                uint32_t sl = step_samples() * rt / 2u;
                if (one && tb_proj.trk[t].machine == TB_M_909)
                    st.hits = (uint16_t)(1u << pf_piece), st.flags = 0;
                else if (one && !st.hits)
                    st.hits = 1, st.note = 60, st.flags = 0;
                else if (st.hits && tb_proj.trk[t].machine != TB_M_909)
                    st.hits = 1;                      /* (the roll's own beat, not the step's 32nds) */
                st.flags &= (uint8_t)TB_DIRECT;       /* (a roll: no chance, no repeats, no slide of its own) */
                st.gate = 4;
                st.hits = TB_HITS(st.hits);
                play_step_as(t, &st, 0, sl, rv, 0);
            }
            continue;
        }
        if (gtick % tps == 0)
            play_track_step(t, tstep[t]);
    }
}

/* ------------------------------------------------------------------ rendering --- */
static uint8_t vm[TB_TRACKS];                         /* the machine each track's voices were made for (0: not yet) */
static void voices_init(unsigned t);
/* (21 Nov 2026) the synths' voices on the second core while this one renders the drums (the block's first part:
 * after it, the second core takes the buses, tb_engine_render); each its own tracks */
int tb_engine_mc_avail(void);
int tb_engine_mc_run(void (*fn)(uint32_t), uint32_t arg);
int tb_engine_mc_wait(unsigned pct);
int tb_engine_mc_busy(void);
/* (21 Nov 2026) the time of each part (tb_engine.h): this core's parts summed over the block, the jobs' running
 * totals (tb_pf_job: written only by the core that runs them, read here) - folded into the busiest and the sum at the
 * next block's start */
__attribute__((weak)) uint32_t tb_engine_ticks(void) { return 0; }
__attribute__((weak)) uint32_t tb_engine_block_ticks(void) { return 0; }
__attribute__((weak)) uint32_t tb_engine_c1_ticks(void) { return 0; }
tb_pf_job_t tb_pf_job __attribute__((aligned(64)));
static uint32_t pf0[TB_PROF_N];                       /* this block's, this core's (C0 WAIT TRK INS PHA CHO) */
static uint32_t pf_last[5];                           /* the running totals at the last fold: C1 SYN DLY REV */
static uint32_t pf_max[TB_PROF_N], pf_sum[TB_PROF_N], pf_blocks;
static volatile uint8_t pf_reset;
static void pf_fold(void)
{
    uint32_t now[5], v;
    unsigned k;
    now[0] = tb_engine_c1_ticks(), now[1] = tb_pf_job.syn, now[2] = tb_pf_job.dly, now[3] = tb_pf_job.rev;
    now[4] = tb_pf_job.trk;
    if (pf_reset) {
        memset(pf_max, 0, sizeof pf_max), memset(pf_sum, 0, sizeof pf_sum), pf_blocks = 0, pf_reset = 0;
    }
    pf0[TB_PROF_C1] = now[0] - pf_last[0], pf0[TB_PROF_SYN] += now[1] - pf_last[1];
    pf0[TB_PROF_DLY] = now[2] - pf_last[2], pf0[TB_PROF_REV] = now[3] - pf_last[3];
    pf0[TB_PROF_TRK] += now[4] - pf_last[4];
    memcpy(pf_last, now, sizeof now);
    for (k = 0; k < TB_PROF_N; k++) {
        v = pf0[k];
        if (v > pf_max[k])
            pf_max[k] = v;
        if (pf_sum[k] < 0x80000000u)
            pf_sum[k] += v;
        pf0[k] = 0;
    }
    pf_blocks++;
}
void tb_engine_prof(uint8_t *max_pct, uint8_t *avg_pct)
{
    uint32_t bt = tb_engine_block_ticks(), b = pf_blocks ? pf_blocks : 1u;
    unsigned k;
    for (k = 0; k < TB_PROF_N; k++) {
        uint32_t m = bt ? pf_max[k] / (bt / 100u ? bt / 100u : 1u) : 0u, a = bt ? pf_sum[k] / b / (bt / 100u ? bt / 100u : 1u) : 0u;
        max_pct[k] = (uint8_t)(m > 255u ? 255u : m);
        avg_pct[k] = (uint8_t)(a > 255u ? 255u : a);
    }
    pf_reset = 1;
}
#define PF_T() tb_engine_ticks()
/* (21 Nov 2026) the second core writes its own buffers: this core copies them only once it is done; late (past the
 * block's deadline) it is not stopped - its tracks stay silent this block, nothing more is handed to it until it has
 * finished (c1_late), nothing it works on is touched meanwhile */
#ifdef RFM_HOST
#define TB_LOWBUF
#else
#define TB_LOWBUF __attribute__((section(".lowbuf")))   /* (the pool is full: LOWBUF; never read before written) */
#endif
static int32_t syn_out[TB_TRACKS][TB_BLOCK] TB_LOWBUF;   /* (LOWBUF: the pool kept 8 KiB free; each track's
                                                         * written whole by syn_job before it is read) */
static unsigned syn_n;
static int32_t syn_lp[TB_TRACKS], syn_lc[TB_TRACKS];
static tb_syn_ctx_t syn_ctx[TB_TRACKS];                /* each synth's block, worked out here before the job */
static uint8_t syn_vm1[TB_TRACKS], syn_vm0[TB_TRACKS]; /* its voices: the second core's, this one's */
static uint8_t c1_late;                               /* a job still on the second core, past its block (1 synths, 2 buses) */
static uint8_t syn_owned;                             /* the synth tracks it holds while late: not rendered here */
uint32_t tb_c1_late_count;                            /* (blocks it was late: the HOME page's diagnostics) */
/* (21 Nov 2026) the two cores share the tracks: any track's sound (a synth's voices one by one, a kick, a 909, a
 * break, a sample, the ACID whole) can go to either, dealt each block by what each cost the block before (TIMER4,
 * whichever core made it), so the two finish together; the second core writes its own buffers (syn_out), this one
 * copies them once it is done. Everything they read is worked out here first (the parameters, the LFOs). */
static const uint8_t *trk_p[TB_TRACKS];               /* each track's parameters for the block, worked out here */
static int32_t trk_lp[TB_TRACKS];
static uint8_t trk_c1;                                /* the whole tracks (not synths) the second core makes */
static uint32_t trk_cost[TB_TRACKS], syn_vcost[TB_TRACKS];   /* the last block's: a track's ticks, a voice's */
static volatile uint32_t trk_acc[TB_TRACKS];          /* this block's, as the cores add them (each its own tracks) */
static uint32_t lane_cost[TB_TRACKS];                 /* (each track's chain after its sound, the last time: a block's worth) */
static uint8_t trk_mask, trk_inl;                     /* the tracks the second core works on in this part; done here instead */
static void gen_track(unsigned t, int32_t *b, unsigned n)   /* a track's sound (not a synth's), added into b */
{
    const uint8_t *P = trk_p[t], *adsr = &P[TB_P_ENV];
    unsigned i;
    switch (tb_proj.trk[t].machine) {
    case TB_M_KICK:
        tb_kick_render(&V[t].kick, P, step_samples() * 4u, b, n);
        tb_envk(&V[t].envk, adsr);
        for (i = 0; i < n; i++)
            b[i] = tb_mul30x(b[i], tb_env_step(&V[t].env, &V[t].envk));   /* (v22: at full, no product) */
        break;
    case TB_M_909:
#ifdef RFM2
        tb_dx_render(&V[t].dx, &V[t].d909, P, (const uint8_t *)tb_proj.trk[t].man.at, adsr, b, n);
#else
        tb909_render(&V[t].d909, P, adsr, trk_lp[t], b, n);
#endif
        break;
    case TB_M_BREAK:
        tb_brk_render(&V[t].brk, P, tb_proj.bpm10, adsr, trk_lp[t], b, n);
        break;
#if TB_HAS_SAMPLE
    case TB_M_SAMPLE: {
        uint8_t q[32];
        memcpy(q, P, 32);
        q[TBB_SYNC] = 0;
        tb_brk_render(&V[t].brk, q, tb_proj.bpm10, adsr, trk_lp[t], b, n);
        break;
    }
#endif
#if TB_HAS_ACID
    case TB_M_ACID:
        tb_acid_render(&V[t].acid, P, b, n, trk_lp[t], lfo_cutoff(t));
        break;
#endif
    default:
        break;
    }
}
static void syn_job(uint32_t mask)
{
    unsigned t;
    uint32_t t0 = PF_T(), a, c;
    for (t = 0; t < TB_TRACKS; t++)
        if ((mask >> t) & 1u) {
            a = PF_T();
            memset(syn_out[t], 0, syn_n * sizeof syn_out[t][0]);
            if ((trk_c1 >> t) & 1u)
                gen_track(t, syn_out[t], syn_n);
            else
                tb_syn_voices(&V[t].syn, &syn_ctx[t], syn_out[t], syn_vm1[t]);
            c = PF_T() - a;
            trk_acc[t] += c;
            if ((trk_c1 >> t) & 1u)
                tb_pf_job.trk += c;
            else
                tb_pf_job.syn += c;
        }
    (void)t0;
}
static uint32_t blk_worth(uint32_t c, unsigned n) { return n >= TB_BLOCK || !n ? c : c * TB_BLOCK / n; }
/* (21 Nov 2026, v22) every part of a block shared (not the first only), and this core no longer waits as soon as its
 * own tracks are made: it runs their chains first (tb_engine_render: the lanes in two passes), then waits
 * (tracks_join) and runs the others'. The dealing counts that: a whole track kept here costs its sound and its chain.
 * The costs are a block's worth (a part of a block scaled up), the parts compared alike */
static void render_tracks(int32_t (*buf)[TB_BLOCK], unsigned n, int split)
{
    unsigned t, mask = 0;
    int syn_inl = 0;
    uint32_t pt0;
    for (t = 0; t < TB_TRACKS; t++) {
        tb_track_t *tr = &tb_proj.trk[t];
        memset(buf[t], 0, n * sizeof(int32_t));
        if (tr->machine != vm[t]) {                   /* another machine here: what the last one held, cleared (audit 2:
                                                       * a synth's voices stayed "live" for ever under the 909, to ring
                                                       * again, stale, the day the synth came back) */
            if (vm[t])
                voices_init(t);
            vm[t] = tr->machine;
        }
    }
    trk_c1 = 0;
    if (split) {
        /* the work of the block in pieces (a synth voice, a whole other track), each with its cost the block before;
         * the dearest first, each to the core with less so far (a tie: the second); the first block, unknown: all
         * to the second */
        uint32_t l0 = 0, l1 = 0, cst[TB_TRACKS * TBS_VOICES];
        uint8_t who[TB_TRACKS * TBS_VOICES], used[TB_TRACKS * TBS_VOICES];
        unsigned np = 0, k, j;
        int dx_core = -1;
        tb_syn_step = step_samples();
        syn_n = n;
        for (t = 0; t < TB_TRACKS; t++) {
            unsigned m = tb_proj.trk[t].machine;
            trk_acc[t] = 0;
            if (m == TB_M_SYNTH && TB_HAS_SYNTH) {
                unsigned lv;
                syn_lp[t] = lfo_pitch(t), syn_lc[t] = lfo_cutoff(t);
                tb_syn_begin(&V[t].syn, prm(t), n, syn_lp[t], syn_lc[t], &syn_ctx[t]);
                lv = tb_syn_live(&V[t].syn);
                syn_vm1[t] = syn_vm0[t] = 0;
                mask |= 1u << t;
                if (syn_ctx[t].rave || syn_ctx[t].noise) {   /* (one piece: its voices share a state) */
                    who[np] = (uint8_t)t, used[np] = 15u, cst[np++] = syn_vcost[t] * 4u;
                } else
                    for (j = 0; j < TBS_VOICES; j++)
                        if ((lv >> j) & 1u)
                            who[np] = (uint8_t)t, used[np] = (uint8_t)(1u << j), cst[np++] = syn_vcost[t];
            } else if (m == TB_M_KICK || m == TB_M_909 || m == TB_M_BREAK || m == TB_M_SAMPLE || m == TB_M_ACID) {
                trk_p[t] = prm(t), trk_lp[t] = lfo_pitch(t);
                who[np] = (uint8_t)t, used[np] = 0, cst[np++] = trk_cost[t];
                mask |= 1u << t;
            }
        }
        for (k = 0; k < np; k++) {                    /* the dearest left, to the lighter core */
            unsigned b = np;
            int c1;
            for (j = 0; j < np; j++)
                if (who[j] != 0xFFu && (b == np || cst[j] > cst[b]))
                    b = j;
            t = who[b];
            c1 = l1 <= l0;
            if (tb_proj.trk[t].machine == TB_M_909) { /* (the 909s all on one core: DRUM-X's kit is one, shared) */
                if (dx_core < 0)
                    dx_core = c1;
                c1 = dx_core;
            }
            if (c1) {
                l1 += cst[b];
                if (used[b]) syn_vm1[t] |= used[b]; else trk_c1 |= (uint8_t)(1u << t);
            } else {
                l0 += cst[b];
                if (used[b]) syn_vm0[t] |= used[b]; else l0 += lane_cost[t];
            }
            who[b] = 0xFFu;
        }
        for (t = 0; t < TB_TRACKS; t++)               /* (the second core's tracks: what it makes, if anything) */
            if (((mask >> t) & 1u) && !syn_vm1[t] && !((trk_c1 >> t) & 1u))
                mask &= ~(1u << t);
        if (mask && !tb_engine_mc_run(syn_job, mask))
            syn_job(mask), syn_inl = 1;               /* (no second core: here, all the same) */
    }
    trk_mask = (uint8_t)mask, trk_inl = (uint8_t)syn_inl;
    for (t = 0; t < TB_TRACKS; t++) {
        tb_track_t *tr = &tb_proj.trk[t];
        const uint8_t *P;
        if (((syn_owned >> t) & 1u))
            continue;                                 /* (the second core still on it, late: silent this block) */
        if (split && tr->machine == TB_M_SYNTH && TB_HAS_SYNTH) {
            if (syn_vm0[t]) {                         /* this core's share of its voices (straight into its buffer) */
                pt0 = PF_T();
                tb_syn_voices(&V[t].syn, &syn_ctx[t], buf[t], syn_vm0[t]);
                pt0 = PF_T() - pt0;
                trk_acc[t] += pt0, pf0[TB_PROF_SYN] += pt0;
            }
            continue;
        }
        if (split && ((trk_c1 >> t) & 1u))
            continue;                                 /* (the second core's) */
        pt0 = PF_T();
        if (tr->machine == TB_M_SYNTH && TB_HAS_SYNTH) {
            tb_syn_step = step_samples();
            P = prm(t);
            tb_syn_render(&V[t].syn, P, buf[t], n, lfo_pitch(t), lfo_cutoff(t));
        } else {
            if (!split)
                trk_p[t] = prm(t), trk_lp[t] = lfo_pitch(t);
            gen_track(t, buf[t], n);
        }
        pt0 = PF_T() - pt0;
        if (split)
            trk_acc[t] += pt0;
        pf0[tr->machine == TB_M_SYNTH ? TB_PROF_SYN : TB_PROF_TRK] += pt0;
    }
}
static void tracks_join(int32_t (*buf)[TB_BLOCK], unsigned n, int split)
{
    unsigned t, i, mask = trk_mask;
    if (mask) {
        int r = 0;
        if (!trk_inl) {
            uint32_t w0 = PF_T();
            r = tb_engine_mc_wait(70u);               /* (by 70 % of the block: the rest of it still to do here) */
            pf0[TB_PROF_WAIT] += PF_T() - w0;
            if (r < 0)
                syn_job(mask), r = 0;                 /* (the second core given up, stopped: here) */
        }
        if (r == 0) {
            for (t = 0; t < TB_TRACKS; t++)
                if ((mask >> t) & 1u) {
                    if (syn_vm0[t] && tb_proj.trk[t].machine == TB_M_SYNTH)
                        for (i = 0; i < n; i++)
                            buf[t][i] += syn_out[t][i];
                    else
                        memcpy(buf[t], syn_out[t], n * sizeof buf[t][0]);
                }
        } else
            c1_late = 1, syn_owned = (uint8_t)mask, tb_c1_late_count++;   /* (late: those tracks silent this block) */
    }
    if (split && !c1_late)                            /* each piece's cost for the next block */
        for (t = 0; t < TB_TRACKS; t++) {
            unsigned lv = 0, j;
            if (tb_proj.trk[t].machine == TB_M_SYNTH) {
                for (j = 0; j < TBS_VOICES; j++)
                    lv += ((syn_vm0[t] | syn_vm1[t]) >> j) & 1u;
                if (lv)
                    syn_vcost[t] = blk_worth(trk_acc[t] / lv, n);
            } else if (trk_acc[t])
                trk_cost[t] = blk_worth(trk_acc[t], n);
        }
}

/* volume 0..127 -> Q15, a square law (100: about -4 dB, 127: 0 dB) */
static int32_t vol_gain(unsigned v) { return (int32_t)(v * v * 2u + (v * v >> 6)); }

/* the RISER's beat of silence: its last bar's last beat */
static int riser_gap_now(void) { return riser_on && tb_proj.riser_gap && bar_count + 1u == riser_end && gtick % 32u >= 24u; }

/* muted by its own MUTE, by another track's SOLO, or by a live effect */
static int muted(unsigned t)
{
    unsigned i, any = 0;
    for (i = 0; i < TB_TRACKS; i++)
        any |= tb_proj.trk[i].solo;
    if (riser_gap_now())                              /* (v25: STOP held is the TAPE STOP now - tb_engine_render) */
        return 1;
    if (drop_on && (dc_end == TB_DEND_STOP || dc_end == TB_DEND_STOPCRASH) && bar_count + 1u == drop_end && gtick % 32u >= 24u)
        return 1;                                     /* (a DROP ending stopped: its last beat, everything out) */
    if ((pf(TB_PF_NOKICK) || (drop_on && dc_kick == TB_DKICK_OUT)) && tb_proj.trk[t].machine == TB_M_KICK)
        return 1;
    if (pf(TB_PF_KICKONLY) && tb_proj.trk[t].machine != TB_M_KICK)
        return 1;
    return tb_proj.trk[t].muted || (any && !tb_proj.trk[t].solo);
}

static void take_keys(void)
{
    while (kr != kw) {
        key_t k;
        KQ_BARRIER();
        k = kq[kr];
        KQ_BARRIER();
        kr = (uint8_t)((kr + 1u) & 63u);
        if (k.note == 255) {                          /* panic: everything on the track off */
            unsigned i;
            arp_t *a = &A[k.track];
            a->n = 0, a->down = 0;
            if (a->on)
                voice(k.track, a->on - 1u, 0, 0, 0), a->on = 0;
            for (i = 0; i < a->nchord_on; i++)
                voice(k.track, a->chord_on[i], 0, 0, 0);
            a->nchord_on = 0;
            for (i = 0; i < nev;)
                if (ev[i].t == k.track)
                    ev[i] = ev[--nev];
                else
                    i++;
            if (TB_HAS_SYNTH && tb_proj.trk[k.track].machine == TB_M_SYNTH)
                tb_syn_all_off(&V[k.track].syn);
            if (tb_proj.trk[k.track].machine == TB_M_BREAK || tb_proj.trk[k.track].machine == TB_M_SAMPLE)
                tb_brk_release(&V[k.track].brk, 255);
            continue;
        }
        if (TB_HAS_ROLL && (int8_t)k.track == rep_track) {   /* NOTE REPEAT: the key held repeats */
            unsigned i;
            for (i = 0; i < RP.n && RP.note[i] != k.note; i++)
                ;
            if (RP.track != k.track)
                RP.n = 0, RP.track = k.track, i = 0;
            if (k.vel) {
                if (i < RP.n || RP.n >= 4u)
                    continue;
                if (!RP.n) {                          /* the first: at once, the next ones on the grid */
                    uint32_t beat = arp_beat(k.track), sps = step_samples();
                    uint32_t at = (ptick / 2u) * sps + (ptick & 1u) * (sps / 2u) + tick_pos;
                    RP.pos = playing ? at % (beat ? beat : 1u) : 0u;
                }
                RP.note[RP.n] = k.note, RP.vel[RP.n] = k.vel, RP.n++;
                note(k.track, k.note, k.vel, EV_REPEAT);
                schedule_f(now + arp_beat(k.track) / 2u, k.track, k.note, 0, EV_REPEAT);
            } else if (i < RP.n) {
                for (; i + 1u < RP.n; i++)
                    RP.note[i] = RP.note[i + 1u], RP.vel[i] = RP.vel[i + 1u];
                RP.n--;
            }
            continue;
        }
        note(k.track, k.note, k.vel, 0);
        if (k.hold)                                   /* a preview: its own note-off, later (heard, then let go) */
            schedule_f(now + (uint32_t)k.hold * (TB_FS / 100u), k.track, k.note, 0, EV_LETGO);
    }
    if (rep_track < 0)
        RP.n = 0;
}

/* a repeat's beat: the keys held again (and written into the steps when recording) */
static void repeat_tick(void)
{
    unsigned t = RP.track, i, len = arp_beat(t) / 2u;
    for (i = 0; i < RP.n; i++) {
        note(t, RP.note[i], RP.vel[i], EV_REPEAT);
        schedule_f(now + len, t, RP.note[i], 0, EV_REPEAT);
        if (rep_rec && playing) {
            unsigned L_ = track_len(t), s = tstep[t];
            uint32_t sl = step_samples() * ticks_per_step(t) / 2u, into = (gtick % ticks_per_step(t)) * (step_samples() / 2u) + tick_pos;
            tb_step_t *st;
            if (into * 2u >= sl)
                s = (s + 1u) % L_, skip_step[t] = (int16_t)s;
            st = &TB_STEP(t, s);
            if (tb_proj.trk[t].machine == TB_M_909)
                st->hits |= (uint16_t)(1u << (RP.note[i] % TB909_PIECES));
            else
                st->hits = 1, st->note = RP.note[i], st->vel = RP.vel[i], st->gate = 4, st->flags = 0;
        }
    }
}

/* what goes over the master: the RISER (noise through a band climbing, a siren under it), its impact (a boom
 * falling, a burst of noise), the metronome's click */
static void extras(int32_t *l, int32_t *r, unsigned n, uint32_t sps)
{
    unsigned i;
    uint32_t t0 = now - n;
    if (TB_HAS_RISER && riser_on && riser_end > riser_start) {      /* (its last beat too: the music out, the riser on to the end) */
        uint32_t span = (riser_end - riser_start) * 32u * (sps / 2u), at = ((bar_count - riser_start) * 32u + gtick % 32u) * (sps / 2u) + tick_pos;
        int32_t p = (int32_t)((uint64_t)(at > span ? span : at) * 65536u / (span ? span : 1u));   /* 0..65536 */
        int32_t amp = (int32_t)(((int64_t)p * p) >> 17) + p / 8;                                   /* rising, ~0..40000 */
        uint32_t hz = (uint32_t)((300ull * tb_exp2(p * 5)) >> 16);                                 /* 300 Hz .. 9.6 kHz */
        int32_t f = 2 * tb_sin(hz * 24348u / 2u), q = 20000;
        uint32_t sinc = (uint32_t)(((150ull * tb_exp2(p * 4)) >> 16) * 97391u);                    /* 150 .. 2400 Hz */
        unsigned ty, lv[TB_NRISER], nset = 0, norm;
        for (ty = 0; ty < TB_NRISER; ty++)
            nset += (lv[ty] = tb_proj.riser_lvl[ty]) != 0;
        if (!nset)                                    /* (none set: the sound shown, all of it) */
            lv[tb_proj.riser_type % TB_NRISER] = 100, nset = 1;
        norm = 100u * 100u / (100u + 30u * (nset - 1u));   /* (several together: each a little softer, the sum level) */
        norm = norm * TB_FXVOL(TB_FXV_RISER) / 100u;  /* (FX held: KNOB 1) */
        for (ty = 0; ty < TB_NRISER; ty++)
            lv[ty] = lv[ty] * norm / 100u;
        for (ty = 0; ty < TB_NRISER; ty++) {          /* the sounds set, together, each as loud as its LEVEL */
            rs_t *S = &RS[ty];
            int32_t tl[TB_BLOCK], tr[TB_BLOCK];
            if (!lv[ty])
                continue;
            memset(tl, 0, n * sizeof *tl), memset(tr, 0, n * sizeof *tr);
            switch (ty) {
            default:                                      /* CLASSIC: noise through a band climbing, a siren under it */
                for (i = 0; i < n; i++) {
                    int32_t x = (int32_t)(tb_noise(&seed) >> 17) - 16384, hi, y, sn;
                    S->lo += (f * S->bd) >> 15;
                    hi = x - S->lo - ((q * S->bd) >> 15);
                    S->bd += (f * hi) >> 15;
                    S->lo = tb_clamp(S->lo, -(1 << 20), 1 << 20), S->bd = tb_clamp(S->bd, -(1 << 20), 1 << 20);
                    S->ph += sinc;
                    sn = tb_sin(S->ph) / 5;
                    y = (int32_t)(((int64_t)(S->bd + sn) * amp) >> 14);
                    tl[i] += y, tr[i] += (int32_t)(((int64_t)(S->bd - sn) * amp) >> 14);
                }
                break;
            case TB_RISER_NOISE: {                        /* NOISE: only noise, a narrow band sweeping up, wide */
                int32_t qn = 9000;
                for (i = 0; i < n; i++) {
                    int32_t x = (int32_t)(tb_noise(&seed) >> 17) - 16384, x2 = (int32_t)(tb_noise(&seed) >> 17) - 16384, hi;
                    S->lo += (f * S->bd) >> 15;
                    hi = x - S->lo - ((qn * S->bd) >> 15);
                    S->bd += (f * hi) >> 15;
                    S->lo = tb_clamp(S->lo, -(1 << 20), 1 << 20), S->bd = tb_clamp(S->bd, -(1 << 20), 1 << 20);
                    S->lo2 += (f * S->hp) >> 15;    /* (the other side: its own noise, the same band) */
                    hi = x2 - S->lo2 - ((qn * S->hp) >> 15);
                    S->hp += (f * hi) >> 15;
                    S->lo2 = tb_clamp(S->lo2, -(1 << 20), 1 << 20), S->hp = tb_clamp(S->hp, -(1 << 20), 1 << 20);
                    tl[i] += (int32_t)(((int64_t)S->bd * amp) >> 14), tr[i] += (int32_t)(((int64_t)S->hp * amp) >> 14);
                }
                break;
            }
            case TB_RISER_WIND: {                         /* WIND: white noise, dark to bright, a slow whoosh in it */
                uint32_t lp = (uint32_t)((150ull * tb_exp2(p * 6)) >> 16);                         /* 150 Hz .. 9.6 kHz */
                int32_t a = (int32_t)(lp * 4669u >> 10), sw;                                        /* (one pole) */
                a = a > 30000 ? 30000 : a;
                S->ph += (uint32_t)n * (uint32_t)(((1u + (uint32_t)p / 16384u) * 97391u));      /* the whoosh: 1 .. 5 Hz */
                sw = 24000 + (tb_sin(S->ph) >> 2);
                for (i = 0; i < n; i++) {
                    int32_t x = (int32_t)(tb_noise(&seed) >> 17) - 16384, x2 = (int32_t)(tb_noise(&seed) >> 17) - 16384;
                    S->lo += ((x - S->lo) * a) >> 15, S->lo2 += ((x2 - S->lo2) * a) >> 15;
                    S->sm[0] += ((S->lo - S->sm[0]) * a) >> 15, S->sm[1] += ((S->lo2 - S->sm[1]) * a) >> 15;
                    tl[i] += (int32_t)(((int64_t)S->sm[0] * amp >> 15) * sw >> 13);
                    tr[i] += (int32_t)(((int64_t)S->sm[1] * amp >> 15) * sw >> 13);
                }
                break;
            }
            case TB_RISER_SIREN: {                        /* SIREN: the rave siren, wailing and climbing */
                uint32_t base = (uint32_t)((280ull * tb_exp2(p * 5 / 2)) >> 16);                    /* 280 .. 1580 Hz */
                S->ph += (uint32_t)n * 6u * 97391u;    /* (its wail: 6 Hz) */
                for (i = 0; i < n; i++) {
                    int32_t w = tb_sin(S->ph + i * 6u * 97391u), s1, s2;
                    uint32_t hz = base + (uint32_t)(((int64_t)base * w) >> 18);   /* +- 12 % */
                    S->osc[0] += hz * 97391u, S->osc[1] += (hz + hz / 120u) * 97391u;
                    s1 = tb_sin(S->osc[0]), s2 = tb_sin(S->osc[1]);
                    s1 = tb_clamp(s1 * 2, -32767, 32767), s2 = tb_clamp(s2 * 2, -32767, 32767);   /* (rounded square) */
                    tl[i] += (int32_t)(((int64_t)s1 * amp) >> 16), tr[i] += (int32_t)(((int64_t)s2 * amp) >> 16);
                }
                break;
            }
            case TB_RISER_SAW: {                          /* SAW UP: a detuned saw chord sliding three octaves up, opening */
                uint32_t hz = (uint32_t)((110ull * tb_exp2(p * 3)) >> 16), k;                       /* 110 .. 880 Hz */
                static const int16_t DET[6] = {-14, 9, 0, 19, -6, 702};   /* (cents; the last, a fifth up) */
                int32_t a = 4000 + (p >> 2);              /* (its filter: closed .. open) */
                uint32_t inc[6];
                for (k = 0; k < 6u; k++)
                    inc[k] = (uint32_t)(((uint64_t)hz * (uint64_t)tb_exp2(DET[k] * 65536 / 1200) >> 16) * 97391u);
                for (i = 0; i < n; i++) {
                    int32_t sl = 0, sr = 0;
                    for (k = 0; k < 6u; k++) {
                        int32_t s = (int32_t)(S->osc[k] >> 16) - 32768;
                        S->osc[k] += inc[k];
                        if (k & 1u) sr += s; else sl += s;
                        if (k >= 4u) sl += s / 2, sr += s / 2;
                    }
                    S->sm[0] += ((sl / 4 - S->sm[0]) * a) >> 15, S->sm[1] += ((sr / 4 - S->sm[1]) * a) >> 15;
                    S->sm[2] += ((S->sm[0] - S->sm[2]) * a) >> 15, S->sm[3] += ((S->sm[1] - S->sm[3]) * a) >> 15;
                    tl[i] += (int32_t)(((int64_t)S->sm[2] * amp) >> 15), tr[i] += (int32_t)(((int64_t)S->sm[3] * amp) >> 15);
                }
                break;
            }
            case TB_RISER_SNARE: {                        /* SNARE ROLL: hits faster and faster (1/4, 1/8, 1/16, 1/32) */
                uint32_t t32 = sps / 2u, stage = (uint32_t)p * 4u / 65537u, every = 8u >> stage;   /* (in 32nds) */
                uint32_t body = (uint32_t)((180ull * tb_exp2(p)) >> 16);                            /* 180 .. 360 Hz */
                int32_t dk = (int32_t)(32767u - 229369u / (every * (t32 ? t32 : 1u) + 1u));   /* (its decay: gone by the next) */
                for (i = 0; i < n; i++) {
                    uint32_t tk = (at + i) / (t32 ? t32 : 1u);
                    int32_t x, y;
                    if (tk != S->tick && tk % every == 0)   /* a hit */
                        S->env = 32767, S->osc[0] = 0;
                    S->tick = tk;
                    x = (int32_t)(tb_noise(&seed) >> 17) - 16384;
                    S->lo += ((x - S->lo) * 19000) >> 15;   /* (the snares: the noise, its hiss softened ..) */
                    S->hp += (S->lo - S->hp) >> 4;      /* (.. above ~450 Hz) */
                    S->osc[0] += body * 97391u;
                    y = ((S->lo - S->hp) + tb_sin(S->osc[0]) * 3 / 4) * 2;
                    y = (int32_t)(((int64_t)y * S->env) >> 15);
                    S->env = (int32_t)(((int64_t)S->env * dk) >> 15);
                    y = (int32_t)(((int64_t)y * (amp * 3 / 4 + 10000)) >> 15);
                    tl[i] += y, tr[i] += y;
                }
                break;
            }
            }
            {
                int32_t q = (int32_t)lv[ty] * 32768 / 100;   /* (the level in Q15, once a block) */
                for (i = 0; i < n; i++)
                    l[i] += (int32_t)(((int64_t)tl[i] * q) >> 15), r[i] += (int32_t)(((int64_t)tr[i] * q) >> 15);
            }
        }
    }
    if (now - impact_at < 66150u) {                   /* the impact: 1.5 s */
        for (i = 0; i < n; i++) {
            uint32_t a = t0 + i - impact_at;
            int32_t e, en, y;
            if (a >= 66150u)
                break;
            e = (int32_t)(32767 - a * 32767u / 66150u), e = (int32_t)(((int64_t)e * e) >> 15);
            en = a < 22050u ? (int32_t)(32767 - a * 32767u / 22050u) : 0, en = (int32_t)(((int64_t)en * en * en) >> 30);
            riser_ph += (uint32_t)((40u + 110u * (uint32_t)e / 32767u) * 97391u);      /* 150 -> 40 Hz */
            y = (int32_t)(((int64_t)tb_sin(riser_ph) * e) >> 15) + (int32_t)((((int64_t)(int32_t)(tb_noise(&seed) >> 17) - 16384) * en) >> 15);
            y = y * (int32_t)TB_FXVOL(TB_FXV_IMPACT) / 100;   /* (FX held: KNOB 4) */
            l[i] += y, r[i] += y;
        }
    }
    if (now - click_at < 2205u) {                     /* the click: 50 ms */
        static const int16_t LV[4] = {6000, 11000, 17000, 25000};   /* METRO's ON 1 .. 4 (REC 1 .. 4) */
        int32_t lv = tb_proj.metro ? LV[(tb_proj.metro - 1u) % 4u] : 14000;
        uint32_t inc = click_bar ? 1760u * 97391u : 1320u * 97391u;
        for (i = 0; i < n; i++) {
            uint32_t a = t0 + i - click_at;
            int32_t e, y;
            if (a >= 2205u)
                continue;
            e = (int32_t)(32767 - a * 32767u / 2205u), e = (int32_t)(((int64_t)e * e) >> 15);
            y = (int32_t)(((int64_t)tb_sin(a * inc) * e >> 15) * lv >> 15);
            l[i] += y, r[i] += y;
        }
    }
}

uint32_t tb_engine_clock24(void)
{
    uint32_t tl = step_samples() / 2u;
    return playing ? ptick * 3u + (tick_pos * 3u) / (tl ? tl : 1u) : 0u;
}

/* the first version's leads (the synth's WAVE RAVE) had the Atelier's chorus on their sends: one bus, a mono delay
 * swept 9 .. 11 ms at 0.4 Hz (Felucca fx.c, its defaults: RATE 40, DEPTH 60), added to both sides with the mix */
static int16_t rcho_line[512];
static int32_t rcho_in[TB_BLOCK];
static uint32_t rcho_w, rcho_ph;
static void rave_chorus(int32_t *l, int32_t *r, unsigned n)
{
    static uint32_t dry;                              /* (zeros written in a row: past its line's length, it adds nothing) */
    unsigned i;
    uint32_t any = 0;
    for (i = 0; i < n; i++)
        any |= (uint32_t)rcho_in[i];
    if (!any && dry >= 512u) {                        /* (the same, bit for bit: its place and its sweep as if it ran) */
        rcho_w += n, rcho_ph += 39980u * n;
        return;
    }
    dry = any ? 0u : dry + n;
    for (i = 0; i < n; i++) {
        int32_t d = (400 << 8) + (((tb_sin(rcho_ph) + 32768) * 360) >> 8), y;
        uint32_t ri = (uint32_t)d >> 8;
        int32_t f = d & 255, c0, c1;
        rcho_line[rcho_w & 511u] = (int16_t)tb_clamp(rcho_in[i] >> 1, -32768, 32767);
        c0 = rcho_line[(rcho_w - ri) & 511u];
        c1 = rcho_line[(rcho_w - ri - 1u) & 511u];
        y = (c0 + (((c1 - c0) * f) >> 8)) << 1;
        rcho_w++, rcho_ph += 39980u;
        l[i] += y, r[i] += y;
        rcho_in[i] = 0;
    }
}

static int any_fx(unsigned t)
{
    unsigned j;
    for (j = 0; j < TB_NFX; j++)
        if (tb_engine_fx_on(t, j))
            return 1;
    return 0;
}
static uint8_t dormant[TB_TRACKS];       /* its effects let go (silent under -48 dB): skipped till it plays again */
static uint32_t raw_quiet[TB_TRACKS];   /* samples each track's instrument has been silent */
/* a product Q15 rounded towards zero (a floor keeps a filter's state stuck just under 0 once the sound has gone) */
static inline int32_t rz15(int32_t a, int32_t k)
{
    int32_t p = a * k;
    return (p + ((p >> 31) & 32767)) >> 15;
}

/* a DC blocker (a one-pole high-pass at ~3.5 Hz, Q16 states): what is no sound (an offset the effects' rounding
 * leaves, that the master's drive and EQ would raise) never reaches the output */
static void dc_block(int32_t *x, unsigned n, int64_t *st)
{
    /* y = x - x1 + y1 (1 - 2^-11): 3.4 Hz (21 Nov 2026: 32-bit sums, no 64-bit multiply a sample - the FM-1's are
     * calls; the state 4 bits finer, the music being 16-bit with headroom) */
    unsigned i;
    int32_t x1 = (int32_t)st[0], acc = (int32_t)st[1];
    if (!x1 && !acc) {                                /* (at rest, silence in: silence out, nothing to work - the same) */
        uint32_t any = 0;
        for (i = 0; i < n; i++)
            any |= (uint32_t)x[i];
        if (!any)
            return;
    }
    for (i = 0; i < n; i++) {
        int32_t in = x[i];
        acc += ((in - x1) << 4) - (acc >> 11) - (acc > 0);   /* (the shift rounds down: +1 on the positive side,
                                                                *  so it settles at exactly 0, as the silence needs) */
        x1 = in;
        x[i] = acc >> 4;
    }
    st[0] = x1, st[1] = acc;
}

/* (21 Nov 2026) the FM-1's second core, as JIANT uses it: the reverb and the delay there, a block behind (2.9 ms: their
 * sends of the last block), while this core renders the tracks of this one - each core its own state, the buses'
 * only theirs. The board's calls (rfm_board.c, hal/fm1_multicore.h); without a second core (or once it was given
 * up) the same, here, at once */
__attribute__((weak)) int tb_engine_mc_avail(void) { return 0; }
__attribute__((weak)) int tb_engine_mc_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn, (void)arg; return 0; }
__attribute__((weak)) int tb_engine_mc_wait(unsigned pct) { (void)pct; return -1; }
__attribute__((weak)) int tb_engine_mc_busy(void) { return 0; }
static int32_t fxs_d[2][TB_BLOCK] TB_LOWBUF, fxs_r[2][TB_BLOCK] TB_LOWBUF, fxw_l[2][TB_BLOCK] TB_LOWBUF, fxw_r[2][TB_BLOCK] TB_LOWBUF;
static struct { uint32_t n, sps; uint8_t wash, hold, ok; } fxa[2];
static uint8_t fx_pipe, fx_cur;                      /* the buses a block behind (a second core seen); this block's set */
static void fx_job(uint32_t k)
{
    memset(fxw_l[k], 0, sizeof fxw_l[k]), memset(fxw_r[k], 0, sizeof fxw_r[k]);
    if (fxa[k].ok)
        tb_fx_run(&tb_proj.fx, fxs_d[k], fxs_r[k], fxw_l[k], fxw_r[k], fxa[k].n, fxa[k].sps, fxa[k].wash, fxa[k].hold);
}

/* (v25) the TAPE STOP (the black key STOP held): the whole machine slowing down as a record stopped by hand - the
 * engine run slower (fewer frames a block: the song, its tempo, its tails, all), the frames stretched to the block
 * (read at a speed falling from 1 to 0 over two beats: the pitch falls with it); stopped, silence; let go, it spins up
 * again in 0.1 s from where it stopped */
static int32_t tape_src[2 * (TB_BLOCK + 2)];         /* the record's frames: [0] the last one heard, then this block's */
static uint32_t tape_sp = 65536u, tape_pos;           /* the speed (Q16, 65536: as recorded); between [0] and [1] (Q16) */
static uint8_t tape_in;
void tb_engine_render(int32_t *out, unsigned n);
static void tape_render(int32_t *out, unsigned n)
{
    uint32_t len = step_samples() * 8u, sp0 = tape_sp, sp1, adv, i, m, p;
    len = len < 17640u ? 17640u : len > 52920u ? 52920u : len;   /* (two beats: 0.4 .. 1.2 s) */
    if (pf(TB_PF_STOP))
        sp1 = sp0 > 65536u * n / len ? sp0 - 65536u * n / len : 0u;
    else                                              /* (let go: up to speed in 0.1 s) */
        sp1 = sp0 + 65536u * n / 4410u < 65536u ? sp0 + 65536u * n / 4410u : 65536u;
    for (adv = 0, i = 0; i < n; i++)
        adv += sp0 + (uint32_t)(((int32_t)sp1 - (int32_t)sp0) * (int32_t)i / (int32_t)n);
    m = (tape_pos + adv) >> 16;                       /* (the record's frames this block: m <= n) */
    if (m) {
        tape_in = 1;
        tb_engine_render(tape_src + 2, m);            /* (the machine itself, m frames on: slower) */
        tape_in = 0;
    }
    for (p = tape_pos, i = 0; i < n; i++) {
        uint32_t k = p >> 16, f = (p >> 1) & 0x7FFFu, k1 = k + 1u > m ? m : k + 1u;
        uint32_t sp = sp0 + (uint32_t)(((int32_t)sp1 - (int32_t)sp0) * (int32_t)i / (int32_t)n);
        int32_t g = sp >= 16384u ? 32768 : (int32_t)(sp * 2u);   /* (the last quarter of its slowing: faded out) */
        int32_t a = tape_src[2 * k], b = tape_src[2 * k1], c = tape_src[2 * k + 1], d = tape_src[2 * k1 + 1];
        out[2 * i] = tb_mul15(a + tb_mul15(b - a, (int32_t)f), g);   /* (Q23: |x| < 2^24, exact) */
        out[2 * i + 1] = tb_mul15(c + tb_mul15(d - c, (int32_t)f), g);
        p += sp;
    }
    tape_pos = tape_pos + adv - (m << 16);
    tape_src[0] = tape_src[2 * m], tape_src[1] = tape_src[2 * m + 1];
    tape_sp = sp1;
    if (sp1 == 65536u && !pf(TB_PF_STOP))
        tape_pos = 0;                                 /* (back as recorded: the next block straight on) */
}
void tb_engine_render(int32_t *out, unsigned n)
{
    static int32_t buf[TB_TRACKS][TB_BLOCK];
    static int32_t mixl[TB_BLOCK], mixr[TB_BLOCK], sdly[TB_BLOCK], srev[TB_BLOCK], fxo[TB_BLOCK], fxr_[TB_BLOCK];
    unsigned i, t, done = 0;
    uint32_t sps = step_samples(), tick0 = sps / 2u;
    int32_t gl[TB_TRACKS], gr_[TB_TRACKS];
    int echo = pf(TB_PF_ECHO), wash = pf(TB_PF_WASH), fx_sent, fx_issued;
    uint32_t pf_t0, pf_r0;
    unsigned pass;
    int tsplit;
    if (!tape_in && n && (pf(TB_PF_STOP) || tape_sp < 65536u)) {   /* (v25) the TAPE STOP */
        tape_render(out, n > TB_BLOCK ? TB_BLOCK : n);
        return;
    }
    pf_fold();
    if (fxk_track >= 0) {                             /* (v25) a KICK ROLL: its hit let go after a step; a beat and a
                                                       *  step after its last, the pattern's kick back */
        fxk_since += n;
        if (fxk_gate && (fxk_gate = fxk_gate > n ? fxk_gate - n : 0u) == 0u)
            tb_env_off(&V[fxk_track].env);
        if (fxk_since > 5u * step_samples())
            fxk_track = -1;
    }                                        /* (the last block's times; this one's from here) */
    pf_t0 = PF_T();
    if (n > TB_BLOCK)
        n = TB_BLOCK;
    if (held_req) {                                   /* (the project being rewritten: silence, nothing of it read) */
        memset(out, 0, 2u * n * sizeof *out);
        return;
    }
    rendering = 1;
    __asm__ volatile("" ::: "memory");
    if (!fx_pipe && tb_engine_mc_avail())
        fx_pipe = 1;                                  /* (from now on the buses a block behind, whichever core works them) */
    fx_sent = 0, fx_issued = 0;                       /* (the buses: handed over after the block's first part) */
    if (c1_late) {                                    /* the second core late on the last block: still at it? */
        int b = tb_engine_mc_busy();
        if (b > 0)
            fx_issued = 1, fx_sent = -1;              /* (yes: nothing for it this block; its tracks silent, the buses not
                                                       *  run - their state is its, being worked) */
        else
            c1_late = 0, syn_owned = 0;               /* (done, or given up: its work of then let go, on as before) */
    }
    tb_brk_transport = (uint8_t)playing;              /* (a DISC break: a key toggles it only with PLAY off) */
    if (play_req == 1 && !playing && ci_req && !ci_left) {   /* a count-in first: the clicks, then PLAY */
        ci_left = ci_req * 4u * sps, ci_pos = 0, ci_req = 0, play_req = 0;
        click_at = now, click_bar = 1;
    } else if (play_req == 2 && ci_left)              /* (stopped while counting in) */
        ci_left = 0, play_req = 0;
    if (ci_left) {
        uint32_t b0 = ci_pos / (4u * sps), b1;
        ci_pos += n;
        b1 = ci_pos / (4u * sps);
        if (ci_pos >= ci_left)
            ci_left = 0, play_req = 1;
        else if (b1 != b0)
            click_at = now, click_bar = b1 % 4u == 0;
    }
    if (play_req == 1 && !playing) {
        playing = 1, tick_pos = 0, gtick = 0, ptick = 0, bar_count = (uint32_t)-1, drop_on = 0, riser_on = 0;
        song_pos = 0, song_bar = (uint32_t)-1;
        if (TB_HAS_SONG && tb_proj.song_on && tb_proj.song_pat[0] < TB_PATTERNS)
            queued = (int8_t)tb_proj.song_pat[0];
        for (t = 0; t < TB_TRACKS; t++)
            tstep[t] = 0, A[t].pos = arp_beat(t);     /* (the arpeggiators on the grid) */
        if (queued >= 0)
            tb_proj.pattern = (uint8_t)queued, queued = -1;
        do_tick();
    } else if (play_req == 2 && playing) {
        playing = 0;
        memset(alk_n, 0, sizeof alk_n);               /* (stopped: the knobs as they are) */
        for (i = 0; i < TB_TRACKS; i++)               /* a break played by the steps: let go (FULL would play on) */
            if (tb_proj.trk[i].machine == TB_M_BREAK)
                tb_brk_release(&V[i].brk, 255);
        for (i = 0; i < nev;)                         /* the notes still to come: only their offs */
            if (ev[i].vel)
                ev[i] = ev[--nev];
            else
                i++;
    }
    play_req = 0;
    take_keys();
    /* the live effects: how long they have been held */
    if (pf_mask != pf_was)
        pf_at = 0, pf_was = pf_mask;
    else
        pf_at += n;
    sweep = (pf(TB_PF_LPSWEEP) || pf(TB_PF_HPSWEEP)) ? (int32_t)(pf_at > 8u * sps ? 65536u : (uint64_t)pf_at * 65536u / (8u * sps)) : 0;
    memset(mixl, 0, sizeof mixl), memset(mixr, 0, sizeof mixr), memset(sdly, 0, sizeof sdly), memset(srev, 0, sizeof srev);
    for (t = 0; t < TB_TRACKS; t++) {
        tb_track_t *tr = &tb_proj.trk[t];
        int32_t f = (int32_t)chv(t, TB_LK_FILT, tr->filt), pan = (int32_t)chv(t, TB_LK_PAN, tr->pan);   /* (0: all left, not "unset": every track starts at 64) */
        int32_t g = vol_gain(chv(t, TB_LK_VOL, tr->vol));   /* (v24: worked out muted or not - 0 after, if muted) */
        trk_mt[t] = (uint8_t)muted(t);
        if (TB_HAS_LFO)
            lfo_run(t, n);
        if (tr->lfo_depth) {                          /* the LFO on the channel */
            int32_t m = (int32_t)(((int64_t)L[t].v * tr->lfo_depth) >> 15);   /* -127..127 */
            if (tr->lfo_dest == TB_DEST_FILTRE)
                f = tb_clamp(f + m / 2, 0, 127);
            else if (tr->lfo_dest == TB_DEST_PAN)
                pan = tb_clamp(pan + m / 2, 0, 127);
            else if (tr->lfo_dest == TB_DEST_VOLUME)
                g = (int32_t)(((int64_t)g * (32767 - (tr->lfo_depth * (32767 - L[t].v) >> 8))) >> 15);
            if (tr->machine != TB_M_SYNTH && tr->lfo_dest == TB_DEST_CUTOFF)
                f = tb_clamp(f + m / 2, 0, 127);
        }
        tb_filter_set(&F[t], (unsigned)f);
        {                                             /* the pan: an equal-power law, both at g in the centre */
            uint32_t ph = (uint32_t)pan * (0x40000000u / 127u);
            int32_t sl = tb_sin(0x40000000u - ph), sr = tb_sin(ph);
            if (g >= 0 && g <= 131071) {              /* (21 Nov 2026: in 32 bits, unsigned - the same numbers, no 64-bit
                                                       *  division: the FM-1 has none, ~1200 cycles each, 12 a block) */
                gl[t] = (int32_t)((uint32_t)g * (uint32_t)sl / 23170u);
                gr_[t] = (int32_t)((uint32_t)g * (uint32_t)sr / 23170u);
            } else {
                gl[t] = (int32_t)(((int64_t)g * sl) / 23170);
                gr_[t] = (int32_t)(((int64_t)g * sr) / 23170);
            }
            if (gl[t] > g)
                gl[t] = g;
            if (gr_[t] > g)
                gr_[t] = g;
            go_l[t] = gl[t], go_r[t] = gr_[t];
            if (trk_mt[t])
                gl[t] = gr_[t] = 0;
        }
    }
    /* the master's filter: the sweeps (2 bars to the end), KILL BASS */
    {
        int mk = 64;
        if (pf(TB_PF_LPSWEEP))
            mk = 62 - (int)((int64_t)sweep * 54 >> 16);
        else if (pf(TB_PF_HPSWEEP))
            mk = 67 + (int)((int64_t)sweep * 52 >> 16);
        else if (pf(TB_PF_BUILD))
            mk = 67 + (int)(pf_at > 16u * sps ? 40 : (int64_t)pf_at * 40 / (16 * (int64_t)sps));
        else if (pf(TB_PF_KILLBASS))
            mk = 90;
        else if (riser_on && tb_proj.riser_filter && riser_end > riser_start) {   /* the RISER: the music thinning */
            uint32_t span = (riser_end - riser_start) * 32u, at = (bar_count - riser_start) * 32u + gtick % 32u;
            mk = 67 + (int)(at * 40u / (span ? span : 1u));
        } else if (drop_on && drop_end > drop_start && dc_filt == TB_DFILT_DOWN) {   /* the music sinking */
            uint32_t span = (drop_end - drop_start) * 32u, at = (bar_count - drop_start) * 32u + gtick % 32u;
            mk = 61 - (int)(at * 46u / (span ? span : 1u));
        } else if (drop_on && drop_end > drop_start && dc_filt == TB_DFILT_UP) {   /* the DROP: the filter rising */
            uint32_t span = (drop_end - drop_start) * 32u, at = (bar_count - drop_start) * 32u + gtick % 32u;
            mk = 67 + (int)(at * 44u / (span ? span : 1u));
        }
        tb_filter_set(&MF[0], (unsigned)mk);
        tb_filter_set(&MF[1], (unsigned)mk);
    }
    while (done < n) {
        unsigned len = n - done;
        uint32_t tick_len = (gtick & 1u) ? sps - tick0 : tick0;
        fire_due();
        for (t = 0; t < TB_TRACKS; t++)               /* the arpeggiators' beats */
            if (arp_on(t) && (A[t].n || A[t].on || A[t].nchord_on)) {
                uint32_t beat = arp_beat(t), sw = 0;
                if (tb_proj.trk[t].arp_swing && (A[t].beat_no & 1u))
                    sw = (uint32_t)((uint64_t)beat * tb_proj.trk[t].arp_swing / 381u);
                if (A[t].pos >= beat + sw) {
                    A[t].pos -= beat + sw;
                    if (A[t].pos > beat)
                        A[t].pos = 0;
                    arp_tick(t);
                }
                if (beat + sw - A[t].pos < len)
                    len = beat + sw - A[t].pos;
            }
        if (TB_HAS_ROLL && rep_track >= 0 && RP.n) {  /* NOTE REPEAT's beats */
            uint32_t beat = arp_beat(RP.track);
            if (RP.pos >= beat) {
                RP.pos -= beat;
                if (RP.pos > beat)
                    RP.pos = 0;
                repeat_tick();
            }
            if (beat - RP.pos < len)
                len = beat - RP.pos;
        }
        if (playing && tick_len - tick_pos < len)
            len = tick_len - tick_pos;
        for (i = 0; i < nev; i++)
            if ((int32_t)(ev[i].at - now) > 0 && ev[i].at - now < len)
                len = ev[i].at - now;
        if (!len)
            len = 1;
        {                                             /* the SIDECHAIN's curve over this block */
            /* (22 Nov 2026: a pump that is heard - down in 1 ms, held a 32nd, back up over 3 sixteenths: the next
             *  kick finds it nearly up; it was back up in 2 sixteenths, squared - under a hardcore kick, not heard) */
            uint32_t hold = step_samples() / 2u, rel = step_samples() * 3u + 1u, at = 44u, k;
            static uint8_t dip_flat;                  /* (no kick for a while: the curve all zeros, once) */
            k = 0;
            if (sc_age == 0xFFFFFFFFu) {
                if (!dip_flat)
                    memset(sc_dip, 0, sizeof sc_dip), dip_flat = 1;
                k = len;
            } else
                dip_flat = 0;
            {                                         /* (21 Nov 2026: (a - at) x 32767 / rel stepped on, its quotient
                                                       *  and remainder carried - no division a sample, the same values) */
                uint32_t q = 0, rm = 0, m0 = 0xFFFFFFFFu;
                for (; k < len && k < TB_BLOCK; k++) {
                    uint32_t a = sc_age + k;
                    if (sc_age == 0xFFFFFFFFu || a >= at + hold + rel)
                        sc_dip[k] = 0;
                    else if (a < at)
                        sc_dip[k] = (int32_t)(a * 32767u / at);
                    else if (a < at + hold)
                        sc_dip[k] = 32767;
                    else {
                        uint32_t r, m = a - at - hold;
                        if (m0 != m - 1u || m0 == 0xFFFFFFFFu) {   /* (the first: once, then carried) */
                            uint64_t num = (uint64_t)m * 32767u;
                            q = (uint32_t)(num / rel), rm = (uint32_t)(num - (uint64_t)q * rel);
                        } else {
                            rm += 32767u;
                            while (rm >= rel)
                                rm -= rel, q++;
                        }
                        m0 = m;
                        r = 32767u - q;                   /* 1 .. 0, then squared */
                        sc_dip[k] = (int32_t)((r * r) >> 15);
                    }
                }
            }
            if (sc_age != 0xFFFFFFFFu)
                sc_age = sc_age + (uint32_t)len > 0x7FFFFFFFu ? 0xFFFFFFFFu : sc_age + (uint32_t)len;
        }
        tsplit = fx_pipe && !fx_issued && !c1_late && len >= 16u && tb_engine_mc_avail();
        render_tracks(buf, len, tsplit);
        pf_r0 = PF_T();
        for (pass = 0; pass < 2u; pass++) {           /* (v22) this core's tracks' chains, then the second core's */
        if (pass) {
            uint32_t w0 = PF_T();
            pf0[TB_PROF_CHN] += w0 - pf_r0;
            tracks_join(buf, len, tsplit);
            if (fx_pipe && !fx_issued && done + len >= n) {   /* the second core, then: the last block's buses (once its
                                                       *  tracks are in: in the block's last part) */
                fx_issued = 1;
                fx_sent = c1_late ? -1 : tb_engine_mc_run(fx_job, (uint32_t)(fx_cur ^ 1u));
            }
            pf_r0 = PF_T();
        {                                             /* the DROP's and the RISER's own drums: beside the tracks */
            static int32_t fb[TB_BLOCK];
            memset(fb, 0, len * sizeof *fb);
            tb909_render(&FXD, FXD_P, FXD_ENV, 0, fb, len);
            {                                         /* (their last whisper let go, as the tracks': under -48 dB for
                                                       *  0.1 s, nothing) */
                static uint32_t fq;
                int32_t bp = 0;
                for (i = 0; i < len; i++)
                    bp = fb[i] > bp ? fb[i] : -fb[i] > bp ? -fb[i] : bp;
                fq = bp < 128 ? (fq < 0xFFFF0000u ? fq + len : fq) : 0u;
                if (fq > 4410u)
                    memset(fb, 0, len * sizeof *fb);
            }
            {
                uint32_t any = 0;
                for (i = 0; i < len; i++)
                    any |= (uint32_t)fb[i];
                if (!any && crash_age >= 198450u) {   /* (nothing from them: the same zeros, without the loop) */
                    memset(fxo + done, 0, len * sizeof *fxo), memset(fxr_ + done, 0, len * sizeof *fxr_);
                    goto fxd_done;
                }
            }
            for (i = 0; i < len; i++) {
                int32_t x = fb[i] + fb[i] / 2 + fb[i] / 8, yl = x, yr = x;   /* (x 1.6: over the music) */
                if (crash_age < 198450u) {            /* (the crash's tail: 4.5 s) */
                    int32_t e = crash_tail_env(crash_age++) * (int32_t)TB_FXVOL(TB_FXV_CRASH) / 100, c;
                    for (c = 0; c < 2; c++) {
                        int32_t nz = (int32_t)(tb_noise(&ct_seed[c]) >> 17) - 16384, hp;
                        ct_hp[c] = ((ct_hp[c] + nz - ct_hx[c]) * 22000) >> 15, ct_hx[c] = nz;   /* (~3 kHz up) */
                        hp = ct_hp[c];
                        ct_lp[c] += ((hp - ct_lp[c]) * 26000) >> 15;                         /* (~11 kHz down) */
                        if (c)
                            yr += (int32_t)(((int64_t)ct_lp[c] * e) >> 15);
                        else
                            yl += (int32_t)(((int64_t)ct_lp[c] * e) >> 15);
                    }
                }
                fxo[done + i] = yl, fxr_[done + i] = yr;
                srev[done + i] += x / 3;              /* (a third into the reverb: the crash rings on) */
            }
        fxd_done:;
        }
        pf0[TB_PROF_RIS] += PF_T() - pf_r0;
        pf_r0 = PF_T();
        }
        for (t = 0; t < TB_TRACKS; t++) {
            tb_track_t *tr = &tb_proj.trk[t];
            int32_t pk = 0, lt0, gd = (int32_t)chv(t, TB_LK_DLY, tr->dly) * 258, grv = (int32_t)chv(t, TB_LK_REV, tr->rev) * 258;
            int32_t a1 = gl[t], a2 = gr_[t];
            int32_t rcho = tr->machine == TB_M_SYNTH && tr->p[TBS_CHORUS] ?   /* (every synth sound: the chorus bus) */
                          
                           (int32_t)(((int64_t)((a1 + a2) >> 1) * tr->p[TBS_CHORUS] * 258) >> 15) : 0;
            if (((trk_mask >> t) & 1u) != pass)
                continue;
            lt0 = (int32_t)PF_T();
            if ((int)t == fxk_track && trk_mt[t])     /* (v24) the KICK ROLLs heard at the kick's level, muted or not */
                a1 = go_l[t], a2 = go_r[t];
            {                                         /* (how long each instrument has made no sound at all) */
                unsigned k;
                uint32_t any = 0;
                for (k = 0; k < len; k++)             /* (all of it at once, no branch a sample: the same answer) */
                    any |= (uint32_t)buf[t][k];
                k = any ? 0u : len;
                raw_quiet[t] = k == len ? (raw_quiet[t] < 0xFFFF0000u ? raw_quiet[t] + len : raw_quiet[t]) : 0u;
            }
            if (wash)
                grv = 32767;
            if (echo && pf_at < sps * 4u)
                gd = 32767;                           /* ECHO OUT: everything into the delay for a beat */
            if (raw_quiet[t] <= 4410u)
                dormant[t] = 0;
            else if (dormant[t] && raw_quiet[t] > 4410u + 2048u && !any_fx(t)) {
                /* asleep: silent under -48 dB, its filter and EQ emptied, its chorus line run dry - what follows would
                 * add only zeros (audit 2: the FM-1's time kept for the tracks that play; the sound the same, bit for bit,
                 * tools/host/tests/audit2). Not with an effect on: theirs sweep and hold on in silence */
                if (TB_HAS_CHORUS && tr->chorus)      /* (its chorus's sweep goes on, as if it ran) */
                    cho_w[t] += len, cho_ph[t] += (uint32_t)len * (9800u + (uint32_t)tb_proj.cho_rate[t] * 1600u);
                goto lane;
            }
            {                                         /* its effects, in its order: in line, or beside (on the clean sound) */
                unsigned par = tb_proj.route_par[t], send = tb_proj.route_send[t], any = 0, j, k;
                uint32_t it0;
                for (j = 0; j < TB_NFX; j++)
                    if (tb_engine_fx_on(t, j) && (par >> j & 1u))
                        any = 1;
                if (any || send)
                    memcpy(fx_cln, buf[t], len * sizeof *fx_cln);
                if (any)
                    memset(fx_sum, 0, len * sizeof *fx_sum);
                for (j = 0; j < 5u; j++) {
                    unsigned e = tb_proj.route_ord[t][j];
                    int32_t *x = buf[t];
                    if (e >= TB_NFX || !tb_engine_fx_on(t, e))
                        continue;
                    if (par >> e & 1u)
                        memcpy(fx_tmp, fx_cln, len * sizeof *fx_tmp), x = fx_tmp;
                    it0 = PF_T();
                    if (e == TB_FX_DRIVE || e == TB_FX_EXTRA) {
                        tb_ins_run(&tr->ins[e == TB_FX_EXTRA], &INS[t][e == TB_FX_EXTRA], x, len);
                        if (tr->ins[e == TB_FX_EXTRA].type == TB_INS_PHASER)
                            pf0[TB_PROF_PHA] += PF_T() - it0;
                    }
#if TB_HAS_STACK
                    else {                            /* the stacked ones, each mixed in as much as its AMOUNT */
                        static const uint8_t TY[TB_NSTK] = {TB_INS_FLANGER, TB_INS_PHASER, TB_INS_TREMOLO};
                        unsigned s = e - TB_FX_FLANGER, mx = tb_proj.stk_mix[t][s];
                        tb_ins_set_t c = tb_proj.stk[t][s];
                        c.type = TY[s];
                        memcpy(stk_dry, x, len * sizeof *stk_dry);
                        tb_ins_run(&c, &STK[t][s], x, len);
                        if (mx < 127u) {
                            int32_t mq = (int32_t)mx * 32768 / 127;   /* (Q15, once a block; <= 2^15: tb_mul15 exact) */
                            for (k = 0; k < len; k++)
                                x[k] = stk_dry[k] + tb_mul15(x[k] - stk_dry[k], mq);
                        }
                        if (c.type == TB_INS_PHASER)
                            pf0[TB_PROF_PHA] += PF_T() - it0;
                    }
#endif
                    pf0[TB_PROF_INS] += PF_T() - it0;
                    if (x == fx_tmp)                  /* beside: what it changed, added at the end */
                        for (k = 0; k < len; k++)
                            fx_sum[k] += fx_tmp[k] - fx_cln[k];
                }
                if (any)
                    for (k = 0; k < len; k++)
                        buf[t][k] += fx_sum[k];
            }
            if (TB_HAS_TEQ && (tr->eq[0] != 64 || tr->eq[1] != 64 || tr->eq[2] != 64)) {   /* its EQ: three bands, 0 cuts them */
                int32_t ge[3], lo = eq_lo[t], hi = eq_hi[t];
                for (i = 0; i < 3u; i++)
                    ge[i] = tr->eq[i] < 64 ? (int32_t)tr->eq[i] * tr->eq[i] : 4096 + ((int32_t)tr->eq[i] - 64) * 64;
                for (i = 0; i < len; i++) {
                    int32_t x = buf[t][i];
                    lo += rz15(x - lo, 1167);         /* (rounded towards zero: a floor left the low band stuck */
                    hi += rz15(x - hi, 11670);        /*  a little under 0 for ever, a DC the EQ and the drives raised) */
                    {                                 /* (v22: the 64-bit sum >> 12 in 32 bits, the same value: each
                                                       *  term split into its top and low 12 bits, the low products'
                                                       *  sum under 3 x 2^12 x 2^13; on the FM-1 a 64-bit product is a call) */
                        int32_t u = lo, v = hi - lo, w = x - hi;
                        buf[t][i] = (u >> 12) * ge[0] + (v >> 12) * ge[1] + (w >> 12) * ge[2]
                                    + (((u & 4095) * ge[0] + (v & 4095) * ge[1] + (w & 4095) * ge[2]) >> 12);
                    }
                }
                eq_lo[t] = lo, eq_hi[t] = hi;
            }
            tb_filter_run(&F[t], buf[t], len);
            {                                         /* the lanes' colours: the bass and the treble it holds */
                int32_t lo = band_lo[t], hi = band_hi[t], el = 0, eh = 0;
                for (i = 0; i < len; i += 8) {        /* (a sample in 8: a colour, not a sound; the interrupt's time
                                                       * kept for the sound) */
                    int32_t x = buf[t][i];
                    lo += (int32_t)(((int64_t)(x - lo) * 7000) >> 15);    /* ~200 Hz at 5.5 kHz */
                    el += (lo < 0 ? -lo : lo) >> 1;
                    eh += ((x - hi) < 0 ? hi - x : x - hi) >> 1;          /* (what changed since the last one read: the treble) */
                    hi = x;
                }
                band_lo[t] = lo, band_hi[t] = hi;
                tone_now[t] = (uint8_t)(el + eh > 64 ? (uint32_t)eh * 3u * 255u / (uint32_t)(el + eh * 3 + 1) : tone_now[t]);
            }
            if (raw_quiet[t] > 4410u) {               /* the instrument silent for 0.1 s: what its effects still give out
                                                       * under -48 dB is let go (their states' last rounding, an offset a
                                                       * drive at its top raises: never a hum once the music stops) */
                int32_t bp = 0;
                for (i = 0; i < len; i++)
                    bp = buf[t][i] > bp ? buf[t][i] : -buf[t][i] > bp ? -buf[t][i] : bp;
                if (bp < 128) {                       /* (and its effects' states emptied: a filter swept by the LFO
                                                       *  would otherwise keep pumping its last crumbs back up) */
                    memset(buf[t], 0, len * sizeof buf[t][0]);
                    F[t].lo = F[t].bd = 0, eq_lo[t] = eq_hi[t] = 0;
                    INS[t][0].lo = INS[t][0].bd = INS[t][0].lp = INS[t][0].fb = 0;
                    INS[t][1].lo = INS[t][1].bd = INS[t][1].lp = INS[t][1].fb = 0;
                    memset(INS[t][0].ap, 0, sizeof INS[t][0].ap), memset(INS[t][1].ap, 0, sizeof INS[t][1].ap);
                    dormant[t] = 1;
                }
            }
            {                                         /* (v22: the same sums, each in a loop of its own - no test a
                                                       *  sample: the FM-1's core pays for every branch) */
                const int32_t *sv = buf[t], *dv;
                int32_t gm = a1 > a2 ? a1 : a2;
                int dk = tr->machine != TB_M_KICK && tr->duck;
                for (i = 0; i < len; i++) {
                    int32_t a = sv[i] < 0 ? -sv[i] : sv[i];
                    pk = a > pk ? a : pk;
                }
                if (dk) {                             /* the SIDECHAIN: under the kick */
                    for (i = 0; i < len; i++) {
                        int32_t dg = 32767 - ((sc_dip[i] * tr->duck) >> 7);
                        fx_tmp[i] = tb_mul15(sv[i], dg > 0 ? dg : 0);
                    }
                    sv = fx_tmp;
                }
                dv = sv;
                if (tb_proj.route_send[t]) {          /* the sends beside the effects: the clean sound, ducked the same */
                    dv = fx_cln;
                    if (dk) {
                        for (i = 0; i < len; i++) {
                            int32_t dg = 32767 - ((sc_dip[i] * tr->duck) >> 7);
                            fx_sum[i] = tb_mul15(fx_cln[i], dg > 0 ? dg : 0);
                        }
                        dv = fx_sum;
                    }
                }
#if TB_HAS_CHORUS
                if (tr->chorus) {                     /* the stereo chorus: two reads, opposite, either side */
                    int32_t amt = tr->chorus * 200;
                    uint32_t c0 = 280u * 256u, dep = (40u + tb_proj.cho_depth[t] * 2u) * 256u;   /* (DEPTH: 1 .. 7 ms) */
                    for (i = 0; i < len; i++) {
                        int32_t s = sv[i], lv = tb_sin(cho_ph[t]), wl, wr;
                        cho[t][cho_w[t] & 1023u] = (int16_t)tb_clamp(s, -32768, 32767);
                        wl = cho_tap(t, c0 + mulu_16((uint32_t)(lv + 32768), dep));   /* (v22: in 32 bits, the same) */
                        wr = cho_tap(t, c0 + mulu_16((uint32_t)(32768 - lv), dep));
                        cho_w[t]++, cho_ph[t] += 9800u + (uint32_t)tb_proj.cho_rate[t] * 1600u;   /* (SPEED: 0.1 .. 4.8 Hz) */
                        if (!echo) {
                            mixl[done + i] += tb_mul15(s - ((s * tr->chorus) >> 8) + ((wl * amt) >> 15), a1);
                            mixr[done + i] += tb_mul15(s - ((s * tr->chorus) >> 8) + ((wr * amt) >> 15), a2);
                        }
                    }
                } else
#endif
                if (!echo) {
                    for (i = 0; i < len; i++) {
                        mixl[done + i] += tb_mul15(sv[i], a1);
                        mixr[done + i] += tb_mul15(sv[i], a2);
                    }
                    if (rcho)                         /* (a first-version lead: its chorus send) */
                        for (i = 0; i < len; i++)
                            rcho_in[done + i] += tb_mul15(sv[i], rcho);
                }
                if (gd && grv)
                    for (i = 0; i < len; i++) {
                        int32_t d = tb_mul15(dv[i], gm);
                        sdly[done + i] += tb_mul15(d, gd);
                        srev[done + i] += tb_mul15(d, grv);
                    }
                else if (gd)
                    for (i = 0; i < len; i++)
                        sdly[done + i] += tb_mul15(tb_mul15(dv[i], gm), gd);
                else if (grv)
                    for (i = 0; i < len; i++)
                        srev[done + i] += tb_mul15(tb_mul15(dv[i], gm), grv);
            }
        lane:
            /* the lane's waveform: the loop column by column as the playhead passes: its peak level (the screen
             * scales each lane to its loudest: the hits stand out, the tails taper) */
            if (playing) {
                uint32_t tps = ticks_per_step(t), Ln = track_len(t);
                uint32_t into = (gtick % tps) * tick0 + tick_pos;
                uint32_t col = (uint32_t)(((uint64_t)tstep[t] * tps * tick0 + into) * TB_WAVE_W / ((uint64_t)Ln * tps * tick0));
                uint8_t v = 0;
                if (pk > 64 && !muted(t))
                    v = (uint8_t)(pk >= 32767 ? 255 : (uint32_t)pk * 255u / 32767u);
                if (col >= TB_WAVE_W)
                    col = TB_WAVE_W - 1;
                if (col != wcol[t]) {
                    uint32_t c = (wcol[t] + 1u) % TB_WAVE_W, guard = 0;
                    while (c != col && guard++ < TB_WAVE_W) {
                        wave[t][c] = v, heat[t][c] = tone_now[t];
                        c = (c + 1u) % TB_WAVE_W;
                    }
                    wave[t][col] = 0, heat[t][col] = tone_now[t];
                    wcol[t] = (uint16_t)col;
                }
                if (v > wave[t][col])
                    wave[t][col] = v, heat[t][col] = tone_now[t];
                wpos[t] = (uint16_t)col;
            }
            {
                uint8_t lv = (uint8_t)(pk > 32767 ? 255 : (uint32_t)pk * 255u / 32767u);
                level[t] = lv > level[t] ? lv : (uint8_t)(level[t] - (level[t] > 8u ? level[t] >> 3 : level[t] ? 1u : 0u));   /* (audit 2:
                                                       * down to 0, not stuck at 7 where level / 8 rounds to nothing) */
            }
            lane_cost[t] = blk_worth(PF_T() - (uint32_t)lt0, len);
        }
        }
        pf0[TB_PROF_CHN] += PF_T() - pf_r0;
        done += len;
        now += len;
        for (t = 0; t < TB_TRACKS; t++)
            A[t].pos += len;
        RP.pos += len;
        if (playing) {
            tick_pos += len;
            if (tick_pos >= tick_len) {
                tick_pos = 0;
                gtick++, ptick++;
                do_tick();
            }
        }
    }
    {
        uint32_t c0 = PF_T();
        rave_chorus(mixl, mixr, n);
        pf0[TB_PROF_CHO] += PF_T() - c0;
    }
    if (fx_pipe && fx_sent < 0) {
        /* (the second core late, still at its job: the buses not run this block - their sends let go, nothing of
         *  theirs added; nothing it reads or writes touched here) */
    } else if (fx_pipe) {                             /* this block's sends kept for the next; the last block's buses in */
        unsigned k = fx_cur;
        int r = 0;
        memcpy(fxs_d[k], sdly, n * sizeof *sdly), memcpy(fxs_r[k], srev, n * sizeof *srev);
        fxa[k].n = n, fxa[k].sps = sps, fxa[k].wash = (uint8_t)(wash != 0), fxa[k].hold = (uint8_t)(echo && pf_at >= sps * 4u);
        fxa[k].ok = 1;
        if (!fx_sent)
            fx_job(k ^ 1u);                           /* (no second core this block: here) */
        else {
            uint32_t w0 = PF_T();
            r = tb_engine_mc_wait(92u);
            pf0[TB_PROF_WAIT] += PF_T() - w0;
            if (r < 0)
                fx_job(k ^ 1u), r = 0;                /* (given up, stopped: here) */
        }
        if (r == 0)
            for (i = 0; i < n; i++)
                mixl[i] += fxw_l[k ^ 1u][i], mixr[i] += fxw_r[k ^ 1u][i];
        else
            c1_late = 2, tb_c1_late_count++;          /* (late: its wet of this block not added; it finishes on its own) */
        fx_cur = (uint8_t)(k ^ 1u);
    } else
        tb_fx_run(&tb_proj.fx, sdly, srev, mixl, mixr, n, sps, wash, echo && pf_at >= sps * 4u);
    pf_r0 = PF_T();
    {
        static int64_t dcs[2][2];
        static uint32_t quiet;
        int32_t pk = 0;
        dc_block(mixl, n, dcs[0]);                    /* (before the master: its drive never sees an offset) */
        dc_block(mixr, n, dcs[1]);
        for (i = 0; i < n; i++) {
            int32_t a = mixl[i] < 0 ? -mixl[i] : mixl[i], b = mixr[i] < 0 ? -mixr[i] : mixr[i];
            pk = a > pk ? a : pk, pk = b > pk ? b : pk;
        }
        /* and no sound at all once what is left is under -60 dB for 50 ms (the tails' last whisper, that the master's
         * compressor, drive and fuzz at their top would otherwise raise into a hiss after the music has stopped) */
        quiet = pk < 33 ? quiet + n : 0u;
        if (quiet > 2205u)
            memset(mixl, 0, n * sizeof *mixl), memset(mixr, 0, n * sizeof *mixr);
    }
    if (TB_HAS_MASTER)
        tb_fx_master(&tb_proj.fx, mixl, mixr, n);
    tb_fx_dist(&tb_proj.fx, mixl, mixr, n);
    tb_filter_run(&MF[0], mixl, n);
    tb_filter_run(&MF[1], mixr, n);
    for (i = 0; i < n; i++)                           /* (the DROP's own drums: after the filters, heard as they are) */
        mixl[i] += fxo[i], mixr[i] += fxr_[i];
    {
        uint32_t x0 = PF_T();
        extras(mixl, mixr, n, sps);
        x0 = PF_T() - x0;
        pf0[TB_PROF_RIS] += x0, pf_r0 += x0;          /* (the master's time without them) */
    }
    {
        static int64_t dco[2][2];
        dc_block(mixl, n, dco[0]);                    /* (and after it: the drive's own offset, the extras') */
        dc_block(mixr, n, dco[1]);
    }
    if (pf(TB_PF_STUTTER)) {                          /* STUTTER: the sound cut on every other 32nd */
        for (i = 0; i < n; i++) {
            uint32_t ph = (now - n + i) % (sps / 2u ? sps / 2u : 1u);
            if (ph >= sps / 4u)
                mixl[i] = mixr[i] = 0;
        }
    }
    /* the ceiling: a limiter that sees the whole block before it plays it (2.9 ms ahead): the gain falls to what
     * keeps the block's peak at the ceiling within its first 16 samples, and comes back over ~200 ms. The ceiling:
     * -1 dBFS; the FM-1 -3 dBFS (13 Nov 2026: its output saturated easily; RAVE FM 2 too) */
    {
        int32_t pk = 0, target;
        static int32_t g = 32767;                           /* Q15 */
        for (i = 0; i < n; i++) {
            int32_t a = mixl[i] < 0 ? -mixl[i] : mixl[i], b = mixr[i] < 0 ? -mixr[i] : mixr[i];
            if (a > pk)
                pk = a;
            if (b > pk)
                pk = b;
        }
#if TB_LITE || defined(RFM2)
        enum { CEIL = 23197, KNEE = 25500 };
#else
        enum { CEIL = 29204, KNEE = 31000 };
#endif
        target = pk > CEIL ? (int32_t)(CEIL * 32767 / pk) : 32767;   /* (CEIL x 32767 < 2^31: 32 bits) */
        if (!pk && g == 32767) {                      /* (silence, the gain at rest: zeros, the same) */
            memset(out, 0, 2u * n * sizeof *out);
            n = 0;
        }
        for (i = 0; i < n; i++) {
            int32_t s, sr;
            if (target < g)
                g -= (g - target + 15) / (int32_t)(i < 16u ? 16u - i : 1u);
            else
                g += (32767 - g) >> 12;
            s = tb_knee(tb_mul15(mixl[i], g), KNEE);       /* (g <= 32767: as the 64-bit product - tb_dsp.h) */
            sr = tb_knee(tb_mul15(mixr[i], g), KNEE);
            out[2 * i] = tb_clamp(s, -32767, 32767) * 256;  /* Q15 -> Q23 */
            out[2 * i + 1] = tb_clamp(sr, -32767, 32767) * 256;
        }
    }
    for (t = 0; t < TB_TRACKS; t++)
        if (active[t])
            active[t]--;
    (void)cpu_busy, (void)cpu_total;
    pf0[TB_PROF_MST] += PF_T() - pf_r0;
    pf0[TB_PROF_C0] = PF_T() - pf_t0;
    if (!tape_in && n)                                /* (the last frame heard: where a TAPE STOP starts from) */
        tape_src[0] = out[2 * n - 2], tape_src[1] = out[2 * n - 1];
    __asm__ volatile("" ::: "memory");
    rendering = 0;
}

unsigned tb_engine_step(unsigned t, unsigned *frac)
{
    uint32_t tick0 = step_samples() / 2u, tps;
    if (t >= TB_TRACKS)
        return 0;
    tps = ticks_per_step(t);
    if (frac)
        *frac = playing ? (unsigned)((((gtick % tps) * tick0 + tick_pos) * 255u) / (tps * tick0 + 1u)) : 0u;
    return tstep[t];
}

void tb_engine_view(tb_view_t *v, unsigned track)
{
    unsigned t, st = 0;
    uint32_t sps = step_samples();
    const tb_track_t *tr;
    v->playing = playing;
    v->bpm10 = tb_proj.bpm10;
    v->pattern = tb_proj.pattern;
    v->bar_beat = (uint8_t)((gtick / 8u) % 4u);
    v->beats = playing && bar_count != (uint32_t)-1 ? bar_count * 4u + (gtick / 8u) % 4u : 0u;
    v->beat_phase = (uint16_t)((((gtick % 8u) * (sps / 2u) + tick_pos) * 1023u) / (4u * sps));
    v->cpu = (uint8_t)tb_engine_cpu();
    for (t = 0; t < TB_TRACKS; t++) {
        tb_lane_t *l = &v->lane[t];
        memcpy(l->name, tb_proj.trk[t].name, sizeof l->name);
        memcpy(l->wave, wave[t], TB_WAVE_W);
        memcpy(l->heat, heat[t], TB_WAVE_W);
        l->pos = wpos[t];
        l->muted = (uint8_t)muted(t);
        l->level = level[t];
        l->active = active[t] != 0;
        l->hits = hit_count[t];
        l->machine = tb_proj.trk[t].machine;
    }
    if (track >= TB_TRACKS)
        return;
    tr = &tb_proj.trk[track];
    /* the envelope of the selected track, where it is now */
    switch (tr->machine) {
    case TB_M_KICK: v->env.lvl = V[track].env.lvl, st = V[track].env.stage; break;
    case TB_M_909: {
        unsigned b, best = 0;
        for (b = 0; b < TB909_PIECES; b++)
            if (V[track].d909.env[b].lvl > V[track].d909.env[best].lvl)
                best = b;
        v->env.lvl = V[track].d909.env[best].lvl, st = V[track].d909.env[best].stage;
        break;
    }
    case TB_M_BREAK: case TB_M_SAMPLE: v->env.lvl = tb_brk_env(&V[track].brk, &st); break;
#if TB_HAS_SYNTH
    case TB_M_SYNTH: v->env.lvl = tb_syn_env(&V[track].syn, v->menu_page == 1, &st); break;
#endif
#if TB_HAS_ACID
    case TB_M_ACID: v->env.lvl = tb_acid_env(&V[track].acid, &st); break;
#endif
    default: v->env.lvl = 0;
    }
    v->env.stage = (uint8_t)st;
    v->lfo.value = (int16_t)tb_clamp(L[track].v, -32767, 32767);
    v->lfo.phase = (uint16_t)(L[track].ph >> 16);
    {                                                 /* the arpeggiator */
        arp_t *a = &A[track];
        unsigned k;
        v->arp.nheld = a->n;
        for (k = 0; k < a->n; k++)
            v->arp.held[k] = a->held[k];
        v->arp.nseq = (uint8_t)arp_order(track, v->arp.seq);
        v->arp.pos = (uint8_t)(v->arp.nseq ? (a->idx + v->arp.nseq - 1u) % v->arp.nseq : 0);
        v->arp.sounding = a->on;
        v->arp.rstep = (uint8_t)((a->rstep + 7u) & 7u);
    }
}

static void voices_init(unsigned t)
{
    tb_kick_init(&V[t].kick);
    tb909_init(&V[t].d909);
#ifdef RFM2
    tb_dx_init(&V[t].dx);
#endif
    tb_brk_init(&V[t].brk);
    if (TB_HAS_SYNTH)
        tb_syn_init(&V[t].syn);
    if (TB_HAS_ACID)
        tb_acid_init(&V[t].acid);
    memset(&V[t].env, 0, sizeof V[t].env);
    memset(&V[t].envk, 0, sizeof V[t].envk);
}

void tb_engine_init(void)
{
    unsigned t;
    tb_dsp_init();
    for (t = 0; t < TB_TRACKS; t++) {
        voices_init(t), vm[t] = 0;
        memset(&F[t], 0, sizeof F[t]);
        tb_ins_init(&INS[t][0]), tb_ins_init(&INS[t][1]);
#if TB_HAS_STACK
        tb_ins_init(&STK[t][0]), tb_ins_init(&STK[t][1]), tb_ins_init(&STK[t][2]);
#endif
        skip_step[t] = -1;
    }
    memset(MF, 0, sizeof MF);
    tb909_init(&FXD), fxk_track = -1;
    tb_fx_init();
    memset(wave, 0, sizeof wave);
    memset(A, 0, sizeof A);
    memset(L, 0, sizeof L);
}

static void name(tb_track_t *t, const char *n)
{
    memset(t->name, 0, sizeof t->name);
    strncpy(t->name, n, sizeof t->name - 1);
}

/* a break's loop on track t of pattern pt: its slices in order, one a step (the Amen's: one an eighth) */
void tb_engine_break_loop(unsigned pt, unsigned t)
{
    tb_track_t *tr = &tb_proj.trk[t];
    tb_pattern_t *P = &tb_proj.pat[pt];
    unsigned b = tr->p[TBB_SRC], bars = tb_brk_bars(b), s, n, per;
    uint32_t a0, b0;
    n = tb_brk_slices(tr->p, &tr->man, 0, &a0, &b0);
    if (tr->p[TBB_DIV] >= TBB_AUTO)                  /* AUTO, MAN: a slice a 16th, as many as the break holds */
        per = 1;
    else
        per = bars * 16u / n ? bars * 16u / n : 1u;
    P->len[t] = (uint8_t)(bars * 16u > TB_TCAP(t) ? TB_TCAP(t) : bars * 16u);
    memset(TB_TSTEPS(P, t), 0, TB_TCAP(t) * sizeof(tb_step_t));
    for (s = 0; s < P->len[t]; s += per) {
        TB_TSTEPS(P, t)[s].hits = 1;
        TB_TSTEPS(P, t)[s].note = (uint8_t)(60u + (s / per) % n);
        TB_TSTEPS(P, t)[s].gate = (uint8_t)(per * 8u > 255u ? 255u : per * 8u);
    }
}

/* (the melodic machines this build has: the SYNTH, else the ACID, else none: the 909) */
#define M_MEL (TB_HAS_SYNTH ? TB_M_SYNTH : TB_HAS_ACID ? TB_M_ACID : TB_M_909)
#define M_MEL2 (TB_HAS_ACID ? TB_M_ACID : M_MEL)
void tb_engine_default_track(unsigned t)
{
    static const struct { uint8_t m; const char *n; } T[TB_TRACKS] = {
#ifdef RFM2
        {TB_M_KICK, "KICK"}, {TB_M_BREAK, "BREAK"}, {TB_M_909, "909"}, {M_MEL, "BASS"}, {M_MEL, "LEAD"},
        {M_MEL, "STAB/HV"}};                          /* (no SAMPLE: the stabs and the hoovers on one track) */
#else
        {TB_M_KICK, "KICK"}, {TB_M_BREAK, "BREAK"}, {TB_M_909, "909"}, {M_MEL, "BASS"},
        {M_MEL, "LEAD"}, {M_MEL, "STAB"}, {M_MEL, "HOOVER"},
#if TB_HAS_SAMPLE
        {TB_M_SAMPLE, "SAMPLE"}};
#else
        {M_MEL2, "ACID"}};                            /* (the FM-1: no SAMPLE machine) */
#endif
#endif
    tb_track_t *tr = &tb_proj.trk[t];
    unsigned s;
    memset(tr, 0, sizeof *tr);
    tr->machine = T[t].m;
    name(tr, T[t].n);
    tr->vol = 100;
    tr->filt = 64;
    tr->pan = 64;
    tr->eq[0] = tr->eq[1] = tr->eq[2] = 64;
    tr->speed = TB_SPEED_1;
    tr->arp_rate = 2, tr->arp_gate = 80, tr->arp_oct = 1;
    tr->lfo_rate = 40, tr->lfo_sync = 1, tr->lfo_dest = TB_DEST_FILTRE;
    for (s = 0; s < 16; s++)
        tr->p[s] = 64;
    /* the envelope: a drum's untouched (A 1 ms, D 127: as long as it rings, S 0, R 127); DECAY down: shorter */
    tr->p[TB_P_ENV + 0] = 7, tr->p[TB_P_ENV + 1] = 127, tr->p[TB_P_ENV + 2] = 0, tr->p[TB_P_ENV + 3] = 127;   /* (v24: ATTACK
                                                       *  7 = 0.97 ms out of the box, no click at the start; 0 on the knob) */
    switch (tr->machine) {
    case TB_M_KICK:
        memcpy(tr->p, TB_KICK_PRESETS[1].p, TB_KICK_NPARAM);
        break;
#if TB_HAS_SYNTH
    case TB_M_SYNTH: {
#ifdef RFM2
        unsigned b = t == 3 ? 0u : t == 4 ? 1u : 2u;  /* (BASS, LEAD, STAB/HV: a stab first; the hoovers in its list) */
#else
        unsigned b = t == 3 ? 0u : t == 4 ? 1u : t == 5 ? 2u : 3u;
#endif
        const tb_syn_sound_t *snd = &TBS_SOUND[tb_syn_default(b)];
        memcpy(tr->p, snd->p, TBS_NPARAM);
        tr->chord = 0;                                /* (chords: none until SCL asks) */
        tr->duck = 50;
        tr->rev = b == 2 ? 30 : b == 1 ? 22 : 8;
        tr->dly = b == 1 || b == 2 ? 20 : 0;
        tr->lfo_dest = b == 3 ? TB_DEST_PITCH : TB_DEST_CUTOFF;
        tr->chorus = b == 3 ? 90 : b == 2 ? 50 : b == 1 ? 30 : 0;   /* (the hoover wide, the stab and lead a little) */
#ifdef RFM2
        tr->duck = 0;                                 /* (RAVE FM 2, 19 Nov 2026: out of the box no sidechain) */
#endif
        break;
    }
#endif
#if TB_HAS_ACID
    case TB_M_ACID:                                   /* (the 303's own sound) */
        memset(tr->p, 0, sizeof tr->p);
        tb_acid_sound(tr->p, 0);                      /* (its gate as tb_acid_sound sets it: on while the note is) */
        tr->lfo_dest = TB_DEST_CUTOFF;
        tr->duck = 50;
        break;
#endif
#ifdef RFM2
    case TB_M_909:                                    /* (RAVE FM 2: DRUM-X's 909 CLUB, each piece as the kit has it) */
        memset(tr->p, 0, 24);
        memset(&tr->man, 0, sizeof tr->man);
        tb_dx_defaults(tr->p, (uint8_t *)tr->man.at);
        tr->duck = 0;
        break;
#endif
    case TB_M_BREAK:
    case TB_M_SAMPLE:
        memset(tr->p, 0, 16);
        tr->p[TBB_SRC] = 0;
        tr->p[TBB_DIV] = (uint8_t)tb_brk_default_div(0);
        tr->p[TBB_MODE] = tr->machine == TB_M_SAMPLE ? TBB_GATE : TBB_FULL;   /* a break: a key launches it (one-shot) */
        tr->p[TBB_PITCH] = 24;
        tr->p[TBB_DECAY] = 127;
        tr->p[TBB_TONE] = 127;
        tr->p[TBB_REL] = tr->machine == TB_M_BREAK ? 40 : 20;
        tr->p[TBB_SYNC] = tr->machine == TB_M_BREAK;
        tr->p[10] = 127;                              /* SAMPLE: LONG, all of it */
        if (tr->machine == TB_M_SAMPLE)               /* (a sample held: as long as the key) */
            tr->p[TB_P_ENV + 1] = 64, tr->p[TB_P_ENV + 2] = 127, tr->p[TB_P_ENV + 3] = 30;
        tr->duck = 30;
#ifdef RFM2
        tr->duck = 0;                                 /* (out of the box: no sidechain) */
#endif
        break;
    default:
        break;
    }
}

void tb_engine_stack_defaults(void)
{
    unsigned t;
    static const uint8_t K[TB_NSTK][3] = {{40, 90, 80}, {30, 100, 60}, {70, 127, 0}};
    for (t = 0; t < TB_TRACKS; t++) {
        memset(tb_proj.stk[t], 0, sizeof tb_proj.stk[t]);
        memset(tb_proj.stk_mix[t], 0, sizeof tb_proj.stk_mix[t]);
        for (unsigned i = 0; i < TB_NSTK; i++)
            memcpy(tb_proj.stk[t][i].p, K[i], 3);
        tb_proj.cho_rate[t] = 26, tb_proj.cho_depth[t] = 65;   /* (the chorus of version 6) */
    }
}

int tb_engine_fx_on(unsigned t, unsigned e)
{
    const tb_track_t *tr = &tb_proj.trk[t];
    return e == TB_FX_DRIVE ? tr->ins[0].type != 0 : e == TB_FX_EXTRA ? tr->ins[1].type != 0 :
           e < TB_NFX ? TB_HAS_STACK && tb_proj.stk_mix[t][e - TB_FX_FLANGER] != 0 : 0;
}

void tb_engine_route_defaults(void)
{
    unsigned t, j;
    for (t = 0; t < TB_TRACKS; t++) {
        for (j = 0; j < 5u; j++)
            tb_proj.route_ord[t][j] = (uint8_t)j;
        tb_proj.route_par[t] = 0, tb_proj.route_send[t] = 0;
    }
}

void tb_engine_upgrade(unsigned from)
{
    unsigned t, k;
    if (from >= 9u)
        goto v9;
    if (from >= 8u)
        goto v8;
    if (from >= 7u)
        goto v7;
    tb_engine_stack_defaults();
    for (t = 0; t < TB_TRACKS; t++)                   /* a movement in an insert: to its own page now */
        for (k = 0; k < 2u; k++) {
            tb_ins_set_t *in = &tb_proj.trk[t].ins[k];
            int s = in->type == TB_INS_FLANGER ? TB_STK_FLANGER : in->type == TB_INS_PHASER ? TB_STK_PHASER :
                    in->type == TB_INS_TREMOLO ? TB_STK_TREMOLO : -1;
            if (s >= 0) {
                memcpy(tb_proj.stk[t][s].p, in->p, 3), tb_proj.stk_mix[t][s] = 127;
                if (s == TB_STK_TREMOLO)
                    tb_proj.stk_mix[t][s] = in->p[1], tb_proj.stk[t][s].p[1] = 127;
            } else if (in->type == TB_INS_CHORUS) {
                if (tb_proj.trk[t].chorus < in->p[2])
                    tb_proj.trk[t].chorus = in->p[2];
            } else
                continue;
            memset(in, 0, sizeof *in);
        }
v7:
    tb_engine_route_defaults();
v8:                                                   /* (what an older project lacks: zeros, not what was there) */
    tb_proj.riser_type = 0;
    memset(tb_proj.riser_lvl, 0, sizeof tb_proj.riser_lvl);
    memset(tb_proj.spare, 0, sizeof tb_proj.spare);
v9:
    tb_proj.version = TB_VERSION;
}

void tb_engine_widen_steps(unsigned old_step)
{
    uint8_t *b = (uint8_t *)&tb_proj;
    /* (into version 11's layout: every track TB_OLD_STEPS; tb_engine_relayout then gives the melodies their room) */
    const uint32_t nw = (uint32_t)sizeof(tb_step_t), ns = TB_OLD_NSTEP;
    const uint32_t at = (uint32_t)offsetof(tb_project_t, pat), np = sizeof(tb_pattern_t) - (TB_NSTEP - TB_OLD_NSTEP) * nw;
    const uint32_t ss = (uint32_t)offsetof(tb_pattern_t, step), ls = ss + ns * nw;
    const uint32_t grow = nw - old_step;
    const uint32_t op = np - ns * grow, tail = (uint32_t)sizeof tb_proj - TB_RELAYOUT - at - TB_PATTERNS * np;   /* (op: a pattern then) */
    int p, i;
    memmove(b + at + TB_PATTERNS * np, b + at + TB_PATTERNS * op, tail);      /* what comes after the patterns */
    for (p = TB_PATTERNS - 1; p >= 0; p--) {                                  /* (from the end: nothing not moved yet overwritten) */
        uint8_t *nb = b + at + (uint32_t)p * np, *ob = b + at + (uint32_t)p * op;
        memmove(nb + ls, ob + ls - ns * grow, sizeof(tb_lock_t) * TB_NLOCK);
        for (i = (int)ns - 1; i >= 0; i--) {
            memmove(nb + ss + (uint32_t)i * nw, ob + ss + (uint32_t)i * old_step, old_step);
            memset(nb + ss + (uint32_t)i * nw + old_step, 0, grow);
        }
        memmove(nb, ob, ss);
    }
}

/* a version 11 project (every track TB_OLD_STEPS steps) to version 12's layout (the melodies TB_STEPS): each pattern
 * bigger, its tracks' steps apart, from the end so nothing is overwritten before it has moved */
void tb_engine_relayout(void)
{
#if TB_NSTEP != TB_OLD_NSTEP
    uint8_t *b = (uint8_t *)&tb_proj;
    const uint32_t nw = (uint32_t)sizeof(tb_step_t), at = (uint32_t)offsetof(tb_project_t, pat), np = sizeof(tb_pattern_t);
    const uint32_t ss = (uint32_t)offsetof(tb_pattern_t, step), ls = (uint32_t)offsetof(tb_pattern_t, lock);
    const uint32_t op = np - (TB_NSTEP - TB_OLD_NSTEP) * nw, tail = (uint32_t)sizeof tb_proj - at - TB_PATTERNS * np;
    int p, t;
    memmove(b + at + TB_PATTERNS * np, b + at + TB_PATTERNS * op, tail);
    for (p = TB_PATTERNS - 1; p >= 0; p--) {
        uint8_t *nb = b + at + (uint32_t)p * np, *ob = b + at + (uint32_t)p * op;
        memmove(nb + ls, ob + ss + TB_OLD_NSTEP * nw, sizeof(tb_lock_t) * TB_NLOCK);
        for (t = TB_TRACKS - 1; t >= 0; t--) {
            uint32_t keep = TB_TCAP(t) < TB_OLD_STEPS ? TB_TCAP(t) : TB_OLD_STEPS;
            memmove(nb + ss + TB_SOFF(t) * nw, ob + ss + (uint32_t)t * TB_OLD_STEPS * nw, keep * nw);
            memset(nb + ss + (TB_SOFF(t) + keep) * nw, 0, (TB_TCAP(t) - keep) * nw);
        }
        memmove(nb, ob, ss);
    }
#endif
}

void tb_engine_default_project(void)
{
    unsigned t, s, pt;
    memset(&tb_proj, 0, sizeof tb_proj);
    tb_proj.magic = TB_MAGIC, tb_proj.version = TB_VERSION;
    tb_proj.bpm10 = 1800;
    tb_proj.root = 0;                                 /* (no scale: every note, as it comes out of the box) */
    tb_proj.scale = 0;
    tb_proj.fx.rev_type = 1, tb_proj.fx.rev_size = 70, tb_proj.fx.rev_damp = 60, tb_proj.fx.rev_level = 100;
    tb_proj.fx.dly_time = 2, tb_proj.fx.dly_fb = 60, tb_proj.fx.dly_tone = 60, tb_proj.fx.dly_level = 100;
    tb_proj.fx.eq_low = tb_proj.fx.eq_mid = tb_proj.fx.eq_high = 64, tb_proj.fx.width = 64;
    tb_proj.drop_bars = 2, tb_proj.drop_roll = tb_proj.drop_filter = tb_proj.drop_crash = 1;
    tb_proj.mut_amount = 70, tb_proj.mut_fills = 0;
    tb_proj.metro_vol = 0, tb_proj.midi_out = 1;     /* (metro_vol: the count-in, TB_COUNTIN) */
    tb_proj.riser_bars = 2, tb_proj.riser_filter = 1, tb_proj.riser_gap = 1, tb_proj.riser_impact = 1;
    tb_proj.riser_lvl[TB_RISER_CLASSIC] = 100;
    tb_proj.fx.dist_gain = 64, tb_proj.fx.dist_tone = 90, tb_proj.fx.dist_mix = 127;
    memset(tb_proj.song_pat, TB_PATTERNS, sizeof tb_proj.song_pat);
    tb_proj.song_pat[0] = 0;
    for (s = 0; s < TB_SONG; s++)
        tb_proj.song_bars[s] = 4;
    for (t = 0; t < TB_TRACKS; t++)
        tb_engine_default_track(t);
    tb_engine_stack_defaults();
    tb_engine_route_defaults();
    for (pt = 0; pt < TB_PATTERNS; pt++)
        for (t = 0; t < TB_TRACKS; t++)
            tb_proj.pat[pt].len[t] = 16;
    /* as it comes out of the box: every pattern empty, no scale, no chord, no arpeggiator (the sounds ready) */
#ifndef RFM2
    tb_proj.trk[2].duck = 40;
    tb_proj.trk[2].rev = 18;
#endif
    tb_proj.trk[1].vol = 84;
    (void)s;
}

void tb_engine_set_machine(unsigned t, unsigned machine)
{
#ifdef RFM2
    static const uint8_t SLOT[TB_NMACHINE] = {0, 0, 1, 2, 5, 5, 3};   /* the track whose defaults a machine takes (six
                                                                     * tracks: the 909 its own, below) */
#else
    static const uint8_t SLOT[TB_NMACHINE] = {0, 0, 1, 2, 3, 7, 3};   /* the track whose defaults a machine takes */
#endif
    _Static_assert(TB_TRACKS > 5, "SLOT: the tracks it names");
    static const char *const N[TB_NMACHINE] = {"", "KICK", "BREAK", "909", "SYNTH", "SAMPLE", "ACID"};
    tb_track_t keep, other, made;
    unsigned s;
    if (t >= TB_TRACKS || machine < TB_M_KICK || machine >= TB_NMACHINE)
        return;
    s = SLOT[machine];
    keep = tb_proj.trk[t], other = tb_proj.trk[s];
    tb_engine_default_track(s);                       /* (made on that slot, then moved here) */
    made = tb_proj.trk[s];
    tb_proj.trk[s] = other;
#ifdef RFM2
    if (machine == TB_M_909) {                        /* (RAVE FM 2: the 909's own sounds on whatever slot) */
        made.machine = TB_M_909;
        memset(made.p, 0, 24);
        memset(&made.man, 0, sizeof made.man);
        tb_dx_defaults(made.p, (uint8_t *)made.man.at);
        made.chord = 0, made.arp = 0;
    }
#endif
    if (TB_HAS_ACID && machine == TB_M_ACID) {        /* (the synth's channel, the 303's sound) */
        made.machine = TB_M_ACID, made.chord = 0, made.arp = 0;
        memset(made.p, 0, sizeof made.p);
        tb_acid_sound(made.p, 0);
        made.lfo_dest = TB_DEST_CUTOFF, made.lfo_depth = 0;
    }
    made.vol = keep.vol, made.filt = keep.filt, made.rev = keep.rev, made.dly = keep.dly, made.pan = keep.pan;
    made.muted = keep.muted, made.solo = keep.solo;
    memcpy(made.eq, keep.eq, sizeof made.eq);
    made.chorus = keep.chorus;
    memset(made.name, 0, sizeof made.name);
    strncpy(made.name, N[machine], sizeof made.name - 1);
    tb_proj.trk[t] = made;
}

#ifdef RFM_HOST
void tb_engine_debug(void)
{
    unsigned t, i, sv, bv;
    fprintf(stderr, "ENGINE pf %08x drop %u riser %u mut %u repeat %u playing %d |", (unsigned)pf_mask, drop_on, riser_on, mut_on,
            RP.n, playing);
    for (t = 0; t < TB_TRACKS; t++) {
        for (i = sv = 0; i < TBS_VOICES; i++)
            sv += V[t].syn.v[i].live != 0;
        for (i = bv = 0; i < TBB_VOICES; i++)
            bv += V[t].brk.v[i].live != 0;
        fprintf(stderr, " t%u m%u arp%u/%u syn%u brk%u", t, tb_proj.trk[t].machine, A[t].n, A[t].on, sv, bv);
        if (tb_proj.trk[t].machine == TB_M_KICK)      /* (the kick's two hits: live, release, level, time) */
            for (i = 0; i < 2u; i++) {
                const uint32_t *h = V[t].kick.a.h[i];
                fprintf(stderr, " k%u[live%u rel%u lvl%d t%u]", i, (unsigned)(h[24] & 255u), (unsigned)((h[24] >> 16) & 255u),
                        (int)h[7], (unsigned)h[0]);
            }
    }
    fprintf(stderr, "\n");
    {
        const char *pd = getenv("RFM_PROJDUMP");     /* (the project, raw: to compare two runs) */
        FILE *f = pd ? fopen(pd, "wb") : NULL;
        if (f)
            fwrite(&tb_proj, sizeof tb_proj, 1, f), fwrite(V, sizeof V, 1, f), fclose(f);
    }
}
#endif
