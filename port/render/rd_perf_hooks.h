/* rd_perf_hooks.h: the cost counters' increments (rd_perf_more.h; rd.h
 * RdPerfRecord noDual*, breakN, breakKB, listBox*, vuListTris), one call
 * at each site, nothing in a site but the call (docs/port/
 * ANDROID_DIAGNOSTICS.md, "The cost counters").  Render thread only (the
 * replay's), as g_rdPerf. */
#ifndef RD_PERF_HOOKS_H
#define RD_PERF_HOOKS_H

#include <stdbool.h>
#include <stdint.h>

/* rd_perf.c: the replay's record begun and ended, and each piece of GPU
   work as it begins (rd__perf_pass's kind, its target's code, its effect):
   SCENE's pass ended and begun again counted by why, with the bytes */
void rd__perf_hooks_begin(void);
void rd__perf_hooks_pass(int kind, int target, int post);
void rd__perf_hooks_end(void);

/* rd_fx_plan.c rd__sprite_above1x's head (every drawn pass a draw's
   DrawCB is filled for, fillDrawCB): the two-pass blend's passes of the 2D
   draws (flags: RdDrawPass.flags; fs its key's: the VU passes are counted
   by vuDraw instead, which may fill a pass twice when the merge refuses
   it) */
void rd__perf_nodual_pass(uint32_t flags, int fs);
/* rd_replay_vu.c vuDraw: a VU draw's planned passes (pair: the colour pass
   and the alpha pass after it; colour: the colour pass alone), its
   triangles (also counted by list: vuListTris) */
void rd__perf_vu_draw(bool pair, bool colour, uint32_t tris);

/* rd_vis.c cmdVerdict: a list's VU draw command the culler kept, with its
   box under its matrix (m NULL: no box) and the target's rectangle (GS
   pixels) */
void rd__perf_box_note(int list, const float (*m)[4], const float *lo, const float *hi,
                       bool scissor, double x0, double y0, double x1, double y1);

#endif
