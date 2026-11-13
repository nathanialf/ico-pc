/*
 * port/platform/robust_persist.h
 *
 * The port's own [dev] gpu_robust = true (window_host.c: Android writes it
 * at the second start in a row that lost the graphics device, issues 123
 * and 179) turns itself off again.  Robust access checks the bounds of
 * every buffer and image read, which costs every StructuredBuffer read of
 * the VU path (docs/port/RHI.md), and nothing ever cleared the key: both
 * reporters carried it for good.
 *
 * A key the game wrote carries a mark beside it, [dev] gpu_robust_by:
 *     "losses; <version>; <clean starts so far>; <run or ->"
 * e.g. "losses; 0.10.2; 1; -": the losses were on that version of the
 * game, 1 clean start has followed, and "run" is written when a start
 * reaches the game and read by the next start, which then knows whether
 * that start ended cleanly.  Every way of preparing the pipelines writes
 * it (window_host.c, one call after both branches): after the start-up
 * set (full or careful), and after the self-tests on a run that makes
 * them on first use (lazy: the setting, an end while preparing them, a
 * low-memory stop, a restart), the phones that lost the device being
 * often the same ones whose set failed.
 *
 * A [dev] gpu_robust = true in config.toml without the mark (Android's
 * v0.7.10 to v0.10.1 wrote it after two lost starts with no mark, issue
 * 179's reporter carried one; or written by hand) is taken as the game's
 * own from another version: the version rule below removes it at once,
 * both lines by the same removal (robust_start_step, ROBUST_OFF_UNMARKED).
 * A device loss turns robust access on again by itself (the next start
 * only, and two lost starts in a row write the key with the mark).  The
 * key's false is never removed (robust access stays off and the loss
 * ladder writes nothing), and ico-pc.ini's gpu_robust (the line a bug
 * report asks for) is not this file's: neither is touched.
 *
 * Every start with the mark reads the previous start's end:
 *   - the previous start lost the device, or ended while preparing the
 *     graphics: the count goes back to 0;
 *   - it reached the game and ended without either: one more clean start;
 *   - it never reached the game and ended without either: the count stays;
 *   - the game's version is not the mark's: the losses were another
 *     build's, so both keys are removed now.
 * At ROBUST_PERSIST_CLEAN_STARTS clean starts in a row both keys are
 * removed (the next two lost starts in a row write them again).
 *
 * Pure C, no globals: the unit test is port/platform/test/robust_persist_test.c;
 * the window's part is window_robust.c.
 */
#ifndef ICO_ROBUST_PERSIST_H
#define ICO_ROBUST_PERSIST_H

#include <stddef.h>

#define ROBUST_PERSIST_CLEAN_STARTS 3

typedef struct RobustMark {
    char version[48]; /* the game's version the losses were on */
    int clean;        /* clean starts in a row since, 0 .. ROBUST_PERSIST_CLEAN_STARTS */
    int running;      /* the last start reached the game and has not been judged */
} RobustMark;

typedef enum RobustAction {
    ROBUST_NONE = 0,    /* no mark: nothing here is the game's */
    ROBUST_STAYS,       /* robust access stays on; the new mark is to be written */
    ROBUST_OFF_CLEAN,   /* clean starts in a row: remove both keys */
    ROBUST_OFF_VERSION, /* another version of the game: remove both keys */
    ROBUST_OFF_UNMARKED /* true without the game's mark: another version's, remove both keys */
} RobustAction;

typedef struct RobustStep {
    RobustAction action;
    RobustMark mark; /* ROBUST_STAYS: the mark to write */
} RobustStep;

/* The mark's value text parsed; 0, or -1 when it is not one of the game's
   (empty, hand-written, another reason than "losses") */
int robust_mark_parse(const char *value, RobustMark *out);
/* The value text of a mark; 0, or -1 when out is too small */
int robust_mark_format(char *out, size_t size, const RobustMark *mark);

/* What this start does with the mark read from the file (old: NULL when
   there is none or it is not the game's), for the game's version, and how
   the previous start ended (prevLost: it lost the graphics device;
   prevEnded: it ended while preparing the graphics) */
RobustStep robust_persist_step(const RobustMark *old, const char *version, int prevLost,
                               int prevEnded);

/* What this start does with config.toml's own [dev] keys (the file's, not
   ico-pc.ini's): fileOn the file says gpu_robust = true, byValue its
   gpu_robust_by (NULL none).  Not true: ROBUST_NONE.  A mark of the game's:
   robust_persist_step.  True without one: ROBUST_OFF_UNMARKED whatever the
   previous start's end, as robust_persist_step gives ROBUST_OFF_VERSION for
   a mark of another version (no version that wrote a key without the mark
   is a version that reads this) */
RobustStep robust_start_step(int fileOn, const char *byValue, const char *version, int prevLost,
                             int prevEnded);

/* The start's log line for a step other than ROBUST_NONE (cfgName names
   config.toml; old may be NULL for ROBUST_OFF_UNMARKED); 0, or -1 when out
   is too small */
int robust_persist_line(char *out, size_t size, const RobustStep *step, const RobustMark *old,
                        const char *version, const char *cfgName);

/* The start's log line for robust access on by ico-pc.ini's gpu_robust
   (iniName names it): the line a bug report asks for, which the game never
   writes or removes; every start says what it costs; 0, or -1 when out is
   too small */
int robust_ini_line(char *out, size_t size, const char *iniName);

/* The start's log line when config.toml's dev.gpu_robust = true is in a form
   the strip cannot take out (still on after it): nothing written, delete the
   line by hand; 0, or -1 when out is too small */
int robust_unremovable_line(char *out, size_t size, const char *cfgName);

/* The file text without its [dev] gpu_robust and gpu_robust_by lines (and
   the root's dotted dev.gpu_robust / dev.gpu_robust_by lines, before any
   header), everything else byte for byte; malloc'd, NULL on out of memory.
   Other hand-written forms (a quoted key, a key the parser joins another
   way) are left: window_robust.c parses the result as config.toml is read
   and, where dev.gpu_robust is still true, writes nothing and says so
   (robust_unremovable_line) */
char *robust_persist_strip(const char *text);

#endif
