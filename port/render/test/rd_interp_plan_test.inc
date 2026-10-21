/* rd_interp_plan_test.inc: the tick's plan (rd_interp.c, "the plan"),
 * included by rd_interp_test.c after the seams.
 *
 *   plan      over the pair builders above (sprites, VU draws, turns and a
 *             skinned draw, the blended camera, the hall's lights, still and
 *             wading instances, a mesh of the tick before alone, the
 *             re-projected strips, the pool's grids, the unmatched prims,
 *             Shadow.c's prisms, the wheels, the seams matched and one part
 *             the tick's alone, a lit turn), the presents of a pair at alpha
 *             0.5 (the tick's first), then 0.25, 0.75, 1 and 0 (later ones)
 *             give the same frame (lists, payload, camera, the VU common
 *             block, the ticks' cameras, the text items) and the same stats
 *             with the plan as with it built again before every present
 *             (rd__interp_plan_reset, what ICO_RD_INTERP_PLAN=0 does); with
 *             the plan it is built at the first present only (none for a
 *             pair that snaps) and allocates nothing after. */

typedef struct PlanShot {
    FrameCopy fc;
    RdFrame head; /* the frame's fields beside its lists and payload */
    RdInterpStats st;
    int ok;
} PlanShot;

static const float kPlanT[5] = {0.5f, 0.25f, 0.75f, 1.0f, 0.0f};

static void planShot(PlanShot *s, int k)
{
    const RdFrame *f =
        rd__interp_frame(rd__prev_frame(), rd__last_frame(), kPlanT[k], k == 0, &s->st);
    s->ok = f != NULL;
    frameCopy(f, &s->fc);
    memset(&s->head, 0, sizeof(s->head));
    if (f) {
        memcpy(&s->head, f, sizeof(s->head));
    }
}

static bool planSame(const PlanShot *a, const PlanShot *b)
{
    const RdFrame *x = &a->head, *y = &b->head;
    if (a->ok != b->ok || a->fc.payloadSize != b->fc.payloadSize ||
        (a->fc.payloadSize && memcmp(a->fc.payload, b->fc.payload, a->fc.payloadSize) != 0) ||
        memcmp(&a->st, &b->st, sizeof(a->st)) != 0) {
        return false;
    }
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        if (a->fc.count[l] != b->fc.count[l] ||
            (a->fc.count[l] &&
             memcmp(a->fc.cmds[l], b->fc.cmds[l], (size_t)a->fc.count[l] * sizeof(RdCmd)) != 0)) {
            return false;
        }
    }
    return memcmp(&x->camera, &y->camera, sizeof(x->camera)) == 0 && x->hasCamera == y->hasCamera &&
           memcmp(&x->vu, &y->vu, sizeof(x->vu)) == 0 && x->hasVu == y->hasVu &&
           x->textItems == y->textItems && x->hasTickCams == y->hasTickCams &&
           memcmp(x->tickCams, y->tickCams, sizeof(x->tickCams)) == 0;
}

/* the pair rd__prev_frame, rd__last_frame through the five presents, the
 * plan built again at each, then kept */
static void planCheck(const char *what)
{
    static PlanShot off[5], on[5];
    /* a present first, so both runs start from the state a pair's present
     * leaves (the turns' memory, the scratch meshes) */
    rd__interp_plan_reset();
    (void)rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, NULL);
    for (int k = 0; k < 5; k++) {
        rd__interp_plan_reset();
        planShot(&off[k], k);
    }
    uint32_t b0, a0, b1, a1, b2, a2;
    rd__interp_plan_counts(&b0, &a0);
    rd__interp_plan_reset();
    for (int k = 0; k < 5; k++) {
        planShot(&on[k], k);
        if (k == 0) {
            rd__interp_plan_counts(&b1, &a1);
        }
    }
    rd__interp_plan_counts(&b2, &a2);
    int same = 0;
    for (int k = 0; k < 5; k++) {
        same += planSame(&off[k], &on[k]);
        frameCopyFree(&off[k].fc);
        frameCopyFree(&on[k].fc);
    }
    const uint32_t want = on[0].st.snap == RD_SNAP_NONE ? 1u : 0u;
    CHECK(same == 5 && b1 - b0 == want && b2 == b1 && a2 == a1,
          "plan (%s): %d of 5 presents the same with the plan (snap %u, keyed %u, blended %u); "
          "%u plans built at the first present (want %u), %u and %u allocations after",
          what, same, on[0].st.snap, on[0].st.keyed, on[0].st.lerped, b1 - b0, want, b2 - b1,
          a2 - a1);
}

static void testPlanIdentity(void)
{
    RdMesh mesh = makeMesh(), skin = makeSkinMesh();
    recordSprites(0);
    recordSprites(1);
    planCheck("sprites");
    recordVu(mesh, 0, 0, 0);
    recordVu(mesh, 1, 1, 1);
    planCheck("VU draws");
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 90.0);
    planCheck("turns");
    s6Frame(mesh, 0.0, 0);
    s6Frame(mesh, 28.0, 0);
    planCheck("the blended camera");
    s6Frame(mesh, 0.0, 0);
    s6Frame(mesh, 28.0, 1);
    planCheck("the blended camera, a part the tick's");
    recordHallLights(mesh, skin, 0);
    recordHallLights(mesh, skin, 1);
    planCheck("hall lights");
    {
        const float abc[3] = {0.0f, 40.0f, 80.0f}, bcd[3] = {40.0f, 80.0f, 120.0f};
        instanceFrame(mesh, abc, 3);
        instanceFrame(mesh, bcd, 3);
        planCheck("instances");
    }
    wadeRipples(mesh, 0, 7, 0);
    wadeRipples(mesh, 1, 6, 1);
    planCheck("wading");
    {
        double bOut[3];
        alonePointAt(2048.0 + 256.0 * (1.0 + RD_CULL_MARGIN) + 30.0, 300.0, bOut);
        aloneFrame(mesh, 0.0, bOut, 0);
        aloneFrame(mesh, 28.0, NULL, 0);
        planCheck("of the tick before alone");
    }
    rpFrame(mesh, 0, 0);
    rpFrame(mesh, 1, 0);
    planCheck("re-projected");
    poolFrame(0, 0);
    poolFrame(1, 0);
    planCheck("pool");
    recordUnmatched(0);
    recordUnmatched(1);
    planCheck("unmatched");
    {
        static const int xs[2] = {10, 60};
        static const int dPrev[2][2] = {{-30, -30}, {-30, -30}},
                         dCur[2][2] = {{-30, 10}, {-30, 10}};
        int incP, incC;
        prismFrame(xs, dPrev, 2, 0, &incP);
        prismFrame(xs, dCur, 2, 8, &incC);
        planCheck("prisms");
    }
    recordWheels(mesh, 0.0, 0, 0, 0);
    recordWheels(mesh, 110.0, 0, 3, 0);
    planCheck("wheels");
    recordLitTurn(mesh, 0.0);
    recordLitTurn(mesh, 90.0);
    planCheck("lit turn");
    {
        static float qa[10][4], qb[10][4];
        double x[3], wa[16], wb[16];
        int32_t want[2];
        const int gsZ = s6GsZ;
        s6GsZ = 1;
        seamPoint(x, want);
        seamWorld(0, wa);
        seamWorld(1, wb);
        RdMesh ma = seamMesh(wa, x, qa), mb = seamMesh(wb, x, qb);
        seamFrame(ma, mb, 0.0, 0);
        seamFrame(ma, mb, 28.0, 0);
        planCheck("seams");
        seamFrameKeys(ma, mb, 0.0, 0, 1);
        seamFrameKeys(ma, mb, 28.0, 0, 5);
        planCheck("seams, a part the tick's");
        s6GsZ = gsZ;
    }
}
