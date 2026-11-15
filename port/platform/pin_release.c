/*
 * port/platform/pin_release.c
 *
 * The pins let go at one picture a tick, and the lines (pin_release.h).
 */
#include "pin_release.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* the trace's state words (pin_release_trace_text) */
enum { LABEL_PINNED = 0, LABEL_LET_GO, LABEL_AGAIN };

void pin_release_reset(PinRelease *p)
{
    memset(p, 0, sizeof(*p));
    p->lastRender = p->lastGame = -1;
}

static void note_one(uint32_t *sec, uint32_t *blk, uint32_t *secUnknown, uint32_t *blkUnknown,
                     uint32_t *secMoves, uint32_t *blkMoves, int *last, int core)
{
    if (core < 0 || core >= PIN_RELEASE_CORES) {
        (*secUnknown)++;
        (*blkUnknown)++;
        return;
    }
    sec[core]++;
    blk[core]++;
    if (*last >= 0 && *last != core) {
        (*secMoves)++;
        (*blkMoves)++;
    }
    *last = core;
}

void pin_release_note(PinRelease *p, int renderCore, int gameCore)
{
    note_one(p->sec.render, p->block.render, &p->sec.renderUnknown, &p->block.renderUnknown,
             &p->sec.renderMoves, &p->block.renderMoves, &p->lastRender, renderCore);
    note_one(p->sec.game, p->block.game, &p->sec.gameUnknown, &p->block.gameUnknown,
             &p->sec.gameMoves, &p->block.gameMoves, &p->lastGame, gameCore);
}

bool pin_release_stuck(const ClockTrapSecond *s, double cpuMedian, unsigned cpuBlocks)
{
    return clock_trap_one_a_tick(s) && clock_trap_cpu_raised(s, cpuMedian, cpuBlocks);
}

unsigned pin_release_second(PinRelease *p, const ClockTrapSecond *s, double cpuMedian,
                            unsigned cpuBlocks, bool allowed, PinCores *cores)
{
    const bool judged = s->frames >= CLOCK_TRAP_MIN_FRAMES;
    const bool stuck = pin_release_stuck(s, cpuMedian, cpuBlocks);
    unsigned ev = stuck ? PIN_RELEASE_EV_STUCK : 0u;
    bool trace = false;

    if (cores != NULL) {
        *cores = p->sec;
    }
    memset(&p->sec, 0, sizeof(p->sec));
    if (p->released) {
        p->heldS++;
        p->blkReleasedS++;
        if (stuck) {
            p->heldStuck++;
            p->blkStuckReleased++;
            p->good = 0;
        } else if (judged) {
            p->good++;
        }
        trace = true;
        p->label = LABEL_LET_GO;
        p->labelN = ++p->traceK;
        if (p->good >= PIN_RELEASE_STABLE_S || !allowed) {
            p->released = false;
            p->runRestores++;
            p->blkRestores++;
            p->traceAfter = PIN_RELEASE_TRACE_AFTER;
            p->traceK = 0;
            ev |= PIN_RELEASE_EV_RESTORE;
        }
    } else {
        if (stuck) {
            p->blkStuckPinned++;
        }
        if (p->traceAfter > 0) {
            p->traceAfter--;
            trace = true;
            p->label = LABEL_AGAIN;
            p->labelN = ++p->traceK;
        } else if (stuck) {
            trace = true;
            p->label = LABEL_PINNED;
            p->labelN = 0;
        }
        if (stuck && allowed) {
            p->released = true;
            p->good = 0;
            p->heldS = p->heldStuck = 0;
            p->traceAfter = 0;
            p->traceK = 0;
            p->runReleases++;
            p->blkReleases++;
            ev |= PIN_RELEASE_EV_RELEASE;
        }
    }
    if (trace && p->traceLines < PIN_RELEASE_TRACE_MAX) {
        p->traceLines++;
        ev |= PIN_RELEASE_EV_TRACE;
    } else if (trace && p->traceLines == PIN_RELEASE_TRACE_MAX) {
        p->traceLines++;
        ev |= PIN_RELEASE_EV_TRACE_END; /* said once: no more this run */
    }
    return ev;
}

/* --- the lines ------------------------------------------------------------ */

typedef struct Out {
    char *buf;
    size_t cap, len;
    int total;
} Out;

#if defined(__GNUC__)
static void put(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void put(Out *o, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(o->len < o->cap ? o->buf + o->len : NULL,
                            o->len < o->cap ? o->cap - o->len : 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    o->total += n;
    o->len = o->len + (size_t)n < o->cap ? o->len + (size_t)n : o->cap;
}

static Out out_of(char *buf, size_t cap)
{
    Out o = {buf, cap, 0, 0};

    if (cap > 0) {
        buf[0] = '\0';
    }
    return o;
}

/* "2-7", "0-4, 7", "none" (the kernel's list in words) */
static void mask_words(Out *o, uint64_t m)
{
    bool any = false;

    for (int i = 0; i < 64;) {
        if (!(m & ((uint64_t)1 << i))) {
            i++;
            continue;
        }
        int j = i;

        while (j + 1 < 64 && (m & ((uint64_t)1 << (j + 1)))) {
            j++;
        }
        put(o, "%s", any ? ", " : "");
        if (j > i) {
            put(o, "%d-%d", i, j);
        } else {
            put(o, "%d", i);
        }
        any = true;
        i = j + 1;
    }
    if (!any) {
        put(o, "none");
    }
}

int pin_release_release_text(const PinRelease *p, const ClockTrapSecond *s, double cpuMedian,
                             char *out, size_t cap)
{
    Out o = out_of(out, cap);

    put(&o,
        "pins let go: %u pictures for %u game frames in a second (one a tick) with a picture's "
        "CPU ",
        s->pictures, s->frames);
    if (s->cpuN) {
        put(&o, "%.1f ms", s->cpuMs / s->cpuN);
    } else {
        put(&o, "not known");
    }
    if (cpuMedian > 0.0) {
        put(&o, " against %.1f (the blocks' median)", cpuMedian);
    } else {
        put(&o, " (no blocks' level yet)");
    }
    put(&o,
        ": the render and game threads may run on any faster core until %u seconds pass "
        "without one picture a tick; %u this run",
        PIN_RELEASE_STABLE_S, p->runReleases);
    return o.total;
}

int pin_release_restore_text(const PinRelease *p, char *out, size_t cap)
{
    Out o = out_of(out, cap);

    put(&o,
        "pins taken again: %u seconds without one picture a tick; let go for %u s, %u of them "
        "at one a tick; %u this run",
        PIN_RELEASE_STABLE_S, p->heldS, p->heldStuck, p->runRestores);
    return o.total;
}

/* the cores of h by their samples, the most first (the lower core on a
   tie): "core 7 30, core 6 1" */
static void cores_by_count(Out *o, const uint32_t *h)
{
    bool used[PIN_RELEASE_CORES];
    bool any = false;

    memset(used, 0, sizeof(used));
    for (;;) {
        int best = -1;

        for (int i = 0; i < PIN_RELEASE_CORES; i++) {
            if (!used[i] && h[i] != 0 && (best < 0 || h[i] > h[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        used[best] = true;
        put(o, "%score %d %u", any ? ", " : "", best, (unsigned)h[best]);
        any = true;
    }
}

static uint32_t sum_of(const uint32_t *h)
{
    uint32_t t = 0;

    for (int i = 0; i < PIN_RELEASE_CORES; i++) {
        t += h[i];
    }
    return t;
}

static void thread_words(Out *o, const char *who, const uint32_t *h, uint32_t unknown,
                         uint32_t moves, uint64_t allowed)
{
    if (sum_of(h) == 0u && unknown == 0u) {
        put(o, "; the %s thread's core not sampled", who);
    } else {
        put(o, "; the %s thread on ", who);
        cores_by_count(o, h);
        if (unknown != 0u) {
            put(o, "%snot known %u", sum_of(h) != 0u ? ", " : "", (unsigned)unknown);
        }
        put(o, " (core changes %u)", (unsigned)moves);
    }
    if (allowed != 0) {
        put(o, ", may run on ");
        mask_words(o, allowed);
    }
}

int pin_release_trace_text(const PinRelease *p, const ClockTrapSecond *s, const PinCores *c,
                           uint64_t renderAllowed, uint64_t gameAllowed, const GpuSysfsRing *ring,
                           char *out, size_t cap)
{
    Out o = out_of(out, cap);
    char sec[768];

    if (p->label == LABEL_LET_GO) {
        put(&o, "pin trace (let go, second %u): ", p->labelN);
    } else if (p->label == LABEL_AGAIN) {
        put(&o, "pin trace (pinned again, second %u of %u): ", p->labelN, PIN_RELEASE_TRACE_AFTER);
    } else {
        put(&o, "pin trace (pinned, at one a tick): ");
    }
    (void)clock_trap_second_text(s, ring, sec, sizeof(sec));
    put(&o, "%s", sec);
    thread_words(&o, "render", c->render, c->renderUnknown, c->renderMoves, renderAllowed);
    thread_words(&o, "game", c->game, c->gameUnknown, c->gameMoves, gameAllowed);
    if (ring == NULL || ring->cpuCount == 0) {
        put(&o, "; their cores' clocks not sampled here");
        return o.total;
    }
    put(&o, "; their cores' clocks in that second (MHz):");
    bool any = false;

    for (int pass = 0; pass < 2; pass++) {
        const uint32_t *h = pass == 0 ? c->render : c->game;

        for (int i = 0; i < PIN_RELEASE_CORES && i < GPU_SYSFS_CPUS; i++) {
            if (h[i] == 0u || (pass == 1 && c->render[i] != 0u)) {
                continue; /* not run on, or said with the render thread's */
            }
            put(&o, "%s core %d", any ? "," : "", i);
            unsigned shown = 0;

            for (uint32_t k = 0; k < ring->count; k++) {
                const uint32_t at =
                    (ring->next + GPU_SYSFS_RING - ring->count + k) % GPU_SYSFS_RING;
                const GpuSysfsSample *x = &ring->s[at];

                if (x->tMs >= s->t0Ms && x->tMs < s->t0Ms + 1000.0 && x->cpuMhz[i] != 0) {
                    put(&o, " %u", (unsigned)x->cpuMhz[i]);
                    shown++;
                }
            }
            if (shown == 0) {
                put(&o, " none");
            }
            any = true;
        }
    }
    if (!any) {
        put(&o, " none run on");
    }
    return o.total;
}

/* "7 98 % (1180 MHz), 6 2 % (clock not sampled)" over h's samples */
static void residency(Out *o, const uint32_t *h, const double *coreMhz, int coreCount)
{
    const uint32_t total = sum_of(h);
    bool used[PIN_RELEASE_CORES];
    bool any = false;

    memset(used, 0, sizeof(used));
    for (;;) {
        int best = -1;

        for (int i = 0; i < PIN_RELEASE_CORES; i++) {
            if (!used[i] && h[i] != 0 && (best < 0 || h[i] > h[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        used[best] = true;
        put(o, "%s%d %u %%", any ? ", " : "", best,
            (unsigned)(((uint64_t)h[best] * 100u + total / 2u) / total));
        if (coreMhz != NULL && best < coreCount && coreMhz[best] > 0.0) {
            put(o, " (%.0f MHz)", coreMhz[best]);
        } else {
            put(o, " (clock not sampled)");
        }
        any = true;
    }
}

int pin_release_block_text(PinRelease *p, const double *coreMhz, int coreCount,
                           const char *ruleWords, char *out, size_t cap)
{
    Out o = out_of(out, cap);
    const PinCores *b = &p->block;

    if (sum_of(b->render) == 0u) {
        put(&o, "; the render thread's cores not sampled");
    } else {
        put(&o, "; the render thread's cores: ");
        residency(&o, b->render, coreMhz, coreCount);
        put(&o, ", core changes %u", (unsigned)b->renderMoves);
    }
    if (sum_of(b->game) == 0u) {
        put(&o, "; the game thread's cores not sampled");
    } else {
        put(&o, "; the game thread's: ");
        residency(&o, b->game, coreMhz, coreCount);
        put(&o, ", core changes %u", (unsigned)b->gameMoves);
    }
    if (ruleWords != NULL) {
        put(&o, "; pins not let go here (%s), at one a tick %u s", ruleWords, p->blkStuckPinned);
    } else {
        put(&o,
            "; pins let go %u, taken again %u, let go for %u s of the block (at one a tick %u s "
            "pinned, %u s let go)",
            p->blkReleases, p->blkRestores, p->blkReleasedS, p->blkStuckPinned,
            p->blkStuckReleased);
    }
    memset(&p->block, 0, sizeof(p->block));
    p->blkReleases = p->blkRestores = p->blkReleasedS = 0;
    p->blkStuckPinned = p->blkStuckReleased = 0;
    return o.total;
}
