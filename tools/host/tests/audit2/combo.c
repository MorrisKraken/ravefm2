/* SPDX-License-Identifier: GPL-3.0-only */
/* Audit 2: the engine alone (no screen), one configuration a run, set straight in the project: the six tracks playing
 * a bar, then the factors below; 8 s rendered; what comes out (full-scale samples, peak, offset) and the time each block
 * took on this PC (mean, max). Driven by combo.py (the covering arrays).
 *   combo F0 F1 ... F11      (each factor's level, see LEVELS in combo.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tb_engine.h"
#include "tb_drumx.h"

/* the board's few calls the engine makes (the PC's stand-ins) */
uint32_t fm1_millis(void) { return 0; }
unsigned tb_board_audio_load(int reset) { (void)reset; return 0; }
#include "tb_store.h"
/* (the sample recorder's flash: not played here) */
int tb_store_smp_prepare(uint32_t off, uint32_t n) { (void)off, (void)n; return 0; }
void tb_store_smp_write(uint32_t off, const uint8_t *d, uint32_t n) { (void)off, (void)d, (void)n; }

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }

int main(int argc, char **argv)
{
    int f[12] = {0}, i, t, s;
    static int32_t pcm[2 * 128];
    double tsum = 0, tmax = 0, peak = 0, dc = 0;
    long clips = 0, n = 0;
    for (i = 0; i < 12 && i + 1 < argc; i++)
        f[i] = atoi(argv[i + 1]);
    tb_engine_init();
    tb_engine_default_project();
    tb_proj.bpm10 = (uint16_t)(f[9] ? 2600 : 1400);
    for (t = 0; t < TB_TRACKS; t++) {
        tb_track_t *tr = &tb_proj.trk[t];
        tb_step_t *st = TB_TSTEPS(&tb_proj.pat[0], t);
        tb_proj.pat[0].len[t] = 16;
        memset(st, 0, 16 * sizeof *st);
        for (s = 0; s < 16; s++) {
            if (tr->machine == TB_M_KICK && s % 4 == 0)
                st[s].hits = 1, st[s].note = 36;
            else if (tr->machine == TB_M_BREAK && s % 2 == 0)
                st[s].hits = 1, st[s].note = (uint8_t)(60 + s / 2);
            else if (tr->machine == TB_M_909)
                st[s].hits = (uint16_t)((s % 4 == 2 ? 2u : 0u) | 8u | (s % 8 == 4 ? 4u : 0u) | (s == 0 ? 128u : 0u));
            else if (tr->machine != TB_M_KICK && tr->machine != TB_M_BREAK && tr->machine != TB_M_909)
                st[s].hits = 1, st[s].note = (uint8_t)(36 + 12 * (t - 2) + (s * 5) % 12), st[s].gate = 6, st[s].vel = 110;
        }
        tr->rev = f[3] ? 127 : 20, tr->dly = f[4] ? 127 : 0;
        if (f[10])
            tr->eq[0] = tr->eq[1] = tr->eq[2] = 127;
        if (t >= 3) {                                  /* the melodic tracks: the effects, the chords, the arp */
            tr->ins[0].type = (uint8_t)f[0], tr->ins[0].p[0] = tr->ins[0].p[1] = tr->ins[0].p[2] = 110;
            if (f[1])
                tb_proj.stk_mix[t][f[1] - 1] = 127;
            tr->chorus = f[2] ? 127 : 0;
            tr->chord = (uint8_t)(f[5] ? 3 : 0);
            tr->arp = (uint8_t)(f[6] ? 1 : 0);
        }
        if (tr->machine == TB_M_909) {
            tr->p[TBX_KIT] = (uint8_t)(f[11] ? 5 : 0);
            tr->p[TBX_MORPH] = (uint8_t)(f[11] ? 127 : 0);
            tr->p[TBX_DRIVE] = (uint8_t)(f[11] ? 127 : 0);
        }
    }
    tb_proj.scale = 1;
    tb_engine_play(1);
    for (i = 0; i < 2756; i++) {                       /* 8 s */
        double t0, d;
        int k;
        if (i == 689) {                               /* at 2 s: the event, the effect held */
            if (f[8] == 1) tb_engine_drop();
            if (f[8] == 2) tb_engine_riser();
            static const int PF[5] = {-1, TB_PF_WASH, TB_PF_ROLL32, TB_PF_KILLBASS, TB_PF_ECHO};
            if (f[7]) tb_engine_perform(1u << PF[f[7]], 2, 0);
            for (k = 3; k < 6; k++)                       /* keys played too: chords and arp on top of the steps */
                tb_engine_note((unsigned)k, (unsigned)(60 + k), 120);
        }
        if (i == 1800)
            for (k = 3; k < 6; k++)
                tb_engine_note((unsigned)k, (unsigned)(60 + k), 0);
        t0 = now_us();
        tb_engine_render(pcm, 128);
        d = now_us() - t0;
        if (i >= 100)                                 /* (the first blocks: the PC's caches, not the engine) */
            tsum += d, tmax = d > tmax ? d : tmax;
        for (k = 0; k < 256; k++) {
            int32_t v = pcm[k] >> 8;
            double a = v < 0 ? -v : v;
            if (v >= 32767 || v <= -32768) clips++;
            peak = a > peak ? a : peak;
            dc += v, n++;
        }
    }
    printf("{\"clips\": %ld, \"peak\": %.4f, \"dc\": %.5f, \"us_mean\": %.2f, \"us_max\": %.1f}\n", clips, peak / 32768.0,
           dc / n / 32768.0, tsum / 2656.0, tmax);
    return 0;
}
