/* rd_replay_aa1.inc: PRIM.AA1's edge geometry and its draws under DATE,
 * included by rd_replay.c after drawExpanded, where doScreen's helpers
 * stand (it keeps rd_replay.c under the 256 KiB file cap).  doScreen draws
 * what aa1Expand returns through the sprite_aa1_* shaders
 * (port/shaders/sprite.hlsl), under DATE primitive by primitive
 * (aa1DatePlan, aa1DateDraw). */

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
 *                (an edge two triangles share gets both fringes)
 * The coverage is the vertex's IcoSpriteAa1Vertex.cov, interpolated without
 * perspective: ICO_AA1_INTERIOR on a triangle's own vertices.
 *
 * A triangle's fringe ends where the GS's do (GSRasterizer.cpp
 * DrawTriangle and DrawEdgeTriangle): an edge pixel is kept only where the
 * triangle's other two edge functions are above 0 (the GS's integer edge
 * functions in 12.4, the vertices sorted by y its way, made positive
 * inside and biased by 1 on its top-left edges) and inside the edge's box,
 * x floor(min x) .. ceil(max x) and y ceil(min y - 1) .. floor(max y + 1)
 * in GS pixels.  A band ending at the vertex instead draws pixels past the
 * corner, outside that wedge (issue 106's spurs), and misses some inside
 * it.  The band is drawn longer than its edge (aa1Reach: a whole number of
 * edge lengths past each end, at least two GS pixels, so its ends stay
 * exactly on the edge's line) and sprite_aa1_ps keeps or discards each of
 * its fragments with those integer tests on the triangle's flat fields
 * (IcoSpriteAa1Vertex v0x .. flags, aa1Wedge).  Cutting the geometry
 * instead would round the cuts to the 1/16 grid and move pixels.  A
 * triangle more than ICO_AA1_CLIP_EXTENT across in x or y keeps the band
 * unclipped and unextended (flags 0), as lines and interiors do; the log
 * counts those (aa1Wedge).  Every fragment the clip keeps lies within one
 * GS pixel of the edge's ends, inside rd__screen_area's pad (the DATE
 * snapshot's area); the band's fragments past that are discarded before
 * anything is written (a stale snapshot texel one of them reads decides
 * nothing). */
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

/* A triangle's clip (the flat fields of its fringes' vertices): v0 the
 * first vertex in the GS's order by y, d1 and d2 the second and third less
 * it (aa1Pack), flags ICO_AA1_CW .. ICO_AA1_CLIP; edge[i] the sorted edge
 * (ICO_AA1_EDGE: 0 v0-v1, 1 v0-v2, 2 v1-v2) of the triangle's edge from its
 * vertex i to vertex i + 1 (mod 3).  flags 0: no clip. */
typedef struct Aa1Wedge {
    uint16_t v0x, v0y;
    uint32_t d1, d2, flags;
    uint8_t edge[3];
} Aa1Wedge;

/* PCSX2's s_ysort: the vertices' order by y for the index
 * (y0 > y1) | (y0 > y2) << 1 | (y1 > y2) << 2 (vertices of equal y keep
 * their order; 2 and 5 cannot happen) */
static const uint8_t kAa1YSort[8][3] = {{0, 1, 2}, {1, 0, 2}, {0, 1, 2}, {1, 2, 0},
                                        {0, 2, 1}, {0, 1, 2}, {2, 0, 1}, {2, 1, 0}};

/* the log: the first triangle command drawn with the clip, and the
 * triangles too large for it (the first few, then at each power of two) */
static uint32_t s_aa1Clipped, s_aa1Unclipped;

/* x in the low 16 bits and y in the high, each as an int16 (|x|, |y| at
 * most ICO_AA1_CLIP_EXTENT) */
static uint32_t aa1Pack(int32_t x, int32_t y)
{
    return (uint32_t)(uint16_t)(int16_t)x | (uint32_t)(uint16_t)(int16_t)y << 16;
}

/* the clip of triangle t (PCSX2 GSRasterizer.cpp DrawTriangle: the sort,
 * the cross product's sign as clockwise, tl0 = y0 == y1 || !cw, tl1 = cw,
 * tl2 = y1 != y2 && !cw) */
static void aa1Wedge(const IcoSpriteVertex *t, Aa1Wedge *w)
{
    const int m = (t[0].y > t[1].y) | (t[0].y > t[2].y) << 1 | (t[1].y > t[2].y) << 2;
    const uint8_t *s = kAa1YSort[m];
    const IcoSpriteVertex *v0 = &t[s[0]], *v1 = &t[s[1]], *v2 = &t[s[2]];
    const int32_t bx = (int32_t)v1->x - v0->x, by = (int32_t)v1->y - v0->y;
    const int32_t cx = (int32_t)v2->x - v0->x, cy = (int32_t)v2->y - v0->y;
    const int64_t cross = (int64_t)bx * cy - (int64_t)by * cx;
    uint8_t at[3];
    memset(w, 0, sizeof(*w));
    for (int j = 0; j < 3; j++) {
        at[s[j]] = (uint8_t)j;
    }
    for (int i = 0; i < 3; i++) {
        /* sorted ends {0,1} -> 0, {0,2} -> 1, {1,2} -> 2 */
        w->edge[i] = (uint8_t)(at[i] + at[(i + 1) % 3] - 1);
    }
    if (cross == 0) {
        return; /* no fringe (aa1Fringe) */
    }
    int32_t x0 = t[0].x, x1 = t[0].x, y0 = t[0].y, y1 = t[0].y;
    for (int j = 1; j < 3; j++) {
        x0 = t[j].x < x0 ? t[j].x : x0;
        x1 = t[j].x > x1 ? t[j].x : x1;
        y0 = t[j].y < y0 ? t[j].y : y0;
        y1 = t[j].y > y1 ? t[j].y : y1;
    }
    if (x1 - x0 > ICO_AA1_CLIP_EXTENT || y1 - y0 > ICO_AA1_CLIP_EXTENT) {
        s_aa1Unclipped++;
        if (s_aa1Unclipped <= 3 || (s_aa1Unclipped & (s_aa1Unclipped - 1)) == 0) {
            rd__log("aa1: a triangle %.1f x %.1f GS pixels is past the edge clip's reach "
                    "(%d): its edges drawn unclipped (%u so far)",
                    (double)(x1 - x0) / 16.0, (double)(y1 - y0) / 16.0, ICO_AA1_CLIP_EXTENT / 16,
                    s_aa1Unclipped);
        }
        return;
    }
    const int cw = cross > 0;
    const int tl0 = v0->y == v1->y || !cw, tl1 = cw, tl2 = v1->y != v2->y && !cw;
    w->v0x = v0->x;
    w->v0y = v0->y;
    w->d1 = aa1Pack(bx, by);
    w->d2 = aa1Pack(cx, cy);
    w->flags = ICO_AA1_CLIP | (cw ? ICO_AA1_CW : 0u) | (tl0 ? ICO_AA1_TL0 : 0u) |
               (tl1 ? ICO_AA1_TL1 : 0u) | (tl2 ? ICO_AA1_TL2 : 0u);
}

/* v moved by (mx, my) (clamped to the 12.4 range) with coverage cov and
 * the clip w's fields for its edge e (none: w NULL) */
static IcoSpriteAa1Vertex aa1Vtx(const IcoSpriteVertex *v, int32_t mx, int32_t my, float cov,
                                 const Aa1Wedge *w, int e)
{
    IcoSpriteAa1Vertex o;
    memset(&o, 0, sizeof(o));
    const int32_t x = (int32_t)v->x + mx, y = (int32_t)v->y + my;
    o.v = *v;
    o.v.x = (uint16_t)(x < 0 ? 0 : (x > 0xFFFF ? 0xFFFF : x));
    o.v.y = (uint16_t)(y < 0 ? 0 : (y > 0xFFFF ? 0xFFFF : y));
    o.cov = cov;
    if (w && w->flags) {
        o.v0x = w->v0x;
        o.v0y = w->v0y;
        o.d1 = w->d1;
        o.d2 = w->d2;
        o.flags = w->flags | w->edge[e];
    }
    return o;
}

/* the point r edge lengths past p, away from q, exactly on the edge's line,
 * its attributes carried along the edge as the GS's edge walk carries them
 * (PCSX2 DrawEdgeTriangle; the colour and Z clamped as its ClampVertex
 * does); p itself where that leaves the 12.4 range */
static IcoSpriteVertex aa1Reach(const IcoSpriteVertex *p, const IcoSpriteVertex *q, int32_t r)
{
    const int32_t x = (int32_t)p->x + r * ((int32_t)p->x - q->x);
    const int32_t y = (int32_t)p->y + r * ((int32_t)p->y - q->y);
    IcoSpriteVertex o = *p;
    if (x < 0 || x > 0xFFFF || y < 0 || y > 0xFFFF) {
        return o;
    }
    o.x = (uint16_t)x;
    o.y = (uint16_t)y;
    const double z = (double)p->z + (double)r * ((double)p->z - (double)q->z);
    o.z = z <= 0.0 ? 0u : (z >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)z);
    for (int c = 0; c < 4; c++) {
        const int32_t k = (int32_t)p->rgba[c] + r * ((int32_t)p->rgba[c] - q->rgba[c]);
        o.rgba[c] = (uint8_t)(k < 0 ? 0 : (k > 255 ? 255 : k));
    }
    o.u = p->u + (float)r * (p->u - q->u);
    o.v = p->v + (float)r * (p->v - q->v);
    return o;
}

/* the edge p-q widened by (mx, my) (one GS pixel along its minor axis, in
 * 12.4): coverage 1 on the edge, 0 on the far side; attributes the edge's.
 * With a clip (w's flags set) the band runs on past both ends (aa1Reach) and
 * carries the clip for its edge e. */
static uint32_t aa1Band(const IcoSpriteVertex *p, const IcoSpriteVertex *q, int32_t mx, int32_t my,
                        const Aa1Wedge *w, int e, IcoSpriteAa1Vertex *o)
{
    IcoSpriteVertex pe = *p, qe = *q;
    if (w && w->flags) {
        /* past each end by at least 32 (two GS pixels along the major
         * axis): the box reaches one GS pixel past an end */
        const int32_t dx = (int32_t)q->x - p->x, dy = (int32_t)q->y - p->y;
        const int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        const int32_t len = ax > ay ? ax : ay;
        const int32_t r = len >= 32 ? 1 : (32 + len - 1) / len; /* len > 0: a fringe's edge */
        pe = aa1Reach(p, q, r);
        qe = aa1Reach(q, p, r);
    }
    const IcoSpriteAa1Vertex p0 = aa1Vtx(&pe, 0, 0, 1.0f, w, e), q0 = aa1Vtx(&qe, 0, 0, 1.0f, w, e);
    const IcoSpriteAa1Vertex p1 = aa1Vtx(&pe, mx, my, 0.0f, w, e),
                             q1 = aa1Vtx(&qe, mx, my, 0.0f, w, e);
    o[0] = p0;
    o[1] = q0;
    o[2] = q1;
    o[3] = p0;
    o[4] = q1;
    o[5] = p1;
    return 6;
}

/* the fringe of edge p-q (the triangle's edge e, from its vertex e to e + 1)
 * of a triangle whose third vertex is t: outside the edge along its minor
 * axis; none for a degenerate triangle */
static uint32_t aa1Fringe(const IcoSpriteVertex *p, const IcoSpriteVertex *q,
                          const IcoSpriteVertex *t, const Aa1Wedge *w, int e, IcoSpriteAa1Vertex *o)
{
    const int64_t dx = (int64_t)q->x - p->x, dy = (int64_t)q->y - p->y;
    const int64_t cross = dx * ((int64_t)t->y - p->y) - dy * ((int64_t)t->x - p->x);
    if (cross == 0) {
        return 0;
    }
    const int sc = cross > 0 ? 1 : -1;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        /* x-major: up or down */
        return aa1Band(p, q, 0, -16 * sc * (dx > 0 ? 1 : -1), w, e, o);
    }
    return aa1Band(p, q, 16 * sc * (dy > 0 ? 1 : -1), 0, w, e, o); /* y-major: left or right */
}

/* the three fringes of triangle t, clipped (aa1Wedge) */
static uint32_t aa1Fringes(const IcoSpriteVertex *t, IcoSpriteAa1Vertex *o)
{
    Aa1Wedge w;
    aa1Wedge(t, &w);
    s_aa1Clipped += w.flags != 0;
    uint32_t k = aa1Fringe(&t[0], &t[1], &t[2], &w, 0, o);
    k += aa1Fringe(&t[1], &t[2], &t[0], &w, 1, &o[k]);
    k += aa1Fringe(&t[2], &t[0], &t[1], &w, 2, &o[k]);
    return k;
}

/* expand()'s output (line pairs or triangles) as AA1 geometry; returns the
 * vertex count (at most 7 per input vertex).  split: the triangles' own
 * vertices first (*nInterior of them) and every fringe after, for a draw
 * whose fringes need Z write off (two draws); else triangle by triangle,
 * the GS's order, *nInterior = the total (one draw).  Lines are all
 * fringe: *nInterior = 0.  ends (not with split; NULL: none): each
 * primitive's end, its vertices from the previous one's end (a triangle's
 * own three first, then its fringes), *nEnds of them (aa1DateDraw) */
static uint32_t aa1Expand(const IcoSpriteVertex *in, uint32_t n, uint8_t topo, int split,
                          IcoSpriteAa1Vertex *o, uint32_t *nInterior, uint32_t *ends,
                          uint32_t *nEnds)
{
    uint32_t k = 0, ne = 0;
    if (topo == RD_PRIM_LINES) {
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            const IcoSpriteVertex *a = &in[i], *b = &in[i + 1];
            const int32_t dx = (int32_t)b->x - a->x, dy = (int32_t)b->y - a->y;
            const int xMajor = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
            if (dx == 0 && dy == 0) {
                continue;
            }
            k += aa1Band(a, b, xMajor ? 0 : -16, xMajor ? -16 : 0, NULL, 0, &o[k]);
            k += aa1Band(a, b, xMajor ? 0 : 16, xMajor ? 16 : 0, NULL, 0, &o[k]);
            if (ends) {
                ends[ne++] = k;
            }
        }
        *nInterior = 0;
        if (nEnds) {
            *nEnds = ne;
        }
        return k;
    }
    const uint32_t clippedBefore = s_aa1Clipped;
    for (uint32_t i = 0; i + 2 < n; i += 3) {
        for (int j = 0; j < 3; j++) {
            o[k++] = aa1Vtx(&in[i + j], 0, 0, ICO_AA1_INTERIOR, NULL, 0);
        }
        if (!split) {
            k += aa1Fringes(&in[i], &o[k]);
            if (ends) {
                ends[ne++] = k;
            }
        }
    }
    *nInterior = k;
    if (nEnds) {
        *nEnds = ne;
    }
    if (split) {
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            k += aa1Fringes(&in[i], &o[k]);
        }
    }
    if (clippedBefore == 0 && s_aa1Clipped > 0) {
        rd__log("aa1: triangle edges clipped to the GS's wedge and box (the first command: %u "
                "triangles, %u clipped, %u vertices)",
                n / 3, s_aa1Clipped, k);
    }
    return k;
}

/* ------------------------------------------------------ PRIM.AA1 and DATE
 * The GS tests TEST.DATE against the destination alpha as each pixel is
 * written, so in an AA1 strip under DATM 1 an edge pixel one triangle
 * writes with alpha below 0x80 (its coverage) turns away the next
 * triangle's pixel there (docs/port/DIVERGENCES.md VD-17).  The DATE
 * snapshot is taken once a draw, so the port drew both.  A PRIM.AA1 draw
 * under DATE that writes alpha now draws its primitives one at a time, in
 * the command's order, each in two draws over its own vertices
 * (aa1DateDraw): the fragments whose written alpha keeps the destination's
 * MSB as DATE wants it, then those that change it, the second draw's
 * stencil changing bit 6 (RD_DATE_STENCIL_BIT) where they land.  Within
 * one primitive the GS's pixels are distinct (a triangle's own pixels and
 * each edge's lie on different sides of its edges), so the order inside
 * the pair decides nothing; across primitives the bit carries what the GS
 * reads.  A triangle's two draws are each split again by Z write (its own
 * pixels with the pass's, its fringes without: edge pixels write no Z),
 * so a triangle of a Z-writing draw takes up to four draws.
 *   the stencil path  (DrawSetup.dateStencil) bit 6 is the alpha's MSB
 *                     (rd__replay_date_take): DATM 1 tests TEST1 and the
 *                     MSB-clear fragments TEST1_CLR, DATM 0 tests TEST0 and
 *                     the MSB-set ones TEST0_SET; DF_DATE off
 *   the snapshot path the R8 snapshot is DATE's test as before (DF_DATE)
 *                     and bit 6, cleared over the draw's area first
 *                     (rd__replay_date_clear), marks the texels a fragment
 *                     of the draw changed the MSB of: every draw tests
 *                     TEST0, the changing fragments TEST0_SET.  Under DATM 1
 *                     a texel that failed the snapshot never passes, and
 *                     one cleared by the draw fails from then on; DATM 0
 *                     alike
 * Every image of a planned pass (the two-pass blend's, the exact blend's
 * attachment read) tests the bit and the last one writes it, so all of
 * them see the same bit.  The storage read (no colour attachment: the
 * shader writes before the stencil test), the MSAA epoch and a depth
 * without the target's size keep the one test per draw, logged. */
typedef struct Aa1DateImg {
    RdPipeKeyInt key;
    RhiPipeline pipe;
    RdUniform cb;
} Aa1DateImg;

/* per planned pass i, part (0: a triangle's own vertices with the pass's
 * Z write, 1: its fringes with Z write off; one part over every vertex
 * without Z write and for lines) and selection (0: the fragments that keep
 * the bit, 1: those that change it), the images drawn */
typedef struct Aa1DatePlan {
    Aa1DateImg img[2][2][2][8];
    uint8_t nImg[2][2][2];
    uint8_t parts[2];
    int np, stencil;
    const Aa1DateImg *first;
} Aa1DatePlan;

static Aa1DatePlan s_aa1Date;

static uint32_t *s_axEnds;

static uint32_t s_axEndsCap;

/* room for the primitive ends of n expanded vertices (a primitive has at
 * least two) */
static uint32_t *aa1Ends(uint32_t n)
{
    const uint32_t need = n / 2 + 1;
    if (need > s_axEndsCap) {
        uint32_t *p = realloc(s_axEnds, (size_t)need * 2 * sizeof(*p));
        if (!p) {
            return NULL;
        }
        s_axEnds = p;
        s_axEndsCap = need * 2;
    }
    return s_axEnds;
}

/* the log: the first command drawn so, and the draws that keep the one
 * test per draw (the first few, then at each power of two) */
static uint32_t s_aa1DateDrawn, s_aa1DateKept;

static bool aa1DateKept(const char *why)
{
    if (rd__pipeline_gathering()) {
        return false;
    }
    s_aa1DateKept++;
    if (s_aa1DateKept <= 3 || (s_aa1DateKept & (s_aa1DateKept - 1)) == 0) {
        rd__log("aa1: a DATE draw keeps DATE's one test per draw (%s): where its primitives "
                "share an edge both are drawn (%u so far)",
                why, s_aa1DateKept);
    }
    return false;
}

/* The draws of a PRIM.AA1 command under DATE (dp, np: its planned passes;
 * lines: a line command), after bindDrawEx: each image's key, pipeline and
 * DrawCB; then the open pass made the images' kind and, on the snapshot
 * path, bit 6 cleared over area.  False (nothing drawn): the command is
 * drawn as before (aa1DateKept says why) */
static bool aa1DatePlan(Replay *r, const RdDrawPass *dp, int np, const DrawSetup *ds,
                        const RhiRect *area, int lines)
{
    Aa1DatePlan *pl = &s_aa1Date;
    const RdTargetRec *td = ds->tdId ? rd__target_rec(ds->tdId) : NULL;
    const int datm = r->st.ds.test.date == RD_DATE_DEST_ALPHA_1;
    /* the fragments that change the bit: DATM 1 those writing an alpha
     * below 0x80, DATM 0 those writing 0x80 or more */
    const uint32_t flipSel = datm ? ICO_DF_AA1_MSB_CLEAR : ICO_DF_AA1_MSB_SET;
    const uint32_t keepSel = datm ? ICO_DF_AA1_MSB_SET : ICO_DF_AA1_MSB_CLEAR;
    memset(pl->nImg, 0, sizeof(pl->nImg));
    pl->np = np;
    pl->stencil = ds->dateStencil;
    pl->first = NULL;
    if (!td || td->tw != ds->tc->tw || td->th != ds->tc->th) {
        return aa1DateKept("no depth and stencil of the target's size");
    }
    if (g_rd.passSamples > 1) {
        return aa1DateKept("the MSAA epoch");
    }
    if (!ds->dateStencil && (ds->dateTex.id == g_rd.dummy.id || !g_rd.fs[RD_FS_DATE_CLEAR].id)) {
        return aa1DateKept("no DATE snapshot or no DATE clear shader");
    }
    for (int i = 0; i < np && i < 2; i++) {
        const int alpha = (dp[i].key.gs.colorMask & 8) != 0;
        const int fba = (dp[i].flags & ICO_DF_FBA) != 0;
        pl->parts[i] = (uint8_t)(!lines && dp[i].key.gs.zwrite == RD_ZWRITE_ON ? 2 : 1);
        for (int part = 0; part < pl->parts[i]; part++) {
            for (int sel = 0; sel < 2; sel++) {
                const uint32_t want = sel ? flipSel : keepSel;
                if ((sel && !alpha) || (alpha && fba && want == ICO_DF_AA1_MSB_CLEAR)) {
                    continue; /* nothing written changes the bit; FBA sets every MSB */
                }
                RdDrawPass p = dp[i];
                if (part == 1 || lines) {
                    p.key.gs.zwrite = RD_ZWRITE_OFF;
                }
                p.flags |= alpha ? want : 0u;
                RdDrawPass ex[8];
                const int ne = expandPassesMode(&p, 1, dstModeFor(r), false, ex);
                for (int j = 0; j < ne; j++) {
                    /* the stencil path's test (expandPasses' twin) */
                    if (ds->dateStencil && !rd__date_stencil_pass(&ex[j])) {
                        return aa1DateKept("a pass without DATE's stencil test");
                    }
                    RdPipeKeyInt k = ex[j].key;
                    if (k.dst == RHI_DST_READ_STORAGE) {
                        return aa1DateKept("the exact blend's storage read");
                    }
                    if (k.depthFmt == RHI_FMT_UNKNOWN ||
                        (!ds->dateStencil && k.gs.stencil != RD_STENCIL_OFF)) {
                        return aa1DateKept("a pass without the stencil");
                    }
                    const uint8_t keep = ds->dateStencil ? k.gs.stencil : RD_STENCIL_DATE_TEST0;
                    const uint8_t flip = keep == RD_STENCIL_DATE_TEST1 ? RD_STENCIL_DATE_TEST1_CLR
                                                                       : RD_STENCIL_DATE_TEST0_SET;
                    k.gs.stencil = sel && j == ne - 1 ? flip : keep;
                    Aa1DateImg *im = &pl->img[i][part][sel][pl->nImg[i][part][sel]++];
                    IcoDrawCB cb;
                    fillDrawCB(r, &ex[j], ds, &cb);
                    im->key = k;
                    im->pipe = rd__get_pipeline(&k);
                    im->cb = rd__draw_group(&cb);
                    if (!im->pipe.id) {
                        return aa1DateKept("a pipeline missing");
                    }
                    pl->first = pl->first ? pl->first : im;
                }
            }
        }
    }
    if (!pl->first) {
        return aa1DateKept("nothing to draw");
    }
    if (!ds->dateStencil) {
        const RdPipeKeyInt kc = rd__date_setup_key(RD_STENCIL_DATE_CLEAR, ds->tc->format);
        if (!rd__get_pipeline(&kc).id) {
            return aa1DateKept("the DATE clear's pipeline missing");
        }
    }
    /* a storage pass begun again with the colour (every image draws in one) */
    dstPassMode(r, &pl->first->key);
    frameRefresh(r, ds);
    if (!ds->dateStencil && !rd__replay_date_clear(r, ds, area)) {
        return aa1DateKept("the DATE clear could not be drawn");
    }
    return true;
}

/* aa1DatePlan's draws, primitive by primitive (ends, nPrim: aa1Expand's)
 * from the bound vertex buffer, the scissor set; returns the draws made */
static uint32_t aa1DateDraw(Replay *r, RhiBindGroup g2, const uint32_t *ends, uint32_t nPrim)
{
    const Aa1DatePlan *pl = &s_aa1Date;
    uint32_t draws = 0;
    for (uint32_t t = 0, s = 0; t < nPrim; s = ends[t++]) {
        const uint32_t e = ends[t];
        for (int i = 0; i < pl->np && i < 2; i++) {
            for (int part = 0; part < pl->parts[i]; part++) {
                /* a triangle's own three vertices, then its fringes */
                const uint32_t first = part ? s + 3 : s;
                const uint32_t count = pl->parts[i] == 1 ? e - s : (part ? e - s - 3 : 3);
                for (int sel = 0; sel < 2 && count; sel++) {
                    for (int j = 0; j < pl->nImg[i][part][sel]; j++) {
                        const Aa1DateImg *im = &pl->img[i][part][sel][j];
                        rd__set_pipeline(s_cl, im->pipe, &im->key);
                        rd__bind_uniform(s_cl, 0, r->frameBG);
                        rd__bind_uniform(s_cl, 1, im->cb);
                        rhi_cmd_set_bind_group(s_cl, 2, g2);
                        dstBind(r, &im->key);
                        drawCmd(s_cl, count, first, 1);
                        draws++;
                    }
                }
            }
        }
    }
    if (s_aa1DateDrawn++ == 0) {
        rd__log("aa1: a DATE draw's primitives drawn one at a time, two draws each through "
                "DATE's stencil bit, so each sees the alpha the ones before it wrote (the first "
                "command: %u primitives in %u draws, the %s path)",
                nPrim, draws, pl->stencil ? "stencil" : "snapshot");
    }
    return draws;
}
