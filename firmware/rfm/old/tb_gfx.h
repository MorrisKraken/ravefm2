/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the screen. A frame is a list of drawing commands, rasterised one band of lines at a time straight into
 * the panel's RGB565 (dithered), in tiles: only the tiles whose commands changed since the last frame are drawn and
 * sent, a band's as one window; two buffers, one drawn while the other goes out. */
#ifndef TB_GFX_H
#define TB_GFX_H
#include "tb_features.h"
#include <stdint.h>
#include "tb_font.h"

#define TB_W 240
#define TB_H 240
#if TB_LITE
#define TB_BAND 8                     /* (the FM-1: its RAM; rows drawn at once) */
#else
#define TB_BAND 16
#endif
#define TB_RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

enum { GFX_LEFT, GFX_CENTER, GFX_RIGHT };
enum { GFX_NORMAL, GFX_ADD };

void gfx_begin(void);
unsigned gfx_overflows(void);
/* rectangles: filled, at alpha a (0..255), normal or additive */
void gfx_rect(int x, int y, int w, int h, uint32_t c, uint8_t a, uint8_t mode);
/* a rounded rectangle, filled (line 0) or outlined line px wide, anti-aliased */
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, uint8_t a, int line);
/* a vertical gradient top -> bottom */
void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t a);
/* text at a baseline; returns its width */
int gfx_text(const tb_font_t *f, int x, int y, const char *s, uint32_t c, uint8_t a, int align);
int gfx_text_width(const tb_font_t *f, const char *s);
/* a waveform lane: w columns, each a level (0..255, its height around the lane's centre line) and a tone (0..255:
 * from bass to treble, its colour); hot: 0..255 how bright the lane is (the selected one) */
void gfx_wave(int x, int y, int w, int h, const uint8_t *levels, const uint8_t *heats, uint8_t hot);
/* an anti-aliased ring segment: centre, radius, thickness, from angle a0 to a1 (1/1024 turns, 0 = up, clockwise) */
void gfx_arc(int cx, int cy, int r, int thick, int a0, int a1, uint32_t c, uint8_t a);
/* an anti-aliased line, additive (light) or normal, w in 1/16 px */
void gfx_line(int x0, int y0, int x1, int y1, int w16, uint32_t c, uint8_t a, uint8_t mode);
/* a soft round glow, additive */
void gfx_glow(int cx, int cy, int r, uint32_t c, uint8_t a);
/* a line through n points (x, y pairs), anti-aliased, w in 1/16 px: one command however many points */
void gfx_poly(const int16_t *xy, unsigned n, int w16, uint32_t c, uint8_t a);
/* a filled area: w columns over x.., each from y + tops[i] down to y + h, a vertical gradient top -> bottom */
void gfx_area(int x, int y, int w, int h, const uint8_t *tops, uint32_t top, uint32_t bottom, uint8_t a);
/* RAVE FM's bat, cut out (TB_LOGO_W x TB_LOGO_H, tools/gen_logo.py), its top left at x, y, through the theme's violet */
void gfx_logo(int x, int y, uint8_t a);
/* the first version's dancer, frame f (gfx_dance_frames() of them, 0: none in this build), over the whole screen */
void gfx_dance(unsigned frame, uint32_t col);
unsigned gfx_dance_frames(void);
/* rasterise: emit(x, y, w, h, pixels RGB565, native order) for every band that changed, the window of its changed
 * tiles (all: force). The pixels are lent: emit may send them while the next band is drawn in another buffer, but must
 * be done with them by its next call but one (the FM-1: the panel's DMA, waited for before the next is started) */
void gfx_end(void (*emit)(int x, int y, int w, int h, uint16_t *px), int force);

/* the palette's scale: 0 black .. 255 near white, through the theme's colours (VIOLET, MATRIX, FEU) */
enum { GFX_RAVE, GFX_VIOLET, GFX_MATRIX, GFX_FEU, GFX_NPAL };
void gfx_palette(unsigned p);
unsigned gfx_palette_now(void);
uint32_t gfx_heat(unsigned t);
/* the screen's ground (the theme's darkest: RAVE a deep purple) */
uint32_t gfx_bg(void);
/* the words' colours in the palette: 0 bright, 1 quiet, 2 dim */
uint32_t gfx_ink(unsigned k);
uint32_t gfx_mix(uint32_t a, uint32_t b, unsigned t);
/* the colour of a sound by its brightness (0: all bass .. 255: all treble), in the theme's three tones */
uint32_t gfx_spectrum(unsigned b);
#endif
