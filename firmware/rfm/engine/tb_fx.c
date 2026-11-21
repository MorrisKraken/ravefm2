/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the channel filter, the delay and the reverb (tb_fx.h). */
#include "tb_fx.h"
#include "tb_hall.h"
#define TB_FX_MAXN 128                   /* (a block at most: the engine's) */
#define RV_GAIN 470                      /* the reverb's level against the old one's (x 1/256) */
#include "tb_dsp.h"
#include "tb_engine.h"                                   /* (tb_engine_ticks, tb_pf_job: the parts' times) */
#include <string.h>

/* ------------------------------------------------------------- filter --- */
/* the Chamberlin state-variable filter: f = 2 sin(pi fc / fs) (Q15), damping q (Q15, 1 / Q) */
void tb_filter_set(tb_filter_t *f, unsigned knob)
{
    uint32_t hz;
    if (knob == f->knob && f->f)
        return;
    f->knob = (uint8_t)knob;
    if (knob >= 62 && knob <= 66) {                       /* the middle: open, untouched */
        f->mode = 0;
        return;
    }
    if (knob < 64) {                                       /* low-pass: 9 kHz at 61 down to 80 Hz at 0 */
        hz = (uint32_t)((9000u * (uint64_t)tb_exp2(-(int32_t)((int64_t)(61u - knob) * 6820 * 65536 / 61000))) >> 16);
        f->mode = 1;
    } else {                                               /* high-pass: 20 Hz at 67 up to 6 kHz at 127 */
        hz = (uint32_t)((20u * (uint64_t)tb_exp2((int32_t)((int64_t)(knob - 67u) * 8230 * 65536 / 60000))) >> 16);
        f->mode = 2;
    }
    f->f = 2 * tb_sin(hz * 24348u);                         /* run at twice the rate (stable to the top): sin(pi hz / 88200) */
    f->q = 26000;                                          /* a little resonance (Q ~1.26) */
}

void tb_filter_run(tb_filter_t *f, int32_t *x, unsigned n)
{
    unsigned i;
    int32_t lo = f->lo, bd = f->bd;
    if (!f->mode) {
        f->lo = f->bd = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t hi = 0, k;
        for (k = 0; k < 2; k++) {                          /* (two passes a sample: oversampled) */
            lo += tb_mul15(bd, f->f);                      /* (as the 64-bit product: 32 bits overflowed on a loud
                                                            * sound; f < 2^16, exact - tb_dsp.h) */
            hi = x[i] - lo - tb_mul15(bd, f->q);
            bd += tb_mul15(hi, f->f);
            lo = tb_clamp(lo, -(1 << 20), 1 << 20);
            bd = tb_clamp(bd, -(1 << 20), 1 << 20);
        }
        x[i] = f->mode == 1 ? lo : hi;
    }
    f->lo = lo, f->bd = bd;
}

/* -------------------------------------------------------------- delay --- */
#if TB_HAS_DELAY
#if TB_HAS_DLY_HIFI
#define DLEN 32768                                          /* 0.74 s: a dotted quarter down to 122 BPM */
static int16_t dl[DLEN], dr[DLEN];
#else   /* (the FM-1: lo-fi: half the rate, 8 bits; 0.56 s (its RAM: the melodies' 128 steps), a dotted quarter down
         * to 162 BPM, a quarter down to 108; a longer time is held at the line's end) */
#define DLEN 24576                                          /* (not a power of 2: the indices wrap by comparison) */
static int8_t dl[DLEN / 2u], dr[DLEN / 2u];
static int32_t dacc_l, dacc_r;
#endif
static uint32_t dw, wow;
static uint32_t d_zero, r_quiet;                         /* zeros the delay wrote in a row; the reverb's silent samples */
static uint8_t d_idle, d_skip, r_idle, r_sleep;
static int32_t dlp_l, dlp_r, dhp_l, dhp_r;

#endif
/* --------------------------------------------------------------- reverb --- */
/* the first version's HALL (tb_hall.c), a predelay before it; the types: its size and colour */
#if TB_HAS_REVERB
#if TB_HAS_REV_LONG
#define PLEN 4096
#else
#define PLEN 1024                                 /* (the FM-1: a predelay up to 23 ms) */
#endif
static int16_t pre[PLEN];
static uint16_t pw;
static int32_t gate_env, gate_open;
static int32_t rin[TB_FX_MAXN], rmid[TB_FX_MAXN], rside[TB_FX_MAXN];
#endif

/* --------------------------------------------------------------- master --- */
static int32_t eq1[2], eq2[2], cenv;

const char *const TB_FX_DLY_NAME[TB_FX_NDLY] = {"1/16", "1/8", "1/8.", "1/4", "1/8T", "1/4T", "1/4."};
__attribute__((unused)) static const uint8_t DLY_Q[TB_FX_NDLY] = {6, 12, 18, 24, 8, 16, 36};      /* in sixths of a sixteenth */
const char *const TB_FX_REV_NAME[TB_FX_NREV] = {"ROOM", "HALL", "PLATE", "CAVE", "GATED"};
const char *const TB_FX_DLYT_NAME[TB_FX_NDLYT] = {"PING-PONG", "STEREO", "MONO", "TAPE", "DUB"};

void tb_fx_init(void)
{
#if TB_HAS_DELAY
    memset(dl, 0, sizeof dl), memset(dr, 0, sizeof dr);
#endif
#if TB_HAS_REVERB
    tb_hall_clear(), memset(pre, 0, sizeof pre);
#endif
#if TB_HAS_DELAY
    dw = 0, dlp_l = dlp_r = dhp_l = dhp_r = 0;
#endif
#if TB_HAS_REVERB
    pw = 0;
#endif
}

static inline int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

#if TB_HAS_REVERB
#endif

#if TB_HAS_DELAY
#if TB_HAS_DLY_HIFI
static int32_t dread(const int16_t *b, uint32_t pos8)   /* the delay line read at a fraction (Q8) behind dw */
{
    uint32_t d = pos8 >> 8, fr = pos8 & 255u;
    int32_t a = b[(dw + DLEN - d) & (DLEN - 1u)], c = b[(dw + DLEN - d - 1u) & (DLEN - 1u)];
    return a + (((c - a) * (int32_t)fr) >> 8);
}
#else
/* (v25) the 8 bits kept as a sign, 3 bits of octave and 4 of mantissa (as the telephone's A-law): a quiet repeat
 * stored in steps of 32, not 256 - the line's hiss 18 dB down where it was heard (between the repeats, in their
 * tails); a loud one in steps of 1/16 to 1/32 of itself, under the sound. 0 stays 0 (nothing kept going round) */
static inline int8_t d_enc(int32_t v)
{
    int32_t m = v < 0 ? -v : v, c;
    if (m > 32767)
        m = 32767;
    if (m < 512)
        c = m >> 5;
    else {
        int e = 23 - __builtin_clz((uint32_t)m);     /* (m 512 .. 32767: e 1 .. 6) */
        c = e << 4 | ((m >> (e + 4)) & 15);
    }
    return (int8_t)(v < 0 ? -c : c);
}
static inline int32_t d_dec(int32_t b)
{
    int32_t c = b < 0 ? -b : b, e = c >> 4, m;
    m = !c ? 0 : !e ? (c << 5) + 16 : ((16 + (c & 15)) << (e + 4)) + (1 << (e + 3));
    return b < 0 ? -m : m;
}
static int32_t dread(const int8_t *b, uint32_t pos8)    /* the same, from the half-rate line (a sample: two) */
{
    /* (half-rate sample j: the mean of 2j and 2j + 1, its centre 2j + 0.5; Q8 in half-rate samples) */
    uint32_t at8 = ((((dw + 2u * DLEN) << 8) - pos8 - 128u) % (DLEN << 8)) >> 1;
    uint32_t i = at8 >> 8, f = at8 & 255u, j = i + 1u == DLEN / 2u ? 0u : i + 1u;
    int32_t a = d_dec(b[i]), c = d_dec(b[j]);
    return a + (((c - a) * (int32_t)f) >> 8);
}
#endif
#endif

void tb_fx_run(const tb_fx_set_t *fx, const int32_t *dly, const int32_t *rev, int32_t *l, int32_t *r, unsigned n,
               uint32_t step, int wash, int hold)
{
    unsigned i, rt = fx->rev_type < TB_FX_NREV ? fx->rev_type : 1u, dt_ = fx->dly_type < TB_FX_NDLYT ? fx->dly_type : 0u;
    uint32_t pt0 = tb_engine_ticks(), pt1;               /* (the delay's time, then the reverb's: tb_engine.h) */
#if TB_HAS_REVERB
    /* the types: ROOM short, HALL as the first version's, PLATE bright, CAVE long and dark, GATED cut after a hit */
    static const uint8_t SZ[TB_FX_NREV] = {70, 127, 100, 127, 90};
    static const int8_t SZ0[TB_FX_NREV] = {0, 0, 0, 25, 0}, DMP[TB_FX_NREV] = {10, 0, -40, 30, 0};
    int32_t rlev = (int32_t)fx->rev_level * 258;
    int size = wash ? 190 : (int)fx->rev_size * SZ[rt] / 127 + SZ0[rt], damp = (int)fx->rev_damp + DMP[rt];
    uint32_t pd = 1u + (uint32_t)fx->rev_pre * (PLEN - 2u) / 127u;
    uint32_t gate_len = 4410u + (uint32_t)fx->rev_size * 140u;                 /* GATED: 100 .. 500 ms */
#endif
#if TB_HAS_DELAY
    uint32_t dt = step * DLY_Q[fx->dly_time < TB_FX_NDLY ? fx->dly_time : 2] / 6u;
    /* (FEEDBACK at its top: many repeats, but each a little lower: the echoes always die away, never run on) */
    int32_t fb = hold ? 30500 : (int32_t)fx->dly_fb * (dt_ == 4 ? 255 : 250);
    int32_t dlp = 6000 + (int32_t)fx->dly_tone * 210 - (dt_ == 3 ? 3000 : 0);
    int32_t dlev = (int32_t)fx->dly_level * 258;
    if (dt > DLEN - 600u)
        dt = DLEN - 600u;
    if (dt < 16u)
        dt = 16u;
#else
    (void)dly, (void)step, (void)hold, (void)dt_;
#endif
#if TB_HAS_DELAY || TB_HAS_REVERB
    {                                                    /* (audit 2) what is silent sleeps: the FM-1's time kept */
        uint32_t din = 0, rinx = 0;
        for (i = 0; i < n; i++)
            din |= (uint32_t)(hold ? 0 : dly[i]), rinx |= (uint32_t)rev[i];
        d_idle = din ? 0 : 1, r_idle = rinx ? 0 : 1;
        if (!r_idle)
            r_sleep = 0, r_quiet = 0;
    }
#endif
#if TB_HAS_DELAY
    if (d_idle && d_zero >= DLEN && !dlp_l && !dlp_r && !dhp_l && !dhp_r) {
        /* the delay asleep: its line all zeros, its filters at rest, nothing in - it would read, filter and write back
         * only zeros: its place and its TAPE's wobble moved on as if it ran (the sound the same, bit for bit) */
        dw = (uint32_t)((dw + n) % DLEN);
        if (dt_ == 3)
            wow += 41000u * n;
        d_skip = 1;
    } else
        d_skip = 0;
#endif
    for (i = 0; i < n; i++) {
#if TB_HAS_DELAY
        if (d_skip)
            goto rev_in;
        /* the delay */
        int32_t in = hold ? 0 : dly[i], yl, yr;
        uint32_t d8 = dt << 8;
        if (dt_ == 3) {                                  /* TAPE: wow and flutter on the read */
            wow += 41000u;
            d8 += (uint32_t)((tb_sin(wow) + 32768) * 3) + (uint32_t)((tb_sin(wow * 7u) + 32768) / 2);
        }
        yl = dread(dl, d8), yr = dread(dr, d8);
        dlp_l += ((yl - dlp_l) * dlp) >> 15;
        dlp_r += ((yr - dlp_r) * dlp) >> 15;
        if (dt_ == 4) {                                  /* DUB: the repeats thin out (a high-pass) and saturate */
            dhp_l += ((dlp_l - dhp_l) * 1400) >> 15;
            dhp_r += ((dlp_r - dhp_r) * 1400) >> 15;
        }
        {
            int32_t fl = dt_ == 4 ? dlp_l - dhp_l : dlp_l, fr = dt_ == 4 ? dlp_r - dhp_r : dlp_r;
            int32_t ql = (fl * fb) >> 15, qr = (fr * fb) >> 15;
            if (dt_ >= 3)
                ql = tb_tanh(ql), qr = tb_tanh(qr);
            int32_t wl, wr;
            switch (dt_) {
            case 0: wl = sat16((in >> 1) + qr), wr = sat16(ql); break;            /* PING-PONG */
            case 2: wl = wr = sat16((in >> 1) + ql); break;                       /* MONO */
            default: wl = sat16((in >> 1) + ql), wr = sat16((in >> 1) + qr);       /* STEREO, TAPE, DUB */
            }
#if TB_HAS_DLY_HIFI
            dl[dw] = (int16_t)wl, dr[dw] = (int16_t)wr;
#else
            if (dw & 1u) {                               /* every other sample: the pair's mean, 8 bits (rounded towards */
                int32_t sl = dacc_l + wl, sr = dacc_r + wr;  /* zero: a floor kept a last -1 going round for ever) */
                dl[dw >> 1] = d_enc((sl + (sl < 0 ? 1 : 0)) >> 1);   /* (v25: companded - d_enc) */
                dr[dw >> 1] = d_enc((sr + (sr < 0 ? 1 : 0)) >> 1);
            } else
                dacc_l = wl, dacc_r = wr;
#endif
            d_zero = wl | wr ? 0u : d_zero < DLEN ? d_zero + 1u : d_zero;   /* (zeros written in a row) */
        }
        dw = dw + 1u == DLEN ? 0u : dw + 1u;
        l[i] += (yl * dlev) >> 15, r[i] += (yr * dlev) >> 15;
    rev_in:;
#endif
#if TB_HAS_REVERB
        if (r_sleep)
            continue;
        pre[pw] = sat16(rev[i] >> 2);                /* the predelay */
        rin[i] = (int32_t)pre[(pw + PLEN - pd) & (PLEN - 1u)] * 4;
        pw = (uint16_t)((pw + 1u) & (PLEN - 1u));
#endif
    }
    pt1 = tb_engine_ticks();
    tb_pf_job.dly += pt1 - pt0;
#if TB_HAS_REVERB
    if (r_sleep) {                                   /* asleep: nothing in, its last tail under -78 dB and let go */
        pw = (uint16_t)((pw + n) & (PLEN - 1u));
        tb_hall_idle(n);
        tb_pf_job.rev += tb_engine_ticks() - pt1;
        return;
    }
    tb_hall_run(rin, rmid, rside, n, size, damp);
    {
        int32_t pk = 0;
        for (i = 0; i < n; i++) {
            int32_t a = rmid[i] < 0 ? -rmid[i] : rmid[i], b = rside[i] < 0 ? -rside[i] : rside[i];
            pk = a + b > pk ? a + b : pk;
        }
        r_quiet = r_idle && pk < 8 ? r_quiet + n : 0u;
        if (r_quiet > 22050u && rt != 4) {            /* (0.5 s of it: the hall emptied, asleep till a sound comes) */
            tb_hall_sleep(), memset(pre, 0, sizeof pre);
            r_sleep = 1, r_quiet = 0;
        }
    }
    for (i = 0; i < n; i++) {
        int32_t a = rev[i] < 0 ? -rev[i] : rev[i], g;
        if (rt == 4) {                                   /* GATED: open on a hit, shut after the gate */
            if (a > 2000)
                gate_open = (int32_t)gate_len;
            if (gate_open > 0)
                gate_open--, gate_env += (32767 - gate_env) >> 6;
            else
                gate_env -= gate_env >> 7;
        } else
            gate_env = 32767;
        g = ((rlev * gate_env) >> 15) * RV_GAIN >> 8;   /* (both under 2^15: 32 bits; g < 2^16, exact - tb_dsp.h) */
        l[i] += tb_mul15(rmid[i] + rside[i], g);
        r[i] += tb_mul15(rmid[i] - rside[i], g);
    }
#else
    (void)rt;
#endif
    tb_pf_job.rev += tb_engine_ticks() - pt1;
}

/* the master: a three-band EQ (the bass under 200 Hz, the top over 4 kHz, +-12 dB), a glue compressor, the stereo
 * width, a warm drive */
void tb_fx_master(const tb_fx_set_t *fx, int32_t *l, int32_t *r, unsigned n)
{
    unsigned i, c;
    int32_t gl = (int32_t)(tb_exp2(((int32_t)fx->eq_low - 64) * 2048) >> 4);    /* Q12: 2^((k - 64) / 32), +-12 dB */
    int32_t gm = (int32_t)(tb_exp2(((int32_t)fx->eq_mid - 64) * 2048) >> 4);
    int32_t gh = (int32_t)(tb_exp2(((int32_t)fx->eq_high - 64) * 2048) >> 4);
    int eq = fx->eq_low != 64 || fx->eq_mid != 64 || fx->eq_high != 64;
    for (c = 0; c < 2 && eq; c++) {
        int32_t *x = c ? r : l;
        for (i = 0; i < n; i++) {
            int32_t lo, hi, mid;
            eq1[c] += tb_mul15(x[i] - eq1[c], 932);                           /* 200 Hz */
            eq2[c] += tb_mul15(x[i] - eq2[c], 17000);                         /* ~4 kHz */
            lo = eq1[c], hi = x[i] - eq2[c], mid = eq2[c] - eq1[c];
            /* (the sum's floor as the 64-bit one: the tops times the gains, then the low 12 bits' products summed and
             * shifted once; the gains under 2^14: 3 x 4095 x 2^14 < 2^31) */
            x[i] = (lo >> 12) * gl + (mid >> 12) * gm + (hi >> 12) * gh
                   + (((lo & 4095) * gl + (mid & 4095) * gm + (hi & 4095) * gh) >> 12);
        }
    }
    if (fx->comp) {                                      /* the glue: both sides together, 3:1, made up */
        int32_t th = (int32_t)((24000ull * tb_exp2(-(int32_t)fx->comp * 65536 * 4 / 127)) >> 16);
        int32_t mk = 4096 + (int32_t)fx->comp * 40;
        for (i = 0; i < n; i++) {
            int32_t a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i], g = 32767;
            if (b > a)
                a = b;
            cenv += a > cenv ? (a - cenv) >> 4 : -((cenv - a) >> 12);
            if (cenv > th)
                g = tb_divq15((uint32_t)(th + (cenv - th) / 3), (uint32_t)(cenv | 1));
            l[i] = tb_mulsh(tb_mul15(l[i], g), mk, 12);
            r[i] = tb_mulsh(tb_mul15(r[i], g), mk, 12);
        }
    }
    if (fx->width != 64) {                               /* the width: the sides narrower or wider */
        int32_t w = (int32_t)fx->width * 512;           /* Q15: 0 .. 2 */
        for (i = 0; i < n; i++) {
            int32_t m = (l[i] + r[i]) / 2, s = tb_mul15((l[i] - r[i]) / 2, w);
            l[i] = m + s, r[i] = m - s;
        }
    }
    if (fx->drive) {
        int32_t dg = 4096 + fx->drive * 64;
        for (i = 0; i < n; i++) {
            l[i] = tb_tanh(tb_mulsh(l[i], dg, 12));
            r[i] = tb_tanh(tb_mulsh(r[i], dg, 12));
        }
    }
}

/* the master's distortion. Each type: a pre-emphasis (what the drive bites into), the curve, a tone (a low-pass from
 * ~1.5 to 18 kHz) and the mix with the clean sound. The level stays about where it was (the louder types cut). */
const char *const TB_FX_DIST_NAME[TB_FX_NDIST] = {"OFF", "WARM", "GABBER", "ROTTERDAM", "TERROR"};
static int32_t ds_pre[2], ds_lp[2], ds_lp2[2], ds_hold[2];
static uint32_t ds_cnt;

void tb_fx_dist(const tb_fx_set_t *fx, int32_t *l, int32_t *r, unsigned n)
{
    unsigned t = fx->dist_type < TB_FX_NDIST ? fx->dist_type : 0u, i, c;
    int32_t gain, mix = fx->dist_mix * 258, tc, out;
    if (!t || !fx->dist_mix)
        return;
    gain = 4096 + (int32_t)fx->dist_gain * (t == 1 ? 40 : t == 2 ? 120 : t == 3 ? 200 : 300);   /* Q12 */
    {                                                     /* the tone: 1.5 .. 18 kHz */
        uint32_t hz = (uint32_t)((1500ull * tb_exp2((int32_t)((int64_t)fx->dist_tone * 65536 * 358 / 12700))) >> 16);
        tc = (int32_t)(hz * 467u / 100u);
        tc = tc > 30000 ? 30000 : tc;
    }
    out = t == 1 ? 30000 : t == 2 ? 26000 : t == 3 ? 28000 : 22000;
    for (c = 0; c < 2; c++) {
        int32_t *x = c ? r : l;
        for (i = 0; i < n; i++) {
            int32_t dry = x[i], v, y;
            /* the pre-emphasis: GABBER the mids up (the kick's "tok"), ROTTERDAM the bass up, TERROR all of it */
            ds_pre[c] += ((dry - ds_pre[c]) * (t == 3 ? 1400 : 5000)) >> 15;
            if (t == 2)
                v = dry + (dry - ds_pre[c]) / 2;
            else if (t == 3)
                v = dry + ds_pre[c];
            else
                v = dry;
            v = tb_mulsh(tb_clamp(v, -98304, 98304), gain, 12);
            switch (t) {
            case 1:                                       /* CHAUD: tanh, a little uneven (the even harmonics) */
                y = tb_tanh(v + (v > 0 ? v / 8 : 0));
                break;
            case 2:                                       /* GABBER: soft into hard, the top clipped flat */
                y = tb_tanh(v);
                y = tb_clamp(y * 5 / 4, -28000, 28000);
                break;
            case 3: {                                     /* ROTTERDAM: hard and folded back a little over the top */
                int32_t a = v < 0 ? -v : v;
                y = a < 26000 ? v : (v > 0 ? 1 : -1) * (26000 - (a - 26000) / 6);
                y = tb_clamp(y, -30000, 30000);
                y = tb_tanh(y + y / 2);
                break;
            }
            default:                                      /* TERROR: a square, crushed (held 2 samples, 10 bits) */
                y = tb_tanh(tb_tanh(v) * 4);
                if ((ds_cnt + i) & 1u)
                    y = ds_hold[c];
                ds_hold[c] = y & ~63;
                break;
            }
            ds_lp[c] += ((y - ds_lp[c]) * tc) >> 15;
            ds_lp2[c] += ((ds_lp[c] - ds_lp2[c]) * tc) >> 15;
            y = tb_mul15(ds_lp2[c], out);
            x[i] = dry + tb_mul15(y - dry, mix);
        }
    }
    ds_cnt += n;
}
