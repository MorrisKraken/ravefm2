/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the screen. A frame is a list of drawing commands (boxes, lines, gradients, smooth text, sprites); at its
 * end the list is drawn band by band (240 x 16, true colour, text blended), each 16 x 16 tile compared with what the
 * panel shows (a strong hash), and only the tiles that changed are sent. No frame buffer. */
#ifndef RFM_CV_H
#define RFM_CV_H
#include <stdint.h>

#define CV_W 240
#define CV_H 240
#define CV_RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

/* the colours (RAVE FM's, hardcore: black, blood red, white, the acid yellow of the warning stripes); the gradient
 * goes through fire: dark red, red, orange, yellow, white */
#define C_BG CV_RGB(0, 0, 0)
#define C_SURF CV_RGB(26, 8, 8)
#define C_SURF2 CV_RGB(48, 14, 14)
#define C_LINE CV_RGB(74, 22, 22)
#define C_TEXT CV_RGB(255, 255, 255)
#define C_SUB CV_RGB(168, 168, 168)
#define C_DIM CV_RGB(92, 92, 92)
#define C_RED CV_RGB(230, 10, 10)
#define C_ORANGE CV_RGB(255, 100, 0)
#define C_YELLOW CV_RGB(245, 255, 0)
#define C_TEAL C_YELLOW
#define C_ACID C_YELLOW
#define C_PINK C_RED
#define C_VIOLET CV_RGB(150, 60, 255)
#define C_SEL C_YELLOW               /* what is selected */
#define C_ACT C_RED                  /* what is lit, playing now */
#define C_REC CV_RGB(255, 30, 30)
#define C_BLACK 0

enum { CV_S, CV_M, CV_L, CV_XL, CV_T };   /* the pixel face (labels), Russo 13, Anton 24 / 38, stencil 16 */
enum { CV_LEFT, CV_CENTER, CV_RIGHT };

void cv_begin(void);
void cv_end(int force);              /* draws the frame and sends what changed */
void cv_hold(void);                  /* the panel shows something else (the boot): the next frame sends all */
uint16_t cv_ramp(unsigned t);        /* the gradient: 0 dark red .. red .. orange .. yellow .. 255 white */
uint16_t cv_mix(uint16_t a, uint16_t b, unsigned t);   /* t 0..32 */
void cv_rect(int x, int y, int w, int h, uint16_t c);
void cv_box(int x, int y, int w, int h, int r, uint16_t c);      /* corners rounded (r 0..4) */
void cv_frame(int x, int y, int w, int h, int r, uint16_t c);    /* .. its outline */
void cv_hgrad(int x, int y, int w, int h, unsigned t0, unsigned t1);   /* the gradient from t0 to t1, left to right */
void cv_line(int x0, int y0, int x1, int y1, uint16_t c);
void cv_circle(int cx, int cy, int r, uint16_t c);
void cv_disc(int cx, int cy, int r, uint16_t c);
void cv_dots(int x, int y, int w, int h, uint16_t c);            /* a checker: every other pixel */
void cv_stripes(int x, int y, int w, int h, uint16_t a, uint16_t b);   /* warning stripes, diagonal, 4 px */
/* a trapezoid: rows y..y+h, from x xt (width wt) on top to xb (wb) at the bottom; a16 its opacity 1..15 (15: solid) */
void cv_trap(int y, int h, int xt, int wt, int xb, int wb, uint16_t c, unsigned a16);
void cv_skull(int cx, int cy, int r, uint16_t bone, uint16_t eyes);  /* a skull, r its half width */
int cv_text(int font, int x, int y, const char *s, uint16_t c, int align);   /* y: the text's top; its width */
int cv_text_w(int font, const char *s);
int cv_font_h(int font);
/* a sprite: run-length (each byte (run - 1) << 4 | colour, 0 clear) w x h, its 16 colours, x scale, mirrored */
void cv_sprite(const uint8_t *rle, const uint16_t *pal, int w, int h, int x, int y, int scale, int flip);
/* the boot screen's picture: same coding, straight on the black, faded to level 0..16 */
void cv_boot(const uint8_t *rle, unsigned n, const uint8_t (*rgb)[3], int w, int h, int y, unsigned level);
#endif
