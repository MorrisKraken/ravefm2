/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the drawings. Thin lines in one colour (the theme's), the accents in its hot colour: the tracks' icons,
 * the speakers, the dancers (each a skeleton posed on the beat, moving as much as its tracks play), the stage. */
#ifndef RFM_ART_H
#define RFM_ART_H
#include <stdint.h>
#include "tb_ui.h"

/* a track's icon (TB_ICON_*), 16 x 16 x scale, its top left at x, y; hit: its second frame */
void art_icon(unsigned icon, int x, int y, int scale, unsigned c, int hit);
/* a speaker cabinet: pump 0..3 (how far the cone moves out), hot: the kick lit it */
void art_speaker(int x, int y, int w, int h, int pump, int hot);
/* the dancers: kind (ART_*), feet at x, y; phase 0..1023 in the beat, beat count; energy 0..255; hit: a hit now */
enum { ART_GABBER, ART_RAVER, ART_DJ, ART_ROBOT, ART_NDANCER };
/* the dancers' size: quarter pixels a unit (4: about 45 px tall) */
void art_scale(int quarter);
void art_dancer(unsigned kind, int x, int y, unsigned phase, uint32_t beats, unsigned energy, int hit, uint32_t ms);
/* the whole stage (the dance screen): the dancers, the speakers, the lights, the levels */
void art_stage(const tb_view_t *v);
#endif
