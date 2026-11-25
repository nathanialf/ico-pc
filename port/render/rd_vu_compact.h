/* rd_vu_compact.h: the index compaction (rd_vu_compact.c; docs/port/
 * PIPELINES.md "The index compaction"; issue 179).
 *
 * Of list 0's triangles a picture draws, 27-54 % on the Mali-G52's four
 * dumps and 38-82 % on the Pixel's opening dumps draw nothing
 * (build-host/tmp/v0116/design/CULLMEASURE.md): the game's own drops,
 * which vu_main_vs sends to VU_CULLED, and triangles wholly past the
 * target's edges or the depth range, which the GPU's clipping discards.
 * Each still cost vu_main_vs three invocations with their record loads.
 *
 * Under the compute record (rd_vu_record.h) a third record pass, pass K,
 * runs after pass T: one dispatch of vu_tri_keep_compute_cs (vu_tri.hlsl
 * vu_tri_keep_cs) a mesh draw doVuDraw will make (a command's one draw; a
 * code 36 command's cut and kick draws a batch, each with its own pass's
 * rule), one invocation a triangle of the draw's index list.  It decides
 * from the record words vu_main_vs reads, with vu_main_vs's statements,
 * whether the triangle draws nothing, and writes the list again, in its
 * order and length, into the record buffer's fourth plane: a kept triangle
 * as it was, one that draws nothing as its corner-1 value three times (a
 * filler whose position is that corner's again, past the same clip plane:
 * clipped, no fragment).  The draw then reads that list (the record buffer
 * bound as its index buffer): its count is known on the CPU (no indirect
 * draw, no read-back before the draw, no new wait), and a filler costs one
 * vertex invocation where the GPU reuses an index within a triangle (index
 * driven vertex shading on Mali), three where it does not (today's).
 *
 * The draw merge (rd_replay_vu.c vuRunTake): every planned draw's values
 * are raised by a raise fixed when the pre-pass plans it (the draws' ranges
 * one after the other in the plan's order, which is the walk's), so
 * consecutive planned draws whose lists lie one after the other in the
 * plane join a run as before, the run's table giving each its raise; the
 * merge builds subtract it (vu_common.hlsli vu_merge_entry).  Without the
 * merge's builds the values are not raised.
 *
 * Exact by construction: nothing the rasteriser's fill rule decides is
 * left out (no zero-area class, no back faces); the box is the bound
 * target's, two texels out, and a corner must be past it by 2^-10 of its
 * magnitude, so the compute's floats and the draw's may differ in their
 * last bits.  The start-up self-test (rd_vuprepass_selftest.c) runs the
 * record's test with the compaction and with an extra copy of a mesh past
 * the target's right edge; a failure runs it again without, and a pass
 * then refuses the compaction for the run ("refused by its self-test"),
 * today's index lists.  The lavapipe A/B (rd_vu_compact_ab) compares every
 * target with it on and off over tools/vu_flat0_dumps.txt.
 *
 * The Mali-G52's form (since v0.11.16; rd_vu_compact_selftest.c, issue
 * 179): v0.11.6 drew black there with the compaction on (from the first 3D
 * picture every picture's work stopped after list 0's start), the cause not
 * proven (build-host/tmp/v01116/handbacks/TRI-COMPACT.md ranks a driver
 * that takes a draw's vertex range from its list before pass K wrote it, a
 * read of a stale list, and the keep stage's one-lane stores widened and
 * lost).  On that class the CPU first writes every planned draw's whole
 * list, raised, into the fourth plane (the record buffer made in host
 * visible memory there: RHI_MEM_UPLOAD), and pass K writes only the
 * fillers (g_aux1.w bit 1).  A list read before pass K is then the whole
 * list, whose range covers every value pass K writes; a filler store that
 * is lost or lands late leaves its triangle as the CPU wrote it, which is
 * the picture without the compaction (each corner of a mixed triangle is
 * one of its own three, all past the same plane or culled: no fragment
 * either way).  Its cost: the lists' bytes written by the CPU every
 * picture (12 a triangle planned), counted in the window's line.  It is
 * tried by its own start-up test, run after the record's at the play's
 * record size (the record's test then runs without it): four draws of 8064
 * triangles a replay in six replays over both frame slots, the lists placed
 * at another offset every replay, each replay's lists and counts copied
 * back and checked against the CPU's lists (rd_vu_cmp_check), the picture
 * byte for byte against the same frames with it off, every stamp of the
 * test's pictures written.  A pass runs it for the run; anything else
 * leaves it off with the arm that failed, its mark (port/platform/
 * compact_mark.h, logs/compact-mark.txt: trying, counting, on, failed)
 * keeping it off on the install after a bad end while it was tried or on.
 *
 * Where: the compute record only (the vertex stores and the render
 * targets: not applicable, their record passes are draws, a compute pass
 * after them would be the compute record's work on a device that does not
 * take it).  By class: RD_VU_COMPACT_ARM_DEFAULT (the Mali-G52: Arm's
 * vendor id under Vulkan with RD_VU_CMP_G52_NAME in the device name, the
 * one Arm device measured, issue 179: 0 from v0.11.7 to v0.11.15, 1 since
 * v0.11.16 with the Mali-G52's form above), RD_VU_COMPACT_DEFAULT (the PowerVR
 * D-Series DXT-48-1536, whose compute record the PowerVR rule ships,
 * rd_vu_class.h); every other device on the compute record is off by
 * default ("measured on the Mali-G52 only").  Tried by the start-up
 * self-test, run only on its pass: a self-test that never ran keeps it off
 * (attempt_ledger.c ico_attempt_decide_compaction), the fourth plane given
 * up with any verdict of off.  The tests' switch ICO_RD_VU_COMPACT=0|1 (no
 * ini key; 1 runs it without a self-test).  Logged once at the record's start and after the self-test
 * ("rd: VU index compaction: ..."), counted in the attempt ledger's row
 * (triangles dropped a block) and in the window's 10 s line
 * ("window: index compaction: ..."), from the counts each workgroup of the
 * keep stage stores and a copy reads back when the frame slot comes round
 * again. */
#ifndef PORT_RENDER_RD_VU_COMPACT_H
#define PORT_RENDER_RD_VU_COMPACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the Mali-G52's Vulkan device name (a substring: "Mali-G52 MC2" in issue
   179's log), the one Arm device the compaction was measured on */
#define RD_VU_CMP_G52_NAME "Mali-G52"

/* the class decisions: on where the self-test passes (1) or off (0); every
   other device on the compute record is off by default (measured on the
   Mali-G52 only).  RD_VU_COMPACT_DEFAULT (1, the DXT's) and
   RD_VU_COMPACT_ARM_DEFAULT (1 since v0.11.16 with the Mali-G52's form,
   0 from v0.11.7: issue 179's black pictures) are the device table's values
   (port/rhi/rhi_device_table.h, its rows powervr-dxt-48-1536 and mali-g52;
   docs/port/DEVICE_TABLE.md), and rd_vu_compact.c reads them from the
   rows */
#include "rhi_device_table.h"

/* the fourth plane: the planned draws' lists from its start (uint values,
   in the plan's order), then from the next whole texel the counts, a texel
   a workgroup of 64 triangles (x drop, y outside, z kept, w 1 for a list 0
   draw), each stored by its workgroup (nothing to clear) */
#define RD_VU_CMP_GROUP 64u

/* a planned draw's pass rule (the shader's g_aux1.x: 0, ICO_VU_CUT_ONLY,
   ICO_VU_KICK_ONLY) */
enum { RD_VU_CMP_WHOLE = 0, RD_VU_CMP_CUT, RD_VU_CMP_KICK };

/* One planned draw: the range [first, first + count) of the command's
 * source list (doVuDraw's), its list's first value in the fourth plane
 * (at), its workgroups' first counts texel from the counts' base (group),
 * its source values' least and greatest, the raise added to each value,
 * its pass rule and whether it is list 0's */
typedef struct RdVuCmpCall {
    uint32_t first, count;
    uint32_t at, group;
    uint32_t lo, hi;
    uint32_t raise;
    uint8_t pass;
    uint8_t list0;
} RdVuCmpCall;

/* A pre-pass's plan: values placed (the next call's at), the counts'
 * texels (the next call's group), the next call's first raised value, the
 * plane's values, the device's largest index value, whether the values are
 * raised (the merge's builds), the calls planned and those that did not fit
 * (drawn from their own lists) */
typedef struct RdVuCmpPlan {
    uint32_t used, groups;
    uint64_t top;
    uint32_t cap;
    uint32_t maxValue;
    bool raise;
    uint32_t calls, refused;
} RdVuCmpPlan;

void rd_vu_cmp_plan_reset(RdVuCmpPlan *p, uint32_t cap, uint32_t maxValue, bool raise);
/* plans the draw of src[first .. first + count) (count a multiple of 3, at
 * least 3) with pass rule pass: its at, group, lo, hi and raise into *out;
 * false (nothing placed) when its list and counts do not fit the plane
 * (the lists, rounded up to a texel, then every planned workgroup's texel)
 * or the device's index values, or the list is empty */
bool rd_vu_cmp_plan_call(RdVuCmpPlan *p, const uint32_t *src, uint32_t first, uint32_t count,
                         uint8_t pass, uint8_t list0, RdVuCmpCall *out);
/* the counts' base: the texel after the plan's lists, from the plane's
   start */
uint32_t rd_vu_cmp_counts_base(const RdVuCmpPlan *p);

/* the counts: by group (0 list 0, 1 the other lists) the triangles the
   game drops, those past the box, those kept; the Mali-G52's form's lists
   written by the CPU (bytes and time over cpuPictures replays) */
typedef struct RdVuCmpCounts {
    uint64_t drop[2], outside[2], kept[2];
    uint32_t pictures; /* the readbacks they came from */
    uint64_t cpuBytes, cpuNs;
    uint32_t cpuPictures;
} RdVuCmpCounts;

/* the Mali-G52's form: one planned draw's list as the CPU writes it before
   pass K, dst[i] = src[i] + raise (mod 2^32, as the shader adds it) */
void rd_vu_cmp_fill(uint32_t *dst, const uint32_t *src, uint32_t count, uint32_t raise);

/* The start-up test's read-back check of one planned draw (its own test,
 * rd_vu_compact_selftest.c): got the list read back (count values), src
 * the draw's source values, raise its raise, texels its workgroups' counts
 * texels read back (x drop, y outside, z kept), expect what the test's
 * geometry says of every triangle (RD_VU_CMP_EXPECT_*).  A triangle reads
 * back as the CPU wrote it, as the filler (corner 1 three times), mixed
 * (each value its own or the filler's, not all one: filler stores lost),
 * or wrong (any other value: a stale or lost list).  Per workgroup the
 * fillers must be its left-out count (drop + outside): fewer are filler
 * writes missing (the mixed ones and those read back whole), more are
 * fillers it did not count; the counts must add up to its triangles */
enum { RD_VU_CMP_EXPECT_ANY = 0, RD_VU_CMP_EXPECT_KEPT, RD_VU_CMP_EXPECT_LEFT };

typedef struct RdVuCmpCheck {
    uint64_t tris, asWritten, fillers, mixed, wrong;
    uint64_t missing, extra; /* against the workgroups' left-out counts */
    uint64_t badCounts;      /* workgroups whose counts are not their triangles */
    uint64_t unexpected;     /* triangles not as the test's geometry says */
    uint64_t leftOut;        /* the workgroups' left-out counts summed */
    uint32_t first;          /* the first problem's triangle in its draw (UINT32_MAX none) */
} RdVuCmpCheck;

void rd_vu_cmp_check_reset(RdVuCmpCheck *r);
/* adds one draw's check into r; true when this draw had no problem */
bool rd_vu_cmp_check(RdVuCmpCheck *r, const uint32_t *got, const uint32_t *src, uint32_t count,
                     uint32_t raise, const uint32_t *texels, int expect);
/* r has no problem (nothing wrong, mixed, missing, extra, unexpected, no
   bad count) */
bool rd_vu_cmp_check_ok(const RdVuCmpCheck *r);
/* the check's words (no newline), the length written (cut at cap - 1):
 *   N triangles read back: A as the CPU wrote them, F fillers where the
 *   GPU counted L left out[; M mixed (a filler store lost in part); K
 *   filler writes missing (an unwritten filler leaves its triangle as it
 *   was); E fillers past the GPU's count; W triangles with a value neither
 *   the CPU's nor a filler (a stale or lost list); B workgroups whose
 *   counts are not their triangles; U triangles not as the test's geometry
 *   says] */
size_t rd_vu_cmp_check_text(char *out, size_t cap, const RdVuCmpCheck *r);

/* adds a picture's counts texels (n texels of four uints: x drop, y
   outside, z kept, w 1 a list 0 draw's) into acc, one picture more */
void rd_vu_cmp_counts_add(RdVuCmpCounts *acc, const uint32_t *texels, uint32_t n);
/* the triangles left out (drop and outside, both groups) of c */
uint64_t rd_vu_cmp_left_out(const RdVuCmpCounts *c);
/* the window's 10 s line (no newline):
 *   window: index compaction: triangles left out as drawing nothing, a
 *     picture: N of M in list 0 (P %), by class: the game's drops D,
 *     outside the target or the depth range O; the other lists N2 of M2
 *     (P2 %) (K pictures read back, two frame slots behind)[; the lists
 *     written by the CPU first (the Mali-G52's form): B KB and T ms a
 *     picture over C pictures]
 * the length written (cut at cap - 1) */
size_t rd_vu_cmp_block_text(char *out, size_t cap, const RdVuCmpCounts *c);

/* ICO_RD_VU_COMPACT: -1 unset, 0, 1 (anything else -1, said once) */
int rd_vu_cmp_env(void);
/* pure: the device's class (attempt_ledger.h ICO_ATT_CMP_*): the Mali-G52
   where the device table's row mali-g52 matches (Arm's vendor id, not
   through MoltenVK, RD_VU_CMP_G52_NAME in the device name; name NULL:
   unknown) or byTests (ICO_RD_VU_COMPACT_ARM=1), else the PowerVR D-Series
   DXT-48-1536 where dxt, else another device */
int rd_vu_cmp_class(const char *name, uint32_t vendor, bool moltenvk, bool dxt, bool byTests);

/* the block's counts, taken and reset (the window's 10 s line; the render
   thread adds at each readback) */
void rd_vu_compact_block(RdVuCmpCounts *out);
/* the counts of every copy recorded so far, after a full wait (the replay
   tool's --stats: one replay has no frame slot coming round); false when
   the compaction never ran */
bool rd_vu_compact_counts_now(RdVuCmpCounts *out);
/* the compaction's state words for the tool and the summary ("on",
   "off by default", "refused by its self-test", "not applicable here") */
const char *rd_vu_compact_state(void);

#ifdef PORT_RENDER_RD_INTERNAL_H
/* ---------------------------------------- the renderer's (rd_replay_vu.c) */
/* rd__vu_prepass_init's, after rd__vu_record_shaders: the class's decision
   (the ledger's row), the keep stage's pipeline, the readback buffers, the
   start line; whether the record carries the fourth plane is fixed here */
void rd__vu_compact_init(const RhiLimits *lim);
void rd__vu_compact_shutdown(void);
/* the record buffers carry the fourth plane (from rd__vu_compact_init where
   the compaction is to be tried; given up with a verdict of off) */
bool rd__vu_compact_plane(void);
/* this replay plans and draws the compaction: on, not refused, the compute
   record, the pre-pass on, the pipeline made, outside the loss bisect's
   window */
bool rd__vu_compact_active(void);
/* the keep stage's compute pipeline */
RhiPipeline rd__vu_compact_pipeline(void);
/* the tests' sabotage (ICO_RD_VU_COMPACT_SABOTAGE=1, read at init, said
   once): every triangle written as a filler (g_aux1.w), so the start-up
   self-test sees the pictures differ and refuses the compaction */
bool rd__vu_compact_sabotage(void);
/* the plan of this replay: reset with the plane's values after the
   counters and whether the values are raised; the calls planned, by index */
RdVuCmpPlan *rd__vu_compact_plan_begin(uint32_t cap, bool raise);
/* plans a draw (rd_vu_cmp_plan_call) and keeps it: its index, or
   UINT32_MAX when not planned */
uint32_t rd__vu_compact_add(const uint32_t *src, uint32_t first, uint32_t count, uint8_t pass,
                            uint8_t list0);
const RdVuCmpCall *rd__vu_compact_call(uint32_t i);
/* the call of the command's calls [c0, c0 + n) with pass rule pass over
   [first, first + count), NULL when none (the draw takes its own list) */
const RdVuCmpCall *rd__vu_compact_find(uint32_t c0, uint32_t n, uint8_t pass, uint32_t first,
                                       uint32_t count);
/* at the pre-pass's start for this frame slot: the counts the slot's last
   copy read back (its frame done: the slot came round), added to the
   block's and the run's, the ledger fired */
void rd__vu_compact_slot_begin(uint32_t slot);
/* after pass K: texels counts texels at byte off of rec copied into the
   slot's readback buffer (grown as needed) */
void rd__vu_compact_readback(RhiCommandList cl, uint32_t slot, RhiBuffer rec, uint64_t off,
                             uint32_t texels);
/* the triangles left out by the last readback (the picture two frame slots
   back; 0 before one): RdPerfRecord.vuTrisLeftOut */
uint32_t rd__vu_compact_last_left_out(void);
/* the self-test (rd_vuprepass_selftest.c): refused for the run (the
   record's test failed with it and passed without it), with the failure's
   words; done: the self-test's end (on: the pre-pass stays on), the row
   and the line */
void rd__vu_compact_refuse(const char *why);
/* the self-test returned before it ran (why: its words): the row off (but
   under the tests' switch), the fourth plane given up, the line */
void rd__vu_compact_selftest_not_run(const char *why);
void rd__vu_compact_selftest_done(bool on);
/* the self-test's lists placed from the fourth plane's middle (the
   record's test with it, rd_vuprepass_selftest.c: large first indices, as
   a busy picture's last draws have); the value they began at in the last
   plan (0 none) */
void rd__vu_compact_selftest_far(bool on);
uint32_t rd__vu_compact_far_at(void);

/* ------------------------------------------------ the Mali-G52's form */
/* the CPU writes the lists first and pass K only the fillers (the class's
   form while the fourth plane is carried; the record buffer is then made
   in host-visible memory, rd_replay_vu.c recEnsure) */
bool rd__vu_compact_cpu_lists(void);
/* the record buffers made in host-visible memory (RHI_MEM_UPLOAD): the
   form's, from its own test on (rd_replay_vu.c recEnsure) */
bool rd__vu_compact_host_record(void);
/* after the plan, before pass K: every planned draw's list written into
   the mapped record buffer rec at value head (the fourth plane's first);
   true when pass K may run (nothing to write in the other form), false
   when the buffer could not be mapped (every draw then reads its own list:
   the caller clears the plan's calls).  Counted for the window's line */
bool rd__vu_compact_lists_first(RhiBuffer rec, uint32_t head);
/* g_aux1.w of the keep stage: bit 0 the tests' sabotage, bit 1 the lists
   written by the CPU (only fillers stored) */
uint32_t rd__vu_compact_aux_flags(void);
/* after pass K and the counts' copy (vuCompactPass), inside the start-up
   test's replays alone: the lists from the plan's first value and the
   counts copied back for rd_vu_compact_selftest.c; head the fourth plane's
   first value, counts its counts' first texel, both from the record's
   start */
void rd__vu_compact_check_copy(RhiCommandList cl, RhiBuffer rec, uint32_t head, uint32_t counts);
/* rd_vuprepass_selftest.c, after the record's test passed with the
   compute record (t SCENE): the form's own start-up test
   (rd_vu_compact_selftest.c); nothing where the class has the other form,
   or the compaction is off before it */
void rd__vu_compact_own_selftest(RdTargetRec *t);
/* the pictures the compaction drew in play with every recorded stamp
   written (its mark's probation, rd__vu_compact_play_health) */
#define RD_VU_CMP_PROBATION 250u
/* the rate card's frames (rd_ratecard_run.c): held off (on) or not; true
   when it was in use before the hold (the card says so) */
bool rd__vu_compact_hold(bool on);
/* a collected record of a picture with VU draws, outside the self-tests
   (rd_health_live.c rd__health_note): hang its recorded stamps came back
   unwritten after its work was signalled finished (missing of them), the
   v0.11.6 sign.  With the Mali-G52's form on: a hang turns it off for the
   run at once and writes its mark failed; a picture without one counts
   for the probation */
void rd__vu_compact_play_health(bool hang, unsigned missing, uint32_t replay, uint32_t frame);
#endif

#endif /* PORT_RENDER_RD_VU_COMPACT_H */
