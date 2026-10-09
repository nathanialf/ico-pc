/* rd_interp.c: presentation between simulation ticks (renderer wave 7, R7b).
 *
 * The game draws one frame per tick (25 Hz PAL, 30 Hz NTSC at the frame
 * step 2).  With RdSettings.interpolate in the Enhanced preset, rd_EndFrame
 * only closes the frame and the host calls rd_Present(alpha) as often as
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
 * drawn more than once).  What blends, per command:
 *
 *   RDC_MESH, RDC_SKINNED   the VU block (VuCB.mem): qw 2 (the UV scroll
 *                           SET_UVOFFSET left, with Texture.c's wrap of
 *                           uOfs/vOfs into (-1, 1] by 2 undone, and the
 *                           cluster fade alpha), qw 4..15 (the common
 *                           block's world to screen, viewport and inverse
 *                           view matrices), 16..27 (the model matrices),
 *                           28..35 (the light matrices; I1: a lit program's
 *                           L1 turns with the model, rotateLight); the
 *                           bones; not qw 0, 1, 3 (constants and the GIF tag)
 *   RDC_GRID                the VU block and each vertex's position (and
 *                           normal when lit); the strip headers, colours
 *                           and STs are cur's
 *   RDC_PARTICLES           the VU block and each particle's position and
 *                           size; UV, grey and alpha are cur's
 *   RDC_SCREEN              XY, Z and colour of each vertex; STQ is cur's
 *   RDC_SHADOW_STRIP        XY and Z of each vertex; since V3 Shadow.c's
 *                           volumes prism by prism (blendPrisms)
 *   RDC_OVERLAY_TEXT        package DEF: an item's anchor, glow stretch and
 *                           colour (string, size and flags must be equal);
 *                           an op's colour and level (blendText)
 * Since I1 the RDC_SCREEN and RDC_SHADOW_STRIP draws of one tick only fade
 * in or out with t, or switch at t = 0.5 (unmatchedPass); prev's are
 * inserted into the output.  Package DEF: so do the RDC_OVERLAY_TEXT items
 * (their alpha), as the quads they stand for.
 * Matrices blend element by element, except (package S2) a normal
 * program's model matrices and a skinned draw's bones, which blend as a
 * slerped rotation and a lerped stretch about a pivot (rotateModel,
 * rotateBone): element by element a turn of 77 degrees in a tick, which
 * stage 3 has, drew a bone 22 % short half way.  A float pair with
 * different bits that is not both finite keeps cur's.  Since package S6 the
 * camera is one rigid blend for the frame (camSetup: the inverse views'
 * rotation slerped, the eye lerped, the projection lerped) and every VU
 * draw through the frame's camera is re-based on it, the draws that are
 * cur's included (camRebase, camCurDraw): an unmatched draw stood a tick
 * ahead of its neighbours.
 *
 * A keyed draw snaps (is cur's) when prev has no match, when the payload's
 * shape differs (mesh, program, batch range, bone, vertex or particle
 * count, prim type; since S2 neither a mesh's code and clip mode nor a
 * shadow volume's topology, which blendPrisms (V3) or shiftShadow handles), or when it
 * jumped: a model's origin in the world (the model to screen translation
 * through the inverse of the frame's world to screen; a skinned draw's first
 * bone) moved more than RD_INTERP_JUMP_WORLD in the tick, or a screen prim's
 * or shadow vertex more than RD_INTERP_JUMP_SCREEN.  A particle whose position moved more than
 * four times its size, or whose alpha is 0 in either frame, keeps cur's.
 *
 * The whole frame snaps (rd__InterpSnap, RD_SNAP_*) without a previous
 * frame, across a discarded frame, on a keep frame, on a camera cut
 * (rd_CameraCut: the game's hard cuts and stage changes; RdCamera.cut), when
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
 * The motion blur (v0.4.3, issue 28).  On the PS2 the sprite draws the
 * previous DISPLAY as Cs over the new SCENE with ALPHA (Cs - Cd) FIX / 128
 * + Cd (staticBlur.c MotionBlur, gif_SetAlpha mode 2), once a tick, and
 * the passes after it in lists 8 to 12 (the aura's add, the softening,
 * the brightness step, fades, the UI, the reduction's tint) make the next
 * DISPLAY of that: D_n = P((1 - a) S_n + a D_n-1) with a = FIX / 128.
 * Every present replays the whole frame, so a present that read the
 * previous present's DISPLAY ran that loop k times a tick (k presents a
 * tick).  The retention can be spread over the k presents (v0.4.2: FIX' =
 * 128 a^(1/k), a over the tick), but P cannot: with its gain g (the tint
 * over 128) and the light L it adds (the aura, the brightness step, a fade
 * to white) a still picture settled at (g (1 - a') S + L) / (1 - g a'),
 * a' = FIX' / 128, instead of the PS2's (g (1 - a) S + L) / (1 - g a).
 * FIX 32 on 30 Hz ticks at 300 presents a second (FIX' 111) kept 5.6 times
 * the PS2's share of L, and a tint of 148 or more (g a' >= 1) ran to white;
 * a tint under 128 went dark the same way.  Now every present of a tick
 * draws the recorded FIX over the DISPLAY the tick started from
 * (DISPLAY_HELD, feedback), so the loop runs once a tick at any present
 * rate and the tick's picture is the PS2's; between ticks the old frame
 * stays the last tick's, as on the PS2.
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

void rd__InterpShutdown(void)
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
 * for a sine scroll (package S: RD_VU_SCROLL_SINE_*), which is never
 * wrapped and can step further: amplitude 0.8 at 7 Hz moves 1.23 in a
 * tick, and unwrapping it put the texture half a repeat off at alpha 0.25
 * and 0.75. */
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

/* ------------------------------------------- rotation-aware blend (S2) */

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

/* the turn between the two rotations of the last rd__BlendAffine, degrees */
static double s_turnDeg;

bool rd__BlendAffine(const double *p, const double *c, double t, const double *pivot, double *o)
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

bool rd__S2Legacy(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("ICO_RD_S2_LEGACY");
        v = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    return v != 0;
}

bool rd__VuOffGrid(void)
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

/* The pivots the rotations turn about (S2).  A skinned draw's bone is the
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
    const RdMeshRec *m = rd__MeshRec(meshId);
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
    const RdMeshRec *m = rd__MeshRec(meshId);
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
    if (!rd__BlendAffine(mp, mc, t, pivot, mo)) {
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

/* Package I1: a lit program's light matrix L1 (qw 28..31) follows the
 * object's turn.  normal_l computes l = max0(L1 n) from the model-space
 * normal n (n.w the ambient weight) and c = max0(L2 l): L1's rows 0..2 are the three lights' directions in model
 * space, row 3 (0, 0, 0, 1) carries n.w into l.w; L2 (qw 32..35) holds the
 * lights' colours in columns 0..2 and the ambient colour in column 3.
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

/* A normal program's model matrices (qw 16..19 model to screen, 20..23
 * model to clip, 24..27 model to view) are each a camera part times the
 * object's model to world W.  W = S^-1 (qw 16..19), S the common block's
 * world to screen (qw 4..7); W blends as a rotation (rd__BlendAffine), each
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
    if (!rd__BlendAffine(wp, wc, t, pivot, wt) || !invert4d(wp, iwp) || !invert4d(wc, iwc)) {
        return false;
    }
    if (s_turnDeg > RD_INTERP_TURN_SNAP) {
        /* a flip, not a motion: the tick's (I1: its lights with it) */
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
    if (lit && !rd__S2Legacy()) {
        s_lightRot += rotateLight(o, p, c, t, iwp, iwc, wt);
    }
    return true;
}

/* ----------------------------------------------- the camera (package S6) */

/* The half-way frame's camera, a rigid transform: the inverse views (view to
 * world: the rotation and the eye) blended by rd__BlendAffine about the eye
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
 * RDC_SHADOW_STRIP) hold GS positions and keep their blend in screen space
 * (unmatched, I1's fade or half-way switch, unmatchedPass); draws under
 * another camera (a reflection's) keep the element-wise blend. */
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
    if (rd__S2Legacy() || !prev->hasCamera || !cur->hasCamera || !(t > 0.0f) || !(t < 1.0f)) {
        return;
    }
    double ip[16], ic[16];
    loadF16(prev->camera.view, s_cam.vp);
    loadF16(cur->camera.view, s_cam.vc);
    if (!invert4d(s_cam.vp, ip) || !invert4d(s_cam.vc, ic) ||
        !rd__BlendAffine(ip, ic, t, NULL, s_cam.it) || !invert4d(s_cam.it, s_cam.vt)) {
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
    }
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

/* m (a VU block, 36 qw) re-based from its tick's view onto Vt by e (Vp^-1 Vt
 * or Vc^-1 Vt); false (m untouched) when a matrix is singular or the result
 * is not finite */
static bool camRebase(float (*m)[4], const double *e, int mats, const double *l)
{
    double s[16], is[16], se[16], w[16], iw[16], k[16], tmp[16], x[16];
    float out[16][4];
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
        mul4d(iw, e, tmp);
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

static int s_rebased; /* S6: VU draws re-based on the blended camera, this frame */

static int s_rebasedCur; /* of them, draws that are cur's */

/* A draw that is cur's (unmatched, mismatched, jumped, unkeyed): cur's
 * object through the blended camera */
static void camCurDraw(RdCmd *c)
{
    if (!s_cam.on || s_cam.still) {
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
    if (how != CAM_NONE && camRebase(vo, s_cam.ec, camModelMats(c->type, c->b[0]),
                                     how == CAM_FULL && s_cam.zoom ? s_cam.lc : NULL)) {
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
    uint8_t type, list, used, _pad;
    int32_t head, tail, cursor; /* prev's occurrences, in order (Node.next) */
} Slot;

typedef struct Node {
    uint32_t list, index;
    int32_t next;
    int32_t out; /* I1: the index of its match in s_out's list, -1: unmatched */
} Node;

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
    case RDC_OVERLAY_TEXT: /* package DEF */
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

/* the next unmatched occurrence in prev of cur's draw c, index i in list l */
static const RdCmd *matchOf(const RdFrame *prev, const RdCmd *c, int l, uint32_t i)
{
    Slot *s = slotFind(c, l, false);
    if (!s || s->cursor < 0) {
        return NULL;
    }
    Node *nd = &s_nodes[s->cursor];
    s->cursor = nd->next;
    nd->out = (int32_t)i;
    return &prev->lists[nd->list].cmds[nd->index];
}

/* ------------------------------------------------------------ per draw */

enum { R_LERP = 0, R_MISMATCH, R_JUMP };

/* S2: the last R_MISMATCH's RD_MISMATCH_*, and whether the last R_LERP of a
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

/* R7d: two mesh ids a draw may blend across: the same mesh, or meshes of
 * one layout (a morphing part's two packets, which the game draws in
 * alternate frames; a mesh rebuilt after an eviction).  v0.4.1 (M0): a
 * replaced mesh (rd_CreateVuMeshReplacement) keeps its id for as long as it
 * lives, so a part drawn with its replacement in both frames blends as
 * itself; across a pack switch (rd_VuMeshRetire, a new id) the layouts
 * usually differ and the draw falls back to no blend, as after any change
 * of vertex count. */
static bool sameMesh(uint32_t a, uint32_t b)
{
    if (a == b) {
        return true;
    }
    const RdMeshRec *x = rd__MeshRec(a), *y = rd__MeshRec(b);
    return x && y && x->vu && y->vu && x->vertexCount == y->vertexCount &&
           x->qwPerVertex == y->qwPerVertex && x->batchCount == y->batchCount &&
           x->indexCount == y->indexCount;
}

static int blendVu(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    s_rotated = 0;
    s_drawTurn = 0.0;
    if (pc->u[2] != cc->u[2]) {
        return mismatch(RD_MISMATCH_SIZE);
    }
    /* S2: the program must match; the MSCAL code and clip mode (b[1], b[2])
     * change when a part crosses the edge of the screen (REGION against
     * SCISSOR, reg_clipPacketBoundingBox) and say nothing about the data */
    if (pc->b[0] != cc->b[0] ||
        (rd__S2Legacy() && (pc->b[1] != cc->b[1] || pc->b[2] != cc->b[2]))) {
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
        if (isNormalProg(cc->b[0]) && worldJump(vp, (const float (*)[4])vc)) {
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

    /* S6: both ticks' blocks through the blended camera (the camera parts
     * then agree; only the object's own motion is left to blend) */
    float vpr[36][4];
    if (s_cam.on && !s_cam.still && camOf(vp, s_cam.vp, NULL) != CAM_NONE &&
        camOf((const float (*)[4])vc, s_cam.vc, s_cam.pc) != CAM_NONE) {
        const int mats = camModelMats(cc->type, cc->b[0]);
        float vcr[36][4];
        memcpy(vpr, vp, sizeof(vpr));
        memcpy(vcr, vc, sizeof(vcr));
        /* the projections blend with the block (linear in them) */
        if (camRebase(vpr, s_cam.ep, mats, NULL) && camRebase(vcr, s_cam.ec, mats, NULL)) {
            memcpy(vc, vcr, sizeof(vc));
            memcpy(vo[4], vc[4], 24 * sizeof(vo[0])); /* qw 4..27 */
            vp = (const float (*)[4])vpr;
            s_rebased++;
        }
    }
    lerpVuBlock(vo, vp, (const float (*)[4])vc, t, hc.scroll);
    /* S2: a turning object keeps its size half way */
    const bool rot = t > 0.0f && t < 1.0f && !rd__S2Legacy();
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
        for (uint32_t s = 0; s < hc.batchCount && (s + 1) * per <= hc.streamQw; s++) {
            for (uint32_t k = 0; k < len; k++) {
                const uint32_t at = s * per + 3 + k * qpv;
                float tmp[4];
                for (uint32_t q = 0; q < lerpQw; q++) {
                    memcpy(tmp, so[at + q], sizeof(tmp));
                    lerpFloats(so[at + q], sp[at + q], tmp, 4, t);
                }
            }
        }
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
    for (uint32_t i = 0; i < n; i++) {
        o[i].x = lerpI(p[i].x, o[i].x, t);
        o[i].y = lerpI(p[i].y, o[i].y, t);
        o[i].z = lerpU(p[i].z, o[i].z, t);
        for (int k = 0; k < 4; k++) {
            o[i].rgba[k] = lerpB(p[i].rgba[k], o[i].rgba[k], t);
        }
    }
    return R_LERP;
}

/* Package DEF: a deferred text item or op.  An item blends its anchor (grid
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

/* S2: the shadow volumes whose topology changed this tick (rd_ShadowTris:
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

/* V3: Shadow.c's volumes blended prism by prism.  A volume is one closed
 * prism per caster triangle (emitVolumeStrip: ten strip positions over six
 * vertices, eight triangles), each triangle counted +1 or -1 by its facing.
 * rd_ShadowTris sorts the triangles into increments and decrements, so two
 * ticks' volumes of equal counts did not pair triangle for triangle once a
 * face had changed sides (the lerp then joined faces of different prisms
 * and the count no longer netted to 0 outside the shadow), and volumes of
 * different counts (prisms come and go as triangles turn from the light)
 * took cur's shape moved by the median shift, a tick ahead of the caster.
 * Here the tags (rd_internal.h, RD_SHADOW_PRISM_TRIS) regroup each tick's
 * prisms; the two sequences, which keep the caster's triangle order, are
 * aligned by dynamic programming (a pair costs the mean square distance of
 * the top caps, the caster triangle, after the volume's median shift, and
 * needs the same strip sign; RD_INTERP_PRISM_GAP prices an unmatched
 * prism); a pair is blended vertex by vertex and its eight triangles are
 * given the signs of their blended facing, the rule emitVolumeStrip applies
 * (plus where W s0 (-1)^i > 0, W the triangle's winding, i its strip
 * position, s0 the strip's sign; the second triangle of each side quad
 * takes the first's), so every blended prism is closed again.  The prisms
 * of one tick only are those of the nearer tick (prev's below t = 0.5,
 * cur's from it), moved by the volume's shift. */
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

static void prismFree(void)
{
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
        const uint32_t tag = rd__ShadowTag(&v[i * 3]);
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
            rd__SetShadowTag(&pr->v[i], 0);
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
    *out = s_sel[r];
    return true;
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
 * then blends as before V3) */
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
                    b.v[k].x = lerpI(a->v[k].x, src->v[k].x, t);
                    b.v[k].y = lerpI(a->v[k].y, src->v[k].y, t);
                    b.v[k].z = lerpU(a->v[k].z, src->v[k].z, t);
                    moved |= !sameXyz(&b.v[k], &src->v[k]);
                }
                if (moved && b.s0 != 0) {
                    prismSigns(&b, plus);
                }
            } else {
                for (int k = 0; k < 6; k++) {
                    b.v[k].x += dx;
                    b.v[k].y += dy;
                    const double z = (double)b.v[k].z + dz;
                    b.v[k].z = z < 0.0 ? 0u : (z > 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)z);
                }
            }
            for (int i = 2; i < 10; i++) {
                const uint32_t n = o * RD_SHADOW_PRISM_TRIS + (uint32_t)i - 2;
                RdScreenVtx *tri = &s_prismOut[(size_t)n * 3];
                tri[0] = b.v[kStripVtx[i - 2]];
                tri[1] = b.v[kStripVtx[i - 1]];
                tri[2] = b.v[kStripVtx[i]];
                for (int k = 0; k < 3; k++) {
                    rd__SetShadowTag(&tri[k], n + 1);
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
    return R_LERP;
}

static int blendShadow(uint8_t *op, const RdFrame *prev, const RdCmd *pc, RdCmd *cc, float t)
{
    if (pc->b[0] != cc->b[0]) {
        return mismatch(RD_MISMATCH_TOPOLOGY);
    }
    if (cc->b[0] == RD_SHADOW_TRIS && !rd__S2Legacy()) {
        const int r = blendPrisms(op, prev, pc, cc, t); /* V3 */
        if (r >= 0) {
            return r;
        }
    }
    if (pc->u[0] != cc->u[0] || pc->u[2] != cc->u[2] || pc->u[3] != cc->u[3]) {
        if (cc->b[0] == RD_SHADOW_TRIS && !rd__S2Legacy()) {
            return shiftShadow(op, prev, pc, cc, t); /* S2 */
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
    /* rd_ShadowStrip: float[4] (12.4 X, Y, Z, unused) */
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

/* R7d.  A mesh draw names its mesh by id and the replay reads the mesh's
 * live stream, which rd_UpdateVuMesh (the morph path) may have rewritten
 * for a frame recorded after cur.  A draw whose stream in cur (or, matched
 * and blended, in prev) differs from the live one is given a scratch mesh
 * of the same layout holding cur's stream, its vertex positions (qw 0) and
 * normals (qw 1, the lit and skinned layouts) blended from prev's
 * (rd__MeshStreamAt: rd_mesh.c keeps what the retained frames drew).  At
 * most RD_INTERP_SCRATCH draws a present; more keep the live stream (I1:
 * 256, was 64; a scratch mesh holds a copy of its mesh's stream, indices
 * and batches, s_scratchBytes, logged).  A mesh rewritten more than once
 * while a frame records keeps the version a retained frame drew (the first
 * rewrite keeps it: rd_mesh.c keepVersion), and the frame takes the last. */
#define RD_INTERP_SCRATCH 256

/* I1: the bytes the scratch meshes of the last present hold */
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

/* rd_Shutdown frees the meshes (rd__MeshShutdown); the ids are forgotten */
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
    RdMeshRec *m = rd__MeshRec(*id);
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
            rd_DestroyVuMesh((RdMesh){*id});
        }
        *id = rd__VuMeshCreateRaw(stream, c->vertexCount, c->qwPerVertex, c->index, c->indexCount,
                                  c->batches, c->batchCount, "interp");
        m = rd__MeshRec(*id);
        if (!m) {
            *id = 0;
            return NULL;
        }
    }
    rd__VuMeshCopyDrawIndex(m, c); /* issue 25: the source's overlap marks */
    m->materialCount = c->materialCount;
    m->srcQw = c->srcQw;
    s_scratchBytes +=
        size + (uint64_t)c->indexCount * 4u + (uint64_t)c->batchCount * sizeof(RdVuBatchRec);
    m->lastUsed = g_rd.frameCounter;
    m->replaySeen = 0; /* upload again */
    m->transient = 1;  /* P1: from the ring, not the device arena */
    s_scratchUsed++;
    return m;
}

/* the draw o (s_out's copy of a cur mesh draw) and its blended match pc in
 * prev (NULL: none, or not blended); true when its stream was replaced */
static bool morphDraw(RdCmd *o, const RdFrame *prev, const RdCmd *pc, const RdFrame *cur, float t)
{
    const RdMeshRec *mc = rd__MeshRec(o->u[0]);
    if (!mc || !mc->vu) {
        return false;
    }
    const float (*cs)[4] = rd__MeshStreamAt(mc, cur->number);
    if (!cs) {
        cs = (const float (*)[4])mc->stream; /* no kept version: as before */
    }
    const size_t size = (size_t)mc->vertexCount * mc->qwPerVertex * 16;
    const float (*ps)[4] = NULL;
    if (pc && prev && t < 1.0f) {
        const RdMeshRec *mp = rd__MeshRec(pc->u[0]);
        ps = mp ? rd__MeshStreamAt(mp, prev->number) : NULL;
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

/* ------------------------------- unmatched CPU-projected draws (I1)
 *
 * A screen prim (RDC_SCREEN) or a shadow volume (RDC_SHADOW_STRIP) holds GS
 * positions and cannot be re-based on the blended camera.  One that has no
 * partner in the other tick (a packet culled in one tick, a string that
 * changed, a caster that came or went) used to stand at the tick: a draw of
 * cur only was drawn whole from t = 0, a tick early, and one of prev only
 * was never drawn between the ticks, so it vanished a tick early.  Now:
 *
 *   a screen prim whose blend fades with its vertex alpha (alphaFades:
 *   ALPHA's C is As and D is Cd, ABE on, PABE off, and the vertex alpha
 *   reaches As: untextured, TCC RGB or MODULATE) is drawn with its alpha
 *   times t (cur's) or 1 - t (prev's): it fades in or out over the interval;
 *
 *   any other screen prim, and every shadow volume, is drawn on the nearer
 *   tick's side of t = 0.5 (prev's below it, cur's from it), whole.  A
 *   shadow volume only counts the stencil (each
 *   triangle +1 or -1, then RDC_SHADOW_RESOLVE darkens every pixel whose
 *   count is not 0 by one shadow colour for all the volumes), so it has no
 *   alpha to fade and a partial volume would leave the count unbalanced:
 *   the switch is the rule blendPrisms already applies to a prism of one
 *   tick only.
 *
 * A draw of prev only is inserted into s_out's list after the match of the
 * keyed draw before it in prev's list (else before the match of the one
 * after it), bracketed by state commands that set prev's state for it and
 * restore cur's (stateDiff); a shadow volume only next to a volume (or
 * before the list's RDC_SHADOW_RESOLVE), and only when the colour and depth
 * targets agree.  A draw that cannot be placed is not drawn, as before. */
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

static void umFree(void)
{
    free(s_umCur);
    free(s_umIns);
    free(s_umQ);
    free(s_umSt);
    free(s_umNext);
    free(s_umCmds);
    free(s_umWant);
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
    /* package DEF: a deferred text item fades as its quads do (its alpha) */
    return c->type == RDC_SCREEN || c->type == RDC_SHADOW_STRIP ||
           (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_ITEM);
}

/* package DEF: an item's alpha times w (the colour and the rest kept) */
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
        return; /* it stays whole, as before */
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
    for (int l = rd__FirstList((int)f->keep); l < RD_LIST_COUNT && k < n; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0;; i++) {
            while (k < n && q[k].list == (uint32_t)l && q[k].pos == i) {
                out[q[k++].slot] = st;
            }
            if (i >= cl->count) {
                break;
            }
            rd__ApplyState(&st, &cl->cmds[i]);
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
/* at most RD_STATE_DIFF_MAX commands (one per kind below); past it the
 * difference is not expressed and the draw is held */
#define EMIT(cmd)                                                                                  \
    do {                                                                                           \
        if (k >= RD_STATE_DIFF_MAX) {                                                              \
            return false;                                                                          \
        }                                                                                          \
        out[k++] = (cmd);                                                                          \
    } while (0)
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
        EMIT(c);
    }
    if (x->blend != y->blend || x->blendFix != y->blendFix || x->abe != y->abe) {
        RdCmd c = stateCmd(RDC_BLEND);
        c.b[0] = y->blend;
        c.b[1] = y->blendFix;
        c.b[2] = y->abe;
        EMIT(c);
    }

    static const struct {
        uint8_t type;
        size_t off;
    } bytes[] = {{RDC_ZWRITE, offsetof(RdDrawState, zwrite)},
                 {RDC_FBA, offsetof(RdDrawState, fba)},
                 {RDC_PABE, offsetof(RdDrawState, pabe)},
                 {RDC_COLCLAMP, offsetof(RdDrawState, colclamp)},
                 {RDC_TEXA, offsetof(RdDrawState, texa)}};

    for (size_t i = 0; i < sizeof(bytes) / sizeof(bytes[0]); i++) {
        const uint8_t u = ((const uint8_t *)x)[bytes[i].off],
                      v = ((const uint8_t *)y)[bytes[i].off];
        if (u != v) {
            RdCmd c = stateCmd(bytes[i].type);
            c.b[0] = v;
            EMIT(c);
        }
    }
    if (x->magFilter != y->magFilter || x->minFilter != y->minFilter) {
        RdCmd c = stateCmd(RDC_FILTER);
        c.b[0] = y->magFilter;
        c.b[1] = y->minFilter;
        EMIT(c);
    }
    if (x->wrap.s != y->wrap.s || x->wrap.t != y->wrap.t) {
        RdCmd c = stateCmd(RDC_WRAP);
        c.b[0] = y->wrap.s;
        c.b[1] = y->wrap.t;
        EMIT(c);
    }
    bool texOn = false;
    if (a->tex != b->tex || x->texFn != y->texFn || x->tcc != y->tcc ||
        (y->texEnabled && !x->texEnabled)) {
        RdCmd c = stateCmd(RDC_TEXTURE);
        c.u[0] = b->tex;
        c.b[0] = y->texFn;
        c.b[1] = y->tcc;
        EMIT(c);
        texOn = true;
    }
    if (!y->texEnabled && (x->texEnabled || texOn)) {
        EMIT(stateCmd(RDC_TEXTURE_OFF)); /* RDC_TEXTURE above turned it on */
    }
    if (memcmp(a->uvOffset, b->uvOffset, sizeof(a->uvOffset)) != 0) {
        RdCmd c = stateCmd(RDC_UVOFFSET);
        c.f[0] = b->uvOffset[0];
        c.f[1] = b->uvOffset[1];
        EMIT(c);
    }
    if (x->fbmsk != y->fbmsk || x->colorMask != y->colorMask) {
        RdCmd c = stateCmd(RDC_COLORMASK);
        c.u[0] = y->fbmsk;
        EMIT(c);
    }
    if (memcmp(a->scissor, b->scissor, sizeof(a->scissor)) != 0) {
        RdCmd c = stateCmd(RDC_SCISSOR);
        for (int i = 0; i < 4; i++) {
            c.u[i] = (uint32_t)b->scissor[i];
        }
        EMIT(c);
    }
    if (a->gouraud != b->gouraud) {
        RdCmd c = stateCmd(RDC_SHADE);
        c.b[0] = (uint8_t)b->gouraud;
        EMIT(c);
    }
    if (a->aa1 != b->aa1) {
        RdCmd c = stateCmd(RDC_AA1);
        c.b[0] = (uint8_t)b->aa1;
        EMIT(c);
    }
    /* the commands must give b exactly (colorMask is derived from FBMSK) */
    RdStateBlock s = *a;
    for (uint32_t i = 0; i < k; i++) {
        rd__ApplyState(&s, &out[i]);
    }
#undef EMIT
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

/* I1: the unmatched screen prims and shadow volumes at t (see above);
 * after the blends and the morph pass, which keep command indices */
static void unmatchedPass(const RdFrame *prev, float t)
{
    s_umInsN = 0;
    /* prev's unplaced draws and their anchors: s_nodes is in list order */
    uint32_t cand = 0;
    for (uint32_t k = 0; k < s_nodeCount; k++) {
        const Node *nd = &s_nodes[k];
        cand += nd->out < 0 && isProjected(&prev->lists[nd->list].cmds[nd->index]);
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
            if (!isProjected(&prev->lists[nd->list].cmds[nd->index])) {
                continue;
            }
            const uint32_t pos = umPlace(prev, k, lastOut);
            if (pos == ~0u || pos > s_out.lists[nd->list].count ||
                !growTo((void **)&s_umIns, &s_umInsCap, s_umInsN + 1, sizeof(UmIns))) {
                s_umHeld++;
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
        s_umHeld += s_umInsN;
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
            s_umHeld++;
            continue;
        }
        if ((pc->type == RDC_SCREEN && alphaFades(&in.want)) || pc->type == RDC_OVERLAY_TEXT) {
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
                              : pc->type == RDC_OVERLAY_TEXT ? pc->u[2]
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
        uint32_t o = 0, src = 0;
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
        i = j;
    }
}

/* ------------------------------------------------- flap detector (S2)
 *
 * A keyed draw's outcome per frame (blended, mismatched, jumped,
 * unmatched), at each frame's first present.  A draw whose outcome changes
 * from one frame to the next is shown at alpha in one tick and at the tick
 * itself in the other (one tick ahead of its neighbours): it flaps.  The
 * draws are told apart by key, type, list and their ordinal among the
 * draws with that key; the table restarts with each log block.  The block's
 * log line counts the flapping draws (4 or more changes in the block) and
 * names the worst RD_FLAP_LINES with their last outcomes, mismatch reason,
 * program and mesh ids. */
enum { O_LERP = 0, O_MISMATCH, O_JUMP, O_UNMATCHED };

#define RD_FLAP_CAP 8192u
#define RD_FLAP_MIN 4u
#define RD_FLAP_LINES 6

typedef struct FlapEnt {
    uint32_t lo, hi;
    uint8_t type, list, used, last;
    uint16_t ord, changes;
    uint8_t why, prog;
    uint32_t frame;
    uint32_t hist; /* 2 bits per frame, newest in the low bits */
    uint32_t meshP, meshC, frames;
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
    e->hist = e->frame + 1u == frame ? (e->hist << 2 | (uint32_t)outcome) : (uint32_t)outcome;
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
    static const char code[4] = {'L', 'M', 'J', 'U'};
    static const char *const why[5] = {"size", "mesh", "state", "header", "topology"};
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
        char h[17];
        const uint32_t k = best->frames < 16u ? best->frames : 16u;
        for (uint32_t j = 0; j < k; j++) {
            h[j] = code[(best->hist >> (2u * (k - 1u - j))) & 3u];
        }
        h[k] = '\0';
        rd__Log("interp: flapping draw key %08x%08x type %u list %u #%u: %u changes in %u frames, "
                "last %s (oldest first; L blended, M mismatched, J jumped, U unmatched), "
                "mismatch %s, program %u, mesh %u -> %u",
                best->hi, best->lo, best->type, best->list, best->ord, best->changes, best->frames,
                h, why[best->why < 5 ? best->why : 0], best->prog, best->meshP, best->meshC);
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

int rd__InterpSnap(const RdFrame *prev, const RdFrame *cur)
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
        const RdTexRec *t = st->ds.texEnabled ? rd__TexRec(st->tex) : NULL;
        return t && t->kind == RD_TEXKIND_TARGET && t->target == id;
    }
    }
}

/* an RDC_COPY of all of from into to (one size and scale) at the head of
 * list l */
static void headCopy(int l, uint32_t from, uint32_t to, uint32_t sizeOf)
{
    RdCmdList *cl = &s_out.lists[l];
    const RdTargetRec *t = rd__TargetRec(sizeOf);
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

/* The feedback passes' inputs, held at the tick.  A frame that writes
 * FEED128 reads what the frame before left there (the aura's feedback),
 * and every present of a tick replays all of its passes, so every present
 * must start from the FEED128 the tick's first present started from: the
 * first copies FEED128 into FEED_HELD at its head, the later ones copy
 * FEED_HELD back into FEED128 at theirs.  Every present then draws the
 * same picture and leaves FEED128 in the tick's final state, which the
 * next tick reads, as on the PS2.  (Dropping a later present's FEED128
 * writes instead, while its reads still ran, pasted the tick's final
 * FEED128 over the screen: after the black reset of a camera cut,
 * auraInspireAfter's GlobalTimer fill, a black frame.)  v0.4.3 (issue 28):
 * the same for DISPLAY when the frame reads it (the motion blur's old
 * frame, the file header): DISPLAY into DISPLAY_HELD at the first
 * present's head, back at the later ones', so every present draws the
 * tick's FIX over the previous tick's picture. */
static void feedback(int firstOfTick)
{
    const uint32_t feed = rd_Target(RD_TARGET_FEED128).id;
    const uint32_t held = rd_Target(RD_TARGET_FEED_HELD).id;
    const uint32_t disp = rd_Target(RD_TARGET_DISPLAY).id;
    const uint32_t dispHeld = rd_Target(RD_TARGET_DISPLAY_HELD).id;
    const int head = rd__FirstList((int)s_out.keep);
    int fed = 0, read = 0;
    RdStateBlock st = s_out.startState;
    for (int l = head; l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &s_out.lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            if (rd__ApplyState(&st, c)) {
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

const RdFrame *rd__InterpFrame(const RdFrame *prev, const RdFrame *cur, float alpha,
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
    st.snap = (uint32_t)rd__InterpSnap(prev, cur);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
            st.keyed += isKeyedDraw(&s_out.lists[l].cmds[i]);
        }
    }
    s_scratchUsed = 0;
    s_scratchBytes = 0;
    s_doneCount = 0;
    s_umCurN = 0; /* I1 */
    s_umFadeIn = s_umFadeOut = s_umHalfIn = s_umHalfOut = s_umHeld = 0;
    s_lightRot = 0;
    s_pivotMesh = 0; /* S2: the pivots are recomputed per present */
    if (s_track) {
        memset(s_ord, 0, sizeof(s_ord));
    }
    const bool blend = st.snap == RD_SNAP_NONE && t < 1.0f && buildIndex(prev);
    s_cam.on = 0;
    s_rebased = s_rebasedCur = 0;
    if (blend) {
        camSetup(prev, cur, t); /* S6 */
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
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
                RdCmd *c = &s_out.lists[l].cmds[i];
                if (!isKeyedDraw(c)) {
                    camCurDraw(c); /* S6: unkeyed, cur's through the blended camera */
                    continue;
                }
                const RdCmd *pc = matchOf(prev, c, l, i);
                uint8_t *op = outPayload(c);
                if (!pc || !op) {
                    st.missing++;
                    camCurDraw(c);
                    if (!pc && isProjected(c)) {
                        umCurOnly((uint32_t)l, i); /* I1: faded in, or from half way */
                    }
                    if (s_track) {
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
                    r = blendVu(op, prev, pc, c, t);
                    break;
                }
                if (r != R_LERP) {
                    camCurDraw(c); /* S6: the tick's draw, through the blended camera */
                }
                st.lerped += r == R_LERP;
                st.mismatch += r == R_MISMATCH;
                st.jump += r == R_JUMP;
                if (r == R_MISMATCH) {
                    st.why[s_why < 5 ? s_why : 0]++;
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
    /* R7d: the other mesh draws (unkeyed, unmatched, snapped) take cur's
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
        unmatchedPass(prev, t); /* I1: inserts commands, so after the passes above */
    }
    st.rebased = (uint32_t)s_rebased;
    st.rebasedCur = (uint32_t)s_rebasedCur;
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
    /* S2: mismatch reasons, rotation blends, frames whose snap differs
     * from the frame before's (whole-frame flaps) */
    uint64_t why[5], rotated, turned, shifted;
    float maxTurn;
    uint32_t snapFlips, lastSnap;
    uint64_t rebased, rebasedCur; /* S6 */
    /* I1: unmatched screen prims and shadow volumes (faded in, faded out,
     * switched at half way, cur's and prev's; prev's not placed), lit draws
     * whose light matrix turned with the model */
    uint64_t fadeIn, fadeOut, halfIn, halfOut, held, lightRot;
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
    for (int k = 0; k < 5; k++) {
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
    rd__Log("interp: %u frames, %u presents: %u blended; snapped: %u no previous, %u gap, "
            "%u keep, %u cut, %u camera, %u fade, %u history, %u size; keyed draws %llu: "
            "%llu blended, %llu unmatched, %llu mismatched, %llu jumped; %llu mesh streams kept or "
            "blended",
            s_pres.frames, s_pres.presents, n[RD_SNAP_NONE], n[RD_SNAP_NO_PREV], n[RD_SNAP_GAP],
            n[RD_SNAP_KEEP], n[RD_SNAP_CUT], n[RD_SNAP_CAMERA], n[RD_SNAP_FADE], n[RD_SNAP_HISTORY],
            n[RD_SNAP_SIZE], (unsigned long long)s_pres.keyed, (unsigned long long)s_pres.lerped,
            (unsigned long long)s_pres.missing, (unsigned long long)s_pres.mismatch,
            (unsigned long long)s_pres.jump, (unsigned long long)s_pres.morph);
    /* S2: a separate line, so the line above keeps its form */
    const uint32_t flapping = flapReport();
    rd__Log(
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
    /* I1: a third line */
    rd__Log("interp: unmatched screen prims and shadow volumes: %llu faded in, %llu faded out, "
            "%llu of the tick drawn from half way, %llu of the tick before drawn until half way, "
            "%llu of the tick before not placed; %llu lit draws whose lights turned with them; "
            "at most %u of %u scratch meshes in a present (%llu bytes)",
            (unsigned long long)s_pres.fadeIn, (unsigned long long)s_pres.fadeOut,
            (unsigned long long)s_pres.halfIn, (unsigned long long)s_pres.halfOut,
            (unsigned long long)s_pres.held, (unsigned long long)s_pres.lightRot, s_pres.scratchMax,
            RD_INTERP_SCRATCH, (unsigned long long)s_pres.scratchBytesMax);
    const uint32_t number = s_pres.number;
    memset(&s_pres, 0, sizeof(s_pres));
    s_pres.number = number;
}

static void presentReset(void)
{
    memset(&s_pres, 0, sizeof(s_pres));
}

bool rd_InterpolationActive(void)
{
    /* both presets (F2): the Original preset presents its PS2-exact tick
     * pictures and the blended ones between them */
    return g_rd.inited && g_rd.settings.interpolate != 0;
}

bool rd_Present(float alpha)
{
    if (!rd_InterpolationActive() || !g_rd.hasDevice || g_rd.videoShown) {
        return false;
    }
    const RdFrame *cur = rd__LastFrame();
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
     * and since v0.4.3 DISPLAY: issue 28), the later ones start from them
     * again (feedback) */
    const int first = cur->number != s_pres.number;
    s_pres.number = cur->number;
    s_pres.presents++;
    if (cur->keep) {
        /* a keep frame draws lists 11..12 over DISPLAY: once; the other
         * presents show DISPLAY again (an empty keep frame) */
        if (first) {
            return rd__ReplayFrame(cur, 1, true);
        }
        static RdFrame empty;
        return rd__ReplayFrame(&empty, 1, true);
    }
    const double t0 = rd__NowMs();
    s_track = first; /* S2: the flap detector, once a frame */
    const RdFrame *f = rd__InterpFrame(rd__PrevFrame(), cur, alpha, first, &s_pres.stats);
    s_track = 0;
    rd__PerfInterpMs(rd__NowMs() - t0); /* P1: charged to the replay below */
    rd__PerfAlpha(alpha, first);
    if (first) {
        presentLog();
    }
    return f != NULL && rd__ReplayFrame(f, 0, true);
}

/* ---------------------------------------------------- present clock (S2) */

float rd_PresentClockAlpha(RdPresentClock *c, double nowMs, double tickAtMs, double tickMs,
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
