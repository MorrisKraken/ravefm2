/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the board as RAVE FM's sound and screen code see it (the calls they were written for), served on the
 * bare FM-1 by the platform (firmware/src/rfm_board.c) and on the host by the simulator. */
#ifndef RFM_FM1_H
#define RFM_FM1_H
#include <stddef.h>
#include <stdint.h>

/* the panel: keys bits 0..26 (F3..G5), buttons bits 0..13 (OCT- OCT+ FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY
 * REC), the encoders' steps since the last read (SELECT ALGORITHM K1 K2 K3 K4 PRESETS), MASTER and the battery 0..1023 */
struct fm1_controls {
    uint32_t keys;
    uint32_t buttons;
    int16_t encoders[7];
    uint16_t knob, battery;
};
void fm1_controls_init(void);
void fm1_controls_read(struct fm1_controls *state);
void fm1_led_set(unsigned key, int on);           /* 0..13 the buttons, 14..40 the keys */
void fm1_led_glow(unsigned key, int on);          /* its dim glow (the backlight), under lit */
uint32_t fm1_millis(void);

/* the audio: blocks rendered, late, the busiest's ticks */
struct fm1_audio_stats { uint32_t blocks, short_intervals, overruns, peak_ticks; };
void fm1_audio_stats_read(struct fm1_audio_stats *stats);

/* the screen: 240 x 240 RGB565, native order; submit starts a band's transfer (the buffer lent until the next call),
 * blit waits for it */
void fm1_screen_init(void);
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565);
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *pixels, size_t count);
int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t *pixels, size_t count);
int fm1_screen_pending(void);
/* (RAVE FM 2, JIANT's renderer) pixels already high byte first, w x h out of rows of stride: sent as they are (the
 * transfer may still read them after the call: fm1_screen_sync waits for it) */
void fm1_screen_blit_be(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px, unsigned stride);
void fm1_screen_sync(void);
/* (RAVE FM 2) the data area read in place (the user's samples played from flash): offset off's address, 0 none */
const uint8_t *fm1_flash_ptr(uint32_t off);

/* MIDI out (USB and the jack); MIDI in arrives as fm1_app_midi_received / fm1_app_sysex (tb_app.c) */
int fm1_midi_send(const uint8_t *message, size_t bytes);
void fm1_app_midi_received(unsigned source, const uint8_t *message, size_t bytes);
void fm1_app_sysex(const uint8_t *message, size_t bytes);

/* RAVE FM's own flash: offsets from 0 in its area */
#define FM1_FLASH_SECTOR_BYTES 4096U
enum fm1_flash_result { FM1_FLASH_OK = 0, FM1_FLASH_INVALID = -1, FM1_FLASH_IO = -2 };
uint32_t fm1_flash_size(void);
int fm1_flash_read(uint32_t offset, void *data, size_t bytes);
int fm1_flash_write(uint32_t offset, const void *data, size_t bytes);
int fm1_flash_erase(uint32_t offset);
#endif
