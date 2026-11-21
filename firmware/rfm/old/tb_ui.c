/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the screens (tb_ui.h). Black, and the sound in the theme's light: every track is its own waveform
 * under a plain playhead, the knobs a heads-up display with the track's picture; the menus each draw what they
 * set (the envelope's curve, the LFO's wave, the arpeggiator's notes, the scale on a keyboard). */
#include "tb_ui.h"
#include "tb_engine.h"
#include "tb_gfx.h"
#include "tb_dsp.h"
#if !TB_HAS_FONT_XL
#define TB_FONT_XL TB_FONT_L                /* (the FM-1: no extra large font) */
#endif
#include <stdio.h>
#include <string.h>

#define CREAM gfx_ink(0)
#define GREY gfx_ink(1)
#define DIM gfx_ink(2)

static const int TOP_H = 24, TITLE_Y = 39, LANES_Y = 45, KNOBS_Y = 204;

/* ------------------------------------------------------------------ the look --- */
/* Rave FM's: flat panels on the purple ground, the violet for what is set, the orange for what is lit now */
#define PANEL gfx_heat(34)
#define PANEL_HI gfx_heat(54)
#define EDGE gfx_heat(66)
#define WELL gfx_heat(7)
#define ACCENT gfx_heat(200)
#define HOT gfx_heat(226)
static uint32_t pink(void) { return gfx_palette_now() == GFX_RAVE ? TB_RGB(255, 69, 197) : gfx_heat(240); }
/* a panel: flat, rounded, a light along its top edge */
static void panel(int x, int y, int w, int h, int r, uint32_t c)
{
    gfx_rrect(x, y, w, h, r, c, 255, 0);
    gfx_rect(x + r, y, w - 2 * r, 1, gfx_mix(c, TB_RGB(255, 255, 255), 30), 255, GFX_NORMAL);
}
/* a graph's ground: darker than the panels, a hairline round it */
static void well(int x, int y, int w, int h)
{
    gfx_rrect(x, y, w, h, 7, WELL, 255, 0);
    gfx_rrect(x, y, w, h, 7, EDGE, 150, 1);
}
/* a word in a pill: outlined (a page), or filled (a value lit); its width */
static int chip(int x, int base, const char *s, uint32_t c, int filled, int right)
{
    int w = gfx_text_width(&TB_FONT_S, s) + 10;
    if (right)
        x -= w;
    gfx_rrect(x, base - 10, w, 14, 7, c, filled ? 255 : 150, filled ? 0 : 1);
    gfx_text(&TB_FONT_S, x + 5, base, s, filled ? TB_RGB(20, 4, 30) : c, 255, GFX_LEFT);
    return w;
}
/* the pages as dots, the one shown a bar; their left edge */
static int dots(int right, int base, int n, int on)
{
    int i, x = right;
    if (n < 2)
        return right;
    for (i = n - 1; i >= 0; i--) {
        int w = i == on ? 10 : 4;
        x -= w;
        gfx_rrect(x, base - 6, w, 4, 2, i == on ? HOT : gfx_heat(70), 255, 0);
        x -= 4;
    }
    return x;
}
/* the row under the top bar: what this is (big), its page (a pill), the pages; the space left on the right */
static int title_row(const char *name, const char *page, int npages, int on)
{
    int x = 7 + gfx_text(&TB_FONT_M, 7, TITLE_Y, name ? name : "", CREAM, 255, GFX_LEFT);
    if (page && *page)
        chip(x + 6, TITLE_Y - 1, page, ACCENT, 0, 0);
    return dots(TB_W - 6, TITLE_Y, npages, on);
}

/* ------------------------------------------------------------------ the icons --- */
/* drawn in a box of 16 units around the centre, scaled to the size asked */
static int IX, IY, IS;
static uint32_t IC;
static uint8_t IA;
static float tb_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int ux(float u) { return IX + (int)(u * (float)IS / 16.0f + (u < 0 ? -0.5f : 0.5f)); }
static int uy(float u) { return IY + (int)(u * (float)IS / 16.0f + (u < 0 ? -0.5f : 0.5f)); }
static int us(float u) { int v = (int)(u * (float)IS / 16.0f + 0.5f); return v < 1 ? 1 : v; }
static void iline(float x0, float y0, float x1, float y1, float w)
{
    int w16 = (int)(w * (float)IS);                   /* (w units x size / 16 px, in 1/16 px) */
    gfx_line(ux(x0), uy(y0), ux(x1), uy(y1), w16 < 12 ? 12 : w16, IC, IA, GFX_NORMAL);
}
static void iring(float x, float y, float r, float th) { gfx_arc(ux(x), uy(y), us(r), us(th), 0, 1024, IC, IA); }
static void ibox(float x, float y, float w, float h, float r, int line, uint32_t c)
{
    gfx_rrect(ux(x), uy(y), us(w), us(h), (int)(r * (float)IS / 16.0f), c, IA, line);
}
static void idot(float x, float y, float r, uint32_t c) { ibox(x - r, y - r, 2 * r, 2 * r, r, 0, c); }

void tb_ui_icon(unsigned icon, int cx, int cy, int size, uint32_t c, uint8_t a)
{
    int i;
    uint32_t bg = TB_RGB(4, 3, 10), hot = gfx_heat(250);
    IX = cx, IY = cy, IS = size, IC = c, IA = a;
    switch (icon) {
    case TB_ICON_KICK:                                  /* a bass drum seen from the side: its head, its shell, its */
        ibox(-3.2f, -7.2f, 10.6f, 12.4f, 1.6f, 1, c);   /* spurs, the pedal and its beater at the back */
        iline(1.2f, -7.0f, 1.2f, 5.0f, 0.5f);
        iline(4.4f, -7.0f, 4.4f, 5.0f, 0.5f);
        idot(-3.2f, -1.0f, 6.4f, bg);
        iring(-3.2f, -1.0f, 6.4f, 1.4f);
        iring(-3.2f, -1.0f, 4.3f, 0.6f);
        idot(-3.2f, -1.0f, 1.3f, hot);
        iline(-6.6f, 4.2f, -8.6f, 7.6f, 1.1f);
        iline(5.0f, 5.0f, 6.2f, 7.6f, 1.0f);
        iline(7.6f, 7.6f, 8.8f, 7.6f, 1.2f);
        iline(8.2f, 7.4f, 8.2f, 1.6f, 0.8f);
        idot(8.2f, 1.4f, 0.9f, c);
        break;
    case TB_ICON_KIT:                                   /* a whole kit: bass drum, toms, snare, floor tom, cymbals */
        iring(0, 3.6f, 4.0f, 1.1f);
        idot(0, 3.6f, 1.0f, hot);
        ibox(-4.6f, -3.6f, 4.0f, 3.0f, 0.8f, 1, c);
        ibox(0.6f, -3.6f, 4.0f, 3.0f, 0.8f, 1, c);
        ibox(-8.8f, 0.4f, 4.2f, 2.4f, 0.6f, 1, c);
        iline(-6.7f, 2.8f, -6.7f, 7.8f, 0.7f);
        ibox(4.8f, 0.8f, 3.8f, 4.6f, 0.6f, 1, c);
        iline(5.2f, 5.4f, 5.0f, 7.8f, 0.7f);
        iline(8.2f, 5.4f, 8.4f, 7.8f, 0.7f);
        iline(-8.8f, -6.0f, -3.8f, -7.4f, 1.1f);
        iline(-6.3f, -6.6f, -6.3f, 0.4f, 0.6f);
        iline(3.8f, -7.4f, 8.8f, -6.0f, 1.1f);
        iline(6.3f, -6.6f, 6.3f, 0.8f, 0.6f);
        break;
    case TB_ICON_909: {                                 /* the drum machine: knobs, a display, the row of steps */
        static const uint8_t H[4] = {150, 190, 225, 250};
        ibox(-8.6f, -6.2f, 17.2f, 12.4f, 2.0f, 1, c);
        idot(-5.6f, -2.8f, 1.2f, c);
        idot(-2.2f, -2.8f, 1.2f, c);
        idot(1.2f, -2.8f, 1.2f, c);
        ibox(3.6f, -4.0f, 3.4f, 2.2f, 0.5f, 0, gfx_mix(c, TB_RGB(0, 0, 0), 120));
        for (i = 0; i < 8; i++)
            ibox(-7.1f + (float)i * 1.85f, 1.3f, 1.3f, 2.6f, 0.3f, 0, gfx_heat(H[i / 2]));
        break;
    }
    case TB_ICON_BASS:                                  /* a bass speaker: the woofer, the tweeter */
        ibox(-6.4f, -8.4f, 12.8f, 16.8f, 2.0f, 1, c);
        iring(0, 2.4f, 4.2f, 1.3f);
        idot(0, 2.4f, 1.4f, hot);
        iring(0, -4.9f, 1.4f, 0.9f);
        break;
    case TB_ICON_LEAD:                                  /* two notes, beamed */
        ibox(-7.6f, 3.0f, 4.6f, 3.4f, 1.7f, 0, c);
        ibox(1.4f, 1.4f, 4.6f, 3.4f, 1.7f, 0, c);
        iline(-3.4f, 4.6f, -3.4f, -6.4f, 1.1f);
        iline(5.6f, 3.0f, 5.6f, -8.0f, 1.1f);
        iline(-3.4f, -6.4f, 5.6f, -8.0f, 2.0f);
        break;
    case TB_ICON_STAB:                                  /* the keys of a chord */
        ibox(-8.6f, -6.6f, 17.2f, 13.2f, 1.5f, 1, c);
        iline(-4.3f, -6.0f, -4.3f, 6.2f, 0.6f);
        iline(0, -6.0f, 0, 6.2f, 0.6f);
        iline(4.3f, -6.0f, 4.3f, 6.2f, 0.6f);
        ibox(-5.6f, -6.6f, 2.6f, 7.0f, 0.4f, 0, c);
        ibox(-1.3f, -6.6f, 2.6f, 7.0f, 0.4f, 0, c);
        ibox(3.0f, -6.6f, 2.6f, 7.0f, 0.4f, 0, c);
        idot(-6.45f, 3.6f, 0.9f, hot);
        idot(-2.15f, 3.6f, 0.9f, hot);
        idot(2.15f, 3.6f, 0.9f, hot);
        break;
    case TB_ICON_HOOVER:                                /* the rave's hoover: a vacuum cleaner, its hose and wand */
        ibox(-8.6f, 0.6f, 9.4f, 6.0f, 3.0f, 1, c);
        idot(-6.0f, 7.4f, 1.2f, c);
        idot(-1.6f, 7.4f, 1.2f, c);
        idot(-5.6f, 3.6f, 0.8f, hot);
        iline(0.8f, 2.6f, 3.4f, -1.4f, 1.2f);
        iline(3.4f, -1.4f, 2.8f, -5.2f, 1.2f);
        iline(2.8f, -5.2f, 5.6f, -7.6f, 1.2f);
        iline(5.6f, -7.6f, 7.0f, 6.0f, 1.2f);
        ibox(4.4f, 6.0f, 5.0f, 1.9f, 0.9f, 0, c);
        break;
    case TB_ICON_SAMPLE:                                /* a microphone */
        ibox(-3.0f, -8.6f, 6.0f, 9.6f, 3.0f, 1, c);
        iline(-1.6f, -5.6f, 1.6f, -5.6f, 0.6f);
        iline(-1.6f, -3.6f, 1.6f, -3.6f, 0.6f);
        iline(-1.6f, -1.6f, 1.6f, -1.6f, 0.6f);
        gfx_arc(ux(0), uy(-2.4f), us(5.4f), us(1.0f), 256, 768, c, a);
        iline(0, 3.0f, 0, 6.6f, 1.1f);
        iline(-3.4f, 7.2f, 3.4f, 7.2f, 1.2f);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ the pieces --- */
/* the playhead: a plain line, lit */
static void playhead(int x, int y0, int h, uint8_t a)
{
    gfx_rect(x - 2, y0, 5, h, gfx_heat(170), (uint8_t)(a / 5), GFX_ADD);
    gfx_rect(x, y0, 1, h, CREAM, a, GFX_NORMAL);
}

/* the largest of the fonts given that s fits in w (the last one if none does) */
static const tb_font_t *fit(const char *s, int w, const tb_font_t *a, const tb_font_t *b, const tb_font_t *c)
{
    if (a && gfx_text_width(a, s) <= w)
        return a;
    if (b && gfx_text_width(b, s) <= w)
        return b;
    return c;
}

/* text in at most w px, on one line or (failing that) two, centred at cx around baseline y */
static void text_fit(int cx, int y, int w, const char *s, uint32_t col, uint8_t a, const tb_font_t *big)
{
    const tb_font_t *f = fit(s, w, big, &TB_FONT_L, &TB_FONT_M);
    if (gfx_text_width(f, s) <= w || !strchr(s, ' ')) {
        gfx_text(f, cx, y, s, col, a, GFX_CENTER);
        return;
    }
    {                                                   /* two lines: cut at the space nearest the middle */
        char l1[32], l2[32];
        int n = (int)strlen(s), best = -1, i;
        for (i = 0; i < n; i++)
            if (s[i] == ' ' && (best < 0 || (i - n / 2) * (i - n / 2) < (best - n / 2) * (best - n / 2)))
                best = i;
        snprintf(l1, sizeof l1, "%.*s", best, s);
        snprintf(l2, sizeof l2, "%s", s + best + 1);
        f = gfx_text_width(&TB_FONT_L, l1) <= w && gfx_text_width(&TB_FONT_L, l2) <= w ? &TB_FONT_L : &TB_FONT_M;
        gfx_text(f, cx, y - f->ascent / 2 - 2, l1, col, a, GFX_CENTER);
        gfx_text(f, cx, y + f->ascent / 2 + 4, l2, col, a, GFX_CENTER);
    }
}

static void play_icon(int x, int y, int playing)
{
    if (playing) {
        int i;
        for (i = 0; i < 10; i++)
            gfx_rect(x + i, y + i / 2, 1, 10 - i, gfx_heat(225), 255, GFX_NORMAL);
    } else
        gfx_rrect(x, y, 9, 9, 2, GREY, 255, 0);
}

static void top_bar(const tb_view_t *v)
{
    char b[16];
    int w, i;
    unsigned pulse = v->playing ? (unsigned)(1023 - v->beat_phase) / 4u : 0;
    /* the band, the beat lighting its lower edge (the bar's first beat the most) */
    gfx_rect(0, 0, TB_W, TOP_H, gfx_heat(6), 255, GFX_NORMAL);
    gfx_rect(0, TOP_H, TB_W, 1, gfx_heat(40), 255, GFX_NORMAL);
    if (pulse)
        gfx_rect(0, TOP_H, TB_W, 1, v->bar_beat == 0 ? HOT : ACCENT, (uint8_t)(pulse * (v->bar_beat == 0 ? 1u : 2u) / 3u), GFX_NORMAL);
    /* the pattern: a badge (the one coming at the bar: blinking beside it) */
    b[0] = (char)('A' + v->pattern), b[1] = 0;
    gfx_rrect(5, 4, 19, 17, 5, pink(), 255, 0);
    gfx_text(&TB_FONT_M, 15, 18, b, TB_RGB(20, 4, 30), 255, GFX_CENTER);
    w = 30;
    if (v->queued >= 0) {
        b[0] = (char)('A' + v->queued);
        gfx_rrect(27, 5, 15, 15, 5, pink(), (v->ms / 250u) % 2u ? 255 : 90, 1);
        gfx_text(&TB_FONT_S, 35, 16, b, pink(), (v->ms / 250u) % 2u ? 255 : 90, GFX_CENTER);
        w = 47;
    }
    /* the tempo */
    snprintf(b, sizeof b, "%u", v->bpm10 / 10u);
    w += gfx_text(&TB_FONT_L, w, 21, b, HOT, 255, GFX_LEFT);
    if (v->bpm10 % 10u) {
        snprintf(b, sizeof b, ".%u", v->bpm10 % 10u);
        w += gfx_text(&TB_FONT_M, w, 21, b, gfx_heat(210), 255, GFX_LEFT);
    }
    gfx_text(&TB_FONT_S, w + 3, 20, "BPM", GREY, 255, GFX_LEFT);
    /* the DROP counting down, the RISER, the MUTATIONS on */
    if (v->drop || v->riser) {
        char d[8];
        int n = v->drop ? v->drop : v->riser, left = v->drop ? v->drop_left : v->riser_left, lit = (v->ms / 200u) % 2u || n == 2;
        snprintf(d, sizeof d, n == 2 ? "%s %d" : "%s !", v->drop ? "DROP" : "RISER", left);
        gfx_rrect(118, 5, 46, 15, 7, lit ? HOT : gfx_heat(90), 255, 0);
        gfx_text(&TB_FONT_S, 141, 16, d, TB_RGB(20, 4, 30), 255, GFX_CENTER);
    } else if (v->mutating) {
        gfx_rrect(142, 5, 22, 15, 7, ACCENT, 255, 1);
        gfx_text(&TB_FONT_S, 153, 16, "M", ACCENT, 255, GFX_CENTER);
    }
    /* recording */
    if (v->rec) {
        unsigned p = v->playing ? 150u + pulse / 4u : 200u;
        gfx_glow(170, 13, 8, TB_RGB(255, 40, 60), (uint8_t)(p / 3u));
        gfx_rrect(166, 9, 9, 9, 4, TB_RGB(255, 60, 80), 255, 0);
    }
    /* the bar: four beats, the one now lit */
    for (i = 0; i < 4; i++) {
        int cx = 184 + i * 9, lit = v->playing && i == v->bar_beat;
        gfx_rrect(cx - 2, 10, 5, 5, 2, lit ? (i ? ACCENT : HOT) : gfx_heat(40), 255, 0);
    }
    play_icon(224, 8, v->playing);
}

/* the row under the top bar: a hint, or the track and the page its knobs are on */
static void hint_line(const tb_view_t *v, int y)
{
    (void)y;
    if (v->hint) {
        gfx_text(&TB_FONT_S, 7, TITLE_Y - 1, v->hint, GREY, 255, GFX_LEFT);
        return;
    }
    title_row(v->lane[v->track].name, v->strip_title, v->strip_pages, v->strip_page);
}

static void lane(const tb_view_t *v, int t, int y, int h)
{
    const tb_lane_t *l = &v->lane[t];
    int sel = t == v->track, wx, ww, i;
    uint32_t ink = l->muted ? DIM : sel ? CREAM : l->active ? CREAM : GREY;
    if (sel) {                                          /* (the selected one: a panel, an orange mark) */
        panel(2, y, TB_W - 4, h, 6, PANEL);
        gfx_rrect(2, y + 5, 3, h - 10, 1, HOT, 255, 0);
    }
    /* its name in the column every lane shares (so the playheads line up): the selected track's big */
    wx = 64;
    if (sel && h > 26) {
        const tb_font_t *f = gfx_text_width(&TB_FONT_M, l->name) <= 54 ? &TB_FONT_M : &TB_FONT_S;
        gfx_text(f, 7, y + h / 2 + 5, l->name, ink, 255, GFX_LEFT);
    } else
        gfx_text(&TB_FONT_S, 7, y + h / 2 + 4, l->name, ink, 255, GFX_LEFT);
    ww = TB_W - 6 - wx;
    /* the beats of its loop: faint ticks, the bars brighter */
    if (l->beats) {
        int every = l->beats > 32 ? 4 : 1;
        for (i = every; i < l->beats; i += every) {
            int x = wx + i * ww / l->beats;
            gfx_rect(x, y + 3, 1, h - 6, gfx_heat(i % 4 ? 22 : 46), 255, GFX_NORMAL);
        }
    }
    {
        uint8_t pk[TB_W], ht[TB_W];
        unsigned mx = 0, g;
        for (i = 0; i < TB_WAVE_W; i++)                /* each lane to its loudest (up to x6) */
            mx = l->wave[i] > mx ? l->wave[i] : mx;
        g = mx > 42u ? 255u * 256u / mx : 6u * 256u;
        for (i = 0; i < ww; i++) {                     /* (a narrower lane: the loop squeezed into it) */
            int c0 = i * TB_WAVE_W / ww, c1 = (i + 1) * TB_WAVE_W / ww, c, m = 0;
            for (c = c0; c < c1 || c == c0; c++)       /* (the loudest of the columns it covers) */
                m = l->wave[c] > m ? l->wave[c] : m;
            m = (int)((unsigned)m * g >> 8);
            pk[i] = (uint8_t)(m > 255 ? 255 : m);
            ht[i] = l->heat[c0];
        }
        gfx_wave(wx, y + 1, ww, h - 2, pk, ht, (uint8_t)(sel ? 110 : 0));
        if (l->muted)
            gfx_rect(wx, y + 1, ww, h - 2, TB_RGB(0, 0, 0), 175, GFX_NORMAL);
        if (v->playing && !l->muted)
            playhead(wx + l->pos * ww / TB_WAVE_W, y + 2, h - 4, sel ? 240 : 170);
    }
    if (v->mute_layer) {                                /* GLO held: which key mutes it (lit: it plays) */
        char b[4];
        int bx = TB_W - 14;
        snprintf(b, sizeof b, "%d", t + 1);
        gfx_rect(TB_W - 30, y + 1, 28, h - 2, TB_RGB(0, 0, 0), 200, GFX_NORMAL);
        gfx_rrect(bx - 9, y + h / 2 - 7, 18, 14, 4, l->muted ? gfx_heat(40) : gfx_heat(210), 255, 0);
        gfx_text(&TB_FONT_S, bx, y + h / 2 + 4, b, l->muted ? GREY : TB_RGB(0, 0, 0), 255, GFX_CENTER);
    } else if (l->muted)
        gfx_text(&TB_FONT_S, TB_W - 6, y + h / 2 + 4, "MUTE", gfx_heat(150), 255, GFX_RIGHT);
    else if (l->solo)
        gfx_text(&TB_FONT_S, TB_W - 6, y + h / 2 + 4, "SOLO", gfx_heat(230), 255, GFX_RIGHT);
}

/* a value as its number and, smaller, its unit */
static int split_unit(const char *v)
{
    static const char *const U[] = {"ms", "s", "%", "Hz", "kHz", "dB", "st", "x"};
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

/* the knobs: four cards, as Rave FM's (the name, the value big, its unit small, a bar); the one turned lit */
static void knobs(const tb_view_t *v)
{
    int k;
    for (k = 0; k < 4; k++) {
        int x = 1 + k * 60, y = KNOBS_Y, w = 58, h = TB_H - KNOBS_Y, bw = w - 12, len = v->knob_pos[k] * bw / 255;
        int lit = v->hud == k && v->hud_alpha && !v->hud_name;
        if (!v->knob_name[k]) {
            gfx_rrect(x, y, w, h, 6, gfx_heat(20), 255, 0);
            continue;
        }
        panel(x, y, w, h, 6, v->knob_lock[k] ? gfx_heat(60) : PANEL);
        if (v->knob_lock[k] || lit)
            gfx_rrect(x, y, w, h, 6, v->knob_lock[k] ? HOT : ACCENT, 255, 1);
        {                                               /* (names and values cut to their card) */
            char nm[16], vl[16];
            const tb_font_t *f;
            int cut, wx;
            snprintf(nm, sizeof nm, "%s", v->knob_name[k]);
            if (gfx_text_width(&TB_FONT_S, nm) > w - 5 && !strcmp(nm, "SIDECHAIN"))
                snprintf(nm, sizeof nm, "S.CHAIN");   /* (too wide here: its short name; turned, the card says it whole) */
            while (gfx_text_width(&TB_FONT_S, nm) > w - 5 && strlen(nm) > 3)
                nm[strlen(nm) - 1] = 0;
            snprintf(vl, sizeof vl, "%s", v->knob_value[k]);
            cut = split_unit(vl);
            if (cut < (int)strlen(vl)) {                /* a number and its unit */
                char num[16];
                const char *u = vl + cut + (vl[cut] == ' ');
                snprintf(num, sizeof num, "%.*s", cut, vl);
                f = gfx_text_width(&TB_FONT_M, num) + gfx_text_width(&TB_FONT_S, u) + 2 <= w - 9 ? &TB_FONT_M : &TB_FONT_S;
                wx = gfx_text(f, x + 4, y + 27, num, CREAM, 255, GFX_LEFT);
                gfx_text(&TB_FONT_S, x + 5 + wx, y + 27, u, GREY, 255, GFX_LEFT);
            } else {
                f = fit(vl, w - 9, &TB_FONT_M, &TB_FONT_S, &TB_FONT_S);
                while (gfx_text_width(f, vl) > w - 9 && strlen(vl) > 3)
                    vl[strlen(vl) - 1] = 0;
                gfx_text(f, x + 4, y + 27, vl, CREAM, 255, GFX_LEFT);
            }
            gfx_text(&TB_FONT_S, x + 4, y + 12, nm, v->knob_lock[k] ? HOT : lit ? ACCENT : GREY, 255, GFX_LEFT);
        }
        gfx_rrect(x + 6, y + 30, bw, 2, 1, gfx_heat(16), 255, 0);
        if (v->knob_bip[k]) {                           /* from the middle */
            int m = x + 6 + bw / 2, e = x + 6 + len;
            gfx_rect(m, y + 29, 1, 4, GREY, 255, GFX_NORMAL);
            if (e != m)
                gfx_rrect(e < m ? e : m, y + 30, e < m ? m - e : e - m, 2, 1, v->knob_lock[k] ? HOT : ACCENT, 255, 0);
        } else if (len)
            gfx_rrect(x + 6, y + 30, len < 2 ? 2 : len, 2, 1, v->knob_lock[k] ? HOT : ACCENT, 255, 0);
    }
}

/* the heads-up display: what is being turned, big, over a darkened screen, with the track's picture. The value
 * always inside the ring: the biggest font it fits in, two lines if it must */
static void hud(const tb_view_t *v)
{
#if !TB_HAS_HUD
    /* the FM-1 build: a plain card across the middle, what is turned and its value (a preset's name) */
    int a = v->hud_alpha;
    const char *name, *value;
    if (v->hud < 0 || !a)
        return;
    if (v->hud_name)
        name = v->hud_name, value = v->hud_value;
    else {
        if (!v->knob_name[v->hud])
            return;
        name = v->knob_name[v->hud], value = v->knob_value[v->hud];
    }
    gfx_rrect(14, 78, TB_W - 28, 74, 10, gfx_bg(), (uint8_t)(a * 15 / 16), 0);
    gfx_rrect(14, 78, TB_W - 28, 74, 10, ACCENT, (uint8_t)a, 1);
    gfx_text(fit(name, TB_W - 48, &TB_FONT_M, &TB_FONT_S, &TB_FONT_S), 120, 100, name, gfx_heat(200), (uint8_t)a, GFX_CENTER);
    text_fit(120, 136, TB_W - 44, value, CREAM, (uint8_t)a, &TB_FONT_L);
}
#else
    int a = v->hud_alpha, end, pos;
    const char *name, *value;
    if (v->hud < 0 || !a)
        return;
    if (v->hud_name)
        name = v->hud_name, value = v->hud_value, pos = v->hud_pos;
    else {
        if (!v->knob_name[v->hud])
            return;
        name = v->knob_name[v->hud], value = v->knob_value[v->hud], pos = v->knob_pos[v->hud];
    }
    end = 128 + pos * 768 / 255;
    gfx_rect(0, 0, TB_W, TB_H, gfx_bg(), (uint8_t)(a * 5 / 6), GFX_NORMAL);
    gfx_glow(120, 112, 86, gfx_heat(120), (uint8_t)(a / 3));
    gfx_arc(120, 112, 70, 10, 128, 896, gfx_heat(30), (uint8_t)a);
    gfx_arc(120, 112, 70, 10, 128, end, gfx_heat(170 + pos * 80 / 255), (uint8_t)a);
    gfx_text(fit(name, 92, &TB_FONT_M, &TB_FONT_S, &TB_FONT_S), 120, 76, name, gfx_heat(200), (uint8_t)a, GFX_CENTER);
    text_fit(120, 120, 104, value, CREAM, (uint8_t)a, &TB_FONT_XL);
    tb_ui_icon(v->lane[v->track].icon, 120, 156, 26, gfx_heat(215), (uint8_t)a);
    gfx_text(&TB_FONT_S, 120, 202, v->lane[v->track].name, GREY, (uint8_t)a, GFX_CENTER);
}
#endif

void tb_ui_home(const tb_view_t *v)
{
    int t, y = LANES_Y;
    gfx_begin();
    top_bar(v);
    hint_line(v, TITLE_Y);
    for (t = 0; t < TB_TRACKS; t++) {
        int h = v->lane_h[t];
        lane(v, t, y, h);
        y += h;
    }
    knobs(v);
    hud(v);
}

static float usin(float u) { return (float)tb_sin((uint32_t)(int64_t)(u * 4294967296.0f)) / 32767.0f; }
#if TB_HAS_PICS
/* EDIT's picture: the oscillator's wave (one cycle and a bit), the filter's curve, the break and its slices */
static void edit_picture(const tb_view_t *v, int x0, int y0, int w, int h)
{
    const uint8_t *p = v->edit_p;
    int i, px = 0, py = 0;
    if (v->edit_kind == 1) {                            /* the wave: two cycles, its sub under, the detune as a shadow */
        unsigned wave = p[0];
        for (i = 0; i <= 96; i++) {
            float u = (float)(i % 48) / 48.0f, y;
            switch (wave) {
            case 1: y = u < 0.5f ? 0.8f : -0.8f; break;
            case 2: y = u < 0.3f ? 0.8f : -0.8f; break;
            case 5: { float t = u < 0.5f ? u * 4 - 1 : 3 - u * 4; y = t * (1.5f - 0.5f * t * t) * 0.8f; break; }
            case 6: { float t = u < 0.5f ? u * 4 - 1 : 3 - u * 4; y = (t * 0.6f + (u < 0.25f || (u >= 0.5f && u < 0.75f) ? 0.25f : -0.25f)); break; }
            case 7: case 8: { float t = u < 0.5f ? u * 4 - 1 : 3 - u * 4; y = t > 0 ? 0.8f - (1 - t) * (1 - t) * 0.8f * (1 + p[1] / 64.0f) : -0.8f + (1 + t) * (1 + t) * 0.8f; break; }
            case 4: y = (u * 2 - 1) * 0.5f + ((u + 0.37f > 1 ? u - 0.63f : u + 0.37f) * 2 - 1) * 0.3f; break;
            case 9: { float r = 1.0f + p[1] / 18.0f, q = u * r; y = 0.8f - (q - (int)q) * 1.6f; break; }   /* SYNC */
            case 10: { float q = u * 3.0f + p[1] / 64.0f; y = 0.8f * (1 - u) * (q - (int)q < 0.5f ? 1 : -1) * (1 - 2 * (q - (int)q) * (q - (int)q)); break; }
            case 11: { float r = 1.0f + p[1] / 42.0f, q = u * r; y = (0.8f - u * 1.6f) * (q - (int)q < 0.5f ? 4 * (q - (int)q) - 1 : 3 - 4 * (q - (int)q)); break; }
            case 12: y = (u < 0.5f ? 0.45f : -0.45f) + (0.35f - (u * 2 - (int)(u * 2)) * 0.7f); break;   /* STAB */
            case 13: { y = 0.3f * (usin(u) * 1.2f + usin(2 * u) * 0.6f + usin(4 * u) * 0.4f); break; }
            case 14: { float a = usin(u) * (1.0f + p[1] / 32.0f), f = a; while (f > 1 || f < -1) f = f > 1 ? 2 - f : -2 - f; y = f * 0.8f; break; }
            default: y = 0.8f - u * 1.6f;
            }
            {
                int x = x0 + i * w / 96, yy = y0 + h / 2 - (int)(y * (float)(h / 2 - 6));
                if (i && (wave == 1 || wave == 2) && ((i % 48 == 0) || (i % 48 == (wave == 1 ? 24 : 14))))
                    gfx_line(px, py, px, yy, 30, gfx_heat(225), 255, GFX_NORMAL);
                else if (i && i % 48 == 0 && wave != 5 && wave < 6)
                    gfx_line(px, py, x, yy, 30, gfx_heat(225), 255, GFX_NORMAL);
                if (i && i % 48)
                    gfx_line(px, py, x, yy, 30, gfx_heat(225), 255, GFX_NORMAL);
                px = x, py = yy;
            }
        }
        if (p[2]) {                                     /* the sub: a square an octave down, as deep as SUB */
            int a = p[2] * (h / 4) / 127, yb = y0 + h - 8;
            gfx_line(x0, yb - a, x0 + w / 2, yb - a, 18, gfx_heat(120), 255, GFX_NORMAL);
            gfx_line(x0 + w / 2, yb - a, x0 + w / 2, yb, 18, gfx_heat(120), 255, GFX_NORMAL);
            gfx_line(x0 + w / 2, yb, x0 + w, yb, 18, gfx_heat(120), 255, GFX_NORMAL);
        }
        return;
    }
    if (v->edit_kind == 5) {                            /* MACRO: its type, what the knob moves, how far it is */
        static const char *const NM[5] = {"MACRO OFF", "SCREECH", "REESE", "WOBBLE", "GROWL"};
        static const char *const WH[5] = {"TYPE: pick what the knob does", "the filter screams, fast vibrato",
                                          "the saws drift apart, the filter sways", "the filter wobbles over 4 octaves",
                                          "the filter growls, more drive"};
        unsigned t = p[1] < 5 ? p[1] : 0;
        int fw = (w - 20) * p[0] / 127;
        gfx_text(&TB_FONT_L, x0 + w / 2, y0 + 34, NM[t], t ? CREAM : GREY, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, x0 + w / 2, y0 + 54, WH[t], GREY, 255, GFX_CENTER);
        gfx_rrect(x0 + 10, y0 + 66, w - 20, 10, 5, gfx_heat(30), 255, 0);
        if (t && fw > 2)
            gfx_rrect(x0 + 10, y0 + 66, fw, 10, 5, gfx_heat(220), 255, 0);
        gfx_text(&TB_FONT_S, x0 + w / 2, y0 + h - 2, "HOME: MACRO by hand (MAIN)", DIM, 255, GFX_CENTER);
        return;
    }
    if (v->edit_kind == 6) {                            /* HOOVER: the pitch over the first 600 ms (dive, whoop) */
        float dive = (p[2] - 64) / 64.0f * 12.0f, who = p[3] / 127.0f * 5.0f;
        gfx_rect(x0, y0 + h / 2, w, 1, gfx_heat(50), 255, GFX_NORMAL);
        float kb = 1, k1 = 1, k2 = 1, rb = 1, r1 = 1, r2 = 1;
        int q;
        for (q = 0; q < 10; q++)                        /* (a step of the drawing: ~10 of the synth's sub-blocks) */
            kb *= 1.0f - 1.0f / 170.0f, k1 *= 1.0f - 1.0f / 400.0f, k2 *= 1.0f - 1.0f / 30.0f;
        for (i = 0; i <= 80; i++, rb *= kb, r1 *= k1, r2 *= k2) {
            float st = dive * rb + who * r1 - who * r2;
            int x = x0 + i * w / 80, y = y0 + h / 2 - (int)(tb_clampf(st / 8.0f, -1, 1) * (float)(h / 2 - 6));
            if (i)
                gfx_line(px, py, x, y, 30, gfx_heat(230), 255, GFX_NORMAL);
            px = x, py = y;
        }
        gfx_text(&TB_FONT_S, x0 + 2, y0 + 12, "PITCH", DIM, 255, GFX_LEFT);
        gfx_text(&TB_FONT_S, x0 + w - 2, y0 + h - 4, "600 ms", DIM, 255, GFX_RIGHT);
        return;
    }
    if (v->edit_kind == 2) {                            /* the filter: low-pass, its peak at the cutoff */
        float fc = p[4] / 127.0f, q = p[5] / 127.0f;
        for (i = 0; i <= 64; i++) {
            float f = i / 64.0f, r = f - fc, g = r < 0 ? 1.0f : 1.0f / (1.0f + r * r * 120.0f);
            int x, y;
            g += q * 0.9f / (1.0f + r * r * 900.0f);
            if (g > 1.9f)
                g = 1.9f;
            x = x0 + i * w / 64, y = y0 + h - 6 - (int)(g / 1.9f * (float)(h - 12));
            if (i) {
                gfx_line(px, py, x, y, 30, gfx_heat(225), 255, GFX_NORMAL);
                gfx_rect(px, y, x - px, y0 + h - 6 - y, gfx_heat(70), 60, GFX_NORMAL);
            }
            px = x, py = y;
        }
        if (p[6]) {                                     /* where its envelope takes it */
            int xe = x0 + (int)((fc + p[6] / 127.0f * 0.5f > 1 ? 1 : fc + p[6] / 127.0f * 0.5f) * (float)w);
            gfx_line(x0 + (int)(fc * (float)w), y0 + 10, xe, y0 + 10, 18, gfx_heat(160), 255, GFX_NORMAL);
            gfx_rrect(xe - 2, y0 + 8, 5, 5, 2, gfx_heat(200), 255, 0);
        }
        return;
    }
    {                                                   /* the break, its slices (BREAK) or the part played (SAMPLE) */
        const tb_seqview_t *s = &v->seq;
        uint8_t pk[TB_W], ht[TB_W];
        for (i = 0; i < w; i++)
            pk[i] = s->brk_wave[i * 200 / w], ht[i] = 130;
        if (v->edit_kind == 4) {
            int a = x0 + p[0] * w / 128, b = a + (p[1] + 1) * w / 128;
            if (b > x0 + w)
                b = x0 + w;
            gfx_rect(a, y0, b - a, h, gfx_heat(60), 255, GFX_NORMAL);
        }
        gfx_wave(x0, y0 + 4, w, h - 8, pk, ht, 80);
        if (v->edit_kind == 3)
            for (i = 0; i < s->nslices; i++) {
                int x = x0 + s->brk_cut[i] * w / 200, x1 = x0 + (i + 1 < s->nslices ? s->brk_cut[i + 1] : 200) * w / 200;
                if (i == s->slice_lit) {                /* the slice set by hand now: lit, its number */
                    char b[4];
                    gfx_rect(x, y0, x1 - x, h, gfx_heat(120), 70, GFX_ADD);
                    gfx_rect(x, y0, 2, h, CREAM, 255, GFX_NORMAL);
                    snprintf(b, sizeof b, "%d", i + 1);
                    gfx_text(&TB_FONT_S, x + 4, y0 + 11, b, CREAM, 255, GFX_LEFT);
                } else
                    gfx_rect(x, y0, 1, h, gfx_heat(i % 4 ? 70 : 140), 255, GFX_NORMAL);
            }
    }
}
#endif

/* a track's own page, the whole screen: its waveform big, the page's name, its four knobs as rings */
void tb_ui_track(const tb_view_t *v)
{
    const tb_lane_t *l = &v->lane[v->track];
    int i;
    gfx_begin();
    top_bar(v);
    title_row(l->name, v->strip_title, v->strip_pages, v->strip_page);
    well(2, 45, TB_W - 4, 155);
#if TB_HAS_PICS
    if (v->edit_kind && v->menu == TB_MENU_EDIT) {
        edit_picture(v, 10, 52, 220, v->hint ? 128 : 140);
    } else
#endif
    {
        uint8_t pk[TB_W], ht[TB_W];
        int ww = 224;
        unsigned mx = 0, g;
        for (i = 0; i < TB_WAVE_W; i++)
            mx = l->wave[i] > mx ? l->wave[i] : mx;
        g = mx > 42u ? 255u * 256u / mx : 6u * 256u;
        for (i = 0; i < ww; i++) {
            unsigned m = (unsigned)l->wave[i * TB_WAVE_W / ww] * g >> 8;
            pk[i] = (uint8_t)(m > 255u ? 255u : m);
            ht[i] = l->heat[i * TB_WAVE_W / ww];
        }
        if (l->beats)
            for (i = 1; i < l->beats; i++)
                gfx_rect(8 + i * ww / l->beats, 50, 1, 144, gfx_heat(i % 4 ? 18 : 40), 255, GFX_NORMAL);
        gfx_wave(8, 52, ww, 140, pk, ht, 120);
        if (l->muted) {
            gfx_rect(8, 52, ww, 140, TB_RGB(0, 0, 0), 175, GFX_NORMAL);
            gfx_text(&TB_FONT_M, 120, 126, "MUTE", gfx_heat(170), 255, GFX_CENTER);
        }
        if (v->playing && !l->muted)
            playhead(8 + l->pos * ww / TB_WAVE_W, 50, 144, 240);
    }
    if (v->hint)
        gfx_text(&TB_FONT_S, 120, 195, v->hint, gfx_heat(110), 255, GFX_CENTER);
    knobs(v);
    hud(v);
}

/* ------------------------------------------------------------- the sequencer --- */
/* the whole loop on the screen: up to 32 steps a row, as many rows as it needs (the fewer the steps, the wider) */
static int sq_y = 48, sq_h = 122;                  /* the grid (BREAK: under its waveform) */
#define SQ_Y sq_y
#define SQ_H sq_h
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
typedef struct { int cols, rows, x0, w, bh; } lay_t;

static lay_t layout(const tb_seqview_t *s)
{
    lay_t l;
    int len = s->len ? s->len : 16;
    l.cols = len < 32 ? len : 32;
    l.rows = (len + l.cols - 1) / l.cols;
    l.x0 = s->kind == TB_SEQ_GRID ? 28 : s->kind == TB_SEQ_NOTES ? 28 : 7;   /* (NOTES: the piano at the left) */
    l.w = 233 - l.x0;
    l.bh = (SQ_H - (l.rows - 1) * 5) / l.rows;
    return l;
}
/* step i's column: its x, width, the top of its row */
static void cell(const lay_t *l, int i, int *x, int *w, int *y)
{
    int c = i % l->cols, r = i / l->cols, xs = l->x0 + c * l->w / l->cols, xe = l->x0 + (c + 1) * l->w / l->cols;
    *x = xs, *w = xe - xs - (xe - xs >= 6 ? 1 : 0), *y = SQ_Y + r * (l->bh + 5);
    if (*w < 1)
        *w = 1;
}

static void seq_pads(const tb_seqview_t *s, const lay_t *l)
{
    int i;
    for (i = 0; i < s->len; i++) {
        const tb_vstep_t *st = &s->s[i];
        int x, w, y;
        cell(l, i, &x, &w, &y);
        gfx_rrect(x, y, w, l->bh, w > 6 ? 3 : 1, gfx_heat(i % 4 == 0 ? 26 : 16), 255, 0);
        if (st->hits) {
            int vel = st->vel ? st->vel : 100, h = l->bh / 6 + vel * (l->bh - l->bh / 6 - 2) / 127, r, nr = ((st->flags >> 3) & 3) + 1;
            gfx_rrect(x, y + l->bh - h, w, h, w > 6 ? 3 : 1, gfx_heat(150 + vel * 100 / 127), (st->flags & 7) ? 140 : 255, 0);
            for (r = 1; r < nr; r++)                    /* the repeats: notches */
                gfx_rect(x + 1, y + l->bh - h + r * h / nr, w - 2 > 0 ? w - 2 : 1, 2, TB_RGB(0, 0, 0), 200, GFX_NORMAL);
            if ((st->hits & 3u) != 1u && w >= 4) {      /* the 32nds: the half that plays lit, the other dark */
                int hw = w / 2;
                if ((st->hits & 3u) == 2u)
                    gfx_rect(x, y + l->bh - h, hw, h, gfx_heat(16), 255, GFX_NORMAL);
                else
                    gfx_rect(x + hw - 1, y + l->bh - h, 2, h, TB_RGB(0, 0, 0), 220, GFX_NORMAL);
            }
            if ((st->flags & 7) && w >= 8)              /* a chance: a ring at the foot */
                gfx_arc(x + w / 2, y + l->bh - 8, 3, 1, 0, 1024 - (st->flags & 7) * 128, CREAM, 255);
        }
        if (s->play == i)
            gfx_rect(x, y, w, l->bh, gfx_heat(150), 90, GFX_ADD);
    }
}

static void seq_grid(const tb_seqview_t *s, const lay_t *l)
{
    int i, r, row;
    int ph = l->bh / 11 < 2 ? 2 : l->bh / 11;
    for (row = 0; row < l->rows; row++) {
        int y0 = SQ_Y + row * (l->bh + 5);
        for (r = 0; r < s->npieces && r < 11; r++) {
            int y = y0 + r * ph, sel = r == s->piece;
            if (sel)
                gfx_rect(1, y, TB_W - 2, ph, gfx_heat(40), 255, GFX_NORMAL);
            if (ph >= 8 || sel)
                gfx_text(&TB_FONT_S, 4, y + (ph >= 9 ? ph - 1 : 9), s->piece_name ? s->piece_name[r] : "",
                         sel ? CREAM : GREY, 255, GFX_LEFT);
        }
    }
    if (s->len * 11 <= 180) {                          /* (a short loop: each cell drawn; the FM-1's frame holds 288 drawings) */
        for (i = 0; i < s->len; i++) {
            int x, w, y;
            cell(l, i, &x, &w, &y);
            for (r = 0; r < s->npieces && r < 11; r++) {
                int on = (s->s[i].hits >> r) & 1, sel = r == s->piece;
                uint32_t c = on ? gfx_heat(sel ? 245 : 185) : gfx_heat(i % 4 == 0 ? 26 : 16);
                gfx_rect(x, y + r * ph, w, ph - (ph > 3 ? 1 : 0), c, 255, GFX_NORMAL);
            }
            if (s->play == i)
                gfx_rect(x, y, w, 11 * ph, gfx_heat(120), 80, GFX_ADD);
        }
        return;
    }
    /* a long one: the rows' ground, the beats, the hits in runs, then the gaps cut in */
    for (row = 0; row < l->rows; row++) {
        int y0 = SQ_Y + row * (l->bh + 5), a = row * l->cols, e = a + l->cols < s->len ? a + l->cols : s->len, xa, xb, w, y;
        if (a >= s->len)
            break;
        cell(l, a, &xa, &w, &y), cell(l, e - 1, &xb, &w, &y);
        gfx_rect(xa, y0, xb + w - xa, 11 * ph, gfx_heat(16), 255, GFX_NORMAL);
        for (i = a; i < e; i++)
            if (i % 4 == 0) {
                int x, ww, yy;
                cell(l, i, &x, &ww, &yy);
                gfx_rect(x, y0, ww, 11 * ph, gfx_heat(26), 255, GFX_NORMAL);
            }
        for (r = 0; r < s->npieces && r < 11; r++)
            for (i = a; i < e;) {
                int j, x, ww, yy, x2;
                if (!((s->s[i].hits >> r) & 1)) {
                    i++;
                    continue;
                }
                for (j = i; j < e && ((s->s[j].hits >> r) & 1); j++)
                    ;
                cell(l, i, &x, &ww, &yy), cell(l, j - 1, &x2, &ww, &yy);
                gfx_rect(x, y0 + r * ph, x2 + ww - x, ph, gfx_heat(r == s->piece ? 245 : 185), 255, GFX_NORMAL);
                i = j;
            }
        if (ph > 3)
            for (r = 1; r <= 11; r++)
                gfx_rect(xa, y0 + r * ph - 1, xb + w - xa, 1, gfx_bg(), 255, GFX_NORMAL);
        for (i = a; i < e; i++) {                      /* (the 1 px gap after a cell 6 px wide or more, as cell() leaves it) */
            int c = i % l->cols, xs = l->x0 + c * l->w / l->cols, xe = l->x0 + (c + 1) * l->w / l->cols;
            if (xe - xs >= 6)
                gfx_rect(xe - 1, y0, 1, 11 * ph, gfx_bg(), 255, GFX_NORMAL);
        }
    }
    for (i = 0; i < s->len; i++)
        if (s->play == i) {
            int x, w, y;
            cell(l, i, &x, &w, &y);
            gfx_rect(x, y, w, 11 * ph, gfx_heat(120), 80, GFX_ADD);
        }
}

#define SN(st) ((int)(st)->note + s->transpose)        /* a note where it sounds */
static void seq_notes(const tb_seqview_t *s, const lay_t *l)
{
    int i, lo = 127, hi = 0, span, row, empty = 1, sw = l->w / l->cols, nn = 0, rich;
    for (i = 0; i < s->len; i++)                      /* the range: the notes as written and as transposed (they move in it) */
        if (s->s[i].hits) {
            int k;
            for (k = 0; k < 4; k++) {                   /* (a chord set by hand: its other notes too) */
                int a = k ? s->s[i].more[k - 1] : s->s[i].note, b = a + s->transpose;
                if (k && !a)
                    continue;
                lo = a < lo ? a : lo, lo = b < lo ? b : lo;
                hi = a > hi ? a : hi, hi = b > hi ? b : hi;
                nn++;
            }
            empty = 0;
        }
    if (lo > hi)
        lo = 54, hi = 66;
    span = hi - lo + 1;
    if (span < 13) {
        lo -= (13 - span) / 2;
        hi = lo + 12;
    }
    span = hi - lo + 1;
    /* (the frame holds 512 drawings: many notes over a wide range, the touches left out so that every note is drawn) */
    rich = nn * 2 + span * 3 * l->rows + s->len < 400;
    for (row = 0; row < l->rows; row++) {             /* the rows: the black keys darker, the octaves marked; the piano */
        int y0 = SQ_Y + row * (l->bh + 5), n, lit = -1000;
        if (s->held >= 0 && s->s[s->held].hits)
            lit = SN(&s->s[s->held]);
        else if (s->cursor >= 0 && s->s[s->cursor].hits)
            lit = (s->cur_k > 0 && s->s[s->cursor].more[s->cur_k - 1] ? s->s[s->cursor].more[s->cur_k - 1] : s->s[s->cursor].note) + s->transpose;
        for (n = lo; n <= hi; n++) {
            int y = y0 + (hi - n) * l->bh / span, h = y0 + (hi - n + 1) * l->bh / span - y;
            int black = (0x54A >> (((n % 12) + 12) % 12)) & 1;
            gfx_rect(l->x0, y, l->w, h, gfx_heat(black ? 5 : 13), 255, GFX_NORMAL);
            if (((n % 12) + 12) % 12 == 0)
                gfx_rect(l->x0, y + h - 1, l->w, 1, gfx_heat(36), 255, GFX_NORMAL);
            if (black) {                                /* the piano: a black key short and dark over the white ones */
                gfx_rect(5, y, 20, h, TB_RGB(214, 204, 232), 255, GFX_NORMAL);
                gfx_rect(5, y, 12, h, n == lit ? HOT : TB_RGB(30, 14, 44), 255, GFX_NORMAL);
            } else {
                int nb = (0x54A >> (((n + 1) % 12 + 12) % 12)) & 1;   /* (the line between two whites: E-F, B-C) */
                gfx_rect(5, y, 20, h, n == lit ? HOT : TB_RGB(214, 204, 232), 255, GFX_NORMAL);
                if (!nb && h > 3)
                    gfx_rect(5, y, 20, 1, TB_RGB(140, 126, 164), 255, GFX_NORMAL);
                if (((n % 12) + 12) % 12 == 0 && h >= 7 && l->rows == 1) {
                    char b[6];
                    snprintf(b, sizeof b, "C%d", n / 12 - 1);
                    gfx_text(&TB_FONT_S, 24, y + h - 1, b, TB_RGB(60, 40, 84), 255, GFX_RIGHT);
                }
            }
        }
        gfx_rect(5, y0, 20, 1, TB_RGB(140, 126, 164), 255, GFX_NORMAL);
    }
    for (i = 0; i < s->len; i++) {                    /* the steps, the beats, the bars */
        int x, w, y;
        if (i % 4 && sw < 7)
            continue;
        cell(l, i, &x, &w, &y);
        gfx_rect(x, y, 1, l->bh, gfx_heat(i % 16 == 0 ? 52 : i % 4 == 0 ? 30 : 17), 255, GFX_NORMAL);
    }
    if (s->cursor >= 0 && s->val) {                   /* step by step: where the next note goes, as long as it will be */
        int x, w, y, x2, w2, y2, e = s->cursor + (s->cur_w ? s->cur_w : 1) - 1;
        cell(l, s->cursor, &x, &w, &y);
        cell(l, e < s->len ? e : s->len - 1, &x2, &w2, &y2);
        if (y2 != y)
            x2 = l->x0 + l->w - w2;
        gfx_rect(x, y, x2 + w2 - x, l->bh, HOT, 30, GFX_ADD);
        gfx_rect(x, y, 2, l->bh, HOT, 255, GFX_NORMAL);
        gfx_rect(x - 2, y, 6, 2, HOT, 255, GFX_NORMAL);
        gfx_rect(x - 1, y + 2, 4, 1, HOT, 255, GFX_NORMAL);
    }
    if (s->play >= 0 && s->play < s->len) {           /* the playhead */
        int x, w, y;
        cell(l, s->play, &x, &w, &y);
        gfx_rect(x, y, w, l->bh, gfx_heat(120), 40, GFX_ADD);
        gfx_rect(x, y, 1, l->bh, CREAM, 200, GFX_NORMAL);
    }
    for (i = 0; i < s->len * 4; i++) {               /* (each step's note, then a chord's others) */
        const tb_vstep_t *st = &s->s[i % s->len];
        int k = i / s->len, pitch = k ? st->more[k - 1] : st->note, sn = pitch + s->transpose;
        int x, w, y, g, vel, len, rowend, nh, ny;
        uint32_t c;
        if (!st->hits || (k && !pitch) || sn < lo || sn > hi)
            continue;
        cell(l, i % s->len, &x, &w, &y);
        {                                               /* (as played: a little early or late) */
            int m = (int)((st->hits >> 11) & 31u) - ((st->hits & 0x8000u) ? 32 : 0);
            x += m * sw / 32;
            if ((st->hits & 3u) == 2u)                  /* (a 32nd later) */
                x += sw / 2;
        }
        g = st->gate ? st->gate : 6;
        len = g * sw / 8;
        if (len < 3)
            len = 3;
        rowend = l->x0 + l->w;
        vel = st->vel ? st->vel : 100;
        ny = y + (hi - sn) * l->bh / span;
        nh = l->bh / span < 4 ? 4 : l->bh / span;
        if (ny + nh > y + l->bh)
            ny = y + l->bh - nh;
        if (s->transpose && rich)                       /* (where it was written: a ghost) */
            gfx_rrect(x + 1, y + (hi - pitch) * l->bh / span, len - 1, nh, 2, gfx_heat(70), 200, 1);
        if ((st->flags & 0x80) && !k) {                 /* ACID's slide: the note joined to the next one */
            int j;
            for (j = i % s->len + 1; j < s->len && !s->s[j].hits; j++)
                ;
            if (j < s->len && SN(&s->s[j]) >= lo && SN(&s->s[j]) <= hi) {
                int x2, w2, y2, ny2;
                cell(l, j, &x2, &w2, &y2);
                ny2 = y2 + (hi - SN(&s->s[j])) * l->bh / span;
                if (y2 == y)
                    gfx_line(x + len - 1, ny + nh / 2, x2 + 1, ny2 + nh / 2, 24, HOT, 255, GFX_NORMAL);
            }
            len = sw + 1;
        }
        if (x + 1 + len > rowend)
            len = rowend - x - 1;
        c = (st->flags & 0x40) ? HOT : gfx_mix(gfx_heat(150), gfx_heat(205), (unsigned)vel * 256u / 127u);
        gfx_rrect(x + 1, ny, len - 1, nh, nh > 5 ? 2 : 1, c, (st->flags & 7) ? 140 : 255, 0);
        if (rich && nh >= 5 && len > 4)                 /* (a light along its top) */
            gfx_rect(x + 2, ny, len - 3, 1, gfx_mix(c, TB_RGB(255, 255, 255), 110), 255, GFX_NORMAL);
        if ((st->flags & 0x40) && l->bh >= 40)          /* ACID's accent: a mark over the note */
            gfx_rect(x + 1, ny - 3, len - 1, 2, CREAM, 255, GFX_NORMAL);
        if (rich && (st->hits & 3u) == 3u && len > 4)   /* (the step and its half: a notch) */
            gfx_rect(x + 1 + sw / 2, ny, 1, nh, TB_RGB(0, 0, 0), 220, GFX_NORMAL);
        if (st->lock && len > 6)                        /* (its locks: a dot in it) */
            gfx_rrect(x + 3, ny + nh / 2 - 1, 3, 3, 1, TB_RGB(255, 255, 255), 255, 0);
        if (s->held == i % s->len || (s->cursor == i % s->len && (s->cur_k < 0 || s->cur_k == k)))
            gfx_rrect(x, ny - 2, len + 1, nh + 4, 3, CREAM, 255, 1);
    }
    if (empty && s->val) {                              /* (nothing yet: how to start) */
        int cx = l->x0 + l->w / 2 + 10, cy = SQ_Y + SQ_H / 2;
        panel(cx - 82, cy - 21, 164, 40, 8, PANEL);
        gfx_text(&TB_FONT_S, cx, cy - 4, "a key: a note", CREAM, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, cx, cy + 11, "the cursor moves on \xc2\xb7 ARP: rest", GREY, 255, GFX_CENTER);
    }
    {                                                   /* the range: its top and bottom notes */
        char b[16];
        snprintf(b, sizeof b, "%s%d", NOTE_NAME[((hi % 12) + 12) % 12], hi / 12 - 1);
        gfx_text(&TB_FONT_S, l->x0 + l->w - 3, SQ_Y + 10, b, gfx_heat(80), 255, GFX_RIGHT);
        snprintf(b, sizeof b, "%s%d", NOTE_NAME[((lo % 12) + 12) % 12], lo / 12 - 1);
        gfx_text(&TB_FONT_S, l->x0 + l->w - 3, SQ_Y + SQ_H - 3, b, gfx_heat(80), 255, GFX_RIGHT);
    }
}

/* BREAK: the steps as the break's own pieces. Each step that plays a slice shows that slice's sound, its number on
 * it: played in order, the steps look like the break above; cut up, you see the pieces moved */
static int slice_end(int k, int n) { return k + 1 < n ? k + 1 : n; }
/* how many steps the slice started at step i lasts: its length, up to the next one (and the row's end) */
static int slice_len(const tb_seqview_t *s, int i, int last)
{
    int j, n;
    for (j = i + 1; j < s->len && j < last && s->sl[j] < 0; j++)
        ;
    n = ((s->s[i].gate ? s->s[i].gate : 8) + 7) / 8;
    n = n < 1 ? 1 : n;
    return n > j - i ? j - i : n;
}
static void seq_slices(const tb_seqview_t *s, const lay_t *l)
{
    int row, i, sw = l->w / l->cols;
    for (i = 0; i < s->len; i += 4) {                 /* the beats */
        int x, w, y;
        cell(l, i, &x, &w, &y);
        gfx_rect(x, y, 1, l->bh, gfx_heat(i % 16 == 0 ? 44 : 22), 255, GFX_NORMAL);
    }
    for (row = 0; row < l->rows; row++) {
        uint8_t pk[TB_W], ht[TB_W];
        int x0 = l->x0, y0 = SQ_Y + row * (l->bh + 5), first = row * l->cols, last = first + l->cols, nb = 0;
        int crowded = 0;
        memset(pk, 0, sizeof pk), memset(ht, 128, sizeof ht);
        for (i = first; i < last && i < s->len; i++)  /* (two-digit numbers too wide for their pieces: every other one) */
            if (s->sl[i] >= 9 && slice_len(s, i, last) * sw < 17)
                crowded = 1;
        for (i = first; i < last && i < s->len; i++) {
            int k = s->sl[i], n, xa, xb, x, w, y, c, lit;
            if (k < 0 || !s->nslices)
                continue;
            n = slice_len(s, i, last);
            cell(l, i, &x, &w, &y);
            xa = x, xb = x + n * sw;
            lit = s->play >= i && s->play < i + n;
            if (xb > x0 + l->w)
                xb = x0 + l->w;
            {
                int ca = s->brk_cut[k], cb = slice_end(k, s->nslices) < s->nslices ? s->brk_cut[k + 1] : 200;
                for (c = xa + 1; TB_HAS_WAVE && c < xb - 1; c++) {     /* (its sound, from its start: a gap at each end) */
                    int q = ca + (c - xa) * (cb - ca) / (xb - xa);
                    pk[c - x0] = s->brk_wave[q < 200 ? q : 199];
                    ht[c - x0] = (uint8_t)(lit ? 0 : 128);
                }
            }
            if (lit)
                gfx_rrect(xa, y0, xb - xa, l->bh, 3, HOT, 40, 0);
            {
                char b[4];
                snprintf(b, sizeof b, "%d", k + 1);
                int narrow = crowded && xb - xa < 17;
                if (!narrow || nb % 2 == 0 || lit)
                    gfx_text(&TB_FONT_S, (xa + xb) / 2, y0 + 9, b, lit ? HOT : CREAM, 255, GFX_CENTER);
                nb += narrow;
            }
        }
        gfx_wave(x0, y0 + 9, l->w, l->bh - 9, pk, ht, 40);
    }
    if (s->val) {                                     /* (nothing yet: how to start) */
        for (i = 0; i < s->len && s->sl[i] < 0; i++)
            ;
        if (i == s->len) {
            int cx = l->x0 + l->w / 2, cy = SQ_Y + SQ_H / 2;
            panel(cx - 82, cy - 21, 164, 40, 8, PANEL);
            gfx_text(&TB_FONT_S, cx, cy - 4, "a key: a piece of the break", CREAM, 255, GFX_CENTER);
            gfx_text(&TB_FONT_S, cx, cy + 11, "the cursor moves on \xc2\xb7 ARP: rest", GREY, 255, GFX_CENTER);
        }
    }
}

/* BREAK, over the steps: the whole break, cut in its slices (numbered); the one playing in orange, the one edited
 * framed */
static void brk_head(const tb_seqview_t *s)
{
    int i, y0 = 49, h = 40, x0 = 7, w = 226, every = s->nslices > 16 ? 4 : s->nslices > 8 ? 2 : 1;
    int playing = -1, p;
    int edit = s->held >= 0 ? s->sl[s->held] : s->cursor >= 0 ? s->sl[s->cursor] : -1;
    uint8_t pk[TB_W], ht[TB_W];
    for (p = s->play; p >= 0 && p < s->len; p--)        /* (the slice sounding: the last one started) */
        if (s->sl[p] >= 0) {
            int g = ((s->s[p].gate ? s->s[p].gate : 8) + 7) / 8;
            playing = s->play - p < (g < 1 ? 1 : g) ? s->sl[p] : -1;
            break;
        }
    int col = 0;
    well(2, y0 - 4, TB_W - 4, h + 8);
    for (i = 0; i < w; i++) {
        int j = i * 200 / w;
        while (col + 1 < s->nslices && j >= s->brk_cut[col + 1])
            col++;
        pk[i] = s->brk_wave[j], ht[i] = (uint8_t)(s->nslices && col == playing ? 0 : 128);
    }
    if (TB_HAS_WAVE)
        gfx_wave(x0, y0 + 10, w, h - 10, pk, ht, 40);
    for (i = 0; i < s->nslices; i++) {
        int xa = x0 + s->brk_cut[i] * w / 200, xb = x0 + (i + 1 < s->nslices ? s->brk_cut[i + 1] : 200) * w / 200;
        if (i)
            gfx_rect(xa, y0 + 10, 1, h - 10, WELL, 255, GFX_NORMAL);
        if (i == edit)
            gfx_rrect(xa, y0 + 9, xb - xa + 1, h - 8, 2, CREAM, 255, 1);
        if (i % every == 0 || i == edit || i == playing) {
            char b[4];
            snprintf(b, sizeof b, "%d", i + 1);
            gfx_text(&TB_FONT_S, xa + 2, y0 + 8, b, i == playing ? HOT : i == edit ? CREAM : gfx_heat(110), 255, GFX_LEFT);
        }
    }
}

/* the screen shows a window of the loop (32 steps of a melody, the whites' cells of a kick or a 909): a copy of the
 * view cut to it, its steps numbered from win (seq_off) */
static int seq_off;
static const tb_seqview_t *seq_window(const tb_seqview_t *s0)
{
    static tb_seqview_t w;
    int a = s0->win, n = s0->wlen, i;
    seq_off = 0;
    if (!n || (a == 0 && n >= s0->len))
        return s0;
    if (a + n > s0->len)
        n = s0->len - a;
    if (n < 1)
        return s0;
    w = *s0;
    for (i = 0; i < n; i++)
        w.s[i] = s0->s[a + i], w.sl[i] = s0->sl[a + i];
    w.len = (uint8_t)n, w.page = 0, w.npages = 1;
    w.play = s0->play >= a && s0->play < a + n ? (int16_t)(s0->play - a) : -1;
    w.held = s0->held >= a && s0->held < a + n ? (int16_t)(s0->held - a) : -1;
    w.cursor = s0->cursor >= a && s0->cursor < a + n ? (int16_t)(s0->cursor - a) : -1;
    seq_off = a;
    return &w;
}

void tb_ui_seq(const tb_view_t *v)
{
    const tb_seqview_t *s0 = &v->seq, *s = seq_window(s0);
    const tb_lane_t *ln = &v->lane[v->track];
    lay_t l;
    int i, x, w, y, x2, w2, y2, a, b_, right;
    char b[32];
    if (s->kind == TB_SEQ_SLICES)
        sq_y = 103, sq_h = 76;
    else
        sq_y = 51, sq_h = 125;
    l = layout(s);
    gfx_begin();
    top_bar(v);
    a = s->page * 16 + 1, b_ = s->page * 16 + 16 > s->len ? s->len : s->page * 16 + 16;
    if (s != s0)                                        /* (a window: all of it is the whites' or the screen's) */
        a = 1, b_ = s->len;
    /* the title: the track, the page; on the right the note's value (step by step) or the keys' steps */
    if (s->held >= 0 && !s->val) {
        snprintf(b, sizeof b, "STEP %d", s->held + 1);
        right = title_row(ln->name, v->strip_page ? v->strip_title : b, v->strip_pages, v->strip_page);
        if (v->strip_page)
            chip(right - 4, TITLE_Y - 1, b, CREAM, 0, 1);
    } else {
        right = title_row(ln->name, v->strip_title, v->strip_pages, v->strip_page);
        if (s->val) {
            right -= 4 + chip(right - 4, TITLE_Y - 1, s->val, HOT, 1, 1);
            if (s->transpose) {
                snprintf(b, sizeof b, "%+d", s->transpose);
                chip(right - 4, TITLE_Y - 1, b, ACCENT, 1, 1);
            }
        } else {
            if (v->notes_edit)                          /* (EDIT: how to delete, said where it is seen) */
                chip(right - 4, TITLE_Y - 1, "OCT- = DELETE", HOT, 1, 1);
            else {
                if (s != s0)                            /* (the window: its steps in the loop, the grid) */
                    snprintf(b, sizeof b, "%d-%d / %d", seq_off + 1, seq_off + s->len, s0->len);
                else
                    snprintf(b, sizeof b, s->npages > 1 ? "%d-%d" : "%d STEPS", s->npages > 1 ? a : s->len, b_);
                if (s0->grid)
                    right -= 4 + chip(right - 4, TITLE_Y - 1, s0->grid, HOT, 1, 1);
                gfx_text(&TB_FONT_S, right - 4, TITLE_Y - 1, b, GREY, 255, GFX_RIGHT);
            }
        }
    }
    if (s->kind == TB_SEQ_SLICES)
        brk_head(s);
    well(2, sq_y - 5, TB_W - 4, sq_h + 10);
    switch (s->kind) {
    case TB_SEQ_GRID: seq_grid(s, &l); break;
    case TB_SEQ_NOTES: seq_notes(s, &l); break;
    case TB_SEQ_SLICES: seq_slices(s, &l); break;
    default: seq_pads(s, &l);
    }
    if (s->kind != TB_SEQ_NOTES)
        for (i = 0; i < s->len; i++)                    /* the steps with locks: a dot at their top */
            if (s->s[i].lock) {
                int xx, ww, yy;
                cell(&l, i, &xx, &ww, &yy);
                gfx_rrect(xx + ww / 2 - 2, yy + 2, 4, 4, 2, TB_RGB(255, 255, 255), 255, 0);
            }
    if (!s->val) {                                      /* the 16 steps the white keys play: framed */
        cell(&l, s->page * 16, &x, &w, &y);
        cell(&l, b_ - 1, &x2, &w2, &y2);
        gfx_rrect(x - 2, y - 2, x2 + w2 - x + 4, l.bh + 4, 4, ACCENT, 140, 1);
    }
    if (s->held >= 0 && s->kind != TB_SEQ_NOTES) {     /* the held step: a frame */
        cell(&l, s->held, &x, &w, &y);
        gfx_rrect(x - 1, y - 1, w + 2, l.bh + 2, 3, CREAM, 255, 1);
    }
    if (s->cursor >= 0 && !(s->kind == TB_SEQ_NOTES && s->val)) {   /* the cursor: its column lit, a mark under it */
        cell(&l, s->cursor, &x, &w, &y);
        int cw = s->cur_w ? s->cur_w : 1;            /* (step by step: the next note's length; NOTES: the note's) */
        if (s->cursor + cw > s->len)
            cw = s->len - s->cursor;
        if (!s->val && !v->notes_edit)
            cw = 1;
        if (l.cols > 0 && s->cursor % l.cols + cw > l.cols)   /* (to the end of its row) */
            cw = l.cols - s->cursor % l.cols;
        gfx_rect(x, y, w * cw, l.bh, HOT, 34, GFX_ADD);
        if (s->kind != TB_SEQ_NOTES)
            gfx_rrect(x - 1, y - 1, w + 2, l.bh + 2, 3, HOT, 255, 1);
    }
    /* the ruler (one row): the steps' numbers at the beats, the cursor's marked */
    if (l.rows == 1)
        for (i = 0; i < s->len; i++) {
            int every = l.w / l.cols >= 5 ? 4 : 8;
            cell(&l, i, &x, &w, &y);
            if (i == s->cursor) {
                snprintf(b, sizeof b, "%d", i + 1 + seq_off);
                gfx_rrect(x + w / 2 - 9, sq_y + sq_h + 7, 18, 12, 6, HOT, 255, 0);
                gfx_text(&TB_FONT_S, x + w / 2, sq_y + sq_h + 17, b, TB_RGB(20, 4, 30), 255, GFX_CENTER);
            } else if ((i + seq_off) % every == 0 && (s->cursor < 0 || i < s->cursor - 1 || i > s->cursor + 2)) {
                snprintf(b, sizeof b, "%d", i + 1 + seq_off);
                gfx_text(&TB_FONT_S, x + w / 2, sq_y + sq_h + 17, b, i == s->play ? CREAM : gfx_heat(90), 255, GFX_CENTER);
            }
        }
    knobs(v);
    hud(v);
}

/* ------------------------------------------------------------------ the menus --- */
static void menu_frame(const tb_view_t *v)
{
    const char *sub = 0;
    gfx_begin();
    top_bar(v);
    if (!(v->menu == TB_MENU_GLO || v->menu == TB_MENU_SYS))
        sub = v->lane[v->track].name;                  /* (a track's page: which) */
    title_row(v->menu_title, sub, v->menu_pages, v->menu_page);
    if (v->menu != TB_MENU_TOOLS)
        well(2, 45, TB_W - 4, 155);
}

/* the envelope: its curve, one point a pixel (the times on a scale that shows the short ones); no attack: it starts
 * at the top */
static void menu_env(const tb_view_t *v)
{
    const tb_envview_t *e = &v->env;
    const int x0 = 13, y0 = 62, y1 = 174, h = y1 - y0;
    int wa = e->a * 50 / 127, wd = 2 + e->d * 52 / 127, ws = 40, wr = 2 + e->r * 52 / 127;
    int tot = wa + wd + ws + wr, room = TB_W - 2 * x0 - 2;
    if (tot * 2 < room)                                 /* (a short envelope drawn wider, at most twice) */
        room = tot * 2;
    wa = wa * room / tot, wd = wd * room / tot, ws = ws * room / tot, wr = wr * room / tot;
    int xa = x0, xd = xa + wa, xs = xd + wd, xr = xs + ws, xe = xr + wr, x, k, px = 0, py = 0;
    int ys = y1 - e->s * h / 127, c[4];
    float sl = e->s / 127.0f;
    /* the guides: the floor, the sustain's level, the segments' borders and their letters */
    gfx_rect(x0, y1, TB_W - 2 * x0, 1, gfx_heat(56), 255, GFX_NORMAL);
    for (x = x0; x < TB_W - x0; x += 6)
        gfx_rect(x, ys, 3, 1, gfx_heat(46), 255, GFX_NORMAL);
    c[0] = (xa + xd) / 2, c[1] = (xd + xs) / 2, c[2] = (xs + xr) / 2, c[3] = (xr + xe) / 2;
    for (k = 1; k < 4; k++)                             /* (the letters never on each other) */
        c[k] = c[k] < c[k - 1] + 12 ? c[k - 1] + 12 : c[k];
    for (k = 0; k < 4; k++) {
        static const char *const N[4] = {"A", "D", "S", "R"};
        int xb = k == 0 ? xd : k == 1 ? xs : k == 2 ? xr : xe;
        if (xb > x0)
            gfx_rect(xb, y0 - 6, 1, h + 6, gfx_heat(22), 255, GFX_NORMAL);
        gfx_text(&TB_FONT_S, c[k], y1 + 14, N[k], v->knob_name[k] && v->hud == k && v->hud_alpha ? HOT : GREY, 255, GFX_CENTER);
    }
    /* the curve: filled under, a light gradient; the line over it (one command each: the FM-1's RAM) */
    uint8_t tops[TB_W];
    int16_t pts[2 * (TB_W / 2 + 8)];
    unsigned np = 0;
    for (x = xa; x <= xe; x++) {
        float lv, u;
        int y;
        if (x < xd)
            u = (float)(x - xa) / (float)wa, lv = 1.0f - (1.0f - u) * (1.0f - u);
        else if (x < xs)
            u = (float)(x - xd) / (float)wd, lv = sl + (1.0f - sl) * (1.0f - u) * (1.0f - u) * (1.0f - u);
        else if (x < xr)
            lv = sl;
        else
            u = (float)(x - xr) / (float)wr, lv = sl * (1.0f - u) * (1.0f - u) * (1.0f - u);
        if (x == xe)
            lv = 0;
        y = y1 - (int)(lv * (float)h + 0.5f);
        tops[x - xa] = (uint8_t)(y - y0);
        if ((x == xa || (x - xa) % 2 == 0 || x == xd || x == xs || x == xr || x == xe) && np < sizeof pts / sizeof *pts / 2u)
            pts[2 * np] = (int16_t)x, pts[2 * np + 1] = (int16_t)y, np++;
    }
    gfx_area(xa, y0, xe - xa + 1, h, tops, gfx_heat(150), WELL, 110);
    gfx_poly(pts, np, 36, HOT, 255);
    (void)px, (void)py;
    /* its corners: the peak, the sustain held, the end */
    gfx_rrect(xd - 2, y0 - 2, 5, 5, 2, CREAM, 255, 0);
    gfx_rrect(xs - 2, ys - 2, 5, 5, 2, CREAM, 255, 0);
    gfx_rrect(xr - 2, ys - 2, 5, 5, 2, CREAM, 255, 0);
    gfx_rrect(xe - 2, y1 - 2, 5, 5, 2, CREAM, 255, 0);
}

static int32_t lfo_shape(unsigned wave, uint32_t ph)
{
    switch (wave) {
    case 1: { int32_t s = (int32_t)(ph >> 15); return s < 65536 ? s - 32768 : 98303 - s; }
    case 2: return ph < 0x80000000u ? 32767 : -32767;
    case 3: return (int32_t)(ph >> 16) - 32768;
    case 4: return 32767 - (int32_t)(ph >> 16);
    case 5: { static const int16_t R[8] = {12000, -26000, 30000, -8000, 22000, -30000, 4000, -18000}; return R[ph >> 29]; }
    default: {
        /* (the sine from the triangle, shaped: close enough to draw) */
        int32_t t = (int32_t)((ph + 0x40000000u) >> 15);
        int32_t tri = t < 65536 ? t - 32768 : 98303 - t;
        return (int32_t)((int64_t)tri * (98304 - (tri < 0 ? -tri : tri)) / 65536);
    }
    }
}

static void menu_lfo(const tb_view_t *v)
{
    const tb_lfoview_t *l = &v->lfo;
    int x0 = 12, w = 216, cy = 120, amp = 8 + l->depth * 40 / 127, i, px = 0, py = 0;
    char b[32];
    gfx_rect(x0, cy, w, 1, gfx_heat(40), 255, GFX_NORMAL);
    gfx_rect(x0 + w / 2, cy - 50, 1, 100, gfx_heat(26), 255, GFX_NORMAL);
    for (i = 0; i <= 96; i++) {                         /* two cycles */
        uint32_t ph = (uint32_t)((uint64_t)(i % 48) * 0x100000000ull / 48u);
        int x = x0 + i * w / 96, y = cy - (int)(lfo_shape(l->wave, ph) * amp / 32767);
        if (i % 48 == 0 && i && l->wave == 2)
            gfx_line(px, py, px, cy + amp, 30, gfx_heat(220), 255, GFX_NORMAL), py = cy + amp;
        if (i)
            gfx_line(px, py, x, y, 30, gfx_heat(220), 255, GFX_NORMAL);
        px = x, py = y;
    }
    {                                                   /* where it is now */
        int x = x0 + (int)((uint32_t)l->phase * (uint32_t)(w / 2) / 65536u), y = cy - (int)(l->value * amp / 32767);
        gfx_rect(x, cy - 50, 1, 100, gfx_heat(150), 120, GFX_ADD);
        gfx_glow(x, y, 11, gfx_heat(220), 200);
        gfx_rrect(x - 3, y - 3, 7, 7, 3, CREAM, 255, 0);
    }
    snprintf(b, sizeof b, "TO %s", l->dest ? l->dest : "");
    gfx_text(&TB_FONT_M, x0, 62, b, CREAM, 255, GFX_LEFT);
    gfx_text(&TB_FONT_S, TB_W - 12, 62, l->sync ? "SYNC" : "FREE", GREY, 255, GFX_RIGHT);
    if (!l->depth)
        gfx_text(&TB_FONT_S, 120, cy + 62, "DEPTH 0: the LFO does nothing", DIM, 255, GFX_CENTER);
}

static void note_name(char *b, size_t n, int note) { snprintf(b, n, "%s%d", NOTE_NAME[note % 12], note / 12 - 1); }

static void menu_arp(const tb_view_t *v)
{
    const tb_arpview_t *a = &v->arp;
    int i, x = 12, lo = 127, hi = 0;
    char b[8];
    /* its state */
    gfx_rrect(TB_W - 58, 50, 52, 18, 6, a->on ? gfx_heat(200) : gfx_heat(40), 255, 0);
    gfx_text(&TB_FONT_S, TB_W - 32, 63, a->on ? "ARP ON" : "ARP OFF", a->on ? TB_RGB(0, 0, 0) : GREY, 255, GFX_CENTER);
    if (a->latch)
        gfx_text(&TB_FONT_S, TB_W - 64, 63, "LATCH", gfx_heat(220), 255, GFX_RIGHT);
    /* the notes it holds */
    if (!a->nheld)
        gfx_text(&TB_FONT_S, 12, 63, a->on ? "play notes: it arpeggiates them" : "ARP held: on / off", GREY, 255, GFX_LEFT);
    for (i = 0; i < a->nheld && i < 6; i++) {
        note_name(b, sizeof b, a->held[i]);
        gfx_rrect(x, 50, 26, 18, 6, gfx_heat(60), 255, 0);
        gfx_text(&TB_FONT_S, x + 13, 63, b, CREAM, 255, GFX_CENTER);
        x += 29;
    }
    /* the order it plays them in: dots by pitch, the one now lit */
    for (i = 0; i < a->nseq; i++) {
        if (a->seq[i] < lo)
            lo = a->seq[i];
        if (a->seq[i] > hi)
            hi = a->seq[i];
    }
    if (a->nseq) {
        int px = 0, py = 0, span = hi - lo ? hi - lo : 1;
        for (i = 0; i < a->nseq; i++) {
            int xx = 20 + i * 200 / (a->nseq > 1 ? a->nseq - 1 : 1), yy = 150 - (a->seq[i] - lo) * 64 / span;
            if (a->nseq == 1)
                xx = 120;
            if (i)
                gfx_line(px, py, xx, yy, 18, gfx_heat(70), 255, GFX_NORMAL);
            px = xx, py = yy;
        }
        for (i = 0; i < a->nseq; i++) {
            int xx = a->nseq == 1 ? 120 : 20 + i * 200 / (a->nseq - 1), yy = 150 - (a->seq[i] - lo) * 64 / span;
            int now = i == a->pos && a->sounding;
            if (now)
                gfx_glow(xx, yy, 13, gfx_heat(220), 220);
            gfx_rrect(xx - 4, yy - 4, 9, 9, 4, now ? CREAM : gfx_heat(150), 255, 0);
        }
    }
    /* its rhythm: 8 steps */
    for (i = 0; i < 8; i++) {
        int on = (a->rhythm >> i) & 1, xx = 14 + i * 27;
        gfx_rrect(xx, 168, 23, 12, 3, on ? gfx_heat(a->rstep == i && a->sounding ? 250 : 150) : gfx_heat(26), 255, 0);
        if (a->rstep == i && a->nheld)
            gfx_rrect(xx - 1, 167, 25, 14, 4, CREAM, 255, 1);
    }
}

static void menu_scale(const tb_view_t *v)
{
    const tb_scaleview_t *sc = &v->scale;
    static const int8_t WH[7] = {0, 2, 4, 5, 7, 9, 11}, BL[5] = {1, 3, 6, 8, 10}, BLX[5] = {1, 2, 4, 5, 6};
    int o, i, k, kw = 15, x0 = 15, y0 = 54, wh = 78, bh = 48;
    char b[48];
    for (o = 0; o < 2; o++)                             /* two octaves from C3: white keys, then black */
        for (i = 0; i < 7; i++) {
            int n = 48 + o * 12 + WH[i], deg = (n - sc->root + 120) % 12, in = (sc->mask >> deg) & 1, x = x0 + (o * 7 + i) * kw;
            uint32_t c = deg == 0 ? gfx_heat(245) : in ? gfx_heat(165) : gfx_heat(40);
            gfx_rrect(x, y0, kw - 1, wh, 3, c, 255, 0);
            for (k = 0; k < sc->nchord; k++)
                if (sc->chord[k] == n)
                    gfx_rrect(x + kw / 2 - 4, y0 + wh - 13, 8, 8, 4, TB_RGB(0, 0, 0), 255, 0);
            if (deg == 0)
                gfx_text(&TB_FONT_S, x + kw / 2, y0 + wh - 18, "R", TB_RGB(0, 0, 0), 255, GFX_CENTER);
        }
    for (o = 0; o < 2; o++)
        for (i = 0; i < 5; i++) {
            int n = 48 + o * 12 + BL[i], deg = (n - sc->root + 120) % 12, in = (sc->mask >> deg) & 1;
            int x = x0 + (o * 7 + BLX[i]) * kw - 5;
            uint32_t c = deg == 0 ? gfx_heat(250) : in ? gfx_heat(120) : TB_RGB(6, 5, 12);
            gfx_rrect(x, y0, 10, bh, 2, c, 255, 0);
            gfx_rrect(x, y0, 10, bh, 2, gfx_heat(20), 255, 1);
            for (k = 0; k < sc->nchord; k++)
                if (sc->chord[k] == n)
                    gfx_rrect(x + 1, y0 + bh - 10, 8, 8, 4, CREAM, 255, 0);
        }
    {
        int len = 0;
        len += snprintf(b + len, sizeof b - (size_t)len, "CHORD ON %s:", NOTE_NAME[sc->root % 12]);
        for (k = 0; k < sc->nchord && len < 40; k++)
            len += snprintf(b + len, sizeof b - (size_t)len, " %s", NOTE_NAME[sc->chord[k] % 12]);
        gfx_text(&TB_FONT_S, 12, y0 + wh + 34, sc->nchord > 1 ? b : "CHORD: OFF (one note)", GREY, 255, GFX_LEFT);
        gfx_text(&TB_FONT_M, 12, y0 + wh + 18, v->scale_name ? v->scale_name : "", CREAM, 255, GFX_LEFT);
    }
}

/* FX, EDIT: 16 tiles, one a white key, in four rows of four */
static void menu_tiles(const tb_view_t *v)
{
    int i;
    for (i = 0; i < 16; i++) {
        int x = 6 + (i % 4) * 58, y = 48 + (i / 4) * 36, lit = (v->tile_lit >> i) & 1;
        const char *t = v->tile[i] ? v->tile[i] : "";
        const char *nl = strchr(t, '\n');
        char n[4], l1[16];
        gfx_rrect(x, y, 54, 33, 6, lit ? gfx_heat(215) : gfx_heat(26), 255, 0);
        if (!lit)
            gfx_rrect(x, y, 54, 33, 6, gfx_heat(70), 200, 1);
        snprintf(n, sizeof n, "%d", i + 1);
        gfx_text(&TB_FONT_S, x + 4, y + 10, n, lit ? TB_RGB(0, 0, 0) : DIM, 255, GFX_LEFT);
        if (nl) {
            snprintf(l1, sizeof l1, "%.*s", (int)(nl - t), t);
            gfx_text(&TB_FONT_S, x + 27, y + 18, l1, lit ? TB_RGB(0, 0, 0) : CREAM, 255, GFX_CENTER);
            gfx_text(&TB_FONT_S, x + 27, y + 29, nl + 1, lit ? TB_RGB(0, 0, 0) : gfx_heat(200), 255, GFX_CENTER);
        } else
            gfx_text(&TB_FONT_S, x + 27, y + 23, t, lit ? TB_RGB(0, 0, 0) : CREAM, 255, GFX_CENTER);
    }
}

/* FX: what the effect does, drawn: a drive's curve, a modulation's wave, a filter's response, a reverb's tail, a
 * delay's repeats, the EQ's curve */
static void curve(int x0, int y0, int w, int h, const int16_t *y, int n, uint32_t c)
{
    int i;
    for (i = 1; i < n; i++)
        gfx_line(x0 + (i - 1) * w / (n - 1), y0 + h / 2 - y[i - 1] * (h / 2) / 32767, x0 + i * w / (n - 1),
                 y0 + h / 2 - y[i] * (h / 2) / 32767, 30, c, 255, GFX_NORMAL);
}

/* an EQ's curve (the track's: 0 cuts, 127 a little up; the master's: +-) */
static void eq_curve(int x0, int y0, int w, int h, const uint8_t *p, int track)
{
    int16_t y[64];
    int i;
    for (i = 0; i < 64; i++) {
        float fx = i / 63.0f, lo = fx < 0.3f ? 1 : fx < 0.45f ? (0.45f - fx) / 0.15f : 0;
        float hi = fx > 0.7f ? 1 : fx > 0.55f ? (fx - 0.55f) / 0.15f : 0, md = 1 - lo - hi, g;
        if (track)
            g = lo * (p[0] < 64 ? (p[0] - 64) * 1.6f : (p[0] - 64) * 0.5f) + md * (p[1] < 64 ? (p[1] - 64) * 1.6f : (p[1] - 64) * 0.5f) +
                hi * (p[2] < 64 ? (p[2] - 64) * 1.6f : (p[2] - 64) * 0.5f);
        else
            g = lo * (p[0] - 64) + md * (p[1] - 64) + hi * (p[2] - 64);
        y[i] = (int16_t)(tb_clampf(g, -70, 70) * 32767.0f / 70.0f);
    }
    gfx_rect(x0, y0 + h / 2, w, 1, gfx_heat(40), 255, GFX_NORMAL);
    curve(x0, y0 + 4, w, h - 8, y, 64, HOT);
    gfx_text(&TB_FONT_S, x0 + 2, y0 + h - 4, "LOW", gfx_heat(90), 255, GFX_LEFT);
    gfx_text(&TB_FONT_S, x0 + w / 2, y0 + h - 4, "MID", gfx_heat(90), 255, GFX_CENTER);
    gfx_text(&TB_FONT_S, x0 + w - 2, y0 + h - 4, "HIGH", gfx_heat(90), 255, GFX_RIGHT);
}

/* FX: this track's chain, a page an effect: its name big, what it does drawn, how much */
static void menu_fx(const tb_view_t *v)
{
    const tb_fxview_t *f = &v->fx;
    int x0 = 14, y0 = 98, w = 212, h = 78, i, pg = f->page;
    int16_t y[64];
    char b[24];
    static const char *const STEP[10] = {"DRV", "CHO", "FLG", "PHS", "TRM", "EXT", "EQ", "REV", "DLY", "RTE"};
    /* the chain: its pages, this one lit, the ones playing marked */
    static const uint8_t SHOW[] = {0,
#if TB_HAS_CHORUS
                                   1,
#endif
#if TB_HAS_STACK
                                   2, 3, 4,
#endif
                                   5,
#if TB_HAS_TEQ
                                   6,
#endif
#if TB_HAS_REVERB
                                   7,
#endif
#if TB_HAS_DELAY
                                   8,
#endif
#if TB_HAS_STACK
                                   9
#endif
    };
    const int ns = (int)sizeof SHOW, sw = 230 / ns;
    for (int k = 0; k < ns; k++) {
        int x = 6 + k * sw;
        i = SHOW[k];
        int on = i == pg;
        gfx_rrect(x, 50, sw - 1, 15, 6, on ? HOT : f->on[i] ? gfx_heat(70) : gfx_heat(22), 255, 0);
        gfx_text(&TB_FONT_S, x + (sw - 1) / 2, 61, STEP[i], on ? TB_RGB(20, 4, 30) : f->on[i] ? CREAM : gfx_heat(90), 255, GFX_CENTER);
        if (f->on[i] && !on)
            gfx_rect(x + (sw - 1) / 2 - 2, 67, 5, 2, ACCENT, 255, GFX_NORMAL);
    }
#if TB_HAS_STACK
    if (pg == 9) {                                      /* ROUTE: where the sound goes, effect by effect */
        static const char *const FXN[5] = {"DRV", "FLG", "PHS", "TRM", "EXT"};
        const int L1 = 98, L2 = 134, L3 = 180, xa = 18, xb = 224;
        int any = 0, j;
        uint32_t line = gfx_heat(90);
        for (j = 0; j < 5; j++)
            if (f->par >> f->ord[j] & 1u)
                any = 1;
        gfx_text(&TB_FONT_S, 4, L1 + 4, "IN", CREAM, 255, GFX_LEFT);
        gfx_rect(xa, L1, xb - xa, 2, line, 255, GFX_NORMAL);
        gfx_line(xb, L1 - 4, xb + 6, L1 + 1, 16, line, 255, GFX_NORMAL);
        gfx_line(xb, L1 + 5, xb + 6, L1 + 1, 16, line, 255, GFX_NORMAL);
        if (any) {                                      /* the split lane: from the clean sound, back at the end */
            gfx_rect(xa, L1, 2, L2 - L1, line, 255, GFX_NORMAL);
            gfx_rect(xa, L2, xb - 4 - xa, 2, line, 255, GFX_NORMAL);
            gfx_rect(xb - 4, L1, 2, L2 - L1 + 2, line, 255, GFX_NORMAL);
            gfx_text(&TB_FONT_S, xa + 4, L2 + 26, "SPLIT: on the clean sound, mixed back", gfx_heat(110), 255, GFX_LEFT);
        }
        for (j = 0; j < 5; j++) {
            unsigned e = f->ord[j] % 5u;
            int x = 24 + j * 39, y = (f->par >> e & 1u) ? L2 : L1, sel = e == f->rsel, on = f->fxon >> e & 1u;
            char nb[4];
            gfx_rrect(x, y - 11, 34, 23, 6, sel ? HOT : on ? gfx_heat(80) : gfx_heat(24), 255, 0);
            if (sel && !on)
                gfx_rrect(x + 2, y - 9, 30, 19, 5, gfx_heat(24), 255, 0);
            gfx_text(&TB_FONT_S, x + 17, y + 4, FXN[e], sel && on ? TB_RGB(20, 4, 30) : on ? CREAM : sel ? HOT : gfx_heat(90), 255, GFX_CENTER);
            snprintf(nb, sizeof nb, "%d", j + 1);
            gfx_text(&TB_FONT_S, x + 17, L1 - 15, nb, sel ? HOT : gfx_heat(70), 255, GFX_CENTER);
        }
        gfx_text(&TB_FONT_S, xb - 6, L1 + 24, "then CHORUS, EQ", gfx_heat(90), 255, GFX_RIGHT);
        /* the reverb and the delay: after the effects, or split from the clean sound */
        {
            int bx = 150, c = f->rev_on ? CREAM : GREY;
            gfx_rrect(bx, L3 - 11, 76, 22, 6, f->rev_on ? gfx_heat(80) : gfx_heat(24), 255, 0);
            gfx_text(&TB_FONT_S, bx + 38, L3 + 4, "REV + DLY", c, 255, GFX_CENTER);
            if (f->send) {
                gfx_rect(xa - 8, L1 + 8, 2, L3 - L1 - 8, line, 255, GFX_NORMAL);
                gfx_rect(xa - 8, L3, bx - xa + 8, 2, line, 255, GFX_NORMAL);
                gfx_text(&TB_FONT_S, xa, L3 - 4, "clean sound", gfx_heat(110), 255, GFX_LEFT);
            } else {
                gfx_rect(xb + 2, L1 + 6, 2, L3 - L1 - 6, line, 255, GFX_NORMAL);
                gfx_rect(bx + 76, L3, xb + 4 - bx - 76, 2, line, 255, GFX_NORMAL);
                gfx_text(&TB_FONT_S, bx - 4, L3 + 4, "after the effects", gfx_heat(110), 255, GFX_RIGHT);
            }
        }
        return;
    }
#endif
    if (pg == 0 || pg == 5) {                           /* DRIVE, EXTRA: its effect, or how to pick one */
        unsigned t = f->type;
        gfx_text(&TB_FONT_L, TB_W - 12, 92, t ? f->name : "OFF", t ? CREAM : GREY, 255, GFX_RIGHT);
        if (!t) {
            gfx_text(&TB_FONT_M, 120, y0 + 30, pg ? "no extra yet" : "no drive yet", GREY, 255, GFX_CENTER);
            gfx_text(&TB_FONT_S, 120, y0 + 50, pg ? "KNOB 1: ring mod, auto-wah, comp" : "KNOB 1: overdrive, dist, fuzz, lo-fi, fold",
                     gfx_heat(110), 255, GFX_CENTER);
            return;
        }
        if (t <= 5) {                                   /* the drives: what goes in (across) comes out (up) */
            float g = 1.0f + f->p[0] / 127.0f * (t == 1 ? 6 : t == 2 ? 12 : t == 3 ? 24 : 1);
            for (i = 0; i < 64; i++) {
                float xin = (i - 31.5f) / 31.5f, o;
                if (t == 4) {                           /* LO-FI: steps */
                    float st = 2.0f / (float)(1 << (16 - f->p[0] * 14 / 127 > 6 ? 6 : 16 - f->p[0] * 14 / 127));
                    o = (float)(int)(xin / st) * st;
                } else if (t == 5) {                    /* FOLD */
                    float u = xin * (1.0f + f->p[0] / 18.0f) + (f->p[1] - 64) / 64.0f;
                    u = u - 4.0f * (float)(int)((u + 1.0f) / 4.0f + 100.0f) + 400.0f;
                    o = u < 1 ? u : 2 - u;
                } else {
                    float d = xin * g;
                    o = d > 1 ? 1 : d < -1 ? -1 : d - d * d * d / 3.0f * (t == 1 ? 1 : 0.3f);
                    o = tb_clampf(o, -1, 1);
                }
                o = xin + (o - xin) * f->p[2] / 127.0f; /* (MIX) */
                y[i] = (int16_t)(o * 30000.0f);
            }
            gfx_line(x0, y0 + h, x0 + w, y0, 12, gfx_heat(40), 255, GFX_NORMAL);
            curve(x0, y0 + 4, w, h - 8, y, 64, HOT);
        } else {                                        /* ring mod, wah, comp: a wave moving */
            int cyc = 1 + f->p[0] / 24;
            for (i = 0; i < 64; i++) {
                uint32_t ph = (uint32_t)((uint64_t)i * cyc * 0x100000000ull / 63u);
                y[i] = (int16_t)(lfo_shape(0, ph) * (10 + f->p[1]) / 140);
            }
            gfx_rect(x0, y0 + h / 2, w, 1, gfx_heat(40), 255, GFX_NORMAL);
            curve(x0, y0 + 4, w, h - 8, y, 64, HOT);
        }
        return;
    }
    if (pg == 6) {                                      /* EQ: the curve */
        eq_curve(x0, y0 - 24, w, h + 22, f->p, 1);
        return;
    }
    /* the rest: how much (big; 0 = off), what it does to the sound */
    snprintf(b, sizeof b, "%d%%", f->p[0] * 100 / 127);
    gfx_text(&TB_FONT_L, TB_W - 12, 92, f->p[0] ? b : "OFF", f->p[0] ? CREAM : GREY, 255, GFX_RIGHT);
#if TB_HAS_STACK || TB_HAS_CHORUS
    if (pg >= 1 && pg <= 4) {                           /* CHORUS .. TREMOLO: playing together, each as much as KNOB 1 */
        static const char *const WHAT[4] = {"wide, doubled", "jet sweep", "swirl", "pulse"};
        float a = f->p[0] / 127.0f;
        gfx_text(&TB_FONT_M, x0, 90, WHAT[pg - 1], f->p[0] ? ACCENT : GREY, 255, GFX_LEFT);
        if (!f->p[0]) {
            gfx_text(&TB_FONT_S, 120, y0 + 40, "KNOB 1: how much \xc2\xb7 they all play together", gfx_heat(110), 255, GFX_CENTER);
            return;
        }
        gfx_rect(x0, y0 + h / 2, w, 1, gfx_heat(40), 255, GFX_NORMAL);
        if (pg == 4) {                                  /* the tremolo: the volume, sine to square */
            int cyc = 2 + f->p[1] / 18;
            for (i = 0; i < 64; i++) {
                uint32_t ph = (uint32_t)((uint64_t)i * cyc * 0x100000000ull / 63u);
                float sn = lfo_shape(0, ph) / 32767.0f, sq = ph < 0x80000000u ? 1.0f : -1.0f, s2 = sn + (sq - sn) * f->p[3] / 127.0f;
                y[i] = (int16_t)(((1.0f - a) + a * (s2 + 1) / 2) * 30000.0f - 15000.0f);
            }
            curve(x0, y0 + 4, w, h - 8, y, 64, HOT);
        } else {                                        /* chorus, flanger, phaser: the sweep (SPEED across, DEPTH up) */
            int cyc = 1 + f->p[1] / 26, k, dep = f->p[2];
            int nlines = pg == 1 ? 2 : pg == 2 ? 1 + f->p[3] / 32 : 4;
            for (k = 0; k < nlines; k++) {
                for (i = 0; i < 64; i++) {
                    uint32_t ph = (uint32_t)((uint64_t)i * cyc * 0x100000000ull / 63u) + (uint32_t)k * (pg == 1 ? 0x80000000u : 0x10000000u);
                    y[i] = (int16_t)((float)lfo_shape(0, ph) * (0.15f + 0.85f * dep / 127.0f) * (0.4f + 0.6f * a) *
                                     (pg == 3 ? 1.0f - k * 0.2f : 1.0f));
                }
                curve(x0, y0 + 4, w, h - 8, y, 64, k ? gfx_heat(150 - k * 25) : HOT);
            }
        }
        return;
    }
#endif
    /* REVERB, DELAY (every track shares them) */
    gfx_text(&TB_FONT_S, 120, y0 + h + 14, "AMOUNT: this track \xc2\xb7 the rest: every track", gfx_heat(90), 255, GFX_CENTER);
    if (pg == 7) {                                      /* the reverb: its tail, as loud as this track sends */
        int n = 70;
        float k = 0.93f + f->p[1] / 127.0f * 0.06f, a = 0.15f + f->p[0] / 127.0f * 0.85f;
        gfx_text(&TB_FONT_M, x0, 90, f->name ? f->name : "", ACCENT, 255, GFX_LEFT);
        for (i = 0; i < n; i++) {
            int x = x0 + i * w / n, hh = (int)(a * (h - 18));
            if (hh > 0)
                gfx_rect(x, y0 + h - 6 - hh, 2, hh, gfx_heat(100 + (int)(a * 140) - f->p[2] / 4), 255, GFX_NORMAL);
            a *= f->type == 4 && i > 25 ? 0.6f : k;
        }
        return;
    }
    {                                                   /* the delay: its repeats, as loud as this track sends */
        static const uint8_t Q[7] = {6, 12, 18, 24, 8, 16, 36};
        int gap = Q[f->p[1] % 7] * 3, x = x0, k;
        float a = 0.15f + f->p[0] / 127.0f * 0.85f, fb = f->p[2] / 140.0f;
        gfx_text(&TB_FONT_M, x0, 90, f->name ? f->name : "", ACCENT, 255, GFX_LEFT);
        for (k = 0; k < 24 && x < x0 + w; k++) {
            int hh = (int)(a * (h / 2 - 8)), up = f->type != 0 || !(k & 1);
            gfx_rect(x, up ? y0 + h / 2 + 4 - hh : y0 + h / 2 + 4, 4, hh, gfx_heat(120 + (int)(a * 120)), 255, GFX_NORMAL);
            x += gap, a *= fb;
            if (a < 0.03f)
                break;
        }
    }
}

/* GLO: the mixer (8 faders, the four the knobs set lit), the song's settings */
static void menu_glo(const tb_view_t *v)
{
    int i;
    if (v->menu_page == 3) {                            /* the SONG: its chain, the place edited, the one playing */
        int first = v->song_edit > 5 ? v->song_edit - 5 : 0, k, x = 8, total = 0;
        for (k = 0; k < 32 && v->song_pat[k] < 8; k++)
            total += v->song_bars[k] ? v->song_bars[k] : 1;
        for (k = first; k < first + 7 && k < 32; k++) {
            int end = v->song_pat[k] >= 8, ed = k == v->song_edit, pl = v->song_on && v->playing && k == v->song_play;
            char b[8];
            gfx_rrect(x, 62, 29, 52, 6, ed ? gfx_heat(205) : pl ? gfx_heat(110) : end ? gfx_heat(12) : gfx_heat(30), 255, 0);
            if (pl && !ed) {
                int bb = v->song_bars[k] ? v->song_bars[k] : 1;
                gfx_rect(x + 2, 108, (v->song_bar + 1) * 25 / bb, 3, CREAM, 255, GFX_NORMAL);
            }
            snprintf(b, sizeof b, "%d", k + 1);
            gfx_text(&TB_FONT_S, x + 15, 74, b, ed ? TB_RGB(0, 0, 0) : DIM, 255, GFX_CENTER);
            if (end)
                snprintf(b, sizeof b, "END");
            else
                snprintf(b, sizeof b, "%c", 'A' + v->song_pat[k]);
            gfx_text(end ? &TB_FONT_S : &TB_FONT_L, x + 15, 96, b, ed ? TB_RGB(0, 0, 0) : end ? GREY : CREAM, 255, GFX_CENTER);
            if (!end) {
                snprintf(b, sizeof b, "x%d", v->song_bars[k] ? v->song_bars[k] : 1);
                gfx_text(&TB_FONT_S, x + 15, 108, b, ed ? TB_RGB(0, 0, 0) : GREY, 255, GFX_CENTER);
            }
            x += 32;
            if (end)
                break;
        }
        {
            char b[40];
            snprintf(b, sizeof b, "%d bars, then the start", total);
            gfx_text(&TB_FONT_S, 12, 136, b, GREY, 255, GFX_LEFT);
        }
        gfx_text(&TB_FONT_S, 12, 160, v->song_on ? "PLAYS: CHAIN (PLAY: from slot 1)" : "PLAYS: LOOP (the chain waits)",
                 v->song_on ? gfx_heat(230) : DIM, 255, GFX_LEFT);
        return;
    }
    if (v->menu_page == 4 || v->menu_page == 5) {       /* the DROP chosen: its plan, bar by bar */
        static const char *const RL[6] = {"", "SNARE", "SNARE", "KICK", "FILL", "KICK+SN"};
        int bars = 1 << (v->dp_len & 3), w = 200 / (bars + 1), x0 = 20, stop = v->dp_end == 1 || v->dp_end == 2;
        char b[64];
        snprintf(b, sizeof b, "%s: FX held + %s", v->dp_name ? v->dp_name : "", v->dp_key ? v->dp_key : "");
        gfx_text(&TB_FONT_S, x0, 62, b, CREAM, 255, GFX_LEFT);
        {
            static const char *const EN[4] = {"then the crash", "its last beat cut, then the crash", "its last beat cut",
                                              "no crash"};
            snprintf(b, sizeof b, "%s \xc2\xb7 %s", v->dp_kick ? "the kick on" : "the kick out", EN[v->dp_end & 3]);
            gfx_text(&TB_FONT_S, x0, 76, b, GREY, 255, GFX_LEFT);
        }
        for (i = 0; i <= bars; i++) {
            int x = x0 + i * w, last = i == bars - 1, after = i == bars, roll = 0;
            gfx_rrect(x, 84, w - 4, 62, 5, after ? gfx_heat(60) : gfx_heat(22), 255, 0);
            if (!after) {
                char nb[8];
                snprintf(nb, sizeof nb, "%d", i + 1);
                gfx_text(&TB_FONT_S, x + (w - 4) / 2, 97, nb, DIM, 255, GFX_CENTER);
            } else
                gfx_text(&TB_FONT_S, x + (w - 4) / 2, 97, "KICK !", CREAM, 255, GFX_CENTER);
            roll = (last && v->dp_roll && v->dp_roll != 4) || (v->dp_roll == 2 && i == bars - 2);
            if (roll && w > 30)
                gfx_text(&TB_FONT_S, x + (w - 4) / 2, 114, RL[v->dp_roll % 6], gfx_heat(230), 255, GFX_CENTER);
            if (last && v->dp_roll == 4 && w > 30)
                gfx_text(&TB_FONT_S, x + (w - 4) * 3 / 4, 114, "FILL", gfx_heat(230), 255, GFX_CENTER);
            if (last && stop)                               /* (its last beat stopped) */
                gfx_rect(x + (w - 4) * 3 / 4, 84, (w - 4) / 4, 62, TB_RGB(0, 0, 0), 200, GFX_NORMAL);
            if (after && (v->dp_end == 0 || v->dp_end == 1))
                gfx_text(&TB_FONT_S, x + (w - 4) / 2, 114, "CRASH", gfx_heat(230), 255, GFX_CENTER);
        }
        if (v->dp_filt == 1)
            gfx_line(x0 + 2, 142, x0 + bars * w - 6, 122, 20, gfx_heat(200), 255, GFX_NORMAL);
        else if (v->dp_filt == 2)
            gfx_line(x0 + 2, 122, x0 + bars * w - 6, 142, 20, gfx_heat(140), 255, GFX_NORMAL);
        gfx_text(&TB_FONT_S, 12, 162, v->drop == 2 ? "DROP RUNNING" : v->drop ? "DROP ON THE NEXT BAR"
                                                 : "KNOB 1: which DROP its knobs set", v->drop ? gfx_heat(240) : GREY, 255, GFX_LEFT);
        return;
    }
    if (v->menu_page == 7) {                            /* the RISER: its sounds' mix, its plan */
        static const char *const NM[TB_NRISER] = {"CLASSIC", "NOISE", "WIND", "SIREN", "SAW UP", "SNARE ROLL"};
        static const char *const WHAT[TB_NRISER + 1] = {"noise climbing, a siren under it", "only noise, a band sweeping up",
                                                        "white noise, dark to bright", "the rave siren, wailing up",
                                                        "a saw chord sliding up", "snare hits, faster and faster",
                                                        "the music under it: filtered or not"};
        int bars = 1 << (v->page_v[2] & 3), cur = v->page_v[0] % (TB_NRISER + 1), x0 = 12, k, y, nset = 0;
        char b[24];
        /* the plan: the bars climbing, the end (a breath, the impact) */
        for (k = 0; k < bars; k++) {
            int w = 112 / bars, x = x0 + k * w, hh = 4 + (k + 1) * 10 / bars;
            gfx_rrect(x, 78 - hh, w - 2, hh, 2, gfx_heat(70 + k * 120 / bars), 255, 0);
        }
        if (v->page_v[3] & 1)
            gfx_rect(x0 + 112 - 112 / bars / 4, 62, 112 / bars / 4, 16, TB_RGB(0, 0, 0), 255, GFX_NORMAL);
        snprintf(b, sizeof b, "%d BAR%s%s", bars, bars > 1 ? "S" : "", v->page_v[3] & 2 ? " + BOOM" : "");
        gfx_text(&TB_FONT_S, TB_W - 12, 76, b, v->page_v[3] & 2 ? CREAM : GREY, 255, GFX_RIGHT);
        /* the mix: each sound, its level (0: off), the one the knobs set lit */
        for (k = 0; k <= TB_NRISER; k++) {
            int sel = k == cur, on = k < TB_NRISER ? v->riser_lvl[k] != 0 : v->riser_filt;
            y = 86 + k * 14;
            if (sel)
                gfx_rrect(6, y - 1, TB_W - 12, 14, 4, gfx_heat(40), 255, 0);
            gfx_text(&TB_FONT_S, x0, y + 10, k < TB_NRISER ? NM[k] : "MUSIC", sel ? CREAM : on ? gfx_heat(200) : DIM, 255, GFX_LEFT);
            if (k < TB_NRISER) {
                int lv = v->riser_lvl[k];
                nset += lv != 0;
                gfx_rrect(96, y + 4, 112, 5, 2, gfx_heat(20), 255, 0);
                if (lv)
                    gfx_rrect(96, y + 4, 2 + lv * 110 / 100, 5, 2, sel ? HOT : ACCENT, 255, 0);
                snprintf(b, sizeof b, lv ? "%d" : "off", lv);
                gfx_text(&TB_FONT_S, TB_W - 10, y + 10, b, lv ? (sel ? CREAM : GREY) : DIM, 255, GFX_RIGHT);
            } else
                gfx_text(&TB_FONT_S, 96, y + 10, on ? "FILTERED" : "AS IT IS", on ? (sel ? CREAM : GREY) : DIM, 255, GFX_LEFT);
        }
        gfx_text(&TB_FONT_S, x0, 196, WHAT[cur], GREY, 255, GFX_LEFT);
        snprintf(b, sizeof b, nset > 1 ? "%d SOUNDS TOGETHER" : nset ? "1 SOUND" : "NO SOUND", nset);
        gfx_text(&TB_FONT_S, x0, 62, v->riser == 2 ? "RISER RUNNING" : v->riser ? "RISER ON THE NEXT BAR" : b,
                 v->riser ? gfx_heat(240) : ACCENT, 255, GFX_LEFT);
        return;
    }
    if (v->menu_page == 8) {                            /* the clock: the beats, the sync */
        for (i = 0; i < 4; i++) {
            int lit = v->playing && i == v->bar_beat, x = 40 + i * 44;
            gfx_rrect(x, 66, 28, 28, 14, lit ? gfx_heat(i ? 200 : 245) : gfx_heat(22), 255, 0);
        }
        gfx_text(&TB_FONT_M, 120, 124, v->page_v[0] ? "SYNC: MIDI CLOCK" : "SYNC: INTERNAL", CREAM, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 144, v->page_v[0] ? "the tempo follows the incoming MIDI clock" : "its own tempo (GLO + PLAY: tap)",
                 GREY, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 160, v->page_v[1] && !v->page_v[0] ? "it sends its clock over MIDI" : "", DIM, 255, GFX_CENTER);
        return;
    }
    if (v->menu_page == 9) {                            /* REC: a beat of four steps, a note played late, where it lands */
        static const char *const G[4] = {"FREE: AS PLAYED", "GRID 1/32", "GRID 1/16", "GRID 1/8"};
        static const uint8_t KEEP[4] = {0, 25, 50, 75};
        int x0 = 20, w = 200, gk = v->page_v[0] % 4, q = v->page_v[1] % 4, played = 34, kept, gw;
        gw = gk == 1 ? w / 8 : gk == 3 ? w / 2 : w / 4;     /* (the grid's spacing on the picture) */
        if (gk == 0)
            kept = played;
        else {
            int g = (played + gw / 2) / gw * gw;
            kept = g + (played - g) * KEEP[q] / 100;
        }
        gfx_rect(x0, 110, w, 1, gfx_heat(60), 255, GFX_NORMAL);
        for (i = 0; i <= 4; i++)                        /* the steps (the beat's four 16ths) */
            gfx_rect(x0 + i * w / 4, 80, 1, 60, gfx_heat(i % 4 ? 50 : 110), 255, GFX_NORMAL);
        if (gk)
            for (i = 0; i * gw <= w; i++)               /* the grid: where the notes go */
                gfx_rect(x0 + i * gw - 1, 96, 3, 30, ACCENT, 255, GFX_NORMAL);
        gfx_rrect(x0 + played - 4, 84, 8, 8, 4, GREY, 255, 1);    /* where the key came */
        gfx_rrect(x0 + kept - 5, 104, 10, 12, 3, gfx_heat(235), 255, 0);   /* where it is recorded */
        if (kept != played)
            gfx_line(x0 + played, 92, x0 + kept, 104, 20, gfx_heat(150), 255, GFX_NORMAL);
        gfx_text(&TB_FONT_M, 120, 66, G[gk], CREAM, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 152, gk == 0 ? "the notes kept exactly as you play them" : q == 0 ? "every note right on the grid"
                                                                                       : "the notes moved closer to the grid", GREY, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 166, "afterwards: SEQ, TOOLS, QUANTIZE", GREY, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 180, "REC: armed \xc2\xb7 PLAY: records \xc2\xb7 REC held: here", DIM, 255, GFX_CENTER);
        return;
    }
    if (v->menu_page == 6) {                            /* the MUTATIONS */
        gfx_text(&TB_FONT_L, 120, 100, v->mutating ? "MUTATIONS ON" : "MUTATIONS OFF", v->mutating ? CREAM : GREY, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 126, "kick, 909 and break vary as they play", DIM, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 142, "(extra hits, ghost notes, slices, fills)", DIM, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 162, "your steps stay as they are \xc2\xb7 FX held + key 16", GREY, 255, GFX_CENTER);
        return;
    }
    if (v->menu_page == 10) {                           /* the MASTER: its EQ, its drive (all the tracks, after them) */
        uint8_t q[3] = {v->page_v[0], v->page_v[1], v->page_v[2]};
        gfx_text(&TB_FONT_S, 14, 64, "the whole mix, after every track", GREY, 255, GFX_LEFT);
        eq_curve(14, 74, 212, 80, q, 0);
        gfx_text(&TB_FONT_S, 14, 182, "DRIVE", GREY, 255, GFX_LEFT);
        gfx_rrect(56, 175, 170, 6, 3, gfx_heat(20), 255, 0);
        if (v->page_v[3])
            gfx_rrect(56, 175, 2 + v->page_v[3] * 168 / 127, 6, 3, HOT, 255, 0);
        return;
    }
    if (v->menu_page == 2) {                            /* the song: its tempo big, the patterns A..H */
        char t[8];
        snprintf(t, sizeof t, "%u", v->bpm10 / 10u);
        gfx_text(&TB_FONT_XL, 120, 104, t, CREAM, 255, GFX_CENTER);
        gfx_text(&TB_FONT_S, 120, 120, "BPM", GREY, 255, GFX_CENTER);
        for (i = 0; i < 8; i++) {
            int x = 14 + i * 27, cur = i == v->pattern, q = i == v->queued;
            char pl[2] = {(char)('A' + i), 0};
            gfx_rrect(x, 136, 23, 26, 5, cur ? gfx_heat(215) : q ? gfx_heat(110) : gfx_heat(24), 255, 0);
            gfx_text(&TB_FONT_M, x + 12, 155, pl, cur ? TB_RGB(0, 0, 0) : GREY, 255, GFX_CENTER);
        }
        gfx_text(&TB_FONT_S, 120, 180, "a pattern picked starts at the end of the bar", DIM, 255, GFX_CENTER);
        return;
    }
    for (i = 0; i < TB_TRACKS; i++) {
        int x = 8 + i * 29, lit = i / 4 == v->menu_page, hh = v->mix_vol[i] * 104 / 127, lv = v->lane[i].level * 104 / 255;
        char n[5];
        gfx_rrect(x, 50, 24, 108, 5, gfx_heat(lit ? 30 : 16), 255, 0);
        gfx_rect(x + 4, 52 + 104 - lv, 4, lv, gfx_heat(170), 255, GFX_NORMAL);
        gfx_rrect(x + 10, 52 + 104 - hh, 10, hh, 3, lit ? gfx_heat(210) : gfx_heat(90), 255, 0);
        snprintf(n, sizeof n, "%d", i + 1);
        gfx_text(&TB_FONT_M, x + 12, 174, n, lit ? CREAM : GREY, 255, GFX_CENTER);
        snprintf(n, sizeof n, "%.3s", v->lane[i].name);
        gfx_text(&TB_FONT_S, x + 12, 188, n, lit ? gfx_heat(200) : DIM, 255, GFX_CENTER);
        if (v->lane[i].muted)
            gfx_text(&TB_FONT_S, x + 12, 64, "M", gfx_heat(240), 255, GFX_CENTER);
        else if (v->lane[i].solo)
            gfx_text(&TB_FONT_S, x + 12, 64, "S", gfx_heat(240), 255, GFX_CENTER);
    }
}

static void menu_sys(const tb_view_t *v)
{
    int i;
    static const char *const ASK[4] = {0, "OCT+: LOAD?", "OCT-: NEW PROJECT?", "SAVE: OVERWRITE?"};
    for (i = 0; i < 8; i++) {                           /* the eight places: the one in use, the one chosen */
        int x = 8 + (i % 4) * 57, y = 54 + (i / 4) * 56, used = (v->slot_used >> i) & 1, sl = i == v->slot_sel, cur = i == v->slot_cur;
        char b[12];
        gfx_rrect(x, y, 53, 50, 7, sl ? gfx_heat(200) : used ? gfx_heat(34) : gfx_heat(14), 255, 0);
        if (cur)
            gfx_rrect(x - 2, y - 2, 57, 54, 8, CREAM, 255, 1);
        snprintf(b, sizeof b, "%d", i + 1);
        gfx_text(&TB_FONT_L, x + 26, y + 26, b, sl ? TB_RGB(0, 0, 0) : used ? CREAM : DIM, 255, GFX_CENTER);
        if (used)
            snprintf(b, sizeof b, "%u BPM", v->slot_bpm[i] / 10u);
        else
            snprintf(b, sizeof b, "EMPTY");
        gfx_text(&TB_FONT_S, x + 26, y + 43, b, sl ? TB_RGB(0, 0, 0) : GREY, 255, GFX_CENTER);
    }
    {                                                   /* (the sound's health: blocks the audio missed, since power on) */
        char b[28];
        if (v->dropouts)
            snprintf(b, sizeof b, "AUDIO: %lu DROPOUTS", (unsigned long)v->dropouts);
        else
            snprintf(b, sizeof b, "AUDIO: NO DROPOUT");
        gfx_text(&TB_FONT_S, 120, 178, b, v->dropouts ? gfx_heat(240) : GREY, 255, GFX_CENTER);
        if (v->audio_pct || v->heap_k) {              /* (what the FM-1 has to spare: the audio's time, the SDK's heap) */
            snprintf(b, sizeof b, "CPU %u%%  RAM %uK", (unsigned)v->audio_pct, (unsigned)v->heap_k);
            gfx_text(&TB_FONT_S, 120, 210, b, v->audio_pct >= 85u ? gfx_heat(240) : DIM, 255, GFX_CENTER);
        }
#if TB_LITE
        gfx_text(&TB_FONT_S, 120, 194, "RESCUE: HOLD OCT- + OCT+ 5 S", DIM, 255, GFX_CENTER);   /* (the FM-1's way out) */
#endif
    }
    if (v->sys_ask && v->sys_ask < 4) {
        gfx_rrect(10, 112, 220, 40, 8, gfx_heat(220), 255, 0);
        gfx_text(&TB_FONT_M, 120, 137, ASK[v->sys_ask], TB_RGB(0, 0, 0), 255, GFX_CENTER);
    }
}

/* the sequencer's tools: the list, the one chosen lit, what it does */
static void menu_tools(const tb_view_t *v)
{
    int i, first = v->tool > 3 ? v->tool - 3 : 0;
    if (first > v->tool_n - 7)
        first = v->tool_n - 7 > 0 ? v->tool_n - 7 : 0;
    for (i = first; i < first + 7 && i < v->tool_n; i++) {
        int y = 50 + (i - first) * 17, lit = i == v->tool;
        if (lit)
            gfx_rrect(6, y, 228, 16, 5, gfx_heat(200), 255, 0);
        gfx_text(&TB_FONT_M, 14, y + 13, v->tool_names ? v->tool_names[i] : "", lit ? TB_RGB(0, 0, 0) : GREY, 255, GFX_LEFT);
    }
    gfx_text(&TB_FONT_S, 12, 182, v->tool_what ? v->tool_what : "", CREAM, 255, GFX_LEFT);
}

void tb_ui_menu(const tb_view_t *v)
{
    menu_frame(v);
    switch (v->menu) {
#if TB_HAS_MENU_PICS
    case TB_MENU_ENV: menu_env(v); break;
    case TB_MENU_LFO: menu_lfo(v); break;
    case TB_MENU_ARP: menu_arp(v); break;
    case TB_MENU_SCALE: menu_scale(v); break;
    case TB_MENU_FX: menu_fx(v); break;
    case TB_MENU_GLO: menu_glo(v); break;
#endif
    case TB_MENU_SYS: menu_sys(v); break;
    case TB_MENU_TOOLS: menu_tools(v); break;
    default: break;
    }
    if (v->hint)
        gfx_text(&TB_FONT_S, 120, 195, v->hint, gfx_heat(110), 255, GFX_CENTER);
    knobs(v);
    hud(v);
}

/* FX held: the live effects, one a white key, lit while held */
void tb_ui_perform(const tb_view_t *v)
{
    gfx_begin();
    top_bar(v);
    title_row("LIVE FX", v->lane[v->track].name, 0, 0);
    menu_tiles(v);
    knobs(v);                                           /* (the DROP's and the RISER's sounds, how loud) */
    hud(v);
}

/* EDIT on the kick: the kick designer. Its sound over the beat, its pitch falling from the punch to its note, the
 * playhead running through it when it plays; the knobs below */
void tb_ui_kick(const tb_view_t *v)
{
    int i, x0 = 9, w = 222, wy = 64, wh = 58, py = 130, ph = 52;
    char b[32];
    gfx_begin();
    top_bar(v);
    title_row("KICK", v->strip_title, v->strip_pages, v->strip_page);
    well(2, 45, TB_W - 4, 155);
    /* the line over it: the preset, how long the drawing lasts */
    gfx_text(&TB_FONT_S, x0, 59, v->kick_name ? v->kick_name : "", ACCENT, 255, GFX_LEFT);
    snprintf(b, sizeof b, "%u ms", v->kick_ms);
    gfx_text(&TB_FONT_S, x0 + w, 59, b, gfx_heat(90), 255, GFX_RIGHT);
    /* its sound */
    {
        uint8_t pk[TB_W], ht[TB_W];
        for (i = 0; i < w; i++)
            pk[i] = v->kick_wave[i * 200 / w], ht[i] = (uint8_t)(20 + i * 70 / w);
        gfx_wave(x0, wy, w, wh, pk, ht, 110);
    }
    /* its pitch: Hz on a log scale (30 .. 600) */
    gfx_rect(x0, py - 3, w, 1, gfx_heat(30), 255, GFX_NORMAL);
    {
        int px = 0, pyy = 0, k;
        for (k = 0; k < 50; k++) {
            int hz = v->kick_hz[k] < 30 ? 30 : v->kick_hz[k] > 600 ? 600 : v->kick_hz[k], lg = 0, x = x0 + k * w / 49, y;
            int t = hz;
            while (t >= 60)                              /* (a coarse log2: octaves over 30 Hz, then the fraction) */
                t /= 2, lg += 64;
            lg += (t - 30) * 64 / 30;
            y = py + ph - 4 - lg * (ph - 8) / 277;
            if (k)
                gfx_line(px, pyy, x, y, 30, gfx_heat(230), 255, GFX_NORMAL);
            px = x, pyy = y;
        }
    }
    if (v->kick_play != 0xFFFF) {                       /* where it is as it plays */
        int x = x0 + v->kick_play * w / 200;
        gfx_rect(x, wy, 1, wh, CREAM, 230, GFX_NORMAL);
        gfx_rect(x, py, 1, ph, CREAM, 160, GFX_NORMAL);
    }
    /* under it: the pitch from the attack to the note */
    snprintf(b, sizeof b, "%u Hz \xe2\x86\x92 %u Hz", (unsigned)v->kick_hz[1], (unsigned)v->kick_hz[44]);
    gfx_text(&TB_FONT_S, x0, 195, b, GREY, 255, GFX_LEFT);
    snprintf(b, sizeof b, "NOTE %s", v->kick_note);
    gfx_text(&TB_FONT_S, x0 + w, 195, b, CREAM, 255, GFX_RIGHT);
    knobs(v);
    hud(v);
}

int tb_ui_dance(const tb_view_t *v)
{
    unsigned n = gfx_dance_frames(), per = n / 12u;
    char b[16];
    if (!n)
        return 0;
    gfx_begin();
    gfx_rect(0, 0, TB_W, TB_H, gfx_bg(), 255, GFX_NORMAL);
    /* beat k of its loop of 12: its landing on the beat, the pose half way on the off-beat */
    gfx_dance((unsigned)(v->beats % 12u) * per + (unsigned)v->beat_phase * per / 1024u, gfx_heat(200));
    snprintf(b, sizeof b, "%u BPM", v->bpm10 / 10u);
    gfx_text(&TB_FONT_S, 8, 230, b, gfx_heat(120), 255, GFX_LEFT);   /* (bottom left, clear of the legs) */
    return 1;
}

void tb_ui_splash(uint32_t ms)
{
#if TB_HAS_SPLASH
    unsigned a = ms > 900 ? 255 : ms * 255 / 900;
    gfx_begin();
    unsigned t = ms > 500 ? ms - 500 : 0, b = t > 700 ? 255 : t * 255 / 700;   /* (the bat first, then its name) */
#else
    unsigned a = 255, b = 255;
    (void)ms;
    gfx_begin();
#endif
    gfx_logo(0, 34, (uint8_t)a);                        /* RAVE FM's bat */
    gfx_text(&TB_FONT_XL, 120, 186, "RAVE FM", gfx_heat(200), (uint8_t)b, GFX_CENTER);
    gfx_text(&TB_FONT_S, 120, 214, "HARDCORE \xc2\xb7 GABBER \xc2\xb7 RAVE", gfx_heat(150), (uint8_t)b, GFX_CENTER);
}
