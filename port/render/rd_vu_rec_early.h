/* rd_vu_rec_early.h: allocations of the first 3D pictures made at start-up
 * instead (docs/port/RENDER.md "Made at start-up"; the plan's rule:
 * rd_early_plan.h).
 *
 * The window calls rd_vu_record_prepare once the self-tests and the
 * start-up set are done and before the game runs (port/platform/
 * window_pipe_finish.c window_pipe_early, behind the start-up screen), on
 * the thread that draws then: the VU pre-pass's record at the size the
 * plan allows (play then makes no new record buffer until a picture needs
 * more), and the mesh arena's first chunk.  Neither changes a picture: the
 * record's slots are placed from texel 0 whatever its size (rd_replay_vu.c
 * rd__vu_prepass), and the arena's first chunk is the one the first mesh
 * would have made (rd_vu_rec_early_ab* compare the pictures byte for
 * byte).  One line says what was made, how long it took and the memory:
 *   "rd: start-up: made before play in 3.1 ms: the VU pre-pass's record at
 *    1024 rows (...), the mesh arena's first chunk (32 MB); 128 MB in all;
 *    the bound: ..."
 * A call without a memory function (the replay tool under
 * ICO_RD_EARLY_PREPARE=1, the tests) takes the record's own limit.
 * Without a call (the tool and the tests otherwise) play grows the record
 * as before. */
#ifndef PORT_RENDER_RD_VU_REC_EARLY_H
#define PORT_RENDER_RD_VU_REC_EARLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The free memory as the system says it (rd_early_plan.h RD_EARLY_MEM_*):
 * RD_EARLY_MEM_OK or _FLOOR with availMb and floorMb set, _SIGNAL, or
 * _UNKNOWN; why: the words for the line ("" none) */
typedef int (*RdEarlyMemoryFn)(long *availMb, long *floorMb, char *why, size_t size);

void rd_vu_record_prepare(RdEarlyMemoryFn memory);
/* what the last call made: the record's rows (0 none) and the chunk */
uint32_t rd_vu_record_early_rows(void);
bool rd_vu_record_early_arena(void);

/* ---- the renderer's own (rd_replay_vu.c, rd_mesh.c) ---- */
uint32_t rd__vu_rec_rows(void);     /* the record's rows now */
uint32_t rd__vu_rec_rows_min(void); /* RD_VU_REC_ROWS_MIN */
uint32_t rd__vu_rec_rows_max(void); /* RD_VU_REC_ROWS_MAX */
/* a row's bytes over every frame slot's buffer and the targets' textures */
uint64_t rd__vu_rec_row_bytes(void);
/* every frame slot's record (and the targets) at rows rows, its line saying
 * so; false: one could not be made, the record freed (play grows it as
 * before) */
bool rd__vu_rec_presize(uint32_t rows);
bool rd__mesh_arena_ready(void);   /* the arena's first chunk exists */
bool rd__mesh_arena_prepare(void); /* the first chunk made now (RD_MESH_CHUNK) */
uint64_t rd__mesh_arena_chunk_bytes(void);

#endif
