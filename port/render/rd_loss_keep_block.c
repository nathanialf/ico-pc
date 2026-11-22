/* rd_loss_keep_block.c: the block a loss-keep copy is made into
 * (rd_loss_keep_block.h). */
#include "rd_loss_keep_block.h"

size_t rd_keep_block_want(size_t lastLen)
{
    if (lastLen == 0) {
        return 0;
    }
    const size_t want = lastLen + lastLen / 8u + ((size_t)1 << 20);

    return want > RD_KEEP_BLOCK_MAX ? RD_KEEP_BLOCK_MAX : want;
}

static bool wanted(const RdKeepBlock *k)
{
    return k->stalled && k->lastLen > 0 && !k->asked && k->block == NULL;
}

RdKeepBlockEvent rd_keep_block_tick(RdKeepBlock *k, const RdLossKeepHost *h, double nowMs,
                                    char *why, size_t whySize)
{
    RdKeepBlockEvent ev = RD_KEEP_BLOCK_NONE;

    if (why != NULL && whySize > 0) {
        why[0] = '\0';
    }
    if (h == NULL) {
        return ev;
    }
    if (k->asked) {
        void *b = NULL;
        size_t sz = 0;
        const int r = h->take(&b, &sz);

        if (r == 1 && b != NULL) {
            k->asked = false;
            k->block = b;
            k->size = sz;
            k->made++;
            ev = RD_KEEP_BLOCK_TAKEN;
        } else if (r != 0) {
            k->asked = false;
            k->failed++;
            ev = RD_KEEP_BLOCK_FAILED;
        }
    }
    if ((k->block != NULL || k->asked || wanted(k)) && h->memory != NULL &&
        (!k->memRead || nowMs - k->memAtMs >= RD_KEEP_BLOCK_MEM_MS)) {
        k->memRead = true;
        k->memAtMs = nowMs;
        k->low = h->memory(why, whySize);
        if (k->low != 0 && (k->block != NULL || k->asked)) {
            h->giveBack(k->block);
            k->block = NULL;
            k->size = 0;
            k->asked = false;
            k->released++;
            ev = RD_KEEP_BLOCK_RELEASED;
        }
    }
    if (wanted(k) && k->low == 0 && (h->memory == NULL || k->memRead)) {
        if (h->ask(rd_keep_block_want(k->lastLen)) == 0) {
            k->asked = true;
            k->asks++;
            if (ev == RD_KEEP_BLOCK_NONE) {
                ev = RD_KEEP_BLOCK_ASKED;
            }
        }
    }
    return ev;
}

void *rd_keep_block_get(RdKeepBlock *k, size_t *size)
{
    void *b = k->block;

    *size = b != NULL ? k->size : 0;
    k->block = NULL;
    k->size = 0;
    return b;
}

void rd_keep_block_copied(RdKeepBlock *k, size_t len, bool intoBlock)
{
    k->stalled = true;
    k->lastLen = len;
    k->used += intoBlock;
}

void rd_keep_block_small(RdKeepBlock *k, const RdLossKeepHost *h, void *block)
{
    k->small++;
    if (block == NULL) {
        return;
    }
    if (h != NULL) {
        h->giveBack(block);
    }
}

void rd_keep_block_end(RdKeepBlock *k, const RdLossKeepHost *h)
{
    if (h != NULL && (k->block != NULL || k->asked)) {
        h->giveBack(k->block);
    }
    *k = (RdKeepBlock){0};
}
