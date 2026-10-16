/* rd_interp_test.c's rotation cases (the rotation-aware blend, rotation
 * continuity across ticks and gaps, the trolley's wheels): included by
 * rd_interp_test.c where they stood, so they share its helpers; a file of
 * their own keeps each under the repository's file size cap */

/* ------------------------------------------------ rotation-aware blend */

/* column-major 4 x 4 (double): a turn of deg about z, uniform scale sc,
 * translation (tx, 0, 0) */
static void turnZ(double *m, double deg, double sc, double tx)
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    memset(m, 0, 16 * sizeof(double));
    m[0] = cos(a) * sc;
    m[1] = sin(a) * sc;
    m[4] = -sin(a) * sc;
    m[5] = cos(a) * sc;
    m[10] = sc;
    m[12] = tx;
    m[15] = 1.0;
}

static double colLen(const double *m, int c)
{
    return sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);
}

static void testRotationBlend(void)
{
    double p[16], c[16], o[16];
    turnZ(p, 0.0, 1.0, 0.0);
    turnZ(c, 90.0, 1.0, 100.0);
    CHECK(rd__blend_affine(p, c, 0.5, NULL, o), "a 90 degree turn blends");
    const double ang = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
    CHECK(fabs(ang - 45.0) < 1e-9 && fabs(colLen(o, 0) - 1.0) < 1e-12 &&
              fabs(colLen(o, 1) - 1.0) < 1e-12 && fabs(colLen(o, 2) - 1.0) < 1e-12 &&
              fabs(o[12] - 50.0) < 1e-12,
          "90 degrees at alpha 0.5: %.9f degrees, axes %.12f %.12f, x %.3f (element-wise: "
          "axes 0.707)",
          ang, colLen(o, 0), colLen(o, 1), o[12]);
    printf("rd_interp_test: 90 degrees at alpha 0.5: %.6f degrees, axes %.9f %.9f %.9f\n", ang,
           colLen(o, 0), colLen(o, 1), colLen(o, 2));
    turnZ(p, -30.0, 2.0, 0.0);
    turnZ(c, 50.0, 3.0, 0.0);
    CHECK(rd__blend_affine(p, c, 0.25, NULL, o), "turn with scale blends");
    const double ang2 = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
    CHECK(fabs(ang2 - (-10.0)) < 1e-9 && fabs(colLen(o, 0) - 2.25) < 1e-12,
          "scale lerped, rotation slerped (%.6f degrees, scale %.6f)", ang2, colLen(o, 0));
    turnZ(p, 170.0, 1.0, 0.0);
    turnZ(c, -170.0, 1.0, 0.0);
    CHECK(rd__blend_affine(p, c, 0.5, NULL, o) &&
              fabs(fabs(atan2(o[1], o[0])) * 180.0 / 3.14159265358979323846 - 180.0) < 1e-9,
          "the short way round (170 to -170 is 180 half way)");
    /* about a pivot: a limb turning 90 degrees about its joint at (100, 0,
     * 0) keeps the joint where it is half way (about the origin it would
     * leave it by 29 units) */
    {
        const double x0[3] = {100.0, 0.0, 0.0};
        turnZ(p, 0.0, 1.0, 0.0);
        turnZ(c, 90.0, 1.0, 100.0); /* R x + (100, -100, 0): R (x - x0) + x0 */
        c[13] = -100.0;
        CHECK(rd__blend_affine(p, c, 0.5, x0, o), "about a pivot");
        const double jx = o[0] * 100.0 + o[12], jy = o[1] * 100.0 + o[13];
        const double a2 = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
        CHECK(fabs(jx - 100.0) < 1e-9 && fabs(jy) < 1e-9 && fabs(a2 - 45.0) < 1e-9,
              "the joint stays at (100, 0) half way (%.6f, %.6f), turned %.6f degrees", jx, jy, a2);
    }
    turnZ(c, 10.0, 1.0, 0.0);
    c[3] = 0.25; /* projective */
    CHECK(!rd__blend_affine(p, c, 0.5, NULL, o),
          "a projective matrix is left to the element-wise blend");
    turnZ(p, 0.0, 1.0, 0.0);
    turnZ(c, 10.0, 1.0, 0.0);
    c[10] = -1.0; /* a mirror */
    CHECK(!rd__blend_affine(p, c, 0.5, NULL, o),
          "opposite handedness is left to the element-wise blend");
}

/* recordTurn's next frame is a camera cut (rd_camera_cut) */
static int s_turnCut;

/* a prelit mesh whose model to world turns deg about z, seen through a
 * world to screen S (a scale and a translation, w = 1); and a skinned draw
 * with one bone turning the same way */
static void recordTurn(RdMesh mesh, RdMesh skin, double deg)
{
    rd_begin_frame();
    frameHead();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    double s[16], w[16], m[16];
    memset(s, 0, sizeof(s));
    s[0] = 2.0;
    s[5] = 2.0;
    s[10] = 1.0;
    s[12] = 2048.0;
    s[13] = 2048.0;
    s[15] = 1.0;
    turnZ(w, deg, 1.0, 10.0);
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            double v = 0.0;
            for (int k = 0; k < 4; k++) {
                v += s[k * 4 + r] * w[cI * 4 + k];
            }
            m[cI * 4 + r] = v;
        }
    }
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            d.vu.mem[4 + cI][r] = (float)s[cI * 4 + r];
            d.vu.mem[16 + cI][r] = (float)m[cI * 4 + r];
            d.vu.mem[20 + cI][r] = (float)m[cI * 4 + r]; /* the clip matrix: the same here */
            d.vu.mem[24 + cI][r] = (float)w[cI * 4 + r]; /* model to view: view identity */
        }
    }
    identity(d.vu.mem, 12);
    rd_select_list(0);
    rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjD, 0, 32));
    /* skinned: one bone */
    static float bone[4][4];
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            bone[cI][r] = (float)w[cI * 4 + r];
        }
    }
    d.prog = RD_PROG_SKIN;
    d.code = 20;
    d.bones = (const float (*)[4])bone;
    d.boneQw = 4;
    rd_draw_vu_mesh(skin, &d, RD_KEY(&kObjD, 1, 20));
    if (s_turnCut) {
        rd_camera_cut();
        s_turnCut = 0;
    }
    rd_end_frame(0);
}

/* a cluster (skinned) layout mesh of one strip of 3 vertices */
static RdMesh makeSkinMesh(void)
{
    static float qw[1 + 3 * 5][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 5 + 3][3] = k == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
        const uint32_t addr = 16u;                   /* bone 0, weight 1: its pivot is the origin */
        memcpy(&qw[1 + k * 5 + 2][0], &addr, 4);
        qw[1 + k * 5 + 2][1] = 1.0f;
        memcpy(&qw[1 + k * 5 + 2][2], &addr, 4);
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 16;
    md.qwPerVertex = RD_VU_QW_SKIN;
    md.batchCount = 1;
    md.batches = &bd;
    return rd_create_vu_mesh(&md);
}

static void testRotationDraws(void)
{
    RdMesh mesh = makeMesh(), skin = makeSkinMesh();
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 90.0);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->lerped == 2 && st->rotated == 2, "mesh and skinned draw blend as rotations (%u, %u)",
          st->lerped, st->rotated);
    const RdFrame *f = built(0.5f);
    const float (*m)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    if (m) {
        /* S (2, 2, 1) times a 45 degree turn: the x axis (sqrt 2, sqrt 2) */
        const float ax = m[16][0], ay = m[16][1];
        const float len = sqrtf(ax * ax + ay * ay);
        CHECK(fabsf(len - 2.0f) < 1e-4f && fabsf(ax - ay) < 1e-4f &&
                  fabsf(m[19][0] - 2068.0f) < 1e-3f,
              "model to screen: 45 degrees, length %.5f (element-wise 1.414), origin x %.3f", len,
              m[19][0]);
        const float bx = m[24][0], by = m[24][1];
        CHECK(fabsf(sqrtf(bx * bx + by * by) - 1.0f) < 1e-5f && fabsf(bx - by) < 1e-5f,
              "model to view: 45 degrees, no scale change (%.5f, %.5f)", bx, by);
    } else {
        CHECK(0, "the turning mesh");
    }
    const RdCmd *sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    if (sk) {
        const RdVuPayload *h = (const RdVuPayload *)(const void *)(f->payload + sk->u[1]);
        const float (*b)[4] = (const float (*)[4])(
            const void *)(f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock));
        CHECK(h->boneQw == 4 &&
                  fabsf(sqrtf(b[0][0] * b[0][0] + b[0][1] * b[0][1]) - 1.0f) < 1e-5f &&
                  fabsf(b[0][0] - b[0][1]) < 1e-5f && fabsf(b[3][0] - 10.0f) < 1e-5f,
              "bone: 45 degrees, no scale change (%.5f, %.5f)", b[0][0], b[0][1]);
    } else {
        CHECK(0, "the skinned draw");
    }
    /* a turn of 150 degrees in a tick is a flip: the tick's matrices */
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 150.0);
    build(0.5f, 1);
    f = built(0.5f);
    const RdFrame *cur = rd__last_frame();
    const float (*mf)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    const float (*mc)[4] = vuBlock(cur, findKey(cur, 0, RD_KEY(&kObjD, 0, 32), 0));
    CHECK(mf && mc && memcmp(mf[16], mc[16], 12 * 16) == 0,
          "a 150 degree turn in a tick keeps the tick's model matrices");
    sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    const RdCmd *skc = findKey(cur, 0, RD_KEY(&kObjD, 1, 20), 0);
    CHECK(sk && skc &&
              memcmp(f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock),
                     cur->payload + skc->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock), 64) == 0,
          "a 150 degree turn in a tick keeps the tick's bone");
}

/* the turn about z of the turning draws' model to view (qw 24) and bone 0
 * in frame f, degrees in (-180, 180] */
static void turnAngles(const RdFrame *f, float *mesh, float *bone)
{
    const float (*m)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    const RdCmd *sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    *mesh = m ? atan2f(m[24][1], m[24][0]) * 57.2957795f : 1000.0f;
    *bone = 1000.0f;
    if (sk) {
        const float (*b)[4] = (const float (*)[4])(
            const void *)(f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock));
        *bone = atan2f(b[0][1], b[0][0]) * 57.2957795f;
    }
}

/* the turning draws' VU blocks and bone (qw 16..27, 64 bytes) of f and the
 * last frame equal */
static bool turnIsTick(const RdFrame *f)
{
    const RdFrame *cur = rd__last_frame();
    const float (*mf)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    const float (*mc)[4] = vuBlock(cur, findKey(cur, 0, RD_KEY(&kObjD, 0, 32), 0));
    const RdCmd *sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    const RdCmd *skc = findKey(cur, 0, RD_KEY(&kObjD, 1, 20), 0);
    return mf && mc && sk && skc && memcmp(mf[16], mc[16], 12 * 16) == 0 &&
           memcmp(f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock),
                  cur->payload + skc->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock), 64) == 0;
}

/* Rotation continuity (issue 98): a trolley wheel at full speed turns
 * more than 180 degrees a tick, and the short way would blend it back.  The
 * draw (and the bone) remembers its turn of the tick before; a turn near it
 * blends the way that continues it. */
static void testRotationContinuity(void)
{
    RdMesh mesh = makeMesh(), skin = makeSkinMesh();
    float am, ab;
    /* 0 -> 170: nothing before it, over RD_INTERP_TURN_SNAP: the tick's */
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 170.0);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->rotated == 2 && st->turnSnap == 2 && st->longWay == 0,
          "170 degrees with no turn before: snapped (rotated %u, snapped %u, long %u)", st->rotated,
          st->turnSnap, st->longWay);
    CHECK(turnIsTick(built(0.5f)), "170 degrees with no turn before keeps the tick's pose");
    /* 170 -> 360: 190 degrees on, 170 back the short way; the snapped turn
     * before was not remembered (its direction is unknown), so this one
     * snaps too */
    recordTurn(mesh, skin, 360.0);
    st = build(0.5f, 1);
    CHECK(st->rotated == 2 && st->turnSnap == 2 && st->longWay == 0 && turnIsTick(built(0.5f)),
          "0, 170, 360: the second pair keeps the tick's pose too (snapped %u, long %u)",
          st->turnSnap, st->longWay);
    /* 0 -> 190 -> 380 with nothing before it: the short way is back 170
     * both times, over the snap: the tick's both times, never blended
     * backwards on the third tick */
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 190.0);
    st = build(0.5f, 1);
    CHECK(st->turnSnap == 2 && st->longWay == 0 && turnIsTick(built(0.5f)),
          "190 degrees with no turn before keeps the tick's pose (snapped %u, long %u)",
          st->turnSnap, st->longWay);
    recordTurn(mesh, skin, 380.0);
    st = build(0.5f, 1);
    CHECK(st->turnSnap == 2 && st->longWay == 0 && turnIsTick(built(0.5f)),
          "0, 190, 380: the second pair keeps the tick's pose too (snapped %u, long %u)",
          st->turnSnap, st->longWay);
    /* a flip of +170 then -170: both keep the tick's pose (the second is
     * not taken the long way after the first) */
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 170.0);
    st = build(0.5f, 1);
    CHECK(st->turnSnap == 2 && st->longWay == 0 && turnIsTick(built(0.5f)),
          "+170 keeps the tick's pose (snapped %u, long %u)", st->turnSnap, st->longWay);
    recordTurn(mesh, skin, 0.0);
    st = build(0.5f, 1);
    CHECK(st->turnSnap == 2 && st->longWay == 0 && turnIsTick(built(0.5f)),
          "+170 then -170 keeps the tick's pose (snapped %u, long %u)", st->turnSnap, st->longWay);
    /* a wheel speeding up: 110 (blends), 160 (within RD_INTERP_TURN_KEEP of
     * 110: kept, blended on forward), 190 (the long way, continuing 160) */
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 110.0);
    st = build(0.5f, 1);
    CHECK(st->rotated == 2 && st->turnSnap == 0 && st->longWay == 0,
          "110 degrees blends (rotated %u, snapped %u, long %u)", st->rotated, st->turnSnap,
          st->longWay);
    recordTurn(mesh, skin, 270.0);
    st = build(0.5f, 1);
    CHECK(st->rotated == 2 && st->turnSnap == 0 && st->longWay == 0,
          "160 degrees after 110 blends (rotated %u, snapped %u, long %u)", st->rotated,
          st->turnSnap, st->longWay);
    turnAngles(built(0.5f), &am, &ab);
    /* 110 + 80 = 190 = -170 */
    CHECK(fabsf(am + 170.0f) < 1e-3f && fabsf(ab + 170.0f) < 1e-3f,
          "alpha 0.5 between 110 and 270: mesh %.5f, bone %.5f degrees (want -170 = 190)", am, ab);
    recordTurn(mesh, skin, 460.0);
    st = build(0.5f, 1);
    CHECK(st->rotated == 2 && st->longWay == 2 && st->turnSnap == 0 &&
              fabsf(st->maxTurn - 190.0f) < 1e-3f,
          "190 degrees after 160: the long way (rotated %u, long %u, snapped %u, largest %.4f)",
          st->rotated, st->longWay, st->turnSnap, st->maxTurn);
    turnAngles(built(0.5f), &am, &ab);
    /* 270 + 95 = 365 = 5; the short way would give 185 = -175 */
    CHECK(fabsf(am - 5.0f) < 1e-3f && fabsf(ab - 5.0f) < 1e-3f,
          "alpha 0.5 between 270 and 460 forward: mesh %.5f, bone %.5f degrees (want 5 = 365)", am,
          ab);
    printf("rd_interp_test: wheel 110, 160 then 190 degrees, alpha 0.5: mesh %.6f, bone %.6f "
           "degrees\n",
           am, ab);
    turnAngles(built(0.25f), &am, &ab);
    CHECK(fabsf(am + 42.5f) < 1e-3f && fabsf(ab + 42.5f) < 1e-3f,
          "alpha 0.25 forward: mesh %.5f, bone %.5f degrees (want -42.5 = 317.5)", am, ab);
    CHECK(turnIsTick(built(1.0f)), "alpha 1 is the tick's pose byte for byte after a long way");
    /* a 20 degree turn after a 20 degree turn: the same blend as without the
     * turn before, bit for bit */
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 20.0);
    build(0.5f, 1);
    recordTurn(mesh, skin, 40.0);
    st = build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    static uint8_t kept[16 * 12 + 64];
    const float (*mk)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    const RdCmd *sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    CHECK(mk && sk && st->longWay == 0 && st->turnSnap == 0, "20 degrees after 20 blends");
    if (mk && sk) {
        memcpy(kept, mk[16], 16 * 12);
        memcpy(kept + 16 * 12, f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock), 64);
    }
    turnAngles(f, &am, &ab);
    CHECK(fabsf(am - 30.0f) < 1e-3f && fabsf(ab - 30.0f) < 1e-3f,
          "20 degrees after 20 at alpha 0.5: mesh %.5f, bone %.5f degrees (want 30)", am, ab);
    recordTurn(mesh, skin, 20.0);
    recordTurn(mesh, skin, 40.0);
    f = built(0.5f);
    mk = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    CHECK(mk && sk && memcmp(kept, mk[16], 16 * 12) == 0 &&
              memcmp(kept + 16 * 12,
                     f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock), 64) == 0,
          "20 degrees blends bit for bit with and without the turn before");
}

/* the turning draws at alpha 0.5 of the last pair stand at want degrees
 * (mod 360) */
static bool turnHalfAt(double want, float *am, float *ab)
{
    turnAngles(built(0.5f), am, ab);
    const double dm = remainder((double)*am - want, 360.0);
    const double db = remainder((double)*ab - want, 360.0);
    return fabs(dm) < 1e-3 && fabs(db) < 1e-3;
}

/* a wheel speeding up to 190 degrees a tick, every pair blended forward:
 * 110, 160, 170 (kept, continuing 160), 190 (the long way); the angle it
 * ends at */
static double turnRamp(RdMesh mesh, RdMesh skin, const char *what)
{
    static const double steps[4] = {110.0, 160.0, 170.0, 190.0};
    double a = 0.0;
    float am, ab;
    recordTurn(mesh, skin, a);
    for (int k = 0; k < 4; k++) {
        recordTurn(mesh, skin, a + steps[k]);
        const RdInterpStats *st = build(0.5f, 1);
        CHECK(st->rotated == 2 && st->turnSnap == 0 && turnHalfAt(a + steps[k] * 0.5, &am, &ab),
              "%s: ramp %.0f degrees blends forward (snapped %u, mesh %.4f, bone %.4f, want "
              "%.1f)",
              what, steps[k], st->turnSnap, am, ab, fmod(a + steps[k] * 0.5, 360.0));
        a += steps[k];
    }
    return a;
}

/* Rotation continuity across a gap (issue 98's residual): a pair not
 * blended between (no present between two ticks, a camera cut, a frame
 * discarded) keeps the turn remembered before it for RD_INTERP_TURN_GAP
 * ticks, when the draw's pose is where that turn carries it */
static void testRotationGap(void)
{
    RdMesh mesh = makeMesh(), skin = makeSkinMesh();
    float am, ab;
    /* (a) 170, 190, then a pair not blended (+195), then 200: on forward
     * the long way, the turn of two ticks before */
    double a = turnRamp(mesh, skin, "skipped pair");
    recordTurn(mesh, skin, a + 195.0); /* no present between these two ticks */
    a += 195.0;
    recordTurn(mesh, skin, a + 200.0);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->rotated == 2 && st->longWay == 2 && st->turnSnap == 0,
          "200 degrees after a pair not blended: the long way (long %u, snapped %u)", st->longWay,
          st->turnSnap);
    CHECK(turnHalfAt(a + 100.0, &am, &ab),
          "200 degrees after a skipped pair, alpha 0.5: mesh %.4f, bone %.4f (want %.1f)", am, ab,
          fmod(a + 100.0, 360.0));
    CHECK(turnIsTick(built(1.0f)), "alpha 1 after a skipped pair is the tick's pose");
    a += 200.0;
    /* and the turn it made is remembered for the next pair as ever */
    recordTurn(mesh, skin, a + 200.0);
    st = build(0.5f, 1);
    CHECK(st->longWay == 2 && st->turnSnap == 0 && turnHalfAt(a + 100.0, &am, &ab),
          "the pair after: the long way again (long %u, snapped %u)", st->longWay, st->turnSnap);
    printf("rd_interp_test: wheel 170, 190, a skipped pair, 200 degrees, alpha 0.5: mesh %.6f, "
           "bone %.6f degrees\n",
           am, ab);

    /* (b) the same with a camera cut between the pairs: the whole-frame
     * snap keeps the memory */
    a = turnRamp(mesh, skin, "cut");
    s_turnCut = 1;
    recordTurn(mesh, skin, a + 195.0);
    a += 195.0;
    st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_CUT, "the cut's pair snaps (snap %u)", st->snap);
    recordTurn(mesh, skin, a + 200.0);
    st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->longWay == 2 && st->turnSnap == 0 &&
              turnHalfAt(a + 100.0, &am, &ab),
          "200 degrees after a cut: the long way (long %u, snapped %u, mesh %.4f, bone %.4f)",
          st->longWay, st->turnSnap, am, ab);

    /* (c) the bound: a turn remembered RD_INTERP_TURN_GAP ticks before the
     * pair's first still serves, one more tick and it does not */
    for (uint32_t ago = RD_INTERP_TURN_GAP; ago <= RD_INTERP_TURN_GAP + 2u; ago++) {
        a = turnRamp(mesh, skin, "bound");
        for (uint32_t k = 0; k < ago; k++) {
            a += 190.0;
            recordTurn(mesh, skin, a); /* not blended */
        }
        recordTurn(mesh, skin, a + 190.0);
        st = build(0.5f, 1);
        if (ago <= RD_INTERP_TURN_GAP) {
            CHECK(st->longWay == 2 && st->turnSnap == 0 && turnHalfAt(a + 95.0, &am, &ab),
                  "a turn %u ticks old serves (long %u, snapped %u, mesh %.4f, bone %.4f)", ago,
                  st->longWay, st->turnSnap, am, ab);
        } else {
            CHECK(st->longWay == 0 && st->turnSnap == 2 && turnIsTick(built(0.5f)),
                  "a turn %u ticks old does not: the tick's pose (long %u, snapped %u)", ago,
                  st->longWay, st->turnSnap);
        }
    }

    /* (d) a frame discarded between: the pair across it never blends
     * (RD_SNAP_GAP), the pair after continues the turn two ticks old */
    a = turnRamp(mesh, skin, "discarded");
    rd_begin_frame();
    rd_discard_frame();
    a += 200.0;
    recordTurn(mesh, skin, a + 200.0);
    a += 200.0;
    st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_GAP, "the pair across the discarded frame snaps (snap %u)", st->snap);
    recordTurn(mesh, skin, a + 200.0);
    st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->longWay == 2 && st->turnSnap == 0 &&
              turnHalfAt(a + 100.0, &am, &ab),
          "200 degrees after a discarded frame: the long way (long %u, snapped %u, mesh %.4f, "
          "bone %.4f)",
          st->longWay, st->turnSnap, am, ab);

    /* (e) a pose the remembered turn does not carry to (the wheel back at
     * 0 between): no memory, the turn over the snap keeps the tick's */
    a = turnRamp(mesh, skin, "pose");
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 190.0);
    st = build(0.5f, 1);
    CHECK(st->longWay == 0 && st->turnSnap == 2 && turnIsTick(built(0.5f)),
          "a turn whose pose the memory does not carry to: the tick's (long %u, snapped %u)",
          st->longWay, st->turnSnap);
    /* and a turn under the snap after a gap blends the short way as
     * without a memory */
    a = turnRamp(mesh, skin, "short");
    recordTurn(mesh, skin, a + 190.0);
    a += 190.0;
    recordTurn(mesh, skin, a + 100.0);
    st = build(0.5f, 1);
    CHECK(st->longWay == 0 && st->turnSnap == 0 && turnHalfAt(a + 50.0, &am, &ab),
          "100 degrees after a gap: the short way (long %u, mesh %.4f, bone %.4f)", st->longWay, am,
          ab);
}

/* The trolley's two wheels (issue 98): rigid draws of one object, the rear
 * a quarter turn on from the front (box.c dispWheels), at x 10 and x 60.
 * same: both of one key (two draws of a key, paired by their place in the
 * world), else parts 0 and 1 as the game keys them.  extra: still keyed
 * draws of another object drawn before them, so the wheels' places in the
 * list change from tick to tick. */
static void wheelDraw(RdMesh mesh, double deg, double tx, RdKey key)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 34;
    double s[16], w[16];
    memset(s, 0, sizeof(s));
    s[0] = 2.0;
    s[5] = 2.0;
    s[10] = 1.0;
    s[12] = 2048.0;
    s[13] = 2048.0;
    s[15] = 1.0;
    turnZ(w, deg, 1.0, tx);
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            double v = 0.0;
            for (int k = 0; k < 4; k++) {
                v += s[k * 4 + r] * w[cI * 4 + k];
            }
            d.vu.mem[4 + cI][r] = (float)s[cI * 4 + r];
            d.vu.mem[16 + cI][r] = (float)v;
            d.vu.mem[20 + cI][r] = (float)v;
            d.vu.mem[24 + cI][r] = (float)w[cI * 4 + r];
        }
    }
    identity(d.vu.mem, 12);
    rd_draw_vu_mesh(mesh, &d, key);
}

static RdKey wheelKey(int same, int w)
{
    return RD_KEY(&kObjE, same ? 0 : w, 34);
}

static void recordWheels(RdMesh mesh, double deg, int same, int extra, int cut)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(0);
    for (int k = 0; k < extra; k++) {
        wheelDraw(mesh, 0.0, 200.0 + 30.0 * k, RD_KEY(&kObjA, k, 34));
    }
    wheelDraw(mesh, deg, 10.0, wheelKey(same, 0));
    wheelDraw(mesh, deg + 90.0, 60.0, wheelKey(same, 1));
    if (cut) {
        rd_camera_cut();
    }
    rd_end_frame(0);
}

/* both wheels of f stand at want and want + 90 degrees (mod 360); their
 * angles in a[2] */
static bool wheelsAt(const RdFrame *f, int same, double want, float a[2])
{
    bool ok = true;
    for (int w = 0; w < 2; w++) {
        const float (*m)[4] = vuBlock(f, findKey(f, 0, wheelKey(same, w), same ? w : 0));
        a[w] = m ? atan2f(m[24][1], m[24][0]) * 57.2957795f : 1000.0f;
        ok = ok && m && fabs(remainder((double)a[w] - (want + 90.0 * w), 360.0)) < 1e-3;
    }
    return ok;
}

static void testRotationWheels(void)
{
    RdMesh mesh = makeMesh();
    float a2[2];
    /* (a) 185 degrees a tick after a ramp, both wheels forward the long way,
     * their places in the list changing every tick; of one key and of two */
    for (int same = 0; same < 2; same++) {
        static const double steps[6] = {110.0, 160.0, 185.0, 185.0, 185.0, 185.0};
        double a = 0.0;
        recordWheels(mesh, a, same, 0, 0);
        for (int k = 0; k < 6; k++) {
            recordWheels(mesh, a + steps[k], same, (k & 1) ? 3 : 0, 0);
            const RdInterpStats *st = build(0.5f, 1);
            const int longWant = steps[k] > 180.0 ? 2 : 0;
            CHECK(st->turnSnap == 0 && (int)st->longWay == longWant &&
                      wheelsAt(built(0.5f), same, a + steps[k] * 0.5, a2),
                  "wheels (%s): %.0f degrees a tick blends forward (snapped %u, long %u, want %d; "
                  "front %.4f, rear %.4f, want %.2f and %.2f)",
                  same ? "one key" : "two keys", steps[k], st->turnSnap, st->longWay, longWant,
                  a2[0], a2[1], fmod(a + steps[k] * 0.5, 360.0),
                  fmod(a + steps[k] * 0.5 + 90.0, 360.0));
            a += steps[k];
        }
    }
    /* (b) the ride of issue 98: box.c's push (0.5 a tick, friction 0.98:
     * v = (v + 0.5) 0.98) on wheels of radius 10 (the angle goes down by v /
     * 10 radians a tick) up to 125 degrees a tick, every pair blended; then
     * a camera cut and the ride on: each pair after it blends forward (the
     * turn, under 180, the short way continuing the turn of two ticks
     * before), none keeps the tick's pose */
    {
        double v = 0.0, a = 0.0, turn = 0.0;
        int snaps = 0, ticks = 0;
        recordWheels(mesh, a, 0, 0, 0);
        while (turn < 125.0 && ticks < 400) {
            v = (v + 0.5) * 0.98;
            turn = v / 10.0 * 57.29577951308232;
            recordWheels(mesh, a - turn, 0, ticks & 1, 0);
            const RdInterpStats *st = build(0.5f, 1);
            snaps += (int)st->turnSnap;
            if (!wheelsAt(built(0.5f), 0, a - turn * 0.5, a2)) {
                snaps += 100;
            }
            a -= turn;
            ticks++;
        }
        CHECK(snaps == 0 && turn >= 125.0,
              "the ride up to %.2f degrees a tick in %d ticks: every pair blended forward (%d)",
              turn, ticks, snaps);
        v = (v + 0.5) * 0.98;
        turn = v / 10.0 * 57.29577951308232;
        recordWheels(mesh, a - turn, 0, 0, 1);
        a -= turn;
        const RdInterpStats *st = build(0.5f, 1);
        CHECK(st->snap == RD_SNAP_CUT, "the cut's pair snaps (snap %u)", st->snap);
        for (int k = 0; k < 5; k++) {
            v = (v + 0.5) * 0.98;
            turn = v / 10.0 * 57.29577951308232;
            recordWheels(mesh, a - turn, 0, k & 1, 0);
            st = build(0.5f, 1);
            CHECK(st->snap == RD_SNAP_NONE && st->rotated == 2 && st->turnSnap == 0 &&
                      st->longWay == 0 && wheelsAt(built(0.5f), 0, a - turn * 0.5, a2),
                  "after the cut, pair %d at %.2f degrees a tick: forward (snapped %u, long %u; "
                  "front %.4f, rear %.4f, want %.4f)",
                  k + 1, turn, st->turnSnap, st->longWay, a2[0], a2[1],
                  remainder(a - turn * 0.5, 360.0));
            a -= turn;
        }
        printf("rd_interp_test: the trolley's ride: %.2f degrees a tick after %d ticks; after a "
               "cut the wheels at alpha 0.5: front %.4f, rear %.4f degrees\n",
               turn, ticks, a2[0], a2[1]);
    }
}

