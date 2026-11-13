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
 *     ...; stalls 3 in the block (the Adreno attempt on)
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
    /* the Adreno 740 attempt (rhi_pass_row.h): every render pass a
       VkRenderPass object on Qualcomm's own driver; state from
       RhiLimits.passObjectsRow, count the block's growth of
       RhiStats.passObjects (window_perf_rank.c) */
    ICO_ATT_ADRENO_A,
    /* a second Adreno attempt: not built (not in this build) */
    ICO_ATT_ADRENO_B,
    /* the subpass read (port/rhi/rhi_subpass_verdict.h,
       port/render/rd_subpass_probe.c): DATE's stencil set-up inside the
       scene's pass on a VkRenderPass path without local read; count the
       set-ups drawn through it (window_perf_rank.c, RdPerfRecord
       .dateStencilTakes where RhiLimits.localReadSubpass) */
    ICO_ATT_SUBPASS,
    /* the blend constant (measured: no draw of the Mali's pictures to
       convert; not in this build) */
    ICO_ATT_BLEND_CONST,
    /* rd_vu_compact.c: the index compaction (the compute record's mesh
       triangles that draw nothing written as a clipped filler in each
       draw's list); state from ico_attempt_decide_compaction, count the
       triangles left out a block (the counters read back) */
    ICO_ATT_COMPACTION,
    /* rd_vu_class.c (rd_vu_record.c): the VU record by compute dispatches
       in place of the vertex stores on the PowerVR class; count the
       dispatches */
    ICO_ATT_VU_COMPUTE_CLASS,
    /* rd_turn_fold.c (rd_present.c): presents whose turn into a pre-rotated
       swapchain was drawn by the box blit itself (no turn pass); second
       count the presents on a turned swapchain that kept the turn pass */
    ICO_ATT_TURN_FOLD,
    /* rd_vu_rec_early.c: the VU record made at start-up at the run's need
       (rd_early_start_rows) instead of the largest the memory bound
       allowed; count the record's growths in play (rd__vu_rec_grew) */
    ICO_ATT_REC_NEED,
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
/* the row's why as last set (NULL none or out of range) */
const char *ico_attempt_why_of(int id);
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
   block" and, where the Adreno row is reported, "(the Adreno attempt on)"
   or "(the Adreno attempt off)"; "" when no row is reported and stalls < 0; the length
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
/* the Adreno attempt (window_perf_rank.c at the first block): row the
   device's RhiLimits.passObjectsRow (its reason while Qualcomm's row holds
   the passes; NULL otherwise, and on the other backends) */
void ico_attempt_decide_pass_objects(const char *row);
/* the record sized from need (rd_vu_rec_early.c rd_vu_record_prepare, once
   at start-up): wanted the pre-pass is on, made the rows made at start-up
   (0 none), bound the rows the memory bound alone allowed, need the run's
   need (rd_early_start_rows; 0 none): on where the need's size made less
   than the bound allowed, else not applicable with the reason */
void ico_attempt_decide_rec_need(int wanted, uint32_t made, uint32_t bound, uint32_t need);
void ico_attempt_decide_no_volume_break(int env, int classOk, int defaultOn, const char *gateWhy);
/* the VU record by compute on the PowerVR class (rd_vu_class.c, at the
   record's start): byName a [dev] vu_record asked for by name, stores auto
   takes the vertex stores here, compute the device has compute stores,
   powervr the PowerVR class under Vulkan, env the tests' switch
   (ICO_RD_VU_COMPUTE_CLASS: -1 unset, 0, 1), defaultOn
   RD_VU_COMPUTE_CLASS_DEFAULT.  1 when the record is to be written by
   compute dispatches (the row on), else 0 */
int ico_attempt_decide_vu_compute(int byName, int stores, int compute, int powervr, int env,
                                  int defaultOn);
/* the turn in the present's blit (rd_turn_fold.c, at each change of its
   inputs): turned the swapchain is pre-rotated (or the tests' twin), env
   the tests' switch (ICO_RD_TURN_FOLD: -1 unset, 0, 1), defaultOn
   RD_TURN_FOLD_DEFAULT, selftest its start-up self-test (-1 not run, 0
   failed, 1 passed).  1 when the fold may run (each present still checks
   its draws), else 0 */
int ico_attempt_decide_turn_fold(int turned, int env, int defaultOn, int selftest);
/* the index compaction (rd_vu_compact.c, at the pre-pass's start and after
   the record's self-test): compute the record is written by compute
   dispatches with the pre-pass on (else notWhy, the record path that
   holds: not applicable), arm the Arm class under Vulkan (the Mali-G52),
   env the tests' switch (ICO_RD_VU_COMPACT: -1 unset, 0, 1), defaultOn
   RD_VU_COMPACT_DEFAULT, armDefaultOn RD_VU_COMPACT_ARM_DEFAULT, selftest
   the record's start-up self-test with it (-1 not run, 0 refused it, 1
   passed).  1 when the compaction runs (the row on), else 0 */
int ico_attempt_decide_compaction(int compute, const char *notWhy, int arm, int env, int defaultOn,
                                  int armDefaultOn, int selftest);
/* the subpass read (rd_subpass_probe.c, at DATE's self-test and after its
   probe): route the device's passes are VkRenderPass objects without local
   read (RhiLimits.subpassRoute), on the read in force for DATE (the device
   made its shape and DATE's self-test and the probe kept it), refused this
   start's self-test or probe, or an earlier start's verdict, refused it;
   offDefault the device's class keeps it off by default this release
   (RhiLimits.subpassOffDefault: the Arm and Qualcomm drivers, any class but
   the Intel driver row); why the decision's words (a string that outlives the row: the device's
   RhiLimits.subpassWhy or the probe's verdict) */
void ico_attempt_decide_subpass(int route, int on, int refused, int offDefault, const char *why);

#endif
