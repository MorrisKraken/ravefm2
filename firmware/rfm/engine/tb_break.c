/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the BREAK track (tb_break.h). The breaks are BC5 blocks (every block decodes on its own: a slice starts
 * anywhere, plays backwards, loops), read through the 909's 8-tap interpolation. */
#include "tb_break.h"
#include "tb_dsp.h"
#include "tb_break_data.h"
#include "tb_909_data.h"          /* (TB909_K: the interpolation, shared) */
#include <string.h>

#define GAIN 2900                 /* Q12: the breaks are stored at their peak; a little under, beside the kick */
#define FADE_END 66               /* the last 1.5 ms of a slice fade out (no click at the cut) */

tb_brk_user_t tb_brk_user;
#define UMAGIC 0x52535554u                 /* "TUSR" */
#if TB_HAS_USER_BRK && defined(RFM2)
#include "tb_store.h"
const uint8_t *tb_brk_rom;
#define POOL_AT(o) (tb_brk_rom ? tb_brk_rom[o] : 0xFFu)
static int user(unsigned b) { return b >= TBB_N && b - TBB_N < tb_brk_user.n && tb_brk_user.magic == UMAGIC; }
#elif TB_HAS_USER_BRK
uint8_t tb_brk_pool[TBB_POOL];
#define POOL_AT(o) tb_brk_pool[o]
static int user(unsigned b) { return b >= TBB_N && b - TBB_N < tb_brk_user.n && tb_brk_user.magic == UMAGIC; }
#else
static int user(unsigned b) { (void)b; return 0; }
#endif
#define U(b) (tb_brk_user.s[(b) - TBB_N])

unsigned tb_brk_count(void) { return TBB_N + (TB_HAS_USER_BRK && tb_brk_user.magic == UMAGIC ? tb_brk_user.n : 0u); }
const char *tb_brk_name(unsigned b) { return user(b) ? U(b).name : TBB_NAME[b < TBB_N ? b : 0]; }
unsigned tb_brk_default_div(unsigned b) { return user(b) ? (U(b).nauto > 1u ? TBB_AUTO : TBB_D16) : b < TBB_N ? TBB[b].div : TBB_D16; }
unsigned tb_brk_bars(unsigned b)
{
    if (user(b)) {                                 /* (a guess: its length at 175 BPM, in bars, at least one) */
        uint32_t bars = (U(b).len + 10971u) / 21943u;
        return bars < 1u ? 1u : bars > 8u ? 8u : bars;
    }
    return b < TBB_N ? TBB[b].bars : 1u;
}
uint32_t tb_brk_len(unsigned b) { return user(b) ? U(b).len : b < TBB_N ? TBB[b].len : 0u; }

#if TB_HAS_USER_BRK
/* mu-law: 8 bits a sample, the quiet parts finer (as the telephone's) */
static int16_t ulaw_dec(uint8_t u)
{
    int32_t t;
    u = (uint8_t)~u;
    t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (int16_t)((u & 0x80) ? 0x84 - t : t - 0x84);
}

static unsigned in_slot = TBB_USER;

void tb_brk_user_keep(unsigned n)
{
    if (tb_brk_user.magic != UMAGIC)
        memset(&tb_brk_user, 0, sizeof tb_brk_user), tb_brk_user.magic = UMAGIC;
    if (n < tb_brk_user.n)
        tb_brk_user.n = n;
}

int tb_brk_user_begin(unsigned u, const char *name, uint32_t n)
{
    uint32_t off = 0;
    unsigned j;
    tb_brk_user_keep(TBB_USER);
    in_slot = TBB_USER;
    if (u >= TBB_USER)
        return -1;
    if (u > tb_brk_user.n)
        u = tb_brk_user.n;
    for (j = 0; j < u; j++)                        /* (after the ones before it; the ones after it go) */
        off = tb_brk_user.s[j].off + tb_brk_user.s[j].len;
#ifdef RFM2
    off = (off + 4095u) & ~4095u;                  /* (in flash: each on its own sectors) */
    if (off >= TBB_POOL)
        return -1;
#endif
    if (n > TBB_POOL - off)
        n = TBB_POOL - off;
    if (n < 64u)
        return -1;
    tb_brk_user.n = u;                             /* (not playable until it is whole) */
    memset(&tb_brk_user.s[u], 0, sizeof tb_brk_user.s[u]);
    for (j = 0; j + 1u < sizeof tb_brk_user.s[u].name && name && name[j]; j++)
        tb_brk_user.s[u].name[j] = (char)(name[j] >= 'a' && name[j] <= 'z' ? name[j] - 32 : name[j]);
    tb_brk_user.s[u].off = off, tb_brk_user.s[u].len = n;
#ifdef RFM2
    if (tb_store_smp_prepare(off, n))               /* (its room in flash, erased) */
        return -1;
#else
    memset(tb_brk_pool + off, 0xFF, n);            /* (mu-law silence) */
#endif
    in_slot = u;
    return 0;
}

void tb_brk_user_write(uint32_t off, const uint8_t *ulaw, uint32_t n)
{
    if (in_slot >= TBB_USER || off >= tb_brk_user.s[in_slot].len)
        return;
    if (n > tb_brk_user.s[in_slot].len - off)
        n = tb_brk_user.s[in_slot].len - off;
#ifdef RFM2
    tb_store_smp_write(tb_brk_user.s[in_slot].off + off, ulaw, n);
#else
    memcpy(tb_brk_pool + tb_brk_user.s[in_slot].off + off, ulaw, n);
#endif
}

void tb_brk_user_end(void)
{
    unsigned u = in_slot, na = 0;
    uint32_t n, i, k, last = 0;
    int32_t prev = 0;
    if (u >= TBB_USER)
        return;
#ifdef RFM2
#define d_at(i) POOL_AT(tb_brk_user.s[u].off + (i))
#else
    const uint8_t *d = tb_brk_pool + tb_brk_user.s[u].off;
#define d_at(i) d[i]
#endif
    n = tb_brk_user.s[u].len;
    tb_brk_user.s[u].at[na++] = 0;                 /* its hits: the level jumping over what it was (10 ms frames) */
    for (i = 0; i + 160u <= n && na < TBB_MAXSLICE; i += 160u) {
        int32_t e = 0, x;
        for (k = 0; k < 160u; k++)
            x = ulaw_dec(d_at(i + k)), e += (x < 0 ? -x : x) >> 4;
        e /= 160;
        if (e > prev * 2 + 40 && i - last > 1600u && i > 800u)
            tb_brk_user.s[u].at[na++] = i, last = i;
        prev = (prev * 3 + e) / 4;
    }
    tb_brk_user.s[u].nauto = (uint8_t)na;
    tb_brk_user.n = u + 1u;
    in_slot = TBB_USER;
#undef d_at
}

#endif
#if !TB_HAS_USER_BRK
void tb_brk_user_keep(unsigned n) { (void)n; }
int tb_brk_user_begin(unsigned u, const char *name, uint32_t n) { (void)u, (void)name, (void)n; return -1; }
void tb_brk_user_write(uint32_t off, const uint8_t *ulaw, uint32_t n) { (void)off, (void)ulaw, (void)n; }
void tb_brk_user_end(void) {}
#endif
/* sample k of break b (0 outside) */
static int32_t smp(unsigned b, int32_t k)
{
    const uint8_t *d;
    uint32_t bit, w;
    int32_t m;
    if (b >= TBB_N) {
#if TB_HAS_USER_BRK
        if (!user(b) || k < 0 || (uint32_t)k >= U(b).len)
            return 0;
        return ulaw_dec(POOL_AT(U(b).off + (uint32_t)k));
#else
        return 0;
#endif
    }
    if (k < 0 || (uint32_t)k >= TBB[b].len)
        return 0;
    d = &TBB_DATA[TBB[b].off + ((uint32_t)k / TBB_BLOCK) * (1u + 2u * TBB_BITS)];
    bit = ((uint32_t)k % TBB_BLOCK) * TBB_BITS;
    w = (uint32_t)d[1u + (bit >> 3)] | ((uint32_t)d[2u + (bit >> 3)] << 8);
    m = (int32_t)((w >> (bit & 7u)) & ((1u << TBB_BITS) - 1u));
    if (m >= 1 << (TBB_BITS - 1))
        m -= 1 << TBB_BITS;
    return tb_clamp((m * (int32_t)TBB_SC[d[0] < 48u ? d[0] : 47u]) >> 6, -32768, 32767);
}

int32_t tb_brk_peek(unsigned b, uint32_t k)
{
    int32_t s = smp(b, (int32_t)k);
    return s < 0 ? -s : s;
}

unsigned tb_brk_slices(const uint8_t *p, const tb_brk_man_t *m, unsigned i, uint32_t *a, uint32_t *b)
{
    unsigned br = p[TBB_SRC] < tb_brk_count() ? p[TBB_SRC] : 0u, div = p[TBB_DIV], n;
    uint32_t len = tb_brk_len(br);
    if (div == TBB_MAN && m && m->n) {
        n = m->n;
        i %= n;
        *a = (uint32_t)m->at[i] * 4u;
        *b = i + 1u < n ? (uint32_t)m->at[i + 1u] * 4u : len;
    } else if (div >= TBB_AUTO && user(br)) {
        n = U(br).nauto ? U(br).nauto : 1u;
        i %= n;
        *a = U(br).at[i];
        *b = i + 1u < n ? U(br).at[i + 1u] : len;
    } else if (div >= TBB_AUTO) {
        n = TBB_N && TBB[br].nauto ? TBB[br].nauto : 1u;
        i %= n;
        *a = TBB_N ? TBB[br].at[i] : 0u;
        *b = i + 1u < n ? TBB[br].at[i + 1u] : len;
    } else {
        n = 4u << (div > 3u ? 3u : div);
        i %= n;
        *a = (uint32_t)((uint64_t)len * i / n);
        *b = (uint32_t)((uint64_t)len * (i + 1u) / n);
    }
    if (*a > len)
        *a = len;
    if (*b > len || *b < *a)
        *b = len;
    return n;
}

void tb_brk_init(tb_brk_t *s) { memset(s, 0, sizeof *s); }

/* per block of 128: e^(-128 / (tau fs)), Q16, tau in us */
static uint32_t blk_decay(uint32_t us)
{
    uint32_t x = 47554467u / (us | 1u), x2, x3;
    if (x > 46000u)
        x = 46000u;
    x2 = (x * x) >> 16;
    x3 = (x2 * x) >> 16;
    return 65536u - x + (x2 >> 1) - x3 / 6u;
}
/* 0..127 -> a time: base us x 2^(v / 18) (7 octaves) */
static uint32_t time_us(unsigned v, uint32_t base) { return (uint32_t)(((uint64_t)base * tb_exp2((int32_t)v * 65536 / 18)) >> 16); }

static void prime(tb_brk_voice_t *v, unsigned br)
{
    unsigned j;
    memset(v->h, 0, sizeof v->h);
    for (j = 0; j < 5u; j++) {                         /* the next 5 samples read, the 3 before silent */
        v->h[(3u + j) & 7u] = (int16_t)smp(br, (int32_t)v->k);
        v->k += v->rev ? (uint32_t)-1 : 1u;
    }
    v->fr = 0;
}

void tb_brk_play(tb_brk_t *s, const uint8_t *p, uint32_t a, uint32_t b, unsigned note, unsigned vel, int semi)
{
    unsigned i, best = 0;
    uint32_t len = tb_brk_len(p[TBB_SRC]);
    tb_brk_voice_t *v;
    for (i = 0; i < TBB_VOICES; i++) {                 /* the ones playing: taken over; a tail rings on */
        tb_brk_voice_t *o = &s->v[i];
        if (o->live && !o->tail)
            o->out = 1;
    }
    for (i = 1; i < TBB_VOICES; i++)                   /* a free voice, or the quietest */
        if (!s->v[i].live ? s->v[best].live : s->v[best].live && s->v[i].q < s->v[best].q)
            best = i;
    v = &s->v[best];
    memset(v, 0, sizeof *v);
    if (b > len)
        b = len;
    v->rev = TB_HAS_BRK_REV && p[TBB_REV] != 0;
    if (p[TBB_MODE] == TBB_FULL)                       /* FULL: to the end of the break (REV: its start) */
        v->rev ? (a = 0) : (b = len);
    if (p[TBB_MODE] == TBB_DISC)                       /* DISC: all of it, round and round */
        a = 0, b = len, v->disc = 1;
    v->a = a, v->b = b;
    v->k = v->rev ? b - 1u : a;
    v->stop = v->rev ? a : b;
    v->loop = p[TBB_MODE] == TBB_LOOP || p[TBB_MODE] == TBB_DISC;
    v->gate = 1;
    v->note = (uint8_t)note;
    v->semi = (int8_t)tb_clamp(semi, -48, 48);
    v->vel = (uint16_t)(vel > 127u ? 32767u : vel * 258u);
    v->q = 1 << 30;
    v->live = b > a;
    tb_env_on(&v->env);
    prime(v, p[TBB_SRC]);
    s->age++;
}

uint8_t tb_brk_transport;
void tb_brk_on(tb_brk_t *s, const uint8_t *p, const tb_brk_man_t *m, unsigned note, unsigned vel)
{
    uint32_t a, b;
    if (p[TBB_MODE] == TBB_DISC && !tb_brk_transport) {   /* DISC, PLAY off: a key starts the record, again stops it */
        unsigned i, on = 0;
        for (i = 0; i < TBB_VOICES; i++)
            if (s->v[i].live && s->v[i].disc && !s->v[i].out)
                s->v[i].out = 2, s->v[i].gate = 0, on = 1, tb_env_off(&s->v[i].env);
        if (on)
            return;
    }
    tb_brk_slices(p, m, (unsigned)((int)note - 60 + p[TBB_START] + 1024), &a, &b);
    tb_brk_play(s, p, a, b, note, vel, 0);
}

void tb_brk_off(tb_brk_t *s, const uint8_t *p, unsigned note)
{
    unsigned i;
    for (i = 0; i < TBB_VOICES; i++) {
        tb_brk_voice_t *v = &s->v[i];
        if (!v->live || v->note != note || !v->gate)
            continue;
        v->gate = 0;
        if (!v->disc)                                  /* (DISC: the record turns on, the key let go) */
            tb_env_off(&v->env);
        if ((p[TBB_MODE] == TBB_GATE || p[TBB_MODE] == TBB_LOOP) && !v->out)
            v->out = 2;                                /* (ONE, FULL: a key let go changes nothing) */
    }
}

/* everything playing let go: it fades out over REL (STOP, a preview's end, the track's panic; FULL plays on otherwise) */
void tb_brk_release(tb_brk_t *s, unsigned note)  /* (note 255: all of them) */
{
    unsigned i;
    for (i = 0; i < TBB_VOICES; i++)
        if (s->v[i].live && !s->v[i].out && (note == 255u || s->v[i].note == note))
            s->v[i].gate = 0, s->v[i].out = 2, tb_env_off(&s->v[i].env);
}

int32_t tb_brk_env(const tb_brk_t *s, unsigned *stage)
{
    unsigned i, best = TBB_VOICES;
    for (i = 0; i < TBB_VOICES; i++)
        if (s->v[i].live && !s->v[i].out)
            best = i;
    if (best == TBB_VOICES) {
        *stage = 0;
        return 0;
    }
    *stage = s->v[best].env.stage;
    return s->v[best].env.lvl;
}

void tb_brk_render(tb_brk_t *s, const uint8_t *p, unsigned bpm10, const uint8_t *adsr, int32_t lfo, int32_t *out, unsigned n)
{
    unsigned br = p[TBB_SRC] < tb_brk_count() ? p[TBB_SRC] : 0u, i, j;
    uint32_t inc0 = (uint32_t)(((uint64_t)TBB_INC * tb_exp2(((int32_t)p[TBB_PITCH] - 24) * 65536 / 12)) >> 16);
    uint32_t dec = p[TBB_DECAY] >= 127 ? 65536u : blk_decay(time_us(p[TBB_DECAY], 15000u));
    uint32_t rel = blk_decay(p[TBB_REL] ? time_us(p[TBB_REL], 8000u) : 2000u);
    uint32_t cut = blk_decay(2000u);                  /* taken over: 2 ms (FULL too: the next slice is the same sound) */
    int32_t pole = p[TBB_TONE] >= 127 ? 0 : tb_clamp((int32_t)((100u * tb_exp2((int32_t)p[TBB_TONE] * 4096)) >> 16) * 5, 400, 32767);
    if (!TBB_N)
        return;
    tb_envk(&s->k, adsr);
    if (lfo)
        inc0 = (uint32_t)(((uint64_t)inc0 * tb_exp2(lfo / 12)) >> 16);
    if ((p[TBB_SYNC] || p[TBB_MODE] == TBB_DISC) && br < TBB_N && TBB[br].bpm100)   /* the tempo, as a deck: faster is higher */
        inc0 = (uint32_t)((uint64_t)inc0 * bpm10 * 10u / TBB[br].bpm100);
    for (j = 0; j < TBB_VOICES; j++) {
        tb_brk_voice_t *v = &s->v[j];
        int32_t q0, dq, g = (GAIN * (int32_t)v->vel) >> 15, ramp = -1;
        uint32_t rem, fr = v->fr, k = v->k, inc = inc0;
        if (!v->live)
            continue;
        if (v->semi)
            inc = (uint32_t)(((uint64_t)inc0 * tb_exp2(v->semi * 65536 / 12)) >> 16);
        if (inc > 8u << 24)
            inc = 8u << 24;
        if (!inc)                                     /* (pitched all the way down: still moving, never a division by 0) */
            inc = 1;
        /* what is left to read, in samples played: the end fade, or (RELEASE) the tail past the slice's end */
        rem = v->rev ? (k + 1u > v->stop ? k + 1u - v->stop : 0u) : (v->stop > k ? v->stop - k : 0u);
        if (!(v->loop && (v->gate || (v->disc && !v->out)))) {
            uint64_t ro = ((uint64_t)rem << 24) / inc;
            if (ro < n + FADE_END) {
                if (p[TBB_REL] && !v->tail && !v->out && !v->rev && !v->loop && v->stop < tb_brk_len(br)) {
                    v->stop = tb_brk_len(br);          /* the last slice rings on into what follows it */
                    v->tail = 1;
                } else
                    ramp = (int32_t)ro;
            }
        }
        q0 = v->q >> 10;
        if (dec < 65536u)
            v->q = (int32_t)(((int64_t)v->q * dec) >> 16);
        if (v->tail || v->out)                         /* the tail and a key let go: RELEASE; taken over: 2 ms */
            v->q = (int32_t)(((int64_t)v->q * (v->tail || v->out == 2 ? rel : cut)) >> 16);
        dq = ((v->q >> 10) - q0) / (int32_t)n;
        for (i = 0; i < n; i++) {
            const int16_t *kt = TB909_K[fr >> 17];
            const int16_t *h = v->h;
            unsigned w = v->w;                         /* (the ring: the oldest tap at h[w & 7]) */
            int32_t x = (h[w & 7u] * kt[0] + h[(w + 1u) & 7u] * kt[1] + h[(w + 2u) & 7u] * kt[2] +
                         h[(w + 3u) & 7u] * kt[3] + h[(w + 4u) & 7u] * kt[4] + h[(w + 5u) & 7u] * kt[5] +
                         h[(w + 6u) & 7u] * kt[6] + h[(w + 7u) & 7u] * kt[7]) >> 14;
            fr += inc;
            while (fr >= 1u << 24) {
                int32_t y = 0;
                fr -= 1u << 24;
                if (v->rev ? (k + 1u > v->stop) : (k < v->stop)) {
                    y = smp(br, (int32_t)k);
                    k += v->rev ? (uint32_t)-1 : 1u;
                } else if (v->loop && (v->gate || (v->disc && !v->out))) {   /* LOOP (DISC): back to the start */
                    k = v->rev ? v->b - 1u : v->a;
                    y = smp(br, (int32_t)k);
                    k += v->rev ? (uint32_t)-1 : 1u;
                }
                v->h[v->w & 7u] = (int16_t)y;
                v->w = (uint16_t)(v->w + 1u);
            }
            if (pole) {
                v->lp += ((x - v->lp) * pole) >> 15;
                x = v->lp;
            }
            {
                int32_t a = q0 >> 6;                   /* Q14 */
                if (ramp >= 0)
                    a = (a * tb_clamp((ramp - (int32_t)i) * 248, 0, 16384)) >> 14;
                if (v->in < 32u)
                    a = (a * (int32_t)v->in) >> 5, v->in++;
                x = tb_mul30x(x, tb_env_step(&v->env, &s->k));   /* (v22: at full, no product) */
                out[i] += (((x * a) >> 14) * g) >> 12;
            }
            q0 += dq;
        }
        v->fr = fr, v->k = k;
        if ((ramp >= 0 && ramp <= (int32_t)n) || v->q < (1 << 17) || !v->env.stage)
            v->live = 0;
    }
}
