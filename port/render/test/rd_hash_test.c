/* rd_hash_test.c: the per-draw lookups' hash (rd_hash.c rd__hash_words),
 * CPU only.
 *
 *   1. its values on a little-endian host for six inputs (no bytes, three,
 *      eight, fifteen with a seed, a DrawCB's 144 with two seeds), worked
 *      from rd_hash.c's steps (the start value, a step a word, the tail as
 *      one zero-padded word, the avalanche);
 *   2. the same bytes at every alignment 0..7 give the same value;
 *   3. the bytes after n are never read into it (other bytes there, the
 *      same value), every length 0..40;
 *   4. two seeds over the same 144 bytes give two values (the reference
 *      values of 1. differ). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_hash.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int littleEndian(void)
{
    const uint32_t one = 1u;
    unsigned char b[4];
    memcpy(b, &one, 4);
    return b[0] == 1u;
}

int main(void)
{
    printf("rd_hash_test\n");
    unsigned char seq[160];
    for (int i = 0; i < 160; i++) {
        seq[i] = (unsigned char)(i + 1); /* 1, 2, 3, ... */
    }
    /* 1. the values, worked from the steps (little-endian words) */
    if (littleEndian()) {
        static const struct {
            size_t n;
            uint32_t seed, want;
        } kWant[] = {
            {0, 0, 0xd918dd23u},   {0, 1, 0xacdbd4f4u},        {3, 0, 0x0b0381c9u},
            {8, 0, 0x7ffe438bu},   {15, 0x1234u, 0x99c56b34u}, {144, 0, 0xd6ef816cu},
            {144, 7, 0xeebfb8ffu},
        };

        for (size_t k = 0; k < sizeof(kWant) / sizeof(kWant[0]); k++) {
            const uint32_t h = rd__hash_words(seq, kWant[k].n, kWant[k].seed);
            CHECK(h == kWant[k].want, "%zu bytes, seed %u: 0x%08x, 0x%08x expected", kWant[k].n,
                  kWant[k].seed, h, kWant[k].want);
        }
    } else {
        printf("rd_hash_test: a big-endian host: the values differ by design, not checked\n");
    }
    /* 2. every alignment */
    unsigned char buf[160 + 8];
    const uint32_t ref = rd__hash_words(seq, 144, 3);
    for (int a = 0; a < 8; a++) {
        memset(buf, 0xA5, sizeof buf);
        memcpy(buf + a, seq, 144);
        const uint32_t h = rd__hash_words(buf + a, 144, 3);
        CHECK(h == ref, "alignment %d: 0x%08x, 0x%08x at 0", a, h, ref);
    }
    /* 3. nothing past n */
    for (size_t n = 0; n <= 40; n++) {
        unsigned char x[48], y[48];
        memset(x, 0x00, sizeof x);
        memset(y, 0xFF, sizeof y);
        memcpy(x, seq, n);
        memcpy(y, seq, n);
        const uint32_t hx = rd__hash_words(x, n, 9), hy = rd__hash_words(y, n, 9);
        CHECK(hx == hy, "%zu bytes: 0x%08x against 0x%08x with other bytes after them", n, hx, hy);
    }
    /* 4. the two seeds' and the empty input's values above differ */
    CHECK(rd__hash_words(seq, 144, 0) != rd__hash_words(seq, 144, 7) || !littleEndian(),
          "two seeds, one value");
    if (failures) {
        printf("rd_hash_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_hash_test: ok\n");
    return 0;
}
