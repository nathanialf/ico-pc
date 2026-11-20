/* rd_ledger_live.h: the renderer's hooks of the scene ledger (rd_ledger.h,
 * rd_ledger_live.c).  Every place that leaves a model's draw out names its
 * reason through one of these (tools/check_drop_ledger.py holds the drop
 * sites to it).  The game's thread: every hook but rd__ledger_cull and
 * rd__ledger_picture, which are the replay's (the render thread). */
#ifndef ICO_RD_LEDGER_LIVE_H
#define ICO_RD_LEDGER_LIVE_H

#include <stdint.h>

#include "rd_internal.h"
#include "rd_ledger.h"

/* rd_create_vu_mesh: a mesh built under name, or refused (RD_LEDGER_NO_MESH) */
void rd__ledger_built(const char *name);
void rd__ledger_build_failed(const char *name);
/* rd_draw_vu_mesh: a draw recorded, batches first .. first + n - 1 of m
   (its triangles counted from them) */
void rd__ledger_draw(const RdMeshRec *m, uint32_t meshId, uint32_t first, uint32_t n);
/* rd_draw_vu_mesh: a draw left out for reason (m NULL: no live record) */
void rd__ledger_draw_out(const RdMeshRec *m, uint32_t meshId, int reason);
/* a draw without a model name (a grid, particles, screen or world prims, a
   shadow volume: what "(a grid)") left out for reason */
void rd__ledger_unnamed_out(const char *what, int reason);
/* rd_tex.c: a texture of the game's name asked for (made from the game's
   data on a cache miss), made, failed (RdLedgerTexWhy) or replaced by a
   pack's picture */
void rd__ledger_tex_asked(const char *name);
void rd__ledger_tex_loaded(const char *name);
void rd__ledger_tex_failed(const char *name, int why);
void rd__ledger_tex_replaced(const char *name);
/* rd_note_cull_drop: the game's box cull left out model's object (part
   -1, rule 2), node (rule 2) or part (rule 3); views the cameras tested */
void rd__ledger_box(const char *model, int part, int rule, int views);
/* rd_vis.c: the replay of frame left c out (RD_VIS_* verdict v); and the
   picture's start (counted whatever the test's mode) */
void rd__ledger_cull(const RdCmd *c, int v, uint32_t frame);
void rd__ledger_picture(uint32_t frame);
/* rd_post / rd_shadow_tris: an effect recorded */
void rd__ledger_post(int kind, const RdPostParams *p);
void rd__ledger_shadow(uint32_t tris);
/* the stage in play so far to the log (a capture, rd_dump_on_demand_log) */
void rd__ledger_log_now(const char *when);

#endif
