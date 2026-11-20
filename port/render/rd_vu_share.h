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
 * What.  A second index list per mesh (RdMeshRec.indexShared, made at the
 * mesh's creation, rd__vu_share_build): the same triangles in the same
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
 *
 * The tests' switch ICO_RD_VU_SHARE=0 (docs/port/TESTING.md; no setting):
 * every draw keeps the original list. */
#ifndef RD_VU_SHARE_H
#define RD_VU_SHARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"

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
    RD_VU_SHARE_WHY_COUNT
};

/* The tests' switch's word: NULL, "" or "1" on, "0" or "off" off. */
bool rd_vu_share_word(const char *e);
/* ICO_RD_VU_SHARE read once a process */
bool rd__vu_share_on(void);

/* m's shared list and its per-batch counts of distinct values, made from
 * its index list and batches (any earlier ones freed); none (NULL) where
 * the list is not of the form every check below needs:
 *   - the batches in index order, back to back from 0 to indexCount, each
 *     a whole number of triangles, their vertex ranges in order and apart;
 *   - every triangle k * 4 + 0, + 1, + 2 with its three corners in its
 *     batch's vertices, no kick twice;
 *   - not a sheet (tiledFrom 0), fewer vertices than the kick limit;
 *   - at least one value shared (else nothing to gain). */
void rd__vu_share_build(RdMeshRec *m);
/* frees m's shared list */
void rd__vu_share_free(RdMeshRec *m);

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
 * not; -1 for a draw that is no candidate (not a mesh command through the
 * record under mode none), which is not counted.  *saved: the vertex
 * invocations its list saves (its values less its distinct ones). */
int rd__vu_share_draw(const RdMeshRec *m, uint8_t type, uint32_t rule, uint32_t chain,
                      uint32_t flags, bool recorded, uint32_t first, uint32_t last,
                      uint32_t firstIndex, uint32_t end, uint32_t *saved);

/* The counts (rd_replay_vu.c vuDraw): a draw through the record of tris
 * triangles, shared or not, the invocations its shared list saves; a
 * candidate kept on the original list by why. */
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

#endif
