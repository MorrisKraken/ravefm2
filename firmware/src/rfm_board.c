/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the board as RAVE FM's code sees it (rfm/include/fm1.h), served by the platform: the panel (fm1_input.h's
 * scan, TIMER5), the LEDs, the screen (lcd.c), MIDI (usb.c's rings, the TRS jack), RAVE FM's own flash area, and the
 * audio interrupt that runs RAVE FM's sound (tb_engine_render) and applies the MASTER knob.
 * Included by ravefm.c after the platform's files (one unit with them). */
#include "../rfm/include/fm1.h"

void tb_engine_render(int32_t *out, unsigned n);    /* rfm/engine/tb_engine.c: n stereo frames, Q23 (-3 dB ceiling) */
void tb_engine_play(int on);
extern volatile uint8_t tb_heavy;                   /* rfm/engine/tb_engine.c: 1, 2 the FM-1 near its time (tb_synth.c) */
void tb_app_init(void);
void tb_app_tick(void);

/* ------------------------------------------------------------ the audio --- */
/* Every half buffer (128 frames, 2.9 ms): RAVE FM renders it in place, then the MASTER gain (Q8, glides a step a
 * frame: no zipper). Its top, 181/256 (-3 dB), puts the engine's -3 dB ceiling at -6 dBFS on the codec: the
 * headroom the FM-1's output stage plays clean with (the platform's own level). TIMER5 outranks this interrupt: its
 * scan nests in the render, the time it takes there is handed back (t5_nested_ticks) so the load is the render's. */
#define RFM_GAIN_MAX 181u
#define HALF_TICKS ((HALF_FRAMES * 1000000u / FS) * FM1_TICKS_PER_US)   /* a half's time in TIMER4 ticks */
static volatile uint8_t rfm_in_audio;               /* in the audio interrupt (TIMER5 nests: rfm_main.c) */
static volatile uint32_t t5_nested_ticks;           /* TIMER4 ticks TIMER5 spent nested in it */
static volatile uint32_t rfm_gain_target;           /* 0..RFM_GAIN_MAX, from the knob (tb_board_controls) */
static uint32_t rfm_gain;                           /* the gain now (the interrupt's) */
static volatile uint8_t rfm_audio_on;               /* the engine is ready: render (else silence) */
static uint32_t rfm_last_half = 2u;
static volatile struct fm1_audio_stats rfm_stats;
static uint8_t rfm_over;                            /* blocks in a row that took (nearly) all their time */
static uint16_t rfm_heavy_hold;                     /* blocks before tb_heavy may step down */
static volatile uint32_t rfm_skipped;               /* blocks let go silent to keep the FM-1 answering */
static volatile uint32_t rfm_blk_t0;                /* when this block's render began (TIMER4): the second core's deadline */

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    rfm_in_audio = 1;                               /* first: TIMER5 nests from here on */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    rfm_blk_t0 = t0;
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), i, used, g = rfm_gain, gt = rfm_gain_target;
        int32_t *o = &abuf[half * HALF_WORDS];
        fm1_audio_ack_half();                       /* first: a flip during a late render stays pending */
        if (half == rfm_last_half)
            rfm_stats.overruns++;                   /* the other half was never rendered */
        rfm_last_half = half;
        if (rfm_audio_on && rfm_over >= 2u) {      /* (audit 2) two blocks in a row past their time: this one is let
                                                     * go, silent, and its time handed to the main loop - a crackle, but
                                                     * the panel, the screen and the watchdog's kick go on: never the
                                                     * frozen screen that the watchdog restarts 8 s later */
            for (i = 0; i < HALF_WORDS; i++)
                o[i] = 0;
            rfm_over = 0, rfm_skipped++;
        } else if (rfm_audio_on) {
            tb_engine_render(o, HALF_FRAMES);
            for (i = 0; i < HALF_FRAMES; i++) {     /* |Q23| * 181 < 2^31: no 64-bit product */
                if (g < gt)
                    g++;
                else if (g > gt)
                    g--;
                o[2u * i] = (o[2u * i] * (int32_t)g) >> 8;
                o[2u * i + 1u] = (o[2u * i + 1u] * (int32_t)g) >> 8;
            }
            rfm_gain = g;
        } else
            for (i = 0; i < HALF_WORDS; i++)
                o[i] = 0;
        used = fm1_ticks() - t0 - t5_nested_ticks;
        if (fm1_audio_free_half() != half)
            rfm_stats.short_intervals++;            /* the DMA moved on while it rendered: late */
        if (used > rfm_stats.peak_ticks)
            rfm_stats.peak_ticks = used;
        {                                           /* (audit 2) the time it really took, TIMER5 inside it too: what
                                                     * the main loop did not get */
            uint32_t wall = fm1_ticks() - t0;
            rfm_over = wall >= HALF_TICKS / 100u * 97u ? (uint8_t)(rfm_over + 1u) : 0u;
            /* (21 Nov 2026) the engine's steps (tb_synth.c), on this core's whole time (its waits for the second core
             * too): up at once - 1 past 85 %, 2 past 95 %; down one step at a time, after 0.5 s under 70 %. The effects
             * are never touched by them */
            if (wall > HALF_TICKS / 100u * 95u)
                tb_heavy = 2, rfm_heavy_hold = 172u;
            else if (wall > HALF_TICKS / 100u * 85u) {
                if (tb_heavy < 1u)
                    tb_heavy = 1;
                if (rfm_heavy_hold < 172u)
                    rfm_heavy_hold = 172u;
            } else if (wall < HALF_TICKS / 100u * 70u && tb_heavy && !rfm_heavy_hold)
                tb_heavy--, rfm_heavy_hold = 172u;
            if (rfm_heavy_hold)
                rfm_heavy_hold--;
            (void)used;
        }
        rfm_stats.blocks++;
    }
    rfm_in_audio = 0;
}
extern void isr_alnk0(void);

static void rfm_audio_init(void)                    /* the codec and ALNK0 (hal/fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

void fm1_audio_stats_read(struct fm1_audio_stats *s)
{
    if (!s)
        return;
    s->blocks = rfm_stats.blocks;
    s->short_intervals = rfm_stats.short_intervals;
    s->overruns = rfm_stats.overruns + rfm_skipped;   /* (the blocks let go counted as dropouts) */
    s->peak_ticks = rfm_stats.peak_ticks;
}
#if FELUCCA_DUAL_CORE
/* (21 Nov 2026) the engine's use of the second core (tb_engine.c: the synths' voices, the reverb and the delay there).
 * A wait has the block's deadline (pct % of the block since it began): late, the second core is not stopped - the
 * engine leaves what it works on alone and gives it nothing more until it is done. Only a job not back in 8 ms (a
 * hang), a wrong read or a fault of it stop it for good (as JIANT's fm1_mc_wait) */
static uint32_t rfm_mc_t0;                          /* when the job on the second core was handed over */
/* (21 Nov 2026) the second core's time at its jobs (tb_engine.h): each job run through rfm_c1_job, which adds its
 * time to a running total on a cache line of its own - written there only, read by core 0 */
static void (*volatile rfm_c1_fn)(uint32_t);
static struct { volatile uint32_t ticks, pad[15]; } rfm_c1 __attribute__((aligned(64)));
static void rfm_c1_job(uint32_t arg)
{
    uint32_t t = fm1_mc_ticks();
    rfm_c1_fn(arg);
    rfm_c1.ticks += fm1_mc_ticks() - t;
}
uint32_t tb_engine_c1_ticks(void) { return rfm_c1.ticks; }
static int rfm_mc_check(void)                       /* 0 done; 1 at it; -1 given up (stopped) */
{
    fm1_mc_sync();
    if (fm1_mc_box.done == fm1_mc_box.job)
        return 0;
    if (fm1_mc_box.bad || fm1_core1_fault.count || fm1_ticks() - rfm_mc_t0 > 8000u * FM1_TICKS_PER_US) {
        fm1_mc_lost = fm1_mc_box.bad ? 1u : fm1_core1_fault.count ? 3u : 2u;
        fm1_mc_hold();
        return -1;
    }
    return 1;
}
int tb_engine_mc_avail(void) { return fm1_mc_on; }
int tb_engine_mc_run(void (*fn)(uint32_t), uint32_t arg)
{
    int r;
    if (!fm1_mc_on)
        return 0;
    rfm_c1_fn = fn;                                 /* (never while a job runs: the engine hands one over only when
                                                     *  the last is done - fm1_mc_run's sync makes it seen first) */
    r = fm1_mc_run(rfm_c1_job, arg);
    if (r)
        rfm_mc_t0 = fm1_ticks();
    return r;
}
int tb_engine_mc_wait(unsigned pct)
{
    uint32_t lim = HALF_TICKS / 100u * pct;
    for (;;) {
        int r = rfm_mc_check();
        if (r <= 0)
            return r;
        if (fm1_ticks() - rfm_blk_t0 > lim)
            return 1;                               /* (late: left at it) */
    }
}
int tb_engine_mc_busy(void) { return fm1_mc_on ? rfm_mc_check() : 0; }
#endif
uint32_t tb_engine_ticks(void) { return fm1_ticks(); }          /* (TIMER4 from either core) */
uint32_t tb_engine_block_ticks(void) { return HALF_TICKS; }
unsigned tb_board_audio_load(int reset)             /* the busiest half since the last reset, % of its time */
{
    uint32_t peak = rfm_stats.peak_ticks;
    if (reset)
        rfm_stats.peak_ticks = 0;
    return (unsigned)(peak * 100u / HALF_TICKS);
}
unsigned tb_board_heap_free(void) { return 0; }     /* (no heap here: every buffer is static) */

/* ---------------------------------------------------------- the panel --- */
/* MASTER: the pot x16, smoothed; its square law (as the platform's): quiet at the bottom, fine control at the top */
static int32_t rfm_knob = -1, rfm_batt = -1;
static uint32_t rfm_gain_of(int32_t k10)
{
    uint32_t k = k10 < 0 ? 0u : k10 > 1023 ? 1023u : (uint32_t)k10;
    return (k * k / 1023u) * RFM_GAIN_MAX / 1023u;
}
static void rfm_knob_poll(void)
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER), b;
    if (a >= 0)
        rfm_knob = rfm_knob < 0 ? a * 16 : rfm_knob + (a * 16 - rfm_knob) / 8;
    b = fm1_adc_read(FM1_ADC_BATT);
    if (b > 0)
        rfm_batt = rfm_batt < 0 ? b * 16 : rfm_batt + (b * 16 - rfm_batt) / 32;
}
static void rfm_knob_boot(void)                     /* before the audio: the first half plays at the knob's level */
{
    uint32_t i;
    (void)fm1_adc_read(FM1_ADC_MASTER);             /* (the first conversion after the pin setup: dropped) */
    for (i = 0; i < 3u && rfm_knob < 0; i++)
        rfm_knob_poll();
    rfm_gain = rfm_gain_target = rfm_gain_of(rfm_knob < 0 ? 512 : rfm_knob / 16);
}

void fm1_controls_init(void) { }                    /* (the scan is up before RAVE FM starts: rfm_main.c) */
void fm1_controls_read(struct fm1_controls *s)
{
    uint32_t e;
    if (!s)
        return;
    s->keys = fm1_in.notes;
    s->buttons = fm1_in.buttons;
    for (e = 0; e < 7u; e++) {
        int32_t v = fm1_enc_take(e);
        s->encoders[e] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
    }
    rfm_knob_poll();
    s->knob = (uint16_t)(rfm_knob < 0 ? 512 : rfm_knob / 16);
    s->battery = (uint16_t)(rfm_batt < 0 ? 0 : rfm_batt / 16);
}
void tb_board_controls(const struct fm1_controls *c)   /* MASTER: the audio's gain */
{
    rfm_gain_target = rfm_gain_of(c->knob);
}
void fm1_led_set(unsigned key, int on)              /* 0..13 the buttons, 14..40 the keys */
{
    if (key <= 40u)
        fm1_led_key(key, on);
}
void fm1_led_glow(unsigned key, int on)             /* (RAVE FM 2) the dim glow under a dark LED: the backlight */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)key) {
                if (on)
                    fm1_led_dim[p] |= (uint8_t)(1u << r);
                else
                    fm1_led_dim[p] &= (uint8_t)~(1u << r);
            }
}
uint32_t fm1_millis(void) { return fm1_ms; }

/* --------------------------------------------------------- the screen --- */
/* RAVE FM draws native RGB565; the panel takes it high byte first: swapped on the way. A blit goes through two row
 * buffers (one fills while the other is sent); a submit sends the caller's band itself (swapped in place, lent
 * until fm1_screen_pending says 0). */
static uint16_t rfm_row[2][240];
static uint8_t rfm_scr_pending;
static uint32_t rfm_scr_at;
static int rfm_scr_valid(unsigned x, unsigned y, unsigned w, unsigned h, const void *px, size_t n)
{
    return px && w && h && x < 240u && y < 240u && w <= 240u - x && h <= 240u - y && n >= (size_t)w * h;
}
void fm1_screen_init(void) { }                      /* (lcd_init: rfm_main.c, before the boot screen) */
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    rfm_scr_pending = 0;
    lcd_fill(x, y, w, h, c);
}
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px, size_t n)
{
    uint32_t left, k, i, b = 0;
    if (!rfm_scr_valid(x, y, w, h, px, n))
        return -1;
    lcd_sync();
    rfm_scr_pending = 0;
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    for (left = w * h; left; left -= k, b ^= 1u) {
        k = left > 240u ? 240u : left;
        for (i = 0; i < k; i++) {
            uint16_t v = *px++;
            rfm_row[b][i] = (uint16_t)(v >> 8 | v << 8);
        }
        lcd_data(rfm_row[b], k * 2u);               /* (waits for the row before it: then this one runs) */
    }
    lcd_sync();
    return 0;
}
int fm1_screen_pending(void)
{
    if (!rfm_scr_pending)
        return 0;
    if (fm1_lcd_done()) {                           /* done */
        lcd_sync();
        rfm_scr_pending = 0;
        return 0;
    }
    if (fm1_ms - rfm_scr_at < 40u)
        return 1;
    lcd_sync();                                     /* (stuck: its timeout, counted, and the bus freed) */
    rfm_scr_pending = 0;
    return -2;
}
void fm1_screen_blit_be(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px, unsigned stride)
{
    if (!px || !w || !h || x >= 240u || y >= 240u || w > 240u - x || h > 240u - y)
        return;
    rfm_scr_pending = 0;
    if (stride == w)
        lcd_blit(x, y, w, h, px);
    else
        lcd_blit_stride(x, y, w, h, px, stride);
}
void fm1_screen_sync(void)
{
    lcd_sync();
    rfm_scr_pending = 0;
}
int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t *px, size_t n)
{
    uint32_t i;
    int busy;
    if (!rfm_scr_valid(x, y, w, h, px, n))
        return -1;
    if ((busy = fm1_screen_pending()) != 0)
        return busy;
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    for (i = 0; i < w * h; i++)
        px[i] = (uint16_t)(px[i] >> 8 | px[i] << 8);
    lcd_data(px, w * h * 2u);                       /* left running (lcd_busy) */
    rfm_scr_at = fm1_ms;
    rfm_scr_pending = 1;
    return 0;
}

/* ---------------------------------------------------------------- MIDI --- */
/* in: usb.c's ring (USB and the TRS jack, filled in TIMER5), drained here in the main loop. SysEx: its bytes through
 * a ring of their own (usb.c sysex_byte), put back together here. out: USB-MIDI event packets (usb.c's out ring). */
#define RFM_SX_RING 1024u
#define RFM_SX_MAX 320u
static volatile uint8_t rfm_sx_ring[RFM_SX_RING];
static volatile uint32_t rfm_sx_w, rfm_sx_r;
static uint8_t rfm_sx_msg[RFM_SX_MAX];
static uint32_t rfm_sx_len;
static uint8_t rfm_sx_on;
static void rfm_sx_put(uint32_t b)                  /* TIMER5 (usb.c) */
{
    if (rfm_sx_w - rfm_sx_r < RFM_SX_RING) {
        rfm_sx_ring[rfm_sx_w % RFM_SX_RING] = (uint8_t)b;
        RING_PUBLISH();
        rfm_sx_w++;
    }
}
static void rfm_sysex_poll(void)
{
    while (rfm_sx_r != rfm_sx_w) {
        uint8_t b = rfm_sx_ring[rfm_sx_r % RFM_SX_RING];
        rfm_sx_r++;
        if (b == 0xF0u) {
            rfm_sx_on = 1;
            rfm_sx_len = 0;
        } else if ((b & 0x80u) && b != 0xF7u) {
            rfm_sx_on = 0;                          /* another status: the message broken off */
            continue;
        }
        if (!rfm_sx_on)
            continue;
        if (rfm_sx_len < RFM_SX_MAX)
            rfm_sx_msg[rfm_sx_len++] = b;
        else
            rfm_sx_on = 0;                          /* too long: not RAVE FM's */
        if (b == 0xF7u && rfm_sx_on) {
            rfm_sx_on = 0;
            fm1_app_sysex(rfm_sx_msg, rfm_sx_len);
        }
    }
}
static void rfm_midi_poll(void)
{
    if (midi_in_overflow) {                         /* a broken stream (the ring was full): what was in it, dropped */
        mi_r = mi_w;
        midi_in_overflow = 0;
        return;
    }
    while (mi_r != mi_w) {
        __asm__ volatile("" ::: "memory");           /* (audit 2: the slot read after the index that says it is there) */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], cin = pkt & 15u, n;
        unsigned src = midi_in_source[at];
        uint8_t m[3];
        m[0] = (uint8_t)(pkt >> 8);
        m[1] = (uint8_t)(pkt >> 16);
        m[2] = (uint8_t)(pkt >> 24);
        n = cin == 0xFu ? 1u : cin == 0xCu || cin == 0xDu ? 2u : 3u;
        RING_PUBLISH();
        mi_r++;
        fm1_app_midi_received(src, m, n);
    }
}
int fm1_midi_send(const uint8_t *m, size_t n)       /* 0: queued (or nobody listening), -1: no room, try again */
{
    uint32_t pk[8], np = 0, i;
    if (!m || !n)
        return -1;
    if (m[0] == 0xF0u) {                            /* SysEx: 3 bytes a packet, the last one 1..3 (CIN 5 6 7) */
        uint32_t room = MQ - (mo_w - mo_r), need = (uint32_t)(n + 2u) / 3u;
        if (!usb.config)
            return 0;
        if (need > room)
            return -1;
        for (i = 0; i < n; i += 3u) {
            uint32_t r = (uint32_t)n - i, cin = r > 3u ? 4u : r == 3u ? 7u : r == 2u ? 6u : 5u, p = cin | (uint32_t)m[i] << 8;
            if (r > 1u)
                p |= (uint32_t)m[i + 1u] << 16;
            if (r > 2u)
                p |= (uint32_t)m[i + 2u] << 24;
            midi_out_event(p);
        }
        return 0;
    }
    if (m[0] >= 0xF8u)
        pk[np++] = 0xFu | (uint32_t)m[0] << 8;
    else if (m[0] >= 0x80u && m[0] < 0xF0u) {
        uint32_t two = (m[0] & 0xE0u) == 0xC0u;     /* Cx Dx: one data byte */
        if (n < (two ? 2u : 3u))
            return -1;
        pk[np++] = (uint32_t)(m[0] >> 4) | (uint32_t)m[0] << 8 | (uint32_t)m[1] << 16 | (two ? 0u : (uint32_t)m[2] << 24);
    } else
        return -1;
    if (!usb.config)
        return 0;
    if (MQ - (mo_w - mo_r) < np)
        return -1;
    for (i = 0; i < np; i++)
        midi_out_event(pk[i]);
    return 0;
}

/* --------------------------------------------------------- the flash --- */
/* RAVE FM's projects: the platform's data area (0x97000 .. 0xE0000, 292 KB), offsets from its start. Writes go
 * through a RAM page (the source must not be in flash while it is written). Erases and writes silence the audio for
 * their moment (storage_hw.c): RAVE FM saves on a press of SAVE. */
#define RFM_FL_BASE FL_DATA_LO
#define RFM_FL_SIZE (FL_DATA_HI - FL_DATA_LO)
static uint8_t rfm_fl_page[256] __attribute__((aligned(4)));
static int rfm_fl_in(uint32_t off, size_t n) { return flash_ok && off <= RFM_FL_SIZE && n <= RFM_FL_SIZE - off; }
uint32_t fm1_flash_size(void) { return flash_ok ? RFM_FL_SIZE : 0u; }
const uint8_t *fm1_flash_ptr(uint32_t off) { return flash_ok && off < RFM_FL_SIZE ? (const uint8_t *)FL_XIP(RFM_FL_BASE + off) : 0; }
int fm1_flash_read(uint32_t off, void *d, size_t n)
{
    if (!d || !rfm_fl_in(off, n))
        return FM1_FLASH_INVALID;
    return n && st_read(RFM_FL_BASE + off, d, (uint32_t)n) ? FM1_FLASH_IO : FM1_FLASH_OK;
}
int fm1_flash_write(uint32_t off, const void *src, size_t n)
{
    const uint8_t *s = src;
    if (!s || !rfm_fl_in(off, n))
        return FM1_FLASH_INVALID;
    while (n) {
        uint32_t k = 256u - (off & 0xFFu), i;
        if (k > n)
            k = (uint32_t)n;
        for (i = 0; i < k; i++)
            rfm_fl_page[i] = s[i];
        if (st_prog(RFM_FL_BASE + off, rfm_fl_page, k))
            return FM1_FLASH_IO;
        fm1_wdt_feed();
        off += k;
        s += k;
        n -= k;
    }
    return FM1_FLASH_OK;
}
int fm1_flash_erase(uint32_t off)
{
    if ((off & (FM1_FLASH_SECTOR_BYTES - 1u)) || !rfm_fl_in(off, FM1_FLASH_SECTOR_BYTES))
        return FM1_FLASH_INVALID;
    fm1_wdt_feed();
    return st_erase(RFM_FL_BASE + off) ? FM1_FLASH_IO : FM1_FLASH_OK;
}
