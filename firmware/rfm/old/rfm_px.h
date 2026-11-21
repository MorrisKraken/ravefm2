/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the screen as pixel art. A 240 x 240 canvas of 16 colours (4 bits a pixel) is drawn whole every frame,
 * with whole pixels only (lines, boxes, the font, sprites: no blending); then only the 8 x 8 cells whose pixels changed
 * go to the panel, each row of changed cells as one window, through two buffers (one converted while the other is
 * sent). The 16 colours are roles (PX_*); a theme gives each its colour. */
#ifndef RFM_PX_H
#define RFM_PX_H
#include <stdint.h>

#define PX_W 240
#define PX_H 240
#define TB_W PX_W
#define TB_H PX_H

/* the colours' roles */
enum {
    PX_BG,        /* black */
    PX_PANEL,     /* the darkest fill: a card, a selected row */
    PX_LINE,      /* hairlines, grids, an empty bar */
    PX_DIM,       /* what is off, quiet */
    PX_MID,       /* the main colour, darker: a second level */
    PX_MAIN,      /* the theme's colour: the drawings, what is set */
    PX_HOT,       /* the accent: what is lit now (the playhead, a hit, the knob turned) */
    PX_INK,       /* bright words */
    PX_GREY,      /* quiet words */
    PX_FAINT,     /* faint words */
    PX_ALERT,     /* REC, MUTE */
    PX_AUX,       /* a second hue: the other notes, the sends */
    PX_WARM,      /* DROP, RISER */
    PX_AUXDK,     /* the second hue, dark (its fills) */
    PX_BAT,       /* the bat's violet */
    PX_WHITE,
    PX_NCOL
};
enum { PX_LEFT, PX_CENTER, PX_RIGHT };

/* the themes (MENU > COLOUR) */
enum { GFX_ACID, GFX_TOXIC, GFX_CYBER, GFX_LAVA, GFX_NPAL };
void gfx_palette(unsigned theme);
unsigned gfx_palette_now(void);

/* a frame: px_begin clears the canvas (the clip: the whole screen), px_flush sends what changed (force: all) */
void px_begin(void);
void px_flush(int force);
/* for tb_app.c: the end of its frame (its emit is not needed: the canvas sends itself) */
void gfx_end(void (*emit)(int x, int y, int w, int h, uint16_t *px), int force);

void px_clip(int x0, int y0, int x1, int y1);   /* drawing kept inside [x0, x1) x [y0, y1) */
void px_noclip(void);
void px_pixel(int x, int y, unsigned c);
unsigned px_get(int x, int y);
void px_hline(int x, int y, int w, unsigned c);
void px_vline(int x, int y, int h, unsigned c);
void px_rect(int x, int y, int w, int h, unsigned c);          /* filled */
void px_frame(int x, int y, int w, int h, unsigned c);         /* outlined, 1 px */
void px_box(int x, int y, int w, int h, unsigned fill, unsigned edge);   /* corners cut: a pixel-art card */
void px_dots(int x, int y, int w, int h, unsigned c);          /* every other pixel (a checker): a dim fill */
void px_line(int x0, int y0, int x1, int y1, unsigned c);
void px_circle(int cx, int cy, int r, unsigned c);
void px_disc(int cx, int cy, int r, unsigned c);
/* text: the pixel font (5 x 7 over the baseline), scale 1..4; y is the top of the capitals; the width drawn */
int px_text(int x, int y, const char *s, unsigned c, int scale, int align);
int px_text_w(const char *s, int scale);
/* a 1-bit sprite: rows of (w + 7) / 8 bytes, bit 7 the leftmost; flip: mirrored */
void px_sprite(const uint8_t *bits, int w, int h, int x, int y, unsigned c, int flip);
/* the boot screen (RAVE FM's bat and name, in their own colours), straight to the panel, faded in to level 0..16 */
void px_boot(unsigned level);
#endif
