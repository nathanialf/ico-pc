/* rd_interp_test.c's seams cases (still parts of the world between ticks,
 * and at alpha 0 through the tick before's camera): included by
 * rd_interp_test.c where they stood, so they share its helpers; a file of
 * their own keeps each under the repository's file size cap */

/* ------------------------------------------- still parts of the world
 *
 * The seams between the parts of a wall while the camera moves (rd_interp.c,
 * still draws): A and B, two prelit parts whose model to world matrices
 * differ (A turned 30 degrees about y and moved one way, B turned -50 and
 * moved another), each a triangle whose first vertex is the world point X,
 * drawn at both ticks while the camera orbits 28 degrees (s6Frame's camera).
 * X is placed at the middle of a 1/16 pixel cell of the picture through the
 * blended camera at alpha 0.5, so the rounding of the two draws' own
 * products (a few ulps) cannot carry it across a cell's edge in one draw
 * and not in the other: a different cell there is a different placement,
 * not rounding. */
static const char kObjSeam;

static void seamWorld(int which, double *w)
{
    const double a = (which ? -50.0 : 30.0) * 3.14159265358979323846 / 180.0;
    memset(w, 0, 16 * sizeof(double));
    w[0] = cos(a);
    w[2] = -sin(a);
    w[5] = 1.0;
    w[8] = sin(a);
    w[10] = cos(a);
    w[15] = 1.0;
    w[12] = which ? 140.0 : -90.0;
    w[13] = which ? -20.0 : 15.0;
    w[14] = which ? 60.0 : -35.0;
}

/* X: (37, 21, 64) moved in x and y until the blended camera at alpha t
 * (aloneExpected) puts it at frac of a 1/16 pixel cell (0.5: its middle);
 * cell its GS X and Y in 1/16 pixels */
static void seamPointAtT(double t, double frac, double x[3], int32_t cell[2])
{
    x[0] = 37.0;
    x[1] = 21.0;
    x[2] = 64.0;
    for (int it = 0; it < 4; it++) {
        double g[2], gx[2], gy[2];
        const double h = 0.01;
        const double xd[3] = {x[0] + h, x[1], x[2]}, yd[3] = {x[0], x[1] + h, x[2]};
        aloneExpected(t, x, g);
        aloneExpected(t, xd, gx);
        aloneExpected(t, yd, gy);
        const double tx = (floor(g[0] * 16.0) + frac) / 16.0,
                     ty = (floor(g[1] * 16.0) + frac) / 16.0;
        const double a = (gx[0] - g[0]) / h, b = (gy[0] - g[0]) / h;
        const double c = (gx[1] - g[1]) / h, d = (gy[1] - g[1]) / h;
        const double det = a * d - b * c;
        if (!(fabs(det) > 1e-12)) {
            break;
        }
        x[0] += ((tx - g[0]) * d - b * (ty - g[1])) / det;
        x[1] += (a * (ty - g[1]) - c * (tx - g[0])) / det;
    }
    double g[2];
    aloneExpected(t, x, g);
    cell[0] = (int32_t)floor(g[0] * 16.0);
    cell[1] = (int32_t)floor(g[1] * 16.0);
}

static void seamPointAt(double frac, double x[3], int32_t cell[2])
{
    seamPointAtT(0.5, frac, x, cell);
}

static void seamPoint(double x[3], int32_t cell[2])
{
    seamPointAt(0.5, x, cell);
}

/* the part's triangle in its model space: X through W^-1 (a turn and a
 * move: R^T (X - t)), then spread units along the model's x and y; qw the
 * mesh's stream as rd_create_vu_mesh takes it (and normal_c's batch) */
static RdMesh seamMeshSpread(const double *w, const double x[3], float (*qw)[4], double spread)
{
    double l[3];
    for (int i = 0; i < 3; i++) {
        l[i] = 0.0;
        for (int j = 0; j < 3; j++) {
            l[i] += w[i * 4 + j] * (x[j] - w[12 + j]);
        }
    }
    memset(qw, 0, 10 * sizeof(qw[0]));
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 3][0] = (float)(l[0] + (k == 1 ? spread : 0.0));
        qw[1 + k * 3][1] = (float)(l[1] + (k == 2 ? spread : 0.0));
        qw[1 + k * 3][2] = (float)l[2];
        qw[1 + k * 3][3] = 1.0f;
        qw[1 + k * 3 + 1][2] = 1.0f;
        qw[1 + k * 3 + 1][3] = k == 0 ? 0.0f : 1.0f;
        qw[1 + k * 3 + 2][0] = qw[1 + k * 3 + 2][1] = qw[1 + k * 3 + 2][2] = 128.0f;
        qw[1 + k * 3 + 2][3] = 128.0f;
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 10;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    return rd_create_vu_mesh(&md);
}

static RdMesh seamMesh(const double *w, const double x[3], float (*qw)[4])
{
    return seamMeshSpread(w, x, qw, 40.0);
}

/* seamFrameKeys records no camera (RdFrame.hasCamera 0) */
static int s_seamNoCamera;

/* a tick: the camera turned deg about the origin, A and B keyed by parts
 * pa and pb */
static void seamFrameKeys(RdMesh ma, RdMesh mb, double deg, int pa, int pb)
{
    rd_begin_frame();
    frameHead();
    double eye[3], v[16], p[16], w[16];
    s6OrbitEye(deg, eye);
    s6View(deg, eye, v);
    s6Proj(p);
    RdCamera cam;
    memset(&cam, 0, sizeof(cam));
    for (int k = 0; k < 16; k++) {
        cam.view[k] = (float)v[k];
        cam.proj[k] = (float)p[k];
    }
    cam.zoom = 500.0f;
    if (!s_seamNoCamera) {
        rd_set_camera(&cam);
    }
    s_s6List = 0;
    seamWorld(0, w);
    s6Draw(ma, v, w, RD_KEY(&kObjSeam, pa, 32));
    seamWorld(1, w);
    s6Draw(mb, v, w, RD_KEY(&kObjSeam, pb, 32));
    rd_end_frame(0);
}

/* A and B keyed by parts part and part + 1 */
static void seamFrame(RdMesh ma, RdMesh mb, double deg, int part)
{
    seamFrameKeys(ma, mb, deg, part, part + 1);
}

/* the first vertex's GS X and Y (ftoi4) of the batch qw through the block m
 * (common block qw 0..15, model matrices qw 16..27): normal_c code 32 */
static bool seamCell(const float (*m)[4], const float (*qw)[4], int32_t out[2])
{
    static Vu1Ref r;
    static VuBatchOut o;
    vu1ref_init(&r);
    vu1ref_load_common(&r, m);
    vu1ref_normal_set_matrix(&r, m + 16);
    vu1ref_normal_c(&r, 32, qw, &o);
    if (o.count != 3) {
        return false;
    }
    out[0] = o.v[0].xyz[0];
    out[1] = o.v[0].xyz[1];
    return true;
}

/* rd_interp.c's RD_INTERP_STILL_REL: two blocks' model to screen rows (qw
 * 16..19: X, Y and W) and model to clip rows (qw 20..23) agree within rel
 * of each quadword's largest magnitude (stillRows' measure) */
static bool seamRowsNear(const float (*a)[4], const float (*b)[4], float rel)
{
    for (int q = 16; q < 24; q++) {
        const unsigned lanes = q < 20 ? 0xBu : 0xFu;
        float m = 0.0f;
        for (int r = 0; r < 4; r++) {
            if (lanes & (1u << r)) {
                m = fmaxf(m, fmaxf(fabsf(a[q][r]), fabsf(b[q][r])));
            }
        }
        for (int r = 0; r < 4; r++) {
            if ((lanes & (1u << r)) && !(fabsf(a[q][r] - b[q][r]) <= m * rel)) {
                return false;
            }
        }
    }
    return true;
}

static void testStillSeams(void)
{
    static float qa[10][4], qb[10][4];
    double x[3], wa[16], wb[16];
    int32_t want[2];
    /* the game's Z row: with s6Proj's Z = z_v + 1 the re-base's recovered
     * model to world (S^-1 times the stored S W) takes its homogeneous term
     * from float(z_v + 1) - float(z_v), exact only while the two share a
     * binade: at the 28 degree tick part A lies at z_v 511.35, so Z 512.35
     * rounds at 6.1e-5 while z_v rounds at 3.05e-5, and A's re-based rows
     * moved by 7.8e-6 of their magnitude, past the still bound.  Through the
     * game's matrix Z is zf + zn z_v (zf 1.07e9): the recovered term is off
     * by about ulp(Z) / zf, 3e-8, as in the dumps (1.2e-7 at most) */
    s6GsZ = 1;
    seamPoint(x, want);
    seamWorld(0, wa);
    seamWorld(1, wb);
    RdMesh ma = seamMesh(wa, x, qa), mb = seamMesh(wb, x, qb);
    CHECK(ma.id && mb.id, "seams: the meshes");
    static float blk[2][36][4], cur[2][36][4], prv[2][36][4];
    const RdKey ka = RD_KEY(&kObjSeam, 0, 32), kb = RD_KEY(&kObjSeam, 1, 32);
    /* matched: both parts at both ticks */
    seamFrame(ma, mb, 0.0, 0);
    seamFrame(ma, mb, 28.0, 0);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->lerped == 2 && st->stillDraws == 2 && st->rotated == 0,
          "seams: two still draws drawn from the tick's block (snap %u, lerped %u, still %u, "
          "re-rounded by the blend %u, rotated %u)",
          st->snap, st->lerped, st->stillDraws, st->stillReRounded, st->rotated);
    const RdFrame *f = built(0.5f);
    const RdFrame *pf = rd__prev_frame(), *cf = rd__last_frame();
    int have = 1;
    for (int k = 0; k < 2; k++) {
        const RdKey key = k ? kb : ka;
        const float (*m)[4] = vuBlock(f, findKey(f, 0, key, 0));
        const float (*mc)[4] = vuBlock(cf, findKey(cf, 0, key, 0));
        const float (*mp)[4] = vuBlock(pf, findKey(pf, 0, key, 0));
        if (!m || !mc || !mp) {
            have = 0;
            continue;
        }
        memcpy(blk[k], m, sizeof(blk[k]));
        memcpy(cur[k], mc, sizeof(cur[k]));
        memcpy(prv[k], mp, sizeof(prv[k]));
    }
    CHECK(have, "seams: the matched draws");
    /* the ticks: alpha 1 the tick's payload byte for byte; alpha 0 seen
     * through the tick before's camera, its view to the bit (camSetup), each
     * still part's rows the tick's block through it (testStillSeamsAlpha0),
     * within the still bound of the tick before's own rows, its lights the
     * tick before's */
    f = built(1.0f);
    CHECK(f && f->payloadSize == cf->payloadSize &&
              memcmp(f->payload, cf->payload, cf->payloadSize) == 0,
          "seams: alpha 1 is the tick's payload byte for byte");
    f = built(0.0f);
    CHECK(f && memcmp(f->camera.view, pf->camera.view, sizeof(f->camera.view)) == 0,
          "seams: alpha 0, the frame's view is the tick before's bit for bit");
    for (int k = 0; have && k < 2; k++) {
        const float (*m)[4] = vuBlock(f, findKey(f, 0, k ? kb : ka, 0));
        CHECK(m && seamRowsNear(m, (const float (*)[4])prv[k], 1.0f / 131072.0f) &&
                  memcmp(m[28], prv[k][28], 8 * sizeof(m[0])) == 0,
              "seams: alpha 0, part %d's rows within the still bound of the tick before's, its "
              "lights the tick before's",
              k);
    }
    /* the same parts unmatched (the tick before drew them under other keys):
     * the tick's block through the same blended camera (camCurDraw) */
    seamFrame(ma, mb, 0.0, 10);
    seamFrame(ma, mb, 28.0, 0);
    st = build(0.5f, 1);
    CHECK(st->missing == 2 && st->curWorld == 2 && st->stillDraws == 0,
          "seams: unmatched, both the tick's through the blended camera (missing %u, parts of "
          "the world %u, still %u)",
          st->missing, st->curWorld, st->stillDraws);
    f = built(0.5f);
    int32_t cell[2][2];
    for (int k = 0; have && k < 2; k++) {
        const float (*u)[4] = vuBlock(f, findKey(f, 0, k ? kb : ka, 0));
        CHECK(u && memcmp(u[4], blk[k][4], 24 * sizeof(u[0])) == 0,
              "seams: part %d matched is bit for bit the tick's block through the blended "
              "camera (qw 4..27)",
              k);
        CHECK(memcmp(blk[k][4], cur[k][4], 24 * sizeof(blk[k][0])) != 0,
              "seams: part %d's rows are not the tick's own (the camera moved)", k);
        CHECK(seamCell((const float (*)[4])blk[k], (const float (*)[4])(k ? qb : qa), cell[k]),
              "seams: vu1_ref's batch of part %d", k);
    }
    if (have) {
        CHECK(cell[0][0] == cell[1][0] && cell[0][1] == cell[1][1] && cell[0][0] == want[0] &&
                  cell[0][1] == want[1],
              "seams: placement: the shared vertex at the middle of one ftoi4 cell, A (%d, %d), "
              "B (%d, %d), through the blended camera (%d, %d)",
              cell[0][0], cell[0][1], cell[1][0], cell[1][1], want[0], want[1]);
    }
    /* A matched, B the tick's alone (the tick before drew it under another
     * key): half way both through the blended camera, no mixed picture
     * (alpha 0: testStillSeamsAlpha0) */
    seamFrameKeys(ma, mb, 0.0, 0, 11);
    seamFrameKeys(ma, mb, 28.0, 0, 1);
    st = build(0.5f, 1);
    CHECK(st->mixed == 0 && st->worldPrevRaw == 0 && st->worldCurRaw == 0 &&
              st->camOffWhy == RD_CAM_OFF_NONE && st->curWorld == 1 && st->stillDraws == 1,
          "seams: half way, A matched and B the tick's: no mixed picture (minority %u, the "
          "tick's through the blended camera %u, still %u)",
          st->mixed, st->curWorld, st->stillDraws);
    st = build(1.0f, 1);
    CHECK(st->mixed == 0 && st->camOffWhy == RD_CAM_OFF_NONE,
          "seams: alpha 1, nothing blends: no mixed picture (minority %u)", st->mixed);
    rd_destroy_vu_mesh(ma);
    rd_destroy_vu_mesh(mb);

    /* the mechanism: C and D, two meshes of one model matrix (A's) whose
     * other corners lie 40 and 300 units out (other centroids, so other
     * pivots for a turn), both matched, the shared point within a quarter
     * of a float ulp of a cell's edge through the blended camera: any
     * difference in the last bits of their rows (the blend and a turn
     * about each mesh's centroid gave them) can put it in two cells.  Their
     * camera and model rows are bit-equal and the point is in one cell */
    static float qc[10][4], qd[10][4];
    double y[3];
    int32_t edge[2], cc[2] = {0, 0}, cd[2] = {0, 0};
    seamPointAt(0.001, y, edge);
    RdMesh mc = seamMeshSpread(wa, y, qc, 40.0), md = seamMeshSpread(wa, y, qd, 300.0);
    for (int k = 0; k < 2; k++) {
        rd_begin_frame();
        frameHead();
        double eye[3], v[16], p[16];
        s6OrbitEye(k ? 28.0 : 0.0, eye);
        s6View(k ? 28.0 : 0.0, eye, v);
        s6Proj(p);
        RdCamera cam;
        memset(&cam, 0, sizeof(cam));
        for (int i = 0; i < 16; i++) {
            cam.view[i] = (float)v[i];
            cam.proj[i] = (float)p[i];
        }
        cam.zoom = 500.0f;
        rd_set_camera(&cam);
        s_s6List = 0;
        s6Draw(mc, v, wa, RD_KEY(&kObjSeam, 20, 32));
        s6Draw(md, v, wa, RD_KEY(&kObjSeam, 21, 32));
        rd_end_frame(0);
    }
    st = build(0.5f, 1);
    CHECK(st->lerped == 2 && st->stillDraws == 2,
          "seams: one model matrix, two meshes: both blended still draws (lerped %u, still %u)",
          st->lerped, st->stillDraws);
    f = built(0.5f);
    const float (*bc)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjSeam, 20, 32), 0));
    const float (*bd)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjSeam, 21, 32), 0));
    CHECK(bc && bd && memcmp(bc[4], bd[4], 24 * sizeof(bc[0])) == 0,
          "seams: one model matrix, two meshes: camera and model rows (qw 4..27) bit-equal");
    if (bc && bd) {
        CHECK(seamCell(bc, (const float (*)[4])qc, cc) &&
                  seamCell(bd, (const float (*)[4])qd, cd) && cc[0] == cd[0] && cc[1] == cd[1],
              "seams: one model matrix, two meshes: the shared point at a cell's edge in one cell, "
              "C (%d, %d), D (%d, %d), the edge (%d, %d)",
              cc[0], cc[1], cd[0], cd[1], edge[0], edge[1]);
    }
    rd_destroy_vu_mesh(mc);
    rd_destroy_vu_mesh(md);
    s6GsZ = 0;
}

/* Alpha 0, the present the timer path's clock clamps to exactly 0 right
 * after a tick closes: A and B as in testStillSeams, X at the middle of a
 * cell through the tick before's camera (aloneExpected at 0).  The blended
 * camera there is the tick before's to the bit (rd_interp.c camSetup), and
 * three pairings give one picture: (1) both matched, both still draws; (2)
 * both unmatched (the tick before drew them under other keys), both the
 * tick's through that camera (camCurDraw); (3) A matched and B unmatched,
 * the wedge's case: a part of the world beside a piece entering the view
 * (the game's cull) or one whose payload changed size.  In all three the
 * frame's view is the tick before's, A's and B's camera and model rows (qw
 * 4..27) are those of (2) bit for bit, no picture is mixed, and X lands in
 * one cell in both, the cell through the tick before's camera.  With no
 * blended camera at alpha 0 (the renderer before camSetup took t = 0) (3)
 * drew A from the tick before's block and B from the tick's, 28 degrees
 * of orbit apart: B's X in the cell of the tick's raw block, far from A's,
 * which the last check measures. */
static void testStillSeamsAlpha0(void)
{
    static float qa[10][4], qb[10][4];
    static float blk[3][2][36][4];
    double x[3], wa[16], wb[16];
    int32_t want[2], cell[3][2][2];
    memset(cell, 0, sizeof(cell));
    s6GsZ = 1;
    seamPointAtT(0.0, 0.5, x, want);
    seamWorld(0, wa);
    seamWorld(1, wb);
    RdMesh ma = seamMesh(wa, x, qa), mb = seamMesh(wb, x, qb);
    CHECK(ma.id && mb.id, "seams 0: the meshes");
    const RdKey ka = RD_KEY(&kObjSeam, 0, 32), kb = RD_KEY(&kObjSeam, 1, 32);
    /* the tick before's keys of A and B per case; the tick's are 0 and 1 */
    static const int kPrevKeys[3][2] = {{0, 1}, {10, 11}, {0, 11}};
    static const char *const kCase[3] = {"(1) both matched", "(2) both unmatched",
                                         "(3) A matched, B unmatched"};
    static const uint32_t kStill[3] = {2, 0, 1}, kCurWorld[3] = {0, 2, 1};
    static float prv[2][36][4], cur[2][36][4];
    int have = 1;
    for (int c = 0; c < 3; c++) {
        seamFrameKeys(ma, mb, 0.0, kPrevKeys[c][0], kPrevKeys[c][1]);
        seamFrameKeys(ma, mb, 28.0, 0, 1);
        const RdInterpStats *st = build(0.0f, 1);
        CHECK(st->snap == RD_SNAP_NONE && st->stillDraws == kStill[c] &&
                  st->curWorld == kCurWorld[c] && st->mixed == 0 && st->worldPrevRaw == 0 &&
                  st->worldCurRaw == 0 && st->camOffWhy == RD_CAM_OFF_NONE,
              "seams 0: %s: still %u (want %u), the tick's through the tick before's camera %u "
              "(want %u), no mixed picture (from the tick before %u, from the tick %u, minority "
              "%u, no blended camera for %u)",
              kCase[c], st->stillDraws, kStill[c], st->curWorld, kCurWorld[c], st->worldPrevRaw,
              st->worldCurRaw, st->mixed, st->camOffWhy);
        const RdFrame *f = built(0.0f);
        const RdFrame *pf = rd__prev_frame(), *cf = rd__last_frame();
        CHECK(f && memcmp(f->camera.view, pf->camera.view, sizeof(f->camera.view)) == 0,
              "seams 0: %s: the frame's view is the tick before's bit for bit", kCase[c]);
        for (int k = 0; k < 2; k++) {
            const float (*m)[4] = f ? vuBlock(f, findKey(f, 0, k ? kb : ka, 0)) : NULL;
            const float (*mc)[4] = vuBlock(cf, findKey(cf, 0, k ? kb : ka, 0));
            const float (*mp)[4] =
                vuBlock(pf, findKey(pf, 0, RD_KEY(&kObjSeam, kPrevKeys[c][k], 32), 0));
            if (!m || !mc || !mp) {
                have = 0;
                continue;
            }
            memcpy(blk[c][k], m, sizeof(blk[c][k]));
            if (c == 0) {
                memcpy(prv[k], mp, sizeof(prv[k]));
                memcpy(cur[k], mc, sizeof(cur[k]));
            }
            CHECK(seamCell(m, (const float (*)[4])(k ? qb : qa), cell[c][k]),
                  "seams 0: %s: vu1_ref's batch of part %d", kCase[c], k);
        }
    }
    CHECK(have, "seams 0: the draws");
    for (int c = 0; have && c < 3; c++) {
        for (int k = 0; k < 2; k++) {
            CHECK(memcmp(blk[c][k][4], blk[1][k][4], 24 * sizeof(blk[c][k][0])) == 0,
                  "seams 0: %s: part %d's camera and model rows (qw 4..27) bit for bit the "
                  "tick's block through the tick before's camera (camCurDraw's, case 2)",
                  kCase[c], k);
        }
        CHECK(cell[c][0][0] == want[0] && cell[c][0][1] == want[1] && cell[c][1][0] == want[0] &&
                  cell[c][1][1] == want[1],
              "seams 0: %s: the shared vertex in one ftoi4 cell, A (%d, %d), B (%d, %d), through "
              "the tick before's camera (%d, %d)",
              kCase[c], cell[c][0][0], cell[c][0][1], cell[c][1][0], cell[c][1][1], want[0],
              want[1]);
    }
    for (int k = 0; have && k < 2; k++) {
        CHECK(seamRowsNear((const float (*)[4])blk[1][k], (const float (*)[4])prv[k],
                           1.0f / 131072.0f) &&
                  memcmp(blk[1][k][4], cur[k][4], 24 * sizeof(cur[k][0])) != 0,
              "seams 0: part %d through the tick before's camera: within the still bound of the "
              "tick before's own rows, not the tick's rows (the camera moved)",
              k);
    }
    /* the mixed picture's counters where it remains: frames without a
     * camera (no blended camera at any alpha), A matched, B unmatched */
    s_seamNoCamera = 1;
    seamFrameKeys(ma, mb, 0.0, 0, 11);
    seamFrameKeys(ma, mb, 28.0, 0, 1);
    s_seamNoCamera = 0;
    const RdInterpStats *nc = build(0.5f, 1);
    CHECK(nc->snap == RD_SNAP_NONE && nc->worldPrevRaw == 1 && nc->worldCurRaw == 1 &&
              nc->mixed == 1 && nc->camOffWhy == RD_CAM_OFF_NO_CAMERA,
          "seams 0: no camera, A matched and B the tick's: a mixed picture (from the tick before "
          "%u, from the tick %u, minority %u, no blended camera for %u)",
          nc->worldPrevRaw, nc->worldCurRaw, nc->mixed, nc->camOffWhy);
    /* what (3) drew with no blended camera at alpha 0: B's own block, the
     * tick's camera, 28 degrees of orbit away from A's place */
    int32_t raw[2] = {0, 0};
    if (have && seamCell((const float (*)[4])cur[1], (const float (*)[4])qb, raw)) {
        CHECK(abs(raw[0] - want[0]) > 16 * 16,
              "seams 0: B through the tick's own camera lands at (%d, %d), more than 16 pixels "
              "from the cell through the tick before's (%d, %d): the wedge before the fix",
              raw[0], raw[1], want[0], want[1]);
    }
    rd_destroy_vu_mesh(ma);
    rd_destroy_vu_mesh(mb);
    s6GsZ = 0;
}
