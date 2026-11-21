/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: what the screen shows, drawn from a snapshot of the instrument (tb_view_t). */
#ifndef TB_UI_H
#define TB_UI_H
#include "tb_features.h"
#include <stdint.h>

#ifdef RFM2
#define TB_TRACKS 6              /* RAVE FM 2: KICK BREAK BASS LEAD SYNTH SAMPLE (Maxime, 14 Nov 2026: no 909, STAB and HOOVER one) */
#else
#define TB_TRACKS 8
#endif
#define TB_WAVE_W 200            /* the waveform lanes' columns: one loop of the track */

/* the tracks' pictures */
enum { TB_ICON_KICK, TB_ICON_KIT, TB_ICON_909, TB_ICON_BASS, TB_ICON_LEAD, TB_ICON_STAB, TB_ICON_HOOVER,
       TB_ICON_SAMPLE, TB_NICON };

typedef struct {
    char name[8];
    uint8_t muted, solo, active;  /* active: a hit in the last ~100 ms */
    uint8_t level;                /* 0..255 */
    uint16_t hits;                /* (RAVE FM 2) its notes so far (each one changes it: the screen misses none) */
    uint8_t machine, icon, beats; /* beats: how many in its loop (the ticks in the lane) */
    uint8_t wave[TB_WAVE_W];      /* the loop's levels (dB scale), 0..255 */
    uint8_t heat[TB_WAVE_W];      /* .. and its attacks, 0..255 */
    uint16_t pos;                 /* the playhead in this track's loop, 0..TB_WAVE_W-1 */
} tb_lane_t;

/* the step sequencer's screen: one page of 16 steps of the selected track */
enum { TB_SEQ_PADS, TB_SEQ_GRID, TB_SEQ_NOTES, TB_SEQ_SLICES };
typedef struct {
    uint16_t hits;
    uint8_t note, vel, gate, flags;   /* flags: the step's (chance, repeats, ACID's accent 0x40 and slide 0x80) */
    uint8_t lock;                     /* it has parameter locks */
    uint8_t more[3];                  /* a chord set by hand: its other notes (0: none) */
} tb_vstep_t;
typedef struct {
    uint8_t kind;                 /* TB_SEQ_*: a pad per step (the kick), the 909's grid, notes, slices */
    uint8_t page, npages, len;    /* the 16 steps the keys edit (a page), how many pages, the loop's length */
    uint8_t win, wlen;            /* the screen: the steps from win, wlen of them (0: the whole loop) */
    const char *grid;             /* the kick's and the 909's grid (1/8, 1/16, 1/32: what the whites are), 0: none */
    int16_t play;                 /* the step playing (-1: stopped) */
    uint8_t play_page;            /* the page the playhead is on */
    int16_t held;                 /* the step held (-1: none) */
    int16_t cursor;               /* NOTES: the step the knobs edit (-1: none) */
    int8_t cur_k;                 /* EDIT: which of its notes (0: its note, 1, 2: a chord's others; -1: all) */
    uint8_t cur_w;                /* NOTES: the next note's length, in steps (step by step) */
    const char *val;              /* NOTES: its value (1/16...), 0: none */
    int8_t transpose;             /* the notes drawn this far up or down (TRANSPO) */
    uint8_t piece, npieces;       /* the 909: the piece edited */
    const char *const *piece_name;
    tb_vstep_t s[128];            /* the whole loop */
    int8_t sl[128];               /* slices: each step's slice (-1: none) */
    uint8_t brk_wave[200];        /* slices: the whole break, its level; where its slices start (0..199) */
    uint8_t brk_cut[33], nslices;
    int8_t slice_lit;             /* the slice of the step held (-1) */
} tb_seqview_t;

/* the menus (ENV LFO ARP SCL FX EDIT) */
enum { TB_MENU_NONE, TB_MENU_ENV, TB_MENU_LFO, TB_MENU_ARP, TB_MENU_SCALE, TB_MENU_FX, TB_MENU_EDIT, TB_MENU_GLO,
       TB_MENU_SYS, TB_MENU_SEQ, TB_MENU_TOOLS, TB_MENU_NOTES };   /* NOTES: SEQ's second press, the notes edited one by one */
typedef struct {
    uint8_t a, d, s, r;           /* the envelope shown: its knobs */
    int32_t lvl;                  /* .. and where it is now (Q30), its stage (0 off, 1 A, 2 D/S, 3 R) */
    uint8_t stage;
} tb_envview_t;
typedef struct {
    uint8_t wave, depth, sync;
    int16_t value;                /* -32767..32767 now */
    uint16_t phase;               /* 0..65535 */
    const char *dest;
} tb_lfoview_t;
typedef struct {
    uint8_t held[8], nheld;       /* the notes it holds */
    uint8_t seq[16], nseq;        /* the order it plays them in (a preview of a cycle) */
    uint8_t pos, sounding;        /* where in the cycle, the note sounding (0: none) */
    uint8_t rhythm, rstep, on, latch;   /* its rhythm (8 steps, a bit each), the step now */
} tb_arpview_t;
typedef struct {
    uint8_t root;
    uint16_t mask;                /* the scale's notes, from the root */
    uint8_t chord[4], nchord;     /* the chord the root key plays on this track */
} tb_scaleview_t;

typedef struct {
    uint8_t page, type, p[4];     /* the FX page (0 DRIVE, 1 CHORUS, 2 FLANGER, 3 PHASER, 4 TREMOLO, 5 EXTRA, 6 EQ,
                                   * 7 REVERB, 8 DELAY), its effect, its knobs (1 .. 4: AMOUNT first, when it has one) */
    uint8_t on[10];               /* which pages' effects play */
    uint8_t ord[5], par, send, rsel, fxon, rev_on;   /* ROUTE (9): the order, the split ones, the sends split, the effect
                                   * picked, the ones playing (bits), whether the track sends to the reverb or delay */
    const char *name;
} tb_fxview_t;

typedef struct {
    uint16_t bpm10;               /* tempo x 10 */
    uint8_t playing, pattern, track, beat, bar_beat;   /* track: the selected one */
    int8_t queued;                /* the pattern coming at the bar (-1: none) */
    uint8_t rec, octave_on;       /* recording; the keyboard's octave shown */
    int8_t octave;
    uint8_t cpu;                  /* % */
    uint32_t dropouts;            /* MENU: the audio blocks late since power on (the FM-1 says), 0: none */
    uint8_t audio_pct;            /* MENU: the audio's busiest block lately, % of its time (0: not known) */
    uint8_t prof_max[13], prof_avg[13];   /* MENU: the engine's parts (tb_engine.h TB_PROF_*), busiest / mean, % */
    uint8_t prof_pg;              /* MENU: what the CPU line shows: 0 the CPU, 1-4 the parts (a new one each second) */
    uint16_t heap_k;              /* MENU: the SDK's heap, KB never used yet (0: not known) */
    uint16_t beat_phase;          /* 0..1023 inside the current beat (the pulse) */
    uint32_t beats;               /* beats since PLAY (the dancer's loop) */
    const char *knob_name[4];
    char knob_value[4][12];
    uint8_t knob_pos[4];          /* 0..255 */
    uint8_t knob_bip[4];          /* the value grows from the middle (FILTRE, BEND, PITCH) */
    uint8_t knob_lock[4];         /* SEQ, a step held: this knob locked on it */
    uint8_t page_v[4];            /* the knobs' raw values (the pages that draw them) */
    int8_t hud;                   /* the knob being turned (-1: none) */
    uint8_t hud_alpha;            /* 0..255: the big display fading out */
    const char *hud_name;         /* the HUD's own words (a preset, the tempo): name, value, position */
    char hud_value[48];
    uint8_t hud_pos;
    const char *hint;             /* a line of help (null: the strip's place: TRACK > PAGE) */
    const char *strip_title;      /* the bottom strip's page: its name, which of how many */
    uint8_t strip_page, strip_pages;
    uint8_t mute_layer;           /* EDIT held: the keys mute and solo */
    uint32_t ms;                  /* a clock for the animations */
    tb_lane_t lane[TB_TRACKS];
    uint8_t lane_h[TB_TRACKS];    /* the lanes' heights now (the loupe animates them) */
    tb_seqview_t seq;
    /* a menu open: which, its page; what it draws */
    uint8_t menu, menu_page, menu_pages;
    const char *menu_title;
    tb_envview_t env;
    tb_lfoview_t lfo;
    tb_arpview_t arp;
    tb_scaleview_t scale;
    const char *tile[16];         /* the live effects (FX held): what each white key does, which are on */
    uint32_t tile_lit;
    uint8_t perform;              /* FX held: their screen */
    uint8_t mute_mask, solo_mask; /* (LIVE FX's black keys) the tracks muted; 7..11: their effects held */
    const char *bk_name[5];       /* (LIVE FX) the black keys 7..11's effects */
    uint8_t notes_edit;
    uint8_t dp_len, dp_roll, dp_filt, dp_kick, dp_end;   /* GLO, DROP: the DROP chosen's settings (TB_DSET_*) */
    const char *dp_name, *dp_key;                       /* .. its name, its key */           /* SEQ's NOTES page (the sequencer drawn, the note chosen lit over its length) */
    uint8_t drop, drop_left, mutating;    /* the DROP (1 waiting, 2 running) and its bars left; MUTATIONS on */
    uint8_t riser, riser_left;    /* the RISER: the same */
    uint8_t slot_cur, slot_sel, sys_ask;  /* the MENU: the project's place, the one chosen, the question (1 load, 2 new, 3 overwrite) */
    uint8_t slot_used;            /* .. which places hold a project, their tempos */
    uint16_t slot_bpm[8];
    const char *slot_nm[8];                   /* (v25) each place's project's name ("" none) */
    uint8_t song_pat[32], song_bars[32];  /* GLO MORCEAU: the chain, the place edited, the one playing and its bar */
    uint8_t song_edit, song_play, song_bar, song_on;
    uint8_t kick_wave[200];       /* EDIT on the kick: its sound, its pitch over time (Hz), where it plays now */
    uint16_t kick_hz[50];
    uint16_t kick_ms, kick_play;  /* how long the drawing lasts; the playhead (0..199, 0xFFFF: none) */
    const char *kick_name;
    char kick_note[8];
    const char *scale_name;
    uint8_t edit_kind, edit_p[12];    /* EDIT's picture: 0 the lane, 1 the oscillator, 2 the filter, 3 the break, 4 the sample's part */
    char scale_buf[24];
    tb_fxview_t fx;
    uint8_t mix_vol[TB_TRACKS];   /* GLO: the mixer */
    uint8_t riser_lvl[6], riser_filt;   /* GLO, RISER: each sound's level, the music filtered */
    uint8_t tool, tool_n, sys_new;    /* the sequencer's tools: the one chosen, how many; the MENU: new project asked */
    const char *const *tool_names;
    const char *tool_what;
    /* (RAVE FM 2) the section's pages, for the tabs and the breadcrumb: their titles, how many, the one shown; the
     * audio's load now (%) for the status line */
    const char *page_title[16];
    uint8_t npage, page_on, load;
    const char *sound;            /* the selected track's sound (its preset's name; 0: none) */
    const char *family;           /* .. its family (a synth's: BASS, LEAD, STAB, HOOVER..; 0: none) */
} tb_view_t;

void tb_ui_home(const tb_view_t *v);
void tb_ui_track(const tb_view_t *v);
void tb_ui_seq(const tb_view_t *v);
void tb_ui_menu(const tb_view_t *v);
void tb_ui_perform(const tb_view_t *v);
void tb_ui_kick(const tb_view_t *v);
void tb_ui_splash(uint32_t ms);
/* the first version's dancer, on the beat (the music playing, nothing touched for a while; 0: none in this build) */
int tb_ui_dance(const tb_view_t *v);
/* a track's picture, about size px across, centred at cx, cy */
void tb_ui_icon(unsigned icon, int cx, int cy, int size, uint32_t c, uint8_t a);
#endif
