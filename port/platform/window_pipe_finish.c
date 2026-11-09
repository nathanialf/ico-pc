/*
 * port/platform/window_pipe_finish.c
 *
 * The free-memory floor's driver class and the background finisher at the
 * window's start (window_pipe_finish.h).
 */
#include "window_pipe_finish.h"

#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_time.h>
#include "diag_host.h"
#include "host_config.h"
#include "memory_floor.h"
#include "pipe_resume.h"
#include "rd.h"
#include "rhi.h"

/* this start resumed the set (window_pipe_floor_retry), and the stop it
   resumed after (its count steps the next stop's) */
static int s_resumed;
static IcoPipeResume s_prevStop;

/* The stop's place for the next start (issue 123): appended to this
   start's stop line in the activity file when the stop is one the next
   start follows (the system's signal, or the free-memory floor on
   Qualcomm's own driver), with its memory numbers in the log */
static void resume_note_stop(void)
{
    RdPipelineSetStop st;
    IcoPipeResume r;
    SDL_Time now;
    SDL_DateTime dt;
    char part[128], mem[160];

    if (!rd_pipeline_set_stop(&st)) {
        return;
    }
    memset(&r, 0, sizeof(r));
    r.at = st.at;
    r.total = st.total;
    r.batch = st.batch;
    r.stops = ico_pipe_resume_stops(s_resumed, &s_prevStop);
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &dt, true)) {
        const int put = snprintf(r.date, sizeof(r.date), "%04d-%02d-%02d %02d:%02d", dt.year,
                                 dt.month, dt.day, dt.hour, dt.minute);
        (void)put;
    }
    (void)ico_pipe_resume_part(part, sizeof(part), &r);
    ico_diag_memory_text(mem, sizeof(mem));
    if (!ico_diag_startup_set_resume_part(part)) {
        /* for this start only (the careful set's own budget, or the floor
           on another driver): the next start makes the set again */
        fprintf(stderr,
                "window: pipelines: the start-up set stopped at pipeline %u of %u (batches of "
                "%u; %s) %s; the next start makes the whole set again\n",
                st.at + 1u, st.total, st.batch, mem[0] ? mem : "memory unknown", st.why);
        return;
    }
    fprintf(stderr,
            "window: pipelines: the start-up set stopped for memory at pipeline %u of %u "
            "(batches of %u, stop %u in a row): %s; %s; the rest is made as the game needs it, "
            "and the next start resumes at pipeline %u in batches of %u\n",
            st.at + 1u, st.total, st.batch, r.stops, mem[0] ? mem : "memory unknown", st.why,
            st.at + 1u, ico_pipe_resume_batch(st.batch));
}

void window_pipe_floor_driver(void)
{
    const RhiLimits *l = rhi_limits();

    ico_diag_set_memory_floor_driver(l != NULL ? l->vendorId : 0u,
                                     l != NULL ? l->driverText : NULL);
    ico_diag_set_pipe_compiler_serial(l != NULL ? l->serialPipelines : NULL);
}

void window_pipe_finish_start(void)
{
    const int driver = ico_diag_memory_floor_driver();

    if (rd_pipelines_lazy_on()) {
        resume_note_stop();
    }
    if (!rd_pipelines_lazy_on() || !ico_diag_floor_stopped()) {
        return;
    }
    if (driver != ICO_MEMORY_FLOOR_DRIVER_OTHER) {
        fprintf(stderr,
                "window: pipelines: the start-up set stopped at the free-memory floor on %s: the "
                "rest is made on first use, not in the background (issue 161: that driver's "
                "compiler ran the device out of memory)\n",
                ico_memory_floor_driver_words(driver));
        return;
    }
    (void)rd_pipelines_background_start(ico_diag_background_memory);
}

void window_pipe_skin_check(int pipePolicy)
{
    const int driver = ico_diag_memory_floor_driver();
    char why[200];
    char logs[ICO_PATH_MAX];

    /* the check's capture on failure (issue 123): its sidecar beside the
       log, so the share menu and the frame dump's zip take it */
    rd_set_vu_skin_capture_dir(ico_host_logs_dir(logs, sizeof logs) == 0 ? logs : NULL);

    if (driver != ICO_MEMORY_FLOOR_DRIVER_OTHER &&
        (ico_diag_floor_stopped() || pipePolicy == ICO_DIAG_PIPESET_LAZY_LOW_MEMORY)) {
        snprintf(why, sizeof why,
                 "the start-up set %s for memory on %s; issue 161: that driver's compiler ran the "
                 "device out of memory",
                 pipePolicy == ICO_DIAG_PIPESET_LAZY_LOW_MEMORY ? "of the last start stopped"
                                                                : "stopped",
                 ico_memory_floor_driver_words(driver));
        rd_vu_skin_background(ico_diag_background_memory, why);
        return;
    }
    rd_vu_skin_background(ico_diag_background_memory, NULL);
}

/* the cache file's size in bytes, -1 when there is none */
static long long cache_file_bytes(const char *path)
{
    SDL_PathInfo info;

    if (path == NULL || path[0] == '\0' || !SDL_GetPathInfo(path, &info) ||
        info.type != SDL_PATHTYPE_FILE) {
        return -1;
    }
    return (long long)info.size;
}

int window_pipe_floor_retry(int pipePolicy)
{
    const char *line = ico_diag_startup_prev_low_memory_line();
    const int loaded = rhi_pipeline_cache_loaded() ? 1 : 0;
    char where[160];
    unsigned at = 0, batch = 0;

    s_resumed = 0;
    memset(&s_prevStop, 0, sizeof(s_prevStop));
    if (pipePolicy != ICO_DIAG_PIPESET_LAZY_LOW_MEMORY) {
        return pipePolicy;
    }
    /* issue 123: the set resumed where the last start's stopped for memory,
       in half its batches (at least 4), the memory asked between batches as
       in any set; before v0.11.2 such a stop turned the set off for every
       later start of the version, and with the pipeline cache empty (another
       driver's file) each new pipeline was compiled inside a picture's
       walk (a 1.4 s freeze for 29 of them) */
    if (ico_pipe_resume_parse(line, &s_prevStop)) {
        at = s_prevStop.at;
        batch = ico_pipe_resume_batch(s_prevStop.batch);
        snprintf(where, sizeof(where), "at pipeline %u of %u after the low-memory stop of %s",
                 at + 1u, s_prevStop.total,
                 s_prevStop.date[0] ? s_prevStop.date : "an unknown date");
    } else {
        /* a line without the place (written by an older version): from the
           first, in half the set's batches */
        snprintf(where, sizeof(where),
                 "at pipeline 1 after the low-memory stop of an earlier start (its line names "
                 "no place)");
    }
    /* the pipelines before the place made after the rest, whatever the
       cache: a stop of a resumed set (itself from a place) leaves keys
       before its own place that no start made, and a key the cache holds
       is made from it in a moment (rd.h rd_set_pipeline_resume's
       skipBefore stays for a caller that knows the whole head is cached) */
    rd_set_pipeline_resume(at, batch, false);
    ico_diag_startup_resume();
    s_resumed = 1;
    {
        char b[32];

        if (batch > 0) {
            snprintf(b, sizeof(b), "batches of %u", batch);
        } else {
            snprintf(b, sizeof(b), "batches of half the set's");
        }
        fprintf(stderr,
                "window: start-up set: resumed %s (%s, stop %u in a row; %s; the pipeline cache "
                "file %lld bytes, loaded by the renderer: %s): %s\n",
                where, b, s_prevStop.stops ? s_prevStop.stops : 1u,
                at == 0 ? "the whole set"
                        : "the pipelines before it made after the rest, from the pipeline cache "
                          "where it holds them",
                cache_file_bytes(ico_diag_pipeline_cache_path()), loaded ? "yes" : "no",
                line[0] ? line : "no stop line");
    }
    return ICO_DIAG_PIPESET_FULL;
}

void window_pipe_lazy_why(char *why, size_t size, int pipePolicy, int relaunch, int saved)
{
    if (relaunch) {
        snprintf(why, size, "(a restart after a graphics driver crash)");
    } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_SETTING) {
        snprintf(why, size, "by the settings ([dev] pipeline_set = \"lazy\")");
    } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_ONCE) {
        snprintf(why, size,
                 "(the previous start ended while preparing them in the careful set; for this "
                 "start only)");
    } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_NOW && saved) {
        snprintf(why, size,
                 "(the previous start ended while preparing them; [dev] pipeline_set = \"lazy\" "
                 "from now on)");
    } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_NOW) {
        snprintf(why, size,
                 "(the previous start ended while preparing them; [dev] pipeline_set = \"lazy\" "
                 "could not be written, so for this start only)");
    } else if (pipePolicy == ICO_DIAG_PIPESET_LAZY_LOW_MEMORY) {
        /* not reached since issue 123's resume (window_pipe_floor_retry
           makes such a start the set resumed), kept for the words */
        snprintf(why, size, "(the previous start's set stopped when the system ran low on memory)");
    } else {
        snprintf(why, size, "(the last run made them that way)");
    }
}
