/* rd_frame.c: the frame lifecycle pieces of wave 2 (package R2c).
 *
 *   rd_FrameHead / rd_FrameFlip   the flip's draw environment and clear
 *                                 (rd.h)
 *   rd_SetTargetZFormat           per-target GS Z to depth scale
 *   rd_SetVuCommon                gsb_MakeCommonMatrix's VU1 parameter block
 *   rd__FillCameraCB              RdCamera into FrameCB
 *
 * The head is recorded twice, at the head of list 0 and of list 11, because
 * whether the frame replays from list 0 or from list 11 (fbKeep) is known
 * only when it closes; rd_EndFrame turns the copy that is not the first
 * replayed list's into RDC_NOPs.  The colour and the half offset are patched
 * in place by rd_FrameFlip, so the commands keep their position at the head
 * of their list.
 */
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

/* ------------------------------------------------------------ frame head */

static void recordHead(RdFrame *f, int copy, int list, const RdFrameHead *h)
{
    rd_SelectList(list);
    f->headStart[copy] = f->lists[list].count;
    /* the draw environment: FRAME (FBMSK 0, which ends a mask an earlier
     * FRAME write left, as the flip's FRAME write does on the GS), ZBUF
     * (PSMZ32, ZMSK 0), XYOFFSET, SCISSOR, PRMODECONT 1 (nothing to do),
     * COLCLAMP 1, DTHE 0 (rd never dithers), TEST 0x50000
     * (sceGsSetDefDrawEnv with ztst 2) */
    rd_ColorMask(0);
    f->headTarget[copy] = f->lists[list].count;
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), h->gsW, h->gsH,
                 RD_TARGET_OFFSET | (h->halfY ? RD_TARGET_HALF_Y : 0));
    rd_ZWrite(1);
    rd_ColClamp(1);
    rd_TestGs(RD_TEST_Z_GEQUAL);
    f->headClear[copy] = ~0u;
    if (h->clear) {
        /* sceGsSetDefClear: TEST 0x30000, PRIM 6, RGBAQ, the sprite, TEST 0x50000 */
        rd_TestGs(RD_TEST_Z_ALWAYS);
        f->headClear[copy] = f->lists[list].count;
        rd_ClearTarget(rd_Target(RD_TARGET_SCENE), h->rgba, 1, h->z);
        rd_ABE(0);
        rd_TextureOff();
        rd_Gouraud(0);
        rd_TestGs(RD_TEST_Z_GEQUAL);
    }
    f->headEnd[copy] = f->lists[list].count;
}

void rd_FrameHead(const RdFrameHead *head)
{
    RdFrame *f = rd__RecFrame();
    if (!f || !head) {
        return;
    }
    const int cur = rd_CurrentList();
    recordHead(f, 0, 0, head);
    recordHead(f, 1, 11, head);
    rd_SelectList(cur);
    f->headValid = 1;
}

void rd_FrameFlip(const uint8_t rgba[4], int halfY)
{
    RdFrame *f = rd__RecFrame();
    if (!f || !f->headValid) {
        return;
    }
    static const int lists[2] = {0, 11};
    for (int i = 0; i < 2; i++) {
        RdCmdList *cl = &f->lists[lists[i]];
        if (f->headTarget[i] < cl->count && cl->cmds[f->headTarget[i]].type == RDC_TARGET) {
            cl->cmds[f->headTarget[i]].b[0] =
                (uint8_t)(RD_TARGET_OFFSET | (halfY ? RD_TARGET_HALF_Y : 0));
        }
        if (rgba && f->headClear[i] < cl->count && cl->cmds[f->headClear[i]].type == RDC_CLEAR) {
            memcpy(cl->cmds[f->headClear[i]].b, rgba, 4);
        }
    }
}

void rd__FrameHeadResolve(RdFrame *f, int keep)
{
    if (!f || !f->headValid) {
        return;
    }
    /* the copy in the list the replay does not start with */
    const int copy = keep ? 0 : 1;
    RdCmdList *cl = &f->lists[copy ? 11 : 0];
    for (uint32_t i = f->headStart[copy]; i < f->headEnd[copy] && i < cl->count; i++) {
        memset(&cl->cmds[i], 0, sizeof(RdCmd));
        cl->cmds[i].type = RDC_NOP;
    }
    f->headValid = 0;
}

/* --------------------------------------------------------------- Z scale */

void rd_SetTargetZFormat(RdTarget t, RdZFormat fmt)
{
    RdTargetRec *r = rd__TargetRec(t.id);
    if (r) {
        r->zFormat = (uint8_t)fmt;
    }
}

float rd__TargetZScale(uint32_t id)
{
    const RdTargetRec *r = rd__TargetRec(id);
    switch (r ? r->zFormat : RD_ZFMT_32) {
    case RD_ZFMT_24:
        return 1.0f / 16777216.0f;
    case RD_ZFMT_16:
        return 1.0f / 65536.0f;
    default:
        /* package QUEEN: PSMZ32 on a float depth buffer (D32S8) is z * 2^-33
           with the top values apart (gs_math.hlsli gs_z_to_depth); on any
           other (the Vulkan D24S8 fallback, rhi.h) z * 2^-32 */
        if (g_rd.hasDevice) {
            const char *ds = rhi_Limits()->depthStencilFormatName;
            if (!ds || strcmp(ds, "D32S8") != 0) {
                return 1.0f / 4294967296.0f;
            }
        }
        return 1.0f / 8589934592.0f;
    }
}

float rd_TargetZScale(RdTarget t)
{
    return rd__TargetZScale(t.id);
}

/* gs_math.hlsli gs_z_to_depth, the same expression */
float rd__GsDepth(uint32_t z, float scale)
{
    if (scale < 1.5e-10f) {
        if (z >= 0xFFFF0000u) {
            return (1.0f - 1.0f / 256.0f) + (float)(z - 0xFFFF0000u) * (1.0f / 16777216.0f);
        }
        return (float)z * scale;
    }
    const uint32_t zmax = scale < 1.0e-9f ? 0xFFFFFFFFu : (uint32_t)(1.0f / scale) - 1u;
    return (float)(z < zmax ? z : zmax) * scale;
}

/* ---------------------------------------------------------- VU block */

void rd_SetVuCommon(const RdVuCommon *block)
{
    RdFrame *f = rd__RecFrame();
    if (f && block) {
        f->vu = *block;
        f->hasVu = 1;
    }
    if (block) {
        /* wave 3 (R3ab): the packet is referenced from the current position
         * of all 13 lists, so every list's VU image takes it here */
        rd__VuLoadCommon(block);
    }
}

const RdVuCommon *rd_GetVuCommon(void)
{
    const RdFrame *f = rd__RecFrame();
    if (f && f->hasVu) {
        return &f->vu;
    }
    f = rd__LastFrame();
    return f && f->hasVu ? &f->vu : NULL;
}

/* ---------------------------------------------------------------- camera */

/* column-major: m[c * 4 + r] */
static void mul44(float *out, const float *a, const float *b)
{
    float t[16];
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    memcpy(out, t, sizeof(t));
}

/* The eye of a view matrix (world to view, affine): -M^-1 t with M the upper
 * 3x3, by cofactors (the game's view is a rotation, but nothing here relies
 * on it). */
static void eyeOf(const float *v, float eye[3])
{
#define M(r, c) ((double)v[(c) * 4 + (r)])
    const double a = M(0, 0), b = M(0, 1), c = M(0, 2);
    const double d = M(1, 0), e = M(1, 1), f = M(1, 2);
    const double g = M(2, 0), h = M(2, 1), i = M(2, 2);
    const double tx = M(0, 3), ty = M(1, 3), tz = M(2, 3);
#undef M
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    if (det == 0.0) {
        eye[0] = eye[1] = eye[2] = 0.0f;
        return;
    }
    const double inv[3][3] = {{A / det, -(b * i - c * h) / det, (b * f - c * e) / det},
                              {B / det, (a * i - c * g) / det, -(a * f - c * d) / det},
                              {C / det, -(a * h - b * g) / det, (a * e - b * d) / det}};
    for (int r = 0; r < 3; r++) {
        eye[r] = (float)-(inv[r][0] * tx + inv[r][1] * ty + inv[r][2] * tz);
    }
}

void rd__FillCameraCB(void *cbv, const RdCamera *cam)
{
    IcoFrameCB *cb = cbv;
    if (!cam) {
        memset(cb->view, 0, sizeof(cb->view));
        memset(cb->proj, 0, sizeof(cb->proj));
        memset(cb->viewProj, 0, sizeof(cb->viewProj));
        for (int i = 0; i < 4; i++) {
            cb->view[i * 5] = cb->proj[i * 5] = cb->viewProj[i * 5] = 1.0f;
        }
        memset(cb->cameraPos, 0, sizeof(cb->cameraPos));
        memset(cb->clip, 0, sizeof(cb->clip));
        return;
    }
    memcpy(cb->view, cam->view, sizeof(cb->view));
    memcpy(cb->proj, cam->proj43, sizeof(cb->proj));
    /* wave 7 (R7a), widescreen: the renderer's projection is proj43 with its
     * GS X compressed about the screen centre (2048) by (4/3) / aspect,
     * X' = f X + (1 - f) 2048 W: the visible screen then shows aspect /
     * (4/3) times as much horizontally (the cull side is GsBase.c
     * gsbHostWidenCull).  The mesh and screen-prim shaders apply the same
     * compression after their projection (g_space, rd_replay.c), so g_proj
     * and g_viewProj agree with what is drawn.  Original: f = 1, untouched. */
    const float f = g_rd.wideX > 0.0f ? g_rd.wideX : 1.0f;
    if (f != 1.0f) {
        for (int c = 0; c < 4; c++) {
            cb->proj[c * 4 + 0] =
                f * cam->proj43[c * 4 + 0] + (1.0f - f) * 2048.0f * cam->proj43[c * 4 + 3];
        }
    }
    mul44(cb->viewProj, cb->proj, cam->view);
    eyeOf(cam->view, cb->cameraPos);
    cb->cameraPos[3] = cam->cut ? 1.0f : 0.0f;
    cb->clip[0] = cam->nearZ;
    cb->clip[1] = cam->farZ;
    cb->clip[2] = cam->zoom;
    /* Original: 4:3; Enhanced: the aspect option (proj above).  The
     * gameplay matrices (matrixptr+0x80/+0xC0) never change. */
    cb->clip[3] = cam->aspect43 / f;
}
