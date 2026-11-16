/* rd_glow_offset.c: how far the held glow's camera is from the present's
 * (rd_glow_offset.h).  Pure. */
#include "rd_glow_offset.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GS_CENTRE 2048.0

/* row r of a column-major m over the x, y, z columns */
static void row3(const float m[16], int r, double out[3])
{
    out[0] = m[r];
    out[1] = m[4 + r];
    out[2] = m[8 + r];
}

bool rd_glow_offset(const float a[16], const float b[16], double *px)
{
    double r0[3], r1[3], r3[3], u[3], v[3], d[3];

    *px = 0.0;
    row3(a, 0, r0);
    row3(a, 1, r1);
    row3(a, 3, r3);
    /* the direction d with (r0 - 2048 r3) . d = 0 and (r1 - 2048 r3) . d = 0:
       their cross product, turned to the eye's front (r3 . d above 0) */
    for (int k = 0; k < 3; k++) {
        u[k] = r0[k] - GS_CENTRE * r3[k];
        v[k] = r1[k] - GS_CENTRE * r3[k];
    }
    d[0] = u[1] * v[2] - u[2] * v[1];
    d[1] = u[2] * v[0] - u[0] * v[2];
    d[2] = u[0] * v[1] - u[1] * v[0];
    double w = r3[0] * d[0] + r3[1] * d[1] + r3[2] * d[2];
    if (!(fabs(w) > 0.0) || !isfinite(w)) {
        return false;
    }
    if (w < 0.0) {
        d[0] = -d[0];
        d[1] = -d[1];
        d[2] = -d[2];
    }
    double s0[3], s1[3], s3[3];
    row3(b, 0, s0);
    row3(b, 1, s1);
    row3(b, 3, s3);
    w = s3[0] * d[0] + s3[1] * d[1] + s3[2] * d[2];
    if (!(w > 0.0) || !isfinite(w)) {
        return false;
    }
    const double x = (s0[0] * d[0] + s0[1] * d[1] + s0[2] * d[2]) / w - GS_CENTRE;
    const double y = (s1[0] * d[0] + s1[1] * d[1] + s1[2] * d[2]) / w - GS_CENTRE;
    *px = sqrt(x * x + y * y);
    return isfinite(*px);
}

void rd_glow_offsets_reset(RdGlowOffsets *o)
{
    memset(o, 0, sizeof(*o));
}

void rd_glow_offsets_add(RdGlowOffsets *o, double px)
{
    if (o->n < RD_GLOW_OFFSETS) {
        o->v[o->n] = (float)px;
    }
    o->n++;
    o->max = px > o->max ? px : o->max;
}

static int cmpf(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

int rd_glow_offsets_text(RdGlowOffsets *o, char *out, size_t cap)
{
    if (cap > 0) {
        out[0] = '\0';
    }
    if (o->n == 0 && o->refused == 0) {
        return 0;
    }
    if (o->n == 0) {
        return snprintf(out, cap,
                        "; the held glow's camera against the present's: %u held presents not "
                        "taken",
                        o->refused);
    }
    const uint32_t k = o->n < RD_GLOW_OFFSETS ? o->n : RD_GLOW_OFFSETS;
    qsort(o->v, k, sizeof(o->v[0]), cmpf);
    const double med =
        (k & 1u) ? (double)o->v[k / 2u] : 0.5 * ((double)o->v[k / 2u - 1u] + (double)o->v[k / 2u]);
    char tail[48] = "";
    if (o->refused) {
        snprintf(tail, sizeof(tail), ", %u not taken", o->refused);
    }
    return snprintf(out, cap,
                    "; the held glow's camera against the present's (the frame centre's "
                    "direction): median %.1f, largest %.1f GS pixels over %u held presents%s",
                    med, o->max, o->n, tail);
}
