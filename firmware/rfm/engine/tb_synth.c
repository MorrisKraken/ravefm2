/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the synth (tb_synth.h). */
#include "tb_synth.h"
#include "tb_engine.h"
#include "tb_dsp.h"
#include <string.h>

const char *const TBS_WAVE_NAME[TBS_NWAVE] = {"SAW", "SQUARE", "PWM", "SUPER", "HOOVER", "SINE", "ORGAN", "FM", "BELL",
                                              "SYNC", "VOICE", "RING", "STAB", "M1", "FOLD", "HOOVER 2", "RAVE"};
const char *const TBS_DTYPE_NAME[TBS_ND] = {"SOFT", "HARD", "FOLD", "ASYM"};
const char *const TBS_GATE_NAME[TBS_NG] = {"OFF", "1/8", "1/16", "1/32", "1/16T", "SWING"};
uint32_t tb_syn_step = 3675u;
const char *const TBS_FTYPE_NAME[TBS_NF] = {"LP", "BP", "HP"};
const char *const TBS_MODE_NAME[TBS_NMODE] = {"MONO", "LEGATO", "POLY"};
const char *const TBS_MTYPE_NAME[TBS_NMTYPE] = {"OFF", "SCREECH", "REESE", "WOBBLE", "GROWL"};

/* (v24) no attack under 1 ms (7: 0.97 ms) out of the box - no click as a note starts; 0 still there on the knob */
#define TBS_A1(a) ((a) ? (a) : 7)
/* a sound: its oscillators, filter, voice and the two envelopes (amp A D S R, filter A D S R) */
#define S(n, fam, ch, w, det, sub, gli, cut, res, env, drv, mode, bend, noise, a, d, s_, r, fa, fd, fs, fr) \
    {n, fam, ch, {w, det, sub, gli, cut, res, env, drv, mode, bend, noise, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                  TBS_A1(a), d, s_, r, fa, fd, fs, fr}}
/* .. with the HOOVER and MACRO knobs: MACRO, its type, the LFO's speed, the chorus, the whoop */
#define S2(n, fam, ch, w, det, sub, gli, cut, res, env, drv, mode, bend, noise, a, d, s_, r, fa, fd, fs, fr, mac, mt, spd, cho, who) \
    {n, fam, ch, {w, det, sub, gli, cut, res, env, drv, mode, bend, noise, 64, mac, mt, spd, cho, who, 0, 0, 0, 0, 0, 0, 0, \
                  TBS_A1(a), d, s_, r, fa, fd, fs, fr}}
/* .. and the hardcore stage: DIST, its type, the filter's type, the high-pass, the vibrato */
#define S3(n, fam, ch, w, det, sub, gli, cut, res, env, drv, mode, bend, noise, a, d, s_, r, fa, fd, fs, fr, mac, mt, spd, cho, who, dis, dty, fty, hpf, vib) \
    {n, fam, ch, {w, det, sub, gli, cut, res, env, drv, mode, bend, noise, 64, mac, mt, spd, cho, who, dis, dty, fty, hpf, vib, 0, 0, \
                  TBS_A1(a), d, s_, r, fa, fd, fs, fr}}
/* .. or all 32 values as they are (the sounds chosen by measure: each far from every other) */
#define R(n, fam, ch, ...) {n, fam, ch, __VA_ARGS__}
/* .. or a lead of the first version (the Atelier's RAVE engine, tb_rave.c), its values as they were there:
 * TYPE (SUB), SPRD (DETUNE), CUT, RESO, DISTO (DRIVE), ENV, BEND, COLOR (MACRO), the A D S R, MONO (LEGATO) or POLY,
 * GLIDE; the chorus and reverb sends it had (CHORUS, WHOOP: the reverb, tb_app.c puts it on the track) */
#define RV(n, ty, spr, cut, res, dis, env, bnd, col, a, d, s_, r, mono, gli, fc, fr) \
    {n, "LEAD", 0, {TBS_RAVE, spr, ty, gli, cut, res, env, dis, (mono) ? TBS_LEGATO : TBS_POLY, bnd, 0, 64, col, 0, 0, fc, fr, \
                    0, 0, 0, 0, 0, 0, 0, a, d, s_, r, 0, 0, 0, 0}}
#define K 255
#define M TBS_MONO
#define L TBS_LEGATO
#define P TBS_POLY
const tb_syn_sound_t TBS_SOUND[] = {
    /*  name             family    chord WAVE        DET SUB GLI CUT RES ENV DRV MODE BND NOI  A    D    S    R   FA  FD  FS  FR (S2: MACRO TYPE SPEED CHORUS WHOOP) */
    /* by family, as PRESETS goes through them (the first version's leads at the end of the LEADs) */
    /* BASS */
    S("REESE",         "BASS",   0, TBS_SAW,     48, 40,  0, 46, 22, 20, 55, M, 64,  0,  0,  80, 120,  25,  0, 70, 40, 25),
    S("ACID 303",      "BASS",   0, TBS_SAW,      0,  0, 28, 26,108, 96, 45, L, 64,  0,  0,  70, 100,  12,  0, 36,  0, 12),
    S("ACID SQUARE",    "BASS",   0, TBS_SQUARE,   0,  0, 28, 22,104, 90, 40, L, 64,  0,  0,  70, 100,  12,  0, 34,  0, 12),
    S("SUB SINE",     "BASS",   0, TBS_SINE,     0, 30,  0,100,  0,  0, 30, M, 64,  0,  0,  80, 115,  22,  0,  0,  0,  0),
    S("GABBER BASS",   "BASS",   0, TBS_SQUARE,  10, 90,  0, 58, 30, 50,110, M, 64,  0,  0,  55,  80,  15,  0, 45,  0, 15),
    S("HOOVER BASS",   "BASS",   0, TBS_HOOVER,  60, 70, 30, 60, 20, 30, 60, M, 44,  0,  0,  70, 110,  25,  0, 60,  0, 25),
    S("FM BASS",       "BASS",   0, TBS_FM,      70, 50,  0, 80, 10, 40, 30, M, 64,  0,  0,  60,  90,  15,  0, 40,  0, 15),
    S("DONK",          "BASS",   0, TBS_BELL,    40, 40,  0, 70, 30, 60, 50, M, 64,  0,  0,  40,   0,  14,  0, 30,  0, 14),
    S("PULSE BASS",    "BASS",   0, TBS_PWM,     30, 60,  0, 44, 40, 70, 45, M, 64,  0,  0,  50,  70,  15,  0, 38,  0, 15),
    S2("MOVING REESE",   "BASS",  0, TBS_SAW,     50, 50,  0, 44, 30, 20, 60, M, 64,  0,  0,  80, 120,  25,  0, 70, 40, 25,  70, 2,  0,  0,  0),
    S2("WOBBLE",       "BASS",  0, TBS_SAW,     30, 60,  0, 40, 60, 10, 70, M, 64,  0,  0,  80, 120,  25,  0, 70, 40, 25,  90, 3,  0,  0,  0),
    /* REESE */
    S2("GROWL",        "REESE",  0, TBS_FM,      40, 40,  0, 60, 30, 30, 90, M, 64,  0,  0,  80, 120,  25,  0, 70, 40, 25,  70, 4,  0,  0,  0),
    /* LEAD */
    S("SCREAMER",      "LEAD",   0, TBS_PWM,     40,  0, 20, 70, 70, 50,105, L, 64,  0,  0,  70, 110,  30,  0, 60, 60, 30),
    S("TRANCE LEAD",   "LEAD",   0, TBS_SUPER,   85,  0, 10, 96, 20, 25, 25, L, 64,  0,  2,  80, 110,  50,  0, 60, 50, 50),
    S("SINE LEAD",     "LEAD",   0, TBS_SINE,     0,  0, 45,110,  0,  0, 40, L, 64,  0,  0,  80, 120,  40,  0,  0,  0,  0),
    S("CHIP",          "LEAD",   0, TBS_SQUARE,   0,  0,  0,120,  0,  0,  0, M, 64,  0,  0,  60, 110,  10,  0,  0,  0,  0),
    S("ACID LEAD",     "LEAD",   0, TBS_SAW,     10,  0, 30, 40,110, 90, 70, L, 64,  0,  0,  60,  90,  20,  0, 40, 20, 20),
    S("FM LEAD",       "LEAD",   0, TBS_FM,      90,  0, 15, 90, 10, 30, 30, L, 64,  0,  0,  70, 110,  35,  0, 50, 30, 35),
    S2("MACRO SCREECH",   "LEAD",0, TBS_SAW,     20,  0, 30, 60, 90, 40, 90, L, 64,  0,  0,  70, 110,  30,  0, 50, 30, 30,  70, 1,  0,  0,  0),
    S2("SQUARE SCREAM","LEAD",0, TBS_SQUARE,  15,  0, 25, 56, 96, 50,100, L, 64,  0,  0,  70, 110,  30,  0, 50, 30, 30,  80, 1,  0,  0,  0),
    S("UPTEMPO RING",  "LEAD",   0, TBS_RING,    70,  0, 22, 96, 30, 30,105, L, 64,  0,  0,  70, 110,  25,  0, 50, 50, 25),
    S("RAW LEAD",      "LEAD",   0, TBS_SUPER,   70,  0, 25, 85, 25, 30,127, L, 64,  0,  0,  80, 115,  30,  0, 60, 60, 30),
    S("GABBER ORGAN",  "LEAD",   0, TBS_M1,     100,  0,  0,105, 10, 30,120, P, 64,  0,  0,  60, 110,  20,  0, 40, 30, 20),
    S("FOLD LEAD",     "LEAD",   0, TBS_FOLD,    60,  0, 15,100, 15, 70, 60, L, 64,  0,  0,  70, 100,  28,  0, 55, 40, 28),
    S("TERROR BEEP",    "LEAD",   0, TBS_SQUARE,   0,  0,  0,110, 10,  0,127, M, 64,  0,  0,  40,  60,  10,  0,  0,  0,  0),
    S3("HOWLING",       "LEAD",   0, TBS_SYNC, 25,  0, 18, 85, 85, 35, 40, L, 64,  0,   0, 70,110, 30,   0, 62, 40, 30,   0,0,  0,  0,  0, 90,TBS_D_HARD,TBS_F_BP, 70, 20),
    S3("RAW SCREECH",    "LEAD",   0, TBS_SAW,      0,  0, 25, 72,108, 25, 40, L, 60,  0,   0, 70,115, 25,   0, 60, 40, 25,   0,0,  0,  0,  0, 105,TBS_D_HARD,TBS_F_BP, 70, 25),
    S3("UPTEMPO SCREECH", "LEAD",   0, TBS_SAW,     20,  0, 15, 82,118, 45, 40, L, 46,  0,   0, 70,115, 22,   0, 40, 30, 22,   0,0,  0,  0,  0, 127,TBS_D_FOLD,TBS_F_BP, 75, 70),
    S3("FRENCH SCREECH", "LEAD",   0, TBS_SAW,     10,  0, 30, 75, 105, 30, 30, L, 64,  0,   0, 70,115, 25,   0, 60, 40, 25, 25,1,  0,  0,  0, 80,TBS_D_SOFT,TBS_F_BP, 65,  0),
    S3("TERROR SCREAM",     "LEAD",   0, TBS_SQUARE,   0,  0, 10, 85, 90, 40, 40, M, 64,  0,   0, 60,100, 15,   0, 40, 20, 15,   0,0,  0,  0,  0, 127,TBS_D_HARD,TBS_F_BP, 80, 40),
    S3("RAW SUPERSAW",   "LEAD",   0, TBS_SUPER,   30,  0, 20, 92, 20, 20, 30, L, 64,  0,   0, 75,115, 28,   0, 60, 60, 28,   0,0,  0,  0,  0,  85,TBS_D_ASYM,TBS_F_LP, 60,  0),
    S3("GABBER HOOVER",  "LEAD",   0, TBS_HOOV2,   40, 70, 30,100, 15, 15, 40, M, 64,  0,   2, 80,115, 35,   0, 60, 40, 35,   0,0,  0, 90, 50,  95,TBS_D_SOFT,TBS_F_LP, 40,  0),
    S3("INDUS RING",     "LEAD",   0, TBS_RING, 20,  0, 10, 88, 60, 40, 40, L, 64,  0,   0, 70,110, 20,   0, 50, 30, 20,   0,0,  0,  0,  0, 95,TBS_D_FOLD,TBS_F_BP, 70, 20),
    S3("ACID GABBER",    "LEAD",   0, TBS_SAW,      0,  0, 28, 30,115,100, 40, L, 64,  0,   0, 60, 90, 12,   0, 34,  0, 12,   0,0,  0,  0,  0, 120,TBS_D_HARD,TBS_F_LP, 55,  0),
    R("GROWLING FM",       "LEAD",   0, {7, 28, 0, 0, 91, 106, 46, 43, 1, 64, 0, 64, 77, 4, 0, 0, 0, 62, 0, 0, 0, 0, 0, 0, 8, 56, 100, 33, 4, 21, 83, 33}),
    R("CHOPPED SUPER",     "LEAD",   0, {3, 66, 0, 13, 100, 93, 41, 48, 1, 64, 0, 64, 0, 0, 72, 0, 0, 0, 0, 0, 0, 0, 2, 0, 10, 67, 105, 17, 10, 43, 69, 17}),
    R("PWM HOWLER",     "LEAD",   0, {2, 44, 0, 14, 100, 83, 0, 49, 0, 64, 0, 64, 0, 0, 0, 0, 0, 98, 1, 1, 32, 0, 0, 0, 9, 58, 108, 18, 4, 80, 10, 18}),
    R("NASAL FOLD",      "LEAD",   0, {14, 81, 0, 0, 60, 67, 60, 43, 0, 64, 0, 64, 0, 0, 0, 0, 0, 0, 0, 1, 73, 0, 0, 0, 7, 74, 99, 20, 8, 37, 74, 20}),
    R("OPENING SYNC",    "LEAD",   0, {9, 95, 0, 31, 76, 7, 85, 42, 0, 64, 15, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 60, 116, 24, 1, 43, 3, 24}),
    R("BREATHY SINE",   "LEAD",   0, {5, 0, 0, 36, 82, 91, 61, 31, 0, 64, 21, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 70, 29, 27, 5, 80, 67, 27}),
    R("SHRILL FM",       "LEAD",   0, {7, 81, 0, 13, 70, 111, 0, 18, 0, 64, 0, 64, 0, 0, 59, 0, 0, 86, 1, 1, 36, 0, 0, 0, 7, 62, 109, 18, 9, 24, 7, 18}),
    R("M1 SCREAM",          "LEAD",   0, {13, 75, 0, 0, 63, 117, 80, 25, 0, 37, 0, 64, 0, 0, 59, 0, 0, 67, 0, 1, 0, 20, 0, 28, 7, 69, 8, 13, 1, 21, 87, 13}),
    R("SQUARE WOBBLE",    "LEAD",   0, {1, 76, 37, 18, 90, 60, 0, 35, 0, 64, 0, 64, 97, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 63, 103, 21, 6, 56, 72, 21}),
    R("M1 GLIDE",       "LEAD",   0, {13, 39, 0, 55, 88, 18, 53, 13, 1, 64, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 84, 0, 0, 0, 7, 65, 15, 30, 4, 35, 15, 30}),
    R("DULL HOOVER",    "LEAD",   0, {15, 41, 0, 0, 45, 76, 0, 14, 0, 64, 14, 64, 0, 0, 0, 104, 38, 0, 0, 0, 25, 0, 0, 0, 4, 62, 94, 22, 4, 43, 51, 22}),
    R("SATURATED SYNC",     "LEAD",   0, {9, 87, 0, 0, 77, 98, 55, 11, 1, 64, 0, 64, 0, 0, 0, 0, 0, 104, 1, 1, 0, 0, 0, 0, 3, 56, 111, 33, 6, 34, 12, 33}),
    RV("ANGER",        TBR_ANGER, 64, 64, 64, 100, 0, 0, 20,   0, 80, 115, 40, 1, 9, 16, 60),
    RV("ANGER V1",     TBR_V1,    64, 64, 64, 64, 0, 0, 7,     0, 80, 115, 40, 1, 9, 0, 60),
    RV("SUPA DUPA",    TBR_SUPA,  64, 64, 64, 84, 0, 0, 64,    0, 80, 115, 50, 0, 0, 50, 70),
    RV("ANGER OCT",    TBR_ANGER, 56, 64, 64, 104, 0, 0, 64,   0, 80, 115, 40, 1, 9, 16, 70),
    RV("RAISE V1",     TBR_V1,    72, 70, 70, 72, 0, 0, 12,    0, 80, 115, 44, 1, 9, 0, 100),
    RV("SLIDE V1",     TBR_V1,    64, 64, 64, 64, 0, 0, 7,     0, 80, 115, 40, 1, 22, 0, 90),
    RV("GLISSE",       TBR_ANGER, 64, 64, 64, 100, 0, 0, 20,   0, 80, 115, 40, 1, 24, 16, 70),
    RV("ANGER OCT V1", TBR_V1,    42, 62, 83, 116, 0, 0, 36,   0, 80, 115, 40, 1, 9, 0, 115),
    RV("POLY ANGER",   TBR_ANGER, 70, 64, 64, 92, 0, 0, 16,    0, 86, 115, 60, 0, 0, 24, 80),
    RV("SALE V1",      TBR_V1,    56, 62, 76, 104, 0, 0, 24,   0, 80, 112, 36, 1, 9, 0, 85),
    RV("RAISE",        TBR_ANGER, 72, 70, 70, 110, 0, 0, 24,   0, 80, 115, 44, 1, 9, 20, 70),
    RV("LARGE V1",     TBR_V1,    100, 60, 64, 70, 0, 0, 16,   0, 82, 115, 48, 1, 10, 30, 90),
    RV("ATTAQUE",      TBR_ANGER, 64, 64, 64, 100, 0, 127, 20, 0, 80, 115, 40, 1, 9, 16, 70),
    RV("HAUT V1",      TBR_V1,    48, 70, 84, 108, 0, 0, 100,  0, 80, 115, 40, 1, 9, 0, 90),
    /* STAB */
    S("RAVE STAB",     "STAB",   1, TBS_SAW,     35,  0,  0, 62, 55, 75, 25, P, 64,  6,  0,  38,   0,  28,  0, 32,  0, 28),
    S("ORGAN STAB",    "STAB",   1, TBS_ORGAN,   70,  0,  0, 90, 10, 30, 30, P, 64,  0,  0,  45,   0,  25,  0, 30,  0, 25),
    S("DARK STAB",     "STAB",   1, TBS_SQUARE,  30,  0,  0, 45, 60, 70, 50, P, 64,  4,  0,  42,   0,  30,  0, 36,  0, 30),
    S("MINOR 7",      "STAB",   6, TBS_SUPER,   40,  0,  0, 70, 40, 60, 30, P, 64,  0,  0,  50,   0,  35,  0, 40,  0, 35),
    S("PIANO HOUSE",   "STAB",   1, TBS_BELL,    25,  0,  0, 85, 10, 40, 20, P, 64,  0,  0,  70,   0,  40,  0, 50,  0, 40),
    S("ANESTHESIA",    "STAB",   4, TBS_STAB,    50,  0,  0, 70, 30, 55, 55, P, 58,  0,  0,  62,  90,  30,  0, 45, 30, 30),
    S("VAMP",          "STAB",   4, TBS_STAB,    25,  0,  0, 95, 15, 30, 30, P, 64,  0,  0,  40,   0,  22,  0, 30,  0, 22),
    S("RIOT",        "STAB",   6, TBS_M1,      70,  0,  0,110,  0, 70, 40, P, 64,  0,  0,  50,  30,  25,  0, 35,  0, 25),
    S("CITY",          "STAB",   5, TBS_M1,      40,  0,  0,110,  0, 90, 20, P, 64,  0,  0,  58,  20,  30,  0, 40,  0, 30),
    S("GABBER STAB",   "STAB",   4, TBS_SAW,     40,  0,  0, 72, 35, 50,127, P, 64,  0,  0,  42,   0,  22,  0, 34,  0, 22),
    S("M1 HOUSE",      "STAB",   6, TBS_M1,      90,  0,  0,110,  0, 60, 25, P, 64,  0,  0,  55,  20,  30,  0, 38,  0, 30),
    S3("SQUARE STAB",     "STAB",   4, TBS_SQUARE,  10,  0,  0, 95, 20, 30, 40, P, 64,  0,   0, 26,  0, 12,   0, 20,  0, 12,   0,0,  0,  0,  0, 110,TBS_D_HARD,TBS_F_LP, 80,  0),
    S3("ROTTERDAM STAB", "STAB",   4, TBS_SAW,     30,  0,  0, 90, 30, 40, 40, P, 64,  0,   0, 32,  0, 15,   0, 25,  0, 15,   0,0,  0,  0,  0, 127,TBS_D_SOFT,TBS_F_LP, 70,  0),
    S3("INDUS STAB",     "STAB",   4, TBS_STAB,    40,  0,  0, 85, 40, 40, 40, P, 64,  0,   0, 24,  0, 12,   0, 20,  0, 12,   0,0,  0,  0,  0, 100,TBS_D_FOLD,TBS_F_BP, 75,  0),
    S3("TERROR STAB",    "STAB",   4, TBS_SQUARE,  40,  0,  0, 90, 50, 40, 40, P, 64,  0,   0, 18,  0, 10,   0, 15,  0, 10,   0,0,  0,  0,  0, 127,TBS_D_HARD,TBS_F_BP, 85,  0),
    S3("HOOVER STAB",    "STAB",   4, TBS_HOOV2,   40,  0,  0,100, 10, 20, 40, P, 58,  0,   0, 40, 10, 25,   0, 35,  0, 25,   0,0,  0,  0, 40,  70,TBS_D_SOFT,TBS_F_LP, 50,  0),
    S3("DARK CHORD",  "STAB",   4, TBS_STAB,    60,  0,  0, 45, 60, 40, 40, P, 64,  0,   0, 70, 80, 40,   0, 60, 30, 40,   0,0,  0,  0,  0,  70,TBS_D_ASYM,TBS_F_LP, 60,  0),
    R("DULL FOLD",      "STAB",   4, {14, 24, 0, 0, 45, 12, 0, 48, 2, 98, 0, 64, 0, 0, 31, 0, 0, 0, 0, 0, 42, 0, 0, 0, 7, 49, 1, 21, 10, 76, 81, 21}),
    R("BRIGHT SYNC",   "STAB",   5, {9, 92, 67, 0, 100, 84, 12, 28, 2, 64, 0, 64, 0, 0, 0, 0, 0, 0, 0, 1, 20, 0, 0, 0, 7, 21, 18, 21, 7, 27, 24, 21}),
    R("CHOPPED SYNC",      "STAB",   1, {9, 53, 0, 0, 74, 87, 0, 45, 2, 64, 0, 64, 0, 0, 53, 0, 0, 76, 1, 1, 67, 49, 1, 0, 7, 23, 14, 14, 1, 33, 81, 14}),
    R("DIVING STAB",     "STAB",   6, {1, 83, 40, 0, 91, 96, 8, 23, 2, 88, 0, 64, 0, 0, 0, 0, 0, 0, 0, 1, 82, 0, 0, 91, 7, 51, 0, 35, 0, 60, 25, 35}),
    R("BELL CHORD",   "STAB",   6, {8, 73, 0, 0, 76, 80, 0, 50, 2, 64, 0, 64, 0, 0, 0, 0, 0, 0, 0, 1, 47, 0, 0, 0, 7, 28, 4, 10, 2, 23, 68, 10}),
    R("SUPER WOBBLE",    "STAB",   4, {3, 33, 0, 0, 72, 68, 49, 22, 2, 64, 0, 64, 82, 3, 20, 0, 0, 0, 0, 1, 0, 0, 0, 0, 7, 41, 29, 21, 2, 41, 86, 21}),
    R("SQUARE SCREECH",   "STAB",   1, {1, 50, 0, 0, 55, 75, 90, 18, 2, 64, 0, 64, 70, 1, 88, 0, 0, 0, 0, 0, 70, 0, 0, 0, 7, 50, 10, 31, 1, 25, 65, 31}),
    R("LOW SYNC",      "STAB",   5, {9, 76, 91, 0, 69, 62, 0, 40, 2, 64, 0, 64, 0, 0, 83, 0, 0, 75, 1, 1, 0, 15, 0, 0, 7, 47, 9, 15, 9, 72, 56, 15}),
    /* HOOVER */
    S("HOOVER",        "HOOVER", 0, TBS_HOOVER,  70, 80, 45, 96, 15, 10, 45, M, 40,  0,  4,  80, 110,  42,  0, 60,  0, 42),
    S("DARK HOOVER",   "HOOVER", 0, TBS_HOOVER,  80, 90, 50, 60, 25, 10, 60, M, 44,  0,  4,  80, 110,  45,  0, 60,  0, 45),
    S3("MENTASM",        "HOOVER", 0, TBS_HOOV2,   40, 90, 20,110, 10, 10, 30, M, 64,  0,   2, 80,118, 40,   0, 60, 60, 40,   0,0,  0,110, 70,   0,TBS_D_SOFT,TBS_F_LP,  0,  0),
    S3("DOMINATOR",     "HOOVER", 0, TBS_HOOV2,   60,110, 40, 95, 15, 15, 50, M, 64,  0,   2, 80,118, 45,   0, 60, 40, 45,   0,0,  0,100, 90,  45,TBS_D_SOFT,TBS_F_LP,  0,  0),
    S3("DIST HOOVER",  "HOOVER", 0, TBS_HOOV2,   50, 90, 30,105, 10, 10, 40, M, 64,  0,   2, 80,118, 40,   0, 60, 40, 40,   0,0,  0, 90, 60, 115,TBS_D_HARD,TBS_F_LP, 45,  0),
    S3("BELGIAN HOOVER",   "HOOVER", 0, TBS_HOOV2,   70,127, 25, 80, 20, 20, 30, M, 64,  0,   4, 80,118, 50,   0, 70, 50, 50,   0,0, 30,100, 40,   0,TBS_D_SOFT,TBS_F_LP,  0,  0),
    S3("ACID HOOVER",   "HOOVER", 0, TBS_HOOV2,   50, 60, 40, 60, 80, 60, 40, M, 64,  0,   4, 80,110, 42,   0, 55, 30, 42,   0,0,  0, 90, 50,  50,TBS_D_SOFT,TBS_F_LP, 30,  0),
    R("WHISTLE HOOVER", "HOOVER", 0, {15, 6, 0, 0, 95, 113, 20, 3, 0, 43, 23, 64, 0, 0, 0, 64, 64, 0, 0, 1, 40, 64, 0, 25, 7, 89, 118, 35, 9, 61, 34, 35}),
    R("PWM HOOVER",      "HOOVER", 0, {2, 21, 85, 39, 78, 100, 39, 13, 0, 64, 0, 64, 0, 0, 78, 0, 50, 62, 3, 1, 60, 78, 0, 0, 7, 71, 120, 42, 7, 28, 42, 42}),
    R("HOLLOW HOOVER",    "HOOVER", 0, {4, 36, 104, 45, 89, 104, 41, 15, 1, 64, 0, 64, 0, 0, 0, 58, 72, 0, 0, 1, 0, 16, 0, 0, 7, 71, 109, 52, 8, 42, 23, 52}),
    R("HARD HOOVER",      "HOOVER", 0, {15, 45, 0, 0, 62, 56, 60, 20, 1, 64, 0, 64, 0, 0, 52, 77, 40, 40, 1, 0, 0, 0, 0, 0, 5, 85, 112, 45, 1, 36, 41, 45}),
    /* PAD */
    S("BRASS",         "PAD",   1, TBS_SAW,     25,  0,  0, 50, 30, 80, 30, P, 64,  0, 14,  50,  30,  30, 10, 48, 20, 30),
    S("DARK PAD",    "PAD",    1, TBS_SUPER,   60,  0,  0, 55, 15, 20, 10, P, 64,  0, 85,  90, 110,  95, 90, 90, 80, 95),
    S("PWM PAD",     "PAD",    1, TBS_PWM,     50,  0,  0, 70, 20, 15, 10, P, 64,  0, 80,  90, 115,  95, 80, 90, 90, 95),
    S("CHOIR",        "PAD",    1, TBS_SAW,     70,  0,  0, 48, 50, 10, 10, P, 64, 10, 75,  90, 110,  90, 70, 90, 90, 90),
    /* PLUCK */
    S("PLUCK",         "PLUCK",  0, TBS_SAW,     20,  0,  0, 50, 40, 80, 20, P, 64,  0,  0,  35,   0,  25,  0, 28,  0, 25),
    S("KALIMBA",       "PLUCK",  0, TBS_BELL,    50,  0,  0,100,  0,  0, 10, P, 64,  0,  0,  45,   0,  40,  0,  0,  0,  0),
    S("BELL",        "PLUCK",  0, TBS_BELL,    80,  0,  0,110,  0,  0, 10, P, 64,  0,  0,  85,   0,  70,  0,  0,  0,  0),
    S("MARIMBA",       "PLUCK",  0, TBS_SINE,     0, 20,  0,110,  0,  0, 10, P, 64,  0,  0,  40,   0,  30,  0,  0,  0,  0),
    S("ALARM",        "PLUCK",     0, TBS_SQUARE,   0,  0, 60, 90, 30, 20, 50, L,100,  0,  0,  60, 120,  30,  0,  0,  0,  0),
    /* ORGAN */
    S("SOFT ORGAN",    "ORGAN",  0, TBS_ORGAN,   30,  0,  0,100,  0,  0, 10, P, 64,  0,  4,  60, 120,  30,  0,  0,  0,  0),
    /* FX */
    S("SIREN",        "FX",     0, TBS_SINE,     0,  0,120,110,  0,  0, 40, L, 10,  0, 20,  60, 120,  60,  0,  0,  0,  0),
    S("LASER",         "FX",     0, TBS_SAW,     20,  0,  0, 90, 60, 90, 60, M,127,  0,  0,  50,   0,  20,  0, 30,  0, 20),
    S("NOISE SWEEP",   "FX",     0, TBS_SINE,     0,  0,  0, 20, 90,110, 30, M, 64,127, 40, 100, 110,  60, 90,100, 40, 60),
};
#undef RV
#undef S
#undef S2
#undef S3
#undef R
#undef K
#undef M
#undef L
#undef P
const unsigned TBS_NSOUND = sizeof TBS_SOUND / sizeof *TBS_SOUND;
unsigned tb_syn_default(unsigned slot)              /* (by name: the library's order may change) */
{
    static const char *const D[4] = {"REESE", "SCREAMER", "RAVE STAB", "HOOVER"};
    unsigned i;
    for (i = 0; i < TBS_NSOUND; i++)
        if (!strcmp(TBS_SOUND[i].name, D[slot & 3u]))
            return i;
    return 0;
}

#define INC_A4 42852281u          /* 440 Hz: 440 x 2^32 / 44100 */
#define SUBBLK 32u                /* the pitch, the filter and the glide move every 32 samples (0.7 ms) */

void tb_syn_init(tb_syn_t *s) { memset(s, 0, sizeof *s); }

static void release(tb_syn_voice_t *v) { v->gate = 0, tb_env_off(&v->amp), tb_env_off(&v->flt); }

/* the WAVE RAVE (tb_rave.c): the voices made the RAVE's (or back), cleared, when a sound of the other kind plays */
#define IS_RAVE(p) ((p)[TBS_WAVE] == TBS_RAVE)
static void rave_mode(tb_syn_t *s, const uint8_t *p)
{
    uint8_t m = IS_RAVE(p) ? 1u : 0u;
    if (s->rmode != m) {
        if (m)
            memset(&s->rave, 0, sizeof s->rave);
        else
            memset(s->v, 0, sizeof s->v);
        s->rmode = m;
    }
}

void tb_syn_all_off(tb_syn_t *s)
{
    unsigned i;
    s->nheld = 0;
    if (s->rmode) {
        tb_rave_all_off(&s->rave);
        return;
    }
    for (i = 0; i < TBS_VOICES; i++)
        release(&s->v[i]);
    s->nheld = 0;
}

static void start(tb_syn_voice_t *v, const uint8_t *p, int note, unsigned key, unsigned vel, int glide, int retrig, uint32_t age)
{
    int32_t t = note << 16;
    if (!v->live || !glide)
        v->pitch = t;
    v->target = t;
    v->shed = 0;
    if (!v->live) {
        unsigned k;
        for (k = 0; k < TBS_OSC; k++)                /* free-running from a spread start: no phasey first cycle */
            v->ph[k] = k * 0x33333333u;
        v->ic1 = v->ic2 = 0;
        v->f1lo = v->f1bd = v->f2lo = v->f2bd = v->dc = v->hp = v->tame = v->dprev = v->t4 = 0;
        v->amp.lvl = v->flt.lvl = 0;
    }
    if (retrig || !v->live) {
        tb_env_on(&v->amp);
        tb_env_on(&v->flt);
        v->bend = ((int32_t)p[TBS_BEND] - 64) * (p[TBS_PTIME] ? 24 : 12) * 65536 / 64;
        v->bendk = p[TBS_PTIME] ? (int32_t)(65536 - (int32_t)(((uint64_t)47554u << 16) / (10ull * tb_exp2((int32_t)p[TBS_PTIME] * 65536 / 16)))) : 0;
        v->gpos = 0, v->glvl = 32767;
        v->wh1 = v->wh2 = (int32_t)p[TBS_WHOOP] * (p[TBS_WAVE] == TBS_HOOV2 ? 7 : 5) * 65536 / 127;   /* up to 5 (7) semitones over, then back */
    }
    v->note = (uint8_t)note;
    v->key = (uint8_t)key;
    v->gate = 1;
    v->live = 1;
    v->vel = (uint16_t)(vel > 127u ? 32767u : vel * 258u);
    v->age = age;
}

static void play_mono(tb_syn_t *s, const uint8_t *p, unsigned key, unsigned vel, const int8_t *iv, unsigned n, int legato_from)
{
    unsigned i;
    for (i = 0; i < TBS_VOICES; i++) {
        tb_syn_voice_t *v = &s->v[i];
        if (i < n)
            start(v, p, (int)key + iv[i], key, vel, p[TBS_GLIDE] && (legato_from || p[TBS_MODE] == TBS_MONO),
                  !(legato_from && p[TBS_MODE] == TBS_LEGATO), ++s->age);
        else if (v->live && v->gate)
            release(v);
    }
}

void tb_syn_on(tb_syn_t *s, const uint8_t *p, unsigned note, unsigned vel, const int8_t *iv, unsigned n)
{
    unsigned i, k;
    int legato = s->nheld > 0;
    static const int8_t ONE[1] = {0};
    if (!n || !iv)
        iv = ONE, n = 1;
    if (n > 4u)
        n = 4u;
    for (i = 0; i < s->nheld && s->held[i] != note; i++)
        ;
    if (i == s->nheld && s->nheld < 8u)
        s->nheld++;
    if (i >= 8u) {                                     /* (21 Nov 2026: a ninth key down: the oldest forgotten - it
                                                        *  was written past the list, over its count) */
        memmove(s->held, s->held + 1, 7), memmove(s->niv, s->niv + 1, 7), memmove(s->iv, s->iv + 1, sizeof s->iv[0] * 7u);
        i = 7u;
    }
    s->held[i] = (uint8_t)note, s->niv[i] = (uint8_t)n;
    for (k = 0; k < n; k++)
        s->iv[i][k] = iv[k];
    rave_mode(s, p);
    if (s->rmode) {                                    /* RAVE: a mono sound its root, a poly one the chord */
        for (k = 0; k < (p[TBS_MODE] == TBS_POLY ? n : 1u); k++)
            tb_rave_on(&s->rave, p, (unsigned)((int)note + iv[k]), vel);
        return;
    }
    if (p[TBS_MODE] != TBS_POLY) {
        play_mono(s, p, note, vel, iv, n, legato);
        return;
    }
    for (k = 0; k < n; k++) {                          /* a free voice, else the one released longest, else the oldest */
        unsigned best = 0;
        for (i = 1; i < TBS_VOICES; i++) {
            const tb_syn_voice_t *a = &s->v[i], *b = &s->v[best];
            int sa = !a->live ? 0 : !a->gate ? 1 : 2, sb = !b->live ? 0 : !b->gate ? 1 : 2;
            if (sa < sb || (sa == sb && a->age < b->age))
                best = i;
        }
        start(&s->v[best], p, (int)note + iv[k], note, vel, 0, 1, ++s->age);
    }
}

void tb_syn_off(tb_syn_t *s, const uint8_t *p, unsigned note)
{
    unsigned i, j;
    rave_mode(s, p);
    if (s->rmode) {                                    /* RAVE: the key's notes let go */
        for (i = 0; i < s->nheld && s->held[i] != note; i++)
            ;
        if (i < s->nheld)
            for (j = 0; j < s->niv[i]; j++)
                tb_rave_off(&s->rave, p, (unsigned)((int)note + s->iv[i][j]));
        else
            tb_rave_off(&s->rave, p, note);
    }
    for (i = j = 0; i < s->nheld; i++)
        if (s->held[i] != note) {
            s->held[j] = s->held[i], s->niv[j] = s->niv[i];
            memcpy(s->iv[j], s->iv[i], 4);
            j++;
        }
    s->nheld = (uint8_t)j;
    if (s->rmode)
        return;
    if (p[TBS_MODE] != TBS_POLY) {
        if (s->nheld) {                                /* back to the key still down */
            unsigned h = s->nheld - 1u;
            if (s->v[0].key == note)
                play_mono(s, p, s->held[h], s->v[0].vel / 258u, s->iv[h], s->niv[h], 1);
            return;
        }
        for (i = 0; i < TBS_VOICES; i++)
            if (s->v[i].gate)
                release(&s->v[i]);
        return;
    }
    for (i = 0; i < TBS_VOICES; i++)
        if (s->v[i].gate && s->v[i].key == note)
            release(&s->v[i]);
}

int32_t tb_syn_env(const tb_syn_t *s, int filter, unsigned *stage)
{
    unsigned i, best = TBS_VOICES;
    if (s->rmode) {                                    /* RAVE: its envelope (Q24 -> Q30), no filter envelope */
        for (i = 0; i < TBR_VOICES; i++)
            if (s->rave.v[i].active && (best == TBS_VOICES || s->rave.v[i].age > s->rave.v[best].age))
                best = i;
        *stage = best == TBS_VOICES || filter ? 0u : s->rave.v[best].stage;
        return best == TBS_VOICES || filter ? 0 : s->rave.v[best].env << 6;
    }
    for (i = 0; i < TBS_VOICES; i++)
        if (s->v[i].live && (best == TBS_VOICES || s->v[i].age > s->v[best].age))
            best = i;
    if (best == TBS_VOICES) {
        *stage = 0;
        return 0;
    }
    *stage = filter ? s->v[best].flt.stage : s->v[best].amp.stage;
    return filter ? s->v[best].flt.lvl : s->v[best].amp.lvl;
}

/* ------------------------------------------------------------- oscillators --- */
/* the polyBLEP: the step's residue around phase 0 (t: the phase, dt: its increment), Q15 */
/* (21 Nov 2026) floor(t 2^15 / dt) for t < dt, as the 64-bit division gives it, in 32 bits (the FM-1 has no 64-bit
 * division of its own): the hardware's when t 2^15 fits, else 15 steps of a long division, the remainder kept
 * under dt (its doubling past 2^32 is past dt: the subtraction, its low word right) */
static inline uint32_t q15div(uint32_t t, uint32_t dt)
{
    uint32_t q = 0, r = t;
    int b;
    if (dt < (1u << 17))
        return (t << 15) / dt;
    for (b = 0; b < 15; b++) {
        uint32_t c = r >> 31;
        r <<= 1, q <<= 1;
        if (c || r >= dt)
            r -= dt, q |= 1u;
    }
    return q;
}
static inline int32_t blep(uint32_t t, uint32_t dt)
{
    if (!dt)                                          /* (no movement: no step to smooth; never a division by 0) */
        return 0;
    if (t < dt) {
        int32_t x = 32768 - (int32_t)q15div(t, dt);
        return -((x * x) >> 15);
    }
    if (t > 0u - dt) {
        int32_t x = (int32_t)q15div(t - (0u - dt), dt);
        return (x * x) >> 15;
    }
    return 0;
}
static inline int32_t saw(uint32_t ph, uint32_t dt) { return (int32_t)(ph >> 16) - 32768 - blep(ph, dt); }
/* a pulse of width w (of 2^32): two saws apart */
static inline int32_t pulse(uint32_t ph, uint32_t dt, uint32_t w)
{
    return (saw(ph, dt) - saw(ph + w, dt)) >> 1;     /* (zero mean whatever the width) */
}

/* a one-pole's coefficient for f Hz (Q15): about 2 pi f / fs, held under 1 */
static int32_t k_pole1(uint32_t hz)
{
    uint32_t a = hz * 4669u >> 10;                    /* 2 pi / 44100 x 32768 = 4.669 */
    return (int32_t)(a > 30000u ? 30000u : a);
}
static uint32_t inc_of(int32_t semi16)            /* a pitch (semitones Q16, MIDI) -> a phase step */
{
    int32_t x = (semi16 - (69 << 16)) / 12;
    if (x > 6 << 16)
        x = 6 << 16;
    return (uint32_t)(((uint64_t)INC_A4 * tb_exp2(x)) >> 16);
}

/* (21 Nov 2026, the light engine) an envelope's 8 one-sample steps at once: k8 the 8-sample coefficients (attack,
 * decay, release: 1 - (1-k)^8, tb_syn_begin), the stages' ends checked once a step */
static int32_t k8_of(int32_t k)
{
    uint64_t r = (uint64_t)((1u << 30) - (uint32_t)k);
    r = (r * r) >> 30, r = (r * r) >> 30, r = (r * r) >> 30;
    return (int32_t)((1u << 30) - (uint32_t)r);
}
static void env8(tb_env_t *e, const tb_envk_t *k, const int32_t *k8)
{
    switch (e->stage) {
    case 1:
        e->lvl += (int32_t)(((int64_t)((1 << 30) + (1 << 28) - e->lvl) * k8[0]) >> 30);
        if (e->lvl >= 1 << 30)
            e->lvl = 1 << 30, e->stage = 2;
        break;
    case 2:
        e->lvl += (int32_t)(((int64_t)(k->sus - e->lvl) * k8[1]) >> 30);
        break;
    case 3:
        e->lvl -= (int32_t)(((int64_t)e->lvl * (k->sus == 0 && k->kd > k->kr ? k8[1] : k8[2])) >> 30);
        if (e->lvl < 1 << 12)
            e->lvl = 0, e->stage = 0;
        break;
    default:
        break;
    }
}

/* (21 Nov 2026) a block in two: what the voices share worked out once (tb_syn_begin: the LFOs moved, the shedding,
 * the coefficients, into c), then the voices, any of them (tb_syn_voices, vmask: which): the two cores can each take
 * some of one synth's voices - each voice's state its own, what they share only read; tb_syn_render both */
int tb_syn_begin(tb_syn_t *s, const uint8_t *p, unsigned n, int32_t lfo_pitch, int32_t lfo_cut, tb_syn_ctx_t *c)
{
    if (s->rmode != (IS_RAVE(p) ? 1u : 0u)) {          /* (another kind of sound: what played is let go, cleared) */
        rave_mode(s, p);
        s->nheld = 0;
    }
    c->p = p, c->n = n, c->lfo_pitch = lfo_pitch, c->lfo_cut = lfo_cut, c->rave = s->rmode;
    if (s->rmode)                                      /* (the RAVE's voices: one block, by one core) */
        return 0;
    unsigned wave = p[TBS_WAVE] < TBS_NWAVE ? p[TBS_WAVE] : 0u, i, j, o0;
    int poly = p[TBS_MODE] == TBS_POLY;
    unsigned mt = p[TBS_MTYPE] < TBS_NMTYPE ? p[TBS_MTYPE] : 0u;
    int32_t mac = mt ? p[TBS_MACRO] : 0, m_oct = 0, m_semi = 0, m_spread = 0;
    unsigned nosc = wave == TBS_STAB || wave == TBS_HOOV2 ? 3u : wave == TBS_SINE || wave >= TBS_ORGAN ? 1u : wave == TBS_SUPER ? (poly ? 3u : 5u) :
                    wave == TBS_HOOVER ? 3u : p[TBS_DETUNE] || (mt == TBS_M_REESE && mac) ? 2u : 1u;
    /* (21 Nov 2026) the FM-1 near its time, by steps (rfm_board.c tb_heavy; the effects never touched):
     *  1: the voices fading out in their release under -40 dB let go (inaudible);
     *  2: that, and past two voices the oldest fades out in ~5 ms (a released one first), 2 oscillators a voice */
    if (tb_heavy && !s->rmode) {
        unsigned nl = 0, old = TBS_VOICES;
        for (j = 0; j < TBS_VOICES; j++) {
            tb_syn_voice_t *v = &s->v[j];
            if (!v->live || v->shed)
                continue;
            if (v->amp.stage == 3 && v->amp.lvl < (1 << 30) / 100) {
                v->live = 0;
                continue;
            }
            nl++;
            if (old == TBS_VOICES || (!v->gate && s->v[old].gate) ||
                (!v->gate == !s->v[old].gate && (int32_t)(v->age - s->v[old].age) < 0))
                old = j;
        }
        if (tb_heavy >= 2u && nl > 2u && old < TBS_VOICES)
            s->v[old].shed = 1;
    }
    if (tb_heavy >= 2u && nosc > 2u && wave != TBS_STAB)   /* (STAB plays its three oscillators by number) */
        nosc = 2u;
    int32_t kg;                                       /* (the glide's step, worked out again only when GLIDE moves: each
                                                       *  synth its own - two cores render synths) */
    if (p[TBS_GLIDE] != s->kg_key || !s->kg_val)
        s->kg_key = p[TBS_GLIDE], s->kg_val = p[TBS_GLIDE] ? tb_env_k(p[TBS_GLIDE] / 2u + 20u) * (int32_t)SUBBLK : 1 << 30;
    kg = s->kg_val;
    int32_t noise = (int32_t)p[TBS_NOISE] * 200;                              /* Q15 */
    int32_t reso = (int32_t)p[TBS_RESO], drv = (int32_t)p[TBS_DRIVE], res_k, drive;
    /* MACRO: what the one knob moves, by its type (its own LFO: SCREECH 7 Hz, REESE 0.3 Hz, WOBBLE 2.5, GROWL 5) */
    if (mac) {
        static const uint32_t MLFO[TBS_NMTYPE] = {0, 682000u, 29200u, 243500u, 487000u};
        int32_t ml;
        s->mlfo += MLFO[mt] * n;
        ml = mt == TBS_M_WOBBLE ? tb_tri(s->mlfo) : tb_sin(s->mlfo);
        switch (mt) {
        case TBS_M_SCREECH:                           /* the filter opening and singing, a vibrato, hotter */
            m_oct = mac * 3 * 65536 / 127 + (int32_t)(((int64_t)ml * mac) >> 15) * 65536 / 254;
            m_semi = (int32_t)(((int64_t)ml * mac) >> 15) * 65536 / 160;
            reso += mac / 2, drv += mac / 2;
            break;
        case TBS_M_REESE:                             /* the saws apart, the filter swaying slowly */
            m_spread = mac * 520;
            m_oct = (int32_t)(((int64_t)ml * mac) >> 15) * 2 * 65536 / 127;
            break;
        case TBS_M_WOBBLE:                            /* the filter wobbling over four octaves */
            m_oct = (int32_t)(((int64_t)ml * mac) >> 15) * 4 * 65536 / 127 - mac * 65536 / 127;
            reso += mac / 4;
            break;
        default:                                      /* GROWL: the filter growling, driven */
            m_oct = (int32_t)(((int64_t)ml * mac) >> 15) * 3 * 65536 / 254 + mac * 65536 / 127;
            drv += mac;
            break;
        }
        reso = reso > 127 ? 127 : reso, drv = drv > 127 ? 127 : drv;
    }
    res_k = 65536 * 2 - reso * 129000 / 127;          /* the damping, Q16: 2 .. 0.03 */
    drive = 4096 + drv * 160;                         /* Q12: x1 .. x6 */
    int32_t sub = (int32_t)p[TBS_SUB] * 200;                                   /* Q15 */
    int32_t vgain = poly ? 15000 : 22000;
    tb_envk(&s->ka, &p[TBS_AMP]);
    tb_envk(&s->kf, &p[TBS_FILT]);
    uint32_t pw;
    if (kg > 1 << 30)
        kg = 1 << 30;
    /* the LFO of PWM and HOOVER: 0.7 Hz; the width 50 % +- 40 % */
    if (p[TBS_SPEED])                                 /* (VITESSE: 0.1 .. 8 Hz; 0: the 0.7 Hz of the classics) */
        s->lfo += (uint32_t)(((uint64_t)9739u * tb_exp2((int32_t)p[TBS_SPEED] * 65536 * 63 / 1270)) >> 16) * n;
    else
        s->lfo += 68174u * n;                         /* 0.7 x 2^32 / 44100 per sample */
    pw = 0x80000000u + (uint32_t)(tb_sin(s->lfo) * 26000);
    /* HOOVER 2's two LFOs (the chopper's width 0.2 .. 0.8 at ~4 Hz, the pulse's 0.1 .. 0.9 at ~5.5 Hz; VITESSE scales
     * them), the vibrato (6 Hz), the hardcore stage */
    uint32_t hw1, hw2;
    {
        uint32_t r = p[TBS_SPEED] ? (uint32_t)(((uint64_t)97390u * tb_exp2((int32_t)p[TBS_SPEED] * 65536 * 3 / 127 - (3 << 16) / 2)) >> 16) : 97390u;
        s->hl1 += (r * 41u / 10u) * n / 4u, s->hl2 += (r * 55u / 10u) * n / 4u;
        hw1 = 0x80000000u + (uint32_t)(tb_tri(s->hl1) * 39321);            /* +- 0.3 of a cycle */
        hw2 = 0x80000000u + (uint32_t)(tb_tri(s->hl2) * 52428);            /* +- 0.4 */
    }
    s->vlfo += 584346u * n;                           /* 6 Hz */
    int32_t vib = p[TBS_VIB] ? (int32_t)(((int64_t)tb_sin(s->vlfo) * p[TBS_VIB]) >> 15) * 516 : 0;   /* Q16 semis, up to 1 */
    unsigned ftype = p[TBS_FTYPE] < TBS_NF ? p[TBS_FTYPE] : 0u, dtype = p[TBS_DTYPE] < TBS_ND ? p[TBS_DTYPE] : 0u;
    int32_t dist = p[TBS_DIST] ? 4096 + (int32_t)p[TBS_DIST] * (int32_t)p[TBS_DIST] * 15 : 0;   /* Q12: x1 .. x60 */
    int32_t hpk = p[TBS_HPF] ? k_pole1(20u + (uint32_t)p[TBS_HPF] * (uint32_t)p[TBS_HPF] / 21u) : 0;  /* 20 .. 790 Hz */
    /* FOLD: its own gain (x1 .. x6: folded over a few times, not into noise) */
    int32_t foldg = p[TBS_DIST] ? 4096 + (int32_t)p[TBS_DIST] * (int32_t)p[TBS_DIST] * 5 / 4 : 0;
    unsigned gate = p[TBS_GATE] < TBS_NG ? p[TBS_GATE] : 0u;
    uint32_t gate_len = gate == TBS_G_8 ? tb_syn_step * 2u : gate == TBS_G_32 ? tb_syn_step / 2u :
                        gate == TBS_G_16T ? tb_syn_step * 2u / 3u : tb_syn_step;
    if (gate_len < 64u)
        gate_len = 64u;
    c->wave = wave, c->nosc = nosc, c->ftype = ftype, c->dtype = dtype, c->gate = gate, c->gate_len = gate_len;
    c->kg = kg, c->noise = noise, c->res_k = res_k, c->drive = drive, c->sub = sub, c->vgain = vgain, c->vib = vib;
    c->dist = dist, c->hpk = hpk, c->foldg = foldg, c->m_oct = m_oct, c->m_semi = m_semi, c->m_spread = m_spread;
    c->pw = pw, c->hw1 = hw1, c->hw2 = hw2;
    c->k8a[0] = k8_of(s->ka.ka), c->k8a[1] = k8_of(s->ka.kd), c->k8a[2] = k8_of(s->ka.kr);
    c->k8f[0] = k8_of(s->kf.ka), c->k8f[1] = k8_of(s->kf.kd), c->k8f[2] = k8_of(s->kf.kr);
    (void)i, (void)j, (void)o0;
    return 1;
}
void tb_syn_voices(tb_syn_t *s, const tb_syn_ctx_t *c, int32_t *out, unsigned vmask)
{
    const uint8_t *p = c->p;
    unsigned n = c->n, i, j, o0;
    if (c->rave) {
        if (vmask & 1u)
            tb_rave_render(&s->rave, p, out, n, c->lfo_pitch, c->lfo_cut);
        return;
    }
    const unsigned wave = c->wave, nosc = c->nosc, ftype = c->ftype, dtype = c->dtype, gate = c->gate;
    const uint32_t gate_len = c->gate_len, pw = c->pw, hw1 = c->hw1, hw2 = c->hw2;
    const int32_t kg = c->kg, noise = c->noise, res_k = c->res_k, drive = c->drive, sub = c->sub, vgain = c->vgain,
                  vib = c->vib, dist = c->dist, hpk = c->hpk, foldg = c->foldg, m_oct = c->m_oct, m_semi = c->m_semi,
                  m_spread = c->m_spread, lfo_pitch = c->lfo_pitch, lfo_cut = c->lfo_cut;
    for (j = 0; j < TBS_VOICES; j++) {
        tb_syn_voice_t *v = &s->v[j];
        if (!v->live || !((vmask >> j) & 1u))
            continue;
        for (o0 = 0; o0 < n; o0 += SUBBLK) {
            unsigned m = n - o0 < SUBBLK ? n - o0 : SUBBLK;
            uint32_t inc[TBS_OSC], isub;
            int32_t tk1 = 0, tk4 = 0;
            int32_t a1, a2, a3;
            int32_t semi;
            /* glide, bend: per sub-block */
            v->pitch += (int32_t)(((int64_t)(v->target - v->pitch) * kg) >> 30);
            if (v->bend) {
                if (v->bendk > 0)                        /* (PTIME: its own time) */
                    v->bend = (int32_t)(((int64_t)v->bend * v->bendk) >> 16);
                else
                    v->bend -= v->bend / 170;            /* ~ 120 ms */
                if (v->bend > -64 && v->bend < 64)
                    v->bend = 0;
            }
            if (v->wh1) {                                /* WHOOP: a slow fall minus a quick one: up, then back */
                v->wh1 -= v->wh1 / 400, v->wh2 -= v->wh2 / 30;
                if (v->wh1 > -64 && v->wh1 < 64)
                    v->wh1 = v->wh2 = 0;
            }
            semi = v->pitch + v->bend + lfo_pitch + m_semi + (v->wh1 - v->wh2) + vib;
            {
                int32_t spread = (int32_t)p[TBS_DETUNE] * (wave == TBS_SUPER ? 420 : wave == TBS_HOOVER ? 380 : wave == TBS_HOOV2 ? 120 : 260) + m_spread;  /* Q16 semis */
                int32_t chorus = (int32_t)p[TBS_CHORUS] * 140;
                static const int8_t SPR[TBS_OSC] = {0, 2, -2, 1, -1};     /* (x half the spread) */
                for (i = 0; i < nosc; i++)
                    inc[i] = inc_of(semi + (nosc == 2 ? (i ? -1 : 1) : SPR[i]) * (wave == TBS_HOOVER && i >= 3u ? spread + chorus : spread) / 2
                                    + (wave == TBS_HOOVER && i >= 3u ? (i == 3u ? -(12 << 16) : 0) : 0));
                isub = inc_of(semi - (12 << 16));
            }
            /* the filter: CUTOFF 30 Hz .. 18 kHz, + ENV up to 7 octaves, + the key (half) */
            {
                /* (21 Nov 2026: the same sums without a 64-bit division - the FM-1 has none of its own) */
                int32_t oct = (int32_t)p[TBS_CUTOFF] * 9 * 65536 / 127 + (int32_t)(((int64_t)((int32_t)p[TBS_ENV] * 7 * 65536 / 127) * (v->flt.lvl >> 14)) >> 16)
                            + ((v->pitch - (60 << 16)) * (int32_t)p[TBS_KTRK]) / (12 * 127) + lfo_cut + m_oct;
                uint32_t hz = (uint32_t)((30ull * tb_exp2(tb_clamp(oct, -(2 << 16), 10 << 16))) >> 16), th;
                int64_t g, den, k2 = res_k;
                if (hz > 18000u)
                    hz = 18000u;
                if (hz != v->c_hz || res_k != v->c_k || !v->c_a1) {   /* (only when they moved: three divisions) */
                    {   /* the hardcore stage's top follows the CUTOFF (x3, 300 Hz .. 14 kHz): the knob heard through it */
                        uint32_t tf = hz * 3u < 300u ? 300u : hz * 3u > 14000u ? 14000u : hz * 3u;
                        v->c_tk1 = k_pole1(tf), v->c_tk4 = k_pole1(tf / 2u);   /* (the light engine: its low-pass at 2x) */
                    }
                    th = hz * 48696u;                              /* pi hz / fs as a phase: tan = sin / cos */
                    /* (th < pi/2: sine and cosine both >= 0, sin << 16 < 2^31 - the same division in 32 bits, and
                     *  den < 2^32: the next too; 21 Nov 2026, the FM-1's 64-bit ones are ~1200 cycles each) */
                    g = (int64_t)(((uint32_t)tb_sin(th) << 16) / (uint32_t)(tb_sin(th + 0x40000000u) | 1));   /* Q16 */
                    den = 65536 + ((g * (g + k2)) >> 16);
                    v->c_a1 = den <= 0xFFFFFFFFll ? (int32_t)(0x80000000u / (uint32_t)den) : (int32_t)((1ll << 31) / den);   /* Q15 */
                    v->c_a2 = (int32_t)((g * v->c_a1) >> 16);
                    v->c_a3 = (int32_t)((g * v->c_a2) >> 16);
                    v->c_hz = hz, v->c_k = res_k;
                }
                tk1 = v->c_tk1, tk4 = v->c_tk4;
                a1 = v->c_a1, a2 = v->c_a2, a3 = v->c_a3;
            }
            /* the new waves' settings, per sub-block: SYNC's and RING's ratio, VOIX's formants, the brightness */
            uint32_t ratio = 65536u, inc2 = 0;
            int32_t envl = (int32_t)((int64_t)p[TBS_ENV] * (v->flt.lvl >> 14) >> 7), ff1 = 0, ff2 = 0;   /* Q15 */
            if (wave == TBS_SYNC)
                ratio = 65536u + (uint32_t)p[TBS_DETUNE] * 3612u + (uint32_t)envl * 12u;   /* x1 .. x8, + the env up to x12 */
            else if (wave == TBS_RING)
                ratio = 65536u + (uint32_t)p[TBS_DETUNE] * 1548u + (uint32_t)envl * 3u;    /* x1 .. x4 */
            if (wave == TBS_SYNC || wave == TBS_RING)
                inc2 = (uint32_t)(((uint64_t)inc[0] * ratio) >> 16);
            if (wave == TBS_VOX) {                    /* OU O A E I: F1, F2 (Hz) */
                static const uint16_t F1[5] = {300, 500, 750, 500, 300}, F2[5] = {870, 1000, 1220, 1750, 2250};
                int32_t pos = (int32_t)p[TBS_DETUNE] * 512 + envl / 2, seg, fr;  /* 0 .. 65024 (+ the env) */
                pos = tb_clamp(pos, 0, 65535);
                seg = pos >> 14, fr = pos & 16383;
                {
                    int32_t a1f = F1[seg] + ((F1[seg + 1 > 4 ? 4 : seg + 1] - F1[seg]) * fr >> 14);
                    int32_t a2f = F2[seg] + ((F2[seg + 1 > 4 ? 4 : seg + 1] - F2[seg]) * fr >> 14);
                    ff1 = 2 * tb_sin((uint32_t)a1f * 48696u), ff2 = 2 * tb_sin((uint32_t)a2f * 48696u);
                }
            }
            /* (21 Nov 2026) the sub-block in stages, each a loop of its own with no choice inside it (the envelopes,
             * the oscillators, the filter and the drive, the hardcore stage, the high-pass, the gate and the output):
             * the same sums in the same order a voice, its state stepped as before; the 64-bit products made 32-bit
             * ones where their sizes allow it exactly (tb_mul15 / tb_mulsh / tb_mac15: tb_dsp.h) */
            int32_t xb[SUBBLK], ab[SUBBLK], fb[SUBBLK];
            const int fmw = wave == TBS_FM || wave == TBS_BELL;
            /* (21 Nov 2026, the light engine) the envelopes stepped 8 samples at a time - the same curves (each of
             * its 8-sample steps the 8 one-sample steps at once: (1-k)^8), the level drawn straight between - then
             * the samples left over one by one as before */
            i = 0;
            for (; i + 8u <= m; i += 8u) {
                int32_t a0 = v->amp.lvl, f0 = v->flt.lvl, da, df;
                unsigned q;
                env8(&v->amp, &s->ka, c->k8a);
                if (v->shed) {                         /* (shed: down ~30 dB in 5 ms, then let go: (63/64)^8) */
                    v->amp.lvl -= (int32_t)(((int64_t)v->amp.lvl * 7726) >> 16);
                    if (v->amp.lvl < (1 << 30) / 1000)
                        v->amp.stage = 0;
                }
                env8(&v->flt, &s->kf, c->k8f);
                da = (v->amp.lvl - a0) >> 3, df = (v->flt.lvl - f0) >> 3;
                for (q = 0; q < 8u; q++)
                    ab[i + q] = a0 + da * (int32_t)(q + 1u);
                if (fmw)
                    for (q = 0; q < 8u; q++)
                        fb[i + q] = f0 + df * (int32_t)q;
            }
            for (; i < m; i++) {                      /* the envelopes (nothing of the sound in them) */
                if (fmw)
                    fb[i] = v->flt.lvl;               /* (FM's index reads the filter's before its step) */
                tb_env_step(&v->amp, &s->ka);
                if (v->shed) {                         /* (shed: down ~30 dB in 5 ms, then let go) */
                    v->amp.lvl -= v->amp.lvl >> 6;
                    if (v->amp.lvl < (1 << 30) / 1000)
                        v->amp.stage = 0;
                }
                ab[i] = v->amp.lvl;
                tb_env_step(&v->flt, &s->kf);
            }
            switch (wave) {                           /* the oscillators */
            case TBS_SQUARE:
                for (i = 0; i < m; i++) {
                    unsigned k;
                    int32_t x = 0;
                    for (k = 0; k < nosc; k++)
                        x += pulse(v->ph[k], inc[k], 0x80000000u), v->ph[k] += inc[k];
                    xb[i] = x;
                }
                break;
            case TBS_PWM:
                for (i = 0; i < m; i++) {
                    unsigned k;
                    int32_t x = 0;
                    for (k = 0; k < nosc; k++)
                        x += pulse(v->ph[k], inc[k], pw + k * 0x20000000u), v->ph[k] += inc[k];
                    xb[i] = x;
                }
                break;
            case TBS_HOOVER:                           /* each a saw and a saw moved by the LFO: a moving comb */
                for (i = 0; i < m; i++) {
                    unsigned k;
                    int32_t x = 0;
                    for (k = 0; k < nosc; k++)
                        x += (saw(v->ph[k], inc[k]) + saw(v->ph[k] + pw + k * 0x15555555u, inc[k])) >> 1, v->ph[k] += inc[k];
                    xb[i] = x;
                }
                break;
            case TBS_HOOV2:                            /* each: the chopped saw and the PWM pulse */
                for (i = 0; i < m; i++) {
                    unsigned k;
                    int32_t x = 0;
                    for (k = 0; k < nosc; k++) {
                        uint32_t ph = v->ph[k];
                        int32_t cs = (ph << 1) < hw1 ? saw(ph, inc[k]) : -26000;   /* (the chopper at twice the note) */
                        x += (cs + pulse(ph, inc[k], hw2)) >> 1;
                        v->ph[k] += inc[k];
                    }
                    xb[i] = x;
                }
                break;
            case TBS_STAB:                             /* two squares apart, a saw an octave up */
                for (i = 0; i < m; i++) {
                    unsigned k;
                    int32_t x = pulse(v->ph[0], inc[0], 0x80000000u) + pulse(v->ph[1], inc[1], 0x80000000u)
                              + (saw(v->ph[2] << 1, inc[2] << 1) >> 1);
                    xb[i] = x * 3 / 4;
                    for (k = 0; k < nosc; k++)
                        v->ph[k] += inc[k];
                }
                break;
            case TBS_SINE:
            case TBS_ORGAN:
            case TBS_FM:
            case TBS_BELL:
            case TBS_SYNC:
            case TBS_RING:
            case TBS_VOX:
            case TBS_M1:
            case TBS_FOLD:
                for (i = 0; i < m; i++) {
                    unsigned k;
                    int32_t x = 0;
                    switch (wave) {                    /* (the waves of one oscillator: the choice a sample) */
                    case TBS_SINE:
                        x = tb_sin(v->ph[0]);
                        break;
                    case TBS_ORGAN: {                  /* drawbars: 8', 4', and (DETUNE) 2 2/3', 2' */
                        int32_t b = (int32_t)p[TBS_DETUNE] * 258;
                        x = (tb_sin(v->ph[0]) * 2 + tb_sin(v->ph[0] * 2u) + ((tb_sin(v->ph[0] * 3u) * b) >> 16)
                             + ((tb_sin(v->ph[0] * 4u) * b) >> 15)) * 2 / 5;
                        break;
                    }
                    case TBS_FM: case TBS_BELL: {      /* two operators: the index from DETUNE and the filter's envelope */
                        uint32_t mph = wave == TBS_FM ? v->ph[0] : v->ph[0] * 7u / 2u;
                        int32_t idx = (int32_t)p[TBS_DETUNE] * 64 + (((int32_t)p[TBS_ENV] * (fb[i] >> 14)) >> 13);
                        x = tb_sin(v->ph[0] + (uint32_t)((tb_sin(mph) * idx) >> 2) * 64u);
                        break;
                    }
                    case TBS_SYNC:                     /* the slave saw, reset when the note's phase wraps */
                        if (v->ph[0] + inc[0] < v->ph[0])
                            v->ph[1] = (uint32_t)(((uint64_t)(v->ph[0] + inc[0]) * ratio) >> 16);
                        else
                            v->ph[1] += inc2;
                        x = (int32_t)(v->ph[1] >> 16) - 32768;
                        x = (x * 7 + saw(v->ph[0], inc[0])) >> 3;
                        v->f1lo += (x - v->f1lo) >> 9;     /* (its partial cycles' offset taken off) */
                        x -= v->f1lo;
                        break;
                    case TBS_RING:                     /* a saw times a sine at the ratio, a little of the saw */
                        v->ph[1] += inc2;
                        x = saw(v->ph[0], inc[0]);
                        x = (tb_mul15(x, tb_sin(v->ph[1])) * 3 + x) >> 2;
                        break;
                    case TBS_VOX: {                    /* a saw through two formants (band-passes) */
                        int32_t src = saw(v->ph[0], inc[0]) >> 1, hi;
                        v->f1lo = tb_clamp(v->f1lo + tb_mulsh(v->f1bd, ff1, 15), -(1 << 20), 1 << 20);
                        hi = src - v->f1lo - (v->f1bd * 5 >> 5);
                        v->f1bd = tb_clamp(v->f1bd + tb_mulsh(hi, ff1, 15), -(1 << 20), 1 << 20);
                        v->f2lo = tb_clamp(v->f2lo + tb_mulsh(v->f2bd, ff2, 15), -(1 << 20), 1 << 20);
                        hi = src - v->f2lo - (v->f2bd * 4 >> 5);
                        v->f2bd = tb_clamp(v->f2bd + tb_mulsh(hi, ff2, 15), -(1 << 20), 1 << 20);
                        x = tb_clamp((v->f1bd * 5 >> 3) + (v->f2bd * 3 >> 3), -32767, 32767);
                        break;
                    }
                    case TBS_M1: {                     /* 8' 4' 2 2/3' 2', the top (1 3/5', 1') ringing off */
                        uint32_t q = v->ph[0];
                        int32_t t = (int32_t)p[TBS_DETUNE] * 129;  /* (the timbre: from round to bright) */
                        int32_t br = 8000 + envl;
                        x = tb_sin(q) * 2 + ((tb_sin(q * 2u) * (16384 + t / 2)) >> 14) + ((tb_sin(q * 3u) * t) >> 15)
                          + ((tb_sin(q * 4u) * (8192 + t / 2)) >> 15)
                          + ((int32_t)(((int64_t)(tb_sin(q * 5u) + tb_sin(q * 8u)) * br) >> 16));
                        x = x * 2 / 7;
                        break;
                    }
                    default: {                         /* FOLD: a sine folded back on itself, the more the louder */
                        int32_t g = 16 + (int32_t)p[TBS_DETUNE] / 8 + (envl >> 11), u;
                        u = ((tb_sin(v->ph[0]) * g) >> 4) + 32768;
                        u = ((u % 131072) + 131072) % 131072;
                        x = u < 65536 ? u - 32768 : 98303 - u;
                        break;
                    }
                    }
                    for (k = 0; k < nosc; k++)
                        v->ph[k] += inc[k];
                    xb[i] = x;
                }
                break;
            default:                                   /* the saws: SAW, SUPER */
                for (i = 0; i < m; i++) {
                    unsigned k;
                    int32_t x = 0;
                    for (k = 0; k < nosc; k++)
                        x += saw(v->ph[k], inc[k]), v->ph[k] += inc[k];
                    xb[i] = x;
                }
            }
            if (nosc != 1) {                           /* the oscillators' sum brought back */
                int32_t ng = nosc == 2 ? 23170 : nosc == 3 ? 18918 : 14654;
                for (i = 0; i < m; i++)
                    xb[i] = tb_mul15(xb[i], ng);
            }
            if (noise)
                for (i = 0; i < m; i++)
                    xb[i] += (((int32_t)(tb_noise(&s->noise) >> 17) - 16384) * noise) >> 14;
            if (sub)
                for (i = 0; i < m; i++) {
                    xb[i] += (pulse(v->sub, isub, 0x80000000u) * sub) >> 15;
                    v->sub += isub;
                }
            {                                          /* the zero-delay state-variable filter, then the drive */
                int32_t ic1 = v->ic1, ic2 = v->ic2;
                for (i = 0; i < m; i++) {
                    int32_t x = xb[i], v1, v2, v3 = x - ic2, y;
                    v1 = tb_mac15(a1, ic1, a2, v3);
                    v2 = ic2 + tb_mac15(a2, ic1, a3, v3);
                    ic1 = 2 * v1 - ic1;
                    ic2 = 2 * v2 - ic2;
                    /* (its state held inside +-5 x 2^20: a guard against a runaway; not -2^22, whose low word,
                     * 0xFFC00000, the build's check takes for a call into the chip's ROM) */
                    if (ic1 > (5 << 20) || ic1 < -(5 << 20))
                        ic1 = ic1 > 0 ? 5 << 20 : -(5 << 20);
                    if (ic2 > (5 << 20) || ic2 < -(5 << 20))
                        ic2 = ic2 > 0 ? 5 << 20 : -(5 << 20);
                    if (ftype == TBS_F_BP)
                        v2 = v1;
                    else if (ftype == TBS_F_HP)
                        v2 = x - (int32_t)(((int64_t)res_k * v1) >> 16) - v2;
                    y = v2 > 65535 ? 65535 : v2 < -65535 ? -65535 : v2;
                    xb[i] = tb_tanh((y * drive) >> 12);       /* the drive: soft from x1, hot at x6 */
                }
                v->ic1 = ic1, v->ic2 = ic2;
            }
            if (dist) {                                /* the hardcore stage: crushed (at 4x: no aliasing hiss), */
                int32_t dprev = v->dprev, t4 = v->t4, tame = v->tame;   /* cleaned under, tamed over */
                for (i = 0; i < m; i++) {
                    int32_t y = xb[i], acc = 0, q;
                    for (q = 1; q <= 2; q++) {        /* (the light engine: at 2x, not 4x) */
                        int32_t u0 = dprev + (((y - dprev) * q) >> 1), d = tb_mulsh(u0, dist, 12), z;
                        switch (dtype) {
                        case TBS_D_HARD: z = tb_clamp(d, -26000, 26000); break;
                        case TBS_D_FOLD: {
                            int32_t u = ((u0 * foldg) >> 12) + 32768;
                            u = ((u % 131072) + 131072) % 131072;
                            z = (u < 65536 ? u - 32768 : 98303 - u) * 3 / 4;
                            break;
                        }
                        case TBS_D_ASYM: z = d > 0 ? tb_tanh(d) : tb_tanh(d / 3) * 2 / 3; break;
                        default: z = tb_tanh(d);
                        }
                        t4 += tb_mul15(z - t4, tk4);  /* (a low-pass at the 4x rate) */
                        acc += t4;
                    }
                    dprev = y;
                    tame += tb_mul15((acc >> 1) - tame, tk1);
                    xb[i] = tame;
                }
                v->dprev = dprev, v->t4 = t4, v->tame = tame;
            }
            if (hpk) {
                int32_t hp = v->hp;
                for (i = 0; i < m; i++) {
                    hp += tb_mul15(xb[i] - hp, hpk);
                    xb[i] -= hp;
                }
                v->hp = hp;
            }
            if (wave >= TBS_SYNC) {
                int32_t dc = v->dc;
                for (i = 0; i < m; i++) {
                    dc += (xb[i] - dc) >> 10;
                    xb[i] -= dc;
                }
                v->dc = dc;
            }
            if (gate) {                                /* GATE: open the first ~60 % of each slice */
                uint32_t len = gate_len, g0 = v->gpos, q = g0 / len, r = g0 % len, third = len / 3u, open = len * 3u / 5u;
                int32_t glvl = v->glvl;
                for (i = 0; i < m; i++) {              /* (the slice and the place in it counted along: g / len, g % len) */
                    uint32_t ph = r;
                    if (gate == TBS_G_SWING && (q & 1u))
                        ph = r + third >= len ? r + third - len : r + third;   /* (the off-beat sixteenth late) */
                    glvl += ((ph < open ? 32767 : 0) - glvl) >> 5;
                    xb[i] = tb_mul15(xb[i], glvl);       /* (the 32-bit product overflowed past 65535: a click) */
                    if (++r == len)
                        r = 0, q++;
                }
                v->gpos = g0 + m, v->glvl = glvl;
            }
            {
                int32_t vg = (v->vel * vgain) >> 15;
                int32_t *o = out + o0;
                for (i = 0; i < m; i++)
                    o[i] += tb_mul15(tb_mul15(xb[i], ab[i] >> 15), vg);
            }
        }
        if (!v->amp.stage)
            v->live = 0;
    }
}
void tb_syn_render(tb_syn_t *s, const uint8_t *p, int32_t *out, unsigned n, int32_t lfo_pitch, int32_t lfo_cut)
{
    tb_syn_ctx_t c;
    tb_syn_begin(s, p, n, lfo_pitch, lfo_cut, &c);
    tb_syn_voices(s, &c, out, 0xFu);
}
unsigned tb_syn_live(const tb_syn_t *s)
{
    unsigned j, m = 0;
    if (s->rmode)
        return 1u;
    for (j = 0; j < TBS_VOICES; j++)
        m |= s->v[j].live ? 1u << j : 0u;
    return m;
}
