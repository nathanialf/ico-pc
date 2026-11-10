/*
 * port/platform/dump_auto.h
 *
 * The automatic dumps' worker (issues 123 and 179): the skin check's capture
 * and the slow-scene dump used to write the .rddump, flush the log and build
 * the zip on the game thread, a 0.8-1.5 s hitch each ("late by 818 ms",
 * resyncs of 890 / 1460 / 841 ms). The game thread now copies what only it
 * may read (the frame's bytes, in memory) and hands the job to one worker
 * thread, which builds the zip, adds the skin check's sidecar, prunes and
 * records the result. The game thread picks the result up at its next poll
 * and prints the line, with both costs:
 *   "slow-scene dump: 3.1 ms on the game thread, 912 ms on the worker".
 *
 * One worker, one job at a time. A job offered while one is written is
 * dropped with a log line (nothing is queued, so the memory is bounded by
 * one job: the copy of one or two frames, at most 192 MiB each). The worker
 * is joined at shutdown (ico_dump_auto_shutdown), a job in flight finishing
 * first. The Settings bug-report picture is a user action and stays
 * synchronous; its zip carries the run's automatic zips still on disk
 * (ico_dump_auto_bundle).
 */
#ifndef ICO_PLATFORM_DUMP_AUTO_H
#define ICO_PLATFORM_DUMP_AUTO_H

#include <stddef.h>
#include "host_config.h"

typedef struct IcoDumpJob {
    char tag[32];                /* "slow-scene dump", "skin-check capture": the log lines' name */
    char zip[ICO_PATH_MAX];      /* the zip to write */
    char dumpName[ICO_PATH_MAX]; /* the dump's entry name; as a loose file when the zip fails */
    char prevName[ICO_PATH_MAX]; /* the tick before's, when prev is not NULL */
    char logPath[ICO_PATH_MAX];  /* the log copied in, "" none */
    char sidecar[ICO_PATH_MAX];  /* a file added to the zip afterwards, "" none */
    char text[640];              /* printed on success (the game thread's poll) */
    void *dump;                  /* malloc blocks, owned by the worker once accepted */
    void *prev;
    size_t dumpLen, prevLen;
    int stored;    /* the entries stored, not deflated */
    int pruneSlow; /* keep ICO_SLOW_DUMP_KEEP slow-*.zip beside the zip */
    double gameMs; /* the game thread's cost of this job (the snapshot) */
} IcoDumpJob;

typedef struct IcoDumpDone {
    char tag[32];
    char zip[ICO_PATH_MAX];
    char text[640];
    char why[160];
    int result; /* 0 both, 1 without the log, -1 not written */
    double gameMs, workerMs;
    size_t bytes; /* the dump's bytes */
} IcoDumpDone;

/* 1 when a job is being written or its result is not yet picked up: the
   caller skips the snapshot (and logs, ico_dump_auto_dropped) */
int ico_dump_auto_busy(void);
/* Takes the job (copied; the dump blocks become the worker's): 0; or -1
   when busy, -2 when no thread could be made, the blocks still the
   caller's. Either failure is a drop: one line (the first 8, then every
   100th) names the tag and the count. */
int ico_dump_auto_submit(const IcoDumpJob *job);
/* The game thread: the finished job's result when there is one (1; then
   *out is filled, the slot free for the next job), else 0 */
int ico_dump_auto_poll(IcoDumpDone *out);
/* Waits for the job in flight, joins the worker; its result stays for a
   last ico_dump_auto_poll. Safe to call twice or without a job. */
void ico_dump_auto_shutdown(void);
/* The function the worker calls before it copies the log in (the window's
   ico_host_log_flush): the log file holds what the game thread printed
   by then */
void ico_dump_auto_set_flush(void (*flush)(void));
/* The jobs dropped so far */
unsigned ico_dump_auto_dropped(void);

/* The bug report's bundle (window_host.c frame_dump, forReport): adds the
   run's automatic zips still on disk to the report zip: the newest
   ICO_BUNDLE_MAX_FILES, whose sizes together stay within
   ICO_BUNDLE_MAX_BYTES; "automatic-captures.txt" in the zip names what is
   in and what was left out, the line returned in line says the same. The
   number of zips added (0: none, or the zip could not be rebuilt: line
   says why). */
#define ICO_BUNDLE_MAX_FILES 3
#define ICO_BUNDLE_MAX_BYTES (12u << 20)
int ico_dump_auto_bundle(const char *reportZip, char *line, size_t lineSize);

/* The run's automatic zips recorded so far (ico_dump_auto_bundle's list),
   a test's hook to record a file the worker did not write */
void ico_dump_auto_record(const char *zip);
void ico_dump_auto_forget(void);
/* A test's gate: while closed (1) the worker holds the job it took before
   writing it, so a second offer meets a busy worker for certain */
void ico_dump_auto_test_gate(int closed);

#endif /* ICO_PLATFORM_DUMP_AUTO_H */
