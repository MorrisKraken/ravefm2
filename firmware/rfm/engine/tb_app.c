/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the control task. The panel read through the FM-1 API (fm1.h: the official board's, or the browser
 * emulator's), the gestures turned into the instrument's changes, the screen drawn about 30 times a second.
 *
 * The buttons do what they do on the FM-1's other firmwares (Felucca's map), so that nobody has to learn them again:
 * a page button opens its page, pressed again it shows the next one; HOME goes back to the eight tracks.
 *   SELECT the tempo (on a page: its pages)     ALGORITHM the track     PRESETS the track's sound     KNOB 1..4 the
 *   four values shown
 *   HOME the tracks (again: the strip's next page; held: the MENU)    EDIT the track's sound, page after page
 *   FX the effects: two inserts per track, reverb, delay, master      GLO the mixer, the song's settings
 *   SCL the scale and chords     ENV the envelope     LFO the LFO     ARP the arpeggiator (held: on / off)
 *   SEQ the steps (again: the tools)     SAVE the project kept     REC record     PLAY play / stop
 *   OCT- OCT+ the keyboard's octave (both: back to 0); in SEQ the frame of 16 steps, a step held: a semitone;
 *             on the tools: OCT+ does it
 * Held with the keys: FX the live effects (white keys) and mutes (black keys); GLO mutes (white 1..8) and solos
 * (9..16); EDIT the track's machine (white 1..5: KICK BREAK 909 SYNTH SAMPLE); SCL the root (any key). */
#include "tb_app.h"
#include "tb_engine.h"
#include "tb_env.h"
#include "tb_store.h"
#include "rfm_gfx.h"
#include "tb_ui.h"
#include "fm1.h"
#include <stdio.h>
#include <string.h>

enum { B_OCTDN, B_OCTUP, B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC };
enum { E_SELECT, E_ALGO, E_K1, E_K2, E_K3, E_K4, E_PRESET };
#define BTN(b) (1u << (b))
#define LONG_MS 450u

/* ------------------------------------------------------------- the knobs' pages --- */
enum { F_PCT, F_RAW, F_ENUM, F_SEMI, F_BIP, F_BEND, F_FILT, F_KNOTE, F_BPM, F_SWING, F_PAT, F_PLUS1, F_BRK,
       F_NOTE, F_SLICE, F_CHANCE, F_LONG, F_REPEAT, F_PAN, F_LFORATE, F_ROOT, F_TIME, F_TRANSPO, F_EQ, F_WIDTH, F_TOOL, F_KSEMI, F_SLOT, F_PNAME, F_PCHR, F_TEQ, F_SPAT, F_MPOS, F_MFINE, F_PCT100, F_PCT5, F_NIDX, F_NLEN };
enum { R_VOL = 32, R_FILT, R_REV, R_DLY, R_DUCK, R_LEN, R_SPEED, R_PAN, R_ARP, R_ARPRATE, R_ARPGATE, R_ARPOCT,
       R_ARPLATCH, R_ARPRHY, R_ARPSWING, R_ARPACC, R_LFOWAVE, R_LFORATE, R_LFODEPTH, R_LFODEST, R_LFOSYNC, R_LFORETRIG,
       R_ROOT, R_SCALE, R_QUANT, R_CHORD, R_INV, R_VOICING, R_REVTYPE, R_REVSIZE, R_REVDAMP, R_REVPRE, R_REVLEV,
       R_DLYTYPE, R_DLYTIME, R_DLYFB, R_DLYTONE, R_DLYLEV, R_EQL, R_EQM, R_EQH, R_COMP, R_DRIVE, R_WIDTH,
       R_INSA, R_INSA1, R_INSA2, R_INSA3, R_INSB, R_INSB1, R_INSB2, R_INSB3, R_TRANSPO, R_GTRANSPO,
       R_BPM, R_SWING, R_PAT, R_PAL, R_TOOL, R_TP1, R_TP2, R_DBARS, R_DROLL, R_DFILT, R_DCRASH, R_MAMT, R_MFILLS,
       R_MTARGET, R_MON, R_TEQL, R_TEQM, R_TEQH, R_DTYPE, R_DGAIN, R_DTONE, R_DMIX, R_RBARS, R_RFILT, R_RGAP,
       R_RIMP, R_RTYPE, R_REND, R_RLVL, R_METRO, R_MVOL, R_SYNC, R_MIDIOUT, R_SLOT, R_PNPOS, R_PNCHR, R_SPOS, R_SPAT, R_SBARS, R_SMODE, R_MSEL, R_MPOS, R_MFINE, R_MNUM, R_USMP, R_RQUANT, R_RGRID, R_TCHO, R_SCUR, R_SVAL,
       R_CHORATE, R_CHODEP, R_STK, R_STK_END = R_STK + 3 * TB_NSTK - 1, R_STKM, R_STKM_END = R_STKM + TB_NSTK - 1,
       R_RSEL, R_RPOS, R_RPAR, R_RSEND, R_NSEL, R_NPITCH, R_NLEN, R_NVEL, R_NCHANCE, R_NREP, R_NACC, R_NSLIDE, R_SPAGE, R_SPIECE, R_SRES, R_NDINFO, R_DSEL, R_DLEN, R_DROL, R_DFIL, R_DKCK, R_DEND,
       R_FXV, R_FXV_END = R_FXV + TB_NFXV - 1, R_LIGHT, R_D9, R_D9_END = R_D9 + D9_N - 1, R_NREFS,
       R_TVOL = 200, R_NONE = 255 };                  /* R_TVOL + t: track t's volume (the mixer) */
typedef struct { const char *name; uint8_t ref; uint16_t min, max; uint8_t fmt; const char *const *names; } pd_t;
typedef struct { const char *title; pd_t k[4]; } pg_t;

static const char *const ONOFF[2] = {"OFF", "ON"};
static const char *const DIVN[6] = {"4", "8", "16", "32", "AUTO", "MAN"};
static const char *const SLMODE[5] = {"ONE", "GATE", "LOOP", "FULL", "DISQUE"};
#if TB_HAS_THEMES
static const char *const PALN[GFX_NPAL] = {"NEON", "ACIDE", "TOXIC", "FEU", "SPECTRE", "VIOLET"};
#define K_PAL {"COLOUR", R_PAL, 0, GFX_NPAL - 1, F_ENUM, PALN}
/* (RAVE FM 2) the buttons' backlight: all, the menu buttons, the keys, none (OCT- and OCT+ always) */
static const char *const LIGHTN[4] = {"TOUT", "MENU", "CLAVIER", "ETEINT"};
#define K_LIGHT {"LUMIERE", R_LIGHT, 0, 3, F_ENUM, LIGHTN}
#else
#define K_PAL {0, R_NONE, 0, 0, 0, 0}
#define K_LIGHT {0, R_NONE, 0, 0, 0, 0}
#endif
static const char *const PIECE[TB909_PIECES] = {"BD", "SD", "CP", "CH", "OH", "MT", "RS", "CR", "LT", "HT", "RD"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const CLAVIER[2] = {"FREE", "SCALE"};
/* the strip's short names (the page shows the full ones big) */
static const char *const DLYT_SHORT[TB_FX_NDLYT] = {"PING", "STEREO", "MONO", "TAPE", "DUB"};
#define TB_RFM2_NO909 0                         /* (RAVE FM 2: the 909 back, on DRUM-X: tb_drumx.h) */
#ifdef RFM2
#define TB_RFM2_NOSMP 1                         /* (RAVE FM 2: no SAMPLE machine, its track the STAB/HOOVER's) */
#else
#define TB_RFM2_NOSMP 0
#endif
static const char *const MACHINE_NAME[TB_NMACHINE] = {"", "KICK", "BREAK", "909", "SYNTH", "SAMPLE", "ACID"};

#define K_NONE {0, R_NONE, 0, 0, 0, 0}
#if TB_HAS_BRK_REV
#define K_BREV {"REV", TBB_REV, 0, 1, F_ENUM, ONOFF}
#else
#define K_BREV K_NONE
#endif
#if TB_HAS_SWING
#define K_SWING {"SWING", R_SWING, 0, 127, F_SWING, 0}
#else
#define K_SWING K_NONE
#endif
#if TB_HAS_SPEED
#define K_SPEED {"SPEED", R_SPEED, 0, 2, F_ENUM, TB_SPEED_NAME}
#else
#define K_SPEED K_NONE
#endif
#define DUCK {"SIDECHAIN", R_DUCK, 0, 127, F_PCT, 0}   /* (the track pushed down by each kick: the pump) */
/* HOME, the same on every track (the kick apart): MAIN (the volume and the machine's three), MIX (filter, reverb,
 * delay, pan: the same knobs as on the FX pages), TRACK (steps, speed, pump, transpose) */
#if TB_HAS_REVERB
#define K_REV {"REVERB", R_REV, 0, 127, F_PCT, 0}
#else
#define K_REV K_NONE
#endif
#if TB_HAS_DELAY
#define K_DLY {"DELAY", R_DLY, 0, 127, F_PCT, 0}
#else
#define K_DLY K_NONE
#endif
#define MIXP {"MIX", {{"FILTER", R_FILT, 0, 127, F_FILT, 0}, K_REV, K_DLY, {"PAN", R_PAN, 0, 127, F_PAN, 0}}}
#define TRACKP {"TRACK", {{"STEPS", R_LEN, 1, TB_STEPS, F_RAW, 0}, K_SPEED, \
                          DUCK, {"TRANSPO", R_TRANSPO, 0, 48, F_TRANSPO, 0}}}
#define VOLK {"VOLUME", R_VOL, 0, 127, F_PCT, 0}
/* (a step's locks: its mix) */
static const pg_t LOCK_MIX_PG = {"MIX", {VOLK, {"FILTER", R_FILT, 0, 127, F_FILT, 0}, K_REV, K_DLY}};
static const pg_t HOME_KICK_PG[] = {
    {"MAIN", {VOLK, {"DIST", TB_KICK_DISTO, 0, 127, F_PCT, 0}, {"PITCH", TB_KICK_PITCH, 0, 48, F_KNOTE, 0},
              {"SUB", TB_KICK_SUB, 0, 127, F_PCT, 0}}},
    {"TRACK", {{"STEPS", R_LEN, 1, TB_STEPS, F_RAW, 0}, K_SPEED,
               {"FILTER", R_FILT, 0, 127, F_FILT, 0}, {"PAN", R_PAN, 0, 127, F_PAN, 0}}},
};
static const pg_t HOME_SYN_PG[] = {
    {"MAIN", {VOLK, {"CUTOFF", TBS_CUTOFF, 0, 127, F_PCT, 0}, {"RESO", TBS_RESO, 0, 127, F_PCT, 0},
              {"MACRO", TBS_MACRO, 0, 127, F_PCT, 0}}},
    MIXP, TRACKP,
};
static const pg_t HOME_RAVE_PG[] = {
    {"MAIN", {VOLK, {"CUTOFF", TBS_CUTOFF, 0, 127, F_PCT, 0}, {"RESO", TBS_RESO, 0, 127, F_PCT, 0},
              {"DISTO", TBS_DRIVE, 0, 127, F_PCT, 0}}},
    MIXP, TRACKP,
};
static const pg_t HOME_ACID_PG[] = {
    {"MAIN", {VOLK, {"CUTOFF", TBA_CUTOFF, 0, 127, F_PCT, 0}, {"RESO", TBA_RESO, 0, 127, F_PCT, 0},
              {"ENVMOD", TBA_ENVMOD, 0, 127, F_PCT, 0}}},
    MIXP, TRACKP,
};
static const pg_t HOME_BRK_PG[] = {
    {"MAIN", {VOLK, {"BREAK", TBB_SRC, 0, 0, F_BRK, 0}, {"PITCH", TBB_PITCH, 0, 48, F_SEMI, 0},
              {"TONE", TBB_TONE, 0, 127, F_PCT, 0}}},
    MIXP, TRACKP,
};
static const pg_t HOME_909_PG[] = {
    {"MAIN", {VOLK, {"TUNE", TB909_TUNE, 0, 127, F_BIP, 0}, {"DECAY", TB909_DECAY, 0, 127, F_PCT, 0},
              {"TONE", TB909_TONE, 0, 127, F_BIP, 0}}},
    MIXP, TRACKP,
};
static const pg_t HOME_SMP_PG[] = {
    {"MAIN", {VOLK, {"SOURCE", TBB_SRC, 0, 0, F_BRK, 0}, {"PITCH", TBB_PITCH, 0, 48, F_SEMI, 0},
              {"DECAY", TBB_DECAY, 0, 127, F_PCT, 0}}},
    MIXP, TRACKP,
};
/* EDIT: the machine's own pages */
static const pg_t KICK_PG[] = {
    {"SHAPE", {{"PITCH", TB_KICK_PITCH, 0, 48, F_KNOTE, 0}, {"PUNCH", TB_KICK_PUNCH, 0, 127, F_PCT, 0},
               {"LONG", TB_KICK_LONG, 0, 127, F_PCT, 0}, {"SUB", TB_KICK_SUB, 0, 127, F_PCT, 0}}},
    {"DIST", {{"TYPE", TB_KICK_TYPE, 0, 0, F_ENUM, 0}, {"DIST", TB_KICK_DISTO, 0, 127, F_PCT, 0},
               {"HOLLOW", TB_KICK_CREUX, 0, 127, F_PCT, 0}, {"TONE", TB_KICK_TONE, 0, 127, F_PCT, 0}}},
    /* (v24) its envelope here too, beside LONG (the same four knobs as ENV's): DECAY (SUSTAIN 0) how long it rings */
    {"ENVELOPE", {{"ATTACK", TB_P_ENV, 0, 127, F_TIME, 0}, {"DECAY", TB_P_ENV + 1, 0, 127, F_TIME, 0},
                  {"SUSTAIN", TB_P_ENV + 2, 0, 127, F_PCT, 0}, {"RELEASE", TB_P_ENV + 3, 0, 127, F_TIME, 0}}},
};
static const pg_t BREAK_PG[] = {
    {"SLICE", {{"BREAK", TBB_SRC, 0, 0, F_BRK, 0}, {"SLICES", TBB_DIV, 0, TB_HAS_MAN_SLICE ? 5 : 4, F_ENUM, DIVN},
               {"MODE", TBB_MODE, 0, 4, F_ENUM, SLMODE}, {"PITCH", TBB_PITCH, 0, 48, F_SEMI, 0}}},
    {"SOUND", {{"DECAY", TBB_DECAY, 0, 127, F_PCT, 0}, {"TONE", TBB_TONE, 0, 127, F_PCT, 0},
             {"RELEASE", TBB_REL, 0, 127, F_PCT, 0}, K_BREV}},
    {"PLAY", {{"START", TBB_START, 0, 31, F_PLUS1, 0}, {"SYNC", TBB_SYNC, 0, 1, F_ENUM, ONOFF}, K_NONE, K_NONE}},
#if TB_HAS_MAN_SLICE
    {"MANUAL", {{"SLICE", R_MSEL, 0, TBB_MAXSLICE - 1, F_PLUS1, 0}, {"START", R_MPOS, 0, 999, F_MPOS, 0},
                   {"FINE", R_MFINE, 0, 128, F_MFINE, 0}, {"COUNT", R_MNUM, 1, TBB_MAXSLICE, F_RAW, 0}}},
#endif
};
#ifdef RFM2
/* (RAVE FM 2) the 909 on DRUM-X (tb_drumx.h): the kit and its sound, the whole kit moved, then the piece chosen
 * (SON: the 909's piece, as SEQ's) its own TUNE DECAY MORPH and LEVEL */
static const char *const DXKITN[TB_DX_NKIT] = {"909 VRAI", "DRUM-X", "909 CLUB", "808 BOOM", "LOFI", "INDUSTRY", "ACOUSTIC", "DUB",
                                              "TECHNO", "BREAKS", "HIP HOP", "LATIN"};
static const pg_t D909_PG[] = {
    {"KIT", {{"KIT", TBX_KIT, 0, TB_DX_NKIT - 1, F_ENUM, DXKITN}, {"MORPH", TBX_MORPH, 0, 127, F_PCT, 0},
             {"FM", TBX_FM, 0, 127, F_PCT, 0}, {"DRIVE", TBX_DRIVE, 0, 127, F_PCT, 0}}},
    {"KIT 2", {{"TUNE", TBX_TUNE, 0, 127, F_BIP, 0}, {"DECAY", TBX_DECAY, 0, 127, F_BIP, 0},
               {"TONE", TBX_TONE, 0, 127, F_BIP, 0}, {"NOISE", TBX_NOISE, 0, 127, F_BIP, 0}}},
    {"SON", {{"SON", R_SPIECE, 0, TB909_PIECES - 1, F_ENUM, PIECE}, {"TUNE", R_D9 + D9_TUNE, 0, 127, F_BIP, 0},
             {"DECAY", R_D9 + D9_DECAY, 0, 127, F_BIP, 0}, {"MORPH", R_D9 + D9_MORPH, 0, 127, F_BIP, 0}}},
    {"NIVEAU", {{"SON", R_SPIECE, 0, TB909_PIECES - 1, F_ENUM, PIECE}, {"LEVEL", R_D9 + D9_LEVEL, 0, 127, F_PCT, 0},
                K_NONE, K_NONE}},
};
#else
static const pg_t D909_PG[] = {
    {"909", {{"TUNE", TB909_TUNE, 0, 127, F_BIP, 0}, {"DECAY", TB909_DECAY, 0, 127, F_PCT, 0},
             {"TONE", TB909_TONE, 0, 127, F_BIP, 0}, {"SNAP", TB909_SNAP, 0, 127, F_BIP, 0}}},
};
#endif
static const pg_t SYNTH_PG[] = {
    {"OSC", {{"WAVE", TBS_WAVE, 0, TBS_NWAVE - 1, F_ENUM, TBS_WAVE_NAME}, {"DETUNE", TBS_DETUNE, 0, 127, F_PCT, 0},
             {"SUB", TBS_SUB, 0, 127, F_PCT, 0}, {"NOISE", TBS_NOISE, 0, 127, F_PCT, 0}}},
    {"FILTER", {{"CUTOFF", TBS_CUTOFF, 0, 127, F_PCT, 0}, {"RESO", TBS_RESO, 0, 127, F_PCT, 0},
                {"ENV", TBS_ENV, 0, 127, F_PCT, 0}, {"KEYTRK", TBS_KTRK, 0, 127, F_PCT, 0}}},
    {"PLAY", {{"MODE", TBS_MODE, 0, TBS_NMODE - 1, F_ENUM, TBS_MODE_NAME}, {"GLIDE", TBS_GLIDE, 0, 127, F_PCT, 0},
             {"BEND", TBS_BEND, 0, 127, F_BEND, 0}, {"DRIVE", TBS_DRIVE, 0, 127, F_PCT, 0}}},
    {"MACRO", {{"TYPE", TBS_MTYPE, 0, TBS_NMTYPE - 1, F_ENUM, TBS_MTYPE_NAME}, {"MACRO", TBS_MACRO, 0, 127, F_PCT, 0},
               K_NONE, K_NONE}},
    {"DIST", {{"DIST", TBS_DIST, 0, 127, F_PCT, 0}, {"TYPE", TBS_DTYPE, 0, TBS_ND - 1, F_ENUM, TBS_DTYPE_NAME},
               {"FILTER", TBS_FTYPE, 0, TBS_NF - 1, F_ENUM, TBS_FTYPE_NAME}, {"LOW CUT", TBS_HPF, 0, 127, F_PCT, 0}}},
    {"CHOP", {{"CHOP", TBS_GATE, 0, TBS_NG - 1, F_ENUM, TBS_GATE_NAME}, {"BEND T", TBS_PTIME, 0, 127, F_PCT, 0},
                 {"BEND", TBS_BEND, 0, 127, F_BEND, 0}, {"GLIDE", TBS_GLIDE, 0, 127, F_PCT, 0}}},
    {"MOTION", {{"VIBRATO", TBS_VIB, 0, 127, F_PCT, 0}, {"WHOOP", TBS_WHOOP, 0, 127, F_PCT, 0},
                   {"SPEED", TBS_SPEED, 0, 127, F_PCT, 0}, {"CHORUS", TBS_CHORUS, 0, 127, F_PCT, 0}}},
};
/* the first version's leads (WAVE RAVE, tb_rave.h): their own knobs, named for what they do there */
static const char *const RAVE_TYPE_NAME[3] = {"ANGER", "V1", "SUPA"};
static const pg_t RAVE_PG[] = {
    {"RAVE", {{"WAVE", TBS_WAVE, 0, TBS_NWAVE - 1, F_ENUM, TBS_WAVE_NAME}, {"TYPE", TBS_SUB, 0, 2, F_ENUM, RAVE_TYPE_NAME},
              {"SPREAD", TBS_DETUNE, 0, 127, F_PCT, 0}, {"OCTAVE", TBS_MACRO, 0, 127, F_PCT, 0}}},
    {"TONE", {{"CUTOFF", TBS_CUTOFF, 0, 127, F_PCT, 0}, {"RESO", TBS_RESO, 0, 127, F_PCT, 0},
              {"DISTO", TBS_DRIVE, 0, 127, F_PCT, 0}, {"FLT ENV", TBS_ENV, 0, 127, F_PCT, 0}}},
    {"PLAY", {{"MODE", TBS_MODE, 0, TBS_NMODE - 1, F_ENUM, TBS_MODE_NAME}, {"GLIDE", TBS_GLIDE, 0, 127, F_PCT, 0},
              {"BEND", TBS_BEND, 0, 127, F_PCT, 0}, {"CHORUS", TBS_CHORUS, 0, 127, F_PCT, 0}}},
};
static const pg_t ACID_PG[] = {
    {"303", {{"WAVE", TBA_WAVE, 0, 1, F_ENUM, TBA_WAVE_NAME}, {"CUTOFF", TBA_CUTOFF, 0, 127, F_PCT, 0},
             {"RESO", TBA_RESO, 0, 127, F_PCT, 0}, {"ENVMOD", TBA_ENVMOD, 0, 127, F_PCT, 0}}},
    {"303 PLAY", {{"DECAY", TBA_DECAY, 0, 127, F_PCT, 0}, {"ACCENT", TBA_ACCENT, 0, 127, F_PCT, 0},
                 {"SLIDE", TBA_SLIDE, 0, 127, F_PCT, 0}, {"DRIVE", TBA_DRIVE, 0, 127, F_PCT, 0}}},
    {"303 TUNE", {{"TUNE", TBA_TUNE, 0, 127, F_BEND, 0}, K_NONE, K_NONE, K_NONE}},
};
static const pg_t SAMPLE_PG[] = {
    {"SAMPLE", {{"SOURCE", TBB_SRC, 0, 0, F_BRK, 0}, {"START", TBB_START, 0, 127, F_PCT, 0},
                {"LONG", 10, 0, 127, F_PCT, 0}, {"PITCH", TBB_PITCH, 0, 48, F_SEMI, 0}}},
    {"SOUND", {{"MODE", TBB_MODE, 0, 2, F_ENUM, SLMODE}, {"DECAY", TBB_DECAY, 0, 127, F_PCT, 0},
             {"TONE", TBB_TONE, 0, 127, F_PCT, 0}, {"RELEASE", TBB_REL, 0, 127, F_PCT, 0}}},
    {"PLAY", {K_BREV, K_NONE, K_NONE, K_NONE}},
};
/* the menus */

#if TB_HAS_USER_BRK
#define K_USMP {"SAMPLES", R_USMP, 0, TBB_USER, F_RAW, 0}
#else
#define K_USMP K_NONE
#endif
#define ADSR(t, i) {t, {{"ATTACK", (i), 0, 127, F_TIME, 0}, {"DECAY", (i) + 1, 0, 127, F_TIME, 0}, \
                        {"SUSTAIN", (i) + 2, 0, 127, F_PCT, 0}, {"RELEASE", (i) + 3, 0, 127, F_TIME, 0}}}
static const pg_t ENV_PG[] = {ADSR("ENVELOPE", TB_P_ENV)};
static const pg_t ENV_SYNTH_PG[] = {ADSR("AMP ENV", TBS_AMP), ADSR("FILTER ENV", TBS_FILT)};
/* the first version's leads: their one envelope, its own times (shown as the knobs' places) */
static const pg_t ENV_RAVE_PG[] = {{"AMP ENV", {{"ATTACK", TBS_AMP, 0, 127, F_PCT, 0}, {"DECAY", TBS_AMP + 1, 0, 127, F_PCT, 0},
                                                {"SUSTAIN", TBS_AMP + 2, 0, 127, F_PCT, 0}, {"RELEASE", TBS_AMP + 3, 0, 127, F_PCT, 0}}}};
static const pg_t LFO_PG[] = {
    {"LFO", {{"SHAPE", R_LFOWAVE, 0, TB_NLFOW - 1, F_ENUM, TB_LFO_WAVE_NAME}, {"SPEED", R_LFORATE, 0, 127, F_LFORATE, 0},
             {"AMOUNT", R_LFODEPTH, 0, 127, F_PCT, 0}, {"DEST", R_LFODEST, 0, TB_NDEST - 1, F_ENUM, TB_DEST_NAME}}},
    {"LFO MODE", {{"SYNC", R_LFOSYNC, 0, 1, F_ENUM, ONOFF}, {"RETRIG", R_LFORETRIG, 0, 1, F_ENUM, ONOFF}, K_NONE, K_NONE}},
};
static const pg_t ARP_PG[] = {
    {"ARP", {{"MODE", R_ARP, 0, TB_ARP_NMODE - 1, F_ENUM, TB_ARP_NAME}, {"SPEED", R_ARPRATE, 0, TB_ARP_NRATE - 1, F_ENUM, TB_ARP_RATE_NAME},
             {"GATE", R_ARPGATE, 0, 127, F_PCT, 0}, {"OCTAVES", R_ARPOCT, 1, 4, F_RAW, 0}}},
    {"ARP PLAY", {{"LATCH", R_ARPLATCH, 0, 1, F_ENUM, ONOFF}, {"RHYTHM", R_ARPRHY, 0, TB_ARP_NRHYTHM - 1, F_ENUM, TB_ARP_RHYTHM_NAME},
                 {"SWING", R_ARPSWING, 0, 127, F_PCT, 0}, {"VELO", R_ARPACC, 0, TB_NACC - 1, F_ENUM, TB_ACC_NAME}}},
};
static const pg_t SCALE_PG[] = {
    {"SCALE", {{"ROOT", R_ROOT, 0, 11, F_ROOT, 0}, {"SCALE", R_SCALE, 0, TB_NSCALE - 1, F_ENUM, TB_SCALE_NAME},
               {"CHORD", R_CHORD, 0, TB_NCHORD - 1, F_ENUM, TB_CHORD_NAME}, {"INVERT", R_INV, 0, 3, F_RAW, 0}}},
    {"CHORDS", {{"VOICING", R_VOICING, 0, TB_NVOICING - 1, F_ENUM, TB_VOICING_NAME}, {"KEYS", R_QUANT, 0, 1, F_ENUM, CLAVIER},
                 K_NONE, K_NONE}},
};
/* FX: this track's chain, in the order the sound goes through it, a page an effect, all of them playing together:
 * DRIVE, CHORUS, FLANGER, PHASER, TREMOLO, EXTRA (ring mod, auto-wah, comp), EQ, then what it sends to the shared
 * REVERB and DELAY (AMOUNT: this track's, the rest: every track's). KNOB 1 is always how much (0: off) or which */
#if TB_HAS_INS_LOFI
static const char *const DRIVE_NAME[6] = {"OFF", "OVERDRIVE", "DIST", "FUZZ", "LO-FI", "FOLD"};
#define NDRIVE 6
#else
static const char *const DRIVE_NAME[4] = {"OFF", "OVERDRIVE", "DIST", "FUZZ"};
#define NDRIVE 4
#endif
#if TB_HAS_INS_MOD
static const char *const MOD_NAME[4] = {"OFF", "RING", "AUTO-WAH", "COMP"};
static const uint8_t MOD_TYPE[4] = {TB_INS_OFF, TB_INS_RING, TB_INS_WAH, TB_INS_COMP};
#define NMOD 4
#else   /* (the FM-1: the PHASER, once a stacked effect, is EXTRA's) */
static const char *const MOD_NAME[3] = {"OFF", "PHASER", "COMP"};
static const uint8_t MOD_TYPE[3] = {TB_INS_OFF, TB_INS_PHASER, TB_INS_COMP};
#define NMOD 3
#endif
enum { FXK_DRIVE, FXK_CHO, FXK_FLA, FXK_PHA, FXK_TRM, FXK_EXT, FXK_TEQ, FXK_REV, FXK_DLY, FXK_ROUTE };   /* = the UI's page */
/* ROUTE: the order the effects that can move go in (EFFECT picks one, PLACE moves it), each in line (after the one
 * before it) or SPLIT (beside: on the clean sound, mixed back at the end); the reverb and delay AFTER the effects or
 * SPLIT (on the clean sound). The chorus and the EQ stay last */
#if TB_HAS_STACK
static const char *const RFX_NAME[TB_NFX] = {"DRIVE", "FLANGE", "PHASER", "TREM", "EXTRA"};
static const char *const PATHN[2] = {"IN LINE", "SPLIT"};
static const char *const SENDN[2] = {"AFTER", "SPLIT"};
#endif
static uint8_t route_sel;                             /* the effect ROUTE shows (not saved) */
#define STK(i, k) (R_STK + 3 * (i) + (k))
static const pg_t FX_PG[] = {
    {"DRIVE", {{"TYPE", R_INSA, 0, NDRIVE - 1, F_ENUM, DRIVE_NAME}, {0, R_INSA1, 0, 127, F_PCT, 0}, {0, R_INSA2, 0, 127, F_PCT, 0},
               {0, R_INSA3, 0, 127, F_PCT, 0}}},
#if TB_HAS_CHORUS
    {"CHORUS", {{"AMOUNT", R_TCHO, 0, 127, F_PCT, 0}, {"SPEED", R_CHORATE, 0, 127, F_PCT, 0},
                {"DEPTH", R_CHODEP, 0, 127, F_PCT, 0}, K_NONE}},
#endif
#if TB_HAS_STACK
    {"FLANGER", {{"AMOUNT", R_STKM + TB_STK_FLANGER, 0, 127, F_PCT, 0}, {"SPEED", STK(TB_STK_FLANGER, 0), 0, 127, F_PCT, 0},
                 {"DEPTH", STK(TB_STK_FLANGER, 1), 0, 127, F_PCT, 0}, {"RETURN", STK(TB_STK_FLANGER, 2), 0, 127, F_PCT, 0}}},
    {"PHASER", {{"AMOUNT", R_STKM + TB_STK_PHASER, 0, 127, F_PCT, 0}, {"SPEED", STK(TB_STK_PHASER, 0), 0, 127, F_PCT, 0},
                {"DEPTH", STK(TB_STK_PHASER, 1), 0, 127, F_PCT, 0}, {"RETURN", STK(TB_STK_PHASER, 2), 0, 127, F_PCT, 0}}},
    {"TREMOLO", {{"AMOUNT", R_STKM + TB_STK_TREMOLO, 0, 127, F_PCT, 0}, {"SPEED", STK(TB_STK_TREMOLO, 0), 0, 127, F_PCT, 0},
                 {"SHAPE", STK(TB_STK_TREMOLO, 2), 0, 127, F_PCT, 0}, K_NONE}},
#endif
    {"EXTRA", {{"TYPE", R_INSB, 0, NMOD - 1, F_ENUM, MOD_NAME}, {0, R_INSB1, 0, 127, F_PCT, 0}, {0, R_INSB2, 0, 127, F_PCT, 0},
               {0, R_INSB3, 0, 127, F_PCT, 0}}},
#if TB_HAS_TEQ
    {"EQ", {{"LOW", R_TEQL, 0, 127, F_TEQ, 0}, {"MID", R_TEQM, 0, 127, F_TEQ, 0}, {"HIGH", R_TEQH, 0, 127, F_TEQ, 0}, K_NONE}},
#endif
#if TB_HAS_REVERB
    {"REVERB", {{"AMOUNT", R_REV, 0, 127, F_PCT, 0}, {"TYPE", R_REVTYPE, 0, TB_FX_NREV - 1, F_ENUM, TB_FX_REV_NAME},
                {"SIZE", R_REVSIZE, 0, 127, F_PCT, 0}, {"DAMP", R_REVDAMP, 0, 127, F_PCT, 0}}},
#endif
#if TB_HAS_DELAY
    {"DELAY", {{"AMOUNT", R_DLY, 0, 127, F_PCT, 0}, {"BEAT", R_DLYTIME, 0, TB_FX_NDLY - 1, F_ENUM, TB_FX_DLY_NAME},
               {"REPEATS", R_DLYFB, 0, 127, F_PCT, 0}, {"TYPE", R_DLYTYPE, 0, TB_FX_NDLYT - 1, F_ENUM, DLYT_SHORT}}},
#endif
#if TB_HAS_STACK
    {"ROUTE", {{"EFFECT", R_RSEL, 0, TB_NFX - 1, F_ENUM, RFX_NAME}, {"PLACE", R_RPOS, 1, 5, F_RAW, 0},
               {"PATH", R_RPAR, 0, 1, F_ENUM, PATHN}, {"REV+DLY", R_RSEND, 0, 1, F_ENUM, SENDN}}},
#endif
};
/* FX_PG's pages as FXK_* (the screen draws by these) */
static const uint8_t FXK_OF[] = {FXK_DRIVE,
#if TB_HAS_CHORUS
                                 FXK_CHO,
#endif
#if TB_HAS_STACK
                                 FXK_FLA, FXK_PHA, FXK_TRM,
#endif
                                 FXK_EXT,
#if TB_HAS_TEQ
                                 FXK_TEQ,
#endif
#if TB_HAS_REVERB
                                 FXK_REV,
#endif
#if TB_HAS_DELAY
                                 FXK_DLY,
#endif
#if TB_HAS_STACK
                                 FXK_ROUTE
#endif
};
_Static_assert(sizeof FXK_OF == sizeof FX_PG / sizeof *FX_PG, "FXK_OF: a page each");
#define TV(i) {0, R_TVOL + (i), 0, 127, F_PCT, 0}
static const char *const DBARS[4] = {"1 BAR", "2 BARS", "4 BARS", "8 BARS"};
#if TB_HAS_MUTATE
static const char *const MFILLS[3] = {"4 BARS", "8 BARS", "16 BARS"};
static const char *const MTARGET[4] = {"ALL", "KICK", "909", "BREAK"};
#endif
static const char *const METRON[9] = {"OFF", "ON 1", "ON 2", "ON 3", "ON 4", "REC 1", "REC 2", "REC 3", "REC 4"};
static const char *const COUNTN[5] = {"OFF", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS"};
static const uint8_t COUNT_BEATS[5] = {0, 1, 2, 4, 8};
static const char *const SYNCN[2] = {"INT", "MIDI"};
static const char *const SMODE[2] = {"LOOP", "CHAIN"};
enum { GLO_MIX1, GLO_MIX2, GLO_GLOBAL, GLO_SONG, GLO_DROP, GLO_DROP2, GLO_MUT, GLO_RISER, GLO_CLOCK, GLO_REC, GLO_MASTER };
/* the RISER: SOUND picks one of its sounds (the last: the music under it), LEVEL how loud it plays (0: off): any of
 * them together */
static const char *const RISERN[TB_NRISER + 1] = {"CLASSIC", "NOISE", "WIND", "SIREN", "SAW UP", "SNARE", "MUSIC"};
static const char *const RENDN[4] = {"PLAIN", "BREATH", "IMPACT", "BOTH"};   /* (the RISER's end: a beat of silence, a hit) */
static const char *const QUANTN[4] = {"100%", "75%", "50%", "25%"};
static const char *const GRIDN[4] = {"FREE", "1/32", "1/16", "1/8"};   /* (REC's grid, as the knob shows it) */
static const uint8_t GRID_OF_KNOB[4] = {TB_RECG_FREE, TB_RECG_32, TB_RECG_16, TB_RECG_8}, KNOB_OF_GRID[4] = {2, 1, 3, 0};
static const char *const QGRIDN[5] = {"", "1/32", "1/16", "1/8", "1/4"};   /* (the QUANTIZE tool's) */
/* the DROP page: which DROP its knobs set (named with the key that plays it, FX held) */
static const char *const DKINDN[TB_NDROP] = {"DROP", "KICK ROLL", "SNARE", "LOW", "STOP", "LONG"};
static uint8_t drop_sel;
#define GLO_HAS_REC (TB_HAS_REC || TB_HAS_METRO)
static const pg_t GLO_PG[] = {
    {"MIXER 1-4", {TV(0), TV(1), TV(2), TV(3)}},
#if TB_TRACKS > 6
    {"MIXER 5-8", {TV(4), TV(5), TV(6), TV(7)}},
#else
    {"MIXER 5-6", {TV(4), TV(5), {0}, {0}}},
#endif
    {"GLOBAL", {{"TEMPO", R_BPM, 60, 300, F_BPM, 0}, K_SWING,
                {"TRANSPO", R_GTRANSPO, 0, 48, F_TRANSPO, 0}, {"PATTERN", R_PAT, 0, TB_PATTERNS - 1, F_PAT, 0}}},
#if TB_HAS_SONG
    {"SONG", {{"SLOT", R_SPOS, 0, TB_SONG - 1, F_PLUS1, 0}, {"PATTERN", R_SPAT, 0, TB_PATTERNS, F_SPAT, 0},
                 {"BARS", R_SBARS, 1, 64, F_RAW, 0}, {"PLAYS", R_SMODE, 0, 1, F_ENUM, SMODE}}},
#endif
#if TB_HAS_DROPS
    {"DROP", {{"DROP", R_DSEL, 0, TB_NDROP - 1, F_ENUM, DKINDN}, {"LENGTH", R_DLEN, 0, 3, F_ENUM, DBARS},
              {"ROLL", R_DROL, 0, TB_NDROLL - 1, F_ENUM, TB_DROLL_NAME}, {"FILTER", R_DFIL, 0, TB_NDFILT - 1, F_ENUM, TB_DFILT_NAME}}},
    {"DROP +", {{"DROP", R_DSEL, 0, TB_NDROP - 1, F_ENUM, DKINDN}, {"KICK", R_DKCK, 0, TB_NDKICK - 1, F_ENUM, TB_DKICK_NAME},
                {"END", R_DEND, 0, TB_NDEND - 1, F_ENUM, TB_DEND_NAME}, K_NONE}},
#endif
#if TB_HAS_MUTATE
    {"MUTATIONS", {{"AMOUNT", R_MAMT, 0, 127, F_PCT, 0}, {"FILLS", R_MFILLS, 0, 2, F_ENUM, MFILLS},
                   {"TRACKS", R_MTARGET, 0, 3, F_ENUM, MTARGET}, {"ACTIVE", R_MON, 0, 1, F_ENUM, ONOFF}}},
#endif
#if TB_HAS_RISER
    {"RISER", {{"SOUND", R_RTYPE, 0, TB_NRISER, F_ENUM, RISERN}, {"LEVEL", R_RLVL, 0, 20, F_PCT5, 0},
               {"LENGTH", R_RBARS, 0, 3, F_ENUM, DBARS}, {"END", R_REND, 0, 3, F_ENUM, RENDN}}},
#endif
#if TB_HAS_MIDI
    {"CLOCK", {{"SYNC", R_SYNC, 0, 1, F_ENUM, SYNCN}, {"MIDI OUT", R_MIDIOUT, 0, 1, F_ENUM, ONOFF}, K_NONE, K_NONE}},
#endif
#if GLO_HAS_REC
    {"REC", {
#if TB_HAS_REC
             {"GRID", R_RGRID, 0, 3, F_ENUM, GRIDN}, {"SNAP", R_RQUANT, 0, 3, F_ENUM, QUANTN},
#else
             K_NONE, K_NONE,
#endif
#if TB_HAS_METRO
             {"METRO", R_METRO, 0, 8, F_ENUM, METRON}, {"COUNT-IN", R_MVOL, 0, 4, F_ENUM, COUNTN}}},
#else
             K_NONE, K_NONE}},
#endif
#endif
#if TB_HAS_MASTER
    {"MASTER", {{"LOW", R_EQL, 0, 127, F_EQ, 0}, {"MID", R_EQM, 0, 127, F_EQ, 0}, {"HIGH", R_EQH, 0, 127, F_EQ, 0},
                {"DRIVE", R_DRIVE, 0, 127, F_PCT, 0}}},
#endif
};
/* GLO_PG's pages as GLO_* (the screen draws by these) */
static const uint8_t GLO_OF[] = {GLO_MIX1, GLO_MIX2, GLO_GLOBAL,
#if TB_HAS_SONG
                                 GLO_SONG,
#endif
#if TB_HAS_DROPS
                                 GLO_DROP, GLO_DROP2,
#endif
#if TB_HAS_MUTATE
                                 GLO_MUT,
#endif
#if TB_HAS_RISER
                                 GLO_RISER,
#endif
#if TB_HAS_MIDI
                                 GLO_CLOCK,
#endif
#if GLO_HAS_REC
                                 GLO_REC,
#endif
#if TB_HAS_MASTER
                                 GLO_MASTER,
#endif
};
_Static_assert(sizeof GLO_OF == sizeof GLO_PG / sizeof *GLO_PG, "GLO_OF: a page each");
static unsigned glo_at(unsigned id) { unsigned i; for (i = 0; i < sizeof GLO_OF; i++) if (GLO_OF[i] == id) return i; return 0; }
static const pg_t SYS_PG[] = {
    {"PROJECTS", {{"PROJECT", R_SLOT, 0, TB_STORE_SLOTS - 1, F_SLOT, 0}, K_USMP, K_LIGHT,
                 K_PAL}},
    /* (v25) the project's name: KNOB 1 the letter's place (shown in [ ]), KNOB 2 the letter; SAVE keeps it */
    {"NAME", {{"NAME", R_PNPOS, 0, 7, F_PNAME, 0}, {"LETTER", R_PNCHR, 0, 37, F_PCHR, 0}, {0, R_NONE, 0, 0, 0, 0},
              {0, R_NONE, 0, 0, 0, 0}}},
};
/* (v25) the names' letters (the first: none, a space); the saved projects' names, read when the MENU opens */
static const char PN_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
static uint8_t pn_pos;
static char slot_name[TB_STORE_SLOTS][9];
static void names_read(void)
{
    unsigned k;
    for (k = 0; k < TB_STORE_SLOTS; k++)
        tb_store_name(k, slot_name[k]);
}
static unsigned pn_idx(char c)
{
    unsigned i;
    for (i = 1; i < sizeof PN_SET - 1u; i++)
        if (PN_SET[i] == c)
            return i;
    return 0;
}
/* SEQ, nothing held: page 1 PAS CURSEUR VALEUR (a drum: the cursor's first value) TRANSPO; page 2 the cursor's step,
   its four values; page 3 VITESSE SWING */
static const char *const VALN[] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
static const uint8_t VAL32[] = {1, 2, 4, 8, 16, 32};   /* (a value, in 32nds) */
static const pg_t SEQ_PG1 = {"SEQ", {{"STEPS", R_LEN, 1, TB_STEPS, F_RAW, 0}, K_NONE, {"VALUE", R_SVAL, 0, 5, F_ENUM, VALN},
                                     {"TRANSPO", R_TRANSPO, 0, 48, F_TRANSPO, 0}}};
static const pg_t SEQ_PG2 = {"NOTE", {K_NONE, K_NONE, K_NONE, K_NONE}};
static const pg_t SEQ_PG3 = {"SETTINGS", {K_SPEED, K_SWING,
                                          {"REC GRID", R_RGRID, 0, 3, F_ENUM, GRIDN}, K_NONE}};
/* the sequencer's tools (SEQ again): choose with KNOB 1, set with KNOB 2 and 3, OCT+ does it */
#if TB_HAS_TOOLS_GEN && TB_HAS_CLIP
enum { T_QUANT, T_COPY, T_PASTE, T_CLEAR, T_DOUBLE, T_HALF, T_SHIFT, T_TRANSPOSE, T_REVERSE, T_EUCLID, T_RANDOM, T_HUMAN,
       T_PCOPY, T_PPASTE, T_PCLEAR, T_STYLE, T_NTOOL };
#else   /* (the FM-1: the tools given up after T_NTOOL, never shown) */
#if TB_HAS_STYLE
enum { T_QUANT, T_COPY, T_PASTE, T_CLEAR, T_DOUBLE, T_HALF, T_SHIFT, T_TRANSPOSE, T_PCLEAR, T_STYLE, T_NTOOL,
#else
enum { T_QUANT, T_COPY, T_PASTE, T_CLEAR, T_DOUBLE, T_HALF, T_SHIFT, T_TRANSPOSE, T_PCLEAR, T_NTOOL, T_STYLE,
#endif
       T_REVERSE, T_EUCLID, T_RANDOM, T_HUMAN, T_PCOPY, T_PPASTE };
#endif
/* the ready rhythms (OUTILS > STYLE): the kick, the 909's pieces, the break's slices ('0'..'9' 'A'..'V'), the tempo */
typedef struct { const char *name; uint16_t bpm; const char *kick, *ch, *oh, *cp, *sd, *rd, *brk, *kick_sound, *alt1, *alt2; } style_t;
/* (the kick: 16 steps or more (a longer loop), 'x' a hit, '1' / '2' a hit with alt1 / alt2's TYPE locked on it;
 * kick_sound: a kick preset it also picks; alt1, alt2: kick presets, by name) */
#if TB_HAS_STYLE
static const style_t STYLE[] = {
    {"GABBER",     180, "x...x...x...x...", "", "..x...x...x...x.", "", "", "", ""},
    {"HAPPY HC",   170, "x...x...x...x...", ".x.x.x.x.x.x.x.x", "..x...x...x...x.", "....x.......x...", "", "", ""},
    {"EARLY HC",   165, "x...x...x...x.x.", "", "..x...x...x...x.", "....x.......x...", "", "", ""},
    {"FRENCHCORE", 200, "x...x...x...x...", "", "..x...x...x...x.", "............x...", "", "", ""},
    {"UPTEMPO",    200, "x...x...x...x..x", "", "..x...x...x...x.", "....x.......x...", "", "", ""},
    {"HARDTEK",    180, "x...x...x...x.xx", ".x...x...x...x..", "..x...x...x...x.", "", "", "", ""},
    {"TERROR",     240, "x.x.x.x.x.x.x.x.", "xxxxxxxxxxxxxxxx", "", "", "....x.......x...", "", ""},
    {"SPEEDCORE",  300, "x.x.x.x.x.x.x.x.", "", "", "", "....x.......x...", "x...x...x...x...", ""},
    {"INDUSTRIAL", 150, "x..x..x...x..x..", "x.x.x.x.x.x.x.x.", "", "", "....x.......x...", "", ""},
    {"DOOMCORE",   140, "x...x...x...x...", "", "", "............x...", "", "..x...x...x...x.", ""},
    {"JUNGLE",     170, "x.........x.....", "", "", "", "....x.......x...", "x.x.x.x.x.x.x.x.", "01.24.6789.4BCDF"},
    {"BREAKCORE",  180, "x..x..x.x..x..x.", "", "", "", ".....x.x....x.xx", "", "0123012345674567"},
    {"RAVE 92",    140, "x...x...x...x...", "", "..x...x...x...x.", "....x.......x...", "", "", "0123456789ABCDEF"},
    {"NEW BEAT",   120, "x...x...x...x...", "x.x.x.x.x.x.x.x.", "", "", "....x.......x...", "", ""},
    {"JUMPSTYLE",  150, "x...x...x...x...", "", "..x...x...x...x.", "....x.......x...", "", "", ""},
    /* the user's frenchcore loop (assets/kicks/frenchcore_loop.wav): 3 bars on the beat, the 4th with its fill */
    {"FRENCH LOOP", 200, "x...x...x...x...x...x...x...x...x...x...x...x...x...x...x.x...x.", "", "", "", "", "", "",
     "FRENCH LOOP", 0, 0},
};
#define NSTYLE (sizeof STYLE / sizeof *STYLE)
#endif
static const char *const STYLE_NAME[] = {"GABBER", "HAPPY HC", "EARLY HC", "FRENCHCORE", "UPTEMPO", "HARDTEK", "TERROR",
                                         "SPEEDCORE", "INDUSTRIAL", "DOOMCORE", "JUNGLE", "BREAKCORE", "RAVE 92", "NEW BEAT",
                                         "JUMPSTYLE", "FRENCH LOOP"};
static void kick_preset_sync(unsigned t);
static const struct { const char *name, *what, *k2, *k3; uint8_t lo2, hi2, def2, lo3, hi3, def3; } TOOL[T_NTOOL] = {
    [T_QUANT] = {"QUANTIZE", "pulls the notes onto the grid", "GRID", "SNAP", 1, 4, 2, 25, 100, 100},
    [T_COPY] = {"COPY", "keeps the track in memory", 0, 0, 0, 0, 0, 0, 0, 0},
    [T_PASTE] = {"PASTE", "puts back the track kept", 0, 0, 0, 0, 0, 0, 0, 0},
    [T_CLEAR] = {"CLEAR", "clears the track (the 909: the sound picked)", 0, 0, 0, 0, 0, 0, 0, 0},
    [T_DOUBLE] = {"DOUBLE", "the loop twice as long, copied", 0, 0, 0, 0, 0, 0, 0, 0},
    [T_HALF] = {"HALF", "keeps the first half", 0, 0, 0, 0, 0, 0, 0, 0},
    [T_SHIFT] = {"SHIFT", "every step moves earlier or later", "STEPS", 0, 0, 30, 16, 0, 0, 0},
    [T_TRANSPOSE] = {"TRANSPOSE", "the notes go up or down", "SEMITONES", 0, 0, 48, 25, 0, 0, 0},
#if TB_HAS_TOOLS_GEN
    [T_REVERSE] = {"REVERSE", "the loop played backwards", 0, 0, 0, 0, 0, 0, 0, 0},
#endif
#if TB_HAS_TOOLS_GEN
    [T_EUCLID] = {"EUCLID", "hits spread as evenly as can be", "HITS", "ROTATION", 1, 32, 5, 0, 31, 0},
#endif
#if TB_HAS_TOOLS_GEN
    [T_RANDOM] = {"RANDOM", "a random rhythm or melody", "DENSITY", 0, 0, 127, 64, 0, 0, 0},
#endif
#if TB_HAS_TOOLS_GEN
    [T_HUMAN] = {"HUMANIZE", "velocities a little random", "AMOUNT", 0, 0, 127, 40, 0, 0, 0},
#endif
#if TB_HAS_CLIP
    [T_PCOPY] = {"COPY PAT.", "keeps the whole pattern (every track)", 0, 0, 0, 0, 0, 0, 0, 0},
#endif
#if TB_HAS_CLIP
    [T_PPASTE] = {"PASTE PAT.", "puts back the pattern kept", 0, 0, 0, 0, 0, 0, 0, 0},
#endif
    [T_PCLEAR] = {"CLEAR PAT.", "clears the pattern's tracks", 0, 0, 0, 0, 0, 0, 0, 0},
#if TB_HAS_STYLE
    [T_STYLE] = {"STYLE", "a ready rhythm: kick, 909, break", "STYLE", "TEMPO", 0, NSTYLE - 1, 0, 0, 1, 1},
#endif
};
static const char *const TOOL_NAME[T_NTOOL] = {
    [T_QUANT] = "QUANTIZE",
    [T_COPY] = "COPY",
    [T_PASTE] = "PASTE",
    [T_CLEAR] = "CLEAR",
    [T_DOUBLE] = "DOUBLE",
    [T_HALF] = "HALF",
    [T_SHIFT] = "SHIFT",
    [T_TRANSPOSE] = "TRANSPOSE",
#if TB_HAS_TOOLS_GEN
    [T_REVERSE] = "REVERSE",
#endif
#if TB_HAS_TOOLS_GEN
    [T_EUCLID] = "EUCLID",
#endif
#if TB_HAS_TOOLS_GEN
    [T_RANDOM] = "RANDOM",
#endif
#if TB_HAS_TOOLS_GEN
    [T_HUMAN] = "HUMANIZE",
#endif
#if TB_HAS_CLIP
    [T_PCOPY] = "COPY PAT.",
#endif
#if TB_HAS_CLIP
    [T_PPASTE] = "PASTE PAT.",
#endif
    [T_PCLEAR] = "CLEAR PAT.",
#if TB_HAS_STYLE
    [T_STYLE] = "STYLE",
#endif
};
/* the live effects (FX held + the white keys) as the screen shows them */
/* (the user's order, 6 Nov 2026: what goes together side by side: the build and the drops, the filters and the bass,
 * the other drops, the space, then the rolls; BUILD ROLL on the black 9) */
enum { KA_RISER, KA_DROP, KA_DLONG, KA_DSTOP, KA_DKROLL, KA_DSNARE, KA_DLOW, KA_PF };
static const uint8_t KEY_ACT[16][2] = {           /* white k: what, and the live effect's bit (KA_PF) */
    {KA_RISER, 0}, {KA_DROP, 0}, {KA_DLONG, 0}, {KA_DSTOP, 0},
    {KA_PF, TB_PF_LPSWEEP}, {KA_PF, TB_PF_HPSWEEP}, {KA_PF, TB_PF_KILLBASS},
    {KA_DKROLL, 0}, {KA_DSNARE, 0}, {KA_DLOW, 0}, {KA_PF, TB_PF_WASH},
    {KA_PF, TB_PF_ROLL8}, {KA_PF, TB_PF_ROLL16}, {KA_PF, TB_PF_ROLL32}, {KA_PF, TB_PF_TROLL16}, {KA_PF, TB_PF_TROLL32}};
static uint16_t pf_lit;                            /* the whites held for a live effect (their tiles lit) */
#ifdef RFM2
/* the black keys 7..11 (FX held): STOP, KICK ROLL, SNARE ROLL, STUTTER, ECHO OUT - held */
static const uint8_t BK_PF[5] = {TB_PF_STOP, TB_PF_KROLL, TB_PF_SROLL, TB_PF_STUTTER, TB_PF_ECHO};
static uint8_t pf_black;                           /* those held */
#endif
static const char *const PF_TILE[16] = {"RISER", "DROP", "LONG\nDROP", "STOP\nDROP", "FILTER\nLP", "FILTER\nHP",
                                        "KILL\nBASS", "KICK\nROLL", "SNARE\nBAR", "LOW\nDROP", "REVERB\nWASH",
                                        "ROLL\n1/8", "ROLL\n1/16", "ROLL\n1/32", "TRACK\n1/16", "TRACK\n1/32"};

#define IS_RAVE_T(t) (tb_proj.trk[t].machine == TB_M_SYNTH && tb_proj.trk[t].p[TBS_WAVE] == TBS_RAVE)
static const pg_t *home_pages(unsigned t, unsigned *n)
{
    switch (tb_proj.trk[t].machine) {
    case TB_M_KICK: *n = 2; return HOME_KICK_PG;
    case TB_M_ACID: *n = 3; return HOME_ACID_PG;
    case TB_M_BREAK: *n = 3; return HOME_BRK_PG;
    case TB_M_909: *n = 3; return HOME_909_PG;
    case TB_M_SAMPLE: *n = 3; return HOME_SMP_PG;
    default: *n = 3; return IS_RAVE_T(t) ? HOME_RAVE_PG : HOME_SYN_PG;
    }
}
static const pg_t *edit_pages(unsigned t, unsigned *n)
{
    switch (tb_proj.trk[t].machine) {
    case TB_M_KICK: *n = sizeof KICK_PG / sizeof *KICK_PG; return KICK_PG;
    case TB_M_BREAK: *n = sizeof BREAK_PG / sizeof *BREAK_PG; return BREAK_PG;
    case TB_M_909: *n = sizeof D909_PG / sizeof *D909_PG; return D909_PG;
    case TB_M_SAMPLE: *n = sizeof SAMPLE_PG / sizeof *SAMPLE_PG; return SAMPLE_PG;
    case TB_M_ACID: *n = sizeof ACID_PG / sizeof *ACID_PG; return ACID_PG;
    default:
        if (IS_RAVE_T(t)) {
            *n = sizeof RAVE_PG / sizeof *RAVE_PG;
            return RAVE_PG;
        }
        *n = sizeof SYNTH_PG / sizeof *SYNTH_PG;
        return SYNTH_PG;
    }
}

/* ------------------------------------------------------------------ the state --- */
static tb_view_t view;
static uint32_t prev_keys, prev_btn, last_draw, hud_at, boot_at, bt_at[14];
static uint32_t idle_at, touch_btn, touch_keys;    /* the last time anything was touched (the dancer) */
static int touch_knob = -1;
#define DANCE_IDLE_MS 10000u                      /* the music playing, nothing touched: the dancer comes */
static uint8_t bt_used[14], bt_long[14];
static uint8_t sel, booting = 1, rec;
static uint8_t menu, menu_page[16];          /* the page open (TB_MENU_*), each one's page */
static uint8_t home_page[TB_TRACKS], preset[TB_TRACKS], last_mode[TB_TRACKS], seq_page[TB_TRACKS], piece[TB_TRACKS];
#if TB_TRACKS == 6
static uint8_t last_note[TB_TRACKS] = {60, 60, 36, 72, 60, 60};
#else
static uint8_t last_note[TB_TRACKS] = {60, 60, 60, 36, 72, 60, 60, 60};
#endif
static uint8_t tool, tool_p2[T_NTOOL], tool_p3[T_NTOOL], sys_new;
static int8_t octave;
static uint8_t key_note_on[27];
static uint32_t key_at[27];
static int16_t key_step[27];
static int16_t held = -1;
static uint8_t held_edit;
static uint8_t solo_mask;
static uint32_t pf_keys;
static uint8_t fx_lock;                              /* (RAVE FM 2, as JIANT) FX held a while, nothing touched, let go: the LIVE FX stay open */
#define FX_LAYER(b) (((b) & BTN(B_FX)) || fx_lock)
static tb_step_t clip_trk[TB_STEPS];
static uint8_t clip_trk_len, clip_trk_full, clip_trk_machine;
#if TB_HAS_CLIP
static tb_pattern_t clip_pat;
static uint8_t clip_pat_full;
#endif
static tb_lock_t clip_lk[TB_NLOCK];
static unsigned clip_nlk;
static char hud_buf[48];
static const char *hud_title;
#if TB_HAS_TOOLS_GEN
static uint32_t seed_ui = 0x2545F491u;
#endif
static uint8_t song_edit;                    /* GLO MORCEAU: the place edited */
static uint8_t man_sel;                      /* BREAK, EDIT A LA MAIN: the slice moved */
static uint8_t lock_page;                    /* SEQ, a step held: 0 its own knobs, then the pages its locks are set on */
static uint8_t proj_slot, slot_sel, sys_ask; /* the project's place, the one chosen in the MENU, a question asked */
static uint8_t proj_fresh = 1;               /* (the clean project of the power on: its place maybe someone else's) */
static uint32_t sys_ask_at;
#if TB_HAS_TAP
static uint32_t tap_at[5];                   /* tap tempo: the last taps */
static uint8_t taps;
#endif
/* undo: the song as it was before the last run of changes (one more press: as it is again). Of the pattern, the
 * track changed (every change is the selected track's; another track: a new run): ~7 KB less than the whole pattern,
 * the FM-1's RAM */
static struct {
    uint8_t valid, pattern, swing, root, scale, track;
    int8_t transpose;
    uint16_t bpm10;
    tb_fx_set_t fx;
    tb_track_t trk[TB_TRACKS];
    uint8_t len[TB_TRACKS];
    tb_step_t step[TB_STEPS];
    tb_lock_t lock[TB_NLOCK];
} undo;
static uint32_t touch_at;
static uint8_t undone;

/* the keys F3..G5: which are white, and each one's place among its colour */
static int key_white(unsigned k) { static const uint8_t W[12] = {1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1}; return W[k % 12]; }
static unsigned key_index(unsigned k)
{
    unsigned i, n = 0;
    for (i = 0; i < k; i++)
        n += key_white(i) == key_white(k);
    return n;
}
static unsigned white_key(unsigned i)
{
    unsigned k;
    for (k = 0; k < 27u; k++)
        if (key_white(k) && key_index(k) == i)
            return k;
    return 0;
}

/* a band of the screen to the panel: started, not waited for (the next band is drawn meanwhile); the one before it
 * waited for first (its buffer is the next one drawn into) */
static void emit(int x, int y, int w, int h, uint16_t *px)
{
    uint32_t t0 = fm1_millis();
    int r;
    while ((r = fm1_screen_pending()) > 0 && fm1_millis() - t0 < 40u)
        ;
    if (fm1_screen_submit((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, px, (size_t)w * (size_t)h) != 0)
        fm1_screen_blit((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, px, (size_t)w * (size_t)h);   /* (busy or failed: the slow way) */
}

static void hud_say(const char *title, const char *value, unsigned pos)
{
#ifdef RFM_HOST
    {   /* (the PC's tests: what the screen says, logged; tools/host/rfm_shot.c) */
        extern void rfm_host_hud(const char *, const char *);
        rfm_host_hud(title, value);
    }
#endif
    hud_title = title;
    if (value != hud_buf)
        snprintf(hud_buf, sizeof hud_buf, "%s", value ? value : "");
    view.hud = 0, view.hud_pos = (uint8_t)pos, hud_at = fm1_millis();
}

static unsigned len_of(unsigned t) { return TB_LEN(t) ? (TB_LEN(t) > TB_TCAP(t) ? TB_TCAP(t) : TB_LEN(t)) : 16u; }
static int is_drum(unsigned t) { return tb_proj.trk[t].machine == TB_M_909; }
/* SEQ's screen: the kick's and the 909's grid (what the 16 whites are: 1/8, 1/16, 1/32 steps; the 909 has no 32nds),
 * the window it shows (the whites' 16 cells: 32, 16 or 8 steps; a melody: 32 steps a screen) */
static uint8_t seq_res[TB_TRACKS] = {1, 1, 1, 1, 1, 1
#if TB_TRACKS > 6
                                       , 1, 1
#endif
};
static const char *const RESN[3] = {"1/8", "1/16", "1/32"};
static int grid_track(unsigned t) { return tb_proj.trk[t].machine == TB_M_KICK || tb_proj.trk[t].machine == TB_M_909; }
static unsigned res_of(unsigned t) { unsigned r = seq_res[t] % 3u; return tb_proj.trk[t].machine == TB_M_909 && r > 1u ? 1u : r; }
static unsigned win_steps(unsigned t) { return grid_track(t) ? (res_of(t) == 0u ? 32u : res_of(t) == 1u ? 16u : 8u) : 32u; }
/* white i (0..15) on the kick's or the 909's grid: its step; *half: the 32nd after it (1/32) */
static unsigned cell_step(unsigned t, unsigned i, unsigned *half)
{
    unsigned base = seq_page[t] * win_steps(t), r = res_of(t);
    *half = r == 2u ? (i & 1u) : 0u;
    return r == 0u ? base + 2u * i : r == 1u ? base + i : base + i / 2u;
}
static int melodic(unsigned t) { unsigned m = tb_proj.trk[t].machine; return m == TB_M_SYNTH || m == TB_M_SAMPLE || m == TB_M_BREAK || m == TB_M_ACID; }
static int arp_allowed(unsigned t) { return melodic(t); }
static uint8_t seq_strip, seq_cur[TB_TRACKS];  /* SEQ: its page (0 SEQ, 1 the cursor's step, 2 REGLAGES); the step edited */
static uint8_t seq_val[TB_TRACKS];             /* SEQ: a note's value (VALN), each track its own */
/* SEQ, nothing held: which of the cursor step's values knob k is (-1: none of them) */
static int seq_step_knob(unsigned k)
{
    if (held >= 0)
        return -1;
    if (seq_strip == 1u)
        return (int)k;
    /* (WRITE's third knob is the LOOP on every track: it once edited a drum's step there, the kick stuck at 16) */
    return -1;
}
static const char *seq_title(void) { return "WRITE"; }
static const uint8_t PER32[TB_NSPEED] = {4, 2, 1};   /* a step, in 32nds of a bar, at each speed */
/* REC's grid (TB_RECG_*; an older project's "as played": FREE) */
static unsigned rec_grid(void)
{
    return (tb_proj.rec_quant & 7u) >= 4u ? TB_RECG_FREE : (tb_proj.rec_quant >> 3) & 3u;
}
static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }   /* (rounded down) */
/* a hit put on track sel's steps at u, in 32nds of a step from the loop's start: on the nearest step, early or late
 * (-16..15); half a step after a step (every track but the 909): that step's 32nd (hits 2, or 3 with its own hit).
 * A step already playing something else: replaced (over) or left (the hit dropped: -1). The step it went on */
static int place_hit(tb_step_t *S, unsigned len, int u, const tb_step_t *e, int over)
{
    unsigned m = tb_proj.trk[sel].machine, L = len * 32u, st, pv;
    int mic, same;
    u = (int)(((u % (int)L) + (int)L) % (int)L);
    st = ((unsigned)u + 16u) / 32u, mic = u - (int)st * 32, st %= len;
    if (m == TB_M_909) {                              /* (the 909: its pieces together, on the step) */
        S[st].hits = TB_WITH_MICRO(TB_HITS(S[st].hits) | TB_HITS(e->hits), TB_HITS(S[st].hits) ? TB_MICRO(S[st].hits) : mic);
        if (!S[st].vel)
            S[st].vel = e->vel;
        return (int)st;
    }
    if (mic == -16) {                                 /* a 32nd after the step before: its half */
        tb_step_t *P = &S[pv = (st + len - 1u) % len];
        same = TB_HITS(P->hits) && (m == TB_M_KICK || P->note == e->note);
        if (!TB_HITS(P->hits) || (same && (TB_HITS(P->hits) & 3u) == 1u && !TB_MICRO(P->hits))) {
            if (!TB_HITS(P->hits))
                *P = *e, P->hits = 2u, P->gate = e->gate > 4u ? 4u : e->gate;
            else
                P->hits = (uint16_t)(P->hits | 2u);
            return (int)pv;
        }
    }
    same = TB_HITS(S[st].hits) && (m == TB_M_KICK || S[st].note == e->note);
    if (same && !mic && (TB_HITS(S[st].hits) & 3u) == 2u) {   /* (its 32nd there already: both) */
        S[st].hits = (uint16_t)(S[st].hits | 1u);
        return (int)st;
    }
    if (TB_HITS(S[st].hits) && !over)
        return same ? (int)st : -1;
    S[st] = *e, S[st].hits = TB_WITH_MICRO(1u, mic);
    return (int)st;
}

/* a note's value, in steps (at least one) */
static unsigned seq_steps(unsigned t)
{
    unsigned n = VAL32[seq_val[t] % 6u] / PER32[tb_proj.trk[t].speed % TB_NSPEED];
    return n ? n : 1u;
}
/* 32nds on a track too slow for them: it goes twice as fast, its steps twice as far (every pattern: the same music) */
static int stretch_track(unsigned t)
{
    unsigned p, i;
    if (tb_proj.trk[t].speed >= TB_NSPEED - 1u)
        return 1;
    for (p = 0; p < TB_PATTERNS; p++)
        if ((tb_proj.pat[p].len[t] ? tb_proj.pat[p].len[t] : 16u) > TB_TCAP(t) / 2u)   /* (twice: within its steps) */
            return 0;
    for (p = 0; p < TB_PATTERNS; p++) {
        tb_pattern_t *P = &tb_proj.pat[p];
        tb_step_t *S = TB_TSTEPS(P, t);
        unsigned L = P->len[t] ? P->len[t] : 16u;
        for (i = L; i-- > 0;) {
            tb_step_t st = S[i];
            int m = TB_MICRO(st.hits) * 2;
            unsigned g = st.gate * 2u;
            st.hits = TB_WITH_MICRO(st.hits, m < -16 ? -16 : m > 15 ? 15 : m);
            st.gate = (uint8_t)(g > 255u ? 255u : g);
            S[2u * i] = st;
            memset(&S[2u * i + 1u], 0, sizeof *S);
        }
        P->len[t] = (uint8_t)(L * 2u);
        for (i = 0; i < TB_NLOCK; i++)
            if (P->lock[i].ref1 && P->lock[i].track == t)
                P->lock[i].step = (uint8_t)(P->lock[i].step * 2u);
    }
    tb_proj.trk[t].speed++;
    seq_cur[t] = (uint8_t)(seq_cur[t] * 2u);
    return 1;
}
/* the step the knobs edit: the one held, else (SEQ) the cursor's */
static tb_step_t *held_step(void)
{
    if (held >= 0 && (unsigned)held < TB_TCAP(sel))
        return &TB_STEP(sel, (unsigned)held);
    if (menu == TB_MENU_SEQ)
        return &TB_STEP(sel, seq_cur[sel] % TB_TCAP(sel));
    return 0;
}

/* ------------------------------------------------- EDIT (SEQ's second page): the notes one by one --- */
/* The loop's notes (on the 909: each hit, one sound on one step): KNOB 1 (or SELECT) CHOOSE goes from one to the
 * next (it plays), KNOB 2 its LENGTH, KNOB 3 TRANSPOSE (the kick: semitones; the break: its slice; the 909: its sound);
 * OCT- deletes it (said on the 4th knob's place; ARP too); a key gives it its note (the kick and the 909: a white key
 * chooses the hit on that step) */
#define NED_MAX 400
static uint8_t ned_at[TB_TRACKS], ned_pc[TB_TRACKS];   /* the note chosen: its step, its 909 sound */
/* a chord set by hand: the notes of a step (0: its note, 1, 2: the others); one added (0: no room), one taken off */
static int hand_ok(unsigned t) { unsigned m = tb_proj.trk[t].machine; return m == TB_M_SYNTH || m == TB_M_SAMPLE || m == TB_M_BREAK; }
static uint8_t *step_note(tb_step_t *st, unsigned k) { return k ? &st->more[(k - 1u) % 3u] : &st->note; }
static int step_has(const tb_step_t *st, unsigned n) { return st->note == n || st->more[0] == n || st->more[1] == n || st->more[2] == n; }
static int step_add(tb_step_t *st, unsigned n)
{
    if (step_has(st, n))
        return 1;
    unsigned j;
    for (j = 0; j < 3u; j++)                          /* (returns its place + 1: 2..4) */
        if (!st->more[j])
            return st->more[j] = (uint8_t)n, (int)j + 2;
    return 0;
}
/* EDIT on a chord: CHOOSE gives first the whole chord (-1), then each of its notes (0..2); a single note: 0 */
static int ned_k(const tb_step_t *st);
static void step_drop(tb_step_t *st, unsigned k)  /* (the last one: the step empty) */
{
    if (k == 0u && !st->more[0]) {
        memset(st, 0, sizeof *st);
        return;
    }
    if (k == 0u)
        st->note = st->more[0], k = 1u;
    for (; k < 3u; k++)                               /* (the ones after it: one place down) */
        st->more[k - 1u] = st->more[k];
    st->more[2] = 0;
}

/* the hits of track sel in order, each step << 4 | its 909 sound (a melody: which of its notes); how many */
static unsigned ned_list(uint16_t *L)
{
    unsigned i, pc, n = 0, len = len_of(sel);
    for (i = 0; i < len && i < TB_TCAP(sel); i++) {
        unsigned h = TB_HITS(TB_STEP(sel, i).hits);
        if (!h)
            continue;
        if (is_drum(sel)) {
            for (pc = 0; pc < TB909_PIECES && n < NED_MAX; pc++)
                if ((h >> pc) & 1u)
                    L[n++] = (uint16_t)(i << 4 | pc);
        } else {
            const tb_step_t *st = &TB_STEP(sel, i);
            L[n++] = (uint16_t)(i << 4);              /* (a chord: the whole of it, then each note) */
            for (pc = 1; st->more[0] && pc <= 4u && n < NED_MAX; pc++)
                if (pc < 3u || st->more[pc - 2u])
                    L[n++] = (uint16_t)(i << 4 | pc);
        }
    }
    return n;
}
/* the note chosen (its place in the list; -1: none): the one chosen, or the next one there is */
static int ned_index(const uint16_t *L, unsigned n)
{
    unsigned i, want = (unsigned)ned_at[sel] << 4 | ned_pc[sel];
    if (!n)
        return -1;
    for (i = 0; i < n; i++)
        if (L[i] >= want)
            break;
    if (i == n)
        i = 0;
    ned_at[sel] = (uint8_t)(L[i] >> 4), ned_pc[sel] = (uint8_t)(L[i] & 15u);
    if (is_drum(sel))
        piece[sel] = ned_pc[sel];
    return (int)i;
}
static int ned_k(const tb_step_t *st) { return !st->more[0] ? 0 : ned_pc[sel] ? (int)ned_pc[sel] - 1 : -1; }
static unsigned ned_pitch(tb_step_t *st) { int k = ned_k(st); return *step_note(st, k < 0 ? 0u : (unsigned)k); }
static void ned_hear(void)
{
    tb_step_t *st = &TB_STEP(sel, ned_at[sel]);
    unsigned nn = is_drum(sel) ? ned_pc[sel] : ned_pitch(st) ? ned_pitch(st) : 60u;
    tb_engine_preview(sel, nn, st->vel ? st->vel : 100u, 220);
}
/* the locks of step a moved to step b (b 255: gone) */
static void ned_locks(unsigned a, unsigned b)
{
    tb_pattern_t *P = &tb_proj.pat[tb_proj.pattern];
    unsigned j;
    for (j = 0; j < TB_NLOCK; j++)
        if (P->lock[j].ref1 && P->lock[j].track == sel && P->lock[j].step == a) {
            if (b == 255u)
                P->lock[j].ref1 = 0;
            else
                P->lock[j].step = (uint8_t)b;
        }
}

/* ------------------------------------------------------------ values by reference --- */
static uint8_t *ins_field(unsigned ref)
{
    tb_track_t *t = &tb_proj.trk[sel];
    unsigned slot = ref >= R_INSB ? 1u : 0u, k = ref - (slot ? R_INSB : R_INSA);
    return k ? &t->ins[slot].p[k - 1u] : &t->ins[slot].type;
}

#if TB_HAS_MAN_SLICE
/* BREAK by hand: the slices as they are cut now become the hand-set ones (DIV: MAN) */
static tb_brk_man_t *man_take(void)
{
    tb_track_t *t = &tb_proj.trk[sel];
    if (t->p[TBB_DIV] != TBB_MAN || !t->man.n) {
        uint32_t a, b;
        unsigned n = tb_brk_slices(t->p, &t->man, 0, &a, &b), i;
        if (n > TBB_MAXSLICE)
            n = TBB_MAXSLICE;
        for (i = 0; i < n; i++)
            tb_brk_slices(t->p, &t->man, i, &a, &b), t->man.at[i] = (uint16_t)(a / 4u);
        t->man.n = (uint8_t)n, t->p[TBB_DIV] = TBB_MAN;
    }
    if (man_sel >= t->man.n)
        man_sel = (uint8_t)(t->man.n - 1u);
    return &t->man;
}
static uint32_t man_len4(void) { return tb_brk_len(tb_proj.trk[sel].p[TBB_SRC]) / 4u; }
static void man_move(tb_brk_man_t *m, unsigned i, int32_t at)
{
    int32_t lo = i ? m->at[i - 1u] + 50 : 0, hi = i + 1u < m->n ? m->at[i + 1u] - 50 : (int32_t)man_len4() - 50;
    m->at[i] = (uint16_t)(at < lo ? lo : at > hi ? hi : at);
}
#endif

static int get(unsigned ref)
{
    tb_track_t *t = &tb_proj.trk[sel];
    if (ref < 32u)
        return t->p[ref];
    if (ref >= R_TVOL && ref < R_TVOL + TB_TRACKS)
        return tb_proj.trk[ref - R_TVOL].vol;
    if (ref == R_INSB) {                              /* (MOD: its effect as a place in its list) */
        unsigned i;
        for (i = 0; i < sizeof MOD_TYPE; i++)
            if (MOD_TYPE[i] == t->ins[1].type)
                return (int)i;
        return 0;
    }
    if (ref >= R_INSA && ref <= R_INSB3)
        return *ins_field(ref);
    if (ref >= R_STK && ref <= R_STK_END)
        return tb_proj.stk[sel][(ref - R_STK) / 3].p[(ref - R_STK) % 3];
    if (ref >= R_STKM && ref <= R_STKM_END)
        return tb_proj.stk_mix[sel][ref - R_STKM];
    if (ref >= R_FXV && ref <= R_FXV_END)             /* the DROP's and the RISER's sounds: 0..20 (5 % a step) */
        return (int)(TB_FXVOL(ref - R_FXV) + 2u) / 5;
    switch (ref) {
    case R_VOL: return t->vol;
    case R_FILT: return t->filt;
    case R_REV: return t->rev;
    case R_DLY: return t->dly;
    case R_DUCK: return t->duck;
    case R_LEN: return (int)len_of(sel);
    case R_SPEED: return t->speed;
    case R_PAN: return t->pan;
    case R_ARP: return t->arp;
    case R_ARPRATE: return t->arp_rate;
    case R_ARPGATE: return t->arp_gate;
    case R_ARPOCT: return t->arp_oct;
    case R_ARPLATCH: return t->arp_latch;
    case R_ARPRHY: return t->arp_rhythm;
    case R_ARPSWING: return t->arp_swing;
    case R_ARPACC: return t->arp_accent;
    case R_LFOWAVE: return t->lfo_wave;
    case R_LFORATE: return t->lfo_rate;
    case R_LFODEPTH: return t->lfo_depth;
    case R_LFODEST: return t->lfo_dest;
    case R_LFOSYNC: return t->lfo_sync;
    case R_LFORETRIG: return t->lfo_retrig;
    case R_ROOT: return tb_proj.root;
    case R_SCALE: return tb_proj.scale;
    case R_QUANT: return tb_proj.quantize;
    case R_CHORD: return t->chord;
    case R_INV: return t->inversion;
    case R_VOICING: return t->voicing;
    case R_REVTYPE: return tb_proj.fx.rev_type;
    case R_REVSIZE: return tb_proj.fx.rev_size;
    case R_REVDAMP: return tb_proj.fx.rev_damp;
    case R_REVPRE: return tb_proj.fx.rev_pre;
    case R_REVLEV: return tb_proj.fx.rev_level;
    case R_DLYTYPE: return tb_proj.fx.dly_type;
    case R_DLYTIME: return tb_proj.fx.dly_time;
    case R_DLYFB: return tb_proj.fx.dly_fb;
    case R_DLYTONE: return tb_proj.fx.dly_tone;
    case R_DLYLEV: return tb_proj.fx.dly_level;
    case R_EQL: return tb_proj.fx.eq_low;
    case R_EQM: return tb_proj.fx.eq_mid;
    case R_EQH: return tb_proj.fx.eq_high;
    case R_COMP: return tb_proj.fx.comp;
    case R_DRIVE: return tb_proj.fx.drive;
    case R_WIDTH: return tb_proj.fx.width;
    case R_TRANSPO: return t->transpose + 24;
    case R_GTRANSPO: return tb_proj.transpose + 24;
    case R_BPM: return tb_proj.bpm10 / 10;
    case R_SWING: return tb_proj.swing;
    case R_PAT: return tb_engine_queued_pattern() >= 0 ? tb_engine_queued_pattern() : tb_proj.pattern;
    case R_PAL: return (int)gfx_palette_now();
    case R_LIGHT: return tb_proj.pad & 3u;
    case R_D9 + D9_TUNE: case R_D9 + D9_DECAY: case R_D9 + D9_MORPH: case R_D9 + D9_LEVEL:
        return ((const uint8_t *)tb_proj.trk[sel].man.at)[(piece[sel] % TB909_PIECES) * D9_N + (ref - R_D9)];
    case R_DSEL: return drop_sel;
    case R_DLEN: return (int)tb_drop_get(drop_sel, TB_DSET_LEN);
    case R_DROL: return (int)tb_drop_get(drop_sel, TB_DSET_ROLL);
    case R_DFIL: return (int)tb_drop_get(drop_sel, TB_DSET_FILT);
    case R_DKCK: return (int)tb_drop_get(drop_sel, TB_DSET_KICK);
    case R_DEND: return (int)tb_drop_get(drop_sel, TB_DSET_END);
    case R_DBARS: return tb_proj.drop_bars;
    case R_DROLL: return tb_proj.drop_roll;
    case R_DFILT: return tb_proj.drop_filter;
    case R_DCRASH: return tb_proj.drop_crash;
    case R_MAMT: return tb_proj.mut_amount;
    case R_MFILLS: return tb_proj.mut_fills;
    case R_MTARGET: return tb_proj.mut_target;
    case R_MON: return tb_engine_mutating();
    case R_TEQL: return t->eq[0];
    case R_TEQM: return t->eq[1];
    case R_TEQH: return t->eq[2];
    case R_DTYPE: return tb_proj.fx.dist_type;
    case R_DGAIN: return tb_proj.fx.dist_gain;
    case R_DTONE: return tb_proj.fx.dist_tone;
    case R_DMIX: return tb_proj.fx.dist_mix;
    case R_RBARS: return tb_proj.riser_bars;
    case R_RFILT: return tb_proj.riser_filter;
    case R_RGAP: return tb_proj.riser_gap;
    case R_RIMP: return tb_proj.riser_impact;
    case R_RTYPE: return tb_proj.riser_type % (TB_NRISER + 1u);
    case R_RLVL: return tb_proj.riser_type >= TB_NRISER ? tb_proj.riser_filter != 0 : (tb_proj.riser_lvl[tb_proj.riser_type] + 2) / 5;
    case R_REND: return (tb_proj.riser_gap ? 1 : 0) | (tb_proj.riser_impact ? 2 : 0);
    case R_METRO: return tb_proj.metro <= 8u ? tb_proj.metro : 0;
    case R_MVOL: return tb_proj.metro_vol <= 4u ? tb_proj.metro_vol : 0;
    case R_SYNC: return tb_proj.sync;
    case R_MIDIOUT: return tb_proj.midi_out;
    case R_SLOT: return slot_sel;
    case R_PNPOS: return pn_pos;
    case R_PNCHR: return (int)pn_idx(tb_proj.pname[pn_pos & 7u]);
    case R_SPOS: return song_edit;
    case R_RQUANT: return (tb_proj.rec_quant & 7u) < 4u ? tb_proj.rec_quant & 7u : 0;
    case R_RGRID: return KNOB_OF_GRID[rec_grid()];
    case R_TCHO: return t->chorus;
    case R_CHORATE: return tb_proj.cho_rate[sel];
    case R_RSEL: return route_sel % TB_NFX;
    case R_RPOS: {
        int j;
        for (j = 0; j < 5; j++)
            if (tb_proj.route_ord[sel][j] == route_sel % TB_NFX)
                return j + 1;
        return 1;
    }
    case R_RPAR: return tb_proj.route_par[sel] >> (route_sel % TB_NFX) & 1;
    case R_RSEND: return tb_proj.route_send[sel] != 0;
    case R_CHODEP: return tb_proj.cho_depth[sel];
    case R_SCUR: return seq_cur[sel];
    case R_SPAGE: return seq_page[sel];
    case R_SPIECE: return piece[sel];
    case R_SRES: return (int)res_of(sel);
    case R_SVAL: return seq_val[sel] % 6u;
#if TB_HAS_MAN_SLICE
    case R_MSEL: return man_sel;
    case R_MPOS: return t->p[TBB_DIV] == TBB_MAN && t->man.n && man_len4() ? (int)((uint32_t)t->man.at[man_sel % t->man.n] * 1000u / man_len4()) : 0;
    case R_MFINE: return 64;
    case R_MNUM: return t->p[TBB_DIV] == TBB_MAN && t->man.n ? t->man.n : 1;
#endif
    case R_USMP: return (int)tb_brk_user.n;
    case R_SPAT: return tb_proj.song_pat[song_edit % TB_SONG] > TB_PATTERNS ? TB_PATTERNS : tb_proj.song_pat[song_edit % TB_SONG];
    case R_SBARS: return tb_proj.song_bars[song_edit % TB_SONG] ? tb_proj.song_bars[song_edit % TB_SONG] : 1;
    case R_SMODE: return tb_proj.song_on;
    case R_TOOL: return tool;
    case R_TP1: return tool_p2[tool];
    case R_TP2: return tool_p3[tool];
    default: return 0;
    }
}

static void touch(void);
static void set(unsigned ref, int v)
{
    tb_track_t *t = &tb_proj.trk[sel];
    uint8_t b = (uint8_t)v;
    if (ref != R_TOOL && ref != R_TP1 && ref != R_TP2 && ref != R_SLOT && ref != R_PNPOS && ref != R_MON && ref != R_PAL && ref != R_LIGHT && ref != R_SPOS && ref != R_SCUR && ref != R_SVAL && ref != R_RSEL &&
        ref != R_SPAGE && ref != R_SPIECE && ref != R_SRES && ref != R_DSEL)
        touch();
    if (ref < 32u) {
        if (t->p[ref] != b && t->machine == TB_M_SYNTH && ref == TBS_MODE)
            tb_engine_panic(sel);
        t->p[ref] = b;
        return;
    }
    if (ref >= R_TVOL && ref < R_TVOL + TB_TRACKS) {
        tb_proj.trk[ref - R_TVOL].vol = b;
        return;
    }
    if (ref >= R_STK && ref <= R_STK_END) {
        tb_proj.stk[sel][(ref - R_STK) / 3].p[(ref - R_STK) % 3] = b;
        return;
    }
    if (ref >= R_STKM && ref <= R_STKM_END) {
        tb_proj.stk_mix[sel][ref - R_STKM] = b;
        return;
    }
    if (ref >= R_FXV && ref <= R_FXV_END) {
        tb_proj.fx_vol[ref - R_FXV] = (uint8_t)((b > 20u ? 20u : b) * 5u + 1u);
        return;
    }
    if (ref == R_INSB)
        b = MOD_TYPE[b < sizeof MOD_TYPE ? b : 0];
    if (ref >= R_INSA && ref <= R_INSB3) {
        uint8_t *f = ins_field(ref);
        if ((ref == R_INSA || ref == R_INSB) && *f != b) {     /* a new effect: its knobs to where it sounds */
            tb_ins_set_t *ins = &t->ins[ref == R_INSB];
            static const uint8_t D[TB_NINS][3] = {{0, 0, 0}, {60, 70, 100}, {60, 60, 100}, {50, 60, 90}, {64, 20, 100},
                                                 {50, 64, 90}, {40, 60, 60}, {30, 60, 70}, {30, 70, 60}, {60, 70, 0},
                                                 {50, 0, 90}, {60, 40, 0}, {40, 40, 0}, {60, 60, 0}, {64, 70, 100},
                                                 {60, 60, 40}};
            ins->p[0] = D[b % TB_NINS][0], ins->p[1] = D[b % TB_NINS][1], ins->p[2] = D[b % TB_NINS][2];
        }
        *f = b;
        return;
    }
    switch (ref) {
    case R_VOL: t->vol = b; break;
    case R_FILT: t->filt = b; break;
    case R_REV: t->rev = b; break;
    case R_DLY: t->dly = b; break;
    case R_DUCK: t->duck = b; break;
    case R_LEN: TB_LEN(sel) = (uint8_t)(b > (int)TB_TCAP(sel) ? (int)TB_TCAP(sel) : b); break;
    case R_SPEED: t->speed = b; break;
    case R_PAN: t->pan = b; break;
    case R_ARP:
        if (!b || !t->arp)
            tb_engine_panic(sel);
        t->arp = b;
        if (b)
            last_mode[sel] = b;
        break;
    case R_ARPRATE: t->arp_rate = b; break;
    case R_ARPGATE: t->arp_gate = b; break;
    case R_ARPOCT: t->arp_oct = b; break;
    case R_ARPLATCH: t->arp_latch = b; if (!b) tb_engine_panic(sel); break;
    case R_ARPRHY: t->arp_rhythm = b; break;
    case R_ARPSWING: t->arp_swing = b; break;
    case R_ARPACC: t->arp_accent = b; break;
    case R_LFOWAVE: t->lfo_wave = b; break;
    case R_LFORATE: t->lfo_rate = b; break;
    case R_LFODEPTH: t->lfo_depth = b; break;
    case R_LFODEST: t->lfo_dest = b; break;
    case R_LFOSYNC: t->lfo_sync = b; if (b && t->lfo_rate >= TB_LFO_NSYNC) t->lfo_rate = 4; break;
    case R_LFORETRIG: t->lfo_retrig = b; break;
    case R_ROOT: tb_proj.root = b; break;
    case R_SCALE: tb_proj.scale = b; break;
    case R_QUANT: tb_proj.quantize = b; break;
    case R_CHORD: t->chord = b; tb_engine_panic(sel); break;
    case R_INV: t->inversion = b; break;
    case R_VOICING: t->voicing = b; break;
    case R_REVTYPE: tb_proj.fx.rev_type = b; break;
    case R_REVSIZE: tb_proj.fx.rev_size = b; break;
    case R_REVDAMP: tb_proj.fx.rev_damp = b; break;
    case R_REVPRE: tb_proj.fx.rev_pre = b; break;
    case R_REVLEV: tb_proj.fx.rev_level = b; break;
    case R_DLYTYPE: tb_proj.fx.dly_type = b; break;
    case R_DLYTIME: tb_proj.fx.dly_time = b; break;
    case R_DLYFB: tb_proj.fx.dly_fb = b; break;
    case R_DLYTONE: tb_proj.fx.dly_tone = b; break;
    case R_DLYLEV: tb_proj.fx.dly_level = b; break;
    case R_EQL: tb_proj.fx.eq_low = b; break;
    case R_EQM: tb_proj.fx.eq_mid = b; break;
    case R_EQH: tb_proj.fx.eq_high = b; break;
    case R_COMP: tb_proj.fx.comp = b; break;
    case R_DRIVE: tb_proj.fx.drive = b; break;
    case R_WIDTH: tb_proj.fx.width = b; break;
    case R_TRANSPO: t->transpose = (int8_t)(v - 24); break;
    case R_GTRANSPO: tb_proj.transpose = (int8_t)(v - 24); break;
    case R_BPM: tb_proj.bpm10 = (uint16_t)(v * 10); break;
    case R_SWING: tb_proj.swing = b; break;
    case R_PAT:
        if (tb_engine_playing())
            tb_engine_queue_pattern((unsigned)v);
        else
            tb_proj.pattern = b, tb_engine_queue_pattern(255);
        break;
    case R_PAL: gfx_palette((unsigned)v); tb_proj.palette = b; break;
    case R_LIGHT: tb_proj.pad = (uint8_t)(b & 3u); break;
    case R_D9 + D9_TUNE: case R_D9 + D9_DECAY: case R_D9 + D9_MORPH: case R_D9 + D9_LEVEL:
        ((uint8_t *)tb_proj.trk[sel].man.at)[(piece[sel] % TB909_PIECES) * D9_N + (ref - R_D9)] = b;
        break;
    case R_DSEL: drop_sel = (uint8_t)(b % TB_NDROP); break;
    case R_DLEN: tb_drop_set(drop_sel, TB_DSET_LEN, b); break;
    case R_DROL: tb_drop_set(drop_sel, TB_DSET_ROLL, b); break;
    case R_DFIL: tb_drop_set(drop_sel, TB_DSET_FILT, b); break;
    case R_DKCK: tb_drop_set(drop_sel, TB_DSET_KICK, b); break;
    case R_DEND: tb_drop_set(drop_sel, TB_DSET_END, b); break;
    case R_DBARS: tb_proj.drop_bars = b; break;
    case R_DROLL: tb_proj.drop_roll = b; break;
    case R_DFILT: tb_proj.drop_filter = b; break;
    case R_DCRASH: tb_proj.drop_crash = b; break;
    case R_MAMT: tb_proj.mut_amount = b; break;
    case R_MFILLS: tb_proj.mut_fills = b; break;
    case R_MTARGET: tb_proj.mut_target = b; break;
    case R_MON: tb_engine_mutate(b); break;
    case R_TEQL: t->eq[0] = b; break;
    case R_TEQM: t->eq[1] = b; break;
    case R_TEQH: t->eq[2] = b; break;
    case R_DTYPE: tb_proj.fx.dist_type = b; break;
    case R_DGAIN: tb_proj.fx.dist_gain = b; break;
    case R_DTONE: tb_proj.fx.dist_tone = b; break;
    case R_DMIX: tb_proj.fx.dist_mix = b; break;
    case R_RBARS: tb_proj.riser_bars = b; break;
    case R_RFILT: tb_proj.riser_filter = b; break;
    case R_RGAP: tb_proj.riser_gap = b; break;
    case R_RIMP: tb_proj.riser_impact = b; break;
    case R_RTYPE: tb_proj.riser_type = b; break;
    case R_RLVL:
        if (tb_proj.riser_type >= TB_NRISER)
            tb_proj.riser_filter = b != 0;
        else
            tb_proj.riser_lvl[tb_proj.riser_type] = (uint8_t)(b > 20u ? 100u : b * 5u);
        break;
    case R_REND: tb_proj.riser_gap = b & 1u, tb_proj.riser_impact = (b >> 1) & 1u; break;
    case R_METRO: tb_proj.metro = b; break;
    case R_MVOL: tb_proj.metro_vol = b; break;
    case R_SYNC: tb_proj.sync = b; break;
    case R_MIDIOUT: tb_proj.midi_out = b; break;
    case R_SLOT: slot_sel = b, sys_ask = 0; break;
    case R_PNPOS: pn_pos = b & 7u; break;
    case R_PNCHR: {                                   /* (a letter set; the spaces at the end: none, 0) */
        int k;
        tb_proj.pname[pn_pos & 7u] = b ? PN_SET[b < sizeof PN_SET - 1u ? b : 0u] : ' ';
        for (k = 7; k >= 0 && (tb_proj.pname[k] == ' ' || !tb_proj.pname[k]); k--)
            tb_proj.pname[k] = 0;
        for (k = 0; k < 8; k++)
            if (!tb_proj.pname[k] && k < 7 && tb_proj.pname[k + 1])
                tb_proj.pname[k] = ' ';
        break;
    }
    case R_SPOS: song_edit = b; break;
    case R_RQUANT: tb_proj.rec_quant = (uint8_t)((rec_grid() << 3) | (b & 3u)); break;
    case R_RGRID: tb_proj.rec_quant = (uint8_t)((GRID_OF_KNOB[b & 3u] << 3) | ((tb_proj.rec_quant & 7u) & 3u)); break;
    case R_TCHO: t->chorus = b; break;
    case R_CHORATE: tb_proj.cho_rate[sel] = b; break;
    case R_RSEL: route_sel = b % TB_NFX; break;
    case R_RPOS: {                                    /* the effect taken out of the order, put back at this place */
        uint8_t *o = tb_proj.route_ord[sel], e = route_sel % TB_NFX, n[5];
        int j, k = 0, at = b < 1 ? 0 : b > 5 ? 4 : b - 1;
        for (j = 0; j < 5; j++)
            if (o[j] != e)
                n[k++] = o[j];
        for (j = 4; j > at; j--)
            n[j] = n[j - 1];
        n[at] = e;
        memcpy(o, n, 5);
        break;
    }
    case R_RPAR:
        tb_proj.route_par[sel] = (uint8_t)((tb_proj.route_par[sel] & ~(1u << (route_sel % TB_NFX))) | ((b & 1u) << (route_sel % TB_NFX)));
        break;
    case R_RSEND: tb_proj.route_send[sel] = b & 1u; break;
    case R_CHODEP: tb_proj.cho_depth[sel] = b; break;
    case R_SVAL:                                      /* a value: 32nds want a fast enough track */
        while (!b && tb_proj.trk[sel].speed < TB_NSPEED - 1u) {
            touch();
            if (!stretch_track(sel))
                return;                               /* (too long: the value stays) */
        }
        seq_val[sel] = b;
        break;
    case R_SPAGE: seq_page[sel] = b; break;           /* WRITE, the kick and the 909: which 16 steps the whites are */
    case R_SPIECE: piece[sel] = (uint8_t)(b % TB909_PIECES), tb_engine_preview(sel, piece[sel], 110, 180); break;
    case R_SRES: {                                    /* the grid: the window zooms, the playhead's bar kept */
        unsigned at = seq_page[sel] * win_steps(sel);
        seq_res[sel] = (uint8_t)b;
        seq_page[sel] = (uint8_t)(at / win_steps(sel));
        break;
    }
    case R_SCUR: {                                    /* the cursor: its frame follows, its step heard */
        const tb_step_t *st;
        seq_cur[sel] = b, seq_page[sel] = (uint8_t)(b / win_steps(sel));
        st = &TB_STEP(sel, b);
        if (TB_HITS(st->hits)) {
            unsigned nn = is_drum(sel) ? piece[sel] : st->note ? st->note : 60u;
            tb_engine_preview(sel, nn, st->vel ? st->vel : 100u, 180);
        }
        break;
    }
#if TB_HAS_MAN_SLICE
    case R_MSEL: {                                    /* (the slice heard as it is chosen) */
        tb_brk_man_t *m = man_take();
        man_sel = (uint8_t)(b < m->n ? b : m->n - 1u);
        tb_engine_preview(sel, (unsigned)(60 + ((int)man_sel - t->p[TBB_START] + 32 * TBB_MAXSLICE) % m->n), 110, 250);
        break;
    }
    case R_MPOS: { tb_brk_man_t *m = man_take(); man_move(m, man_sel, (int32_t)((uint32_t)v * man_len4() / 1000u)); break; }
    case R_MFINE: { tb_brk_man_t *m = man_take(); man_move(m, man_sel, (int32_t)m->at[man_sel] + (v - 64) * 8); break; }
    case R_MNUM: {                                    /* more: the longest cut in two; fewer: the last one goes */
        tb_brk_man_t *m = man_take();
        while (m->n < b && m->n < TBB_MAXSLICE) {
            unsigned i, best = 0;
            uint32_t bl = 0;
            for (i = 0; i < m->n; i++) {
                uint32_t e = i + 1u < m->n ? m->at[i + 1u] : man_len4(), l = e - m->at[i];
                if (l > bl)
                    bl = l, best = i;
            }
            if (bl < 200u)
                break;
            for (i = m->n; i > best + 1u; i--)
                m->at[i] = m->at[i - 1u];
            m->at[best + 1u] = (uint16_t)(m->at[best] + bl / 2u);
            m->n++;
        }
        if (b < m->n)
            m->n = b < 1 ? 1 : b;
        break;
    }
#endif
    case R_USMP:                                      /* fewer: the last ones gone (and the flash kept so) */
        if (b < tb_brk_user.n) {
            tb_brk_user_keep(b);
            tb_store_samples_save();
            hud_say("SAMPLES", b ? "LAST ONES CLEARED" : "ALL CLEARED", 0);
        }
        break;
    case R_SPAT: tb_proj.song_pat[song_edit % TB_SONG] = b; break;
    case R_SBARS: tb_proj.song_bars[song_edit % TB_SONG] = b; break;
    case R_SMODE: tb_proj.song_on = b; break;
    case R_TOOL: tool = b; break;
    case R_TP1: tool_p2[tool] = b; break;
    case R_TP2: tool_p3[tool] = b; break;
    default: break;
    }
}

static void note_text(char *o, size_t n, int v) { snprintf(o, n, "%s%d", NOTE_NAME[v % 12], v / 12 - 1); }

static void value_text(char *o, size_t n, const pd_t *d, int v)
{
    switch (d->fmt) {
    case F_RAW: snprintf(o, n, "%d", v); break;
    case F_PLUS1: snprintf(o, n, "%d", v + 1); break;
    case F_ENUM:
        if (d->names)
            snprintf(o, n, "%s", d->names[v < (int)d->min ? d->min : v > (int)d->max ? d->max : v]);
        else
            snprintf(o, n, "%s", TB_KICK_TYPE_NAME[(unsigned)v < TB_KICK_NTYPES ? v : 0]);
        break;
    case F_SEMI: snprintf(o, n, v == 24 ? "0" : "%+d", v - 24); break;
    case F_TRANSPO: snprintf(o, n, v == 24 ? "0" : "%+d", v - 24); break;
    case F_KSEMI: snprintf(o, n, v == 60 ? "0" : "%+d", v - 60); break;
    case F_BIP: snprintf(o, n, v == 64 ? "0" : "%+d", v - 64); break;
    case F_EQ: snprintf(o, n, v == 64 ? "0 dB" : "%+ddB", (v - 64) * 12 / 64); break;
    case F_TEQ:
        if (!v)
            snprintf(o, n, "CUT");
        else if (v == 64)
            snprintf(o, n, "0 dB");
        else if (v > 64)
            snprintf(o, n, "+%ddB", (v - 64) * 6 / 63 ? (v - 64) * 6 / 63 : 1);
        else {                                        /* (v / 64)^2: 20 log10 */
            static const uint8_t DB[9] = {40, 30, 24, 18, 13, 9, 6, 3, 1};
            snprintf(o, n, "-%udB", DB[v * 8 / 64]);
        }
        break;
    case F_PNAME: {                                   /* (v25) the name, the letter being set in [ ] */
        char nm[9];
        unsigned k;
        for (k = 0; k < 8u; k++)
            nm[k] = tb_proj.pname[k] ? tb_proj.pname[k] : ' ';
        nm[8] = 0;
        snprintf(o, n, "%.*s[%c]%s", v, nm, nm[v & 7], nm + (v & 7) + 1);
        for (k = (unsigned)strlen(o); k > 0 && o[k - 1u] == ' '; k--)
            o[k - 1u] = 0;
        break;
    }
    case F_PCHR: snprintf(o, n, v ? "%c" : "SPACE", PN_SET[v < (int)sizeof PN_SET - 1 ? v : 0]); break;
    case F_SLOT: {
        unsigned bpm;
        if (slot_name[v % TB_STORE_SLOTS][0])
            snprintf(o, n, "%d \xc2\xb7 %s", v + 1, slot_name[v % TB_STORE_SLOTS]);
        else if (tb_store_info((unsigned)v, &bpm, 0))
            snprintf(o, n, "%d \xc2\xb7 %u", v + 1, bpm / 10u);
        else
            snprintf(o, n, "%d \xc2\xb7 EMPTY", v + 1);
        break;
    }
    case F_WIDTH: snprintf(o, n, "%d%%", v * 100 / 64); break;
    case F_BEND: snprintf(o, n, v == 64 ? "0" : "%+d", (v - 64) * 12 / 64); break;
    case F_PAN: snprintf(o, n, v == 64 ? "C" : v < 64 ? "L%d" : "R%d", v < 64 ? 64 - v : v - 64); break;
    case F_FILT:
        if (v >= 62 && v <= 66)
            snprintf(o, n, "OPEN");
        else
            snprintf(o, n, v < 64 ? "LP %d" : "HP %d", v < 64 ? 64 - v : v - 64);
        break;
    case F_KNOTE: {                                   /* (the note the kick would have at this value: a lock's too) */
        uint8_t q[32];
        memcpy(q, tb_proj.trk[sel].p, sizeof q);
        if (d->ref < 32u)
            q[d->ref] = (uint8_t)v;
        note_text(o, n, tb_kick_note(q));
        break;
    }
    case F_NOTE: note_text(o, n, v); break;
    case F_ROOT: snprintf(o, n, "%s", NOTE_NAME[v % 12]); break;
    case F_SLICE: snprintf(o, n, "%d", v - 60 + 1); break;
    case F_BPM: snprintf(o, n, "%d", v); break;
    case F_SWING: snprintf(o, n, "%d%%", 50 + v * 25 / 127); break;
    case F_PAT: snprintf(o, n, "%c", 'A' + v); break;
    case F_SPAT: snprintf(o, n, v >= TB_PATTERNS ? "END" : "%c", 'A' + v); break;
    case F_PCT100: snprintf(o, n, "%d%%", v); break;
    case F_PCT5: snprintf(o, n, v ? "%d%%" : "OFF", v * 5); break;
    case F_NIDX: {                                    /* EDIT: which, of how many */
        uint16_t L[NED_MAX];
        unsigned nn = ned_list(L);
        if (nn)
            snprintf(o, n, "%d OF %u", v + 1, nn);
        else
            snprintf(o, n, "NONE");
        break;
    }
    case F_NLEN: {                                    /* EDIT: a length in half steps, as a part of the bar (reduced) */
        unsigned a = (unsigned)v * PER32[tb_proj.trk[sel].speed % TB_NSPEED], b = 64u;
        while (a && !(a & 1u) && b > 1u)
            a >>= 1, b >>= 1;
        if (a >= b && a % b == 0u)
            snprintf(o, n, "%u BAR%s", a / b, a / b > 1u ? "S" : "");
        else
            snprintf(o, n, "%u/%u", a, b);
        break;
    }
    case F_MPOS: {                                    /* where the slice starts, in ms */
        uint32_t ms = tb_brk_len(tb_proj.trk[sel].p[TBB_SRC]) / 16u * (uint32_t)v / 1000u;
        snprintf(o, n, "%u ms", (unsigned)ms);
        break;
    }
    case F_MFINE: {
        const tb_track_t *t = &tb_proj.trk[sel];
        unsigned q = t->man.n ? t->man.at[man_sel % t->man.n] : 0u;   /* (4 samples: 0.25 ms) */
        snprintf(o, n, "%u.%02u", q / 4000u, q % 4000u / 40u);
        break;
    }
    case F_BRK: snprintf(o, n, "%s", tb_brk_name((unsigned)v)); break;
    case F_CHANCE: snprintf(o, n, "%d%%", (8 - v) * 100 / 8); break;
    case F_LONG: snprintf(o, n, v % 8 ? "%d.%d" : "%d", v / 8, v % 8 * 10 / 8); break;
    case F_REPEAT: snprintf(o, n, "x%d", v); break;
    case F_TOOL: snprintf(o, n, "%s", TOOL_NAME[v % T_NTOOL]); break;
    case F_LFORATE:
        if (tb_proj.trk[sel].lfo_sync)
            snprintf(o, n, "%s", TB_LFO_SYNC_NAME[v % TB_LFO_NSYNC]);
        else {
            unsigned mhz = (unsigned)((50ull * (1ull << (v / 14))) * (100u + (v % 14) * 100u / 14u) / 100u);
            if (mhz >= 1000u)
                snprintf(o, n, "%u.%uHz", mhz / 1000u, mhz % 1000u / 100u);
            else
                snprintf(o, n, "0.%02uHz", mhz / 10u);
        }
        break;
    case F_TIME: {
        unsigned us = tb_env_us((unsigned)v);
        if (!v)
            snprintf(o, n, "0");
        else if (us < 1000000u)
            snprintf(o, n, "%ums", us / 1000u ? us / 1000u : 1u);
        else
            snprintf(o, n, "%u.%us", us / 1000000u, us % 1000000u / 100000u);
        break;
    }
    default: snprintf(o, n, "%d", d->max > d->min ? (v - (int)d->min) * 100 / ((int)d->max - (int)d->min) : v);
    }
}

/* ------------------------------------------------------- the step being held (SEQ) --- */
static int step_knob(unsigned k, pd_t *d, int *v)
{
    tb_step_t *s = held_step();
    unsigned m = tb_proj.trk[sel].machine;
    static const pd_t NOTE = {"NOTE", 0, 24, 108, F_NOTE, 0}, SLICE = {"SLICE", 0, 60, 91, F_SLICE, 0},
                      PITCH = {"PITCH", 0, 36, 84, F_KSEMI, 0}, VELO = {"VELO", 1, 1, 127, F_PCT, 0},
                      LONG = {"LONG", 2, 1, 255, F_LONG, 0}, CHANCE = {"CHANCE", 3, 0, 7, F_CHANCE, 0},
                      REPEAT = {"REPEAT", 4, 1, 4, F_REPEAT, 0}, ACC = {"ACCENT", 5, 0, 1, F_ENUM, ONOFF},
                      SLD = {"SLIDE", 6, 0, 1, F_ENUM, ONOFF};
    const pd_t *Lk[4];
    const pd_t *CHN = TB_HAS_CHANCE ? &CHANCE : 0, *RPT = TB_HAS_RATCHET ? &REPEAT : 0;
    if (!s)
        return 0;
    if (m == TB_M_909)
        Lk[0] = &VELO, Lk[1] = CHN ? CHN : &LONG, Lk[2] = RPT, Lk[3] = CHN ? &LONG : 0;
    else if (m == TB_M_KICK)
        Lk[0] = &PITCH, Lk[1] = &VELO, Lk[2] = &LONG, Lk[3] = CHN;
    else if (m == TB_M_ACID)
        Lk[0] = &NOTE, Lk[1] = &ACC, Lk[2] = &SLD, Lk[3] = &LONG;
    else
        Lk[0] = m == TB_M_BREAK ? &SLICE : &NOTE, Lk[1] = &VELO, Lk[2] = &LONG, Lk[3] = CHN;
    if (k >= 4u || !Lk[k])
        return 0;
    *d = *Lk[k];
    switch (d->ref) {
    case 0: *v = s->note ? s->note : 60; break;
    case 1: *v = s->vel ? s->vel : 100; break;
    case 2: *v = s->gate ? s->gate : 6; break;
    case 3: *v = TB_PROB(s->flags); break;
    case 5: *v = (s->flags & TB_ACCENT) != 0; break;
    case 6: *v = (s->flags & TB_SLIDE) != 0; break;
    default: *v = (int)TB_RATCH(s->flags);
    }
    return 1;
}

static void step_set(unsigned ref, int v)
{
    tb_step_t *s = held_step();
    if (!s)
        return;
    touch();
    switch (ref) {
    case 0: s->note = (uint8_t)v; last_note[sel] = (uint8_t)v; break;
    case 1: s->vel = (uint8_t)v; break;
    case 2: s->gate = (uint8_t)v; break;
    case 3: s->flags = (uint8_t)((s->flags & 0xE0u) | TB_FLAGS((unsigned)v, TB_RATCH(s->flags))); break;
    case 5: s->flags = (uint8_t)(v ? s->flags | TB_ACCENT : s->flags & ~TB_ACCENT); break;
    case 6: s->flags = (uint8_t)(v ? s->flags | TB_SLIDE : s->flags & ~TB_SLIDE); break;
    default: s->flags = (uint8_t)((s->flags & 0xE0u) | TB_FLAGS(TB_PROB(s->flags), (unsigned)v));
    }
    held_edit = 1;
}

/* the knobs now: the page open, the held step, the sequencer's, or the home strip */
static const pg_t *menu_pages(unsigned m, unsigned *n)
{
    int syn = tb_proj.trk[sel].machine == TB_M_SYNTH;
    switch (m) {
    case TB_MENU_ENV:
        if (syn && IS_RAVE_T(sel)) {
            *n = 1u;
            return ENV_RAVE_PG;
        }
        *n = syn ? 2u : 1u;
        return syn ? ENV_SYNTH_PG : ENV_PG;
    case TB_MENU_LFO: *n = 2; return LFO_PG;
    case TB_MENU_ARP: *n = 2; return ARP_PG;
    case TB_MENU_SCALE: *n = 2; return SCALE_PG;
    case TB_MENU_FX: *n = sizeof FX_PG / sizeof *FX_PG; return FX_PG;
    case TB_MENU_GLO: *n = sizeof GLO_PG / sizeof *GLO_PG; return GLO_PG;
    case TB_MENU_SYS: *n = sizeof SYS_PG / sizeof *SYS_PG; return SYS_PG;
    case TB_MENU_EDIT: return edit_pages(sel, n);
    default: *n = 0; return 0;
    }
}
static unsigned mpage(void)
{
    unsigned n;
    menu_pages(menu, &n);
    return n ? menu_page[menu] % n : 0u;
}

static const pg_t *strip(void)
{
    unsigned n;
    const pg_t *P;
    if (menu && menu != TB_MENU_SEQ && menu != TB_MENU_TOOLS && menu != TB_MENU_NOTES) {
        P = menu_pages(menu, &n);
        return &P[mpage()];
    }
    if (menu == TB_MENU_SEQ)
        return seq_strip == 2u ? &SEQ_PG3 : seq_strip ? &SEQ_PG2 : &SEQ_PG1;
    P = home_pages(sel, &n);
    return &P[home_page[sel] % n];
}

/* SEQ, a step held: the pages its locks go on (0: the step's own knobs): the machine's, its envelope, its mix */
static const pg_t *lock_pg(unsigned i, unsigned *n)
{
    unsigned ne, nv = tb_proj.trk[sel].machine == TB_M_SYNTH ? 2u : 1u;
    const pg_t *E, *V;
    if (!TB_HAS_PLOCK) {                              /* (the FM-1: no locks, the step's own knobs only) */
        *n = 1u;
        (void)i;
        return 0;
    }
    E = edit_pages(sel, &ne), V = nv == 2u ? ENV_SYNTH_PG : ENV_PG;
    *n = 1u + ne + nv + 1u;
    if (!i)
        return 0;
    i--;
    if (i < ne)
        return &E[i];
    i -= ne;
    if (i < nv)
        return &V[i];
    return &LOCK_MIX_PG;
}
/* a knob's range and name where they depend on the sound (kick types, breaks, the LFO's rate, the synth's wave) */
static int knob_fix(pd_t *d)
{
    if (d->fmt == F_ENUM && !d->names && d->ref == TB_KICK_TYPE)
        d->max = (uint16_t)(TB_KICK_NTYPES - 1u);
    if (d->fmt == F_BRK)
        d->max = (uint16_t)(tb_brk_count() ? tb_brk_count() - 1u : 0u);
    if (d->ref == R_LEN)                              /* (the kick, the break, the 909: 64 steps; the melodies: more) */
        d->max = (uint16_t)TB_TCAP(sel);
    if (d->ref == R_RLVL && tb_proj.riser_type >= TB_NRISER)   /* (the music: filtered under the riser, or not) */
        d->name = "FILTER", d->max = 1, d->fmt = F_ENUM, d->names = ONOFF;
    if (d->fmt == F_LFORATE && tb_proj.trk[sel].lfo_sync)
        d->max = TB_LFO_NSYNC - 1u;
    if (d->ref == TBS_DETUNE && tb_proj.trk[sel].machine == TB_M_SYNTH) {
        unsigned w = tb_proj.trk[sel].p[TBS_WAVE];
        d->name = w == TBS_ORGAN ? "DRAWBARS" : w == TBS_FM || w == TBS_BELL ? "INDEX" : w == TBS_SYNC ? "SYNC" :
                  w == TBS_VOX ? "VOWEL" : w == TBS_RING ? "RATIO" : w == TBS_M1 ? "TIMBRE" : w == TBS_FOLD ? "FOLD" :
                  w == TBS_RAVE ? "SPREAD" : "DETUNE";
    }
    return d->name != 0;
}
static int lock_val(unsigned ref, int base)
{
    int i = held >= 0 ? tb_lock_find(tb_proj.pattern, sel, (unsigned)held, ref) : -1;
    return i >= 0 ? tb_proj.pat[tb_proj.pattern].lock[i].val : base;
}

_Static_assert(R_NREFS <= R_TVOL, "the knobs' references");
static int knob(unsigned k, pd_t *d, int *v)
{
    const pg_t *p;
    if (FX_LAYER(prev_btn)) {                         /* FX held: the DROP's and the RISER's sounds, how loud */
        static const pd_t FXV[TB_NFXV] = {{"RISER", R_FXV + TB_FXV_RISER, 0, 20, F_PCT5, 0}, {"ROLLS", R_FXV + TB_FXV_ROLL, 0, 20, F_PCT5, 0},
                                          {"CRASH", R_FXV + TB_FXV_CRASH, 0, 20, F_PCT5, 0}, {"IMPACT", R_FXV + TB_FXV_IMPACT, 0, 20, F_PCT5, 0}};
        if (k >= TB_NFXV)
            return 0;
        *d = FXV[k], *v = get(d->ref);
        return 1;
    }
    if (menu == TB_MENU_SEQ) {                        /* WRITE: where, how long, the loop, its speed */
        static const pd_t W_CUR = {"CURSOR", R_SCUR, 0, 0, F_PLUS1, 0}, W_LEN = {"LENGTH", R_SVAL, 0, 5, F_ENUM, VALN},
                          W_LOOP = {"LOOP", R_LEN, 1, TB_STEPS, F_RAW, 0}, W_SPD = {"SPEED", R_SPEED, 0, 2, F_ENUM, TB_SPEED_NAME},
                          W_PAGE = {"PAGE", R_SPAGE, 0, 0, F_PLUS1, 0}, W_SND = {"SOUND", R_SPIECE, 0, TB909_PIECES - 1, F_ENUM, PIECE},
                          W_TR = {"TRANSPO", R_TRANSPO, 0, 48, F_TRANSPO, 0}, W_RES = {"GRID", R_SRES, 0, 2, F_ENUM, RESN};
        unsigned len = len_of(sel);
        if (k == 0 && melodic(sel))
            *d = W_CUR, d->max = (uint16_t)(len - 1u), *v = seq_cur[sel] % len;
        else if (k == 0)
            *d = W_PAGE, d->max = (uint16_t)((len + win_steps(sel) - 1u) / win_steps(sel) - 1u), *v = seq_page[sel] <= d->max ? seq_page[sel] : 0;
        else if (k == 1 && melodic(sel))
            *d = W_LEN, *v = seq_val[sel] % 6u;
        else if (k == 1 && is_drum(sel))
            *d = W_SND, *v = piece[sel] % TB909_PIECES;
        else if (k == 2)
            *d = W_LOOP, d->max = (uint16_t)TB_TCAP(sel), *v = get(R_LEN);
        else if (k == 3 && grid_track(sel))
            *d = W_RES, d->max = tb_proj.trk[sel].machine == TB_M_909 ? 1u : 2u, *v = (int)res_of(sel);
        else if (k == 3 && melodic(sel) && tb_proj.trk[sel].machine != TB_M_BREAK)
            *d = W_TR, *v = get(R_TRANSPO);
        else if (k == 3 && TB_HAS_SPEED)
            *d = W_SPD, *v = get(R_SPEED);
        else
            return 0;
        return 1;
    }
    if (menu == TB_MENU_SEQ && held >= 0) {
        unsigned n;
        const pg_t *lp = lock_pg(lock_page, &n);
        if (!lp)
            return step_knob(k, d, v);
        if (k >= 4u || lp->k[k].ref == R_NONE || !lp->k[k].name || !tb_lock_ok(lp->k[k].ref))
            return 0;
        *d = lp->k[k];
        if (!knob_fix(d))
            return 0;
        *v = lock_val(d->ref, get(d->ref));
        if (*v > (int)d->max)
            *v = d->max;
        return 1;
    }
    if (menu == TB_MENU_SEQ && seq_step_knob(k) >= 0)   /* SEQ: the cursor's step, its values */
        return step_knob((unsigned)seq_step_knob(k), d, v);
    if (menu == TB_MENU_SEQ && !seq_strip && k == 1u) {  /* SEQ: the cursor (the step its values are) */
        d->name = "CURSOR", d->ref = R_SCUR, d->min = 0, d->max = (uint16_t)(len_of(sel) - 1u), d->fmt = F_PLUS1, d->names = 0;
        *v = seq_cur[sel] < len_of(sel) ? seq_cur[sel] : 0;
        return 1;
    }
    if (menu == TB_MENU_NOTES) {                      /* EDIT: which, its pitch, its length, its level (MORE: the rest) */
        uint16_t L[NED_MAX];
        unsigned nn = ned_list(L), m = tb_proj.trk[sel].machine;
        int ix = ned_index(L, nn);
        const tb_step_t *st = ix >= 0 ? &TB_STEP(sel, ned_at[sel]) : 0;
        static const pd_t K0 = {"CHOOSE", R_NSEL, 0, 0, F_NIDX, 0}, KN = {"TRANSPOSE", R_NPITCH, 24, 108, F_NOTE, 0},
                          KK = {"TRANSPOSE", R_NPITCH, 36, 84, F_KSEMI, 0}, KS = {"SLICE", R_NPITCH, 60, 91, F_SLICE, 0},
                          KP = {"SOUND", R_NPITCH, 0, TB909_PIECES - 1, F_ENUM, PIECE},
                          KL = {"LENGTH", R_NLEN, 1, 64, F_NLEN, 0}, KV = {"VELO", R_NVEL, 1, 127, F_PCT, 0},
                          KC = {"CHANCE", R_NCHANCE, 0, 7, F_CHANCE, 0}, KR = {"REPEAT", R_NREP, 1, 4, F_REPEAT, 0},
                          KA = {"ACCENT", R_NACC, 0, 1, F_ENUM, ONOFF}, KSL = {"SLIDE", R_NSLIDE, 0, 1, F_ENUM, ONOFF};
        static const pd_t KD = {"MOVE", R_NDINFO, 0, 0, F_PLUS1, 0};
        if (k == 0) {                                 /* CHOOSE, LENGTH, TRANSPOSE, and OCT- deletes (said on the 4th) */
            *d = K0, d->max = (uint16_t)(nn ? nn - 1u : 0u), *v = ix >= 0 ? ix : 0;
            return 1;
        }
        if (k == 3) {                                 /* MOVE: its step, the note moved along the grid */
            *d = KD, d->max = (uint16_t)(len_of(sel) - 1u), *v = ned_at[sel];
            return st != 0;
        }
        if (!st)
            return 0;
        if (k == 1 && st->more[0] && ned_k(st) >= 0)   /* (one note of a chord: the length is the chord's) */
            return 0;
        if (k == 1 && m != TB_M_KICK) {               /* (the kick: as long as its sound) */
            *d = KL, *v = ((st->gate ? st->gate : 6) + 3) / 4 ? ((st->gate ? st->gate : 6) + 3) / 4 : 1;   /* (half steps) */
            return 1;
        }
        if (k == 2) {
            *d = m == TB_M_909 ? KP : m == TB_M_KICK ? KK : m == TB_M_BREAK ? KS : KN;
            *v = m == TB_M_909 ? ned_pc[sel] : ned_pitch((tb_step_t *)st) ? ned_pitch((tb_step_t *)st) : 60;
            return 1;
        }
        (void)KV, (void)KC, (void)KR, (void)KA, (void)KSL;
        return 0;
    }
    if (menu == TB_MENU_TOOLS) {                      /* the tools: which, and its settings */
        static const pd_t T0 = {"TOOL", R_TOOL, 0, T_NTOOL - 1, F_TOOL, 0};
        if (!tool_p2[tool] && TOOL[tool].k2)          /* (its settings start where they are useful) */
            tool_p2[tool] = TOOL[tool].def2;
        if (tool_p3[tool] < TOOL[tool].lo3 && TOOL[tool].k3)
            tool_p3[tool] = TOOL[tool].def3;
        if (k == 0)
            *d = T0;
        else if (k == 1 && TOOL[tool].k2) {
            d->name = TOOL[tool].k2, d->ref = R_TP1, d->min = TOOL[tool].lo2, d->max = TOOL[tool].hi2, d->names = 0;
            d->fmt = tool == T_TRANSPOSE ? F_TRANSPO : tool == T_SHIFT ? F_BIP : F_RAW;
            if (tool == T_SHIFT)
                d->fmt = F_TRANSPO;
            if (tool == T_STYLE)
                d->fmt = F_ENUM, d->names = STYLE_NAME;
            if (tool == T_QUANT)
                d->fmt = F_ENUM, d->names = QGRIDN;
        } else if (k == 2 && TOOL[tool].k3) {
            d->name = TOOL[tool].k3, d->ref = R_TP2, d->min = TOOL[tool].lo3, d->max = TOOL[tool].hi3, d->fmt = F_RAW, d->names = 0;
            if (tool == T_STYLE)
                d->fmt = F_ENUM, d->names = ONOFF;
            if (tool == T_QUANT)
                d->fmt = F_PCT100;
        }
        else
            return 0;
        *v = get(d->ref);
        return 1;
    }
    p = strip();
    if (k >= 4u || p->k[k].ref == R_NONE)
        return 0;
    *d = p->k[k];
    if (d->ref >= R_TVOL && d->ref < R_TVOL + TB_TRACKS)  /* the mixer: each knob its track's name */
        d->name = tb_proj.trk[d->ref - R_TVOL].name;
    if ((d->ref >= R_INSA1 && d->ref <= R_INSA3) || (d->ref >= R_INSB1 && d->ref <= R_INSB3)) {
        const tb_ins_set_t *ins = &tb_proj.trk[sel].ins[d->ref >= R_INSB1];
        d->name = TB_INS_KNOB[ins->type % TB_NINS][(d->ref - (d->ref >= R_INSB1 ? R_INSB1 : R_INSA1))];
        if (!d->name)
            return 0;
    }
    if (!d->name || !knob_fix(d))
        return 0;
    if (menu == TB_MENU_SEQ && d->ref == R_TRANSPO && (is_drum(sel) || tb_proj.trk[sel].machine == TB_M_BREAK)) {
        static const pd_t SW = {"SWING", R_SWING, 0, 127, F_SWING, 0};   /* (no notes to transpose: the swing here) */
        *d = SW;
    }
    *v = get(d->ref);
    if (*v > (int)d->max)
        *v = d->max;
    return 1;
}

/* EDIT: a knob turned (v: its new value) */
static void ned_turn(unsigned ref, int v)
{
    uint16_t L[NED_MAX];
    unsigned nn = ned_list(L), m = tb_proj.trk[sel].machine;
    int ix = ned_index(L, nn);
    tb_step_t *st = ix >= 0 ? &TB_STEP(sel, ned_at[sel]) : 0;
    if (ref == R_NSEL && nn) {                        /* the next note, or the one before: shown, heard */
        ned_at[sel] = (uint8_t)(L[v % (int)nn] >> 4), ned_pc[sel] = (uint8_t)(L[v % (int)nn] & 15u);
        seq_page[sel] = (uint8_t)(ned_at[sel] / win_steps(sel));
        if (is_drum(sel))
            piece[sel] = ned_pc[sel];
        ned_hear();
        return;
    }
    if (!st)
        return;
    if (ref == R_NDINFO) {                            /* MOVE: the note along the grid, a step a turn */
        unsigned a = ned_at[sel], len = len_of(sel), b;
        int dir = v > (int)a ? 1 : -1;
        int moves = v > (int)a ? v - (int)a : (int)a - v;
        touch();
        while (moves-- > 0) {
            tb_step_t *sa = &TB_STEP(sel, a), *sb;
            int bb = (int)a + dir;
            int kk = is_drum(sel) ? 0 : ned_k(sa);
            unsigned k = kk < 0 ? 0u : (unsigned)kk, pitch = is_drum(sel) ? 0u : *step_note(sa, k);
            if (hand_ok(sel) && kk >= 0) {            /* a note: free, onto a note there too (a chord by hand) */
                while (bb >= 0 && bb < (int)len && TB_HITS(TB_STEP(sel, (unsigned)bb).hits) &&
                       step_has(&TB_STEP(sel, (unsigned)bb), pitch))
                    bb += dir;                        /* (the same note there: past it) */
            } else
                while (bb >= 0 && bb < (int)len &&
                       (is_drum(sel) ? (TB_HITS(TB_STEP(sel, (unsigned)bb).hits) >> ned_pc[sel]) & 1u : TB_HITS(TB_STEP(sel, (unsigned)bb).hits) != 0))
                    bb += dir;                        /* (a whole chord, the 909, the kick, the 303: to the next free step) */
            if (bb < 0 || bb >= (int)len) {
                hud_say("MOVE", dir > 0 ? "THE END OF THE LOOP" : "THE START OF THE LOOP", 0);
                break;
            }
            b = (unsigned)bb, sb = &TB_STEP(sel, b);
            if (is_drum(sel)) {
                if (!TB_HITS(sb->hits))
                    sb->vel = sa->vel, sb->gate = sa->gate, sb->flags = sa->flags;
                sb->hits = (uint16_t)TB_WITH_MICRO(TB_HITS(sb->hits) | (1u << ned_pc[sel]), TB_MICRO(sb->hits));
                sa->hits = (uint16_t)TB_WITH_MICRO(TB_HITS(sa->hits) & ~(1u << ned_pc[sel]), TB_MICRO(sa->hits));
                if (!TB_HITS(sa->hits))
                    memset(sa, 0, sizeof *sa), ned_locks(a, 255u);
            } else if (!TB_HITS(sb->hits)) {          /* an empty step: the note there (a whole chord: all of it) */
                if (!sa->more[0] || kk < 0)
                    *sb = *sa, memset(sa, 0, sizeof *sa), ned_locks(a, b);
                else {
                    *sb = *sa, sb->note = (uint8_t)pitch, memset(sb->more, 0, sizeof sb->more);
                    step_drop(sa, k);
                }
                if (kk >= 0)
                    ned_pc[sel] = 0;
            } else {                                  /* onto a note: one more in its chord */
                int at = step_add(sb, pitch);
                if (!at) {
                    hud_say("MOVE", "4 NOTES A STEP MAX", 0);
                    break;
                }
                if (!sa->more[0])
                    memset(sa, 0, sizeof *sa), ned_locks(a, 255u);
                else
                    step_drop(sa, k);
                ned_pc[sel] = (uint8_t)at;            /* (that note of it chosen: it can go on, or back) */
            }
            a = b;
        }
        ned_at[sel] = (uint8_t)a, seq_page[sel] = (uint8_t)(a / win_steps(sel));
        ned_hear();
        return;
    }
    touch();
    switch (ref) {
    case R_NPITCH:
        if (m == TB_M_909) {                          /* (another sound on the same step) */
            st->hits = (uint16_t)TB_WITH_MICRO((TB_HITS(st->hits) & ~(1u << ned_pc[sel])) | (1u << (unsigned)v), TB_MICRO(st->hits));
            ned_pc[sel] = (uint8_t)v, piece[sel] = (uint8_t)v;
        } else if (ned_k(st) < 0) {                   /* a whole chord: all of it, as far */
            int d = v - (int)st->note, j;
            for (j = 0; j < 4; j++)
                if (*step_note(st, (unsigned)j)) {
                    int n2 = (int)*step_note(st, (unsigned)j) + d;
                    *step_note(st, (unsigned)j) = (uint8_t)(n2 < 1 ? 1 : n2 > 127 ? 127 : n2);
                }
            last_note[sel] = (uint8_t)v;
        } else
            *step_note(st, (unsigned)ned_k(st)) = (uint8_t)v, last_note[sel] = (uint8_t)v;
        ned_hear();
        break;
    case R_NLEN: st->gate = (uint8_t)(v * 4 - 1 > 255 ? 255 : v * 4 - 1); break;   /* (as WRITE makes them) */
    case R_NVEL: st->vel = (uint8_t)v; ned_hear(); break;
    case R_NCHANCE: st->flags = (uint8_t)((st->flags & 0xE0u) | TB_FLAGS((unsigned)v, TB_RATCH(st->flags))); break;
    case R_NREP: st->flags = (uint8_t)((st->flags & 0xE0u) | TB_FLAGS(TB_PROB(st->flags), (unsigned)v)); break;
    case R_NACC: st->flags = (uint8_t)(v ? st->flags | TB_ACCENT : st->flags & ~TB_ACCENT); break;
    default: break;
    }
}
/* EDIT, ARP: the note chosen erased (one press, one note; SAVE held brings it back) */
static void ned_erase(void)
{
    uint16_t L[NED_MAX];
    unsigned nn = ned_list(L), s;
    int ix = ned_index(L, nn);
    tb_step_t *st;
    char nb[12];
    if (ix < 0) {
        hud_say("ERASE", "NO NOTES", 0);
        return;
    }
    touch();
    s = ned_at[sel], st = &TB_STEP(sel, s);
    if (is_drum(sel)) {
        snprintf(nb, sizeof nb, "%s %u", PIECE[ned_pc[sel] % TB909_PIECES], s + 1u);
        st->hits = (uint16_t)TB_WITH_MICRO(TB_HITS(st->hits) & ~(1u << ned_pc[sel]), TB_MICRO(st->hits));
        if (!TB_HITS(st->hits))
            memset(st, 0, sizeof *st), ned_locks(s, 255u);
    } else {
        if (tb_proj.trk[sel].machine == TB_M_KICK)
            snprintf(nb, sizeof nb, "STEP %u", s + 1u);
        else
            note_text(nb, sizeof nb, ned_pitch(st));
        if (st->more[0] && ned_k(st) >= 0)            /* (one note of a chord: the others stay) */
            step_drop(st, (unsigned)ned_k(st)), ned_pc[sel] = 0;
        else
            memset(st, 0, sizeof *st), ned_locks(s, 255u);
    }
    hud_say("ERASED", nb, 0);
    if (nn > 1u)                                      /* (the next one chosen) */
        ned_at[sel] = (uint8_t)(L[((unsigned)ix + 1u) % nn] >> 4), ned_pc[sel] = (uint8_t)(L[((unsigned)ix + 1u) % nn] & 15u);
}
static unsigned note_of_key(unsigned k);
/* EDIT, a key: a melody's note gets that note; on the kick or the 909, a white key chooses the hit on that step */
static void ned_key(unsigned k)
{
    uint16_t L[NED_MAX];
    unsigned nn = ned_list(L), i;
    int ix = ned_index(L, nn);
    if (!melodic(sel)) {
        unsigned hf, s = cell_step(sel, key_index(k), &hf);
        if (!key_white(k))
            return;
        for (i = 0; i < nn; i++)
            if ((L[i] >> 4) == s) {
                ned_at[sel] = (uint8_t)s, ned_pc[sel] = (uint8_t)(L[i] & 15u);
                if (is_drum(sel))
                    piece[sel] = ned_pc[sel];
                ned_hear();
                return;
            }
        hud_say("EDIT", "NOTHING ON THIS STEP", 0);
        return;
    }
    if (ix < 0)
        return;
    touch();
    {
        tb_step_t *st = &TB_STEP(sel, ned_at[sel]);
        int d = (int)note_of_key(k) - (int)ned_pitch(st), j, kk = ned_k(st);
        for (j = 0; j < 4; j++)                       /* (a whole chord: all of it moved as far) */
            if ((kk < 0 || j == kk) && *step_note(st, (unsigned)j)) {
                int n2 = (int)*step_note(st, (unsigned)j) + d;
                *step_note(st, (unsigned)j) = (uint8_t)(n2 < 1 ? 1 : n2 > 127 ? 127 : n2);
            }
    }
    if (tb_proj.trk[sel].machine != TB_M_BREAK)
        last_note[sel] = (uint8_t)note_of_key(k);
    ned_hear();
}

/* (audit 2) KNOB ACCEL, as JIANT's (src/ui_input.c accel, ON there by default): a fast turn of a wide value (range over
 * 16, not a list) sweeps further, a slow one stays one step a detent. The speed is the time per detent over this read
 * and the last (both under ACC_GAP ms, the same way round): 80 / ms -> 40 ms x2, 20 ms x4, 10 ms x8, 5 ms x16, capped
 * by the range (x2 up to 32, x4 up to 64, x8 up to 100, x16 above: 0..127 in a dozen quick detents). A new turn, its
 * first detents, a single quick one and a reversal: one step a detent. The sign is always the detents'. */
#define ACC_GAP 60u
#define ACC_RATE 80u
static int accel(unsigned k, int s, int range)
{
    static uint32_t enc_t[8];
    uint32_t now = fm1_millis() & 0xFFFFFFu, st = enc_t[k & 7u], up = s > 0, pi = st >> 25, a, i, m = 1;
    if (range <= 16 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;                   /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;                                       /* a new turn, or reversed */
    else if (pi < ACC_GAP) {
        m = ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        a = range > 100 ? 16u : range > 64 ? 8u : range > 32 ? 4u : 2u;   /* (the cap) */
        m = m < 1u ? 1u : m > a ? a : m;
    }
    enc_t[k & 7u] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int)m;
}

static void turn(unsigned k, int dlt)
{
    pd_t d;
    int v;
    if (!dlt || !knob(k, &d, &v))
        return;
    if (prev_btn & BTN(B_FX))                         /* (FX held, its knobs turned: FX let go is no tap) */
        bt_used[B_FX] = 1;
    if (d.fmt == F_ENUM || d.fmt == F_BRK || d.fmt == F_PAT || d.fmt == F_SPAT || d.fmt == F_ROOT || d.fmt == F_TOOL || d.fmt == F_SLOT ||
        d.ref == R_SPOS || d.ref == R_MSEL || d.ref == R_MNUM || d.ref == R_USMP || d.max - d.min < 12)
        dlt = dlt > 0 ? 1 : -1;                       /* (lists: one at a time) */
    else
        dlt = accel(k, dlt, (int)d.max - (int)d.min);  /* (a wide value: faster as it is turned faster) */
#ifdef RFM_HOST
    {   /* (the PC's tests: each knob step, logged) */
        extern void rfm_host_knob(unsigned k, int dlt, int v, int min, int max);
        rfm_host_knob(k, dlt, v, (int)d.min, (int)d.max);
    }
#endif
    v += dlt;
    v = v < (int)d.min ? (int)d.min : v > (int)d.max ? (int)d.max : v;
    if (TB_HAS_PLOCK && menu == TB_MENU_SEQ && held >= 0 && lock_page) {   /* a lock on the held step (back to the knob: none) */
        touch();
        if (v == get(d.ref))
            tb_lock_clear(tb_proj.pattern, sel, (unsigned)held, d.ref);
        else if (tb_lock_set(tb_proj.pattern, sel, (unsigned)held, d.ref, (unsigned)v) < 0) {
            hud_say("LOCKS", "4 PER STEP MAX", 255);
            return;
        }
        held_edit = 1;
    } else if (menu == TB_MENU_NOTES) {
        ned_turn(d.ref, v);
    } else if (menu == TB_MENU_SEQ && held >= 0)
        step_set(d.ref, v);
    else if (menu == TB_MENU_SEQ && seq_step_knob(k) >= 0) {   /* the cursor's step (an empty one: a note made) */
        tb_step_t *st = held_step();
        if (!TB_HITS(st->hits)) {
            touch();
            if (is_drum(sel))
                st->hits = (uint16_t)(1u << piece[sel]);
            else {
                memset(st, 0, sizeof *st);
                st->hits = 1;
                st->note = tb_proj.trk[sel].machine == TB_M_BREAK ? (uint8_t)(60u + seq_cur[sel] % 32u) : last_note[sel];
            }
        }
        step_set(d.ref, v);
        if (d.ref == 0) {                             /* (a note changed: heard) */
            tb_engine_preview(sel, st->note, st->vel ? st->vel : 100u, 180);
        }
    } else
        set(d.ref, v);
    if (menu == TB_MENU_TOOLS && k == 0 && !tool_p2[tool] && TOOL[tool].k2)
        tool_p2[tool] = TOOL[tool].def2;              /* (a tool's settings start where they are useful) */
    if (menu == TB_MENU_TOOLS && k == 0 && tool_p3[tool] < TOOL[tool].lo3 && TOOL[tool].k3)
        tool_p3[tool] = TOOL[tool].def3;
    view.hud = (int8_t)k;
    hud_title = 0;
    hud_at = fm1_millis();
}

/* ------------------------------------------------------------------- the keys --- */
static unsigned note_of_key(unsigned k)
{
    int n = tb_proj.trk[sel].machine == TB_M_BREAK ? 60 + (int)k : 53 + (int)k;   /* (BREAK: the lowest key, slice 1) */
    n += 12 * octave;
    n = n < 0 ? 0 : n > 127 ? 127 : n;
    if (tb_proj.quantize && tb_proj.scale && tb_proj.trk[sel].machine != TB_M_BREAK)
        n = (int)tb_scale_snap((unsigned)n);
    return (unsigned)n;
}

static unsigned step_ms(unsigned t)
{
    static const uint8_t T[3] = {4, 2, 1};
    return 15000u * 10u / (tb_proj.bpm10 ? tb_proj.bpm10 : 1800u) * T[tb_proj.trk[t].speed < 3 ? tb_proj.trk[t].speed : 1] / 2u;
}

static void rec_down(unsigned k, unsigned what)
{
    unsigned len = len_of(sel), frac, s;
    static const uint8_t KEEP[4] = {0, 25, 50, 75};   /* STRENGTH 100 % .. 25 %: how much of the timing is kept */
    if (!tb_engine_playing())
        return;
    touch();
    /* where the key came (in 32nds of a step), pulled toward REC's grid (FREE: left there), put on the steps */
    s = tb_engine_step(sel, &frac);
    {
        static const uint8_t G32[4] = {2, 1, 4, 0};   /* (TB_RECG_*: the grid in 32nds of a bar) */
        unsigned gr = rec_grid(), ks = tb_proj.rec_quant & 7u;
        int u = (int)s * 32 + (int)(frac / 8u), nu = u, p;
        tb_step_t e;
        if (gr != TB_RECG_FREE) {
            int gu = (int)G32[gr] * 32 / (int)PER32[tb_proj.trk[sel].speed % TB_NSPEED], g = fdiv(u + gu / 2, gu) * gu;
            nu = g + (u - g) * KEEP[ks < 4u ? ks : 0] / 100;
        }
        memset(&e, 0, sizeof e);
        if (is_drum(sel))
            e.hits = (uint16_t)(1u << what);
        else {
            e.hits = 1, e.note = (uint8_t)what, e.gate = 6;
            if (hand_ok(sel) && tb_proj.trk[sel].chord) {   /* SCL's chord on: its notes written in the step, as heard */
                uint8_t cn[4];
                unsigned nc = tb_chord_notes(sel, what, cn), j;
                e.note = cn[0];
                for (j = 1; j < nc && j < 4u; j++)
                    e.more[j - 1u] = cn[j];
            }
            if (hand_ok(sel)) {                       /* keys held together landing on one step: a chord there */
                unsigned L = len * 32u, at = ((((unsigned)((nu % (int)L) + (int)L)) % L) + 16u) / 32u % len, i, j;
                for (i = 0; i < 27u; i++)
                    if (i != k && key_step[i] == (int)at && TB_HITS(TB_STEP(sel, at).hits)) {
                        tb_step_t *q = &TB_STEP(sel, at);
                        step_add(q, e.note);
                        for (j = 0; j < 3u && e.more[j]; j++)
                            step_add(q, e.more[j]);
                        key_step[k] = (int16_t)at;
                        return;
                    }
            }
        }
        p = place_hit(TB_TSTEPS(&tb_proj.pat[tb_proj.pattern], sel), len, nu, &e, 1);
        if (p >= 0 && (unsigned)p != s && TB_MICRO(TB_STEP(sel, (unsigned)p).hits) >= 0)
            tb_engine_skip(sel, (unsigned)p);         /* (a step still to come: not played twice) */
        key_step[k] = (int16_t)p;
    }
}

static void rec_up(unsigned k)
{
    if (key_step[k] >= 0 && melodic(sel) && tb_engine_playing()) {
        unsigned ms = fm1_millis() - key_at[k], g = (ms * 8u + step_ms(sel) / 2u) / (step_ms(sel) | 1u);
        TB_STEP(sel, (unsigned)key_step[k]).gate = (uint8_t)(g < 1u ? 1u : g > 255u ? 255u : g);
    }
    key_step[k] = -1;
}

/* WRITE, the kick and the 909: a white key, its step on or off (the 909: the sound chosen), heard as it goes on */
static void seq_key_down(unsigned i)
{
    unsigned hf, s = cell_step(sel, i, &hf), len = len_of(sel), on;
    tb_step_t *st;
    if (s >= len)
        return;
    st = &TB_STEP(sel, s);
    touch();
    if (tb_proj.trk[sel].machine == TB_M_KICK) {      /* the kick: the step, or (1/32) its half: the 32nd after it */
        unsigned bit = hf ? 2u : 1u, h = TB_HITS(st->hits) & 3u;
        on = !(h & bit);
        h = on ? h | bit : h & ~bit;
        if (on && !TB_HITS(st->hits))
            memset(st, 0, sizeof *st);
        if (h)
            st->hits = (uint16_t)TB_WITH_MICRO(h, TB_MICRO(st->hits));
        else
            memset(st, 0, sizeof *st), ned_locks(s, 255u);
    } else if (is_drum(sel)) {
        on = !((TB_HITS(st->hits) >> piece[sel]) & 1u);
        if (on) {
            if (!TB_HITS(st->hits))
                memset(st, 0, sizeof *st);
            st->hits = (uint16_t)TB_WITH_MICRO(TB_HITS(st->hits) | (1u << piece[sel]), TB_MICRO(st->hits));
        } else {
            st->hits = (uint16_t)TB_WITH_MICRO(TB_HITS(st->hits) & ~(1u << piece[sel]), TB_MICRO(st->hits));
            if (!TB_HITS(st->hits))
                memset(st, 0, sizeof *st), ned_locks(s, 255u);
        }
    } else {
        on = !TB_HITS(st->hits);
        if (on)
            memset(st, 0, sizeof *st), st->hits = 1, st->note = 60;
        else
            memset(st, 0, sizeof *st), ned_locks(s, 255u);
    }
    if (on) {
        tb_engine_note(sel, is_drum(sel) ? piece[sel] : st->note, st->vel ? st->vel : 100u);   /* (heard as it is set) */
        tb_engine_note(sel, is_drum(sel) ? piece[sel] : st->note, 0);
    }
}
static void seq_key_up(unsigned i) { (void)i; }

/* SEQ, a melody, step by step: a key, a note at the cursor as long as the value, the cursor after it */
static void seq_clear_steps(unsigned from, unsigned n)
{
    tb_pattern_t *P = &tb_proj.pat[tb_proj.pattern];
    unsigned len = len_of(sel), i, j;
    for (i = 0; i < n && from + i < len; i++) {
        memset(&TB_STEP(sel, from + i), 0, sizeof(tb_step_t));
        for (j = 0; j < TB_NLOCK; j++)
            if (P->lock[j].ref1 && P->lock[j].track == sel && P->lock[j].step == from + i)
                P->lock[j].ref1 = 0;
    }
}
static void seq_advance(unsigned n)
{
    unsigned len = len_of(sel);
    seq_cur[sel] = (uint8_t)((seq_cur[sel] % len + n) % len);
    seq_page[sel] = (uint8_t)(seq_cur[sel] / win_steps(sel));
}
static int16_t enter_at = -1;                     /* step by step: the step the keys held now went into */
static void seq_enter(unsigned k)
{
    unsigned len = len_of(sel), c = seq_cur[sel] % len, n = seq_steps(sel), note = note_of_key(k), g = n * 8u - 1u, i;
    tb_step_t *st = &TB_STEP(sel, c);
    int others = 0;
    for (i = 0; i < 27u; i++)
        others |= i != k && key_note_on[i];
    if (others && enter_at >= 0 && enter_at < (int)len && hand_ok(sel)) {   /* keys held together: a chord there */
        touch();
        if (!step_add(&TB_STEP(sel, (unsigned)enter_at), note)) {
            hud_say("CHORD", "4 NOTES A STEP MAX", 0);
            return;
        }
        tb_engine_note(sel, note, 110);
        key_note_on[k] = (uint8_t)(note + 1u);
        return;
    }
    enter_at = (int16_t)c;
    touch();
    seq_clear_steps(c, n);                            /* (what the note covers: gone) */
    st->hits = 1, st->note = (uint8_t)note;
    if (hand_ok(sel) && tb_proj.trk[sel].chord) {     /* SCL's chord on: its notes written (4 at most), seen, moved as one */
        uint8_t cn[4];
        unsigned nc = tb_chord_notes(sel, note, cn), j;
        st->note = cn[0];
        for (j = 1; j < nc && j < 4u; j++)
            st->more[j - 1u] = cn[j];
    }
    st->gate = (uint8_t)(g > 255u ? 255u : g);
    if (tb_proj.trk[sel].machine != TB_M_BREAK)
        last_note[sel] = (uint8_t)note;
    tb_engine_note(sel, note, 110);
    key_note_on[k] = (uint8_t)(note + 1u);
    held = -1;
    seq_advance(n);
}
static void seq_rest(void)
{
    touch();
    seq_clear_steps(seq_cur[sel] % len_of(sel), seq_steps(sel));
    seq_advance(seq_steps(sel));
}

/* ----------------------------------------------------------- the sequencer's tools --- */
static void do_tool(void)
{
    tb_pattern_t *P = &tb_proj.pat[tb_proj.pattern];
    tb_step_t *S = TB_TSTEPS(P, sel);
    unsigned len = len_of(sel), i, m = tb_proj.trk[sel].machine, cap = TB_TCAP(sel);
    int p2 = tool_p2[tool], p3 = tool_p3[tool];
    const char *done = "DONE";
    if (tool != T_COPY && tool != T_PCOPY)
        touch();
    switch (tool) {
    case T_COPY:
        memset(clip_trk, 0, sizeof clip_trk), memcpy(clip_trk, S, cap * sizeof *S); clip_trk_len = (uint8_t)len; clip_trk_full = 1; clip_trk_machine = (uint8_t)m;
        for (i = 0, clip_nlk = 0; i < TB_NLOCK; i++)
            if (P->lock[i].ref1 && P->lock[i].track == sel)
                clip_lk[clip_nlk++] = P->lock[i];
        done = "COPIED";
        break;
    case T_PASTE:
        if (!clip_trk_full || clip_trk_machine != m) {
            hud_say("PASTE", "NOTHING TO PASTE", 0);
            return;
        }
        memcpy(S, clip_trk, cap * sizeof *S), P->len[sel] = (uint8_t)(clip_trk_len > cap ? cap : clip_trk_len);
        tb_lock_clear(tb_proj.pattern, sel, 255, 255);
        for (i = 0; i < clip_nlk; i++)
            tb_lock_set(tb_proj.pattern, sel, clip_lk[i].step, clip_lk[i].ref1 - 1u, clip_lk[i].val);
        done = "PASTED";
        break;
    case T_CLEAR:
        if (is_drum(sel))
            for (i = 0; i < cap; i++)
                S[i].hits &= (uint16_t)~(1u << piece[sel]), S[i].hits = TB_HITS(S[i].hits) ? S[i].hits : 0;
        else
            memset(S, 0, cap * sizeof *S), tb_lock_clear(tb_proj.pattern, sel, 255, 255);
        done = "CLEARED";
        break;
    case T_DOUBLE:
        if (len > cap / 2u) {                         /* (twice as long: no more than the steps there are) */
            hud_say("DOUBLE", cap == 64 ? "64 STEPS MAX" : "128 STEPS MAX", 0);
            return;
        }
        memcpy(&S[len], S, len * sizeof *S), P->len[sel] = (uint8_t)(len * 2u);
        for (i = 0; i < TB_NLOCK; i++)                /* (the locks too) */
            if (P->lock[i].ref1 && P->lock[i].track == sel && P->lock[i].step < len)
                tb_lock_set(tb_proj.pattern, sel, P->lock[i].step + len, P->lock[i].ref1 - 1u, P->lock[i].val);
        done = "x2";
        break;
    case T_HALF:
        P->len[sel] = (uint8_t)(len > 1u ? len / 2u : 1u);
        for (i = 0; i < TB_NLOCK; i++)
            if (P->lock[i].ref1 && P->lock[i].track == sel && P->lock[i].step >= P->len[sel])
                P->lock[i].ref1 = 0;
        done = "/2";
        break;
    case T_SHIFT: {                                   /* PAS: -16 .. +16 */
        int d = p2 - 16, k;
        tb_step_t tmp[TB_STEPS];
        memcpy(tmp, S, len * sizeof *S);
        for (k = 0; k < (int)len; k++)
            S[((k + d) % (int)len + (int)len) % (int)len] = tmp[k];
        for (i = 0; i < TB_NLOCK; i++)
            if (P->lock[i].ref1 && P->lock[i].track == sel && P->lock[i].step < len)
                P->lock[i].step = (uint8_t)((((int)P->lock[i].step + d) % (int)len + (int)len) % (int)len);
        break;
    }
    case T_TRANSPOSE: {
        int d = p2 - 24;
        if (is_drum(sel) || m == TB_M_BREAK) {
            hud_say("TRANSPOSE", "NOT ON THIS TRACK", 0);
            return;
        }
        for (i = 0; i < len; i++)
            if (S[i].hits) {
                int n = (S[i].note ? S[i].note : 60) + d;
                S[i].note = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
            }
        break;
    }
#if TB_HAS_TOOLS_GEN
    case T_REVERSE:
        for (i = 0; i < len / 2u; i++) {
            tb_step_t tmp = S[i];
            S[i] = S[len - 1u - i], S[len - 1u - i] = tmp;
        }
        for (i = 0; i < TB_NLOCK; i++)
            if (P->lock[i].ref1 && P->lock[i].track == sel && P->lock[i].step < len)
                P->lock[i].step = (uint8_t)(len - 1u - P->lock[i].step);
        break;
    case T_EUCLID: {
        unsigned kk = (unsigned)p2 > len ? len : (unsigned)p2;
        for (i = 0; i < len; i++) {
            unsigned j = (i + len - (unsigned)p3 % len) % len, on = kk && ((j * kk) % len) < kk;
            tb_step_t *st = &S[i];
            if (is_drum(sel)) {
                if (on)
                    st->hits |= (uint16_t)(1u << piece[sel]);
                else
                    st->hits &= (uint16_t)~(1u << piece[sel]), st->hits = TB_HITS(st->hits) ? st->hits : 0;
            } else if (on) {
                if (!st->hits)
                    memset(st, 0, sizeof *st), st->note = m == TB_M_BREAK ? (uint8_t)(60u + i % 32u) : last_note[sel];
                st->hits = 1;
            } else
                memset(st, 0, sizeof *st);
        }
        break;
    }
    case T_RANDOM:
        for (i = 0; i < len; i++) {
            tb_step_t *st = &S[i];
            unsigned on;
            seed_ui ^= seed_ui << 13, seed_ui ^= seed_ui >> 17, seed_ui ^= seed_ui << 5;
            on = (seed_ui & 127u) < (unsigned)p2;
            if (is_drum(sel)) {
                if (on)
                    st->hits |= (uint16_t)(1u << piece[sel]);
                else
                    st->hits &= (uint16_t)~(1u << piece[sel]), st->hits = TB_HITS(st->hits) ? st->hits : 0;
                continue;
            }
            memset(st, 0, sizeof *st);
            if (!on)
                continue;
            st->hits = 1;
            if (m == TB_M_BREAK)                      /* a break: its slices shuffled (the jungle chop) */
                st->note = (uint8_t)(60u + (seed_ui >> 8) % 32u);
            else if (m == TB_M_ACID) {                /* a 303 line: the scale, octave jumps, accents and slides */
                int nn = (int)last_note[sel] + (int)((seed_ui >> 8) % 8u) - 2 + (((seed_ui >> 12) & 7u) == 0 ? 12 : 0);
                st->note = (uint8_t)tb_scale_snap((unsigned)(nn < 0 ? 0 : nn));
                st->gate = 5;
                st->flags = (uint8_t)((((seed_ui >> 16) & 3u) == 0 ? TB_ACCENT : 0u) | (((seed_ui >> 19) % 5u) == 0 ? TB_SLIDE : 0u));
            }
            else if (m == TB_M_KICK)
                st->note = 60;
            else {
                int n = (int)last_note[sel] + (int)((seed_ui >> 8) % 15u) - 7;
                st->note = (uint8_t)tb_scale_snap((unsigned)(n < 0 ? 0 : n));
                st->gate = (uint8_t)(2u + (seed_ui >> 16) % 10u);
            }
            st->vel = (uint8_t)(80u + (seed_ui >> 20) % 48u);
        }
        break;
    case T_HUMAN:
        for (i = 0; i < len; i++)
            if (S[i].hits) {
                int v = (S[i].vel ? S[i].vel : 100);
                seed_ui ^= seed_ui << 13, seed_ui ^= seed_ui >> 17, seed_ui ^= seed_ui << 5;
                v += (int)((seed_ui >> 9) % (unsigned)(p2 / 2 + 1)) - p2 / 4;
                S[i].vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
            }
        break;
#endif
#if TB_HAS_CLIP
    case T_PCOPY: memcpy(&clip_pat, P, sizeof clip_pat); clip_pat_full = 1; done = "PATTERN COPIED"; break;
    case T_PPASTE:
        if (!clip_pat_full) {
            hud_say("PASTE", "NOTHING TO PASTE", 0);
            return;
        }
        memcpy(P, &clip_pat, sizeof clip_pat);
        done = "PATTERN PASTED";
        break;
#endif
    case T_PCLEAR: memset(P->step, 0, sizeof P->step); memset(P->lock, 0, sizeof P->lock); done = "PATTERN EMPTY"; break;
    case T_QUANT: {                                   /* every hit pulled onto the GRID, STRENGTH % of the way */
        static tb_step_t N[TB_STEPS];
        static const uint8_t G32[5] = {2, 1, 2, 4, 8};
        int gu = (int)G32[p2 % 5] * 32 / (int)PER32[tb_proj.trk[sel].speed % TB_NSPEED], st = p3 < 25 ? 100 : p3;
        memset(N, 0, sizeof N);
        for (i = 0; i < len; i++) {
            tb_step_t e = S[i];
            unsigned h = TB_HITS(e.hits), r;
            int mic = TB_MICRO(e.hits);
            if (!h)
                continue;
            for (r = 0; r < 2u; r++) {                /* (a step and its 32nd: two hits) */
                int u, g;
                if (m == TB_M_909 ? r : !(h >> r & 1u))
                    continue;
                u = (int)i * 32 + (int)r * 16 + mic;
                g = fdiv(u + gu / 2, gu) * gu;
                e.hits = (uint16_t)(m == TB_M_909 ? h : 1u);
                place_hit(N, len, g + (u - g) * (100 - st) / 100, &e, 0);
            }
        }
        memcpy(S, N, len * sizeof *S);
        done = "QUANTIZED";
        break;
    }
#if TB_HAS_STYLE
    case T_STYLE: {                                   /* the kick, the 909 and the break of the first such tracks */
        const style_t *st = &STYLE[(unsigned)p2 % NSTYLE];
        unsigned t, k;
        int tk = -1, td = -1, tbk = -1;
        for (t = 0; t < TB_TRACKS; t++) {
            unsigned mm = tb_proj.trk[t].machine;
            if (mm == TB_M_KICK && tk < 0) tk = (int)t;
            if (mm == TB_M_909 && td < 0) td = (int)t;
            if (mm == TB_M_BREAK && tbk < 0) tbk = (int)t;
        }
        if (tk >= 0) {
            unsigned kl = (unsigned)strlen(st->kick);
            kl = kl > TB_TCAP(tk) ? TB_TCAP(tk) : kl;
            memset(TB_TSTEPS(P, tk), 0, TB_TCAP(tk) * sizeof(tb_step_t)), P->len[tk] = (uint8_t)kl;
            for (k = 0; k < kl; k++) {
                char c = st->kick[k];
                const char *alt = c == '1' ? st->alt1 : c == '2' ? st->alt2 : 0;
                unsigned a;
                if (c != '.')
                    TB_TSTEPS(P, tk)[k].hits = 1, TB_TSTEPS(P, tk)[k].note = 60;
                tb_lock_clear(tb_proj.pattern, (unsigned)tk, k, TB_KICK_TYPE);
                for (a = 0; alt && a < TB_KICK_NPRESETS; a++)
                    if (!strcmp(TB_KICK_PRESETS[a].name, alt)) {
                        tb_lock_set(tb_proj.pattern, (unsigned)tk, k, TB_KICK_TYPE, TB_KICK_PRESETS[a].p[TB_KICK_TYPE]);
                        break;
                    }
            }
            for (k = 0; st->kick_sound && k < TB_KICK_NPRESETS; k++)
                if (!strcmp(TB_KICK_PRESETS[k].name, st->kick_sound)) {
                    memcpy(tb_proj.trk[tk].p, TB_KICK_PRESETS[k].p, TB_KICK_NPARAM);
                    kick_preset_sync((unsigned)tk);
                    break;
                }
        }
        if (td >= 0) {
            static const uint8_t PC[5] = {TB909_CH, TB909_OH, TB909_CP, TB909_SD, TB909_RD};
            const char *row[5] = {st->ch, st->oh, st->cp, st->sd, st->rd};
            unsigned r;
            memset(TB_TSTEPS(P, td), 0, TB_TCAP(td) * sizeof(tb_step_t)), P->len[td] = 16;
            for (r = 0; r < 5u; r++)
                for (k = 0; k < 16u && row[r][k]; k++)
                    if (row[r][k] == 'x')
                        TB_TSTEPS(P, td)[k].hits |= (uint16_t)(1u << PC[r]);
        }
        if (tbk >= 0 && st->brk[0]) {
            memset(TB_TSTEPS(P, tbk), 0, TB_TCAP(tbk) * sizeof(tb_step_t)), P->len[tbk] = 16;
            for (k = 0; k < 16u; k++) {
                char c = st->brk[k];
                if (c == '.')
                    continue;
                TB_TSTEPS(P, tbk)[k].hits = 1, TB_TSTEPS(P, tbk)[k].gate = 8;
                TB_TSTEPS(P, tbk)[k].note = (uint8_t)(60 + (c <= '9' ? c - '0' : c - 'A' + 10));
            }
        }
        if (p3)
            tb_proj.bpm10 = (uint16_t)(st->bpm * 10u);
        done = st->name;
        break;
    }
#endif
    default: break;
    }
    hud_say(TOOL_NAME[tool], done, 255);
}

/* ------------------------------------------------------------------ the keys down --- */
static void key_down(unsigned k, const struct fm1_controls *c)
{
    tb_track_t *tr = &tb_proj.trk[sel];
    unsigned w = key_white(k), idx = key_index(k);
    if (c->buttons & BTN(B_GLO)) {                    /* GLO held: mutes (white 1..8), solos (9..16) */
        bt_used[B_GLO] = 1;
        if (w && idx < TB_TRACKS)
            tb_proj.trk[idx].muted ^= 1u;
        else if (w && idx >= 8u && idx < 8u + TB_TRACKS)
            solo_mask ^= (uint8_t)(1u << (idx - 8u));
        return;
    }
    if (FX_LAYER(c->buttons)) {                       /* FX held (or locked open): the live effects; the black keys mute */
        bt_used[B_FX] = 1;
        unsigned act = w && idx < 16u ? KEY_ACT[idx][0] : 255u;
        if (TB_HAS_RISER && act == KA_RISER) {        /* RISER: from the next bar */
            unsigned bars = TB_DROP_BARS(tb_proj.riser_bars);
            tb_engine_riser();
            snprintf(hud_buf, sizeof hud_buf, "%u BAR%s", bars, bars > 1u ? "S" : "");
            hud_say(tb_engine_riser_state(0) == 2u ? "RISER CANCELLED" : "RISER", hud_buf, 255);
        } else if (act == KA_DROP) {                  /* DROP: the breakdown, from the next bar */
            unsigned bars = TB_DROP_BARS(tb_drop_get(TB_DROP_CLASSIC, TB_DSET_LEN));
            tb_engine_drop();
            snprintf(hud_buf, sizeof hud_buf, "%u BAR%s", bars, bars > 1u ? "S" : "");
            hud_say(tb_engine_drop_state(0) == 2u ? "DROP CANCELLED" : "DROP", hud_buf, 255);
        } else if (TB_HAS_DROPS && act >= KA_DLONG && act <= KA_DLOW) {   /* the DROP's other kinds */
            static const uint8_t DK[5] = {TB_DROP_LONG, TB_DROP_STOP, TB_DROP_KICKROLL, TB_DROP_SNARE, TB_DROP_LOW};
            unsigned kd = DK[act - KA_DLONG];
            tb_engine_drop_kind(kd);
            hud_say(tb_engine_drop_state(0) == 2u ? "DROP CANCELLED" : TB_DROP_NAME[kd], "FROM THE NEXT BAR", 255);
        } else if (TB_HAS_LIVEFX && act == KA_PF)
            pf_keys |= 1u << KEY_ACT[idx][1], pf_lit |= (uint16_t)(1u << idx);
        else if (!w && idx < TB_TRACKS) {             /* black 1..6: the tracks' mutes */
            tb_proj.trk[idx].muted ^= 1u;
            hud_say(tb_proj.trk[idx].muted ? "MUTE" : "UNMUTE", tb_proj.trk[idx].name, 255);
        }
#ifdef RFM2
        else if (TB_HAS_LIVEFX && !w && idx >= TB_TRACKS && idx < 2u * TB_TRACKS - 1u) {   /* black 7..11: held, a live
                                                       * effect (19 Nov 2026, in the solos' place) */
            unsigned e = BK_PF[idx - TB_TRACKS];
            pf_keys |= 1u << e, pf_black |= (uint8_t)(1u << (idx - TB_TRACKS));
            hud_say(TB_PF_NAME[e], "HELD", 255);
        }
#else
        else if (TB_HAS_LIVEFX && !w && idx == 8u)    /* black 9: BUILD ROLL */
            pf_keys |= 1u << TB_PF_BUILD;
#endif
        return;
    }
    if (c->buttons & BTN(B_EDIT)) {                   /* EDIT held: the machine (white 1..5) */
        bt_used[B_EDIT] = 1;
        unsigned mm = TB_M_KICK + idx;
        if (w && idx < TB_NMACHINE - 1u && (TB_HAS_SAMPLE || mm != TB_M_SAMPLE) && (TB_HAS_SYNTH || mm != TB_M_SYNTH) &&
            (TB_HAS_ACID || mm != TB_M_ACID) && (!TB_RFM2_NO909 || mm != TB_M_909) && (!TB_RFM2_NOSMP || mm != TB_M_SAMPLE)) {
            touch();
            tb_engine_panic(sel);
            tb_engine_set_machine(sel, TB_M_KICK + idx);
            preset[sel] = 0;
            kick_preset_sync(sel);
            hud_say("MACHINE", MACHINE_NAME[TB_M_KICK + idx], idx * 255u / (TB_NMACHINE - 2u));
        }
        return;
    }
    if (c->buttons & BTN(B_SCL)) {                    /* SCL held: the key is the root */
        bt_used[B_SCL] = 1;
        tb_proj.root = (uint8_t)((53u + k) % 12u);
        hud_say("ROOT", NOTE_NAME[tb_proj.root], tb_proj.root * 255u / 11u);
        return;
    }
    if (menu == TB_MENU_NOTES) {                      /* EDIT: a key gives the note its pitch, or chooses a hit */
        ned_key(k);
        return;
    }
    if (TB_HAS_ROLL && (c->buttons & BTN(B_ARP)) && menu != TB_MENU_SEQ) {   /* ARP held: NOTE REPEAT (the key repeats) */
        if (!bt_used[B_ARP]) {
            snprintf(hud_buf, sizeof hud_buf, "%s", TB_ARP_RATE_NAME[tr->arp_rate % TB_ARP_NRATE]);
            hud_say("NOTE REPEAT", hud_buf, 255);
        }
        bt_used[B_ARP] = 1;
        tb_engine_repeat(sel, rec && tb_engine_playing());
    }
    if (menu == TB_MENU_SEQ && melodic(sel)) {        /* SEQ, a melody: step by step */
        seq_enter(k);
        return;
    }
    if (menu == TB_MENU_SEQ) {
        if (w)
            seq_key_down(idx);
        else if (is_drum(sel) && idx < TB909_PIECES) {
            piece[sel] = (uint8_t)idx;
            tb_engine_preview(sel, idx, 110, 180);
        }
        return;
    }
    if (tr->machine == TB_M_909) {
        if (!w || idx >= TB909_PIECES)
            return;
        piece[sel] = (uint8_t)idx;
        tb_engine_note(sel, idx, 110);
        key_note_on[k] = (uint8_t)(idx + 1u);
        key_at[k] = fm1_millis();
        if (rec && tb_engine_playing())
            rec_down(k, idx);
        return;
    }
    {
        unsigned n = note_of_key(k);
        tb_engine_note(sel, n, 110);
        key_note_on[k] = (uint8_t)(n + 1u);
        key_at[k] = fm1_millis();
        if (tr->machine != TB_M_BREAK)
            last_note[sel] = (uint8_t)n;
        if (rec && tb_engine_playing() && !(tr->arp && arp_allowed(sel)))
            rec_down(k, n);
    }
}

static void key_up(unsigned k)
{
    unsigned idx = key_index(k);
    if (key_white(k) && idx < 16u && ((pf_lit >> idx) & 1u)) {   /* (a live effect let go) */
        pf_keys &= ~(1u << KEY_ACT[idx][1]), pf_lit &= (uint16_t)~(1u << idx);
        return;
    }
#ifdef RFM2
    if (!key_white(k) && idx >= TB_TRACKS && idx < 2u * TB_TRACKS - 1u && ((pf_black >> (idx - TB_TRACKS)) & 1u)) {
        pf_keys &= ~(1u << BK_PF[idx - TB_TRACKS]), pf_black &= (uint8_t)~(1u << (idx - TB_TRACKS));   /* (let go) */
        return;
    }
#endif
    if (!key_white(k) && idx == 8u && ((pf_keys >> TB_PF_BUILD) & 1u)) {   /* (BUILD ROLL let go) */
        pf_keys &= ~(1u << TB_PF_BUILD);
        return;
    }
    if (menu == TB_MENU_SEQ && key_note_on[k]) {      /* (a note entered: let go) */
        tb_engine_note(sel, key_note_on[k] - 1u, 0);
        key_note_on[k] = 0;
        held = -1;
        return;
    }
    if (menu == TB_MENU_SEQ) {
        if (key_white(k))
            seq_key_up(idx);
        return;
    }
    if (key_note_on[k]) {
        tb_engine_note(sel, key_note_on[k] - 1u, 0);
        key_note_on[k] = 0;
    }
    if (rec)
        rec_up(k);
    else
        key_step[k] = -1;
}

/* --------------------------------------------------------- undo, projects, tap --- */
static void swap_bytes(void *a, void *b, size_t n)
{
    uint8_t *x = (uint8_t *)a, *y = (uint8_t *)b, t;
    while (n--)
        t = *x, *x++ = *y, *y++ = t;
}
/* the song as it is, kept before a run of changes (a change after 1.2 s of none starts a new run) */
static void touch(void)
{
    uint32_t now = fm1_millis();
    if (!TB_HAS_UNDO)
        return;
    if (!undo.valid || now - touch_at > 1200u || undo.track != sel || undo.pattern != tb_proj.pattern) {
        const tb_pattern_t *P = &tb_proj.pat[tb_proj.pattern];
        undo.track = (uint8_t)sel;
        memcpy(undo.len, P->len, sizeof undo.len), memcpy(undo.lock, P->lock, sizeof undo.lock);
        memcpy(undo.step, TB_TSTEPS(P, sel), TB_TCAP(sel) * sizeof *undo.step);
        undo.valid = 1, undo.pattern = tb_proj.pattern, undo.swing = tb_proj.swing, undo.root = tb_proj.root;
        undo.scale = tb_proj.scale, undo.transpose = tb_proj.transpose, undo.bpm10 = tb_proj.bpm10;
        undo.fx = tb_proj.fx;
        memcpy(undo.trk, tb_proj.trk, sizeof undo.trk);
    }
    touch_at = now;
}
/* SAVE held: the song and the kept one swap places (undo; again: redo) */
static void undo_swap(void)
{
    unsigned t;
    if (!undo.valid) {
        hud_say("UNDO", "NOTHING TO UNDO", 0);
        return;
    }
    {
        tb_pattern_t *P = &tb_proj.pat[undo.pattern % TB_PATTERNS];
        unsigned ut = undo.track % TB_TRACKS;
        swap_bytes(P->len, undo.len, sizeof undo.len), swap_bytes(P->lock, undo.lock, sizeof undo.lock);
        swap_bytes(TB_TSTEPS(P, ut), undo.step, TB_TCAP(ut) * sizeof *undo.step);
    }
    swap_bytes(tb_proj.trk, undo.trk, sizeof undo.trk);
    swap_bytes(&tb_proj.fx, &undo.fx, sizeof undo.fx);
    swap_bytes(&tb_proj.swing, &undo.swing, 1), swap_bytes(&tb_proj.root, &undo.root, 1);
    swap_bytes(&tb_proj.scale, &undo.scale, 1), swap_bytes(&tb_proj.transpose, &undo.transpose, 1);
    swap_bytes(&tb_proj.bpm10, &undo.bpm10, sizeof undo.bpm10);
    for (t = 0; t < TB_TRACKS; t++)
        tb_engine_panic(t);
    undone = !undone;
    touch_at = 0;                                     /* (the next change: a new run) */
    hud_say(undone ? "UNDONE" : "REDONE", undone ? "SAVE HELD: REDO" : "SAVE HELD: UNDO", undone ? 0u : 255u);
}

/* (RAVE FM 2) PRESETS goes through each list in alphabetical order inside its category: the kicks by their kind, the
 * synth's sounds by their family (the families in the library's order), the 303's and the breaks by name. The lists
 * stay as they are (a project keeps what it keeps); ord_* gives the list's index at each place of the order */
static uint8_t ord_kick[256], ord_syn[256], ord_acid[64], ord_brk[64], ord_ready;
static int fam_rank(const char *f)
{
    unsigned i;
    for (i = 0; i < TBS_NSOUND; i++)
        if (!strcmp(TBS_SOUND[i].family, f))
            return (int)i;
    return 0;
}
static unsigned kick_fam(unsigned i)                  /* a kick's family: its kind (the first version's: the same) */
{
    unsigned k = TB_KICK_PRESETS[i].p[TB_KICK_TYPE], kc = TB_KICK_NTYPES - 22u;   /* (22: the first version's kinds, */
    return k >= kc ? k - kc : k;                                                    /* the first 22 of RAVE FM's) */
}
static int kick_rank(unsigned i)                      /* the families in the bank's order (where each first comes) */
{
    unsigned j, f = kick_fam(i);
    for (j = 0; j < TB_KICK_NPRESETS; j++)
        if (kick_fam(j) == f)
            return (int)j;
    return 0;
}
static int ord_before(unsigned m, unsigned a, unsigned b)   /* list item a comes before b */
{
    int c = 0;
    const char *na = "", *nb = "";
    switch (m) {
    case TB_M_KICK:
        c = kick_rank(a) - kick_rank(b);
        na = TB_KICK_PRESETS[a].name, nb = TB_KICK_PRESETS[b].name;
        break;
#if TB_HAS_SYNTH
    case TB_M_SYNTH:
        c = fam_rank(TBS_SOUND[a].family) - fam_rank(TBS_SOUND[b].family);
        na = TBS_SOUND[a].name, nb = TBS_SOUND[b].name;
        break;
#endif
#if TB_HAS_ACID
    case TB_M_ACID: na = TBA_SOUND[a].name, nb = TBA_SOUND[b].name; break;
#endif
    default: na = tb_brk_name(a), nb = tb_brk_name(b); break;
    }
    if (!c)
        c = strcmp(na, nb);
    return c < 0 || (c == 0 && a < b);
}
static void ord_sort(unsigned m, uint8_t *o, unsigned n)
{
    unsigned i, j;
    for (i = 0; i < n; i++) {                         /* (insertion: ~150 at most, once) */
        uint8_t v = (uint8_t)i;
        for (j = i; j > 0 && ord_before(m, v, o[j - 1]); j--)
            o[j] = o[j - 1];
        o[j] = v;
    }
}
static unsigned ord_n(unsigned m)
{
    unsigned n = m == TB_M_KICK ? TB_KICK_NPRESETS : m == TB_M_SYNTH ? (TB_HAS_SYNTH ? TBS_NSOUND : 0u) :
                 m == TB_M_ACID ? (TB_HAS_ACID ? TBA_NSOUND : 0u) : m == TB_M_BREAK || m == TB_M_SAMPLE ? tb_brk_count() : 0u;
    unsigned cap = m == TB_M_KICK || m == TB_M_SYNTH ? 256u : 64u;
    return n > cap ? cap : n;
}
static uint8_t *ord_of(unsigned m)
{
    if (!ord_ready) {
        ord_sort(TB_M_KICK, ord_kick, ord_n(TB_M_KICK));
        ord_sort(TB_M_SYNTH, ord_syn, ord_n(TB_M_SYNTH));
        ord_sort(TB_M_ACID, ord_acid, ord_n(TB_M_ACID));
        ord_sort(TB_M_BREAK, ord_brk, ord_n(TB_M_BREAK));
        ord_ready = 1;
    }
    return m == TB_M_KICK ? ord_kick : m == TB_M_SYNTH ? ord_syn : m == TB_M_ACID ? ord_acid : ord_brk;
}
static unsigned ord_at(unsigned m, unsigned pos)      /* the list's index at this place of the order */
{
    unsigned n = ord_n(m);
    return n ? ord_of(m)[pos < n ? pos : n - 1u] : 0u;
}
static unsigned ord_pos(unsigned m, unsigned idx)     /* the place of the list's index in the order */
{
    unsigned n = ord_n(m), i;
    const uint8_t *o = ord_of(m);
    for (i = 0; i < n; i++)
        if (o[i] == idx)
            return i;
    return 0;
}

/* a track's preset: the place in the order of the sound its knobs are (its name on the screen, PRESETS going on from
 * it; none: the first) */
static void kick_preset_sync(unsigned t)
{
    unsigned i, m = tb_proj.trk[t].machine;
    if (m == TB_M_KICK) {
        for (i = 0; i < TB_KICK_NPRESETS; i++)
            if (!memcmp(TB_KICK_PRESETS[i].p, tb_proj.trk[t].p, TB_KICK_NPARAM)) {
                preset[t] = (uint8_t)ord_pos(m, i);
                return;
            }
    }
#if TB_HAS_SYNTH
    if (m == TB_M_SYNTH)
        for (i = 0; i < TBS_NSOUND; i++)
            if (!memcmp(TBS_SOUND[i].p, tb_proj.trk[t].p, TBS_NPARAM)) {
                preset[t] = (uint8_t)ord_pos(m, i);
                return;
            }
#endif
    if (m == TB_M_BREAK || m == TB_M_SAMPLE)
        preset[t] = (uint8_t)ord_pos(m, tb_proj.trk[t].p[TBB_SRC]);
}
static void after_load(void)
{
    proj_fresh = 0;
    unsigned t;
    gfx_palette(tb_proj.palette < GFX_NPAL ? tb_proj.palette : 0);
    for (t = 0; t < TB_TRACKS; t++)
        tb_engine_panic(t), preset[t] = 0, seq_page[t] = 0, kick_preset_sync(t);
    undo.valid = 0, held = -1, solo_mask = 0;
}
/* SAVE: into the project's place (the MENU open: into the place chosen; one in use asks first) */
static void save_button(void)
{
    unsigned slot = menu == TB_MENU_SYS ? slot_sel : proj_slot;
    int r;
    if (menu == TB_MENU_SYS && slot != proj_slot && tb_store_info(slot, 0, 0) && sys_ask != 3u) {
        sys_ask = 3, sys_ask_at = fm1_millis();
        return;
    }
    if (menu != TB_MENU_SYS && proj_fresh && tb_store_info(slot, 0, 0) && sys_ask != 3u) {
        static char q[12];                            /* (every place in use: the first one asked before) */
        snprintf(q, sizeof q, "PROJECT %u", slot + 1u);
        sys_ask = 3, sys_ask_at = fm1_millis();
        hud_say(q, "IN USE: SAVE AGAIN", 0);
        return;
    }
    sys_ask = 0;
    r = tb_store_save(slot);
    snprintf(hud_buf, sizeof hud_buf, r == -4 ? "TOO BIG" : r ? "ERROR" : "SAVED");
    if (!r)
        proj_slot = (uint8_t)slot, proj_fresh = 0, memcpy(slot_name[slot], tb_proj.pname, 8), slot_name[slot][8] = 0;
    hud_title = 0;
    {
        static char t[12];
        snprintf(t, sizeof t, "PROJECT %u", slot + 1u);
        hud_say(t, hud_buf, r ? 0u : 255u);
    }
}
/* the MENU: OCT+ loads the place chosen, OCT- starts a new project (each asked once, then done) */
static void sys_action(unsigned what)
{
    static char t[12];
    snprintf(t, sizeof t, "PROJECT %u", slot_sel + 1u);
    if (sys_ask != what) {
        if (what == 1u && !tb_store_info(slot_sel, 0, 0)) {
            hud_say(t, "EMPTY", 0);
            return;
        }
        sys_ask = (uint8_t)what, sys_ask_at = fm1_millis();
        return;
    }
    sys_ask = 0;
    tb_engine_play(0);
    if (what == 1u) {
        int ok;
        tb_engine_hold(1);                            /* (the sound held while the project is read in) */
        ok = tb_store_load(slot_sel);
        if (ok)
            proj_slot = slot_sel, after_load();
        tb_engine_hold(0);
        hud_say(t, ok ? "LOADED" : "UNREADABLE", ok ? 255 : 0);
        return;
    }
    {                                                 /* a new one: in the first empty place (else the one chosen) */
        unsigned k;
        uint8_t pal = tb_proj.palette, pad = tb_proj.pad;
        tb_engine_hold(1);
        tb_engine_default_project();
        tb_proj.palette = pal, tb_proj.pad = pad;     /* (the colour and the light as they are) */
        after_load();
        tb_engine_hold(0);
        proj_slot = slot_sel;
        for (k = 0; k < TB_STORE_SLOTS; k++)
            if (!tb_store_info(k, 0, 0)) {
                proj_slot = (uint8_t)k;
                break;
            }
        slot_sel = proj_slot;
        snprintf(t, sizeof t, "PROJECT %u", proj_slot + 1u);
        hud_say(t, "NEW", 255);
    }
}

#if TB_HAS_TAP
/* GLO held + PLAY tapped: the tempo from the last taps (a pause of 2 s: start again) */
static void tap(void)
{
    uint32_t now = fm1_millis();
    unsigned i;
    if (taps && now - tap_at[taps - 1u] > 2000u)
        taps = 0;
    if (taps == 5u) {
        for (i = 0; i < 4u; i++)
            tap_at[i] = tap_at[i + 1u];
        taps = 4;
    }
    tap_at[taps++] = now;
    if (taps >= 2u) {
        uint32_t span = tap_at[taps - 1u] - tap_at[0];
        unsigned bpm10 = span ? (unsigned)(600000u * (taps - 1u) / span) : 0u;
        if (bpm10 >= 600u && bpm10 <= 3000u) {
            touch();
            tb_proj.bpm10 = (uint16_t)((bpm10 + 5u) / 10u * 10u);
        }
    }
    snprintf(hud_buf, sizeof hud_buf, taps >= 2u ? "%u" : "TAPE...", tb_proj.bpm10 / 10u);
    hud_say("TAP TEMPO", hud_buf, (tb_proj.bpm10 - 600u) * 255u / 2400u);
}
#endif

/* --------------------------------------------------------------------- MIDI --- */
/* in (the routing task: counted and queued, nothing more): the clock, start and stop, notes for the track */
static volatile uint32_t m_clocks;
static volatile uint8_t m_start, m_stop, mq_w, mq_r;
static volatile uint8_t mq[32][3];
#ifdef TB_PORT
#define fm1_app_midi_received tb_app_midi_received
#endif
void fm1_app_midi_received(unsigned source, const uint8_t *m, size_t n)
{
    (void)source;
    if (!TB_HAS_MIDI || !n)
        return;
    switch (m[0]) {
    case 0xF8: m_clocks++; return;
    case 0xFA: case 0xFB: m_start = 1; return;
    case 0xFC: m_stop = 1; return;
    default: break;
    }
    if (n >= 3u && ((m[0] & 0xF0u) == 0x90u || (m[0] & 0xF0u) == 0x80u)) {
        uint8_t w = (uint8_t)((mq_w + 1u) & 31u);
        if (w == mq_r)
            return;
        mq[mq_w][0] = m[0], mq[mq_w][1] = m[1], mq[mq_w][2] = (m[0] & 0xF0u) == 0x80u ? 0 : m[2];
        mq_w = w;
    }
}
static void midi_tick(void)
{
    static uint32_t sent, seen, stamp_n[8], stamp_ms[8];
    static uint8_t was_playing, ns;
    if (!TB_HAS_MIDI)
        return;
    uint32_t now = fm1_millis();
    while (mq_r != mq_w) {                            /* notes from outside: on the selected track */
        unsigned note = mq[mq_r][1], vel = mq[mq_r][2];
        mq_r = (uint8_t)((mq_r + 1u) & 31u);
        if (is_drum(sel)) {                           /* (General MIDI's drums onto the 909's pieces) */
            static const int8_t GM[16] = {TB909_BD, TB909_RS, TB909_SD, TB909_CP, TB909_SD, TB909_LT, TB909_CH, TB909_LT,
                                          TB909_CH, TB909_MT, TB909_OH, TB909_MT, TB909_HT, TB909_CR, TB909_HT, TB909_RD};
            if (note < 35u || note > 51u)
                continue;
            note = (unsigned)GM[note >= 36u ? note - 36u : 0u];
        }
        tb_engine_note(sel, note, vel);
    }
    if (tb_proj.sync) {                               /* SYNC MIDI: the tempo of the clock coming in, its start / stop */
        uint32_t c = m_clocks;
        if (m_start)
            m_start = 0, tb_engine_play(1);
        if (m_stop)
            m_stop = 0, tb_engine_play(0);
        if (c != seen) {
            seen = c;
            if (ns == 8u) {
                memmove(stamp_n, stamp_n + 1, 7 * sizeof *stamp_n), memmove(stamp_ms, stamp_ms + 1, 7 * sizeof *stamp_ms);
                ns = 7;
            }
            stamp_n[ns] = c, stamp_ms[ns] = now, ns++;
            if (ns >= 2u && stamp_n[ns - 1u] - stamp_n[0] >= 24u && now - stamp_ms[0] < 4000u) {
                uint32_t dn = stamp_n[ns - 1u] - stamp_n[0], dms = stamp_ms[ns - 1u] - stamp_ms[0];
                unsigned bpm10 = dms ? (unsigned)(25000ull * dn / dms) : 0u;     /* 60000 x 10 / 24 */
                if (bpm10 >= 300u && bpm10 <= 3000u && (bpm10 > tb_proj.bpm10 + 4u || bpm10 + 4u < tb_proj.bpm10))
                    tb_proj.bpm10 = (uint16_t)bpm10;
            }
        }
        if (ns && now - stamp_ms[ns - 1u] > 1000u)
            ns = 0;
    } else
        m_start = m_stop = 0;
    if (tb_proj.midi_out && !tb_proj.sync) {          /* MIDI OUT: start, stop and 24 clocks a beat */
        uint8_t msg = 0;
        unsigned k = 0;
        if (tb_engine_playing() && !was_playing)
            msg = 0xFA, fm1_midi_send(&msg, 1), sent = 0;
        else if (!tb_engine_playing() && was_playing)
            msg = 0xFC, fm1_midi_send(&msg, 1);
        if (tb_engine_playing()) {
            uint32_t due = tb_engine_clock24();
            if (due - sent > 48u)
                sent = due;
            msg = 0xF8;
            while (sent < due && k++ < 8u && fm1_midi_send(&msg, 1) == 0)
                sent++;
        }
    }
    was_playing = (uint8_t)tb_engine_playing();
}

/* ------------------------------------------------------- the phone (2nd screen) --- */
/* SysEx, manufacturer 7D (non-commercial), then "TB" and a command. From the phone (or the page): H hello (the
 * state is sent for 6 s after each), C a command, S D E a sample coming in (start, data 7-bit packed, end). To it:
 * V the state, G the selected track's steps, N the tracks' names. */
static uint32_t remote_until, remote_v, remote_g, remote_n;
static uint8_t smp_save, smp_new;
static void remote_send(uint8_t cmd, const uint8_t *d, unsigned n)
{
    uint8_t m[128];
    unsigned i;
    if (n > sizeof m - 6u)
        n = sizeof m - 6u;
    m[0] = 0xF0, m[1] = 0x7D, m[2] = 'T', m[3] = 'B', m[4] = cmd;
    for (i = 0; i < n; i++)
        m[5 + i] = (uint8_t)(d[i] & 0x7Fu);
    m[5 + n] = 0xF7;
    fm1_midi_send(m, n + 6u);
}
static uint32_t r28(const uint8_t *d) { return (uint32_t)d[0] | (uint32_t)d[1] << 7 | (uint32_t)d[2] << 14 | (uint32_t)d[3] << 21; }

#ifdef TB_PORT                                        /* (the FM-1's port: its glue takes the SysEx first) */
#define fm1_app_sysex tb_app_sysex
#endif
void fm1_app_sysex(const uint8_t *m, size_t n)
{
    const uint8_t *d = m + 5;
    unsigned dn;
    if (!TB_HAS_PHONE)
        return;
    if (n < 6u || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 'T' || m[3] != 'B')
        return;
    dn = (unsigned)n - 6u;
    remote_until = fm1_millis() + 6000u;
    switch (m[4]) {
    case 'H':
        remote_n = 0;
        break;
#if TB_HAS_USER_BRK
    case 'S': {                                       /* a sample: its place (127: a new one), length, name */
        char name[12];
        unsigned i;
        if (dn < 5u)
            return;
        for (i = 0; i < 11u && i + 5u < dn; i++)
            name[i] = (char)d[5 + i];
        name[i] = 0;
        if (tb_brk_user_begin(d[0] == 127 ? tb_brk_user.n : d[0], name, r28(d + 1)) < 0)
            hud_say("SAMPLE", "NO ROOM LEFT", 0);
        break;
    }
    case 'D': {                                       /* its bytes: groups of 8, the first the next 7's top bits */
        uint8_t out[112];
        unsigned i, k = 0;
        if (dn < 4u)
            return;
        for (i = 4; i < dn && k < sizeof out; i += 8) {
            unsigned j, hb = d[i];
            for (j = 0; j < 7u && i + 1u + j < dn; j++)
                out[k++] = (uint8_t)(d[i + 1 + j] | (((hb >> j) & 1u) << 7));
        }
        tb_brk_user_write(r28(d), out, k);
        break;
    }
    case 'E':
        tb_brk_user_end();
        smp_save = 1, smp_new = 1;
        break;
#endif
    case 'C': {                                       /* a command: what, and its arguments */
        unsigned a = dn > 1u ? d[1] : 0u, b = dn > 2u ? d[2] : 0u;
        if (!dn)
            return;
        switch (d[0]) {
        case 1: if (a < TB_TRACKS) tb_proj.trk[a].muted ^= 1u; break;
        case 2: if (a < TB_TRACKS) solo_mask ^= (uint8_t)(1u << a); break;
        case 3: if (a < TB_PATTERNS) set(R_PAT, (int)a); break;
        case 4: tb_engine_play(!tb_engine_playing()); break;
        case 5: { unsigned bpm10 = a << 7 | b; if (bpm10 >= 600u && bpm10 <= 3000u) tb_proj.bpm10 = (uint16_t)bpm10; break; }
        case 6: tb_engine_drop(); break;
        case 7: tb_engine_riser(); break;
        case 8: if (a < TB_TRACKS) sel = (uint8_t)a, held = -1, lock_page = 0; break;
        case 9:                                       /* a step of the selected track on / off */
            if (a < len_of(sel)) {
                tb_step_t *st = &TB_STEP(sel, a);
                touch();
                if (is_drum(sel))
                    st->hits ^= (uint16_t)(1u << piece[sel]);
                else if (st->hits)
                    memset(st, 0, sizeof *st);
                else
                    st->hits = 1, st->note = tb_proj.trk[sel].machine == TB_M_BREAK ? (uint8_t)(60u + a % 32u) : last_note[sel];
            }
            break;
#if TB_HAS_MUTATE
        case 10: tb_engine_mutate(!tb_engine_mutating()); break;
#endif
#if TB_HAS_TAP
        case 11: tap(); break;
#endif
        case 12: if (a < TB909_PIECES) piece[sel] = (uint8_t)a; break;
        default: break;
        }
        break;
    }
    default:
        break;
    }
}

static void remote_tick(uint32_t now)
{
    uint8_t d[64];
    unsigned t, k;
    if (!TB_HAS_PHONE)
        return;
    if (smp_save) {                                   /* (a sample in: kept, and onto the track if it plays samples) */
        unsigned m = tb_proj.trk[sel].machine, last = tb_brk_count() - 1u;
        smp_save = 0;
        tb_store_samples_save();
        if (smp_new && (m == TB_M_SAMPLE || m == TB_M_BREAK)) {
            tb_engine_panic(sel);
            tb_proj.trk[sel].p[TBB_SRC] = (uint8_t)last;
            tb_proj.trk[sel].p[TBB_DIV] = (uint8_t)tb_brk_default_div(last);
        }
        smp_new = 0;
        hud_say("SAMPLE", tb_brk_name(last), 255);
    }
    if ((int32_t)(remote_until - now) <= 0)
        return;
    if (now - remote_v >= 80u) {                      /* V: the state */
        unsigned ds = tb_engine_drop_state(0), rs = tb_engine_riser_state(0), frac;
        remote_v = now, k = 0;
        d[k++] = (uint8_t)(tb_proj.bpm10 >> 7), d[k++] = (uint8_t)tb_proj.bpm10;
        d[k++] = (uint8_t)tb_engine_playing(), d[k++] = tb_proj.pattern, d[k++] = (uint8_t)(tb_engine_queued_pattern() + 1);
        d[k++] = sel, d[k++] = (uint8_t)ds, d[k++] = (uint8_t)rs, d[k++] = (uint8_t)tb_engine_mutating();
        d[k++] = tb_proj.song_on, d[k++] = (uint8_t)tb_engine_song_pos(0), d[k++] = rec;
        for (t = 0; t < TB_TRACKS; t++)
            d[k++] = (uint8_t)(tb_proj.trk[t].muted | ((solo_mask >> t) & 1u) << 1 | view.lane[t].muted << 2);
        for (t = 0; t < TB_TRACKS; t++)
            d[k++] = (uint8_t)(view.lane[t].level >> 1);
        for (t = 0; t < TB_TRACKS; t++)
            d[k++] = (uint8_t)tb_engine_step(t, &frac);
        for (t = 0; t < TB_TRACKS; t++)
            d[k++] = (uint8_t)(len_of(t) - 1u);
        for (t = 0; t < TB_TRACKS; t++)
            d[k++] = tb_proj.trk[t].machine;
        remote_send('V', d, k);
    }
    if (now - remote_g >= 200u) {                     /* G: the selected track's steps (on / off), 7 a byte */
        unsigned len = len_of(sel), i;
        remote_g = now, k = 0;
        d[k++] = sel, d[k++] = (uint8_t)(len - 1u), d[k++] = piece[sel];
        for (i = 0; i < 19u; i++)
            d[k + i] = 0;
        for (i = 0; i < len; i++) {
            const tb_step_t *st = &TB_STEP(sel, i);
            if (is_drum(sel) ? (st->hits >> piece[sel]) & 1u : st->hits != 0)
                d[k + i / 7u] |= (uint8_t)(1u << (i % 7u));
        }
        remote_send('G', d, k + 19u);
    }
    if (now - remote_n >= 2000u) {                    /* N: the names */
        remote_n = now, k = 0;
        for (t = 0; t < TB_TRACKS; t++) {
            unsigned j;
            for (j = 0; j < 7u; j++)
                d[k++] = (uint8_t)(tb_proj.trk[t].name[j] ? tb_proj.trk[t].name[j] : ' ');
        }
        remote_send('N', d, k);
    }
}

/* ----------------------------------------------------------------- the buttons --- */
static void preset_turn(int d)
{
    tb_track_t *t = &tb_proj.trk[sel];
    int p = (int)preset[sel] + (d > 0 ? 1 : -1), n, q;
    if (menu == TB_MENU_SEQ && held >= 0) {           /* SEQ, a step held: its repeats */
        tb_step_t *s = held_step();
        int r = (int)TB_RATCH(s->flags) + (d > 0 ? 1 : -1);
        r = r < 1 ? 1 : r > 4 ? 4 : r;
        step_set(4, r);
        snprintf(hud_buf, sizeof hud_buf, "x%d", r);
        hud_say("REPEAT", hud_buf, (unsigned)(r - 1) * 85u);
        return;
    }
    switch (t->machine) {
    case TB_M_KICK: n = (int)TB_KICK_NPRESETS; break;
    case TB_M_SYNTH: n = TB_HAS_SYNTH ? (int)TBS_NSOUND : 0; break;
    case TB_M_ACID: n = TB_HAS_ACID ? (int)TBA_NSOUND : 0; break;
    case TB_M_BREAK: case TB_M_SAMPLE: n = (int)tb_brk_count(); break;
    default: n = 0;
    }
    if (n <= 0) {
        hud_say("909", "ONE SOUND", 255);
        return;
    }
    p = p < 0 ? 0 : p >= n ? n - 1 : p;
    if (p == (int)preset[sel] && (d > 0 ? p == n - 1 : p == 0))
        return;                                       /* the end of the list: nothing more (no sound) */
    touch();
    preset[sel] = (uint8_t)p;
    q = (int)ord_at(t->machine, (unsigned)p);         /* (the list's index of that place in the order) */
    switch (t->machine) {
    case TB_M_KICK:
        memcpy(t->p, TB_KICK_PRESETS[q].p, TB_KICK_NPARAM);
        hud_say("KICK", TB_KICK_PRESETS[q].name, (unsigned)p * 255u / (unsigned)(n - 1));
        tb_engine_preview(sel, 60, 110, 400);
        break;
#if TB_HAS_SYNTH
    case TB_M_SYNTH: {
        const tb_syn_sound_t *snd = &TBS_SOUND[q];
        tb_engine_panic(sel);
        memcpy(t->p, snd->p, TBS_NPARAM);
        /* (the scale and its chord stay as they are: another sound, the same SCL) */
        if (!strcmp(snd->family, "HOOVER") && t->chorus < 60)
            t->chorus = 90;                           /* (a hoover is wide: its chorus) */
        if (snd->p[TBS_WAVE] == TBS_RAVE && snd->p[TBS_WHOOP])
            t->rev = snd->p[TBS_WHOOP];               /* (the first version's leads: their reverb was part of the sound) */
        hud_say(snd->family, snd->name, (unsigned)p * 255u / (unsigned)(n - 1));
        tb_engine_preview(sel, last_note[sel], 110, 300);   /* (heard as it is chosen) */
        break;
    }
#endif
    case TB_M_BREAK:
        tb_engine_panic(sel);
        t->p[TBB_SRC] = (uint8_t)q;
        t->p[TBB_DIV] = (uint8_t)tb_brk_default_div((unsigned)q);
        t->p[TBB_MODE] = TBB_FULL;                    /* a break chosen: a key launches it (one-shot, to its end) */
        tb_engine_break_loop(tb_proj.pattern, sel);
        hud_say("BREAK", tb_brk_name((unsigned)q), (unsigned)p * 255u / (unsigned)(n > 1 ? n - 1 : 1));
        if (!tb_engine_playing())                     /* (stopped: heard from its start, 2.5 s, then let go) */
            tb_engine_preview(sel, 60, 110, 2500);
        break;
#if TB_HAS_ACID
    case TB_M_ACID:
        tb_acid_sound(t->p, (unsigned)q);
        hud_say("ACID", TBA_SOUND[q].name, (unsigned)p * 255u / (unsigned)(n - 1));
        break;
#endif
    case TB_M_SAMPLE:
        t->p[TBB_SRC] = (uint8_t)q;
        hud_say("SOURCE", tb_brk_name((unsigned)q), (unsigned)p * 255u / (unsigned)(n > 1 ? n - 1 : 1));
        break;
    default:
        break;
    }
}

/* (RAVE FM 2) the selected track's sound, for the header: its preset's name (a break, a sample: its source) */
static const char *sound_now(unsigned t)
{
    const tb_track_t *tr = &tb_proj.trk[t];
    unsigned q;
    switch (tr->machine) {
    case TB_M_KICK: q = ord_at(tr->machine, preset[t]); return q < TB_KICK_NPRESETS ? TB_KICK_PRESETS[q].name : 0;
#if TB_HAS_SYNTH
    case TB_M_SYNTH: q = ord_at(tr->machine, preset[t]); return q < TBS_NSOUND ? TBS_SOUND[q].name : 0;
#endif
#if TB_HAS_ACID
    case TB_M_ACID: q = ord_at(tr->machine, preset[t]); return q < TBA_NSOUND ? TBA_SOUND[q].name : 0;
#endif
    case TB_M_BREAK: case TB_M_SAMPLE: return tr->p[TBB_SRC] < tb_brk_count() ? tb_brk_name(tr->p[TBB_SRC]) : 0;
    default: return 0;
    }
}

/* a page button: opens its page, or (open already) the next page */
/* (RAVE FM 2) a page's group: the first word of its title (MIXER 1-4 and MIXER 5-8 one group, DROP and DROP + one) */
static int same_group(const char *a, const char *b)
{
    while (*a && *a != ' ' && *a == *b)
        a++, b++;
    return (*a == 0 || *a == ' ') && (*b == 0 || *b == ' ');
}
/* a page button: from elsewhere, its section as it was left; inside it, the first page of the next group */
static unsigned next_group(const pg_t *P, unsigned n, unsigned pg)
{
    unsigned i;
    if (!P || n < 2u)
        return 0;
    pg %= n;                                          /* (a page left on another track's longer list) */
    i = pg;
    do
        i = (i + 1u) % n;
    while (i != pg && i && same_group(P[i].title, P[pg].title));
    return i;
}
static void page_button(unsigned m)
{
    if (menu == m) {
        unsigned n;
        const pg_t *P = menu_pages(m, &n);
        if (n > 1u)
            menu_page[m] = (uint8_t)next_group(P, n, menu_page[m]);
    } else
        menu = (uint8_t)m;
    held = -1;
}

static void buttons(const struct fm1_controls *c)
{
    uint32_t b = c->buttons, down = b & ~prev_btn, up = ~b & prev_btn, now = fm1_millis();
    unsigned len = len_of(sel), npages = (len + win_steps(sel) - 1u) / win_steps(sel), i;
    for (i = 0; i < 14u; i++)                         /* (each button: when it went down, held long, used with keys) */
        if (down & BTN(i))
            bt_at[i] = now, bt_used[i] = 0, bt_long[i] = 0;
#if TB_HAS_TAP
    if ((down & BTN(B_PLAY)) && (b & BTN(B_GLO))) {   /* GLO held + PLAY: tap tempo */
        bt_used[B_GLO] = 1;
        tap();
    } else
#endif
    if (down & BTN(B_PLAY)) {
        if (TB_HAS_REC && rec && !tb_engine_playing() && !tb_engine_counting() && tb_proj.metro_vol && tb_proj.metro_vol <= 4u)
            tb_engine_countin(COUNT_BEATS[tb_proj.metro_vol]);   /* (recording from the bar's start: counted in) */
        tb_engine_play(!tb_engine_playing() && !tb_engine_counting());
    }
    /* REC: armed (playing: what is played is recorded, in time; stopped: from PLAY); held: its settings */
    if (TB_HAS_REC && (b & BTN(B_REC)) && !bt_long[B_REC] && now - bt_at[B_REC] >= LONG_MS) {
        bt_long[B_REC] = 1;
        menu = TB_MENU_GLO, menu_page[TB_MENU_GLO] = (uint8_t)glo_at(GLO_REC), held = -1;
    }
    if (TB_HAS_REC && (up & BTN(B_REC)) && !bt_long[B_REC]) {
        rec = !rec;                                   /* (its light says it: no overlay) */
    }
    /* HOME: the tracks (there already: the strip's next page); held: the MENU */
    if ((b & BTN(B_HOME)) && !bt_long[B_HOME] && now - bt_at[B_HOME] >= LONG_MS) {
        bt_long[B_HOME] = 1;
        menu = TB_MENU_SYS, sys_new = 0, sys_ask = 0, slot_sel = proj_slot;
        names_read();
    }
    if ((up & BTN(B_HOME)) && !bt_long[B_HOME]) {
        if (menu)
            menu = 0;
        else {
            unsigned n;
            const pg_t *P = home_pages(sel, &n);
            home_page[sel] = (uint8_t)next_group(P, n, home_page[sel]);
        }
        held = -1;
    }
    /* the page buttons; those that are also held with keys (FX GLO EDIT SCL) act when let go, if unused */
    if ((down & BTN(B_FX)) && fx_lock) {              /* (FX again: the lock off, nothing else) */
        fx_lock = 0;
        bt_used[B_FX] = 1;
        pf_keys = 0, pf_lit = 0, pf_black = 0;
        hud_say("FX", "UNLOCKED", 255);
    }
    if (up & BTN(B_FX)) {
        if (!bt_used[B_FX] && now - bt_at[B_FX] >= LONG_MS) {   /* held a while, nothing touched: locked open */
            fx_lock = 1;
            hud_say("FX LOCKED", "[FX] TO UNLOCK", 255);
        } else if (!bt_used[B_FX])
            page_button(TB_MENU_FX);
        if (!fx_lock)
            pf_keys = 0, pf_lit = 0, pf_black = 0;
    }
    if ((up & BTN(B_GLO)) && !bt_used[B_GLO])
        page_button(TB_MENU_GLO);
    if ((up & BTN(B_EDIT)) && !bt_used[B_EDIT])
        page_button(TB_MENU_EDIT);
    if (TB_HAS_SCALE && (up & BTN(B_SCL)) && !bt_used[B_SCL])
        page_button(TB_MENU_SCALE);
    if (down & BTN(B_ENV))
        page_button(TB_MENU_ENV);
    if (TB_HAS_LFO && (down & BTN(B_LFO)))
        page_button(TB_MENU_LFO);
    if (down & BTN(B_SEQ)) {                          /* SEQ: the steps; again: the notes one by one; again: the
                                                       * tools; again: the steps */
        menu = menu == TB_MENU_SEQ ? (TB_HAS_NOTES ? TB_MENU_NOTES : TB_HAS_TOOLS ? TB_MENU_TOOLS : TB_MENU_SEQ)
             : menu == TB_MENU_NOTES ? (TB_HAS_TOOLS ? TB_MENU_TOOLS : TB_MENU_SEQ) : TB_MENU_SEQ;
        if (menu == TB_MENU_SEQ)
            hud_say("WRITE", melodic(sel) ? "A KEY: A NOTE \xc2\xb7 ARP: A REST" : "WHITE KEYS: STEPS ON / OFF", 0);
        else if (menu == TB_MENU_NOTES)
            hud_say("EDIT", "OCT- DELETES THE NOTE", 128);
        else
            hud_say("TOOLS", "OCT+: DO IT", 255);
        if (menu == TB_MENU_SEQ)
            seq_strip = 0;                            /* (the sequencer opens on its first page) */
        held = -1;
        if (seq_page[sel] >= npages)
            seq_page[sel] = 0;
    }
    /* ARP: its page; held alone: on / off; held with keys: NOTE REPEAT (see key_down) */
    if (up & BTN(B_ARP)) {
        tb_engine_repeat(-1, 0);
        if (bt_used[B_ARP])
            ;
        else if (now - bt_at[B_ARP] >= LONG_MS) {
            if (TB_HAS_ARP && arp_allowed(sel)) {
                uint8_t m = tb_proj.trk[sel].arp ? 0 : (last_mode[sel] ? last_mode[sel] : TB_ARP_UP);
                set(R_ARP, m);
                hud_say("ARP", TB_ARP_NAME[m], m ? 255u : 0u);
            } else
                hud_say("ARP", "NOT HERE", 0);
        } else if (menu == TB_MENU_SEQ && melodic(sel))
            seq_rest();                               /* (SEQ, a melody: a rest, the cursor on) */
        else if (TB_HAS_NOTES && menu == TB_MENU_NOTES)
            ned_erase();                              /* (EDIT: the note chosen erased) */
        else if (TB_HAS_ARP)
            page_button(TB_MENU_ARP);
    }
    /* SAVE: the project kept; held: undo (again: redo) */
    if (TB_HAS_UNDO && (b & BTN(B_SAVE)) && !bt_long[B_SAVE] && now - bt_at[B_SAVE] >= LONG_MS) {
        bt_long[B_SAVE] = 1;
        undo_swap();
    }
    if ((up & BTN(B_SAVE)) && !bt_long[B_SAVE])
        save_button();
    /* OCT */
    if ((b & (BTN(B_OCTDN) | BTN(B_OCTUP))) == (BTN(B_OCTDN) | BTN(B_OCTUP)) && (down & (BTN(B_OCTDN) | BTN(B_OCTUP)))) {
        octave = 0;                                   /* both: back to 0 */
        hud_say("OCTAVE", "0", 128);
    } else if (down & (BTN(B_OCTDN) | BTN(B_OCTUP))) {
        int d = (down & BTN(B_OCTUP)) ? 1 : -1;
        if (TB_HAS_TOOLS && menu == TB_MENU_TOOLS) {  /* the tools: OCT+ does it, OCT- back to the steps */
            if (d > 0)
                do_tool();
            else
                menu = TB_MENU_SEQ;
        } else if (TB_HAS_NOTES && menu == TB_MENU_NOTES) {   /* EDIT: OCT- deletes the note chosen */
            if (d < 0)
                ned_erase();
        } else if (menu == TB_MENU_SYS) {
            sys_action(d > 0 ? 1u : 2u);
        } else if (menu == TB_MENU_SEQ && held >= 0 && !melodic(sel)) {
            tb_step_t *s = held_step();
            if (s && !is_drum(sel)) {
                int n = (s->note ? s->note : 60) + d;
                step_set(0, n < 24 ? 24 : n > 108 ? 108 : n);
                if (tb_proj.trk[sel].machine == TB_M_KICK)
                    snprintf(hud_buf, sizeof hud_buf, s->note == 60 ? "0" : "%+d", s->note - 60);
                else
                    note_text(hud_buf, sizeof hud_buf, s->note);
                hud_say(d > 0 ? "+1 SEMITONE" : "-1 SEMITONE", hud_buf, (s->note - 24u) * 255u / 84u);
                tb_engine_preview(sel, s->note, 100, 180);
            }
        } else if (menu == TB_MENU_SEQ && !melodic(sel)) {
            int p = (int)seq_page[sel] + d;
            seq_page[sel] = (uint8_t)(p < 0 ? 0 : p >= (int)npages ? (int)npages - 1 : p);
        } else {
            octave = (int8_t)(octave + d < -3 ? -3 : octave + d > 3 ? 3 : octave + d);
            snprintf(hud_buf, sizeof hud_buf, octave ? "%+d" : "0", octave);
            hud_say("OCTAVE", hud_buf, (unsigned)(octave + 3) * 255u / 6u);
        }
    }
}

static void input(const struct fm1_controls *c)
{
    uint32_t kdown = c->keys & ~prev_keys, kup = ~c->keys & prev_keys;
    unsigned k;
    buttons(c);
    if (TB_HAS_PLOCK && c->encoders[E_SELECT] && menu == TB_MENU_SEQ && held >= 0) {   /* a step held: SELECT its pages (the locks) */
        unsigned n;
        int pg = (int)lock_page + (c->encoders[E_SELECT] > 0 ? 1 : -1);
        const pg_t *lp;
        lock_pg(0, &n);
        lock_page = (uint8_t)(pg < 0 ? 0 : pg >= (int)n ? (int)n - 1 : pg);
        lp = lock_pg(lock_page, &n);
        hud_say(lp ? "LOCKS" : "THE STEP", lp ? lp->title : "NOTE VELO LENGTH", lock_page * 255u / (n - 1u));
    } else if (c->encoders[E_SELECT] && menu == TB_MENU_SEQ) {   /* WRITE: SELECT as KNOB 1 (the cursor, the page) */
        turn(0, c->encoders[E_SELECT] > 0 ? 1 : -1);
    } else if (c->encoders[E_SELECT] && menu == TB_MENU_NOTES) {   /* EDIT: SELECT as KNOB 1 (the note chosen) */
        turn(0, c->encoders[E_SELECT] > 0 ? 1 : -1);
    } else if (c->encoders[E_SELECT] && menu && menu != TB_MENU_SEQ) {   /* SELECT on a page: its pages (as everywhere) */
        unsigned n;
        int d = c->encoders[E_SELECT] > 0 ? 1 : -1;
        if (menu == TB_MENU_TOOLS) {
            int t = (int)tool + d;
            tool = (uint8_t)(t < 0 ? 0 : t >= T_NTOOL ? T_NTOOL - 1 : t);
            if (!tool_p2[tool] && TOOL[tool].k2)
                tool_p2[tool] = TOOL[tool].def2;
            if (tool_p3[tool] < TOOL[tool].lo3 && TOOL[tool].k3)
                tool_p3[tool] = TOOL[tool].def3;
        } else {
            const pg_t *P = menu_pages(menu, &n);
            if (n > 1u) {
                int pg = (int)menu_page[menu] + d;
                menu_page[menu] = (uint8_t)(pg < 0 ? 0 : pg >= (int)n ? (int)n - 1 : pg);
                hud_say("PAGE", P[menu_page[menu]].title, menu_page[menu] * 255u / (n - 1u));
            }
        }
    } else if (c->encoders[E_SELECT] && !(c->buttons & BTN(B_GLO))) {   /* (RAVE FM 2) SELECT on HOME: its pages */
        unsigned n;
        int pg = (int)home_page[sel] + (c->encoders[E_SELECT] > 0 ? 1 : -1);
        const pg_t *P = home_pages(sel, &n);
        home_page[sel] = (uint8_t)(pg < 0 ? 0 : pg >= (int)n ? (int)n - 1 : pg);
        if (P && n > 1u)
            hud_say("PAGE", P[home_page[sel]].title, home_page[sel] * 255u / (n - 1u));
    } else if (c->encoders[E_SELECT]) {               /* GLO held + SELECT: the tempo */
        int d = c->encoders[E_SELECT], v = tb_proj.bpm10 / 10 + (d > 2 || d < -2 ? d * 3 : d);
        bt_used[B_GLO] = 1;
        set(R_BPM, v < 60 ? 60 : v > 300 ? 300 : v);
        snprintf(hud_buf, sizeof hud_buf, "%u", tb_proj.bpm10 / 10u);
        hud_say("TEMPO", hud_buf, (tb_proj.bpm10 - 600u) * 255u / 2400u);
    }
    if (c->encoders[E_ALGO]) {                        /* ALGORITHM: the track */
        int s = (int)sel + (c->encoders[E_ALGO] > 0 ? 1 : -1);
        unsigned i;
        for (i = 0; i < 27u; i++)
            if (key_note_on[i])
                tb_engine_note(sel, key_note_on[i] - 1u, 0), key_note_on[i] = 0;
        sel = (uint8_t)(s < 0 ? 0 : s > TB_TRACKS - 1 ? TB_TRACKS - 1 : s);
        held = -1;
        lock_page = 0;
        if (seq_page[sel] * win_steps(sel) >= len_of(sel))
            seq_page[sel] = 0;
        view.hud = -1;                                /* (the track: the lanes show it, no overlay) */
    }
    for (k = 0; k < 4u; k++) {
        int d = c->encoders[E_K1 + k];
        turn(k, d);                                   /* (audit 2: no x3 when a long frame let detents pile up, a jump
                                                       * that came and went with the screen's load: the speed alone,
                                                       * accel(), sets the sweep) */
    }
    if (c->encoders[E_PRESET])
        preset_turn(c->encoders[E_PRESET]);
    for (k = 0; k < 27u; k++) {
        if (kup & (1u << k))
            key_up(k);
        if (kdown & (1u << k))
            key_down(k, c);
    }
    for (k = 0; k < TB_TRACKS; k++)
        tb_proj.trk[k].solo = (uint8_t)((solo_mask >> k) & 1u);
    tb_engine_perform(pf_keys, sel, piece[sel]);
    tb_engine_click((tb_proj.metro >= 1u && tb_proj.metro <= 4u) || (tb_proj.metro >= 5u && tb_proj.metro <= 8u && rec));
    if (sys_ask && fm1_millis() - sys_ask_at > 4000u)
        sys_ask = 0;
    tb_engine_rec(rec && tb_engine_playing() && tb_proj.trk[sel].arp && arp_allowed(sel) ? (int)sel : -1);
    prev_btn = c->buttons, prev_keys = c->keys;
}

/* ------------------------------------------------------------------- the lights --- */
static void leds(void)
{
    static uint64_t was;
    static uint8_t led_sent;                         /* (the first frame: every LED written, nothing assumed lit) */
    uint64_t on = 0;
    unsigned i;
    if (tb_engine_playing())
        on |= 1ull << B_PLAY;
    if (rec && (tb_engine_playing() || (fm1_millis() / 300u) % 2u))
        on |= 1ull << B_REC;
    if (menu == TB_MENU_SEQ || menu == TB_MENU_TOOLS || menu == TB_MENU_NOTES) on |= 1ull << B_SEQ;
    if (menu == TB_MENU_GLO) on |= 1ull << B_GLO;
    if (!menu) on |= 1ull << B_HOME;
    if (tb_proj.trk[sel].arp && arp_allowed(sel)) on |= 1ull << B_ARP;
    if (menu == TB_MENU_ENV) on |= 1ull << B_ENV;
    if (menu == TB_MENU_LFO) on |= 1ull << B_LFO;
    if (menu == TB_MENU_SCALE) on |= 1ull << B_SCL;
    if (menu == TB_MENU_FX || pf_keys || fx_lock) on |= 1ull << B_FX;
    if (menu == TB_MENU_EDIT) on |= 1ull << B_EDIT;
    if (menu == TB_MENU_SEQ) {                        /* SEQ: the steps on the white keys, the playhead blinking */
        unsigned frac, play = tb_engine_step(sel, &frac), len = len_of(sel);
        for (i = 0; i < 16u; i++) {
            unsigned hf, si = grid_track(sel) ? cell_step(sel, i, &hf) : seq_page[sel] * 16u + i;
            const tb_step_t *st = &TB_STEP(sel, si % TB_TCAP(sel));
            int lit = si < len && (is_drum(sel) ? (st->hits >> piece[sel]) & 1u :
                                   tb_proj.trk[sel].machine == TB_M_KICK ? (TB_HITS(st->hits) >> hf) & 1u : st->hits != 0);
            if (!grid_track(sel))
                hf = 0;
            if (tb_engine_playing() && si == play && !hf)
                lit = !lit;
            if (lit)
                on |= 1ull << (14u + white_key(i));
        }
    } else if (pf_keys)
        for (i = 0; i < 16u; i++)
            if ((pf_keys >> i) & 1u)
                on |= 1ull << (14u + white_key(i));
    for (i = 0; i < 41u; i++)
        if (!led_sent || (((on ^ was) >> i) & 1u))
            fm1_led_set(i, (int)((on >> i) & 1u));
    was = on, led_sent = 1;
    {                                                /* (RAVE FM 2) the backlight: a glow under the dark ones */
        static uint64_t glowed;
        static uint8_t glow_sent;                    /* (the first frame, then every 64: every LED written, whatever was
                                                      * assumed or whatever cleared them) */
        uint64_t g = 3ull;                           /* (OCT- and OCT+: always) */
        unsigned mode = tb_proj.pad & 3u;
        if (mode == 0u || mode == 1u)
            g |= 0x3FFFull;                          /* the 14 buttons */
        if (mode == 0u || mode == 2u)
            g |= ((1ull << 27) - 1ull) << 14;        /* the 27 keys */
        for (i = 0; i < 41u; i++)
            if (!glow_sent || (((g ^ glowed) >> i) & 1u))
                fm1_led_glow(i, (int)((g >> i) & 1u));
        glowed = g, glow_sent = (uint8_t)((glow_sent + 1u) & 63u) | 1u;
        if (glow_sent == 63u)
            glow_sent = 0;
    }
}

/* ------------------------------------------------------------------- the view --- */
static void fill_break_view(void)
{
    tb_seqview_t *s = &view.seq;
    s->slice_lit = -1;
    {                   /* the break itself, its slices marked */
        const tb_track_t *t = &tb_proj.trk[sel];
        uint32_t L = tb_brk_len(t->p[TBB_SRC]), a, b, j, q;
        for (j = 0; TB_HAS_WAVE && j < 200u && L; j++) {
            uint32_t x0 = L * j / 200u, x1 = L * (j + 1u) / 200u, pk = 0;
            for (q = x0; q < x1; q += (x1 - x0) / 10u + 1u) {
                uint32_t v = (uint32_t)tb_brk_peek(t->p[TBB_SRC], q);
                if (v > pk)
                    pk = v;
            }
            s->brk_wave[j] = (uint8_t)(pk > 32767u ? 255u : pk * 255u / 32767u);
        }
        s->nslices = (uint8_t)tb_brk_slices(t->p, &t->man, 0, &a, &b);
        if (s->nslices > 32u)
            s->nslices = 32u;
        for (j = 0; j < s->nslices && L; j++) {
            tb_brk_slices(t->p, &t->man, j, &a, &b);
            s->brk_cut[j] = (uint8_t)(a * 200u / L);
        }
        if (held >= 0) {
            const tb_step_t *h = &TB_STEP(sel, (unsigned)held);
            if (h->hits)
                s->slice_lit = (int8_t)(((int)h->note - 60 + t->p[TBB_START] + 1024) % (s->nslices ? s->nslices : 1));
        }
    }
}

static void build_seq_view(void)
{
    tb_seqview_t *s = &view.seq;
    unsigned len = len_of(sel), i, frac, play = tb_engine_step(sel, &frac);
    unsigned m = tb_proj.trk[sel].machine;
    s->kind = m == TB_M_909 ? TB_SEQ_GRID : m == TB_M_KICK ? TB_SEQ_PADS : m == TB_M_BREAK ? TB_SEQ_SLICES : TB_SEQ_NOTES;
    s->len = (uint8_t)len;
    s->npages = (uint8_t)((len + 15u) / 16u);
    s->page = seq_page[sel];
    s->play_page = (uint8_t)(play / 16u);
    s->grid = grid_track(sel) && menu == TB_MENU_SEQ ? RESN[res_of(sel)] : 0;
    s->wlen = (uint8_t)win_steps(sel), s->win = (uint8_t)(seq_page[sel] * win_steps(sel));
    if (!grid_track(sel)) {                           /* a melody: the screen of 32 its cursor (or the note) is on */
        unsigned at = menu == TB_MENU_NOTES ? ned_at[sel] : seq_cur[sel] % (len ? len : 1u);
        s->win = (uint8_t)(at / 32u * 32u);
    }
    if (tb_engine_playing() && held < 0 && menu == TB_MENU_SEQ && s->wlen && s->wlen < len) {
        /* playing: the screen follows what plays (its 32 steps played, the next ones); the whites with it on a kick, a 909 */
        s->win = (uint8_t)(play / s->wlen * s->wlen);
        if (grid_track(sel))
            seq_page[sel] = (uint8_t)(play / s->wlen);
    }
    if (s->wlen >= len)
        s->win = 0, s->wlen = 0;
    s->play = (int16_t)(tb_engine_playing() ? (int)play : -1);
    s->held = held;
    s->cursor = (int16_t)((held < 0 || melodic(sel)) && seq_cur[sel] < len ? seq_cur[sel] : -1);
    s->cur_w = (uint8_t)(melodic(sel) ? seq_steps(sel) : 1u);   /* (a melody: the next note's length) */
    s->cur_k = -1;
    s->val = melodic(sel) ? VALN[seq_val[sel] % 6u] : 0;
    if (menu == TB_MENU_NOTES) {                     /* EDIT: the note chosen lit, as long as it lasts */
        uint16_t L[NED_MAX];
        unsigned nn = ned_list(L);
        int ix = ned_index(L, nn);
        s->held = -1, s->val = 0;
        s->cursor = (int16_t)(ix >= 0 ? ned_at[sel] : -1);
        s->cur_k = (int8_t)(ix >= 0 && !is_drum(sel) ? ned_k(&TB_STEP(sel, ned_at[sel])) : 0);
        if (ix >= 0) {
            unsigned g = TB_STEP(sel, ned_at[sel]).gate ? TB_STEP(sel, ned_at[sel]).gate : 6u;
            s->cur_w = (uint8_t)(melodic(sel) ? (g + 7u) / 8u : 1u);
            s->page = seq_page[sel] = (uint8_t)(ned_at[sel] / win_steps(sel));
        }
    }
    {                                                 /* the notes shown where they sound (TRANSPO: theirs and the song's) */
        const tb_track_t *tr = &tb_proj.trk[sel];
        s->transpose = (int8_t)(m == TB_M_BREAK || m == TB_M_KICK || m == TB_M_909 ? 0 : tr->transpose + tb_proj.transpose);
    }
    s->piece = piece[sel];
    s->npieces = TB909_PIECES;
    s->piece_name = PIECE;
    s->slice_lit = -1;
    if (s->kind == TB_SEQ_SLICES)
        fill_break_view();
    for (i = 0; i < len; i++) {
        const tb_step_t *st = &TB_STEP(sel, i);
        s->s[i].hits = st->hits, s->s[i].note = st->note, s->s[i].vel = st->vel, s->s[i].gate = st->gate;
        s->s[i].flags = st->flags, s->s[i].lock = 0;
        memcpy(s->s[i].more, st->more, sizeof s->s[i].more);
        s->sl[i] = -1;
        if (s->kind == TB_SEQ_SLICES && TB_HITS(st->hits) && s->nslices)   /* the slice it plays */
            s->sl[i] = (int8_t)(((int)st->note - 60 + tb_proj.trk[sel].p[TBB_START] + 1024) % s->nslices);
    }
    {
        const tb_pattern_t *P = &tb_proj.pat[tb_proj.pattern];
        for (i = 0; i < TB_NLOCK; i++)               /* the steps with locks: marked */
            if (P->lock[i].ref1 && P->lock[i].track == sel && P->lock[i].step < len)
                s->s[P->lock[i].step].lock = 1;
    }
}

/* the kick designer: the kick played once off-line (as it is now), its sound and its pitch over the beat */
static void kick_designer(void)
{
    static tb_kick_t kd;
    static uint8_t key[TB_KICK_NPARAM + 1];
    static uint32_t key_beat;
    const tb_track_t *t = &tb_proj.trk[sel];
    uint32_t beat = (uint32_t)(TB_FS * 600u / (tb_proj.bpm10 ? tb_proj.bpm10 : 1800u)), n = beat > 26460u ? 26460u : beat, i, done = 0;
    int32_t blk[TB_BLOCK], prev = 0, lp1 = 0, lp2 = 0;
    unsigned col = 0, k;
    uint32_t last_up = 0;
    uint16_t hz[50];
    uint32_t age = tb_engine_age(sel);
    view.kick_play = (uint16_t)(age < n ? age * 200u / n : 0xFFFFu);
    view.kick_ms = (uint16_t)(n * 1000u / TB_FS);
    if (!memcmp(key, t->p, TB_KICK_NPARAM) && key_beat == beat && key[TB_KICK_NPARAM] == 1)
        return;                                       /* (unchanged: drawn already) */
    memcpy(key, t->p, TB_KICK_NPARAM), key[TB_KICK_NPARAM] = 1, key_beat = beat;
    memset(view.kick_wave, 0, sizeof view.kick_wave), memset(hz, 0, sizeof hz);
    tb_kick_init(&kd);
    tb_kick_hit(&kd, t->p, 0, beat, 12345u);
    while (done < n) {
        unsigned m = n - done < TB_BLOCK ? n - done : TB_BLOCK;
        memset(blk, 0, sizeof blk);
        tb_kick_render(&kd, t->p, beat, blk, m);
        for (i = 0; i < m; i++, done++) {
            int32_t x = blk[i], a = x < 0 ? -x : x;
            uint8_t v = (uint8_t)(a >= 32767 ? 255 : a * 255 / 32767);
            col = done * 200u / n;
            if (v > view.kick_wave[col])
                view.kick_wave[col] = v;
            lp1 += ((x - lp1) * 1100) >> 15;          /* (the fundamental: the drive's harmonics filtered off) */
            lp2 += ((lp1 - lp2) * 1100) >> 15;
            x = lp2, a = x < 0 ? -x : x;
            if (prev < 0 && x >= 0) {                   /* a cycle starts: its length is the pitch there */
                if (last_up && done - last_up > 20u) {
                    uint32_t f = TB_FS / (done - last_up), s0 = last_up * 50u / n, s1 = done * 50u / n;
                    for (k = s0; k <= s1 && k < 50u; k++)
                        hz[k] = (uint16_t)f;
                }
                last_up = done;
            }
            if (a > 60)
                prev = x;
        }
    }
    for (k = 1; k < 50u; k++)                          /* (gaps: the last pitch heard) */
        if (!hz[k])
            hz[k] = hz[k - 1];
    for (k = 0; k < 50u; k++) {                        /* (a little smoothing) */
        unsigned a0 = hz[k ? k - 1u : 0u], a1 = hz[k], a2 = hz[k < 49u ? k + 1u : 49u];
        view.kick_hz[k] = (uint16_t)((a0 + 2u * a1 + a2) / 4u);
    }
}

static void build_menu_view(void)
{
    const tb_track_t *t = &tb_proj.trk[sel];
    unsigned n, i, pg = mpage();
    const pg_t *P = menu_pages(menu, &n);
    view.menu_pages = (uint8_t)n;
    view.menu_page = (uint8_t)(menu == TB_MENU_GLO ? GLO_OF[pg] : pg);
    view.menu_title = P ? P[pg].title : "";
    if (!TB_HAS_MENU_PICS && menu != TB_MENU_SYS && menu != TB_MENU_TOOLS)
        return;                                       /* (no drawings: nothing more to show) */
    switch (menu) {
    case TB_MENU_ENV: {
        const uint8_t *e = &t->p[t->machine == TB_M_SYNTH ? (pg ? TBS_FILT : TBS_AMP) : TB_P_ENV];
        view.env.a = e[0], view.env.d = e[1], view.env.s = e[2], view.env.r = e[3];
        break;
    }
    case TB_MENU_LFO:
        view.lfo.wave = t->lfo_wave, view.lfo.depth = t->lfo_depth, view.lfo.sync = t->lfo_sync;
        view.lfo.dest = TB_DEST_NAME[t->lfo_dest % TB_NDEST];
        break;
    case TB_MENU_ARP:
        view.arp.rhythm = TB_ARP_RHYTHM[t->arp_rhythm % TB_ARP_NRHYTHM];
        view.arp.on = t->arp != 0, view.arp.latch = t->arp_latch;
        break;
    case TB_MENU_SCALE: {
        uint8_t cn[4];
        snprintf(view.scale_buf, sizeof view.scale_buf, "%s %s", NOTE_NAME[tb_proj.root % 12u],
                 tb_proj.scale ? TB_SCALE_NAME[tb_proj.scale % TB_NSCALE] : "CHROMATIC");
        view.scale_name = view.scale_buf;
        unsigned root = 48u + tb_proj.root % 12u;
        view.scale.root = tb_proj.root;
        view.scale.mask = TB_SCALE_MASK[tb_proj.scale % TB_NSCALE];
        view.scale.nchord = (uint8_t)tb_chord_notes(sel, root, cn);
        for (i = 0; i < view.scale.nchord; i++)
            view.scale.chord[i] = cn[i];
        break;
    }
    case TB_MENU_FX: {                                /* what the page draws: the effect, how much */
        tb_fxview_t *f = &view.fx;
        memset(f, 0, sizeof *f);
        pg = FXK_OF[pg % (sizeof FX_PG / sizeof *FX_PG)];
        f->page = (uint8_t)pg;
        /* which effects play (the ribbon lights them) */
        f->on[FXK_DRIVE] = t->ins[0].type != 0, f->on[FXK_CHO] = t->chorus != 0, f->on[FXK_EXT] = t->ins[1].type != 0;
        for (i = 0; i < TB_NSTK; i++)
            f->on[FXK_FLA + i] = TB_HAS_STACK && tb_proj.stk_mix[sel][i] != 0;
        f->on[FXK_CHO] &= TB_HAS_CHORUS;
        f->on[FXK_TEQ] = t->eq[0] != 64 || t->eq[1] != 64 || t->eq[2] != 64;
        f->on[FXK_REV] = t->rev != 0, f->on[FXK_DLY] = t->dly != 0;
        f->on[FXK_ROUTE] = tb_proj.route_par[sel] || tb_proj.route_send[sel];
        for (i = 0; i < 5; i++)
            f->on[FXK_ROUTE] |= tb_proj.route_ord[sel][i] != i;
        f->on[FXK_ROUTE] &= TB_HAS_STACK;
        if (pg == FXK_DRIVE || pg == FXK_EXT) {
            const tb_ins_set_t *in = &t->ins[pg == FXK_EXT];
            f->type = in->type, f->p[0] = in->p[0], f->p[1] = in->p[1], f->p[2] = in->p[2];
            f->name = TB_INS_NAME[f->type % TB_NINS];
        } else if (pg == FXK_CHO) {
            f->p[0] = t->chorus, f->p[1] = tb_proj.cho_rate[sel], f->p[2] = tb_proj.cho_depth[sel];
        } else if (pg >= FXK_FLA && pg <= FXK_TRM) {
            const tb_ins_set_t *in = &tb_proj.stk[sel][pg - FXK_FLA];
            f->p[0] = tb_proj.stk_mix[sel][pg - FXK_FLA], f->p[1] = in->p[0], f->p[2] = in->p[1], f->p[3] = in->p[2];
        } else if (pg == FXK_TEQ) {
            f->p[0] = t->eq[0], f->p[1] = t->eq[1], f->p[2] = t->eq[2];
            f->name = "EQ";
        } else if (pg == FXK_ROUTE) {
            memcpy(f->ord, tb_proj.route_ord[sel], 5);
            f->par = tb_proj.route_par[sel], f->send = tb_proj.route_send[sel], f->rsel = route_sel % TB_NFX;
            for (i = 0; i < TB_NFX; i++)
                f->fxon |= (uint8_t)(tb_engine_fx_on(sel, i) << i);
            f->rev_on = t->rev || t->dly;
        } else if (pg == FXK_REV) {
            f->type = tb_proj.fx.rev_type, f->p[0] = t->rev, f->p[1] = tb_proj.fx.rev_size, f->p[2] = tb_proj.fx.rev_damp;
            f->name = TB_FX_REV_NAME[f->type % TB_FX_NREV];
        } else {
            f->type = tb_proj.fx.dly_type, f->p[0] = t->dly, f->p[1] = tb_proj.fx.dly_time, f->p[2] = tb_proj.fx.dly_fb;
            f->name = TB_FX_DLYT_NAME[f->type % TB_FX_NDLYT];
        }
        break;
    }
    case TB_MENU_GLO:
        {                                             /* the DROP chosen on its page */
            static const char *const DN[TB_NDROP] = {"DROP", "KICK ROLL", "SNARE BAR", "LOW DROP", "STOP", "LONG DROP"};
            static const char *const DK[TB_NDROP] = {"WHITE 2", "WHITE 8", "WHITE 9", "WHITE 10", "WHITE 4", "WHITE 3"};
            view.dp_len = (uint8_t)tb_drop_get(drop_sel, TB_DSET_LEN), view.dp_roll = (uint8_t)tb_drop_get(drop_sel, TB_DSET_ROLL);
            view.dp_filt = (uint8_t)tb_drop_get(drop_sel, TB_DSET_FILT), view.dp_kick = (uint8_t)tb_drop_get(drop_sel, TB_DSET_KICK);
            view.dp_end = (uint8_t)tb_drop_get(drop_sel, TB_DSET_END);
            view.dp_name = DN[drop_sel % TB_NDROP], view.dp_key = DK[drop_sel % TB_NDROP];
        }
        for (i = 0; i < TB_TRACKS; i++)
            view.mix_vol[i] = tb_proj.trk[i].vol;
        memcpy(view.riser_lvl, tb_proj.riser_lvl, sizeof view.riser_lvl), view.riser_filt = tb_proj.riser_filter;
        {
            unsigned bar;
            view.song_play = (uint8_t)tb_engine_song_pos(&bar);
            view.song_bar = (uint8_t)bar;
            view.song_edit = song_edit, view.song_on = tb_proj.song_on;
            memcpy(view.song_pat, tb_proj.song_pat, TB_SONG), memcpy(view.song_bars, tb_proj.song_bars, TB_SONG);
        }
        break;
    case TB_MENU_TOOLS:
        view.menu_title = "TOOLS";
        view.tool = tool;
        view.tool_n = T_NTOOL;
        view.tool_names = TOOL_NAME;
        view.tool_what = TOOL[tool].what;
        break;
    case TB_MENU_SYS:
        view.sys_new = sys_new;
        view.slot_cur = proj_slot, view.slot_sel = slot_sel, view.sys_ask = sys_ask, view.slot_used = 0;
        {
            struct fm1_audio_stats as;
            memset(&as, 0, sizeof as);
            fm1_audio_stats_read(&as);
            view.dropouts = as.overruns;
        }
        {   /* (the FM-1's spare: the audio's busiest block in the last second, the heap not used yet) */
            static uint32_t load_at;
            if (fm1_millis() - load_at >= 1000u) {
                load_at = fm1_millis();
                view.audio_pct = (uint8_t)tb_board_audio_load(1);
                tb_engine_prof(view.prof_max, view.prof_avg);   /* (the parts' times, TIMER4: tb_engine.h) */
                view.prof_pg = (uint8_t)((view.prof_pg + 1u) % 5u);
                view.heap_k = (uint16_t)(tb_board_heap_free() / 1024u);
            }
        }
        for (i = 0; i < TB_STORE_SLOTS && i < 8u; i++) {
            unsigned bpm = 0;
            if (tb_store_info(i, &bpm, 0))
                view.slot_used |= (uint8_t)(1u << i);
            view.slot_bpm[i] = (uint16_t)bpm;
            view.slot_nm[i] = slot_name[i];
        }
        break;
    case TB_MENU_EDIT:
        view.edit_kind = 0;
        if (t->machine == TB_M_KICK) {
            kick_designer();
            view.kick_name = TB_KICK_PRESETS[ord_at(TB_M_KICK, preset[sel])].name;
            note_text(view.kick_note, sizeof view.kick_note, tb_kick_note(t->p));
        } else if (t->machine == TB_M_SYNTH && pg <= 1u) {   /* OSC: its wave; FILTRE: its curve */
            view.edit_kind = (uint8_t)(1u + pg);
            memcpy(view.edit_p, t->p, 12);
        } else if (t->machine == TB_M_SYNTH && pg == 3u) {   /* MACRO */
            view.edit_kind = 5u;
            view.edit_p[0] = t->p[TBS_MACRO], view.edit_p[1] = t->p[TBS_MTYPE];
            view.edit_p[2] = t->p[TBS_BEND], view.edit_p[3] = t->p[TBS_WHOOP];
        } else if (t->machine == TB_M_ACID && pg <= 1u) {    /* the 303's filter: its curve, where the envelope takes it */
            view.edit_kind = 2;
            memset(view.edit_p, 0, sizeof view.edit_p);
            view.edit_p[4] = t->p[TBA_CUTOFF], view.edit_p[5] = t->p[TBA_RESO], view.edit_p[6] = t->p[TBA_ENVMOD];
        } else if (t->machine == TB_M_BREAK || t->machine == TB_M_SAMPLE) {   /* the break, its slices / the part */
            view.edit_kind = t->machine == TB_M_BREAK ? 3u : 4u;
            fill_break_view();
            view.edit_p[0] = t->p[TBB_START], view.edit_p[1] = t->p[10];
            if (t->machine == TB_M_BREAK && pg == 3u)     /* A LA MAIN: the slice moved, lit */
                view.seq.slice_lit = (int8_t)man_sel;
        }
        break;
    default:
        break;
    }
}

static void build_view(uint32_t now)
{
    unsigned t, k, n;
    const pg_t *sp;
    {                                                 /* (RAVE FM 2) the section's pages: the tabs, the breadcrumb */
        static const char *const SEQT[3] = {"STEPS", "NOTES", "TOOLS"};
        const pg_t *P = 0;
        unsigned on = 0;
        n = 0;
        if (menu == TB_MENU_SEQ || menu == TB_MENU_NOTES || menu == TB_MENU_TOOLS) {
            for (k = 0; k < 3u; k++)
                view.page_title[k] = SEQT[k];
            n = TB_HAS_TOOLS ? 3u : 2u, on = menu == TB_MENU_SEQ ? 0u : menu == TB_MENU_NOTES ? 1u : 2u;
        } else if (menu) {
            P = menu_pages(menu, &n), on = mpage();
        } else
            P = home_pages(sel, &n), on = home_page[sel];
        if (P)
            for (k = 0; k < n && k < 16u; k++)
                view.page_title[k] = P[k].title;
        view.npage = (uint8_t)(n > 16u ? 16u : n), view.page_on = (uint8_t)on;
        {
            static uint32_t at;
            if (now - at > 500u)                      /* (the busiest block of the last half second) */
                at = now, view.load = (uint8_t)tb_board_audio_load(menu != TB_MENU_SYS);
        }
    }
    view.menu = menu, view.menu_page = (uint8_t)(menu == TB_MENU_GLO ? GLO_OF[mpage()] : mpage());
    tb_engine_view(&view, sel);
    view.track = sel;
    view.ms = now;
    view.queued = (int8_t)tb_engine_queued_pattern();
    view.rec = rec;
    view.mute_layer = (uint8_t)((prev_btn & BTN(B_GLO)) && bt_used[B_GLO]);
    view.perform = fx_lock ? 2u : (uint8_t)((prev_btn & BTN(B_FX)) && (bt_used[B_FX] || fm1_millis() - bt_at[B_FX] > 250u));
    {
        unsigned ds = tb_engine_drop_state(0);
        unsigned rs = tb_engine_riser_state(0), rl;
        unsigned dk = tb_engine_drop_kind_now(), dt = dk == TB_DROP_CLASSIC ? 1u : dk == TB_DROP_LONG ? 2u : dk == TB_DROP_STOP ? 3u :
                      dk == TB_DROP_KICKROLL ? 7u : dk == TB_DROP_SNARE ? 8u : dk == TB_DROP_LOW ? 9u : 99u;
        view.tile_lit = pf_lit | (ds && dt < 16u ? 1u << dt : 0u) | (rs ? 1u : 0u);
        view.riser = (uint8_t)rs;
        tb_engine_riser_state(&rl);
        view.riser_left = (uint8_t)rl;
        view.drop = (uint8_t)ds;
        tb_engine_drop_state(&ds);
        view.drop_left = (uint8_t)ds;
        view.mutating = (uint8_t)tb_engine_mutating();
    }
    for (k = 0; k < 16u; k++)
        view.tile[k] = PF_TILE[k];
    view.mute_mask = view.solo_mask = 0;
    for (t = 0; t < TB_TRACKS; t++)
        view.mute_mask |= (uint8_t)((tb_proj.trk[t].muted & 1u) << t);
#ifdef RFM2
    view.solo_mask = pf_black;                        /* (the black keys 7..11: the live effects held) */
    {
        static const char *const BK_KEY[5] = {"TAPE", "K ROLL", "S ROLL", "STUTTER", "ECHO"};   /* (on the keys: short) */
        for (k = 0; k < 5u; k++)
            view.bk_name[k] = BK_KEY[k];
    }
#else
    view.solo_mask = solo_mask;
#endif
    for (t = 0; t < TB_TRACKS; t++) {
        view.lane[t].icon = (uint8_t)(tb_proj.trk[t].machine == TB_M_KICK ? TB_ICON_KICK : tb_proj.trk[t].machine == TB_M_BREAK ? TB_ICON_KIT :
                                      tb_proj.trk[t].machine == TB_M_909 ? TB_ICON_909 : tb_proj.trk[t].machine == TB_M_SAMPLE ? TB_ICON_SAMPLE :
                                      tb_proj.trk[t].machine == TB_M_ACID ? TB_ICON_BASS :
#ifdef RFM2
                                      t == 3 ? TB_ICON_BASS : t == 4 ? TB_ICON_LEAD : TB_ICON_STAB);
#else
                                      t == 3 ? TB_ICON_BASS : t == 4 ? TB_ICON_LEAD : t == 5 ? TB_ICON_STAB : t == 6 ? TB_ICON_HOOVER : TB_ICON_LEAD);
#endif
        view.lane[t].solo = tb_proj.trk[t].solo;
        view.lane[t].beats = (uint8_t)((len_of(t) + 3u) / 4u);
    }
    {                                                 /* the loupe: the selected lane grows, the others make room */
        static int16_t h16[TB_TRACKS];
        static int init;
        int sum = 0;
        for (t = 0; t < TB_TRACKS; t++) {
            int target = (t == sel ? 44 : 16) * 16;
            if (!init)
                h16[t] = (int16_t)target;
            h16[t] = (int16_t)(h16[t] + (target - h16[t]) / 3);
            view.lane_h[t] = (uint8_t)((h16[t] + 8) / 16);
            sum += view.lane_h[t];
        }
        init = 1;
        view.lane_h[sel] = (uint8_t)(view.lane_h[sel] + (156 - sum));
    }
    sp = strip();
    home_pages(sel, &n);
    view.strip_title = sp->title;
    view.sound = sound_now(sel);
#if TB_HAS_SYNTH
    view.family = tb_proj.trk[sel].machine == TB_M_SYNTH && ord_at(TB_M_SYNTH, preset[sel]) < TBS_NSOUND ?
                  TBS_SOUND[ord_at(TB_M_SYNTH, preset[sel])].family : 0;
#endif
    view.strip_pages = (uint8_t)(menu ? 0 : n);
    view.strip_page = (uint8_t)(home_page[sel] % n);
    if (menu == TB_MENU_SEQ)                           /* SEQ: WRITE, EDIT (and MORE), the TOOLS */
        view.strip_title = "WRITE", view.strip_pages = 0, view.strip_page = 0;
    if (menu == TB_MENU_NOTES)
        view.strip_title = "EDIT", view.strip_pages = 0, view.strip_page = 0;
    if (menu == TB_MENU_SEQ && held >= 0) {           /* a step held: its own knobs, or a page of locks */
        unsigned nl;
        const pg_t *lp = lock_pg(lock_page, &nl);
        view.strip_title = lp ? lp->title : "STEPS";
        view.strip_pages = (uint8_t)nl, view.strip_page = lock_page;
    }
    if (menu == TB_MENU_EDIT) {                       /* the editor: its pages as the strip's dots */
        menu_pages(menu, &n);
        view.strip_pages = (uint8_t)n, view.strip_page = (uint8_t)mpage();
    }
    for (k = 0; k < 4u; k++) {
        pd_t d;
        int v;
        if (knob(k, &d, &v)) {
            view.knob_name[k] = d.name;
            value_text(view.knob_value[k], sizeof view.knob_value[k], &d, v);
            view.knob_pos[k] = (uint8_t)(d.max > d.min ? (v - (int)d.min) * 255 / ((int)d.max - (int)d.min) : 0);
            view.knob_bip[k] = (uint8_t)(d.fmt == F_SEMI || d.fmt == F_BIP || d.fmt == F_BEND || d.fmt == F_FILT ||
                                         d.fmt == F_PAN || d.fmt == F_TRANSPO || d.fmt == F_EQ || d.fmt == F_WIDTH || d.fmt == F_TEQ);
            view.knob_lock[k] = (uint8_t)(menu == TB_MENU_SEQ && held >= 0 && lock_page &&
                                          tb_lock_find(tb_proj.pattern, sel, (unsigned)held, d.ref) >= 0);
            view.page_v[k] = (uint8_t)v;
        } else
            view.knob_name[k] = 0, view.knob_value[k][0] = 0, view.knob_pos[k] = 0, view.knob_bip[k] = 0, view.knob_lock[k] = 0,
            view.page_v[k] = 0;
    }
    if (view.hud >= 0) {
        uint32_t age = now - hud_at;
        uint32_t on = hud_title ? 700u : 140u, off = hud_title ? 300u : 90u;   /* (a knob: gone as soon as it rests) */
        view.hud_alpha = (uint8_t)(age < on ? 255u : age < on + off ? (on + off - age) * 255u / off : 0u);
        if (!view.hud_alpha)
            view.hud = -1, hud_title = 0;
    }
    view.hud_name = hud_title;
    snprintf(view.hud_value, sizeof view.hud_value, "%s", hud_buf);
    view.hint = 0;
#if TB_HAS_HINTS
    if (view.mute_layer)
        view.hint = "GLO + whites: mute 1-8, solo 9-16 \xc2\xb7 PLAY: tap";
    else if (prev_btn & BTN(B_EDIT))
        view.hint = "EDIT + white 1-6: KICK BREAK 909 SYNTH SAMPLE ACID";
    else if (prev_btn & BTN(B_SCL))
        view.hint = "SCL + a key: the root";
    else if (menu == TB_MENU_TOOLS)
        view.hint = "SELECT: the tool \xc2\xb7 OCT+: do it \xc2\xb7 OCT-: back";
    else if (menu == TB_MENU_NOTES)
        view.hint = is_drum(sel) ? "1: the hit \xc2\xb7 2: its level \xc2\xb7 3: length \xc2\xb7 4: erase" :
                    "1: the note \xc2\xb7 2: pitch \xc2\xb7 3: length \xc2\xb7 4: erase \xc2\xb7 OCT: octave";
    else if (menu == TB_MENU_SYS)
        view.hint = menu_page[TB_MENU_SYS] == 1u ? "KNOB 1: the place \xc2\xb7 KNOB 2: the letter \xc2\xb7 SAVE: kept"
                                                 : "OCT+ load \xc2\xb7 SAVE saves here \xc2\xb7 OCT- new";
    else if (menu == TB_MENU_SEQ)
        view.hint = held >= 0 && lock_page ? "knob turned = lock on this step \xc2\xb7 SELECT: page"
                  : held >= 0 ? (tb_proj.trk[sel].machine == TB_M_BREAK ? "KNOB 1 or OCT: the slice \xc2\xb7 PRESETS: repeat"
                                 : melodic(sel) || tb_proj.trk[sel].machine == TB_M_KICK ? "KNOB 1 or OCT: the note \xc2\xb7 PRESETS: repeat"
                                                                                         : "knobs: this step \xc2\xb7 PRESETS: repeat")
                              : seq_strip == 1u ? "the step under the cursor \xc2\xb7 SELECT: page"
                              : seq_strip ? "SELECT: back"
                              : melodic(sel) ? "key: a note, the cursor moves on \xc2\xb7 ARP: a rest"
                              : is_drum(sel) ? "whites: the steps \xc2\xb7 blacks: the sound \xc2\xb7 KNOB 2: cursor"
                                             : "whites: the steps \xc2\xb7 KNOB 2: the cursor";
    else if (rec)
        view.hint = tb_engine_playing() ? "REC: what you play is recorded" : "REC ready: PLAY starts recording";
#endif
    view.notes_edit = menu == TB_MENU_NOTES;
    build_seq_view();                                 /* (every page: the footer shows the track's steps) */
    if (menu)
        build_menu_view();
}

/* the board's own look at the controls (the FM-1: its MASTER knob); nothing here */
__attribute__((weak)) void tb_board_controls(const struct fm1_controls *c) { (void)c; }
__attribute__((weak)) unsigned tb_board_audio_load(int reset) { (void)reset; return 0; }
__attribute__((weak)) unsigned tb_board_heap_free(void) { return 0; }

#ifdef TB_TEST
unsigned tb_app_test_page(void) { return (unsigned)menu << 16 | (unsigned)sel << 8 | (unsigned)view.strip_page; }
#endif
void tb_app_init(void)
{
    unsigned k;
    tb_engine_init();
    {
        unsigned slot = 0;
        tb_store_samples_load();
        tb_engine_default_project();                  /* (powered on: a clean project; the saved ones in the MENU) */
        tb_proj.palette = GFX_PAL_DEFAULT;            /* (the yellow, the first time) */
#if !TB_LITE
        {                                             /* (the colour and the light the last saved project had: kept) */
            unsigned last;
            if (tb_store_load_last(&last)) {
                uint8_t pal = tb_proj.palette, pad = tb_proj.pad;
                tb_engine_default_project();
                tb_proj.palette = pal < GFX_NPAL ? pal : GFX_PAL_DEFAULT, tb_proj.pad = (uint8_t)(pad & 3u);
            }
        }
#endif
        proj_slot = 0;
        for (slot = 0; slot < TB_STORE_SLOTS; slot++)  /* (SAVE: into the first empty place) */
            if (!tb_store_info(slot, 0, 0)) {
                proj_slot = (uint8_t)slot;
                break;
            }
    }
    gfx_palette(tb_proj.palette < GFX_NPAL ? tb_proj.palette : 0);
    for (k = 0; k < TB_TRACKS; k++)
        kick_preset_sync(k);
    memset(seq_val, 1, sizeof seq_val);              /* (a note: a 16th) */
    fm1_controls_init();
    fm1_screen_init();
    view.hud = -1;
    for (k = 0; k < 27u; k++)
        key_step[k] = -1;
    boot_at = fm1_millis();
}

/* (the board's hang watch, rfm_main.c: where the tick was when the main loop stopped answering, and on which page)
 * 1 the controls, 2 the keys and knobs, 3 MIDI, 4 the view, 5 the LEDs, 6 the drawing, 7 the screen sent; 0 between */
volatile uint8_t tb_app_stage, tb_app_page;
void tb_app_tick(void)
{
    struct fm1_controls c;
    uint32_t now = fm1_millis();
    tb_app_stage = 1, tb_app_page = (uint8_t)menu;
    fm1_controls_read(&c);
    tb_board_controls(&c);
    {   /* touched: any button, key, encoder, the MASTER knob moved (its noise aside) */
        unsigned e;
        int moved = c.buttons != touch_btn || c.keys != touch_keys || touch_knob < 0 || c.knob > touch_knob + 12 || c.knob + 12 < touch_knob;
        for (e = 0; e < 7u; e++)
            moved |= c.encoders[e] != 0;
        if (moved)
            idle_at = now, touch_btn = c.buttons, touch_keys = c.keys, touch_knob = c.knob;
    }
    if (booting) {
        if (now - boot_at > 2600u || c.buttons || c.keys) {   /* (RAVE FM's bat: a moment; a press skips it) */
            booting = 0;
            prev_btn = c.buttons, prev_keys = c.keys;
        }
    } else {
        tb_app_stage = 2;
        input(&c);
    }
    tb_app_stage = 3;
    midi_tick();
    remote_tick(now);
    {   /* (the screen gives way to the sound: 30 frames a second, fewer when the audio's interrupt is busy) */
        unsigned ld = tb_board_audio_load(0), gap = ld >= 80u ? 66u : ld >= 65u ? 50u : 33u;
        if (now - last_draw < gap) {
            tb_app_stage = 0;
            return;
        }
    }
    last_draw = now;
    if (booting)
        tb_ui_splash(now - boot_at);
    else {
        tb_app_stage = 4, tb_app_page = (uint8_t)menu;
        build_view(now);
        tb_app_stage = 5;
        leds();
        tb_app_stage = 6;
        if (view.playing && !tb_engine_counting() && now - idle_at >= DANCE_IDLE_MS && tb_ui_dance(&view))
            ;                                         /* the first version's dancer, on the beat; a touch: the page back */
        else if (view.perform)
            tb_ui_perform(&view);
        else if (view.mute_layer)
            tb_ui_home(&view);
        else if (menu == TB_MENU_SEQ || menu == TB_MENU_NOTES)
            tb_ui_seq(&view);
        else if (TB_HAS_KICK_PAGE && menu == TB_MENU_EDIT && tb_proj.trk[sel].machine == TB_M_KICK)
            tb_ui_kick(&view);
        else if (menu == TB_MENU_EDIT)
            tb_ui_track(&view);
        else if (menu)
            tb_ui_menu(&view);
        else
            tb_ui_home(&view);
    }
    tb_app_stage = 7;
    gfx_end(emit, 0);
    tb_app_stage = 0;
}
