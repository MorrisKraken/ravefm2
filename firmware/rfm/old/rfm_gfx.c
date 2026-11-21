/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: tb_gfx.h's calls on the display list (rfm_gfx.h). */
#include "rfm_gfx.h"
#include "tb_dsp.h"

const tb_font_t TB_FONT_S = {CV_S, 7}, TB_FONT_M = {CV_M, 11}, TB_FONT_L = {CV_L, 17}, TB_FONT_XL = {CV_L, 17};
int gfx_area_map;
static unsigned theme;

/* ------------------------------------------------------------------------------------- the picture area --- */
static int my(int y) { return gfx_area_map ? 80 + (y - 45) * 139 / 155 : y; }
static int mh(int y, int h) { return gfx_area_map ? my(y + h) - my(y) : h; }

void gfx_begin(void) { cv_begin(); }
unsigned gfx_overflows(void) { return 0; }
void gfx_end(void (*emit)(int x, int y, int w, int h, uint16_t *px), int force)
{
    (void)emit;
    gfx_area_map = 0;
    cv_end(force);
}
/* the themes: which colour of the gradient the drawings take (MENU > COLOUR) */
void gfx_palette(unsigned t) { theme = t < GFX_NPAL ? t : 0u; cv_hold(); }
unsigned gfx_palette_now(void) { return theme; }

/* ------------------------------------------------------------------------------------------------ colours --- */
static const uint8_t THEME_T[GFX_NPAL] = {40, 200, 110, 255};      /* the main colour's place in the gradient */
uint16_t gfx_role(unsigned role)
{
    unsigned m = THEME_T[theme];
    switch (role) {
    case PX_BG: return C_BG;
    case PX_PANEL: return C_SURF;
    case PX_LINE: return C_LINE;
    case PX_DIM: return C_DIM;
    case PX_MID: return cv_mix(C_BG, cv_ramp(m), 18);
    case PX_MAIN: return cv_ramp(m);
    case PX_HOT: return C_ACT;
    case PX_INK: return C_TEXT;
    case PX_GREY: return C_SUB;
    case PX_FAINT: return C_DIM;
    case PX_ALERT: return C_REC;
    case PX_AUX: return C_YELLOW;
    case PX_WARM: return C_ORANGE;
    case PX_AUXDK: return cv_mix(C_BG, C_YELLOW, 9);
    case PX_BAT: return C_VIOLET;
    default: return C_TEXT;
    }
}
uint16_t gfx_c(uint32_t c)
{
    if (c & 0x80000000u)
        return c & 0x40000000u ? (uint16_t)c : gfx_role(c & 15u);
    return CV_RGB((c >> 16) & 255u, (c >> 8) & 255u, c & 255u);
}
#define RAW(c565) (0xC0000000u | (c565))
uint32_t gfx_heat(unsigned t)
{
    if (t < 10u)
        return PXC(PX_BG);
    if (t < 24u)
        return PXC(PX_PANEL);
    if (t < 45u)
        return PXC(PX_LINE);
    if (t < 80u)
        return PXC(PX_DIM);
    if (t < 200u)                                       /* (up to the theme's colour, then on to the lit one) */
        return RAW(cv_mix(C_DIM, gfx_role(PX_MAIN), (t - 80u) * 32u / 120u));
    return RAW(cv_mix(gfx_role(PX_MAIN), C_ACT, (t - 200u) * 32u / 55u));
}
uint32_t gfx_bg(void) { return PXC(PX_BG); }
uint32_t gfx_ink(unsigned k) { return PXC(k == 0 ? PX_INK : k == 1 ? PX_GREY : PX_FAINT); }
uint32_t gfx_mix(uint32_t a, uint32_t b, unsigned t) { return RAW(cv_mix(gfx_c(a), gfx_c(b), t * 32u / 255u)); }
uint32_t gfx_spectrum(unsigned b) { return RAW(cv_ramp(b)); }
static uint16_t faded(uint32_t c, unsigned a) { return cv_mix(C_BG, gfx_c(c), a >= 250u ? 32u : a * 32u / 255u); }

/* ------------------------------------------------------------------------------------------------ shapes --- */
void gfx_rect(int x, int y, int w, int h, uint32_t c, uint8_t a, uint8_t mode)
{
    if (a < 16u || w <= 0 || h <= 0)
        return;
    if (mode == GFX_ADD || a < 200u) {                  /* (a light or a veil: a checker of its colour, mixed) */
        if (w <= 2 || h <= 2)
            cv_rect(x, my(y), w, mh(y, h), faded(c, a));
        else
            cv_dots(x, my(y), w, mh(y, h), faded(c, a * 2u > 255u ? 255u : a * 2u));
        return;
    }
    cv_rect(x, my(y), w, mh(y, h), gfx_c(c));
}
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, uint8_t a, int line)
{
    uint16_t col = faded(c, a);
    if (a < 16u || w <= 0 || h <= 0 || (gfx_area_map && y + h <= 45))
        return;
    if (line)
        cv_frame(x, my(y), w, mh(y, h), r > 4 ? 4 : r, col);
    else if (a < 120u)
        cv_dots(x, my(y), w, mh(y, h), col);
    else
        cv_box(x, my(y), w, mh(y, h), r > 4 ? 4 : r, col);
}
void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t a)
{
    cv_rect(x, my(y), w, mh(y, h) / 2, faded(top, a));
    cv_rect(x, my(y) + mh(y, h) / 2, w, mh(y, h) - mh(y, h) / 2, faded(bottom, a));
}
void gfx_title_text(const char *s);
int gfx_text(const tb_font_t *f, int x, int y, const char *s, uint32_t c, uint8_t a, int align)
{
    if (!f)
        f = &TB_FONT_S;
    if (gfx_area_map && y < 45) {                       /* (by the old title: to the breadcrumb) */
        gfx_title_text(s);
        return cv_text_w(f->font, s);
    }
    if (a < 60u)
        return cv_text_w(f->font, s);
    return cv_text(f->font, x, my(y) - f->ascent, s, faded(c, a), align == GFX_CENTER ? CV_CENTER : align == GFX_RIGHT ? CV_RIGHT : CV_LEFT);
}
int gfx_text_width(const tb_font_t *f, const char *s) { return cv_text_w(f ? f->font : CV_S, s); }

void gfx_wave(int x, int y, int w, int h, const uint8_t *lv, const uint8_t *ht, uint8_t hot)
{
    int i, yy = my(y), hh = mh(y, h), mid = yy + hh / 2, half = hh / 2 - 1;
    for (i = 0; i < w; i++) {
        int a = lv[i] * half / 255;
        uint16_t c = cv_ramp(ht ? 255u - ht[i] : 128u);
        if (!hot)
            c = cv_mix(C_BG, c, 14);
        if (a > 0)
            cv_rect(x + i, mid - a, 1, 2 * a + 1, c);
        else if (lv[i])
            cv_rect(x + i, mid, 1, 1, c);
    }
}
static void arc_pt(int cx, int cy, int r, int ang, int *px_, int *py_)
{
    uint32_t ph = (uint32_t)ang << 22;
    int s = tb_sin(ph), c = tb_sin(ph + 0x40000000u);
    *px_ = cx + (s * r + 16384) / 32768;
    *py_ = cy - (c * r + 16384) / 32768;
}
void gfx_arc(int cx, int cy, int r, int thick, int a0, int a1, uint32_t c, uint8_t a)
{
    int k, ang, steps;
    uint16_t col = faded(c, a);
    if (a < 40u || a1 <= a0)
        return;
    cy = my(cy);
    steps = r + 12;
    for (k = 0; k < (thick > 1 ? thick : 1); k++) {
        int rr = r - thick / 2 + k, x0 = 0, y0 = 0, first = 1;
        for (ang = a0;; ang += 1024 / steps) {
            int x1, y1, at = ang > a1 ? a1 : ang;
            arc_pt(cx, cy, rr, at, &x1, &y1);
            if (!first)
                cv_line(x0, y0, x1, y1, col);
            x0 = x1, y0 = y1, first = 0;
            if (at >= a1)
                break;
        }
    }
}
void gfx_line(int x0, int y0, int x1, int y1, int w16, uint32_t c, uint8_t a, uint8_t mode)
{
    uint16_t col = faded(c, a);
    (void)mode;
    if (a < 30u)
        return;
    y0 = my(y0), y1 = my(y1);
    cv_line(x0, y0, x1, y1, col);
    if (w16 >= 40) {
        int dx = x1 - x0, dy = y1 - y0;
        if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
            cv_line(x0, y0 + 1, x1, y1 + 1, col);
        else
            cv_line(x0 + 1, y0, x1 + 1, y1, col);
    }
}
void gfx_glow(int cx, int cy, int r, uint32_t c, uint8_t a)
{
    if (a > 60u)
        cv_circle(cx, my(cy), r / 2 > 1 ? r / 2 : 1, faded(c, a / 2u));
}
void gfx_poly(const int16_t *xy, unsigned n, int w16, uint32_t c, uint8_t a)
{
    unsigned i;
    for (i = 1; i < n; i++)
        gfx_line(xy[2 * i - 2], xy[2 * i - 1], xy[2 * i], xy[2 * i + 1], w16, c, a, GFX_NORMAL);
}
void gfx_area(int x, int y, int w, int h, const uint8_t *tops, uint32_t top, uint32_t bottom, uint8_t a)
{
    int i, yy = my(y), hh = mh(y, h);
    uint16_t ct = faded(top, a), cb = faded(bottom, a / 2u);
    for (i = 0; i < w; i++) {
        int t = tops[i] * hh / (h ? h : 1);
        if (t < hh) {
            cv_rect(x + i, yy + t + 1, 1, hh - t - 1, cb);
            cv_rect(x + i, yy + t, 1, 1, ct);
        }
    }
}
