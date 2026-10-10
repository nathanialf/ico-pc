/* rd_interp.c: presentation between simulation ticks.
 *
 * The game draws one frame per tick (25 Hz PAL, 30 Hz NTSC at the frame
 * step 2).  With RdSettings.interpolate on, in either preset, rd_end_frame
 * only closes the frame and the host calls rd_present(alpha) as often as
 * the display allows: the last closed frame (cur) is replayed with the data
 * of every keyed draw blended from its match in the frame before (prev),
 * weight alpha on cur.  At alpha 0 the picture is prev's, so presentation
 * runs one tick behind the simulation; it never feeds back into it (this
 * file only reads the retained frames, rd_core.c's RD_FRAME_RING).
 *
 * Structure comes from cur: its lists, state commands, textures, targets
 * and unkeyed draws are replayed as recorded.  A draw blends when it has a
 * key (RdKey, RD_KEY at the call sites; 0 = never) and prev has a draw of
 * the same type in the same list with the same key; the n-th occurrence of
 * a key in cur matches the n-th in prev (the "call ordinal" for packets
 * drawn more than once).  Objects the game draws many times under one key
 * (the swim ripples, the fog billboards) are paired by place first: a VU
 * draw of a key prev drew more than once pairs with prev's draw at the same
 * place in the world; when some of the key's draws stand still, the draws
 * left over pair nearest first, a pair that are not each other's nearest
 * being a new draw and one that went (the new one is cur's), and when none
 * does, the ordinal decides, or their order and nearness when the number
 * of draws changed (placeSlot).  No draw of cur stays unmatched while a
 * draw of prev with its key, type and list is free.  What blends, per
 * command:
 *
 *   RDC_MESH, RDC_SKINNED   the VU block (VuCB.mem): qw 2 (the UV scroll
 *                           SET_UVOFFSET left, with Texture.c's wrap of
 *                           uOfs/vOfs into (-1, 1] by 2 undone, and the
 *                           cluster fade alpha), qw 4..15 (the common
 *                           block's world to screen, viewport and inverse
 *                           view matrices), 16..27 (the model matrices),
 *                           28..35 (the light matrices: in a lit draw,
 *                           prev's three lights first take the slots of
 *                           cur's lights they are nearest in direction,
 *                           as the game orders them by strength,
 *                           pairLights; a lit program's L1 then turns with
 *                           the model, rotateLight); the bones; not qw 0,
 *                           1, 3 (constants and the GIF tag)
 *   RDC_GRID                the VU block and each vertex's position (and
 *                           normal when lit), and its ST when the grid
 *                           samples a target (the pool's surface: the STs
 *                           are screen positions through the tick's
 *                           camera); the strip headers, colours and an
 *                           image's STs are cur's
 *   RDC_PARTICLES           the VU block and each particle's position and
 *                           size; UV, grey and alpha are cur's
 *   RDC_SCREEN              XY, Z and colour of each vertex; STQ is cur's.
 *                           A prim the frame camera projected on the CPU
 *                           (RD_SCREEN_FRAME_CAMERA) blends its vertices'
 *                           world points and is seen through the blended
 *                           camera ("CPU-projected draws" below)
 *   RDC_SHADOW_STRIP        XY and Z of each vertex; Shadow.c's volumes
 *                           prism by prism (blendPrisms), in the world when
 *                           marked RD_SHADOW_FRAME_CAMERA
 *   RDC_OVERLAY_TEXT        an item's anchor, glow stretch and colour
 *                           (string, size and flags must be equal); an
 *                           op's colour and level (blendText)

 * The RDC_SCREEN and RDC_SHADOW_STRIP draws of one tick only fade in or out
 * with t, or switch at t = 0.5 (unmatchedPass); prev's are inserted into
 * the output.  So do the RDC_OVERLAY_TEXT items (their alpha), as the quads
 * they stand for.  A VU draw of prev only is drawn for the interval through
 * the blended camera when the camera left it behind: its object and part
 * not drawn in cur's list and its bounds outside cur's picture (prevAlone).
 * Matrices blend element by element, except a normal program's model
 * matrices and a skinned draw's bones, which blend as a slerped rotation
 * and a lerped stretch about a pivot (rotateModel, rotateBone): element by
 * element a turn of 77 degrees in a tick, which stage 3 has, would draw a
 * bone 22 % short half way.  A float pair with different bits that is not
 * both finite keeps cur's.  The camera is one rigid blend for the frame
 * (camSetup: the inverse views' rotation slerped, the eye lerped, the
 * projection lerped) and every VU draw through the frame's camera is
 * re-based on it, the draws that are cur's included (camRebase,
 * camCurDraw); otherwise an unmatched draw would stand a tick ahead of its
 * neighbours.
 *
 * A keyed draw snaps (is cur's) when prev has no match, when the payload's
 * shape differs (mesh, program, batch range, bone, vertex or particle
 * count, prim type; not a mesh's code and clip mode, nor a shadow volume's
 * topology, which blendPrisms or shiftShadow handles), or when it jumped: a
 * model's origin in the world (the model to screen translation through the
 * inverse of the frame's world to screen; a skinned draw's first bone)
 * moved more than RD_INTERP_JUMP_WORLD in the tick, or a screen prim's or
 * shadow vertex more than RD_INTERP_JUMP_SCREEN.  A particle whose position
 * moved more than four times its size, or whose alpha is 0 in either
 * frame, keeps cur's.
 *
 * The whole frame snaps (rd__interp_snap, RD_SNAP_*) without a previous
 * frame, across a discarded frame, on a keep frame, on a camera cut
 * (rd_camera_cut: the game's hard cuts and stage changes; RdCamera.cut), when
 * the camera turned more than RD_INTERP_CAMERA_TURN or moved more than
 * RD_INTERP_CAMERA_MOVE in the tick (a cut the hooks missed), at a fade
 * edge (either frame fully faded: what is behind a black screen may change
 * all at once), after the display options recreated the targets, and when
 * the scene size changed.
 *
 * Held at the tick (cur's, unkeyed or state): CLUT animation (textures are
 * updated in place), the rand-driven draws (the menu sparkle, lightning:
 * world prims and particles' screen packets with key 0), the dissolve FIX
 * (an ALPHA state command), film noise (an unkeyed post sprite), and the
 * feedback passes' inputs (feedback): the first present of a tick keeps
 * FEED128 (the aura's) in FEED_HELD and DISPLAY (the motion blur's old
 * frame) in DISPLAY_HELD, and the later presents start from them again, so
 * every present of a tick draws the same feedback and FEED128 and DISPLAY
 * advance once per tick as on the PS2.
 *
 * The motion blur (issue 28).  On the PS2 the sprite draws the previous
 * DISPLAY as Cs over the new SCENE with ALPHA (Cs - Cd) FIX / 128 + Cd
 * (staticBlur.c MotionBlur, gif_SetAlpha mode 2), once a tick, and the passes
 * after it in lists 8 to 12 (the aura's add, the softening, the brightness
 * step, fades, the UI, the reduction's tint) make the next DISPLAY of that:
 * D_n = P((1 - a) S_n + a D_n-1) with a = FIX / 128.  Every present replays
 * the whole frame, so a present that read the previous present's DISPLAY
 * would run that loop k times a tick (k presents a tick).  The retention
 * could be spread over the k presents (FIX' = 128 a^(1/k), a over the tick),
 * but P cannot: with its gain g (the tint over 128) and the light L it adds
 * (the aura, the brightness step, a fade to white) a still picture would
 * settle at (g (1 - a') S + L) / (1 - g a'), a' = FIX' / 128, instead of the
 * PS2's (g (1 - a) S + L) / (1 - g a).  FIX 32 on 30 Hz ticks at 300 presents
 * a second (FIX' 111) would keep 5.6 times the PS2's share of L, and a tint
 * of 148 or more (g a' >= 1) would run to white; a tint under 128 would go
 * dark the same way.  Instead every present of a tick draws the recorded FIX
 * over the DISPLAY the tick started from (DISPLAY_HELD, feedback), so the
 * loop runs once a tick at any present rate and the tick's picture is the
 * PS2's; between ticks the old frame stays the last tick's, as on the PS2.
 */
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"

/* ------------------------------------------------------------ the result */

static RdFrame s_out;

static void outFree(void)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        free(s_out.lists[l].cmds);
    }
    free(s_out.payload);
    memset(&s_out, 0, sizeof(s_out));
}

/* s_out = cur, its own copy of the lists and the payload; no temporary
 * targets of its own */
static bool copyFrame(const RdFrame *cur)
{
    RdCmdList lists[RD_LIST_COUNT];
    uint8_t *payload = s_out.payload;
    uint32_t payloadCap = s_out.payloadCap;
    memcpy(lists, s_out.lists, sizeof(lists));
    s_out = *cur;
    s_out.tempCount = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        RdCmdList *o = &lists[l];
        const RdCmdList *c = &cur->lists[l];
        if (c->count > o->cap) {
            RdCmd *p = realloc(o->cmds, (size_t)c->count * sizeof(RdCmd));
            if (!p) {
                memcpy(s_out.lists, lists, sizeof(lists));
                s_out.payload = payload;
                s_out.payloadCap = payloadCap;
                return false;
            }
            o->cmds = p;
            o->cap = c->count;
        }
        if (c->count) {
            memcpy(o->cmds, c->cmds, (size_t)c->count * sizeof(RdCmd));
        }
        o->count = c->count;
    }
    memcpy(s_out.lists, lists, sizeof(lists));
    if (cur->payloadSize > payloadCap) {
        uint8_t *p = realloc(payload, cur->payloadSize);
        if (!p) {
            s_out.payload = payload;
            s_out.payloadCap = payloadCap;
            s_out.payloadSize = 0;
            return false;
        }
        payload = p;
        payloadCap = cur->payloadSize;
    }
    if (cur->payloadSize) {
        memcpy(payload, cur->payload, cur->payloadSize);
    }
    s_out.payload = payload;
    s_out.payloadCap = payloadCap;
    s_out.payloadSize = cur->payloadSize;
    return true;
}

static void presentReset(void);
static void scratchReset(void);
static void flapFree(void);
static void prismFree(void);
static void umFree(void);

void rd__interp_shutdown(void)
{
    umFree();
    prismFree();
    flapFree();
    outFree();
    presentReset();
    scratchReset();
}

/* ---------------------------------------------------------------- blends */

static inline float lerpF(float p, float c, float t)
{
    /* exact at both ends: t = 0 gives p, t = 1 gives c */
    return (1.0f - t) * p + t * c;
}

static inline bool sameBits(float a, float b)
{
    uint32_t x, y;
    memcpy(&x, &a, 4);
    memcpy(&y, &b, 4);
    return x == y;
}

/* o[i] holds cur's value c[i]; blend it from p[i] */
static void lerpFloats(float *o, const float *p, const float *c, uint32_t n, float t)
{
    for (uint32_t i = 0; i < n; i++) {
        if (sameBits(p[i], c[i]) || !isfinite(p[i]) || !isfinite(c[i])) {
            continue;
        }
        o[i] = lerpF(p[i], c[i], t);
    }
}

/* Texture.c's linear UV scroll keeps uOfs/vOfs in (-1, 1] by adding or
 * taking 2 (tex_textureAnimation); a step of more than 1 is that wrap.  Not
 * for a sine scroll (RD_VU_SCROLL_SINE_*), which is never wrapped and can
 * step further: amplitude 0.8 at 7 Hz moves 1.23 in a tick, and unwrapping
 * it would put the texture half a repeat off at alpha 0.25 and 0.75. */
static float lerpWrap(float p, float c, float t)
{
    if (c - p > 1.0f) {
        p += 2.0f;
    } else if (p - c > 1.0f) {
        p -= 2.0f;
    }
    return lerpF(p, c, t);
}

static int32_t lerpI(int32_t p, int32_t c, float t)
{
    if (p == c) {
        return c;
    }
    const double v = (1.0 - (double)t) * p + (double)t * c;
    return (int32_t)floor(v + 0.5);
}

static uint32_t lerpU(uint32_t p, uint32_t c, float t)
{
    if (p == c) {
        return c;
    }
    const double v = (1.0 - (double)t) * p + (double)t * c;
    return (uint32_t)floor(v + 0.5);
}

static uint8_t lerpB(uint8_t p, uint8_t c, float t)
{
    return (uint8_t)lerpU(p, c, t);
}

/* VuCB.mem: qw 2 xy the UV scroll (a linear scroll's wrap undone; sine
 * axes, scroll's RD_VU_SCROLL_SINE_U / _V, straight), zw; qw 4..35
 * matrices and lights.  qw 0, 1, 3 are constants and a GIF tag: cur's. */
static void lerpVuBlock(float (*o)[4], const float (*p)[4], const float (*c)[4], float t,
                        uint8_t scroll)
{
    for (int k = 0; k < 2; k++) {
        if (!sameBits(p[2][k], c[2][k]) && isfinite(p[2][k]) && isfinite(c[2][k])) {
            o[2][k] = (scroll & (1u << k)) != 0u ? lerpF(p[2][k], c[2][k], t)
                                                 : lerpWrap(p[2][k], c[2][k], t);
        }
    }
    lerpFloats(&o[2][2], &p[2][2], &c[2][2], 2, t);
    lerpFloats(o[4], p[4], c[4], 32 * 4, t);
}

/* ----------------------------------------------------------- jump tests */

static float dist3(const float a[3], const float b[3])
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/* The inverse of a column-major 4 x 4 (cofactors), false when singular */
static bool invert4d(const double *a, double *o)
{
    double inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (!(fabs(det) > 1e-30) || !isfinite(det)) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        o[i] = inv[i] / det;
    }
    return true;
}

static bool invert4(const float *m, double *o)
{
    double a[16];
    for (int i = 0; i < 16; i++) {
        a[i] = m[i];
    }
    return invert4d(a, o);
}

/* ------------------------------------------------- rotation-aware blend */

/* o = a b, column-major 4 x 4 (o may not alias a or b) */
static void mul4d(const double *a, const double *b, double *o)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            double v = 0.0;
            for (int k = 0; k < 4; k++) {
                v += a[k * 4 + r] * b[c * 4 + k];
            }
            o[c * 4 + r] = v;
        }
    }
}

static double det3(const double m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* The rotation of the polar decomposition A = R S (Higham's iteration R <-
 * (R + R^-T) / 2), m[row][col]; false when singular or not converged */
static bool polarRotation(const double a[3][3], double r[3][3])
{
    memcpy(r, a, sizeof(double) * 9);
    for (int it = 0; it < 64; it++) {
        const double d = det3(r);
        if (!(fabs(d) > 1e-30) || !isfinite(d)) {
            return false;
        }
        double it_[3][3]; /* (R^-1)^T = cofactor matrix / det */
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                const int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
                it_[i][j] = (r[i1][j1] * r[i2][j2] - r[i1][j2] * r[i2][j1]) / d;
            }
        }
        double diff = 0.0;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                const double v = 0.5 * (r[i][j] + it_[i][j]);
                diff += fabs(v - r[i][j]);
                r[i][j] = v;
            }
        }
        if (diff < 1e-13) {
            return true;
        }
    }
    return false;
}

static void quatFromRot(const double m[3][3], double q[4])
{
    const double tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0) {
        const double s = sqrt(tr + 1.0) * 2.0;
        q[3] = 0.25 * s;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const double s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        q[3] = (m[2][1] - m[1][2]) / s;
        q[0] = 0.25 * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const double s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        q[3] = (m[0][2] - m[2][0]) / s;
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25 * s;
        q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        const double s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        q[3] = (m[1][0] - m[0][1]) / s;
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25 * s;
    }
}

static void rotFromQuat(const double q[4], double m[3][3])
{
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    m[0][0] = 1.0 - 2.0 * (y * y + z * z);
    m[0][1] = 2.0 * (x * y - z * w);
    m[0][2] = 2.0 * (x * z + y * w);
    m[1][0] = 2.0 * (x * y + z * w);
    m[1][1] = 1.0 - 2.0 * (x * x + z * z);
    m[1][2] = 2.0 * (y * z - x * w);
    m[2][0] = 2.0 * (x * z - y * w);
    m[2][1] = 2.0 * (y * z + x * w);
    m[2][2] = 1.0 - 2.0 * (x * x + y * y);
}

static void slerp(const double a[4], const double b0[4], double t, double o[4])
{
    double b[4] = {b0[0], b0[1], b0[2], b0[3]};
    double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0) { /* the short way */
        d = -d;
        for (int i = 0; i < 4; i++) {
            b[i] = -b[i];
        }
    }
    double wa = 1.0 - t, wb = t;
    if (d < 0.9999) {
        const double th = acos(d), s = sin(th);
        wa = sin((1.0 - t) * th) / s;
        wb = sin(t * th) / s;
    }
    double n = 0.0;
    for (int i = 0; i < 4; i++) {
        o[i] = wa * a[i] + wb * b[i];
        n += o[i] * o[i];
    }
    n = sqrt(n);
    for (int i = 0; i < 4; i++) {
        o[i] /= n;
    }
}

static bool isAffine(const double *m)
{
    const double s = fabs(m[0]) + fabs(m[5]) + fabs(m[10]) + 1.0;
    return fabs(m[3]) < 1e-5 * s && fabs(m[7]) < 1e-5 * s && fabs(m[11]) < 1e-5 * s &&
           fabs(m[15] - 1.0) < 1e-5;
}

/* the turn between the two rotations of the last rd__blend_affine, degrees */
static double s_turnDeg;

bool rd__blend_affine(const double *p, const double *c, double t, const double *pivot, double *o)
{
    s_turnDeg = 0.0;
    if (!isAffine(p) || !isAffine(c)) {
        return false;
    }
    double ap[3][3], ac[3][3], rp[3][3], rc[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            ap[i][j] = p[j * 4 + i];
            ac[i][j] = c[j * 4 + i];
        }
    }
    if (!polarRotation(ap, rp) || !polarRotation(ac, rc)) {
        return false;
    }
    const double dp = det3(rp), dc = det3(rc);
    if ((dp < 0.0) != (dc < 0.0)) {
        return false; /* a mirror between the two: no rotation joins them */
    }
    const double sign = dp < 0.0 ? -1.0 : 1.0; /* A = (sign R) (sign S) */
    double sp[3][3], sc[3][3], qp[4], qc[4], qt[4], rt[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            rp[i][j] *= sign;
            rc[i][j] *= sign;
        }
    }
    for (int i = 0; i < 3; i++) { /* S = R^T A (with the sign folded in) */
        for (int j = 0; j < 3; j++) {
            double vp = 0.0, vc = 0.0;
            for (int k = 0; k < 3; k++) {
                vp += rp[k][i] * ap[k][j];
                vc += rc[k][i] * ac[k][j];
            }
            sp[i][j] = vp;
            sc[i][j] = vc;
        }
    }
    quatFromRot(rp, qp);
    quatFromRot(rc, qc);
    {
        const double d = fabs(qp[0] * qc[0] + qp[1] * qc[1] + qp[2] * qc[2] + qp[3] * qc[3]);
        s_turnDeg = 2.0 * acos(d > 1.0 ? 1.0 : d) * 180.0 / 3.14159265358979323846;
    }
    slerp(qp, qc, t, qt);
    rotFromQuat(qt, rt);
    double out[16];
    const double x0[3] = {pivot ? pivot[0] : 0.0, pivot ? pivot[1] : 0.0, pivot ? pivot[2] : 0.0};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double v = 0.0;
            for (int k = 0; k < 3; k++) {
                v += rt[i][k] * ((1.0 - t) * sp[k][j] + t * sc[k][j]);
            }
            out[j * 4 + i] = v;
        }
    }
    /* the pivot's image follows the straight line between its two ticks'
     * images; the blended 3 x 3 turns the rest about it */
    for (int i = 0; i < 3; i++) {
        double yp = p[12 + i], yc = c[12 + i], ax = 0.0;
        for (int j = 0; j < 3; j++) {
            yp += p[j * 4 + i] * x0[j];
            yc += c[j * 4 + i] * x0[j];
            ax += out[j * 4 + i] * x0[j];
        }
        out[12 + i] = (1.0 - t) * yp + t * yc - ax;
    }
    out[3] = out[7] = out[11] = 0.0;
    out[15] = 1.0;
    for (int i = 0; i < 16; i++) {
        if (!isfinite(out[i])) {
            return false;
        }
    }
    memcpy(o, out, sizeof(out));
    return true;
}

bool rd__s2_legacy(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("ICO_RD_S2_LEGACY");
        v = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    return v != 0;
}

bool rd__vu_off_grid(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("ICO_RD_VU_OFFGRID");
        v = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    return v != 0;
}

static void loadQw4(const float (*m)[4], int at, double *o)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            o[c * 4 + r] = m[at + c][r];
        }
    }
}

static bool sameQw4(const float (*a)[4], const float (*b)[4], int at)
{
    return memcmp(a[at], b[at], 4 * 4 * sizeof(float)) == 0;
}

/* The pivots the rotations turn about.  A skinned draw's bone is the
 * node times the cluster (bind) inverse, so its translation is not the
 * joint: blended about it, a turning limb would leave its joint.  Each
 * bone's pivot is the weighted centroid of the mesh vertices bound to it
 * (the stream's weight qword: VU address bone * 4 + 16 and weight, twice);
 * a rigid mesh's is its vertices' centroid.  Both in the mesh's space, so
 * the part's middle follows the straight line between its two ticks, as an
 * element-wise blend has it, and the turn is blended about it. */
#define RD_INTERP_BONES 60

static double s_pivot[RD_INTERP_BONES][4]; /* x, y, z, weight */

static uint32_t s_pivotMesh;

static void skinPivots(uint32_t meshId)
{
    if (meshId == s_pivotMesh) {
        return;
    }
    memset(s_pivot, 0, sizeof(s_pivot));
    s_pivotMesh = meshId;
    const RdMeshRec *m = rd__mesh_rec(meshId);
    if (!m || !m->vu || m->qwPerVertex != RD_VU_QW_SKIN || !m->stream) {
        return;
    }
    for (uint32_t v = 0; v < m->vertexCount; v++) {
        const float *pos = m->stream[(size_t)v * RD_VU_QW_SKIN];
        const float *wt = m->stream[(size_t)v * RD_VU_QW_SKIN + 2];
        for (int k = 0; k < 2; k++) {
            uint32_t addr;
            memcpy(&addr, &wt[k * 2], 4);
            const float w = wt[k * 2 + 1];
            if (addr < 16u || ((addr - 16u) & 3u) != 0u || (addr - 16u) / 4u >= RD_INTERP_BONES ||
                !(w > 0.0f) || !isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2])) {
                continue;
            }
            double *pv = s_pivot[(addr - 16u) / 4u];
            pv[0] += w * pos[0];
            pv[1] += w * pos[1];
            pv[2] += w * pos[2];
            pv[3] += w;
        }
    }
    for (int b = 0; b < RD_INTERP_BONES; b++) {
        if (s_pivot[b][3] > 0.0) {
            for (int k = 0; k < 3; k++) {
                s_pivot[b][k] /= s_pivot[b][3];
            }
        }
    }
}

/* a rigid mesh's vertex centroid; false without vertices */
static bool meshCentroid(uint32_t meshId, double out[3])
{
    const RdMeshRec *m = rd__mesh_rec(meshId);
    if (!m || !m->vu || !m->stream || m->vertexCount == 0 || m->qwPerVertex == 0) {
        return false;
    }
    double s[3] = {0.0, 0.0, 0.0};
    uint32_t n = 0;
    for (uint32_t v = 0; v < m->vertexCount; v++) {
        const float *pos = m->stream[(size_t)v * m->qwPerVertex];
        if (isfinite(pos[0]) && isfinite(pos[1]) && isfinite(pos[2])) {
            s[0] += pos[0];
            s[1] += pos[1];
            s[2] += pos[2];
            n++;
        }
    }
    if (n == 0) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        out[k] = s[k] / n;
    }
    return true;
}

/* A bone (4 qwords, affine) blended as a rotation; false: left to the
 * element-wise blend */
static bool rotateBone(float (*o)[4], const float (*p)[4], const float (*c)[4], float t,
                       const double *pivot)
{
    double mp[16], mc[16], mo[16];
    loadQw4(p, 0, mp);
    loadQw4(c, 0, mc);
    if (!rd__blend_affine(mp, mc, t, pivot, mo)) {
        return false;
    }
    if (s_turnDeg > RD_INTERP_TURN_SNAP) {
        memcpy(o, c, 4 * sizeof(o[0])); /* a flip, not a motion: the tick's */
        return true;
    }
    for (int k = 0; k < 4; k++) {
        for (int r = 0; r < 4; r++) {
            o[k][r] = (float)mo[k * 4 + r];
        }
    }
    return true;
}

/* A lit program's light matrix L1 (qw 28..31) follows the object's turn.
 * normal_l computes l = max0(L1 n) from the model-space normal n (n.w the
 * ambient weight) and c = max0(L2 l): L1's rows 0..2 are the three lights'
 * directions in model space, row 3 (0, 0, 0, 1) carries n.w into l.w; L2
 * (qw 32..35) holds the lights' colours in columns 0..2 and the ambient
 * colour in column 3.
 * RegistPacket.c builds L1 = Ln N: Light.c's normal light matrix
 * (_MakeNormalLightMatrix: the negated unit directions as rows, an unused
 * light a zero row) times the node's 3 x 3 with its scale, the translation
 * cleared, which is W's 3 x 3.  Element by element a turn of the object
 * shortens the rows by cos(theta / 2) half way, so the lighting dims (29 %
 * at 90 degrees a tick).  Here the world-space part Ln = L1 W^-1 blends
 * element-wise (the lights change between ticks only as the near lights or
 * their strengths do) and the result is Ln(t) W(t), W(t) the model blend
 * rotateModel made; row 3, column 3 and L2 (colours, ambient) keep the
 * element-wise blend.  A zero row (no light) stays zero.  False: L1 keeps
 * the element-wise blend. */
static int s_lightRot; /* draws whose L1 blended with the model's turn, this present */

static bool rotateLight(float (*o)[4], const float (*p)[4], const float (*c)[4], float t,
                        const double *iwp, const double *iwc, const double *wt)
{
    if (sameQw4(p, c, 28)) {
        return false; /* the lighting in the model's space did not change */
    }
    double lp[3][3], lc[3][3], res[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            /* Ln = L1 W^-1: row i, column j (W affine: W^-1's 3 x 3 inverts W's) */
            double a = 0.0, b = 0.0;
            for (int k = 0; k < 3; k++) {
                a += (double)p[28 + k][i] * iwp[j * 4 + k];
                b += (double)c[28 + k][i] * iwc[j * 4 + k];
            }
            lp[i][j] = a;
            lc[i][j] = b;
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double v = 0.0;
            for (int k = 0; k < 3; k++) {
                v += ((1.0 - t) * lp[i][k] + t * lc[i][k]) * wt[j * 4 + k];
            }
            if (!isfinite(v) || fabs(v) > 3.0e38) {
                return false;
            }
            res[i][j] = v;
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            o[28 + j][i] = (float)res[i][j];
        }
    }
    return true;
}

/* The near lights' slots.  Light.c's light_getNearLight fills L1's rows 0..2
 * and L2's columns 0..2 with the three lights nearest the object, ordered by
 * strength (falloff, scale and mean colour), so two lights whose strengths
 * cross between ticks change slots: in the dark hall a pulsing light passes
 * a steady one and slots 1 and 2 of the boy's lights swap.  Blended slot by
 * slot, the half-way frame would light him from a direction between the two
 * lights, with a mix of their colours: some normals a quarter darker in red
 * than in either tick (rd_interp_test's hall lights).  Before the blend,
 * prev's three lights are put in the slots of the lights of cur they are
 * nearest in direction: light i is element i of qw 28..31 (L1's row i) and
 * qw 32 + i (L2's column i); qw 35, the ambient, stays.  The shading of
 * prev's block is the same in any order (the program sums the three lights'
 * terms).  A zero row (no light) pairs with a zero row.  The order changes
 * only when another pairing is closer than the recorded one. */
static int s_lightPair; /* lit draws whose previous lights were re-paired, this present */

static bool isLitDraw(uint8_t type, uint8_t prog)
{
    switch (type) {
    case RDC_MESH:
        return prog == RD_PROG_LIT || prog == RD_PROG_LIT_SPEC;
    case RDC_SKINNED:
        return prog == RD_PROG_SKIN || prog == RD_PROG_SKIN_SPEC;
    case RDC_GRID:
        return prog == RD_PROG_GRID_LIT;
    default:
        return false;
    }
}

/* the three lights' unit directions in the world, for the pairing: a draw
 * with a model matrix (normal_l, the grid: L1 is in the model's space)
 * through its model to world W = S^-1 M (qw 4..7, 16..19), Ln = L1 W^-1 as
 * rotateLight has it, so a turn of the object between the ticks does not
 * move them; a skinned draw's L1 as it is (cluster turns the normals by
 * the bones, which are in the world), and as it is when W is singular.
 * zero[i]: the row is zero (no light) */
static void lightDirs(const float (*m)[4], bool model, double d[3][3], bool zero[3])
{
    double iw[16];
    bool world = false;
    if (model) {
        double s[16], is[16], mm[16], w[16];
        loadQw4(m, 4, s);
        loadQw4(m, 16, mm);
        if (invert4d(s, is)) {
            mul4d(is, mm, w);
            world = invert4d(w, iw);
        }
    }
    for (int i = 0; i < 3; i++) {
        double len = 0.0;
        for (int j = 0; j < 3; j++) {
            double v = 0.0;
            if (world) {
                for (int k = 0; k < 3; k++) {
                    v += (double)m[28 + k][i] * iw[j * 4 + k];
                }
            } else {
                v = m[28 + j][i];
            }
            d[i][j] = v;
            len += v * v;
        }
        len = sqrt(len);
        zero[i] = !(len > 1e-6) || !isfinite(len);
        for (int j = 0; j < 3; j++) {
            d[i][j] = zero[i] ? 0.0 : d[i][j] / len;
        }
    }
}

/* p (prev's block, a copy) with its lights moved into the slots of cur's
 * (c) they pair with; false: the recorded order is the nearest */
static bool pairLights(float (*p)[4], const float (*c)[4], bool model)
{
    static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                                    {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    double dp[3][3], dc[3][3], score[3][3];
    bool zp[3], zc[3];
    lightDirs((const float (*)[4])p, model, dp, zp);
    lightDirs(c, model, dc, zc);
    for (int i = 0; i < 3; i++) { /* cur's light i, prev's light j */
        for (int j = 0; j < 3; j++) {
            if (zc[i] || zp[j]) {
                score[i][j] = zc[i] == zp[j] ? 2.0 : -2.0;
            } else {
                score[i][j] = dc[i][0] * dp[j][0] + dc[i][1] * dp[j][1] + dc[i][2] * dp[j][2];
            }
        }
    }
    int best = 0;
    double bestScore = score[0][0] + score[1][1] + score[2][2];
    const double keep = bestScore;
    for (int k = 1; k < 6; k++) {
        const double s = score[0][perms[k][0]] + score[1][perms[k][1]] + score[2][perms[k][2]];
        if (s > bestScore) {
            bestScore = s;
            best = k;
        }
    }
    if (best == 0 || !(bestScore > keep + 1e-3)) {
        return false;
    }
    float was[8][4];
    memcpy(was, p[28], sizeof(was));
    for (int i = 0; i < 3; i++) {
        const int j = perms[best][i];
        for (int k = 0; k < 4; k++) {
            p[28 + k][i] = was[k][j]; /* L1's row */
        }
        memcpy(p[32 + i], was[4 + j], sizeof(p[0])); /* L2's column */
    }
    return true;
}

/* A normal program's model matrices (qw 16..19 model to screen, 20..23
 * model to clip, 24..27 model to view) are each a camera part times the
 * object's model to world W.  W = S^-1 (qw 16..19), S the common block's
 * world to screen (qw 4..7); W blends as a rotation (rd__blend_affine), each
 * camera part C = M W^-1 element-wise like the common block, and the result
 * is C(t) W(t).  An element-wise blend of M shortens the axes of a turning
 * object by cos(theta / 2) half way (8 % at a 45 degree turn in a tick).
 * With lit (normal_l), the light matrix L1 turns with W (rotateLight).
 * o holds the element-wise blend already; false: it stays. */
static bool rotateModel(float (*o)[4], const float (*p)[4], const float (*c)[4], float t,
                        const double *pivot, bool lit)
{
    double sp[16], sc[16], isp[16], isc[16], mp[16], mc[16], wp[16], wc[16], wt[16], iwp[16],
        iwc[16];
    if (sameQw4(p, c, 16)) {
        return false; /* the object and the camera did not move */
    }
    loadQw4(p, 4, sp);
    loadQw4(c, 4, sc);
    loadQw4(p, 16, mp);
    loadQw4(c, 16, mc);
    if (!invert4d(sp, isp) || !invert4d(sc, isc)) {
        return false;
    }
    mul4d(isp, mp, wp);
    mul4d(isc, mc, wc);
    if (!rd__blend_affine(wp, wc, t, pivot, wt) || !invert4d(wp, iwp) || !invert4d(wc, iwc)) {
        return false;
    }
    if (s_turnDeg > RD_INTERP_TURN_SNAP) {
        /* a flip, not a motion: the tick's (its lights with it) */
        memcpy(o[16], c[16], (lit ? 16 : 12) * sizeof(o[0]));
        return true;
    }
    float res[12][4];
    for (int at = 16; at < 28; at += 4) {
        double cp[16], cc[16], ct[16], m[16];
        if (sameQw4(p, c, at)) {
            for (int k = 0; k < 4; k++) {
                memcpy(res[at - 16 + k], o[at + k], sizeof(res[0]));
            }
            continue;
        }
        loadQw4(p, at, mp);
        loadQw4(c, at, mc);
        mul4d(mp, iwp, cp);
        mul4d(mc, iwc, cc);
        for (int i = 0; i < 16; i++) {
            ct[i] = (1.0 - t) * cp[i] + t * cc[i];
        }
        mul4d(ct, wt, m);
        for (int i = 0; i < 16; i++) {
            if (!isfinite(m[i]) || fabs(m[i]) > 3.0e38) {
                return false;
            }
            res[at - 16 + i / 4][i % 4] = (float)m[i];
        }
    }
    memcpy(o[16], res, sizeof(res));
    if (lit && !rd__s2_legacy()) {
        s_lightRot += rotateLight(o, p, c, t, iwp, iwc, wt);
    }
    return true;
}

/* ------------------------------------------------------------- the camera */

/* The half-way frame's camera, a rigid transform: the inverse views (view to
 * world: the rotation and the eye) blended by rd__blend_affine about the eye
 * (rotation slerped, eye lerped), not the camera's matrices element by
 * element.  Every VU draw seen through the frame's camera (its inverse view,
 * qw 12..15, inverts RdCamera.view) is re-based on it, matched or not: a
 * tick's camera part C = Q V (Q the projection: qw 4..7 S = P V, the model
 * matrices M = Q V W) becomes Q Vt = C E with E = V^-1 Vt, which on a model
 * matrix is M W^-1 E W (W = S^-1 M, the model to world).  A matched draw
 * blends its two ticks' re-based blocks (both then through Vt: the camera
 * parts agree and only the object moves); an unmatched, mismatched, jumped
 * or unkeyed one is cur's object through Vt, so it stays with its neighbours
 * instead of standing a tick ahead of them.  CPU-projected draws (RDC_SCREEN,
 * RDC_SHADOW_STRIP) hold GS positions: those the frame camera projected
 * (marked at the game's projection sites) are taken through the world onto
 * the blended camera too (reprojDraw, reprojBlend), the rest keep their
 * blend in screen space; unmatched, both fade or switch half way
 * (unmatchedPass); draws under another camera (a reflection's) keep the
 * element-wise blend.
 *
 * Two kinds of part carry the frame's camera block but are not placed
 * through the view (RdVuDraw.view, the command's b[4], from the game's node
 * flags; reg_setMMatrixPacket): a part locked to the camera (flag 2, its
 * model matrices the 500 unit screen times its place in view space) is
 * already where the camera puts it, so only its camera block is re-based
 * and its model matrices stay the ticks' (moving it as a world object
 * would carry it across the screen, or behind the eye, with every step of
 * the blended camera); a billboard (flag 4, the screen matrix times a
 * matrix whose rotation is the view's) keeps its rotation in view space and
 * moves only its place, the world point, onto the blended camera, so it
 * faces that camera (camRebase's view). */
static bool isNormalProg(uint8_t prog);
static uint8_t *outPayload(const RdCmd *c);

typedef struct CamBlend {
    int on;
    int still;             /* the two views are the same: E is the identity */
    double vp[16], vc[16]; /* the two ticks' views */
    double ep[16], ec[16]; /* Vp^-1 Vt, Vc^-1 Vt */
    double vt[16], it[16]; /* the blended view and its inverse */
    double pc[16];         /* cur's projection (RdCamera.proj43) */
    double lc[16];         /* Pt Pc^-1: cur's projection to the blended one */
    int zoom;              /* the projections differ (the zoom eases) */
    double pp[16];         /* prev's projection */
    double lp[16];         /* Pt Pp^-1, valid with zoomPrev: for a draw of prev alone */
    int zoomPrev;
    float itf[16];
} CamBlend;

static CamBlend s_cam;

static uint32_t s_camGen, s_camGenSeen[2]; /* camSetup calls: camOf's cache */

static void loadF16(const float *f, double *o)
{
    for (int i = 0; i < 16; i++) {
        o[i] = f[i];
    }
}

/* s_cam for prev, cur at t; off without both cameras, in the legacy mode,
 * or when the blend fails (not affine, a mirror) */
static void camSetup(const RdFrame *prev, const RdFrame *cur, float t)
{
    memset(&s_cam, 0, sizeof(s_cam));
    s_camGen++;
    if (rd__s2_legacy() || !prev->hasCamera || !cur->hasCamera || !(t > 0.0f) || !(t < 1.0f)) {
        return;
    }
    double ip[16], ic[16];
    loadF16(prev->camera.view, s_cam.vp);
    loadF16(cur->camera.view, s_cam.vc);
    if (!invert4d(s_cam.vp, ip) || !invert4d(s_cam.vc, ic) ||
        !rd__blend_affine(ip, ic, t, NULL, s_cam.it) || !invert4d(s_cam.it, s_cam.vt)) {
        return;
    }
    mul4d(ip, s_cam.vt, s_cam.ep);
    mul4d(ic, s_cam.vt, s_cam.ec);
    s_cam.still = memcmp(prev->camera.view, cur->camera.view, sizeof(prev->camera.view)) == 0;
    /* the projection: GsBase.c's zoom eases towards its target (zoomCurrent,
     * the focus distance), a few per cent a tick in stage 3; a matched draw
     * blends it with the rest of its block, a draw that is cur's takes
     * Pt = lerp(Pp, Pc) through Pt Pc^-1 */
    double pp[16], pt[16], ipc[16];
    loadF16(prev->camera.proj43, pp);
    loadF16(cur->camera.proj43, s_cam.pc);
    if (memcmp(prev->camera.proj43, cur->camera.proj43, sizeof(prev->camera.proj43)) != 0 &&
        invert4d(s_cam.pc, ipc)) {
        for (int i = 0; i < 16; i++) {
            pt[i] = (1.0 - t) * pp[i] + t * s_cam.pc[i];
        }
        mul4d(pt, ipc, s_cam.lc);
        s_cam.zoom = 1;
        s_cam.still = 0;
        double ipp[16];
        s_cam.zoomPrev = invert4d(pp, ipp);
        if (s_cam.zoomPrev) {
            mul4d(pt, ipp, s_cam.lp);
        }
    }
    memcpy(s_cam.pp, pp, sizeof(pp));
    for (int i = 0; i < 16; i++) {
        s_cam.itf[i] = (float)s_cam.it[i];
    }
    s_cam.on = 1;
}

/* How a block relates to the view v: CAM_NONE; CAM_VIEW, its inverse view
 * (qw 12..15) times v is the identity to 2e-3 (relative to the eye's
 * distance for the translation); CAM_FULL, and its world to screen (qw
 * 4..7) is the projection proj times v: the frame's camera, whose
 * projection blends too */
enum { CAM_NONE = 0, CAM_VIEW, CAM_FULL };

static int camOfUncached(const float (*m)[4], const double *v, const double *proj);

static int camOf(const float (*m)[4], const double *v, const double *proj)
{
    /* most blocks of a frame carry the same common block: the last answer
     * per view (prev's, cur's) */
    static float last[2][12][4];
    static const double *lastV[2];
    static int lastR[2];
    const int slot = v == s_cam.vp ? 0 : 1;
    if (lastV[slot] == v && s_camGen == s_camGenSeen[slot] &&
        memcmp(last[slot], m[4], sizeof(last[slot])) == 0) {
        return lastR[slot];
    }
    memcpy(last[slot], m[4], sizeof(last[slot]));
    lastV[slot] = v;
    s_camGenSeen[slot] = s_camGen;
    lastR[slot] = camOfUncached(m, v, proj);
    return lastR[slot];
}

static int camOfUncached(const float (*m)[4], const double *v, const double *proj)
{
    double iv[16], p[16], s[16];
    loadQw4(m, 12, iv);
    mul4d(iv, v, p);
    /* GsBase.c's inverse view (_InversMatrix) is the transpose of a view
     * that is a rotation to about 1e-4 (stage 3: a product of 0.99989) */
    const double tol = 2e-3 * (1.0 + fabs(v[12]) + fabs(v[13]) + fabs(v[14]));
    for (int i = 0; i < 16; i++) {
        const double want = (i % 5) == 0 ? 1.0 : 0.0;
        if (!(fabs(p[i] - want) <= (i >= 12 ? tol : 2e-3))) {
            return CAM_NONE;
        }
    }
    if (!proj) {
        return CAM_VIEW;
    }
    mul4d(proj, v, p);
    loadQw4(m, 4, s);
    for (int i = 0; i < 16; i++) {
        if (!(fabs(s[i] - p[i]) <= 1e-4 * (1.0 + fabs(p[i])))) {
            return CAM_VIEW;
        }
    }
    return CAM_FULL;
}

static void storeQw4(float (*m)[4], int at, const double *d)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            m[at + c][r] = (float)d[c * 4 + r];
        }
    }
}

/* The model matrices a program draws with, the camera on their left: a
 * normal program's qw 16..27; the grid's and the particles' qw 16..19 (the
 * particles' qw 20..23 is the screen matrix alone); a skinned draw's bones
 * are in the world, its camera is qw 4..7 only. */
static int camModelMats(uint8_t type, uint8_t prog)
{
    if (type == RDC_MESH && isNormalProg(prog)) {
        return 3;
    }
    return type == RDC_GRID || type == RDC_PARTICLES ? 1 : 0;
}

/* The view a VU draw's model matrices are built in (rd_mesh.h
 * RD_VU_VIEW_*): only a normal program's mesh has one */
static int vuView(const RdCmd *c)
{
    if (c->type != RDC_MESH || !isNormalProg(c->b[0]) || c->b[4] > RD_VU_VIEW_FACING) {
        return RD_VU_VIEW_WORLD;
    }
    return c->b[4];
}

/* m (a VU block, 36 qw) re-based from its tick's view v onto Vt by e (v^-1
 * Vt: Vp^-1 Vt or Vc^-1 Vt), its model matrices by their view (vuView):
 * through the view, as the world seen by Vt; locked to the camera, kept;
 * facing the camera, the place moved as a world point and the rotation
 * kept in view space; false (m untouched) when a matrix is singular or the
 * result is not finite */
static bool camRebase(float (*m)[4], const double *e, int mats, const double *l, const double *v,
                      int view)
{
    double s[16], is[16], se[16], w[16], iw[16], k[16], tmp[16], x[16];
    float out[16][4];
    if (view == RD_VU_VIEW_LOCKED) {
        mats = 0;
    }
    loadQw4(m, 4, s);
    mul4d(s, e, se);
    if (l) {
        mul4d(l, se, tmp);
        memcpy(se, tmp, sizeof(se));
    }
    storeQw4(out, 0, se);
    storeQw4(out, 4, s_cam.it);
    if (mats > 0) {
        double m16[16];
        loadQw4(m, 16, m16);
        if (!invert4d(s, is)) {
            return false;
        }
        mul4d(is, m16, w);
        if (!invert4d(w, iw)) {
            return false;
        }
        double eb[16];
        const double *ek = e;
        if (view == RD_VU_VIEW_FACING) {
            /* X = v W, the billboard in view space: its place p moves by D p
             * - p (D = v e v^-1 = Vt v^-1, the view-space step to the blended
             * camera), its rotation stays; E_b = v^-1 T(D p - p) v */
            double iv[16], vw[16], d[16], tr[16];
            if (!invert4d(v, iv)) {
                return false;
            }
            mul4d(v, w, vw);
            if (!(fabs(vw[15]) > 1e-12)) {
                return false;
            }
            mul4d(v, e, tmp);
            mul4d(tmp, iv, d);
            double pv[3], dp[3];
            for (int r = 0; r < 3; r++) {
                pv[r] = vw[12 + r] / vw[15];
            }
            for (int r = 0; r < 3; r++) {
                dp[r] = d[r] * pv[0] + d[4 + r] * pv[1] + d[8 + r] * pv[2] + d[12 + r];
            }
            memset(tr, 0, sizeof(tr));
            tr[0] = tr[5] = tr[10] = tr[15] = 1.0;
            for (int r = 0; r < 3; r++) {
                tr[12 + r] = dp[r] - pv[r];
            }
            mul4d(iv, tr, tmp);
            mul4d(tmp, v, eb);
            ek = eb;
        }
        mul4d(iw, ek, tmp);
        mul4d(tmp, w, k); /* W^-1 E W */
    }
    float model[12][4];
    for (int a = 0; a < mats; a++) {
        double mm[16];
        loadQw4(m, 16 + 4 * a, mm);
        mul4d(mm, k, x);
        if (l && a == 0) { /* qw 16..19, model to screen: the projection's */
            mul4d(l, x, tmp);
            memcpy(x, tmp, sizeof(x));
        }
        storeQw4(model, 4 * a, x);
    }
    for (int q = 0; q < 8; q++) {
        for (int r = 0; r < 4; r++) {
            if (!isfinite(out[q][r]) || fabsf(out[q][r]) > 3.0e38f) {
                return false;
            }
        }
    }
    for (int q = 0; q < 4 * mats; q++) {
        for (int r = 0; r < 4; r++) {
            if (!isfinite(model[q][r]) || fabsf(model[q][r]) > 3.0e38f) {
                return false;
            }
        }
    }
    memcpy(m[4], out[0], 4 * sizeof(m[0]));
    memcpy(m[12], out[4], 4 * sizeof(m[0]));
    if (mats > 0) {
        memcpy(m[16], model, (size_t)mats * 4 * sizeof(m[0]));
    }
    return true;
}

/* ------------------------------------------------ CPU-projected draws
 *
 * A screen prim marked RD_SCREEN_FRAME_CAMERA or a shadow volume marked
 * RD_SHADOW_FRAME_CAMERA holds vertices the game projected on the CPU
 * through its tick's camera (lightning.c set_vertex, lineManager.c,
 * Shadow.c's volumes, GifPacket.c's rotTransPers strips, RegistPacket.c's
 * lines): GS X / 16, Y / 16 and Z / 16 are x / w, y / w and z / w of the
 * world point through S = proj43 view (RdCamera; GsBase.c's +0x100), which
 * inverts.  While the camera moves, each vertex is taken back into the
 * world through its tick's S^-1 and projected with the blended camera St =
 * Pt Vt, the camera every VU draw is re-based on (camSetup, camRebase), in
 * the VU's float order (vu_common.hlsli vu_matm: c0 x + c1 y + c2 z + c3
 * each step rounded; vu_divide: q = 1 / w, x q, y q, z q; vu_ftoi4), so a
 * decal or a bolt lying on a wall keeps the wall's depth instead of the
 * tick's, which a GEQUAL test failed while the camera moved and passed
 * when it stopped.  A matched pair blends its two world points; a draw of
 * one tick is that tick's points through St.  STQ (the tick's own) is
 * scaled by w_tick / w_new: S / Q stays at each vertex and the texture
 * follows the new depth across the prim.  A vertex stays where its tick
 * put it (matched: blended in screen space as before) when its Z is 0 or
 * saturated by ftoi4, when it is not in front of its tick's camera, or when
 * St does not see it in front with a GS position.  Nothing is re-projected
 * with a still camera (s_cam.still) or outside a blend, so those frames
 * are byte for byte what they were.  The reflection prims recorded under
 * rd_push_camera are never marked. */
typedef struct ReprojCam {
    int on;
    double ip[16], ic[16]; /* (Pp Vp)^-1 and (Pc Vc)^-1: the ticks' GS screen to world */
    float st[16];          /* Pt Vt, world to GS screen, columns as a VU block holds them */
} ReprojCam;

static ReprojCam s_rp;

static bool s_rpOff; /* rd__interp_set_reproject(false) */

static uint32_t s_reproj; /* draws re-projected, this present */

void rd__interp_set_reproject(bool on)
{
    s_rpOff = !on;
}

/* s_rp for the camera camSetup blended at t */
static void reprojSetup(float t)
{
    memset(&s_rp, 0, sizeof(s_rp));
    if (s_rpOff || !s_cam.on || s_cam.still) {
        return;
    }
    double sp[16], sc[16], pt[16], st[16];
    mul4d(s_cam.pp, s_cam.vp, sp);
    mul4d(s_cam.pc, s_cam.vc, sc);
    if (!invert4d(sp, s_rp.ip) || !invert4d(sc, s_rp.ic)) {
        return;
    }
    for (int i = 0; i < 16; i++) {
        pt[i] = s_cam.zoom ? (1.0 - t) * s_cam.pp[i] + t * s_cam.pc[i] : s_cam.pc[i];
    }
    mul4d(pt, s_cam.vt, st);
    for (int i = 0; i < 16; i++) {
        if (!isfinite(st[i]) || fabs(st[i]) > 3.0e38) {
            return;
        }
        s_rp.st[i] = (float)st[i];
    }
    s_rp.on = 1;
}

/* whether c is a draw the frame camera projected that this file re-projects */
static bool frameProjected(const RdCmd *c)
{
    if (c->type == RDC_SCREEN) {
        return c->b[4] == RD_SCREEN_FRAME_CAMERA && c->b[1] == RD_SPACE_WORLD &&
               c->b[0] != RD_PRIM_SPRITES && c->b[3] == 0;
    }
    return c->type == RDC_SHADOW_STRIP && c->b[0] == RD_SHADOW_TRIS &&
           c->b[1] == RD_SHADOW_FRAME_CAMERA;
}

/* v's world point p through inv (a tick's S^-1) and its w there; false when
 * Z is 0 or saturated by ftoi4, or the point is not in front of the camera */
static bool reprojUnproject(const double *inv, const RdScreenVtx *v, double p[3], double *w)
{
    if (v->z == 0 || v->z >= 0x7FFFFFFFu) {
        return false;
    }
    const double s[3] = {(double)v->x / 16.0, (double)v->y / 16.0, (double)v->z / 16.0};
    double h[4];
    for (int r = 0; r < 4; r++) {
        h[r] = inv[r] * s[0] + inv[4 + r] * s[1] + inv[8 + r] * s[2] + inv[12 + r];
    }
    if (!(h[3] > 0.0) || !isfinite(h[3])) {
        return false; /* h[3] = 1 / w */
    }
    for (int k = 0; k < 3; k++) {
        p[k] = h[k] / h[3];
        if (!isfinite(p[k])) {
            return false;
        }
    }
    *w = 1.0 / h[3];
    return true;
}

/* the world point p through St as the VU computes a vertex (vu_mat with w
 * 1, vu_divide, vu_ftoi4) into o's X, Y and Z, and its w; false (o
 * untouched) when w is not positive, X or Y leaves the GS's 16-bit window
 * or Z the ftoi4 range */
static bool reprojProject(const double p[3], RdScreenVtx *o, double *w)
{
    const float *m = s_rp.st;
    const float x = (float)p[0], y = (float)p[1], z = (float)p[2];
    float h[4];
    for (int r = 0; r < 4; r++) {
        float a = m[r] * x;
        a = a + m[4 + r] * y;
        a = a + m[8 + r] * z;
        a = a + m[12 + r] * 1.0f;
        h[r] = a;
    }
    if (!(h[3] > 0.0f) || !isfinite(h[3])) {
        return false;
    }
    const float q = 1.0f / h[3];
    const float gx = h[0] * q * 16.0f, gy = h[1] * q * 16.0f, gz = h[2] * q * 16.0f;
    if (!(gx >= 0.0f && gx < 65536.0f) || !(gy >= 0.0f && gy < 65536.0f) ||
        !(gz >= 1.0f && gz < 2147483648.0f)) {
        return false;
    }
    o->x = (int32_t)gx; /* ftoi4: truncation */
    o->y = (int32_t)gy;
    o->z = (uint32_t)(int32_t)gz;
    *w = h[3];
    return true;
}

/* STQ times w / wNew (the tick's w over the new one) */
static void reprojStq(RdScreenVtx *v, double w, double wNew)
{
    const double k = w / wNew;
    v->s = (float)((double)v->s * k);
    v->t = (float)((double)v->t * k);
    v->q = (float)((double)v->q * k);
}

/* v (a vertex of the tick whose S^-1 is inv) moved by d in the world and
 * through St; false (v untouched) where it cannot be */
static bool reprojVertex(const double *inv, RdScreenVtx *v, const double *d, bool stq)
{
    double p[3], w, wNew;
    RdScreenVtx o = *v;
    if (!reprojUnproject(inv, v, p, &w)) {
        return false;
    }
    if (d) {
        for (int k = 0; k < 3; k++) {
            p[k] += d[k];
        }
    }
    if (!reprojProject(p, &o, &wNew)) {
        return false;
    }
    if (stq) {
        reprojStq(&o, w, wNew);
    }
    *v = o;
    return true;
}

/* a matched pair's vertex: o (cur's) becomes the blend at t of the world
 * points of p (prev's) and o through St, cur's STQ scaled; false (o
 * untouched) where either has no world point or St cannot draw it */
static bool reprojBlend(RdScreenVtx *o, const RdScreenVtx *p, float t, bool stq)
{
    double a[3], b[3], m[3], wp, wc, wNew;
    RdScreenVtx r = *o;
    if (!reprojUnproject(s_rp.ip, p, a, &wp) || !reprojUnproject(s_rp.ic, o, b, &wc)) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        m[k] = (1.0 - (double)t) * a[k] + (double)t * b[k];
    }
    if (!reprojProject(m, &r, &wNew)) {
        return false;
    }
    if (stq) {
        reprojStq(&r, wc, wNew);
    }
    *o = r;
    return true;
}

/* the vertices of c (a draw of one tick, frameProjected) in payload v, of
 * the tick whose S^-1 is inv, through St */
static void reprojDraw(const RdCmd *c, uint8_t *v, const double *inv)
{
    if (!s_rp.on || !v || !frameProjected(c)) {
        return;
    }
    RdScreenVtx *o = (RdScreenVtx *)(void *)v;
    const bool screen = c->type == RDC_SCREEN;
    const uint32_t n = screen ? c->u[1] : c->u[0] + c->u[3];
    const bool stq = screen && c->b[2] == 0;
    for (uint32_t i = 0; i < n; i++) {
        (void)reprojVertex(inv, &o[i], NULL, stq);
    }
    s_reproj++;
}

static int s_rebased; /* VU draws re-based on the blended camera, this frame */

static int s_rebasedCur; /* of them, draws that are cur's */

/* A draw that is cur's (unmatched, mismatched, jumped, unkeyed): cur's
 * object through the blended camera */
static void camCurDraw(RdCmd *c)
{
    if (!s_cam.on || s_cam.still) {
        return;
    }
    if (frameProjected(c)) {
        reprojDraw(c, outPayload(c), s_rp.ic); /* cur's points through the blended camera */
        return;
    }
    switch (c->type) {
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
        break;
    default:
        return;
    }
    if (c->u[2] < sizeof(RdVuPayload) + sizeof(RdVuBlock)) {
        return;
    }
    uint8_t *op = outPayload(c);
    if (!op) {
        return;
    }
    float (*vo)[4] = (float (*)[4])(void *)(op + sizeof(RdVuPayload));
    const int how = camOf((const float (*)[4])vo, s_cam.vc, s_cam.pc);
    if (how != CAM_NONE &&
        camRebase(vo, s_cam.ec, camModelMats(c->type, c->b[0]),
                  how == CAM_FULL && s_cam.zoom ? s_cam.lc : NULL, s_cam.vc, vuView(c))) {
        s_rebased++;
        s_rebasedCur++;
    }
}

/* A draw's model origin in the world: the common block's world to GS
 * screen (qw 4..7) inverted, applied to the model to screen translation
 * (qw 19: normal programs' +0x140 x node, the grid's and the particles'
 * screen matrix).  qw 24..27 (model to view) would be simpler but is not
 * uploaded by every pass: list 5's dissolve draws carry another object's.
 * The camera's motion cancels, so this is the object's own move. */
static bool vuWorldOrigin(const float (*m)[4], float out[3])
{
    double inv[16], w[4] = {0.0, 0.0, 0.0, 0.0};
    if (!invert4(&m[4][0], inv)) {
        return false;
    }
    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 4; k++) {
            w[r] += inv[k * 4 + r] * m[19][k];
        }
    }
    if (!(fabs(w[3]) > 1e-12)) {
        return false;
    }
    for (int j = 0; j < 3; j++) {
        out[j] = (float)(w[j] / w[3]);
    }
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}

/* The screen position of a model to GS screen matrix's origin (qw at..at+3,
 * columns): its translation column divided by w. */
static bool vuScreenOrigin(const float (*m)[4], int at, float out[2])
{
    const float *t = m[at + 3];
    if (!(t[3] > 0.0f) || !isfinite(t[0]) || !isfinite(t[1])) {
        return false;
    }
    out[0] = t[0] / t[3];
    out[1] = t[1] / t[3];
    return isfinite(out[0]) && isfinite(out[1]);
}

static bool screenJump(const float (*p)[4], const float (*c)[4], int at)
{
    float a[2], b[2];
    if (!vuScreenOrigin(p, at, a) || !vuScreenOrigin(c, at, b)) {
        return false;
    }
    return fabsf(a[0] - b[0]) > RD_INTERP_JUMP_SCREEN || fabsf(a[1] - b[1]) > RD_INTERP_JUMP_SCREEN;
}

/* the world test, else (a singular common block) the screen test */
static bool worldJump(const float (*p)[4], const float (*c)[4])
{
    float a[3], b[3];
    if (vuWorldOrigin(p, a) && vuWorldOrigin(c, b)) {
        return dist3(a, b) > RD_INTERP_JUMP_WORLD;
    }
    return screenJump(p, c, 16);
}

static bool isNormalProg(uint8_t prog)
{
    return prog == RD_PROG_PRELIT || prog == RD_PROG_LIT || prog == RD_PROG_LIT_SPEC ||
           prog == RD_PROG_REFLECT;
}

static bool vtxJump(const RdScreenVtx *p, const RdScreenVtx *c, uint32_t n)
{
    const int32_t lim = (int32_t)(RD_INTERP_JUMP_SCREEN * 16.0f);
    for (uint32_t i = 0; i < n; i++) {
        if (abs(p[i].x - c[i].x) > lim || abs(p[i].y - c[i].y) > lim) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------- matching */

typedef struct Slot {
    uint32_t lo, hi;
    uint8_t type, list, used;
    uint8_t placed;                      /* cur's occurrences are paired (placeSlot) */
    int32_t head, tail, cursor;          /* prev's occurrences, in order (Node.next) */
    uint32_t count;                      /* of them */
    int32_t curHead, curTail, curCursor; /* cur's, when prev has two or more (CurNode) */
    uint32_t spare;                      /* placeSlot: prev's occurrences no draw of cur picked */
} Slot;

typedef struct Node {
    uint32_t list, index;
    int32_t next;
    int32_t out; /* the index of its match in s_out's list, -1: unmatched */
    int32_t org; /* placeSlot: 1 the draw's origin is in at, -1 it has none, 0 not known */
    float at[3]; /* its model origin in the world (vuWorldOrigin) */
} Node;

/* cur's occurrence of a key prev drew two or more times (placeSlot) */
typedef struct CurNode {
    uint32_t index; /* in s_out's list */
    int32_t next;
    int32_t pick;  /* the prev Node it is paired with, -1 none */
    int32_t org;   /* placeSlot: 1 the draw's origin is in at, -1 it has none */
    int32_t apart; /* paired with a draw of another place: drawn as the tick's */
    float at[3];
} CurNode;

static CurNode *s_curNodes;

static uint32_t s_curCap, s_curCount;

static int s_placed; /* draws paired by their place in the world, not by their ordinal */

static Slot *s_slots;

static uint32_t s_slotCap;

static Node *s_nodes;

static uint32_t s_nodeCap, s_nodeCount;

static bool isKeyedDraw(const RdCmd *c)
{
    if (c->keyLo == 0 && c->keyHi == 0) {
        return false;
    }
    switch (c->type) {
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_SHADOW_STRIP:
    case RDC_OVERLAY_TEXT: /* deferred text */
        return true;
    default:
        return false;
    }
}

static uint32_t slotHash(uint32_t lo, uint32_t hi, uint8_t type, uint8_t list)
{
    uint32_t h = lo * 0x9E3779B1u;
    h ^= hi * 0x85EBCA77u + 0x165667B1u;
    h ^= ((uint32_t)type << 8 | list) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x27D4EB2Fu;
    h ^= h >> 13;
    return h;
}

static Slot *slotFind(const RdCmd *c, int list, bool insert)
{
    uint32_t i = slotHash(c->keyLo, c->keyHi, c->type, (uint8_t)list) & (s_slotCap - 1);
    for (;;) {
        Slot *s = &s_slots[i];
        if (!s->used) {
            if (!insert) {
                return NULL;
            }
            s->used = 1;
            s->lo = c->keyLo;
            s->hi = c->keyHi;
            s->type = c->type;
            s->list = (uint8_t)list;
            s->head = s->tail = s->cursor = -1;
            s->curHead = s->curTail = s->curCursor = -1;
            return s;
        }
        if (s->lo == c->keyLo && s->hi == c->keyHi && s->type == c->type && s->list == list) {
            return s;
        }
        i = (i + 1) & (s_slotCap - 1);
    }
}

/* the table of prev's keyed draws; false on out of memory */
static bool buildIndex(const RdFrame *prev)
{
    uint32_t n = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < prev->lists[l].count; i++) {
            n += isKeyedDraw(&prev->lists[l].cmds[i]);
        }
    }
    uint32_t cap = 64;
    while (cap < n * 2 + 16) {
        cap *= 2;
    }
    if (cap > s_slotCap) {
        Slot *p = realloc(s_slots, (size_t)cap * sizeof(Slot));
        if (!p) {
            return false;
        }
        s_slots = p;
        s_slotCap = cap;
    }
    memset(s_slots, 0, (size_t)s_slotCap * sizeof(Slot));
    if (n > s_nodeCap) {
        Node *p = realloc(s_nodes, (size_t)n * sizeof(Node));
        if (!p) {
            return false;
        }
        s_nodes = p;
        s_nodeCap = n;
    }
    s_nodeCount = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < prev->lists[l].count; i++) {
            const RdCmd *c = &prev->lists[l].cmds[i];
            if (!isKeyedDraw(c)) {
                continue;
            }
            Slot *s = slotFind(c, l, true);
            const int32_t k = (int32_t)s_nodeCount++;
            s_nodes[k].list = (uint32_t)l;
            s_nodes[k].index = i;
            s_nodes[k].next = -1;
            s_nodes[k].out = -1;
            s_nodes[k].org = 0;
            s->count++;
            if (s->tail >= 0) {
                s_nodes[s->tail].next = k;
            } else {
                s->head = s->cursor = k;
            }
            s->tail = k;
        }
    }
    return true;
}

/* Same-key instances.  The game keys a packet by its object and part
 * (RegistPacket.c), not by the instance, so objects drawn many times share
 * one key: the ripples of swimming and wading (hamon, wfrip1, splash_mini:
 * a ring of up to 30 stage animations entered by the boy's steps and
 * strokes, each fixed in the world where it was entered), the fog
 * billboards (fog1).  Paired by ordinal, when the oldest instance goes
 * every later one would blend from the place and size of the one before
 * it, a jump back and forth once per spawn.  So the VU meshes and grids
 * of a key prev drew two or more times are paired by their model
 * origin in the world (vuWorldOrigin; the camera's motion cancels) first:
 * each draw of cur with the first unpaired draw of prev at the same origin
 * (sameOrigin).  When some are, the slot's instances stand
 * still, and the draws of the two ticks left over are paired with each
 * other, the nearest first, then in order (pairLeftovers), so that no draw
 * of cur stays unmatched while a draw of prev with its key is free: two
 * that are each other's nearest among all the slot's draws of the other
 * tick are one instance that moved and blend; any other pair is a new
 * instance and one that went, and the new one is drawn as the tick's
 * (CurNode.apart), not blended from the other's place.  When none is (the
 * instances move: the wading splash's spray, splash_shibuki, rises and falls
 * with its age) and their number holds, the pairing is the ordinal one;
 * when one went or came the ordinal would cross instances, so they are
 * paired in order, the draws of the longer side left out being those that
 * keep the pairs nearest (alignSlot). */
static bool canPlace(const RdCmd *c)
{
    /* not particles: MicroCode.c keys their batches by emitter */
    return c->type == RDC_MESH || c->type == RDC_GRID;
}

static bool sameOrigin(const float a[3], const float b[3])
{
    /* the origins of a still object, inverted from two ticks' matrices
     * through different cameras, agree to about 1e-6 of their distance from
     * the world's origin (the pool's ripples, 1000 units out, six ticks
     * apart: within 1e-3); instances of one key stand apart by many units
     * (the ripples by 11 or more) */
    const float m = fmaxf(fmaxf(fabsf(a[0]), fabsf(a[1])), fmaxf(fabsf(a[2]), 1.0f));
    return dist3(a, b) <= 0.01f + 2e-5f * m;
}

static bool growTo(void **p, uint32_t *cap, uint32_t n, size_t size);

static const void *payloadAt(const RdFrame *f, uint32_t off, uint32_t size);

/* the model origin of draw c of frame f (prev's or s_out), false without one */
static bool drawOrigin(const RdFrame *f, const RdCmd *c, float out[3])
{
    if (c->u[2] < sizeof(RdVuPayload) + sizeof(RdVuBlock)) {
        return false;
    }
    const uint8_t *p = payloadAt(f, c->u[1], c->u[2]);
    if (!p) {
        return false;
    }
    float m[36][4];
    memcpy(m, p + sizeof(RdVuPayload), sizeof(m));
    return vuWorldOrigin((const float (*)[4])m, out);
}

/* cur's (s_out's) occurrences of the keys prev drew two or more times, in
 * order; false on out of memory (the pairing is then the ordinal one) */
static bool indexCur(void)
{
    s_curCount = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
            const RdCmd *c = &s_out.lists[l].cmds[i];
            if (!isKeyedDraw(c) || !canPlace(c)) {
                continue;
            }
            Slot *s = slotFind(c, l, false);
            if (!s || s->count < 2) {
                continue;
            }
            if (!growTo((void **)&s_curNodes, &s_curCap, s_curCount + 1, sizeof(CurNode))) {
                for (uint32_t k = 0; k < s_slotCap; k++) {
                    s_slots[k].curHead = s_slots[k].curTail = s_slots[k].curCursor = -1;
                }
                return false;
            }
            const int32_t k = (int32_t)s_curCount++;
            s_curNodes[k].index = i;
            s_curNodes[k].next = -1;
            s_curNodes[k].pick = -1;
            s_curNodes[k].org = 0;
            s_curNodes[k].apart = 0;
            if (s->curTail >= 0) {
                s_curNodes[s->curTail].next = k;
            } else {
                s->curHead = s->curCursor = k;
            }
            s->curTail = k;
        }
    }
    return true;
}

/* the most occurrences of a key alignSlot pairs; beyond, the ordinal */
#define ALIGN_MAX 64

/* Pairs the moving occurrences of slot s, prev's s->count and cur's m of
 * them, every one with its origin, when the counts differ: in their order
 * (the game draws a ring of instances in slot order, so the ones that stay
 * keep theirs), leaving out of the longer side the draws that make the
 * pairs' summed distance the least.  The pairs' count, 0 when it cannot. */
static uint32_t alignSlot(Slot *s, uint32_t m)
{
    static int32_t pn[ALIGN_MAX], cn[ALIGN_MAX];
    static double cost[(ALIGN_MAX + 1) * (ALIGN_MAX + 1)];
    static uint8_t take[(ALIGN_MAX + 1) * (ALIGN_MAX + 1)];
    const uint32_t n = s->count;
    if (n > ALIGN_MAX || m > ALIGN_MAX || n == m) {
        return 0;
    }
    uint32_t i = 0;
    for (int32_t k = s->head; k >= 0; k = s_nodes[k].next) {
        if (s_nodes[k].org <= 0) {
            return 0;
        }
        pn[i++] = k;
    }
    i = 0;
    for (int32_t j = s->curHead; j >= 0; j = s_curNodes[j].next) {
        if (s_curNodes[j].org <= 0) {
            return 0;
        }
        cn[i++] = j;
    }
    /* short side a (each of its draws paired) in the long side b */
    const bool curShort = m < n;
    const uint32_t na = curShort ? m : n, nb = curShort ? n : m, w = nb + 1;
    for (uint32_t b = 0; b <= nb; b++) {
        cost[b] = 0.0;
    }
    for (uint32_t a = 1; a <= na; a++) {
        for (uint32_t b = a; b <= nb; b++) {
            const float *pa = curShort ? s_curNodes[cn[a - 1]].at : s_nodes[pn[a - 1]].at;
            const float *pb = curShort ? s_nodes[pn[b - 1]].at : s_curNodes[cn[b - 1]].at;
            const double pair = cost[(a - 1) * w + b - 1] + (double)dist3(pa, pb);
            /* b > a: b's draw may be left out */
            if (b > a && cost[a * w + b - 1] < pair) {
                cost[a * w + b] = cost[a * w + b - 1];
                take[a * w + b] = 0;
            } else {
                cost[a * w + b] = pair;
                take[a * w + b] = 1;
            }
        }
    }
    for (uint32_t a = na, b = nb; a > 0; b--) {
        if (take[a * w + b]) {
            if (curShort) {
                s_curNodes[cn[a - 1]].pick = pn[b - 1];
            } else {
                s_curNodes[cn[b - 1]].pick = pn[a - 1];
            }
            a--;
        }
    }
    return na;
}

/* the slot's draw of the other tick nearest to at (every one with its
 * origin, paired or not): a prev Node with prevSide, else a CurNode; -1
 * when none has an origin */
static int32_t nearestOf(const Slot *s, const float at[3], bool prevSide)
{
    int32_t best = -1;
    float bd = 0.0f;
    if (prevSide) {
        for (int32_t k = s->head; k >= 0; k = s_nodes[k].next) {
            const float d = s_nodes[k].org > 0 ? dist3(s_nodes[k].at, at) : -1.0f;
            if (d >= 0.0f && (best < 0 || d < bd)) {
                best = k;
                bd = d;
            }
        }
    } else {
        for (int32_t j = s->curHead; j >= 0; j = s_curNodes[j].next) {
            const float d = s_curNodes[j].org > 0 ? dist3(s_curNodes[j].at, at) : -1.0f;
            if (d >= 0.0f && (best < 0 || d < bd)) {
                best = j;
                bd = d;
            }
        }
    }
    return best;
}

/* cur's draw j paired with prev's k: apart unless both have origins and
 * each is the other's nearest (the same instance, moved) */
static void pairLeft(const Slot *s, int32_t j, int32_t k)
{
    CurNode *cn = &s_curNodes[j];
    const Node *nd = &s_nodes[k];
    cn->pick = k;
    cn->apart = cn->org > 0 && nd->org > 0 &&
                (nearestOf(s, cn->at, true) != k || nearestOf(s, nd->at, false) != j);
}

/* The draws of slot s left without a pair once some are paired by their
 * place (the still instances): cur's with prev's, by the nearest origins
 * first, then in order (draws without an origin, or more than ALIGN_MAX
 * on a side).  Node.out marks the prev draws taken while it runs and is
 * reset after. */
static void pairLeftovers(Slot *s)
{
    uint32_t nc = 0, np = 0;
    for (int32_t j = s->curHead; j >= 0; j = s_curNodes[j].next) {
        if (s_curNodes[j].pick >= 0) {
            s_nodes[s_curNodes[j].pick].out = 0;
        } else {
            nc++;
        }
    }
    for (int32_t k = s->head; k >= 0; k = s_nodes[k].next) {
        np += s_nodes[k].out < 0;
    }
    if (nc > 0 && np > 0 && nc <= ALIGN_MAX && np <= ALIGN_MAX) {
        for (;;) {
            int32_t bj = -1, bk = -1;
            float bd = 0.0f;
            for (int32_t j = s->curHead; j >= 0; j = s_curNodes[j].next) {
                const CurNode *cn = &s_curNodes[j];
                if (cn->pick >= 0 || cn->org <= 0) {
                    continue;
                }
                for (int32_t k = s->head; k >= 0; k = s_nodes[k].next) {
                    if (s_nodes[k].out >= 0 || s_nodes[k].org <= 0) {
                        continue;
                    }
                    const float d = dist3(cn->at, s_nodes[k].at);
                    if (bj < 0 || d < bd) {
                        bj = j;
                        bk = k;
                        bd = d;
                    }
                }
            }
            if (bj < 0) {
                break;
            }
            pairLeft(s, bj, bk);
            s_nodes[bk].out = 0;
        }
    }
    /* the rest in order */
    int32_t k = s->head;
    for (int32_t j = s->curHead; j >= 0; j = s_curNodes[j].next) {
        if (s_curNodes[j].pick >= 0) {
            continue;
        }
        while (k >= 0 && s_nodes[k].out >= 0) {
            k = s_nodes[k].next;
        }
        if (k < 0) {
            break;
        }
        pairLeft(s, j, k);
        s_nodes[k].out = 0;
    }
    for (int32_t q = s->head; q >= 0; q = s_nodes[q].next) {
        s_nodes[q].out = -1;
    }
}

/* pairs the occurrences of slot s (see above) into CurNode.pick */
static void placeSlot(const RdFrame *prev, Slot *s)
{
    s->placed = 1;
    const RdCmdList *cl = &s_out.lists[s->list];
    for (int32_t k = s->head; k >= 0; k = s_nodes[k].next) {
        Node *nd = &s_nodes[k];
        nd->org = drawOrigin(prev, &prev->lists[nd->list].cmds[nd->index], nd->at) ? 1 : -1;
    }
    /* the draws at the same origin; out marks the prev draws taken (reset
     * below: matchOf sets it) */
    uint32_t same = 0, m = 0;
    for (int32_t j = s->curHead; j >= 0; j = s_curNodes[j].next) {
        CurNode *cn = &s_curNodes[j];
        m++;
        cn->org = drawOrigin(&s_out, &cl->cmds[cn->index], cn->at) ? 1 : -1;
        if (cn->org < 0) {
            continue;
        }
        for (int32_t k = s->head; k >= 0; k = s_nodes[k].next) {
            if (s_nodes[k].out < 0 && s_nodes[k].org > 0 && sameOrigin(s_nodes[k].at, cn->at)) {
                cn->pick = k;
                s_nodes[k].out = 0;
                same++;
                break;
            }
        }
    }
    for (int32_t k = s->head; k >= 0; k = s_nodes[k].next) {
        s_nodes[k].out = -1;
    }
    if (same == 0) {
        same = alignSlot(s, m);
    } else {
        pairLeftovers(s); /* the still instances' leftovers */
    }
    /* o: the ordinal's pick */
    for (int32_t j = s->curHead, o = s->head; j >= 0; j = s_curNodes[j].next) {
        if (same == 0) { /* none still or aligned: the ordinal pairing */
            s_curNodes[j].pick = o;
        } else {
            s_placed += s_curNodes[j].pick != o;
        }
        o = o >= 0 ? s_nodes[o].next : -1;
    }
    /* prev's draws no draw of cur picked (matchOf's reason for a draw of
     * cur left unpaired) */
    s->spare = s->count;
    for (int32_t j = s->curHead; j >= 0; j = s_curNodes[j].next) {
        s->spare -= s_curNodes[j].pick >= 0 && s->spare > 0;
    }
}

/* matchOf's last answer: why it found no match (RD_UNMATCHED_*), and
 * whether its match is a draw of another place (CurNode.apart) */
static int s_unWhy;

static bool s_matchApart;

/* the next unmatched occurrence in prev of cur's draw c, index i in list l */
static const RdCmd *matchOf(const RdFrame *prev, const RdCmd *c, int l, uint32_t i)
{
    s_matchApart = false;
    s_unWhy = RD_UNMATCHED_ABSENT;
    Slot *s = slotFind(c, l, false);
    if (!s) {
        return NULL;
    }
    if (s->curCursor >= 0 && s_curNodes[s->curCursor].index == i) {
        if (!s->placed) {
            placeSlot(prev, s);
        }
        const int32_t k = s_curNodes[s->curCursor].pick;
        s_matchApart = s_curNodes[s->curCursor].apart != 0;
        s->curCursor = s_curNodes[s->curCursor].next;
        if (k < 0) {
            /* pairLeftovers leaves none while a draw of prev is free */
            s_unWhy = s->spare ? RD_UNMATCHED_UNPLACED : RD_UNMATCHED_FEWER;
            s_matchApart = false;
            return NULL;
        }
        s_nodes[k].out = (int32_t)i;
        return &prev->lists[s_nodes[k].list].cmds[s_nodes[k].index];
    }
    if (s->cursor < 0) {
        s_unWhy = RD_UNMATCHED_FEWER;
        return NULL;
    }
    Node *nd = &s_nodes[s->cursor];
    s->cursor = nd->next;
    nd->out = (int32_t)i;
    return &prev->lists[nd->list].cmds[nd->index];
}

/* ------------------------------------------------------------ per draw */

enum { R_LERP = 0, R_MISMATCH, R_JUMP };

/* the last R_MISMATCH's RD_MISMATCH_*, and whether the last R_LERP of a
 * mesh blended its model matrices or bones as rotations */
static int s_why;

static int s_rotated;

static double s_drawTurn; /* the largest turn of the last blended draw, degrees */

static int mismatch(int why)
{
    s_why = why;
    return R_MISMATCH;
}

static const void *payloadAt(const RdFrame *f, uint32_t off, uint32_t size)
{
    return off <= f->payloadSize && size <= f->payloadSize - off ? f->payload + off : NULL;
}

/* two mesh ids a draw may blend across: the same mesh, or meshes of one
 * layout (a morphing part's two packets, which the game draws in alternate
 * frames; a mesh rebuilt after an eviction).  A replaced mesh
 * (rd_create_vu_mesh_replacement) keeps its id for as long as it lives, so a
 * part drawn with its replacement in both frames blends as itself; across a
 * pack switch (rd_vu_mesh_retire, a new id) the layouts usually differ and the
 * draw falls back to no blend, as after any change of vertex count. */
static bool sameMesh(uint32_t a, uint32_t b)
{
    if (a == b) {
        return true;
    }
    const RdMeshRec *x = rd__mesh_rec(a), *y = rd__mesh_rec(b);
    return x && y && x->vu && y->vu && x->vertexCount == y->vertexCount &&
           x->qwPerVertex == y->qwPerVertex && x->batchCount == y->batchCount &&
           x->indexCount == y->indexCount;
}

/* The grid draw blendVu is given samples a target (RD_TEXKIND_TARGET) under
 * its state: its STs are screen positions.  pool.c makes the pool surface's
 * STs (its refraction and reflection grids, sampling the scene copied into
 * a work target) by projecting each vertex with the tick's camera
 * (pool.c:549-550, 590-591).  The camera and the copied scene blend, so
 * with cur's STs the image under the water would stand a tick ahead of the
 * scene and jump back at every tick; blended with the positions it follows
 * the blended camera.  A grid with an image keeps cur's STs (its texture's
 * own coordinates, the UV scroll in qw 2 blending instead). */
static bool s_gridScreenSt;

static int s_gridSt; /* grids whose STs blended (s_gridScreenSt), this present */

static int blendVu(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    s_rotated = 0;
    s_drawTurn = 0.0;
    if (pc->u[2] != cc->u[2]) {
        return mismatch(RD_MISMATCH_SIZE);
    }
    /* the program must match; the MSCAL code and clip mode (b[1], b[2])
     * change when a part crosses the edge of the screen (REGION against
     * SCISSOR, reg_clipPacketBoundingBox) and say nothing about the data */
    if (pc->b[0] != cc->b[0] || vuView(pc) != vuView(cc) ||
        (rd__s2_legacy() && (pc->b[1] != cc->b[1] || pc->b[2] != cc->b[2]))) {
        return mismatch(RD_MISMATCH_STATE);
    }
    if (!sameMesh(pc->u[0], cc->u[0])) {
        return mismatch(RD_MISMATCH_MESH);
    }
    const uint8_t *pp = payloadAt(prev, pc->u[1], pc->u[2]);
    if (!pp || cc->u[2] < sizeof(RdVuPayload) + sizeof(RdVuBlock)) {
        return mismatch(RD_MISMATCH_SIZE);
    }
    RdVuPayload hp, hc;
    memcpy(&hp, pp, sizeof(hp));
    memcpy(&hc, op, sizeof(hc));
    if (hp.firstBatch != hc.firstBatch || hp.batchCount != hc.batchCount ||
        hp.boneQw != hc.boneQw || hp.streamQw != hc.streamQw ||
        hp.vertsPerBatch != hc.vertsPerBatch || hp.qwPerVertex != hc.qwPerVertex ||
        hp.materialCount != hc.materialCount) {
        return mismatch(RD_MISMATCH_HEADER);
    }
    const float (*vp)[4] = (const float (*)[4])(const void *)(pp + sizeof(RdVuPayload));
    float (*vo)[4] = (float (*)[4])(void *)(op + sizeof(RdVuPayload));
    float vc[36][4];
    memcpy(vc, vo, sizeof(vc));
    const uint32_t after = (uint32_t)(sizeof(RdVuPayload) + sizeof(RdVuBlock));
    const float (*bp)[4] = (const float (*)[4])(const void *)(pp + after);
    float (*bo)[4] = (float (*)[4])(void *)(op + after);
    const float (*sp)[4] = bp + hp.boneQw;
    float (*so)[4] = bo + hc.boneQw;

    /* the jump tests, on cur's data before it is blended */
    switch (cc->type) {
    case RDC_MESH:
        /* a part locked to the camera has no place in the world */
        if (isNormalProg(cc->b[0]) && vuView(cc) != RD_VU_VIEW_LOCKED &&
            worldJump(vp, (const float (*)[4])vc)) {
            return R_JUMP;
        }
        break;
    case RDC_SKINNED:
        if (hc.boneQw >= 4 && fabsf(bp[3][3] - 1.0f) < 1e-3f && fabsf(bo[3][3] - 1.0f) < 1e-3f &&
            dist3(bp[3], bo[3]) > RD_INTERP_JUMP_WORLD) {
            return R_JUMP;
        }
        break;
    case RDC_GRID:
    case RDC_PARTICLES:
        if (worldJump(vp, (const float (*)[4])vc)) {
            return R_JUMP;
        }
        break;
    default:
        break;
    }

    /* both ticks' blocks through the blended camera (the camera parts
     * then agree; only the object's own motion is left to blend) */
    float vpr[36][4];
    if (s_cam.on && !s_cam.still && camOf(vp, s_cam.vp, NULL) != CAM_NONE &&
        camOf((const float (*)[4])vc, s_cam.vc, s_cam.pc) != CAM_NONE) {
        const int mats = camModelMats(cc->type, cc->b[0]);
        float vcr[36][4];
        memcpy(vpr, vp, sizeof(vpr));
        memcpy(vcr, vc, sizeof(vcr));
        /* the projections blend with the block (linear in them) */
        const int view = vuView(cc);
        if (camRebase(vpr, s_cam.ep, mats, NULL, s_cam.vp, view) &&
            camRebase(vcr, s_cam.ec, mats, NULL, s_cam.vc, view)) {
            memcpy(vc, vcr, sizeof(vc));
            memcpy(vo[4], vc[4], 24 * sizeof(vo[0])); /* qw 4..27 */
            vp = (const float (*)[4])vpr;
            s_rebased++;
        }
    }
    /* prev's lights in the slots of the lights of cur they pair with, before
     * the blend and the light's turn (pairLights, rotateLight) */
    float vpl[36][4];
    if (isLitDraw(cc->type, cc->b[0])) {
        memcpy(vpl, vp, sizeof(vpl));
        if (pairLights(vpl, (const float (*)[4])vc, cc->type != RDC_SKINNED)) {
            vp = (const float (*)[4])vpl;
            s_lightPair++;
        }
    }
    lerpVuBlock(vo, vp, (const float (*)[4])vc, t, hc.scroll);
    /* a turning object keeps its size half way */
    const bool rot = t > 0.0f && t < 1.0f && !rd__s2_legacy();
    if (cc->type == RDC_MESH && isNormalProg(cc->b[0]) && rot) {
        double centre[3];
        const bool hasCentre = meshCentroid(cc->u[0], centre);
        s_rotated |= rotateModel(vo, vp, (const float (*)[4])vc, t, hasCentre ? centre : NULL,
                                 cc->b[0] == RD_PROG_LIT || cc->b[0] == RD_PROG_LIT_SPEC);
        s_drawTurn = fmax(s_drawTurn, s_turnDeg);
    }
    if (hc.boneQw) {
        float tmp[4], cb[4][4];
        uint32_t q = 0;
        if (rot && cc->type == RDC_SKINNED) {
            skinPivots(cc->u[0]);
        }
        for (; rot && cc->type == RDC_SKINNED && q + 4 <= hc.boneQw; q += 4) {
            memcpy(cb, bo[q], sizeof(cb));
            if (sameQw4(bp + q, (const float (*)[4])cb, 0)) {
                continue;
            }
            const double *pv = q / 4u < RD_INTERP_BONES ? s_pivot[q / 4u] : NULL;
            if (pv && pv[3] > 0.0 && rotateBone(bo + q, bp + q, (const float (*)[4])cb, t, pv)) {
                s_rotated = 1;
                s_drawTurn = fmax(s_drawTurn, s_turnDeg);
                continue;
            }
            for (int k = 0; k < 4; k++) {
                lerpFloats(bo[q + k], bp[q + k], cb[k], 4, t);
            }
        }
        for (; q < hc.boneQw; q++) {
            memcpy(tmp, bo[q], sizeof(tmp));
            lerpFloats(bo[q], bp[q], tmp, 4, t);
        }
    }
    if (cc->type == RDC_GRID && hc.streamQw) {
        /* per strip: the VIF qword, tag, colour, stripLen vertices, MSCNT */
        const uint32_t qpv = hc.qwPerVertex, len = hc.vertsPerBatch;
        const uint32_t per = len * qpv + 4;
        const uint32_t lerpQw = qpv == RD_VU_QW_GRID_LIT ? 2u : 1u; /* pos (, normal) */
        /* the ST, the vertex's last qword, too when it is a projection of the
         * vertex through the tick's camera into a target the frame drew
         * (s_gridScreenSt: the pool's refraction and reflection) */
        const bool st = s_gridScreenSt && qpv >= 2u;
        for (uint32_t s = 0; s < hc.batchCount && (s + 1) * per <= hc.streamQw; s++) {
            for (uint32_t k = 0; k < len; k++) {
                const uint32_t at = s * per + 3 + k * qpv;
                float tmp[4];
                for (uint32_t q = 0; q < qpv; q++) {
                    if (q >= lerpQw && !(st && q == qpv - 1u)) {
                        continue;
                    }
                    memcpy(tmp, so[at + q], sizeof(tmp));
                    lerpFloats(so[at + q], sp[at + q], tmp, 4, t);
                }
            }
        }
        s_gridSt += st;
    } else if (cc->type == RDC_PARTICLES && hc.streamQw >= 6) {
        /* 6 header qwords, then (x, y, z, size), (u, v, grey, alpha) each */
        const uint32_t n = (hc.streamQw - 6) / 2;
        for (uint32_t i = 0; i < n; i++) {
            const float *a = sp[6 + 2 * i], *ap = sp[7 + 2 * i];
            float *b = so[6 + 2 * i];
            const float *bq = so[7 + 2 * i];
            const float size = fmaxf(fabsf(a[3]), fabsf(b[3]));
            if (ap[3] == 0.0f || bq[3] == 0.0f || dist3(a, b) > 4.0f * size) {
                continue;
            }
            float tmp[4];
            memcpy(tmp, b, sizeof(tmp));
            lerpFloats(b, a, tmp, 4, t);
        }
    }
    return R_LERP;
}

static int blendScreen(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    if (pc->u[1] != cc->u[1]) {
        return mismatch(RD_MISMATCH_SIZE);
    }
    if (pc->b[0] != cc->b[0] || pc->b[1] != cc->b[1] || pc->b[2] != cc->b[2]) {
        return mismatch(RD_MISMATCH_STATE);
    }
    const uint32_t n = cc->u[1];
    const RdScreenVtx *p = payloadAt(prev, pc->u[0], n * (uint32_t)sizeof(RdScreenVtx));
    if (!p) {
        return mismatch(RD_MISMATCH_SIZE);
    }
    RdScreenVtx *o = (RdScreenVtx *)(void *)op;
    if (vtxJump(p, o, n)) {
        return R_JUMP;
    }
    /* both projected by their tick's camera: blended in the world */
    const bool world = s_rp.on && frameProjected(pc) && frameProjected(cc);
    for (uint32_t i = 0; i < n; i++) {
        if (!world || !reprojBlend(&o[i], &p[i], t, cc->b[2] == 0)) {
            o[i].x = lerpI(p[i].x, o[i].x, t);
            o[i].y = lerpI(p[i].y, o[i].y, t);
            o[i].z = lerpU(p[i].z, o[i].z, t);
        }
        for (int k = 0; k < 4; k++) {
            o[i].rgba[k] = lerpB(p[i].rgba[k], o[i].rgba[k], t);
        }
    }
    s_reproj += world;
    return R_LERP;
}

/* A deferred text item or op.  An item blends its anchor (grid
 * units: a jump beyond RD_INTERP_JUMP_SCREEN of them snaps, about the screen
 * prims' limit), its glow stretch and its colour; its string, size and flags
 * stay cur's and must match prev's.  An op blends its colour and level, as
 * the fade and letterbox sprites do. */
static int blendText(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    if (pc->b[0] != cc->b[0] || pc->b[1] != cc->b[1] || pc->u[2] != cc->u[2]) {
        return mismatch(RD_MISMATCH_STATE);
    }
    const uint8_t *p = payloadAt(prev, pc->u[1], pc->u[2]);
    if (!p) {
        return mismatch(RD_MISMATCH_SIZE);
    }
    if (cc->b[0] == RD_OTEXT_OP) {
        RdTextOp a, o;
        memcpy(&a, p, sizeof(a));
        memcpy(&o, op, sizeof(o));
        for (int k = 0; k < 4; k++) {
            o.rgba[k] = lerpB(a.rgba[k], o.rgba[k], t);
        }
        o.fix = lerpB(a.fix, o.fix, t);
        memcpy(op, &o, sizeof(o));
        return R_LERP;
    }
    RdTextItem a, o;
    memcpy(&a, p, sizeof(a));
    memcpy(&o, op, sizeof(o));
    if (strncmp(a.utf8, o.utf8, RD_TEXT_BYTES) != 0 || !sameBits(a.size, o.size) ||
        a.flags != o.flags || a.additive != o.additive || a.hasXf != o.hasXf) {
        return mismatch(RD_MISMATCH_HEADER);
    }
    if (fabsf(a.x - o.x) > RD_INTERP_JUMP_SCREEN || fabsf(a.y - o.y) > RD_INTERP_JUMP_SCREEN) {
        return R_JUMP;
    }
    o.x = lerpF(a.x, o.x, t);
    o.y = lerpF(a.y, o.y, t);
    if (o.hasXf) {
        lerpFloats(o.xf, a.xf, o.xf, 6, t);
    }
    for (int k = 0; k < 4; k++) {
        o.rgba[k] = lerpB(a.rgba[k], o.rgba[k], t);
    }
    memcpy(op, &o, sizeof(o));
    return R_LERP;
}

/* The shadow volumes whose topology changed this tick (rd_shadow_tris:
 * the silhouette gains or loses edges as the caster animates, in most ticks
 * for a walking character) are not snapped to cur's, which would put the
 * shadow a tick ahead of its caster in every other tick: cur's volume is
 * moved by (1 - t) of the shift between the two volumes, the difference of
 * their vertices' medians in X, Y and Z (robust to the few triangles that
 * came or went). */
static int s_shifted;

static int *s_med;

static uint32_t s_medCap;

static int cmpInt(const void *a, const void *b)
{
    const int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/* the median of field k (0 x, 1 y, 2 z) of n screen vertices */
static bool medianOf(const RdScreenVtx *v, uint32_t n, int k, double *out)
{
    if (n == 0) {
        return false;
    }
    if (n > s_medCap) {
        int *p = realloc(s_med, (size_t)n * sizeof(int));
        if (!p) {
            return false;
        }
        s_med = p;
        s_medCap = n;
    }
    for (uint32_t i = 0; i < n; i++) {
        s_med[i] = k == 0 ? v[i].x : (k == 1 ? v[i].y : (int)(v[i].z >> 1));
    }
    qsort(s_med, n, sizeof(int), cmpInt);
    *out = (double)s_med[n / 2] * (k == 2 ? 2.0 : 1.0);
    return true;
}

static int shiftShadow(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    const uint32_t np = pc->u[0] + pc->u[3], nc = cc->u[0] + cc->u[3];
    const RdScreenVtx *p = payloadAt(prev, pc->u[1], np * (uint32_t)sizeof(RdScreenVtx));
    RdScreenVtx *o = (RdScreenVtx *)(void *)op;
    double mp[3], mc[3];
    for (int k = 0; k < 3; k++) {
        if (!p || !medianOf(p, np, k, &mp[k]) || !medianOf(o, nc, k, &mc[k])) {
            return mismatch(RD_MISMATCH_TOPOLOGY);
        }
    }
    const double lim = RD_INTERP_JUMP_SCREEN * 16.0;
    if (fabs(mp[0] - mc[0]) > lim || fabs(mp[1] - mc[1]) > lim) {
        return R_JUMP;
    }
    const double w = 1.0 - (double)t;
    const int32_t dx = (int32_t)floor(w * (mp[0] - mc[0]) + 0.5);
    const int32_t dy = (int32_t)floor(w * (mp[1] - mc[1]) + 0.5);
    const double dz = floor(w * (mp[2] - mc[2]) + 0.5);
    for (uint32_t i = 0; i < nc; i++) {
        o[i].x += dx;
        o[i].y += dy;
        const double z = (double)o[i].z + dz;
        o[i].z = z < 0.0 ? 0u : (z > 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)z);
    }
    s_shifted = 1;
    return R_LERP;
}

/* Shadow.c's volumes blended prism by prism.  A volume is one closed prism
 * per caster triangle (emitVolumeStrip: ten strip positions over six
 * vertices, eight triangles), each triangle counted +1 or -1 by its facing.
 * rd_shadow_tris sorts the triangles into increments and decrements, so two
 * ticks' volumes of equal counts do not pair triangle for triangle once a
 * face has changed sides (a vertex lerp would join faces of different prisms
 * and the count would not net to 0 outside the shadow), and volumes of
 * different counts (prisms come and go as triangles turn from the light)
 * would take cur's shape moved by the median shift, a tick ahead of the
 * caster.  Here the tags (rd_internal.h, RD_SHADOW_PRISM_TRIS) regroup each
 * tick's prisms; the two sequences, which keep the caster's triangle order,
 * are aligned by dynamic programming (a pair costs the mean square distance
 * of the top caps, the caster triangle, after the volume's median shift, and
 * needs the same strip sign; RD_INTERP_PRISM_GAP prices an unmatched prism);
 * a pair is blended vertex by vertex and its eight triangles are given the
 * signs of their blended facing, the rule emitVolumeStrip applies (plus where
 * W s0 (-1)^i > 0, W the triangle's winding, i its strip position, s0 the
 * strip's sign; the second triangle of each side quad takes the first's), so
 * every blended prism is closed again.  The prisms of one tick only are those
 * of the nearer tick (prev's below t = 0.5, cur's from it), moved by the
 * volume's shift. */
typedef struct ShPrism {
    RdScreenVtx v[6]; /* emitVolumeStrip's vi[0..5]: the top cap 0..2, the bottom 3..5 */
    int8_t plus[8];   /* the recorded signs of its triangles (1: increment) */
    int8_t s0;        /* the strip sign (0: every triangle degenerate) */
    int32_t match;    /* the other tick's prism, -1 */
} ShPrism;

/* emitVolumeStrip's order[0..9]: the vertex at each strip position */
static const uint8_t kStripVtx[10] = {0, 1, 3, 4, 5, 1, 2, 0, 5, 3};

static ShPrism *s_prism[2];

static uint32_t s_prismCap[2];

static const RdScreenVtx **s_byTag;

static int8_t *s_tagSign;

static uint32_t s_byTagCap, s_tagSignCap;

static uint8_t *s_dpWay;

static size_t s_dpCap;

static double *s_dpRow;

static uint32_t s_dpRowCap;

static RdScreenVtx *s_prismOut;

static int8_t *s_prismOutSign;

static uint32_t s_prismOutCap, s_prismOutSignCap;

static double *s_sel;

static uint32_t s_selCap;

static double *s_wpt;

static uint32_t s_wptCap;

static void prismFree(void)
{
    free(s_wpt);
    s_wpt = NULL;
    s_wptCap = 0;
    for (int k = 0; k < 2; k++) {
        free(s_prism[k]);
        s_prism[k] = NULL;
        s_prismCap[k] = 0;
    }
    free(s_byTag);
    free(s_tagSign);
    free(s_dpWay);
    free(s_dpRow);
    free(s_prismOut);
    free(s_prismOutSign);
    free(s_sel);
    s_sel = NULL;
    s_selCap = 0;
    s_byTag = NULL;
    s_tagSign = NULL;
    s_dpWay = NULL;
    s_dpRow = NULL;
    s_prismOut = NULL;
    s_prismOutSign = NULL;
    s_byTagCap = s_tagSignCap = s_dpRowCap = s_prismOutCap = s_prismOutSignCap = 0;
    s_dpCap = 0;
}

static bool growTo(void **p, uint32_t *cap, uint32_t n, size_t size)
{
    if (n <= *cap) {
        return true;
    }
    void *q = realloc(*p, (size_t)n * size);
    if (!q) {
        return false;
    }
    *p = q;
    *cap = n;
    return true;
}

static bool sameXyz(const RdScreenVtx *a, const RdScreenVtx *b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

/* twice the signed area of the triangle a b c, in GS 12.4 units */
static int64_t windingOf(const RdScreenVtx *a, const RdScreenVtx *b, const RdScreenVtx *c)
{
    return ((int64_t)b->x - a->x) * ((int64_t)c->y - a->y) -
           ((int64_t)b->y - a->y) * ((int64_t)c->x - a->x);
}

/* the strip triangle that sets the sign of strip position i (2..9): the
 * second triangle of a side quad (3, 6, 9) takes the first's */
static int signSource(int i)
{
    return i == 3 || i == 6 || i == 9 ? i - 1 : i;
}

/* the signs of a prism's eight triangles from its vertices and s0 */
static void prismSigns(const ShPrism *pr, int8_t *plus)
{
    for (int i = 2; i < 10; i++) {
        const int s = signSource(i);
        const int64_t w =
            windingOf(&pr->v[kStripVtx[s - 2]], &pr->v[kStripVtx[s - 1]], &pr->v[kStripVtx[s]]);
        plus[i - 2] = (int8_t)((s & 1 ? -w : w) * pr->s0 > 0);
    }
}

/* the prisms of a tagged RD_SHADOW_TRIS draw (inc and n in triangles) into
 * s_prism[k]; false when it is not Shadow.c's structure or is untagged */
static bool prismsOf(const RdScreenVtx *v, uint32_t inc, uint32_t n, int k)
{
    if (n == 0 || n % RD_SHADOW_PRISM_TRIS != 0 ||
        !growTo((void **)&s_byTag, &s_byTagCap, n, sizeof(*s_byTag)) ||
        !growTo((void **)&s_tagSign, &s_tagSignCap, n, sizeof(*s_tagSign))) {
        return false;
    }
    const uint32_t np = n / RD_SHADOW_PRISM_TRIS;
    if (!growTo((void **)&s_prism[k], &s_prismCap[k], np, sizeof(ShPrism))) {
        return false;
    }
    memset(s_byTag, 0, n * sizeof(*s_byTag));
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t tag = rd__shadow_tag(&v[i * 3]);
        if (tag == 0 || tag > n || s_byTag[tag - 1]) {
            return false;
        }
        s_byTag[tag - 1] = &v[i * 3];
        s_tagSign[tag - 1] = (int8_t)(i < inc);
    }
    for (uint32_t p = 0; p < np; p++) {
        ShPrism *pr = &s_prism[k][p];
        const RdScreenVtx *const *tri = &s_byTag[p * RD_SHADOW_PRISM_TRIS];
        const RdScreenVtx *pos[10];
        pos[0] = &tri[0][0];
        pos[1] = &tri[0][1];
        for (int j = 0; j < RD_SHADOW_PRISM_TRIS; j++) {
            if (!sameXyz(&tri[j][0], pos[j]) || !sameXyz(&tri[j][1], pos[j + 1])) {
                return false;
            }
            pos[j + 2] = &tri[j][2];
            pr->plus[j] = s_tagSign[p * RD_SHADOW_PRISM_TRIS + j];
        }
        for (int i = 0; i < 10; i++) {
            const RdScreenVtx *seen = &pr->v[kStripVtx[i]];
            if ((i == 5 || i >= 7) && !sameXyz(seen, pos[i])) {
                return false; /* positions 5, 7, 8, 9 repeat vertices 1, 0, 5, 3 */
            }
            pr->v[kStripVtx[i]] = *pos[i];
        }
        for (int i = 0; i < 6; i++) {
            rd__set_shadow_tag(&pr->v[i], 0);
        }
        /* s0 from the triangle of the largest area: plus = W s0 (-1)^i > 0 */
        int64_t best = 0;
        pr->s0 = 0;
        for (int i = 2; i < 10; i++) {
            if (signSource(i) != i) {
                continue;
            }
            const int64_t w = windingOf(pos[i - 2], pos[i - 1], pos[i]);
            const int64_t a = w < 0 ? -w : w;
            if (a > best) {
                best = a;
                const int s = (w > 0 ? 1 : -1) * (i & 1 ? -1 : 1);
                pr->s0 = (int8_t)(pr->plus[i - 2] ? s : -s);
            }
        }
        pr->match = -1;
    }
    return true;
}

/* the cost of pairing prev's prism a with cur's b (shift: cur minus prev),
 * -1 when they cannot pair */
static double prismCost(const ShPrism *a, const ShPrism *b, const double *shift)
{
    if (a->s0 != b->s0) {
        return -1.0;
    }
    const double lim = RD_INTERP_JUMP_SCREEN * 16.0;
    double sum = 0.0;
    for (int i = 0; i < 6; i++) {
        const double dx = (double)b->v[i].x - a->v[i].x, dy = (double)b->v[i].y - a->v[i].y;
        if (fabs(dx) > lim || fabs(dy) > lim) {
            return -1.0;
        }
        if (i < 3) {
            sum += (dx - shift[0]) * (dx - shift[0]) + (dy - shift[1]) * (dy - shift[1]);
        }
    }
    return sum / (3.0 * 256.0); /* square GS pixels */
}

/* the element of rank m / 2 of s_sel[0..m - 1] (m > 0; reordered) */
static double selectMedian(uint32_t m);

/* the median of field k (0 x, 1 y, 2 z) of the six vertices of s_prism[s]'s
 * n prisms, by selection (the volume's shift; a sort of every triangle's
 * vertices cost a millisecond a present) */
static bool prismMedian(int s, uint32_t n, int k, double *out)
{
    const uint32_t m = n * 6;
    if (m == 0 || !growTo((void **)&s_sel, &s_selCap, m, sizeof(double))) {
        return false;
    }
    for (uint32_t p = 0; p < n; p++) {
        for (int i = 0; i < 6; i++) {
            const RdScreenVtx *v = &s_prism[s][p].v[i];
            s_sel[p * 6 + (uint32_t)i] = k == 0 ? v->x : (k == 1 ? v->y : (double)v->z);
        }
    }
    *out = selectMedian(m);
    return true;
}

/* the median of the world points (x, y, z apart) of the six vertices of
 * s_prism[s]'s n prisms through inv (that tick's S^-1), the vertices with
 * no world point left out; false when none has one */
static bool worldMedian(int s, uint32_t n, const double *inv, double out[3])
{
    const uint32_t m = n * 6;
    if (m == 0 || !growTo((void **)&s_wpt, &s_wptCap, m * 3, sizeof(double)) ||
        !growTo((void **)&s_sel, &s_selCap, m, sizeof(double))) {
        return false;
    }
    uint32_t got = 0;
    for (uint32_t p = 0; p < n; p++) {
        for (int i = 0; i < 6; i++) {
            double w;
            got += reprojUnproject(inv, &s_prism[s][p].v[i], &s_wpt[got * 3], &w);
        }
    }
    if (got == 0) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        for (uint32_t j = 0; j < got; j++) {
            s_sel[j] = s_wpt[j * 3 + (uint32_t)k];
        }
        out[k] = selectMedian(got);
    }
    return true;
}

static double selectMedian(uint32_t m)
{
    /* Hoare's selection of the element of rank m / 2 */
    uint32_t a = 0, b = m - 1;
    const uint32_t r = m / 2;
    while (a < b) {
        const double pivot = s_sel[a + (b - a) / 2];
        uint32_t i = a, j = b;
        while (i <= j) {
            while (s_sel[i] < pivot) {
                i++;
            }
            while (s_sel[j] > pivot) {
                j--;
            }
            if (i <= j) {
                const double tmp = s_sel[i];
                s_sel[i] = s_sel[j];
                s_sel[j] = tmp;
                i++;
                if (j == 0) {
                    break;
                }
                j--;
            }
        }
        if (r <= j) {
            b = j;
        } else if (r >= i) {
            a = i;
        } else {
            break;
        }
    }
    return s_sel[r];
}

/* s_prism[0] (np, prev) against s_prism[1] (nc, cur): sets .match; false
 * when the table would be too large.  The path is searched in a band about
 * the diagonal: the difference of the counts and RD_INTERP_PRISM_BAND more
 * (a prism count changes by a few a tick; the full table cost 1.8 ms a
 * present in stage 5's six volumes). */
#define RD_INTERP_PRISM_BAND 16

static bool alignPrisms(uint32_t np, uint32_t nc, const double *shift)
{
    const size_t w = (size_t)nc + 1, cells = ((size_t)np + 1) * w;
    if (cells > (size_t)1 << 22) {
        return false;
    }
    if (cells > s_dpCap) {
        uint8_t *q = realloc(s_dpWay, cells);
        if (!q) {
            return false;
        }
        s_dpWay = q;
        s_dpCap = cells;
    }
    if (!growTo((void **)&s_dpRow, &s_dpRowCap, (uint32_t)(2 * w), sizeof(double))) {
        return false;
    }
    const double gap = RD_INTERP_PRISM_GAP * RD_INTERP_PRISM_GAP;
    /* j - i within [lo, hi] */
    const int64_t d = (int64_t)nc - (int64_t)np;
    const int64_t lo = (d < 0 ? d : 0) - RD_INTERP_PRISM_BAND;
    const int64_t hi = (d > 0 ? d : 0) + RD_INTERP_PRISM_BAND;
    double *row = s_dpRow, *last = s_dpRow + w;

    enum { WAY_PAIR = 0, WAY_PREV, WAY_CUR }; /* WAY_PREV: prev's prism alone */

    for (uint32_t j = 0; j <= nc; j++) {
        last[j] = j <= hi ? gap * j : HUGE_VAL;
        s_dpWay[j] = WAY_CUR;
    }
    for (uint32_t i = 1; i <= np; i++) {
        const int64_t j0 = (int64_t)i + lo < 1 ? 1 : (int64_t)i + lo;
        const int64_t j1 = (int64_t)i + hi > (int64_t)nc ? (int64_t)nc : (int64_t)i + hi;
        for (uint32_t j = 0; j <= nc; j++) {
            row[j] = HUGE_VAL;
        }
        if (-(int64_t)i >= lo) {
            row[0] = gap * i;
        }
        s_dpWay[i * w] = WAY_PREV;
        for (int64_t jj = j0; jj <= j1; jj++) {
            const uint32_t j = (uint32_t)jj;
            double best = last[j] + gap;
            uint8_t way = WAY_PREV;
            if (row[j - 1] + gap < best) {
                best = row[j - 1] + gap;
                way = WAY_CUR;
            }
            const double c = prismCost(&s_prism[0][i - 1], &s_prism[1][j - 1], shift);
            if (c >= 0.0 && last[j - 1] + c <= best) {
                best = last[j - 1] + c;
                way = WAY_PAIR;
            }
            row[j] = best;
            s_dpWay[i * w + j] = way;
        }
        double *tmp = last;
        last = row;
        row = tmp;
    }
    uint32_t i = np, j = nc;
    while (i > 0 || j > 0) {
        const uint8_t way = i == 0 ? WAY_CUR : (j == 0 ? WAY_PREV : s_dpWay[i * w + j]);
        if (way == WAY_PAIR) {
            s_prism[0][i - 1].match = (int32_t)(j - 1);
            s_prism[1][j - 1].match = (int32_t)(i - 1);
            i--;
            j--;
        } else if (way == WAY_PREV) {
            i--;
        } else {
            j--;
        }
    }
    return true;
}

/* space for n vertices in s_out's payload: cur's place when it fits, else
 * appended (the command is pointed there) */
static RdScreenVtx *shadowOut(RdCmd *cc, uint8_t *op, uint32_t n)
{
    const uint32_t had = cc->u[0] + cc->u[3];
    if (n <= had) {
        return (RdScreenVtx *)(void *)op;
    }
    const uint32_t off = (s_out.payloadSize + 15u) & ~15u;
    const uint64_t end = (uint64_t)off + (uint64_t)n * sizeof(RdScreenVtx);
    if (end > UINT32_MAX) {
        return NULL;
    }
    if (end > s_out.payloadCap) {
        const uint64_t cap = end + end / 2;
        uint8_t *p = realloc(s_out.payload, (size_t)(cap > UINT32_MAX ? end : cap));
        if (!p) {
            return NULL;
        }
        s_out.payload = p;
        s_out.payloadCap = (uint32_t)(cap > UINT32_MAX ? end : cap);
    }
    s_out.payloadSize = (uint32_t)end;
    cc->u[1] = off;
    return (RdScreenVtx *)(void *)(s_out.payload + off);
}

/* R_LERP, R_JUMP, or -1 when the volumes are not tagged prisms (the caller
 * then blends them vertex by vertex or, across a topology change, moves
 * cur's by the median shift: shiftShadow) */
static int blendPrisms(uint8_t *op, const RdFrame *prev, const RdCmd *pc, RdCmd *cc, float t)
{
    const uint32_t nvp = pc->u[0] + pc->u[3], nvc = cc->u[0] + cc->u[3];
    const RdScreenVtx *p = payloadAt(prev, pc->u[1], nvp * (uint32_t)sizeof(RdScreenVtx));
    const RdScreenVtx *c = (const RdScreenVtx *)(const void *)op;
    if (!p || nvp % 3 != 0 || nvc % 3 != 0 || !prismsOf(p, pc->u[0] / 3, nvp / 3, 0) ||
        !prismsOf(c, cc->u[0] / 3, nvc / 3, 1)) {
        return -1;
    }
    double mp[3], mc[3];
    const uint32_t np = nvp / 3 / RD_SHADOW_PRISM_TRIS, nc = nvc / 3 / RD_SHADOW_PRISM_TRIS;
    for (int k = 0; k < 3; k++) {
        if (!prismMedian(0, np, k, &mp[k]) || !prismMedian(1, nc, k, &mc[k])) {
            return -1;
        }
    }
    const double lim = RD_INTERP_JUMP_SCREEN * 16.0;
    if (fabs(mp[0] - mc[0]) > lim || fabs(mp[1] - mc[1]) > lim) {
        return R_JUMP;
    }
    const double shift[2] = {mc[0] - mp[0], mc[1] - mp[1]};
    if (!alignPrisms(np, nc, shift)) {
        return -1;
    }
    /* the prisms drawn: every pair, and the nearer tick's unmatched ones */
    const int prevSide = t < 0.5f;
    uint32_t out = 0, alone = 0;
    for (uint32_t j = 0; j < nc; j++) {
        out += s_prism[1][j].match >= 0 || !prevSide;
        alone += s_prism[1][j].match < 0;
    }
    for (uint32_t i = 0; i < np; i++) {
        out += s_prism[0][i].match < 0 && prevSide;
        alone += s_prism[0][i].match < 0;
    }
    const uint32_t nv = out * RD_SHADOW_PRISM_TRIS * 3;
    if (!growTo((void **)&s_prismOut, &s_prismOutCap, nv, sizeof(RdScreenVtx)) ||
        !growTo((void **)&s_prismOutSign, &s_prismOutSignCap, out * RD_SHADOW_PRISM_TRIS, 1)) {
        return -1;
    }
    /* the moves of a prism of one tick only: to its place at t */
    const double w = prevSide ? (double)t : (double)t - 1.0;
    const int32_t dx = (int32_t)floor(w * (mc[0] - mp[0]) + 0.5);
    const int32_t dy = (int32_t)floor(w * (mc[1] - mp[1]) + 0.5);
    const double dz = floor(w * (mc[2] - mp[2]) + 0.5);
    /* both volumes projected by their tick's camera: the prisms blend in
     * the world and are seen through the blended camera (the floor they
     * fall on is), a prism of one tick only moved by the volume's shift in
     * the world; a vertex with no world point keeps the screen-space rule */
    const bool world = s_rp.on && frameProjected(pc) && frameProjected(cc);
    double wp[3], wc[3], wd[3];
    const bool wShift = world && worldMedian(0, np, s_rp.ip, wp) && worldMedian(1, nc, s_rp.ic, wc);
    for (int k = 0; k < 3; k++) {
        wd[k] = wShift ? w * (wc[k] - wp[k]) : 0.0;
    }
    uint32_t inc = 0, o = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t q = 0; q < (pass ? np : nc); q++) {
            const ShPrism *src = &s_prism[pass ? 0 : 1][q];
            if (pass == 1 && src->match >= 0) {
                continue; /* drawn as cur's pair */
            }
            if (src->match < 0 && (pass == 1) != prevSide) {
                continue; /* the farther tick's */
            }
            ShPrism b = *src;
            int8_t plus[8];
            memcpy(plus, src->plus, sizeof(plus));
            if (src->match >= 0) {
                const ShPrism *a = &s_prism[0][src->match];
                bool moved = false;
                for (int k = 0; k < 6; k++) {
                    if (!world || !reprojBlend(&b.v[k], &a->v[k], t, false)) {
                        b.v[k].x = lerpI(a->v[k].x, src->v[k].x, t);
                        b.v[k].y = lerpI(a->v[k].y, src->v[k].y, t);
                        b.v[k].z = lerpU(a->v[k].z, src->v[k].z, t);
                    }
                    moved |= !sameXyz(&b.v[k], &src->v[k]);
                }
                if (moved && b.s0 != 0) {
                    prismSigns(&b, plus);
                }
            } else {
                bool turned = false;
                for (int k = 0; k < 6; k++) {
                    if (wShift && reprojVertex(pass ? s_rp.ip : s_rp.ic, &b.v[k], wd, false)) {
                        turned = true;
                        continue;
                    }
                    b.v[k].x += dx;
                    b.v[k].y += dy;
                    const double z = (double)b.v[k].z + dz;
                    b.v[k].z = z < 0.0 ? 0u : (z > 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)z);
                }
                if (turned && b.s0 != 0) {
                    prismSigns(&b, plus); /* the faces' facing through the blended camera */
                }
            }
            for (int i = 2; i < 10; i++) {
                const uint32_t n = o * RD_SHADOW_PRISM_TRIS + (uint32_t)i - 2;
                RdScreenVtx *tri = &s_prismOut[(size_t)n * 3];
                tri[0] = b.v[kStripVtx[i - 2]];
                tri[1] = b.v[kStripVtx[i - 1]];
                tri[2] = b.v[kStripVtx[i]];
                for (int k = 0; k < 3; k++) {
                    rd__set_shadow_tag(&tri[k], n + 1);
                }
                s_prismOutSign[n] = plus[i - 2];
                inc += plus[i - 2] != 0;
            }
            o++;
        }
    }
    RdScreenVtx *dst = shadowOut(cc, op, nv);
    if (!dst) {
        return -1;
    }
    const uint32_t tris = out * RD_SHADOW_PRISM_TRIS;
    uint32_t a = 0, d = inc;
    for (uint32_t k = 0; k < tris; k++) {
        const uint32_t at = s_prismOutSign[k] ? a++ : d++;
        memcpy(&dst[(size_t)at * 3], &s_prismOut[(size_t)k * 3], 3 * sizeof(RdScreenVtx));
    }
    cc->u[0] = inc * 3;
    cc->u[3] = (tris - inc) * 3;
    s_shifted = alone > 0;
    s_reproj += world;
    return R_LERP;
}

static int blendShadow(uint8_t *op, const RdFrame *prev, const RdCmd *pc, RdCmd *cc, float t)
{
    if (pc->b[0] != cc->b[0]) {
        return mismatch(RD_MISMATCH_TOPOLOGY);
    }
    if (cc->b[0] == RD_SHADOW_TRIS && !rd__s2_legacy()) {
        const int r = blendPrisms(op, prev, pc, cc, t);
        if (r >= 0) {
            return r;
        }
    }
    if (pc->u[0] != cc->u[0] || pc->u[2] != cc->u[2] || pc->u[3] != cc->u[3]) {
        if (cc->b[0] == RD_SHADOW_TRIS && !rd__s2_legacy()) {
            return shiftShadow(op, prev, pc, cc, t);
        }
        return mismatch(RD_MISMATCH_TOPOLOGY); /* the volume's topology changed */
    }
    if (cc->b[0] == RD_SHADOW_TRIS) {
        const uint32_t n = cc->u[0] + cc->u[3];
        const RdScreenVtx *p = payloadAt(prev, pc->u[1], n * (uint32_t)sizeof(RdScreenVtx));
        if (!p) {
            return mismatch(RD_MISMATCH_SIZE);
        }
        RdScreenVtx *o = (RdScreenVtx *)(void *)op;
        if (vtxJump(p, o, n)) {
            return R_JUMP;
        }
        for (uint32_t i = 0; i < n; i++) {
            o[i].x = lerpI(p[i].x, o[i].x, t);
            o[i].y = lerpI(p[i].y, o[i].y, t);
            o[i].z = lerpU(p[i].z, o[i].z, t);
        }
        return R_LERP;
    }
    /* rd_shadow_strip: float[4] (12.4 X, Y, Z, unused) */
    const uint32_t n = cc->u[0];
    const float (*p)[4] = payloadAt(prev, pc->u[1], n * 16u);
    if (!p) {
        return mismatch(RD_MISMATCH_SIZE);
    }
    float (*o)[4] = (float (*)[4])(void *)op;
    for (uint32_t i = 0; i < n; i++) {
        if (fabsf(p[i][0] - o[i][0]) > RD_INTERP_JUMP_SCREEN * 16.0f ||
            fabsf(p[i][1] - o[i][1]) > RD_INTERP_JUMP_SCREEN * 16.0f) {
            return R_JUMP;
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        float tmp[4];
        memcpy(tmp, o[i], sizeof(tmp));
        lerpFloats(o[i], p[i], tmp, 3, t);
    }
    return R_LERP;
}

/* The payload of a keyed draw in s_out (cur's copy) */
static uint8_t *outPayload(const RdCmd *c)
{
    uint32_t off, size;
    switch (c->type) {
    case RDC_SCREEN:
        off = c->u[0];
        size = c->u[1] * (uint32_t)sizeof(RdScreenVtx);
        break;
    case RDC_SHADOW_STRIP:
        off = c->u[1];
        size = c->b[0] == RD_SHADOW_TRIS ? (c->u[0] + c->u[3]) * (uint32_t)sizeof(RdScreenVtx)
                                         : c->u[0] * 16u;
        break;
    default:
        off = c->u[1];
        size = c->u[2];
        break;
    }
    return (uint8_t *)payloadAt(&s_out, off, size);
}

/* ---------------------------------------------------- morphing meshes */

/* A mesh draw names its mesh by id and the replay reads the mesh's live
 * stream, which rd_update_vu_mesh (the morph path) may have rewritten for a
 * frame recorded after cur.  A draw whose stream in cur (or, matched and
 * blended, in prev) differs from the live one is given a scratch mesh of the
 * same layout holding cur's stream, its vertex positions (qw 0) and normals
 * (qw 1, the lit and skinned layouts) blended from prev's (rd__mesh_stream_at:
 * rd_mesh.c keeps what the retained frames drew).  At most RD_INTERP_SCRATCH
 * draws a present; more keep the live stream (a scratch mesh holds a copy of
 * its mesh's stream, indices and batches, s_scratchBytes, logged).  A mesh
 * rewritten more than once while a frame records keeps the version a retained
 * frame drew (the first rewrite keeps it: rd_mesh.c keepVersion), and the
 * frame takes the last. */
#define RD_INTERP_SCRATCH 256

/* the bytes the scratch meshes of the last present hold */
static uint64_t s_scratchBytes;

static uint32_t s_scratch[RD_INTERP_SCRATCH];

static uint32_t s_scratchUsed;

/* the mesh draws morphDraw saw blended, in list order */
static struct {
    uint32_t list, index;
} *s_done;

static uint32_t s_doneCount, s_doneCap;

static void blendedMesh(uint32_t list, uint32_t index)
{
    if (s_doneCount == s_doneCap) {
        const uint32_t cap = s_doneCap ? s_doneCap * 2 : 256;
        void *p = realloc(s_done, (size_t)cap * sizeof(*s_done));
        if (!p) {
            return; /* the later pass sees it again: cur's stream, unblended */
        }
        s_done = p;
        s_doneCap = cap;
    }
    s_done[s_doneCount].list = list;
    s_done[s_doneCount].index = index;
    s_doneCount++;
}

/* rd_shutdown frees the meshes (rd__mesh_shutdown); the ids are forgotten */
static void scratchReset(void)
{
    memset(s_scratch, 0, sizeof(s_scratch));
    s_scratchUsed = 0;
    free(s_done);
    s_done = NULL;
    s_doneCount = s_doneCap = 0;
}

static RdMeshRec *scratchFor(const RdMeshRec *c, const float (*stream)[4])
{
    if (s_scratchUsed >= RD_INTERP_SCRATCH) {
        return NULL;
    }
    uint32_t *id = &s_scratch[s_scratchUsed];
    RdMeshRec *m = rd__mesh_rec(*id);
    const size_t size = (size_t)c->vertexCount * c->qwPerVertex * 16;
    if (m && m->vertexCount == c->vertexCount && m->qwPerVertex == c->qwPerVertex &&
        m->batchCount == c->batchCount && m->indexCount == c->indexCount) {
        if (size) {
            memcpy(m->stream, stream, size);
        }
        if (c->indexCount) {
            memcpy(m->index, c->index, (size_t)c->indexCount * 4);
        }
        if (c->batchCount) {
            memcpy(m->batches, c->batches, (size_t)c->batchCount * sizeof(RdVuBatchRec));
        }
    } else {
        if (m) {
            rd_destroy_vu_mesh((RdMesh){*id});
        }
        *id = rd__vu_mesh_create_raw(stream, c->vertexCount, c->qwPerVertex, c->index,
                                     c->indexCount, c->batches, c->batchCount, "interp");
        m = rd__mesh_rec(*id);
        if (!m) {
            *id = 0;
            return NULL;
        }
    }
    rd__vu_mesh_copy_draw_index(m, c); /* issue 25: the source's overlap marks */
    m->materialCount = c->materialCount;
    m->srcQw = c->srcQw;
    s_scratchBytes +=
        size + (uint64_t)c->indexCount * 4u + (uint64_t)c->batchCount * sizeof(RdVuBatchRec);
    m->lastUsed = g_rd.frameCounter;
    m->replaySeen = 0; /* upload again */
    m->transient = 1;  /* from the ring, not the device arena */
    s_scratchUsed++;
    return m;
}

/* the draw o (s_out's copy of a cur mesh draw) and its blended match pc in
 * prev (NULL: none, or not blended); true when its stream was replaced */
static bool morphDraw(RdCmd *o, const RdFrame *prev, const RdCmd *pc, const RdFrame *cur, float t)
{
    const RdMeshRec *mc = rd__mesh_rec(o->u[0]);
    if (!mc || !mc->vu) {
        return false;
    }
    const float (*cs)[4] = rd__mesh_stream_at(mc, cur->number);
    if (!cs) {
        cs = (const float (*)[4])mc->stream; /* no kept version: the live stream */
    }
    const size_t size = (size_t)mc->vertexCount * mc->qwPerVertex * 16;
    const float (*ps)[4] = NULL;
    if (pc && prev && t < 1.0f) {
        const RdMeshRec *mp = rd__mesh_rec(pc->u[0]);
        ps = mp ? rd__mesh_stream_at(mp, prev->number) : NULL;
        if (ps == cs || (ps && memcmp(ps, cs, size) == 0)) {
            ps = NULL; /* the same shape: nothing to blend */
        }
    }
    if (!ps && cs == (const float (*)[4])mc->stream) {
        return false;
    }
    RdMeshRec *s = scratchFor(mc, cs);
    if (!s) {
        return false;
    }
    if (ps) {
        const uint32_t qpv = mc->qwPerVertex;
        const uint32_t n = qpv >= 4 ? 2u : 1u; /* pos (, normal) */
        for (uint32_t v = 0; v < mc->vertexCount; v++) {
            for (uint32_t q = 0; q < n; q++) {
                const size_t at = (size_t)v * qpv + q;
                lerpFloats(s->stream[at], ps[at], cs[at], 3, t);
            }
        }
    }
    o->u[0] = s->gen << 16 | (uint32_t)(s - g_rd.meshes + 1);
    return true;
}

/* ------------------------------------------ unmatched CPU-projected draws
 *
 * A screen prim (RDC_SCREEN) or a shadow volume (RDC_SHADOW_STRIP) holds GS
 * positions and cannot be re-based on the blended camera.  One that has no
 * partner in the other tick (a packet culled in one tick, a string that
 * changed, a caster that came or went) would otherwise stand at the tick: a
 * draw of cur only would be drawn whole from t = 0, a tick early, and one of
 * prev only would never be drawn between the ticks, so it would vanish a
 * tick early.  Instead:
 *
 *   a screen prim whose blend fades with its vertex alpha (alphaFades:
 *   ALPHA's C is As and D is Cd, ABE on, PABE off, and the vertex alpha
 *   reaches As: untextured, TCC RGB or MODULATE) is drawn with its alpha
 *   times t (cur's) or 1 - t (prev's): it fades in or out over the interval;
 *
 *   any other screen prim, and every shadow volume, is drawn on the nearer
 *   tick's side of t = 0.5 (prev's below it, cur's from it), whole.  A
 *   shadow volume only counts the stencil (each triangle +1 or -1, then
 *   RDC_SHADOW_RESOLVE darkens every pixel whose count is not 0 by one
 *   shadow colour for all the volumes), so it has no alpha to fade and a
 *   partial volume would leave the count unbalanced: the switch is the rule
 *   blendPrisms already applies to a prism of one tick only.

 *
 * A draw of prev only is inserted into s_out's list after the match of the
 * keyed draw before it in prev's list (else before the match of the one
 * after it), bracketed by state commands that set prev's state for it and
 * restore cur's (stateDiff); a shadow volume only next to a volume (or
 * before the list's RDC_SHADOW_RESOLVE), and only when the colour and depth
 * targets agree.  A draw that cannot be placed is not drawn. */
typedef struct UmAt {
    uint32_t list, index;
} UmAt;

typedef struct UmIns {
    uint32_t list, pos; /* inserted before s_out's command pos */
    uint32_t node;      /* the prev draw's s_nodes entry */
    uint32_t seq;       /* prev's order, the tie break */
    float weight;       /* alpha weight, 1: whole */
    uint32_t cmds;      /* the commands it adds: the bracket and the draw */
    RdStateBlock want;  /* prev's state at the draw */
    RdStateBlock have;  /* cur's at pos */
} UmIns;

typedef struct StQ {
    uint32_t list, pos, slot;
} StQ;

static UmAt *s_umCur;

static uint32_t s_umCurN, s_umCurCap;

static UmIns *s_umIns;

static uint32_t s_umInsN, s_umInsCap;

static StQ *s_umQ;

static uint32_t s_umQCap;

static RdStateBlock *s_umSt, *s_umWant;

static uint32_t s_umStCap, s_umWantCap;

static int32_t *s_umNext;

static uint32_t s_umNextCap;

static RdCmd *s_umCmds;

static uint32_t s_umCmdsCap;

/* this present: faded in, faded out, switched at half way (cur's, prev's),
 * prev's not placed */
static uint32_t s_umFadeIn, s_umFadeOut, s_umHalfIn, s_umHalfOut, s_umHeld;

/* this present: VU draws of prev alone drawn (prevAlone), and those that
 * could not be (not placed, state not reachable, block not re-based) */
static uint32_t s_umPrevKept, s_umPrevHeld;

/* per s_nodes entry: 1 when it is a VU draw of prev alone to draw */
static uint8_t *s_umVu;

static uint32_t s_umVuCap;

static void umFree(void)
{
    free(s_umCur);
    free(s_umIns);
    free(s_umQ);
    free(s_umSt);
    free(s_umNext);
    free(s_umCmds);
    free(s_umWant);
    free(s_umVu);
    s_umVu = NULL;
    s_umVuCap = 0;
    s_umWant = NULL;
    s_umWantCap = 0;
    s_umCur = NULL;
    s_umIns = NULL;
    s_umQ = NULL;
    s_umSt = NULL;
    s_umNext = NULL;
    s_umCmds = NULL;
    s_umCurN = s_umCurCap = s_umInsN = s_umInsCap = s_umQCap = s_umStCap = s_umNextCap = 0;
    s_umCmdsCap = 0;
}

static bool isProjected(const RdCmd *c)
{
    /* a deferred text item fades as its quads do (its alpha) */
    return c->type == RDC_SCREEN || c->type == RDC_SHADOW_STRIP ||
           (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_ITEM);
}

/* an item's alpha times w (the colour and the rest kept) */
static void scaleItemAlpha(uint8_t *payload, float w)
{
    RdTextItem it;
    memcpy(&it, payload, sizeof(it));
    it.rgba[3] = (uint8_t)lrintf((float)it.rgba[3] * w);
    memcpy(payload, &it, sizeof(it));
}

/* a keyed draw of cur with no match in prev (RDC_SCREEN, RDC_SHADOW_STRIP) */
static void umCurOnly(uint32_t list, uint32_t index)
{
    if (!growTo((void **)&s_umCur, &s_umCurCap, s_umCurN + 1, sizeof(UmAt))) {
        return; /* out of memory: it stays whole */
    }
    s_umCur[s_umCurN].list = list;
    s_umCur[s_umCurN].index = index;
    s_umCurN++;
}

/* whether the draw's vertex alpha scales its effect down to nothing: the
 * blend is C = As with D = Cd (GS ALPHA), ABE on and PABE off (with PABE a
 * pixel whose alpha drops under 0x80 is written unblended), and As carries
 * the vertex alpha (untextured, TCC RGB, or MODULATE's At x Af) */
static bool alphaFades(const RdStateBlock *s)
{
    const RdDrawState *d = &s->ds;
    if (!d->abe || d->pabe) {
        return false;
    }
    switch (d->blend) {
    case RD_BLEND_LERP_AS:
    case RD_BLEND_CS_AS_ADD_CD:
    case RD_BLEND_CD_SUB_CS_AS:
    case RD_BLEND_LERP_AS_ALT:
    case RD_BLEND_CD_AS_ADD_CD:
        break;
    default:
        return false;
    }
    return !d->texEnabled || d->tcc == RD_TCC_RGB || d->texFn == RD_TEXFN_MODULATE;
}

static int cmpStQ(const void *a, const void *b)
{
    const StQ *x = a, *y = b;
    if (x->list != y->list) {
        return x->list < y->list ? -1 : 1;
    }
    return (x->pos > y->pos) - (x->pos < y->pos);
}

/* the state of f before command pos of list (every earlier command applied)
 * for each of the n queries q, into out[q.slot]; sorts q */
static void statesAt(const RdFrame *f, StQ *q, uint32_t n, RdStateBlock *out)
{
    qsort(q, n, sizeof(StQ), cmpStQ);
    RdStateBlock st = f->startState;
    uint32_t k = 0;
    for (int l = rd__first_list((int)f->keep); l < RD_LIST_COUNT && k < n; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0;; i++) {
            while (k < n && q[k].list == (uint32_t)l && q[k].pos == i) {
                out[q[k++].slot] = st;
            }
            if (i >= cl->count) {
                break;
            }
            rd__apply_state(&st, &cl->cmds[i]);
        }
    }
    while (k < n) {
        out[q[k++].slot] = st; /* past the replayed lists: not expected */
    }
}

static RdCmd stateCmd(uint8_t type)
{
    RdCmd c;
    memset(&c, 0, sizeof(c));
    c.type = type;
    return c;
}

/* the state commands stateDiff can emit: one per kind it checks (TEST,
 * BLEND, the five bytes, FILTER, WRAP, TEXTURE, TEXTURE_OFF, UVOFFSET,
 * COLORMASK, SCISSOR, SHADE, AA1); out holds this many */
#define RD_STATE_DIFF_MAX 16

/* the state commands that take a to b into out (RD_STATE_DIFF_MAX at
 * most); false when b cannot be reached so (another target, a derived
 * field) */
static bool stateDiff(const RdStateBlock *a, const RdStateBlock *b, RdCmd *out, uint32_t *n)
{
    const RdDrawState *x = &a->ds, *y = &b->ds;
    uint32_t k = 0;
    *n = 0;
    if (a->color != b->color || a->depth != b->depth || a->gsW != b->gsW || a->gsH != b->gsH ||
        a->useOffset != b->useOffset) {
        return false;
    }
    if (memcmp(&x->test, &y->test, sizeof(x->test)) != 0) {
        RdCmd c = stateCmd(RDC_TEST);
        c.b[0] = y->test.ate;
        c.b[1] = y->test.atst;
        c.b[2] = y->test.aref;
        c.b[3] = y->test.afail;
        c.b[4] = y->test.date;
        c.b[5] = y->test.zte;
        c.b[6] = y->test.ztst;
        out[k++] = c;
    }
    if (x->blend != y->blend || x->blendFix != y->blendFix || x->abe != y->abe) {
        RdCmd c = stateCmd(RDC_BLEND);
        c.b[0] = y->blend;
        c.b[1] = y->blendFix;
        c.b[2] = y->abe;
        out[k++] = c;
    }

    static const struct {
        uint8_t type;
        size_t off;
    } bytes[] = {{RDC_ZWRITE, offsetof(RdDrawState, zwrite)},
                 {RDC_FBA, offsetof(RdDrawState, fba)},
                 {RDC_PABE, offsetof(RdDrawState, pabe)},
                 {RDC_COLCLAMP, offsetof(RdDrawState, colclamp)},
                 {RDC_TEXA, offsetof(RdDrawState, texa)}};

    /* each kind is emitted at most once: TEST, BLEND, the bytes, then
     * FILTER, WRAP, TEXTURE, TEXTURE_OFF, UVOFFSET, COLORMASK, SCISSOR,
     * SHADE and AA1, so k stays within out */
    _Static_assert(2 + sizeof(bytes) / sizeof(bytes[0]) + 9 <= RD_STATE_DIFF_MAX,
                   "stateDiff's kinds fit RD_STATE_DIFF_MAX");

    for (size_t i = 0; i < sizeof(bytes) / sizeof(bytes[0]); i++) {
        const uint8_t u = ((const uint8_t *)x)[bytes[i].off],
                      v = ((const uint8_t *)y)[bytes[i].off];
        if (u != v) {
            RdCmd c = stateCmd(bytes[i].type);
            c.b[0] = v;
            out[k++] = c;
        }
    }
    if (x->magFilter != y->magFilter || x->minFilter != y->minFilter) {
        RdCmd c = stateCmd(RDC_FILTER);
        c.b[0] = y->magFilter;
        c.b[1] = y->minFilter;
        out[k++] = c;
    }
    if (x->wrap.s != y->wrap.s || x->wrap.t != y->wrap.t) {
        RdCmd c = stateCmd(RDC_WRAP);
        c.b[0] = y->wrap.s;
        c.b[1] = y->wrap.t;
        out[k++] = c;
    }
    bool texOn = false;
    if (a->tex != b->tex || x->texFn != y->texFn || x->tcc != y->tcc ||
        (y->texEnabled && !x->texEnabled)) {
        RdCmd c = stateCmd(RDC_TEXTURE);
        c.u[0] = b->tex;
        c.b[0] = y->texFn;
        c.b[1] = y->tcc;
        out[k++] = c;
        texOn = true;
    }
    if (!y->texEnabled && (x->texEnabled || texOn)) {
        out[k++] = stateCmd(RDC_TEXTURE_OFF); /* RDC_TEXTURE above turned it on */
    }
    if (memcmp(a->uvOffset, b->uvOffset, sizeof(a->uvOffset)) != 0) {
        RdCmd c = stateCmd(RDC_UVOFFSET);
        c.f[0] = b->uvOffset[0];
        c.f[1] = b->uvOffset[1];
        out[k++] = c;
    }
    if (x->fbmsk != y->fbmsk || x->colorMask != y->colorMask) {
        RdCmd c = stateCmd(RDC_COLORMASK);
        c.u[0] = y->fbmsk;
        out[k++] = c;
    }
    if (memcmp(a->scissor, b->scissor, sizeof(a->scissor)) != 0) {
        RdCmd c = stateCmd(RDC_SCISSOR);
        for (int i = 0; i < 4; i++) {
            c.u[i] = (uint32_t)b->scissor[i];
        }
        out[k++] = c;
    }
    if (a->gouraud != b->gouraud) {
        RdCmd c = stateCmd(RDC_SHADE);
        c.b[0] = (uint8_t)b->gouraud;
        out[k++] = c;
    }
    if (a->aa1 != b->aa1) {
        RdCmd c = stateCmd(RDC_AA1);
        c.b[0] = (uint8_t)b->aa1;
        out[k++] = c;
    }
    /* the commands must give b exactly (colorMask is derived from FBMSK) */
    RdStateBlock s = *a;
    for (uint32_t i = 0; i < k; i++) {
        rd__apply_state(&s, &out[i]);
    }
    *n = k;
    return memcmp(&s, b, sizeof(s)) == 0;
}

/* n bytes appended to s_out's payload (16-aligned), NULL on out of memory */
static uint8_t *outAppend(uint32_t n, uint32_t *off)
{
    const uint32_t at = (s_out.payloadSize + 15u) & ~15u;
    const uint64_t end = (uint64_t)at + n;
    if (end > UINT32_MAX) {
        return NULL;
    }
    if (end > s_out.payloadCap) {
        const uint64_t cap = end + end / 2;
        const uint32_t c32 = (uint32_t)(cap > UINT32_MAX ? end : cap);
        uint8_t *p = realloc(s_out.payload, c32);
        if (!p) {
            return NULL;
        }
        s_out.payload = p;
        s_out.payloadCap = c32;
    }
    s_out.payloadSize = (uint32_t)end;
    *off = at;
    return s_out.payload + at;
}

static void scaleAlpha(RdScreenVtx *v, uint32_t n, float w)
{
    for (uint32_t i = 0; i < n; i++) {
        v[i].rgba[3] = (uint8_t)floorf((float)v[i].rgba[3] * w + 0.5f);
    }
}

static int cmpIns(const void *a, const void *b)
{
    const UmIns *x = a, *y = b;
    if (x->list != y->list) {
        return x->list < y->list ? -1 : 1;
    }
    if (x->pos != y->pos) {
        return x->pos < y->pos ? -1 : 1;
    }
    return (x->seq > y->seq) - (x->seq < y->seq);
}

/* the first RDC_SHADOW_RESOLVE of s_out's list l, or ~0u */
static uint32_t resolveOf(uint32_t l)
{
    for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
        if (s_out.lists[l].cmds[i].type == RDC_SHADOW_RESOLVE) {
            return i;
        }
    }
    return ~0u;
}

/* where prev's unmatched draw of node k goes in s_out, ~0u: nowhere */
static uint32_t umPlace(const RdFrame *prev, uint32_t k, int32_t lastOut)
{
    const Node *nd = &s_nodes[k];
    const RdCmd *pc = &prev->lists[nd->list].cmds[nd->index];
    const RdCmdList *ol = &s_out.lists[nd->list];
    const int32_t at = lastOut >= 0 ? lastOut : s_umNext[k];
    if (pc->type != RDC_SHADOW_STRIP) {
        return at < 0 ? ~0u : (uint32_t)(lastOut >= 0 ? at + 1 : at);
    }
    /* a volume goes between a stencil reset and its resolve: next to a
     * volume, else before the list's resolve */
    if (at >= 0 && (uint32_t)at < ol->count && ol->cmds[at].type == RDC_SHADOW_STRIP) {
        return (uint32_t)(lastOut >= 0 ? at + 1 : at);
    }
    return resolveOf(nd->list);
}

/* ------------------------------------------ VU draws of the tick before
 *
 * A VU draw of prev with no match in cur is not drawn between the ticks
 * unless it went because of the camera: the game culls against the tick's
 * camera, so an object the turning camera leaves behind is drawn in prev and
 * not in cur, and would vanish at the interval's start while the blended
 * camera still sees it.  Such a draw (prevAlone) is drawn for the whole
 * interval, prev's object through the blended camera (camRebase with Vp^-1
 * Vt, and Pt Pp^-1 while the zoom eases), when
 *
 *   the camera moves (s_cam on and not still: a still camera leaves nothing
 *   behind, and the frame stays as it was);
 *   it is a mesh (a normal program's, not locked to the camera), a skinned
 *   mesh, a grid or particles, drawn through the frame's camera (camOf);
 *   no draw of cur in its list has its object and part (the key without
 *   its last byte, RD_KEY's ordinal: n * 4 + pass at RegistPacket.c), so it
 *   is never drawn beside its own object;
 *   a sphere holding it is wholly outside cur's picture (outsideCur): the
 *   model to world matrix (S^-1 M: qw 4..7 inverted times qw 16..19; a
 *   skinned draw's bones) applied to the farthest of its vertices from the
 *   model's origin (the mesh's stream, a grid's or the particles' copied
 *   stream, a particle's size added), tested against the planes of cur's
 *   world to GS screen (Pc Vc) at the picture's edges.
 *
 * It goes in with unmatchedPass's insertion (umPlace, prev's state through
 * stateDiff), with prev's mesh stream (morphDraw); one that cannot be
 * placed, whose state cannot be reached or whose block cannot be re-based
 * is not drawn (held). */
static bool isVuDraw(const RdCmd *c)
{
    return c->type == RDC_MESH || c->type == RDC_SKINNED || c->type == RDC_GRID ||
           c->type == RDC_PARTICLES;
}

static double len3d(const float *v)
{
    return sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);
}

/* the longest of the 3 x 3's columns of a column-major 4 x 4: how far it
 * carries a point at distance 1 from the origin, at most */
static double colScale(const double *m)
{
    double s = 0.0;
    for (int c = 0; c < 3; c++) {
        s = fmax(s, sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] +
                         m[c * 4 + 2] * m[c * 4 + 2]));
    }
    return s;
}

/* a sphere (centre c, radius r, world units) holding draw pc of prev,
 * payload p (see above); false when it cannot be bounded */
static bool prevSphere(const RdFrame *prev, const RdCmd *pc, const uint8_t *p, double c[3],
                       double *r)
{
    const uint32_t after = (uint32_t)(sizeof(RdVuPayload) + sizeof(RdVuBlock));
    RdVuPayload h;
    memcpy(&h, p, sizeof(h));
    const uint32_t extra = (pc->u[2] - after) / 16u;
    if (h.boneQw > extra || h.streamQw > extra - h.boneQw) {
        return false;
    }
    float m[36][4];
    memcpy(m, p + sizeof(RdVuPayload), sizeof(m));
    const float (*bones)[4] = (const float (*)[4])(const void *)(p + after);
    const float (*stream)[4] = bones + h.boneQw;
    double reach = 0.0;
    if (pc->type == RDC_MESH || pc->type == RDC_SKINNED) {
        const RdMeshRec *mr = rd__mesh_rec(pc->u[0]);
        if (!mr || !mr->vu || mr->qwPerVertex == 0) {
            return false;
        }
        const float (*vs)[4] = rd__mesh_stream_at(mr, prev->number);
        if (!vs) {
            vs = (const float (*)[4])mr->stream;
        }
        if (!vs && mr->vertexCount) {
            return false;
        }
        for (uint32_t v = 0; v < mr->vertexCount; v++) {
            reach = fmax(reach, len3d(vs[(size_t)v * mr->qwPerVertex]));
        }
    } else if (pc->type == RDC_GRID) {
        /* per strip: the VIF qword, tag, colour, stripLen vertices, MSCNT */
        const uint32_t qpv = h.qwPerVertex ? h.qwPerVertex : 1u, len = h.vertsPerBatch;
        const uint32_t per = len * qpv + 4u;
        for (uint32_t st = 0; st < h.batchCount && (st + 1u) * per <= h.streamQw; st++) {
            for (uint32_t k = 0; k < len; k++) {
                reach = fmax(reach, len3d(stream[st * per + 3u + k * qpv]));
            }
        }
    } else if (h.streamQw >= 6u) {
        /* 6 header qwords, then (x, y, z, size), (u, v, grey, alpha) each */
        for (uint32_t i = 0; i < (h.streamQw - 6u) / 2u; i++) {
            const float *q = stream[6u + 2u * i];
            reach = fmax(reach, len3d(q) + fabs((double)q[3]));
        }
    }
    if (!isfinite(reach)) {
        return false;
    }
    if (pc->type == RDC_SKINNED) {
        /* the bones are in the world (camModelMats): each one's place and
         * reach */
        if (h.boneQw < 4u) {
            return false;
        }
        double b0[16];
        loadQw4(bones, 0, b0);
        for (int j = 0; j < 3; j++) {
            c[j] = b0[12 + j];
        }
        double rr = 0.0;
        for (uint32_t q = 0; q + 4u <= h.boneQw; q += 4u) {
            double b[16];
            loadQw4(bones, (int)q, b);
            const double dx = b[12] - c[0], dy = b[13] - c[1], dz = b[14] - c[2];
            rr = fmax(rr, sqrt(dx * dx + dy * dy + dz * dz) + reach * colScale(b));
        }
        *r = rr;
    } else {
        double sm[16], is[16], mm[16], w[16];
        loadQw4((const float (*)[4])m, 4, sm);
        loadQw4((const float (*)[4])m, 16, mm);
        if (!invert4d(sm, is)) {
            return false;
        }
        mul4d(is, mm, w); /* the model to world */
        if (!(fabs(w[15]) > 1e-12)) {
            return false;
        }
        for (int j = 0; j < 3; j++) {
            c[j] = w[12 + j] / w[15];
        }
        *r = reach * colScale(w) / fabs(w[15]);
    }
    return isfinite(c[0]) && isfinite(c[1]) && isfinite(c[2]) && isfinite(*r);
}

/* whether the sphere is wholly outside cur's picture: beyond one of the
 * planes of S = Pc Vc (world to GS screen) through the picture's edges,
 * GS 2048 (GsBase.c's centre) +- the half width (gsW / 2 widened by the
 * display's aspect: rd_replay.c's wide x scale g_rd.wideX) and +- gsH / 2,
 * or behind the eye (w <= 0) */
static bool outsideCur(const double c[3], double r)
{
    double sc[16];
    mul4d(s_cam.pc, s_cam.vc, sc);
    const float wide = g_rd.wideX > 0.0f && g_rd.wideX < 1.0f ? g_rd.wideX : 1.0f;
    const double hw = 0.5 * (double)(s_out.gsW ? s_out.gsW : 512u) / (double)wide;
    const double hh = 0.5 * (double)(s_out.gsH ? s_out.gsH : 448u);
    double pl[5][4];
    for (int k = 0; k < 4; k++) {
        const double x = sc[k * 4], y = sc[k * 4 + 1], w = sc[k * 4 + 3];
        pl[0][k] = x - (2048.0 - hw) * w;
        pl[1][k] = (2048.0 + hw) * w - x;
        pl[2][k] = y - (2048.0 - hh) * w;
        pl[3][k] = (2048.0 + hh) * w - y;
        pl[4][k] = w;
    }
    for (int i = 0; i < 5; i++) {
        const double n = sqrt(pl[i][0] * pl[i][0] + pl[i][1] * pl[i][1] + pl[i][2] * pl[i][2]);
        const double d = pl[i][0] * c[0] + pl[i][1] * c[1] + pl[i][2] * c[2] + pl[i][3];
        if (n > 0.0 && d < -r * n) {
            return true;
        }
    }
    return false;
}

/* whether s_out's list l has a keyed draw of pc's object and part */
static bool partInCur(uint32_t l, const RdCmd *pc)
{
    const uint64_t part = ((uint64_t)pc->keyHi << 32 | pc->keyLo) >> 8;
    const RdCmdList *cl = &s_out.lists[l];
    for (uint32_t i = 0; i < cl->count; i++) {
        const RdCmd *c = &cl->cmds[i];
        if (isKeyedDraw(c) && ((uint64_t)c->keyHi << 32 | c->keyLo) >> 8 == part) {
            return true;
        }
    }
    return false;
}

/* whether prev's unmatched draw of node k is a VU draw of prev alone to
 * draw (see above) */
static bool prevAlone(const RdFrame *prev, uint32_t k)
{
    const Node *nd = &s_nodes[k];
    const RdCmd *pc = &prev->lists[nd->list].cmds[nd->index];
    if (!s_cam.on || s_cam.still || !isVuDraw(pc) ||
        pc->u[2] < sizeof(RdVuPayload) + sizeof(RdVuBlock)) {
        return false;
    }
    if (pc->type == RDC_MESH && (!isNormalProg(pc->b[0]) || vuView(pc) == RD_VU_VIEW_LOCKED)) {
        return false;
    }
    const uint8_t *p = payloadAt(prev, pc->u[1], pc->u[2]);
    if (!p) {
        return false;
    }
    float m[36][4];
    memcpy(m, p + sizeof(RdVuPayload), sizeof(m));
    double c[3], r = 0.0;
    return camOfUncached((const float (*)[4])m, s_cam.vp, s_cam.pp) != CAM_NONE &&
           prevSphere(prev, pc, p, c, &r) && outsideCur(c, r) && !partInCur(nd->list, pc);
}

/* prev's VU block m (its copy in s_out) through the blended camera */
static bool rebasePrev(float (*m)[4], const RdCmd *pc)
{
    const int how = camOfUncached((const float (*)[4])m, s_cam.vp, s_cam.pp);
    const bool proj = how == CAM_FULL && s_cam.zoom;
    if (how == CAM_NONE || (proj && !s_cam.zoomPrev)) {
        return false;
    }
    return camRebase(m, s_cam.ep, camModelMats(pc->type, pc->b[0]), proj ? s_cam.lp : NULL,
                     s_cam.vp, vuView(pc));
}

/* the unmatched screen prims and shadow volumes at t (see above), and the
 * VU draws of prev alone (prevAlone); after the blends and the morph pass,
 * which keep command indices */
static void unmatchedPass(const RdFrame *prev, float t)
{
    s_umInsN = 0;
    /* prev's unplaced draws and their anchors: s_nodes is in list order */
    uint32_t cand = 0;
    const bool vuAlone = s_cam.on && !s_cam.still &&
                         growTo((void **)&s_umVu, &s_umVuCap, s_nodeCount ? s_nodeCount : 1u, 1);
    for (uint32_t k = 0; k < s_nodeCount; k++) {
        const Node *nd = &s_nodes[k];
        const RdCmd *pc = &prev->lists[nd->list].cmds[nd->index];
        if (vuAlone) {
            s_umVu[k] = nd->out < 0 && prevAlone(prev, k);
        }
        cand += nd->out < 0 && (isProjected(pc) || (vuAlone && s_umVu[k]));
    }
    if (cand == 0 && s_umCurN == 0) {
        return;
    }
    if (cand && growTo((void **)&s_umNext, &s_umNextCap, s_nodeCount, sizeof(int32_t))) {
        int32_t next = -1;
        uint32_t list = ~0u;
        for (uint32_t k = s_nodeCount; k-- > 0;) {
            if (s_nodes[k].list != list) {
                list = s_nodes[k].list;
                next = -1;
            }
            s_umNext[k] = next;
            if (s_nodes[k].out >= 0) {
                next = s_nodes[k].out;
            }
        }
        int32_t lastOut = -1;
        list = ~0u;
        for (uint32_t k = 0; k < s_nodeCount; k++) {
            const Node *nd = &s_nodes[k];
            if (nd->list != list) {
                list = nd->list;
                lastOut = -1;
            }
            if (nd->out >= 0) {
                lastOut = nd->out;
                continue;
            }
            const bool vu = vuAlone && s_umVu[k];
            if (!vu && !isProjected(&prev->lists[nd->list].cmds[nd->index])) {
                continue;
            }
            const uint32_t pos = umPlace(prev, k, lastOut);
            if (pos == ~0u || pos > s_out.lists[nd->list].count ||
                !growTo((void **)&s_umIns, &s_umInsCap, s_umInsN + 1, sizeof(UmIns))) {
                if (vu) {
                    s_umPrevHeld++;
                } else {
                    s_umHeld++;
                }
                continue;
            }
            UmIns *in = &s_umIns[s_umInsN++];
            in->list = nd->list;
            in->pos = pos;
            in->node = k;
            in->seq = k;
            in->weight = 1.0f;
        }
    }
    /* the states: cur's at its unmatched draws and at the insertions,
     * prev's at its unmatched draws */
    const uint32_t nq = s_umCurN + s_umInsN;
    if (!growTo((void **)&s_umQ, &s_umQCap, nq ? nq : 1, sizeof(StQ)) ||
        !growTo((void **)&s_umSt, &s_umStCap, nq ? nq : 1, sizeof(RdStateBlock))) {
        return;
    }
    for (uint32_t i = 0; i < s_umCurN; i++) {
        s_umQ[i] = (StQ){s_umCur[i].list, s_umCur[i].index, i};
    }
    for (uint32_t i = 0; i < s_umInsN; i++) {
        s_umQ[s_umCurN + i] = (StQ){s_umIns[i].list, s_umIns[i].pos, s_umCurN + i};
    }
    statesAt(&s_out, s_umQ, nq, s_umSt);
    for (uint32_t i = 0; i < s_umInsN; i++) {
        const Node *nd = &s_nodes[s_umIns[i].node];
        s_umQ[i] = (StQ){nd->list, nd->index, i};
    }
    const RdStateBlock *have = s_umSt + s_umCurN; /* cur's at each insertion */
    if (s_umInsN && !growTo((void **)&s_umWant, &s_umWantCap, s_umInsN, sizeof(RdStateBlock))) {
        for (uint32_t i = 0; i < s_umInsN; i++) {
            const Node *nd = &s_nodes[s_umIns[i].node];
            if (isVuDraw(&prev->lists[nd->list].cmds[nd->index])) {
                s_umPrevHeld++;
            } else {
                s_umHeld++;
            }
        }
        s_umInsN = 0;
    }
    const RdStateBlock *want = s_umWant;
    if (s_umInsN) {
        statesAt(prev, s_umQ, s_umInsN, s_umWant);
    }
    /* cur's unmatched draws: faded in, or from half way */
    for (uint32_t i = 0; i < s_umCurN; i++) {
        RdCmd *c = &s_out.lists[s_umCur[i].list].cmds[s_umCur[i].index];
        if (c->type == RDC_SCREEN && alphaFades(&s_umSt[i])) {
            RdScreenVtx *v = (RdScreenVtx *)(void *)outPayload(c);
            if (v) {
                scaleAlpha(v, c->u[1], t);
            }
            s_umFadeIn++;
        } else if (c->type == RDC_OVERLAY_TEXT) {
            uint8_t *p = outPayload(c);
            if (p && c->u[2] == sizeof(RdTextItem)) {
                scaleItemAlpha(p, t);
            }
            s_umFadeIn++;
        } else if (t < 0.5f) {
            c->type = RDC_NOP;
            s_umHalfIn++;
        }
    }
    /* prev's: faded out, or until half way; the others dropped */
    uint32_t keep = 0;
    for (uint32_t i = 0; i < s_umInsN; i++) {
        UmIns in = s_umIns[i];
        const RdCmd *pc = &prev->lists[in.list].cmds[s_nodes[in.node].index];
        in.want = want[i];
        in.have = have[i];
        RdCmd tmp[RD_STATE_DIFF_MAX];
        uint32_t n0 = 0, n1 = 0;
        if (!stateDiff(&have[i], &in.want, tmp, &n0) || !stateDiff(&in.want, &have[i], tmp, &n1)) {
            if (isVuDraw(pc)) {
                s_umPrevHeld++;
            } else {
                s_umHeld++;
            }
            continue;
        }
        if (isVuDraw(pc)) {
            /* prev alone (prevAlone): the whole interval, whole */
        } else if ((pc->type == RDC_SCREEN && alphaFades(&in.want)) ||
                   pc->type == RDC_OVERLAY_TEXT) {
            in.weight = 1.0f - t;
            s_umFadeOut++;
        } else if (t < 0.5f) {
            s_umHalfOut++;
        } else {
            continue; /* the far tick's */
        }
        in.cmds = n0 + n1 + 1;
        s_umIns[keep++] = in;
    }
    s_umInsN = keep;
    if (s_umInsN == 0) {
        return;
    }
    qsort(s_umIns, s_umInsN, sizeof(UmIns), cmpIns);
    /* the payloads first (s_out's payload may move), then the lists */
    for (uint32_t i = 0; i < s_umInsN; i++) {
        UmIns *in = &s_umIns[i];
        const RdCmd *pc = &prev->lists[in->list].cmds[s_nodes[in->node].index];
        const uint32_t from = pc->type == RDC_SCREEN ? pc->u[0] : pc->u[1];
        const uint32_t size = pc->type == RDC_SCREEN ? pc->u[1] * (uint32_t)sizeof(RdScreenVtx)
                              : pc->type == RDC_OVERLAY_TEXT || isVuDraw(pc) ? pc->u[2]
                              : pc->b[0] == RD_SHADOW_TRIS
                                  ? (pc->u[0] + pc->u[3]) * (uint32_t)sizeof(RdScreenVtx)
                                  : pc->u[0] * 16u;
        const void *src = payloadAt(prev, from, size);
        uint32_t off = 0;
        uint8_t *dst = src ? outAppend(size ? size : 16u, &off) : NULL;
        if (!dst) {
            in->node = ~0u; /* dropped */
            continue;
        }
        memcpy(dst, src, size);
        reprojDraw(pc, dst, s_rp.ip); /* prev's points through the blended camera */
        if (isVuDraw(pc) && !rebasePrev((float (*)[4])(void *)(dst + sizeof(RdVuPayload)), pc)) {
            in->node = ~0u; /* held: not through the blended camera */
            s_umPrevHeld++;
            continue;
        }
        if (pc->type == RDC_SCREEN && in->weight < 1.0f) {
            scaleAlpha((RdScreenVtx *)(void *)dst, pc->u[1], in->weight);
        }
        if (pc->type == RDC_OVERLAY_TEXT && size == sizeof(RdTextItem)) {
            scaleItemAlpha(dst, in->weight);
        }
        in->seq = off; /* now the payload offset */
    }
    uint32_t i = 0;
    while (i < s_umInsN) {
        const uint32_t l = s_umIns[i].list;
        uint32_t j = i, add = 0;
        for (; j < s_umInsN && s_umIns[j].list == l; j++) {
            add += s_umIns[j].node != ~0u ? s_umIns[j].cmds : 0u;
        }
        RdCmdList *cl = &s_out.lists[l];
        const uint32_t cap = cl->count + add;
        if (!growTo((void **)&s_umCmds, &s_umCmdsCap, cap, sizeof(RdCmd))) {
            i = j;
            continue;
        }
        uint32_t o = 0, src = 0, kept = 0;
        for (uint32_t k = i; k < j; k++) {
            const UmIns *in = &s_umIns[k];
            if (in->node == ~0u) {
                continue;
            }
            while (src < in->pos) {
                s_umCmds[o++] = cl->cmds[src++];
            }
            /* prev's state for the draw, then cur's again (checked above) */
            RdCmd pre[RD_STATE_DIFF_MAX], post[RD_STATE_DIFF_MAX];
            uint32_t n0 = 0, n1 = 0;
            (void)stateDiff(&in->have, &in->want, pre, &n0);
            (void)stateDiff(&in->want, &in->have, post, &n1);
            memcpy(&s_umCmds[o], pre, n0 * sizeof(RdCmd));
            o += n0;
            RdCmd c = prev->lists[l].cmds[s_nodes[in->node].index];
            if (c.type == RDC_SCREEN) {
                c.u[0] = in->seq;
            } else {
                c.u[1] = in->seq;
            }
            if (c.type == RDC_MESH || c.type == RDC_SKINNED) {
                (void)morphDraw(&c, NULL, NULL, prev, 1.0f); /* the stream prev drew */
            }
            kept += isVuDraw(&c);
            s_umCmds[o++] = c;
            if (c.type == RDC_OVERLAY_TEXT && c.b[0] == RD_OTEXT_ITEM) {
                s_out.textItems++; /* rd_present.c textCollect skips a frame with none */
            }
            memcpy(&s_umCmds[o], post, n1 * sizeof(RdCmd));
            o += n1;
        }
        while (src < cl->count) {
            s_umCmds[o++] = cl->cmds[src++];
        }
        if (o > cl->cap) {
            RdCmd *p = realloc(cl->cmds, (size_t)o * sizeof(RdCmd));
            if (!p) {
                i = j;
                continue;
            }
            cl->cmds = p;
            cl->cap = o;
        }
        memcpy(cl->cmds, s_umCmds, (size_t)o * sizeof(RdCmd));
        cl->count = o;
        s_umPrevKept += kept;
        i = j;
    }
}

/* ---------------------------------------------------------- flap detector
 *
 * A keyed draw's outcome per frame (blended, mismatched, jumped,
 * unmatched), at each frame's first present.  A draw whose outcome changes
 * from one frame to the next is shown at alpha in one tick and at the tick
 * itself in the other (one tick ahead of its neighbours): it flaps.  The
 * draws are told apart by key, type, list and their ordinal among the
 * draws with that key; the table restarts with each log block.  The block's
 * log line counts the flapping draws (4 or more changes in the block) and
 * names the worst RD_FLAP_LINES with their last outcomes, mismatch reason,
 * the reason of their last unmatched outcome (RD_UNMATCHED_*), program and
 * mesh ids.  A frame without a note between two that have one (the draw
 * not drawn, or the whole frame snapped; less than RD_FLAP_SPAN apart) is
 * in the outcomes too: an unmatched outcome after one is the draw coming
 * back, not a draw left unpaired beside its match. */
enum { O_LERP = 0, O_MISMATCH, O_JUMP, O_UNMATCHED, O_ABSENT };

/* the outcomes the history holds, 3 bits each */
#define RD_FLAP_SPAN 16u

/* the RD_UNMATCHED_* of the next O_UNMATCHED note */
static int s_noteWhy;

#define RD_FLAP_CAP 8192u
#define RD_FLAP_MIN 4u
#define RD_FLAP_LINES 6

typedef struct FlapEnt {
    uint32_t lo, hi;
    uint8_t type, list, used, last;
    uint16_t ord, changes;
    uint8_t why, prog;
    uint32_t frame;
    uint64_t hist; /* 3 bits per frame (O_*), newest in the low bits */
    uint32_t meshP, meshC, frames;
    uint8_t span;  /* the frames hist holds, RD_FLAP_SPAN at most */
    uint8_t unWhy; /* RD_UNMATCHED_* of the last O_UNMATCHED */
} FlapEnt;

static FlapEnt *s_flap;

static uint32_t s_flapCount;

static int s_track;

typedef struct OrdEnt {
    uint32_t lo, hi;
    uint8_t type, list, used, _pad;
    uint16_t n;
} OrdEnt;

static OrdEnt s_ord[4096];

static uint32_t flapHash(uint32_t lo, uint32_t hi, uint8_t type, uint8_t list, uint32_t ord)
{
    return slotHash(lo ^ ord * 0x9E3779B9u, hi, type, list);
}

static uint16_t ordinalOf(const RdCmd *c, int list)
{
    uint32_t i = slotHash(c->keyLo, c->keyHi, c->type, (uint8_t)list) & 4095u;
    for (uint32_t probe = 0; probe < 4096u; probe++, i = (i + 1) & 4095u) {
        OrdEnt *e = &s_ord[i];
        if (!e->used) {
            e->used = 1;
            e->lo = c->keyLo;
            e->hi = c->keyHi;
            e->type = c->type;
            e->list = (uint8_t)list;
            e->n = 1;
            return 0;
        }
        if (e->lo == c->keyLo && e->hi == c->keyHi && e->type == c->type && e->list == list) {
            return e->n++;
        }
    }
    return 0xFFFF;
}

static void flapNote(const RdCmd *c, int list, int outcome, const RdCmd *pc, uint32_t frame)
{
    if (!s_flap) {
        s_flap = calloc(RD_FLAP_CAP, sizeof(FlapEnt));
        if (!s_flap) {
            return;
        }
    }
    const uint16_t ord = ordinalOf(c, list);
    uint32_t i = flapHash(c->keyLo, c->keyHi, c->type, (uint8_t)list, ord) & (RD_FLAP_CAP - 1u);
    FlapEnt *e = NULL;
    for (uint32_t probe = 0; probe < 64u; probe++, i = (i + 1) & (RD_FLAP_CAP - 1u)) {
        FlapEnt *x = &s_flap[i];
        if (!x->used) {
            if (s_flapCount >= RD_FLAP_CAP / 2u) {
                return; /* full for this block */
            }
            memset(x, 0, sizeof(*x));
            x->used = 1;
            x->lo = c->keyLo;
            x->hi = c->keyHi;
            x->type = c->type;
            x->list = (uint8_t)list;
            x->ord = ord;
            x->last = (uint8_t)outcome;
            s_flapCount++;
            e = x;
            break;
        }
        if (x->lo == c->keyLo && x->hi == c->keyHi && x->type == c->type && x->list == list &&
            x->ord == ord) {
            e = x;
            if (x->frame + 1u == frame && x->last != (uint8_t)outcome) {
                x->changes++;
            }
            break;
        }
    }
    if (!e) {
        return;
    }
    /* frames since the last note in which the draw was not drawn */
    const uint32_t gap = e->frames && frame > e->frame ? frame - e->frame - 1u : RD_FLAP_SPAN;
    if (gap < RD_FLAP_SPAN) {
        for (uint32_t g = 0; g < gap; g++) {
            e->hist = e->hist << 3 | (uint64_t)O_ABSENT;
        }
        e->hist = e->hist << 3 | (uint64_t)outcome;
        e->span = (uint8_t)(e->span + gap + 1u < RD_FLAP_SPAN ? e->span + gap + 1u : RD_FLAP_SPAN);
    } else {
        e->hist = (uint64_t)outcome;
        e->span = 1;
    }
    if (outcome == O_UNMATCHED) {
        e->unWhy = (uint8_t)s_noteWhy;
    }
    e->last = (uint8_t)outcome;
    e->frame = frame;
    e->frames++;
    if (outcome == O_MISMATCH) {
        e->why = (uint8_t)s_why;
    }
    e->prog = (c->type == RDC_MESH || c->type == RDC_SKINNED) ? c->b[0] : 0;
    e->meshC = (c->type == RDC_MESH || c->type == RDC_SKINNED) ? c->u[0] : 0;
    e->meshP = pc && (pc->type == RDC_MESH || pc->type == RDC_SKINNED) ? pc->u[0] : 0;
}

static void flapFree(void)
{
    free(s_med);
    s_med = NULL;
    s_medCap = 0;
    free(s_flap);
    s_flap = NULL;
    s_flapCount = 0;
}

/* the block's flapping draws into the log; the table restarts */
static uint32_t flapReport(void)
{
    static const char code[8] = {'L', 'M', 'J', 'U', '-', '?', '?', '?'};
    static const char *const why[RD_MISMATCH_COUNT] = {"size", "mesh", "state", "header",
                                                       "topology"};
    static const char *const unWhy[RD_UNMATCHED_COUNT] = {
        "not drawn the tick before", "drawn fewer times the tick before",
        "left by the pairing by place", "payload out of range"};
    uint32_t n = 0;
    if (!s_flap) {
        return 0;
    }
    for (uint32_t i = 0; i < RD_FLAP_CAP; i++) {
        n += s_flap[i].used && s_flap[i].changes >= RD_FLAP_MIN;
    }
    for (int line = 0; line < RD_FLAP_LINES; line++) {
        FlapEnt *best = NULL;
        for (uint32_t i = 0; i < RD_FLAP_CAP; i++) {
            FlapEnt *e = &s_flap[i];
            if (e->used && e->changes >= RD_FLAP_MIN && (!best || e->changes > best->changes)) {
                best = e;
            }
        }
        if (!best) {
            break;
        }
        char h[RD_FLAP_SPAN + 1u];
        const uint32_t k = best->span < RD_FLAP_SPAN ? best->span : RD_FLAP_SPAN;
        for (uint32_t j = 0; j < k; j++) {
            h[j] = code[(best->hist >> (3u * (k - 1u - j))) & 7u];
        }
        h[k] = '\0';
        rd__log("interp: flapping draw key %08x%08x type %u list %u #%u: %u changes in %u frames, "
                "last %s (oldest first; L blended, M mismatched, J jumped or paired with a draw "
                "of another place, U unmatched, - not drawn or the frame snapped), mismatch %s, "
                "unmatched %s, program %u, mesh %u -> %u",
                best->hi, best->lo, best->type, best->list, best->ord, best->changes, best->frames,
                h, why[best->why < RD_MISMATCH_COUNT ? best->why : 0],
                unWhy[best->unWhy < RD_UNMATCHED_COUNT ? best->unWhy : 0], best->prog, best->meshP,
                best->meshC);
        best->changes = 0; /* listed */
    }
    memset(s_flap, 0, RD_FLAP_CAP * sizeof(FlapEnt));
    s_flapCount = 0;
    return n;
}

/* ------------------------------------------------------------ the frame */

static float frob(const float *a, const float *b)
{
    float s = 0.0f;
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            s += a[c * 4 + r] * b[c * 4 + r];
        }
    }
    return s;
}

/* the eye of a column-major view matrix: -R^T t */
static void viewEye(const float *v, float eye[3])
{
    for (int j = 0; j < 3; j++) {
        eye[j] = -(v[j * 4 + 0] * v[12] + v[j * 4 + 1] * v[13] + v[j * 4 + 2] * v[14]);
    }
}

static bool cameraJump(const RdCamera *p, const RdCamera *c)
{
    const float fp = frob(p->view, p->view), fc = frob(c->view, c->view);
    if (!(fp > 0.0f) || !(fc > 0.0f)) {
        return false;
    }
    /* cos of the turn between the two rotations, scale removed */
    const float cosT = (3.0f * frob(p->view, c->view) / sqrtf(fp * fc) - 1.0f) * 0.5f;
    if (cosT < cosf(RD_INTERP_CAMERA_TURN * 3.14159265f / 180.0f)) {
        return true;
    }
    float a[3], b[3];
    viewEye(p->view, a);
    viewEye(c->view, b);
    return dist3(a, b) > RD_INTERP_CAMERA_MOVE;
}

int rd__interp_snap(const RdFrame *prev, const RdFrame *cur)
{
    if (!cur || !prev || !prev->closed) {
        return RD_SNAP_NO_PREV;
    }
    if (prev->number + 1 != cur->number) {
        return RD_SNAP_GAP;
    }
    if (prev->keep || cur->keep) {
        return RD_SNAP_KEEP;
    }
    if (cur->cut || cur->camera.cut) {
        return RD_SNAP_CUT;
    }
    if (g_rd.interpFloor && prev->number < g_rd.interpFloor) {
        return RD_SNAP_HISTORY;
    }
    if (prev->gsW != cur->gsW || prev->gsH != cur->gsH) {
        return RD_SNAP_SIZE;
    }
    if (prev->fade > 0x80u || cur->fade > 0x80u) {
        return RD_SNAP_FADE; /* 1 + an alpha of 0x80 or more: black */
    }
    if (prev->hasCamera && cur->hasCamera && cameraJump(&prev->camera, &cur->camera)) {
        return RD_SNAP_CAMERA;
    }
    return RD_SNAP_NONE;
}

static void lerpCamera(RdCamera *o, const RdCamera *p, const RdCamera *c, float t)
{
    lerpFloats(o->view, p->view, c->view, 16, t);
    lerpFloats(o->proj43, p->proj43, c->proj43, 16, t);
    lerpFloats(&o->zoom, &p->zoom, &c->zoom, 1, t);
    lerpFloats(&o->nearZ, &p->nearZ, &c->nearZ, 1, t);
    lerpFloats(&o->farZ, &p->farZ, &c->farZ, 1, t);
}

/* Does c, under the state st, write the target id? */
static int writesTarget(const RdCmd *c, const RdStateBlock *st, uint32_t id)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return 0;
    case RDC_CLEAR:
        return c->u[0] == id;
    case RDC_COPY:
        return c->u[1] == id;
    default:
        return st->color == id;
    }
}

/* Does c, under the state st, sample the target id? */
static int readsTarget(const RdCmd *c, const RdStateBlock *st, uint32_t id)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
    case RDC_CLEAR:
        return 0;
    case RDC_COPY:
        return c->u[0] == id;
    default: {
        const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
        return t && t->kind == RD_TEXKIND_TARGET && t->target == id;
    }
    }
}

/* Does a draw under the state st sample a target (any)? */
static bool readsAnyTarget(const RdStateBlock *st)
{
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    return t && t->kind == RD_TEXKIND_TARGET;
}

/* an RDC_COPY of all of from into to (one size and scale) at the head of
 * list l */
static void headCopy(int l, uint32_t from, uint32_t to, uint32_t sizeOf)
{
    RdCmdList *cl = &s_out.lists[l];
    const RdTargetRec *t = rd__target_rec(sizeOf);
    uint32_t off;
    RdCopyRec *r = t ? (RdCopyRec *)(void *)outAppend(sizeof(RdCopyRec), &off) : NULL;
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->w = t->w;
    r->h = t->h;
    if (!growTo((void **)&cl->cmds, &cl->cap, cl->count + 1, sizeof(RdCmd))) {
        return;
    }
    memmove(&cl->cmds[1], &cl->cmds[0], (size_t)cl->count * sizeof(RdCmd));
    cl->count++;
    RdCmd *c = &cl->cmds[0];
    memset(c, 0, sizeof(*c));
    c->type = RDC_COPY;
    c->u[0] = from;
    c->u[1] = to;
    c->u[2] = off;
}

/* The feedback passes' inputs, held at the tick.  A frame that writes FEED128
 * reads what the frame before left there (the aura's feedback), and every
 * present of a tick replays all of its passes, so every present must start
 * from the FEED128 the tick's first present started from: the first copies
 * FEED128 into FEED_HELD at its head, the later ones copy FEED_HELD back into
 * FEED128 at theirs.  Every present then draws the same picture and leaves
 * FEED128 in the tick's final state, which the next tick reads, as on the
 * PS2.  (Dropping a later present's FEED128 writes instead, while its reads
 * still run, would paste the tick's final FEED128 over the screen: after the
 * black reset of a camera cut, auraInspireAfter's GlobalTimer fill, a black
 * frame.)  For issue 28, the same for DISPLAY when the frame reads it (the
 * motion blur's old frame, the file header): DISPLAY into DISPLAY_HELD at the
 * first present's head, back at the later ones', so every present draws the
 * tick's FIX over the previous tick's picture. */
static void feedback(int firstOfTick)
{
    const uint32_t feed = rd_target(RD_TARGET_FEED128).id;
    const uint32_t held = rd_target(RD_TARGET_FEED_HELD).id;
    const uint32_t disp = rd_target(RD_TARGET_DISPLAY).id;
    const uint32_t dispHeld = rd_target(RD_TARGET_DISPLAY_HELD).id;
    const int head = rd__first_list((int)s_out.keep);
    int fed = 0, read = 0;
    RdStateBlock st = s_out.startState;
    for (int l = head; l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &s_out.lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            if (rd__apply_state(&st, c)) {
                continue;
            }
            fed |= feed && writesTarget(c, &st, feed);
            read |= disp && readsTarget(c, &st, disp);
        }
    }
    if (fed && held) {
        headCopy(head, firstOfTick ? feed : held, firstOfTick ? held : feed, feed);
    }
    if (read && dispHeld) {
        headCopy(head, firstOfTick ? disp : dispHeld, firstOfTick ? dispHeld : disp, disp);
    }
}

const RdFrame *rd__interp_frame(const RdFrame *prev, const RdFrame *cur, float alpha,
                                int firstOfTick, RdInterpStats *stats)
{
    RdInterpStats st;
    memset(&st, 0, sizeof(st));
    if (!cur || !copyFrame(cur)) {
        if (stats) {
            *stats = st;
        }
        return NULL;
    }
    const float t = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    st.snap = (uint32_t)rd__interp_snap(prev, cur);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
            st.keyed += isKeyedDraw(&s_out.lists[l].cmds[i]);
        }
    }
    s_scratchUsed = 0;
    s_scratchBytes = 0;
    s_doneCount = 0;
    s_umCurN = 0;
    s_umFadeIn = s_umFadeOut = s_umHalfIn = s_umHalfOut = s_umHeld = 0;
    s_umPrevKept = s_umPrevHeld = 0;
    s_lightRot = 0;
    s_lightPair = s_placed = s_gridSt = 0;
    s_pivotMesh = 0; /* the pivots are recomputed per present */
    if (s_track) {
        memset(s_ord, 0, sizeof(s_ord));
    }
    const bool blend = st.snap == RD_SNAP_NONE && t < 1.0f && buildIndex(prev);
    if (blend) {
        (void)indexCur(); /* false: the ordinal pairing for every key */
    }
    s_cam.on = 0;
    s_rp.on = 0;
    s_rebased = s_rebasedCur = 0;
    s_reproj = 0;
    if (blend) {
        camSetup(prev, cur, t); /* the blended camera */
        reprojSetup(t);         /* and the CPU-projected draws' */
        if (prev->hasCamera && cur->hasCamera) {
            lerpCamera(&s_out.camera, &prev->camera, &cur->camera, t);
            if (s_cam.on) {
                for (int k = 0; k < 16; k++) {
                    s_out.camera.view[k] = (float)s_cam.vt[k];
                }
            }
        }
        if (prev->hasVu && cur->hasVu) {
            lerpFloats(s_out.vu.screenView, prev->vu.screenView, cur->vu.screenView, 16, t);
            lerpFloats(s_out.vu.viewport, prev->vu.viewport, cur->vu.viewport, 16, t);
            lerpFloats(s_out.vu.invView, prev->vu.invView, cur->vu.invView, 16, t);
            if (s_cam.on && !s_cam.still) {
                double sv[16], se[16];
                loadF16(cur->vu.screenView, sv);
                mul4d(sv, s_cam.ec, se);
                if (s_cam.zoom) {
                    double lse[16];
                    mul4d(s_cam.lc, se, lse);
                    memcpy(se, lse, sizeof(se));
                }
                for (int k = 0; k < 16; k++) {
                    s_out.vu.screenView[k] = (float)se[k];
                }
                memcpy(s_out.vu.invView, s_cam.itf, sizeof(s_cam.itf));
            }
        }
        RdStateBlock rs = s_out.startState; /* the state each draw replays under */
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
                RdCmd *c = &s_out.lists[l].cmds[i];
                if (rd__apply_state(&rs, c)) {
                    continue;
                }
                if (!isKeyedDraw(c)) {
                    camCurDraw(c); /* unkeyed, cur's through the blended camera */
                    continue;
                }
                const RdCmd *pc = matchOf(prev, c, l, i);
                uint8_t *op = outPayload(c);
                if (!pc || !op) {
                    const int why = pc ? RD_UNMATCHED_PAYLOAD : s_unWhy;
                    st.missing++;
                    st.unmatchedWhy[why < RD_UNMATCHED_COUNT ? why : 0]++;
                    camCurDraw(c);
                    if (!pc && isProjected(c)) {
                        umCurOnly((uint32_t)l, i); /* faded in, or from half way */
                    }
                    if (s_track) {
                        s_noteWhy = why;
                        flapNote(c, l, O_UNMATCHED, NULL, cur->number);
                    }
                    continue;
                }
                int r;
                s_rotated = 0;
                s_shifted = 0;
                switch (c->type) {
                case RDC_SCREEN:
                    r = blendScreen(op, prev, pc, c, t);
                    break;
                case RDC_SHADOW_STRIP:
                    r = blendShadow(op, prev, pc, c, t);
                    break;
                case RDC_OVERLAY_TEXT:
                    r = blendText(op, prev, pc, c, t);
                    break;
                default:
                    s_gridScreenSt = c->type == RDC_GRID && readsAnyTarget(&rs);
                    r = s_matchApart ? R_JUMP : blendVu(op, prev, pc, c, t);
                    s_gridScreenSt = false;
                    break;
                }
                st.apart += s_matchApart; /* a draw of another place: the tick's */
                if (r != R_LERP) {
                    camCurDraw(c); /* the tick's draw, through the blended camera */
                }
                st.lerped += r == R_LERP;
                st.mismatch += r == R_MISMATCH;
                st.jump += r == R_JUMP;
                if (r == R_MISMATCH) {
                    st.why[s_why < RD_MISMATCH_COUNT ? s_why : 0]++;
                }
                if (r == R_LERP && s_rotated && (c->type == RDC_MESH || c->type == RDC_SKINNED)) {
                    st.rotated++;
                    st.turned += s_drawTurn > 10.0;
                    st.maxTurn = fmaxf(st.maxTurn, (float)s_drawTurn);
                }
                st.shifted += r == R_LERP && s_shifted;
                if (s_track) {
                    flapNote(c, l, r == R_LERP ? O_LERP : (r == R_JUMP ? O_JUMP : O_MISMATCH), pc,
                             cur->number);
                }
                if (r == R_LERP && (c->type == RDC_MESH || c->type == RDC_SKINNED)) {
                    st.morph += morphDraw(c, prev, pc, cur, t);
                    blendedMesh((uint32_t)l, i);
                }
            }
        }
    }
    /* the other mesh draws (unkeyed, unmatched, snapped) take cur's
     * stream when the live one has moved on */
    uint32_t next = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
            RdCmd *c = &s_out.lists[l].cmds[i];
            if (c->type != RDC_MESH && c->type != RDC_SKINNED) {
                continue;
            }
            if (next < s_doneCount && s_done[next].list == (uint32_t)l && s_done[next].index == i) {
                next++;
                continue;
            }
            st.morph += morphDraw(c, NULL, NULL, cur, 1.0f);
        }
    }
    if (blend) {
        unmatchedPass(prev, t); /* inserts commands, so after the passes above */
    }
    st.prevKept = s_umPrevKept;
    st.prevHeld = s_umPrevHeld;
    st.rebased = (uint32_t)s_rebased;
    st.rebasedCur = (uint32_t)s_rebasedCur;
    st.lightPaired = (uint32_t)s_lightPair;
    st.placed = (uint32_t)s_placed;
    st.gridSt = (uint32_t)s_gridSt;
    st.reprojected = s_reproj;
    feedback(firstOfTick);
    if (stats) {
        *stats = st;
    }
    return &s_out;
}

/* ------------------------------------------------------------- present */

static struct {
    uint32_t number; /* the frame of the last present */
    RdInterpStats stats;
    /* the log: per frame, at its first present */
    uint32_t frames, presents, snaps[RD_SNAP_COUNT];
    uint64_t keyed, lerped, missing, mismatch, jump, morph;
    /* mismatch reasons, rotation blends, frames whose snap differs
     * from the frame before's (whole-frame flaps) */
    uint64_t why[RD_MISMATCH_COUNT], rotated, turned, shifted;
    float maxTurn;
    uint32_t snapFlips, lastSnap;
    uint64_t rebased, rebasedCur; /* VU draws re-based on the blended camera */
    /* unmatched screen prims and shadow volumes (faded in, faded out,
     * switched at half way, cur's and prev's; prev's not placed), lit draws
     * whose light matrix turned with the model */
    uint64_t fadeIn, fadeOut, halfIn, halfOut, held, lightRot;
    /* lit draws whose previous lights were re-paired (pairLights), draws of
     * a key drawn several times paired by their place (placeSlot), grids
     * whose screen-space STs blended (s_gridScreenSt) */
    uint64_t lightPaired, placed, gridSt;
    /* unmatched keyed draws by reason, draws paired with a draw of another
     * place, VU draws of the tick before alone kept and held */
    uint64_t unmatchedWhy[RD_UNMATCHED_COUNT], apart, prevKept, prevHeld;
    uint32_t scratchMax;      /* the most scratch meshes a present used */
    uint64_t scratchBytesMax; /* and the most bytes they held */
} s_pres;

#define RD_INTERP_LOG_FRAMES 250

static void presentLog(void)
{
    const RdInterpStats *st = &s_pres.stats;
    s_pres.frames++;
    s_pres.snaps[st->snap < RD_SNAP_COUNT ? st->snap : 0]++;
    s_pres.keyed += st->keyed;
    s_pres.lerped += st->lerped;
    s_pres.missing += st->missing;
    s_pres.mismatch += st->mismatch;
    s_pres.jump += st->jump;
    s_pres.morph += st->morph;
    for (int k = 0; k < RD_MISMATCH_COUNT; k++) {
        s_pres.why[k] += st->why[k];
    }
    s_pres.rotated += st->rotated;
    s_pres.turned += st->turned;
    s_pres.shifted += st->shifted;
    s_pres.rebased += st->rebased;
    s_pres.rebasedCur += st->rebasedCur;
    s_pres.maxTurn = fmaxf(s_pres.maxTurn, st->maxTurn);
    s_pres.fadeIn += s_umFadeIn;
    s_pres.fadeOut += s_umFadeOut;
    s_pres.halfIn += s_umHalfIn;
    s_pres.halfOut += s_umHalfOut;
    s_pres.held += s_umHeld;
    s_pres.lightRot += (uint64_t)s_lightRot;
    s_pres.lightPaired += st->lightPaired;
    s_pres.placed += st->placed;
    s_pres.gridSt += st->gridSt;
    for (int k = 0; k < RD_UNMATCHED_COUNT; k++) {
        s_pres.unmatchedWhy[k] += st->unmatchedWhy[k];
    }
    s_pres.apart += st->apart;
    s_pres.prevKept += st->prevKept;
    s_pres.prevHeld += st->prevHeld;
    s_pres.scratchMax = s_scratchUsed > s_pres.scratchMax ? s_scratchUsed : s_pres.scratchMax;
    s_pres.scratchBytesMax =
        s_scratchBytes > s_pres.scratchBytesMax ? s_scratchBytes : s_pres.scratchBytesMax;
    s_pres.snapFlips +=
        s_pres.frames > 1 && (st->snap == RD_SNAP_NONE) != (s_pres.lastSnap == RD_SNAP_NONE);
    s_pres.lastSnap = st->snap;
    if (s_pres.frames < RD_INTERP_LOG_FRAMES) {
        return;
    }
    const uint32_t *n = s_pres.snaps;
    rd__log("interp: %u frames, %u presents: %u blended; snapped: %u no previous, %u gap, "
            "%u keep, %u cut, %u camera, %u fade, %u history, %u size; keyed draws %llu: "
            "%llu blended, %llu unmatched, %llu mismatched, %llu jumped; %llu mesh streams kept or "
            "blended",
            s_pres.frames, s_pres.presents, n[RD_SNAP_NONE], n[RD_SNAP_NO_PREV], n[RD_SNAP_GAP],
            n[RD_SNAP_KEEP], n[RD_SNAP_CUT], n[RD_SNAP_CAMERA], n[RD_SNAP_FADE], n[RD_SNAP_HISTORY],
            n[RD_SNAP_SIZE], (unsigned long long)s_pres.keyed, (unsigned long long)s_pres.lerped,
            (unsigned long long)s_pres.missing, (unsigned long long)s_pres.mismatch,
            (unsigned long long)s_pres.jump, (unsigned long long)s_pres.morph);
    /* a separate line, so the line above keeps its form */
    const uint32_t flapping = flapReport();
    rd__log(
        "interp: mismatched by size %llu, mesh %llu, state %llu, header %llu, topology %llu; "
        "%llu mesh draws blended as rotations (%llu turning over 10 degrees a tick, the largest "
        "%.1f degrees); %llu shadow volumes moved across a topology change; %u draws flapping "
        "(%u or more outcome changes); %u frames changed between blended and snapped; %llu VU "
        "draws through the blended camera (%llu of them the tick's: unmatched, snapped or unkeyed)",
        (unsigned long long)s_pres.why[0], (unsigned long long)s_pres.why[1],
        (unsigned long long)s_pres.why[2], (unsigned long long)s_pres.why[3],
        (unsigned long long)s_pres.why[4], (unsigned long long)s_pres.rotated,
        (unsigned long long)s_pres.turned, (double)s_pres.maxTurn,
        (unsigned long long)s_pres.shifted, flapping, RD_FLAP_MIN, s_pres.snapFlips,
        (unsigned long long)s_pres.rebased, (unsigned long long)s_pres.rebasedCur);
    /* a third line: the unmatched draws and the scratch meshes */
    rd__log("interp: unmatched screen prims and shadow volumes: %llu faded in, %llu faded out, "
            "%llu of the tick drawn from half way, %llu of the tick before drawn until half way, "
            "%llu of the tick before not placed; %llu lit draws whose lights turned with them; "
            "at most %u of %u scratch meshes in a present (%llu bytes)",
            (unsigned long long)s_pres.fadeIn, (unsigned long long)s_pres.fadeOut,
            (unsigned long long)s_pres.halfIn, (unsigned long long)s_pres.halfOut,
            (unsigned long long)s_pres.held, (unsigned long long)s_pres.lightRot, s_pres.scratchMax,
            RD_INTERP_SCRATCH, (unsigned long long)s_pres.scratchBytesMax);
    /* a fourth line: the lights' slots, the same-key instances, the pool's
     * screen-space STs */
    rd__log("interp: %llu lit draws whose previous lights were re-paired by direction; %llu draws "
            "of a key drawn several times paired by their place in the world instead of their "
            "order; %llu grids sampling a target whose STs blended",
            (unsigned long long)s_pres.lightPaired, (unsigned long long)s_pres.placed,
            (unsigned long long)s_pres.gridSt);
    /* a fifth line: why keyed draws stayed unmatched, and the VU draws of
     * the tick before alone */
    const uint64_t *u = s_pres.unmatchedWhy;
    rd__log("interp: unmatched keyed draws: %llu not drawn the tick before, %llu drawn fewer "
            "times the tick before, %llu left by the pairing by place, %llu payload out of "
            "range; %llu paired with a draw of another place (drawn as the tick's); VU draws of "
            "the tick before alone, outside the camera's picture: %llu of the tick before kept, "
            "%llu held",
            (unsigned long long)u[RD_UNMATCHED_ABSENT], (unsigned long long)u[RD_UNMATCHED_FEWER],
            (unsigned long long)u[RD_UNMATCHED_UNPLACED],
            (unsigned long long)u[RD_UNMATCHED_PAYLOAD], (unsigned long long)s_pres.apart,
            (unsigned long long)s_pres.prevKept, (unsigned long long)s_pres.prevHeld);
    const uint32_t number = s_pres.number;
    memset(&s_pres, 0, sizeof(s_pres));
    s_pres.number = number;
}

/* rd_last_present_info's record; valid once a present ran */
static RdPresentInfo s_info;
static bool s_infoValid;

static void presentReset(void)
{
    memset(&s_pres, 0, sizeof(s_pres));
    memset(&s_info, 0, sizeof(s_info));
    s_infoValid = false;
}

/* with interpolation off rd_end_frame presents each frame once, whole */
void rd__note_frame_present(const RdFrame *f)
{
    s_info.frame = f->number;
    s_info.keep = f->keep;
    s_info.fade = f->fade;
    s_info.snap = (uint32_t)rd__interp_snap(rd__prev_frame(), f);
    s_info.firstOfTick = 1;
    s_infoValid = true;
}

bool rd_last_present_info(RdPresentInfo *out)
{
    if (!out || !s_infoValid) {
        return false;
    }
    *out = s_info;
    return true;
}

bool rd_interpolation_active(void)
{
    /* both presets: the Original preset presents its PS2-exact tick
     * pictures and the blended ones between them */
    return g_rd.inited && g_rd.settings.interpolate != 0;
}

bool rd_present(float alpha)
{
    if (!rd_interpolation_active() || !g_rd.hasDevice || g_rd.videoShown) {
        return false;
    }
    const RdFrame *cur = rd__last_frame();
    if (!cur || !cur->closed) {
        return false;
    }
    if (!(alpha >= 0.0f)) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    /* the tick's first present keeps the feedback passes' inputs (FEED128,
     * and DISPLAY for issue 28), the later ones start from them
     * again (feedback) */
    const int first = cur->number != s_pres.number;
    s_pres.number = cur->number;
    s_pres.presents++;
    s_info.frame = cur->number;
    s_info.keep = cur->keep;
    s_info.fade = cur->fade;
    s_info.snap = (uint32_t)rd__interp_snap(rd__prev_frame(), cur);
    s_info.firstOfTick = (uint32_t)first;
    s_infoValid = true;
    if (cur->keep) {
        /* a keep frame draws lists 11..12 over DISPLAY: once; the other
         * presents show DISPLAY again (an empty keep frame) */
        if (first) {
            return rd__replay_frame(cur, 1, true);
        }
        static RdFrame empty;
        return rd__replay_frame(&empty, 1, true);
    }
    const double t0 = rd__now_ms();
    s_track = first; /* the flap detector, once a frame */
    const RdFrame *f = rd__interp_frame(rd__prev_frame(), cur, alpha, first, &s_pres.stats);
    s_track = 0;
    rd__perf_interp_ms(rd__now_ms() - t0); /* charged to the replay below */
    rd__perf_alpha(alpha, first);
    if (first) {
        presentLog();
    }
    return f != NULL && rd__replay_frame(f, 0, true);
}

/* ---------------------------------------------------------- present clock */

float rd_present_clock_alpha(RdPresentClock *c, double nowMs, double tickAtMs, double tickMs,
                             double nominalMs)
{
    if (!(tickMs > 0.0)) {
        return 0.0f;
    }
    const double step = c->stepMs > 0.0 ? c->stepMs : (nominalMs > 0.0 ? nominalMs : 0.0);
    const double since = nowMs - c->lastMs;
    bool reset = c->presents == 0 || since < 0.0 || !(step > 0.0) || since > 4.0 * step;
    if (!reset) {
        /* the running mean of the intervals, a sixteenth of the difference a
         * present */
        c->stepMs = step + (since - step) / 16.0;
        const double predicted = c->clockMs + c->stepMs;
        const double err = nowMs - predicted;
        if (fabs(err) > 0.5 * tickMs) {
            reset = true;
        } else {
            c->clockMs = predicted + err / 8.0;
        }
    }
    if (reset) {
        c->clockMs = nowMs;
        if (c->presents != 0 && since > 0.0 && (!(step > 0.0) || since <= 4.0 * step)) {
            c->stepMs = since; /* the first interval, when nothing nominal was given */
        } else if (!(c->stepMs > 0.0)) {
            c->stepMs = nominalMs > 0.0 ? nominalMs : 0.0;
        }
        c->presents = 0;
        c->resets++;
    }
    c->presents++;
    c->lastMs = nowMs;
    double a = (c->clockMs - tickAtMs) / tickMs;
    a = a < 0.0 ? 0.0 : (a > 0.999 ? 0.999 : a);
    return (float)a;
}
