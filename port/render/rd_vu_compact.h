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
 * Where: the compute record only (the vertex stores and the render
 * targets: not applicable, their record passes are draws, a compute pass
 * after them would be the compute record's work on a device that does not
 * take it).  By class: RD_VU_COMPACT_ARM_DEFAULT (the Mali-G52: Arm's
 * vendor id under Vulkan with RD_VU_CMP_G52_NAME in the device name, the
 * one Arm device measured, issue 179), RD_VU_COMPACT_DEFAULT (the PowerVR
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
   Mali-G52 only) */
#ifndef RD_VU_COMPACT_DEFAULT
#define RD_VU_COMPACT_DEFAULT 1
#endif
#ifndef RD_VU_COMPACT_ARM_DEFAULT
#define RD_VU_COMPACT_ARM_DEFAULT 1
#endif

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
   game drops, those past the box, those kept */
typedef struct RdVuCmpCounts {
    uint64_t drop[2], outside[2], kept[2];
    uint32_t pictures; /* the readbacks they came from */
} RdVuCmpCounts;

/* adds a picture's counts texels (n texels of four uints: x drop, y
   outside, z kept, w 1 a list 0 draw's) into acc, one picture more */
void rd_vu_cmp_counts_add(RdVuCmpCounts *acc, const uint32_t *texels, uint32_t n);
/* the triangles left out (drop and outside, both groups) of c */
uint64_t rd_vu_cmp_left_out(const RdVuCmpCounts *c);
/* the window's 10 s line (no newline):
 *   window: index compaction: triangles left out as drawing nothing, a
 *     picture: N of M in list 0 (P %), by class: the game's drops D,
 *     outside the target or the depth range O; the other lists N2 of M2
 *     (P2 %) (K pictures read back, two frame slots behind)
 * the length written (cut at cap - 1) */
size_t rd_vu_cmp_block_text(char *out, size_t cap, const RdVuCmpCounts *c);

/* ICO_RD_VU_COMPACT: -1 unset, 0, 1 (anything else -1, said once) */
int rd_vu_cmp_env(void);
/* pure: the device's class (attempt_ledger.h ICO_ATT_CMP_*): the Mali-G52
   where Arm's vendor id (not through MoltenVK) has RD_VU_CMP_G52_NAME in its
   device name (name NULL: unknown) or byTests (ICO_RD_VU_COMPACT_ARM=1), else
   the PowerVR D-Series DXT-48-1536 where dxt, else another device */
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
#endif

#endif /* PORT_RENDER_RD_VU_COMPACT_H */
