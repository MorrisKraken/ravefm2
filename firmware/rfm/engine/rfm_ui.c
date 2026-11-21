/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the screens (tb_ui.h), in JIANT's frame (../jui/rfm_screen.c: its renderer, header, tabs, cards, panel,
 * footer); the pictures inside the panel drawn with rfm_gfx.h's calls (the panel's canvas). */
#include "tb_ui.h"
#include "tb_engine.h"
#include "rfm_gfx.h"
#include "tb_dsp.h"
#include "tb_store.h"
#include <stdio.h>
#include <string.h>
#include "../jui/rfm_screen.c"

#define CREAM gfx_ink(0)
#define GREY gfx_ink(1)
#define DIM gfx_ink(2)
/* (the old pictures' layout: their title row, the lanes under it; mapped into the panel) */
#define TITLE_Y 39
#define LANES_Y 45
#define PIC_Y GR_Y
#define PIC_H GR_H
#define PANEL PXC(PX_PANEL)
#define PANEL_HI PXC(PX_LINE)
#define EDGE PXC(PX_LINE)
#define WELL PXC(PX_BG)
#define ACCENT PXC(PX_MAIN)
#define HOT PXC(PX_HOT)
static void panel(int x, int y, int w, int h, int r, uint32_t c) { gfx_rrect(x, y, w, h, r > 4 ? 4 : r, c, 255, 0); }
static void well(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }   /* (the panel: no frame) */
static int chip(int x, int base, const char *s, uint32_t c, int filled, int right)
{
    (void)x, (void)c, (void)filled, (void)right;
    if (base < LANES_Y)                                 /* (by the old title: to the footer) */
        gfx_title_text(s);
    return gfx_text_width(&TB_FONT_S, s) + 6;
}
static int title_row(const char *name, const char *page, int npages, int on)
{
    (void)name, (void)page, (void)npages, (void)on;
    return TB_W - 6;
}
static void knobs(const tb_view_t *v) { (void)v; }      /* (the cards: drawn by the frame) */
static void hud(const tb_view_t *v) { (void)v; }        /* (the toast and the footer: gfx_end) */
static void playhead(int x, int y0, int h, uint8_t a) { gfx_rect(x, y0, 1, h, a > 200u ? CREAM : GREY, 255, GFX_NORMAL); }
static const tb_font_t *fit(const char *s, int w, const tb_font_t *a, const tb_font_t *b, const tb_font_t *c)
{
    if (a && gfx_text_width(a, s) <= w)
        return a;
    if (b && gfx_text_width(b, s) <= w)
        return b;
    return c;
}
static void txt_fit(int cx, int y, int w, const char *s, uint32_t col, uint8_t a, const tb_font_t *big)
{
    const tb_font_t *f = fit(s, w, big, &TB_FONT_M, &TB_FONT_S);
    gfx_text(f, cx, y, s, col, a, GFX_CENTER);
}

/* ------------------------------------------------------------------ the dancers --- */
/* Each track a dancer (tools/gen_chars.py: pixel art, 32 x 48, 8 frames on two beats): the kick's gabber, the break's
 * robot, the bass's block head, the lead's air horn, the synth's hooded skull, the sample's gamer DJ. */
#include "rfm_chars.h"
static uint32_t heard_ms[TB_TRACKS];                   /* when each track was last heard */
static uint8_t peak[TB_TRACKS];                        /* .. how loud, lately */
static uint32_t hit_ms[TB_TRACKS];                     /* its last hit */
static uint8_t lvl_prev[TB_TRACKS];
static uint16_t hits_seen[TB_TRACKS];
static void listen(const tb_view_t *v)
{
    unsigned t;
    for (t = 0; t < TB_TRACKS; t++) {
        const tb_lane_t *l = &v->lane[t];
        if (v->playing && !l->muted && (l->active || l->level >= 8u))
            heard_ms[t] = v->ms ? v->ms : 1u;
        if (l->hits != hits_seen[t] || (!l->muted && l->level >= lvl_prev[t] + 48u))   /* (a new note, or the level leaping) */
            hit_ms[t] = v->ms ? v->ms : 1u;
        hits_seen[t] = l->hits;
        lvl_prev[t] = l->level;
        peak[t] = (uint8_t)(l->level > peak[t] ? l->level : peak[t] > 3u ? peak[t] - 3u : 0u);
    }
}
static int any_solo(const tb_view_t *v)
{
    unsigned t;
    for (t = 0; t < TB_TRACKS; t++)
        if (v->lane[t].solo)
            return 1;
    return 0;
}
static int plays(const tb_view_t *v, unsigned t)        /* heard now (a hit in the last 1.5 s) */
{
    const tb_lane_t *l = &v->lane[t];
    return v->playing && !l->muted && heard_ms[t] && v->ms - heard_ms[t] < 1500u && (!any_solo(v) || l->solo);
}
static int fsin(unsigned u, int amp) { return (int)(tb_sin((uint32_t)u << 22) * amp / 32768); }   /* u: 1024 a turn (the shift drops the turns: no mask, its constant reads as a ROM address to the build's check) */
static unsigned dancer_of(unsigned icon)                /* the TB_ICON_* -> its dancer */
{
    static const uint8_t D[TB_NICON] = {0, 1, 1, 2, 3, 4, 4, 5};
    return D[icon % TB_NICON] % RFM_NCHAR;
}
/* a translucent pixel / row: c over what the canvas holds, a16 0..16 (the canvas holds the panel's byte order) */
static inline void blend_px(int32_t x, int32_t y, uint16_t c, uint32_t a16)
{
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h) {
        uint16_t *d = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
        uint32_t o = swap16(*d), s;
        o = (o | o << 16) & 0x07E0F81Fu;
        s = ((uint32_t)c | (uint32_t)c << 16) & 0x07E0F81Fu;
        o = (o + (((s - o) * a16) >> 4)) & 0x07E0F81Fu;
        *d = swap16((uint16_t)(o | o >> 16));
    }
}
/* a beam of light: from (x0, y0) w0 wide down to (x1, y1) w1 wide, every other row (light, and half the work) */
static void beam(int32_t x0, int32_t y0, int32_t w0, int32_t x1, int32_t y1, int32_t w1, uint16_t c, uint32_t a16)
{
    int32_t y, h = y1 - y0;
    for (y = y0; y < y1; y++) {
        int32_t l = x0 - w0 / 2 + (x1 - w1 / 2 - (x0 - w0 / 2)) * (y - y0) / (h ? h : 1);
        int32_t r = x0 + w0 / 2 + (x1 + w1 / 2 - (x0 + w0 / 2)) * (y - y0) / (h ? h : 1), x;
        uint32_t a = a16 * (uint32_t)(h - (y - y0) / 2) / (uint32_t)(h ? h : 1);   /* (fading as it falls) */
        if ((y & 1) == 0)
            continue;
        for (x = l < 0 ? 0 : l; x <= r && x < (int32_t)cv_w; x++)
            blend_px(x, y, c, a);
    }
}
static void pool(int32_t cx, int32_t cy, int32_t rx, uint16_t c, uint32_t a16)   /* a lit oval on the floor */
{
    int32_t dy, ry = rx / 4 + 1;
    for (dy = -ry; dy <= ry; dy++) {
        int32_t w = rx * (ry * ry - dy * dy) / (ry * ry), x;
        for (x = -w; x <= w; x++)
            blend_px(cx + x, cy + dy, c, a16);
    }
}
/* a dancer's frame on the canvas, its feet at (x, feet); pal: its colours (lit, far, or a silhouette) */
static void sprite(unsigned k, unsigned f, const uint16_t *pal, int32_t x, int32_t feet, int flip)
{
    const uint8_t *r = RFM_CHAR_RLE[k] + RFM_CHAR_OFF[k][f % RFM_CHAR_NFR];
    int32_t i = 0, n = RFM_CHAR_W * RFM_CHAR_H, x0 = x - RFM_CHAR_W / 2, y0 = feet - RFM_CHAR_H + 1;
    while (i < n) {
        int32_t run = (*r >> 4) + 1, c = *r & 15, m;
        r++;
        if (c)
            for (m = 0; m < run; m++) {
                int32_t p = i + m, sx = p % RFM_CHAR_W, sy = p / RFM_CHAR_W;
                cv_pset(x0 + (flip ? RFM_CHAR_W - 1 - sx : sx), y0 + sy, pal[c]);
            }
        i += run;
    }
}
static uint16_t pal_far[RFM_NCHAR][16], pal_dark[RFM_NCHAR][16];
static uint32_t pal_gen;
static const uint16_t *pal_of(unsigned k, int far, int lit)
{
    unsigned i, j;
    if (pal_gen != ux.gen + 1u) {
        for (i = 0; i < RFM_NCHAR; i++)
            for (j = 0; j < 16u; j++) {
                pal_far[i][j] = ux_mix(T_BG, RFM_CHAR_PAL[i][j], 70);
                pal_dark[i][j] = j ? ux_mix(T_BG, ux_mix(T_RAISE, RFM_CHAR_PAL[i][j], 30), 85) : 0;
            }
        pal_gen = ux.gen + 1u;
    }
    return !lit ? pal_dark[k] : far ? pal_far[k] : RFM_CHAR_PAL[k];
}
void tb_ui_icon(unsigned icon, int cx, int cy, int size, uint32_t c, uint8_t a)
{
    (void)icon, (void)cx, (void)cy, (void)size, (void)c, (void)a;   /* (RAVE FM 2: no dancer icons; the old sprites dropped) */
}

/* ------------------------------------------------------------------ HOME --- */
/* The dance floor, in the dark: a truss of lamps at the top (one a track), stacks of speakers at the sides, the floor's
 * lines going away. The tracks playing come to the front (the loudest three), dance on the tempo and jump on their
 * hits under their own lamp's beam; the others stand at the back, darker; the silent and the muted ones in the shadow
 * (a muted one under a red cross). The lamps swing on the beat and flare on the hits, the speakers pump on the kick,
 * the lead's and the synth's lasers fan out on their notes. GLO held: a key's box on each, lit where it plays. */
#define FLOOR_Y 52                                       /* the horizon */
static int16_t dpx[TB_TRACKS], dpy[TB_TRACKS];         /* where each dancer is (its feet), Q4 */
static uint8_t in_front[TB_TRACKS], placed;
static uint32_t last_beat = 0xFFFFFFFFu, last_ms;
static int kick_now(const tb_view_t *v)
{
    unsigned t;
    for (t = 0; t < TB_TRACKS; t++)
        if (v->lane[t].active && !v->lane[t].muted && v->lane[t].icon == TB_ICON_KICK)
            return 1;
    return 0;
}
static uint16_t lamp_col(unsigned t) { return t % 3u == 1u ? T_TEXT : t % 3u == 0u ? T_THEME : ux_mix(T_BG, T_REC, 70); }   /* (red, white, blood) */
static void place(const tb_view_t *v, int32_t *tx, int32_t *ty, int *far)
{
    static const int16_t FX[3] = {44, 120, 196}, BX[TB_TRACKS] = {34, 78, 120, 162, 206, 120};
    unsigned t, k, nf = 0, nb = 0, front[TB_TRACKS], back[TB_TRACKS], i;
    if (v->beats != last_beat || !v->playing) {        /* (decided on the beat: no jitter) */
        last_beat = v->beats;
        for (t = 0; t < TB_TRACKS; t++)
            in_front[t] = 0;
        for (k = 0; k < 3u; k++) {
            int best = -1;
            for (t = 0; t < TB_TRACKS; t++)
                if (!in_front[t] && (v->playing ? plays(v, t) : t < 3u) &&
                    (best < 0 || peak[t] > peak[best]))
                    best = (int)t;
            if (best >= 0)
                in_front[best] = 1;
        }
    }
    for (t = 0; t < TB_TRACKS; t++) {
        if (in_front[t] && v->playing && !plays(v, t))
            in_front[t] = 0;                           /* (it stopped between beats: back it goes) */
        if (in_front[t])
            front[nf++] = t;
        else
            back[nb++] = t;
    }
    for (i = 0; i < nf; i++)                           /* the front: spread, a little staggered */
        tx[front[i]] = nf == 1 ? 120 : nf == 2 ? (i ? 160 : 80) : FX[i], ty[front[i]] = 136 - (int32_t)(i == 1) * 4,
        far[front[i]] = 0;
    for (i = 0; i < nb; i++)                           /* the back: in a loose row, every other one a step back */
        tx[back[i]] = nb > 1 ? 46 + (int32_t)i * 148 / (int32_t)(nb - 1) : BX[2], ty[back[i]] = 92 - (int32_t)(i & 1u) * 6,
        far[back[i]] = 1;
}
static void speakers(int32_t x, int pump)               /* a stack of three cabinets, battered */
{
    int32_t k;
    for (k = 0; k < 3; k++) {
        int32_t y = 30 + k * 34, cx = x + 12, cy = y + 16;
        cv_rect(x, y, 24, 32, RGB(14, 12, 12));
        cv_frame(x, y, 24, 32, RGB(40, 34, 34));
        circle(cx, cy - 4, 8 + pump, RGB(70, 60, 60), 0);
        circle(cx, cy - 4, 3, pump > 1 ? T_THEME : RGB(50, 40, 40), 1);
        circle(cx, cy + 11, 2, RGB(50, 44, 44), 0);
    }
}
/* the warehouse's back wall: concrete blocks behind a chain-link fence, a skull sprayed on it (its eyes red on the
 * kick), TERROR stencilled and dripping; chains hanging from the truss; the strobe lights it all white */
static void warehouse(const tb_view_t *v, int kick)
{
    int32_t x, y, k, flash = kick && (v->beats & 1u);
    uint16_t brick = flash ? RGB(90, 86, 84) : RGB(22, 18, 18), mortar = flash ? RGB(40, 36, 36) : RGB(8, 6, 6);
    uint16_t fence = flash ? RGB(140, 136, 130) : RGB(34, 30, 30), paint = flash ? RGB(255, 40, 40) : RGB(110, 8, 8);
    cv_rect(0, 0, 240, FLOOR_Y, brick);
    for (y = 6; y < FLOOR_Y; y += 8) {                  /* the blocks */
        cv_rect(0, y, 240, 1, mortar);
        for (x = (y / 8) % 2 ? 0 : 10; x < 240; x += 20)
            cv_rect(x, y - 7, 1, 7, mortar);
    }
    {                                                   /* the skull, sprayed in the middle */
        int32_t cx = 120, cy = 22;
        circle(cx, cy, 13, paint, 0), circle(cx, cy, 12, paint, 0);
        cv_rect(cx - 7, cy + 11, 15, 6, brick), cv_frame(cx - 7, cy + 11, 15, 6, paint);
        for (k = -4; k <= 4; k += 4)
            cv_rect(cx + k, cy + 12, 1, 5, paint);
        circle(cx - 5, cy + 1, 3, kick ? T_THEME : RGB(0, 0, 0), 1), circle(cx + 5, cy + 1, 3, kick ? T_THEME : RGB(0, 0, 0), 1);
        cv_rect(cx - 1, cy + 6, 3, 2, RGB(0, 0, 0));
    }
    {                                                   /* TERROR, stencilled either side, dripping */
        static const uint8_t DRIP[10] = {5, 2, 8, 3, 6, 1, 9, 4, 2, 7};
        int32_t w = text_w(&AF_L, "TERROR");
        for (k = 0; k < 2; k++) {
            int32_t tx = k ? 230 - w : 34;
            if (k) {                                    /* (the right one: HARDCORE, smaller) */
                w = text_w(&AF_X, "HARDCORE"), tx = 206 - w;
                cv_text_on(tx, 30, &AF_X, "HARDCORE", paint, brick);
                continue;
            }
            cv_text_on(tx, 6, &AF_X, "TERROR", paint, brick);
            for (x = 0; x < 10; x++)
                cv_rect(tx + 2 + x * 4, 16, 1, DRIP[x], paint);
        }
    }
    for (y = -40; y < FLOOR_Y; y += 7)                  /* the fence over it */
        cv_line(0, y, 60, y + 60, fence), cv_line(60, y + 60, 0, y + 120, fence);
    for (x = 0; x < 240; x += 14) {
        cv_line(x, 0, x + FLOOR_Y, FLOOR_Y, fence);
        cv_line(x + FLOOR_Y, 0, x, FLOOR_Y, fence);
    }
    for (k = 0; k < 5; k++) {                           /* the chains, swaying */
        int32_t cx = 40 + k * 40, sw = fsin(v->ms / 7u + (unsigned)k * 200u, 2), n = 4 + (k * 3) % 4;
        for (y = 4; y < 4 + n * 4; y += 4)
            cv_frame(cx + sw * (y - 4) / 20, y, 2, 3, RGB(90, 80, 76));
    }
}
static void home_floor(const tb_view_t *v)
{
    int32_t tx[TB_TRACKS], ty[TB_TRACKS], far[TB_TRACKS], k, step;
    unsigned t, i, ord[TB_TRACKS];
    int kick = kick_now(v), pump = kick ? 2 : v->playing && v->beat_phase < 160u ? 1 : 0;
    listen(v);
    step = (int32_t)((v->ms - last_ms) / 16u);
    step = step < 1 ? 1 : step > 6 ? 6 : step;
    last_ms = v->ms;
    warehouse(v, kick);
    cv_rect(0, FLOOR_Y, 240, 1, RGB(50, 10, 10));      /* the floor: dark concrete, its cracks, old blood */
    cv_line(30, FLOOR_Y + 20, 70, FLOOR_Y + 36, RGB(28, 24, 24)), cv_line(70, FLOOR_Y + 36, 84, FLOOR_Y + 70, RGB(28, 24, 24));
    cv_line(170, FLOOR_Y + 10, 200, FLOOR_Y + 46, RGB(28, 24, 24)), cv_line(150, FLOOR_Y + 60, 210, FLOOR_Y + 80, RGB(28, 24, 24));
    pool(60, FLOOR_Y + 62, 16, RGB(90, 0, 0), 9), pool(186, FLOOR_Y + 34, 12, RGB(90, 0, 0), 8);
    cv_rect(0, 1, 240, 2, RGB(60, 56, 56));            /* the truss */
    speakers(0, pump);
    speakers(216, pump);
    place(v, tx, ty, far);
    for (t = 0; t < TB_TRACKS; t++) {                  /* walking to their places */
        if (!placed)
            dpx[t] = (int16_t)(tx[t] << 4), dpy[t] = (int16_t)(ty[t] << 4);
        else {
            int32_t ddx = (tx[t] << 4) - dpx[t], ddy = (ty[t] << 4) - dpy[t], m = step * 16;
            dpx[t] = (int16_t)(dpx[t] + (ddx > m ? m : ddx < -m ? -m : ddx));
            dpy[t] = (int16_t)(dpy[t] + (ddy > m ? m : ddy < -m ? -m : ddy));
        }
    }
    placed = 1;
    for (t = 0; t < TB_TRACKS; t++) {                  /* the lamps, and the beams of the tracks playing */
        int32_t lx = 20 + (int32_t)t * 40, on = plays(v, t);
        uint16_t c = lamp_col(t);
        cv_rrect(lx - 5, 3, 11, 5, 1, on ? c : T_RAISE, T_BG);
        if (on) {
            int32_t fx = dpx[t] >> 4, fy = dpy[t] >> 4, sw = fsin(v->beat_phase / 2u + t * 170u, 6);
            uint32_t a = 3u + peak[t] * 3u / 255u + (v->ms - hit_ms[t] < 120u ? 4u : 0u);
            beam(lx, 8, 4, fx + sw, fy - 2, 34, c, a);
            pool(fx + sw, fy, 18, c, a);
        }
    }
    for (t = 0; t < TB_TRACKS; t++) {                  /* the lasers: the lead's and the synth's notes */
        const tb_lane_t *l = &v->lane[t];
        uint32_t age = v->ms - hit_ms[t];
        if ((l->icon != TB_ICON_LEAD && l->icon != TB_ICON_STAB && l->icon != TB_ICON_HOOVER) || !plays(v, t) || age > 240u)
            continue;
        {
            int32_t ox = l->icon == TB_ICON_LEAD ? 60 : 180, sw = fsin(v->ms / 3u + t * 300u, 40);
            uint16_t c = ux_mix(T_BG, l->icon == TB_ICON_LEAD ? T_REC : T_THEME, 100 - (int32_t)age * 70 / 240);
            for (k = -2; k <= 2; k++)
                cv_line(ox, FLOOR_Y - 4, ox + k * 50 + sw, GR_H - 1, c);
            circle(ox, FLOOR_Y - 4, 1, T_TEXT, 1);
        }
    }
    for (t = 0; t < TB_TRACKS; t++)
        ord[t] = t;
    for (t = 0; t < TB_TRACKS; t++)                    /* (the farthest drawn first) */
        for (i = t + 1; i < TB_TRACKS; i++)
            if (dpy[ord[i]] < dpy[ord[t]]) {
                unsigned x = ord[t];
                ord[t] = ord[i], ord[i] = x;
            }
    for (i = 0; i < TB_TRACKS; i++) {                  /* the dancers */
        const tb_lane_t *l;
        int32_t x, feet, on, hit;
        unsigned d, f;
        t = ord[i], l = &v->lane[t];
        x = dpx[t] >> 4, feet = dpy[t] >> 4, on = plays(v, t), hit = on && v->ms - hit_ms[t] < 100u;
        d = dancer_of(l->icon);
        f = on ? (unsigned)((v->beats % 4u) * 2u + (v->beat_phase >= 512u)) : 0u;
        sprite(d, f, pal_of(d, far[t], on || !v->playing), x, feet - (hit ? (far[t] ? 1 : 2) : 0), (int)(t & 1u) && d != 5u);
        if (l->muted) {                                /* muted: a red cross over its head */
            cv_line(x - 3, feet - 53, x + 3, feet - 47, T_REC), cv_line(x + 3, feet - 53, x - 3, feet - 47, T_REC);
        } else if (l->solo)
            cv_text_in(x - 14, feet - 58, 28, &AF_X, "SOLO", T_ACCENT, T_BG);
        if ((int)t == v->track && !v->mute_layer) {     /* the selected one: a small arrow, bouncing on the beat */
            int32_t ay = feet - 54 - (l->muted || l->solo ? 9 : 0) - (v->playing && v->beat_phase < 300u ? 2 : 0), r;
            for (r = 0; r < 4; r++)
                cv_rect(x - 3 + r, ay + r, 7 - 2 * r, 1, T_ACCENT);
        }
        if (v->mute_layer) {                           /* GLO held: its key's box, lit while it plays */
            char b[3] = {(char)('1' + t), 0, 0};
            cv_rrect(x - 7, feet - 62, 14, 12, 3, l->muted ? T_RAISE : T_THEME, T_BG);
            cv_text_in(x - 7, feet - 62 + CAP_IN(X, 12), 14, &AF_X, b, l->muted ? T_MID : T_INK, l->muted ? T_RAISE : T_THEME);
        }
    }
    for (k = 0; k < 120; k++) {                        /* the smoke, drifting low over the floor */
        int32_t sx = (int32_t)((k * 53u + v->ms / 40u) % 240u), sy = GR_H - 4 - (int32_t)((k * 29u) % 26u);
        blend_px(sx, sy, RGB(120, 110, 110), 3), blend_px(sx + 1, sy, RGB(120, 110, 110), 2);
    }
    if (kick && (v->beats & 1u))                       /* the strobe's edges on every other kick */
        cv_rect(0, 0, 2, GR_H, T_TEXT), cv_rect(238, 0, 2, GR_H, T_TEXT);
}
static void home_skel(const tb_view_t *v);
void tb_ui_home(const tb_view_t *v)
{
    frame_with(v, "HOME", v->mute_layer ? "[GLO] MUTE" : 0, 0, 0, 0);
    home_skel(v);
}

/* ------------------------------------------------------------------ the instruments --- */
/* As JIANT draws each engine on its EDIT page, each kind of track its instrument, in thin lines, played by the sound: the
 * kick a bass drum (its skin rippling, the beater striking), the break a drum kit (the cymbals shaking, the drums
 * jumping), a bass a speaker cabinet (its cone pumping), a lead or a stab a keyboard (its keys going down with the
 * notes), a hoover a vacuum cleaner (its hose swaying; the sound is named after it), the sample a sampler's pads (lit as
 * it plays). Drawn in the panel's rows from y0, h tall */
enum { IN_DRUM, IN_KIT, IN_CAB, IN_KEYS, IN_HOOVER, IN_PADS };
static unsigned instrument_of(const tb_view_t *v)
{
    const tb_lane_t *l = &v->lane[v->track];
    if (l->machine == TB_M_KICK)
        return IN_DRUM;
    if (l->machine == TB_M_BREAK || l->machine == TB_M_909)
        return IN_KIT;
    if (l->machine == TB_M_SAMPLE)
        return IN_PADS;
    if (l->machine == TB_M_ACID || (v->family && !strcmp(v->family, "BASS")))
        return IN_CAB;
    if (v->family && !strcmp(v->family, "HOOVER"))
        return IN_HOOVER;
    return IN_KEYS;
}
static void ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t k, px = cx + rx, py = cy;
    for (k = 1; k <= 48; k++) {
        int32_t x = cx + fsin((unsigned)k * 1024u / 48u + 256u, rx), y = cy + fsin((unsigned)k * 1024u / 48u, ry);
        cv_line(px, py, x, y, c);
        px = x, py = y;
    }
}
static void instrument_lines(const tb_view_t *v, int32_t y0, int32_t h)
{
    const tb_lane_t *l = &v->lane[v->track];
    uint32_t age = v->ms - hit_ms[v->track];
    int32_t hit = l->active || age < 90u, lv = peak[v->track], cx = 120, cy = y0 + h / 2, k;
    uint16_t c = T_THEME, c2 = T_K(1), hi = hit ? T_TEXT : T_THEME;
    listen(v);
    switch (instrument_of(v)) {
    case IN_DRUM: {                                      /* the bass drum, from the front; the pedal under it */
        int32_t r = h / 2 - 6, rip = hit ? (int32_t)(90u - (age > 90u ? 90u : age)) / 18 : 0;
        circle(cx, cy - 2, r, hi, 0);
        circle(cx, cy - 2, r - 3, c, 0);
        for (k = 0; k < 10; k++) {                       /* its lugs round the rim */
            int32_t a = k * 1024 / 10, x = cx + fsin((unsigned)a, r + 1), y = cy - 2 + fsin((unsigned)a + 256u, r + 1);
            cv_rect(x - 1, y - 1, 3, 3, c2);
        }
        circle(cx, cy - 2, r / 2 + rip, hit ? T_ACCENT : T_LINE, 0);   /* the skin rippling */
        if (rip > 1)
            circle(cx, cy - 2, r / 3 + rip / 2, T_ACCENT, 0);
        cv_text_in(cx - 30, cy - 8, 60, &AF_X, "RAVE FM", hit ? T_TEXT : T_MID, T_BG);
        {                                                /* the beater: back, then on the skin */
            int32_t bx = cx + (hit ? 0 : 10), by = cy + r - 14;
            cv_line(cx + 26, y0 + h - 2, bx, by, T_MID);
            circle(bx, by, 3, hit ? T_ACCENT : T_MID, 1);
            cv_rect(cx + 16, y0 + h - 3, 22, 2, T_MID);
        }
        break;
    }
    case IN_KIT: {                                       /* the kit: kick, snare, toms, hi-hat, crash */
        int32_t sh = hit ? fsin(v->ms * 3u, 3) : 0, j = hit ? 2 : 0;
        circle(cx, cy + 24, 26, hi, 0);                  /* the kick */
        circle(cx, cy + 24, 8, T_LINE, 0);
        ellipse(cx - 28, cy - 8 - j / 2, 14, 5, c);       /* the toms on top */
        cv_line(cx - 42, cy - 8, cx - 42, cy + 2, c), cv_line(cx - 14, cy - 8, cx - 14, cy + 2, c);
        ellipse(cx + 28, cy - 8 - j / 2, 14, 5, c);
        cv_line(cx + 14, cy - 8, cx + 14, cy + 2, c), cv_line(cx + 42, cy - 8, cx + 42, cy + 2, c);
        ellipse(cx - 62, cy + 14 - j, 16, 5, hi);         /* the snare */
        cv_line(cx - 78, cy + 14, cx - 78, cy + 22, c), cv_line(cx - 46, cy + 14, cx - 46, cy + 22, c);
        cv_line(cx - 62, cy + 22, cx - 62, y0 + h - 2, T_MID);
        cv_line(cx - 100, cy - 26 + sh, cx - 78, cy - 26 - sh, c2);   /* the hi-hat */
        cv_line(cx - 100, cy - 23, cx - 78, cy - 23, c2);
        cv_line(cx - 89, cy - 23, cx - 89, y0 + h - 2, T_MID);
        cv_line(cx + 66, cy - 40 - sh, cx + 104, cy - 34 + sh, c2);  /* the crash */
        cv_line(cx + 85, cy - 37, cx + 85, y0 + h - 2, T_MID);
        if (hit)
            for (k = 0; k < 3; k++)
                cv_line(cx + 92 + k * 4, cy - 46 - k * 3, cx + 96 + k * 4, cy - 50 - k * 3, T_ACCENT);
        break;
    }
    case IN_CAB: {                                       /* the speaker cabinet, its cone pumping */
        int32_t w = 100, hh = h - 8, x = cx - w / 2, y = y0 + 4, p = lv * 6 / 255 + (hit ? 3 : 0);
        cv_frame(x, y, w, hh, hi);
        cv_frame(x + 3, y + 3, w - 6, hh - 6, T_LINE);
        circle(cx, y + hh / 2 + 10, 30 + p / 2, c, 0);
        circle(cx, y + hh / 2 + 10, 22 + p, c2, 0);
        circle(cx, y + hh / 2 + 10, 7 + p / 2, hi, 1);
        circle(cx, y + 20, 9, c, 0);
        circle(cx, y + 20, 3, T_MID, 1);
        for (k = 0; k < 4; k++)                          /* the sound coming out, either side */
            if (lv > k * 50) {
                cv_line(x - 10 - k * 8, cy - 14 + k * 2, x - 10 - k * 8, cy + 14 - k * 2, heat_mix((uint32_t)(220 - k * 40)));
                cv_line(x + w + 9 + k * 8, cy - 14 + k * 2, x + w + 9 + k * 8, cy + 14 - k * 2, heat_mix((uint32_t)(220 - k * 40)));
            }
        break;
    }
    case IN_HOOVER: {                                    /* the hoover: a vacuum cleaner, its hose swaying with the sound */
        int32_t bx = 52, by = y0 + h - 30, sw = fsin(v->ms / 2u, 6 + lv / 40);
        cv_rrect(bx - 30, by - 22, 64, 30, 10, c, T_BG);
        cv_rrect(bx - 28, by - 20, 60, 26, 9, T_BG, c);
        circle(bx - 16, by + 10, 7, hi, 0), circle(bx + 20, by + 10, 7, hi, 0);
        cv_text_in(bx - 30, by - 13, 64, &AF_X, "HOOVER", hit ? T_TEXT : T_MID, T_BG);
        {
            int32_t px = bx + 34, py = by - 8;
            for (k = 1; k <= 12; k++) {                  /* the hose */
                int32_t x = bx + 34 + k * 9, y = by - 8 - fsin((unsigned)k * 60u, 22) + (sw * k) / 12;
                cv_line(px, py, x, y, k & 1 ? c2 : c);
                px = x, py = y;
            }
            cv_line(px, py, px + 12, py + 30, c);        /* the wand, the floor head */
            cv_rect(px + 4, py + 30, 22, 4, hi);
            if (hit)
                for (k = 0; k < 4; k++)
                    cv_pset(px + 6 + k * 5, py + 26 - (int32_t)(age / 20u) - k * 3, T_ACCENT);
        }
        break;
    }
    case IN_PADS: {                                      /* the sampler: a screen, sixteen pads lit as it plays */
        int32_t x = cx - 60, y = y0 + 6;
        cv_frame(x, y, 120, h - 12, T_LINE);
        cv_frame(x + 8, y + 6, 104, 22, c);
        for (k = 0; k < 100; k++) {
            int32_t a = l->wave[k * TB_WAVE_W / 100] * 9 / 255;
            cv_rect(x + 10 + k, y + 17 - a, 1, 2 * a + 1, k * TB_WAVE_W / 100 < l->pos ? c2 : T_MID);
        }
        for (k = 0; k < 16; k++) {
            int32_t px = x + 10 + (k % 4) * 26, py = y + 36 + (k / 4) * (h - 56) / 4, on = hit && (int32_t)((v->beats * 5u + (unsigned)k * 7u) % 16u) < 3;
            cv_rrect(px, py, 22, (h - 56) / 4 - 3, 3, on ? T_ACCENT : T_SURF, T_BG);
        }
        break;
    }
    default: {                                           /* a keyboard: its keys going down with the notes */
        int32_t x = 10, y = y0 + 18, kw = 14, n = 16, kh = h - 30;
        int32_t lit = hit ? (int32_t)((v->beats * 3u + v->ms / 120u) % (unsigned)n) : -1;
        cv_frame(x - 4, y - 14, kw * n + 8, kh + 18, T_LINE);
        for (k = 0; k < 4; k++)                          /* its knobs */
            circle(x + 8 + k * 18, y - 7, 3, k == 0 ? c2 : c, 0);
        cv_rect(x + 90, y - 10, 60, 6, T_SURF);
        for (k = 0; k < n; k++) {
            int32_t down = k == lit || (hit && instrument_of(v) == IN_KEYS && v->family && !strcmp(v->family, "STAB") && (k == (lit + 4) % n || k == (lit + 7) % n));
            cv_frame(x + k * kw, y + (down ? 2 : 0), kw + 1, kh, down ? T_ACCENT : T_TEXT);
            if (down)
                cv_rect(x + k * kw + 1, y + 3, kw - 1, kh - 2, ux_mix(T_BG, T_ACCENT, 40));
        }
        for (k = 0; k < n - 1; k++)                      /* the black keys */
            if (k % 7 != 2 && k % 7 != 6)
                cv_rect(x + k * kw + kw - 4, y, 9, kh * 3 / 5, T_SURF), cv_frame(x + k * kw + kw - 4, y, 9, kh * 3 / 5, T_MID);
        break;
    }
    }
    if (l->muted)
        cv_text_in(cx - 30, y0 + 2, 60, &AF_X, "MUTE", T_REC, T_BG);
}

/* ------------------------------------------------------------------ the line art --- */
/* Maxime's drawings (tools/gen_art.py) and HOME's skeletons (tools/gen_skel.py): two inks, 2 bits each a pixel, in the
 * palette's colours (RFM_ART_INK: a warm and a cool one per palette), brightened by the sound */
#include "rfm_art.h"
#include "rfm_skel.h"
static uint16_t add565(uint16_t a, uint16_t b)          /* a + b, each channel held at its top */
{
    uint32_t r = (a >> 11) + (b >> 11), g = ((a >> 5) & 63u) + ((b >> 5) & 63u), bl = (a & 31u) + (b & 31u);
    return (uint16_t)((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (bl > 31u ? 31u : bl));
}
static uint16_t dim565(uint16_t c, uint32_t k)          /* c x k / 256 */
{
    return (uint16_t)((((c >> 11) * k >> 8) << 11) | ((((c >> 5) & 63u) * k >> 8) << 5) | ((c & 31u) * k >> 8));
}
/* the 16 colours of a drawing: its halo ink at level w (0..3), its line ink at level c, the third ink where both are;
 * (the stronger of the two; both as strong: the third); bright 0..256 (256: the inks as they are, more: past them, towards white) */
static void art_lut(uint16_t lut[16], const uint16_t ink[3], uint32_t bright)
{
    unsigned w, c;
    uint32_t over = bright > 256u ? bright - 256u : 0u, b = bright > 256u ? 256u : bright;
    for (w = 0; w < 4u; w++)
        for (c = 0; c < 4u; c++) {
            unsigned lv = w > c ? w : c;
            uint16_t x = dim565(w && w == c ? ink[2] : w > c ? ink[0] : ink[1], lv * b / 3u);   /* (the stronger ink; even: the third) */
            if (over && lv)
                x = ux_mix(x, 0xFFFFu, (int32_t)(over * lv / 12u));
            lut[w << 2 | c] = x;
        }
}
/* a drawing at (x0, y0), w x h, its run-length unpacked as it goes (the column and the row counted along, no
 * division a pixel): flip mirrored; split: a column from which on it is drawn dy2 lower (0: none); roff: each band of
 * h / nb rows moved sideways (0: none); only the pixels with (value & mask) */
static void art_draw(const uint8_t *r, int32_t w, int32_t h, int32_t x0, int32_t y0, int flip, const uint16_t *lut,
                     int32_t split, int32_t dy2, const int8_t *roff, int32_t nb, unsigned mask)
{
    int32_t sx = 0, sy = 0, dx = roff ? roff[0] : 0;
    if (!split && !roff && x0 >= 0 && x0 + w <= (int32_t)cv_w) {   /* (v25) HOME's skeletons: the drawing whole inside
                                                       *  the canvas's width, no split, no bands - its row found once a
                                                       *  row, a piece stored straight (the same pixels, the same order) */
        int32_t yy = y0 + cv_oy;
        uint16_t *row = (uint32_t)yy < cv_h ? cv_px + (uint32_t)yy * cv_w + x0 : 0;
        while (sy < h) {
            int32_t run = (*r >> 4) + 1, c = *r & 15;
            r++;
            if (c & (int32_t)mask) {
                uint16_t sc = swap16(lut[c]);
                while (run > 0) {
                    int32_t seg = w - sx < run ? w - sx : run, k;
                    if (row) {
                        uint16_t *q = row + (flip ? w - sx - seg : sx);
                        for (k = 0; k < seg; k++)
                            q[k] = sc;
                    }
                    run -= seg, sx += seg;
                    if (sx == w) {
                        sx = 0, yy++;
                        if (++sy >= h)
                            break;
                        row = (uint32_t)yy < cv_h ? cv_px + (uint32_t)yy * cv_w + x0 : 0;
                    }
                }
            } else {
                sx += run;
                while (sx >= w && sy < h) {
                    sx -= w, yy++, sy++;
                    row = (uint32_t)yy < cv_h ? cv_px + (uint32_t)yy * cv_w + x0 : 0;
                }
            }
        }
        return;
    }
    while (sy < h) {
        int32_t run = (*r >> 4) + 1, c = *r & 15;
        r++;
        if (!(c & (int32_t)mask)) {                   /* (nothing to draw: the run skipped at once, rows and all) */
            sx += run;
            while (sx >= w && sy < h) {
                sx -= w, sy++;
                if (roff && sy < h)
                    dx = roff[sy * nb / h];
            }
            continue;
        }
        {                                             /* (v25: a run a row's piece at a time, filled - not a pixel at
                                                       *  a time, each clipped: the same pixels, in the same order) */
            uint16_t sc = swap16(lut[c]);
            while (run > 0 && sy < h) {
                int32_t seg = w - sx < run ? w - sx : run, yy, a, b;
                if (split && sx < split && sx + seg > split)
                    seg = split - sx;                 /* (the split's column: the rest lower, a piece of its own) */
                yy = y0 + sy + (split && sx >= split ? dy2 : 0) + cv_oy;
                if ((uint32_t)yy < cv_h) {
                    a = flip ? x0 + dx + w - sx - seg : x0 + dx + sx;
                    b = a + seg;
                    a = a < 0 ? 0 : a;
                    b = b > (int32_t)cv_w ? (int32_t)cv_w : b;
                    if (b - a > 6)
                        cv_fill16(&cv_px[(uint32_t)yy * cv_w + (uint32_t)a], (uint32_t)(b - a), sc);
                    else {                            /* (a short piece: stored straight, no call) */
                        uint16_t *q = &cv_px[(uint32_t)yy * cv_w];
                        for (; a < b; a++)
                            q[a] = sc;
                    }
                }
                run -= seg, sx += seg;
                if (sx == w) {
                    sx = 0, sy++;
                    if (roff && sy < h)
                        dx = roff[sy * nb / h];
                }
            }
        }
    }
}
static void art_blit(const uint8_t *r, int32_t w, int32_t h, int32_t x0, int32_t y0, int flip, const uint16_t *lut,
                     int32_t split, int32_t dy2)
{
    art_draw(r, w, h, x0, y0, flip, lut, split, dy2, 0, 1, 15u);
}
static void art_inks(uint16_t ink[3])
{
    unsigned p = ux_pal < sizeof UI_ART_INK / sizeof UI_ART_INK[0] ? ux_pal : 0u;
    ink[0] = UI_ART_INK[p][0], ink[1] = UI_ART_INK[p][1], ink[2] = UI_ART_INK[p][2];
}

/* the background, as JIANT's stars: a field of them flying at us out of the middle, in the palette's inks; they fly a
 * stretch every beat (the tempo), much faster for a moment on each note (warp: the near ones drawn as streaks) */
#define NSTAR 36
static uint32_t star_go, star_ms;                     /* how far they have flown (Q8), when last drawn */
static uint32_t star_hash(uint32_t i) { i = (i ^ 61u) ^ (i >> 16); i *= 9u; i ^= i >> 4; i *= 0x27d4eb2du; return i ^ (i >> 15); }
static void stars(const tb_view_t *v, int32_t cx, int32_t cy, uint32_t warp, const uint16_t ink[3])
{
    uint32_t dt = star_ms ? v->ms - star_ms : 0u, sp, i;
    dt = dt > 100u ? 100u : dt;
    star_ms = v->ms;
    sp = v->playing ? (uint32_t)v->bpm10 * 12u / 10u : 40u;   /* (a beat ~ 320 deep at any tempo) */
    sp = sp * (256u + warp * 5u) / 256u;
    star_go += dt * sp / 20u;
    for (i = 0; i < NSTAR; i++) {
        uint32_t h = star_hash(i + 1u), z = 4096u - (((star_go >> 4) + (h >> 20)) & 4095u);
        int32_t x = (int32_t)(h & 2047u) - 1024, y = (int32_t)((h >> 11) & 1023u) - 512, sx, sy;
        uint32_t lv;
        uint16_t c;
        if (z < 96u)
            continue;
        sx = cx + x * 40 / (int32_t)z, sy = cy + y * 40 / (int32_t)z;
        lv = (4096u - z) * 256u / 4096u;                  /* (near: brighter) */
        c = dim565(i % 3u ? ink[1] : ink[2], 40u + lv * 200u / 256u);
        if (warp > 40u && z < 2200u && z > 420u) {       /* (warping: the near ones streak back to where they were) */
            uint32_t z2 = z + 260u + warp * 3u;
            cv_line(cx + x * 40 / (int32_t)z2, cy + y * 40 / (int32_t)z2, sx, sy, dim565(c, 150u));
        }
        cv_pset(sx, sy, c);
        if (z < 900u)
            cv_pset(sx + 1, sy, c), cv_pset(sx, sy + 1, c), cv_pset(sx + 1, sy + 1, c);
    }
}

/* the instrument: the kind of sound -> its drawing */
static unsigned art_of(const tb_view_t *v)
{
    const tb_lane_t *l = &v->lane[v->track];
    const char *f = v->family;
    if (l->machine == TB_M_KICK)
        return RFM_ART_KICK;
    if (l->machine == TB_M_BREAK || l->machine == TB_M_909)
        return RFM_ART_BREAK;
    if (l->machine == TB_M_SAMPLE)
        return RFM_ART_SAMPLE;
    if (l->machine == TB_M_ACID)
        return RFM_ART_ACID;
    if (f) {
        if (!strcmp(f, "BASS"))
            return RFM_ART_BASS;
        if (!strcmp(f, "HOOVER"))
            return RFM_ART_HOOVER;
        if (!strcmp(f, "STAB"))
            return RFM_ART_STAB;
        if (!strcmp(f, "PAD") || !strcmp(f, "ORGAN"))
            return RFM_ART_PAD;
        if (!strcmp(f, "PLUCK"))
            return RFM_ART_PLUCK;
        if (!strcmp(f, "FX"))
            return RFM_ART_FX;
    }
    return RFM_ART_LEAD;
}
/* where each drawing's eyes are (its pixels; 0: none) */
static const uint8_t ART_EYES[RFM_NART][4] = {
    {39, 57, 60, 57}, {20, 20, 27, 20}, {59, 65, 84, 65}, {38, 15, 60, 15}, {45, 60, 72, 60}, {42, 47, 74, 47},
    {32, 47, 71, 47}, {38, 43, 60, 43}, {65, 57, 78, 57}, {48, 38, 78, 38}, {0, 0, 0, 0}};
static uint32_t gap_ms[TB_TRACKS], last_hit[TB_TRACKS];   /* the time between its notes (smoothed): how hard it plays */
static void glow(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t dx, dy;
    for (dy = -r - 2; dy <= r + 2; dy++)
        for (dx = -r - 2; dx <= r + 2; dx++) {
            int32_t d2 = dx * dx + dy * dy;
            if (d2 <= r * r)
                cv_pset(cx + dx, cy + dy, d2 * 3 <= r * r ? 0xFFFFu : c);
            else if (d2 <= (r + 2) * (r + 2))
                blend_px(cx + dx, cy + dy, c, 6u);
        }
}
/* The instrument: its drawing, lit by the sound. Each note (each hit, for the drums: so on the tempo) flashes it and
 * jolts it up; the louder, the brighter; its eyes burn on the notes. The faster it is played, the angrier: the eyes
 * bigger and whiter, the brows frowning down over them, the inks running hot, the drawing shaking on the hits. The
 * two skeletons (the sample) dance in turn, one up on a hit, the other on the next. */
static void instrument(const tb_view_t *v, int32_t y0, int32_t h)
{
    const tb_lane_t *l = &v->lane[v->track];
    unsigned a = art_of(v), t = (unsigned)v->track;
    int32_t w = RFM_ART_WH[a][0], ah = RFM_ART_WH[a][1], x0 = (240 - w) / 2, top = y0 + (h - ah) / 2;
    uint32_t age, flash, rage, bright;
    uint16_t lut[16], ink[3], warm, cool;
    int32_t jolt, shake = 0;
    listen(v);
    if (hit_ms[t] != last_hit[t]) {                    /* a new note: how long since the last */
        uint32_t g = last_hit[t] ? hit_ms[t] - last_hit[t] : 1000u;
        g = g > 1000u ? 1000u : g;
        gap_ms[t] = gap_ms[t] ? (gap_ms[t] * 3u + g) / 4u : g;
        last_hit[t] = hit_ms[t];
    }
    age = hit_ms[t] ? v->ms - hit_ms[t] : 100000u;
    if (age > 2000u)
        gap_ms[t] = 1000u;                             /* (silent: calm again) */
    flash = age < 180u ? 180u - age : 0u;              /* 180 .. 0 after a note */
    rage = gap_ms[t] && gap_ms[t] < 500u ? (500u - gap_ms[t]) * 256u / 400u : 0u;   /* 0 calm .. 256 frantic (1/8 s) */
    rage = rage > 256u ? 256u : rage;
    bright = 120u + flash * 136u / 180u + (uint32_t)peak[t] * 40u / 255u + (flash ? rage * 70u / 256u : 0u);
    jolt = -(int32_t)(flash * (5u + rage / 64u) / 180u);     /* (a jump up on each note, falling back) */
    if (flash > 50u)                                   /* (shaking while it rings, harder played harder) */
        shake = ((v->ms / 33u) & 1u ? 1 : -1) * (int32_t)(1u + flash / 70u + rage / 90u);
    art_inks(ink);
    warm = ink[0], cool = ink[1];
    if (rage > 100u)                                   /* (angry: the line ink takes the halo's) */
        ink[1] = cool = ux_mix(cool, warm, (int32_t)((rage - 100u) * 60u / 156u));
    if (l->muted)
        bright = 70u, flash = 0, rage = 0, jolt = 0, shake = 0;
    art_lut(lut, ink, bright);
    stars(v, 120, top + ah / 2, flash * (64u + rage) / 180u, ink);
    if (a == RFM_ART_SAMPLE)                           /* the two skeletons: one jumps, then the other */
        art_blit(RFM_ART_RLE[a], w, ah, x0, top, 0, lut, w / 2, flash > 40u ? ((last_hit[t] / 97u) & 1u ? -6 : 0) : 0);
    else if (flash > 110u) {                           /* the note's first moment: the glitch, the inks torn apart */
        int8_t roff[8];
        uint16_t lw[16], lc[16];
        int32_t k, sp = 2 + (int32_t)(flash - 110u) / 18 + (int32_t)rage / 80;
        uint32_t hs = last_hit[t] * 2654435761u;
        for (k = 0; k < 8; k++, hs = hs * 1103515245u + 12345u)
            roff[k] = (int8_t)((int32_t)((hs >> 16) % (uint32_t)(2 * sp + 1)) - sp);
        for (k = 0; k < 16; k++) {                      /* (the halo alone, the line alone) */
            lw[k] = (k >> 2) ? lut[k & 12] : 0;
            lc[k] = (k & 3) ? lut[k & 3] : 0;
        }
        for (k = 0; k < 16; k++)
            if (!lw[k])
                lw[k] = lc[k];
        art_draw(RFM_ART_RLE[a], w, ah, x0 + shake - sp / 2, top + jolt, 0, lw, 0, 0, roff, 8, 15u);
        for (k = 0; k < 8; k++)
            roff[k] = (int8_t)(-roff[k] / 2);
        art_draw(RFM_ART_RLE[a], w, ah, x0 + shake + sp / 2, top + jolt, 0, lc, 0, 0, roff, 8, 3u);   /* the line ink, torn the other way */
    } else
        art_blit(RFM_ART_RLE[a], w, ah, x0 + shake, top + jolt, 0, lut, 0, 0);
    if (ART_EYES[a][0] && !l->muted) {                 /* the eyes, and the brows when it is played hard */
        int32_t k, r = flash ? 1 + (int32_t)(flash / 70u) + (int32_t)(rage / 110u) : 0;
        uint16_t ec = ux_mix(warm, 0xFFFFu, (int32_t)(rage * 50u / 256u));
        for (k = 0; k < 2; k++) {
            int32_t ex = x0 + shake + ART_EYES[a][k * 2], ey = top + jolt + ART_EYES[a][k * 2 + 1];
            if (r)
                glow(ex, ey, r, ec);
            if (rage > 90u) {                          /* the brow: slanting down to the nose, steeper the angrier */
                int32_t s = k ? -1 : 1, d = 1 + (int32_t)(rage / 64u);
                cv_line(ex - 5 * s, ey - 6 - d, ex + 3 * s, ey - 5 + d / 2, warm);
                cv_line(ex - 5 * s, ey - 7 - d, ex + 3 * s, ey - 6 + d / 2, warm);
            }
        }
    }
    if (l->muted)
        cv_text_in(120 - 30, y0 + 2, 60, &AF_X, "MUTE", T_REC, T_BG);
}

/* ------------------------------------------------------------------ HOME: the two skeletons --- */
/* Two skeletons in tracksuits, facing each other, dancing the hakken (8 frames on two beats, on the tempo). The left one
 * dances when the kick plays; the right one too, but only when a melody plays as well (the bass, the lead, the synth).
 * Both flash on the kick; one not dancing stands still, dim. In the palette's inks. */
static int melody_now(const tb_view_t *v)
{
    unsigned t;
    for (t = 0; t < TB_TRACKS; t++) {
        const tb_lane_t *l = &v->lane[t];
        if ((l->machine == TB_M_SYNTH || l->machine == TB_M_ACID) && plays(v, t))
            return 1;
    }
    return 0;
}
static int kick_plays(const tb_view_t *v)
{
    unsigned t;
    for (t = 0; t < TB_TRACKS; t++)
        if (v->lane[t].icon == TB_ICON_KICK && plays(v, t))
            return 1;
    return 0;
}
static uint32_t kick_ms;
/* HOME: the skeletons dance the hakken (tools/gen_home.py: Maxime's drawing posed, 16 frames: a step of the front leg,
 * a step of the back one, 8 frames each: knee up, kick out, stamp, stand). A step is set off by a note, played or
 * typed on the keys (with or without PLAY): the left one steps on every kick; the right one on every kick while a
 * melody plays (the bass, the lead, the synth), or on the melody's notes when no kick plays. A step lasts most of a
 * beat (PLAY: the tempo's; else the time between the notes), then the dancer stands, stamped, till the next. */
static uint32_t dn_step[2], dn_dur[2], dn_seen[TB_TRACKS], dn_kick, dn_mel, dn_gap = 300u;
static uint8_t dn_leg[2];
static void dn_go(unsigned d, uint32_t now, uint32_t dur, uint32_t gap)
{
    if (dn_step[d] && now - dn_step[d] < dn_dur[d] / 2u)
        return;                                          /* (notes too close: let it finish half its step) */
    dn_leg[d] ^= 1u;
    dn_step[d] = now ? now : 1u;
    dn_dur[d] = dur ? dur : gap;
}
static void home_skel(const tb_view_t *v)
{
    uint16_t ink[3], warm, cool, lut[16];
    int32_t floor_y = GR_H - 4;
    uint32_t now = v->ms, beat = v->bpm10 ? 600000u / v->bpm10 : 333u, dur = 0, t;
    int kick_hit = 0, mel_hit = 0, kick_on, mel_on, d;
    listen(v);
    for (t = 0; t < TB_TRACKS; t++) {                    /* the notes since the last frame */
        const tb_lane_t *l = &v->lane[t];
        if (hit_ms[t] == dn_seen[t] || l->muted)
            continue;
        dn_seen[t] = hit_ms[t];
        if (l->icon == TB_ICON_KICK)
            kick_hit = 1;
        else if (l->machine == TB_M_SYNTH || l->machine == TB_M_ACID)
            mel_hit = 1;
    }
    if (kick_hit) {
        if (dn_kick && now - dn_kick < 1000u)
            dn_gap = now - dn_kick;
        dn_kick = now ? now : 1u;
    }
    if (mel_hit)
        dn_mel = now ? now : 1u;
    kick_on = dn_kick && now - dn_kick < 1500u;
    mel_on = dn_mel && now - dn_mel < 1500u;
    if (v->playing)
        dur = beat * 9u / 10u;
    else
        dur = dn_gap < 160u ? 160u : dn_gap > 360u ? 360u : dn_gap;
    if (dur < 120u)
        dur = 120u;
    if (kick_hit)
        dn_go(0, now, dur, dn_gap);
    if ((kick_hit && mel_on) || (mel_hit && !(dn_kick && now - dn_kick < 700u)))   /* (no kick lately: on the notes) */
        dn_go(1, now, dur, dn_gap);
    art_inks(ink);
    warm = ink[0], cool = ink[1];
    {
        uint32_t ka = dn_kick ? now - dn_kick : 100000u;
        stars(v, 120, GR_H / 3, ka < 150u ? 150u - ka : 0u, ink);
    }
    cv_rect(10, floor_y + 1, 220, 1, ux_mix(T_BG, warm, 30));             /* the floor */
    for (d = 0; d < 2; d++) {
        uint32_t e = dn_step[d] ? now - dn_step[d] : 100000u, fl = e < 150u ? 150u - e : 0u;
        int on = e < 1500u;
        unsigned f = dn_step[d] ? dn_leg[d] * (RFM_SKEL_NFR / 2u) + (e < dn_dur[d] ? e * (RFM_SKEL_NFR / 2u) / dn_dur[d] : 6u)
                                : RFM_SKEL_IDLE;
        int32_t w = RFM_SKEL_W[d], x = d == 0 ? 120 - 2 - w : 122;
        if (on)                                          /* its light on the floor, flaring on the step */
            pool(x + w / 2, floor_y, 26 + (int32_t)fl / 15, d ? cool : warm, 3u + fl / 40u);
        art_lut(lut, ink, on ? 200u + fl * 110u / 150u : 80u);
        art_blit(RFM_SKEL_RLE[d] + RFM_SKEL_OFF[d][f % RFM_SKEL_NFR], w, RFM_SKEL_H, x, floor_y - RFM_SKEL_H + 1, 0, lut, 0, 0);
    }
    if (kick_on && dn_kick && now - dn_kick < 60u)       /* the strobe's edges on the kick */
        cv_rect(0, 0, 2, GR_H, warm), cv_rect(238, 0, 2, GR_H, warm);
}

/* (no floating point on the FM-1 build: the drawings in fixed point, Q10 = 1.0) */
static int q_sin(int u) { return tb_sin((uint32_t)u << 22) / 32; }     /* u: 1024 a turn; Q10 */
static int q_tri(int u) { return u < 512 ? u * 4 - 1024 : 3072 - u * 4; }       /* -1 .. 1 .. -1 */
#if TB_HAS_PICS
/* EDIT's picture: the oscillator's wave (one cycle and a bit), the filter's curve, the break and its slices */
static void edit_picture(const tb_view_t *v, int x0, int y0, int w, int h)
{
    const uint8_t *p = v->edit_p;
    int i, px = 0, py = 0;
    if (v->edit_kind == 1) {                            /* the wave: two cycles, its sub under, the detune as a shadow */
        unsigned wave = p[0];
        for (i = 0; i <= 96; i++) {
            int u = (i % 48) * 1024 / 48, y, t, q, fr;
            switch (wave) {
            case 1: y = u < 512 ? 819 : -819; break;
            case 2: y = u < 307 ? 819 : -819; break;
            case 5: t = q_tri(u); y = (t * 1536 / 1024 - t * t / 1024 * t / 1024 / 2) * 819 / 1024; break;
            case 6: t = q_tri(u); y = t * 614 / 1024 + ((u < 256 || (u >= 512 && u < 768)) ? 256 : -256); break;
            case 7: case 8:
                t = q_tri(u);
                y = t > 0 ? 819 - (1024 - t) * (1024 - t) / 1024 * 819 / 1024 * (64 + p[1]) / 64 : -819 + (1024 + t) * (1024 + t) / 1024 * 819 / 1024;
                break;
            case 4: y = (u * 2 - 1024) / 2 + (((u + 379) & 1023) * 2 - 1024) * 307 / 1024; break;
            case 9: q = u * (1024 + p[1] * 1024 / 18) / 1024, fr = q & 1023, y = 819 - fr * 1638 / 1024; break;   /* SYNC */
            case 10:
                q = u * 3 + p[1] * 16, fr = q & 1023;
                y = 819 * (1024 - u) / 1024 * (fr < 512 ? 1 : -1) * (1024 - 2 * fr * fr / 1024) / 1024;
                break;
            case 11: q = u * (1024 + p[1] * 1024 / 42) / 1024, fr = q & 1023, y = (819 - u * 1638 / 1024) * q_tri(fr) / 1024; break;
            case 12: y = (u < 512 ? 461 : -461) + (358 - ((u * 2) & 1023) * 717 / 1024); break;   /* STAB */
            case 13: y = (q_sin(u) * 1229 / 1024 + q_sin(2 * u) * 614 / 1024 + q_sin(4 * u) * 410 / 1024) * 307 / 1024; break;
            case 14:
                q = q_sin(u) * (32 + p[1]) / 32;
                while (q > 1024 || q < -1024)
                    q = q > 1024 ? 2048 - q : -2048 - q;
                y = q * 819 / 1024;
                break;
            default: y = 819 - u * 1638 / 1024;
            }
            {
                int x = x0 + i * w / 96, yy = y0 + h / 2 - y * (h / 2 - 6) / 1024;
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
        int dive = (p[2] - 64) * 12 * 256 / 64, who = p[3] * 5 * 256 / 127;   /* (semitones, Q8) */
        int32_t kb = 65536, k1 = 65536, k2 = 65536, rb = 65536, r1 = 65536, r2 = 65536;   /* (Q16) */
        int q;
        gfx_rect(x0, y0 + h / 2, w, 1, gfx_heat(50), 255, GFX_NORMAL);
        for (q = 0; q < 10; q++)                        /* (a step of the drawing: ~10 of the synth's sub-blocks) */
            kb = kb * 169 / 170, k1 = k1 * 399 / 400, k2 = k2 * 29 / 30;
        for (i = 0; i <= 80; i++) {
            int st = (dive * (rb >> 4) + who * (r1 >> 4) - who * (r2 >> 4)) >> 12;   /* (Q8) */
            int c = st / 8, x = x0 + i * w / 80, y;
            c = c > 256 ? 256 : c < -256 ? -256 : c;
            y = y0 + h / 2 - c * (h / 2 - 6) / 256;
            if (i)
                gfx_line(px, py, x, y, 30, gfx_heat(230), 255, GFX_NORMAL);
            px = x, py = y;
            rb = (int32_t)(((uint32_t)rb >> 8) * ((uint32_t)kb >> 8)), r1 = (int32_t)(((uint32_t)r1 >> 8) * ((uint32_t)k1 >> 8));
            r2 = (int32_t)(((uint32_t)r2 >> 8) * ((uint32_t)k2 >> 8));
        }
        gfx_text(&TB_FONT_S, x0 + 2, y0 + 12, "PITCH", DIM, 255, GFX_LEFT);
        gfx_text(&TB_FONT_S, x0 + w - 2, y0 + h - 4, "600 ms", DIM, 255, GFX_RIGHT);
        return;
    }
    if (v->edit_kind == 2) {                            /* the filter: low-pass, its peak at the cutoff */
        int fc = p[4] * 1024 / 127, q = p[5] * 1024 / 127;   /* (Q10) */
        for (i = 0; i <= 64; i++) {
            int r = i * 16 - fc, r2 = r * r / 1024, g = r < 0 ? 1024 : 1024 * 1024 / (1024 + r2 * 120), x, y;
            g += q * 922 / 1024 * 1024 / (1024 + r2 * 900);
            if (g > 1946)
                g = 1946;
            x = x0 + i * w / 64, y = y0 + h - 6 - g * (h - 12) / 1946;
            if (i) {
                gfx_line(px, py, x, y, 30, gfx_heat(225), 255, GFX_NORMAL);
                gfx_rect(px, y, x - px, y0 + h - 6 - y, gfx_heat(70), 60, GFX_NORMAL);
            }
            px = x, py = y;
        }
        if (p[6]) {                                     /* where its envelope takes it */
            int e = fc + p[6] * 512 / 127, xe = x0 + (e > 1024 ? 1024 : e) * w / 1024;
            gfx_line(x0 + fc * w / 1024, y0 + 10, xe, y0 + 10, 18, gfx_heat(160), 255, GFX_NORMAL);
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
    frame(v, v->menu == TB_MENU_EDIT ? "EDIT" : "HOME");
    if (v->menu == TB_MENU_EDIT && v->page_on == 0) {   /* the first page: the instrument */
        instrument(v, 4, GR_H - 8);
        return;
    }
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
    frame(v, "SEQ");
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
    static const char *const SEC[] = {"HOME", "ENV", "LFO", "ARP", "SCALE", "FX", "EDIT", "GLO", "MENU", "SEQ", "SEQ", "SEQ"};
    frame(v, SEC[v->menu < sizeof SEC / sizeof *SEC ? v->menu : 0]);
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
    int sl = e->s * 1024 / 127;                         /* (Q10) */
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
        int lv, u, y, m;
        if (x < xd)
            u = (x - xa) * 1024 / (wa ? wa : 1), m = 1024 - u, lv = 1024 - m * m / 1024;
        else if (x < xs)
            u = (x - xd) * 1024 / (wd ? wd : 1), m = 1024 - u, lv = sl + (1024 - sl) * (m * m / 1024) / 1024 * m / 1024;
        else if (x < xr)
            lv = sl;
        else
            u = (x - xr) * 1024 / (wr ? wr : 1), m = 1024 - u, lv = sl * (m * m / 1024) / 1024 * m / 1024;
        if (x == xe)
            lv = 0;
        y = y1 - (lv * h + 512) / 1024;
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
        uint32_t ph = (uint32_t)(i % 48) * 89478485u;      /* (2^32 / 48 a point) */
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
        int fx = i * 1024 / 63, lo = fx < 307 ? 1024 : fx < 461 ? (461 - fx) * 1024 / 154 : 0;   /* (Q10) */
        int hi = fx > 717 ? 1024 : fx > 563 ? (fx - 563) * 1024 / 154 : 0, md = 1024 - lo - hi, g;
#define EQG(v) (track ? ((v) < 64 ? ((v) - 64) * 16 / 10 : ((v) - 64) / 2) : (v) - 64)
        g = (lo * EQG(p[0]) + md * EQG(p[1]) + hi * EQG(p[2])) / 1024;
#undef EQG
        g = g > 70 ? 70 : g < -70 ? -70 : g;
        y[i] = (int16_t)(g * 32767 / 70);
    }
    gfx_rect(x0, y0 + h / 2, w, 1, gfx_heat(40), 255, GFX_NORMAL);
    curve(x0, y0 + 4, w, h - 8, y, 64, HOT);
    gfx_text(&TB_FONT_S, x0 + 2, y0 + h - 4, "LOW", gfx_heat(90), 255, GFX_LEFT);
    gfx_text(&TB_FONT_S, x0 + w / 2, y0 + h - 4, "MID", gfx_heat(90), 255, GFX_CENTER);
    gfx_text(&TB_FONT_S, x0 + w - 2, y0 + h - 4, "HIGH", gfx_heat(90), 255, GFX_RIGHT);
}

/* FX: this track's chain, a page an effect: its name big, what it does drawn, how much */
/* the FX pages' pictures, as JIANT draws its own: a caption line (what it is, left; how much, right), then the effect
 * drawn across the panel in the thermal colours (bright where it is strong): DRIVE and EXTRA their curve in a box with
 * their words beside it, CHORUS FLANGER PHASER their sweeps, TREMOLO its volume, EQ its three points on the line, REVERB
 * its tail, DELAY its repeats. Off: the caption says it, the drawing dim */
static void fx_caption(const char *l, const char *r, int on)
{
    char a[40], b[24];
    latin(a, sizeof a, l);
    latin(b, sizeof b, r);
    cv_rect(4, 2, 3, 1, T_THEME), cv_rect(9, 2, 3, 1, T_THEME), cv_rect(14, 2, 3, 1, T_RAISE);
    cv_free_text(4, 8, &AF_X, a, on ? T_TEXT : T_MID, T_BG, 150);
    cv_text_r(236, 8, &AF_X, b, on ? T_MID : T_DIM, T_BG);
}
static uint16_t thermal(int32_t i, int32_t n, int on)   /* along a drawing: bright first, cooling */
{
    int32_t d = i * 200 / (n ? n : 1);
    uint16_t c = heat_mix(255u - (uint32_t)(d < 0 ? 0 : d > 255 ? 255 : d));   /* (past the drawing's end: the coolest,
                                                                                * not a wrapped-round heat) */
    return on ? c : ux_mix(T_BG, c, 35);
}
static void fx_wave(int32_t y0, int32_t h, int32_t cyc, int32_t amp, uint32_t ph0, int on, int32_t shape)
{
    int32_t x, py = 0;
    for (x = 0; x < 232; x++) {
        uint32_t ph = ph0 + (uint32_t)x * (uint32_t)cyc * 18512790u;   /* (2^32 / 232 a cycle) */
        int32_t s = lfo_shape(0, ph), y;
        if (shape)                                       /* (the tremolo: sine .. square) */
            s = s + ((ph < 0x80000000u ? 32767 : -32767) - s) * shape / 127;
        y = y0 + h / 2 - s * amp / 32767 * (h / 2 - 2) / 1000;
        if (x)
            cv_line(4 + x - 1, py, 4 + x, y, thermal(x, 232, on));
        py = y;
    }
}
static void menu_fx_j(const tb_view_t *v)
{
    const tb_fxview_t *f = &v->fx;
    int pg = f->page, i, on;
    char a[40], b[24];
    const int32_t gy = 26, gh = 104;                     /* the drawing's rows */
    if (pg == 0 || pg == 5) {                            /* DRIVE, EXTRA */
        unsigned t = f->type;
        on = t != 0;
        snprintf(a, sizeof a, "%s", t ? f->name : "OFF");
        fx_caption(a, pg ? "EXTRA" : "DRIVE", on);
        cv_frame(4, gy, 104, 104, T_LINE);
        cv_line(5, gy + 102, 106, gy + 1, T_RAISE);
        {
            int32_t px = 0, py = 0;
            for (i = 0; i < 100; i++) {                  /* what goes in (across) comes out (up) */
                int32_t xin = (i * 2 - 99) * 1024 / 99, o;
                if (!t)
                    o = xin;
                else if (t <= 5) {
                    int32_t g = 1024 + f->p[0] * 1024 / 127 * (t == 1 ? 6 : t == 2 ? 12 : t == 3 ? 24 : 1);
                    if (t == 4) {
                        int32_t sh = 16 - f->p[0] * 14 / 127 > 6 ? 6 : 16 - f->p[0] * 14 / 127, st = 2048 >> sh;
                        o = st ? (xin / st) * st : xin;
                    } else if (t == 5) {
                        int32_t u = xin * (18 + f->p[0]) / 18 + (f->p[1] - 64) * 16;
                        u = ((u + 1024) % 4096 + 4096) % 4096 - 1024;
                        o = u < 1024 ? u : 2048 - u;
                    } else {
                        int32_t d = xin * g / 1024;
                        o = d > 1024 ? 1024 : d < -1024 ? -1024 : d - d * d / 1024 * d / 1024 / 3 * (t == 1 ? 10 : 3) / 10;
                        o = o > 1024 ? 1024 : o < -1024 ? -1024 : o;
                    }
                    o = xin + (o - xin) * f->p[2] / 127;
                } else
                    o = lfo_shape(0, (uint32_t)i * (1u + f->p[0] / 24u) * 42949673u) / 32 * (10 + f->p[1]) / 140;
                {
                    int32_t x = 6 + i, y = gy + 52 - o * 48 / 1024;
                    if (i)
                        cv_line(px, py, x, y, on ? heat_mix(110u + (uint32_t)i * 140u / 100u) : T_DIM);
                    px = x, py = y;
                }
            }
            if (on)
                circle(px, py, 1, T_TEXT, 1);
        }
        if (on) {                                        /* its words beside it */
            latin(a, sizeof a, f->name);
            cv_text(118, gy + 2, &AF_M, a, T_TEXT);
            for (i = 0; i < 3; i++)
                if (v->knob_name[i + 1]) {
                    snprintf(a, sizeof a, "%s %s", v->knob_name[i + 1], v->knob_value[i + 1]);
                    latin(b, sizeof b, a);
                    cv_free_text(118, gy + 26 + i * 16, &AF_X, b, T_MID, T_BG, 118);
                }
        } else {
            cv_text(118, gy + 2, &AF_M, pg ? "NO EXTRA" : "NO DRIVE", T_MID);
            cv_free_text(118, gy + 26, &AF_X, "KNOB 1 PICKS ONE", T_DIM, T_BG, 118);
        }
        return;
    }
    if (pg >= 1 && pg <= 4) {                            /* CHORUS FLANGER PHASER TREMOLO */
        static const char *const N[4] = {"WIDE, DOUBLED", "JET SWEEP", "SWIRL", "PULSE"};
        on = f->p[0] != 0;
        snprintf(b, sizeof b, "%d%%", f->p[0] * 100 / 127);
        if (v->knob_name[1])
            snprintf(a, sizeof a, "%s \xc2\xb7 %s %s", N[pg - 1], v->knob_name[1], v->knob_value[1]);
        else
            snprintf(a, sizeof a, "%s", N[pg - 1]);
        fx_caption(a, on ? b : "OFF", on);
        cv_rect(4, gy + gh / 2, 232, 1, T_RAISE);
        {
            int32_t amp = 300 + 700 * f->p[0] / 127, cyc = 1 + f->p[1] / 26, dep = 300 + 700 * f->p[2] / 127, k;
            uint32_t run = v->ms * 220000u;              /* (it moves) */
            if (pg == 4)
                fx_wave(gy, gh, 2 + f->p[1] / 18, amp, run, on, f->p[3]);
            else
                for (k = 0; k < (pg == 1 ? 2 : pg == 2 ? 1 + f->p[3] / 43 : 3); k++)
                    fx_wave(gy, gh, cyc + (pg == 3 ? k : 0), amp * dep / 1000 * (pg == 3 ? 10 - k * 3 : 10) / 10,
                            run + (uint32_t)k * (pg == 1 ? 0x80000000u : 0x18000000u), on, 0);
        }
        return;
    }
    if (pg == 6) {                                       /* EQ: three points on the line */
        static const int32_t X[3] = {40, 120, 200};
        static const char *const L[3] = {"LOW", "MID", "HIGH"};
        int32_t y[3], k, px = 4, py, mid = gy + 44;
        on = f->p[0] != 64 || f->p[1] != 64 || f->p[2] != 64;
        snprintf(a, sizeof a, "LOW %s \xc2\xb7 MID %s \xc2\xb7 HIGH %s", v->knob_value[0], v->knob_value[1], v->knob_value[2]);
        fx_caption(a, on ? "ON" : "FLAT", on);
        cv_rect(4, mid, 232, 1, T_RAISE);
        for (k = 0; k < 3; k++)
            y[k] = mid - (f->p[k] - 64) * 38 / 64;
        py = y[0];
        for (k = 4; k < 236; k++) {                      /* (smooth between the points) */
            int32_t s = k < X[0] ? 0 : k >= X[2] ? 2 : k < X[1] ? 0 : 1, yy;
            if (k < X[0] || k >= X[2])
                yy = y[s];
            else {
                int32_t t = (k - X[s]) * 1024 / (X[s + 1] - X[s]), e = (1024 - lfo_shape(0, (uint32_t)t << 21) / 32 * 0 - 0);
                (void)e;
                t = t * t / 1024 * (3072 - 2 * t) / 1024;  /* (smoothstep) */
                yy = y[s] + (y[s + 1] - y[s]) * t / 1024;
            }
            if (k > 4)
                cv_line(px, py, k, yy, on ? T_ACCENT : T_DIM);
            px = k, py = yy;
        }
        for (k = 0; k < 3; k++) {
            cv_rect(X[k] - 2, y[k] - 2, 5, 5, f->p[k] != 64 ? T_ACCENT : T_MID);
            cv_text_in(X[k] - 20, gy + 92, 40, &AF_X, L[k], T_MID, T_BG);
        }
        return;
    }
    if (pg == 7) {                                       /* REVERB: its tail, as loud as the track sends */
        int32_t x, amp = 200 + f->p[0] * 800 / 127, k = 990 + f->p[1] * 9 / 127 - f->p[2] / 40, hgt = gh - 10;
        on = f->p[0] != 0;
        snprintf(b, sizeof b, "%s %s", v->knob_name[0] ? v->knob_name[0] : "", v->knob_value[0]);
        latin(a, sizeof a, f->name ? f->name : "");
        fx_caption(a, b, on);
        for (x = 0; x < 232; x++) {
            int32_t h = amp * hgt / 1000;
            if (h > 0)
                cv_rect(4 + x, gy + gh - h, 1, h, thermal(x, 232, on));
            amp = amp * (f->type == 4 && x > 90 ? 900 : k) / 1000;
        }
        cv_rect(4, gy + gh, 232, 1, T_RAISE);
        return;
    }
    {                                                    /* DELAY: its repeats */
        static const uint8_t Q[7] = {6, 12, 18, 24, 8, 16, 36};
        int32_t gap = Q[f->p[1] % 7] * 3 / 2 + 6, x = 6, k, amp = 1000, fb = f->p[2] * 1000 / 135, hgt = gh - 10;
        on = f->p[0] != 0;
        snprintf(a, sizeof a, "%s \xc2\xb7 %s", v->knob_value[1], f->name ? f->name : "");
        snprintf(b, sizeof b, "%s %s", v->knob_name[0] ? v->knob_name[0] : "", v->knob_value[0]);
        fx_caption(a, b, on);
        for (k = 0; k < 40 && x < 234; k++) {
            int32_t h = amp * hgt / 1000;
            uint16_t c = k == 0 ? (on ? T_TEXT : T_MID) : thermal(k * 24, 232, on);
            cv_rect(x, gy + gh - h, 3, h, c);
            if (f->type == 0 && k & 1)                   /* (PING: every other one the other side, lower) */
                cv_rect(x, gy + gh - h, 3, 1, T_BG);
            x += gap, amp = amp * fb / 1000;
            if (amp < 20)
                break;
        }
        cv_rect(4, gy + gh, 232, 1, T_RAISE);
    }
}

static void menu_fx(const tb_view_t *v)
{
    const tb_fxview_t *f = &v->fx;
    if (f->page != 9) {                                  /* (ROUTE: its own drawing, below) */
        menu_fx_j(v);
        return;
    }
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
    const int ns = 0 * (int)sizeof SHOW, sw = 230 / (int)sizeof SHOW;   /* (the chain: in the tabs now) */
    (void)STEP, (void)sw;
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
            int g = 1024 + f->p[0] * 1024 / 127 * (t == 1 ? 6 : t == 2 ? 12 : t == 3 ? 24 : 1);   /* (Q10) */
            for (i = 0; i < 64; i++) {
                int xin = (i * 2 - 63) * 1024 / 63, o;
                if (t == 4) {                           /* LO-FI: steps */
                    int sh = 16 - f->p[0] * 14 / 127 > 6 ? 6 : 16 - f->p[0] * 14 / 127, st = 2048 >> sh;
                    o = st ? (xin / st) * st : xin;
                } else if (t == 5) {                    /* FOLD */
                    int u = xin * (18 + f->p[0]) / 18 + (f->p[1] - 64) * 16;
                    u = ((u + 1024) % 4096 + 4096) % 4096 - 1024;
                    o = u < 1024 ? u : 2048 - u;
                } else {
                    int d = xin * g / 1024;
                    o = d > 1024 ? 1024 : d < -1024 ? -1024 : d - d * d / 1024 * d / 1024 / 3 * (t == 1 ? 10 : 3) / 10;
                    o = o > 1024 ? 1024 : o < -1024 ? -1024 : o;
                }
                o = xin + (o - xin) * f->p[2] / 127;    /* (MIX) */
                y[i] = (int16_t)(o * 30000 / 1024);
            }
            gfx_line(x0, y0 + h, x0 + w, y0, 12, gfx_heat(40), 255, GFX_NORMAL);
            curve(x0, y0 + 4, w, h - 8, y, 64, HOT);
        } else {                                        /* ring mod, wah, comp: a wave moving */
            int cyc = 1 + f->p[0] / 24;
            for (i = 0; i < 64; i++) {
                uint32_t ph = (uint32_t)i * (uint32_t)cyc * 68174084u;   /* (2^32 / 63 a point) */
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
        int a = f->p[0] * 1024 / 127;                   /* (Q10) */
        gfx_text(&TB_FONT_M, x0, 90, WHAT[pg - 1], f->p[0] ? ACCENT : GREY, 255, GFX_LEFT);
        if (!f->p[0]) {
            gfx_text(&TB_FONT_S, 120, y0 + 40, "KNOB 1: how much \xc2\xb7 they all play together", gfx_heat(110), 255, GFX_CENTER);
            return;
        }
        gfx_rect(x0, y0 + h / 2, w, 1, gfx_heat(40), 255, GFX_NORMAL);
        if (pg == 4) {                                  /* the tremolo: the volume, sine to square */
            int cyc = 2 + f->p[1] / 18;
            for (i = 0; i < 64; i++) {
                uint32_t ph = (uint32_t)i * (uint32_t)cyc * 68174084u;   /* (2^32 / 63 a point) */
                int sn = lfo_shape(0, ph) / 32, sq = ph < 0x80000000u ? 1024 : -1024, s2 = sn + (sq - sn) * f->p[3] / 127;
                y[i] = (int16_t)(((1024 - a) + a * (s2 + 1024) / 2048) * 30000 / 1024 - 15000);
            }
            curve(x0, y0 + 4, w, h - 8, y, 64, HOT);
        } else {                                        /* chorus, flanger, phaser: the sweep (SPEED across, DEPTH up) */
            int cyc = 1 + f->p[1] / 26, k, dep = f->p[2];
            int nlines = pg == 1 ? 2 : pg == 2 ? 1 + f->p[3] / 32 : 4;
            for (k = 0; k < nlines; k++) {
                for (i = 0; i < 64; i++) {
                    uint32_t ph = (uint32_t)i * (uint32_t)cyc * 68174084u + (uint32_t)k * (pg == 1 ? 0x80000000u : 0x10000000u);
                    y[i] = (int16_t)(lfo_shape(0, ph) * (154 + 870 * dep / 127) / 1024 * (410 + 614 * a / 1024) / 1024 *
                                     (pg == 3 ? 10 - k * 2 : 10) / 10);
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
        int k = 952 + f->p[1] * 61 / 127, a = 154 + f->p[0] * 870 / 127;   /* (Q10) */
        gfx_text(&TB_FONT_M, x0, 90, f->name ? f->name : "", ACCENT, 255, GFX_LEFT);
        for (i = 0; i < n; i++) {
            int x = x0 + i * w / n, hh = a * (h - 18) / 1024;
            if (hh > 0)
                gfx_rect(x, y0 + h - 6 - hh, 2, hh, gfx_heat(100 + a * 140 / 1024 - f->p[2] / 4), 255, GFX_NORMAL);
            a = a * (f->type == 4 && i > 25 ? 614 : k) / 1024;
        }
        return;
    }
    {                                                   /* the delay: its repeats, as loud as this track sends */
        static const uint8_t Q[7] = {6, 12, 18, 24, 8, 16, 36};
        int gap = Q[f->p[1] % 7] * 3, x = x0, k;
        int a = 154 + f->p[0] * 870 / 127, fb = f->p[2] * 1024 / 140;   /* (Q10) */
        gfx_text(&TB_FONT_M, x0, 90, f->name ? f->name : "", ACCENT, 255, GFX_LEFT);
        for (k = 0; k < 24 && x < x0 + w; k++) {
            int hh = a * (h / 2 - 8) / 1024, up = f->type != 0 || !(k & 1);
            gfx_rect(x, up ? y0 + h / 2 + 4 - hh : y0 + h / 2 + 4, 4, hh, gfx_heat(120 + a * 120 / 1024), 255, GFX_NORMAL);
            x += gap, a = a * fb / 1024;
            if (a < 31)
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
        gfx_text(&TB_FONT_S, 120, 126, "kick and break vary as they play", DIM, 255, GFX_CENTER);
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
        for (i = 0; i < TB_PATTERNS; i++) {
            int x = 120 - TB_PATTERNS * 27 / 2 + 2 + i * 27, cur = i == v->pattern, q = i == v->queued;
            char pl[2] = {(char)('A' + i), 0};
            gfx_rrect(x, 136, 23, 26, 5, cur ? gfx_heat(215) : q ? gfx_heat(110) : gfx_heat(24), 255, 0);
            gfx_text(&TB_FONT_M, x + 12, 155, pl, cur ? TB_RGB(0, 0, 0) : GREY, 255, GFX_CENTER);
        }
        gfx_text(&TB_FONT_S, 120, 180, "a pattern picked starts at the end of the bar", DIM, 255, GFX_CENTER);
        return;
    }
    {                                                   /* the mixer, as JIANT's: a strip a track (its number, its name,
                                                         * its meter, its fader), the four the knobs set lit */
        int32_t sw = 236 / TB_TRACKS, k;
        for (i = 0; i < TB_TRACKS; i++) {
            const tb_lane_t *l = &v->lane[i];
            int32_t x = 2 + i * sw, w = sw - 3, lit = i / 4 == v->menu_page, sel = i == v->track;
            int32_t lv = l->level * 14 / 255, fv = v->mix_vol[i] * (w - 8) / 127;
            char n[6];
            uint16_t bg = lit ? T_SURF : T_BG, kc = T_K(i % 4);
            cv_rrect(x, 0, w, GR_H - 4, 4, bg, T_BG);
            n[0] = (char)('1' + i), n[1] = 0;
            cv_rrect(x + 4, 4, 13, 13, 3, sel ? T_ACCENT : l->muted ? T_RAISE : T_MID, bg);
            cv_text_in(x + 4, 4 + CAP_IN(X, 13), 13, &AF_X, n, T_INK, sel ? T_ACCENT : l->muted ? T_RAISE : T_MID);
            if (l->muted || l->solo)
                cv_text_r(x + w - 4, 4 + CAP_IN(X, 13), &AF_X, l->muted ? "M" : "S", l->muted ? T_REC : T_ACCENT, bg);
            latin(n, sizeof n, l->name);
            n[4] = 0;
            cv_text(x + 4, 22, &AF_X, n, l->muted ? T_DIM : lit ? T_TEXT : T_MID);
            for (k = 0; k < 14; k++)                    /* the meter: segments, lit up to the level */
                cv_rect(x + 5, 104 - k * 5, 9, 3, k < lv ? heat_mix((uint32_t)(60 + k * 14)) : T_RAISE);
            snprintf(n, sizeof n, "%d", v->mix_vol[i] * 100 / 127);
            cv_text_r(x + w - 3, 40, &AF_X, n, lit ? kc : T_MID, bg);
            cv_rect(x + 4, GR_H - 14, w - 8, 3, T_RAISE);   /* the fader */
            cv_rect(x + 4, GR_H - 14, fv < 2 ? 2 : fv, 3, lit ? kc : T_MID);
        }
    }
}

static void menu_sys(const tb_view_t *v)
{
    int i;
    static const char *const ASK[4] = {0, "OCT+: LOAD?", "OCT-: NEW PROJECT?", "SAVE: OVERWRITE?"};
    for (i = 0; i < TB_STORE_SLOTS; i++) {              /* the places: the one in use, the one chosen */
        int x = 8 + (i % 3) * 76, y = 54 + (i / 3) * 56, used = (v->slot_used >> i) & 1, sl = i == v->slot_sel, cur = i == v->slot_cur;
        char b[12];
        gfx_rrect(x, y, 70, 50, 7, sl ? gfx_heat(200) : used ? gfx_heat(34) : gfx_heat(14), 255, 0);
        if (cur)
            gfx_rrect(x - 2, y - 2, 74, 54, 8, CREAM, 255, 1);
        snprintf(b, sizeof b, "%d", i + 1);
        gfx_text(&TB_FONT_L, x + 35, y + 26, b, sl ? TB_RGB(0, 0, 0) : used ? CREAM : DIM, 255, GFX_CENTER);
        if (used && v->slot_nm[i] && v->slot_nm[i][0])   /* (v25) its name, if it has one */
            snprintf(b, sizeof b, "%.8s", v->slot_nm[i]);
        else if (used)
            snprintf(b, sizeof b, "%u BPM", v->slot_bpm[i] / 10u);
        else
            snprintf(b, sizeof b, "EMPTY");
        gfx_text(&TB_FONT_S, x + 35, y + 43, b, sl ? TB_RGB(0, 0, 0) : GREY, 255, GFX_CENTER);
    }
    {                                                   /* (the sound's health, on one line - 21 Nov 2026: the CPU's own
                                                         * line sat on the hint): the audio's busiest block in the last
                                                         * second, % of its time; the blocks it missed since power on */
        char b[48];
        /* (21 Nov 2026) one line, a new view each second (a second line under it sat on the hint, unreadable): the
         * CPU and the dropouts; then where the time goes (TIMER4, the FM-1's own), the busiest block of the last
         * second and, for the cores, its mean: the two cores and this one's wait for the other; the tracks, the
         * synths, the insert effects, the phaser of them; the chorus, the delay, the reverb */
        const uint8_t *M = v->prof_max, *A = v->prof_avg;
        if (v->prof_pg == 1)
            snprintf(b, sizeof b, "C0 %u/%u%%  C1 %u/%u%%  WAIT %u%%", M[0], A[0], M[1], A[1], M[2]);
        else if (v->prof_pg == 2)
            snprintf(b, sizeof b, "TRK %u  SYN %u  INS %u  PHA %u%%", M[3], M[4], M[5], M[6]);
        else if (v->prof_pg == 3)
            snprintf(b, sizeof b, "CHO %u  DLY %u  REV %u%%", M[7], M[8], M[9]);
        else if (v->prof_pg == 4)
            snprintf(b, sizeof b, "CHN %u  RIS %u  MST %u%%", M[10], M[11], M[12]);
        else
            snprintf(b, sizeof b, "CPU %u%%  -  %lu DROPOUT%s", (unsigned)v->audio_pct, (unsigned long)v->dropouts,
                     v->dropouts == 1u ? "" : "S");
        gfx_text(&TB_FONT_S, 120, 178, b,
                 v->prof_pg == 0 && (v->dropouts || v->audio_pct >= 85u) ? gfx_heat(240) : GREY, 255, GFX_CENTER);
#if TB_LITE
        gfx_text(&TB_FONT_S, 120, 163, "RESCUE: HOLD OCT- + OCT+ 5 S", DIM, 255, GFX_CENTER);   /* (the FM-1's way out) */
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

/* LIVE FX (FX held): the keyboard as it is under the hand (19 Nov 2026, Maxime: a piano, each key its effect) - the
 * sixteen white keys, what each does written up its key, lit while it plays; the eleven black keys over them where
 * they sit, the first six a track's MUTE (red while muted), the next five a live effect (lit while held) */
void tb_ui_perform(const tb_view_t *v)
{
    static const char *const ALT[1] = {"LIVE FX"};
    static const uint8_t BK_AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* black j: after white BK_AT[j] (F#..F#) */
    enum { BH = 52, KW = 236 / 16 };
    int i;
    frame_with(v, "LIVE", v->perform == 2 ? "[FX] LOCKED" : "[FX] HOLD", ALT, 1, 0);
    {                                                /* (the keyboard as it was: not drawn again - 27 turned labels a
                                                      *  frame were twice another page's work; 21 Nov 2026) */
        uint32_t sig = 0x4B455953u ^ v->tile_lit * 2654435761u ^ ((uint32_t)v->mute_mask << 16) ^ ((uint32_t)v->solo_mask << 24)
                     ^ gfx_palette_now() * 40503u;
        for (i = 0; i < 16; i++)
            sig = str_hash(sig, v->tile[i] ? v->tile[i] : "");
        for (i = 0; i < 6; i++)
            sig = str_hash(sig, v->lane[i].name);
        if (gr_same(v, sig))
            return;
    }
    cv_rect(0, 0, 240, GR_H, T_BG);
    for (i = 0; i < 16; i++) {                       /* the white keys: their effect up the lower part, under the blacks */
        int x = 2 + i * 236 / 16, w = 2 + (i + 1) * 236 / 16 - x - 1, lit = (v->tile_lit >> i) & 1;
        const char *t = v->tile[i] ? v->tile[i] : "";
        char raw[24], sl[24];
        uint16_t bg = lit ? T_THEME : T_TEXT, ink = lit ? T_INK : RGB(20, 16, 24);
        size_t k;
        snprintf(raw, sizeof raw, "%s", t);
        for (k = 0; raw[k]; k++)
            if (raw[k] == '\n')
                raw[k] = ' ';
        latin(sl, sizeof sl, raw);
        cv_rect(x, 0, w, GR_H - 2, bg);
        if (sl[0])
            cv_text_up(x + (w - AF_S_CAP_H) / 2 - AF_S_CAP_Y, GR_H - 6, &AF_S, sl, ink, bg, GR_H - BH - 8);
    }
    for (i = 0; i < 11; i++) {                       /* the black keys over them */
        int cx = 2 + (BK_AT[i] + 1) * 236 / 16, x = cx - 5, w = 11;
        unsigned t = i < TB_TRACKS ? (unsigned)i : (unsigned)(i - TB_TRACKS + 1);
        int on = i < TB_TRACKS ? (v->mute_mask >> t) & 1 : (v->solo_mask >> (i - TB_TRACKS)) & 1;
        uint16_t bg = on ? (i < TB_TRACKS ? RGB(220, 36, 36) : T_THEME) : RGB(14, 11, 18);
        uint16_t ink = on ? (i < TB_TRACKS ? RGB(255, 255, 255) : T_INK) : T_TEXT;
        char lb[16], sl[16], nm[8];
        size_t k;
        snprintf(nm, sizeof nm, "%.5s", v->lane[t].name);   /* (its name, short: STAB/HV -> STAB) */
        for (k = 0; nm[k]; k++)
            if (nm[k] == '/')
                nm[k] = 0;
        if (i < TB_TRACKS)
            snprintf(lb, sizeof lb, "M %s", nm);
        else
            snprintf(lb, sizeof lb, "%s", v->bk_name[i - TB_TRACKS] ? v->bk_name[i - TB_TRACKS] : "");
        latin(sl, sizeof sl, lb);
        cv_rect(x - 1, 0, w + 2, BH + 1, T_BG);
        cv_rect(x, 0, w, BH, bg);
        cv_text_up(x + (w - AF_X_CAP_H) / 2 - AF_X_CAP_Y, BH - 3, &AF_X, sl, ink, bg, BH - 5);
    }
    (void)KW;
}

void tb_ui_kick(const tb_view_t *v)
{
    int i, x0 = 9, w = 222, wy = 64, wh = 58, py = 130, ph = 52;
    char b[32];
    frame(v, "EDIT");
    if (v->page_on == 0) {                              /* the first page: the bass drum */
        instrument(v, 4, GR_H - 8);
        return;
    }
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

/* (the first version's stage after a while untouched: none; JIANT's screen stays as it is) */
int tb_ui_dance(const tb_view_t *v)
{
    (void)v;
    return 0;
}

/* the start: RAVE FM's bat and its name, in their violet, faded in (~0.6 s), nothing else */
#include "rfm_boot.h"
/* a 16-colour run-length picture (tools/gen_boot.py), at x0, y0, faded in (level 0..16), in strips of the canvas */
static void boot_pic(const uint8_t *rle, unsigned len, unsigned w, unsigned h, const uint8_t (*rgb)[3], unsigned x0,
                     unsigned y0, unsigned level)
{
    unsigned i, x = 0, row = 0, part = 76u;
    uint16_t pal[16];
    for (i = 0; i < 16u; i++)
        pal[i] = RGB(rgb[i][0] * level / 16u, rgb[i][1] * level / 16u, rgb[i][2] * level / 16u);
    cv_begin(w, part, 0);
    for (i = 0; i < len; i++) {
        unsigned run = (rle[i] >> 4) + 1u, c = rle[i] & 15u;
        while (run--) {
            cv_px[(row % part) * w + x] = swap16(pal[c]);
            if (++x == w) {
                x = 0, row++;
                if (row % part == 0u || row == h) {
                    cv_h = row % part ? row % part : part;
                    cv_blit(x0, y0 + (row - cv_h));
                    if (row < h)
                        cv_begin(w, part, 0);
                }
            }
        }
    }
}
/* the boot screen: the bat, RAVE FM in chrome under it (the 90s wordmarks' letters, in the bat's violets), fading in */
void tb_ui_splash(uint32_t ms)
{
    static unsigned shown = 99;
    unsigned level = ms >= 600u ? 16u : ms * 16u / 600u, y0 = (TB_H - RFM_BOOT_H - 6u - RFM_LOGO_H) / 2u;
    if (level == shown)
        return;
    if (shown == 99u)                                   /* (the first: the screen black round it) */
        lcd_fill(0, 0, 240, 240, 0);
    shown = level;
    boot_pic(RFM_BOOT_RLE, sizeof RFM_BOOT_RLE, RFM_BOOT_W, RFM_BOOT_H, RFM_BOOT_RGB, 0, y0, level);
    boot_pic(RFM_LOGO_RLE, sizeof RFM_LOGO_RLE, RFM_LOGO_W, RFM_LOGO_H, RFM_LOGO_RGB, (TB_W - RFM_LOGO_W) / 2u,
             y0 + RFM_BOOT_H + 6u, level);
    scr_force = 1;
}
