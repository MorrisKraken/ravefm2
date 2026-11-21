/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the pixel-art canvas (rfm_px.h). */
#include "rfm_px.h"
#include "fm1.h"
#include "rfm_font.h"
#include "rfm_boot.h"
#include <string.h>

/* the canvas: two pixels a byte (the left one in the low nibble), in the RAM every DMA reaches (not the pool) */
#define ROWB (PX_W / 2)
#define CELL 8
#define CX (PX_W / CELL)
#define CY (PX_H / CELL)
static uint8_t fb[PX_H * ROWB] __attribute__((section(".bss.rfm_fb"), aligned(4)));
static uint8_t shown[PX_H * ROWB] __attribute__((section(".pool"), aligned(4)));   /* what the panel shows */
static uint16_t out[2][PX_W * CELL] __attribute__((section(".bss.rfm_fb"), aligned(4)));
static uint16_t pal[PX_NCOL];
static uint8_t theme, all_dirty = 1, held;          /* held: the boot screen is on the panel, not the canvas */
static int cx0, cy0, cx1 = PX_W, cy1 = PX_H;

/* ---------------------------------------------------------------------------------------------- the themes --- */
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
static const uint16_t THEMES[GFX_NPAL][PX_NCOL] = {
    /* ACID: lime, acid yellow */
    {RGB(0, 0, 0), RGB(12, 22, 6), RGB(30, 52, 16), RGB(58, 92, 30), RGB(104, 160, 40), RGB(180, 255, 30),
     RGB(255, 240, 50), RGB(236, 255, 214), RGB(140, 164, 120), RGB(82, 100, 70), RGB(255, 46, 99), RGB(36, 224, 208),
     RGB(255, 138, 30), RGB(10, 54, 50), RGB(160, 96, 255), RGB(255, 255, 255)},
    /* TOXIC: acid yellow, lime */
    {RGB(0, 0, 0), RGB(24, 22, 4), RGB(54, 50, 12), RGB(96, 90, 24), RGB(170, 160, 30), RGB(255, 236, 40),
     RGB(170, 255, 40), RGB(255, 252, 220), RGB(168, 160, 112), RGB(104, 98, 66), RGB(255, 46, 99), RGB(255, 140, 40),
     RGB(255, 90, 40), RGB(60, 36, 8), RGB(160, 96, 255), RGB(255, 255, 255)},
    /* CYBER: toxic aqua, magenta */
    {RGB(0, 0, 0), RGB(4, 20, 24), RGB(12, 46, 54), RGB(22, 84, 96), RGB(30, 150, 160), RGB(40, 240, 220),
     RGB(255, 60, 220), RGB(220, 255, 250), RGB(116, 160, 164), RGB(66, 98, 102), RGB(255, 46, 99), RGB(180, 255, 30),
     RGB(255, 200, 40), RGB(36, 60, 8), RGB(160, 96, 255), RGB(255, 255, 255)},
    /* LAVA: orange, yellow */
    {RGB(0, 0, 0), RGB(26, 10, 4), RGB(58, 24, 8), RGB(100, 44, 14), RGB(180, 76, 20), RGB(255, 120, 30),
     RGB(255, 236, 60), RGB(255, 236, 214), RGB(170, 132, 112), RGB(104, 80, 66), RGB(255, 40, 60), RGB(180, 255, 30),
     RGB(255, 60, 40), RGB(36, 60, 8), RGB(160, 96, 255), RGB(255, 255, 255)},
};
void gfx_palette(unsigned t)
{
    unsigned i;
    theme = (uint8_t)(t < GFX_NPAL ? t : 0);
    for (i = 0; i < PX_NCOL; i++)
        pal[i] = THEMES[theme][i];
    all_dirty = 1;
}
unsigned gfx_palette_now(void) { return theme; }

/* ------------------------------------------------------------------------------------------------ drawing --- */
void px_clip(int x0, int y0, int x1, int y1)
{
    cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0;
    cx1 = x1 > PX_W ? PX_W : x1, cy1 = y1 > PX_H ? PX_H : y1;
}
void px_noclip(void) { cx0 = cy0 = 0, cx1 = PX_W, cy1 = PX_H; }
void px_begin(void)
{
    if (!pal[PX_INK])
        gfx_palette(theme);
    memset(fb, 0, sizeof fb);
    px_noclip();
    held = 0;
}
static inline void put(int x, int y, unsigned c)
{
    uint8_t *p = &fb[y * ROWB + (x >> 1)];
    *p = (x & 1) ? (uint8_t)((*p & 0x0F) | (c << 4)) : (uint8_t)((*p & 0xF0) | (c & 15u));
}
void px_pixel(int x, int y, unsigned c)
{
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1)
        put(x, y, c);
}
unsigned px_get(int x, int y)
{
    if (x < 0 || y < 0 || x >= PX_W || y >= PX_H)
        return 0;
    return (x & 1) ? fb[y * ROWB + (x >> 1)] >> 4 : fb[y * ROWB + (x >> 1)] & 15u;
}
void px_hline(int x, int y, int w, unsigned c)
{
    int e = x + w;
    uint8_t *row, cc = (uint8_t)(c & 15u);
    if (y < cy0 || y >= cy1)
        return;
    if (x < cx0)
        x = cx0;
    if (e > cx1)
        e = cx1;
    if (x >= e)
        return;
    row = &fb[y * ROWB];
    if (x & 1)
        put(x++, y, cc);
    if (e & 1)
        put(--e, y, cc);
    if (e > x)
        memset(row + (x >> 1), cc | cc << 4, (size_t)((e - x) >> 1));
}
void px_vline(int x, int y, int h, unsigned c)
{
    int e = y + h;
    if (x < cx0 || x >= cx1)
        return;
    if (y < cy0)
        y = cy0;
    if (e > cy1)
        e = cy1;
    for (; y < e; y++)
        put(x, y, c);
}
void px_rect(int x, int y, int w, int h, unsigned c)
{
    int j;
    for (j = 0; j < h; j++)
        px_hline(x, y + j, w, c);
}
void px_frame(int x, int y, int w, int h, unsigned c)
{
    if (w <= 0 || h <= 0)
        return;
    px_hline(x, y, w, c);
    px_hline(x, y + h - 1, w, c);
    px_vline(x, y + 1, h - 2, c);
    px_vline(x + w - 1, y + 1, h - 2, c);
}
void px_box(int x, int y, int w, int h, unsigned fill, unsigned edge)
{
    if (w < 3 || h < 3)
        return;
    px_rect(x + 1, y + 1, w - 2, h - 2, fill);
    px_hline(x + 1, y, w - 2, edge);
    px_hline(x + 1, y + h - 1, w - 2, edge);
    px_vline(x, y + 1, h - 2, edge);
    px_vline(x + w - 1, y + 1, h - 2, edge);
}
void px_dots(int x, int y, int w, int h, unsigned c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = (x + y + j) & 1; i < w; i += 2)
            px_pixel(x + i, y + j, c);
}
void px_line(int x0, int y0, int x1, int y1, unsigned c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy, n = 0;
    if (y0 == y1) {
        px_hline(x0 < x1 ? x0 : x1, y0, dx + 1, c);
        return;
    }
    if (x0 == x1) {
        px_vline(x0, y0 < y1 ? y0 : y1, -dy + 1, c);
        return;
    }
    for (;;) {
        int e2;
        px_pixel(x0, y0, c);
        if ((x0 == x1 && y0 == y1) || ++n > 1000)
            break;
        e2 = 2 * e;
        if (e2 >= dy)
            e += dy, x0 += sx;
        if (e2 <= dx)
            e += dx, y0 += sy;
    }
}
void px_circle(int cx, int cy, int r, unsigned c)
{
    int x = r, y = 0, e = 1 - r;
    if (r <= 0) {
        px_pixel(cx, cy, c);
        return;
    }
    while (x >= y) {
        px_pixel(cx + x, cy + y, c), px_pixel(cx - x, cy + y, c), px_pixel(cx + x, cy - y, c), px_pixel(cx - x, cy - y, c);
        px_pixel(cx + y, cy + x, c), px_pixel(cx - y, cy + x, c), px_pixel(cx + y, cy - x, c), px_pixel(cx - y, cy - x, c);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else
            x--, e += 2 * (y - x) + 1;
    }
}
void px_disc(int cx, int cy, int r, unsigned c)
{
    int x = r, y = 0, e = 1 - r;
    if (r <= 0) {
        px_pixel(cx, cy, c);
        return;
    }
    while (x >= y) {
        px_hline(cx - x, cy + y, 2 * x + 1, c), px_hline(cx - x, cy - y, 2 * x + 1, c);
        px_hline(cx - y, cy + x, 2 * y + 1, c), px_hline(cx - y, cy - x, 2 * y + 1, c);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else
            x--, e += 2 * (y - x) + 1;
    }
}

/* ---------------------------------------------------------------------------------------------- the font --- */
/* UTF-8: the middle dot and the arrow right have glyphs; any other sequence a '?' */
static unsigned next_code(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    unsigned c = *s++;
    if (c >= 0x80) {
        if (c == 0xC2 && *s == 0xB7)
            c = 127, s++;
        else if (c == 0xE2 && s[0] == 0x86 && s[1] == 0x92)
            c = 128, s += 2;
        else if (c == 0xE2 && s[0] == 0x99 && s[1] == 0xAA)
            c = 129, s += 2;
        else {
            while ((*s & 0xC0) == 0x80)
                s++;
            c = '?';
        }
    }
    *ps = (const char *)s;
    return c < RFM_FONT_FIRST || c >= RFM_FONT_FIRST + RFM_FONT_N ? '?' : c;
}
int px_text_w(const char *s, int scale)
{
    int w = 0;
    if (!s)
        return 0;
    while (*s) {
        unsigned c = next_code(&s);
        w += (RFM_FONT_W[c - RFM_FONT_FIRST] + 1) * scale;
    }
    return w ? w - scale : 0;
}
int px_text(int x, int y, const char *s, unsigned col, int scale, int align)
{
    int w = px_text_w(s, scale), x0;
    if (!s)
        return 0;
    if (align == PX_CENTER)
        x -= w / 2;
    else if (align == PX_RIGHT)
        x -= w;
    x0 = x;
    while (*s) {
        unsigned c = next_code(&s) - RFM_FONT_FIRST, r, b;
        const uint8_t *g = RFM_FONT[c];
        for (r = 0; r < 9u; r++)
            if (g[r])
                for (b = 0; b < 5u; b++)
                    if (g[r] >> (4u - b) & 1u) {
                        if (scale == 1)
                            px_pixel(x + (int)b, y + (int)r, col);
                        else
                            px_rect(x + (int)b * scale, y + (int)r * scale, scale, scale, col);
                    }
        x += (RFM_FONT_W[c] + 1) * scale;
    }
    return x - x0 - (x > x0 ? scale : 0);
}

void px_sprite(const uint8_t *bits, int w, int h, int x, int y, unsigned c, int flip)
{
    int i, j, rb = (w + 7) / 8;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (bits[j * rb + i / 8] >> (7 - i % 8) & 1)
                px_pixel(flip ? x + w - 1 - i : x + i, y + j, c);
}

/* ---------------------------------------------------------------------------------------------- to the panel --- */
static void send(int x, int y, int w, int h, uint16_t *px)
{
    uint32_t t0 = fm1_millis();
    while (fm1_screen_pending() > 0 && fm1_millis() - t0 < 40u)
        ;
    if (fm1_screen_submit((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, px, (size_t)w * (size_t)h) != 0)
        fm1_screen_blit((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, px, (size_t)w * (size_t)h);
}
static void wait_sent(void)
{
    uint32_t t0 = fm1_millis();
    while (fm1_screen_pending() > 0 && fm1_millis() - t0 < 40u)
        ;
}
void px_flush(int force)
{
    int r, c, b = 0;
    if (held)
        return;
    force |= all_dirty;
    all_dirty = 0;
    for (r = 0; r < CY; r++) {
        uint8_t dirty[CX];
        for (c = 0; c < CX; c++) {                      /* each cell against what the panel shows: its 8 rows of 4 bytes */
            const uint32_t *p = (const uint32_t *)&fb[r * CELL * ROWB + c * (CELL / 2)];
            uint32_t *q = (uint32_t *)&shown[r * CELL * ROWB + c * (CELL / 2)], k, diff = 0;
            for (k = 0; k < CELL; k++) {
                diff |= p[k * (ROWB / 4)] ^ q[k * (ROWB / 4)];
                q[k * (ROWB / 4)] = p[k * (ROWB / 4)];
            }
            dirty[c] = (uint8_t)(force || diff);
        }
        for (c = 0; c < CX;) {
            int c1, x0, w, j, i;
            uint16_t *o;
            if (!dirty[c]) {
                c++;
                continue;
            }
            for (c1 = c; c1 < CX && dirty[c1]; c1++)
                ;
            x0 = c * CELL, w = (c1 - c) * CELL;
            o = out[b];                                 /* (free: the send after it waited for it) */
            for (j = 0; j < CELL; j++) {
                const uint8_t *s = &fb[(r * CELL + j) * ROWB + x0 / 2];
                for (i = 0; i < w / 2; i++) {
                    uint8_t v = s[i];
                    *o++ = pal[v & 15u];
                    *o++ = pal[v >> 4];
                }
            }
            send(x0, r * CELL, w, CELL, out[b]);
            b ^= 1;
            c = c1;
        }
    }
}
void gfx_end(void (*emit)(int x, int y, int w, int h, uint16_t *px), int force)
{
    (void)emit;
    px_flush(force);
}

/* ---------------------------------------------------------------------------------------------- the boot --- */
void px_boot(unsigned level)
{
    static uint16_t row[PX_W];
    uint16_t bp[16];
    unsigned i, y = 0, x = 0;
    if (level > 16u)
        level = 16u;
    for (i = 0; i < 16u; i++) {
        unsigned r = RFM_BOOT_RGB[i][0] * level / 16u, g = RFM_BOOT_RGB[i][1] * level / 16u, bl = RFM_BOOT_RGB[i][2] * level / 16u;
        bp[i] = RGB(r, g, bl);
    }
    wait_sent();
    for (i = 0; i < sizeof RFM_BOOT_RLE; i++) {
        unsigned run = (RFM_BOOT_RLE[i] >> 4) + 1u, c = RFM_BOOT_RLE[i] & 15u;
        while (run--) {
            row[x++] = bp[c];
            if (x == RFM_BOOT_W) {
                fm1_screen_blit(0, (PX_H - RFM_BOOT_H) / 2u + y, PX_W, 1, row, PX_W);
                x = 0, y++;
            }
        }
    }
    all_dirty = 1, held = 1;                            /* (the canvas no longer is what the panel shows) */
}
