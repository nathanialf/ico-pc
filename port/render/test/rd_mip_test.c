/* rd_mip_test: the Enhanced mip chain's size (rdtex_mip_chain_bytes) and
 * rdtex_build_mip_chain staying inside it for non-square power-of-two
 * textures, whose 1-texel-high levels the old w*h*4/3 sizing missed
 * (128x4 needs 764 bytes, got 746).  CPU only.
 *
 * Each chain is built into a buffer of exactly rdtex_mip_chain_bytes bytes
 * followed by guard bytes; a guard that changed is an overflow (ASan builds
 * also catch it on the malloc'd buffer). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_tex.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL: ");                                                                      \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define GUARD 64

/* The sum written out by hand, independent of the helper's loop. */
static size_t expected(uint32_t w, uint32_t h)
{
    size_t bytes = 0;

    for (uint32_t k = 1; (w >> (k - 1)) > 1 || (h >> (k - 1)) > 1; k++) {
        const uint32_t lw = (w >> k) ? (w >> k) : 1, lh = (h >> k) ? (h >> k) : 1;
        bytes += (size_t)lw * lh * 4;
    }
    return bytes;
}

static void chain(uint32_t w, uint32_t h, size_t want, uint32_t wantLevels)
{
    const size_t n = rdtex_mip_chain_bytes(w, h);
    CHECK(n == want, "%ux%u: %zu chain bytes, expected %zu", w, h, n, want);
    CHECK(n == expected(w, h), "%ux%u: %zu chain bytes, the level sum is %zu", w, h, n,
          expected(w, h));
    uint8_t *base = malloc((size_t)w * h * 4);
    uint8_t *buf = malloc(n + GUARD);
    if (!base || !buf) {
        CHECK(0, "%ux%u: out of memory", w, h);
        free(base);
        free(buf);
        return;
    }
    for (size_t i = 0; i < (size_t)w * h * 4; i++) {
        base[i] = (uint8_t)(i * 7 + 3);
    }
    memset(buf + n, 0xA5, GUARD);
    const uint32_t levels = rdtex_build_mip_chain(base, w, h, buf, 1);
    CHECK(levels == wantLevels, "%ux%u: %u levels, expected %u", w, h, levels, wantLevels);
    int intact = 1;
    for (size_t i = 0; i < GUARD; i++) {
        intact &= buf[n + i] == 0xA5;
    }
    CHECK(intact, "%ux%u: rdtex_build_mip_chain wrote past rdtex_mip_chain_bytes", w, h);
    free(base);
    free(buf);
}

int main(void)
{
    /* 128x4: 64x2 32x1 16x1 8x1 4x1 2x1 1x1 = 512+128+64+32+16+8+4 */
    chain(128, 4, 764, 7);
    CHECK((size_t)128 * 4 * 4 / 3 + 64 < 764, "128x4: the old size (746) was short");
    /* 512x2: 256x1 128x1 ... 1x1 = 4 * (256+128+...+1) = 4 * 511 */
    chain(512, 2, 2044, 9);
    chain(2, 512, 2044, 9);
    chain(16, 16, 4 * (64 + 16 + 4 + 1), 4);
    chain(1, 1, 0, 0);
    chain(1, 8, 4 * (4 + 2 + 1), 3);
    if (failures) {
        printf("rd_mip_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mip_test: ok\n");
    return 0;
}
