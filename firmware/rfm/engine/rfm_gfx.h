/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the drawing calls the pictures inside the pages are written with (THUNDERBOX's tb_gfx.h), drawn on the
 * panel's canvas of JIANT's renderer (../jui/rfm_screen.c). A colour is a role (PXC(PX_*), gfx_heat: the gradient, gfx_ink) or an RGB value;
 * translucency is mixed with the ground. The pictures were laid out for rows 45..200: drawn into the page's picture
 * area (rows 80..219) when gfx_area_map is on. */
#ifndef RFM_GFX_H
#define RFM_GFX_H
#include "tb_features.h"
#include <stdint.h>

#define TB_W 240
#define TB_H 240
#define TB_BAND 8
#define TB_RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))
#define PXC(role) (0x80000000u | (uint32_t)(role))
enum { PX_BG, PX_PANEL, PX_LINE, PX_DIM, PX_MID, PX_MAIN, PX_HOT, PX_INK, PX_GREY, PX_FAINT, PX_ALERT, PX_AUX, PX_WARM,
       PX_AUXDK, PX_BAT, PX_WHITE, PX_NCOL };
enum { GFX_ACID, GFX_TOXIC, GFX_CYBER, GFX_LAVA, GFX_VIOLET, GFX_LOGO, GFX_NPAL };   /* (RAVE FM 2: NEON ACIDE TOXIC FEU SPECTRE
                                                                            * VIOLET, the logo's) */
#define GFX_PAL_DEFAULT 1u   /* (RAVE FM 2: ACIDE, the yellow one, until a saved project says otherwise) */

typedef struct { int font, ascent; } tb_font_t;
extern const tb_font_t TB_FONT_S, TB_FONT_M, TB_FONT_L, TB_FONT_XL;

enum { GFX_LEFT, GFX_CENTER, GFX_RIGHT };
enum { GFX_NORMAL, GFX_ADD };

extern int gfx_area_map;              /* the old picture rows mapped into the picture area */
void gfx_begin(void);
unsigned gfx_overflows(void);
void gfx_end(void (*emit)(int x, int y, int w, int h, uint16_t *px), int force);
void gfx_palette(unsigned theme);
unsigned gfx_palette_now(void);
uint16_t gfx_role(unsigned role);
uint16_t gfx_c(uint32_t c);           /* a colour (role or RGB) as RGB565 */
void gfx_rect(int x, int y, int w, int h, uint32_t c, uint8_t a, uint8_t mode);
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, uint8_t a, int line);
void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t a);
int gfx_text(const tb_font_t *f, int x, int y, const char *s, uint32_t c, uint8_t a, int align);
int gfx_text_width(const tb_font_t *f, const char *s);
void gfx_wave(int x, int y, int w, int h, const uint8_t *levels, const uint8_t *heats, uint8_t hot);
void gfx_arc(int cx, int cy, int r, int thick, int a0, int a1, uint32_t c, uint8_t a);
void gfx_line(int x0, int y0, int x1, int y1, int w16, uint32_t c, uint8_t a, uint8_t mode);
void gfx_glow(int cx, int cy, int r, uint32_t c, uint8_t a);
void gfx_poly(const int16_t *xy, unsigned n, int w16, uint32_t c, uint8_t a);
void gfx_area(int x, int y, int w, int h, const uint8_t *tops, uint32_t top, uint32_t bottom, uint8_t a);
uint32_t gfx_heat(unsigned t);
uint32_t gfx_bg(void);
uint32_t gfx_ink(unsigned k);
uint32_t gfx_mix(uint32_t a, uint32_t b, unsigned t);
uint32_t gfx_spectrum(unsigned b);
#endif
