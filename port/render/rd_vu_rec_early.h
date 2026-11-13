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
 * Within the bound the record is made at the run's need, not at its
 * largest (rd_early_plan.h rd_early_start_rows: 512 rows where the presents
 * are not interpolated and the velocity is off, else 1024), so a phone
 * under its free-memory floor does not hold rows no picture fills; a
 * picture that needs more grows it in play at the picture's start, as a
 * device whose bound allowed less always has (rd_replay_vu.c recEnsure;
 * counted).  ICO_RD_EARLY_ROWS=N (the tests: a power of two from 16 to
 * 1024) takes N as the need instead.
 * Without a call (the tool and the tests otherwise) play grows the record
 * as before. */
#ifndef PORT_RENDER_RD_VU_REC_EARLY_H
#define PORT_RENDER_RD_VU_REC_EARLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_early_plan.h" /* RdVuRecMem */

/* The free memory as the system says it (rd_early_plan.h RD_EARLY_MEM_*):
 * RD_EARLY_MEM_OK or _FLOOR with availMb and floorMb set, _SIGNAL, or
 * _UNKNOWN; why: the words for the line ("" none) */
typedef int (*RdEarlyMemoryFn)(long *availMb, long *floorMb, char *why, size_t size);

void rd_vu_record_prepare(RdEarlyMemoryFn memory);
/* what the last call made: the record's rows (0 none) and the chunk */
uint32_t rd_vu_record_early_rows(void);
bool rd_vu_record_early_arena(void);

/* The record's memory for the window's memory line (any thread: atomics
 * the replay sets once a replay with a pre-pass, rd__vu_rec_note): the
 * rows it held as the last replay began and their bytes over every frame
 * slot, the most rows a picture needed since the last call (taken: the
 * next call starts from 0) and in the run, the growths in play in the run
 * (past what start-up made), the rows made at start-up (0 none) and the
 * upload rings' bytes (every frame slot) */
void rd_vu_record_memory(RdVuRecMem *out); /* RdVuRecMem: rd_early_plan.h */
/* the loss dump's frame copies held now (rd_dump.c keepTake: at most 4 of
   at most 16 MiB, taken when a slot wait blocked 100 ms or more), bytes */
uint64_t rd_loss_keep_held_bytes(void);

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
/* the replay's pre-pass (rd_replay_vu.c vuPrepassRun): the rows its picture
 * needs (pass V's and pass T's), the rows held as it began, the upload
 * rings' bytes; and a growth in play */
void rd__vu_rec_note(uint32_t needRows, uint32_t heldRows, uint64_t heldBytes, uint64_t ringBytes);
void rd__vu_rec_grew(void);
bool rd__mesh_arena_ready(void);   /* the arena's first chunk exists */
bool rd__mesh_arena_prepare(void); /* the first chunk made now (RD_MESH_CHUNK) */
uint64_t rd__mesh_arena_chunk_bytes(void);

#endif
