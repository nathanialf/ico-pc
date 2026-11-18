/*
 * port/platform/last_run_skin.h
 *
 * The skin check's counts in logs/last-run.txt and the system's summary
 * (issue 217, v0.11.10).  On the Adreno 710 of issue 217 the runs that lost
 * the graphics device died about 4 s into the first 3D scene, before the
 * skin check's first 10 s line: nothing in their logs said whether its box
 * checks and twins were drawn.  The window hands the check's run counts
 * (port/render/rd.h rd_vu_skin_run_counts) to last_run.c
 * (last_run_set_skin_source), which writes them in the pipelines slot at
 * every heartbeat and phase change, and in the system's 128-byte summary
 * (Android's exit record) when they change; the report's summary.txt has the
 * long words (report.c).  Nothing here changes behaviour.
 *
 * Pure: the words alone (tested by last_run_skin_test on every platform).
 */
#ifndef ICO_PLATFORM_LAST_RUN_SKIN_H
#define ICO_PLATFORM_LAST_RUN_SKIN_H

#include <stddef.h>

/* The check's counts this run: on (its start line said on), phone (on a
   tile-based phone GPU: none of its own pipelines compiled), the checks
   drawn, the box checks drawn, the twins drawn, its own pipelines begun */
typedef struct LastRunSkin {
    int on, phone;
    unsigned long long checks, box, twins, begun;
} LastRunSkin;

/* The pipelines slot's words: "the skin check: 35 checks, 0 box checks
   drawn, 0 twins drawn, 0 of its own pipelines begun (a tile-based phone
   GPU: none compiled)"; "" when the check is off (s NULL or s->on 0) */
void last_run_skin_words(const LastRunSkin *s, char *out, size_t size);
/* The summary's short form (at most LAST_RUN_SKIN_SHORT_MAX bytes with the
   NUL: room for three 20-digit counts): "skin box 0 twin 0 made 0"; "" when
   the check is off */
#define LAST_RUN_SKIN_SHORT_MAX 96
void last_run_skin_short(const LastRunSkin *s, char *out, size_t size);

#endif
