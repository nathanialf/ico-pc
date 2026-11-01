/*
 * port/game/camera_wall.c
 *
 * The blended camera's path through the stage (camera_wall.h): the rule and
 * the segment tests against one wall and one floor, on plain floats in the
 * element's own frame.  No game header: the live walk over the stage's
 * collision is camera_wall_stage.c's.
 */
#include <math.h>
#include <stddef.h>

#include "camera_wall.h"
#include "rd_camera_blend.h" /* RD_INTERP_CAMERA_MOVE (header only) */

int ico_camera_wall_cross_wall(const float a[3], const float b[3], const IcoCameraWallQuad *w,
                               float r)
{
    /* the signed distances in front of the wall's plane (clip_wall_1's pa[2]
       and pb[2], through the corner it measures from) */
    const double sa = ((double)a[0] - w->x) * w->nx + ((double)a[2] - w->z) * w->nz;
    const double sb = ((double)b[0] - w->x) * w->nx + ((double)b[2] - w->z) * w->nz;
    double p[3], u, uu, lo, hi, t;

    if ((sa > 0.0) == (sb > 0.0)) {
        return 0; /* both in front, or both on the plane or behind it */
    }
    t = sa / (sa - sb);
    p[0] = a[0] + ((double)b[0] - a[0]) * t;
    p[1] = a[1] + ((double)b[1] - a[1]) * t;
    p[2] = a[2] + ((double)b[2] - a[2]) * t;
    /* along the wall (clip_wall_1's pa[0]), 0 at the corner, length at the
       other end */
    u = (p[0] - w->x) * w->nz - (p[2] - w->z) * w->nx;
    if (u < -(double)r || u > (double)w->length + r) {
        return 0;
    }
    uu = u < 0.0 ? 0.0 : (u > w->length ? w->length : u);
    if (w->length > 0.0f) {
        lo = w->bottom[0] + ((double)w->bottom[1] - w->bottom[0]) * uu / w->length;
        hi = w->top[0] + ((double)w->top[1] - w->top[0]) * uu / w->length;
    } else {
        lo = w->bottom[0] < w->bottom[1] ? w->bottom[0] : w->bottom[1];
        hi = w->top[0] > w->top[1] ? w->top[0] : w->top[1];
    }
    return p[1] >= lo - r && p[1] <= hi + r;
}

/* the squared distance from (x, z) to the edge (x0, z0) - (x1, z1) */
static double edgeDist2(double x, double z, double x0, double z0, double x1, double z1)
{
    const double ex = x1 - x0, ez = z1 - z0;
    const double l2 = ex * ex + ez * ez;
    double t = l2 > 0.0 ? ((x - x0) * ex + (z - z0) * ez) / l2 : 0.0;
    double dx, dz;

    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    dx = x - (x0 + ex * t);
    dz = z - (z0 + ez * t);
    return dx * dx + dz * dz;
}

int ico_camera_wall_cross_floor(const float a[3], const float b[3], const IcoCameraWallFloor *f,
                                float r)
{
    /* clip_floor_1's sides: at or above the plane (>= 0) and below it */
    const double sa =
        (double)f->n[0] * a[0] + (double)f->n[1] * a[1] + (double)f->n[2] * a[2] + f->d;
    const double sb =
        (double)f->n[0] * b[0] + (double)f->n[1] * b[1] + (double)f->n[2] * b[2] + f->d;
    const int n = f->count < 3 ? 0 : (f->count > 4 ? 4 : f->count);
    double x, z, t;
    int inside = 0;
    int i, j;

    if ((sa >= 0.0) == (sb >= 0.0) || n == 0) {
        return 0;
    }
    t = sa / (sa - sb);
    x = a[0] + ((double)b[0] - a[0]) * t;
    z = a[2] + ((double)b[2] - a[2]) * t;
    /* even-odd in x and z (FloorPointInside's rule), then the rim */
    for (i = 0, j = n - 1; i < n; j = i++) {
        const double xi = f->x[i], zi = f->z[i], xj = f->x[j], zj = f->z[j];
        if ((zi > z) != (zj > z) && x < (xj - xi) * (z - zi) / (zj - zi) + xi) {
            inside = !inside;
        }
    }
    if (inside) {
        return 1;
    }
    for (i = 0, j = n - 1; i < n; j = i++) {
        if (edgeDist2(x, z, f->x[j], f->z[j], f->x[i], f->z[i]) <= (double)r * r) {
            return 1;
        }
    }
    return 0;
}

int ico_camera_wall_step(IcoCameraWall *s, const float eye[3], int cutPending, int photo,
                         IcoCameraWallQuery query, void *user, float *moved)
{
    const double dx = (double)eye[0] - s->eye[0];
    const double dy = (double)eye[1] - s->eye[1];
    const double dz = (double)eye[2] - s->eye[2];
    const double d = s->valid ? sqrt(dx * dx + dy * dy + dz * dz) : 0.0;
    int kind = ICO_CAMERA_WALL_NONE;

    if (moved) {
        *moved = (float)d;
    }
    if (!isfinite(eye[0]) || !isfinite(eye[1]) || !isfinite(eye[2])) {
        s->valid = 0;
        return ICO_CAMERA_WALL_NONE;
    }
    if (s->valid && !cutPending && !photo && d > 0.0 && d <= RD_INTERP_CAMERA_MOVE && query) {
        kind = query(s->eye, eye, ICO_CAMERA_WALL_RADIUS, user);
    }
    s->eye[0] = eye[0];
    s->eye[1] = eye[1];
    s->eye[2] = eye[2];
    s->valid = 1;
    return kind;
}
