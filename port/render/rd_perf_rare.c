/*
 * port/render/rd_perf_rare.c
 *
 * A stalled replay's list 0 named by its pipelines (rd_perf_rare.h).
 */
#include "rd_perf_rare.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint32_t hashOf(uint32_t id)
{
    return id * 2654435761u; /* Knuth's multiplier: ids are small and dense */
}

void rd_perf_rare_replay_reset(RdPerfRareReplay *r, uint32_t replay)
{
    r->replay = replay;
    r->n = r->lookups = r->dropped = 0;
    memset(r->at, 0, sizeof(r->at));
}

void rd_perf_rare_replay_add(RdPerfRareReplay *r, uint32_t id, uint8_t vs, uint8_t fs, uint8_t dst)
{
    if (id == 0) {
        return;
    }
    r->lookups++;
    uint32_t s = hashOf(id) >> 25; /* 7 bits: RD_PERF_RARE_SLOTS */
    for (uint32_t probe = 0; probe < RD_PERF_RARE_SLOTS; probe++) {
        const uint8_t at = r->at[s];
        if (at == 0) {
            if (r->n == RD_PERF_RARE_KEYS) {
                r->dropped++;
                return;
            }
            RdPerfRareKey *k = &r->k[r->n++];
            k->id = id;
            k->lookups = 1;
            k->vs = vs;
            k->fs = fs;
            k->dst = dst;
            r->at[s] = (uint8_t)r->n;
            return;
        }
        RdPerfRareKey *k = &r->k[at - 1u];
        if (k->id == id) {
            if (k->lookups < UINT16_MAX) {
                k->lookups++;
            }
            return;
        }
        s = (s + 1u) & (RD_PERF_RARE_SLOTS - 1u);
    }
    r->dropped++;
}

void rd_perf_rare_block_reset(RdPerfRareBlock *b)
{
    b->replays = b->n = b->dropped = 0;
    memset(b->at, 0, sizeof(b->at));
}

static int blockFind(const RdPerfRareBlock *b, uint32_t id, uint32_t *slot)
{
    uint32_t s = hashOf(id) >> 22; /* 10 bits: RD_PERF_RARE_BSLOTS */
    for (uint32_t probe = 0; probe < RD_PERF_RARE_BSLOTS; probe++) {
        const uint16_t at = b->at[s];
        if (at == 0) {
            *slot = s;
            return -1;
        }
        if (b->id[at - 1u] == id) {
            *slot = s;
            return (int)(at - 1u);
        }
        s = (s + 1u) & (RD_PERF_RARE_BSLOTS - 1u);
    }
    *slot = RD_PERF_RARE_BSLOTS;
    return -1;
}

void rd_perf_rare_block_add(RdPerfRareBlock *b, const RdPerfRareReplay *r)
{
    b->replays++;
    for (uint32_t i = 0; i < r->n; i++) {
        uint32_t slot;
        const int at = blockFind(b, r->k[i].id, &slot);
        if (at >= 0) {
            b->seen[at]++;
        } else if (b->n < RD_PERF_RARE_BLOCK && slot < RD_PERF_RARE_BSLOTS) {
            b->id[b->n] = r->k[i].id;
            b->seen[b->n] = 1;
            b->n++;
            b->at[slot] = (uint16_t)b->n;
        } else {
            b->dropped++;
        }
    }
}

uint32_t rd_perf_rare_seen(const RdPerfRareBlock *b, uint32_t id)
{
    uint32_t slot;
    const int at = blockFind(b, id, &slot);
    return at >= 0 ? b->seen[at] : 0u;
}

typedef struct Out {
    char *buf;
    size_t n, at;
    int total;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *dst = o->at < o->n ? o->buf + o->at : NULL;
    const size_t room = o->at < o->n ? o->n - o->at : 0;
    const int w = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->total += w;
        o->at += (size_t)w;
    }
}

static void putKey(Out *o, const RdPerfRareKey *k, uint32_t seen, RdPerfRareName vsName,
                   RdPerfRareName fsName)
{
    put(o, "pipeline %u (", k->id);
    if (vsName != NULL) {
        put(o, "%s", vsName(k->vs));
    } else {
        put(o, "vs %u", (unsigned)k->vs);
    }
    if (fsName != NULL) {
        put(o, "/%s", fsName(k->fs));
    } else {
        put(o, " fs %u", (unsigned)k->fs);
    }
    put(o, "%s) %u lookup%s, in %u replay%s", k->dst ? ", a destination read" : "",
        (unsigned)k->lookups, k->lookups == 1 ? "" : "s", seen, seen == 1 ? "" : "s");
}

int rd_perf_rare_text(const RdPerfRareBlock *b, const RdPerfRareReplay *r, double pct,
                      RdPerfRareName vsName, RdPerfRareName fsName, char *buf, size_t n)
{
    Out o = {buf, n, 0, 0};
    if (n > 0) {
        buf[0] = '\0';
    }
    put(&o, "GPU clock: replay %u's list 0 (the stalled replay): %u pipelines in %u lookups",
        r->replay, r->n, r->lookups);
    if (r->dropped) {
        put(&o, " (%u lookups past the %u kept)", r->dropped, RD_PERF_RARE_KEYS);
    }
    if (r->n == 0) {
        put(&o, "; none asked for in list 0");
        return o.total;
    }
    /* rare: seen in under pct % of the block's replays, fewest first */
    const double limit = (double)b->replays * pct / 100.0;
    bool used[RD_PERF_RARE_KEYS];
    memset(used, 0, sizeof(used));
    uint32_t rare = 0;
    for (uint32_t i = 0; i < r->n; i++) {
        rare += (double)rd_perf_rare_seen(b, r->k[i].id) < limit;
    }
    put(&o, "; seen in under %.0f %% of the block's %u replays: ", pct, b->replays);
    if (rare == 0) {
        put(&o, "none");
    }
    for (uint32_t shown = 0; shown < rare && shown < RD_PERF_RARE_SHOW; shown++) {
        int best = -1;
        uint32_t bestSeen = 0;
        for (uint32_t i = 0; i < r->n; i++) {
            const uint32_t s = rd_perf_rare_seen(b, r->k[i].id);
            if (!used[i] && (double)s < limit && (best < 0 || s < bestSeen)) {
                best = (int)i;
                bestSeen = s;
            }
        }
        used[best] = true;
        put(&o, "%s", shown ? ", " : "");
        putKey(&o, &r->k[best], bestSeen, vsName, fsName);
    }
    if (rare > RD_PERF_RARE_SHOW) {
        put(&o, " (%u more)", rare - RD_PERF_RARE_SHOW);
    }
    /* the most looked up of the rest */
    put(&o, "; the most used: ");
    for (uint32_t shown = 0; shown < RD_PERF_RARE_COMMON; shown++) {
        int best = -1;
        for (uint32_t i = 0; i < r->n; i++) {
            if (!used[i] && (best < 0 || r->k[i].lookups > r->k[best].lookups)) {
                best = (int)i;
            }
        }
        if (best < 0) {
            if (shown == 0) {
                put(&o, "none besides");
            }
            break;
        }
        used[best] = true;
        put(&o, "%s", shown ? ", " : "");
        putKey(&o, &r->k[best], rd_perf_rare_seen(b, r->k[best].id), vsName, fsName);
    }
    if (b->dropped) {
        put(&o, "; %u of the block's pipelines past the %u counted", b->dropped,
            RD_PERF_RARE_BLOCK);
    }
    return o.total;
}
