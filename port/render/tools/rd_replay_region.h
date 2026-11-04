/* rd_replay_region.h: rd_replay_tool's --region-stats, --region-png and
 * --region-out (rd_replay_region.c): the region rule's verdict on every
 * triangle of the frame's VU draws, through the CPU oracle (vu1_ref.h). */
#ifndef PORT_RENDER_TOOLS_RD_REPLAY_REGION_H
#define PORT_RENDER_TOOLS_RD_REPLAY_REGION_H

#include <stdbool.h>

#include "rd_internal.h"

typedef struct RdRegionOpts {
    const char *png;  /* --region-png: the dropped triangles painted (NULL: none) */
    const char *out;  /* --region-out: the --nop spec of the draws that draw nothing */
    const char *mesh; /* --mesh: only that mesh's draws get a line (NULL: every one) */
    /* --region-mask: where the PS2's rule and the camera rule draw
     * differently, painted (NULL: none) */
    const char *mask;
} RdRegionOpts;

/* Prints the lines for frame f (the frame the replay draws: with --interp
 * the blended one) under the replay's settings (rd__vu_extended); false
 * when the PNG or the spec could not be written. */
bool rd_replay_region(const char *dump, const RdFrame *f, const RdRegionOpts *o);

#endif
