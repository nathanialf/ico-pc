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

void stall_block_add(StallBlock *b, const StallBlockRec *r)
{
    const int stalled = r->presented && r->presentMs >= STALL_BLOCK_MS;

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
        }
        b->stalls++;
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
    unsigned i;

    if (!(blockSeconds > 0.0)) {
        blockSeconds = 0.0;
    }
    b->runBlocks++;
    b->runSeconds += blockSeconds;
    b->runStalls += b->stalls;
    if (band < 0) {
        b->bandNoneSeconds += blockSeconds;
        b->bandNoneStalls += b->stalls;
    } else {
        b->bandSeconds[band] += blockSeconds;
        b->bandStalls[band] += b->stalls;
    }
    if (!(displayHz > 0.0)) {
        b->rateUnknownSeconds += blockSeconds;
        b->rateUnknownStalls += b->stalls;
        return;
    }
    for (i = 0; i < b->rateN; i++) {
        if (fabs(b->rateHz[i] - displayHz) <= 0.05) {
            break;
        }
    }
    if (i == b->rateN) {
        if (b->rateN < STALL_BLOCK_RATES) {
            b->rateHz[b->rateN++] = displayHz;
        } else {
            i = STALL_BLOCK_RATES - 1; /* counted with the last */
        }
    }
    b->rateSeconds[i] += blockSeconds;
    b->rateStalls[i] += b->stalls;
}

int stall_block_end(StallBlock *b, double blockSeconds, double displayHz, int askedMhz,
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
            put(buf, size, &at, "%s%.0f", i ? ", " : "", b->stallMs[i]);
        }
        put(buf, size, &at, " ms%s; GPU idle before the stalled replay: ",
            b->stalls > listed ? ", the first listed" : "");
        for (i = 0; i < listed; i++) {
            const char *sep = i ? ", " : "";

            if (b->stallGap[i] == 1 && b->stallGapMs[i] >= 0.0) {
                put(buf, size, &at, "%s%.1f ms", sep, b->stallGapMs[i]);
            } else if (b->stallGap[i] == 1) {
                put(buf, size, &at, "%sbegun %.1f ms before the one before ended", sep,
                    -b->stallGapMs[i]);
            } else if (b->stallGap[i] == 2) {
                put(buf, size, &at, "%snot measured (the GPU clock restarted)", sep);
            } else {
                put(buf, size, &at, "%snot measured", sep);
            }
        }
        put(buf, size, &at, ")");
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
    if (askedMhz > 0) {
        put(buf, size, &at, ", frame rate pin %.2f Hz asked", (double)askedMhz / 1000.0);
    } else {
        put(buf, size, &at, ", no frame rate asked");
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
