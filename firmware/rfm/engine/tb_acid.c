/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: ACID (tb_acid.h). The ladder: four one-poles in a row, the last fed back (RESO), the input through a
 * soft clip (what makes it growl), run twice a sample (88.2 kHz) so that it stays stable and in tune up high. */
#include "tb_acid.h"
#include "tb_dsp.h"
#include <string.h>

const char *const TBA_WAVE_NAME[2] = {"SAW", "SQUARE"};
/*                                       WAVE CUT RES ENV DEC ACC SLD DRV TUNE */
const tb_acid_sound_t TBA_SOUND[] = {
    {"ACID HOUSE",    {0, 40, 95, 70, 50, 70, 50, 20, 64}},
    {"SQUELCH",       {0, 28,118, 96, 40, 90, 45, 40, 64}},
    {"ACID SQUARE",    {1, 36,100, 80, 46, 75, 50, 30, 64}},
    {"ROTTERDAM 303", {0, 34,112, 90, 36,100, 40, 95, 64}},
    {"HARDCORE ACID", {0, 46,120,110, 30,110, 35,120, 64}},
    {"RUBBER",        {1, 20, 80, 60, 70, 60, 70, 10, 64}},
    {"DEEP 303",      {0, 18, 70, 50, 90, 40, 60,  5, 64}},
    {"ACID LEAD",     {0, 56,110, 84, 44, 80, 30, 60, 76}},
    {"SCREAM 303",    {1, 52,124,120, 26,120, 30,110, 64}},
    {"SLIDE GUM",     {0, 30, 90, 70, 60, 70,100, 30, 64}},
    {"TECHNO 303",    {0, 24,104, 66, 56, 64, 45, 50, 64}},
    {"DIST ACID",     {1, 40,116, 96, 34, 96, 40,127, 64}},
};
const unsigned TBA_NSOUND = sizeof TBA_SOUND / sizeof *TBA_SOUND;

void tb_acid_init(tb_acid_t *a) { memset(a, 0, sizeof *a); }

void tb_acid_sound(uint8_t *p, unsigned i)
{
    const tb_acid_sound_t *s = &TBA_SOUND[i % TBA_NSOUND];
    memcpy(p, s->p, TBA_NPARAM);
    p[24] = 0, p[25] = 127, p[26] = 127, p[27] = 10;  /* the gate: on while the note is, a short release */
}

void tb_acid_on(tb_acid_t *a, const uint8_t *p, unsigned note, unsigned vel)
{
    int32_t t = (int32_t)note << 16;
    if (a->gate && a->live) {                          /* held into: the slide (no new envelope) */
        a->target = t, a->note = (uint8_t)note, a->sliding = 1;
        return;
    }
    a->pitch = a->target = t, a->sliding = 0;
    a->note = (uint8_t)note, a->gate = 1;
    a->accent = (uint8_t)(vel >= TBA_ACCENT_VEL && p[TBA_ACCENT]);
    a->fenv = 32767;
    if (a->accent)
        a->aenv = 32767;
    if (!a->live)
        memset(a->y, 0, sizeof a->y), a->amp.lvl = 0;
    a->live = 1;
    tb_env_on(&a->amp);
}

void tb_acid_off(tb_acid_t *a, unsigned note)
{
    if (a->gate && a->note == note)
        a->gate = 0, tb_env_off(&a->amp);
}

int32_t tb_acid_env(const tb_acid_t *a, unsigned *stage)
{
    *stage = a->amp.stage;
    return a->amp.lvl;
}

static inline int32_t blep(uint32_t t, uint32_t dt)
{
    if (!dt)                                          /* (no movement: no step to smooth; never a division by 0) */
        return 0;
    if (t < dt) {
        int32_t x = 32768 - (int32_t)(((uint64_t)t << 15) / dt);
        return -((x * x) >> 15);
    }
    if (t > 0u - dt) {
        int32_t x = (int32_t)(((uint64_t)(t - (0u - dt)) << 15) / dt);
        return (x * x) >> 15;
    }
    return 0;
}
static inline int32_t saw(uint32_t ph, uint32_t dt) { return (int32_t)(ph >> 16) - 32768 - blep(ph, dt); }

#define SUB 16u
void tb_acid_render(tb_acid_t *a, const uint8_t *p, int32_t *out, unsigned n, int32_t lfo_pitch, int32_t lfo_cut)
{
    unsigned o0, i, k;
    int32_t kd, ks, kslide, res, drive, acc_amt = p[TBA_ACCENT] * 258;
    if (!a->live)
        return;
    tb_envk(&a->ka, &p[24]);
    /* per sub-block (16 samples): the filter's decay 0.15 .. 2.5 s (an accent's: 0.15 s), the accent's climb ~50 ms */
    kd = 32767 - (int32_t)(((int64_t)SUB * 32767 * 1000) / (44100 * (150 + (int32_t)p[TBA_DECAY] * 2350 / 127)));
    ks = (int32_t)(SUB * 32767u / 2205u);
    kslide = (int32_t)(SUB * 32767u / (441u + p[TBA_SLIDE] * 60u));           /* 10 .. 180 ms */
    res = (int32_t)p[TBA_RESO] * 4300 / 127;                                    /* Q10: 0 .. 4.2 */
    drive = 4096 + (int32_t)p[TBA_DRIVE] * 150;
    for (o0 = 0; o0 < n; o0 += SUB) {
        unsigned m = n - o0 < SUB ? n - o0 : SUB;
        uint32_t inc, hz;
        int32_t oct, g, semi;
        if (a->sliding)
            a->pitch += (int32_t)(((int64_t)(a->target - a->pitch) * kslide) >> 15);
        else
            a->pitch = a->target;
        a->fenv = (int32_t)(((int64_t)a->fenv * (a->accent ? 32767 - 32767 * (int32_t)SUB / 6615 : kd)) >> 15);
        a->aenv -= a->aenv * (int32_t)SUB / 6615;
        a->sweep += (int32_t)(((int64_t)(a->aenv - a->sweep) * ks) >> 15);
        semi = a->pitch + ((int32_t)p[TBA_TUNE] - 64) * 65536 * 12 / 64 + lfo_pitch;
        inc = (uint32_t)(((uint64_t)42852281u * tb_exp2((semi - (69 << 16)) / 12)) >> 16);
        /* the cutoff: 40 Hz .. 10 kHz, + ENV MOD up to 4.5 octaves, + the accent's climb up to 2.5 */
        oct = (int32_t)p[TBA_CUTOFF] * 8 * 65536 / 127 + (int32_t)(((int64_t)p[TBA_ENVMOD] * 295000 / 127 * a->fenv) >> 15) +
              (int32_t)(((int64_t)acc_amt * a->sweep >> 15) * 163840 >> 15) + lfo_cut;
        hz = (uint32_t)((40ull * tb_exp2(tb_clamp(oct, -(1 << 16), 9 << 16))) >> 16);
        if (hz > 16000u)
            hz = 16000u;
        g = (int32_t)((uint64_t)hz * 2334u / 1000u);   /* 2 pi f / 88200, Q15 */
        if (g > 26000)
            g = 26000;
        for (i = 0; i < m; i++) {
            int32_t x = p[TBA_WAVE] ? (saw(a->ph, inc) - saw(a->ph + 0x80000000u, inc)) / 2 : saw(a->ph, inc), y;
            a->ph += inc;
            x = x * 3 / 4;
            for (k = 0; k < 2u; k++) {                 /* the ladder, twice */
                int32_t in = tb_tanh(x - (int32_t)(((int64_t)a->y[3] * res) >> 10));
                a->y[0] += (int32_t)(((int64_t)(in - a->y[0]) * g) >> 15);
                a->y[1] += (int32_t)(((int64_t)(a->y[0] - a->y[1]) * g) >> 15);
                a->y[2] += (int32_t)(((int64_t)(a->y[1] - a->y[2]) * g) >> 15);
                a->y[3] += (int32_t)(((int64_t)(a->y[2] - a->y[3]) * g) >> 15);
            }
            y = a->y[3] + (int32_t)(((int64_t)a->y[3] * res) >> 11);   /* (the level lost to RESO, given back) */
            y = tb_tanh((int32_t)(((int64_t)y * drive) >> 12));
            tb_env_step(&a->amp, &a->ka);
            y = (int32_t)(((int64_t)y * (a->amp.lvl >> 15)) >> 15);
            if (a->accent)
                y += y / 3;
            out[o0 + i] += y * 3 / 4;
        }
    }
    if (!a->amp.stage)
        a->live = 0;
}
