/* SPDX-License-Identifier: GPL-3.0-only */
/* Audit 2: SCALE on its own - every scale x root x chord x inversion x voicing, on a melodic track (default STAB/HV,
 * or argv[1]), every key from the bottom to the top, held, chords stacked over notes still ringing; with the
 * sanitizers (scale_asan) or timed (scale). What comes out: blocks at full scale, the slowest block, the mean. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tb_engine.h"
uint32_t fm1_millis(void) { return 0; }
unsigned tb_board_audio_load(int reset) { (void)reset; return 0; }
#include "tb_store.h"
int tb_store_smp_prepare(uint32_t off, uint32_t n) { (void)off, (void)n; return 0; }
void tb_store_smp_write(uint32_t off, const uint8_t *d, uint32_t n) { (void)off, (void)d, (void)n; }
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int32_t pcm[2 * 128];
static long blocks, clips; static double tmax, tsum;
static char where[96], slow_where[5][96]; static double slow[5];
static void run(int nb)
{
    int i, k;
    for (i = 0; i < nb; i++) {
        double t0 = now_us(), d;
        tb_engine_render(pcm, 128);
        d = now_us() - t0; tsum += d; tmax = d > tmax ? d : tmax; blocks++;
        { int j, m = 0; for (j = 1; j < 5; j++) if (slow[j] < slow[m]) m = j;
          if (d > slow[m]) slow[m] = d, strcpy(slow_where[m], where); }
        for (k = 0; k < 256; k++)
            if ((pcm[k] >> 8) >= 32767 || (pcm[k] >> 8) <= -32768) clips++;
    }
}
int main(int argc, char **argv)
{
    unsigned t = argc > 1 ? (unsigned)atoi(argv[1]) : 5u, sc, root, ch, inv, vo, n;
    long combos = 0;
    tb_engine_init();
    tb_engine_default_project();
    for (sc = 0; sc < TB_NSCALE; sc++)
        for (root = 0; root < 12; root += (sc ? 5 : 11))
            for (ch = 0; ch < TB_NCHORD; ch++)
                for (inv = 0; inv < 4; inv++)
                    for (vo = 0; vo < TB_NVOICING; vo++) {
                        tb_track_t *tr = &tb_proj.trk[t];
                        tb_proj.scale = (uint8_t)sc, tb_proj.root = (uint8_t)root;
                        tr->chord = (uint8_t)ch, tr->inversion = (uint8_t)inv, tr->voicing = (uint8_t)vo;
                        tb_engine_panic(t);
                        for (n = (combos % 7); n < 128; n += 9) {   /* (bottom to top; the notes overlap) */
                            snprintf(where, sizeof where, "scale %u root %u chord %u inv %u voicing %u note %u", sc, root, ch, inv, vo, n);
                            tb_engine_note(t, n, 127);
                            run(1);
                            if (n >= 9)
                                tb_engine_note(t, n - 9, 0);
                        }
                        run(4);
                        combos++;
                    }
    tb_engine_panic(t);
    run(40);
    printf("track %u, combinations %ld, blocks %ld, full-scale samples %ld, slowest block %.1f us, mean %.1f us\n", t, combos,
           blocks, clips, tmax, tsum / blocks);
    { int j; for (j = 0; j < 5; j++) printf("  slow %.1f us: %s\n", slow[j], slow_where[j]); }
    return 0;
}
