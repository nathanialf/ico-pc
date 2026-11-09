/*
 * port/platform/pipe_resume.c
 *
 * The start-up set resumed after a low-memory stop (pipe_resume.h).
 */
#include "pipe_resume.h"

#include <stdio.h>
#include <string.h>

size_t ico_pipe_resume_part(char *out, size_t cap, const IcoPipeResume *r)
{
    int w;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    w = snprintf(out, cap, ICO_PIPE_RESUME_WORDS "%u of %u, batches of %u, stop %u, %s", r->at,
                 r->total, r->batch, r->stops, r->date[0] != '\0' ? r->date : "an unknown date");
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

int ico_pipe_resume_parse(const char *line, IcoPipeResume *out)
{
    const char *p = line != NULL ? strstr(line, ICO_PIPE_RESUME_WORDS) : NULL;
    IcoPipeResume r;
    int used = 0;

    memset(out, 0, sizeof(*out));
    if (p == NULL) {
        return 0;
    }
    memset(&r, 0, sizeof(r));
    p += sizeof ICO_PIPE_RESUME_WORDS - 1;
    if (sscanf(p, "%u of %u, batches of %u, stop %u, %n", &r.at, &r.total, &r.batch, &r.stops,
               &used) != 4 ||
        used <= 0 || r.total == 0 || r.at >= r.total || r.batch == 0 || r.stops == 0) {
        return 0;
    }
    p += used;
    if (strcmp(p, "an unknown date") != 0) {
        size_t n = strcspn(p, "\r\n");

        n = n < sizeof r.date - 1 ? n : sizeof r.date - 1;
        memcpy(r.date, p, n);
        r.date[n] = '\0';
    }
    *out = r;
    return 1;
}

unsigned ico_pipe_resume_batch(unsigned stopped)
{
    const unsigned half = stopped / 2u;

    if (stopped == 0) {
        return 0;
    }
    return half > ICO_PIPE_RESUME_MIN_BATCH ? half : ICO_PIPE_RESUME_MIN_BATCH;
}

unsigned ico_pipe_resume_at(const IcoPipeResume *r, unsigned total)
{
    return r->at < total ? r->at : 0u;
}

unsigned ico_pipe_resume_stops(int resumed, const IcoPipeResume *prev)
{
    return resumed && prev != NULL && prev->stops > 0 ? prev->stops + 1u : 1u;
}
