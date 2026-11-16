/* rd_plain_camera.c: the camera rule for static meshes and grids on a device
 * that keeps the VU plain set (rd_plain_camera.h; issue 123's right wall). */
#include "rd_plain_camera.h"

#include <stdio.h>
#include "attempt_ledger.h"

bool rd__plain_keeps_drops(bool noBeside, bool camera, bool skinned)
{
    return noBeside && (!camera || skinned);
}

void rd__plain_camera_note(bool camera, bool skinned, uint32_t *cameraDraws, uint32_t *keptDraws)
{
    if (camera) {
        (*cameraDraws)++;
        ico_attempt_fire(ICO_ATT_PLAIN_CAMERA, 1);
    } else if (skinned) {
        (*keptDraws)++;
        ico_attempt_fire2(ICO_ATT_PLAIN_CAMERA, 1);
    }
}

size_t rd__plain_camera_text(char *buf, size_t n, uint64_t cameraDraws, uint64_t keptDraws,
                             uint64_t replays)
{
    if (n == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (replays == 0 || (cameraDraws == 0 && keptDraws == 0)) {
        return 0;
    }
    const int k =
        snprintf(buf, n,
                 "; plain set (issue 163): %.0f static and grid draws a replay by the "
                 "camera rule, %.0f skinned draws a replay keep the PS2's drops",
                 (double)cameraDraws / (double)replays, (double)keptDraws / (double)replays);
    if (k < 0) {
        buf[0] = '\0';
        return 0;
    }
    return (size_t)k < n ? (size_t)k : n - 1;
}
