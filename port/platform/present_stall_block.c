/*
 * port/platform/present_stall_block.c
 *
 * The present stalls per 10 s block and per run against the GPU span band
 * and the display's rate (present_stall_block.h).
 */
#include "present_stall_block.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void stall_block_init(StallBlock *b)
{
    memset(b, 0, sizeof(*b));
}

static void block_reset(StallBlock *b)
{
    b->presents = b->stalls = b->restarts = 0;
    b->spanN = 0;
    b->spanSum = b->spanMin = b->spanMax = 0.0;
}

/* the run's bucket for a display rate (> 0): found, made, or the last one
   once STALL_BLOCK_RATES are made */
static unsigned rate_bucket(StallBlock *b, double hz)
{
    unsigned i;

    for (i = 0; i < b->rateN; i++) {
        if (fabs(b->rateHz[i] - hz) <= 0.05) {
            return i;
        }
    }
    if (b->rateN < STALL_BLOCK_RATES) {
        b->rateHz[b->rateN] = hz;
        return b->rateN++;
    }
    return STALL_BLOCK_RATES - 1; /* counted with the last */
}

/* a stall into the run's sums, at its own display rate */
static void run_stall(StallBlock *b, const StallBlockRec *r, double sinceMs)
{
    b->runStalls++;
    if (r->gapBackMs > 0.0) {
        b->runRestartStalls++;
    }
    if (sinceMs >= STALL_BLOCK_SLUMBER_MS) {
        b->runLongGap++;
    } else if (sinceMs >= 0.0) {
        b->runShortGap++;
    }
    if (r->gpuValid) {
        const double held = r->presentMs - r->gpuMs;

        if (b->runHeldN == 0 || held < b->runHeldMin) {
            b->runHeldMin = held;
        }
        if (b->runHeldN == 0 || held > b->runHeldMax) {
            b->runHeldMax = held;
        }
        b->runHeldN++;
    }
    if (r->displayHz > 0.0) {
        b->rateStalls[rate_bucket(b, r->displayHz)]++;
    } else {
        b->rateUnknownStalls++;
    }
}

void stall_block_add(StallBlock *b, const StallBlockRec *r)
{
    const int stalled = r->presented && r->presentMs >= STALL_BLOCK_MS;
    const double sinceMs =
        r->startMs > 0.0 && b->lastStartMs > 0.0 ? r->startMs - b->lastStartMs : -1.0;

    if (r->startMs > 0.0) {
        b->lastStartMs = r->startMs;
    }

    if (r->gapBackMs > 0.0) {
        b->restarts++;
    }
    if (r->presented) {
        b->presents++;
    }
    if (stalled) {
        if (b->stalls < STALL_BLOCK_LIST) {
            const unsigned i = b->stalls;

            b->stallMs[i] = r->presentMs;
            b->stallGpu[i] = r->gpuValid != 0;
            b->stallGpuMs[i] = r->gpuMs;
            b->stallGap[i] = r->gapValid ? 1 : r->gapBackMs > 0.0 ? 2 : 0;
            b->stallGapMs[i] = r->gapMs;
            b->stallHz[i] = r->displayHz;
            b->stallSinceMs[i] = sinceMs;
        }
        b->stalls++;
        run_stall(b, r, sinceMs);
        return; /* a stalled replay's span is not the block's band */
    }
    if (r->presented && r->gpuValid && r->gpuMs > 0.0) {
        if (b->spanN == 0 || r->gpuMs < b->spanMin) {
            b->spanMin = r->gpuMs;
        }
        if (b->spanN == 0 || r->gpuMs > b->spanMax) {
            b->spanMax = r->gpuMs;
        }
        b->spanSum += r->gpuMs;
        b->spanN++;
    }
}

int stall_block_band(const StallBlock *b)
{
    int i;

    if (b->spanN == 0) {
        return -1;
    }
    i = (int)floor(b->spanSum / b->spanN / STALL_BLOCK_BAND_MS);
    return i < 0 ? 0 : i >= STALL_BLOCK_BANDS ? STALL_BLOCK_BANDS - 1 : i;
}

/* appends to buf at *at (cut at size, always terminated) */
static void put(char *buf, size_t size, size_t *at, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (*at >= size) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf + *at, size - *at, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *at += (size_t)n < size - *at ? (size_t)n : size - *at - 1;
    }
}

static void band_name(int i, char *out, size_t size)
{
    if (i < 0) {
        snprintf(out, size, "not timed");
    } else if (i == STALL_BLOCK_BANDS - 1) {
        snprintf(out, size, "%.0f ms and over", i * STALL_BLOCK_BAND_MS);
    } else {
        snprintf(out, size, "%.0f-%.0f ms", i * STALL_BLOCK_BAND_MS, (i + 1) * STALL_BLOCK_BAND_MS);
    }
}

static void run_join(StallBlock *b, double blockSeconds, double displayHz)
{
    const int band = stall_block_band(b);

    if (!(blockSeconds > 0.0)) {
        blockSeconds = 0.0;
    }
    b->runBlocks++;
    b->runSeconds += blockSeconds;
    if (band < 0) {
        b->bandNoneSeconds += blockSeconds;
        b->bandNoneStalls += b->stalls;
    } else {
        b->bandSeconds[band] += blockSeconds;
        b->bandStalls[band] += b->stalls;
    }
    if (displayHz > 0.0) {
        b->rateSeconds[rate_bucket(b, displayHz)] += blockSeconds;
    } else {
        b->rateUnknownSeconds += blockSeconds;
    }
}

int stall_block_end(StallBlock *b, double blockSeconds, double displayHz,
                    const StallBlockClock *clock, char *buf, size_t size)
{
    size_t at = 0;
    unsigned i, listed;
    char band[32];

    if (size > 0) {
        buf[0] = '\0';
    }
    run_join(b, blockSeconds, displayHz);
    if (b->runStalls == 0 || size == 0) {
        block_reset(b);
        return 0;
    }
    listed = b->stalls < STALL_BLOCK_LIST ? b->stalls : STALL_BLOCK_LIST;
    put(buf, size, &at, "present stalls: %u over %.0f ms in %.1f s", b->stalls, STALL_BLOCK_MS,
        blockSeconds);
    if (b->stalls > 0) {
        put(buf, size, &at, " (");
        for (i = 0; i < listed; i++) {
            put(buf, size, &at, "%s%.0f ms", i ? "; " : "", b->stallMs[i]);
            if (b->stallHz[i] > 0.0) {
                put(buf, size, &at, " at %.2f Hz", b->stallHz[i]);
            }
            if (b->stallSinceMs[i] >= 0.0) {
                put(buf, size, &at, ": the replay before began %.1f ms earlier",
                    b->stallSinceMs[i]);
            } else {
                put(buf, size, &at, ": the replay before not known");
            }
            if (b->stallGpu[i]) {
                put(buf, size, &at, ", the present held %.1f ms past the replay's GPU span",
                    b->stallMs[i] - b->stallGpuMs[i]);
            }
            if (b->stallGap[i] == 1 && b->stallGapMs[i] >= 0.0) {
                put(buf, size, &at, ", GPU idle before it %.1f ms", b->stallGapMs[i]);
            } else if (b->stallGap[i] == 1) {
                put(buf, size, &at, ", begun %.1f ms before the one before ended",
                    -b->stallGapMs[i]);
            } else if (b->stallGap[i] == 2) {
                put(buf, size, &at, ", GPU idle before it not measured (the GPU clock restarted)");
            } else {
                put(buf, size, &at, ", GPU idle before it not measured");
            }
        }
        put(buf, size, &at, "%s)", b->stalls > listed ? "; the first listed" : "");
    }
    put(buf, size, &at, "; GPU clock restarts %u", b->restarts);
    band_name(stall_block_band(b), band, sizeof band);
    if (b->spanN > 0) {
        put(buf, size, &at, "; GPU span %.1f ms a replay (%.1f-%.1f ms, band %s",
            b->spanSum / b->spanN, b->spanMin, b->spanMax, band);
    } else {
        put(buf, size, &at, "; GPU span not timed (");
    }
    if (b->stalls > 0) {
        put(buf, size, &at, "%sstalled replays ", b->spanN > 0 ? "; " : "");
        for (i = 0; i < listed; i++) {
            if (b->stallGpu[i]) {
                put(buf, size, &at, "%s%.1f", i ? ", " : "", b->stallGpuMs[i]);
            } else {
                put(buf, size, &at, "%snot timed", i ? ", " : "");
            }
        }
        put(buf, size, &at, " ms");
    } else if (b->spanN == 0) {
        put(buf, size, &at, "no stall");
    }
    put(buf, size, &at, ")");
    if (displayHz > 0.0) {
        put(buf, size, &at, "; display %.2f Hz", displayHz);
    } else {
        put(buf, size, &at, "; display rate not known");
    }
    if (clock != NULL && clock->n > 0) {
        put(buf, size, &at, "; GPU clock %d-%d MHz (mean %.0f, %u samples)", clock->minMhz,
            clock->maxMhz, clock->meanMhz, clock->n);
    } else {
        put(buf, size, &at, "; GPU clock not readable");
    }
    block_reset(b);
    return 1;
}

static void per_minute(char *buf, size_t size, size_t *at, const char *name, unsigned stalls,
                       double seconds)
{
    put(buf, size, at, "%s %u in %.1f min (%.1f a minute)", name, stalls, seconds / 60.0,
        seconds > 0.0 ? stalls * 60.0 / seconds : 0.0);
}

int stall_block_run_text(const StallBlock *b, char *buf, size_t size)
{
    size_t at = 0;
    char name[40];
    int first = 1;

    if (size > 0) {
        buf[0] = '\0';
    }
    if (b->runBlocks == 0 || size == 0) {
        return 0;
    }
    put(buf, size, &at, "present stalls this run: %u over %.0f ms in %.1f min (%.1f a minute)",
        b->runStalls, STALL_BLOCK_MS, b->runSeconds / 60.0,
        b->runSeconds > 0.0 ? b->runStalls * 60.0 / b->runSeconds : 0.0);
    if (b->runStalls > 0) {
        put(buf, size, &at,
            "; %u with the GPU clock restarted, %u after a CPU-side gap of %.0f ms or more (%u "
            "under, %u not known)",
            b->runRestartStalls, b->runLongGap, STALL_BLOCK_SLUMBER_MS, b->runShortGap,
            b->runStalls - b->runLongGap - b->runShortGap);
        if (b->runHeldN > 0) {
            put(buf, size, &at, ", held past the GPU span %.0f-%.0f ms", b->runHeldMin,
                b->runHeldMax);
        }
    }
    put(buf, size, &at, "; by display rate:");
    for (unsigned i = 0; i < b->rateN; i++) {
        snprintf(name, sizeof name, "%s%.2f Hz", first ? " " : ", ", b->rateHz[i]);
        per_minute(buf, size, &at, name, b->rateStalls[i], b->rateSeconds[i]);
        first = 0;
    }
    if (b->rateUnknownSeconds > 0.0 || b->rateUnknownStalls > 0) {
        per_minute(buf, size, &at, first ? " not known" : ", not known", b->rateUnknownStalls,
                   b->rateUnknownSeconds);
    }
    put(buf, size, &at, "; by the block's GPU span:");
    first = 1;
    for (int i = 0; i < STALL_BLOCK_BANDS; i++) {
        char band[32];

        if (!(b->bandSeconds[i] > 0.0) && b->bandStalls[i] == 0) {
            continue;
        }
        band_name(i, band, sizeof band);
        snprintf(name, sizeof name, "%s%s", first ? " " : ", ", band);
        per_minute(buf, size, &at, name, b->bandStalls[i], b->bandSeconds[i]);
        first = 0;
    }
    if (b->bandNoneSeconds > 0.0 || b->bandNoneStalls > 0) {
        per_minute(buf, size, &at, first ? " not timed" : ", not timed", b->bandNoneStalls,
                   b->bandNoneSeconds);
    }
    return (int)at;
}
