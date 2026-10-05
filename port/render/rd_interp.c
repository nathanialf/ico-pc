/* rd_interp.c: presentation between simulation ticks (renderer wave 7, R7b;
 * docs/port/RENDER_API.md section 20).
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
 *                           28..35 (the light matrices); the bones; not qw
 *                           0, 1, 3 (constants and the GIF tag)
 *   RDC_GRID                the VU block and each vertex's position (and
 *                           normal when lit); the strip headers, colours
 *                           and STs are cur's
 *   RDC_PARTICLES           the VU block and each particle's position and
 *                           size; UV, grey and alpha are cur's
 *   RDC_SCREEN              XY, Z and colour of each vertex; STQ is cur's
 *   RDC_SHADOW_STRIP        XY and Z of each vertex
 * Matrices blend element by element, except (package S2) a normal
 * program's model matrices and a skinned draw's bones, which blend as a
 * slerped rotation and a lerped stretch about a pivot (rotateModel,
 * rotateBone): element by element a turn of 77 degrees in a tick, which
 * stage 3 has, drew a bone 22 % short half way.  A float pair with
 * different bits that is not both finite keeps cur's.
 *
 * A keyed draw snaps (is cur's) when prev has no match, when the payload's
 * shape differs (mesh, program, batch range, bone, vertex or particle
 * count, prim type; since S2 neither a mesh's code and clip mode nor a
 * shadow volume's topology, which shiftShadow moves instead), or when it
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
 * aura's feedback through FEED128: its sprites that write FEED128 run in
 * the first present of a tick only, so the buffer advances once per tick
 * as on the PS2 and the other presents paste the held buffer.
 *
 * Feedback per present: the motion blur's sprite (DISPLAY back into SCENE
 * with LERP FIX) runs at every present.  On the PS2 DISPLAY keeps a =
 * (128 - FIX) / 128 of itself per tick T; a present that stands for dt
 * ticks uses the FIX that keeps a^dt (rd__BlurFeedbackFix: FIX' = 128 -
 * 128 a^dt, rounded), so k presents of dt = 1/k keep a per tick, whatever
 * the present rate.  dt is the alpha advanced since the previous present
 * (plus whole ticks when frames were closed in between), at least 1/256,
 * at most 4.
 */
#include <math.h>
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

void rd__InterpShutdown(void)
{
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

/* Texture.c's UV scroll keeps uOfs/vOfs in (-1, 1] by adding or taking 2
 * (tex_textureAnimation); a step of more than 1 is that wrap. */
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

/* VuCB.mem: qw 2 xy the UV scroll (wrap undone), zw; qw 4..35 matrices and
 * lights.  qw 0, 1, 3 are constants and a GIF tag: cur's. */
static void lerpVuBlock(float (*o)[4], const float (*p)[4], const float (*c)[4], float t)
{
    for (int k = 0; k < 2; k++) {
        if (!sameBits(p[2][k], c[2][k]) && isfinite(p[2][k]) && isfinite(c[2][k])) {
            o[2][k] = lerpWrap(p[2][k], c[2][k], t);
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

/* A normal program's model matrices (qw 16..19 model to screen, 20..23
 * model to clip, 24..27 model to view) are each a camera part times the
 * object's model to world W.  W = S^-1 (qw 16..19), S the common block's
 * world to screen (qw 4..7); W blends as a rotation (rd__BlendAffine), each
 * camera part C = M W^-1 element-wise like the common block, and the result
 * is C(t) W(t).  An element-wise blend of M shortens the axes of a turning
 * object by cos(theta / 2) half way (8 % at a 45 degree turn in a tick).
 * o holds the element-wise blend already; false: it stays. */
static bool rotateModel(float (*o)[4], const float (*p)[4], const float (*c)[4], float t,
                        const double *pivot)
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
        memcpy(o[16], c[16], 12 * sizeof(o[0])); /* a flip, not a motion: the tick's */
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
    return true;
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

/* the next unmatched occurrence in prev of cur's draw c in list l */
static const RdCmd *matchOf(const RdFrame *prev, const RdCmd *c, int l)
{
    Slot *s = slotFind(c, l, false);
    if (!s || s->cursor < 0) {
        return NULL;
    }
    const Node *nd = &s_nodes[s->cursor];
    s->cursor = nd->next;
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
 * alternate frames; a mesh rebuilt after an eviction) */
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

    lerpVuBlock(vo, vp, (const float (*)[4])vc, t);
    /* S2: a turning object keeps its size half way */
    const bool rot = t > 0.0f && t < 1.0f && !rd__S2Legacy();
    if (cc->type == RDC_MESH && isNormalProg(cc->b[0]) && rot) {
        double centre[3];
        const bool hasCentre = meshCentroid(cc->u[0], centre);
        s_rotated |= rotateModel(vo, vp, (const float (*)[4])vc, t, hasCentre ? centre : NULL);
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

static int blendShadow(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    if (pc->b[0] != cc->b[0]) {
        return mismatch(RD_MISMATCH_TOPOLOGY);
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
 * most RD_INTERP_SCRATCH draws a present; more keep the live stream. */
#define RD_INTERP_SCRATCH 64

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
    m->materialCount = c->materialCount;
    m->srcQw = c->srcQw;
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

/* The feedback passes for a present standing for dt ticks: the motion
 * blur's FIX, and the aura's FEED128 writes only in a tick's first present. */
static void feedback(float dt, int firstOfTick)
{
    const uint32_t feed = rd_Target(RD_TARGET_FEED128).id;
    RdStateBlock st = s_out.startState;
    for (int l = rd__FirstList((int)s_out.keep); l < RD_LIST_COUNT; l++) {
        RdCmdList *cl = &s_out.lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            RdCmd *c = &cl->cmds[i];
            if (rd__ApplyState(&st, c) || c->type != RDC_POST_STUB) {
                continue;
            }
            if (c->b[0] == RD_POST_MOTION_BLUR) {
                RdPostRec *r = (RdPostRec *)payloadAt(&s_out, c->u[1], sizeof(RdPostRec));
                if (r) {
                    r->scalar[2] = dt;
                }
            } else if (c->b[0] == RD_POST_AURA && !firstOfTick && st.color == feed) {
                c->type = RDC_NOP;
            }
        }
    }
}

const RdFrame *rd__InterpFrame(const RdFrame *prev, const RdFrame *cur, float alpha, float dt,
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
    s_doneCount = 0;
    s_pivotMesh = 0; /* S2: the pivots are recomputed per present */
    if (s_track) {
        memset(s_ord, 0, sizeof(s_ord));
    }
    const bool blend = st.snap == RD_SNAP_NONE && t < 1.0f && buildIndex(prev);
    if (blend) {
        if (prev->hasCamera && cur->hasCamera) {
            lerpCamera(&s_out.camera, &prev->camera, &cur->camera, t);
        }
        if (prev->hasVu && cur->hasVu) {
            lerpFloats(s_out.vu.screenView, prev->vu.screenView, cur->vu.screenView, 16, t);
            lerpFloats(s_out.vu.viewport, prev->vu.viewport, cur->vu.viewport, 16, t);
            lerpFloats(s_out.vu.invView, prev->vu.invView, cur->vu.invView, 16, t);
        }
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
                RdCmd *c = &s_out.lists[l].cmds[i];
                if (!isKeyedDraw(c)) {
                    continue;
                }
                const RdCmd *pc = matchOf(prev, c, l);
                uint8_t *op = outPayload(c);
                if (!pc || !op) {
                    st.missing++;
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
                default:
                    r = blendVu(op, prev, pc, c, t);
                    break;
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
    feedback(dt, firstOfTick);
    if (stats) {
        *stats = st;
    }
    return &s_out;
}

/* ------------------------------------------------------------- present */

static struct {
    uint32_t number; /* the frame of the last present */
    float alpha;     /* and its alpha */
    RdInterpStats stats;
    /* the log: per frame, at its first present */
    uint32_t frames, presents, snaps[RD_SNAP_COUNT];
    uint64_t keyed, lerped, missing, mismatch, jump, morph;
    /* S2: mismatch reasons, rotation blends, frames whose snap differs
     * from the frame before's (whole-frame flaps) */
    uint64_t why[5], rotated, turned, shifted;
    float maxTurn;
    uint32_t snapFlips, lastSnap;
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
    s_pres.maxTurn = fmaxf(s_pres.maxTurn, st->maxTurn);
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
        "(%u or more outcome changes); %u frames changed between blended and snapped",
        (unsigned long long)s_pres.why[0], (unsigned long long)s_pres.why[1],
        (unsigned long long)s_pres.why[2], (unsigned long long)s_pres.why[3],
        (unsigned long long)s_pres.why[4], (unsigned long long)s_pres.rotated,
        (unsigned long long)s_pres.turned, (double)s_pres.maxTurn,
        (unsigned long long)s_pres.shifted, flapping, RD_FLAP_MIN, s_pres.snapFlips);
    const uint32_t number = s_pres.number;
    const float alpha = s_pres.alpha;
    memset(&s_pres, 0, sizeof(s_pres));
    s_pres.number = number;
    s_pres.alpha = alpha;
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
    const int first = cur->number != s_pres.number;
    float dt;
    if (first) {
        const uint32_t k =
            s_pres.number && cur->number > s_pres.number ? cur->number - s_pres.number : 1u;
        dt = (float)k - s_pres.alpha + alpha;
    } else {
        dt = alpha - s_pres.alpha;
    }
    dt = dt < 1.0f / 256.0f ? 1.0f / 256.0f : (dt > 4.0f ? 4.0f : dt);
    s_pres.number = cur->number;
    s_pres.alpha = alpha;
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
    const RdFrame *f = rd__InterpFrame(rd__PrevFrame(), cur, alpha, dt, first, &s_pres.stats);
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
