/* rhi_traffic.h: the attachment memory each render pass asks a tile-based
 * GPU to move, counted on the CPU from the pass's description as the
 * renderer hands it to rhi_cmd_begin_render_pass (rhi_backend.c), and the
 * bytes of the texture copies (issue 179: the logs could not say what a
 * picture's 20 passes load and store).
 *
 * Counting only: no backend call, nothing recorded, nothing read from the
 * GPU.  The forwarders call it beside the backend's own entry points:
 * rhi_create_texture (rhi_traffic_texture_made: its format and samples
 * into a table by handle), rhi_destroy_texture (rhi_traffic_texture_gone),
 * rhi_cmd_begin_render_pass (rhi_traffic_pass), rhi_cmd_copy_texture and
 * rhi_cmd_copy_texture_to_buffer (rhi_traffic_copy); rhi_get_stats adds
 * the sums (RhiStats.passColorLoadBytes ... copyTextureBytes), which grow
 * for the whole run (a new device does not set them back) and are read
 * as differences, as every RhiStats count.
 *
 * What a pass asks, per attachment, over its render area (the whole size
 * without one) times its samples: a LOAD load reads the attachment into
 * tile memory, a STORE store writes it back, a CLEAR load fills the tile
 * memory without a read (counted apart: no memory traffic, the clear's
 * work only); a read-only depth is never stored.  Bytes a texel: the
 * format's (rhi_traffic_texel_bytes); a depth-stencil format's depth 4 and
 * stencil 1 (D24S8 backing is 4 together: the figure is then 1 byte high
 * a texel).  These are the bytes the passes ASK for, uncompressed: a
 * driver that compresses the attachments (Arm's AFBC, Qualcomm's UBWC)
 * moves fewer, and one that drops a store nothing reads moves none.  An
 * attachment the table does not know (the window's image, a handle made
 * before the table, a full table) counts as 4 bytes a texel, one sample,
 * and the pass is counted in passUntracked.
 *
 * Every thread may call it: the table is under a spin lock, the sums are
 * atomic.  Pure apart from that state: port/rhi/test/rhi_traffic_test.c. */
#ifndef ICO_RHI_TRAFFIC_H
#define ICO_RHI_TRAFFIC_H

#include <stdbool.h>
#include <stdint.h>

#include "rhi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An attachment's format and samples as the table knows them */
typedef struct RhiTrafficFmt {
    uint8_t format;  /* RhiFormat */
    uint8_t samples; /* 1, 2, 4, 8 */
    bool known;      /* false: not in the table (4 bytes a texel, one sample) */
} RhiTrafficFmt;

/* One pass's figures (rhi_traffic_pass_sum), bytes and texels */
typedef struct RhiTrafficPass {
    uint64_t colorLoad, colorStore, depthLoad, depthStore, clear;
    uint64_t pixels; /* the render area's texels (once, whatever the attachments) */
    bool untracked;  /* an attachment was not in the table */
} RhiTrafficPass;

/* The bytes a texel of format f's aspect: aspect RHI_ASPECT_COLOR the
   format's colour (RGBA8 4, R8 1, RGBA16F 8, ...; a depth format's depth),
   RHI_ASPECT_DEPTH the depth (4 for D32F and D32F_S8, else 0),
   RHI_ASPECT_STENCIL the stencil (1 for D32F_S8, else 0); 0 for a
   block-compressed format (never an attachment) */
uint32_t rhi_traffic_texel_bytes(RhiFormat f, RhiViewAspect aspect);

/* The pass's figures from its description and its attachments' formats
   (color[i] for pass->color[i], depth for pass->depth; pure) */
void rhi_traffic_pass_sum(const RhiRenderPassDesc *pass, const RhiTrafficFmt *color,
                          RhiTrafficFmt depth, RhiTrafficPass *out);

/* The forwarders' calls (rhi_backend.c) */
void rhi_traffic_texture_made(RhiTexture t, const RhiTextureDesc *desc);
void rhi_traffic_texture_gone(RhiTexture t);
void rhi_traffic_pass(const RhiRenderPassDesc *pass);
/* a copy out of src's region: its bytes read and written (2 x the texels
   times the aspect's bytes) */
void rhi_traffic_copy(RhiTexture src, RhiViewAspect aspect, RhiRect region);
/* the table emptied (rhi_shutdown: the handles belong to the device) */
void rhi_traffic_forget(void);
/* the sums into out's traffic fields (the rest of out untouched) */
void rhi_traffic_stats(RhiStats *out);
/* the table's lookup (for the test): false when t is not in it */
bool rhi_traffic_format_of(RhiTexture t, RhiTrafficFmt *out);

#ifdef __cplusplus
}
#endif

#endif /* ICO_RHI_TRAFFIC_H */
