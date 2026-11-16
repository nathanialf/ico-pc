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
    ICO_ATT_NOT_BUILT,
    /* removed: an attempt a later release took off (the high queue
       priority, v0.11.8); the row is kept so two releases' logs compare row
       by row, its start words say when and why, and it is left out of the
       blocks */
    ICO_ATT_REMOVED
} IcoAttemptState;

/* the high queue priority's row's words since its removal (its why at
   every start) */
#define ICO_ATT_QPRIO_REMOVED_WHY "taken off, issues 215 and 123"

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
       RhiStats.passObjects (window_perf_rank.c), second count the block's
       stalls whatever the state (ico_attempt_block_text's stalls,
       window_stall_tally_take's) */
    ICO_ATT_ADRENO_A,
    /* issue 123's second attempt, v0.11.7: the game's queue at high global
       priority on Qualcomm's own driver under Android.  Removed in v0.11.8
       (issues 215 and 123: the hangs did not change, and it was new on a
       handheld that ended at every start): state ICO_ATT_REMOVED from the
       start, why ICO_ATT_QPRIO_REMOVED_WHY, no count (its stall count moved
       to ICO_ATT_ADRENO_A's second) */
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
    /* rd_pace_gpu_live.c (rd_thread.c presentRebase): with pictures as made,
       each in-between picture drawn for the moment its GPU work is predicted
       to end (off by default: computed, not drawn); count the pictures the
       model placed, second count those held at its bound */
    ICO_ATT_PACE_GPU,
    /* rd_pace_rules.c (rd_thread.c presentRebase): with pictures as made,
       each picture's moment from its own timer (rd_as_made_timer.h; off by
       default: computed, not drawn); count the pictures it gave a moment,
       second count those held inside their frame's tick */
    ICO_ATT_AS_MADE_TIMER,
    /* rd_pace_screen_live.c (rd_pace_rules.c, rd_thread.c presentRebase):
       with pictures as made, each picture drawn for the time the screen
       will show it (rd_pace_screen.h; off by default: computed, not drawn);
       count the pictures it placed, second count those held at their frame
       pair's tick's edges */
    ICO_ATT_PACE_SCREEN,
    /* rd_pace_even_live.c (window_present.c's hold, rd_pace_rules.c): with
       pictures as made, every picture shown for the same whole number of
       refreshes (rd_pace_even.h; off by default, an rc variant); count the
       pictures aimed, second count those late for their refresh at the
       hand-over */
    ICO_ATT_PACE_EVEN,
    /* rd_paste_spec.c: the glow's paste drawn by its own entry (fx_paste_ps,
       fx_frame_ps's paste path alone) behind its start-up self-test; count
       the pastes drawn by it, second count the premultiplied adds that kept
       fx_frame_ps */
    ICO_ATT_PASTE_SPEC,
    /* the glow chain in three passes (R folded into the first blur pass, E
       and A into the second): not built, refused by its measurement (each
       folded texel would recompute about 14 and 28 taps where the passes
       load 8 and 7: v0.11.5's FOLD4, which the phone measured slower) */
    ICO_ATT_CHAIN3,
    /* rd_volume_fs.c: the shadow volumes drawn by date_clear_ps (no colour
       output, no discard) in place of sprite_ps, behind its start-up
       self-test; count the volume draws by it, second count the volume
       draws that kept sprite_ps where it is offered */
    ICO_ATT_VOLUME_FS,
    /* rd_comp_spec.c: the shadow composites and levels drawn by their own
       entry (sprite_comp_ps, sprite_ps's path for them alone) behind its
       start-up self-test; count the draws by it, second count the draws of
       their forms that kept sprite_ps where it is offered */
    ICO_ATT_COMP_SPEC,
    /* window_robust.c (robust_persist.h ROBUST_OFF_UNMARKED): a [dev]
       gpu_robust = true without the game's mark (issue 179's: written by
       v0.7.10 to v0.10.1, or by hand) removed from config.toml at the
       start, robust access off from that start; static, no count */
    ICO_ATT_ROBUST_LINE,
    /* rd_vu_skin_check.c (issue 123, v0.11.7): a skin check whose readback
       a GPU reset left (not written again, or read in the span of the
       GPU's own sign of a reset: rd_vu_skin_check.h rd__skin_skip) skipped,
       with no failure and no capture; state at the check's start line
       (rd_vu_skin_selftest.c), count the checks skipped */
    ICO_ATT_SKIN_RESET_SKIP,
    /* rd_health_live.c (issue 179, v0.11.7): the picture-health report
       (counts only, nothing switched); count the presented 3D pictures
       whose recorded stamps came back unwritten after their work was
       signalled finished, second count the GPU clock restarts with work
       queued (an idle GPU's are not counted) */
    ICO_ATT_PICTURE_HEALTH,
    /* pin_release.h (window_clock_trap.c, cpu_keep.h; the owner's Pixel,
       v0.11.7): the render and game threads' single-core pins let go while
       the game is at one picture a tick with a picture's CPU raised, taken
       again after 10 s without; count the times let go, second count the
       times taken again */
    ICO_ATT_PIN_RELEASE,
    /* rd_shadow_count.c, rd_shadowres.c (issue 123, v0.11.8): above 1x the
       first shadow level's step reads the count from the depth-stencil's
       stencil, with no full-size count pass, where the device's own timing
       found it the cheapest of the three forms (refused with the form it
       kept otherwise); count the steps from the stencil, second count the
       full-size passes the guard drew after all */
    ICO_ATT_SHADOW_COUNT,
    /* rd_onepass.c (issue 179, v0.11.8): a LERP As pair of the two-pass
       blend drawn in one pass where its alpha is proven at most 128 (no FBA,
       no PABE), behind its start-up self-test; count the pairs in one pass,
       second count the pairs kept two-pass */
    ICO_ATT_ONEPASS,
    /* set_size.c, window_pipe_finish.c, rd_pipeline.c (issue 215, v0.11.8):
       the start-up set sized by the device's own memory fall, where the
       free-memory floor applies (Android): a family whose rest does not fit
       above the floor stops at once and is made on first use; count the
       pipelines left to first use, second count those made on first use
       after the stop */
    ICO_ATT_SET_SIZE,
    /* rd_plain_camera.c, rd_pipeline.c (issue 123, v0.11.8): on a device
       that keeps the VU plain set (the Adreno 740 on Qualcomm's driver,
       Turnip) the static meshes and grids take the camera rule, the skinned
       draws keep the PS2's drops (issue 163); count the mesh and grid draws
       by the camera, second count the skinned draws kept */
    ICO_ATT_PLAIN_CAMERA,
    /* rd_hold.c (issue 123, v0.11.8): while the game is paused and its scene
       is the one the last full picture drew, lists 11-12 drawn over a copy
       of SCENE taken at list 11 (Vulkan, no joined list 11 passes, no MSAA,
       exact blend or temporal upscaler), behind the first held picture's
       check against the same picture in full; count the pictures held,
       second count those drawn in full while paused */
    ICO_ATT_SCENE_HOLD,
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
   its self-test", "not applicable here", "not in this build", "removed" */
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
   or "(the Adreno attempt off)"; stalls > 0 also counts on the Adreno
   row's second count ("(N stalls in the block)" after its passes); rows
   not in this build or removed are left out; "" when no row is reported
   and stalls < 0; the length written */
size_t ico_attempt_block_text(char *out, size_t cap, int stalls);

/* every row back to its state at the start (NOT_SET, the rows not built
   NOT_BUILT, the removed rows REMOVED with their why) and 0 (the tests) */
void ico_attempt_reset(void);

/* The start line in parts that each fit cap (window_perf_rank.c: a 4 KB
   buffer): from row *row on, "attempts at the start:" (row 0) or
   "attempts at the start (continued):" and the whole rows that fit; *row
   is left at the next row (ICO_ATT_COUNT when all were written).  A row
   longer than the whole buffer is cut there and passed.  The length. */
size_t ico_attempt_start_part(char *out, size_t cap, int *row);

/* The deciders: each row's state and why from the facts its owner has, one
   call where it decides (so the test can set every row as a device of each
   class would: attempt_ledger_test).  The whys are static strings.
   cores (render_thread.c place_init, both rows): faster the cores faster
   than the slowest, pin the start's pins allowed (Android, [dev]
   big_cores), noPin why not (NULL none) */
void ico_attempt_decide_cores(int faster, int pin, const char *noPin);
/* the slow-scene capture in two steps: every platform (window_perf_rank.c);
   its counts the slow-scene dumps alone (F12's and the skin check's
   captures are made another way) */
void ico_attempt_decide_capture(void);
/* the flat outputs at the provoking corner (rd_vu_flat0.c at the
   renderer's start): shipped build on, or the tests' twins; prepass the VU
   pre-pass draws (rd.h rd_vu_prepass_on): without it vu_main_vs never runs
   and the row is not applicable.  ico_attempt_flat0_reach: the same with
   the pre-pass as it is now (window_perf_rank.c every block: the pre-pass
   can turn off after the start, its self-test or its record), nothing
   before the renderer said */
void ico_attempt_decide_flat0(int on, int prepass);
void ico_attempt_flat0_reach(int prepass);
/* one DATE set-up (rd_date_one.c, each replay's scan): mode 0 off
   (ICO_RD_DATE_ONE=0), 1 on, 2 the tests' force; setting MSAA or the exact
   blend in force (nothing shared) */
void ico_attempt_decide_date_one(int mode, int setting);
/* the empty composites (rd_shadow_box.c, each replay's scan): box the
   volumes' box on (ICO_RD_SHADOW_BOX), mode the skip's (0 off, 1 on) */
void ico_attempt_decide_empty_skip(int box, int mode);
/* the no-volume break (rd_no_volume.c, each replay's scan): env the tests'
   switch (-1 unset, 0, 1), classOk the PowerVR or MoltenVK class under
   Vulkan, measured the device it was shown clean on (the PowerVR D-Series
   DXT-48-1536: the default's reach; the rest of the class off by default),
   defaultOn RD_NO_VOLUME_BREAK_DEFAULT, gateWhy a setting gate's refusal
   with the class allowed (the DATE path, MSAA, the pass window, the exact
   blend, GS state as shader data; NULL none) */
/* the Adreno attempt (window_perf_rank.c at the first block): row the
   device's RhiLimits.passObjectsRow (its reason while Qualcomm's row holds
   the passes; NULL otherwise, and on the other backends) */
void ico_attempt_decide_pass_objects(const char *row);
/* the record sized from need (rd_vu_rec_early.c rd_vu_record_prepare, once
   at start-up): wanted the pre-pass is on, made the rows made at start-up
   (0 none), bound the rows the memory bound alone allowed, need the run's
   need (rd_early_start_rows; 0 none), have the rows the record held before
   (rd__vu_rec_rows): on where the need's size made less than the bound
   allowed, else not applicable with the reason (nothing made because the
   record already held the need is said as that, not as the memory) */
void ico_attempt_decide_rec_need(int wanted, uint32_t made, uint32_t bound, uint32_t need,
                                 uint32_t have);
void ico_attempt_decide_no_volume_break(int env, int classOk, int measured, int defaultOn,
                                        const char *gateWhy);
/* the VU record by compute on the PowerVR class (rd_vu_class.c, at the
   record's start): byName a [dev] vu_record asked for by name, stores auto
   takes the vertex stores here, compute the device has compute stores,
   powervr the PowerVR class under Vulkan, measured the device is the
   PowerVR it was shown on (rd_vu_class.h rd__vu_class_measured: the
   D-Series DXT-48-1536; the rest of the class off by default), env the
   tests' switch (ICO_RD_VU_COMPUTE_CLASS: -1 unset, 0, 1), defaultOn
   RD_VU_COMPUTE_CLASS_DEFAULT.  1 when the record is to be written by
   compute dispatches (the row on), else 0 */
int ico_attempt_decide_vu_compute(int byName, int stores, int compute, int powervr, int measured,
                                  int env, int defaultOn);
/* the turn in the present's blit (rd_turn_fold.c, at each change of its
   inputs): turned the swapchain is pre-rotated (or the tests' twin), env
   the tests' switch (ICO_RD_TURN_FOLD: -1 unset, 0, 1), defaultOn
   RD_TURN_FOLD_DEFAULT, selftest its start-up self-test (-1 not run, 0
   failed, 1 passed).  1 when the fold may run (each present still checks
   its draws), else 0 */
int ico_attempt_decide_turn_fold(int turned, int env, int defaultOn, int selftest);

/* the index compaction's device classes (rd_vu_compact.c rd_vu_cmp_class):
   another device, the Mali-G52 by its Vulkan device name (issue 179's
   Mali-G52 MC2; or the tests' ICO_RD_VU_COMPACT_ARM=1), the PowerVR
   D-Series DXT-48-1536 (rd_vu_class.h rd__vu_class_measured: the compute
   record the PowerVR rule ships there) */
enum { ICO_ATT_CMP_OTHER = 0, ICO_ATT_CMP_G52 = 1, ICO_ATT_CMP_DXT = 2 };

/* the index compaction (rd_vu_compact.c, at the pre-pass's start and after
   the record's self-test): compute the record is written by compute
   dispatches with the pre-pass on (else notWhy, the record path that
   holds: not applicable), cls the device's class (ICO_ATT_CMP_*: another
   device is off by default, measured on the Mali-G52 only), env the tests'
   switch (ICO_RD_VU_COMPACT: -1 unset, 0, 1), defaultOn
   RD_VU_COMPACT_DEFAULT (the DXT's), armDefaultOn RD_VU_COMPACT_ARM_DEFAULT
   (the Mali-G52's), selftest the record's start-up self-test with it (-1
   not run, 0 refused it, 1 passed: a self-test that never ran keeps it
   off, but under the tests' switch).  1 when the compaction runs (the row
   on), else 0 */
int ico_attempt_decide_compaction(int compute, const char *notWhy, int cls, int env, int defaultOn,
                                  int armDefaultOn, int selftest);
/* pure, no row: the compaction is to be tried by the record's start-up
   self-test (the keep stage made, the fourth plane allocated): the
   decider's verdict were the self-test to pass */
int ico_attempt_compaction_candidate(int compute, int cls, int env, int defaultOn,
                                     int armDefaultOn);
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
/* the in-between pictures for the GPU's predicted end (rd_pace_gpu_live.c,
   the poster's thread at each change): asMade the window presents pictures
   as made (vsync off, uncapped, the render thread on: pace_rt_as_made),
   clock a picture's GPU span and first submit came back (the slot clock:
   Vulkan with timestamps), waited RD_PACE_GPU_NO_CLOCK measurements came
   back without one, on the model's moment is drawn (RD_PACE_GPU_DEFAULT,
   ICO_RD_PACE_GPU).  Not paced: not applicable; as made with the clock: off
   by default (the model computed and reported, not drawn) or on; as made,
   waited and no clock: not applicable (the backend has none); as made, not
   waited, no clock: the row left as it was */
void ico_attempt_decide_pace_gpu(int asMade, int clock, int waited, int on);
/* the own timer for pictures as made (rd_pace_rules.c, the poster's thread
   at each change): asMade as ico_attempt_decide_pace_gpu's, on the timer's
   moment is drawn, why the words of its decision (rd_as_made_timer.h
   rd_as_made_timer_decide: RD_AS_MADE_TIMER_DEFAULT on Android only,
   ICO_RD_AS_MADE_TIMER; NULL: fixed words).  Not paced: not applicable; as
   made: off by default (computed and reported, not drawn) or on */
void ico_attempt_decide_as_made_timer(int asMade, int on, const char *why);
/* the screen's refresh for pictures as made (rd_pace_screen_live.c, the
   poster's thread at each change): asMade as ico_attempt_decide_pace_gpu's,
   clock the GPU model's slot clock (rd_pace_gpu_clock_state: 1 yes, 0 none
   after its wait, -1 not known), times the display's own times (1 one came
   back, 0 none after 600 pictures as made, -1 not known), on the rule is
   drawn, why the words of its decision (rd_pace_screen.h
   rd_pace_screen_decide: RD_PACE_SCREEN_DEFAULT on Android only,
   ICO_RD_PACE_SCREEN over it; NULL: fixed words).  Not paced: not
   applicable; no slot clock: not applicable (no GPU end to predict from);
   no display times: not applicable (desktop, no VK_GOOGLE_display_timing);
   both: on, or off by default (computed and reported, not drawn: a
   non-Android build, the tests' switch); else the row left as it was */
void ico_attempt_decide_pace_screen(int asMade, int clock, int times, int on, const char *why);
/* the even cadence for pictures as made (rd_pace_even_live.c, the poster's
   thread at each change): asMade, clock and times as
   ico_attempt_decide_pace_screen's, on it is used (RD_PACE_EVEN_DEFAULT,
   ICO_RD_PACE_EVEN).  Not paced: not applicable; no slot clock or no
   display times: not applicable (the pictures as made); both: off by
   default (three phone rcs did not land pictures on their refresh) or on
   with the tests' switch; else the row left as it was */
void ico_attempt_decide_pace_even(int asMade, int clock, int times, int on);
/* the glow's paste by its own entry (rd_paste_spec.c, at start-up and after
   its self-test): module fx_paste_ps was made (Vulkan; 0 on the other
   backends), env the tests' switch (ICO_RD_PASTE_SPEC: -1 unset, 0, 1; its
   sabotage counts as 1), defaultOn RD_PASTE_SPEC_DEFAULT, selftest its
   start-up self-test (-1 not run, 0 failed, 1 passed).  1 when the paste
   may take it (each draw still passes its gate), else 0 */
int ico_attempt_decide_paste_spec(int module, int env, int defaultOn, int selftest);
/* the shadow volumes' stencil-only entry (rd_volume_fs.c, at start-up and
   after its self-test): module a Vulkan device with date_clear_ps's module
   (0 on the other backends), env the tests' switch (ICO_RD_VOLUME_FS: -1
   unset, 0, 1; its sabotage counts as 1), defaultOn RD_VOLUME_FS_DEFAULT,
   selftest its start-up self-test (-1 not run, 0 failed, 1 passed).  1 when
   the volumes may take it (each draw still passes its gate), else 0 */
int ico_attempt_decide_volume_fs(int module, int env, int defaultOn, int selftest);
/* the shadow composites' and levels' own entry (rd_comp_spec.c, at start-up
   and after its self-test): module sprite_comp_ps was made (Vulkan; 0 on the
   other backends), device the dual-source fixed-function blend (0: the
   two-pass blend or the exact blend keep sprite_ps), env the tests' switch
   (ICO_RD_COMP_SPEC: -1 unset, 0, 1; its sabotage counts as 1), defaultOn
   RD_COMP_SPEC_DEFAULT, selftest its start-up self-test (-1 not run, 0
   failed, 1 passed).  1 when the draws may take it (each still passes its
   gate), else 0 */
int ico_attempt_decide_comp_spec(int module, int device, int env, int defaultOn, int selftest);
/* the two-pass blend's pairs in one pass (rd_onepass.c, at start-up and
   after each self-test): twoPass the two-pass blend in use with a device,
   env the tests' switch (ICO_RD_ONEPASS: -1 unset, 0, 1; its sabotage
   counts as 1), defaultOn RD_ONEPASS_DEFAULT, selftest its start-up
   self-test (-1 not run, 0 refused, 1 passed); returns on */
int ico_attempt_decide_onepass(int twoPass, int env, int defaultOn, int selftest);
/* the old robust-access line (window_robust.c window_robust_start, the
   window's first start): found config.toml held [dev] gpu_robust = true
   without the game's mark, removed it was taken off (0: the file could not
   be written, robust access stays on and the next start tries again; -1:
   the line is in a hand-written form the strip cannot take out, nothing
   written, to be deleted by hand).  Found and removed: on; found, not
   removed: refused (by the file, or by its form); not found: not
   applicable */
void ico_attempt_decide_robust_line(int found, int removed);
/* no capture after a stall (rd_vu_skin_selftest.c at the skin check's start
   line): on the skin check runs on this device (an Adreno with a window,
   or the tests' switch); else not applicable */
void ico_attempt_decide_skin_skip(int on);
/* the picture-health report (port/render/rd_health_live.c at the first
   record): on where the records carry the slot clock's signs (Vulkan),
   counting; else not applicable */
void ico_attempt_decide_picture_health(int vulkan);
/* the pins let go at one picture a tick (window_clock_trap.c every block):
   android the build is Android's, rule the keeper's rule in force
   (render_thread.h ico_render_thread_pin_release_rule: 0 none, 1 three or
   more faster cores, 2 the few-cores rule).  On with rule 1; else not
   applicable with the reason */
void ico_attempt_decide_pin_release(int android, int rule);
/* the shadow count from the stencil (rd_shadow_count.c at the renderer's
   start): view the device reads the stencil through a view of its own (the
   count in one draw, RhiLimits.stencilView): on, the form timed at the
   first shadowed scene above 1x (rd_shadow_count.c sets it again there);
   else not applicable */
void ico_attempt_decide_shadow_count(int view);
/* the start-up set sized by the memory fall (window_pipe_finish.c, as the
   window registers it before the set): floorOn the free-memory floor
   applies (Android, the floor not set off): on; else not applicable */
void ico_attempt_decide_set_size(int floorOn);
/* the camera rule on the plain set (rd_pipeline.c rd__vu_policy_init, at
   each device start): plainRow the device keeps the VU plain set
   (g_rd.vuNoBeside), camera the camera rule in force ([dev] region_rule).
   On with both; not applicable with the reason otherwise */
void ico_attempt_decide_plain_camera(int plainRow, int camera);
/* the paused scene held under the menu (rd_hold.c at the first paused
   picture, and when the device's verdict changes): offWhy NULL, on; else not
   applicable with offWhy (a static string: not Vulkan, list 11's passes
   joined, MSAA, the exact blend, a temporal upscaler, ...) */
void ico_attempt_decide_scene_hold(const char *offWhy);

#endif
