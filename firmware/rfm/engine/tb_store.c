/* SPDX-License-Identifier: GPL-3.0-only */
/* THUNDERBOX: the projects in flash (tb_store.h).
 * The packing: a zero byte is followed by how many zeros it stands for (1..255); any other byte is itself. The
 * patterns' empty steps are zeros: a project of a few patterns takes a few kilobytes. */
#include "tb_store.h"
#include "tb_engine.h"
#include "tb_break.h"
#include "fm1.h"
#include <string.h>
#include <stddef.h>

typedef struct { uint32_t magic, seq, bytes, crc; uint16_t version, bpm10; uint32_t raw; } hdr_t;
#define HMAGIC 0x32425454u                    /* "TTB2" */
#define SEC FM1_FLASH_SECTOR_BYTES

/* the samples' place: the last sectors of the flash (when it is big enough to hold them and the projects) */
#if TB_HAS_USER_BRK && defined(RFM2)
/* (RAVE FM 2) a sector for their table, then their bytes, read in place */
#define SMP_BYTES (SEC + (TBB_POOL + SEC - 1u) / SEC * SEC)
static uint32_t smp_at(void) { return fm1_flash_size() >= SMP_BYTES + 2u * TB_STORE_SLOTS * 12288u ? (fm1_flash_size() - SMP_BYTES) / SEC * SEC : 0u; }
#elif TB_HAS_USER_BRK
#define SMP_BYTES (((uint32_t)sizeof(tb_brk_user_t) + TBB_POOL + 8u + SEC - 1u) / SEC * SEC)
static uint32_t smp_at(void) { return fm1_flash_size() >= 4u * SMP_BYTES ? fm1_flash_size() - SMP_BYTES : 0u; }
#else
static uint32_t smp_at(void) { return 0u; }   /* (no samples of the user's: the projects have it all) */
#endif
/* where the projects start (the FM-1: after RAVE FM's pack, its sounds and code, at the user area's start) */
uint32_t tb_store_base;
#if !TB_LITE
static uint32_t half_bytes(void)               /* each place: two halves, whole sectors */
{
    uint32_t end = smp_at() ? smp_at() : fm1_flash_size(), b = (tb_store_base + SEC - 1u) / SEC * SEC;
    uint32_t h = end > b ? (end - b) / (2u * TB_STORE_SLOTS) : 0u;
    return h / SEC * SEC;
}
static uint32_t half_at(unsigned slot, unsigned h)
{
    return (tb_store_base + SEC - 1u) / SEC * SEC + (2u * slot + h) * half_bytes();
}

#endif

static uint32_t crc32(const uint8_t *p, uint32_t n, uint32_t c)
{
    uint32_t i, k;
    c = ~c;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

#if !TB_LITE
static int half_hdr(unsigned slot, unsigned h, hdr_t *o)
{
    if (!half_bytes() || fm1_flash_read(half_at(slot, h), o, sizeof *o) != FM1_FLASH_OK)
        return 0;
    return o->magic == HMAGIC &&
           ((o->raw == sizeof(tb_project_t) && o->version == TB_VERSION) || (o->raw == TB_V6_BYTES && o->version == 6u) ||
            (o->raw == TB_V7_BYTES && o->version == 7u) || (o->raw == TB_V8_BYTES && o->version == 8u) ||
            (o->raw == TB_V9_BYTES && o->version == 9u) || (o->raw == TB_V10_BYTES && o->version == 10u) ||
            (o->raw == TB_V11_BYTES && o->version == 11u)) &&
           o->bytes <= half_bytes() - sizeof(hdr_t);
}
/* the newer whole half of a place (-1: none); its header */
static int newest(unsigned slot, hdr_t *o)
{
    hdr_t h[2];
    int ok0 = half_hdr(slot, 0, &h[0]), ok1 = half_hdr(slot, 1, &h[1]), b;
    if (!ok0 && !ok1)
        return -1;
    b = !ok0 ? 1 : !ok1 ? 0 : (int32_t)(h[1].seq - h[0].seq) > 0 ? 1 : 0;
    *o = h[b];
    return b;
}

#endif

/* ---------------------------------------------------------------- the writing --- */
static uint8_t wbuf[256];
static uint32_t wpos, wbase, wcrc, wlen, wcap;    /* (wcap: the room for the packed bytes) */
static int werr;
static void flush(void)
{
    if (!wpos || werr)
        return;
    if (wlen + wpos > wcap)
        werr = -4;
    else if (fm1_flash_write(wbase + sizeof(hdr_t) + wlen, wbuf, wpos) != FM1_FLASH_OK)
        werr = -3;
    wcrc = crc32(wbuf, wpos, wcrc), wlen += wpos, wpos = 0;
}
static int wcount;                               /* (1: only count the packed bytes, into wlen) */
static void put(uint8_t b)
{
    if (wcount) {
        wlen++;
        return;
    }
    wbuf[wpos++] = b;
    if (wpos == sizeof wbuf)
        flush();
}

#if !TB_LITE
static uint32_t global_seq(void)
{
    unsigned s, hh;
    uint32_t best = 0;
    hdr_t h;
    for (s = 0; s < TB_STORE_SLOTS; s++)
        for (hh = 0; hh < 2u; hh++)
            if (half_hdr(s, hh, &h) && (int32_t)(h.seq - best) > 0)
                best = h.seq;
    return best;
}

#endif
static void pack_proj(void)
{
    unsigned i, run = 0;
    const uint8_t *p = (const uint8_t *)&tb_proj;
    for (i = 0; i < sizeof tb_proj && !werr; i++) {
        if (!p[i]) {
            if (++run == 255u)
                put(0), put(255), run = 0;
            continue;
        }
        if (run)
            put(0), put((uint8_t)run), run = 0;
        put(p[i]);
    }
    if (run)
        put(0), put((uint8_t)run);
}

#if !TB_LITE
int tb_store_save(unsigned slot)
{
    hdr_t h, w;
    int b;
    unsigned i;
    if (slot >= TB_STORE_SLOTS || half_bytes() < SEC)
        return -1;
    b = newest(slot, &h);
    b = b < 0 ? 0 : !b;                               /* the other half (the older one) takes it */
    wbase = half_at(slot, (unsigned)b), wpos = 0, wcrc = 0, wlen = 0, werr = 0, wcap = half_bytes() - (uint32_t)sizeof(hdr_t);
    for (i = 0; i < half_bytes(); i += SEC)
        if (fm1_flash_erase(wbase + i) != FM1_FLASH_OK)
            return -2;
    pack_proj();
    flush();
    if (werr)
        return werr;
    memset(&w, 0, sizeof w);
    w.magic = HMAGIC, w.seq = global_seq() + 1u, w.bytes = wlen, w.crc = wcrc, w.version = TB_VERSION;
    w.bpm10 = tb_proj.bpm10, w.raw = sizeof(tb_project_t);
    if (fm1_flash_write(wbase, &w, sizeof w) != FM1_FLASH_OK)   /* the header last: the half counts once whole */
        return -3;
    return 0;
}

#endif

/* ---------------------------------------------------------------- the reading --- */
static int unpack(uint32_t base, const hdr_t *h, int check)
{
    uint8_t buf[256], *o = (uint8_t *)&tb_proj;
    uint32_t done = 0, c = 0, out = 0;
    int zero = 0;
    while (done < h->bytes) {
        uint32_t k = h->bytes - done < sizeof buf ? h->bytes - done : sizeof buf, i;
        if (fm1_flash_read(base + done, buf, k) != FM1_FLASH_OK)
            return 0;
        if (check)
            c = crc32(buf, k, c);
        else
            for (i = 0; i < k; i++) {
                if (zero) {
                    if (out + buf[i] > h->raw)
                        return 0;
                    memset(o + out, 0, buf[i]), out += buf[i], zero = 0;
                } else if (!buf[i])
                    zero = 1;
                else {
                    if (out >= h->raw)
                        return 0;
                    o[out++] = buf[i];
                }
            }
        done += k;
    }
    return check ? c == h->crc : out == h->raw;
}

static void loaded(void)                          /* (a project just read in: made whole) */
{
    if (tb_proj.version < 11u)                    /* (its steps: 6 bytes then, 8 in version 10) */
        tb_engine_widen_steps(tb_proj.version < 10u ? 6u : 8u);
    if (tb_proj.version < 12u)                    /* (the patterns of version 11: every track 64 steps on the FM-1) */
        tb_engine_relayout();
    if (tb_proj.version != TB_VERSION)            /* an older project: what it lacks starts off */
        tb_engine_upgrade(tb_proj.version);
    if (tb_proj.metro_vol > 4u) {                 /* (saved before the count-in: METRO ON / RECORDING, a volume) */
        tb_proj.metro = tb_proj.metro == 1u ? 2u : tb_proj.metro == 2u ? 6u : 0u;
        tb_proj.metro_vol = 0;
    }
    {                                             /* (no riser sound set: the one its page shows) */
        unsigned k, any = 0;
        for (k = 0; k < TB_NRISER; k++)
            any |= tb_proj.riser_lvl[k];
        if (!any)
            tb_proj.riser_lvl[tb_proj.riser_type % TB_NRISER] = 100;
    }
    {                                             /* (the effects' order: each once, or the usual one) */
        unsigned t, j, seen;
        for (t = 0; t < TB_TRACKS; t++) {
            for (j = 0, seen = 0; j < 5u; j++)
                seen |= tb_proj.route_ord[t][j] < TB_NFX ? 1u << tb_proj.route_ord[t][j] : 0u;
            if (seen != 31u)
                for (j = 0; j < 5u; j++)
                    tb_proj.route_ord[t][j] = (uint8_t)j;
        }
    }
}

#if !TB_LITE
int tb_store_load(unsigned slot)
{
    hdr_t h[2];
    int ok[2], b, k;
    if (slot >= TB_STORE_SLOTS)
        return 0;
    ok[0] = half_hdr(slot, 0, &h[0]), ok[1] = half_hdr(slot, 1, &h[1]);
    b = !ok[0] ? 1 : !ok[1] ? 0 : (int32_t)(h[1].seq - h[0].seq) > 0 ? 1 : 0;
    for (k = 0; k < 2 && ok[b]; k++, b = !b) {        /* the newest whole one (checked before tb_proj is touched) */
        if (!ok[b] || !unpack(half_at(slot, (unsigned)b) + sizeof(hdr_t), &h[b], 1))
            continue;
        if (!unpack(half_at(slot, (unsigned)b) + sizeof(hdr_t), &h[b], 0) || tb_proj.magic != TB_MAGIC || tb_proj.version != h[b].version)
            return 0;
        loaded();
        return 1;
        return 0;                                     /* (cannot happen once the CRC held) */
    }
    return 0;
}

int tb_store_load_last(unsigned *slot)
{
    unsigned s, best = 0;
    uint32_t seq = 0;
    int any = 0;
    hdr_t h;
    for (s = 0; s < TB_STORE_SLOTS; s++)
        if (newest(s, &h) >= 0 && (!any || (int32_t)(h.seq - seq) > 0))
            any = 1, seq = h.seq, best = s;
    if (!any || !tb_store_load(best))
        return 0;
    if (slot)
        *slot = best;
    return 1;
}

int tb_store_name(unsigned slot, char out[9])
{
    hdr_t h;
    uint8_t buf[128];
    uint32_t base, done = 0, pos = 0, at = (uint32_t)offsetof(tb_project_t, pname);
    int b, zero = 0;
    memset(out, 0, 9);
    if (slot >= TB_STORE_SLOTS || (b = newest(slot, &h)) < 0 || h.version < 12u || h.raw != sizeof(tb_project_t))
        return 0;
    base = half_at(slot, (unsigned)b) + sizeof(hdr_t);
    while (done < h.bytes && pos < at + 8u) {         /* (the packed bytes counted along: the name's 8 caught) */
        uint32_t k = h.bytes - done < sizeof buf ? h.bytes - done : sizeof buf, i;
        if (fm1_flash_read(base + done, buf, k) != FM1_FLASH_OK)
            return 0;
        for (i = 0; i < k && pos < at + 8u; i++) {
            if (zero)
                pos += buf[i], zero = 0;              /* (a run of zeros: the name's 0s, out already) */
            else if (!buf[i])
                zero = 1;
            else {
                if (pos >= at && pos < at + 8u)
                    out[pos - at] = (char)buf[i];
                pos++;
            }
        }
        done += k;
    }
    return out[0] != 0;
}
int tb_store_info(unsigned slot, unsigned *bpm10, unsigned *bytes)
{
    hdr_t h;
    if (slot >= TB_STORE_SLOTS || newest(slot, &h) < 0)
        return 0;
    if (bpm10)
        *bpm10 = h.bpm10;
    if (bytes)
        *bytes = h.bytes;
    return 1;
}

#else
/* ------------------------------------------------- the lite build's pool of sectors ---
 * The space the FM-1 leaves after RAVE FM's pack (about 25 sectors) is shared by the eight places: a save takes
 * just the sectors its packed project needs, wherever they are free (never on a place's last project, so a save
 * cut short leaves it whole), its header last. A place's project: its newest whole record. */
typedef struct { hdr_t h; uint32_t slot; } phdr_t;
#define PMAGIC 0x33425454u                    /* "TTB3" */
#define PMAX 256u                             /* (sectors at most) */
static uint32_t pool_at(void) { return (tb_store_base + SEC - 1u) / SEC * SEC; }
static unsigned pool_n(void)
{
    uint32_t a = pool_at(), e = fm1_flash_size(), n = e > a ? (e - a) / SEC : 0u;
    return n > PMAX ? PMAX : (unsigned)n;
}
static unsigned rec_secs(uint32_t bytes) { return (unsigned)((sizeof(phdr_t) + bytes + SEC - 1u) / SEC); }
/* the record whose header starts sector i (0: none) */
static int rec(unsigned i, phdr_t *o)
{
    const hdr_t *h = &o->h;
    if (fm1_flash_read(pool_at() + i * SEC, o, sizeof *o) != FM1_FLASH_OK)
        return 0;
    return h->magic == PMAGIC && o->slot < TB_STORE_SLOTS && h->seq &&
           ((h->raw == sizeof(tb_project_t) && h->version == TB_VERSION) || (h->raw == TB_V6_BYTES && h->version == 6u) ||
            (h->raw == TB_V7_BYTES && h->version == 7u) || (h->raw == TB_V8_BYTES && h->version == 8u) ||
            (h->raw == TB_V9_BYTES && h->version == 9u) || (h->raw == TB_V10_BYTES && h->version == 10u) ||
            (h->raw == TB_V11_BYTES && h->version == 11u)) &&
           h->bytes <= h->raw * 2u && i + rec_secs(h->bytes) <= pool_n();
}
/* a place's newest record older than seq `below` (0: any): its sector (-1: none), its header */
static int pnewest(unsigned slot, uint32_t below, phdr_t *o)
{
    unsigned i, n = pool_n();
    int best = -1;
    phdr_t r;
    for (i = 0; i < n; i++)
        if (rec(i, &r) && r.slot == slot && (!below || (int32_t)(below - r.h.seq) > 0) &&
            (best < 0 || (int32_t)(r.h.seq - o->h.seq) > 0))
            best = (int)i, *o = r;
    return best;
}
static uint32_t pseq(unsigned *last_end)     /* the newest seq of all; where its record ends */
{
    unsigned i, n = pool_n();
    uint32_t best = 0;
    phdr_t r;
    for (i = 0; i < n; i++)
        if (rec(i, &r) && (int32_t)(r.h.seq - best) > 0) {
            best = r.h.seq;
            if (last_end)
                *last_end = (i + rec_secs(r.h.bytes)) % n;
        }
    return best;
}

int tb_store_save(unsigned slot)
{
    uint8_t busy[PMAX];
    unsigned n = pool_n(), need, s, i, k, from = 0, pass;
    int at = -1, b;
    uint32_t seq;
    phdr_t w;
    if (slot >= TB_STORE_SLOTS || !n)
        return -1;
    wcount = 1, wlen = 0, werr = 0;                  /* its packed size */
    pack_proj();
    wcount = 0;
    need = rec_secs(wlen);
    seq = pseq(&from);
    for (pass = 0; pass < 2u && at < 0; pass++) {    /* free: no place's last project on it (then: but this place's) */
        memset(busy, 0, sizeof busy);
        for (s = 0; s < TB_STORE_SLOTS; s++)
            if ((s != slot || !pass) && (b = pnewest(s, 0, &w)) >= 0)
                for (i = 0; i < rec_secs(w.h.bytes); i++)
                    busy[(unsigned)b + i] = 1;
        for (k = 0; k < n && at < 0; k++) {          /* (from after the last save: the sectors wear evenly) */
            unsigned st = (from + k) % n;
            if (st + need > n)
                continue;
            for (i = 0; i < need && !busy[st + i]; i++)
                ;
            if (i == need)
                at = (int)st;
        }
    }
    if (at < 0)
        return -4;
    for (i = 0; i < need; i++)
        if (fm1_flash_erase(pool_at() + ((unsigned)at + i) * SEC) != FM1_FLASH_OK)
            return -2;
    wbase = pool_at() + (unsigned)at * SEC + (uint32_t)(sizeof(phdr_t) - sizeof(hdr_t));
    wcap = need * SEC - (uint32_t)sizeof(phdr_t), wpos = 0, wcrc = 0, wlen = 0, werr = 0;
    pack_proj();
    flush();
    if (werr)
        return werr;
    memset(&w, 0, sizeof w);
    w.h.magic = PMAGIC, w.h.seq = seq + 1u, w.h.bytes = wlen, w.h.crc = wcrc, w.h.version = TB_VERSION;
    w.h.bpm10 = tb_proj.bpm10, w.h.raw = sizeof(tb_project_t), w.slot = slot;
    if (fm1_flash_write(pool_at() + (unsigned)at * SEC, &w, sizeof w) != FM1_FLASH_OK)   /* the header last */
        return -3;
    return 0;
}

int tb_store_load(unsigned slot)
{
    phdr_t r;
    uint32_t below = 0;
    int i, k;
    if (slot >= TB_STORE_SLOTS)
        return 0;
    for (k = 0; k < 4 && (i = pnewest(slot, below, &r)) >= 0; k++, below = r.h.seq) {   /* the newest whole one */
        uint32_t data = pool_at() + (unsigned)i * SEC + (uint32_t)sizeof(phdr_t);
        if (!unpack(data, &r.h, 1))
            continue;
        if (!unpack(data, &r.h, 0) || tb_proj.magic != TB_MAGIC || tb_proj.version != r.h.version)
            return 0;
        loaded();
        return 1;
    }
    return 0;
}

int tb_store_load_last(unsigned *slot)
{
    unsigned s, best = 0;
    uint32_t seq = 0;
    int any = 0;
    phdr_t r;
    for (s = 0; s < TB_STORE_SLOTS; s++)
        if (pnewest(s, 0, &r) >= 0 && (!any || (int32_t)(r.h.seq - seq) > 0))
            any = 1, seq = r.h.seq, best = s;
    if (!any || !tb_store_load(best))
        return 0;
    if (slot)
        *slot = best;
    return 1;
}

int tb_store_name(unsigned slot, char out[9]) { (void)slot; memset(out, 0, 9); return 0; }   /* (the lite build: none) */
int tb_store_info(unsigned slot, unsigned *bpm10, unsigned *bytes)
{
    phdr_t r;
    if (slot >= TB_STORE_SLOTS || pnewest(slot, 0, &r) < 0)
        return 0;
    if (bpm10)
        *bpm10 = r.h.bpm10;
    if (bytes)
        *bytes = r.h.bytes;
    return 1;
}
#endif

#if TB_HAS_USER_BRK && defined(RFM2)
/* ---------------------------------------------------------------- the samples --- */
int tb_store_smp_prepare(uint32_t off, uint32_t n)
{
    uint32_t at = smp_at(), a, e;
    if (!at || off + n > TBB_POOL)
        return -1;
    a = at + SEC + off, e = a + n;                     /* (a sample starts on a sector: tb_break.c) */
    for (; a < e; a += SEC)
        if (fm1_flash_erase(a) != FM1_FLASH_OK)
            return -2;
    tb_brk_rom = fm1_flash_ptr(at + SEC);
    return 0;
}
void tb_store_smp_write(uint32_t off, const uint8_t *p, uint32_t n)
{
    uint32_t at = smp_at();
    if (at && off + n <= TBB_POOL)
        fm1_flash_write(at + SEC + off, p, n);
}
int tb_store_samples_save(void)                       /* the table: its sector rewritten (the bytes are in place) */
{
    uint32_t at = smp_at(), h[2];
    if (!at)
        return -1;
    if (fm1_flash_erase(at) != FM1_FLASH_OK)
        return -2;
    if (fm1_flash_write(at + 8u, &tb_brk_user, sizeof tb_brk_user) != FM1_FLASH_OK)
        return -3;
    h[0] = 0x52464D53u, h[1] = crc32((const uint8_t *)&tb_brk_user, sizeof tb_brk_user, 0);   /* "SMFR" */
    return fm1_flash_write(at, h, sizeof h) == FM1_FLASH_OK ? 0 : -3;
}
int tb_store_samples_load(void)
{
    uint32_t at = smp_at(), h[2];
    tb_brk_rom = at ? fm1_flash_ptr(at + SEC) : 0;
    if (!at || fm1_flash_read(at, h, sizeof h) != FM1_FLASH_OK || h[0] != 0x52464D53u ||
        fm1_flash_read(at + 8u, &tb_brk_user, sizeof tb_brk_user) != FM1_FLASH_OK ||
        crc32((const uint8_t *)&tb_brk_user, sizeof tb_brk_user, 0) != h[1] || tb_brk_user.n > TBB_USER) {
        memset(&tb_brk_user, 0, sizeof tb_brk_user);
        return 0;
    }
    return 1;
}
#elif TB_HAS_USER_BRK
/* ---------------------------------------------------------------- the samples --- */
int tb_store_samples_save(void)
{
    uint32_t at = smp_at(), used = 0, i, crc;
    unsigned k;
    if (!at)
        return -1;
    for (k = 0; k < tb_brk_user.n && k < TBB_USER; k++)
        if (tb_brk_user.s[k].off + tb_brk_user.s[k].len > used)
            used = tb_brk_user.s[k].off + tb_brk_user.s[k].len;
    for (i = 0; i < SMP_BYTES; i += SEC)
        if (fm1_flash_erase(at + i) != FM1_FLASH_OK)
            return -2;
    crc = crc32((const uint8_t *)&tb_brk_user, sizeof tb_brk_user, 0);
    crc = crc32(tb_brk_pool, used, crc);
    if (fm1_flash_write(at + 8u, &tb_brk_user, sizeof tb_brk_user) != FM1_FLASH_OK ||
        (used && fm1_flash_write(at + 8u + sizeof tb_brk_user, tb_brk_pool, used) != FM1_FLASH_OK))
        return -3;
    {                                                 /* (the length and the CRC last) */
        uint32_t h[2] = {used, crc};
        if (fm1_flash_write(at, h, sizeof h) != FM1_FLASH_OK)
            return -3;
    }
    return 0;
}

int tb_store_samples_load(void)
{
    uint32_t at = smp_at(), h[2];
    if (!at || fm1_flash_read(at, h, sizeof h) != FM1_FLASH_OK || h[0] > TBB_POOL)
        return 0;
    if (fm1_flash_read(at + 8u, &tb_brk_user, sizeof tb_brk_user) != FM1_FLASH_OK ||
        fm1_flash_read(at + 8u + sizeof tb_brk_user, tb_brk_pool, h[0]) != FM1_FLASH_OK)
        return 0;
    if (crc32(tb_brk_pool, h[0], crc32((const uint8_t *)&tb_brk_user, sizeof tb_brk_user, 0)) != h[1] || tb_brk_user.n > TBB_USER) {
        memset(&tb_brk_user, 0, sizeof tb_brk_user);
        return 0;
    }
    return 1;
}
#else
int tb_store_samples_save(void) { return -1; }
int tb_store_samples_load(void) { return 0; }
#endif
