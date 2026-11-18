/*
 * port/platform/last_run_skin.c
 *
 * The skin check's counts in words for logs/last-run.txt and the system's
 * summary (last_run_skin.h; issue 217).
 */
#include "last_run_skin.h"

#include <stdio.h>

void last_run_skin_words(const LastRunSkin *s, char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (s == NULL || !s->on) {
        return;
    }
    snprintf(out, size,
             "the skin check: %llu checks, %llu box checks drawn, %llu twins drawn, %llu of its "
             "own pipelines begun%s",
             s->checks, s->box, s->twins, s->begun,
             s->phone ? " (a tile-based phone GPU: none compiled)" : "");
}

void last_run_skin_short(const LastRunSkin *s, char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (s == NULL || !s->on) {
        return;
    }
    snprintf(out, size, "skin box %llu twin %llu made %llu", s->box, s->twins, s->begun);
}
