/* rd_vis.c: the port's visibility test per scene draw (rd_vis.h;
 * docs/port/RENDER.md, "Culling").
 *
 * Today most of list 0's draw commands put nothing on the screen: the game
 * culls whole objects by box against the native frame with a margin
 * (GsBase.c gsb_ClipBox), and every packet of an object it keeps is sent,
 * so the parts beside or behind the camera are drawn and clipped away
 * (77 % of the commands of issue 179's frame; 54 to 86 % over the
 * occlusion corpus, build-host/tmp/v098/evidence/perf/occlusion.md).  On a
 * tile-based GPU each costs its vertex and binning work.  This test drops
 * them before the replay looks at the frame.
 *
 * The bound.  A mesh's positions are qword 0 of each vertex of its stream
 * (rd_mesh.h RD_VU_QW_*); the box of every vertex of the stream (the
 * draws' batches and a sheet's tiles all lie in it) with the w lane's range
 * is kept per mesh by (id, stream serial, vertex count, layout, stream):
 * RdMeshRec.streamSerial is new at every write of the stream
 * (rd__mesh_stream_written: creation, rd_update_vu_mesh's morphs, the
 * interpolation's scratch copies and their blend), so a kept box is always
 * the stream the device draws; a record never written through it (serial
 * 0) is bounded again at every draw.  A skinned stream keeps a box per
 * bone slot (rd_vis.h RdVisSkin).  A grid's vertices are in the command's
 * payload (the frame's copy of the Mesh3D buffer), bounded at every draw.
 * Particles are not tested.
 *
 * The test.  The programs give each vertex the homogeneous position
 * h = M (x, y, z, w) in VU order (vu_common.hlsli vu_mat; M = VU memory
 * 16..19 for normal_c, normal_l and mesh, w = 1, or pos.w under code 36;
 * cluster: h = mem[4..7] (w0 B0 (p, 1) + w1 B1 (p, 1), 1)).  h is linear
 * in the position, so every vertex's h is a convex combination of the h of
 * the box's corners (8, or 16 over the w range), computed here in double;
 * the shader's float h differs from the exact one by a few roundings of
 * the sum of the absolute products, which every plane value below must
 * clear (RD_VIS_TOL of that sum).  For a side of the target's rectangle
 * (rd_vis.h RdVisRect: the left one, x0) two planes through the eye's
 * line are tested, h.x - (x0 - 2) h.w and h.x - (x0 - 1) h.w: the command
 * is skipped when both are negative at every corner (each linear, so at
 * every vertex too).  What the GPU draws of a triangle then lies left of
 * x0 - 1:
 *   - a corner drawn at its GS position (vu_vtx_position: every corner of
 *     a triangle drawn exactly, and a clipped triangle's corners in front
 *     of the eye with X and Y not saturated) has w > 0 (the region test
 *     needs 0 < w, cluster's mem[0].w < w with mem[0].w >= 0 checked;
 *     vu_gs_ok and the extended snap need w > 0; code 36 is skipped only
 *     with every corner in front), so x / w < x0 - 2, and its ftoi4 X is
 *     within 1/16 of that (truncation toward zero; a saturated X keeps its
 *     side): X < x0 - 1.9, and (X, 1) is beyond the second plane;
 *   - a corner in the homogeneous form (vu_homogeneous_position: behind
 *     the eye, saturated, or below GS Z 0) is h itself through the
 *     clip-space mapping, linear: beyond the second plane at any w;
 *   - the GPU draws the points of the clip-space triangle with w > 0 (it
 *     clips at -w <= x <= w and 0 <= z <= w), each a convex combination of
 *     the three corners: beyond the second plane with w > 0, so left of
 *     x0 - 1, a whole pixel and more from the target's first sample (GS x0
 *     on the pixel-centre convention, x0 - 0.5 / scale at worst under MSAA
 *     or without the half-pixel origin).
 * Code 34 (clip mode none) wraps X and Y to 16 bits and is never skipped.
 * Behind the eye: every corner's w < 0 gives every vertex w < 0, no GS
 * position (each needs w > 0) and every homogeneous corner clipped away;
 * not for code 36, whose triangles without a clip flag are drawn at their
 * GS positions whatever w is.  The chain plane, the later mark's depth
 * bias, the card test, the tick cameras' rule and the content table only
 * leave out or move depth: never a position.  So a skipped command draws
 * no pixel of its target, at every preset, scale and aspect: the verdict
 * depends only on the target's GS size and the payload.
 *
 * The shadow frame.  The only way of leaving commands out that is proved
 * byte for byte is the replay tool's --nop: the command's type becomes
 * RDC_NOP, every other field, every index and so every command's ordinal
 * (g_rdStoreOrd) stay, and every scan that walks the lists has a case for
 * it (the store plan, the passes' areas and cuts, the crack fill's place,
 * the ring's estimate, the meshes' upload, the vertex pre-pass, the blur
 * fold, the constant reads, the list loop).  rd__vis_frame returns a copy
 * of the frame whose lists with a skipped command are copies with those
 * commands NOPs (the payload and every other list shared).  What decides
 * across draws or across replays reads the frame as recorded
 * (rd__vis_source, rd__vis_culled): the strip joining over a part's
 * packets (rd_join.c: a skipped packet's strips still join the others'),
 * the fades fold's plan (rd_fades_fold.c: its reads of SCENE's depth
 * before the clear), and the store plan's carried read (rd_store_plan.c:
 * a depth read before its clear keeps the stores from then on). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_mesh.h"
#include "rd_vis.h"

/* the lists tested: the scene's (list 11 is the 2D layer and the fades,
 * list 12 the reduction) */
#define VIS_LAST_LIST 10

RdVisRect rd__vis_rect(uint32_t gsW, uint32_t gsH, uint32_t tw, uint32_t th)
{
    RdVisRect r;
    r.x0 = 2048.0 - (double)(gsW >> 1);
    r.y0 = 2048.0 - (double)(gsH >> 1);
    r.x1 = r.x0 + (double)(tw > gsW ? tw : gsW);
    r.y1 = r.y0 + (double)(th > gsH ? th : gsH) + 1.0;
    return r;
}

static void boxAdd(RdVisBox *b, const float *p)
{
    if (b->n == 0) {
        memcpy(b->lo, p, sizeof(b->lo));
        memcpy(b->hi, p, sizeof(b->hi));
    } else {
        for (int k = 0; k < 4; k++) {
            b->lo[k] = p[k] < b->lo[k] ? p[k] : b->lo[k];
            b->hi[k] = p[k] > b->hi[k] ? p[k] : b->hi[k];
        }
    }
    b->n++;
}

static bool finite4(const float *p)
{
    return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && isfinite(p[3]);
}

bool rd__vis_box(const float (*stream)[4], uint32_t vertexCount, uint32_t qpv, RdVisBox *out)
{
    memset(out, 0, sizeof(*out));
    if (!stream || qpv == 0) {
        return vertexCount == 0;
    }
    for (uint32_t v = 0; v < vertexCount; v++) {
        const float *p = stream[(size_t)v * qpv];
        if (!finite4(p)) {
            out->n = 0;
            return false;
        }
        boxAdd(out, p);
    }
    return true;
}

bool rd__vis_grid_box(const float (*stream)[4], uint32_t streamQw, uint32_t strips,
                      uint32_t stripLen, uint32_t qpv, RdVisBox *out)
{
    memset(out, 0, sizeof(*out));
    if (!stream || stripLen < 3u || qpv < 2u || strips == 0) {
        return false;
    }
    /* per strip: the VIF qword, the tag and the colour, the vertices, MSCNT
     * (vu_grid.hlsl: vu_batch = (3, 1)) */
    const uint64_t per = (uint64_t)stripLen * qpv + 4u;
    if ((uint64_t)strips * per > streamQw) {
        return false;
    }
    for (uint32_t s = 0; s < strips; s++) {
        for (uint32_t k = 0; k < stripLen; k++) {
            const float *p = stream[(size_t)(s * per + 3u + (uint64_t)k * qpv)];
            if (!finite4(p)) {
                out->n = 0;
                return false;
            }
            boxAdd(out, p);
        }
    }
    return true;
}

/* vu_skin.hlsl vu_bone_mat's slot of a weight's VU address */
static uint32_t skinSlot(uint32_t addr)
{
    const uint32_t i = addr - 16u;
    return i < 236u ? i : 236u;
}

bool rd__vis_skin(const float (*stream)[4], uint32_t vertexCount, uint32_t qpv, RdVisSkin *out)
{
    memset(out, 0, sizeof(*out));
    if (!stream || qpv < 3u || vertexCount == 0) {
        return false;
    }
    for (uint32_t v = 0; v < vertexCount; v++) {
        const float *p = stream[(size_t)v * qpv];
        const float *wq = stream[(size_t)v * qpv + 2u];
        uint32_t a0, a1;
        memcpy(&a0, &wq[0], 4);
        memcpy(&a1, &wq[2], 4);
        const float w0 = wq[1], w1 = wq[3];
        if (!finite4(p) || !(w0 >= 0.0f && w0 <= 1.0f) || !(w1 >= 0.0f && w1 <= 1.0f)) {
            return false; /* ok stays false: nothing skipped */
        }
        const double e = fabs((double)w0 + (double)w1 - 1.0);
        out->emax = e > out->emax ? e : out->emax;
        const uint32_t s[2] = {skinSlot(a0), skinSlot(a1)};
        for (int k = 0; k < 2; k++) {
            out->used[s[k] >> 5] |= 1u << (s[k] & 31u);
            boxAdd(&out->box[s[k]], p);
            out->qwEnd = s[k] + 4u > out->qwEnd ? s[k] + 4u : out->qwEnd;
        }
    }
    out->ok = true;
    return true;
}

/* ------------------------------------------------------------ the planes
 * Plane p of side p / 2 at margin RD_VIS_MARGIN (p even) or
 * RD_VIS_MARGIN_IN (odd), negative beyond it: left h.x - (x0 - e) h.w,
 * right (x1 + e) h.w - h.x, top h.y - (y0 - e) h.w, bottom
 * (y1 + e) h.w - h.y; plane 8 is h.w (negative behind the eye). */
#define VIS_PLANES 9

typedef struct VisPlane {
    int lane;     /* x (0) or y (1) */
    double sign;  /* of the lane */
    double coefW; /* of h.w */
} VisPlane;

static void planesOf(const RdVisRect *r, VisPlane pl[8])
{
    const double e[2] = {RD_VIS_MARGIN, RD_VIS_MARGIN_IN};
    for (int k = 0; k < 2; k++) {
        pl[0 + k] = (VisPlane){0, 1.0, -(r->x0 - e[k])};
        pl[2 + k] = (VisPlane){0, -1.0, r->x1 + e[k]};
        pl[4 + k] = (VisPlane){1, 1.0, -(r->y0 - e[k])};
        pl[6 + k] = (VisPlane){1, -1.0, r->y1 + e[k]};
    }
}

static double planeAt(const VisPlane *p, const double h[4])
{
    return p->sign * h[p->lane] + p->coefW * h[3];
}

/* the plane values' running maxima (and h.w's minimum) over the corners;
 * bad: a corner not finite (never skipped) */
typedef struct VisAcc {
    VisPlane pl[8];
    double max[VIS_PLANES];
    double minW;
    bool any, bad;
} VisAcc;

static void accInit(VisAcc *a, const RdVisRect *r)
{
    memset(a, 0, sizeof(*a));
    planesOf(r, a->pl);
}

static void accCorner(VisAcc *a, const double h[4])
{
    if (!isfinite(h[0]) || !isfinite(h[1]) || !isfinite(h[2]) || !isfinite(h[3])) {
        a->bad = true;
        return;
    }
    double v[VIS_PLANES];
    for (int p = 0; p < 8; p++) {
        v[p] = planeAt(&a->pl[p], h);
    }
    v[8] = h[3];
    for (int p = 0; p < VIS_PLANES; p++) {
        a->max[p] = !a->any || v[p] > a->max[p] ? v[p] : a->max[p];
    }
    a->minW = !a->any || h[3] < a->minW ? h[3] : a->minW;
    a->any = true;
}

/* plane p's tolerance: RD_VIS_TOL of the sum of the absolute products
 * (mag: each lane's bound of |M| |corner|) */
static double planeTol(const VisAcc *a, int p, const double mag[4])
{
    if (p == 8) {
        return RD_VIS_TOL * mag[3];
    }
    return RD_VIS_TOL * (mag[a->pl[p].lane] + fabs(a->pl[p].coefW) * mag[3]);
}

/* every vertex's plane value is at most (1 - emax) m + emax |c| when the
 * corners' maximum m is negative (the skinned blend, rd__vis_test_skin;
 * emax 0 for the others): negative by more than the tolerance */
static bool beyond(const VisAcc *a, int p, const double mag[4], double emax, const double *c)
{
    const double m = a->max[p];
    if (!(m < 0.0)) {
        return false;
    }
    const double bound = (1.0 - emax) * m + (c ? emax * fabs(c[p]) : 0.0);
    return bound < -planeTol(a, p, mag);
}

static int decide(const VisAcc *a, const double mag[4], bool scissor, double emax, const double *c)
{
    if (!a->any || a->bad) {
        return RD_VIS_KEEP;
    }
    /* behind the eye (not code 36) first: the sides' planes hold there too
     * (nothing with w > 0 is drawn) but say less */
    if (!scissor && beyond(a, 8, mag, emax, c)) {
        return RD_VIS_NEAR;
    }
    /* code 36: only with every corner in front of the eye */
    const bool front = !scissor || a->minW > planeTol(a, 8, mag);
    for (int s = 0; s < 4 && front; s++) {
        if (beyond(a, 2 * s, mag, emax, c) && beyond(a, 2 * s + 1, mag, emax, c)) {
            return RD_VIS_LEFT + s;
        }
    }
    return RD_VIS_KEEP;
}

static bool finiteCols(const float (*m)[4], int n)
{
    for (int i = 0; i < n; i++) {
        if (!finite4(m[i])) {
            return false;
        }
    }
    return true;
}

/* h = m[0] x + m[1] y + m[2] z + m[3] w, in the VU's order, in double */
static void hOf(const float (*m)[4], double x, double y, double z, double w, double h[4])
{
    for (int c = 0; c < 4; c++) {
        double s = (double)m[0][c] * x;
        s = s + (double)m[1][c] * y;
        s = s + (double)m[2][c] * z;
        s = s + (double)m[3][c] * w;
        h[c] = s;
    }
}

static double absMax(float a, float b)
{
    const double x = fabs((double)a), y = fabs((double)b);
    return x > y ? x : y;
}

int rd__vis_test(const float (*m)[4], const RdVisBox *b, bool scissor, const RdVisRect *r)
{
    if (!m || !b || !r || b->n == 0 || !finiteCols(m, 4)) {
        return RD_VIS_KEEP;
    }
    /* w: 1, or under code 36 the stream's pos.w (vu_prelit.hlsl), with 1
     * too (a program without a scissor variant given code 36's clip mode
     * reads w = 1: vu_lit.hlsl vu_lit_mode) */
    double ws[2] = {1.0, 1.0};
    if (scissor) {
        ws[0] = b->lo[3] < 1.0f ? (double)b->lo[3] : 1.0;
        ws[1] = b->hi[3] > 1.0f ? (double)b->hi[3] : 1.0;
    }
    const double ax = absMax(b->lo[0], b->hi[0]), ay = absMax(b->lo[1], b->hi[1]);
    const double az = absMax(b->lo[2], b->hi[2]);
    const double aw = fabs(ws[0]) > fabs(ws[1]) ? fabs(ws[0]) : fabs(ws[1]);
    double mag[4];
    for (int c = 0; c < 4; c++) {
        mag[c] = fabs((double)m[0][c]) * ax + fabs((double)m[1][c]) * ay +
                 fabs((double)m[2][c]) * az + fabs((double)m[3][c]) * aw;
    }
    VisAcc a;
    accInit(&a, r);
    const int nw = scissor ? 2 : 1;
    for (int k = 0; k < 8; k++) {
        const double x = (double)(k & 1 ? b->hi[0] : b->lo[0]);
        const double y = (double)(k & 2 ? b->hi[1] : b->lo[1]);
        const double z = (double)(k & 4 ? b->hi[2] : b->lo[2]);
        for (int j = 0; j < nw; j++) {
            double h[4];
            hOf(m, x, y, z, ws[j], h);
            accCorner(&a, h);
        }
    }
    return decide(&a, mag, scissor, 0.0, NULL);
}

int rd__vis_test_skin(const float (*mem)[4], const float (*bones)[4], uint32_t boneQw,
                      const RdVisSkin *s, const RdVisRect *r)
{
    if (!mem || !bones || !s || !r || !s->ok || s->qwEnd == 0) {
        return RD_VIS_KEEP;
    }
    /* the bones every slot reads come from the payload (VuBoneCB holds 240
     * qwords; the card bits go after the slots the weights read), the
     * region test's lower w is not negative (inside then needs w > 0), the
     * blend's weights sum near 1 */
    const uint32_t have = boneQw < 240u ? boneQw : 240u;
    if (s->qwEnd > have || !(mem[0][3] >= 0.0f) || !finiteCols(&mem[4], 4) || !(s->emax < 0.5)) {
        return RD_VIS_KEEP;
    }
    const float (*m4)[4] = &mem[4];
    /* the blend w0 X0 + w1 X1 (X = B (p, 1)): each lane at most twice the
     * largest |B| (|p|, 1) of the slots it reads */
    double pb[3] = {0.0, 0.0, 0.0};
    for (uint32_t i = 0; i < RD_VIS_SLOTS; i++) {
        if (!(s->used[i >> 5] & (1u << (i & 31u)))) {
            continue;
        }
        const float (*bm)[4] = &bones[i];
        if (!finiteCols(bm, 4)) {
            return RD_VIS_KEEP;
        }
        const RdVisBox *b = &s->box[i];
        const double a[3] = {absMax(b->lo[0], b->hi[0]), absMax(b->lo[1], b->hi[1]),
                             absMax(b->lo[2], b->hi[2])};
        for (int k = 0; k < 3; k++) {
            const double v = fabs((double)bm[0][k]) * a[0] + fabs((double)bm[1][k]) * a[1] +
                             fabs((double)bm[2][k]) * a[2] + fabs((double)bm[3][k]);
            pb[k] = 2.0 * v > pb[k] ? 2.0 * v : pb[k];
        }
    }
    double mag[4];
    for (int c = 0; c < 4; c++) {
        mag[c] = fabs((double)m4[0][c]) * pb[0] + fabs((double)m4[1][c]) * pb[1] +
                 fabs((double)m4[2][c]) * pb[2] + fabs((double)m4[3][c]);
    }
    VisAcc a;
    accInit(&a, r);
    for (uint32_t i = 0; i < RD_VIS_SLOTS; i++) {
        if (!(s->used[i >> 5] & (1u << (i & 31u)))) {
            continue;
        }
        const float (*bm)[4] = &bones[i];
        const RdVisBox *b = &s->box[i];
        for (int k = 0; k < 8; k++) {
            const double x = (double)(k & 1 ? b->hi[0] : b->lo[0]);
            const double y = (double)(k & 2 ? b->hi[1] : b->lo[1]);
            const double z = (double)(k & 4 ? b->hi[2] : b->lo[2]);
            double xb[4], h[4];
            hOf(bm, x, y, z, 1.0, xb);
            hOf(m4, xb[0], xb[1], xb[2], 1.0, h);
            accCorner(&a, h);
        }
    }
    /* the planes' values at the translation (mem[7]: the blend's origin,
     * where a vertex whose weights sum to s lies a share 1 - s of the way) */
    double t[4], c[VIS_PLANES];
    for (int k = 0; k < 4; k++) {
        t[k] = (double)m4[3][k];
    }
    for (int p = 0; p < 8; p++) {
        c[p] = planeAt(&a.pl[p], t);
    }
    c[8] = t[3];
    return decide(&a, mag, false, s->emax, c);
}

/* ------------------------------------------------------------ the frame */

/* a mesh's kept bound (by its slot) */
typedef struct VisEntry {
    uint32_t id, vertexCount, qpv;
    uint64_t serial;
    const void *stream;
    bool boxOk, skinTried;
    RdVisBox box;
    RdVisSkin *skin; /* skinned meshes: made on their first skinned draw */
    /* the box of each batch's vertices (batchN of them; bbOk: every batch
     * bounded), made on the first draw of a part of the mesh */
    RdVisBox *bb;
    uint32_t batchN;
    bool bbTried, bbOk;
} VisEntry;

static struct {
    int mode;    /* RD_VIS_*: rd__vis_init's, or a test's */
    int forced;  /* rd__vis_set_mode's, -1 none */
    bool inited; /* rd__vis_init ran (the mode read) */
    RdFrame shadow;
    const RdFrame *src;
    RdCmd *cmds[RD_LIST_COUNT];
    uint32_t cap[RD_LIST_COUNT];
    /* the walk's skipped commands: list << 24 is not enough for every
     * index, so two arrays */
    uint8_t *skipL;
    uint32_t *skipI;
    uint32_t nSkip, capSkip;
    VisEntry *cache; /* RD_MAX_MESHES, made on the first use */
    VisEntry scratch;
    RdVisSkin scratchSkin;
    RdVisCounts last;
    uint32_t why[RD_VIS_WHY_COUNT]; /* the last walk's, by reason */
    int whyCur;                     /* the command being decided */
    bool range;                     /* the batch-range box (ICO_RD_CULL_RANGE) */
} s_vis = {.forced = -1, .range = true};

static const char *const s_whyNames[RD_VIS_WHY_COUNT] = {"skipped by the stream's box",
                                                         "skipped by the batches' box",
                                                         "particles",
                                                         "2D lists",
                                                         "no target",
                                                         "clip none",
                                                         "no payload",
                                                         "clip unknown",
                                                         "no mesh",
                                                         "no bound",
                                                         "skinned refused",
                                                         "grid refused",
                                                         "skinned reaches",
                                                         "grid reaches",
                                                         "scissor mesh reaches",
                                                         "mesh of batches reaches",
                                                         "one-batch mesh reaches"};

const char *rd__vis_why_name(int why)
{
    return why >= 0 && why < RD_VIS_WHY_COUNT ? s_whyNames[why] : "?";
}

void rd__vis_why(uint32_t out[RD_VIS_WHY_COUNT])
{
    memcpy(out, s_vis.why, sizeof(s_vis.why));
}

static int modeOf(const char *v)
{
    if (!v || !v[0]) {
        return RD_VIS_FRUSTUM;
    }
    if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0 || strcmp(v, "false") == 0) {
        return RD_VIS_OFF;
    }
    if (strcmp(v, "full") == 0 || strcmp(v, "2") == 0) {
        return RD_VIS_FULL;
    }
    return RD_VIS_FRUSTUM;
}

const char *rd__vis_mode_name(int mode)
{
    return mode == RD_VIS_OFF ? "off" : mode == RD_VIS_FULL ? "full" : "frustum";
}

void rd__vis_init(void)
{
    const char *e = getenv("ICO_RD_CULL");
    s_vis.mode = s_vis.forced >= 0 ? s_vis.forced : modeOf(e);
    s_vis.inited = true;
    s_vis.src = NULL;
    memset(&s_vis.last, 0, sizeof(s_vis.last));
    memset(s_vis.why, 0, sizeof(s_vis.why));
    const char *rg = getenv("ICO_RD_CULL_RANGE"); /* 0: the stream's box alone, a comparison */
    s_vis.range = !(rg && (strcmp(rg, "0") == 0 || strcmp(rg, "off") == 0));
    rd__log("cull: %s ([dev] cull, ICO_RD_CULL%s%s)%s", rd__vis_mode_name(s_vis.mode),
            e && e[0] ? " = " : "", e && e[0] ? e : "",
            s_vis.mode == RD_VIS_OFF ? ": every scene draw replayed"
            : s_vis.mode == RD_VIS_FULL
                ? ": the occlusion test is not built yet, the frustum test alone: a scene draw "
                  "wholly outside its target or behind the eye is not replayed"
                : ": a scene draw wholly outside its target or behind the eye is not replayed");
}

int rd__vis_mode(void)
{
    if (!s_vis.inited) {
        s_vis.mode = s_vis.forced >= 0 ? s_vis.forced : modeOf(getenv("ICO_RD_CULL"));
        s_vis.inited = true;
    }
    return s_vis.mode;
}

void rd__vis_set_mode(int mode)
{
    s_vis.forced = mode >= RD_VIS_OFF && mode <= RD_VIS_FULL ? mode : -1;
    s_vis.inited = false;
}

const RdFrame *rd__vis_source(const RdFrame *f)
{
    return f && f == &s_vis.shadow && s_vis.src ? s_vis.src : f;
}

const RdCmd *rd__vis_culled(const RdFrame *f, int l, uint32_t i)
{
    if (!f || f != &s_vis.shadow || !s_vis.src || l < 0 || l >= RD_LIST_COUNT) {
        return NULL;
    }
    const RdCmdList *sl = &f->lists[l], *ol = &s_vis.src->lists[l];
    if (i >= sl->count || i >= ol->count || sl->cmds == ol->cmds) {
        return NULL;
    }
    const RdCmd *o = &ol->cmds[i];
    return sl->cmds[i].type == RDC_NOP && o->type != RDC_NOP ? o : NULL;
}

void rd__vis_counts(RdVisCounts *out)
{
    *out = s_vis.last;
}

/* mesh m's (id) kept entry, bounded again when its stream changed; NULL
 * without memory */
static VisEntry *entryOf(uint32_t id, const RdMeshRec *m)
{
    const uint32_t slot = (id & 0xFFFF) - 1u;
    VisEntry *e;
    if (m->streamSerial == 0 || slot >= RD_MAX_MESHES) {
        e = &s_vis.scratch; /* never written through the serial: every draw */
        e->id = 0;
    } else {
        if (!s_vis.cache) {
            s_vis.cache = calloc(RD_MAX_MESHES, sizeof(VisEntry));
            if (!s_vis.cache) {
                return NULL;
            }
        }
        e = &s_vis.cache[slot];
    }
    if (e->id != 0 && e->id == id && e->serial == m->streamSerial &&
        e->vertexCount == m->vertexCount && e->qpv == m->qwPerVertex &&
        e->stream == (const void *)m->stream) {
        return e;
    }
    e->id = e == &s_vis.scratch ? 0u : id;
    e->serial = m->streamSerial;
    e->vertexCount = m->vertexCount;
    e->qpv = m->qwPerVertex;
    e->stream = m->stream;
    e->boxOk = rd__vis_box((const float (*)[4])m->stream, m->vertexCount, m->qwPerVertex, &e->box);
    e->skinTried = false;
    free(e->bb);
    e->bb = NULL;
    e->batchN = 0;
    e->bbTried = e->bbOk = false;
    return e;
}

/* The box of the vertices of batches first .. last - 1 of mesh m (the ones a
 * draw reads: its indices lie in its batches' own vertices, rd_internal.h
 * RdVuBatchRec); false when a batch lies outside the stream, the mesh
 * carries a sheet's tiles (a draw out of the box reads them too), or the
 * range holds no vertex. Per-batch boxes are kept on the entry; the scratch
 * entry (a stream never written through the serial) is measured each time. */
static bool rangeBox(VisEntry *e, const RdMeshRec *m, uint32_t first, uint32_t last, RdVisBox *out)
{
    memset(out, 0, sizeof(*out));
    if (!m->batches || m->tiledFrom || first >= last || last > m->batchCount) {
        return false;
    }
    const uint32_t qpv = m->qwPerVertex;
    const bool keep = e != &s_vis.scratch;
    if (keep && !e->bbTried) {
        e->bbTried = true;
        e->bb = calloc(m->batchCount, sizeof(RdVisBox));
        e->batchN = e->bb ? m->batchCount : 0;
        e->bbOk = e->bb != NULL;
        for (uint32_t b = 0; e->bbOk && b < m->batchCount; b++) {
            const RdVuBatchRec *br = &m->batches[b];
            e->bbOk = (uint64_t)br->firstVertex + br->vertexCount <= m->vertexCount &&
                      rd__vis_box((const float (*)[4])m->stream + (size_t)br->firstVertex * qpv,
                                  br->vertexCount, qpv, &e->bb[b]);
        }
    }
    if (keep && (!e->bbOk || e->batchN != m->batchCount)) {
        return false;
    }
    for (uint32_t b = first; b < last; b++) {
        RdVisBox t;
        const RdVisBox *bx = keep ? &e->bb[b] : &t;
        if (!keep) {
            const RdVuBatchRec *br = &m->batches[b];
            if ((uint64_t)br->firstVertex + br->vertexCount > m->vertexCount ||
                !rd__vis_box((const float (*)[4])m->stream + (size_t)br->firstVertex * qpv,
                             br->vertexCount, qpv, &t)) {
                memset(out, 0, sizeof(*out));
                return false;
            }
        }
        if (bx->n == 0) {
            continue;
        }
        if (out->n == 0) {
            *out = *bx;
        } else {
            for (int k = 0; k < 4; k++) {
                out->lo[k] = bx->lo[k] < out->lo[k] ? bx->lo[k] : out->lo[k];
                out->hi[k] = bx->hi[k] > out->hi[k] ? bx->hi[k] : out->hi[k];
            }
            out->n += bx->n;
        }
    }
    return out->n != 0;
}

static const RdVisSkin *skinOf(VisEntry *e, const RdMeshRec *m)
{
    RdVisSkin *s = e == &s_vis.scratch ? &s_vis.scratchSkin : e->skin;
    if (!s) {
        s = e->skin = malloc(sizeof(RdVisSkin));
        if (!s) {
            return NULL;
        }
        e->skinTried = false;
    }
    if (!e->skinTried) {
        (void)rd__vis_skin((const float (*)[4])m->stream, m->vertexCount, m->qwPerVertex, s);
        e->skinTried = e != &s_vis.scratch;
    }
    return s;
}

/* the walk's counts and the command's verdict */
typedef struct VisWalk {
    const RdFrame *f;
    RdVisCounts n;
    bool noMemory;
} VisWalk;

static int cmdVerdict(VisWalk *w, int l, const RdCmd *c, const RdStateBlock *st)
{
    const RdFrame *f = w->f;
    if (c->type == RDC_PARTICLES || l > VIS_LAST_LIST) {
        w->n.exempt++; /* the particles (v1), the 2D layer's lists */
        s_vis.whyCur = c->type == RDC_PARTICLES ? RD_VIS_WHY_PARTICLES : RD_VIS_WHY_LIST_2D;
        return RD_VIS_KEEP;
    }
    const RdTargetRec *tc = st->color ? rd__target_rec(st->color) : NULL;
    if (!tc) {
        w->n.exempt++;
        s_vis.whyCur = RD_VIS_WHY_NO_TARGET;
        return RD_VIS_KEEP;
    }
    const uint64_t after = sizeof(RdVuPayload) + sizeof(RdVuBlock);
    if (!f->payload || c->u[2] < after || (uint64_t)c->u[1] + c->u[2] > f->payloadSize) {
        w->n.unbounded++;
        s_vis.whyCur = RD_VIS_WHY_NO_PAYLOAD;
        return RD_VIS_KEEP;
    }
    const uint8_t *pay = f->payload + c->u[1];
    RdVuPayload p;
    memcpy(&p, pay, sizeof(p));
    if (p.clip == RD_VU_CLIP_NONE) {
        w->n.exempt++; /* code 34: X and Y wrap to 16 bits */
        s_vis.whyCur = RD_VIS_WHY_CLIP_NONE;
        return RD_VIS_KEEP;
    }
    if (p.clip != RD_VU_CLIP_REGION && p.clip != RD_VU_CLIP_SCISSOR) {
        w->n.unbounded++;
        s_vis.whyCur = RD_VIS_WHY_CLIP_UNKNOWN;
        return RD_VIS_KEEP;
    }
    const bool scissor = p.clip == RD_VU_CLIP_SCISSOR;
    float mem[36][4];
    memcpy(mem, pay + sizeof(RdVuPayload), sizeof(mem));
    const RdVisRect r = rd__vis_rect(st->gsW, st->gsH, tc->w, tc->h);
    int v = RD_VIS_KEEP;
    bool bounded = false;
    int why = RD_VIS_WHY_NO_MESH; /* until a bound is made */
    if (c->type == RDC_MESH || c->type == RDC_SKINNED) {
        const bool skin = c->type == RDC_SKINNED;
        const bool progOk = skin ? (p.prog == RD_PROG_SKIN || p.prog == RD_PROG_SKIN_SPEC)
                                 : p.prog <= RD_PROG_REFLECT;
        const RdMeshRec *m = progOk ? rd__mesh_rec(c->u[0]) : NULL;
        if (m && m->vu && m->stream && m->vertexCount && m->qwPerVertex &&
            p.firstBatch < m->batchCount) {
            VisEntry *e = entryOf(c->u[0], m);
            w->noMemory = w->noMemory || !e;
            why = skin ? RD_VIS_WHY_NO_SKIN : RD_VIS_WHY_NO_BOX;
            if (e && !skin && e->boxOk && e->box.n) {
                bounded = true;
                v = rd__vis_test((const float (*)[4]) & mem[16], &e->box, scissor, &r);
                why = scissor            ? RD_VIS_WHY_KEEP_SCISSOR
                      : p.batchCount > 1 ? RD_VIS_WHY_KEEP_MESH_N
                                         : RD_VIS_WHY_KEEP_MESH_1;
                /* the draw reads some of the mesh's batches (doVuDraw's
                 * range): their own box may lie off the target when the
                 * whole stream's does not */
                uint32_t last = p.firstBatch + p.batchCount;
                if (last > m->batchCount || p.batchCount == 0) {
                    last = m->batchCount;
                }
                RdVisBox rb;
                if (v == RD_VIS_KEEP && s_vis.range &&
                    (p.firstBatch != 0 || last != m->batchCount) &&
                    rangeBox(e, m, p.firstBatch, last, &rb)) {
                    v = rd__vis_test((const float (*)[4]) & mem[16], &rb, scissor, &r);
                    if (v != RD_VIS_KEEP) {
                        why = RD_VIS_WHY_SKIP_RANGE;
                    }
                } else if (v != RD_VIS_KEEP) {
                    why = RD_VIS_WHY_SKIP_WHOLE;
                }
            } else if (e && skin && (uint64_t)after + (uint64_t)p.boneQw * 16u <= c->u[2]) {
                const RdVisSkin *s = skinOf(e, m);
                w->noMemory = w->noMemory || !s;
                if (s && s->ok) {
                    bounded = true;
                    const float (*bones)[4] = (const float (*)[4])(const void *)(pay + after);
                    v = rd__vis_test_skin((const float (*)[4])mem, bones, p.boneQw, s, &r);
                    why = v == RD_VIS_KEEP ? RD_VIS_WHY_KEEP_SKIN : RD_VIS_WHY_SKIP_WHOLE;
                }
            }
        }
    } else if (c->type == RDC_GRID) {
        why = RD_VIS_WHY_NO_GRID;
        const uint64_t at = after + (uint64_t)p.boneQw * 16u;
        if ((p.prog == RD_PROG_GRID || p.prog == RD_PROG_GRID_LIT) &&
            at + (uint64_t)p.streamQw * 16u <= c->u[2]) {
            const float (*stream)[4] = (const float (*)[4])(const void *)(pay + at);
            RdVisBox b;
            if (rd__vis_grid_box(stream, p.streamQw, p.batchCount, p.vertsPerBatch, p.qwPerVertex,
                                 &b) &&
                b.n) {
                bounded = true;
                v = rd__vis_test((const float (*)[4]) & mem[16], &b, scissor, &r);
                why = v == RD_VIS_KEEP ? RD_VIS_WHY_KEEP_GRID : RD_VIS_WHY_SKIP_WHOLE;
            }
        }
    }
    if (!bounded) {
        w->n.unbounded++;
    }
    s_vis.whyCur = why;
    return v;
}

static void visWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    VisWalk *w = user;
    if (c->type < RDC_MESH || c->type > RDC_PARTICLES) {
        return;
    }
    w->n.draws++;
    const int v = cmdVerdict(w, list, c, st);
    w->n.byVerdict[v]++;
    s_vis.why[s_vis.whyCur]++;
    if (v == RD_VIS_KEEP) {
        return;
    }
    if (s_vis.nSkip == s_vis.capSkip) {
        const uint32_t cap = s_vis.capSkip ? s_vis.capSkip * 2u : 1024u;
        uint8_t *nl = realloc(s_vis.skipL, cap);
        if (nl) {
            s_vis.skipL = nl;
        }
        uint32_t *ni = nl ? realloc(s_vis.skipI, (size_t)cap * sizeof(uint32_t)) : NULL;
        if (ni) {
            s_vis.skipI = ni;
        }
        if (!nl || !ni) {
            w->noMemory = true;
            w->n.byVerdict[v]--;
            w->n.byVerdict[RD_VIS_KEEP]++;
            s_vis.why[s_vis.whyCur]--;
            s_vis.why[RD_VIS_WHY_KEEP_MESH_1]++;
            return; /* drawn */
        }
        s_vis.capSkip = cap;
    }
    s_vis.skipL[s_vis.nSkip] = (uint8_t)list;
    s_vis.skipI[s_vis.nSkip] = index;
    s_vis.nSkip++;
    w->n.skipped++;
    w->n.frustum++;
}

/* rate-limited: the first 8, then every 100th */
static void noMemoryLine(void)
{
    static uint32_t s_seen;
    s_seen++;
    if (s_seen <= 8u || s_seen % 100u == 0u) {
        rd__log("cull: no memory for the shadow lists or the bounds: the scene draws it could "
                "not take are replayed (%u replays so far)",
                s_seen);
    }
}

const RdFrame *rd__vis_frame(const RdFrame *f, int keep)
{
    s_vis.src = NULL;
    if (!f || rd__vis_mode() == RD_VIS_OFF || g_rdWin.first || f->number == 0) {
        memset(&s_vis.last, 0, sizeof(s_vis.last));
        memset(s_vis.why, 0, sizeof(s_vis.why));
        return f;
    }
    memset(s_vis.why, 0, sizeof(s_vis.why));
    const double t0 = rd__now_ms();
    VisWalk w;
    memset(&w, 0, sizeof(w));
    w.f = f;
    s_vis.nSkip = 0;
    RdStateBlock st = f->startState;
    rd__walk(f, keep, &st, visWalk, &w);
    const RdFrame *out = f;
    if (s_vis.nSkip) {
        /* the shadow: the frame, its lists with a skipped command copied */
        bool ok = true;
        bool copy[RD_LIST_COUNT] = {false};
        for (uint32_t k = 0; k < s_vis.nSkip; k++) {
            copy[s_vis.skipL[k]] = true;
        }
        for (int l = 0; l < RD_LIST_COUNT && ok; l++) {
            const uint32_t n = f->lists[l].count;
            if (!copy[l] || n <= s_vis.cap[l]) {
                continue;
            }
            RdCmd *g = realloc(s_vis.cmds[l], (size_t)n * sizeof(RdCmd));
            if (!g) {
                ok = false;
                break;
            }
            s_vis.cmds[l] = g;
            s_vis.cap[l] = n;
        }
        if (ok) {
            s_vis.shadow = *f;
            for (int l = 0; l < RD_LIST_COUNT; l++) {
                if (!copy[l]) {
                    continue;
                }
                const uint32_t n = f->lists[l].count;
                memcpy(s_vis.cmds[l], f->lists[l].cmds, (size_t)n * sizeof(RdCmd));
                s_vis.shadow.lists[l].cmds = s_vis.cmds[l];
                s_vis.shadow.lists[l].cap = n;
            }
            for (uint32_t k = 0; k < s_vis.nSkip; k++) {
                s_vis.cmds[s_vis.skipL[k]][s_vis.skipI[k]].type = RDC_NOP;
            }
            s_vis.src = f;
            out = &s_vis.shadow;
        } else {
            w.noMemory = true;
            w.n.byVerdict[RD_VIS_KEEP] += w.n.skipped;
            w.n.skipped = w.n.frustum = 0;
            s_vis.why[RD_VIS_WHY_KEEP_MESH_1] +=
                s_vis.why[RD_VIS_WHY_SKIP_WHOLE] + s_vis.why[RD_VIS_WHY_SKIP_RANGE];
            s_vis.why[RD_VIS_WHY_SKIP_WHOLE] = s_vis.why[RD_VIS_WHY_SKIP_RANGE] = 0;
        }
    }
    if (w.noMemory) {
        noMemoryLine();
    }
    w.n.ms = rd__now_ms() - t0;
    s_vis.last = w.n;
    g_rdPerf.cullDraws = w.n.draws;
    g_rdPerf.cullSkipped = w.n.skipped;
    g_rdPerf.cullFrustum = w.n.frustum;
    g_rdPerf.cullOccluded = w.n.occluded;
    g_rdPerf.cullMs = w.n.ms;
    return out;
}
