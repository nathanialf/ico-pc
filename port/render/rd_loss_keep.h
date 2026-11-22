/* rd_loss_keep.h: the memory copies of frames the GPU may not have finished
 * (rd_dump.c, "the lost frame"; rd_loss_pick.h says why and what they cost). */
#ifndef PORT_RENDER_RD_LOSS_KEEP_H
#define PORT_RENDER_RD_LOSS_KEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rd_loss_keep_block.h"

struct RdFrame;

/* The host that makes the copies' blocks (rd_loss_keep_block.h): the
 * window's, set once before the game thread starts (port/platform/
 * window_keep_block.c); host must stay valid for the run.  NULL (the
 * default): every copy measures its frame and allocates it once here. */
void rd_loss_keep_set_host(const RdLossKeepHost *host);

/* rd_begin_frame, the game thread, before the ring's oldest slot is reset:
 * f (that slot's closed frame, may be NULL) is copied to memory when the
 * backend still has a submission open at or after its replay; the copies the
 * GPU has moved past are freed.  Cheap when nothing is open (one backend
 * progress read, a pass-ring table scan). */
void rd__loss_keep_recycle(const struct RdFrame *f);
/* the copies freed, the host's block given back (rd_shutdown) */
void rd__loss_keep_shutdown(void);

/* Tests (rd_state_test): the copy the recycle makes of f, made now as a
 * stalled recycle makes it (the block's tick first, then the copy: into
 * the host's block when one is held, else measured and allocated once),
 * whatever the GPU's progress; a copy of f held before is dropped first.
 * bytes stay the renderer's until rd__loss_keep_shutdown.  False when no
 * copy was made. */
typedef struct RdLossKeepTestCopy {
    const void *bytes;
    size_t len, cap;
    bool intoBlock;
    uint64_t asks, made, used, small, released; /* RdKeepBlock's counts */
    uint64_t held;                              /* rd_loss_keep_held_bytes after it */
} RdLossKeepTestCopy;

bool rd__loss_keep_test_copy(const struct RdFrame *f, RdLossKeepTestCopy *out);
/* the block's tick alone, as a recycle runs it */
void rd__loss_keep_test_tick(void);

#endif /* PORT_RENDER_RD_LOSS_KEEP_H */
