/* rd_plain_camera.h: the camera rule on a device that keeps the VU plain set
 * (issue 123's right wall; docs/port/VU1_PROGRAMS.md "The two variants",
 * docs/port/DIVERGENCES.md VD-21 and VD-29).
 *
 * A device of the table's adreno-740-qualcomm row or Mesa's Turnip draws
 * every VU draw through the plain modules (rd_pipeline.c rd__vu_set_choose,
 * g_rd.vuNoBeside).  Until v0.11.7 every such draw kept the PS2's region
 * drops, because a skinned draw's clipped triangles showed a stray vertex as
 * a sliver on Turnip (issue 163): a bone's address or matrix read wrong
 * throws a vertex far off, and the PS2's drop hides the triangles it
 * belongs to.  A static mesh (RDC_MESH) or a grid (RDC_GRID) reads no bone
 * (its positions are its stream through the VU memory's matrix), so it
 * takes the camera rule there as on every other device: a dropped triangle
 * that faces the eye is drawn, clipped by the GPU, over the whole frame
 * (vu_common.hlsli vu_drop_fate).  The camera rule is a flag of the draw
 * (ICO_VU_CAMERA in VuCB's vu_draw.z), compiled into the plain modules
 * already: no module and no pipeline key is new.  The skinned draws
 * (RDC_SKINNED) keep the drops exactly as before; under [dev] region_rule =
 * ps2 every draw keeps them (no side bands on such a device).
 *
 * CPU only: the decision (rd_replay_vu.c vuDropMode), the counts the replay
 * makes (rd_replay_vu.c doVuDraw) and the 10 s line's words (rd_perf.c),
 * tested by rd_plain_camera_test. */
#ifndef PORT_RENDER_RD_PLAIN_CAMERA_H
#define PORT_RENDER_RD_PLAIN_CAMERA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the region rule's start line on such a device under the camera rule
 * (rd_pipeline.c rd__vu_policy_init, after "rd: ") */
#define RD_PLAIN_CAMERA_START                                                                      \
    "region rule: the camera decides for static meshes on this device's plain set; skinned "       \
    "draws keep the PS2's drops (issue 163)"

/* Whether a VU draw keeps the PS2's drops for the device's plain set:
 * noBeside the device keeps the plain set (g_rd.vuNoBeside), camera the
 * camera rule in force (rd__region_camera), skinned the draw is RDC_SKINNED.
 * True for a skinned draw on such a device, and for every draw on it under
 * region_rule = ps2; false off such a device (the other rules decide) */
bool rd__plain_keeps_drops(bool noBeside, bool camera, bool skinned);

/* One mesh, grid or skinned draw on such a device (the replay's
 * RdPerfRecord fields, rd.h vuPlainCamera, vuPlainKept): camera the draw
 * took the camera rule, skinned it is RDC_SKINNED.  A draw under the camera
 * rule adds to *cameraDraws, a skinned draw without it to *keptDraws (a
 * mesh under region_rule = ps2 adds to neither); the attempt ledger's row
 * counts the same (ICO_ATT_PLAIN_CAMERA) */
void rd__plain_camera_note(bool camera, bool skinned, uint32_t *cameraDraws, uint32_t *keptDraws);

/* The 10 s line's words for `replays` replays (rd_perf.c): "; plain set
 * (issue 163): 120 static and grid draws a replay by the camera rule, 14
 * skinned draws a replay keep the PS2's drops" (whole numbers, as the
 * camera rule's own words); nothing (0, buf "") when both counts are 0 or
 * replays is 0.  The length written (cut to fit n; 0 when n is 0) */
size_t rd__plain_camera_text(char *buf, size_t n, uint64_t cameraDraws, uint64_t keptDraws,
                             uint64_t replays);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RENDER_RD_PLAIN_CAMERA_H */
