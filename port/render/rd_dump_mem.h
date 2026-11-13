/*
 * port/render/rd_dump_mem.h
 *
 * The slow-scene dump's frame in two steps (rd_dump.c; issue 179).
 *
 * The automatic slow-scene dump cost the moto g24 power 47.1-56.0 ms on
 * the game thread for 8.6-10.3 MB of frame (its v0.11.5 log: "the render
 * thread's work in flight 0.0 ms of it, the frame's bytes 56.0 ms"): more
 * than one 40 ms tick.  The zip and the file writes were already the
 * worker's (port/platform/dump_auto.c); what the game thread paid was the
 * copy, and how it was made: a block grown by doubling from 1 MiB
 * (rd_dump_retained_frame_mem: 1, 2, 4, 8 then 16 MiB for a 10 MB frame;
 * a realloc that cannot grow in place moves the bytes so far, up to 15 MiB
 * in all, and every block's pages are touched for the first time on the
 * game thread: about 25 MB for a 10 MB frame).  The copy itself cannot
 * leave the game thread: the textures the frame names are written in
 * place by its next calls (rd_core.c rd_update_texture, the sub-rectangle
 * updates), the meshes and targets freed, and the ring reuses the frame's
 * own slot two frames on.
 *
 * So the game thread measures the frame first (rd_dump_retained_frame_bytes:
 * the same walk, no byte copied), the dump worker makes a block of that
 * size and touches its pages (dump_auto.h ico_dump_auto_prepare), and the
 * game thread copies the frame into it at a later picture
 * (rd_dump_retained_frame_into): one pass of exactly the dump's bytes, no
 * allocation of the block, nothing written to a file, nothing compressed.
 * The bytes are the same as rd_dump_retained_frame_mem's for the same
 * frame (rd_state_test).
 */
#ifndef ICO_RD_DUMP_MEM_H
#define ICO_RD_DUMP_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The bytes rd_dump_retained_frame_mem would make of the retained frame
   `number` (the same drain of the render thread and settle of the decode
   pool first; nothing copied): false (0) when the ring does not hold it
   or the frame is over the memory dump's 192 MiB bound (logged, as
   rd_dump_retained_frame_mem's) */
bool rd_dump_retained_frame_bytes(uint32_t number, size_t *bytes);

typedef enum RdDumpInto {
    RD_DUMP_INTO_OK = 0, /* *len bytes written at block */
    RD_DUMP_INTO_GONE,   /* the ring no longer holds the frame: nothing written */
    RD_DUMP_INTO_SMALL,  /* the frame needs more than cap bytes: the block's bytes are not a dump */
    RD_DUMP_INTO_FAILED  /* a part of the frame could not be read (its facing table) */
} RdDumpInto;

/* The retained frame `number` serialised into block (cap bytes, the
   caller's), as rd_dump_retained_frame_mem's bytes; the frame's number
   into *dumped (0 unless OK) */
RdDumpInto rd_dump_retained_frame_into(uint32_t number, void *block, size_t cap, size_t *len,
                                       uint32_t *dumped);

#endif /* ICO_RD_DUMP_MEM_H */
