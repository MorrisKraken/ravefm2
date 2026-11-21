/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the screen (rfm_cv.h): a display list drawn in bands, the changed tiles sent. */
#include "rfm_cv.h"
#include "fm1.h"
#include "rfm_font.h"
#include "rfm_aafont.h"
#include <string.h>

enum { OP_RECT, OP_BOX, OP_FRAME, OP_HGRAD, OP_LINE, OP_CIRCLE, OP_DISC, OP_DOTS, OP_TEXT, OP_SPRITE, OP_STRIPES, OP_TRAP };
typedef struct {
    uint8_t op, a, b, f;              /* the command; its small arguments (radius, font, scale, flip / align) */
    int16_t x, y, w, h;               /* its box (a line: its two ends) */
    int16_t y0, y1;                   /* the rows it touches */
    uint16_t c;
    const void *p, *q;                /* text, sprite data, palette */
} cmd_t;
#define NCMD 1400
#define TEXTPOOL 6144
#define BAND 16
static cmd_t cmd[NCMD] __attribute__((section(".pool")));
static char tpool[TEXTPOOL] __attribute__((section(".pool")));
static unsigned ncmd, ntext, full = 1, held;          /* held: the boot screen is on the panel */
static uint16_t band[CV_W * BAND] __attribute__((aligned(4)));
static uint16_t out[2][CV_W * BAND] __attribute__((aligned(4)));
static uint32_t tile_sum[CV_H / BAND][CV_W / BAND] __attribute__((section(".pool")));
static int by0;                       /* the band's first row */

/* ------------------------------------------------------------------------------------------------ colours --- */
static const uint8_t STOP[6][3] = {{110, 0, 0}, {230, 10, 10}, {255, 100, 0}, {255, 190, 0}, {245, 255, 0},
                                   {255, 255, 255}};
uint16_t cv_ramp(unsigned t)
{
    unsigned s, f, r, g, b;
    if (t > 255u)
        t = 255u;
    s = t * 5u / 256u, f = t * 5u % 256u;
    r = STOP[s][0] + (int)(STOP[s + 1u][0] - STOP[s][0]) * (int)f / 256;
    g = STOP[s][1] + (int)(STOP[s + 1u][1] - STOP[s][1]) * (int)f / 256;
    b = STOP[s][2] + (int)(STOP[s + 1u][2] - STOP[s][2]) * (int)f / 256;
    return CV_RGB(r, g, b);
}
static inline uint32_t spread(uint16_t c) { return ((uint32_t)c | (uint32_t)c << 16) & 0x07E0F81Fu; }
uint16_t cv_mix(uint16_t a, uint16_t b, unsigned t)
{
    uint32_t x = spread(a), y = spread(b);
    if (t >= 32u)
        return b;
    x = (x + (((y - x) * t) >> 5)) & 0x07E0F81Fu;
    return (uint16_t)(x | x >> 16);
}

/* ------------------------------------------------------------------------------------------- the list --- */
static cmd_t *add(int op, int x, int y, int w, int h, uint16_t c)
{
    cmd_t *k;
    if (ncmd >= NCMD)
        return 0;
    k = &cmd[ncmd++];
    k->op = (uint8_t)op, k->a = k->b = k->f = 0;
    k->x = (int16_t)x, k->y = (int16_t)y, k->w = (int16_t)w, k->h = (int16_t)h, k->c = c;
    k->y0 = (int16_t)y, k->y1 = (int16_t)(y + h);
    k->p = k->q = 0;
    return k;
}
void cv_begin(void) { ncmd = 0, ntext = 0, held = 0; }
void cv_hold(void) { full = 1; }
void cv_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0)
        add(OP_RECT, x, y, w, h, c);
}
void cv_box(int x, int y, int w, int h, int r, uint16_t c)
{
    cmd_t *k;
    if (w > 0 && h > 0 && (k = add(OP_BOX, x, y, w, h, c)))
        k->a = (uint8_t)(r < 0 ? 0 : r > 4 ? 4 : r);
}
void cv_frame(int x, int y, int w, int h, int r, uint16_t c)
{
    cmd_t *k;
    if (w > 1 && h > 1 && (k = add(OP_FRAME, x, y, w, h, c)))
        k->a = (uint8_t)(r < 0 ? 0 : r > 4 ? 4 : r);
}
void cv_hgrad(int x, int y, int w, int h, unsigned t0, unsigned t1)
{
    cmd_t *k;
    if (w > 0 && h > 0 && (k = add(OP_HGRAD, x, y, w, h, 0)))
        k->a = (uint8_t)(t0 > 255u ? 255u : t0), k->b = (uint8_t)(t1 > 255u ? 255u : t1);
}
void cv_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    cmd_t *k = add(OP_LINE, x0, y0, x1, y1, c);
    if (k)
        k->y0 = (int16_t)(y0 < y1 ? y0 : y1), k->y1 = (int16_t)((y0 > y1 ? y0 : y1) + 1);
}
void cv_circle(int cx, int cy, int r, uint16_t c)
{
    cmd_t *k = add(OP_CIRCLE, cx, cy, r, 0, c);
    if (k)
        k->y0 = (int16_t)(cy - r), k->y1 = (int16_t)(cy + r + 1);
}
void cv_disc(int cx, int cy, int r, uint16_t c)
{
    cmd_t *k = add(OP_DISC, cx, cy, r, 0, c);
    if (k)
        k->y0 = (int16_t)(cy - r), k->y1 = (int16_t)(cy + r + 1);
}
void cv_dots(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0)
        add(OP_DOTS, x, y, w, h, c);
}
void cv_stripes(int x, int y, int w, int h, uint16_t a, uint16_t b)
{
    cmd_t *k;
    if (w > 0 && h > 0 && (k = add(OP_STRIPES, x, y, w, h, a)))
        k->q = 0, k->a = (uint8_t)(b >> 8), k->b = (uint8_t)b;
}
void cv_trap(int y, int h, int xt, int wt, int xb, int wb, uint16_t c, unsigned a16)
{
    cmd_t *k;
    if (h > 0 && a16 && (k = add(OP_TRAP, xt, y, wt, h, c)))
        k->a = (uint8_t)(a16 > 15u ? 15u : a16), k->p = (const void *)(uintptr_t)((uint16_t)xb | (uint32_t)(uint16_t)wb << 16);
}
void cv_skull(int cx, int cy, int r, uint16_t bone, uint16_t eyes)
{
    int e = r * 3 / 8 > 1 ? r * 3 / 8 : 1, ey = cy + r / 6, jw = r * 6 / 5 | 1, jy = cy + r * 2 / 3, i;
    cv_disc(cx, cy, r, bone);                            /* the cranium */
    cv_box(cx - jw / 2, jy, jw, r / 2 + 2, 1, bone);     /* the jaw */
    cv_rect(cx - r + 1, cy + r / 2, 2 * r - 1, 1, bone);  /* (the cheekbones) */
    cv_disc(cx - r * 2 / 5, ey, e, C_BG), cv_disc(cx + r * 2 / 5, ey, e, C_BG);   /* the sockets */
    if (eyes != C_BG)
        cv_disc(cx - r * 2 / 5, ey, e / 3, eyes), cv_disc(cx + r * 2 / 5, ey, e / 3, eyes);
    cv_rect(cx - r / 10, ey + e, r / 5 + 1, r / 5 + 1, C_BG);   /* the nose */
    for (i = -jw / 2 + 2; i < jw / 2 - 1; i += (r > 8 ? 3 : 2))   /* the teeth */
        cv_rect(cx + i, jy + 1, 1, r / 3 + 1, C_BG);
}
void cv_sprite(const uint8_t *rle, const uint16_t *pal, int w, int h, int x, int y, int scale, int flip)
{
    cmd_t *k = add(OP_SPRITE, x, y, w, h, 0);
    if (!k)
        return;
    k->p = rle, k->q = pal, k->b = (uint8_t)(scale < 1 ? 1 : scale), k->f = (uint8_t)(flip != 0);
    k->y1 = (int16_t)(y + h * k->b);
}

/* ------------------------------------------------------------------------------------------- the fonts --- */
static const rfm_aafont_t *aa(int font) { return font == CV_M ? &AA_M : font == CV_L ? &AA_L : font == CV_T ? &AA_T : &AA_XL; }
static unsigned code(const char **ps)                    /* UTF-8: the middle dot, the arrow, a note; others '?' */
{
    const unsigned char *s = (const unsigned char *)*ps;
    unsigned c = *s++;
    if (c >= 0x80) {
        if (c == 0xC2 && *s == 0xB7)
            c = 127, s++;
        else if (c == 0xE2 && s[0] == 0x86 && s[1] == 0x92)
            c = 128, s += 2;
        else {
            while ((*s & 0xC0) == 0x80)
                s++;
            c = '?';
        }
    }
    *ps = (const char *)s;
    return c;
}
static int aa_index(const rfm_aafont_t *f, unsigned c)
{
    unsigned i;
    if (c == 127)
        c = '.';
    if (c == 128)
        c = '>';
    if (f->n == 95u)
        return c >= 32u && c < 127u ? (int)(c - 32u) : -1;
    for (i = 0; i < f->n; i++)
        if ((unsigned char)f->map[i] == c)
            return (int)i;
    if (c >= 'a' && c <= 'z')
        return aa_index(f, c - 32u);
    return -1;
}
int cv_font_h(int font) { return font == CV_S ? 9 : aa(font)->asc; }
int cv_text_w(int font, const char *s)
{
    int w = 0;
    if (!s)
        return 0;
    while (*s) {
        unsigned c = code(&s);
        if (font == CV_S) {
            if (c < RFM_FONT_FIRST || c >= RFM_FONT_FIRST + RFM_FONT_N)
                c = '?';
            w += RFM_FONT_W[c - RFM_FONT_FIRST] + 1;
        } else {
            int i = aa_index(aa(font), c);
            w += i < 0 ? aa(font)->h / 3 : aa(font)->g[i].adv;
        }
    }
    return font == CV_S && w ? w - 1 : w;
}
int cv_text(int font, int x, int y, const char *s, uint16_t c, int align)
{
    int w = cv_text_w(font, s);
    size_t n;
    cmd_t *k;
    if (!s || !*s)
        return 0;
    n = strlen(s) + 1u;
    if (ntext + n > TEXTPOOL)
        return w;
    if (align == CV_CENTER)
        x -= w / 2;
    else if (align == CV_RIGHT)
        x -= w;
    k = add(OP_TEXT, x, y, w, font == CV_S ? 9 : aa(font)->h + 2, c);
    if (!k)
        return w;
    memcpy(tpool + ntext, s, n);
    k->p = tpool + ntext, k->a = (uint8_t)font;
    ntext += (unsigned)n;
    return w;
}

/* ------------------------------------------------------------------------------------------- in a band --- */
static inline void put(int x, int y, uint16_t c)
{
    if ((unsigned)x < CV_W && (unsigned)(y - by0) < BAND)
        band[(y - by0) * CV_W + x] = c;
}
static inline void blend(int x, int y, uint16_t c, unsigned a16)  /* a16 0..15 */
{
    if ((unsigned)x < CV_W && (unsigned)(y - by0) < BAND) {
        uint16_t *d = &band[(y - by0) * CV_W + x];
        *d = a16 >= 15u ? c : cv_mix(*d, c, a16 * 32u / 15u);
    }
}
static void hrow(int x, int y, int w, uint16_t c)
{
    int e = x + w, i;
    uint16_t *d;
    if ((unsigned)(y - by0) >= BAND)
        return;
    if (x < 0)
        x = 0;
    if (e > CV_W)
        e = CV_W;
    d = &band[(y - by0) * CV_W];
    for (i = x; i < e; i++)
        d[i] = c;
}
static int inset(int r, int k)                          /* a rounded corner: the pixels cut on its k-th row */
{
    static const uint8_t IN[5][4] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 1, 0}};
    return k < r ? IN[r][k] : 0;
}
static void do_box(const cmd_t *k, int outline)
{
    int j, r = k->a;
    for (j = 0; j < k->h; j++) {
        int y = k->y + j, in = inset(r, j < r ? j : k->h - 1 - j);
        if ((unsigned)(y - by0) >= BAND)
            continue;
        if (!outline || j == 0 || j == k->h - 1)
            hrow(k->x + in, y, k->w - 2 * in, k->c);
        else {
            int in2 = inset(r, j - 1 < r ? j - 1 : k->h - j);
            put(k->x + in, y, k->c), put(k->x + k->w - 1 - in, y, k->c);
            if (in2 > in + 1) {                         /* (the curve's steps joined) */
                hrow(k->x + in, y, in2 - in, k->c);
                hrow(k->x + k->w - in2, y, in2 - in, k->c);
            }
        }
    }
}
static void do_line(const cmd_t *k)
{
    int x0 = k->x, y0 = k->y, x1 = k->w, y1 = k->h;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy, n = 0;
    for (;;) {
        int e2;
        put(x0, y0, k->c);
        if ((x0 == x1 && y0 == y1) || ++n > 800)
            break;
        e2 = 2 * e;
        if (e2 >= dy)
            e += dy, x0 += sx;
        if (e2 <= dx)
            e += dx, y0 += sy;
    }
}
static void do_circle(const cmd_t *k, int fill)
{
    int x = k->w, y = 0, e = 1 - x, cx = k->x, cy = k->y;
    if (x <= 0) {
        put(cx, cy, k->c);
        return;
    }
    while (x >= y) {
        if (fill) {
            hrow(cx - x, cy + y, 2 * x + 1, k->c), hrow(cx - x, cy - y, 2 * x + 1, k->c);
            hrow(cx - y, cy + x, 2 * y + 1, k->c), hrow(cx - y, cy - x, 2 * y + 1, k->c);
        } else {
            put(cx + x, cy + y, k->c), put(cx - x, cy + y, k->c), put(cx + x, cy - y, k->c), put(cx - x, cy - y, k->c);
            put(cx + y, cy + x, k->c), put(cx - y, cy + x, k->c), put(cx + y, cy - x, k->c), put(cx - y, cy - x, k->c);
        }
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else
            x--, e += 2 * (y - x) + 1;
    }
}
static void do_text(const cmd_t *k)
{
    const char *s = k->p;
    int x = k->x;
    while (*s) {
        unsigned c = code(&s);
        if (k->a == CV_S) {
            unsigned r, b;
            const uint8_t *g;
            if (c < RFM_FONT_FIRST || c >= RFM_FONT_FIRST + RFM_FONT_N)
                c = '?';
            g = RFM_FONT[c - RFM_FONT_FIRST];
            for (r = 0; r < 9u; r++)
                if (g[r] && (unsigned)(k->y + (int)r - by0) < BAND)
                    for (b = 0; b < 5u; b++)
                        if (g[r] >> (4u - b) & 1u)
                            put(x + (int)b, k->y + (int)r, k->c);
            x += RFM_FONT_W[c - RFM_FONT_FIRST] + 1;
        } else {
            const rfm_aafont_t *f = aa(k->a);
            int i = aa_index(f, c), gx, gy, j, rb;
            const rfm_glyph_t *g;
            const uint8_t *d;
            if (i < 0) {
                x += f->h / 3;
                continue;
            }
            g = &f->g[i], d = f->data + g->off, rb = (g->bw + 1) / 2;
            gx = x + g->bx, gy = k->y + g->by;
            for (j = 0; j < g->bh; j++) {
                int y = gy + j, q;
                if ((unsigned)(y - by0) >= BAND)
                    continue;
                for (q = 0; q < g->bw; q++) {
                    unsigned a = q & 1 ? d[j * rb + q / 2] >> 4 : d[j * rb + q / 2] & 15u;
                    if (a)
                        blend(gx + q, y, k->c, a);
                }
            }
            x += g->adv;
        }
    }
}
static void do_sprite(const cmd_t *k)
{
    const uint8_t *r = k->p;
    const uint16_t *pal = k->q;
    int n = k->w * k->h, i = 0, sc = k->b;
    if (k->y + k->h * sc <= by0 || k->y >= by0 + BAND)
        return;
    while (i < n) {
        int run = (*r >> 4) + 1, c = *r & 15;
        r++;
        if (c) {
            int m;
            for (m = 0; m < run; m++) {
                int p = i + m, sx = p % k->w, sy = p / k->w, x, y, u, v;
                if (k->f)
                    sx = k->w - 1 - sx;
                x = k->x + sx * sc, y = k->y + sy * sc;
                if (y + sc <= by0 || y >= by0 + BAND)
                    continue;
                for (v = 0; v < sc; v++)
                    for (u = 0; u < sc; u++)
                        put(x + u, y + v, pal[c]);
            }
        }
        i += run;
    }
}
static void do_trap(const cmd_t *k)               /* top: x, w; bottom: in p; a: its opacity 1..15 */
{
    uint32_t pb = (uint32_t)(uintptr_t)k->p;
    int xb = (int16_t)(pb & 0xFFFFu), wb = (int16_t)(pb >> 16), j, h = k->h;
    for (j = 0; j < h; j++) {
        int y = k->y + j, x0, x1, q;
        if ((unsigned)(y - by0) >= BAND)
            continue;
        x0 = k->x + (xb - k->x) * j / (h > 1 ? h - 1 : 1);
        x1 = k->x + k->w + (xb + wb - k->x - k->w) * j / (h > 1 ? h - 1 : 1);
        if (k->a >= 15u)
            hrow(x0, y, x1 - x0, k->c);
        else
            for (q = x0 < 0 ? 0 : x0; q < x1 && q < CV_W; q++)
                blend(q, y, k->c, k->a);
    }
}
static void draw_band(void)
{
    unsigned i;
    for (i = 0; i < CV_W * BAND; i++)
        band[i] = C_BG;
    for (i = 0; i < ncmd; i++) {
        const cmd_t *k = &cmd[i];
        int j, q;
        if (k->y1 <= by0 || k->y0 >= by0 + BAND)
            continue;
        switch (k->op) {
        case OP_RECT:
            for (j = 0; j < k->h; j++)
                hrow(k->x, k->y + j, k->w, k->c);
            break;
        case OP_BOX: do_box(k, 0); break;
        case OP_FRAME: do_box(k, 1); break;
        case OP_HGRAD:
            for (q = 0; q < k->w; q++) {
                uint16_t c = cv_ramp(k->a + ((int)k->b - (int)k->a) * q / (k->w > 1 ? k->w - 1 : 1));
                for (j = 0; j < k->h; j++)
                    put(k->x + q, k->y + j, c);
            }
            break;
        case OP_LINE: do_line(k); break;
        case OP_CIRCLE: do_circle(k, 0); break;
        case OP_DISC: do_circle(k, 1); break;
        case OP_DOTS:
            for (j = 0; j < k->h; j++)
                for (q = (k->x + k->y + j) & 1; q < k->w; q += 2)
                    put(k->x + q, k->y + j, k->c);
            break;
        case OP_STRIPES:
            for (j = 0; j < k->h; j++)
                for (q = 0; q < k->w; q++)
                    put(k->x + q, k->y + j, ((k->x + q + k->y + j) >> 2) & 1 ? (uint16_t)(k->a << 8 | k->b) : k->c);
            break;
        case OP_TEXT: do_text(k); break;
        case OP_TRAP: do_trap(k); break;
        case OP_SPRITE: do_sprite(k); break;
        }
    }
}

/* -------------------------------------------------------------------------------------------- to the panel --- */
static void send(int x, int y, int w, int h, uint16_t *px)
{
    uint32_t t0 = fm1_millis();
    while (fm1_screen_pending() > 0 && fm1_millis() - t0 < 40u)
        ;
    if (fm1_screen_submit((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, px, (size_t)w * (size_t)h) != 0)
        fm1_screen_blit((unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, px, (size_t)w * (size_t)h);
}
void cv_end(int force)
{
    int r, t, b = 0;
    if (held)
        return;
    force |= (int)full;
    full = 0;
    for (r = 0; r < CV_H / BAND; r++) {
        uint8_t dirty[CV_W / BAND];
        by0 = r * BAND;
        draw_band();
        for (t = 0; t < CV_W / BAND; t++) {             /* each tile's sum: 16 rows of 8 words, mixed */
            const uint32_t *p = (const uint32_t *)&band[t * BAND];
            uint32_t h = 0x811C9DC5u, j, q;
            for (j = 0; j < BAND; j++)
                for (q = 0; q < BAND / 2u; q++) {
                    h = (h ^ p[j * (CV_W / 2) + q]) * 0x01000193u;
                    h ^= h >> 15;
                }
            dirty[t] = (uint8_t)(force || h != tile_sum[r][t]);
            tile_sum[r][t] = h;
        }
        for (t = 0; t < CV_W / BAND;) {
            int t1, x0, w, j;
            if (!dirty[t]) {
                t++;
                continue;
            }
            for (t1 = t; t1 < CV_W / BAND && dirty[t1]; t1++)
                ;
            x0 = t * BAND, w = (t1 - t) * BAND;
            for (j = 0; j < BAND; j++)
                memcpy(&out[b][j * w], &band[j * CV_W + x0], (size_t)w * 2u);
            send(x0, by0, w, BAND, out[b]);
            b ^= 1;
            t = t1;
        }
    }
}

void cv_boot(const uint8_t *rle, unsigned n, const uint8_t (*rgb)[3], int w, int h, int y, unsigned level)
{
    uint16_t pal[16];
    unsigned i, x = 0, row = 0;
    if (level > 16u)
        level = 16u;
    for (i = 0; i < 16u; i++)
        pal[i] = CV_RGB(rgb[i][0] * level / 16u, rgb[i][1] * level / 16u, rgb[i][2] * level / 16u);
    {
        uint32_t t0 = fm1_millis();
        while (fm1_screen_pending() > 0 && fm1_millis() - t0 < 40u)
            ;
    }
    for (i = 0; i < n; i++) {
        unsigned run = (rle[i] >> 4) + 1u, c = rle[i] & 15u;
        while (run--) {
            band[x++] = pal[c];
            if (x == (unsigned)w) {
                fm1_screen_blit(0, (unsigned)(y + (int)row), (unsigned)w, 1, band, (size_t)w);
                x = 0, row++;
            }
        }
    }
    (void)h;
    full = 1, held = 1;
}
