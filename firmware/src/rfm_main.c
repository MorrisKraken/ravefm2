/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: start-up and the main loop. The start (fm1_cstart) is the platform's own, in its order: the watchdog,
 * the boot-loop guard, the interrupts, RAM cleared and unpacked (boot_lz4.h), the supplies as the stock firmware sets
 * them, the core rails, the guards. Then: the screen, the panel, RAVE FM, the audio, USB, the TRS jack, TIMER5; and the
 * loop: the watchdog, USB, the update entry, MIDI in, RAVE FM's tick. */
extern uint32_t _data_start[], _data_end[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[];
extern uint32_t _hot_start[], _hot_end[], _low_start[], _low_end[];
extern char _etext[];                               /* .text's end: the packed RAM sections from here (build) */
#include "boot_lz4.h"

/* ------------------------------------------------------- a hang's record --- */
/* The main loop stopped answering 4 s (the watchdog resets at 8 s, a crash screen never comes): TIMER5 notes where
 * it was, 16 times 5 ms apart (the code TIMER5 interrupted: the main loop's, or the audio's if it never let go), and
 * the app's stage and page; the record survives the reset (.noinit) and the next start shows it. */
#define RFM_HANG_MAGIC 0x48414E47u                  /* "HANG" */
typedef struct { uint32_t magic, uptime_ms, stage, page, audio_mask, n, pc[16]; } rfm_hang_t;
rfm_hang_t rfm_hang __attribute__((section(".noinit")));
static volatile uint32_t rfm_beat;                  /* the main loop's turns */
extern volatile uint8_t tb_app_stage, tb_app_page;
static void hang_watch(void)                        /* (in TIMER5, every tick) */
{
    static uint32_t seen, t0, sub;
    static uint8_t n, done;
    uint32_t b = rfm_beat, pc;
    if (done)
        return;
    if (b != seen) {
        seen = b, t0 = fm1_ms, n = 0, sub = 0;
        return;
    }
    if (fm1_ms - t0 < 4000u || ++sub < 50u)
        return;
    sub = 0;
    pc = fm1_reti();                                /* (where TIMER5 came in) */
    if (n == 0u)
        rfm_hang.audio_mask = 0, rfm_hang.stage = tb_app_stage, rfm_hang.page = tb_app_page;
    rfm_hang.pc[n] = pc;
    if (rfm_in_audio)
        rfm_hang.audio_mask |= 1u << n;
    if (++n == 16u) {
        rfm_hang.n = 16, rfm_hang.uptime_ms = fm1_ms, rfm_hang.magic = RFM_HANG_MAGIC;
        done = 1;
    }
}

/* ------------------------------------------------------------ TIMER5 --- */
/* 10 kHz: the panel's scan and the milliseconds on every tick, nested in the audio's render too (it outranks it);
 * USB MIDI (2 kHz) and the TRS jack (2 kHz) only outside the render, on the first tick after it.
 * USB audio cannot wait: one packet per 1 ms, and a render holds the core for most of its 2.9 ms. uac_service
 * runs on every tick, inside the render too (EP4 only). The full poll stays outside. */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed, last, acc;
    uint32_t t0 = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t0 - last;                               /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    hang_watch();
    if (sub % 5u == 0u)
        owed |= 1u;
    if (sub % 5u == 2u)
        owed |= 2u;
    if (++sub == 10u)
        sub = 0;
    if (rfm_in_audio) {
#if FELUCCA_UAC
        uac_service();
        if ((owed & 1u) && usb.up && usb.config && uin.alt)
            uin_rx();
#endif
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UAC
    else
        uac_service();
#endif
    if (owed & 2u)
        uart_midi_poll();
    owed = 0;
}
extern void isr_timer5(void);

/* ------------------------------------------------------------- a crash --- */
/* the screen says it, with the numbers that find it (hex), 10 s, then a restart; the record stays in .noinit */
static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}
static void fm1_fault(const fm1_crash_t *c)
{
    static const char L[6][5] = {"VEC ", "PC  ", "RETS", "EMU ", "DBG ", "SP  "};
    const uint32_t v[6] = {c->vec, c->pc, c->rets, c->emu, c->dbg, c->sp};
    char b[24];
    uint32_t i, t0;
#if FELUCCA_DUAL_CORE
    if (fm1_mc_on)
        fm1_mc_bar_set();                           /* (a crash beside the second core: one core until power-off) */
    fm1_mc_hold();
#endif
    fm1_audio_stop();
    lcd_wake_now();
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 20, 240, &AF_M, "RAVE FM CRASH", T_TEXT, 1);
    for (i = 0; i < 6u; i++) {
        str_cpy(b, L[i], 6);
        b[4] = ' ';
        hexs(b + 5, v[i]);
        draw_text_box(30, 64 + 22 * i, 180, &AF_S, b, T_THEME, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 10000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* ------------------------------------------------------------- UBOOT --- */
static void rfm_uboot(const char *why)              /* the chip's own USB loader (the rescue tools, M-Flasher) */
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 110, 240, &AF_M, why, T_THEME, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional: not a failed boot */
    fm1_enter_uboot();
}

/* (the supply setup's guard: PW_CR_START from its start until the loop runs; a reset before that finds it and the
 * next start keeps the boot loader's supplies, until the power goes off) */
#define PW_CR_START 0x5057A001u
#define PW_CR_SKIP 0x5057A002u
static uint32_t pw_crumb __attribute__((section(".noinit.zbt")));

static void rfm_main(void)
{
    uint32_t f, id, way_t0 = 0;
    f = irq_save();
    id = FL_FAR(fl_jedec_ram)();                    /* the 1 MB part: Puya's, or the late 2026 units' */
    irq_restore(f);
    flash_ok = id == 0x856014u || id == 0xB36014u;
    if (flash_ok)
        fl_plain_window_init();                     /* (the data area read in place: the user's samples, played from flash) */
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();                         /* the staging an update left */
#endif
    lcd_init();
    if (rfm_hang.magic == RFM_HANG_MAGIC) {         /* the last run hung: where, 15 s, then on */
        char b[24];
        uint32_t i;
        rfm_hang.magic = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 10, 240, &AF_M, "RAVE FM BLOCAGE", T_TEXT, 1);
        str_cpy(b, "ETAPE   PAGE   SON  ", 21);
        b[6] = (char)('0' + rfm_hang.stage % 10u), b[13] = (char)('0' + rfm_hang.page / 10u % 10u);
        b[14] = (char)('0' + rfm_hang.page % 10u);
        for (i = 0; i < 16u; i++)
            if ((rfm_hang.audio_mask >> i) & 1u)
                b[19] = b[19] == ' ' ? '1' : b[19] < '9' ? (char)(b[19] + 1) : '9';
        draw_text_box(0, 40, 240, &AF_S, b, T_THEME, 1);
        for (i = 0; i < 16u; i++) {
            hexs(b, rfm_hang.pc[i]);
            b[8] = (rfm_hang.audio_mask >> i) & 1u ? '*' : ' ', b[9] = 0;
            draw_text_box(i < 8u ? 14u : 128u, 64u + 21u * (i % 8u), 106, &AF_S, b, T_THEME, 0);
        }
        for (i = 0; i < 1500u; i++) {
            fm1_wdt_feed();
            fm1_delay_ms(10);
        }
        lcd_fill(0, 0, 240, 240, T_BG);
    }
    fm1_input_init();
    fm1_adc_init();
    rfm_knob_boot();
    tb_app_init();                                  /* RAVE FM: its sound, its projects' index, the boot screen */
    rfm_audio_on = 1;
    rfm_audio_init();
    usb_start();
    uart_midi_init();
    fm1_timer5_start(isr_timer5, 4);                /* above the audio (3): the scan nests in the render */
    fm1_guard_lock_top();
#if FELUCCA_DUAL_CORE
    /* (21 Nov 2026, as JIANT) the second core: the reverb and the delay on it (tb_engine.c). Not after a failed boot,
     * not after a crash or a hang beside it since power-on (fm1_mc_bar), only on the stock supplies (fm1_power_ready).
     * Its entry word is in the locked top of RAM: opened for it, the interrupts not on yet */
    if (!bootguard.failed && !fm1_mc_barred() && fm1_power_ready) {
        fm1_guard_unlock_top();
        fm1_mc_start();
        fm1_guard_lock_top();
    }
#endif
    fm1_irq_enable_all();
    if (pw_crumb != PW_CR_SKIP)
        pw_crumb = 0;                               /* up: the supply setup went well */

    for (;;) {
        fm1_wdt_feed();
        rfm_beat++;
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) { /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        if ((fm1_in.buttons & 3u) != 3u || fm1_in.notes)
            way_t0 = fm1_ms;                        /* OCT- + OCT+ held 5 s, nothing else: the way out */
        else if (fm1_ms - way_t0 > 5000u)
            rfm_uboot("UBOOT");
#if FELUCCA_OTA
        ota_service();                              /* the updater's handshake */
        if (usb.ota_req) {                          /* the updater's upgrade command */
            usb.ota_req = 0;
            tb_engine_play(0);
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
        }
#endif
        if (usb.uboot_req)                          /* SysEx F0 22 24 35 7D F7 from the host */
            rfm_uboot("UBOOT (USB)");
        rfm_sysex_poll();
        rfm_midi_poll();
        tb_app_tick();
    }
}

void fm1_cstart(void)
{
    uint32_t *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    fm1_core1_stop();                               /* a second core a reset left running would read the RAM cleared below */
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {                   /* two starts that died early: the USB loader, for a new install */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
#if FELUCCA_DUAL_CORE
    if (fm1_mc_bar.magic != FM1_MC_BAR_MAGIC)       /* (after a power-off: garbage) */
        fm1_mc_bar.magic = FM1_MC_BAR_MAGIC, fm1_mc_bar.barred = 0;
    else if ((p3 & 4u) && (fm1_mc_bar.barred & 2u))
        fm1_mc_bar_set();                           /* the watchdog, the second core running: one core until power-off */
    fm1_mc_bar.barred &= ~2u;
#endif
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (d = _low_start; d < _low_end; d++)
        *d = 0;
    {                                               /* unpacked from _etext on, one LZ4 block each, in this order */
        const uint8_t *z = (const uint8_t *)_etext;
        z = boot_lz4(z, (uint8_t *)_rt_start, (uint8_t *)_rt_end);     /* the flash driver's RAM code */
        z = boot_lz4(z, (uint8_t *)_hot_start, (uint8_t *)_hot_end);   /* the sound's hottest code */
        boot_lz4(z, (uint8_t *)_data_start, (uint8_t *)_data_end);
    }
    if (pw_crumb == PW_CR_START || pw_crumb == PW_CR_SKIP)
        pw_crumb = PW_CR_SKIP;                      /* the last start hung here or before the loop: not again */
    else {
        pw_crumb = PW_CR_START;
        fm1_power_ready = (uint8_t)fm1_power_init();   /* the supplies as the stock firmware sets them */
    }
    fm1_rails_boot();                               /* the core supplies to the stock levels, before the audio */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    rfm_main();
    for (;;)
        ;
}
