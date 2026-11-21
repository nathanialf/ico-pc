/* rd_vu_share.h: shared vertices for clip-mode-none strips (issue 179;
 * docs/port/PIPELINES.md, "Shared vertices for clip mode none").
 *
 * Why.  Every VU draw through the record names a triangle's corner by the
 * index value kick * 4 + corner (vertex kick - 2 + corner): no value repeats,
 * so the GPU runs vu_main_vs three times a triangle.  On the issue 179
 * reporter's Mali-G52 the boat scene of the opening (stage 42) puts about
 * 39,000 small mesh triangles on the screen, four times stage 43's, at the
 * same visible fill; most of them are the dock's prelit code 34 meshes
 * (build-host/tmp/v01113/handbacks/SCENE179.md).
 *
 * What.  A second index list per mesh (RdMeshRec.indexShared, made once,
 * at the first upload of a replay that draws the mesh as a candidate:
 * rd__vu_share_consider, rd__vu_share_build): the same triangles in the same
 * order, each corner's value replaced by one canonical value of the same
 * vertex: (v + 2) * 4 + 0 where the triangle v + 2 is in the list (the
 * vertex's own provoking value), else the first triangle's of the list that
 * has the vertex as a corner.  Every value is one the original list holds
 * for that vertex, and a triangle k's first value stays k * 4 + 0, so its
 * provoking vertex (the flat outputs: the planes) is the same invocation as
 * before.  The GPU reuses an invocation for an equal value within a draw.
 *
 * Why the same bytes.  Under clip mode none (vu_rec.w's mode
 * ICO_VU_CLIP_NONE: the prelit program at code 34, rd_replay_vu.c
 * vuTriRule) what vu_main_vs writes at value k * 4 + c besides the flat
 * outputs depends on the vertex k - 2 + c alone: its position (the vertex's
 * own wrap test, vu_corner_position), colour, STQ, the side band's mark 0;
 * the triangle's verdict only culls it (hidden, or every corner past the
 * chain plane) or moves its depth (the later bias).  So a draw takes the
 * shared list only when none of those can happen: a mesh command (not
 * skinned, a grid or particles), drawn through the record by the main
 * shader, rule mode none, no chain plane (vu_batch.z 0, RDC_MESH b[5] 0), no
 * kick of its range carrying ICO_VU_MARK_HIDE or ICO_VU_MARK_LATER, not a
 * probe draw, not a sheet of the content table; the index compaction and a
 * draw that falls back to the program's own entry keep the original list.
 * Every other draw is unchanged: the same lists, the same GPU draws.
 *
 * The device copy and the ring hold the shared list right after the
 * original (rd__mesh_index_bytes, rd__mesh_write_indices): the original's
 * offset, which the compaction and everything else read, is unchanged.
 * The ring's estimate (rd_upload.c rd__estimate_ring) counts a mesh's
 * upload by rd__mesh_ring_bytes, which counts the shared list wherever the
 * upload that follows may write it (one already made, or one the replay's
 * consideration will make), so the estimate covers the writes by
 * construction.  The interpolation's scratch meshes (transient, rewritten
 * every present) never get one.
 *
 * The tests' switch ICO_RD_VU_SHARE=0 (docs/port/TESTING.md; no setting):
 * every draw keeps the original list. */
#ifndef RD_VU_SHARE_H
#define RD_VU_SHARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"
#include "rd_mesh.h"

/* shareUnique's top bit: the batch has a kick with ICO_VU_MARK_HIDE or
 * ICO_VU_MARK_LATER (decided once, at the build: both marks are made at
 * the mesh's creation, before it, and never change after), the rest the
 * batch's distinct values */
#define RD_VU_SHARE_MARKED 0x80000000u

/* why a candidate draw (a mesh command through the record under rule mode
 * none) kept the original list; RD_VU_SHARE_YES: it took the shared one */
enum {
    RD_VU_SHARE_YES = 0,
    RD_VU_SHARE_OFF,   /* ICO_RD_VU_SHARE=0 */
    RD_VU_SHARE_FORM,  /* the mesh has no shared list (rd__vu_share_build's form) */
    RD_VU_SHARE_CHAIN, /* a chain plane (vu_batch.z) */
    RD_VU_SHARE_MARKS, /* a kick of the range with the hide or later mark */
    RD_VU_SHARE_OTHER, /* a probe draw, a sheet, a range off the batches */
    RD_VU_SHARE_ENTRY, /* vuDraw: the compaction's list or the program's own entry */
    RD_VU_SHARE_CLIP,  /* a mesh draw under a rule mode other than none (region, scissor) */
    RD_VU_SHARE_NOREC, /* a mesh draw the pre-pass did not record (no verdict) */
    RD_VU_SHARE_WHY_COUNT
};

/* The tests' switch's word: NULL, "" or "1" on, "0" or "off" off. */
bool rd_vu_share_word(const char *e);
/* ICO_RD_VU_SHARE read once a process */
bool rd__vu_share_on(void);

/* m's shared list and its per-batch counts of distinct values and mark
 * flags, made from its index list and batches (any earlier ones freed);
 * none (NULL) where the list is not of the form every check below needs:
 *   - the prelit layout (RD_VU_QW_PRELIT: the program that alone draws in
 *     clip mode none; lit and skinned meshes never get one);
 *   - the batches in index order, back to back from 0 to indexCount, each
 *     a whole number of triangles, their vertex ranges in order and apart;
 *   - every triangle k * 4 + 0, + 1, + 2 with its three corners in its
 *     batch's vertices, no kick twice;
 *   - not a sheet (tiledFrom 0), fewer vertices than the kick limit;
 *   - at least one value shared (else nothing to gain). */
void rd__vu_share_build(RdMeshRec *m);
/* frees m's shared list */
void rd__vu_share_free(RdMeshRec *m);

/* Whether command c (its payload p) may draw its mesh with the shared
 * list: a mesh command of the prelit program at clip mode none (code 34)
 * without a chain plane.  A mesh never drawn by one never gets a list. */
bool rd__vu_share_candidate(const RdCmd *c, const RdVuPayload *p);
/* At a replay's mesh uploads, before any of them (rd_upload.c
 * rd__upload_meshes): m's list made once (shareTried) when c is a
 * candidate and m is not transient; a device copy made without it is then
 * marked for upload again (gpuDirty), so a bound copy always holds the
 * list a draw may take. */
void rd__vu_share_consider(RdMeshRec *m, const RdCmd *c, const RdVuPayload *p);
/* The ring bytes command c's mesh m may take at the replay's uploads
 * (rd__estimate_ring): its stream and mark bytes, its index lists as
 * rd__mesh_index_bytes will count them after rd__vu_share_consider (the
 * shared list's too where one is made or may be made now), each upload's
 * alignment (align, at least 16). */
uint64_t rd__mesh_ring_bytes(const RdMeshRec *m, const RdCmd *c, const RdVuPayload *p,
                             uint64_t align);
/* the times a build scanned a mesh's marks (the tests: once a mesh, never
 * a draw) */
uint64_t rd__vu_share_mark_scans(void);

/* The bytes m's index lists take in the device copy and the ring: the
 * original's, and the shared list's after it where m has one. */
uint64_t rd__mesh_index_bytes(const RdMeshRec *m);
/* writes m's lists there (rd__mesh_index_bytes bytes) */
void rd__mesh_write_indices(const RdMeshRec *m, uint8_t *dst);

/* Whether the draw of mesh m's batches first .. last - 1 (indices
 * firstIndex .. end - 1) may take the shared list: type its command's
 * (RDC_*), rule the draw's VuCB rule word (vu_rec.w), chain its chain
 * plane's code (vu_batch.z), flags its VuCB draw flags (vu_draw.z),
 * recorded true when the pre-pass recorded it.  RD_VU_SHARE_YES or why
 * not: a mesh command under another rule mode RD_VU_SHARE_CLIP, then one
 * the pre-pass did not record RD_VU_SHARE_NOREC (words for the log only:
 * which draws take the shared list is unchanged); -1 for a command that
 * is no mesh (skinned, a grid, particles), which is not counted.  *saved:
 * the vertex invocations its list saves (its values less its distinct
 * ones).  The draw's triangles ((end - firstIndex) / 3) are kept for the
 * rd__vu_share_kept that follows it on the same (the replay's) thread. */
int rd__vu_share_draw(const RdMeshRec *m, uint8_t type, uint32_t rule, uint32_t chain,
                      uint32_t flags, bool recorded, uint32_t first, uint32_t last,
                      uint32_t firstIndex, uint32_t end, uint32_t *saved);

/* The counts (rd_replay_vu.c vuDraw): a draw through the record of tris
 * triangles, shared or not, the invocations its shared list saves; a
 * candidate kept on the original list by why: the run's count of draws,
 * the block's of triangles (the last rd__vu_share_draw's: for
 * RD_VU_SHARE_ENTRY the draw that had been given the shared list).  A few
 * adds a draw. */
void rd__vu_share_note(uint32_t tris, bool shared, uint32_t saved);
void rd__vu_share_kept(int why);

/* The run's counts for rd_replay_tool --stats ("VU shared vertices ...",
 * after "stats: "); snprintf's count. */
int rd__vu_share_stats_text(char *out, size_t cap);
/* the run's counts (the unit test and the tool) */
void rd__vu_share_totals(uint64_t *sharedTris, uint64_t *saved, uint64_t *recordedTris,
                         uint64_t *sharedDraws);
/* back to zero (the unit test) */
void rd__vu_share_reset(void);
/* The block's kept triangles by why, a picture (n pictures), taken back to
 * zero: "; shared vertices kept (triangles a picture): form 12k, chain 0,
 * marks 3.1k, other 0, entry 0, region 20k, no verdict 0" (the switch's
 * own "off N" first when ICO_RD_VU_SHARE=0).  rd_vu_share_text appends it;
 * here for the unit test.  The length written. */
size_t rd__vu_share_kept_text(char *out, size_t cap, unsigned n);

#endif
