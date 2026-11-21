/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM's reverb: the first version's HALL (tb_hall.c) */
#ifndef TB_HALL_H
#define TB_HALL_H
#include <stdint.h>
void tb_hall_clear(void);
void tb_hall_sleep(void);           /* its tail emptied, its place and drift kept */
void tb_hall_idle(unsigned n);      /* n samples of silence, asleep: its place moved on */
/* n samples of the send in -> the wet mid and side out (left = mid + side, right = mid - side); SIZE (0 .. 200: RT60
 * 0.5 s x 2^(size / 30)), DAMP 0 .. 127 */
void tb_hall_run(const int32_t *in, int32_t *mid, int32_t *side, unsigned n, int size, int damp);
#endif
