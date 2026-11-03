/* rd_interp_test.c's cases for rd_interp_mesh.c: what the interpolation
 * reads from a mesh's stream alone, kept while the stream's serial
 * (RdMeshRec.streamSerial) is the same.
 *   memo  rd__interp_streams_same: two meshes of the same bytes are the
 *         same shape, compared once and the answer kept for the next ask;
 *         a write of either (rd_update_vu_mesh) compares them again and
 *         answers no; one buffer is the same shape without a comparison; a
 *         record with no serial is compared at every ask.
 *         rd__interp_skin_pivots: a skinned mesh's bone pivot is its bound
 *         vertices' centroid, the same at the next present and after
 *         another mesh's, and moves with the stream when it is rewritten */

/* the rising draw's skinned stream (makeRisingSkinMesh) moved dx in x */
static void memoSkinQw(float (*qw)[4], float dx)
{
    static const float pos[3][3] = {
        {0.0f, -100.0f, 0.0f}, {10.0f, -100.0f, 0.0f}, {0.0f, -90.0f, 0.0f}};
    memset(qw, 0, 16 * 16);
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 5][0] = pos[k][0] + dx;
        qw[1 + k * 5][1] = pos[k][1];
        qw[1 + k * 5][2] = pos[k][2];
        qw[1 + k * 5][3] = 1.0f;
        qw[1 + k * 5 + 3][3] = k == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
        const uint32_t addr = 16u;                   /* bone 0, weight 1 */
        memcpy(&qw[1 + k * 5 + 2][0], &addr, 4);
        qw[1 + k * 5 + 2][1] = 1.0f;
        memcpy(&qw[1 + k * 5 + 2][2], &addr, 4);
    }
}

static RdMesh memoSkinMesh(float dx)
{
    static float qw[16][4];
    memoSkinQw(qw, dx);
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

/* bone 0's pivot is (10 / 3 + dx, -290 / 3, 0) of weight 3 */
static bool memoPivotAt(const double (*p)[4], float dx)
{
    return p && fabs(p[0][0] - (10.0 / 3.0 + dx)) < 1e-9 && fabs(p[0][1] + 290.0 / 3.0) < 1e-9 &&
           p[0][2] == 0.0 && p[0][3] == 3.0 && p[1][3] == 0.0;
}

static void testStreamMemos(void)
{
    /* the same shape */
    RdMesh a = morphMesh(0.0f), b = morphMesh(0.0f);
    const RdMeshRec *ra = rd__mesh_rec(a.id), *rb = rd__mesh_rec(b.id);
    CHECK(ra && rb && ra->streamSerial && rb->streamSerial && ra->streamSerial != rb->streamSerial,
          "memo: each created mesh has a serial of its own");
    if (ra && rb) {
        const size_t size = (size_t)ra->vertexCount * ra->qwPerVertex * 16;
        const float (*pa)[4] = (const float (*)[4])ra->stream;
        uint32_t c0, k0, c, k;
        rd__interp_streams_counts(&c0, &k0);
        CHECK(rd__interp_streams_same(a.id, ra, pa, b.id, rb, (const float (*)[4])rb->stream, size),
              "memo: two meshes of the same bytes are the same shape");
        rd__interp_streams_counts(&c, &k);
        CHECK(c == c0 + 1 && k == k0, "memo: compared once (%u, %u kept)", c - c0, k - k0);
        CHECK(rd__interp_streams_same(a.id, ra, pa, b.id, rb, (const float (*)[4])rb->stream, size),
              "memo: asked again, the same shape");
        rd__interp_streams_counts(&c, &k);
        CHECK(c == c0 + 1 && k == k0 + 1, "memo: the answer kept (%u compared, %u kept)", c - c0,
              k - k0);
        const uint64_t was = rb->streamSerial;
        morphUpdate(b, 50.0f);
        CHECK(rb->streamSerial != was, "memo: a rewrite gives the stream a new serial");
        CHECK(
            !rd__interp_streams_same(a.id, ra, pa, b.id, rb, (const float (*)[4])rb->stream, size),
            "memo: after the rewrite, not the same shape");
        CHECK(
            !rd__interp_streams_same(a.id, ra, pa, b.id, rb, (const float (*)[4])rb->stream, size),
            "memo: asked again, not the same shape");
        rd__interp_streams_counts(&c, &k);
        CHECK(c == c0 + 2 && k == k0 + 2, "memo: the rewrite compared again (%u compared, %u kept)",
              c - c0, k - k0);
        CHECK(rd__interp_streams_same(a.id, ra, pa, a.id, ra, pa, size),
              "memo: one buffer is the same shape");
        RdMeshRec fa = *ra, fb = *rb;
        fa.streamSerial = fb.streamSerial = 0; /* records with no serial */
        CHECK(!rd__interp_streams_same(a.id, &fa, pa, b.id, &fb, (const float (*)[4])rb->stream,
                                       size) &&
                  !rd__interp_streams_same(a.id, &fa, pa, b.id, &fb, (const float (*)[4])rb->stream,
                                           size),
              "memo: records with no serial: not the same shape");
        rd__interp_streams_counts(&c, &k);
        CHECK(c == c0 + 4 && k == k0 + 2,
              "memo: records with no serial compared at every ask "
              "(%u compared, %u kept)",
              c - c0, k - k0);
    }
    rd_destroy_vu_mesh(a);
    rd_destroy_vu_mesh(b);

    /* the pivots */
    RdMesh s1 = memoSkinMesh(0.0f), s2 = memoSkinMesh(50.0f);
    CHECK(s1.id && s2.id, "memo: the skinned meshes");
    rd__interp_pivots_present();
    CHECK(memoPivotAt(rd__interp_skin_pivots(s1.id), 0.0f),
          "memo: a bone's pivot is its vertices' centroid");
    rd__interp_pivots_present();
    CHECK(memoPivotAt(rd__interp_skin_pivots(s1.id), 0.0f), "memo: the same at the next present");
    CHECK(memoPivotAt(rd__interp_skin_pivots(s2.id), 50.0f), "memo: another mesh's its own");
    CHECK(memoPivotAt(rd__interp_skin_pivots(s1.id), 0.0f), "memo: the first's again after it");
    static float qw[16][4];
    memoSkinQw(qw, 20.0f);
    CHECK(rd_update_vu_mesh(s1, (const float (*)[4])qw), "memo: the skinned mesh rewritten");
    rd__interp_pivots_present();
    CHECK(memoPivotAt(rd__interp_skin_pivots(s1.id), 20.0f),
          "memo: the pivot moves with the rewritten stream");
    rd_destroy_vu_mesh(s1);
    rd_destroy_vu_mesh(s2);
}
