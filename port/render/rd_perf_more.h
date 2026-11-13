/* rd_perf_more.h: the cost counters' block sums and the window's two lines
 * that rank pixel and vertex costs per device (issue 179;
 * docs/port/ANDROID_DIAGNOSTICS.md, "The cost counters" and "Comparing two
 * logs").
 *
 * The renderer counts per replay into its record (rd.h RdPerfRecord:
 * noDual*, breakN, breakKB, listBox*, vuListTris; rd_perf_hooks.c and the
 * one-line calls in rd_fx_plan.c, rd_replay_vu.c and rd_vis.c); the window
 * adds every record into a block (rd_perf_more_add, window_perf_rank.c)
 * and prints at each 10 s block ("window: " before each):
 *   costs a picture: two-pass blend 3.0 draws drawn twice (2.1k triangles'
 *     vertex work twice), 1.0 drawn once by the colour pass; SCENE's pass
 *     ended mid-picture 3.0 times (DATE 2.0, 9.2 MB; aura 1.0, 4.5 MB);
 *     DATE snapshots 3.0 (the first 1.0, an alpha write since 2.0), 0.0
 *     reused, 0.0 composites with an empty box; boxes against triangles:
 *     list 0 52.1k triangles in 1.85 Mpx of boxes (3.9 times its target's
 *     0.47 Mpx GS; 112 draws, 6 without a box), list 3 ...
 *   compare: stage 42 scene (24a4demo): 91 pictures (0 keep frames); a
 *     picture 52.1k triangles, 351 draws, 21.2 passes, 3.0 breaks; GPU ms
 *     a picture: span 109.9, before list 0 4.3, list 0 76.5, list 3 10.1,
 *     aura 5.5, reduction 4.3, glow 0.0, crack fill 0.0, present 2.9,
 *     pre-pass 6.2; vertex invocations a picture: the pre-pass's 85k, the
 *     draws' 156k (3 a triangle; never summed); glow plans: none 91; robust
 *     access on, buffers (gpu_robust)
 * The first is the costs a device's next change would cut; the second the
 * fields two logs of the same scene (v0.11.5 against v0.11.6, two devices)
 * are compared by.  Pure (port/render/test/rd_perf_more_test.c). */
#ifndef RD_PERF_MORE_H
#define RD_PERF_MORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd.h"

/* The break's reason from the first piece of GPU work after SCENE's pass
 * (rd.h RdPerfPass's kind, target code and effect: RD_BREAK_*) */
int rd_perf_break_of(int kind, int target, int post);
/* "DATE", "shadow", "aura", "reduction", "crack fill", "glow", "lamps",
 * "copies", "present", "again", "other" */
const char *rd_perf_break_word(int why);

/* The screen box of a VU draw: the box (lo, hi: x, y, z, w lanes) under
 * the clip position h = m[0] x + m[1] y + m[2] z + m[3] w (VU memory
 * 16..19, columns; w = 1, or for the scissor mode the box's w range: 16
 * corners), projected (x / w, y / w) and clipped to the rectangle x0..x1,
 * y0..y1 (GS pixels, rd_vis.h RdVisRect): its area in GS pixels; the whole
 * rectangle when a corner is at or behind the eye or not finite */
double rd_perf_box_area(const float (*m)[4], const float *lo, const float *hi, bool scissor,
                        double x0, double y0, double x1, double y1);

/* A block's sums (the window's; rd_perf_more_reset empties it) */
typedef struct RdPerfMore {
    uint32_t n, keep, gpuN;
    uint32_t prepassN; /* the records added with the pre-pass on */
    double tris, draws, passes;
    double noDualPairs, noDualColour, noDualTris;
    double breakN[RD_BREAK_COUNT], breakKB[RD_BREAK_COUNT];
    double dateSnaps, dateWhy[RD_DATE_RETAKE_COUNT], dateReused, boxEmpty;
    double listBoxPx[RD_LIST_COUNT], listBoxDraws[RD_LIST_COUNT], listBoxNone[RD_LIST_COUNT];
    double listTris[RD_LIST_COUNT];
    uint32_t listTargetPx[RD_LIST_COUNT]; /* the last seen */
    double preVerts, drawInv;
    /* GPU ms over the gpuN records with GPU times */
    double gpuMs, uploadMs, prepassMs, presentMs, listMs[RD_LIST_COUNT];
    double postMs[RD_PERF_POST_COUNT];
    uint32_t glowPlan[RD_GLOW_LIGHT_OFF + 1]; /* records by RdPerfRecord.glowLight */
} RdPerfMore;

void rd_perf_more_reset(RdPerfMore *b);
/* one record in; prepass: the VU pre-pass on (the draws then run
   vu_main_vs three times a triangle, else the programs' own nine) */
void rd_perf_more_add(RdPerfMore *b, const RdPerfRecord *r, bool prepass);
/* the draws' vertex shader invocations of a record: 3 a triangle with the
   pre-pass (vu_main_vs, no vertex reuse: index kick * 4 + corner), 9
   without (window_scene.h) */
uint64_t rd_perf_more_draw_invocations(uint64_t tris, bool prepass);

/* The two lines' texts (after "window: "); the length written, as
   snprintf's but cut at cap - 1.  The costs line is "" with no record;
   scene: the block's scene key (window_scene_key), tiler: a tile-based GPU
   ("before list 0" as the uploads' name), robust: robust access's words
   (rhi.h RhiLimits.robustText: "on, buffers (gpu_robust)"; NULL left out:
   issue 179's question whether a slow block ran with it) */
size_t rd_perf_more_cost_text(const RdPerfMore *b, char *out, size_t cap);
size_t rd_perf_more_compare_text(const RdPerfMore *b, const char *scene, bool tiler,
                                 const char *robust, char *out, size_t cap);

#endif
