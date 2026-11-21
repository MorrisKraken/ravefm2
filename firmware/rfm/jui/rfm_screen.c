/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the screen, JIANT's (included by rfm_ui.c, one unit).
 *   - jgfx.c: JIANT's renderer as is (strips drawn off screen, blitted; the panel sent by its changed cells).
 *   - the frame of every page, as JIANT V10.7.23 draws its lens pages (ui_draw.c draw_head, tabs_paint, draw_column,
 *     draw_foot): the header 0..22 and its rule, the group tabs 26..38, four cards 42..79, the panel 84..223, the
 *     footer 225..239 (the selected track's steps, the page's name, its number). Red, white and black (gen_rfm_palettes).
 *   - the drawing calls RAVE FM's pictures are written with (rfm_gfx.h) on the panel's canvas. */
#include "jgfx.c"

#define LT_Y 26
#define LT_H 13
#define CARD_Y 42
#define CARD_H 38
#define CARD_W 57
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define GR_Y 84                                         /* the panel */
#define GR_H 140
#define FOOT_Y 225
#define FOOT_H 15
#define H_HEAD 24
#define HEAD_RULE_Y 22
#define HEAD_MID 11
#define HEAD_XY (HEAD_MID - AF_X_CAP_H / 2 - AF_X_CAP_Y)
#define HEAD_BMY (HEAD_MID - AF_M_CAP_H / 2 - AF_M_CAP_Y)
#define CARD_XY 2
#define ROLL_Y 14

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (s && *s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}
/* RAVE FM's words are UTF-8; JIANT's faces Latin-1 (the middle dot B7, the ellipsis 85): converted, an arrow as ">" */
static void latin(char *d, size_t n, const char *s)
{
    size_t k = 0;
    while (*s && k + 1u < n) {
        uint8_t c = (uint8_t)*s;
        if (c == 0xC2u && (uint8_t)s[1] >= 0xA0u)
            d[k++] = s[1], s += 2;
        else if (c == 0xC3u && s[1])
            d[k++] = (char)((uint8_t)s[1] + 0x40u), s += 2;
        else if (c == 0xE2u && (uint8_t)s[1] == 0x86u && s[2])
            d[k++] = (uint8_t)s[2] == 0x90u ? '<' : (uint8_t)s[2] == 0x92u ? '>' : (uint8_t)s[2] == 0x91u ? '^' : 'v', s += 3;
        else if (c == 0xE2u && (uint8_t)s[1] == 0x80u && (uint8_t)s[2] == 0xA6u)
            d[k++] = ELLIPSIS, s += 3;
        else if (c == 0xE2u && (uint8_t)s[1] == 0x99u && s[2])
            d[k++] = '#', s += 3;                           /* (a note sign) */
        else if (c >= 0x80u && c < 0xC0u)
            s++;
        else if (c >= 0xC0u) {
            s++;
            while (((uint8_t)*s & 0xC0u) == 0x80u)
                s++;
            d[k++] = '?';
        } else
            d[k++] = (char)c, s++;
    }
    d[k] = 0;
}

/* ------------------------------------------------------------------------------------------- the palette --- */
static unsigned pal_now;
static uint8_t scr_force = 1;                           /* the whole screen again (a page changed, the palette) */
void gfx_palette(unsigned t)
{
    pal_now = t < GFX_NPAL ? t : 0u;
    palette_set(pal_now);
    scr_force = 1;
}
unsigned gfx_palette_now(void) { return pal_now; }

/* --------------------------------------------------------------- the pictures' calls (rfm_gfx.h) on the panel --- */
const tb_font_t TB_FONT_S = {0, 9}, TB_FONT_M = {1, 12}, TB_FONT_L = {2, 15}, TB_FONT_XL = {2, 15};
int gfx_area_map;
static int pic_on;                                      /* the panel's canvas is open */
/* the old pictures' rows (45..200, or the screen's with gfx_area_map off) on the panel's 140 */
static int my(int y) { return gfx_area_map ? (y - 45) * 139 / 155 : y - GR_Y; }
static int mh(int y, int h) { return gfx_area_map ? my(y + h) - my(y) : h; }
static uint16_t heat_mix(unsigned t)                    /* JIANT's thermal scale on RAVE FM's colours: dim .. theme .. accent .. text */
{
    if (t > 255u)
        t = 255u;
    if (t < 110u)
        return ux_mix(T_DIM, T_THEME, (int32_t)(t * 100u / 110u));
    if (t < 210u)
        return ux_mix(T_THEME, T_ACCENT, (int32_t)((t - 110u) * 100u / 100u));
    return ux_mix(T_ACCENT, T_TEXT, (int32_t)((t - 210u) * 100u / 45u));
}
uint16_t gfx_role(unsigned role)
{
    switch (role) {
    case PX_BG: return T_BG;
    case PX_PANEL: return T_SURF;
    case PX_LINE: return T_LINE;
    case PX_DIM: return T_DIM;
    case PX_MID: return ux_mix(T_BG, T_THEME, 45);
    case PX_MAIN: return T_THEME;
    case PX_HOT: return T_ACCENT;
    case PX_INK: return T_TEXT;
    case PX_GREY: return T_MID;
    case PX_FAINT: return T_DIM;
    case PX_ALERT: return T_REC;
    case PX_AUX: return T_K(2);
    case PX_WARM: return T_K(1);
    case PX_AUXDK: return ux_mix(T_BG, T_K(2), 35);
    case PX_BAT: return RGB(150, 60, 255);
    default: return T_TEXT;
    }
}
uint16_t gfx_c(uint32_t c)
{
    if (c & 0x80000000u)
        return c & 0x40000000u ? (uint16_t)c : gfx_role(c & 15u);
    return RGB((c >> 16) & 255u, (c >> 8) & 255u, c & 255u);
}
#define RAW(c565) (0xC0000000u | (c565))
uint32_t gfx_heat(unsigned t)
{
    if (t < 12u)
        return PXC(PX_BG);
    if (t < 30u)
        return PXC(PX_PANEL);
    if (t < 55u)
        return PXC(PX_LINE);
    return RAW(heat_mix((t - 55u) * 255u / 200u));
}
uint32_t gfx_bg(void) { return PXC(PX_BG); }
uint32_t gfx_ink(unsigned k) { return PXC(k == 0 ? PX_INK : k == 1 ? PX_GREY : PX_FAINT); }
uint32_t gfx_mix(uint32_t a, uint32_t b, unsigned t) { return RAW(ux_mix(gfx_c(a), gfx_c(b), (int32_t)(t * 100u / 255u))); }
uint32_t gfx_spectrum(unsigned b) { return RAW(heat_mix(b)); }
static uint16_t faded(uint32_t c, unsigned a) { return a >= 245u ? gfx_c(c) : ux_mix(T_BG, gfx_c(c), (int32_t)(a * 100u / 255u)); }
static const aafont_t *face(const tb_font_t *f, const char *s)
{
    if (!f || f->font == 0)
        return &AF_S;
    if (f->font == 2 && *s) {                           /* (L: in V where it is figures, else M) */
        const char *p;
        for (p = s; *p; p++)
            if (glyph_at(&AF_V, (uint8_t)*p) < 0)
                return &AF_M;
        return &AF_V;
    }
    return &AF_M;
}
static void circle(int cx, int cy, int r, uint16_t c, int fill)
{
    int x = r, y = 0, e = 1 - r;
    if (r <= 0) {
        cv_pset(cx, cy, c);
        return;
    }
    while (x >= y) {
        if (fill) {
            cv_rect(cx - x, cy + y, 2 * x + 1, 1, c), cv_rect(cx - x, cy - y, 2 * x + 1, 1, c);
            cv_rect(cx - y, cy + x, 2 * y + 1, 1, c), cv_rect(cx - y, cy - x, 2 * y + 1, 1, c);
        } else {
            cv_pset(cx + x, cy + y, c), cv_pset(cx - x, cy + y, c), cv_pset(cx + x, cy - y, c), cv_pset(cx - x, cy - y, c);
            cv_pset(cx + y, cy + x, c), cv_pset(cx - y, cy + x, c), cv_pset(cx + y, cy - x, c), cv_pset(cx - y, cy - x, c);
        }
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else
            x--, e += 2 * (y - x) + 1;
    }
}

void gfx_rect(int x, int y, int w, int h, uint32_t c, uint8_t a, uint8_t mode)
{
    (void)mode;
    if (!pic_on || a < 16u || w <= 0 || h <= 0)
        return;
    if (mode == GFX_ADD) {                              /* (a highlight: added, the steps under it seen) */
        uint16_t k = gfx_c(c);
        unsigned r = ((k >> 11) * a) >> 8, g = (((k >> 5) & 63u) * a) >> 8, b = ((k & 31u) * a) >> 8;
        cv_rect_add(x, my(y), w, mh(y, h) > 0 ? mh(y, h) : 1, (uint16_t)(r << 11 | g << 5 | b));
        return;
    }
    cv_rect(x, my(y), w, mh(y, h) > 0 ? mh(y, h) : 1, faded(c, a));
}
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, uint8_t a, int line)
{
    uint16_t col = faded(c, a);
    int yy, hh;
    if (!pic_on || a < 16u || w <= 0 || h <= 0 || (gfx_area_map && y + h <= 45))
        return;
    yy = my(y), hh = mh(y, h);
    if (line) {                                         /* the outline only: what is under it stays (the steps) */
        int rr = r > 5 ? 5 : r < 0 ? 0 : r, px, py, e;
        if (rr * 2 > w)
            rr = w / 2;
        if (rr * 2 > hh)
            rr = hh / 2;
        cv_rect(x + rr, yy, w - 2 * rr, 1, col), cv_rect(x + rr, yy + hh - 1, w - 2 * rr, 1, col);
        cv_rect(x, yy + rr, 1, hh - 2 * rr, col), cv_rect(x + w - 1, yy + rr, 1, hh - 2 * rr, col);
        for (px = rr, py = 0, e = 1 - rr; rr > 0 && px >= py;) {   /* the corners: a circle's quarters */
            int ax[2] = {px, py}, ay[2] = {py, px}, k;
            for (k = 0; k < 2; k++) {
                cv_pset(x + rr - ax[k], yy + rr - ay[k], col), cv_pset(x + w - 1 - rr + ax[k], yy + rr - ay[k], col);
                cv_pset(x + rr - ax[k], yy + hh - 1 - rr + ay[k], col), cv_pset(x + w - 1 - rr + ax[k], yy + hh - 1 - rr + ay[k], col);
            }
            py++;
            if (e < 0)
                e += 2 * py + 1;
            else
                px--, e += 2 * (py - px) + 1;
        }
    } else
        cv_rrect(x, yy, w, hh, r > 5 ? 5 : r, col, T_BG);
}
void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t a)
{
    int yy = my(y), hh = mh(y, h), j;
    if (!pic_on)
        return;
    for (j = 0; j < hh; j++)
        cv_rect(x, yy + j, w, 1, ux_mix(faded(top, a), faded(bottom, a), hh > 1 ? j * 100 / (hh - 1) : 0));
}
void gfx_title_text(const char *s);
int gfx_text(const tb_font_t *f, int x, int y, const char *s, uint32_t c, uint8_t a, int align)
{
    char b[48];
    const aafont_t *af;
    int w;
    latin(b, sizeof b, s);
    af = face(f, b);
    w = text_w(af, b);
    if (!pic_on)
        return w;
    if (gfx_area_map && y < 45) {                       /* (by the old title: to the footer) */
        gfx_title_text(s);
        return w;
    }
    if (a < 60u)
        return w;
    {
        int yy = my(y) - (int)af->asc + 1;               /* (the old y: the baseline) */
        int xx = align == GFX_CENTER ? x - w / 2 : align == GFX_RIGHT ? x - w : x;
        if (w > 236) {                                  /* (wider than the panel: from its left, the end ellipsised) */
            xx = 2;
            cv_text_fit(xx, yy, af, b, faded(c, a), T_BG, 236);
            return w;
        }
        if (xx < 2)
            xx = 2;
        if (xx + w > 238)
            xx = 238 - w;
        cv_text(xx, yy, af, b, faded(c, a));
    }
    return w;
}
int gfx_text_width(const tb_font_t *f, const char *s)
{
    char b[48];
    latin(b, sizeof b, s);
    return text_w(face(f, b), b);
}
void gfx_wave(int x, int y, int w, int h, const uint8_t *lv, const uint8_t *ht, uint8_t hot)
{
    int i, yy = my(y), hh = mh(y, h), mid = yy + hh / 2, half = hh / 2 - 1;
    if (!pic_on)
        return;
    for (i = 0; i < w; i++) {
        int a = lv[i] * half / 255;
        uint16_t c = heat_mix(ht ? 255u - ht[i] : 128u);
        if (!hot)
            c = ux_mix(T_BG, c, 45);
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
    if (!pic_on || a < 40u || a1 <= a0)
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
    if (!pic_on || a < 30u)
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
    if (pic_on && a > 60u)
        circle(cx, my(cy), r / 2 > 1 ? r / 2 : 1, faded(c, a / 2u), 0);
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
    if (!pic_on)
        return;
    for (i = 0; i < w; i++) {
        int t = tops[i] * hh / (h ? h : 1);
        if (t < hh) {
            cv_rect(x + i, yy + t + 1, 1, hh - t - 1, cb);
            cv_rect(x + i, yy + t, 1, 1, ct);
        }
    }
}
unsigned gfx_overflows(void) { return 0; }

/* -------------------------------------------------------------------------------------------- the frame --- */
static char foot_extra[40];                             /* what a picture put by its title: the footer's right */
void gfx_title_text(const char *s)
{
    size_t n = strlen(foot_extra);
    if (n + strlen(s) + 3 < sizeof foot_extra)
        snprintf(foot_extra + n, sizeof foot_extra - n, "%s%s", n ? " " : "", s);
}
static const char *scr_section, *scr_alt;          /* the section; a layer's own name (its tab) */
static uint32_t sig_head, sig_tabs, sig_foot, sig_card[4], scr_page_sig;

/* the header (draw_head): the track's number in its box, the play state, the tempo, REC; in the middle the track's
 * sound (or a message, a layer's name); the octave; the audio's load */
static void head(const tb_view_t *v, const char *msg)
{
    char b[16], ttl[48], raw[48];
    int32_t x, lx, r = 200 - 6, bb[4];
    uint32_t sig = v->playing * 3u + v->rec * 5u + (uint32_t)(v->octave + 8) * 11u + v->track * 13131u + v->bpm10 * 101u +
                   (v->load / 20u) * 0x9E3779B1u + (uint32_t)(v->queued + 2) * 77u + v->pattern * 997u +
                   (v->drop + v->riser * 3u + v->mutating * 9u) * 1231u;
    if (msg)
        latin(raw, sizeof raw, msg);
    else
        snprintf(raw, sizeof raw, "%s%s%s", v->lane[v->track].name, v->sound && strcmp(v->sound, v->lane[v->track].name) ? " \xc2\xb7 " : "",
                 v->sound && strcmp(v->sound, v->lane[v->track].name) ? v->sound : "");
    latin(ttl, sizeof ttl, raw);
    sig = str_hash(sig, ttl) + (msg != 0) * 7u;
    if (!scr_force && sig == sig_head)
        return;
    sig_head = sig;
    cv_begin(240, H_HEAD, T_BG);
    b[0] = (char)('1' + v->track % 8u), b[1] = 0;      /* the track's box */
    cv_rrect(4, HEAD_MID - 6, 13, 13, 3, T_ACCENT, T_BG);
    cv_text_in(4, HEAD_XY, 13, &AF_X, b, T_INK, T_ACCENT);
    if (v->playing) {                                  /* the play state: a triangle, 8 x 11 */
        int32_t k;
        for (k = 0; k < 11; k++)
            cv_rect(22, HEAD_MID - 5 + k, 1 + (5 - (k < 5 ? 5 - k : k - 5)) * 7 / 5, 1, T_THEME);
    } else
        cv_rect(22, HEAD_MID - 4, 8, 9, T_MID);
    snprintf(b, sizeof b, "%u", v->bpm10 / 10u);
    x = cv_text(35, HEAD_BMY, &AF_M, b, T_THEME);
    text_ink(&AF_M, b, bb);
    lx = 35 + bb[2];
    if (v->rec) {                                      /* REC: its dot */
        cv_rrect(lx + 5, HEAD_MID - 4, 9, 9, 4, T_REC, T_BG);
        lx += 5 + 9;
    }
    (void)x;
    if (v->octave && !msg) {                           /* the octave, right */
        snprintf(b, sizeof b, "%+d", v->octave);
        x = r - (text_w(&AF_S, "OCT") + 4 + text_w(&AF_S, b));
        cv_text(cv_text(x, HEAD_MID - AF_S_CAP_H / 2 - AF_S_CAP_Y, &AF_S, "OCT", T_MID) + 4,
                HEAD_MID - AF_S_CAP_H / 2 - AF_S_CAP_Y, &AF_S, b, T_THEME);
        r = x - 6;
    }
    if (v->drop || v->riser || v->mutating) {          /* DROP / RISER / MUTATIONS: a word, right */
        if (v->drop || v->riser) {
            int n = v->drop ? v->drop : v->riser, left = v->drop ? v->drop_left : v->riser_left;
            snprintf(b, sizeof b, n == 2 ? "%s %d" : "%s", v->drop ? "DROP" : "RISER", left);
        } else
            snprintf(b, sizeof b, "MUT");
        x = r - text_w(&AF_X, b);
        cv_text(x, HEAD_XY, &AF_X, b, T_ACCENT);
        r = x - 6;
    }
    {                                                  /* the middle: the sound, on x 120 between the tempo and the right */
        int32_t w = text_w(&AF_X, ttl), l = lx + 8;
        x = 120 - HALF_UP(w);
        if (x + w > r)
            x = r - w;
        if (x < l)
            x = l;
        cv_free_text(x, HEAD_XY, &AF_X, ttl, msg ? T_THEME : T_MID, T_BG, r - x);
    }
    {                                                  /* the load: five bars; the pattern after them */
        uint32_t k, n = (v->load * 5u + 50u) / 100u;
        if (v->load && !n)
            n = 1;
        for (k = 0; k < 5u; k++)
            cv_rect(200 + (int32_t)k * 4, HEAD_MID - 3, 3, 7, k < n ? T_THEME : T_RAISE);
        b[0] = (char)('A' + v->pattern % 26u), b[1] = 0;
        cv_rrect(222, HEAD_MID - 6, 13, 13, 3, v->queued >= 0 && (v->ms / 250u) % 2u ? T_ACCENT : T_RAISE, T_BG);
        cv_text_in(222, HEAD_XY, 13, &AF_X, b, v->queued >= 0 && (v->ms / 250u) % 2u ? T_INK : T_TEXT,
                   v->queued >= 0 && (v->ms / 250u) % 2u ? T_ACCENT : T_RAISE);
    }
    cv_rect(0, HEAD_RULE_Y, 240, 1, T_LINE);           /* the rule */
    cv_blit(0, 0);
}

/* the tabs (tabs_paint): the section's groups, the one shown THEME with INK words, the others RAISE with MID; a dot a
 * page under the name. A group: the pages whose titles begin with the same word */
static int group_len(const char *t) { int n = 0; while (t[n] && t[n] != ' ') n++; return n; }
static int same_group(const char *a, const char *b)
{
    int n = group_len(a);
    return n == group_len(b) && !strncmp(a, b, (size_t)n);
}
static void tab_name(char *b, size_t n, const char *t, int shortn)
{
    static const char *const LONGN[] = {"CHORUS", "CLOCK", "DELAY", "DRIVE", "EXTRA", "FILTER", "FLANGER", "GLOBAL",
                                        "MASTER", "MIXER", "MOTION", "MUTATIONS", "PHASER", "PROJECTS", "REVERB",
                                        "RISER", "ROUTE", "SAMPLE", "SCALE", "SHAPE", "SLICE", "SOUND", "TREMOLO",
                                        "MANUAL", "CHORDS", "MACRO", "TRACK", "STEPS", "NOTES", "TOOLS", "MAIN"};
    static const char *const SHORTN[] = {"CHO", "CLK", "DLY", "DRV", "EXT", "FLT", "FLG", "GLB", "MST", "MIX", "MOT",
                                         "MUT", "PHS", "PROJ", "REV", "RIS", "RTE", "SMP", "SCL", "SHP", "SLC", "SND",
                                         "TRM", "MAN", "CHD", "MAC", "TRK", "STEP", "NOTE", "TOOL", "MAIN"};
    int len = group_len(t);
    unsigned i;
    if (shortn)
        for (i = 0; i < sizeof LONGN / sizeof *LONGN; i++)
            if ((int)strlen(LONGN[i]) == len && !strncmp(t, LONGN[i], (size_t)len)) {
                snprintf(b, n, "%s", SHORTN[i]);
                return;
            }
    snprintf(b, n, "%.*s", shortn && len > 4 ? 4 : len < 12 ? len : 11, t);
}
#define TAB_X(i, n) (4 + (int32_t)(i) * 234 / (int32_t)(n))
#define TAB_SHOWN 5
static void tabs(const tb_view_t *v, const char *const *alt, int nalt, int alt_on)
{
    const char *g[16];
    int first[17], ng = 0, i, on = 0, k0;
    uint32_t sig = 7u;
    if (alt) {                                         /* (a layer's own tabs: no dots) */
        for (i = 0; i < nalt; i++)
            g[i] = alt[i], first[i] = i;
        ng = nalt, first[ng] = ng, on = alt_on;
    } else {
        for (i = 0; i < v->npage && ng < 16; i++)
            if (!i || !same_group(v->page_title[i], v->page_title[i - 1]))
                first[ng] = i, g[ng++] = v->page_title[i];
        first[ng] = v->npage;
        for (i = 0; i < ng; i++)
            if (v->page_on >= first[i] && v->page_on < first[i + 1])
                on = i;
    }
    for (i = 0; i < ng; i++)
        sig = str_hash(sig * 31u + (uint32_t)first[i], g[i]);
    sig += (uint32_t)on * 40503u + v->page_on * 977u + (alt != 0) * 13u;
    if (v->menu == TB_MENU_FX)
        for (i = 0; i < 10; i++)
            sig += v->fx.on[i] * (uint32_t)(i + 1) * 7919u;
    if (!scr_force && sig == sig_tabs)
        return;
    sig_tabs = sig;
    cv_begin(240, LT_H, T_BG);
    if (!ng) {
        cv_blit(0, LT_Y);
        return;
    }
    k0 = ng <= TAB_SHOWN ? 0 : on < 2 ? 0 : on + 3 > ng ? ng - TAB_SHOWN : on - 2;
    {
        int n = ng - k0 < TAB_SHOWN ? ng - k0 : TAB_SHOWN, l;
        for (l = 0; l < n; l++) {
            int gi = k0 + l, x = TAB_X(l, n), w = TAB_X(l + 1, n) - 2 - x, nd = alt ? 0 : first[gi + 1] - first[gi], d;
            int lit = gi == on, dx = x + HALF_UP(w - (nd * 4 - 2));
            char nm[16], lat[16];
            uint16_t bg = lit ? T_THEME : T_RAISE, ink = lit ? T_INK : T_MID;
            tab_name(nm, sizeof nm, g[gi], 0);
            latin(lat, sizeof lat, nm);
            if (text_w(&AF_X, lat) > w - 6) {
                tab_name(nm, sizeof nm, g[gi], 1);
                latin(lat, sizeof lat, nm);
            }
            if (v->menu == TB_MENU_FX && !alt && first[gi] < 10 && !lit && v->fx.on[first[gi]])
                ink = T_TEXT;                          /* (FX: the effects that play, brighter) */
            cv_rrect(x, 0, w, LT_H, 4, bg, T_BG);
            cv_text_in(x, CAP_IN(X, LT_H), w, &AF_X, lat, ink, bg);
            for (d = 0; d < nd && nd > 1; d++)
                cv_rect(dx + d * 4, LT_H - 2, 2, 1, !lit ? T_DIM : first[gi] + d == v->page_on ? T_INK : ux_mix(T_THEME, T_INK, 40));
        }
        if (k0)
            cv_rect(0, 4, 2, 6, T_DIM);
        if (k0 + TAB_SHOWN < ng)
            cv_rect(238, 4, 2, 6, T_DIM);
    }
    cv_blit(0, LT_Y);
}

/* a card (draw_column, a lens page's): its outline in its colour (2 px the knob just turned), the label (MID) top left,
 * the value big (V where it is a number and fits, else M, else S) in the knob's colour, its unit (S, MID) on its line,
 * the gauge (3 px, rounded) at the foot. No knob: an empty outline (LINE) */
static int split_unit(const char *v)
{
    static const char *const U[] = {"ms", "s", "%", "Hz", "kHz", "dB", "st", "x", "MS", "S", "HZ", "KHZ", "DB", "ST",
                                    "BPM", "STEP", "STEPS", "BAR", "BARS"};
    int n = (int)strlen(v), i, k;
    for (i = 1; i < n; i++)
        if ((v[i - 1] >= '0' && v[i - 1] <= '9') && !(v[i] >= '0' && v[i] <= '9') && v[i] != '.')
            break;
    if (i >= n || !(v[0] == '-' || v[0] == '+' || v[0] == '.' || (v[0] >= '0' && v[0] <= '9')))
        return n;
    for (k = 0; k < (int)(sizeof U / sizeof *U); k++)
        if (!strcmp(v + i + (v[i] == ' '), U[k]))
            return i;
    return n;
}
static int face_has_all(const aafont_t *f, const char *s)   /* every glyph of s in a sparse face (V: figures) */
{
    for (; *s; s++)
        if (glyph_at(f, fold(f, (uint8_t)*s)) < 0)
            return 0;
    return 1;
}
/* a knob as JIANT draws them: its ring (270 degrees, 7:30 .. 4:30 o'clock, anti-aliased, ui_keycaps.h's quadrant masks),
 * the value's arc lit along it (from the start; bipolar: from 12 o'clock), a cap in the middle with its pointer line
 * turned to the value. Box top-left (x, y), outer radius r; ratio 0..1000 */
#define KA_END 384
static void knob_arc(int32_t x, int32_t y, int32_t r, const uint8_t *cov, const uint8_t *ang, int32_t lo, int32_t hi,
                     uint16_t tr, uint16_t vc, uint16_t bg)
{
    const uint16_t *rt = ramp(tr, bg), *rv = ramp(vc, bg);
    int32_t i, j, m;
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t k = (uint32_t)(j * r + i), a = (cov[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t q = ang[k];
            if (!a)
                continue;
            for (m = 0; m < 4; m++) {                    /* top right, bottom right, bottom left, top left */
                int32_t sa = m == 0 ? q : m == 1 ? 512 - q : m == 2 ? q - 512 : -q;
                int32_t px = m < 2 ? x + r + i : x + r - 1 - i, py = (m == 0 || m == 3 ? y + r - 1 - j : y + r + j) + cv_oy;
                if (sa < -KA_END || sa > KA_END || (uint32_t)px >= cv_w || (uint32_t)py >= cv_h)
                    continue;
                cv_px[(uint32_t)py * cv_w + (uint32_t)px] = (sa >= lo && sa <= hi ? rv : rt)[a];
            }
        }
}
static void knob_drawn(int32_t x, int32_t y, int32_t ratio, int bip, uint16_t vc)
{
    int32_t r = KNOB_SMALL_R, a = -KA_END + ratio * 2 * KA_END / 1000, a0, a1, cx = x + r, cy = y + r, k;
    uint32_t ph = (uint32_t)(a < 0 ? a + 1024 : a) << 22;
    int32_t sx = tb_sin(ph), cy_ = tb_sin(ph + 0x40000000u);   /* (12 o'clock is 0: x = sin, y = -cos) */
    if (bip)
        a0 = (a < 0 ? a : 0) - 8, a1 = (a > 0 ? a : 0) + 8;
    else
        a0 = -KA_END, a1 = ratio > 0 ? a : -KA_END - 1;
    knob_arc(x, y, r, KNOB_SMALL_COV, KNOB_SMALL_ANG, a0, a1, T_RAISE, vc, T_BG);
    cv_rrect(cx - 5, cy - 5, 10, 10, 5, T_RAISE, T_BG);  /* the cap */
    for (k = 1; k <= 5; k++)                             /* its pointer */
        cv_px[(uint32_t)(cy + cv_oy - cy_ * k / 32768) * cv_w + (uint32_t)(cx + sx * k / 32768)] = swap16(k > 1 ? vc : T_TEXT);
}
/* a text longer than its room: it rolls, slowly, its own size kept (a pause at each end), the margins cleaned after it */
static int32_t roll_off(int32_t over, uint32_t ms)
{
    uint32_t pos;
    if (over <= 0)
        return 0;
    pos = (ms / 70u) % (uint32_t)(over + 40);
    return pos < 20u ? 0 : pos < 20u + (uint32_t)over ? (int32_t)pos - 20 : over;
}
static void roll_text(int32_t y, const aafont_t *f, const char *s, uint16_t fg, int32_t room, int32_t off, uint16_t oc, int lw)
{
    cv_text(5 - off, y, f, s, fg);
    cv_rect(lw, y, 5 - lw, f->h, T_BG), cv_rect(0, y, lw, f->h, oc);   /* (the left margin, the border) */
    cv_rect(5 + room, y, CARD_W - lw - 5 - room, f->h, T_BG), cv_rect(CARD_W - lw, y, lw, f->h, oc);
}
static void card(const tb_view_t *v, int c)
{
    char lab[16], val[24], unit[12], key[64];
    const char *nm = v->knob_name[c];
    int hot = v->hud == c && v->hud_alpha > 60 && !v->hud_name, lock = v->knob_lock[c], ratio, cut, lw, knob = 1, roll = 0;
    uint16_t vc, oc;
    const aafont_t *vf = &AF_M;
    int32_t x, room = CARD_W - 8 - 2 * KNOB_SMALL_R - 1, uw, ch = CARD_H, ry = ROLL_Y;
    uint32_t sig;
    if (nm) {
        char raw[16];
        latin(lab, sizeof lab, nm);
        snprintf(raw, sizeof raw, "%s", v->knob_value[c]);
        cut = split_unit(raw);
        latin(unit, sizeof unit, raw + cut + (raw[cut] == ' '));
        raw[cut] = 0;
        latin(val, sizeof val, raw);
    } else
        lab[0] = val[0] = unit[0] = 0;
    ratio = nm ? v->knob_pos[c] * 1000 / 255 : -1;
    vc = lock ? T_ACCENT : T_K(c);
    oc = nm ? vc : T_LINE;
    lw = 1 + (hot && nm);
    snprintf(key, sizeof key, "%s|%s|%s|%d|%d|%d|%d|%u", lab, val, unit, ratio * 49 / 1000, lock, hot, v->knob_bip[c],
             nm && (text_w(&AF_X, lab) > CARD_W - 10 || text_w(&AF_M, val) > CARD_W - 2) ? (unsigned)(v->ms / 70u) : 0u);
    sig = str_hash(vc * 31u + ux.gen, key);
    if (!scr_force && sig == sig_card[c])
        return;
    sig_card[c] = sig;
    cv_begin(CARD_W, (uint32_t)ch, T_BG);
    cv_rrect(0, 0, CARD_W, ch, 5, oc, T_BG);            /* (card_line) */
    cv_rrect(lw, lw, CARD_W - 2 * lw, ch - 2 * lw, 5 - lw, T_BG, oc);
    if (nm) {
        if (text_w(&AF_X, lab) > CARD_W - 10)
            roll_text(CARD_XY, &AF_X, lab, lock ? T_ACCENT : T_MID, CARD_W - 10, roll_off(text_w(&AF_X, lab) - (CARD_W - 10), v->ms), oc, lw);
        else
            cv_text(5, CARD_XY, &AF_X, lab, lock ? T_ACCENT : T_MID);
        uw = unit[0] ? text_w(&AF_S, unit) + 3 : 0;
        if (face_has_all(&AF_V, val) && val[0] && text_w(&AF_V, val) + uw <= room)
            vf = &AF_V;
        else if (text_w(vf, val) + uw > room) {
            /* (a word the knob would cut: the knob goes, the word whole in its own size; a number keeps its knob) */
            if (text_w(&AF_M, val) + uw <= CARD_W - 10)
                knob = 0, room = CARD_W - 10;
            else if (text_w(&AF_M, val) + uw <= CARD_W - 2) /* (a few pixels over: the size under it, whole, still) */
                knob = 0, room = CARD_W - 10, vf = &AF_S;
            else
                knob = 0, room = CARD_W - 10, roll = 1;   /* (longer still: it rolls, in its size) */
        }
#ifdef RFM_HOST
        {   /* (the PC's check: a value or a name still cut, logged) */
            extern void rfm_host_cut(const char *lab, const char *val, int what);
            if (roll) {
                char t[40];
                snprintf(t, sizeof t, "%s+%dpx", val, (int)(text_w(vf, val) + uw - room));
                rfm_host_cut(lab, t, 1);
            }
            if (text_w(&AF_X, lab) > CARD_W - 10)
                rfm_host_cut(lab, val, 0);
        }
#endif
        if (roll)
            roll_text(ry - 1, vf, val, vc, room, roll_off(text_w(vf, val) + uw - room, v->ms), oc, lw), x = 5 + room - uw;
        else if (vf != &AF_S)
            x = cv_text(5, vf == &AF_V ? ry - AF_V_CAP_Y + 1 : ry - 1, vf, val, vc);
        else
            x = cv_text_fit(5, ry + 2, vf, val, vc, T_BG, room - uw);
        if (unit[0]) {
            int32_t uy = vf == &AF_V ? ry - AF_V_CAP_Y + 1 + AF_V.asc - AF_S.asc : vf == &AF_M ? ry - 1 + AF_M.asc - AF_S.asc : ry + 2;
            cv_text(x + 3, uy, &AF_S, unit, T_MID);
        }
        if (knob)
            knob_drawn(CARD_W - 4 - 2 * KNOB_SMALL_R, ch - 3 - 2 * KNOB_SMALL_R, ratio, v->knob_bip[c], vc);   /* the knob */
    }
    cv_blit((uint32_t)CARD_X(c), CARD_Y);
}

/* the footer (draw_foot, a lens page's): the selected track's 16 steps (its page), the playhead under them; the
 * page's name; its place n/N at the right (what a picture put by its title, before it) */
static void foot(const tb_view_t *v)
{
    char nm[40], cnt[12], ex[40];
    const char *pg = scr_alt ? scr_alt : v->npage ? v->page_title[v->page_on < v->npage ? v->page_on : 0] : scr_section;
    const tb_seqview_t *s = &v->seq;
    uint32_t sig = 3u, i;
    int32_t x0 = 6, y = 3, mx = 234, x;
    unsigned page = s->len > 16u ? (s->play >= 0 && v->playing ? (unsigned)s->play / 16u : s->page) : 0u;
    latin(nm, sizeof nm, pg ? pg : "");
    latin(ex, sizeof ex, foot_extra[0] ? foot_extra : v->hint && !v->npage ? v->hint : "");
    cnt[0] = 0;
    if (v->npage > 1 && !scr_alt)
        snprintf(cnt, sizeof cnt, "%d/%d", v->page_on + 1, v->npage);
    for (i = 0; i < 16u; i++) {
        unsigned si = page * 16u + i;
        sig = sig * 3u + (si < s->len ? (s->s[si].hits != 0) + 2u * ((s->s[si].flags & 0x40u) != 0) : 7u);
    }
    sig = str_hash(str_hash(str_hash(sig + (uint32_t)(s->play + 1) * 40503u + page * 977u, nm), cnt), ex);
    if (!scr_force && sig == sig_foot)
        return;
    sig_foot = sig;
    cv_begin(240, FOOT_H, T_BG);
    for (i = 0; i < 16u; i++) {                        /* the steps: lit THEME (accented: ACCENT), off a stub */
        unsigned si = page * 16u + i;
        x = x0 + (int32_t)i * 5 + (int32_t)(i / 4u) * 2;
        if (si >= s->len)
            continue;
        if (s->s[si].hits)
            cv_rect(x, y + AF_X_CAP_Y, 4, 7, (s->s[si].flags & 0x40u) ? T_ACCENT : T_THEME);
        else
            cv_rect(x, y + AF_X_CAP_Y + 4, 4, 3, T_RAISE);
        if (v->playing && s->play == (int)si)
            cv_rect(x, y + AF_X_CAP_Y + 9, 4, 2, T_TEXT);
    }
    x = x0 + 15 * 5 + 3 * 2 + 4 + 4;
    if (s->len > 16u) {                                /* the pages of a longer loop */
        unsigned np = (s->len + 15u) / 16u, k;
        for (k = 0; k < np && k < 8u; k++)
            cv_rect(x + (int32_t)k * 4, y + AF_X_CAP_Y + 2, 3, 3, k == page ? T_TEXT : T_RAISE);
        x += (int32_t)(np < 8u ? np : 8u) * 4 + 3;
    }
    if (cnt[0])
        mx = cv_text_r(234, y, &AF_X, cnt, T_TEXT, T_BG) - 8;
    if (ex[0] && text_w(&AF_X, nm) + text_w(&AF_X, ex) + 10 < mx - x) {
        cv_text_r(mx, y, &AF_X, ex, T_MID, T_BG);
        mx -= text_w(&AF_X, ex) + 8;
    }
    {
        char gr[40];                                   /* the group (THEME), the rest of the name (TEXT) */
        int gl = group_len(nm);
        snprintf(gr, sizeof gr, "%.*s", gl, nm);
        x = cv_free_text(x, y, &AF_X, gr, T_TEXT, T_BG, mx - x);
        if (nm[gl] && x + 8 < mx)
            cv_free_text(x + 5, y, &AF_X, nm + gl + 1, T_MID, T_BG, mx - x - 5);
    }
    foot_extra[0] = 0;
    cv_blit(0, FOOT_Y);
}

/* a message (a preset chosen, the tempo, help): JIANT's toast, a pill at the panel's foot, the words in it */
static void toast(const tb_view_t *v)
{
    char nm[48], val[48];
    int32_t w, wn, x, y = GR_H - 22;
    if (!(v->hud >= 0 && v->hud_name && v->hud_alpha > 70) || !strcmp(v->hud_name, "PAGE"))
        return;                                         /* (a page's name: the tabs and the footer say it) */
    latin(nm, sizeof nm, v->hud_name);
    latin(val, sizeof val, v->hud_value);
    wn = text_w(&AF_X, nm);
    {
        const aafont_t *vf = text_w(&AF_M, val) + wn + 28 <= 232 ? &AF_M : &AF_X;
        char cut[48];
        text_fit(cut, sizeof cut, val, vf, 232 - wn - 28);
        w = wn + 10 + text_w(vf, cut) + 18;
        x = 120 - w / 2;
        cv_rrect(x, y, w, 19, 9, T_SURF, T_BG);
        cv_frame(x + 9, y, w - 18, 1, T_THEME);
        cv_text_on(x + 9, y + CAP_IN(X, 19), &AF_X, nm, T_MID, T_SURF);
        if (vf == &AF_M)
            cv_text_on(x + 9 + wn + 10, y + CAP_IN(M, 19), &AF_M, cut, T_TEXT, T_SURF);
        else
            cv_text_on(x + 9 + wn + 10, y + CAP_IN(X, 19), &AF_X, cut, T_TEXT, T_SURF);
        return;
    }
}

/* the whole frame, the panel's canvas then opened for the picture (gfx_end sends it) */
static const tb_view_t *scr_view;
static void frame_with(const tb_view_t *v, const char *section, const char *msg, const char *const *alt, int nalt, int alt_on)
{
    uint32_t ps = str_hash((uint32_t)v->menu * 31u + v->page_on * 7u + (v->perform ? 99u : 0u) + (alt ? 5u : 0u), section);
    if (ps != scr_page_sig) {                          /* another page: all of it again */
        scr_page_sig = ps;
        scr_force = 1;
    }
    if (scr_force) {
        lcd_fill(0, H_HEAD, 240, LT_Y - H_HEAD, T_BG);
        lcd_fill(0, LT_Y + LT_H, 240, CARD_Y - LT_Y - LT_H, T_BG);
        lcd_fill(0, CARD_Y + CARD_H, 240, GR_Y - CARD_Y - CARD_H, T_BG);
        lcd_fill(0, GR_Y + GR_H, 240, FOOT_Y - GR_Y - GR_H, T_BG);
        lcd_fill(0, CARD_Y, 3, CARD_H, T_BG);
        lcd_fill(237, CARD_Y, 3, CARD_H, T_BG);
        {
            int c;
            for (c = 0; c < 3; c++)
                lcd_fill((uint32_t)(CARD_X(c) + CARD_W), CARD_Y, 2, CARD_H, T_BG);
        }
    }
    scr_section = section;
    scr_alt = alt ? alt[alt_on] : 0;
    scr_view = v;
    head(v, msg);
    tabs(v, alt, nalt, alt_on);
    {
        int c;
        for (c = 0; c < 4; c++)
            card(v, c);
    }
    cv_begin(240, GR_H, T_BG);
    pic_on = 1;
    gfx_area_map = 1;
}
static void frame(const tb_view_t *v, const char *section) { frame_with(v, section, 0, 0, 0, 0); }

void gfx_begin(void) {}
/* the end of a frame: the toast over the picture, the panel sent by its changed cells, the footer */
/* (21 Nov 2026) a page whose picture is what it was: gr_same(its signature) is 1, the page draws nothing in it and
 * gfx_end sends nothing of it - the screen keeps it. Not with a message over it, not on a page's first frame */
static uint32_t gr_sig_last;
static uint8_t gr_skip;
static int gr_same(const tb_view_t *v, uint32_t sig)
{
    int toast_on = v->hud >= 0 && v->hud_name && v->hud_alpha > 70 && strcmp(v->hud_name, "PAGE");
    if (toast_on)
        sig ^= 0x9E3779B9u + (uint32_t)v->hud_alpha * 2654435761u + str_hash((uint32_t)v->hud, v->hud_name) +
               str_hash(1u, v->hud_value);
    gr_skip = (uint8_t)(!scr_force && !dc_stale && sig == gr_sig_last && !toast_on);
    gr_sig_last = sig;
    return gr_skip;
}

void gfx_end(void (*emit)(int x, int y, int w, int h, uint16_t *px), int force)
{
    (void)emit, (void)force;
    if (!pic_on)
        return;
    if (gr_skip) {                                     /* (the picture as it was: nothing of it sent) */
        gr_skip = 0;
        pic_on = 0;
        gfx_area_map = 0;
        if (scr_view)
            foot(scr_view);
        scr_force = 0;
        return;
    }
    if (scr_view)
        toast(scr_view);
    dc_frame(scr_force);
    cv_blit_cells(0, GR_Y);
    pic_on = 0;
    gfx_area_map = 0;
    if (scr_view)
        foot(scr_view);
    scr_force = 0;
}
