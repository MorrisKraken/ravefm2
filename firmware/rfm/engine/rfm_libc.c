/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2: the C library its sound and screen code need (the bare FM-1 has none): snprintf (the formats RAVE FM uses: %d %u %s %c,
 * a width, 0-padding, a precision for strings), and the runtime's divisions. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

int vsnprintf(char *out, size_t size, const char *f, va_list ap)
{
    size_t n = 0;
#define PUT(ch) do { char c_ = (char)(ch); if (n + 1u < size) out[n] = c_; n++; } while (0)
    for (; *f; f++) {
        char tmp[12];
        const char *s;
        int len, i, width = 0, prec = -1, zero = 0, left = 0, plus = 0;
        if (*f != '%') {
            PUT(*f);
            continue;
        }
        f++;
        for (;; f++) {                                /* the flags, in any order: - 0 + (space) */
            if (*f == '-')
                left = 1;
            else if (*f == '0')
                zero = 1;
            else if (*f == '+')
                plus = 1;
            else if (*f != ' ')
                break;
        }
        if (*f == '*') {                              /* the width given as an argument */
            width = va_arg(ap, int), f++;
            if (width < 0)
                left = 1, width = -width;
        } else
            while (*f >= '0' && *f <= '9')
                width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            prec = 0, f++;
            if (*f == '*') {                          /* the precision given as an argument ("%.*s") */
                prec = va_arg(ap, int), f++;
                if (prec < 0)
                    prec = -1;
            } else
                while (*f >= '0' && *f <= '9')
                    prec = prec * 10 + (*f++ - '0');
        }
        if (left)
            zero = 0;
        while (*f == 'l' || *f == 'h' || *f == 'z')
            f++;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': {
            unsigned long v;
            int neg = 0, base = (*f == 'x' || *f == 'X') ? 16 : 10;
            if (*f == 'd' || *f == 'i') {
                long d = va_arg(ap, int);
                neg = d < 0, v = neg ? 0ul - (unsigned long)d : (unsigned long)d;
            } else
                v = va_arg(ap, unsigned);
            len = 0;
            do {
                tmp[len++] = "0123456789ABCDEF"[v % (unsigned)base];
                v /= (unsigned)base;
            } while (v && len < 11);
            {
                int sign = neg || (plus && base == 10 && (*f == 'd' || *f == 'i')), all = len + sign;
                char sc = neg ? '-' : '+';
                if (sign && zero)
                    PUT(sc);
                if (!left)
                    for (i = all; i < width; i++)
                        PUT(zero ? '0' : ' ');
                if (sign && !zero)
                    PUT(sc);
                while (len)
                    PUT(tmp[--len]);
                if (left)
                    for (i = all; i < width; i++)
                        PUT(' ');
            }
            break;
        }
        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "";
            for (len = 0; s[len] && (prec < 0 || len < prec); len++)
                ;
            if (!left)
                for (i = len; i < width; i++)
                    PUT(' ');
            for (i = 0; i < len; i++)
                PUT(s[i]);
            if (left)
                for (i = len; i < width; i++)
                    PUT(' ');
            break;
        case 'c': PUT((char)va_arg(ap, int)); break;
        case '%': PUT('%'); break;
        default: PUT('%'); if (*f) PUT(*f); else f--;
        }
    }
    if (size)
        out[n < size ? n : size - 1u] = 0;
    return (int)n;
#undef PUT
}

int snprintf(char *out, size_t size, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vsnprintf(out, size, f, ap);
    va_end(ap);
    return r;
}

/* the divisions the code generator calls for (64 bits; 32 bits too, should the CPU's own not be used): no runtime
 * library on the bare FM-1. Shift and subtract, no division in them */
#define RT __attribute__((used))
RT uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
    /* (21 Nov 2026: was 64 rounds of 64-bit shifts and compares, ~1200 cycles a division, ~25 a block for one
     *  synth note, ~90 for six tracks - 12 % of the FM-1. Now: both in 32 bits, the CPU's own division; else the
     *  rounds only over the quotient's bits, the divisor first lined up under the dividend 8 bits at a time) */
    uint64_t q = 0;
    int s = 0;
    if (!d) {
        if (rem)
            *rem = 0;
        return ~(uint64_t)0;
    }
    if (!(n >> 32) && !(d >> 32)) {
        uint32_t a = (uint32_t)n, b = (uint32_t)d, qq = a / b;
        if (rem)
            *rem = a - qq * b;
        return qq;
    }
    if (d > n) {
        if (rem)
            *rem = n;
        return 0;
    }
    while (!(d >> 56) && (d << 8) <= n)
        d <<= 8, s += 8;
    while (!(d >> 63) && (d << 1) <= n)
        d <<= 1, s++;
    for (; s >= 0; s--, d >>= 1)
        if (n >= d)
            n -= d, q |= (uint64_t)1 << s;
    if (rem)
        *rem = n;
    return q;
}
RT uint64_t __udivdi3(uint64_t a, uint64_t b) { return __udivmoddi4(a, b, 0); }
RT uint64_t __umoddi3(uint64_t a, uint64_t b)
{
    uint64_t r;
    __udivmoddi4(a, b, &r);
    return r;
}
RT int64_t __divdi3(int64_t a, int64_t b)
{
    uint64_t q = __udivmoddi4(a < 0 ? 0 - (uint64_t)a : (uint64_t)a, b < 0 ? 0 - (uint64_t)b : (uint64_t)b, 0);
    return (a < 0) != (b < 0) ? (int64_t)(0 - q) : (int64_t)q;
}
RT int64_t __moddi3(int64_t a, int64_t b)
{
    uint64_t r;
    __udivmoddi4(a < 0 ? 0 - (uint64_t)a : (uint64_t)a, b < 0 ? 0 - (uint64_t)b : (uint64_t)b, &r);
    return a < 0 ? (int64_t)(0 - r) : (int64_t)r;
}
static uint32_t udivmod32(uint32_t n, uint32_t d, uint32_t *rem)
{
    uint32_t q = 0, r = 0;
    int i;
    if (!d) {
        *rem = 0;
        return ~0u;
    }
    for (i = 31; i >= 0; i--) {
        r = r << 1 | ((n >> i) & 1u);
        if (r >= d)
            r -= d, q |= 1u << i;
    }
    *rem = r;
    return q;
}
RT uint32_t __udivsi3(uint32_t a, uint32_t b) { uint32_t r; return udivmod32(a, b, &r); }
RT uint32_t __umodsi3(uint32_t a, uint32_t b) { uint32_t r; udivmod32(a, b, &r); return r; }
RT int32_t __divsi3(int32_t a, int32_t b)
{
    uint32_t r, q = udivmod32(a < 0 ? 0u - (uint32_t)a : (uint32_t)a, b < 0 ? 0u - (uint32_t)b : (uint32_t)b, &r);
    return (a < 0) != (b < 0) ? (int32_t)(0u - q) : (int32_t)q;
}
RT int32_t __modsi3(int32_t a, int32_t b)
{
    uint32_t r;
    udivmod32(a < 0 ? 0u - (uint32_t)a : (uint32_t)a, b < 0 ? 0u - (uint32_t)b : (uint32_t)b, &r);
    return a < 0 ? (int32_t)(0u - r) : (int32_t)r;
}

/* (RAVE FM 2: no C library at all on the bare FM-1; the platform's libc.c has memset, memcpy, memcmp) */
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    if (a == b || !n)
        return d;
    if (a < b)
        while (n--)
            *a++ = *b++;
    else
        while (n--)
            a[n] = b[n];
    return d;
}
size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n && *a && *a == *b; n--)
        a++, b++;
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}
int abs(int v) { return v < 0 ? -v : v; }
char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}
