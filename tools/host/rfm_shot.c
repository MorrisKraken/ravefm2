/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2 on the PC: the board (fm1.h) in memory, RAVE FM's whole code as the FM-1 runs it (the sound renders 128
 * frames, then the control tick, as the main loop does), driven by a script; screenshots as PPM.
 *   rfm_shot OUTDIR "B11 W300 K5 W100 Sname ..."
 *   B<n> press button n | H<n>/U<n> hold / let go button n | K<n> tap key n | P<n>/R<n> hold / let go key n
 *   E<e>,<d> turn encoder e by d | W<ms> wait | S<name> screenshot | M<v> MASTER 0..1023 | G<name>,<n>,<ms> n frames
 *   every ms ms (an animation) | C<n> palette n */
#include "fm1.h"
#include "tb_app.h"
#include "tb_engine.h"
#include "rfm_gfx.h"
#include "tb_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_ZERO_STATS
#define CALLGRIND_DUMP_STATS
#endif

static uint16_t fb[240][240];
static uint32_t frames, buttons, keys, master = 700;
static int16_t enc[7];
/* (the demo film: RFM_FILM=dir - the screen 30 times a second (screen.raw, 240 x 240 RGB565 each), what is pressed,
 * turned and lit at each (state.txt), the session's marks (marks.txt: N<text>) */
static FILE *film_scr, *film_st, *film_mk;
static uint32_t film_next, film_n, film_enc, film_on = 1;
static int film_dir[7];
static uint8_t leds[41];
static int32_t pcm[2 * 128];
static const char *outdir = ".";
static double peak;

void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *s)
{
    unsigned i;
    s->keys = keys, s->buttons = buttons, s->knob = (uint16_t)master, s->battery = 900;
    for (i = 0; i < 7u; i++) {
        if (enc[i])
            film_enc |= 1u << i, film_dir[i] = enc[i];
        s->encoders[i] = enc[i], enc[i] = 0;
    }
}
void fm1_led_set(unsigned key, int on) { if (key < 41u) leds[key] = (uint8_t)(on != 0); }
static uint8_t glows[41];
void rfm_host_cut(const char *lab, const char *val, int what)   /* (rfm_screen.c: a card's text cut) */
{
    static char seen[400][40]; static int n;
    char k[40]; int i;
    snprintf(k, sizeof k, "%s:%s", what ? "VALUE" : "NAME", what ? val : lab);
    for (i = 0; i < n; i++) if (!strcmp(seen[i], k)) return;
    if (n < 400) { strcpy(seen[n++], k); fprintf(stderr, "CUT %s (%s)\n", k, lab); }
}
void rfm_host_knob(unsigned k, int dlt, int v, int mn, int mx) { if (getenv("RFM_KNOBLOG")) fprintf(stderr, "KNOB %u %+d from %d [%d..%d]\n", k, dlt, v, mn, mx); }
void rfm_host_hud(const char *t, const char *v) { fprintf(stderr, "HUD %s | %s\n", t ? t : "", v ? v : ""); }
void fm1_led_glow(unsigned key, int on) { if (key < 41u) glows[key] = (uint8_t)(on != 0); }
uint32_t fm1_millis(void) { return (uint32_t)((uint64_t)frames * 1000u / 44100u); }
void fm1_audio_stats_read(struct fm1_audio_stats *s) { memset(s, 0, sizeof *s); }
void fm1_screen_init(void) {}
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    unsigned i, j;
    for (j = y; j < y + h && j < 240; j++)
        for (i = x; i < x + w && i < 240; i++)
            fb[j][i] = c;
}
static unsigned long sent_px;                     /* (the pixels sent to the panel: P prints them) */
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px, size_t count)
{
    unsigned i, j;
    sent_px += (unsigned long)w * h;
    if (x + w > 240 || y + h > 240 || count < (size_t)w * h)
        return -1;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            fb[y + j][x + i] = px[j * w + i];
    return 0;
}
int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t *px, size_t count)
{
    return fm1_screen_blit(x, y, w, h, px, count);
}
int fm1_screen_pending(void) { return 0; }
void fm1_screen_blit_be(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px, unsigned stride)
{
    unsigned i, j;
    sent_px += (unsigned long)w * h;
    for (j = 0; j < h && y + j < 240; j++)
        for (i = 0; i < w && x + i < 240; i++) {
            uint16_t v = px[j * stride + i];
            fb[y + j][x + i] = (uint16_t)(v >> 8 | v << 8);
        }
}
void fm1_screen_sync(void) {}
int fm1_midi_send(const uint8_t *m, size_t n) { (void)m, (void)n; return 0; }
#define WFLASH (292u * 1024u)
static uint8_t flash[WFLASH];
uint32_t fm1_flash_size(void) { return WFLASH; }
const uint8_t *fm1_flash_ptr(uint32_t o) { return o < WFLASH ? flash + o : 0; }
int fm1_flash_read(uint32_t o, void *d, size_t n)
{
    if (o > WFLASH || n > WFLASH - o) return FM1_FLASH_INVALID;
    memcpy(d, flash + o, n);
    return FM1_FLASH_OK;
}
int fm1_flash_write(uint32_t o, const void *d, size_t n)
{
    size_t i;
    if (o > WFLASH || n > WFLASH - o) return FM1_FLASH_INVALID;
    for (i = 0; i < n; i++) flash[o + i] &= ((const uint8_t *)d)[i];
    return FM1_FLASH_OK;
}
int fm1_flash_erase(uint32_t o)
{
    if (o % FM1_FLASH_SECTOR_BYTES || o >= WFLASH) return FM1_FLASH_INVALID;
    memset(flash + o, 0xFF, FM1_FLASH_SECTOR_BYTES);
    return FM1_FLASH_OK;
}

#include <time.h>
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static double au_sum, au_max, ui_sum, ui_max; static unsigned au_n, ui_n;   /* (T: the time each part took, on this PC) */
static FILE *wav;                                   /* (RFM_WAV=file: the sound written, 16-bit stereo raw) */
/* (21 Nov 2026) the FM-1's second core, RFM_MC=1: the engine's reverb and delay handed to it, a block behind - here
 * at once, or (RFM_THREADS) in a thread of its own, as on the FM-1 for ThreadSanitizer */
static int mc_env = -1;
/* (21 Nov 2026) RFM_PROF=1: the engine's parts timed with this PC's clock as TIMER4 (24 ticks a microsecond; F prints
 * them) - the PC's times, to see the measure works and compare versions: never the FM-1's */
static volatile uint32_t mc_ticks;
static int prof_env(void) { static int e = -1; if (e < 0) e = getenv("RFM_PROF") != 0; return e; }
uint32_t tb_engine_ticks(void) { return prof_env() ? (uint32_t)(uint64_t)(now_us() * 24.0) : 0u; }
uint32_t tb_engine_block_ticks(void) { int d = prof_env() ? atoi(getenv("RFM_PROF")) : 0; return d > 0 ? 2902u * 24u / (unsigned)d : 0u; }   /* (RFM_PROF=d: a block d times shorter - the PC is faster) */
uint32_t tb_engine_c1_ticks(void) { return mc_ticks; }
static void mc_timed(void (*fn)(uint32_t), uint32_t arg) { uint32_t t = tb_engine_ticks(); fn(arg); mc_ticks += tb_engine_ticks() - t; }
int tb_engine_mc_avail(void)
{
    if (mc_env < 0)
        mc_env = getenv("RFM_MC") != 0;
    return mc_env;
}
#ifdef RFM_THREADS
#include <pthread.h>
static pthread_t mc_th;
static void (*mc_fn)(uint32_t);
static uint32_t mc_arg;
static int mc_busy_;
static void *mc_main(void *a) { (void)a; mc_timed(mc_fn, mc_arg); return 0; }
int tb_engine_mc_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!tb_engine_mc_avail())
        return 0;
    mc_fn = fn, mc_arg = arg, mc_busy_ = 1;
    pthread_create(&mc_th, 0, mc_main, 0);
    return 1;
}
int tb_engine_mc_wait(unsigned pct)
{
    (void)pct;
    if (mc_busy_)
        pthread_join(mc_th, 0), mc_busy_ = 0;
    return 0;
}
int tb_engine_mc_busy(void) { return tb_engine_mc_wait(0), 0; }
#else
/* (RFM_MC_LATE=n: every n-th job "late" - not run when handed over, its wait answers late; it runs, done, at the next
 *  look: the engine's late paths tried) */
static void (*mc_fn)(uint32_t);
static uint32_t mc_arg, mc_count;
static int mc_pending;
int tb_engine_mc_run(void (*fn)(uint32_t), uint32_t arg)
{
    static int late_n = -1;
    if (!tb_engine_mc_avail())
        return 0;
    if (late_n < 0)
        late_n = getenv("RFM_MC_LATE") ? atoi(getenv("RFM_MC_LATE")) : 0;
    if (mc_pending) {
        fprintf(stderr, "MC ERROR: a job handed over while one is pending\n");
        abort();
    }
    if (late_n > 0 && ++mc_count % (uint32_t)late_n == 0) {
        mc_fn = fn, mc_arg = arg, mc_pending = 1;
        return 1;
    }
    mc_timed(fn, arg);
    return 1;
}
int tb_engine_mc_wait(unsigned pct) { (void)pct; return mc_pending ? 1 : 0; }
int tb_engine_mc_busy(void)
{
    if (mc_pending)
        mc_pending = 0, mc_timed(mc_fn, mc_arg);
    return 0;
}
#endif

#ifdef RFM_THREADS
/* (audit 2) the FM-1's order: the sound in an interrupt that cuts into the main loop anywhere. Here: the sound in its
 * own thread, rendering block after block, the control tick in the main thread at the same time; ThreadSanitizer and
 * AddressSanitizer then see what the one-after-the-other run cannot */
#include <pthread.h>
#include <sched.h>
static volatile int au_stop, au_on;
static pthread_t au_th;
static void *au_loop(void *x)
{
    static int32_t apcm[2 * 128];
    unsigned i;
    (void)x;
    while (!au_stop) {
        tb_engine_render(apcm, 128);
        if (wav) {
            int16_t o[256];
            for (i = 0; i < 256; i++) {
                int32_t v = apcm[i] >> 8;
                o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            fwrite(o, 2, 256, wav);
        }
        __atomic_fetch_add(&frames, 128u, __ATOMIC_RELAXED);
        sched_yield();
    }
    return 0;
}
static void run(unsigned ms)
{
    uint32_t target;
    if (!au_on) {
        const char *w = getenv("RFM_WAV");
        if (w)
            wav = fopen(w, "wb");
        au_on = 1;
        pthread_create(&au_th, 0, au_loop, 0);
    }
    target = __atomic_load_n(&frames, __ATOMIC_RELAXED) + ms * 441u / 10u;
    while (__atomic_load_n(&frames, __ATOMIC_RELAXED) < target)
        tb_app_tick();
}
static void au_end(void) { if (au_on) { au_stop = 1; pthread_join(au_th, 0); } }
#else
static void au_end(void) {}
static void run(unsigned ms)
{
    unsigned n = ms * 441u / 1280u + 1u, i;
    static int wav_tried;
    if (!wav_tried) {
        const char *w = getenv("RFM_WAV");
        wav_tried = 1;
        if (w)
            wav = fopen(w, "wb");
    }
    while (n--) {
        double t0 = now_us(), t1;
        tb_engine_render(pcm, 128);
        t1 = now_us() - t0, au_sum += t1, au_n++;
        if (t1 > au_max) au_max = t1;
        if (wav && film_on) {
            int16_t o[256];
            for (i = 0; i < 256; i++) {
                int32_t v = pcm[i] >> 8;
                o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            fwrite(o, 2, 256, wav);
        }
        for (i = 0; i < 256; i++) {
            double v = pcm[i] / 8388608.0;
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
        frames += 128;
        t0 = now_us();
        tb_app_tick();
        if (!film_on)
            film_next = frames + 128u;                 /* (paused: a frame as soon as it comes back) */
        while (film_scr && film_on && frames >= film_next) {      /* (a film frame due: the screen as it is, the panel's state) */
            unsigned k;
            fwrite(fb, 2, 240 * 240, film_scr);
            fprintf(film_st, "%u %x %x ", film_n, buttons, keys);
            for (k = 0; k < 41u; k++)
                fputc('0' + (leds[k] | glows[k] << 1), film_st);
            fprintf(film_st, " %x", film_enc);
            for (k = 0; k < 7u; k++)
                fprintf(film_st, " %d", film_dir[k]);
            fputc('\n', film_st);
            film_enc = 0, film_n++, film_next += 1470u;
        }
        t1 = now_us() - t0;
        if (t1 > 20.0) {                              /* (a tick that drew) */
            ui_sum += t1, ui_n++;
            if (t1 > ui_max) ui_max = t1;
        }
    }
}
#endif
static void save(const char *name)
{
    char p[512];
    FILE *f;
    unsigned x, y;
    snprintf(p, sizeof p, "%s/%s.ppm", outdir, name);
    f = fopen(p, "wb");
    fprintf(f, "P6 240 240 255\n");
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            uint16_t c = fb[y][x];
            unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}
uint32_t rfm_pf_force;
int rfm_dbg_sc;
int main(int argc, char **argv)
{
    int i;
    char *tok;
    if (argc < 3)
        return 2;
    if (getenv("RFM_HEAVY")) {                       /* (audit 2: the FM-1's overload levels forced, as its interrupt
                                                      *  sets them - tb_heavy 1 or 2) */
        extern volatile uint8_t tb_heavy;
        tb_heavy = (uint8_t)atoi(getenv("RFM_HEAVY"));
    }
    outdir = argv[1];
    if (getenv("RFM_FILM")) {
        char p[512];
        snprintf(p, sizeof p, "%s/screen.raw", getenv("RFM_FILM")); film_scr = fopen(p, "wb");
        snprintf(p, sizeof p, "%s/state.txt", getenv("RFM_FILM")); film_st = fopen(p, "w");
        snprintf(p, sizeof p, "%s/marks.txt", getenv("RFM_FILM")); film_mk = fopen(p, "w");
    }
    memset(flash, 0xFF, sizeof flash);
    tb_app_init();
    for (tok = strtok(argv[2], " "); tok; tok = strtok(0, " ")) {
        char *a = tok;
        int v = atoi(a + 1);
        switch (a[0]) {
        case 'B': buttons |= 1u << v; run(60); buttons &= ~(1u << v); run(60); break;
        case 'H': buttons |= 1u << v; run(40); break;
        case 'U': buttons &= ~(1u << v); run(40); break;
        case 'K': keys |= 1u << v; run(80); keys &= ~(1u << v); run(80); break;
        case 'P': keys |= 1u << v; run(40); break;
        case 'R': keys &= ~(1u << v); run(40); break;
        case 'E': { int e = v, d = atoi(strchr(a, ',') + 1); if (e >= 0 && e < (int)(sizeof enc / sizeof enc[0])) enc[e] = (int16_t)(enc[e] + d); run(40); break; }   /* (a session's encoder past the 7: none) */
        case 'W': run((unsigned)v); break;
        case 'p': {                                     /* (p: every preset, for the manual) */
            extern const tb_kick_preset_t TB_KICK_PRESETS[]; extern const unsigned TB_KICK_NPRESETS;
            extern const tb_syn_sound_t TBS_SOUND[]; extern const unsigned TBS_NSOUND;
            extern const tb_acid_sound_t TBA_SOUND[]; extern const unsigned TBA_NSOUND;
            unsigned i;
            for (i = 0; i < TB_KICK_NPRESETS; i++) fprintf(stderr, "KICK|%s\n", TB_KICK_PRESETS[i].name);
            for (i = 0; i < TBS_NSOUND; i++) fprintf(stderr, "SYN|%s|%s\n", TBS_SOUND[i].family, TBS_SOUND[i].name);
            for (i = 0; i < TBA_NSOUND; i++) fprintf(stderr, "ACID|%s\n", TBA_SOUND[i].name);
            for (i = 0; i < tb_brk_count(); i++) fprintf(stderr, "BRK|%s\n", tb_brk_name(i));
            break;
        }
        case 'V': {                                     /* (V<t>: track t's steps, as written) */
            unsigned st, t = (unsigned)v;
            fprintf(stderr, "TRACK %u len %u speed %u:", t, TB_LEN(t), tb_proj.trk[t].speed);
            for (st = 0; st < TB_TCAP(t); st++)
                if (TB_HITS(TB_STEP(t, st).hits))
                    fprintf(stderr, " %u:%u/g%u", st, TB_STEP(t, st).note, TB_STEP(t, st).gate);
            fprintf(stderr, "\n");
            break;
        }
        case 'J': {                                     /* (J<t>,<s>: on until track t's sequencer reaches step s) */
            unsigned s = (unsigned)atoi(strchr(a, ',') + 1), guard = 200000u;
            while (tb_engine_step((unsigned)v, 0) == s && guard--) run(0);
            while (tb_engine_step((unsigned)v, 0) != s && guard--) run(0);
            fprintf(stderr, "JUMP %u blocks\n", 200000u - guard);
            break;
        }
        case 'A': {                                     /* (each track's level and activity, as the screen gets them) */
            static tb_view_t vw;
            unsigned t;
            tb_engine_view(&vw, 0);
            fprintf(stderr, "LEVELS");
            for (t = 0; t < TB_TRACKS; t++)
                fprintf(stderr, " %u:%u/%u", t, vw.lane[t].level, vw.lane[t].active);
            fprintf(stderr, "\n");
            break;
        }
        case 'X': { extern void tb_engine_debug(void); tb_engine_debug(); break; }
        case 'F': {                                     /* (RFM_PROF: the parts' busiest / mean since the last F, %) */
            static const char *const NM[TB_PROF_N] = {"C0", "C1", "WAIT", "TRK", "SYN", "INS", "PHA", "CHO", "DLY", "REV", "CHN", "RIS", "MST"};
            uint8_t mx[TB_PROF_N], av[TB_PROF_N];
            unsigned k;
            tb_engine_prof(mx, av);
            fprintf(stderr, "PROF");
            for (k = 0; k < TB_PROF_N; k++)
                fprintf(stderr, " %s %u/%u", NM[k], mx[k], av[k]);
            fprintf(stderr, "\n");
            break;
        }
        case 'Z': CALLGRIND_ZERO_STATS; break;          /* (callgrind: from here) */
        case 'D': CALLGRIND_DUMP_STATS; break;          /* (callgrind: to here) */
        case 'T': fprintf(stderr, "time: audio %.1f us a block (max %.1f) | screen %.0f us a frame (max %.0f), %u frames\n", au_n ? au_sum / au_n : 0, au_max, ui_n ? ui_sum / ui_n : 0, ui_max, ui_n); au_sum = au_max = ui_sum = ui_max = 0; au_n = ui_n = 0; break;
        case 'L': { int k, n = 0; for (k = 0; k < 41; k++) n += glows[k]; fprintf(stderr, "glow: %d of 41 lit\n", n); break; }
        case 'M': master = (uint32_t)v; break;
        case 'I': rfm_dbg_sc = v; break;
        case 'O': film_on = (uint32_t)v; break;        /* (the film paused / going on: O0 / O1) */
        case 'N': if (film_mk) fprintf(film_mk, "%u %s\n", film_n, a + 1); break;   /* (the film's mark here) */
        case 'Y': rfm_pf_force = (uint32_t)v; run(40); break;   /* (the live effects held: their bits; Y0 lets go) */
        case 'Q': { static uint32_t t0; fprintf(stderr, "panel: %lu px in %u ms = %lu KB/s (SPI 12 MHz: ~1500 KB/s)\n", sent_px, (unsigned)(fm1_millis() - t0), (unsigned long)(sent_px * 2ul / ((fm1_millis() - t0) ? (fm1_millis() - t0) : 1u))); sent_px = 0; t0 = fm1_millis(); break; }
        case 'C': tb_proj.palette = (uint8_t)v; gfx_palette((unsigned)v); run(60); break;   /* the palette */
        case 'S': run(40); save(a + 1); break;
        case 'G': {
            char name[64]; int n, ms, k;
            char *c1 = strchr(a, ','), *c2 = c1 ? strchr(c1 + 1, ',') : 0;
            if (!c1 || !c2) break;
            snprintf(name, sizeof name, "%.*s", (int)(c1 - a - 1), a + 1);
            n = atoi(c1 + 1), ms = atoi(c2 + 1);
            for (k = 0; k < n; k++) {
                char nm[96];
                run((unsigned)ms);
                snprintf(nm, sizeof nm, "%s_%03d", name, k);
                save(nm);
            }
            break;
        }
        }
    }
    au_end();
    fprintf(stderr, "peak %.3f\n", peak);
    (void)i;
    return 0;
}
