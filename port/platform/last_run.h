/*
 * port/platform/last_run.h
 *
 * logs/last-run.txt: where a run was when it ended without a word (issue
 * 215: a Retroid Pocket 6 whose runs end with no crash line, no device loss
 * and no watchdog end, at a different point each time).
 *
 * The file has a fixed size (LAST_RUN_BYTES) and a fixed layout: one line a
 * field, each in a slot of its own width at its own offset, padded with
 * blanks and ended by a check of the slot's bytes ("... [3fa1c2d0]\n").  It is
 * rewritten in place with one positional write of the whole file: at every
 * heartbeat (2 s, not synced: a process the system kills leaves the
 * system's page cache, which still reaches the disc) and at every phase
 * change (synced: about ten a run).  A write the process did not finish
 * leaves each slot whole, from one write or the other, or failing its
 * check (last_run_parse marks it torn, never a mix).
 *
 * The fields: the version and commit, the pid, the start time (UTC),
 * seconds since the start, the phase (starting with the start's last step,
 * the start-up set with its pipeline, a self-test by its step's name, the
 * game starting, the first picture, play with the stage and the last Main
 * tick), in front or in the background since when, the last onTrimMemory
 * level and when, the GPU's reset count at the start and now (Android's
 * kernel GPU driver; the sampler's setter), free memory now and the run's
 * lowest with the start-up set's floor, the process's memory now and its
 * peak, the driver's GPU memory for this process where a source is
 * readable, the pipelines made and the last pipeline note, and the end:
 * "running" until one is written ("clean", "crash (signal N)" from the
 * crash handler with async-signal-safe calls only, the device loss's and
 * the watchdog's own words).
 *
 * At the next start (diag_host.c ico_diag_startup_activity_begin, beside
 * the activity file's lines) the file is read before it is rewritten, its
 * fields go to the log as "ico_pc: the previous run: <field>: <value>"
 * lines, and it is copied to logs/last-run-previous.txt, which a report zip
 * carries with last-run.txt (report.c).  Nothing here changes behaviour.
 *
 * Android 11 and later: each phase change also keeps a summary of at most
 * LAST_RUN_SUMMARY_MAX bytes, which the next heartbeat (last_run_beat, the
 * heartbeat's own thread: never the game's fiber stack) hands to
 * ActivityManager.setProcessStateSummary
 * (android/last_run_android.c), which the system keeps in the process's
 * exit record even when the files are lost.
 */
#ifndef ICO_PLATFORM_LAST_RUN_H
#define ICO_PLATFORM_LAST_RUN_H

#include <stddef.h>
#include <stdint.h>

/* the slots, in the file's order */
enum {
    LAST_RUN_HEADER,
    LAST_RUN_VERSION,
    LAST_RUN_PID,
    LAST_RUN_STARTED,
    LAST_RUN_SECONDS,
    LAST_RUN_PHASE,
    LAST_RUN_APP,
    LAST_RUN_TRIM,
    LAST_RUN_RESETS,
    LAST_RUN_FREE,
    LAST_RUN_PROCESS,
    LAST_RUN_GPU_MEMORY,
    LAST_RUN_PIPELINES,
    LAST_RUN_END,
    LAST_RUN_SLOTS
};

/* the file's size: the slots' widths summed (last_run.c s_width) */
#define LAST_RUN_BYTES 2120
/* a slot's end: " [hhhhhhhh]\n" (its check) */
#define LAST_RUN_TAIL 12
/* the widest slot's value, with its NUL */
#define LAST_RUN_VALUE_MAX 440
/* the header's value: the layout's version */
#define LAST_RUN_FORMAT "format 1"
/* ActivityManager.setProcessStateSummary's limit (128 bytes), with the NUL */
#define LAST_RUN_SUMMARY_MAX 129

/* the phases, in the order a start passes them */
enum {
    LAST_RUN_PHASE_STARTING,
    LAST_RUN_PHASE_SET,
    LAST_RUN_PHASE_SELFTEST,
    LAST_RUN_PHASE_GAME,
    LAST_RUN_PHASE_FIRST_PICTURE,
    LAST_RUN_PHASE_PLAY,
    LAST_RUN_PHASE_UNKNOWN
};

/* the ends */
enum {
    LAST_RUN_END_RUNNING,
    LAST_RUN_END_CLEAN,
    LAST_RUN_END_CRASH,
    LAST_RUN_END_LOSS,
    LAST_RUN_END_HANG,
    LAST_RUN_END_WATCHDOG,
    LAST_RUN_END_UNKNOWN
};

/* a slot as last_run_parse found it */
enum {
    LAST_RUN_SLOT_READ,
    LAST_RUN_SLOT_CUT,  /* the file ends before the slot does */
    LAST_RUN_SLOT_TORN, /* its check fails: a write the process did not finish */
};

/* What a last-run.txt says (last_run_parse).  -1 for a figure not read or
   not known. */
typedef struct LastRunFacts {
    int found; /* the header read with LAST_RUN_FORMAT */
    int slots; /* the slots read */
    int state[LAST_RUN_SLOTS];
    char value[LAST_RUN_SLOTS][LAST_RUN_VALUE_MAX];
    long pid;
    double seconds; /* since the start, at the last write */
    int phase;      /* LAST_RUN_PHASE_* */
    int stage;      /* play's stage, -1 none */
    long tick;      /* play's last Main tick, -1 none */
    int background; /* 1 in the background, 0 in front, -1 unknown */
    int trimLevel;  /* the last onTrimMemory level, -1 none */
    int resetsStart, resetsNow;
    long freeMb, freeLowMb, floorMb;
    long rssMb, peakMb;
    int end;       /* LAST_RUN_END_* */
    int endSignal; /* a crash's signal, -1 none */
} LastRunFacts;

/* --- The pure part (tested on every platform) ------------------------------ */

const char *last_run_slot_key(int slot);
size_t last_run_slot_offset(int slot);
size_t last_run_slot_width(int slot);
/* The slot's line, exactly last_run_slot_width(slot) bytes (no NUL), into
   out: "<key>: <value>", blanks, " [hhhhhhhh]\n"; a value too long is cut, a
   newline in it becomes a blank.  Async-signal-safe (no library call). */
void last_run_slot_build(int slot, const char *value, char *out);
/* The whole file from the slots' values (NULL: ""), LAST_RUN_BYTES bytes */
void last_run_text(const char *const values[LAST_RUN_SLOTS], char *out);
/* Reads n bytes of a last-run.txt (a file cut short, or torn by a write
   the process did not finish): every slot whole and checked is read,
   the others are marked.  Returns the slots read. */
int last_run_parse(const char *buf, size_t n, LastRunFacts *out);
/* The log line for one slot of the previous run's file, without "ico_pc: "
   ("the previous run: phase: play: stage 1, Main tick 4 (since 14.2 s)");
   0 for a slot that has no line (the header). */
int last_run_previous_line(const LastRunFacts *f, int slot, char *out, size_t size);
/* "2026-11-16 20:56:01 UTC" for seconds since 1970 */
void last_run_utc_text(long long unixSeconds, char *out, size_t size);
/* The phase's words ("play", "the start-up set", ...) */
const char *last_run_phase_words(int phase);
/* The phase a step line moves the run to (ico_diag_step's text, the
   current phase, a self-test in progress, the start-up set not finished):
   the start's "the game runs" step; from the game on, and in a set not
   finished, the current phase; a self-test's step; else "starting" */
int last_run_phase_for_step(const char *step, int current, int selftest, int setOpen);
/* The stage a milestone names ("first Main tick done (stage_no 1)",
   "stage_no -1 -> 1", trace_host.c), LAST_RUN_NO_STAGE for none */
#define LAST_RUN_NO_STAGE (-2)
int last_run_milestone_stage(const char *milestone);
/* The system's summary (at most 128 bytes, LAST_RUN_SUMMARY_MAX with the
   NUL): "ico-pc <phase> | resets 47->66 | free 1911 MB, lowest 1897" */
void last_run_summary_text(const char *phase, int resetsStart, int resetsNow, long freeMb,
                           long lowMb, char *out, size_t size);
/* logs/last-run.txt and logs/last-run-previous.txt beside the activity
   file's path (its folder); 0 when both fit */
int last_run_paths(const char *activityPath, char *cur, char *prev, size_t size);

/* --- The run's file (last_run.c) ------------------------------------------- */

/* What the heartbeat hands over (diag_host.c): -1 for a figure not known */
typedef struct LastRunBeat {
    unsigned ticks;     /* the Main ticks so far */
    unsigned pipesMade; /* ico_diag_set_pipelines_made */
    const char *note;   /* the activity note, "" none */
    long rssKb, peakKb, availKb, totalKb;
    long floorMb; /* -1 no floor on this system, 0 off */
    /* the system's split after the free figure (mem_breakdown.h
       mem_breakdown_last_run_text: "file cache 2104, anon 3012, ..."; ""
       none) */
    char memText[112];
} LastRunBeat;

/* Reads the previous run's file beside activityPath (each line through
   log, without "ico_pc: "), copies it to last-run-previous.txt, then
   writes this run's (synced).  Again later: the file is opened again. */
void last_run_begin(const char *activityPath, const char *version, void (*log)(const char *line));
/* The heartbeat's figures: the file rewritten, not synced */
void last_run_beat(const LastRunBeat *b);
/* Phase changes (written and synced; Android's summary): a step line, a
   self-test on or off, the start-up set's count, a present, a milestone */
void last_run_step(const char *step);
void last_run_selftest(int on);
void last_run_set_pipelines(unsigned done, unsigned total);
/* 1 when this present moved the run to the first picture (once a run;
   without the file too) */
int last_run_present(void);
void last_run_milestone(const char *milestone);
/* in front (0) or in the background (1) */
void last_run_background(int background);
/* an onTrimMemory level and its name */
void last_run_trim(int level, const char *name);
/* The GPU's reset count (Android's sampler): the start's (why: NULL when
   readable, else why not), then each sample's */
void last_run_set_gpu_resets_start(int32_t start, const char *why);
void last_run_set_gpu_resets(int32_t now);
/* the activity note (diag_host.c ico_diag_set_activity, "" cleared):
   copied only, written with the next write */
void last_run_set_note(const char *note);
/* the driver's GPU memory for this process, as the sampler words it */
void last_run_set_gpu_memory(const char *text);
/* the text last set (mem_watch.c's lines) */
const char *last_run_gpu_memory_text(void);
/* the system's summary hook (Android 11+, android/last_run_android.c),
   called by last_run_beat's thread with the summary kept since its last
   call, if a phase change kept one */
void last_run_set_summary_hook(void (*fn)(const char *summary));
/* Ends: written and synced.  last_run_end_crash: async-signal-safe (the
   crash handler; code a signal, or on Windows the exception's code) */
void last_run_end_clean(void);
void last_run_end_words(const char *words);
void last_run_end_crash(unsigned long code);
/* the file's path ("" before last_run_begin) */
const char *last_run_path(void);
/* Issue 215 (set_size.h): the start-up set's size decision in short words
   ("start-up set sized by the memory fall: VU left to first use (14.3 MB a
   pipeline)", "" none),
   after the phase's own words in every phase from then on */
void last_run_set_size_words(const char *words);
/* the lowest free memory the beats read, MB (-1 none yet) */
long last_run_lowest_mb(void);

#endif
