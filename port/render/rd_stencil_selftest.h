/* rd_stencil_selftest.h: the stencil loaded and stored inside the shadow
 * window only, on the fog's local path (rd_stencil_selftest.c;
 * docs/port/WORK_BUFFERS.md "The stencil outside the shadow window").
 *
 * Every target depth is D32F_S8.  The shadow count (bits 0-5) lives from its
 * reset to its resolve; DATE's bit 6 is made again in each pass that tests it
 * (rd_replay_tiler.c rd__replay_date_take clears it over its area first); bit
 * 7 is unused.  So outside the shadow window nothing reads what a pass loads
 * or stores of the stencil.  rd__stencil_ops_packed (rd_shadow.c) loads and
 * stores it with the depth on every pass where rd__stencil_cut_forced says
 * so: on every fog path but the one that samples the depth in place
 * (v0.4.5's Adreno 710 read, commit a54e2f896), except where the profile's
 * class is Imagination's (PowerVR), the fog's path is the local read and the
 * start-up self-test below passed.  No setting: ICO_RD_STENCIL_PACKED=1 (the
 * stencil with the depth everywhere) or =0 (inside the window only, whatever
 * the class and path; the self-test still runs and is logged) for the tests.
 */
#ifndef ICO_RD_STENCIL_SELFTEST_H
#define ICO_RD_STENCIL_SELFTEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rhi.h"

struct RdPerfRecord;

/* true: every pass on a depth loads and stores its stencil with the depth
 * (rd__stencil_ops_packed, the shadow reset's own pass); false: inside the
 * shadow window only (rd__stencil_ops) */
bool rd__stencil_cut_forced(void);

/* the fog's depth path changed (rd_fog_path.c rd__set_fog_path): the
 * decision again, a line when it changes after the start line */
void rd__stencil_cut_path(int fogPath);

/* The start-up self-test (rd_fog_path.c rd_fog_selftest, once a renderer,
 * after the fog's path is chosen and the other self-tests ran): run where
 * the class and the path would take the cut (or ICO_RD_STENCIL_PACKED=0),
 * nothing elsewhere; logs the start line either way.  False when it ran and
 * failed. */
bool rd__stencil_cut_selftest(void);
/* the tests: this renderer's self-test passed, and its words */
bool rd__stencil_cut_test_ok(void);
const char *rd__stencil_cut_test_words(void);

/* beginPass's hook (rd_replay.c), after the store plan, the pass area and
 * the shadow merge's arm decided the pass's ops (also the new targets'
 * clears, rd_upload.c, and the wrap accumulator's pass, rd_replay.c
 * doScreenWrap: every pass whose stencil ops rd__stencil_ops_packed
 * decides, so the A/B's traffic falls by exactly these): counts the stencil stores
 * and loads not asked while the depth's are, and under ICO_RD_STORE_POISON=1
 * clears a stencil load the cut left undefined to 0x5A (after the area and
 * the arm, so neither changes; lavapipe keeps DONT_CARE contents).  ms: the
 * pass draws into SCENE_MS (its stencil is never the window's) */
void rd__stencil_cut_pass(RhiRenderPassDesc *p, bool ms);

/* the 10 s summary (rd_perf.c): one record's counters summed, then the
 * clause "; stencil: ..." appended at *at */
void rd__stencil_cut_pop(const struct RdPerfRecord *rec);
void rd__stencil_cut_text(char *out, size_t size, size_t *at);
/* the replay tool's --stats line after "stats: " (one replay's record) */
void rd__stencil_cut_stats(const struct RdPerfRecord *rec, char *out, size_t size);

/* rd_profile.c: the Imagination (PowerVR) class of the device profile
 * (Vulkan, Imagination's vendor id, not through MoltenVK) */
bool rd__profile_powervr(void);

#endif
