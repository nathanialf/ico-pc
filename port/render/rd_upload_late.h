/* rd_upload_late.h: the uploads a replay records after the VU pre-pass
 * (rd_upload.c rd__upload_late; issue 179).
 *
 * A texture uploaded over a sampled one is moved from SHADER_READ to
 * COPY_DST first, a barrier whose first scope is the fragment shader: it
 * waits for every earlier fragment shader, the picture before's included,
 * and in Vulkan's first synchronisation scopes every later transfer command
 * then waits too (the barrier's second scope holds them).  Recorded before
 * the mesh uploads, as before v0.11.3, that wait passed into the mesh
 * copies and, through their own barriers (transfer to the vertex and
 * compute stages), into the VU record that reads the meshes: the record
 * could not run beside the picture before's fragment work.  So the replay
 * (rd_replay.c replayFrame) records the mesh uploads, the new targets'
 * clears and the VU pre-pass first (the dummy texture's one upload, once a
 * device, still first: it was never in SHADER_READ, so its barriers wait
 * for no fragment work), then the texture uploads and the fog's table.  Nothing the pre-pass
 * draws or dispatches reads a texture or the fog's table (its groups are
 * the frame's uniform block and the record's group, rd_replay_vu.c
 * vuStageDraw: no texture group), so the order changes no byte the GPU
 * writes; the lists that sample the textures and the table come after
 * both.
 *
 * The bytes those uploads take in the upload ring stay where they were,
 * before the base of the VU blocks' storage view (rd_replay_vu.c
 * s_ringBase, which a ring past the device's largest storage range needs):
 * the replay reserves them first (rd__upload_late_reserve: the bytes the
 * dirty textures take as the uploads lay them out, the changed rectangle of
 * one uploaded in place, so a replay that re-uploads a rectangle wastes
 * none) and the uploads take that window (rd__ring_window); an upload the
 * window cannot hold (a decode that landed in between) takes the ring's
 * end, as any allocation.
 *
 * What still holds the record: a barrier whose first scope reaches
 * fragment work and whose second holds the transfer stage before it, e.g.
 * the previous picture's last copy out of a render target (its barrier
 * waits for the colour output, and the closing barrier of its list orders
 * that copy before the next list's transfers): the mesh copies, and so the
 * record, then wait for the fragment work that copy read.  The hold lines
 * and counts (vk_cmd.c vkr_hold_note) name each such barrier. */
#ifndef PORT_RENDER_RD_UPLOAD_LATE_H
#define PORT_RENDER_RD_UPLOAD_LATE_H

#include <stdint.h>

#include "rd_internal.h" /* RdFrame */

/* the ring window of the late uploads, reserved now (before the mesh
 * uploads and the pre-pass): as many bytes as the dirty textures (laid
 * out as rd__upload_dirty_textures lays them), the dummy's upload and the
 * fog's table take; ~0 when the ring has no room (they then take the
 * ring's end); the dummy's one upload is recorded here, before them */
uint64_t rd__upload_late_reserve(void);
/* the texture uploads and the fog's table of frame f, into the window
 * rd__upload_late_reserve gave (win) */
void rd__upload_late(const RdFrame *f, int keep, uint64_t win);
/* the window rd__ring_alloc takes first from now on: [off, end), end 0
 * none; the window's next offset before the call */
uint64_t rd__ring_window(uint64_t off, uint64_t end);

#endif
