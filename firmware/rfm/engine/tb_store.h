/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the projects kept in the FM-1's flash: eight places, each written in turn into two halves with a count
 * and a CRC (a save cut short leaves the one before it whole). A project is packed (its empty steps take nothing). */
#ifndef TB_STORE_H
#define TB_STORE_H
#include "tb_features.h"
#include <stdint.h>
#ifdef RFM2
#define TB_STORE_SLOTS 6               /* (RAVE FM 2: six places, the user's samples beside them) */
#else
#define TB_STORE_SLOTS 8
#endif
/* tb_proj into place slot: 0 saved; < 0 the flash refused (-4: it does not fit) */
int tb_store_save(unsigned slot);
/* place slot into tb_proj: 1 loaded, 0 empty or damaged (tb_proj untouched) */
int tb_store_load(unsigned slot);
/* the last project saved, whichever place: 1 loaded (its place in *slot), 0 none */
int tb_store_load_last(unsigned *slot);
/* what place slot holds: 1 a project (its tempo x 10, its packed size), 0 nothing */
int tb_store_info(unsigned slot, unsigned *bpm10, unsigned *bytes);
/* (v25) a place's project's name (8 letters and a 0; "" none or unreadable), read from the flash, tb_proj untouched */
int tb_store_name(unsigned slot, char out[9]);
/* the user's samples (tb_break.h), kept apart from the projects at the end of the flash: 0 saved / 1 loaded */
int tb_store_samples_save(void);
int tb_store_samples_load(void);
#ifdef RFM2
/* (RAVE FM 2) the samples' bytes written straight into flash: their room erased (0 done), bytes at off */
int tb_store_smp_prepare(uint32_t off, uint32_t n);
void tb_store_smp_write(uint32_t off, const uint8_t *p, uint32_t n);
#endif
/* the first byte of the user flash the projects may use (0: all of it; the FM-1: after the pack) */
extern uint32_t tb_store_base;
#endif
