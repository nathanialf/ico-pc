/*
 * port/render/xxh3.c
 *
 * xxh3_64 (xxh3.h): XXH3_64bits of xxHash v0.8.2 with seed 0 and
 * the default secret, the hash PCSX2 names replacement textures with
 * (GSTextureCache.cpp, GSXXH3_64bits).  A reimplementation of the
 * algorithm in plain C, scalar only, written from xxhash.h; the default
 * secret below is xxhash.h's XXH3_kSecret.  The results equal xxHash's on
 * its own sanity vectors (texpack_name_test).  xxHash's notice:
 *
 * xxHash - Extremely Fast Hash algorithm
 * Copyright (C) 2012-2021 Yann Collet
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at:
 *   - xxHash homepage: https://www.xxhash.com
 *   - xxHash source repository: https://github.com/Cyan4973/xxHash
 */
#include <stddef.h>
#include <stdint.h>
#include "xxh3.h"

#define P32_1 0x9E3779B1u
#define P32_2 0x85EBCA77u
#define P32_3 0xC2B2AE3Du
#define P64_1 0x9E3779B185EBCA87ull
#define P64_2 0xC2B2AE3D27D4EB4Full
#define P64_3 0x165667B19E3779F9ull
#define P64_4 0x85EBCA77C2B2AE63ull
#define P64_5 0x27D4EB2F165667C5ull
#define PMX_1 0x165667919E3779F9ull
#define PMX_2 0x9FB21C651E98DF25ull

#define SECRET_SIZE 192
#define STRIPE_LEN 64
#define ACC_NB 8
#define STRIPES_PER_BLOCK ((SECRET_SIZE - STRIPE_LEN) / 8) /* 16 */
#define BLOCK_LEN (STRIPE_LEN * STRIPES_PER_BLOCK)         /* 1024 */
#define MIDSIZE_MAX 240

/* xxhash.h's XXH3_kSecret, as a string so no integer table is needed */
static const char kSecret[SECRET_SIZE + 1] =
    "\xb8\xfe\x6c\x39\x23\xa4\x4b\xbe\x7c\x01\x81\x2c\xf7\x21\xad\x1c"
    "\xde\xd4\x6d\xe9\x83\x90\x97\xdb\x72\x40\xa4\xa4\xb7\xb3\x67\x1f"
    "\xcb\x79\xe6\x4e\xcc\xc0\xe5\x78\x82\x5a\xd0\x7d\xcc\xff\x72\x21"
    "\xb8\x08\x46\x74\xf7\x43\x24\x8e\xe0\x35\x90\xe6\x81\x3a\x26\x4c"
    "\x3c\x28\x52\xbb\x91\xc3\x00\xcb\x88\xd0\x65\x8b\x1b\x53\x2e\xa3"
    "\x71\x64\x48\x97\xa2\x0d\xf9\x4e\x38\x19\xef\x46\xa9\xde\xac\xd8"
    "\xa8\xfa\x76\x3f\xe3\x9c\x34\x3f\xf9\xdc\xbb\xc7\xc7\x0b\x4f\x1d"
    "\x8a\x51\xe0\x4b\xcd\xb4\x59\x31\xc8\x9f\x7e\xc9\xd9\x78\x73\x64"
    "\xea\xc5\xac\x83\x34\xd3\xeb\xc3\xc5\x81\xa0\xff\xfa\x13\x63\xeb"
    "\x17\x0d\xdd\x51\xb7\xf0\xda\x49\xd3\x16\x55\x26\x29\xd4\x68\x9e"
    "\x2b\x16\xbe\x58\x7d\x47\xa1\xfc\x8f\xf8\xb8\xd1\x7a\xd0\x31\xce"
    "\x45\xcb\x3a\x8f\x95\x16\x04\x28\xaf\xd7\xfb\xca\xbb\x4b\x40\x7e";

/* little-endian reads whatever the host */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static uint64_t rotl64(uint64_t x, int r)
{
    return (x << r) | (x >> (64 - r));
}

static uint64_t swap64(uint64_t x)
{
    x = ((x << 8) & 0xFF00FF00FF00FF00ull) | ((x >> 8) & 0x00FF00FF00FF00FFull);
    x = ((x << 16) & 0xFFFF0000FFFF0000ull) | ((x >> 16) & 0x0000FFFF0000FFFFull);
    return (x << 32) | (x >> 32);
}

/* the 128-bit product of a and b, its two halves xored (XXH3_mul128_fold64),
   from 32-bit pieces so it needs no compiler extension */
static uint64_t mulFold64(uint64_t a, uint64_t b)
{
    uint64_t lolo = (a & 0xFFFFFFFFu) * (b & 0xFFFFFFFFu);
    uint64_t hilo = (a >> 32) * (b & 0xFFFFFFFFu);
    uint64_t lohi = (a & 0xFFFFFFFFu) * (b >> 32);
    uint64_t hihi = (a >> 32) * (b >> 32);
    uint64_t cross = (lolo >> 32) + (hilo & 0xFFFFFFFFu) + lohi;
    uint64_t hi = (hilo >> 32) + (cross >> 32) + hihi;
    uint64_t lo = (cross << 32) | (lolo & 0xFFFFFFFFu);
    return lo ^ hi;
}

static uint64_t xxh64Avalanche(uint64_t h)
{
    h ^= h >> 33;
    h *= P64_2;
    h ^= h >> 29;
    h *= P64_3;
    h ^= h >> 32;
    return h;
}

static uint64_t avalanche(uint64_t h)
{
    h ^= h >> 37;
    h *= PMX_1;
    h ^= h >> 32;
    return h;
}

static uint64_t rrmxmx(uint64_t h, uint64_t len)
{
    h ^= rotl64(h, 49) ^ rotl64(h, 24);
    h *= PMX_2;
    h ^= (h >> 35) + len;
    h *= PMX_2;
    return h ^ (h >> 28);
}

static uint64_t mix16(const uint8_t *in, const uint8_t *sec)
{
    return mulFold64(rd64(in) ^ rd64(sec), rd64(in + 8) ^ rd64(sec + 8));
}

static uint64_t len0to16(const uint8_t *in, size_t len, const uint8_t *sec)
{
    if (len > 8) {
        uint64_t lo = rd64(in) ^ (rd64(sec + 24) ^ rd64(sec + 32));
        uint64_t hi = rd64(in + len - 8) ^ (rd64(sec + 40) ^ rd64(sec + 48));
        return avalanche(len + swap64(lo) + hi + mulFold64(lo, hi));
    }
    if (len >= 4) {
        uint64_t in64 = rd32(in + len - 4) + ((uint64_t)rd32(in) << 32);
        return rrmxmx(in64 ^ (rd64(sec + 8) ^ rd64(sec + 16)), len);
    }
    if (len > 0) {
        uint32_t combined = (uint32_t)in[0] << 16 | (uint32_t)in[len >> 1] << 24 |
                            (uint32_t)in[len - 1] | (uint32_t)len << 8;
        return xxh64Avalanche((uint64_t)combined ^ (uint64_t)(rd32(sec) ^ rd32(sec + 4)));
    }
    return xxh64Avalanche(rd64(sec + 56) ^ rd64(sec + 64));
}

static uint64_t len17to128(const uint8_t *in, size_t len, const uint8_t *sec)
{
    uint64_t acc = len * P64_1;

    if (len > 32) {
        if (len > 64) {
            if (len > 96) {
                acc += mix16(in + 48, sec + 96);
                acc += mix16(in + len - 64, sec + 112);
            }
            acc += mix16(in + 32, sec + 64);
            acc += mix16(in + len - 48, sec + 80);
        }
        acc += mix16(in + 16, sec + 32);
        acc += mix16(in + len - 32, sec + 48);
    }
    acc += mix16(in, sec);
    acc += mix16(in + len - 16, sec + 16);
    return avalanche(acc);
}

static uint64_t len129to240(const uint8_t *in, size_t len, const uint8_t *sec)
{
    uint64_t acc = len * P64_1;
    size_t rounds = len / 16;
    size_t i;

    for (i = 0; i < 8; i++) {
        acc += mix16(in + 16 * i, sec + 16 * i);
    }
    acc = avalanche(acc);
    for (i = 8; i < rounds; i++) {
        acc += mix16(in + 16 * i, sec + 16 * (i - 8) + 3); /* XXH3_MIDSIZE_STARTOFFSET */
    }
    /* XXH3_SECRET_SIZE_MIN - XXH3_MIDSIZE_LASTOFFSET */
    acc += mix16(in + len - 16, sec + 136 - 17);
    return avalanche(acc);
}

static void accumulate512(uint64_t *acc, const uint8_t *in, const uint8_t *sec)
{
    for (int i = 0; i < ACC_NB; i++) {
        uint64_t v = rd64(in + 8 * i);
        uint64_t k = v ^ rd64(sec + 8 * i);
        acc[i ^ 1] += v;
        acc[i] += (k & 0xFFFFFFFFu) * (k >> 32);
    }
}

static void scramble(uint64_t *acc, const uint8_t *sec)
{
    for (int i = 0; i < ACC_NB; i++) {
        uint64_t a = acc[i];
        a ^= a >> 47;
        a ^= rd64(sec + 8 * i);
        acc[i] = a * P32_1;
    }
}

static uint64_t hashLong(const uint8_t *in, size_t len, const uint8_t *sec)
{
    uint64_t acc[ACC_NB] = {P32_3, P64_1, P64_2, P64_3, P64_4, P32_2, P64_5, P32_1};
    size_t blocks = (len - 1) / BLOCK_LEN;
    size_t n;
    size_t s;
    size_t stripes;
    uint64_t r;

    for (n = 0; n < blocks; n++) {
        for (s = 0; s < STRIPES_PER_BLOCK; s++) {
            accumulate512(acc, in + n * BLOCK_LEN + s * STRIPE_LEN, sec + s * 8);
        }
        scramble(acc, sec + SECRET_SIZE - STRIPE_LEN);
    }
    /* the last partial block, then the last stripe (which may overlap it) */
    stripes = ((len - 1) - BLOCK_LEN * blocks) / STRIPE_LEN;
    for (s = 0; s < stripes; s++) {
        accumulate512(acc, in + blocks * BLOCK_LEN + s * STRIPE_LEN, sec + s * 8);
    }
    accumulate512(acc, in + len - STRIPE_LEN, sec + SECRET_SIZE - STRIPE_LEN - 7);

    /* merge, from XXH3_SECRET_MERGEACCS_START (11) */
    r = len * P64_1;
    for (int i = 0; i < 4; i++) {
        r += mulFold64(acc[2 * i] ^ rd64(sec + 11 + 16 * i),
                       acc[2 * i + 1] ^ rd64(sec + 11 + 16 * i + 8));
    }
    return avalanche(r);
}

uint64_t xxh3_64(const void *p, size_t n)
{
    const uint8_t *in = p;
    const uint8_t *sec = (const uint8_t *)kSecret;

    if (n <= 16) {
        return len0to16(in, n, sec);
    }
    if (n <= 128) {
        return len17to128(in, n, sec);
    }
    if (n <= MIDSIZE_MAX) {
        return len129to240(in, n, sec);
    }
    return hashLong(in, n, sec);
}
