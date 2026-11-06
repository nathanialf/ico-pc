/* rd_vis_hiz_test.c: the occlusion test's cheaper collect (rd_vis_hiz.c
 * rd__vis_hiz_mesh_ex and rd__vis_hiz_screen_area, rd_vis_hiz_fast.c, rd_vis.c
 * hizCollect; docs/port/RENDER.md, "Culling", the occlusion test's cost),
 * CPU only, without a device (rd__init_record_only):
 *
 *   area    random perspective matrices and boxes (code 32, and code 36's
 *           pos.w range): every triangle of three random points of the box
 *           whose corners rd__vis_hiz_vertex accepts has rd__vis_hiz_tri's
 *           area at most rd__vis_hiz_screen_area's bound
 *   fast    random meshes of many batches (small clusters, wide ones, ones
 *           crossing the eye plane) under random cameras, clip modes 32, 34
 *           and 36 and batch ranges: the fast pass, with every batch's box
 *           and the check on, puts out the very triangles of the full pass
 *           in its order, field for field, and the check finds none it left
 *           wrongly; both of its shortcuts are taken
 *   frame   random recorded frames of list 0 (walls near, boxes far, some
 *           draws under Z test ALWAYS, some of part of their mesh) under
 *           [dev] cull = full with ICO_RD_CULL_HIZ_FAST=0 (the plain pass),
 *           then on with ICO_RD_CULL_HIZ_CHECK=1 three times (the second and
 *           third take the occluders from the collect cache): the same
 *           commands replayed as NOPs, the same reasons (cull why, cull
 *           occlusion) and counts, no check wrong; draws hidden, cache hits
 *           and the vertex pass's shortcuts seen over the frames
 *   cache   a frame replayed again after its mesh's stream was written (a
 *           new serial) and after its matrix moved: the cache misses, the
 *           verdicts follow the new data */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_mesh.h"
#include "rd_replay.h"
#include "rd_vis.h"
#include "shader_consts.h"
#include "../../test/ico_check.h"

static uint64_t s_rng = 0x2545F4914F6CDD1Dull;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    return (uint32_t)(s_rng >> 16);
}

/* uniform in [a, b] */
static double uni(double a, double b)
{
    return a + (b - a) * (double)(rnd() & 0xFFFFFF) / (double)0xFFFFFF;
}

static void setEnvVar(const char *name, const char *v)
{
#ifdef _WIN32
    _putenv_s(name, v ? v : "");
#else
    if (v) {
        setenv(name, v, 1);
    } else {
        unsetenv(name);
    }
#endif
}

/* A camera: world to camera space by a random rotation r (a place in front
 * of it is r^T times a camera-space point), then the GS projection h.x =
 * f X + 2048 Z, h.y = f Y + 2048 Z, h.z = kz, h.w = Z, in the VU's order
 * (m[k][c]: input k into output c) */
typedef struct Cam {
    double r[3][3], f, kz;
} Cam;

static void randomCam(Cam *c)
{
    /* a rotation from three random angles */
    const double a = uni(-3.1, 3.1), b = uni(-1.5, 1.5), g = uni(-3.1, 3.1);
    const double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b), cg = cos(g), sg = sin(g);
    const double rz[3][3] = {{ca, -sa, 0}, {sa, ca, 0}, {0, 0, 1}};
    const double ry[3][3] = {{cb, 0, sb}, {0, 1, 0}, {-sb, 0, cb}};
    const double rx[3][3] = {{1, 0, 0}, {0, cg, -sg}, {0, sg, cg}};
    double t[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            t[i][j] = 0.0;
            for (int k = 0; k < 3; k++) {
                t[i][j] += rz[i][k] * ry[k][j];
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            c->r[i][j] = 0.0;
            for (int k = 0; k < 3; k++) {
                c->r[i][j] += t[i][k] * rx[k][j];
            }
        }
    }
    c->f = uni(150.0, 600.0);
    c->kz = uni(1.0e5, 4.0e6);
}

static void camMatrix(const Cam *c, float m[4][4])
{
    for (int k = 0; k < 3; k++) {
        m[k][0] = (float)(c->f * c->r[0][k] + 2048.0 * c->r[2][k]);
        m[k][1] = (float)(c->f * c->r[1][k] + 2048.0 * c->r[2][k]);
        m[k][2] = 0.0f;
        m[k][3] = (float)c->r[2][k];
    }
    m[3][0] = m[3][1] = 0.0f;
    m[3][2] = (float)c->kz;
    m[3][3] = 0.0f;
}

/* the world point at camera-space (x, y, z) */
static void worldOf(const Cam *c, double x, double y, double z, double out[3])
{
    for (int k = 0; k < 3; k++) {
        out[k] = c->r[0][k] * x + c->r[1][k] * y + c->r[2][k] * z;
    }
}

/* code 36's clip matrix: |x|, |y|, |z| well below w for world points of a
 * few hundred units */
static void clipMatrix(float c[4][4])
{
    memset(c, 0, 16 * sizeof(float));
    c[0][0] = c[1][1] = c[2][2] = 1.0e-3f;
    c[3][3] = 1.0f;
}

/* ----------------------------------------------------------------- area */

static void checkArea(void)
{
    uint32_t tris = 0, under = 0;
    for (int trial = 0; trial < 3000; trial++) {
        Cam cam;
        randomCam(&cam);
        float m[4][4];
        camMatrix(&cam, m);
        const bool scissor = (trial & 1) != 0;
        /* a box about a camera-space point in front of the eye (some cross
         * the eye plane: the bound then refuses) */
        const double z = uni(-20.0, 400.0);
        double ctr[3];
        worldOf(&cam, uni(-0.6, 0.6) * z, uni(-0.5, 0.5) * z, z, ctr);
        const double half = uni(0.01, 1.0) * (trial % 3 == 0 ? 60.0 : 4.0);
        RdVisBox b;
        memset(&b, 0, sizeof(b));
        for (int k = 0; k < 3; k++) {
            b.lo[k] = (float)(ctr[k] - half * uni(0.2, 1.0));
            b.hi[k] = (float)(ctr[k] + half * uni(0.2, 1.0));
        }
        b.lo[3] = scissor ? (float)uni(0.8, 1.0) : 1.0f;
        b.hi[3] = scissor ? (float)uni(1.0, 1.2) : 1.0f;
        b.n = 8;
        double bound;
        if (!rd__vis_hiz_screen_area((const float (*)[4])m, &b, scissor, &bound)) {
            continue;
        }
        float cl[4][4];
        clipMatrix(cl);
        for (int t = 0; t < 16; t++) {
            RdVisHizVtx v[3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) {
                float p[4];
                for (int i = 0; i < 3; i++) {
                    p[i] = (float)uni(b.lo[i], b.hi[i]);
                    /* the corners themselves now and then */
                    if ((rnd() & 7u) == 0u) {
                        p[i] = (rnd() & 1u) ? b.hi[i] : b.lo[i];
                    }
                }
                p[3] = (float)uni(b.lo[3], b.hi[3]);
                ok = rd__vis_hiz_vertex((const float (*)[4])m,
                                        scissor ? (const float (*)[4])cl : NULL, p, &v[k]);
            }
            RdVisHizTri tri;
            if (!ok || !rd__vis_hiz_tri(&v[0], &v[1], &v[2], &tri)) {
                continue;
            }
            tris++;
            under += tri.area <= bound ? 1u : 0u;
            CHECK(tri.area <= bound, "area: trial %d: a triangle's area %.9g above the bound %.9g",
                  trial, tri.area, bound);
        }
    }
    CHECK(tris > 1000u, "area: %u triangles accepted", tris);
    printf("  area: %u of %u accepted triangles within their box's bound\n", under, tris);
}

/* ----------------------------------------------------------------- fast */

enum { MAX_B = 12, MAX_V = 12, QPV = 3 };

/* a random mesh of nb batches (strips of 3 to MAX_V prelit vertices) seen
 * by cam: small clusters, wide ones, ones crossing the eye plane; pos.w in
 * [0.9, 1.1] (code 36 reads it) */
static uint32_t randomMesh(const Cam *cam, uint32_t nb, double zNear, double zFar, bool wide,
                           const char *name)
{
    static float st[MAX_B * MAX_V * QPV][4];
    static uint32_t index[MAX_B * MAX_V * 3];
    RdVuBatchRec br[MAX_B];
    memset(st, 0, sizeof(st));
    memset(br, 0, sizeof(br));
    uint32_t nv = 0, ni = 0;
    for (uint32_t b = 0; b < nb; b++) {
        const uint32_t kind = rnd() % 4u;
        const uint32_t vc = 3u + rnd() % (MAX_V - 2u);
        const double z = uni(zNear, zFar);
        const double cx = uni(-0.4, 0.4) * z, cy = uni(-0.35, 0.35) * z;
        /* a cluster's half size in camera units: tiny (a few GS pixels),
         * wide (a large share of the frame), medium */
        const double half = kind == 0           ? uni(0.001, 0.02) * z
                            : kind == 1 || wide ? uni(0.2, 0.6) * z
                                                : uni(0.02, 0.15) * z;
        br[b].firstIndex = ni;
        br[b].firstVertex = nv;
        br[b].vertexCount = vc;
        for (uint32_t v = 0; v < vc; v++) {
            double zz = z + uni(-half, half);
            if (kind == 3 && v == vc / 2u) {
                zz = -uni(1.0, 20.0); /* behind the eye */
            }
            double w[3];
            worldOf(cam, cx + uni(-half, half), cy + uni(-half, half), zz, w);
            float *p = st[(nv + v) * QPV];
            p[0] = (float)w[0];
            p[1] = (float)w[1];
            p[2] = (float)w[2];
            p[3] = (float)uni(0.9, 1.1);
            st[(nv + v) * QPV + 1][3] = v == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
            st[(nv + v) * QPV + 2][3] = 128.0f;
            if (v >= 2u) {
                for (uint32_t c = 0; c < 3u; c++) {
                    index[ni++] = ICO_VU_INDEX(nv + v, c);
                }
            }
        }
        br[b].indexCount = ni - br[b].firstIndex;
        nv += vc;
    }
    return rd__vu_mesh_create_raw((const float (*)[4])st, nv, QPV, index, ni, br, nb, name);
}

static bool sameTri(const RdVisHizTri *a, const RdVisHizTri *b)
{
    return memcmp(a->x, b->x, sizeof(a->x)) == 0 && memcmp(a->y, b->y, sizeof(a->y)) == 0 &&
           a->r == b->r && a->area == b->area && a->lb == b->lb && a->cmd == b->cmd &&
           a->sel == b->sel;
}

static void checkFast(void)
{
    static RdVisHizList full, fast;
    uint32_t meshes = 0, kept = 0, batchLeft = 0, areaLeft = 0, checked = 0;
    for (int trial = 0; trial < 400; trial++) {
        Cam cam;
        randomCam(&cam);
        const uint32_t nb = 1u + rnd() % MAX_B;
        const uint32_t id = randomMesh(&cam, nb, 8.0, 300.0, false, "hiz fast");
        const RdMeshRec *m = id ? rd__mesh_rec(id) : NULL;
        CHECK(m != NULL, "fast: trial %d: the mesh made", trial);
        if (!m) {
            continue;
        }
        meshes++;
        RdVisBox bb[MAX_B];
        for (uint32_t b = 0; b < nb; b++) {
            const RdVuBatchRec *r = &m->batches[b];
            (void)rd__vis_box((const float (*)[4])m->stream + (size_t)r->firstVertex * QPV,
                              r->vertexCount, QPV, &bb[b]);
        }
        float mem[36][4];
        memset(mem, 0, sizeof(mem));
        camMatrix(&cam, &mem[16]);
        clipMatrix(&mem[20]);
        static const uint8_t clips[3] = {RD_VU_CLIP_REGION, RD_VU_CLIP_NONE, RD_VU_CLIP_SCISSOR};
        for (int k = 0; k < 3; k++) {
            RdVuPayload p;
            memset(&p, 0, sizeof(p));
            p.clip = clips[k];
            p.firstBatch = rnd() % nb;
            p.batchCount = (rnd() & 3u) == 0u ? 0u : rnd() % (nb - p.firstBatch + 1u);
            const double minArea = (rnd() & 1u) ? 512.0 * 448.0 / 64.0 : 640.0 * 480.0 / 64.0;
            full.n = fast.n = 0;
            uint32_t lbFull, lbFast;
            const int aFull =
                rd__vis_hiz_mesh(m, &p, (const float (*)[4])mem, 7u, minArea, &full, &lbFull);
            RdVisHizPass ps;
            memset(&ps, 0, sizeof(ps));
            ps.fast = true;
            ps.check = true;
            ps.bb = bb;
            ps.bbN = nb;
            const int aFast = rd__vis_hiz_mesh_ex(m, &p, (const float (*)[4])mem, 7u, minArea,
                                                  &fast, &lbFast, &ps);
            CHECK(aFull >= 0 && aFast >= 0, "fast: trial %d: memory", trial);
            bool same = full.n == fast.n;
            for (uint32_t t = 0; same && t < full.n; t++) {
                same = sameTri(&full.t[t], &fast.t[t]);
            }
            CHECK(same, "fast: trial %d clip %u batches %u + %u: %u triangles in full, %u fast",
                  trial, (unsigned)p.clip, p.firstBatch, p.batchCount, full.n, fast.n);
            CHECK(ps.wrong == 0u, "fast: trial %d clip %u: %u left wrongly", trial,
                  (unsigned)p.clip, ps.wrong);
            kept += full.n;
            batchLeft += ps.trisBatch;
            areaLeft += ps.trisArea;
            checked += ps.checked;
        }
    }
    CHECK(kept > 100u && batchLeft > 100u && areaLeft > 100u,
          "fast: %u kept, %u left by their batch's box, %u by their own", kept, batchLeft,
          areaLeft);
    printf("  fast: %u meshes x 3 clip modes: %u triangles kept by both passes; %u left by "
           "their batch's box, %u by their own, %u checked in full, none wrongly\n",
           meshes, kept, batchLeft, areaLeft, checked);
}

/* ---------------------------------------------------------------- frame */

/* a VU mesh command into list 0: the payload with mem[16..19] m, mem[20..23]
 * the clip matrix, the batch range */
static RdCmd *pushMesh(RdFrame *f, uint32_t mesh, uint8_t clip, const float (*m)[4],
                       uint32_t firstBatch, uint32_t batchCount)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    memset(buf, 0, sizeof(buf));
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.code = clip == RD_VU_CLIP_SCISSOR ? 36 : clip == RD_VU_CLIP_NONE ? 34 : 32;
    p.clip = clip;
    p.prog = RD_PROG_PRELIT;
    p.firstBatch = firstBatch;
    p.batchCount = batchCount;
    RdVuBlock blk;
    memset(&blk, 0, sizeof(blk));
    memcpy(&blk.mem[16], m, 64);
    memcpy(&blk.mem[4], m, 64);
    clipMatrix(&blk.mem[20]);
    memcpy(buf, &p, sizeof(p));
    memcpy(buf + sizeof(p), &blk, sizeof(blk));
    const uint32_t off = rd__frame_payload(f, buf, (uint32_t)sizeof(buf));
    RdCmd *c = rd__st_push(f, 0, RDC_MESH);
    c->u[0] = mesh;
    c->b[0] = RD_PROG_PRELIT;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(buf);
    return c;
}

static void pushTarget(RdFrame *f, uint32_t id)
{
    RdCmd *c = rd__st_push(f, 0, RDC_TARGET);
    c->u[0] = id;
    c->u[1] = id;
    c->u[2] = 512u | (448u << 16);
    c->b[0] = 1;
}

static void pushClear(RdFrame *f, uint32_t id)
{
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    c->u[0] = id;
    c->b[4] = 1;
}

/* what a walk decided: every list-0 command's type in the frame it
 * returned, the reasons and the counts that do not time */
typedef struct Walk {
    uint8_t type[256];
    uint32_t why[RD_VIS_WHY_COUNT], hz[RD_VIS_HZ_COUNT];
    RdVisCounts n;
} Walk;

static void walkOf(const RdFrame *f, Walk *w)
{
    memset(w, 0, sizeof(*w));
    const RdFrame *s = rd__vis_frame(f, 0);
    for (uint32_t i = 0; i < s->lists[0].count && i < 256u; i++) {
        w->type[i] = s->lists[0].cmds[i].type;
    }
    rd__vis_why(w->why);
    rd__vis_hiz_why(w->hz);
    rd__vis_counts(&w->n);
}

static bool sameWalk(const Walk *a, const Walk *b)
{
    return memcmp(a->type, b->type, sizeof(a->type)) == 0 &&
           memcmp(a->why, b->why, sizeof(a->why)) == 0 &&
           memcmp(a->hz, b->hz, sizeof(a->hz)) == 0 && a->n.draws == b->n.draws &&
           a->n.skipped == b->n.skipped && a->n.frustum == b->n.frustum &&
           a->n.occluded == b->n.occluded && a->n.exempt == b->n.exempt &&
           a->n.unbounded == b->n.unbounded &&
           memcmp(a->n.byVerdict, b->n.byVerdict, sizeof(a->n.byVerdict)) == 0 &&
           a->n.hizTris == b->n.hizTris && a->n.hizOverBudget == b->n.hizOverBudget &&
           a->n.hizTested == b->n.hizTested && a->n.hizResets == b->n.hizResets &&
           a->n.hizCells == b->n.hizCells && a->n.hizTrisKept == b->n.hizTrisKept;
}

/* a one-batch strip mesh of the camera-space points cs[0 .. n - 1] */
static uint32_t stripMesh(const Cam *cam, const double (*cs)[3], uint32_t n, const char *name)
{
    float st[8 * QPV][4];
    uint32_t index[6 * 3];
    memset(st, 0, sizeof(st));
    uint32_t ni = 0;
    for (uint32_t v = 0; v < n && v < 8u; v++) {
        double w[3];
        worldOf(cam, cs[v][0], cs[v][1], cs[v][2], w);
        float *p = st[v * QPV];
        p[0] = (float)w[0];
        p[1] = (float)w[1];
        p[2] = (float)w[2];
        p[3] = 1.0f;
        st[v * QPV + 1][3] = v == 0 ? 0.0f : 1.0f;
        st[v * QPV + 2][3] = 128.0f;
        if (v >= 2u) {
            for (uint32_t c = 0; c < 3u; c++) {
                index[ni++] = ICO_VU_INDEX(v, c);
            }
        }
    }
    RdVuBatchRec batch;
    memset(&batch, 0, sizeof(batch));
    batch.indexCount = ni;
    batch.vertexCount = n;
    return rd__vu_mesh_create_raw((const float (*)[4])st, n, QPV, index, ni, &batch, 1, name);
}

/* A wall square near the eye (camera z 20, x / z and y / z from -0.5 to
 * 0.5: its first triangle the half below the diagonal x + y = 0) and a
 * tiny box far behind it at x / z = y / z = -0.2, well inside that
 * triangle: hidden in every camera */
static void pushPair(RdFrame *f, const Cam *cam, const float (*m)[4])
{
    const double wall[4][3] = {{-10, -10, 20}, {10, -10, 20}, {-10, 10, 20}, {10, 10, 20}};
    const double z = 200.0, u = -0.2 * z, e = 0.5;
    const double box[4][3] = {
        {u - e, u - e, z}, {u + e, u - e, z + 1.0}, {u - e, u + e, z + 2.0}, {u + e, u + e, z}};
    const uint32_t w = stripMesh(cam, wall, 4, "hiz pair wall");
    const uint32_t b = stripMesh(cam, box, 4, "hiz pair box");
    if (w && b) {
        pushMesh(f, w, RD_VU_CLIP_REGION, m, 0, 1);
        pushMesh(f, b, RD_VU_CLIP_REGION, m, 0, 1);
    }
}

/* a random frame: a camera, walls near it (wide meshes), boxes far behind
 * (one small cluster each), a wall and a box it hides, now and then a draw
 * under Z test ALWAYS, a clear, or a draw of part of its mesh */
static void randomFrame(RdFrame *f, uint32_t scene, const Cam *cam)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->number = 9;
    pushTarget(f, scene);
    rd__st_test(f, 0, RD_TEST_Z_GEQUAL);
    float m[4][4];
    camMatrix(cam, m);
    const int draws = 6 + (int)(rnd() % 20u);
    const int pairAt = (int)(rnd() % (uint32_t)draws);
    for (int d = 0; d < draws && f->lists[0].count < 240u; d++) {
        if (d == pairAt) {
            pushPair(f, cam, (const float (*)[4])m);
        }
        const uint32_t what = rnd() % 16u;
        if (what == 0u) {
            pushClear(f, scene);
            continue;
        }
        const bool wall = what < 7u;
        const uint32_t nb = 1u + rnd() % (MAX_B / 2u);
        const uint32_t id = wall ? randomMesh(cam, nb, 10.0, 40.0, true, "hiz frame wall")
                                 : randomMesh(cam, 1, 60.0, 300.0, false, "hiz frame box");
        if (!id) {
            continue;
        }
        const bool always = (rnd() % 8u) == 0u;
        if (always) {
            rd__st_test(f, 0, RD_TEST_Z_ALWAYS);
        }
        const uint8_t clip = (rnd() % 4u) == 0u   ? RD_VU_CLIP_SCISSOR
                             : (rnd() % 6u) == 0u ? RD_VU_CLIP_NONE
                                                  : RD_VU_CLIP_REGION;
        const uint32_t n = wall ? nb : 1u;
        const uint32_t first = (rnd() % 3u) == 0u ? rnd() % n : 0u;
        const uint32_t count = first ? 1u + rnd() % (n - first) : 0u;
        pushMesh(f, id, clip, (const float (*)[4])m, first, count);
        if (always) {
            rd__st_test(f, 0, RD_TEST_Z_GEQUAL);
        }
    }
    f->closed = 1;
}

static void checkFrames(void)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    uint32_t frames = 0, occluded = 0, hits = 0, left = 0, checked = 0, draws = 0;
    rd__vis_set_mode(RD_VIS_FULL);
    for (int trial = 0; trial < 60; trial++) {
        Cam cam;
        randomCam(&cam);
        static RdFrame f;
        randomFrame(&f, scene, &cam);
        static Walk plain, fast;
        setEnvVar("ICO_RD_CULL_HIZ_FAST", "0");
        setEnvVar("ICO_RD_CULL_HIZ_CHECK", NULL);
        rd__vis_init();
        walkOf(&f, &plain);
        CHECK(plain.n.hizCacheHits == 0u && plain.n.hizTrisBatch == 0u && plain.n.hizTrisArea == 0u,
              "frame %d: the plain pass takes no shortcut", trial);
        setEnvVar("ICO_RD_CULL_HIZ_FAST", NULL);
        setEnvVar("ICO_RD_CULL_HIZ_CHECK", "1");
        rd__vis_init();
        CHECK(rd__vis_hiz_fast(), "frame %d: the fast collect on by default", trial);
        for (int rep = 0; rep < 3; rep++) {
            walkOf(&f, &fast);
            CHECK(sameWalk(&plain, &fast),
                  "frame %d, walk %d: the fast collect's verdicts and counts differ from the "
                  "plain pass's (occluded %u vs %u, occluder triangles %u vs %u, cells %llu vs "
                  "%llu)",
                  trial, rep, fast.n.occluded, plain.n.occluded, fast.n.hizTrisKept,
                  plain.n.hizTrisKept, (unsigned long long)fast.n.hizCells,
                  (unsigned long long)plain.n.hizCells);
            CHECK(fast.n.hizWrong == 0u, "frame %d, walk %d: %u checks wrong", trial, rep,
                  fast.n.hizWrong);
            CHECK(rep == 0 || fast.n.hizCacheHits == fast.n.hizCacheDraws,
                  "frame %d, walk %d: %u of %u occluder draws from the cache", trial, rep,
                  fast.n.hizCacheHits, fast.n.hizCacheDraws);
            hits += fast.n.hizCacheHits;
            left += fast.n.hizTrisBatch + fast.n.hizTrisArea;
            checked += fast.n.hizChecked;
        }
        frames++;
        occluded += plain.n.occluded;
        draws += plain.n.draws;
        rd__frame_free(&f);
    }
    setEnvVar("ICO_RD_CULL_HIZ_CHECK", NULL);
    rd__vis_init();
    CHECK(occluded > 0u && hits > 0u && left > 0u && checked > 0u,
          "frames: %u occluded, %u cache hits, %u triangles left early, %u checked", occluded, hits,
          left, checked);
    printf("  frame: %u random frames (%u draws): the same verdicts with the fast collect as "
           "with the plain pass, %u occluded; %u cache hits, %u triangles left early, %u checks "
           "right\n",
           frames, draws, occluded, hits, left, checked);
}

/* ---------------------------------------------------------------- cache */

/* a wall of two triangles in front of a box (kOrtho: GS coordinates are
 * the positions), drawn twice; then the wall's stream written (moved off
 * the box, still on the target) and the matrix moved: the cache misses and
 * the verdicts follow */
static void checkCache(void)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    static const float kOrtho[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    float wall[4 * QPV][4], box[4 * QPV][4];
    memset(wall, 0, sizeof(wall));
    memset(box, 0, sizeof(box));
    const float xy[4][2] = {{1800, 1830}, {2300, 1830}, {1800, 2260}, {2300, 2260}};
    for (int v = 0; v < 4; v++) {
        wall[v * QPV][0] = xy[v][0];
        wall[v * QPV][1] = xy[v][1];
        wall[v * QPV][2] = 1000.0f;
        wall[v * QPV][3] = 1.0f;
        wall[v * QPV + 1][3] = v == 0 ? 0.0f : 1.0f;
        box[v * QPV][0] = 1850.0f + (float)(v & 1) * 50.0f;
        box[v * QPV][1] = 1870.0f + (float)(v >> 1) * 50.0f;
        box[v * QPV][2] = 400.0f;
        box[v * QPV][3] = 1.0f;
        box[v * QPV + 1][3] = v == 0 ? 0.0f : 1.0f;
    }
    const uint32_t index[6] = {ICO_VU_INDEX(2, 0), ICO_VU_INDEX(2, 1), ICO_VU_INDEX(2, 2),
                               ICO_VU_INDEX(3, 0), ICO_VU_INDEX(3, 1), ICO_VU_INDEX(3, 2)};
    RdVuBatchRec batch;
    memset(&batch, 0, sizeof(batch));
    batch.indexCount = 6;
    batch.vertexCount = 4;
    const uint32_t wallId =
        rd__vu_mesh_create_raw((const float (*)[4])wall, 4, QPV, index, 6, &batch, 1, "hiz c wall");
    const uint32_t boxId =
        rd__vu_mesh_create_raw((const float (*)[4])box, 4, QPV, index, 6, &batch, 1, "hiz c box");
    CHECK(wallId && boxId, "cache: meshes made");
    static RdFrame f;
    memset(&f, 0, sizeof(f));
    rd__reset_state_block(&f.startState);
    f.number = 9;
    pushTarget(&f, scene);
    rd__st_test(&f, 0, RD_TEST_Z_GEQUAL);
    pushMesh(&f, wallId, RD_VU_CLIP_REGION, kOrtho, 0, 1);
    pushMesh(&f, boxId, RD_VU_CLIP_REGION, kOrtho, 0, 1);
    const uint32_t at = f.lists[0].count - 1u; /* the box */
    f.closed = 1;
    rd__vis_set_mode(RD_VIS_FULL);
    rd__vis_init();
    Walk w;
    walkOf(&f, &w);
    CHECK(w.type[at] == RDC_NOP && w.n.hizCacheHits == 0u && w.n.hizCacheDraws == 1u,
          "cache: the box hidden, the wall's triangles made (%u of %u from the cache)",
          w.n.hizCacheHits, w.n.hizCacheDraws);
    walkOf(&f, &w);
    CHECK(w.type[at] == RDC_NOP && w.n.hizCacheHits == 1u,
          "cache: drawn again, the wall's triangles from the cache (%u)", w.n.hizCacheHits);
    /* the wall's stream written: moved right, still on the target but off
     * the box */
    RdMeshRec *m = rd__mesh_rec(wallId);
    CHECK(m != NULL, "cache: the wall's record");
    if (m) {
        for (int v = 0; v < 4; v++) {
            m->stream[(size_t)v * QPV][0] += 300.0f;
        }
        rd__mesh_stream_written(m);
        walkOf(&f, &w);
        CHECK(w.type[at] == RDC_MESH && w.n.hizCacheHits == 0u && w.n.hizCacheDraws == 1u,
              "cache: a new serial misses, the box kept (%u of %u from the cache)",
              w.n.hizCacheHits, w.n.hizCacheDraws);
        for (int v = 0; v < 4; v++) {
            m->stream[(size_t)v * QPV][0] -= 300.0f;
        }
        rd__mesh_stream_written(m);
        walkOf(&f, &w);
        CHECK(w.type[at] == RDC_NOP, "cache: the wall back, the box hidden again");
    }
    /* the wall's matrix moved (x + 300): the key's bytes differ */
    static RdFrame g;
    memset(&g, 0, sizeof(g));
    rd__reset_state_block(&g.startState);
    g.number = 9;
    pushTarget(&g, scene);
    rd__st_test(&g, 0, RD_TEST_Z_GEQUAL);
    float moved[4][4];
    memcpy(moved, kOrtho, sizeof(moved));
    moved[3][0] = 300.0f;
    pushMesh(&g, wallId, RD_VU_CLIP_REGION, (const float (*)[4])moved, 0, 1);
    pushMesh(&g, boxId, RD_VU_CLIP_REGION, kOrtho, 0, 1);
    const uint32_t atG = g.lists[0].count - 1u;
    g.closed = 1;
    walkOf(&g, &w);
    CHECK(w.type[atG] == RDC_MESH && w.n.hizCacheHits == 0u && w.n.hizCacheDraws == 1u,
          "cache: another matrix misses, the box kept (%u of %u from the cache)", w.n.hizCacheHits,
          w.n.hizCacheDraws);
    rd__vis_hiz_cache_clear();
    walkOf(&f, &w);
    CHECK(w.type[at] == RDC_NOP && w.n.hizCacheHits == 0u, "cache: cleared, made again");
    rd__vis_set_mode(-1);
    rd__vis_init();
    rd__frame_free(&f);
    rd__frame_free(&g);
    printf("  cache: hit when drawn again; missed after a stream write and a matrix move\n");
}

int main(void)
{
    if (!rd__init_record_only(512, 448)) {
        printf("rd_vis_hiz_test: no context\n");
        return 1;
    }
    checkArea();
    checkFast();
    checkFrames();
    checkCache();
    rd_shutdown();
    return ico_check_summary("rd_vis_hiz_test");
}
