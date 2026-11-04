/* gl_sync_wait.c: the OpenGL backend's fence wait with a time limit
 * (gl_sync_wait.h). */
#include "gl_sync_wait.h"

#include <stddef.h>

#define GLSW_SLOW_NS 2000000000ull

GlswResult gl_sync_wait_limited(const GlswOps *ops, uint64_t limitNs, uint64_t sliceNs,
                                uint64_t *heldNs, uint32_t *slices)
{
    const uint64_t t0 = ops->now(ops->user);
    uint64_t held = 0;
    uint32_t n = 0;
    bool said = false;
    GlswResult r;

    if (sliceNs == 0) {
        sliceNs = 1;
    }
    for (;;) {
        const GlswSlice s = ops->wait(ops->user, sliceNs);
        n++;
        held = ops->now(ops->user) - t0;
        if (s == GLSW_SLICE_SIGNALED) {
            r = GLSW_SIGNALED;
            break;
        }
        if (s == GLSW_SLICE_FAILED) {
            r = GLSW_FAILED;
            break;
        }
        if (!said && held >= GLSW_SLOW_NS) {
            said = true;
            if (ops->slow != NULL) {
                ops->slow(ops->user, held);
            }
        }
        if (limitNs != 0 && held >= limitNs) {
            r = GLSW_LIMIT;
            break;
        }
        if (ops->pump != NULL) {
            ops->pump(ops->user, held);
        }
    }
    if (heldNs != NULL) {
        *heldNs = held;
    }
    if (slices != NULL) {
        *slices = n;
    }
    return r;
}
