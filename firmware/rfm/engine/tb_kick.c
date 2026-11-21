/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: KICK (tb_kick.h). The voice, all fixed point: an oscillator (a rounded triangle, the 909's, or a sine,
 * the 808's) whose pitch falls in up to two exponential drops onto the note; held over LONG (a 35 % sag) or decaying
 * over it, then released; a band-passed noise tick on top; -> drive 1 (asymmetric when ASYM) -> a low cut -> a
 * resonant mid bump (CREUX) -> drive 2 (level kept) -> two one-pole low-passes (TONE) -> a DC blocker; then the SUB
 * sine added clean, through the same low cut so it adds to the fundamental. */
#include "tb_kick.h"
#include "tb_dsp.h"
#include <string.h>
#include "tb_kick_smp.h"            /* (generated: the sampled kicks) */

typedef struct {
    uint8_t note;                 /* the note it lands on (MIDI) */
    uint16_t f_sw;                /* the drop above it (Hz Q4) */
    uint32_t sw_us;               /* its time constant */
    uint16_t f_sw2;               /* a second, slower drop (Hz Q4, 0 = none) */
    uint32_t sw2_us;
    uint16_t len_ms;              /* how long it lasts at 200 BPM (the same part of a beat at any tempo) */
    uint8_t env;                  /* 0: held (a 35 % sag), 1: decaying over the length */
    uint8_t rel_ms;               /* the release after it */
    uint8_t wave;                 /* 0: rounded triangle (909), 1: sine (808) */
    uint16_t d1, asym, cut;       /* drive 1 (x Q4), its imbalance (Q8), the low cut (Hz) */
    uint16_t mid, mid_hz;         /* the mid bump (x Q8), its centre (Hz) */
    uint16_t d2;                  /* drive 2 (x Q4) */
    uint16_t tone, click;         /* the low-pass (Hz), the tick (Q8) */
    uint16_t hold_ms;             /* the first drop held this long at 200 BPM before it falls */
    uint16_t body;                /* a sine layer on the note itself from the hit (Q8, 0: none) */
    uint16_t tail_tone;           /* the low-pass after the hold (Hz, 0: TONE's all along) */
    /* (the kick bank of Nov 2026; all 0 on the earlier types: they sound as they did, bit for bit) */
    uint8_t dmode;                /* drive 1's curve: KD_TANH (soft), KD_CLIP (to a square: the millennium kick),
                                   * KD_FOLD (folded back: hollow, metallic overtones) */
    uint16_t click_hz;            /* the tick's centre (Hz, 0: 3 kHz) */
    uint8_t click_ms;             /* how fast it dies (time constant, ms; 0: ~6 ms) */
    uint8_t click_kind;           /* KC_NOISE (a band of noise), KC_TOK (a short sine: a wooden tok), KC_CHIRP (a sine
                                   * falling fast from 4x onto click_hz: a laser zap) */
    uint8_t fm, fm_ratio;         /* the body phase-modulated by a sine at fm_ratio (Q4) x its pitch, by fm (Q8 of a
                                   * cycle), following the envelope: metal, bells, industrial (0: none) */
    uint8_t grit;                 /* noise in the body before the drive, following the envelope (Q8): texture, crackle */
    uint16_t mid_hz2;             /* the mid bump moving to here (Hz, 0: it stays) .. */
    uint16_t mid_ms;              /* .. with this time constant (ms): a resonance that talks */
    uint8_t pre;                  /* the highs pushed into drive 1 (Q6 over a 1 kHz split): a mixer's channel driven
                                   * with its EQ up, the early gabber way (0: none) */
    uint8_t smp;                  /* a sampled kick, 1 + its index in TBKS (the user's own, tools/gen_kick_smp.py): it
                                   * plays as recorded, PITCH its speed, PUNCH its attack, LONG cuts it shorter, DISTO
                                   * drives it, TONE darkens it, SUB adds its note; CREUX does nothing (0: none) */
} tb_kick_shape_t;
enum { KD_TANH, KD_CLIP, KD_FOLD };
enum { KC_NOISE, KC_TOK, KC_CHIRP };

enum { KT_ROND, KT_CREUX, KT_PROF, KT_NASAL, KT_FUZZ, KT_CASSE, KT_ASYM, KT_GABBR, KT_MOKUM, KT_TOK, KT_TERRO, KT_SPEED, KT_UPTMP, KT_DOEF, KT_EARLY, KT_BELGE, KT_DOOM, KT_909_, KT_BOOM, KT_JUMP, KT_JUMPD, KT_BOUNC,
       /* the kicks with a tail (a second voice: raw, frenchcore, uptempo, industrial...) */
       KT_HURLE, KT_CANARD, KT_ZAAG, KT_FRENCH, KT_ELAST, KT_MITRA, KT_SIFFL, KT_CRECE, KT_ACIER, KT_GOUFF, KT_PIOCH,
       KT_GRIZZ, KT_PILON, KT_CRAQU, KT_ZAAGL, KT_TUNNEL, KT_MARTO, KT_SONAR,
       /* the bank of Nov 2026 (tools/kick/bank_nov26_kicks.py): after the earlier ones, so a saved TYPE stays itself */
#define KB_ENUM
#include "tb_kick_nov26.inc"
#undef KB_ENUM
       /* the user's frenchcore kicks, sampled (5 Nov 2026) */
       KT_FLOUD, KT_FDIST, KT_FLOOP,
       KT_COUNT };
const char *const TB_KICK_TYPE_NAME[KT_COUNT + TB_AKICK_NTYPES] = {"ROUND", "HOLLOW", "DEEP", "NASAL", "FUZZ", "BROKEN", "ASYM", "GABBR", "MOKUM", "TOK", "TERRO", "SPEED", "UPTMP", "DOEF", "EARLY", "BELGN", "DOOM", "909", "BOOM", "JUMP", "JUMPD", "BOUNC",
                                               "HOWL", "DUCK", "ZAAG", "FRENCH", "ELAST", "GUNNR", "WHSTL", "RATTL", "STEEL",
                                               "ABYSS", "PICK", "GRIZZ", "PESTL", "CRNCH", "ZAAGL", "TUNNL", "HAMMR", "SONAR",
#define KB_NAME
#include "tb_kick_nov26.inc"
#undef KB_NAME
                                               "FLOUD", "FDIST", "FLOOP",
                                               /* the first version's (the Atelier's: tb_akick.c) */
                                               "*ROND", "*CREUX", "*PROF", "*NASAL", "*FUZZ", "*CASSE", "*ASYM", "*GABBR",
                                               "*MOKUM", "*TOK", "*TERRO", "*SPEED", "*UPTMP", "*DOEF", "*EARLY", "*BELGE",
                                               "*DOOM", "*909", "*BOOM", "*JUMP", "*JUMPD", "*BOUNC",
};
static const tb_kick_shape_t KICK_TYPES[KT_COUNT] = {
    /* note, f_sw, sw_us, f_sw2, sw2_us, len_ms, env, rel_ms, wave, d1, asym, cut, mid, mid_hz, d2, tone, click, hold_ms, body, tail_tone */
    [KT_ROND] = {30, 2240, 35000, 0, 0, 300, 0, 12, 0, 128, 0, 25, 0, 800, 24, 2200, 0, 0, 0, 0},   /* F#1 */
    [KT_CREUX] = {31, 2880, 25000, 0, 0, 260, 0, 12, 0, 960, 0, 90, 768, 450, 48, 3500, 0, 0, 0, 0},   /* G1 */
    [KT_PROF] = {30, 2880, 28000, 0, 0, 280, 0, 12, 0, 800, 0, 80, 896, 350, 48, 3000, 0, 0, 0, 0},   /* F#1 */
    [KT_NASAL] = {31, 3200, 24000, 0, 0, 250, 0, 12, 0, 960, 0, 100, 1024, 600, 64, 4000, 51, 0, 0, 0},   /* G1 */
    [KT_FUZZ] = {31, 4000, 22000, 0, 0, 270, 0, 12, 0, 3200, 154, 150, 1024, 3000, 320, 14000, 205, 0, 0, 0},   /* G1 */
    [KT_CASSE] = {31, 4000, 22000, 0, 0, 270, 0, 12, 0, 4800, 230, 200, 1280, 3500, 480, 16000, 256, 0, 0, 0},   /* G1 */
    [KT_ASYM] = {31, 3200, 22000, 0, 0, 270, 0, 12, 0, 960, 205, 70, 384, 1000, 64, 6000, 77, 0, 0, 0},   /* G1 */
    [KT_GABBR] = {33, 5600, 12000, 0, 0, 240, 0, 12, 0, 1280, 77, 120, 640, 1000, 96, 6000, 128, 0, 0, 0},   /* A1 */
    [KT_MOKUM] = {34, 6400, 9000, 0, 0, 150, 1, 10, 0, 2400, 60, 100, 640, 900, 96, 8000, 140, 0, 0, 0},   /* A#1 */
    [KT_TOK] = {36, 4800, 10000, 0, 0, 160, 1, 10, 0, 1280, 0, 150, 1024, 1100, 128, 7000, 100, 0, 0, 0},   /* C2 */
    [KT_TERRO] = {37, 9600, 6000, 0, 0, 110, 1, 6, 0, 4800, 120, 140, 768, 1500, 192, 12000, 200, 0, 0, 0},   /* C#2 */
    [KT_SPEED] = {40, 14400, 4000, 0, 0, 60, 1, 4, 0, 3200, 0, 180, 512, 2000, 128, 14000, 256, 0, 0, 0},   /* E2 */
    [KT_UPTMP] = {37, 8000, 12000, 2400, 45000, 260, 0, 10, 0, 4000, 0, 40, 768, 1100, 320, 12000, 150, 0, 0, 0},   /* C#2 */
    [KT_DOEF] = {30, 2560, 27000, 0, 0, 290, 0, 12, 0, 1280, 0, 80, 0, 1000, 48, 6000, 0, 0, 0, 0},   /* F#1 */
    [KT_EARLY] = {31, 2880, 25000, 0, 0, 280, 0, 12, 0, 1600, 50, 60, 384, 700, 48, 5000, 60, 0, 0, 0},   /* G1 */
    [KT_BELGE] = {31, 3520, 20000, 0, 0, 330, 1, 30, 0, 320, 0, 30, 256, 500, 32, 5000, 80, 0, 0, 0},   /* G1 */
    [KT_DOOM] = {26, 1920, 40000, 0, 0, 520, 1, 80, 1, 192, 0, 15, 0, 1000, 32, 1800, 0, 0, 0, 0},   /* D1 */
    [KT_909_] = {31, 2880, 22000, 0, 0, 300, 0, 12, 0, 32, 0, 20, 0, 1000, 16, 6000, 77, 0, 0, 0},   /* G1 */
    [KT_BOOM] = {28, 1440, 30000, 0, 0, 600, 1, 100, 1, 64, 0, 15, 0, 1000, 16, 3000, 0, 0, 0, 0},   /* E1 */
    [KT_JUMP] = {31, 3680, 25000, 1280, 40000, 270, 0, 12, 0, 640, 0, 4, 0, 1000, 64, 10000, 60, 0, 0, 0},   /* G1 */
    [KT_JUMPD] = {31, 4160, 22000, 1600, 40000, 270, 0, 12, 0, 1920, 0, 5, 384, 1800, 128, 12000, 120, 0, 0, 0},   /* G1 */
    [KT_BOUNC] = {33, 4160, 15000, 960, 30000, 180, 0, 8, 0, 960, 0, 6, 256, 1400, 64, 11000, 90, 0, 0, 0},   /* A1 */
    /* the punches of the kicks with a tail: short, the tail does the rest */
    [KT_HURLE] = {31, 5600, 9000, 0, 0, 85, 1, 8, 0, 1600, 0, 40, 384, 1200, 96, 9000, 140, 0, 0, 0},
    [KT_CANARD] = {31, 4800, 10000, 0, 0, 90, 1, 8, 0, 1280, 0, 40, 256, 1000, 64, 8000, 110, 0, 0, 0},
    [KT_ZAAG] = {30, 4800, 11000, 0, 0, 110, 1, 10, 0, 1280, 0, 30, 256, 900, 64, 7000, 100, 0, 0, 0},
    [KT_FRENCH] = {33, 8000, 6000, 0, 0, 60, 1, 6, 0, 2400, 0, 60, 512, 1500, 128, 12000, 220, 0, 0, 0},
    [KT_ELAST] = {33, 6400, 7000, 0, 0, 70, 1, 6, 0, 1600, 0, 50, 384, 1300, 96, 10000, 160, 0, 0, 0},
    [KT_MITRA] = {37, 11200, 4000, 0, 0, 40, 1, 4, 0, 3200, 0, 120, 768, 1800, 192, 14000, 256, 0, 0, 0},
    [KT_SIFFL] = {36, 9600, 5000, 0, 0, 50, 1, 5, 0, 2400, 0, 90, 512, 1600, 160, 13000, 200, 0, 0, 0},
    [KT_CRECE] = {31, 3200, 20000, 0, 0, 120, 1, 10, 0, 960, 0, 25, 256, 900, 48, 6000, 80, 0, 0, 0},
    [KT_ACIER] = {33, 6400, 8000, 0, 0, 70, 1, 6, 0, 2400, 0, 60, 512, 2000, 128, 12000, 200, 0, 0, 0},
    [KT_GOUFF] = {26, 2240, 30000, 0, 0, 140, 1, 12, 1, 480, 0, 15, 0, 800, 32, 3000, 40, 0, 0, 0},
    [KT_PIOCH] = {38, 12800, 3500, 0, 0, 35, 1, 4, 0, 4000, 0, 150, 768, 2200, 192, 15000, 256, 0, 0, 0},
    [KT_GRIZZ] = {30, 4800, 10000, 0, 0, 90, 1, 8, 0, 1600, 0, 35, 384, 900, 96, 8000, 120, 0, 0, 0},
    [KT_PILON] = {32, 5600, 9000, 0, 0, 90, 1, 8, 0, 1280, 0, 40, 384, 1100, 80, 9000, 130, 0, 0, 0},
    [KT_CRAQU] = {31, 5600, 8000, 0, 0, 75, 1, 6, 0, 2400, 100, 50, 512, 1400, 128, 11000, 180, 0, 0, 0},
    [KT_ZAAGL] = {29, 4160, 12000, 0, 0, 120, 1, 10, 0, 960, 0, 25, 256, 800, 48, 6000, 90, 0, 0, 0},
    [KT_TUNNEL] = {35, 9600, 5000, 0, 0, 45, 1, 5, 0, 2400, 0, 80, 512, 1600, 160, 13000, 220, 0, 0, 0},
    [KT_MARTO] = {33, 6400, 9000, 0, 0, 70, 1, 6, 0, 3200, 60, 60, 640, 1300, 192, 12000, 200, 0, 0, 0},
    [KT_SONAR] = {31, 4800, 11000, 0, 0, 90, 1, 8, 0, 960, 0, 30, 256, 1000, 48, 8000, 100, 0, 0, 0},
#define KB_SHAPE
#include "tb_kick_nov26.inc"
#undef KB_SHAPE
    /* the sampled ones: their note the tail's (measured), the rest only there to keep the shared set-up sane */
    [KT_FLOUD] = {.note = 28, .f_sw = 2880, .sw_us = 20000, .len_ms = 300, .rel_ms = 12, .d1 = 64, .cut = 20, .mid_hz = 1000,
                  .d2 = 16, .tone = 8000, .smp = 1},
    [KT_FDIST] = {.note = 30, .f_sw = 2880, .sw_us = 20000, .len_ms = 300, .rel_ms = 12, .d1 = 64, .cut = 20, .mid_hz = 1000,
                  .d2 = 16, .tone = 8000, .smp = 2},
    [KT_FLOOP] = {.note = 28, .f_sw = 2880, .sw_us = 20000, .len_ms = 300, .rel_ms = 12, .d1 = 64, .cut = 20, .mid_hz = 1000,
                  .d2 = 16, .tone = 8000, .smp = 3},
};

/* the tails */
enum { TW_NONE, TW_SAW, TW_SQUARE, TW_TRI, TW_FM, TW_FOLD, TW_NOISE, TW_SAW2 };   /* SAW2: two saws, mod = their
                                                                                 * detune (cents): a raw screech */
enum { TE_FALL, TE_HOLD, TE_SWELL, TE_PULSE };
typedef struct {
    uint8_t wave;                 /* TW_* */
    int8_t semis;                 /* its pitch from the kick's note */
    int16_t bend;                 /* where its pitch starts, semitones x 16 over (+: falls onto it) or under (-: rises) */
    uint16_t bend_ms;             /* how fast it gets there (x 10: a time constant in tenths of ms) */
    uint16_t at_ms, len_ms;       /* when it comes in after the hit, how long it lasts (at 200 BPM) */
    uint8_t env;                  /* TE_*: falls, held then cut, swells to the next kick, held and pulsing */
    uint16_t cut;                 /* its filter (Hz; NOISE: % of its pitch) */
    int8_t fenv;                  /* the filter's envelope: octaves x 16 at the start, back to the cutoff over a third */
    uint8_t reso, bp;             /* 0..120; a band-pass (else a low-pass) */
    uint16_t drive;               /* before the filter (x Q4) */
    uint8_t mix;                  /* its level (Q8 of the punch's) */
    uint8_t mod, ratio;           /* FM: the index, the modulator's ratio (Q4); FOLD: how far it folds */
    uint8_t wob_hz, wob;          /* PULSE's rate / a vibrato's (Hz x 4), the vibrato (semitones x 8) */
} tb_kick_tail_t;
static const tb_kick_tail_t KICK_TAIL[KT_COUNT] = {
    /*            wave       semi bend  b_ms at   len  env       cut  fenv reso bp drive mix mod ratio wobhz wob */
    [KT_HURLE] = {TW_SAW,    12,  48,  300, 25, 240, TE_HOLD,   900,  24, 100, 0,  640, 230,  0, 16, 24,  2},
    [KT_CANARD] = {TW_TRI,   12,  32,  250, 25, 250, TE_FALL,  1300,   8,  92, 1,  960, 240,  0, 16,  0,  0},
    [KT_ZAAG] = {TW_SAW,      0,   0,   10, 90, 170, TE_SWELL,  700, -24,  70, 0, 1280, 255,  0, 16,  0,  0},
    [KT_FRENCH] = {TW_TRI,    0, 192,  600, 12, 300, TE_FALL,  2600,   0,  30, 0,  480, 255,  0, 16,  0,  0},
    [KT_ELAST] = {TW_FOLD,    0, -80,  500, 15, 260, TE_FALL,  2200,   8,  40, 0,  320, 240, 40, 16,  0,  0},
    [KT_MITRA] = {TW_SQUARE, 12,  64,  150,  8, 120, TE_FALL,  1500,  16,  60, 1, 2400, 255,  0, 16,  0,  0},
    [KT_SIFFL] = {TW_FM,     24,  32,  200, 10, 150, TE_HOLD,  2600,  16, 108, 0,  640, 200, 90, 16,  0,  0},
    [KT_CRECE] = {TW_TRI,     0,  16,  300, 30, 260, TE_PULSE, 3000,   0,  20, 0, 1280, 230,  0, 16,120,  0},
    [KT_ACIER] = {TW_FM,      7,  16,  200, 12, 200, TE_FALL,  2200,  16,  70, 1,  960, 230,100, 23,  0,  0},
    [KT_GOUFF] = {TW_FOLD,    0,  24,  800, 40, 500, TE_HOLD,   600,   8,  30, 0,  640, 255, 24, 16,  0,  0},
    [KT_PIOCH] = {TW_NOISE,  12,   0,   10,  6,  90, TE_FALL,   300,   0, 115, 1, 2400, 255,  0, 16,  0,  0},
    [KT_GRIZZ] = {TW_SAW,     0,  32,  300, 22, 260, TE_HOLD,   800,  24,  90, 0, 1280, 240,  0, 16, 32, 16},
    [KT_PILON] = {TW_TRI,    12,  32,  300, 22, 210, TE_HOLD,  1800,   8,  40, 0,  800, 230,  0, 16,  0,  0},
    [KT_CRAQU] = {TW_FOLD,    0,  32,  300, 18, 230, TE_FALL,  1500,  16,  50, 0, 1600, 240, 90, 16,  0,  0},
    [KT_ZAAGL] = {TW_SAW,     0,   0,   10, 70, 200, TE_SWELL,  550, -32,  85, 0, 1600, 255,  0, 16, 22,  6},
    [KT_TUNNEL] = {TW_SQUARE, 12,-160,  900, 10, 180, TE_FALL,  1400,  -8,  70, 1, 1920, 255,  0, 16,  0,  0},
    [KT_MARTO] = {TW_TRI,     0,  16,  200, 15, 140, TE_FALL,  5000,   0,  10, 0, 2400, 240,  0, 16,  0,  0},
    [KT_SONAR] = {TW_FM,     12,   0,   10, 20, 420, TE_FALL,  2000,   0,  20, 0,  160, 200, 20, 16, 20,  4},
#define KB_TAIL
#include "tb_kick_nov26.inc"
#undef KB_TAIL
};


const tb_kick_preset_t TB_KICK_PRESETS[] = {
    /* one a sound (chosen by ear): each a kick of its own, its pitch and length then on the knobs */
    {"ROUND", {KT_ROND, 24, 64, 64, 64, 64, 64, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"ROND F#1", {KT_COUNT + 0, 24, 64, 64, 64, 64, 64, 0}},
    {"BOUM D#1", {KT_COUNT + 0, 21, 56, 80, 64, 64, 56, 0}},
    {"HOLLOW", {KT_CREUX, 24, 64, 64, 64, 64, 64, 70}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"RTD 93 F#1", {KT_COUNT + 1, 23, 72, 60, 76, 76, 70, 80}},
    {"CREUX RTD G1", {KT_COUNT + 1, 24, 64, 64, 64, 64, 64, 70}},
    {"CRX LONG F1", {KT_COUNT + 1, 22, 64, 80, 64, 64, 64, 60}},
    {"COURT SEC A1", {KT_COUNT + 1, 26, 70, 30, 72, 72, 72, 80}},
    {"FUZZ", {KT_FUZZ, 24, 64, 64, 64, 64, 64, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"FUZZ SALE G1", {KT_COUNT + 4, 24, 64, 64, 64, 64, 64, 0}},
    {"FZ NASAL A1", {KT_COUNT + 4, 26, 64, 64, 64, 100, 54, 0}},
    {"INDUS F#1", {KT_COUNT + 4, 23, 64, 62, 72, 92, 80, 0}},
    {"TWISTED", {KT_ASYM, 25, 64, 64, 64, 64, 64, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"ASYM G#1", {KT_COUNT + 6, 25, 64, 64, 64, 64, 64, 0}},
    {"HARD ROUND", {KT_GABBR, 20, 60, 66, 48, 56, 52, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"GABB DUR A#1", {KT_COUNT + 7, 25, 76, 58, 84, 72, 78, 0}},
    {"GABBER A1", {KT_COUNT + 7, 24, 64, 64, 64, 64, 64, 0}},
    {"GABB ROND F1", {KT_COUNT + 7, 20, 60, 66, 48, 56, 52, 0}},
    {"LASER A1", {KT_COUNT + 7, 24, 112, 64, 64, 64, 70, 0}},
    {"NASAL", {KT_NASAL, 28, 68, 56, 72, 88, 76, 90}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"NAS AIGU B1", {KT_COUNT + 3, 28, 68, 56, 72, 88, 76, 90}},
    {"MOUTH", {KT_MOKUM, 22, 60, 96, 60, 64, 58, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"MOKUM A#1", {KT_COUNT + 8, 24, 64, 64, 64, 64, 64, 0}},
    {"MKM SALE B1", {KT_COUNT + 8, 25, 70, 60, 92, 76, 80, 0}},
    {"MKM LONG G#1", {KT_COUNT + 8, 22, 60, 96, 60, 64, 58, 0}},
    {"FAST", {KT_UPTMP, 24, 64, 64, 64, 64, 64, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"UPTEMPO C#2", {KT_COUNT + 12, 24, 64, 64, 64, 64, 64, 0}},
    {"MONSTER", {KT_CASSE, 19, 76, 96, 96, 127, 56, 43}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"CASSE F#1", {KT_COUNT + 5, 23, 64, 64, 64, 64, 64, 0}},
    {"CRX CASS G#1", {KT_COUNT + 5, 25, 64, 64, 64, 112, 64, 0}},
    {"SLAP", {KT_BELGE, 24, 64, 64, 64, 64, 64, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"BELGE 91 G1", {KT_COUNT + 15, 24, 64, 64, 64, 64, 64, 0}},
    {"DARK", {KT_DOOM, 24, 64, 64, 64, 64, 64, 27}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"DOOMCORE D1", {KT_COUNT + 16, 24, 64, 64, 64, 64, 64, 27}},
    {"SIMPLE", {KT_909_, 24, 64, 64, 64, 64, 64, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"909 SAT B1", {KT_COUNT + 17, 28, 66, 64, 100, 64, 70, 0}},
    {"909 PUR G1", {KT_COUNT + 17, 24, 64, 64, 64, 64, 64, 0}},
    {"BOOM", {KT_BOOM, 24, 64, 64, 64, 64, 64, 0}},
    {"BOUNCE", {KT_JUMP, 24, 64, 64, 64, 64, 64, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"JUMPSTYLE G1", {KT_COUNT + 19, 24, 64, 64, 64, 64, 64, 0}},
    {"HARD BOUNCE", {KT_BOUNC, 25, 70, 56, 90, 76, 76, 0}},
    /* .. and the first version's of its kind (tb_akick.c) */
    {"BOUNCE A1", {KT_COUNT + 21, 24, 64, 64, 64, 64, 64, 0}},
    {"BNC ROND F1", {KT_COUNT + 21, 20, 58, 72, 52, 56, 52, 21}},
    {"ROUND BOUNCE", {KT_BOUNC, 20, 58, 72, 52, 56, 52, 21}},
    /* the kicks with a tail: raw, frenchcore, uptempo, industrial, the 90s rattle */
    {"HOWLER", {KT_HURLE, 24, 64, 64, 64, 64, 64, 0}},
    {"ZAAG", {KT_ZAAG, 24, 64, 64, 64, 64, 64, 0}},
    {"FRENCHY", {KT_FRENCH, 24, 64, 64, 64, 64, 64, 0}},
    {"ELASTIC", {KT_ELAST, 24, 64, 64, 64, 64, 64, 0}},
    {"RATTLE", {KT_CRECE, 24, 64, 64, 64, 64, 64, 0}},
    {"STEEL", {KT_ACIER, 24, 64, 64, 64, 64, 64, 0}},
    {"GRIZZLY", {KT_GRIZZ, 24, 64, 64, 64, 64, 64, 0}},
    {"CRUNCHY", {KT_CRAQU, 24, 64, 64, 64, 64, 64, 0}},
    /* (archived, their types kept, still on the TYPE knob: BROKEN, DRY, ROLLING, SLOW, DEEP, STRIKE, ZAAG LONG, near
     * copies of FUZZ, TWISTED, TWISTED, DARK, HOLLOW, BOUNCE, ZAAG: see docs/kicks/CLEANUP.md; and, after listening, DUCK
     * and ABYSS: docs/kicks/archive_presets.tsv) */
    /* the bank of Nov 2026: 50 kicks, by family */
#define KB_PRESET
#include "tb_kick_nov26.inc"
#undef KB_PRESET
    /* the user's frenchcore kicks, as recorded (sampled: assets/kicks/) */
    {"FRENCH LOUD", {KT_FLOUD, 24, 64, 64, 64, 64, 64, 0}},
#if !TB_LITE
    {"FRENCH DIST", {KT_FDIST, 24, 64, 64, 64, 64, 64, 0}},
#endif
    {"FRENCH LOOP", {KT_FLOOP, 24, 64, 64, 64, 64, 64, 0}},
    /* the first version's kicks of the kinds above has none of (tb_akick.c), by kind */
    {"ABYSSE D#1", {KT_COUNT + 2, 21, 60, 76, 64, 92, 56, 110}},
    {"PROFOND F#1", {KT_COUNT + 2, 24, 64, 64, 64, 64, 64, 110}},
    {"TOK C2", {KT_COUNT + 9, 24, 64, 64, 64, 64, 64, 0}},
    {"TOK GRAS A1", {KT_COUNT + 9, 21, 56, 84, 56, 72, 48, 35}},
    {"TOK SEC D2", {KT_COUNT + 9, 26, 72, 36, 72, 80, 70, 0}},
    {"TERROR C#2", {KT_COUNT + 10, 24, 64, 64, 64, 64, 64, 0}},
    {"TERR FOU E2", {KT_COUNT + 10, 27, 80, 52, 100, 80, 84, 0}},
    {"SPEEDCORE E2", {KT_COUNT + 11, 24, 64, 64, 64, 64, 64, 0}},
    {"T.DOME F#1", {KT_COUNT + 13, 24, 64, 64, 64, 64, 64, 0}},
    {"DOEF F1", {KT_COUNT + 13, 23, 100, 50, 78, 70, 68, 0}},
    {"DOUX LONG F1", {KT_COUNT + 13, 23, 60, 84, 40, 60, 50, 0}},
    {"EARLY G1", {KT_COUNT + 14, 24, 64, 64, 64, 64, 64, 43}},
    {"E.SATURE A1", {KT_COUNT + 14, 26, 68, 64, 88, 72, 72, 43}},
    {"E.ROULE F1", {KT_COUNT + 14, 22, 60, 76, 56, 60, 54, 55}},
    {"HARDJUMP A1", {KT_COUNT + 20, 26, 72, 60, 84, 72, 74, 0}},
};
const unsigned TB_KICK_NPRESETS = sizeof TB_KICK_PRESETS / sizeof TB_KICK_PRESETS[0];
const unsigned TB_KICK_NTYPES = KT_COUNT + TB_AKICK_NTYPES;

/* ------------------------------------------------------- fixed point --- */
/* 2^(x / 65536) in Q16, x in -16..14 octaves */
static uint32_t k_exp2(int32_t x)
{
    int32_t n = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu, p;
    p = 65536u + ((f * (45556u + ((f * (14822u + ((f * 5160u) >> 16))) >> 16))) >> 16);
    if (n >= 15)
        return 0x7FFFFFFFu;
    if (n >= 0)
        return p << n;
    return n <= -31 ? 0u : p >> -n;
}
static int32_t k_pole(uint32_t hz) { return (int32_t)(k_exp2(-(int32_t)(hz * 13471u / 1000u)) >> 1); }
static uint32_t k_blk(uint32_t us) { return k_exp2(-(int32_t)(68606880u / (us < 500u ? 500u : us))); }
static int32_t k_svf(uint32_t hz) { return 2 * tb_sin(hz * 48696u); }
static int32_t k_log2(uint32_t x)
{
    int32_t n = 0;
    uint32_t u;
    while (x >= 131072u) {
        x >>= 1;
        n++;
    }
    u = x - 65536u;
    return (n << 16) + (int32_t)((u * (94535u - ((u * 28999u) >> 16))) >> 16);
}
static uint32_t k_isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}
/* the longest time constant (us) after which a drop of f_sw above f_end is within an eighth of a semitone at half of
 * len_ms */
static uint32_t k_land(int32_t f_sw, int32_t f_end, int32_t len_ms)
{
    uint32_t r = (uint32_t)(((int64_t)f_sw * 139 << 16) / (f_end > 0 ? f_end : 1));
    int32_t l2;
    if (r <= 65536u)
        return 0xFFFFFFFFu;
    l2 = k_log2(r);
    return (uint32_t)(((uint64_t)(uint32_t)len_ms * 47277000u) / (uint32_t)(l2 > 64 ? l2 : 64));
}
static int32_t k_scale(int32_t a, int32_t oct, int32_t lo, int32_t hi)
{
    return tb_clamp((int32_t)(((int64_t)a * k_exp2(oct)) >> 16), lo, hi);
}
static int32_t mulq16(int32_t a, uint32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
/* the factor (Q16) a value falls by in 16 samples for a time constant in tenths of ms */
static uint32_t k_g16(int32_t tenths) { return k_exp2(-(int32_t)(343030 / (tenths > 1 ? tenths : 1))); }

/* the first version's types (after RAVE FM's): their knobs with TYPE counted from 0 */
#define IS_ATELIER(p) ((p)[0] >= KT_COUNT && (p)[0] < KT_COUNT + TB_AKICK_NTYPES)
static const uint8_t *atelier(const uint8_t *p, uint8_t *q)
{
    memcpy(q, p, TB_KICK_NPARAM);
    q[0] = (uint8_t)(p[0] - KT_COUNT);
    return q;
}
__attribute__((noinline)) static void mode(tb_kick_t *k, unsigned a)            /* the union made the first version's (1) or RAVE FM's, cleared */
{
    if (k->amode != a) {
        if (a)
            tb_akick_init(&k->a);
        else {
            memset(k, 0, sizeof *k);
            k->key[0] = 0xFF;
        }
        k->amode = (uint8_t)a;
    }
}

int tb_kick_note(const uint8_t *p)
{
    uint8_t q[TB_KICK_NPARAM];
    if (IS_ATELIER(p))
        return tb_akick_note(atelier(p, q));
    return tb_clamp((int32_t)KICK_TYPES[tb_clamp(p[0], 0, KT_COUNT - 1)].note + tb_clamp(p[1], 0, 48) - 24, 12, 72);
}

/* the knobs, the semitones played and a beat (samples) -> the coefficients */
__attribute__((noinline)) static void k_setup(tb_kick_coef_t *c, const uint8_t *p, int32_t keys, uint32_t beat)
{
    const tb_kick_shape_t *s = &KICK_TYPES[tb_clamp(p[0], 0, KT_COUNT - 1)];
    int32_t semis = tb_clamp(p[1], 0, 48) - 24 + keys, pun = p[2] - 64, lon = p[3] - 64, dis = p[4] - 64,
            cre = p[5] - 64, ton = p[6] - 64;
    int32_t note = tb_clamp((int32_t)s->note + semis, 12, 72), up = (note - (int32_t)s->note) * 65536 / 12;
    int32_t f_end = k_scale(440 * 16, (note - 69) * 65536 / 12, 8 * 16, 1100 * 16);
    int32_t f_sw = k_scale(s->f_sw, up + pun * 2048, 0, 3000 * 16);
    int32_t f_sw2 = k_scale(s->f_sw2, up + pun * 2048, 0, 1500 * 16);
    int32_t sw_us = k_scale((int32_t)s->sw_us, pun * 614, 2000, 120000);
    int32_t sw2_us = k_scale(s->sw2_us ? (int32_t)s->sw2_us : 10000, pun * 614, 3000, 200000);
    uint32_t bt = beat ? beat : 13230u;
    int32_t len = k_scale((int32_t)((uint32_t)s->len_ms * bt / 13230u), lon * 1638, 25, 2000);
    int32_t d1 = k_scale(s->d1, dis * 4096, 16, 400 * 16);
    int32_t d2 = k_scale(s->d2, dis * 1638, 16, 40 * 16);
    int32_t as = s->asym ? tb_clamp((int32_t)s->asym + dis * 77 / 128, 0, 243) : 0;
    int32_t mid = tb_clamp((int32_t)s->mid + cre * 16, 0, 1536);
    int32_t tone = k_scale(s->tone, ton * 2731, 800, 18000);
    int32_t ck = k_scale(s->click, ton * 1365, 0, 384);
    int32_t sb = tb_clamp(p[7], 0, 127) * 3;
    uint32_t dc = s->cut < 18u ? s->cut : 18u, fe = (uint32_t)f_end >> 4, mk;
    int32_t ref, hold = (int32_t)((uint32_t)s->hold_ms * bt / 13230u);
    {
        int32_t heard = len < (int32_t)(bt * 10u / 441u) ? len : (int32_t)(bt * 10u / 441u);
        uint32_t l1, l2 = k_land(f_sw2, f_end, heard);
        hold = tb_clamp(hold, 0, heard / 3);
        l1 = k_land(f_sw, f_end, heard - hold);
        sw_us = tb_clamp(sw_us, 2000, (int32_t)(l1 < 120000u ? l1 : 120000u));
        sw2_us = tb_clamp(sw2_us, 2000, (int32_t)(l2 < 200000u ? l2 : 200000u));
    }
    fe = fe ? fe : 1u;
    mk = k_isqrt(65536u + ((uint32_t)s->cut * s->cut << 16) / (fe * fe)) * k_isqrt(65536u + (dc * dc << 16) / (fe * fe)) >> 8;
    c->inc_end = (uint32_t)f_end * 6087u;
    c->inc_sw = (uint32_t)f_sw * 6087u;
    c->inc_sw2 = (uint32_t)f_sw2 * 6087u;
    c->g_sw = k_blk((uint32_t)sw_us);
    c->g_sw2 = k_blk((uint32_t)sw2_us);
    c->len = (uint32_t)len * 441u / 10u;
    c->hold = (uint32_t)hold * 441u / 10u;
    c->body = (int32_t)s->body;
    c->env = s->env;
    c->wave = s->wave;
    c->g_dec = k_blk((uint32_t)len * 500u);
    c->g_rel = k_blk((uint32_t)(s->rel_ms ? s->rel_ms : 12u) * 1000u);
    c->g_choke = k_blk(3000u);
    c->d1p = d1;
    c->d1n = d1 * (256 - as) >> 8;
    c->asg = 32767 - as * 64;
    c->a_cut = k_pole((uint32_t)s->cut);
    c->a_dc = k_pole(dc);
    c->f_mid = k_svf(s->mid_hz);
    c->mid = mid;
    c->d2 = d2;
    ref = tb_tanh(tb_clamp(d2 * 2048, 0, 1 << 20));
    c->n2 = (int32_t)((32767u << 15) / (uint32_t)(ref > 1000 ? ref : 1000));
    c->a_tone = 32767 - k_pole((uint32_t)tone);
    c->a_tone2 = s->tail_tone ? 32767 - k_pole((uint32_t)k_scale(s->tail_tone, ton * 2731, 100, 18000)) : c->a_tone;
    c->f_clk = k_svf(s->click_hz ? s->click_hz : 3000u);
    c->click = ck;
    /* (Nov 2026: 0 everywhere on the earlier types) */
    c->dmode = s->dmode;
    c->ck_kind = s->click_kind;
    c->ck_g = s->click_ms ? (int32_t)k_blk((uint32_t)s->click_ms * 250u) : 0;   /* (k_blk(us): a time constant of 4 us) */
    c->ck_inc = (int32_t)((uint32_t)(s->click_hz ? s->click_hz : 3000u) * 97391u);
    c->fm = s->fm, c->fm_ratio = s->fm_ratio ? s->fm_ratio : 16;
    c->grit = s->grit;
    c->mid_f0 = s->mid_hz, c->mid_f1 = s->mid_hz2 ? s->mid_hz2 : s->mid_hz;
    c->mid_g = s->mid_hz2 ? (int32_t)k_blk((uint32_t)(s->mid_ms ? s->mid_ms : 40u) * 250u) : 0;
    c->pre = s->pre;
    c->a_pre = 32767 - k_pole(1000u);
    c->sub = tb_clamp((int32_t)((uint32_t)sb * (mk < 768u ? mk : 768u) >> 8), 0, 1200);
    c->smp = s->smp && s->smp <= TBKS_N ? s->smp : 0u;
    c->s_inc = 65536u, c->s_pk = c->s_drv = c->s_norm = c->s_a = c->s_sub = 0;
    if (c->smp) {                                     /* (a sampled kick: the knobs on the recording) */
        uint32_t L = TBKS[c->smp - 1].len;
        c->s_len = L;
        c->s_inc = k_exp2(semis * 65536 / 12);        /* PITCH and the keys: its speed */
        c->s_end = lon < 0 ? tb_clamp((int32_t)((uint64_t)L * (uint32_t)(64 + lon) / 64u), 1323, (int32_t)L) : L;
        c->s_pk = pun * 512;                          /* PUNCH: its first 25 ms up to x2 or down to nothing */
        c->s_drv = dis > 0 ? 16 + dis * 2 : 0;        /* DISTO up: driven (x1 .. x9) */
        c->s_norm = dis > 0 ? (int32_t)((20000u << 15) / (uint32_t)tb_tanh(20000 * c->s_drv >> 4)) : 0;
        c->s_a = ton < 0 ? 32767 - k_pole((uint32_t)k_scale(18000, ton * 6000, 150, 18000)) : 0;   /* TONE down: darker */
        c->s_sub = tb_clamp(p[7], 0, 127) * 48;       /* SUB: its note under it */
    }
    {                                                 /* the tail: LONG its length, DISTO its drive, CREUX its */
        const tb_kick_tail_t *tl = &KICK_TAIL[tb_clamp(p[0], 0, KT_COUNT - 1)];   /* resonance, TONE its filter */
        c->t_wave = tl->wave;
        if (tl->wave) {
            int32_t tn = tb_clamp(note + tl->semis, 12, 100);
            int32_t ft = k_scale(440 * 16, (tn - 69) * 65536 / 12, 8 * 16, 4000 * 16);
            int32_t tlen = k_scale((int32_t)((uint32_t)tl->len_ms * bt / 13230u), lon * 1638, 20, 2000);
            int32_t tat = (int32_t)((uint32_t)tl->at_ms * bt / 13230u), cut;
            c->t_inc = (uint32_t)ft * 6087u;
            c->t_at = (uint32_t)tat * 441u / 10u;
            c->t_len = (uint32_t)tlen * 441u / 10u;
            c->t_env = tl->env, c->t_bp = tl->bp, c->t_ratio = tl->ratio ? tl->ratio : 16;
            c->t_bend = tl->bend * 4096;
            c->t_g_bend = (int32_t)k_g16(tl->bend_ms);
            c->t_g_dec = (int32_t)k_g16(tlen * 10 * 2 / 5);
            c->t_g_rel = (int32_t)k_g16(tl->env == TE_SWELL ? 60 : 90);   /* (the swell: out in ~6 ms, not cut) */
            c->t_g_fe = (int32_t)k_g16(tlen * 10 / 3);
            c->t_fenv = tl->fenv * 4096;
            cut = tl->wave == TW_NOISE ? (ft >> 4) * tl->cut / 100 : tl->cut;
            c->t_cut = k_scale(cut, ton * 2048, 40, 7000);
            c->t_q = 45875 - tb_clamp((int32_t)tl->reso + cre / 2, 0, 124) * 340;
            c->t_drive = k_scale(tl->drive, dis * 4096, 16, 400 * 16);
            c->t_mix = tl->mix;
            c->t_mod = tl->mod;
            c->t_lfo = (uint32_t)tl->wob_hz * 24350u;
            c->t_wob = tl->wob * 8192;
            c->t_det = tl->wave == TW_SAW2 ? (int32_t)k_exp2((int32_t)tl->mod * 65536 / 1200) - 65536 : 0;
        }
    }
}

static void k_refresh(tb_kick_t *k, const uint8_t *p, int semis, uint32_t beat)
{
    if (memcmp(k->key, p, TB_KICK_NPARAM) || k->note != semis || k->beat != beat) {
        memcpy(k->key, p, TB_KICK_NPARAM);
        k->note = (int8_t)semis;
        k->beat = beat;
        k_setup(&k->c, p, semis, beat);
    }
}

void tb_kick_init(tb_kick_t *k)
{
    memset(k, 0, sizeof *k);
    k->key[0] = 0xFF;
}

void tb_kick_hit(tb_kick_t *k, const uint8_t *p, int semis, uint32_t beat, uint32_t seed)
{
    tb_kick_hit_t *h;
    uint8_t q[TB_KICK_NPARAM];
    mode(k, IS_ATELIER(p));
    if (k->amode) {
        tb_akick_hit(&k->a, atelier(p, q), semis, beat, seed);
        return;
    }
    k_refresh(k, p, semis, beat);
    if (k->h[k->cur].live)
        k->h[k->cur].choke = 1;                      /* one drum: a new hit chokes the last */
    k->cur ^= 1u;
    h = &k->h[k->cur];
    memset(h, 0, sizeof *h);
    h->sw = h->sw2 = 65536u;
    h->clk = 65536;
    h->mf = k->c.mid_f0;
    h->ckinc = (uint32_t)k->c.ck_inc * (k->c.ck_kind == KC_CHIRP ? 4u : 1u);
    h->sidx = k->c.smp;
    h->seed = seed | 1u;
    h->live = 1;
}

/* the output's ceiling: straight up to 20 000, then a tanh into the last 11 000 */
static inline int32_t k_ceiling(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= 20000)
        return x;
    a = 20000 + (11000 * tb_tanh(tb_clamp((int32_t)(((int64_t)(a - 20000) << 15) / 11000), 0, 1 << 20)) >> 15);
    return x < 0 ? -a : a;
}

/* the tail, n samples into b: a wave (its pitch bending onto the note) -> a drive -> a resonant filter (its own
 * envelope) -> a soft ceiling -> a DC blocker, under its envelope */
static void k_tail(const tb_kick_coef_t *c, tb_kick_hit_t *h, int32_t *b, unsigned n)
{
    unsigned i;
    int32_t comp = tb_clamp(c->t_q, 8192, 32768);
    for (i = 0; i < n; i++) {
        int32_t s, x, hi, out, L;
        uint32_t t = h->tt++;
        b[i] = 0;
        if (!c->t_wave || h->tdone || t < c->t_at)
            continue;
        t -= c->t_at;
        h->tlfo += c->t_lfo;
        if ((t & 15u) == 0) {                         /* (the slow things, every 16 samples) */
            int32_t semis, oct;
            uint32_t hz;
            if (!t) {
                h->tbend = c->t_bend, h->tfe = c->t_fenv;
                h->tl = c->t_env == TE_SWELL ? 0 : 1 << 24;
                h->tgate = 1 << 24;
            } else {
                h->tbend = mulq16(h->tbend, (uint32_t)c->t_g_bend);
                h->tfe = mulq16(h->tfe, (uint32_t)c->t_g_fe);
            }
            /* the shape (before the drive: a kick distorted, it stays loud while it falls) */
            if (c->t_env == TE_FALL)
                h->tl = t ? mulq16(h->tl, (uint32_t)c->t_g_dec) : h->tl;
            else if (c->t_env == TE_SWELL) {
                uint32_t u = (uint32_t)(((uint64_t)(t < c->t_len ? t : c->t_len) << 16) / c->t_len);
                h->tl = (int32_t)(((uint64_t)u * u) >> 8);   /* (in 64 bits: at its end u is 65536, its square
                                                               * 2^32 wrapped to 0 and cut the swell dead) */
            } else                                    /* (HOLD, PULSE: a quarter's sag) */
                h->tl = (1 << 24) - (int32_t)(((uint64_t)(t < c->t_len ? t : c->t_len) << 22) / c->t_len);
            /* the gate (after it all: open over the length, then a quick release; a new hit chokes it) */
            if (h->choke)
                h->tgate = mulq16(h->tgate, k_g16(30));
            else if (t >= c->t_len)
                h->tgate = mulq16(h->tgate, (uint32_t)c->t_g_rel);
            if ((t >= c->t_len || h->choke) && h->tgate < (1 << 12))
                h->tdone = 1;
            semis = h->tbend + (c->t_wob ? (int32_t)(((int64_t)tb_sin(h->tlfo) * c->t_wob) >> 15) : 0);
            h->tinc = (int32_t)(((uint64_t)c->t_inc * k_exp2(semis / 12)) >> 16);
            oct = h->tfe;
            hz = (uint32_t)(((uint64_t)(uint32_t)c->t_cut * k_exp2(tb_clamp(oct, -(6 << 16), 6 << 16))) >> 16);
            h->tf = k_svf(hz < 40u ? 40u : hz > 7000u ? 7000u : hz);
        }
        L = h->tgate;
        if (c->t_env == TE_SWELL)                     /* (the swell heard as one, the drive would flatten it) */
            L = (int32_t)(((int64_t)L * (h->tl >> 8)) >> 16);
        if (t < 88u)                                  /* (in over 2 ms) */
            L = (int32_t)((int64_t)L * (int32_t)t / 88);
        h->tph += (uint32_t)h->tinc;
        switch (c->t_wave) {
        case TW_SAW: s = (int32_t)(h->tph >> 16) - 32768; break;
        case TW_SAW2:                                 /* (two saws a few cents apart: they beat, they scream) */
            h->tph2 += (uint32_t)(h->tinc + mulq16(h->tinc, (uint32_t)c->t_det));
            s = ((int32_t)(h->tph >> 16) - 32768 + (int32_t)(h->tph2 >> 16) - 32768) >> 1;
            break;
        case TW_SQUARE: s = h->tph < 0x80000000u ? 24000 : -24000; break;
        case TW_TRI:
            s = tb_tri(h->tph + 0x40000000u);
            s = (s * (49152 - (((s * s) >> 15) >> 1))) >> 15;
            break;
        case TW_FM:
            h->tmph += (uint32_t)(((uint64_t)(uint32_t)h->tinc * c->t_ratio) >> 4);
            s = tb_sin(h->tph + ((uint32_t)(tb_sin(h->tmph) * (int32_t)c->t_mod) << 9));
            break;
        case TW_FOLD: {
            int32_t u = (int32_t)(((int64_t)tb_sin(h->tph) * (16 + (int32_t)c->t_mod)) >> 4) + 32768;
            u = ((u % 131072) + 131072) % 131072;
            s = u < 65536 ? u - 32768 : 98303 - u;
            break;
        }
        default: s = (int32_t)(tb_noise(&h->seed) >> 16) - 32768;
        }
        s = (int32_t)(((int64_t)s * (h->tl >> 9)) >> 15);
        x = tb_tanh(tb_clamp((int32_t)(((int64_t)s * c->t_drive) >> 4), -(1 << 20), 1 << 20));
        h->tlo = tb_clamp(h->tlo + (int32_t)(((int64_t)h->tf * h->tbd) >> 15), -(1 << 20), 1 << 20);
        hi = x - h->tlo - (int32_t)(((int64_t)c->t_q * h->tbd) >> 15);
        h->tbd = tb_clamp(h->tbd + (int32_t)(((int64_t)h->tf * hi) >> 15), -(1 << 20), 1 << 20);
        out = tb_tanh((int32_t)(((int64_t)(c->t_bp ? h->tbd : h->tlo) * comp) >> (c->t_bp ? 12 : 14)));
        h->tdy = ((c->a_dc >> 1) * (h->tdy + out - h->tdx)) >> 14;
        h->tdx = out;
        out = h->tdy;
        if (c->t_env == TE_PULSE) {                   /* (PULSE: chopped at its rate, the gabber's rattle) */
            int32_t v = (tb_sin(h->tlfo) + 32767) >> 1;
            out = (out * ((v * v) >> 15)) >> 15;
        }
        b[i] = (int32_t)((((int64_t)out * (L >> 9)) >> 15) * c->t_mix >> 8);
    }
}

#ifdef TB_KICK_DEBUG
int tb_kick_dbg;
#endif
/* the sampled kick (BC8, see tools/gen_kick_smp.py): sample i of kick k */
static inline int32_t k_smp_at(const uint8_t *d, uint32_t i)
{
    const int8_t *b = (const int8_t *)d + (i >> 4) * 17u;
    return ((int32_t)b[1 + (i & 15u)] * TBKS_SC[(uint8_t)b[0]]) >> 6;
}
/* as recorded at 64 everywhere (PITCH 24): the samples themselves, then the knobs; a new hit chokes it in 3 ms */
static void k_smp(const tb_kick_coef_t *c, tb_kick_hit_t *h, int32_t *y, unsigned n)
{
    /* (its own recording, the one it was hit with: a TYPE changed (a lock) while it is choked plays on the same) */
    const uint8_t *d = (const uint8_t *)TBKS_DATA + TBKS[h->sidx - 1].off;
    uint32_t i, L = TBKS[h->sidx - 1].len, end = h->sidx == c->smp ? c->s_end : L, pn = 1102u;   /* (PUNCH: 25 ms) */
    int32_t og0 = h->og ? h->og : 32767, og1 = h->choke ? (int32_t)(((uint32_t)og0 * c->g_choke) >> 16) : og0, og, dog;
    h->og = og1 > 0 ? og1 : 1;
    og = og0, dog = (og1 - og0) / (int32_t)n;
    for (i = 0; i < n; i++, og += dog) {
        uint32_t j = h->spos >> 16, f = (h->spos >> 1) & 0x7FFFu;
        int32_t x;
        if (j + 1u >= end || (h->choke && og < 40)) {
            h->live = 0;
            return;
        }
        x = k_smp_at(d, j);
        if (f)                                        /* (between two samples: PITCH moved) */
            x += ((k_smp_at(d, j + 1u) - x) * (int32_t)f) >> 15;
        if (end < L && end - j < 176u)         /* (LONG down: out over 4 ms where it stops) */
            x = x * (int32_t)(end - j) / 176;
        if (c->s_pk && j < pn)
            x += (int32_t)(((int64_t)x * c->s_pk * (int32_t)(((pn - j) * 3807u) >> 7)) >> 30);   /* ((pn - j) / pn in Q15:
                                                                                      * 32768 / 1102 = 3807 / 128) */
        if (c->s_drv)
            x = (int32_t)(((int64_t)tb_tanh(tb_clamp(x * c->s_drv >> 4, -(1 << 20), 1 << 20)) * c->s_norm) >> 15);
        if (c->s_a) {
            h->slo += (int32_t)(((int64_t)c->s_a * (x - h->slo)) >> 15);
            x = h->slo;
        }
        if (c->s_sub) {
            x += (int32_t)(((int64_t)tb_sin(h->sph) * c->s_sub * tb_divq15(L - j, L)) >> 30);   /* (no 64-bit division) */
            h->sph += c->inc_end;
        }
        if (og < 32767)
            x = (int32_t)(((int64_t)x * og) >> 15);
        y[i] += k_ceiling(x);
        h->spos += c->s_inc;
    }
}

static void k_run(const tb_kick_coef_t *c, tb_kick_hit_t *h, int32_t *y, unsigned n)
{
    int32_t tb[TB_BLOCK];
    uint32_t i, inc0, inc1, sw1 = h->t < c->hold ? h->sw : ((h->sw >> 1) * (c->g_sw >> 1)) >> 14,
             sw21 = ((h->sw2 >> 1) * (c->g_sw2 >> 1)) >> 14;
    int32_t og0 = 0, og1 = 0, e0, e1, k0 = h->clk, k1 = c->ck_g ? mulq16(h->clk, (uint32_t)c->ck_g)
                                              : (int32_t)(((uint32_t)h->clk >> 4) * 52305u >> 12);
    int32_t fmid = c->f_mid;
    if (h->sidx) {
        k_smp(c, h, y, n);
        return;
    }
    if (c->mid_g) {                                   /* (the mid moving: its place, block by block) */
        h->mf = c->mid_f1 + mulq16(h->mf - c->mid_f1, (uint32_t)c->mid_g);
        fmid = k_svf((uint32_t)tb_clamp(h->mf, 30, 12000));
    }
    inc0 = c->inc_end + (uint32_t)mulq16((int32_t)(c->inc_sw >> 1), h->sw) * 2u
         + (uint32_t)mulq16((int32_t)(c->inc_sw2 >> 1), h->sw2) * 2u;
    inc1 = c->inc_end + (uint32_t)mulq16((int32_t)(c->inc_sw >> 1), sw1) * 2u
         + (uint32_t)mulq16((int32_t)(c->inc_sw2 >> 1), sw21) * 2u;
    if (h->t < c->len && !h->choke && !h->rel) {     /* the body */
        if (c->env) {
            if (!h->lvl)
                h->lvl = 32767;
            e0 = h->lvl;
            e1 = (int32_t)(((uint32_t)h->lvl * c->g_dec) >> 16);
            h->lvl = e1 > 1 ? e1 : 1;
        } else {
            e0 = 32767 - (int32_t)((11468u * h->t) / c->len);
            e1 = h->t + n < c->len ? 32767 - (int32_t)((11468u * (h->t + n)) / c->len) : 21299;
        }
    } else {                                          /* the release (or the choke) */
        if (!h->rel) {
            h->rel = 1;
            if (!c->env)
                h->lvl = h->t < c->len ? 32767 - (int32_t)((11468u * h->t) / c->len) : 21299;
            else if (!h->lvl)
                h->lvl = 32767;
        }
        e0 = h->lvl;
        e1 = (int32_t)(((uint32_t)h->lvl * (h->choke ? c->g_choke : c->g_rel)) >> 16);
        h->lvl = e1 > 0 ? e1 : 1;
        /* (after the drive too: a kick driven hard stayed loud while its level fell, then stopped dead, a click; now
         * it fades out under the same release) */
        og0 = h->og ? h->og : 32767;
        og1 = (int32_t)(((uint32_t)og0 * (h->choke ? c->g_choke : c->g_rel)) >> 16);
        h->og = og1 > 0 ? og1 : 1;
        if (og1 < 40)
            h->pdead = 1;
    }
    if (h->pdead && (!c->t_wave || h->tdone)) {       /* (the punch and the tail both gone) */
        h->live = 0;
        return;
    }
    k_tail(c, h, tb, n);
    if (!h->at)
        h->at = c->a_tone;
    else if (h->t >= c->hold)
        h->at += (c->a_tone2 - h->at) >> 4;
    {
        int32_t dinc = (int32_t)(inc1 - inc0) / (int32_t)n, de = (e1 - e0) / (int32_t)n, dk = (k1 - k0) / (int32_t)n;
        uint32_t inc = inc0;
        int32_t e = e0, k = k0, og = og0, dog = (og1 - og0) / (int32_t)n;
        for (i = 0; i < n; i++) {
            int32_t s, x, nz, hi, sb;
            h->ph += inc;
            h->sph += inc;
            inc += (uint32_t)dinc;
            if (c->fm) {                              /* (FM: the body's own sine bent by another, as loud as it) */
                uint32_t idx = (uint32_t)(((int32_t)c->fm * e) >> 15);
                h->mph += (uint32_t)(((uint64_t)inc * c->fm_ratio) >> 4);
                s = tb_sin(h->ph + ((uint32_t)(tb_sin(h->mph) * (int32_t)idx) << 9));
            } else if (c->wave)
                s = tb_sin(h->ph);
            else {
                s = tb_tri(h->ph + 0x40000000u);
                s = (s * (49152 - (((s * s) >> 15) >> 1))) >> 15;
            }
            if (c->body) {
                s = ((s * (256 - (c->body >> 1))) >> 8) + ((tb_sin(h->bph) * (c->body >> 1)) >> 8);
                h->bph += c->inc_end;
            }
            x = (s * e) >> 15;
            sb = (tb_sin(h->sph) * e) >> 15;
            if (c->grit)                              /* (grit: noise riding the body) */
                x += ((((int32_t)tb_noise(&h->seed) >> 17) * c->grit >> 8) * e) >> 15;
            e += de;
            if (c->click) {
                if (c->ck_kind == KC_NOISE) {
                    nz = (int32_t)tb_noise(&h->seed) >> 17;
                    h->clo += (c->f_clk * h->cbd) >> 15;
                    hi = nz - h->clo - h->cbd;
                    h->cbd += (c->f_clk * hi) >> 15;
                } else {                              /* (TOK: a sine; CHIRP: one falling onto it) */
                    h->cph += h->ckinc;
                    if (c->ck_kind == KC_CHIRP)
                        h->ckinc -= (h->ckinc - (uint32_t)c->ck_inc) >> 7;
                    h->cbd = tb_sin(h->cph) >> 1;
                }
                x += (((h->cbd * (k >> 4)) >> 12) * c->click) >> 8;
            }
            k += dk;
            if (c->pre) {                             /* (the highs pushed: the mixer's EQ before its gain) */
                h->plo += ((c->a_pre >> 1) * (x - h->plo)) >> 14;
                x += ((x - h->plo) * c->pre) >> 6;
                x = tb_clamp(x, -(1 << 18), 1 << 18);
            }
            if (c->dmode == KD_CLIP) {                /* (clipped: towards a square, a tone of its own) */
                x = x > 0 ? tb_clamp((x * c->d1p) >> 4, 0, 31000)
                          : (tb_clamp((x * c->d1n) >> 4, -31000, 0) * c->asg) >> 15;
            } else if (c->dmode == KD_FOLD) {         /* (folded: past full scale it comes back down) */
                int32_t u = tb_clamp((x * (x > 0 ? c->d1p : c->d1n)) >> 4, -(1 << 20), 1 << 20);
                x = (tb_sin((uint32_t)u << 15) * 30000) >> 15;
            } else
            x = x > 0 ? tb_tanh(tb_clamp((x * c->d1p) >> 4, 0, 1 << 20))
                      : (tb_tanh(tb_clamp((x * c->d1n) >> 4, -(1 << 20), 0)) * c->asg) >> 15;
            h->hy = ((c->a_cut >> 1) * (h->hy + x - h->hx)) >> 14;
            h->hx = x;
            x = h->hy;
            if (c->mid) {
                h->mlo = tb_clamp(h->mlo + ((fmid * (h->mbd >> 2)) >> 13), -400000, 400000);
                hi = x - h->mlo - ((h->mbd >> 2) * 20480 >> 13);
                h->mbd = tb_clamp(h->mbd + ((fmid * (hi >> 2)) >> 13), -400000, 400000);
                x += (h->mbd >> 2) * c->mid >> 6;
            }
            x = tb_clamp(x, -(1 << 21), 1 << 21);
            x = (tb_tanh(tb_clamp((x * c->d2) >> 4, -(1 << 20), 1 << 20)) * c->n2) >> 15;
            h->t1 += ((h->at >> 1) * (x - h->t1)) >> 14;
            h->t2 += ((h->at >> 1) * (h->t1 - h->t2)) >> 14;
            h->dy = ((c->a_dc >> 1) * (h->dy + h->t2 - h->dx)) >> 14;
            h->dx = h->t2;
            if (c->sub) {
                h->shy = ((c->a_cut >> 1) * (h->shy + sb - h->shx)) >> 14;
                h->shx = sb;
                h->sdy = ((c->a_dc >> 1) * (h->sdy + h->shy - h->sdx)) >> 14;
                h->sdx = h->shy;
                sb = h->sdy;
            }
            if (c->t_wave && h->t + i > c->len) {     /* (a kick with a tail: its punch gone 8 ms after its length) */
                uint32_t g = h->t + i - c->len;
                int32_t pg = g >= 353u ? 0 : 32767 - (int32_t)(g * 92u);
                h->dy = h->dy, sb = (sb * pg) >> 15;
                x = ((((h->dy * 5) >> 3) * pg) >> 15) + ((sb * c->sub) >> 9);
            } else
                x = ((h->dy * 5) >> 3) + ((sb * c->sub) >> 9);
            if (og0) {                                /* (released: faded after the drive) */
                x = (int32_t)(((int64_t)x * og) >> 15);
                og += dog;
            }
            x += (tb[i] * 5) >> 3;
#ifdef TB_KICK_DEBUG
            if (tb_kick_dbg == 1) x = ((h->dy * 5) >> 3) + ((sb * c->sub) >> 9);
            if (tb_kick_dbg == 2) x = (tb[i] * 5) >> 3;
#endif
            if (c->sub)
                x = (x >> 1) * 32767 >> 14;
            y[i] += k_ceiling(x);
        }
    }
    h->t += n;
    h->sw = sw1;
    h->sw2 = sw21;
    h->clk = k1;
}

void tb_kick_render(tb_kick_t *k, const uint8_t *p, uint32_t beat, int32_t *out, unsigned n)
{
    unsigned i;
    uint8_t q[TB_KICK_NPARAM];
    if (k->amode != IS_ATELIER(p)) {                  /* (another kind of kick chosen: what rang is let go) */
        mode(k, IS_ATELIER(p));
        return;
    }
    if (k->amode) {
        tb_akick_render(&k->a, atelier(p, q), beat, out, n);
        return;
    }
    k_refresh(k, p, k->note, beat);
    for (i = 0; i < 2u; i++)
        if (k->h[i].live)
            k_run(&k->c, &k->h[i], out, n);
}
