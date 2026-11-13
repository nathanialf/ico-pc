/*
 * port/platform/attempt_ledger.h
 *
 * The attempt ledger (docs/port/ANDROID_DIAGNOSTICS.md, "The attempt
 * ledger"): every behaviour a release changes on purpose for a device
 * class, one row each, with its state on this device and how often it
 * fired.  A reporter's log then shows at a glance which attempts ran on
 * their device, and two logs (two releases, two devices) can be compared
 * attempt by attempt.  The lines ("window: " before each):
 *   attempts at the start: few-cores rule on (2 cores faster than the
 *     slowest); pipeline set on every core on (...); capture in two steps
 *     on (a dump worker); flat outputs at the provoking corner on (every
 *     device); one DATE set-up not reported; ...
 *   attempts: few-cores rule on 3 threads moved; capture in two steps on 1
 *     captures (0 grown here); flat outputs at the provoking corner on
 *     (static); one DATE set-up on 270 shared (0 retaken by the guard);
 *     ...; stalls 3 in the block (the Adreno attempts on)
 * The first once a run at the first 10 s block (and again when a state
 * changes later: the first 8 changes, then every 100th), the second every
 * 10 s block.
 *
 * The registry is a table (attempt_ledger.c k_rows): a package adds one
 * enum value here and one row there (its words and its counts' words),
 * sets its state once where it decides (ico_attempt_state, any thread) and
 * counts where it fires (ico_attempt_fire, ico_attempt_fire2: one relaxed
 * atomic add, any thread, hot paths included).  It lives in ico_thread,
 * which the renderer (ico_render) and the platform (ico_platform, the
 * window) both link.  The texts are pure over the rows' atomics
 * (port/platform/test/attempt_ledger_test.c).
 */
#ifndef ICO_PLATFORM_ATTEMPT_LEDGER_H
#define ICO_PLATFORM_ATTEMPT_LEDGER_H

#include <stddef.h>
#include <stdint.h>

/* An attempt's state on this device: NOT_SET its owner has not said (not
   wired in this build, or not reached yet); ON in force; OFF_DEFAULT built
   but off by default here (a phone rc turns it on); REFUSED a start-up
   self-test refused it on this device; NA not applicable to this device
   (its class or capability is another's) */
typedef enum IcoAttemptState {
    ICO_ATT_NOT_SET = 0,
    ICO_ATT_ON,
    ICO_ATT_OFF_DEFAULT,
    ICO_ATT_REFUSED,
    ICO_ATT_NA,
    /* not in this build: a row kept for an attempt not built yet (its lines
       say so at the start and leave it out of the blocks) */
    ICO_ATT_NOT_BUILT
} IcoAttemptState;

/* The rows, in the lines' order */
enum {
    /* render_thread.c: threads moved off the faster cores by the
       few-cores rule (the keeper's moves and the once-a-second sweep) */
    ICO_ATT_FEW_CORES = 0,
    /* render_thread.c: pipeline workers of a set made
       while the few-cores rule is in force kept on every core; count the
       workers, second count the sets */
    ICO_ATT_PIPE_SET_CORES,
    /* window_dump_auto.c: slow-scene captures made in two steps;
       second count the captures grown on the game thread instead */
    ICO_ATT_CAPTURE_TWO_STEP,
    /* rd_vu_flat0.c: static, no count */
    ICO_ATT_FLAT0,
    /* rd_date_one.c: the shadow composites that shared the first
       composite's DATE set-up; second count the retakes the run-time guard
       made */
    ICO_ATT_DATE_ONE,
    /* the shadow composites skipped with an empty source box (their
       set-ups not drawn) */
    ICO_ATT_EMPTY_SKIP,
    /* rd_no_volume.c: no-volume pictures whose count and level passes
       were skipped with SCENE's pass still ended at the resolve */
    ICO_ATT_NO_VOLUME_BREAK,
    /* the Adreno 740 attempts (their files name them at their state call's
       why; the rows' words are generic until renamed here) */
    ICO_ATT_ADRENO_A,
    ICO_ATT_ADRENO_B,
    /* the other exact cuts planned for the tile-based phones: the subpass
       read, the blend constant, the index compaction */
    ICO_ATT_SUBPASS,
    ICO_ATT_BLEND_CONST,
    ICO_ATT_COMPACTION,
    ICO_ATT_COUNT
};

/* the row's state and why (a static string: kept as a pointer; NULL "");
   a change from the state before moves the serial */
void ico_attempt_state(int id, IcoAttemptState state, const char *why);
/* the row's counts (any thread; a relaxed atomic add each) */
void ico_attempt_fire(int id, uint64_t n);
void ico_attempt_fire2(int id, uint64_t n);

/* the row's state now (ICO_ATT_NOT_SET for an id out of range) */
IcoAttemptState ico_attempt_state_of(int id);
/* the row's words ("few-cores rule"), NULL out of range */
const char *ico_attempt_name(int id);
/* the state's word: "not reported", "on", "off by default", "refused by
   its self-test", "not applicable here" */
const char *ico_attempt_state_word(IcoAttemptState s);
/* moves at every state change (the start line printed again) */
uint32_t ico_attempt_serial(void);

/* "attempts at the start: few-cores rule on (why); ..." (every row; a why
   in brackets where given); the length written (as snprintf's, cut at
   cap - 1) */
size_t ico_attempt_start_text(char *out, size_t cap);
/* "attempts: few-cores rule on 3 threads moved; ..." with the counts since
   the last call (each row's counts taken: the next call starts from
   here); rows not reported left out; stalls >= 0 adds "; stalls N in the
   block" and, where an Adreno row is reported, "(the Adreno attempts on)"
   or "(... off)"; "" when no row is reported and stalls < 0; the length
   written */
size_t ico_attempt_block_text(char *out, size_t cap, int stalls);

/* every row back to its state at the start (NOT_SET, the rows not built
   NOT_BUILT) and 0 (the tests) */
void ico_attempt_reset(void);

/* The deciders: each row's state and why from the facts its owner has, one
   call where it decides (so the test can set every row as a device of each
   class would: attempt_ledger_test).  The whys are static strings.
   cores (render_thread.c place_init, both rows): faster the cores faster
   than the slowest, pin the start's pins allowed (Android, [dev]
   big_cores), noPin why not (NULL none) */
void ico_attempt_decide_cores(int faster, int pin, const char *noPin);
/* the slow-scene capture in two steps: every platform (window_perf_rank.c) */
void ico_attempt_decide_capture(void);
/* the flat outputs at the provoking corner (rd_vu_flat0.c): shipped build
   on, or the tests' twins */
void ico_attempt_decide_flat0(int on);
/* one DATE set-up (rd_date_one.c, each replay's scan): mode 0 off
   (ICO_RD_DATE_ONE=0), 1 on, 2 the tests' force; setting MSAA or the exact
   blend in force (nothing shared) */
void ico_attempt_decide_date_one(int mode, int setting);
/* the empty composites (rd_shadow_box.c, each replay's scan): box the
   volumes' box on (ICO_RD_SHADOW_BOX), mode the skip's (0 off, 1 on) */
void ico_attempt_decide_empty_skip(int box, int mode);
/* the no-volume break (rd_no_volume.c, each replay's scan): env the tests'
   switch (-1 unset, 0, 1), classOk the PowerVR or MoltenVK class under
   Vulkan, defaultOn RD_NO_VOLUME_BREAK_DEFAULT, gateWhy a setting gate's
   refusal with the class allowed (the DATE path, MSAA, the pass window, the
   exact blend, GS state as shader data; NULL none) */
void ico_attempt_decide_no_volume_break(int env, int classOk, int defaultOn, const char *gateWhy);

#endif
