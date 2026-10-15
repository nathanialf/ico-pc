/* rd_camera_blend.h: the camera of a blended picture, in one place.
 *
 * The presenter draws the pictures between two ticks through a camera
 * blended from the two ticks' cameras (rd_interp.c camSetup): the inverse
 * views (view to world) are blended as affine matrices, the 3 x 3 split by
 * polar decomposition into a rotation, slerped, and a stretch, lerped, and
 * the eye lerped; the projection is lerped element by element.  The game's
 * cull (ico2/seki/src/GsBase.c) has to know that camera too, so that a part
 * the blended pictures show is not culled at the tick.  These functions are
 * the only implementation: rd_interp.c's rd__blend_affine, invert4d and
 * mul4d call them, and GsBase.c calls them directly.
 *
 * Header only (static inline), no renderer dependency: the game's files
 * include it in every build, the headless one too.  Matrices are
 * column-major 4 x 4 (m[c * 4 + r]), as the game's and the renderer's. */
#ifndef PORT_RENDER_RD_CAMERA_BLEND_H
#define PORT_RENDER_RD_CAMERA_BLEND_H

#include <math.h>
#include <string.h>

/* The presenter's limits for one tick's camera step (world units are the
 * game's centimetres): a camera that turned further or whose eye moved
 * further in a tick is a cut, and the pictures snap to the tick instead of
 * blending (rd_interp.c rd__interp_snap).  The game's cull uses the same
 * limits: past them it culls against the tick's camera alone, as the
 * pictures are drawn with that camera alone. */
#define RD_INTERP_CAMERA_MOVE 300.0f /* the eye, per tick */
#define RD_INTERP_CAMERA_TURN 30.0f  /* degrees, per tick */

/* The cull's margin while pictures are blended: the game's cull frustum
 * (GsBase.c gsbHostCullMargin) is 1 + RD_CULL_MARGIN times the picture's
 * half-width and half-height, so a part just past the picture's edge is
 * still registered at the tick.  The presenter's test of whether a draw of
 * the tick before alone can have gone because of that cull (rd_interp.c
 * prevAlone) uses the same frustum, so the two agree on one number. */
#define RD_CULL_MARGIN 0.12f

/* The inverse of a column-major 4 x 4 (cofactors); 0 when singular */
static inline int rdcb_invert4d(const double *a, double *o)
{
    double inv[16];
    double det;
    int i;

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
    det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (!(fabs(det) > 1e-30) || !isfinite(det)) {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        o[i] = inv[i] / det;
    }
    return 1;
}

/* o = a b, column-major 4 x 4 (o may not alias a or b) */
static inline void rdcb_mul4d(const double *a, const double *b, double *o)
{
    int c, r, k;

    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            double v = 0.0;

            for (k = 0; k < 4; k++) {
                v += a[k * 4 + r] * b[c * 4 + k];
            }
            o[c * 4 + r] = v;
        }
    }
}

static inline double rdcb_det3(const double m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* The rotation of the polar decomposition A = R S (Higham's iteration R <-
 * (R + R^-T) / 2), m[row][col]; 0 when singular or not converged */
static inline int rdcb_polar_rotation(const double a[3][3], double r[3][3])
{
    int it, i, j;

    memcpy(r, a, sizeof(double) * 9);
    for (it = 0; it < 64; it++) {
        const double d = rdcb_det3((const double (*)[3])r);
        double it_[3][3]; /* (R^-1)^T = cofactor matrix / det */
        double diff = 0.0;

        if (!(fabs(d) > 1e-30) || !isfinite(d)) {
            return 0;
        }
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                const int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;

                it_[i][j] = (r[i1][j1] * r[i2][j2] - r[i1][j2] * r[i2][j1]) / d;
            }
        }
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                const double v = 0.5 * (r[i][j] + it_[i][j]);

                diff += fabs(v - r[i][j]);
                r[i][j] = v;
            }
        }
        if (diff < 1e-13) {
            return 1;
        }
    }
    return 0;
}

static inline void rdcb_quat_from_rot(const double m[3][3], double q[4])
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

static inline void rdcb_rot_from_quat(const double q[4], double m[3][3])
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

/* The slerp from a to b0 at t: the short way (the turn of at most 180
 * degrees between the two rotations), or with longWay the long way (b0's
 * other sign: the turn of 360 degrees less that, the other way round) */
static inline void rdcb_slerp_way(const double a[4], const double b0[4], double t, int longWay,
                                  double o[4])
{
    double b[4];
    double d;
    double wa = 1.0 - t, wb = t;
    double n = 0.0;
    int i;

    b[0] = b0[0];
    b[1] = b0[1];
    b[2] = b0[2];
    b[3] = b0[3];
    d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (longWay ? d > 0.0 : d < 0.0) { /* the short way, or the long way */
        d = -d;
        for (i = 0; i < 4; i++) {
            b[i] = -b[i];
        }
    }
    if (d < 0.9999) {
        const double th = acos(d), s = sin(th);
        wa = sin((1.0 - t) * th) / s;
        wb = sin(t * th) / s;
    }
    for (i = 0; i < 4; i++) {
        o[i] = wa * a[i] + wb * b[i];
        n += o[i] * o[i];
    }
    n = sqrt(n);
    for (i = 0; i < 4; i++) {
        o[i] /= n;
    }
}

static inline void rdcb_slerp(const double a[4], const double b0[4], double t, double o[4])
{
    rdcb_slerp_way(a, b0, t, 0, o);
}

static inline int rdcb_is_affine(const double *m)
{
    const double s = fabs(m[0]) + fabs(m[5]) + fabs(m[10]) + 1.0;

    return fabs(m[3]) < 1e-5 * s && fabs(m[7]) < 1e-5 * s && fabs(m[11]) < 1e-5 * s &&
           fabs(m[15] - 1.0) < 1e-5;
}

/* The ways rdcb_blend_affine_turn blended a turn */
enum {
    RDCB_WAY_SHORT = 0, /* the short way, no turn continued (none given, or none near) */
    RDCB_WAY_KEPT,      /* the short way, continuing the turn given */
    RDCB_WAY_LONG       /* the long way, continuing the turn given */
};

/* The turn from rotation qp to rotation qc in qp's own frame (qp^-1 qc,
 * the sign of the shorter of its two ways) as a rotation vector: the axis
 * times the angle in radians.  A turn's axis is the same in the object's
 * frame from tick to tick when it spins about an axis of its own (a wheel
 * about its axle), whatever its carrier does. */
static inline void rdcb_turn_vector(const double qp[4], const double qc[4], double v[3])
{
    /* conj(qp) qc: w = pw cw + pv . cv, v = pw cv - cw pv - pv x cv */
    double w = qp[3] * qc[3] + qp[0] * qc[0] + qp[1] * qc[1] + qp[2] * qc[2];
    double x = qp[3] * qc[0] - qc[3] * qp[0] - (qp[1] * qc[2] - qp[2] * qc[1]);
    double y = qp[3] * qc[1] - qc[3] * qp[1] - (qp[2] * qc[0] - qp[0] * qc[2]);
    double z = qp[3] * qc[2] - qc[3] * qp[2] - (qp[0] * qc[1] - qp[1] * qc[0]);
    double s, a;

    if (w < 0.0) { /* the short way */
        w = -w;
        x = -x;
        y = -y;
        z = -z;
    }
    s = sqrt(x * x + y * y + z * z);
    if (!(s > 1e-12) || !isfinite(s)) {
        v[0] = v[1] = v[2] = 0.0;
        return;
    }
    a = 2.0 * atan2(s, w); /* 0 .. pi */
    v[0] = x / s * a;
    v[1] = y / s * a;
    v[2] = z / s * a;
}

/* Rotation-aware blending of an affine 4 x 4 (column-major, w row 0 0 0 1)
 * at t (0 = p, 1 = c): the 3 x 3 polar-decomposed into a rotation (slerped)
 * and a stretch (lerped); the image of pivot (x, y, z; NULL: the origin)
 * lerped, so the blended matrix turns about it.  0 (o untouched) when
 * either matrix is not affine, is singular, or the two have opposite
 * handedness.
 *
 * The rotation turns the short way (at most 180 degrees) unless prefer is
 * given: prefer is a turn to continue (a rotation vector in the frame of
 * the tick before, rdcb_turn_vector's form, radians), the turn the same
 * object made between the two ticks before.  Of the short way and the long
 * way (the other sign of qc: 360 degrees less the short turn, about the
 * opposite axis) the one nearer prefer is taken when it lies within keepDeg
 * degrees of it (the distance between the two rotation vectors) and turns
 * at most mostDeg degrees (under 350): a wheel turning 170 then 190 degrees
 * a tick keeps turning forward instead of blending back 170.  Otherwise, or
 * without prefer, the short way.
 *
 * turnDeg (may be NULL) receives the turn blended in degrees (the short
 * turn, or 360 less it the long way), 0 when the matrices are not affine;
 * turnVec (may be NULL) the turn blended as a rotation vector (radians, the
 * frame of p), zeros when the matrices are not affine; way (may be NULL)
 * RDCB_WAY_*.  With prefer NULL the blend is the plain short-way one bit
 * for bit (rdcb_blend_affine). */
static inline int rdcb_blend_affine_turn(const double *p, const double *c, double t,
                                         const double *pivot, const double *prefer, double keepDeg,
                                         double mostDeg, double *o, double *turnDeg,
                                         double *turnVec, int *way)
{
    double ap[3][3], ac[3][3], rp[3][3], rc[3][3];
    double sp[3][3], sc[3][3], qp[4], qc[4], qt[4], rt[3][3];
    double out[16];
    double x0[3];
    double dp, dc, sign;
    int i, j, k;

    int longWay = 0;

    if (turnDeg) {
        *turnDeg = 0.0;
    }
    if (turnVec) {
        turnVec[0] = turnVec[1] = turnVec[2] = 0.0;
    }
    if (way) {
        *way = RDCB_WAY_SHORT;
    }
    if (!rdcb_is_affine(p) || !rdcb_is_affine(c)) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            ap[i][j] = p[j * 4 + i];
            ac[i][j] = c[j * 4 + i];
        }
    }
    if (!rdcb_polar_rotation((const double (*)[3])ap, rp) ||
        !rdcb_polar_rotation((const double (*)[3])ac, rc)) {
        return 0;
    }
    dp = rdcb_det3((const double (*)[3])rp);
    dc = rdcb_det3((const double (*)[3])rc);
    if ((dp < 0.0) != (dc < 0.0)) {
        return 0; /* a mirror between the two: no rotation joins them */
    }
    sign = dp < 0.0 ? -1.0 : 1.0; /* A = (sign R) (sign S) */
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            rp[i][j] *= sign;
            rc[i][j] *= sign;
        }
    }
    for (i = 0; i < 3; i++) { /* S = R^T A (with the sign folded in) */
        for (j = 0; j < 3; j++) {
            double vp = 0.0, vc = 0.0;

            for (k = 0; k < 3; k++) {
                vp += rp[k][i] * ap[k][j];
                vc += rc[k][i] * ac[k][j];
            }
            sp[i][j] = vp;
            sc[i][j] = vc;
        }
    }
    rdcb_quat_from_rot((const double (*)[3])rp, qp);
    rdcb_quat_from_rot((const double (*)[3])rc, qc);
    if (turnDeg) {
        const double d = fabs(qp[0] * qc[0] + qp[1] * qc[1] + qp[2] * qc[2] + qp[3] * qc[3]);

        *turnDeg = 2.0 * acos(d > 1.0 ? 1.0 : d) * 180.0 / 3.14159265358979323846;
    }
    if (prefer || turnVec) {
        const double pi = 3.14159265358979323846;
        double vs[3], vl[3], ds = 0.0, dl = 0.0, as, al;
        int chosen = RDCB_WAY_SHORT;

        rdcb_turn_vector(qp, qc, vs);
        as = sqrt(vs[0] * vs[0] + vs[1] * vs[1] + vs[2] * vs[2]);
        al = 2.0 * pi - as; /* the long way's angle, about the opposite axis */
        for (i = 0; i < 3; i++) {
            vl[i] = as > 1e-9 ? -vs[i] / as * al : 0.0;
        }
        if (prefer) {
            const double keep = keepDeg * pi / 180.0;
            const double most = (mostDeg < 350.0 ? mostDeg : 350.0) * pi / 180.0;

            for (i = 0; i < 3; i++) {
                ds += (vs[i] - prefer[i]) * (vs[i] - prefer[i]);
                dl += (vl[i] - prefer[i]) * (vl[i] - prefer[i]);
            }
            ds = sqrt(ds);
            dl = sqrt(dl);
            if (as > 1e-9 && dl < ds) {
                if (dl <= keep && al <= most) {
                    chosen = RDCB_WAY_LONG;
                }
            } else if (ds <= keep && as <= most) {
                chosen = RDCB_WAY_KEPT;
            }
        }
        longWay = chosen == RDCB_WAY_LONG;
        if (turnVec) {
            for (i = 0; i < 3; i++) {
                turnVec[i] = longWay ? vl[i] : vs[i];
            }
        }
        if (turnDeg && longWay) {
            *turnDeg = 360.0 - *turnDeg;
        }
        if (way) {
            *way = chosen;
        }
    }
    rdcb_slerp_way(qp, qc, t, longWay, qt);
    rdcb_rot_from_quat(qt, rt);
    x0[0] = pivot ? pivot[0] : 0.0;
    x0[1] = pivot ? pivot[1] : 0.0;
    x0[2] = pivot ? pivot[2] : 0.0;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            double v = 0.0;

            for (k = 0; k < 3; k++) {
                v += rt[i][k] * ((1.0 - t) * sp[k][j] + t * sc[k][j]);
            }
            out[j * 4 + i] = v;
        }
    }
    /* the pivot's image follows the straight line between its two ticks'
     * images; the blended 3 x 3 turns the rest about it */
    for (i = 0; i < 3; i++) {
        double yp = p[12 + i], yc = c[12 + i], ax = 0.0;

        for (j = 0; j < 3; j++) {
            yp += p[j * 4 + i] * x0[j];
            yc += c[j * 4 + i] * x0[j];
            ax += out[j * 4 + i] * x0[j];
        }
        out[12 + i] = (1.0 - t) * yp + t * yc - ax;
    }
    out[3] = out[7] = out[11] = 0.0;
    out[15] = 1.0;
    for (i = 0; i < 16; i++) {
        if (!isfinite(out[i])) {
            return 0;
        }
    }
    memcpy(o, out, sizeof(out));
    return 1;
}

/* rdcb_blend_affine_turn the short way: turnDeg (may be NULL) receives the
 * turn between the two rotations in degrees, 0 when the matrices are not
 * affine.  The camera's blend (rdcb_blend_view, rd_interp.c camSetup). */
static inline int rdcb_blend_affine(const double *p, const double *c, double t, const double *pivot,
                                    double *o, double *turnDeg)
{
    return rdcb_blend_affine_turn(p, c, t, pivot, NULL, 0.0, 0.0, o, turnDeg, NULL, NULL);
}

/* The blended camera's view at t, as rd_interp.c camSetup builds it: the
 * two views inverted (view to world), blended about the origin, and the
 * result inverted back.  0 when either view is singular or the blend fails
 * (camSetup then draws without a blended camera). */
static inline int rdcb_blend_view(const double *vp, const double *vc, double t, double *vt)
{
    double ip[16], ic[16], it[16];

    if (!rdcb_invert4d(vp, ip) || !rdcb_invert4d(vc, ic) ||
        !rdcb_blend_affine(ip, ic, t, NULL, it, NULL) || !rdcb_invert4d(it, vt)) {
        return 0;
    }
    return 1;
}

/* The blended camera's projection at t: element by element, as camSetup's
 * Pt = lerp(Pp, Pc) for the easing zoom */
static inline void rdcb_blend_proj(const double *pp, const double *pc, double t, double *pt)
{
    int i;

    for (i = 0; i < 16; i++) {
        pt[i] = (1.0 - t) * pp[i] + t * pc[i];
    }
}

/* The sum of the products of the two upper-left 3 x 3s' elements */
static inline float rdcb_frob3(const float *a, const float *b)
{
    float s = 0.0f;
    int c, r;

    for (c = 0; c < 3; c++) {
        for (r = 0; r < 3; r++) {
            s += a[c * 4 + r] * b[c * 4 + r];
        }
    }
    return s;
}

/* The eye of a column-major view matrix: -R^T t */
static inline void rdcb_view_eye(const float *v, float eye[3])
{
    int j;

    for (j = 0; j < 3; j++) {
        eye[j] = -(v[j * 4 + 0] * v[12] + v[j * 4 + 1] * v[13] + v[j * 4 + 2] * v[14]);
    }
}

/* 1 when the camera turned more than turnDeg degrees or its eye moved more
 * than move units between the views vp and vc (world to view,
 * column-major); 0 when either view's 3 x 3 is zero.  rdcb_camera_jump
 * below is it at the presenter's limits; GsBase.c's cull also asks it at
 * half of them (gsbHostCullViews).  The float square root and cosine go
 * through the double functions: the game's files map sqrtf and cosf to
 * their own (port/compat/ico_libc.h), the renderer's do not, and a float's
 * square root rounded from the double's is the float square root. */
static inline int rdcb_camera_beyond(const float *vp, const float *vc, float turnDeg, float move)
{
    const float fp = rdcb_frob3(vp, vp), fc = rdcb_frob3(vc, vc);
    float cosT, a[3], b[3], x, y, z;

    if (!(fp > 0.0f) || !(fc > 0.0f)) {
        return 0;
    }
    /* cos of the turn between the two rotations, scale removed */
    cosT = (3.0f * rdcb_frob3(vp, vc) / (float)sqrt((double)(fp * fc)) - 1.0f) * 0.5f;
    if (cosT < (float)cos((double)(turnDeg * 3.14159265f / 180.0f))) {
        return 1;
    }
    rdcb_view_eye(vp, a);
    rdcb_view_eye(vc, b);
    x = a[0] - b[0];
    y = a[1] - b[1];
    z = a[2] - b[2];
    return (float)sqrt((double)(x * x + y * y + z * z)) > move;
}

/* 1 when the camera turned more than RD_INTERP_CAMERA_TURN or its eye moved
 * more than RD_INTERP_CAMERA_MOVE between the views vp and vc (world to
 * view, column-major): the presenter's cut rule (rd_interp.c cameraJump),
 * in float as it computes it, so the cull and the presenter agree on the
 * same two matrices. */
static inline int rdcb_camera_jump(const float *vp, const float *vc)
{
    return rdcb_camera_beyond(vp, vc, RD_INTERP_CAMERA_TURN, RD_INTERP_CAMERA_MOVE);
}

#endif /* PORT_RENDER_RD_CAMERA_BLEND_H */
