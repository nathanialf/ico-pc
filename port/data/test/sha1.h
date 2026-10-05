/*
 * port/data/test/sha1.h
 *
 * SHA-1 (FIPS 180-4, section 6.1) for the tests' hash checks.  Test-only:
 * the port itself verifies the disc in Phase 5 with its own code.
 */
#ifndef ICO_PORT_TEST_SHA1_H
#define ICO_PORT_TEST_SHA1_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t h[5];
    uint64_t len;
    unsigned char buf[64];
    size_t n;
} Sha1;

static uint32_t sha1_rol(uint32_t x, int s)
{
    return (x << s) | (x >> (32 - s));
}

static void sha1_block(Sha1 *c, const unsigned char *p)
{
    uint32_t w[80];
    uint32_t a, b, d, e, f, k, t, cc;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = c->h[0];
    b = c->h[1];
    cc = c->h[2];
    d = c->h[3];
    e = c->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20) {
            f = (b & cc) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ cc ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & cc) | (b & d) | (cc & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ cc ^ d;
            k = 0xCA62C1D6u;
        }
        t = sha1_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = sha1_rol(b, 30);
        b = a;
        a = t;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
}

static void sha1_init(Sha1 *c)
{
    c->h[0] = 0x67452301u;
    c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->len = 0;
    c->n = 0;
}

static void sha1_update(Sha1 *c, const void *data, size_t len)
{
    const unsigned char *p = data;

    c->len += len;
    while (len > 0) {
        size_t take = 64 - c->n;

        if (take > len) {
            take = len;
        }
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 64) {
            sha1_block(c, c->buf);
            c->n = 0;
        }
    }
}

/* the digest as 40 lower-case hex digits */
static void sha1_final_hex(Sha1 *c, char out[41])
{
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80;
    unsigned char lenb[8];
    int i;

    sha1_update(c, &pad, 1);
    pad = 0;
    while (c->n != 56) {
        sha1_update(c, &pad, 1);
    }
    for (i = 0; i < 8; i++) {
        lenb[i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    sha1_update(c, lenb, 8);
    for (i = 0; i < 5; i++) {
        snprintf(out + 8 * i, 9, "%08x", (unsigned)c->h[i]);
    }
}

#endif /* ICO_PORT_TEST_SHA1_H */
