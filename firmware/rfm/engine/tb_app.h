/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the control task's entry points (tb_app.c); the audio side is tb_engine_render. */
#ifndef TB_APP_H
#define TB_APP_H
void tb_app_init(void);
void tb_app_tick(void);       /* as often as the board's control task runs (at least every few ms) */
#ifdef TB_PORT
#include <stddef.h>
#include <stdint.h>
/* the FM-1's port: its glue (fm1/port/app/ravefm_glue.c) takes the SDK's hooks and passes them on */
void tb_app_sysex(const uint8_t *m, size_t n);
void tb_app_midi_received(unsigned source, const uint8_t *m, size_t n);
struct fm1_controls;
void tb_board_controls(const struct fm1_controls *c);
#endif
/* the board's (the FM-1's port; elsewhere 0): the audio's busiest block since the last reset, % of its time; the
 * SDK's heap never used yet, bytes */
unsigned tb_board_audio_load(int reset);
unsigned tb_board_heap_free(void);
#endif
