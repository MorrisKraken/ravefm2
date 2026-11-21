/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the screen's rasteriser (tb_gfx.h). */
#include "tb_gfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { OP_RECT, OP_RRECT, OP_VGRAD, OP_TEXT, OP_WAVE, OP_ARC, OP_LINE, OP_GLOW, OP_LOGO, OP_DANCE, OP_POLY, OP_AREA };
#include "tb_logo.h"
#include "tb_dance.h"

/* a drawing command: 28 bytes (the FM-1's RAM). p[]: each op's own; the ops that keep data in the arena (text, wave,
 * polyline, area) keep its place there in p[2] (not part of what the command's look is hashed by: its data is) */
typedef struct {
    uint8_t op, mode, a, hot;
    int16_t x, y, w, h;          /* bounding box (clipping, dirty tiles) */
    uint32_t c;
    int16_t p[6];
} cmd_t;
#define C2(c) ((uint32_t)(uint16_t)(c)->p[0] << 16 | (uint16_t)(c)->p[1])   /* a second colour (VGRAD, AREA) */
#define AT(c) (arena + (uint16_t)(c)->p[2])                                     /* its data in the arena */

#if TB_LITE
#define NCMD 288                         /* (the FM-1: its RAM; the most a page takes: ~240) */
#define ARENA 4096                       /* (the most a frame takes: ~3 KB) */
#else
#define NCMD 512
#define ARENA 8192
#endif
static cmd_t cmds[NCMD];
static unsigned ncmd, arena_used, overflow;
static uint8_t arena[ARENA] __attribute__((aligned(4)));
static const tb_font_t *fonts[8];        /* (a text's font: its index here) */
static unsigned nfonts;
/* the screen, cut in bands of TB_BAND rows, each in tiles of TB_TILE columns: a hash of what each tile shows, kept;
 * a band goes out as the one window that holds its changed tiles (JIANT's lesson, Felucca's FM-1: at 12 MHz the whole
 * panel takes ~77 ms, the changes alone a few) */
#define TB_TILE 16
#define NTILE (TB_W / TB_TILE)
static uint32_t tile_hash[TB_H / TB_BAND][NTILE];
static uint32_t tile_now[NTILE];
/* two band buffers, RGB565: one drawn while the other goes out (the panel's DMA: the FM-1's band transfer) */
static uint16_t pix[2][TB_BAND * TB_W] __attribute__((aligned(4)));
static unsigned pix_k;
static uint16_t *px;                     /* the band drawn now: its window's rows, win_w pixels each */
static int win_x0, win_x1, win_w;
static uint32_t heat_lut[256];           /* (filled before use: heat_ready) */
static int heat_ready;

/* ------------------------------------------------------------------ colours --- */
uint32_t gfx_mix(uint32_t a, uint32_t b, unsigned t)
{
    unsigned r = ((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t;
    unsigned g = ((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t;
    unsigned bl = (a & 255) * (256 - t) + (b & 255) * t;
    return TB_RGB(r >> 8, g >> 8, bl >> 8);
}

/* the palettes: a scale from black through the theme's colours to near white, and the ink of the words */
static const struct { uint8_t at; uint32_t c; } PAL[GFX_NPAL][8] = {
    {   /* RAVE: Rave FM's purple (its ground, its panels, its violet #bd75ff), then the Thunderdome's orange when lit */
        {0, TB_RGB(12, 2, 20)},      {26, TB_RGB(42, 14, 60)},    {60, TB_RGB(70, 38, 94)},
        {110, TB_RGB(123, 73, 165)}, {165, TB_RGB(168, 102, 242)}, {200, TB_RGB(189, 117, 255)},
        {226, TB_RGB(255, 140, 64)}, {255, TB_RGB(255, 230, 196)},
    },
    {   /* INDIGO: indigo, blue-violet, lavender */
        {0, TB_RGB(0, 0, 0)},        {40, TB_RGB(14, 6, 46)},     {90, TB_RGB(48, 20, 140)},
        {135, TB_RGB(84, 52, 222)},  {175, TB_RGB(116, 96, 255)}, {210, TB_RGB(152, 140, 255)},
        {238, TB_RGB(198, 192, 255)}, {255, TB_RGB(236, 238, 255)},
    },
    {   /* MATRIX: the green of the terminal */
        {0, TB_RGB(0, 0, 0)},        {40, TB_RGB(2, 22, 8)},      {90, TB_RGB(6, 72, 26)},
        {135, TB_RGB(14, 140, 50)},  {175, TB_RGB(34, 205, 80)},  {210, TB_RGB(70, 250, 120)},
        {238, TB_RGB(160, 255, 185)}, {255, TB_RGB(225, 255, 232)},
    },
    {   /* FEU: red, orange, yellow */
        {0, TB_RGB(0, 0, 0)},        {40, TB_RGB(60, 4, 0)},      {90, TB_RGB(150, 18, 0)},
        {135, TB_RGB(225, 60, 0)},   {175, TB_RGB(255, 118, 0)},  {210, TB_RGB(255, 178, 10)},
        {238, TB_RGB(255, 228, 70)}, {255, TB_RGB(255, 250, 200)},
    },
};
static unsigned pal_now;
static const uint32_t BG[GFX_NPAL] = {TB_RGB(26, 6, 40), TB_RGB(4, 2, 12), TB_RGB(0, 5, 2), TB_RGB(8, 2, 0)};
uint32_t gfx_bg(void) { return BG[pal_now]; }
static uint32_t ink_c[3];                /* the words: bright, quiet, dim */

static void heat_init(void)
{
    unsigned i, k = 0;
    for (i = 0; i < 256; i++) {
        while (k + 1 < 7 && i > PAL[pal_now][k + 1].at)
            k++;
        heat_lut[i] = gfx_mix(PAL[pal_now][k].c, PAL[pal_now][k + 1].c,
                              (i - PAL[pal_now][k].at) * 256u / (PAL[pal_now][k + 1].at - PAL[pal_now][k].at));
    }
    ink_c[0] = gfx_mix(TB_RGB(255, 255, 255), heat_lut[230], 70);
    ink_c[1] = gfx_mix(TB_RGB(128, 128, 128), heat_lut[150], 70);
    ink_c[2] = gfx_mix(TB_RGB(70, 70, 70), heat_lut[110], 60);
    heat_ready = 1;
}

void gfx_palette(unsigned p)
{
    pal_now = TB_HAS_THEMES && p < GFX_NPAL ? p : 0;
    heat_init();
}
unsigned gfx_palette_now(void) { return pal_now; }
uint32_t gfx_ink(unsigned k) { if (!heat_ready) heat_init(); return ink_c[k < 3 ? k : 2]; }

uint32_t gfx_heat(unsigned t)
{
    if (!heat_ready)
        heat_init();
    return heat_lut[t > 255 ? 255 : t];
}

/* the lanes' colours by what they hold, as on a DJ's screen: the bass warm, the mids the theme's, the treble cold */
static const uint32_t SPEC[GFX_NPAL][3] = {
    {TB_RGB(255, 122, 26), TB_RGB(189, 117, 255), TB_RGB(255, 176, 240)},
    {TB_RGB(255, 60, 140), TB_RGB(130, 96, 255), TB_RGB(90, 225, 255)},
    {TB_RGB(150, 230, 40), TB_RGB(40, 210, 90), TB_RGB(120, 255, 230)},
    {TB_RGB(220, 20, 10), TB_RGB(255, 130, 0), TB_RGB(255, 245, 150)},
};
uint32_t gfx_spectrum(unsigned b)
{
    const uint32_t *c = SPEC[pal_now];
    b = b > 255 ? 255 : b;
    return b < 128 ? gfx_mix(c[0], c[1], b * 2u) : gfx_mix(c[1], c[2], (b - 128u) * 2u);
}

/* ---------------------------------------------------------------- recording --- */
static cmd_t *add(uint8_t op, int x, int y, int w, int h)
{
    cmd_t *c;
    if (ncmd >= NCMD || w <= 0 || h <= 0 || x >= TB_W || y >= TB_H || x + w <= 0 || y + h <= 0) {
        overflow += ncmd >= NCMD;
        return 0;
    }
    c = &cmds[ncmd++];
    memset(c, 0, sizeof *c);
    c->op = op, c->x = (int16_t)x, c->y = (int16_t)y, c->w = (int16_t)w, c->h = (int16_t)h;
    return c;
}

/* n bytes into the arena: their place (-1: no room) */
static int keep(const void *p, unsigned n)
{
    unsigned at = arena_used;
    if (arena_used + n > ARENA)
        return -1;
    memcpy(arena + at, p, n);
    arena_used += (n + 3u) & ~3u;
    return (int)at;
}

unsigned gfx_overflows(void) { return overflow; }   /* (the commands a frame could not hold, all told) */
static unsigned peak_cmd, peak_arena;
unsigned gfx_peak(unsigned arena_too) { return arena_too ? peak_arena : peak_cmd; }   /* (tests: the most a frame took) */
void gfx_begin(void)
{
    if (ncmd > peak_cmd)
        peak_cmd = ncmd;
    if (arena_used > peak_arena)
        peak_arena = arena_used;
    ncmd = arena_used = 0;
    if (!heat_ready)
        heat_init();
}

void gfx_rect(int x, int y, int w, int h, uint32_t col, uint8_t a, uint8_t mode)
{
    cmd_t *c = add(OP_RECT, x, y, w, h);
    if (c)
        c->c = col, c->a = a, c->mode = mode;
}

void gfx_rrect(int x, int y, int w, int h, int r, uint32_t col, uint8_t a, int line)
{
    cmd_t *c = add(OP_RRECT, x, y, w, h);
    if (c)
        c->c = col, c->a = a, c->p[0] = (int16_t)r, c->p[1] = (int16_t)line;
}

void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t a)
{
    cmd_t *c = add(OP_VGRAD, x, y, w, h);
    if (c)
        c->c = top, c->p[0] = (int16_t)(bottom >> 16), c->p[1] = (int16_t)bottom, c->a = a;
}

static uint32_t utf8(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t u = *p++;
    if (u >= 0xC0 && u < 0xE0 && (*p & 0xC0) == 0x80)
        u = ((u & 31) << 6) | (*p++ & 63);
    else if (u >= 0xE0 && u < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
        u = ((u & 15) << 12) | ((uint32_t)(p[0] & 63) << 6) | (p[1] & 63);
        p += 2;
    }
    *s = (const char *)p;
    return u;
}

static const tb_glyph_t *glyph(const tb_font_t *f, uint32_t code)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (f->g[m].code == code)
            return &f->g[m];
        if (f->g[m].code < code)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return 0;
}

int gfx_text_width(const tb_font_t *f, const char *s)
{
    int w = 0;
    while (*s) {
        const tb_glyph_t *g = glyph(f, utf8(&s));
        if (g)
            w += g->adv;
    }
    return w;
}

int gfx_text(const tb_font_t *f, int x, int y, const char *s, uint32_t col, uint8_t a, int align)
{
    int w = gfx_text_width(f, s);
    cmd_t *c;
    if (align == GFX_CENTER)
        x -= w / 2;
    else if (align == GFX_RIGHT)
        x -= w;
    c = add(OP_TEXT, x - 2, y - f->ascent, w + 6, f->ascent + f->descent + 2);   /* (the box: what its glyphs may touch) */
    if (c) {
        int at = keep(s, (unsigned)strlen(s) + 1u);
        unsigned k;
        for (k = 0; k < nfonts && fonts[k] != f; k++)
            ;
        if (k == nfonts && nfonts < 8u)
            fonts[nfonts++] = f;
        c->c = col, c->a = a, c->p[0] = (int16_t)x, c->p[1] = (int16_t)y, c->p[2] = (int16_t)at, c->p[3] = (int16_t)k;
        if (at < 0 || k >= nfonts)
            ncmd--;
    }
    return w;
}

void gfx_wave(int x, int y, int w, int h, const uint8_t *levels, const uint8_t *heats, uint8_t hot)
{
    cmd_t *c = add(OP_WAVE, x, y, w, h);
    if (c) {
        int at = keep(levels, (unsigned)w);
        if (at >= 0 && keep(heats, (unsigned)w) < 0)
            at = -1;
        c->hot = hot, c->p[2] = (int16_t)at;
        if (at < 0)
            ncmd--;
    }
}

void gfx_arc(int cx, int cy, int r, int thick, int a0, int a1, uint32_t col, uint8_t a)
{
    cmd_t *c = add(OP_ARC, cx - r - 1, cy - r - 1, 2 * r + 3, 2 * r + 3);
    if (c)
        c->c = col, c->a = a, c->p[0] = (int16_t)cx, c->p[1] = (int16_t)cy, c->p[2] = (int16_t)r,
        c->p[3] = (int16_t)thick, c->p[4] = (int16_t)a0, c->p[5] = (int16_t)a1;
}

void gfx_line(int x0, int y0, int x1, int y1, int w16, uint32_t col, uint8_t a, uint8_t mode)
{
    int pad = w16 / 16 + 2;
    int lx = x0 < x1 ? x0 : x1, ly = y0 < y1 ? y0 : y1;
    cmd_t *c = add(OP_LINE, lx - pad, ly - pad, abs(x1 - x0) + 2 * pad + 1, abs(y1 - y0) + 2 * pad + 1);
    if (c)
        c->c = col, c->a = a, c->mode = mode, c->p[0] = (int16_t)x0, c->p[1] = (int16_t)y0, c->p[2] = (int16_t)x1,
        c->p[3] = (int16_t)y1, c->p[4] = (int16_t)w16;
}

void gfx_glow(int cx, int cy, int r, uint32_t col, uint8_t a)
{
    cmd_t *c = add(OP_GLOW, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    if (c)
        c->c = col, c->a = a, c->p[0] = (int16_t)cx, c->p[1] = (int16_t)cy, c->p[2] = (int16_t)r;
}

void gfx_poly(const int16_t *xy, unsigned n, int w16, uint32_t col, uint8_t a)
{
    int lx = 32767, ly = 32767, hx = -32768, hy = -32768, pad = w16 / 16 + 2, at;
    unsigned i;
    cmd_t *c;
    if (n < 2u)
        return;
    for (i = 0; i < n; i++) {
        lx = xy[2 * i] < lx ? xy[2 * i] : lx, hx = xy[2 * i] > hx ? xy[2 * i] : hx;
        ly = xy[2 * i + 1] < ly ? xy[2 * i + 1] : ly, hy = xy[2 * i + 1] > hy ? xy[2 * i + 1] : hy;
    }
    c = add(OP_POLY, lx - pad, ly - pad, hx - lx + 2 * pad + 1, hy - ly + 2 * pad + 1);
    if (!c)
        return;
    at = keep(xy, n * 4u);
    c->c = col, c->a = a, c->p[2] = (int16_t)at, c->p[3] = (int16_t)n, c->p[4] = (int16_t)w16;
    if (at < 0)
        ncmd--;
}

void gfx_area(int x, int y, int w, int h, const uint8_t *tops, uint32_t top, uint32_t bottom, uint8_t a)
{
    cmd_t *c = add(OP_AREA, x, y, w, h);
    int at;
    if (!c)
        return;
    at = keep(tops, (unsigned)w);
    c->c = top, c->p[0] = (int16_t)(bottom >> 16), c->p[1] = (int16_t)bottom, c->a = a, c->p[2] = (int16_t)at;
    if (at < 0)
        ncmd--;
}

void gfx_logo(int x, int y, uint8_t a)
{
    cmd_t *c = add(OP_LOGO, x, y, TB_LOGO_W, TB_LOGO_H);
    if (c)
        c->a = a, c->p[0] = (int16_t)x, c->p[1] = (int16_t)y;
}

void gfx_dance(unsigned frame, uint32_t col)
{
#if TB_DANCE
    cmd_t *c = add(OP_DANCE, 0, 0, 2 * DANCE_W, 2 * DANCE_H);
    if (c)
        c->c = col, c->p[0] = (int16_t)(frame % (DANCE_BEATS * DANCE_PER));
#else
    (void)frame, (void)col;
#endif
}
unsigned gfx_dance_frames(void)
{
#if TB_DANCE
    return DANCE_BEATS * DANCE_PER;
#else
    return 0;
#endif
}

/* -------------------------------------------------------------- rasterising --- */
static int band_y0;
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* RGB565 <-> 8 bits a channel; written back through the ordered dither (the gradients without steps) */
static inline uint16_t pack(int x, int y, unsigned r, unsigned g, unsigned b)
{
    unsigned d = BAYER[y & 3][x & 3];
    r = (r + (d >> 1)) >> 3, g = (g + (d >> 2)) >> 2, b = (b + (d >> 1)) >> 3;
    return (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b));
}
#define R8(v) ((((v) >> 11) << 3) | ((v) >> 13))
#define G8(v) ((((v) >> 5 & 63) << 2) | ((v) >> 9 & 3))
#define B8(v) ((((v) & 31) << 3) | ((v) >> 2 & 7))
/* the window's columns of a command: from, to */
#define X0(c) ((c)->x > win_x0 ? (c)->x : win_x0)
#define X1(c) ((c)->x + (c)->w < win_x1 ? (c)->x + (c)->w : win_x1)

static inline void px_blend(int x, int y, uint32_t col, unsigned a)   /* a 0..256 */
{
    uint16_t *d;
    int r, g, b;
    if ((unsigned)(x - win_x0) >= (unsigned)win_w || (unsigned)(y - band_y0) >= TB_BAND || !a)
        return;
    d = px + (y - band_y0) * win_w + (x - win_x0);
    if (a >= 256u) {
        *d = pack(x, y, (col >> 16) & 255u, (col >> 8) & 255u, col & 255u);
        return;
    }
    r = (int)R8(*d), g = (int)G8(*d), b = (int)B8(*d);
    r += ((((int)(col >> 16) & 255) - r) * (int)a) >> 8;
    g += ((((int)(col >> 8) & 255) - g) * (int)a) >> 8;
    b += ((((int)col & 255) - b) * (int)a) >> 8;
    *d = pack(x, y, (unsigned)r, (unsigned)g, (unsigned)b);
}

static inline void px_add(int x, int y, uint32_t col, unsigned a)
{
    uint16_t *d;
    unsigned r, g, b;
    if ((unsigned)(x - win_x0) >= (unsigned)win_w || (unsigned)(y - band_y0) >= TB_BAND || !a)
        return;
    d = px + (y - band_y0) * win_w + (x - win_x0);
    r = R8(*d) + (((col >> 16) & 255) * a >> 8);
    g = G8(*d) + (((col >> 8) & 255) * a >> 8);
    b = B8(*d) + ((col & 255) * a >> 8);
    *d = pack(x, y, r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

static void rows(const cmd_t *c, int *y0, int *y1)
{
    *y0 = c->y > band_y0 ? c->y : band_y0;
    *y1 = c->y + c->h < band_y0 + TB_BAND ? c->y + c->h : band_y0 + TB_BAND;
}

static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* atan2(y, x) / 2 pi in turns, within 0.0015 turn (no maths library) */
static float turns(float y, float x)
{
    float ax = __builtin_fabsf(x), ay = __builtin_fabsf(y), a = (ax > ay ? ay / (ax > 0 ? ax : 1) : ax / (ay > 0 ? ay : 1)), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   /* atan(a), a in 0..1 */
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0)
        r = 3.14159274f - r;
    if (y < 0)
        r = -r;
    return r / 6.28318531f;
}

/* signed distance from a pixel centre to a rounded rectangle's edge (negative inside) */
static float rr_dist(const cmd_t *c, float px, float py)
{
    float r = c->p[0], hx = c->w * 0.5f, hy = c->h * 0.5f;
    float qx = __builtin_fabsf(px - (c->x + hx)) - (hx - r), qy = __builtin_fabsf(py - (c->y + hy)) - (hy - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return __builtin_sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

/* an anti-aliased segment of a line (OP_LINE, OP_POLY), its rows y0 .. y1 */
static void segment(const cmd_t *c, int x0, int y0_, int x1, int y1_, unsigned a, int ya, int yb)
{
    float ax = x0 + 0.5f, ay = y0_ + 0.5f, bx = x1 + 0.5f, by = y1_ + 0.5f;
    float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy, hw = c->p[4] / 32.0f, il2 = l2 > 0 ? 1.0f / l2 : 0;
    int pad = c->p[4] / 16 + 2, lx = (x0 < x1 ? x0 : x1) - pad, hx = (x0 < x1 ? x1 : x0) + pad + 1, x, y;
    if (lx < X0(c))
        lx = X0(c);
    if (hx > X1(c))
        hx = X1(c);
    for (y = ya; y < yb; y++)
        for (x = lx; x < hx; x++) {
            float qx = x + 0.5f, qy = y + 0.5f, t = ((qx - ax) * vx + (qy - ay) * vy) * il2, dx, dy, cov, d2;
            t = clampf(t);
            dx = qx - (ax + t * vx), dy = qy - (ay + t * vy), d2 = dx * dx + dy * dy;
            if (d2 > (hw + 0.5f) * (hw + 0.5f))
                continue;
            cov = clampf(hw + 0.5f - __builtin_sqrtf(d2));
            if (c->mode == GFX_ADD)
                px_add(x, y, c->c, (unsigned)(cov * a));
            else
                px_blend(x, y, c->c, (unsigned)(cov * a));
        }
}

static void draw(const cmd_t *c)
{
    int x, y, y0, y1;
    unsigned a = c->a + (c->a >> 7);
    rows(c, &y0, &y1);
    switch (c->op) {
    case OP_RECT:
        for (y = y0; y < y1; y++)
            for (x = X0(c); x < X1(c); x++) {
                if (c->mode == GFX_ADD)
                    px_add(x, y, c->c, a);
                else
                    px_blend(x, y, c->c, a);
            }
        break;
    case OP_RRECT: {
        int r = c->p[0], line = c->p[1];
        for (y = y0; y < y1; y++)
            for (x = X0(c); x < X1(c); x++) {
                float cov, d;
                int nearc = (x < c->x + r || x >= c->x + c->w - r) && (y < c->y + r || y >= c->y + c->h - r);
                if (!nearc && !line) {
                    px_blend(x, y, c->c, a);
                    continue;
                }
                if (!nearc && x >= c->x + line && x < c->x + c->w - line && y >= c->y + line && y < c->y + c->h - line)
                    continue;
                d = rr_dist(c, x + 0.5f, y + 0.5f);
                cov = clampf(0.5f - d);
                if (line)
                    cov -= clampf(0.5f - d - line);
                px_blend(x, y, c->c, (unsigned)(cov * a));
            }
        break;
    }
    case OP_VGRAD:
        for (y = y0; y < y1; y++) {
            uint32_t col = gfx_mix(c->c, C2(c), (unsigned)((y - c->y) * 256 / (c->h > 1 ? c->h - 1 : 1)));
            for (x = X0(c); x < X1(c); x++)
                px_blend(x, y, col, a);
        }
        break;
    case OP_TEXT: {
        const char *s = (const char *)AT(c);
        const tb_font_t *f = fonts[c->p[3]];
        int pen = c->p[0], base = c->p[1];
        while (*s) {
            const tb_glyph_t *g = glyph(f, utf8(&s));
            int gx, gy, top;
            if (!g)
                continue;
            top = base - f->ascent + g->y;
            if (top < band_y0 + TB_BAND && top + g->h > band_y0 && pen + g->x + g->w > win_x0 && pen + g->x < win_x1)
                for (gy = 0; gy < g->h; gy++) {
                    int yy = top + gy;
                    if (yy < band_y0 || yy >= band_y0 + TB_BAND)
                        continue;
                    for (gx = 0; gx < g->w; gx++) {
                        unsigned i = (unsigned)(gy * g->w + gx), v = f->data[g->off + (i >> 1)];
                        v = (i & 1u) ? v >> 4 : v & 15u;
                        if (v)
                            px_blend(pen + g->x + gx, yy, c->c, v * 17u * a / 255u);
                    }
                }
            pen += g->adv;
        }
        break;
    }
    case OP_WAVE: {
        const uint8_t *pk = AT(c), *ht = pk + ((c->w + 3) & ~3);
        int cy16 = c->y * 16 + c->h * 8, half16 = c->h * 8 - 16, i;
        for (i = X0(c) - c->x; i < X1(c) - c->x; i++) {
            int xx = c->x + i, v = pk[i], hh = v * half16 / 255;
            uint32_t col = gfx_spectrum(ht[i]);
            for (y = y0; y < y1; y++) {
                int dy = y * 16 + 8 - cy16;
                if (dy < 0)
                    dy = -dy;
                if (dy <= hh + 16 && v) {
                    /* the body in the colour of what it holds, brighter along the centre, the tip anti-aliased */
                    int t = 256 - dy * 256 / (hh + 16), cov = hh + 16 - dy;
                    unsigned lift = (unsigned)(t * 70 >> 8) + c->hot / 3u;
                    uint32_t cc = gfx_mix(gfx_mix(TB_RGB(0, 0, 0), col, 150 + (unsigned)(v * 106 / 255)), TB_RGB(255, 255, 255), lift > 200 ? 200 : lift);
                    px_blend(xx, y, cc, (unsigned)(cov >= 16 ? 256 : cov * 16));
                } else if (dy < 8) {
                    px_blend(xx, y, gfx_heat(36 + c->hot / 6u), 256);              /* the silent line */
                }
            }
        }
        break;
    }
    case OP_ARC: {
        float cx = c->p[0], cy = c->p[1], r = c->p[2], th = c->p[3];
        float rm = r - th * 0.5f, a0 = c->p[4] / 1024.0f, a1 = c->p[5] / 1024.0f;
        for (y = y0; y < y1; y++)
            for (x = X0(c); x < X1(c); x++) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = __builtin_sqrtf(dx * dx + dy * dy);
                float cov = clampf(th * 0.5f + 0.5f - __builtin_fabsf(d - rm)), ang;
                if (cov <= 0)
                    continue;
                ang = turns(dx, -dy);
                if (ang < 0)
                    ang += 1;
                if (ang < a0 || ang > a1) {      /* the ends: soft by the arc length outside */
                    float o = ang < a0 ? a0 - ang : ang - a1;
                    if (o > 0.5f)
                        o = 1 - o;
                    cov *= clampf(1 - o * 6.2831853f * d);
                }
                px_blend(x, y, c->c, (unsigned)(cov * a));
            }
        break;
    }
    case OP_LINE:
        segment(c, c->p[0], c->p[1], c->p[2], c->p[3], a, y0, y1);
        break;
    case OP_POLY: {                              /* a line through its points, each segment as OP_LINE draws one */
        const int16_t *q = (const int16_t *)AT(c);
        int i, pad = c->p[4] / 16 + 2;
        for (i = 0; i + 1 < c->p[3]; i++, q += 2) {
            int lo = q[1] < q[3] ? q[1] : q[3], hi = q[1] < q[3] ? q[3] : q[1];
            int lx = q[0] < q[2] ? q[0] : q[2], hx = q[0] < q[2] ? q[2] : q[0];
            if (lo - pad < y1 && hi + pad >= y0 && lx - pad < win_x1 && hx + pad >= win_x0)
                segment(c, q[0], q[1], q[2], q[3], a, y0 > lo - pad ? y0 : lo - pad, y1 < hi + pad + 1 ? y1 : hi + pad + 1);
        }
        break;
    }
    case OP_AREA: {                              /* columns from their tops (tops[i]: rows under y) down to the floor */
        const uint8_t *t = AT(c);
        uint32_t c2 = C2(c);
        for (x = X0(c); x < X1(c); x++) {        /* (each column its gradient, from its top to the floor) */
            int top = t[x - c->x], span = c->h - top - 1 > 0 ? c->h - top - 1 : 1;
            for (y = y0 > c->y + top ? y0 : c->y + top; y < y1; y++)
                px_blend(x, y, gfx_mix(c->c, c2, (unsigned)((y - c->y - top) * 256 / span)), a);
        }
        break;
    }
    case OP_LOGO: {                              /* the bat, cut out: its brightness through the theme's violet, over */
        static uint32_t ramp[256];
        static unsigned ramp_pal = 255;
        if (ramp_pal != pal_now || !heat_ready) {
            unsigned i;
            if (!heat_ready)
                heat_init();
            for (i = 0; i < 256; i++)
                ramp[i] = i < 190 ? gfx_mix(TB_RGB(6, 0, 10), heat_lut[200], i * 256u / 190u)
                                  : gfx_mix(heat_lut[200], TB_RGB(244, 232, 255), (i - 190u) * 256u / 66u);
            ramp_pal = pal_now;
        }
        for (y = y0; y < y1; y++) {
            const unsigned char *row = TB_LOGO + (y - c->p[1]) * TB_LOGO_W, *al = TB_LOGO_A + (y - c->p[1]) * TB_LOGO_W;
            for (x = X0(c); x < X1(c); x++) {
                unsigned k = al[x - c->p[0]];     /* (outside the bat: nothing drawn, the ground stays as it is) */
                if (k)
                    px_blend(x, y, ramp[row[x - c->p[0]]], (k + (k >> 7)) * a >> 8);
            }
        }
        break;
    }
#if TB_DANCE
    case OP_DANCE: {                             /* the dancer: its runs (off, on, ..) to the band's rows, each pixel x2 */
        /* (the bands come down the screen: the next one goes on from the run where this one stopped) */
        static const uint8_t *cur_p;
        static unsigned cur_at, cur_on, cur_f = 0xFFFFu;
        const uint8_t *p = DANCE_RUNS + DANCE_OFF[c->p[0]];
        unsigned at = 0, on = 0, sy0 = (unsigned)y0 / 2u, sy1 = ((unsigned)y1 + 1u) / 2u, from = sy0 * DANCE_W, to = sy1 * DANCE_W;
        if (cur_f == (unsigned)c->p[0] && cur_at <= from)
            p = cur_p, at = cur_at, on = cur_on;
        while (at < to) {
            const uint8_t *p0 = p;
            unsigned n = 0, b;
            do
                b = *p++, n += b;
            while (b == 255u);
            if (at + n >= to)
                cur_p = p0, cur_at = at, cur_on = on, cur_f = (unsigned)c->p[0];
            if (on && at + n > from) {           /* lit: the part of the run in these rows */
                unsigned i = at < from ? from : at, e = at + n < to ? at + n : to;
                for (; i < e; i++) {
                    int sx = (int)(i % DANCE_W) * 2, yy = (int)(i / DANCE_W) * 2;
                    px_blend(sx, yy, c->c, 256), px_blend(sx + 1, yy, c->c, 256);
                    px_blend(sx, yy + 1, c->c, 256), px_blend(sx + 1, yy + 1, c->c, 256);
                }
            }
            at += n, on ^= 1u;
        }
        break;
    }
#endif
    case OP_GLOW: {
        float cx = c->p[0] + 0.5f, cy = c->p[1] + 0.5f, r = c->p[2];
        for (y = y0; y < y1; y++)
            for (x = X0(c); x < X1(c); x++) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy, k = 1 - __builtin_sqrtf(dx * dx + dy * dy) / r;
                if (k > 0)
                    px_add(x, y, c->c, (unsigned)(k * k * a));
            }
        break;
    }
    }
}

static uint32_t mixh(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* what a command looks like, hashed: its parameters (not its place in the arena) and its data */
static uint32_t cmd_sig(const cmd_t *c)
{
    const uint32_t *w = (const uint32_t *)c;
    uint32_t h = 2166136261u;
    unsigned k;
    for (k = 0; k < 4u; k++)                     /* op .. c */
        h = mixh(h, w[k]);
    for (k = 0; k < 6u; k++)
        if (k != 2u || (c->op != OP_TEXT && c->op != OP_WAVE && c->op != OP_POLY && c->op != OP_AREA))
            h = mixh(h, (uint16_t)c->p[k]);
    if (c->op == OP_TEXT) {
        const uint8_t *s = AT(c);
        while (*s)
            h = mixh(h, *s++);
    } else if (c->op == OP_WAVE) {
        const uint8_t *s = AT(c);
        for (k = 0; k < (unsigned)c->w; k++)
            h = mixh(h, s[k] | (unsigned)s[k + ((c->w + 3u) & ~3u)] << 8);
    } else if (c->op == OP_POLY) {
        const uint16_t *s = (const uint16_t *)AT(c);
        for (k = 0; k < 2u * (unsigned)c->p[3]; k++)
            h = mixh(h, s[k]);
    } else if (c->op == OP_AREA) {
        const uint8_t *s = AT(c);
        for (k = 0; k < (unsigned)c->w; k++)
            h = mixh(h, s[k]);
    }
    return h;
}

/* band b: each tile's hash, from the commands over it in order */
static void band_tiles(int b)
{
    unsigned i, k;
    int by0 = b * TB_BAND;
    for (k = 0; k < NTILE; k++)
        tile_now[k] = 2166136261u ^ pal_now;
    for (i = 0; i < ncmd; i++) {
        const cmd_t *c = &cmds[i];
        int t0, t1;
        uint32_t h;
        if (c->y >= by0 + TB_BAND || c->y + c->h <= by0)
            continue;
        t0 = c->x < 0 ? 0 : c->x / TB_TILE, t1 = c->x + c->w > TB_W ? NTILE - 1 : (c->x + c->w - 1) / TB_TILE;
        if (t0 > t1)
            continue;
        h = cmd_sig(c);
        for (k = (unsigned)t0; k <= (unsigned)t1; k++)
            tile_now[k] = mixh(tile_now[k], h);
    }
}

/* rasterise: every band whose tiles changed, its window of them (all: force), drawn in one buffer while the
 * other goes out */
void gfx_end(void (*emit)(int x, int y, int w, int h, uint16_t *px), int force)
{
    int b;
    unsigned i, k;
    for (b = 0; b < TB_H / TB_BAND; b++) {
        int lo = NTILE, hi = -1, n;
        uint16_t g;
        band_tiles(b);
        for (k = 0; k < NTILE; k++)
            if (force || tile_now[k] != tile_hash[b][k]) {
                tile_hash[b][k] = tile_now[k];
                lo = (int)k < lo ? (int)k : lo, hi = (int)k;
            }
        if (hi < 0)
            continue;
        band_y0 = b * TB_BAND;
        win_x0 = lo * TB_TILE, win_x1 = (hi + 1) * TB_TILE, win_w = win_x1 - win_x0;
        px = pix[pix_k];
        pix_k ^= 1u;
        n = win_w * TB_BAND;
        {                                             /* the ground: the theme's (dithered as all the rest) */
            int x, y;
            for (y = 0; y < TB_BAND; y++)
                for (x = 0; x < win_w; x++)
                    g = pack(win_x0 + x, band_y0 + y, (BG[pal_now] >> 16) & 255u, (BG[pal_now] >> 8) & 255u, BG[pal_now] & 255u),
                    px[y * win_w + x] = g;
        }
        for (i = 0; i < ncmd; i++)
            if (cmds[i].y < band_y0 + TB_BAND && cmds[i].y + cmds[i].h > band_y0 && cmds[i].x < win_x1 && cmds[i].x + cmds[i].w > win_x0)
                draw(&cmds[i]);
        emit(win_x0, band_y0, win_w, TB_BAND, px);
        (void)n;
    }
}
