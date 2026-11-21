/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the drawings (rfm_art.h). Original art: the dancers are skeletons of ten joints, posed by hand (four poses
 * a dance, one a beat) and eased from one pose to the next; how far they move is how loud their tracks are. */
#include "rfm_art.h"
#include "rfm_px.h"
#include "rfm_sprites.h"
#include "tb_dsp.h"
#include <stdio.h>

/* ------------------------------------------------------------------------------------------------ icons --- */
void art_icon(unsigned icon, int x, int y, int scale, unsigned c, int hit)
{
    const uint8_t *b = RFM_ICON[icon % 8u][hit ? 1 : 0];
    int i, j;
    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            if (b[j * 2 + i / 8] >> (7 - i % 8) & 1) {
                if (scale <= 1)
                    px_pixel(x + i, y + j, c);
                else
                    px_rect(x + i * scale, y + j * scale, scale, scale, c);
            }
}

/* ---------------------------------------------------------------------------------------------- speakers --- */
void art_speaker(int x, int y, int w, int h, int pump, int hot)
{
    int cx = x + w / 2, wy = y + h * 5 / 8, r = w * 3 / 10;
    px_rect(x + 1, y + 1, w - 2, h - 2, PX_BG);
    px_frame(x, y, w, h, PX_MAIN);
    px_frame(x + 2, y + 2, w - 4, h - 4, PX_LINE);
    px_circle(cx, y + h / 5, 2, PX_MID);                  /* the tweeter */
    px_circle(cx, wy, r + pump, hot ? PX_HOT : PX_MAIN);  /* the woofer: its edge moves out on the kick */
    px_circle(cx, wy, r + pump + 2, pump > 1 ? PX_MID : PX_LINE);
    px_disc(cx, wy, r / 3 + (pump > 1), hot ? PX_HOT : PX_MID);
}

/* ---------------------------------------------------------------------------------------------- dancers --- */
/* joints around the hip (y up is negative): neck, head, left elbow, left hand, right elbow, right hand, left knee,
 * left foot, right knee, right foot */
enum { J_NECK, J_HEAD, J_LE, J_LH, J_RE, J_RH, J_LK, J_LF, J_RK, J_RF, NJ };
typedef struct { int8_t p[NJ][2]; } pose_t;
static const pose_t REST = {{{0, -16}, {0, -23}, {-5, -9}, {-6, -2}, {5, -9}, {6, -2}, {-3, 9}, {-4, 19}, {3, 9}, {4, 19}}};
/* the gabber's hakken: the legs kick forward in turn, the fists pump */
static const pose_t GABBER[4] = {
    {{{0, -15}, {0, -22}, {-6, -10}, {-4, -17}, {6, -8}, {7, -1}, {-4, 9}, {-5, 19}, {4, 8}, {5, 19}}},
    {{{1, -16}, {2, -23}, {-5, -21}, {-3, -29}, {6, -9}, {8, -3}, {-3, 9}, {-4, 19}, {7, 5}, {13, 9}}},
    {{{0, -14}, {0, -21}, {-6, -8}, {-7, -1}, {6, -10}, {4, -17}, {-4, 8}, {-5, 19}, {4, 9}, {5, 19}}},
    {{{-1, -16}, {-2, -23}, {-6, -9}, {-8, -3}, {5, -21}, {3, -29}, {-7, 5}, {-13, 9}, {3, 9}, {4, 19}}},
};
/* the robot: arms snapped at right angles */
static const pose_t ROBOT[4] = {
    {{{0, -16}, {0, -23}, {-8, -16}, {-8, -24}, {8, -16}, {8, -8}, {-4, 9}, {-4, 19}, {4, 9}, {4, 19}}},
    {{{0, -16}, {0, -23}, {-8, -16}, {-8, -8}, {8, -16}, {8, -24}, {-4, 9}, {-4, 19}, {4, 9}, {4, 19}}},
    {{{0, -15}, {0, -22}, {-8, -15}, {-16, -15}, {8, -15}, {16, -15}, {-5, 8}, {-6, 19}, {5, 8}, {6, 19}}},
    {{{0, -16}, {0, -23}, {-6, -10}, {-6, -2}, {6, -22}, {14, -22}, {-4, 9}, {-4, 19}, {4, 9}, {4, 19}}},
};

static int ease(int t)                                  /* 0..1024 -> 0..1024, eased in and out */
{
    return t < 512 ? t * t / 512 : 1024 - (1024 - t) * (1024 - t) / 512;
}
static void lerp_pose(const pose_t *a, const pose_t *b, int t, int amp, int (*o)[2])
{
    int j, k;
    for (j = 0; j < NJ; j++)
        for (k = 0; k < 2; k++) {
            int v = a->p[j][k] * 1024 + (b->p[j][k] - a->p[j][k]) * t, r = REST.p[j][k] * 1024;   /* (x 1024) */
            int q = r + (v - r) / 256 * amp;            /* (as far from the rest as the energy says) */
            o[j][k] = (q + (q < 0 ? -512 : 512)) / 1024;
        }
}
/* the dancer's own units: around its feet (ox, oy), s4 quarter pixels a unit */
static int ox, oy, s4 = 4;
static int UX(int u) { return ox + (u * s4 + (u < 0 ? -2 : 2)) / 4; }
static int UY(int u) { return oy + (u * s4 + (u < 0 ? -2 : 2)) / 4; }
void art_scale(int quarter) { s4 = quarter < 2 ? 2 : quarter > 12 ? 12 : quarter; }
static void L(int x0, int y0, int x1, int y1, unsigned c) { px_line(UX(x0), UY(y0), UX(x1), UY(y1), c); }
static void C(int x, int y, int r, unsigned c) { px_circle(UX(x), UY(y), (r * s4 + 2) / 4, c); }
static void F(int x, int y, int w, int h, unsigned c) { px_frame(UX(x), UY(y), (w * s4 + 2) / 4, (h * s4 + 2) / 4, c); }
static void R(int x, int y, int w, int h, unsigned c) { px_rect(UX(x), UY(y), (w * s4 + 2) / 4, (h * s4 + 2) / 4, c); }
static void D(int x, int y, unsigned c) { px_pixel(UX(x), UY(y), c); }
static void bones(int (*j)[2], unsigned c, int body)
{
#define P(n) j[n][0], j[n][1] - 20
    if (body)
        L(0, -20, P(J_NECK), c);
    L(P(J_NECK), P(J_LE), c), L(P(J_LE), P(J_LH), c);
    L(P(J_NECK), P(J_RE), c), L(P(J_RE), P(J_RH), c);
    L(0, -20, P(J_LK), c), L(P(J_LK), P(J_LF), c);
    L(0, -20, P(J_RK), c), L(P(J_RK), P(J_RF), c);
#undef P
}
static int isin(uint32_t ph, int r) { return (tb_sin(ph) * r) / 32767; }

void art_dancer(unsigned kind, int x, int y, unsigned phase, uint32_t beats, unsigned energy, int hit, uint32_t ms)
{
    int j[NJ][2], amp = 70 + (int)energy * 186 / 255, t = ease((int)phase), hx, hy, k;
    unsigned b = (unsigned)(beats % 4u);
    ox = x, oy = y;
    if (energy < 24u)                                   /* (silent: a slow sway, half time) */
        b = (unsigned)((beats / 2u) % 4u), t = ease((int)(((beats & 1u) * 1024u + phase) / 2u)), amp = 60;
    switch (kind) {
    case ART_GABBER:
        lerp_pose(&GABBER[b], &GABBER[(b + 1u) % 4u], t, amp, j);
        bones(j, PX_MAIN, 1);
        hx = j[J_HEAD][0], hy = j[J_HEAD][1] - 20;
        C(hx, hy, 4, PX_MAIN);                          /* (bald, a grin) */
        D(hx + 2, hy - 1, PX_MAIN);
        L(hx - 1, hy + 2, hx + 2, hy + 2, PX_MID);
        L(-3, -19, 3, -19, PX_MID);                     /* (the tracksuit's waistband) */
        if (hit)                                        /* the kick: a shock under his feet */
            L(-10, 1, 10, 1, PX_HOT);
        break;
    case ART_ROBOT: {
        lerp_pose(&ROBOT[b], &ROBOT[(b + 1u) % 4u], t < 700 ? (t * 3 / 2 > 1024 ? 1024 : t * 3 / 2) : 1024, amp, j);
        bones(j, PX_MAIN, 0);
        F(-5, -37, 11, 18, PX_MAIN);                    /* its body: a box, a screen on it */
        L(-3, -33, 3, -33, PX_LINE);
        D(-2, -29, hit ? PX_HOT : PX_MID), D(0, -29, PX_MID), D(2, -29, hit ? PX_MID : PX_HOT);
        hx = j[J_HEAD][0], hy = j[J_HEAD][1] - 21;
        F(hx - 4, hy - 4, 9, 8, PX_MAIN);               /* its head, its antenna, its eyes lit on a hit */
        L(hx, hy - 8, hx, hy - 5, PX_MAIN);
        D(hx, hy - 9, hit ? PX_HOT : PX_MID);
        D(hx - 2, hy - 1, hit ? PX_HOT : PX_MAIN), D(hx + 2, hy - 1, hit ? PX_HOT : PX_MAIN);
        break;
    }
    case ART_RAVER: {                                   /* glowsticks in circles, a step side to side */
        uint32_t th = (uint32_t)((beats % 2u) * 1024u + phase) << 21;
        int sway = isin((uint32_t)phase << 21, 2 * amp / 256 + 1) * ((beats & 1u) ? 1 : -1), r = 4 + amp * 4 / 256;
        int nx = sway, ny = -16 + (t > 512 ? 1 : 0), lift = t < 512 ? t / 128 : (1024 - t) / 128;
        j[J_NECK][0] = nx, j[J_NECK][1] = ny;
        j[J_HEAD][0] = nx, j[J_HEAD][1] = ny - 7;
        j[J_LH][0] = nx - 7 + isin(th + 0x40000000u, r), j[J_LH][1] = ny - 6 + isin(th, r);
        j[J_RH][0] = nx + 7 - isin(th + 0x40000000u, r), j[J_RH][1] = ny - 6 + isin(th + 0x80000000u, r);
        j[J_LE][0] = (nx + j[J_LH][0]) / 2 - 3, j[J_LE][1] = (ny + j[J_LH][1]) / 2 + 2;
        j[J_RE][0] = (nx + j[J_RH][0]) / 2 + 3, j[J_RE][1] = (ny + j[J_RH][1]) / 2 + 2;
        j[J_LK][0] = -3 + sway, j[J_LK][1] = 9, j[J_LF][0] = -5, j[J_LF][1] = 19 - ((beats & 1u) ? 0 : lift);
        j[J_RK][0] = 3 + sway, j[J_RK][1] = 9, j[J_RF][0] = 5, j[J_RF][1] = 19 - ((beats & 1u) ? lift : 0);
        bones(j, PX_MAIN, 1);
        hx = j[J_HEAD][0], hy = j[J_HEAD][1] - 20;
        C(hx, hy, 4, PX_MAIN);
        for (k = -3; k <= 3; k += 2)                    /* (spiky hair) */
            L(hx + k, hy - 4, hx + k + (k > 0) - (k < 0), hy - 8, PX_MAIN);
        for (k = 0; k < 2; k++) {                       /* the sticks: along the forearm, beyond the hand */
            int ex = j[k ? J_RE : J_LE][0], ey = j[k ? J_RE : J_LE][1] - 20;
            int hx2 = j[k ? J_RH : J_LH][0], hy2 = j[k ? J_RH : J_LH][1] - 20;
            L(hx2, hy2, hx2 + (hx2 - ex) * 3 / 4, hy2 + (hy2 - ey) * 3 / 4, hit ? PX_HOT : PX_AUX);
        }
        break;
    }
    default: {                                          /* the DJ, behind the decks: nods, a hand on the headphones,
                                                         * the other scratching while the break plays */
        int nod = t < 300 ? t / 100 : t < 700 ? 3 : (1024 - t) / 108, sc = energy > 24u ? isin((uint32_t)ms << 23, 3) : 0;
        int sp = (int)(ms / 4u), ny = -34 + nod / 2;
        L(0, -20, 0, ny, PX_MAIN);
        L(0, ny, -8, -32, PX_MAIN), L(-8, -32, -4, -40 + nod, PX_MAIN);   /* (a hand on the headphones) */
        L(0, ny, 9, -26, PX_MAIN), L(9, -26, 12 + sc, -16, PX_MAIN);     /* (the other on the record) */
        hx = 1, hy = -41 + nod;
        C(hx, hy, 4, PX_MAIN);
        L(hx - 4, hy - 5, hx + 4, hy - 5, PX_MAIN);     /* a cap */
        L(hx + 2, hy - 4, hx + 6, hy - 4, PX_MAIN);
        R(hx - 6, hy - 1, 2, 4, PX_AUX), R(hx + 5, hy - 1, 2, 4, PX_AUX);   /* the headphones */
        L(hx - 6, hy - 1, hx - 5, hy - 6, PX_AUX), L(hx + 6, hy - 1, hx + 5, hy - 6, PX_AUX);
        R(-22, -15, 45, 14, PX_BG);                     /* the decks */
        F(-22, -15, 45, 14, PX_MAIN);
        L(-22, -12, 22, -12, PX_LINE);
        for (k = 0; k < 2; k++) {
            int cx = -12 + k * 24, cy = -7;
            uint32_t a = (uint32_t)(sp + k * 300) << 22;
            C(cx, cy, 4, PX_MID);
            L(cx, cy, cx + isin(a + 0x40000000u, 3), cy + isin(a, 3) / 2, hit && k ? PX_HOT : PX_MAIN);
        }
        L(0, -10, 0, -3, PX_LINE);
        break;
    }
    }
}

/* ------------------------------------------------------------------------------------------------- stage --- */
static unsigned kind_of(unsigned icon)
{
    return icon == TB_ICON_KICK ? ART_GABBER : icon == TB_ICON_909 ? ART_ROBOT : icon == TB_ICON_KIT || icon == TB_ICON_SAMPLE ? ART_DJ
                                                                                                          : ART_RAVER;
}
void art_stage(const tb_view_t *v)
{
    unsigned energy[ART_NDANCER] = {0, 0, 0, 0}, kick_lvl = 0, t, k;
    int hit[ART_NDANCER] = {0, 0, 0, 0}, kick_hit = 0, pump;
    unsigned ph = v->playing ? v->beat_phase : 0;
    uint32_t bt = v->playing ? v->beats : 0, ms = v->ms;
    char b[24];
    for (t = 0; t < TB_TRACKS; t++) {
        const tb_lane_t *l = &v->lane[t];
        unsigned d = kind_of(l->icon), lv = l->muted ? 0u : l->level;
        if (lv > energy[d])
            energy[d] = lv;
        if (l->active && !l->muted) {
            hit[d] = 1;
            if (l->icon == TB_ICON_KICK)
                kick_hit = 1;
        }
        if (l->icon == TB_ICON_KICK && lv > kick_lvl)
            kick_lvl = lv;
    }
    /* the lights: a mirror ball, its beams sweeping over the bars */
    {
        uint32_t sweep = (uint32_t)((v->beats % 16u) * 1024u + v->beat_phase) << 18;
        for (k = 0; k < 5u; k++) {
            int ex = 120 + isin(sweep + k * 0x33333333u, 150);
            px_line(120, 18, ex, 200, k == (v->beats % 5u) && v->playing ? PX_DIM : PX_LINE);
        }
        px_vline(120, 0, 9, PX_LINE);
        px_disc(120, 16, 7, PX_BG);
        px_circle(120, 16, 7, PX_MID);
        px_hline(113, 16, 15, PX_LINE), px_vline(120, 9, 15, PX_LINE);
        px_pixel(120 + isin((uint32_t)ms << 21, 5), 14, PX_INK);
    }
    /* the DJ's riser, at the back */
    px_rect(78, 150, 84, 50, PX_BG);
    px_frame(78, 150, 84, 50, PX_LINE);
    for (k = 0; k < 4u; k++)
        px_hline(80, 158 + (int)k * 10, 80, PX_LINE);
    art_scale(6);
    art_dancer(ART_DJ, 120, 150, ph, bt, energy[ART_DJ], hit[ART_DJ], ms);
    /* the floor */
    px_hline(0, 201, 240, PX_MID);
    /* the speakers: the cones out on the kick */
    pump = kick_hit ? 2 + (int)(kick_lvl > 160u) : v->playing && v->beat_phase < 160u ? 1 : 0;
    art_speaker(2, 124, 36, 77, pump, kick_hit);
    art_speaker(6, 88, 28, 36, pump / 2, 0);
    art_speaker(202, 124, 36, 77, pump, kick_hit);
    art_speaker(206, 88, 28, 36, pump / 2, 0);
    /* the dancers, in front */
    art_dancer(ART_GABBER, 58, 200, ph, bt, energy[ART_GABBER], hit[ART_GABBER], ms);
    art_dancer(ART_ROBOT, 182, 200, ph, bt, energy[ART_ROBOT], hit[ART_ROBOT], ms);
    art_scale(4);                                       /* (the raver up on the left stack) */
    art_dancer(ART_RAVER, 20, 87, ph, bt, energy[ART_RAVER], hit[ART_RAVER], ms);
    /* the levels: a bar a track */
    for (t = 0; t < TB_TRACKS; t++) {
        const tb_lane_t *l = &v->lane[t];
        int x = 8 + (int)t * 29, h = l->muted ? 0 : l->level * 30 / 255;
        px_hline(x, 237, 24, PX_LINE);
        if (h > 0) {
            px_rect(x, 237 - h, 24, h, l->active ? PX_MAIN : PX_MID);
            px_hline(x, 237 - h, 24, l->active ? PX_HOT : PX_MAIN);
        }
    }
    snprintf(b, sizeof b, "%u BPM", v->bpm10 / 10u);
    px_text(4, 4, b, PX_GREY, 1, PX_LEFT);
    b[0] = (char)('A' + v->pattern), b[1] = 0;
    px_text(236, 4, b, PX_HOT, 1, PX_RIGHT);
}
