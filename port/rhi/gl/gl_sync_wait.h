/* gl_sync_wait.h: the OpenGL backend's fence wait with a time limit
 * (gl_device.c gl_wait_sync), apart from GL so a unit test drives it with a
 * stub that never signals (rhi_gl_sync_wait_test).
 *
 * The wait is cut into slices (glClientWaitSync's timeout): after each
 * slice that did not signal, the time held so far is checked against the
 * limit, a line is due once past 2 s, and the pump runs (the window takes
 * its events when the waiting thread is its own).  A limit of 0 waits as
 * long as it takes (the tests and tools, where no limit is set: the
 * rhi_gl suites under llvmpipe). */
#ifndef PORT_RHI_GL_GL_SYNC_WAIT_H
#define PORT_RHI_GL_GL_SYNC_WAIT_H

#include <stdbool.h>
#include <stdint.h>

/* what one slice's wait returned */
typedef enum GlswSlice {
    GLSW_SLICE_SIGNALED = 0, /* GL_ALREADY_SIGNALED, GL_CONDITION_SATISFIED */
    GLSW_SLICE_TIMEOUT = 1,  /* GL_TIMEOUT_EXPIRED */
    GLSW_SLICE_FAILED = 2    /* GL_WAIT_FAILED */
} GlswSlice;

/* how the whole wait ended */
typedef enum GlswResult {
    GLSW_SIGNALED = 0,
    GLSW_FAILED = 1,
    GLSW_LIMIT = 2 /* not signalled within the limit: the caller gives up */
} GlswResult;

typedef struct GlswOps {
    void *user;
    /* one wait of at most timeoutNs */
    GlswSlice (*wait)(void *user, uint64_t timeoutNs);
    /* a monotonic clock in ns */
    uint64_t (*now)(void *user);
    /* after a slice that timed out (may be NULL) */
    void (*pump)(void *user, uint64_t heldNs);
    /* once, when the wait has held 2 s (may be NULL) */
    void (*slow)(void *user, uint64_t heldNs);
} GlswOps;

/* Waits in slices of sliceNs until the fence signals, the wait fails, or
 * limitNs (0: none) has passed since the call; *heldNs (may be NULL) gets
 * the time held, *slices the number of slice waits. */
GlswResult gl_sync_wait_limited(const GlswOps *ops, uint64_t limitNs, uint64_t sliceNs,
                                uint64_t *heldNs, uint32_t *slices);

#endif /* PORT_RHI_GL_GL_SYNC_WAIT_H */
