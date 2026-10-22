/* rd_replay_aa1.inc: PRIM.AA1's edge geometry, included by rd_replay.c
 * where doScreen's helpers stand (it keeps rd_replay.c under the 256 KiB
 * file cap).  doScreen draws what aa1Expand returns through the
 * sprite_aa1_* shaders (port/shaders/sprite.hlsl). */

/* -------------------------------------------------------------- PRIM.AA1
 * The model is PCSX2's software renderer's
 * (GSRasterizer.cpp DrawEdgeLine / DrawEdgeTriangle, GSDrawScanline.cpp):
 * a line or triangle with PRIM.AA1 has edge pixels, which carry a coverage
 * and write no Z; sprite_aa1_ps turns the coverage into the alpha.
 *   a line       is all edge: per step along its major axis the two pixels
 *                nearest it on the minor axis, coverage 1 - d (d the
 *                distance along the minor axis from the pixel's sample
 *                point to the line, in GS pixels): drawn as the line
 *                widened by one GS pixel to each side of the minor axis,
 *                coverage 1 on the line and 0 at the far sides
 *   a triangle   is drawn as without AA1 (its interior pixels), and each of
 *                its three edges gets the pixels outside it whose sample
 *                point lies within one GS pixel of it along its minor axis,
 *                coverage 1 - d again: a one-pixel fringe; every triangle
 *                on its own, as the GS draws a strip's triangles one by one
 * The coverage is the vertex's IcoSpriteAa1Vertex.cov, interpolated without
 * perspective: ICO_AA1_INTERIOR on a triangle's own vertices. */
static int aa1Prim(uint8_t prim)
{
    return prim == RD_PRIM_LINES || prim == RD_PRIM_LINE_STRIP || prim == RD_PRIM_TRIANGLES ||
           prim == RD_PRIM_TRIANGLE_STRIP || prim == RD_PRIM_TRIANGLE_FAN;
}

static IcoSpriteAa1Vertex *s_ax;

static uint32_t s_axCap;

static IcoSpriteAa1Vertex *axScratch(uint64_t n)
{
    if (n > s_axCap) {
        const uint32_t cap = (uint32_t)(n < 4096 ? 4096 : n * 2);
        IcoSpriteAa1Vertex *p = realloc(s_ax, (size_t)cap * sizeof(*p));
        if (!p) {
            return NULL;
        }
        s_ax = p;
        s_axCap = cap;
    }
    return s_ax;
}

static IcoSpriteAa1Vertex aa1Vtx(const IcoSpriteVertex *v, int32_t mx, int32_t my, float cov)
{
    IcoSpriteAa1Vertex o;
    const int32_t x = (int32_t)v->x + mx, y = (int32_t)v->y + my;
    o.v = *v;
    o.v.x = (uint16_t)(x < 0 ? 0 : (x > 0xFFFF ? 0xFFFF : x));
    o.v.y = (uint16_t)(y < 0 ? 0 : (y > 0xFFFF ? 0xFFFF : y));
    o.cov = cov;
    return o;
}

/* the edge p-q widened by (mx, my) (one GS pixel along its minor axis, in
 * 12.4): coverage 1 on the edge, 0 on the far side; attributes the edge's */
static uint32_t aa1Band(const IcoSpriteVertex *p, const IcoSpriteVertex *q, int32_t mx, int32_t my,
                        IcoSpriteAa1Vertex *o)
{
    const IcoSpriteAa1Vertex p0 = aa1Vtx(p, 0, 0, 1.0f), q0 = aa1Vtx(q, 0, 0, 1.0f);
    const IcoSpriteAa1Vertex p1 = aa1Vtx(p, mx, my, 0.0f), q1 = aa1Vtx(q, mx, my, 0.0f);
    o[0] = p0;
    o[1] = q0;
    o[2] = q1;
    o[3] = p0;
    o[4] = q1;
    o[5] = p1;
    return 6;
}

/* the fringe of edge p-q of a triangle whose third vertex is t: outside
 * the edge along its minor axis; none for a degenerate triangle */
static uint32_t aa1Fringe(const IcoSpriteVertex *p, const IcoSpriteVertex *q,
                          const IcoSpriteVertex *t, IcoSpriteAa1Vertex *o)
{
    const int64_t dx = (int64_t)q->x - p->x, dy = (int64_t)q->y - p->y;
    const int64_t cross = dx * ((int64_t)t->y - p->y) - dy * ((int64_t)t->x - p->x);
    if (cross == 0) {
        return 0;
    }
    const int sc = cross > 0 ? 1 : -1;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        return aa1Band(p, q, 0, -16 * sc * (dx > 0 ? 1 : -1), o); /* x-major: up or down */
    }
    return aa1Band(p, q, 16 * sc * (dy > 0 ? 1 : -1), 0, o); /* y-major: left or right */
}

/* expand()'s output (line pairs or triangles) as AA1 geometry; returns the
 * vertex count.  split: the triangles' own vertices first (*nInterior of
 * them) and every fringe after, for a draw whose fringes need Z write off
 * (two draws); else triangle by triangle, the GS's order, *nInterior = the
 * total (one draw).  Lines are all fringe: *nInterior = 0. */
static uint32_t aa1Expand(const IcoSpriteVertex *in, uint32_t n, uint8_t topo, int split,
                          IcoSpriteAa1Vertex *o, uint32_t *nInterior)
{
    uint32_t k = 0;
    if (topo == RD_PRIM_LINES) {
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            const IcoSpriteVertex *a = &in[i], *b = &in[i + 1];
            const int32_t dx = (int32_t)b->x - a->x, dy = (int32_t)b->y - a->y;
            const int xMajor = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
            if (dx == 0 && dy == 0) {
                continue;
            }
            k += aa1Band(a, b, xMajor ? 0 : -16, xMajor ? -16 : 0, &o[k]);
            k += aa1Band(a, b, xMajor ? 0 : 16, xMajor ? 16 : 0, &o[k]);
        }
        *nInterior = 0;
        return k;
    }
    for (uint32_t i = 0; i + 2 < n; i += 3) {
        for (int j = 0; j < 3; j++) {
            o[k++] = aa1Vtx(&in[i + j], 0, 0, ICO_AA1_INTERIOR);
        }
        if (!split) {
            k += aa1Fringe(&in[i], &in[i + 1], &in[i + 2], &o[k]);
            k += aa1Fringe(&in[i + 1], &in[i + 2], &in[i], &o[k]);
            k += aa1Fringe(&in[i + 2], &in[i], &in[i + 1], &o[k]);
        }
    }
    *nInterior = k;
    if (split) {
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            k += aa1Fringe(&in[i], &in[i + 1], &in[i + 2], &o[k]);
            k += aa1Fringe(&in[i + 1], &in[i + 2], &in[i], &o[k]);
            k += aa1Fringe(&in[i + 2], &in[i], &in[i + 1], &o[k]);
        }
    }
    return k;
}
