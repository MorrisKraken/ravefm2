/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the platform's compilation unit. What makes the FM-1 run (start-up, screen, keys and knobs, the audio's
 * interrupt, USB MIDI and the TRS jack, the flash, the update entry, the crash guard) comes from the FM-1's open
 * firmware family (Felucca, JIANT: hal/ and the files included below, GPL-3.0); RAVE FM's own code (its sound, its
 * music, its screens) is the other units (firmware/rfm), which see the board through fm1.h (rfm_board.c serves it).
 * tools/build_ravefm.py compiles this unit and RAVE FM's own to LLVM IR and links them into one program. */

/* ---------------------------------------------------- build options --- */
#define FELUCCA_FLASH 1          /* the flash driver: RAVE FM's projects (rfm_board.c), the update's staging */
#define FELUCCA_OTA 1            /* the update entry: M-UPGRADE and the web installers install over RAVE FM */
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_CDC 0            /* no serial console */
#define FELUCCA_UAC 0            /* no USB audio: USB is MIDI */
#define FELUCCA_UAC_TONE 0
#define FELUCCA_UART 1           /* the TRS MIDI IN jack */
#ifndef FELUCCA_DUAL_CORE
#define FELUCCA_DUAL_CORE 1      /* (21 Nov 2026) two cores, as JIANT: the reverb and the delay on core 1 (tb_engine.c) */
#endif
#define FELUCCA_HOTRAM 1
#define FELUCCA_BT 0
#define FELUCCA_USB_HOST 0
#ifndef RFM2
#define RFM2 1                   /* (the platform's few changes for RAVE FM: usb.c's maker name) */
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_RAVEFM2"    /* the package's identity (build_ravefm.py passes it too) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "RAVE FM 2"
#endif

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_power.h"            /* the stock supply setup (rfm_main.c, before the rails) */
#include "fm1_irq.h"
#include "fm1_guard.h"
#if FELUCCA_DUAL_CORE
#include "fm1_multicore.h"
#endif
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"

/* --------------------------------------------------- base, display --- */
#include "libc.c"
#include "lcd.c"

/* ----------------------------------------------- what they share --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
#define FS 44100u
#define HALF_FRAMES 128u          /* the I2S half buffer: 2.9 ms */
#define HALF_WORDS (HALF_FRAMES * 2u)
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));   /* the audio DMA's two halves */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));

/* ------------------------------------------------------- MIDI, USB --- */
static void rfm_sx_put(uint32_t b);       /* every SysEx byte from USB (TIMER5): to RAVE FM (rfm_board.c) */
#define RFM_SYSEX_BYTE(b) rfm_sx_put(b)
#include "usb.c"
#include "midi_uart.c"

/* --------------------------------------------------- flash, update --- */
#if FELUCCA_DUAL_CORE
#define FL_PARK() fm1_mc_park()  /* core 1 out of any job before the flash goes off (hal/fm1_multicore.h) */
#endif
#include "fm1_flash.h"
#include "storage_hw.c"
#include "rfm_text.c"            /* the platform's own few words on the screen: UPDATE, UBOOT, the crash */
#include "ota.c"
#include "ota_hw.c"

/* ------------------------------------------- the board, the program --- */
#include "rfm_board.c"           /* fm1.h for RAVE FM's code; the audio interrupt */
#include "rfm_main.c"            /* start-up and the main loop */
