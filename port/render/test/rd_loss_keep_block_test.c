/* rd_loss_keep_block_test.c: the block a loss-keep copy is made into
 * (port/render/rd_loss_keep_block.c), pure, over a fake host whose worker
 * makes a block when the test says so.
 *
 *   want       the size asked for: the last copy and an eighth more and 1
 *              MiB, at most 16 MiB; nothing for no copy
 *   steady     no copy yet (steady play, no stalled wait): nothing asked,
 *              the memory never read, no block held
 *   cycle      the first copy (the measured allocation: no block yet) asks
 *              for a block of its size; while the worker makes it the next
 *              copy takes the measured allocation again; once made, the
 *              tick takes it, a copy is made into it and the next is asked
 *              for at once, of the new copy's size
 *   small      a block the frame did not fit is given back (freed by the
 *              host) and the next ask is of the bigger copy's size
 *   failed     a block the worker could not make: none held, asked again
 *              after the next copy
 *   floor      under the free-memory floor the block held is given back
 *              and an ask out is withdrawn; nothing is asked while the
 *              readings stay under it (read at most once a second); above
 *              it again a block is asked for; the system's signal the same
 *   end        the renderer's end gives the block back and withdraws the ask
 *
 * Every block the fake host made is freed by the end (the asan preset
 * checks). */
#include "rd_loss_keep_block.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the fake worker: one ask at a time; made when the test sets ready */
static struct {
    int asked;    /* an ask is out */
    int ready;    /* the test: the worker has made it */
    int failNext; /* the test: the next ask cannot be made */
    size_t bytes; /* the ask's size */
    int asks, takes, givenBack, withdrawn;
    int memory; /* what the memory function says */
    int memReads;
    long live; /* blocks made and not freed */
} fk;

static int fakeAsk(size_t bytes)
{
    if (fk.asked) {
        return -1;
    }
    fk.asked = 1;
    fk.ready = 0;
    fk.bytes = bytes;
    fk.asks++;
    return 0;
}

static int fakeTake(void **block, size_t *size)
{
    *block = NULL;
    *size = 0;
    if (!fk.asked) {
        return -1;
    }
    if (fk.failNext) {
        fk.failNext = 0;
        fk.asked = 0;
        return -1;
    }
    if (!fk.ready) {
        return 0;
    }
    *block = malloc(fk.bytes);
    if (*block == NULL) {
        fk.asked = 0;
        return -1;
    }
    fk.live++;
    *size = fk.bytes;
    fk.asked = 0;
    fk.ready = 0;
    fk.takes++;
    return 1;
}

static void fakeGiveBack(void *block)
{
    if (fk.asked) {
        fk.asked = 0;
        fk.withdrawn++;
    }
    if (block != NULL) {
        free(block);
        fk.live--;
        fk.givenBack++;
    }
}

static int fakeMemory(char *why, size_t size)
{
    fk.memReads++;
    if (why != NULL && size > 0) {
        snprintf(why, size, "%s", fk.memory == 0 ? "fine" : "under the floor");
    }
    return fk.memory;
}

static const RdLossKeepHost s_host = {fakeAsk, fakeTake, fakeGiveBack, fakeMemory};

/* a copy as rd_dump.c keepTake makes it: into the held block when it fits,
   else (no block, or too small) the measured allocation; true when it went
   into a block */
static bool copy(RdKeepBlock *k, size_t len, void **kept)
{
    size_t size = 0;
    void *b = rd_keep_block_get(k, &size);

    if (b != NULL && len <= size) {
        rd_keep_block_copied(k, len, true);
        *kept = b;
        return true;
    }
    if (b != NULL) {
        rd_keep_block_small(k, &s_host, b);
    }
    *kept = malloc(len);
    fk.live += *kept != NULL;
    rd_keep_block_copied(k, len, false);
    return false;
}

static void drop(void **kept)
{
    if (*kept != NULL) {
        free(*kept);
        fk.live--;
        *kept = NULL;
    }
}

static void want(void)
{
    const size_t mib = (size_t)1 << 20;

    CHECK(rd_keep_block_want(0) == 0);
    CHECK(rd_keep_block_want(8 * mib) == 8 * mib + mib + mib);
    CHECK(rd_keep_block_want(7000000) == 7000000 + 875000 + mib);
    CHECK(rd_keep_block_want(15 * mib) == RD_KEEP_BLOCK_MAX);
    CHECK(rd_keep_block_want(RD_KEEP_BLOCK_MAX) == RD_KEEP_BLOCK_MAX);
    CHECK(RD_KEEP_BLOCK_MAX == 16 * mib);
}

static void steady(void)
{
    RdKeepBlock k = {0};
    double t = 1000.0;

    memset(&fk, 0, sizeof(fk));
    for (int i = 0; i < 500; i++, t += 40.0) {
        CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_NONE);
    }
    CHECK(fk.asks == 0 && fk.memReads == 0 && k.block == NULL && !k.asked);
    /* no host: nothing at all */
    CHECK(rd_keep_block_tick(&k, NULL, t, NULL, 0) == RD_KEEP_BLOCK_NONE);
    rd_keep_block_end(&k, &s_host);
}

static void cycle(void)
{
    RdKeepBlock k = {0};
    void *a = NULL, *b = NULL, *c = NULL;
    double t = 5000.0;

    memset(&fk, 0, sizeof(fk));
    /* the first copy: no block yet, the measured allocation */
    CHECK(!copy(&k, 7000000, &a));
    CHECK(k.stalled && k.lastLen == 7000000 && k.used == 0);
    /* the next tick asks for a block of its size (the memory read first) */
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    CHECK(fk.asks == 1 && fk.bytes == rd_keep_block_want(7000000) && k.asked && fk.memReads == 1);
    /* being made: nothing held, a copy now takes the measured allocation */
    t += 40.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_NONE);
    CHECK(k.asked && k.block == NULL);
    CHECK(!copy(&k, 7100000, &b));
    CHECK(fk.asks == 1); /* the ask still out: no second */
    /* made: the tick takes it */
    fk.ready = 1;
    t += 40.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_TAKEN);
    CHECK(k.block != NULL && k.size == rd_keep_block_want(7000000) && !k.asked && k.made == 1);
    /* used: the copy goes into it */
    drop(&a);
    CHECK(copy(&k, 7200000, &a));
    CHECK(k.used == 1 && k.block == NULL && k.lastLen == 7200000);
    /* and the next asked for at once, of the new copy's size */
    t += 40.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    CHECK(fk.asks == 2 && fk.bytes == rd_keep_block_want(7200000));
    fk.ready = 1;
    t += 40.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_TAKEN);
    CHECK(copy(&k, 7200000, &c) && k.used == 2 && k.made == 2);
    drop(&a);
    drop(&b);
    drop(&c);
    rd_keep_block_end(&k, &s_host);
    CHECK(fk.live == 0);
}

static void small(void)
{
    RdKeepBlock k = {0};
    void *a = NULL, *b = NULL;
    double t = 9000.0;

    memset(&fk, 0, sizeof(fk));
    CHECK(!copy(&k, 4000000, &a));
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    fk.ready = 1;
    CHECK(rd_keep_block_tick(&k, &s_host, t + 40.0, NULL, 0) == RD_KEEP_BLOCK_TAKEN);
    /* the frame grew past the block: given back, the measured allocation */
    CHECK(!copy(&k, 9000000, &b));
    CHECK(k.small == 1 && fk.givenBack == 1 && k.used == 0 && k.lastLen == 9000000);
    CHECK(rd_keep_block_tick(&k, &s_host, t + 80.0, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    CHECK(fk.bytes == rd_keep_block_want(9000000));
    drop(&a);
    drop(&b);
    rd_keep_block_end(&k, &s_host);
    CHECK(fk.live == 0 && fk.withdrawn == 1);
}

static void failed(void)
{
    RdKeepBlock k = {0};
    void *a = NULL, *b = NULL;
    double t = 12000.0;

    memset(&fk, 0, sizeof(fk));
    CHECK(!copy(&k, 3000000, &a));
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    fk.failNext = 1;
    CHECK(rd_keep_block_tick(&k, &s_host, t + 40.0, NULL, 0) == RD_KEEP_BLOCK_FAILED);
    CHECK(k.failed == 1 && k.block == NULL);
    /* the same tick asks again (a copy has been made, none held): the
       failed one's result came first */
    CHECK(fk.asks == 2 && k.asked);
    CHECK(!copy(&k, 3000000, &b));
    drop(&a);
    drop(&b);
    rd_keep_block_end(&k, &s_host);
    CHECK(fk.live == 0);
}

static void floorRule(void)
{
    RdKeepBlock k = {0};
    void *a = NULL;
    char why[64];
    double t = 20000.0;

    memset(&fk, 0, sizeof(fk));
    CHECK(!copy(&k, 5000000, &a));
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    fk.ready = 1;
    t += 40.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_TAKEN);
    CHECK(k.block != NULL && fk.live == 2);
    /* under the floor, read again a second after the last reading: the
       block given back (freed) */
    fk.memory = 1;
    t += 500.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, why, sizeof(why)) == RD_KEEP_BLOCK_NONE);
    CHECK(k.block != NULL); /* not read yet: under a second since the last */
    t += 500.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, why, sizeof(why)) == RD_KEEP_BLOCK_RELEASED);
    CHECK(k.block == NULL && k.released == 1 && fk.givenBack == 1 && fk.live == 1);
    CHECK(strcmp(why, "under the floor") == 0);
    /* while the readings stay under: nothing asked, a reading a second */
    const int reads = fk.memReads;
    for (int i = 0; i < 50; i++) {
        t += 40.0;
        CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_NONE);
    }
    CHECK(fk.asks == 1 && !k.asked);
    CHECK(fk.memReads - reads == 2); /* 2000 ms: two readings */
    /* above it again: asked for */
    fk.memory = 0;
    t += 1000.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    CHECK(fk.asks == 2 && k.asked);
    /* the system's signal with the ask out: withdrawn */
    fk.memory = 2;
    t += 1000.0;
    CHECK(rd_keep_block_tick(&k, &s_host, t, NULL, 0) == RD_KEEP_BLOCK_RELEASED);
    CHECK(!k.asked && fk.withdrawn == 1 && k.released == 2);
    drop(&a);
    rd_keep_block_end(&k, &s_host);
    CHECK(fk.live == 0);
}

static void end(void)
{
    RdKeepBlock k = {0};
    void *a = NULL;

    memset(&fk, 0, sizeof(fk));
    CHECK(!copy(&k, 2000000, &a));
    CHECK(rd_keep_block_tick(&k, &s_host, 100.0, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    fk.ready = 1;
    CHECK(rd_keep_block_tick(&k, &s_host, 140.0, NULL, 0) == RD_KEEP_BLOCK_TAKEN);
    rd_keep_block_end(&k, &s_host);
    CHECK(fk.givenBack == 1 && k.block == NULL && !k.stalled && k.lastLen == 0);
    /* with an ask out: withdrawn */
    drop(&a);
    CHECK(!copy(&k, 2000000, &a));
    CHECK(rd_keep_block_tick(&k, &s_host, 200.0, NULL, 0) == RD_KEEP_BLOCK_ASKED);
    rd_keep_block_end(&k, &s_host);
    CHECK(fk.withdrawn == 1 && !fk.asked);
    drop(&a);
    CHECK(fk.live == 0);
}

int main(void)
{
    want();
    steady();
    cycle();
    small();
    failed();
    floorRule();
    end();
    if (failures == 0) {
        printf("rd_loss_keep_block_test: ok\n");
    }
    return failures != 0;
}
