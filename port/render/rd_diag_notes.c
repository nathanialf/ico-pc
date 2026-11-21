/* rd_diag_notes.c: the display line's width note and the upload ring's
 * grow words (rd_diag_notes.h). */
#include "rd_diag_notes.h"

#include <stdio.h>

bool rd_width_note(RdWidthNote *n, uint32_t gw, bool logging)
{
    bool due = logging;
    if (n->shown != 0u && gw != n->shown) {
        if (!n->again) {
            due = true;
            n->from = n->shown;
        }
        n->again = true;
    }
    if (due) {
        n->shown = gw;
    }
    return due;
}

static size_t done(int w, size_t cap, char *out)
{
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

size_t rd_width_note_text(const RdWidthNote *n, bool logging, char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (logging || !n->again || n->from == 0u) {
        return 0;
    }
    return done(snprintf(out, cap, " (written again: the frame's width changed from %u)",
                         (unsigned)n->from),
                cap, out);
}

void rd_ring_note_made(RdRingNote *n, double nowMs)
{
    n->made = true;
    n->madeMs = nowMs;
    n->used = false;
    n->usedMs = 0.0;
    n->usedNeed = 0;
}

void rd_ring_note_use(RdRingNote *n, double nowMs, uint64_t need)
{
    n->used = true;
    n->usedMs = nowMs;
    n->usedNeed = need;
}

/* a size in whole MB as the grow line words its need (rounded down) */
static unsigned long long mb_of(uint64_t b)
{
    return (unsigned long long)(b >> 20);
}

size_t rd_ring_note_text(const RdRingNote *old, uint64_t oldCap, double nowMs, char *out,
                         size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    if (!old->made || oldCap == 0) {
        return done(snprintf(out, cap, "; no ring before"), cap, out);
    }
    const double age = nowMs > old->madeMs ? (nowMs - old->madeMs) / 1000.0 : 0.0;
    if (!old->used) {
        return done(snprintf(out, cap,
                             "; the %llu MB ring it replaces was made %.1f s before, never used "
                             "before",
                             mb_of(oldCap), age),
                    cap, out);
    }
    const double last = nowMs > old->usedMs ? (nowMs - old->usedMs) / 1000.0 : 0.0;
    return done(snprintf(out, cap,
                         "; the %llu MB ring it replaces was made %.1f s before, last used %.2f s "
                         "before for %llu MB",
                         mb_of(oldCap), age, last, mb_of(old->usedNeed)),
                cap, out);
}
