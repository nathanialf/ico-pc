/*
 * port/platform/set_size.c
 *
 * The start-up set sized by the device's own memory fall (set_size.h).
 */
#include "set_size.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_host.h"
#include "last_run.h"

static const char *const k_family[SET_SIZE_FAMILIES] = {"2D", "VU"};

/* the figures a text shows are kept to these, so the longest line is known
   (SET_SIZE_LINE_MAX): 9999999 MB is 9.5 TB, beyond any device's memory */
#define SET_SIZE_TEXT_MB 9999999L
#define SET_SIZE_TEXT_FALL 999999.0

static long text_mb(long v)
{
    return v > SET_SIZE_TEXT_MB ? SET_SIZE_TEXT_MB : v < -SET_SIZE_TEXT_MB ? -SET_SIZE_TEXT_MB : v;
}

static double text_fall(double v)
{
    return v > SET_SIZE_TEXT_FALL    ? SET_SIZE_TEXT_FALL
           : v < -SET_SIZE_TEXT_FALL ? -SET_SIZE_TEXT_FALL
                                     : v;
}

static int float_order(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

double set_size_median(const float *v, unsigned n)
{
    static float s[SET_SIZE_SAMPLES_MAX];
    if (n == 0 || v == NULL) {
        return 0.0;
    }
    if (n > SET_SIZE_SAMPLES_MAX) {
        n = SET_SIZE_SAMPLES_MAX;
    }
    memcpy(s, v, (size_t)n * sizeof s[0]);
    qsort(s, n, sizeof s[0], float_order);
    return (double)s[(n - 1u) / 2u];
}

/* a rest's cost in MB: left x fall rounded to the nearest MB, 0 for no fall,
   at most SET_SIZE_TEXT_MB (beyond any device's memory), so two of them add
   without overflow where a long is 32 bits (Windows) */
static long rest_cost(unsigned left, double fall)
{
    const double cap = (double)SET_SIZE_TEXT_MB;
    double c;
    if (left == 0 || !(fall > 0.0)) {
        return 0;
    }
    c = (double)left * fall + 0.5;
    return c >= cap ? (long)cap : (long)c;
}

void set_size_decide(const SetSizeIn *in, SetSizeOut *out)
{
    int order[SET_SIZE_FAMILIES];
    long kept = 0;
    memset(out, 0, sizeof *out);
    out->stop = in->stopped;
    out->costMb[0] = out->costMb[1] = -1;
    if (in->freeMb < 0 || in->floorMb <= 0) {
        return;
    }
    out->active = 1;
    out->headroomMb = in->freeMb - in->floorMb - (in->needMb > 0 ? in->needMb : 0);
    order[0] = in->next == SET_SIZE_VU ? SET_SIZE_VU : SET_SIZE_2D;
    order[1] = 1 - order[0];
    for (int k = 0; k < SET_SIZE_FAMILIES; k++) {
        const int f = order[k];
        const unsigned bit = 1u << f;
        long cost;
        if (in->batches[f] < SET_SIZE_MIN_BATCHES) {
            continue; /* measured first */
        }
        cost = rest_cost(in->left[f], in->fallMb[f]);
        out->costMb[f] = cost;
        if (in->stopped & bit) {
            continue; /* made on first use already */
        }
        if (cost > 0 && kept + cost > out->headroomMb) {
            out->stop |= bit;
            out->newly |= bit;
        } else {
            kept += cost;
        }
    }
    out->keptMb = kept;
}

void set_size_begin(SetSizeRun *r, long freeMb, long floorMb)
{
    memset(r, 0, sizeof *r);
    r->active = freeMb >= 0 && floorMb > 0;
    r->startMb = freeMb;
    r->floorMb = floorMb;
    r->lastMb = freeMb;
    r->lowMb = freeMb;
}

/* one family's part of the decision line */
static size_t family_text(char *out, size_t size, const SetSizeRun *r, const SetSizeIn *in,
                          const SetSizeOut *o, int f)
{
    const unsigned bit = 1u << f;
    const char *outcome;
    char state[160];
    int w;
    if (in->batches[f] < SET_SIZE_MIN_BATCHES) {
        snprintf(state, sizeof state, "not measured yet (%u of %u batches)", in->batches[f],
                 SET_SIZE_MIN_BATCHES);
    } else if (!(in->fallMb[f] > 0.0)) {
        snprintf(state, sizeof state,
                 "no fall (%.1f MB a pipeline, the median of %u batches): its %u left cost "
                 "nothing",
                 text_fall(in->fallMb[f]), in->batches[f], in->left[f]);
    } else {
        snprintf(state, sizeof state,
                 "%.1f MB a pipeline (the median of %u batches): its %u left x %.1f = %ld MB",
                 text_fall(in->fallMb[f]), in->batches[f], in->left[f], text_fall(in->fallMb[f]),
                 text_mb(o->costMb[f]));
    }
    if (o->newly & bit) {
        outcome = "stops now, the rest made on first use";
    } else if (r->stopped & bit) {
        outcome = "stopped earlier";
    } else if (in->left[f] == 0) {
        outcome = "none left";
    } else if (in->batches[f] < SET_SIZE_MIN_BATCHES) {
        outcome = "measured when reached";
    } else {
        outcome = "goes on";
    }
    w = snprintf(out, size, "%s %s, %s", k_family[f], state, outcome);
    return w < 0 ? 0 : (size_t)w < size ? (size_t)w : size - 1;
}

unsigned set_size_ask(SetSizeRun *r, const SetSizeAsk *a, char *line, size_t lineSize, char *why,
                      size_t whySize)
{
    SetSizeIn in;
    SetSizeOut o;
    unsigned due;
    if (line != NULL && lineSize > 0) {
        line[0] = '\0';
    }
    if (why != NULL && whySize > 0) {
        why[0] = '\0';
    }
    if (!r->active) {
        return 0;
    }
    if (a->freeMb >= 0) {
        /* the batches since the last ask that read the memory: a sample when
           one family's pipelines alone were made */
        const unsigned d0 = a->made[0] > r->lastMade[0] ? a->made[0] - r->lastMade[0] : 0u;
        const unsigned d1 = a->made[1] > r->lastMade[1] ? a->made[1] - r->lastMade[1] : 0u;
        if (r->lastMb >= 0 && (d0 > 0u) != (d1 > 0u)) {
            const int f = d1 > 0u ? SET_SIZE_VU : SET_SIZE_2D;
            const unsigned d = d1 > 0u ? d1 : d0;
            if (r->n[f] < SET_SIZE_SAMPLES_MAX) {
                r->fall[f][r->n[f]++] = (float)((double)(r->lastMb - a->freeMb) / (double)d);
            }
        } else if (d0 > 0u && d1 > 0u) {
            r->mixed++;
        }
        r->lastMade[0] = a->made[0];
        r->lastMade[1] = a->made[1];
        r->lastMb = a->freeMb;
        if (r->lowMb < 0 || a->freeMb < r->lowMb) {
            r->lowMb = a->freeMb;
        }
    }
    memset(&in, 0, sizeof in);
    in.freeMb = a->freeMb;
    in.floorMb = r->floorMb;
    in.needMb = a->needMb;
    in.next = a->next;
    in.stopped = r->stopped;
    for (int f = 0; f < SET_SIZE_FAMILIES; f++) {
        in.left[f] = a->left[f];
        in.batches[f] = r->n[f];
        in.fallMb[f] = r->n[f] >= SET_SIZE_MIN_BATCHES ? set_size_median(r->fall[f], r->n[f]) : 0.0;
    }
    set_size_decide(&in, &o);
    if (!o.active) {
        return r->stopped;
    }
    /* a line at a family's first trusted verdict and at a stop */
    due = o.newly;
    for (int f = 0; f < SET_SIZE_FAMILIES; f++) {
        if (in.batches[f] >= SET_SIZE_MIN_BATCHES && in.left[f] > 0 && !(r->said & (1u << f)) &&
            !(r->stopped & (1u << f))) {
            due |= 1u << f;
        }
    }
    if (due != 0u && line != NULL && lineSize > 0) {
        char fam[SET_SIZE_FAMILIES][400];
        int w;
        for (int f = 0; f < SET_SIZE_FAMILIES; f++) {
            (void)family_text(fam[f], sizeof fam[f], r, &in, &o, f);
        }
        w = snprintf(line, lineSize,
                     "ico_pc: pipelines: the start-up set's size at pipeline %u of %u: free %ld "
                     "MB less the floor %ld MB less play's known need %ld MB leaves %ld MB; %s; "
                     "%s; the rests kept %ld MB",
                     a->place + 1u, a->total, text_mb(a->freeMb), text_mb(r->floorMb),
                     text_mb(a->needMb > 0 ? a->needMb : 0), text_mb(o.headroomMb), fam[0], fam[1],
                     text_mb(o.keptMb));
        (void)w;
        r->said |= due;
    }
    if (o.newly != 0u) {
        size_t at = 0;
        for (int f = 0; f < SET_SIZE_FAMILIES; f++) {
            if (!(o.newly & (1u << f))) {
                continue;
            }
            r->leftAtStop[f] = in.left[f];
            r->placeAtStop[f] = a->place;
            r->fallAtStop[f] = in.fallMb[f];
            if (why != NULL && at < whySize) {
                const int w = snprintf(
                    why + at, whySize - at, "%s%s %.1f MB a pipeline, its rest %ld MB",
                    at > 0 ? "; " : "", k_family[f], text_fall(in.fallMb[f]), text_mb(o.costMb[f]));
                at = w < 0 ? at : at + (size_t)w < whySize ? at + (size_t)w : whySize - 1;
            }
        }
        if (why != NULL && at < whySize) {
            const int w = snprintf(why + at, whySize - at, ", against %ld MB above the floor",
                                   text_mb(o.headroomMb));
            (void)w;
        }
    }
    r->stopped = o.stop;
    return r->stopped;
}

size_t set_size_rule_text(char *out, size_t size, int active, const char *offWhy, unsigned total,
                          long freeMb, long floorMb, long recordMb, long arenaMb, long recordMadeMb,
                          long arenaMadeMb)
{
    int w;
    if (size == 0) {
        return 0;
    }
    if (!active) {
        w = snprintf(out, size,
                     "ico_pc: pipelines: the start-up set's size by the device's memory fall: off "
                     "(%s): the set as before",
                     offWhy != NULL ? offWhy : "no free-memory floor on this system");
    } else {
        w = snprintf(out, size,
                     "ico_pc: pipelines: the start-up set's size by the device's memory fall: on "
                     "(%u pipelines; free %ld MB, the floor %ld MB; play's known need not made yet "
                     "%ld MB: the VU pre-pass's record %ld MB and the mesh arena's first chunk %ld "
                     "MB; made already, before the free figure was read: the record %ld MB, the "
                     "chunk %ld MB): each "
                     "family's fall of free memory a pipeline (2D: every pipeline without a VU "
                     "vertex shader; VU) is the median of its batches, trusted after %u; a "
                     "family whose pipelines not made x its fall do not fit in free memory less "
                     "the floor less that need stops at once and is made on first use; a family "
                     "with no fall never stops by this rule",
                     total, text_mb(freeMb), text_mb(floorMb), text_mb(recordMb + arenaMb),
                     text_mb(recordMb), text_mb(arenaMb), text_mb(recordMadeMb),
                     text_mb(arenaMadeMb), SET_SIZE_MIN_BATCHES);
    }
    return w < 0 ? 0 : (size_t)w < size ? (size_t)w : size - 1;
}

size_t set_size_words(const SetSizeRun *r, char *out, size_t size)
{
    int w;
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!r->active || (r->said == 0u && r->stopped == 0u)) {
        return 0;
    }
    if (r->stopped == 0u) {
        w = snprintf(out, size, "start-up set sized by the memory fall: every family fits");
    } else if (r->stopped == 3u) {
        w = snprintf(out, size,
                     "start-up set sized by the memory fall: 2D, VU left to first use (%.1f, %.1f "
                     "MB a pipeline)",
                     text_fall(r->fallAtStop[0]), text_fall(r->fallAtStop[1]));
    } else {
        const int f = r->stopped == 2u ? SET_SIZE_VU : SET_SIZE_2D;
        w = snprintf(out, size,
                     "start-up set sized by the memory fall: %s left to first use (%.1f MB a "
                     "pipeline)",
                     k_family[f], text_fall(r->fallAtStop[f]));
    }
    return w < 0 ? 0 : (size_t)w < size ? (size_t)w : size - 1;
}

size_t set_size_first_picture_text(long freeMb, long totalMb, long lowMb, long floorMb,
                                   const char *words, char *out, size_t size)
{
    char low[64];
    int w;
    if (size == 0) {
        return 0;
    }
    if (lowMb >= 0) {
        snprintf(low, sizeof low, "%ld MB", text_mb(lowMb));
    } else {
        snprintf(low, sizeof low, "not known");
    }
    w = snprintf(out, size,
                 "ico_pc: memory at the first picture: free %ld MB of %ld MB (the floor %ld MB), "
                 "the lowest since the start %s; the %.*s",
                 text_mb(freeMb), text_mb(totalMb), text_mb(floorMb), low, SET_SIZE_WORDS_MAX - 1,
                 words != NULL && words[0] != '\0' ? words
                                                   : "start-up set made as each family fit");
    return w < 0 ? 0 : (size_t)w < size ? (size_t)w : size - 1;
}

/* the run's published state (the window's thread during the set; read at
   the first picture, after it) */
static int s_active;
static long s_floorMb = -1, s_lowMb = -1;
static char s_words[SET_SIZE_WORDS_MAX];
static int s_firstDone;

void set_size_publish(int active, long floorMb, long lowMb, const char *words)
{
    s_active = active != 0;
    s_floorMb = floorMb;
    s_lowMb = lowMb;
    snprintf(s_words, sizeof s_words, "%s", words != NULL ? words : "");
}

void set_size_first_picture(void)
{
    long availKb, totalKb, low;
    char line[400];
    if (s_firstDone) {
        return;
    }
    s_firstDone = 1;
    if (!s_active || ico_diag_device_memory(&availKb, &totalKb) != 0) {
        return;
    }
    low = last_run_lowest_mb();
    if (s_lowMb >= 0 && (low < 0 || s_lowMb < low)) {
        low = s_lowMb;
    }
    if (low < 0 || availKb / 1024 < low) {
        low = availKb / 1024;
    }
    (void)set_size_first_picture_text(availKb / 1024, totalKb / 1024, low, s_floorMb, s_words, line,
                                      sizeof line);
    ico_diag_log("%s", line);
}
