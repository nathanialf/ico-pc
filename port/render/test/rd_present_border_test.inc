/* rd_present_test.c's open border cases (issue 129): included by
 * rd_present_test.c after fullPixelRun, so they share its helpers; a file
 * of their own keeps it under the repository's file size cap.
 *
 * With Full pixel on and the frame at its exact width (2340 x 1080's 832
 * columns, 4:3's 512, rd__frame_cropped false) the reduction draws its two
 * outer GS columns a side again as columns 2 and W - 3
 * (rd_blur_border.c, rd__blur_border_strips).  SCENE red with a green
 * sprite over GS columns 1 .. W - 2 (column 2 reads a quarter of column 1
 * with the mirror on, column W - 3 a quarter of W - 2 with it off: the
 * PS2's bias): the present's outer columns are green, and byte for byte
 * the present of SCENE green over every column; without the strips
 * (rd__set_blur_border_repeat off) the very edge is not green, and nothing
 * else differs.
 * On a cropped frame (16:9) and with Full pixel off the strips change no
 * byte.  At 1x (the GS model) DISPLAY's columns 0 and 1 are column 2's
 * bytes and W - 2, W - 1 column W - 3's, over a SCENE of a colour a column;
 * at 2x (the hardware sprite) every DISPLAY texel of a strip is one value a
 * row. */

typedef struct EdgeRun {
    uint8_t *out, *disp;
    uint32_t dw, dh, fw;
    RhiRect box;
} EdgeRun;

static void edgeFree(EdgeRun *e)
{
    free(e->out);
    free(e->disp);
    memset(e, 0, sizeof(*e));
}

/* scene 0: red, green over GS columns 1 .. W - 2; 1: green over every
 * column; 2: a colour a column (hash).  full as exactRun's (2: the border
 * mask).  repeat: the strips drawn */
static bool edgeRun(float aspect, uint32_t outW, uint32_t outH, float scale, int mirror, int full,
                    int scene, int repeat, EdgeRun *e)
{
    memset(e, 0, sizeof(*e));
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.aspect = aspect;
    s.sceneScale = scale; /* 0: the window's box */
    s.outputWidth = outW;
    s.outputHeight = outH;
    s.mirror = (uint8_t)mirror;
    s.fullPixel = (uint8_t)(full == 1);
    s.borderMask = (uint8_t)(full == 2);
    const uint32_t FW = (uint32_t)ico_frame_width_for(aspect);
    rd__set_blur_border_repeat(repeat != 0);
    if (!rd_init(FW, 512, &s, NULL)) {
        rd__set_blur_border_repeat(true);
        return false;
    }
    static const uint8_t red[4] = {255, 0, 0, 0x80}, green[4] = {0, 255, 0, 0x80};
    const int32_t left = 2048 - (int32_t)FW / 2, top = 2048 - 256;
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), red, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), FW, 512, 1);
    opaque2D();
    rd_texture_off();
    if (scene == 2) {
        RdScreenVtx *v = (RdScreenVtx *)malloc(sizeof(RdScreenVtx) * 2u * FW);
        for (uint32_t c = 0; v && c < FW; c++) {
            const int32_t X = left + (int32_t)c;
            const uint32_t hh = hash((uint32_t)X * 2246822519u + 129u);
            const uint8_t col[4] = {(uint8_t)hh, (uint8_t)(hh >> 8), (uint8_t)(hh >> 16), 0x80};
            v[2 * c] = vtx(X * 16, top * 16, 0, col, 0.0f, 0.0f);
            v[2 * c + 1] = vtx((X + 1) * 16, (top + 512) * 16, 0, col, 0.0f, 0.0f);
        }
        if (v) {
            rd_screen_prims(RD_PRIM_SPRITES, v, 2u * FW, RD_SPACE_WORLD, 1, 0);
        }
        free(v);
    } else {
        const int32_t c0 = scene == 1 ? 0 : 1, c1 = scene == 1 ? (int32_t)FW : (int32_t)FW - 1;
        const RdScreenVtx v[2] = {vtx((left + c0) * 16, top * 16, 0, green, 0.0f, 0.0f),
                                  vtx((left + c1) * 16, (top + 512) * 16, 0, green, 0.0f, 0.0f)};
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    const size_t cap = (size_t)4096 * 2048 * 4;
    e->out = (uint8_t *)malloc((size_t)outW * outH * 4);
    e->disp = (uint8_t *)malloc(cap);
    e->fw = FW;
    uint32_t ow = 0, oh = 0;
    rd__present_box(outW, outH, aspect, &e->box);
    const bool ok = e->out && e->disp &&
                    rd__read_present(e->out, (size_t)outW * outH * 4, &ow, &oh) && ow == outW &&
                    oh == outH &&
                    rd__read_target(rd_target(RD_TARGET_DISPLAY), e->disp, cap, &e->dw, &e->dh);
    CHECK(rhi_vk_validation_error_count() == 0, "open border: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    rd__set_blur_border_repeat(true);
    if (!ok) {
        edgeFree(e);
    }
    return ok;
}

#define EDGE_GREEN(q) ((q)[0] <= 2 && (q)[1] >= 250 && (q)[2] <= 2)

/* the present's pixels of a and b that differ outside the outer columns
 * (margin output pixels a side of the box), and inside them */
static void edgeDiff(const EdgeRun *a, const EdgeRun *b, uint32_t outW, uint32_t outH,
                     uint32_t margin, uint32_t *outside, uint32_t *inside)
{
    *outside = *inside = 0;
    const int32_t x0 = a->box.x, x1 = a->box.x + (int32_t)a->box.w;
    for (uint32_t y = 0; y < outH; y++) {
        for (uint32_t x = 0; x < outW; x++) {
            const size_t o = ((size_t)y * outW + x) * 4;
            if (memcmp(&a->out[o], &b->out[o], 4) == 0) {
                continue;
            }
            const int32_t X = (int32_t)x;
            const int edge =
                (X >= x0 && X < x0 + (int32_t)margin) || (X < x1 && X >= x1 - (int32_t)margin);
            *(edge ? inside : outside) += 1;
        }
    }
}

static void checkOpenBorderEdge(const char *name, float aspect, uint32_t outW, uint32_t outH,
                                float scale, int mirror)
{
    IcoFrame fr;
    ico_frame_make(&fr, aspect, 448);
    const int cropped = fr.wExact < (float)fr.w - 0.01f;
    EdgeRun a, b, c;
    const uint64_t strips0 = rd__blur_border_strip_count();
    if (!edgeRun(aspect, outW, outH, scale, mirror, 1, 0, 1, &a)) {
        CHECK(0, "%s: readback", name);
        return;
    }
    const uint64_t strips = rd__blur_border_strip_count() - strips0;
    if (!edgeRun(aspect, outW, outH, scale, mirror, 1, 0, 0, &c)) {
        CHECK(0, "%s: readback without the strips", name);
        edgeFree(&a);
        return;
    }
    /* output pixels a GS pixel across, and the outer columns' reach */
    const double k = (double)a.box.w / (cropped ? (double)fr.wExact : (double)a.fw);
    const uint32_t margin = (uint32_t)ceil(2.0 * k) + 1u;
    uint32_t outside = 0, inside = 0;
    edgeDiff(&a, &c, outW, outH, margin, &outside, &inside);
    if (cropped) {
        CHECK(strips == 0 && outside == 0 && inside == 0,
              "%s: cropped: no strip drawn (%llu) and the present byte for byte without them (%u "
              "pixels differ)",
              name, (unsigned long long)strips, outside + inside);
    } else {
        CHECK(strips >= 2 && strips % 2 == 0, "%s: the two strips drawn (%llu)", name,
              (unsigned long long)strips);
        CHECK(outside == 0 && inside > 0,
              "%s: the strips change the outer columns alone (%u pixels inside, %u outside)", name,
              inside, outside);
        if (edgeRun(aspect, outW, outH, scale, mirror, 1, 1, 1, &b)) {
            CHECK(memcmp(a.out, b.out, (size_t)outW * outH * 4) == 0,
                  "%s: the present is SCENE green over every column's, byte for byte", name);
            edgeFree(&b);
        } else {
            CHECK(0, "%s: readback (green everywhere)", name);
        }
        /* the box's outer columns green, every row; without the strips the
           very edge is not */
        const uint32_t n = (uint32_t)floor(2.0 * k);
        uint32_t bad = 0, notGreen = 0;
        for (uint32_t y = 0; y < a.box.h; y++) {
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t xs[2] = {(uint32_t)a.box.x + i, (uint32_t)a.box.x + a.box.w - 1 - i};
                for (int side = 0; side < 2; side++) {
                    const size_t o = (((size_t)a.box.y + y) * outW + xs[side]) * 4;
                    bad += !EDGE_GREEN(&a.out[o]);
                    notGreen += i == 0 && !EDGE_GREEN(&c.out[o]);
                }
            }
        }
        CHECK(bad == 0, "%s: the present's %u outer columns a side are green (%u pixels not)", name,
              n, bad);
        CHECK(notGreen > 0, "%s: without the strips the edge is not green (%u pixels)", name,
              notGreen);
        printf("%s: the strips changed %u output pixels, %u columns a side green\n", name, inside,
               n);
    }
    edgeFree(&a);
    edgeFree(&c);
    /* Full pixel off (0, the GS's own border; 2, the border mask): nothing
       drawn again, the present byte for byte */
    for (int full = 0; full <= 2; full += 2) {
        EdgeRun d, e;
        if (edgeRun(aspect, outW, outH, scale, mirror, full, 0, 1, &d) &&
            edgeRun(aspect, outW, outH, scale, mirror, full, 0, 0, &e)) {
            CHECK(memcmp(d.out, e.out, (size_t)outW * outH * 4) == 0,
                  "%s: Full pixel off (%d): the present byte for byte with and without the strips",
                  name, full);
            edgeFree(&e);
        } else {
            CHECK(0, "%s: Full pixel off (%d) readback", name, full);
        }
        edgeFree(&d);
    }
    if (cropped) {
        return;
    }
    /* DISPLAY over a colour a column: at 1x (the GS model) columns 0, 1
       are column 2's bytes and W - 2, W - 1 column W - 3's; at a scale (the
       hardware sprite) each strip is one value a row */
    EdgeRun f;
    if (!edgeRun(aspect, outW, outH, scale, mirror, 1, 2, 1, &f)) {
        CHECK(0, "%s: DISPLAY readback", name);
        return;
    }
    const double sx = (double)f.dw / (double)f.fw;
    uint32_t bad = 0;
    for (uint32_t y = 0; y < f.dh; y++) {
        const uint8_t *row = &f.disp[(size_t)y * f.dw * 4];
        if (sx == 1.0) {
            const uint32_t W = f.dw;
            bad += memcmp(&row[0], &row[2 * 4], 4) != 0 ||
                   memcmp(&row[1 * 4], &row[2 * 4], 4) != 0 ||
                   memcmp(&row[(W - 2) * 4], &row[(W - 3) * 4], 4) != 0 ||
                   memcmp(&row[(W - 1) * 4], &row[(W - 3) * 4], 4) != 0;
            continue;
        }
        /* the texels whose centres lie well inside GS pixels 0 .. 1 and
           W - 2 .. W - 1 */
        const uint32_t nl = (uint32_t)ceil(2.0 * sx - 0.75);
        const uint32_t r0 = (uint32_t)ceil(sx * (double)(f.fw - 2) - 0.25);
        for (uint32_t j = 1; j < nl; j++) {
            bad += memcmp(&row[j * 4], &row[0], 4) != 0;
        }
        for (uint32_t j = r0 + 1; j < f.dw; j++) {
            bad += memcmp(&row[j * 4], &row[r0 * 4], 4) != 0;
        }
    }
    CHECK(bad == 0, "%s: DISPLAY's strips repeat their column (%gx; %u rows or texels differ)",
          name, sx, bad);
    edgeFree(&f);
}

static void checkOpenBorderEdges(void)
{
    const float phone = 2340.0f / 1080.0f;
    checkOpenBorderEdge("open edge 2340x1080 2x", phone, 1664, 768, 0.0f, 0);
    checkOpenBorderEdge("open edge 2340x1080 2x mirror", phone, 1664, 768, 0.0f, 1);
    checkOpenBorderEdge("open edge 2340x1080 1x", phone, 1664, 768, 1.0f, 0);
    checkOpenBorderEdge("open edge 2340x1080 1x mirror", phone, 1664, 768, 1.0f, 1);
    checkOpenBorderEdge("open edge 4:3 2x", 4.0f / 3.0f, 1024, 768, 0.0f, 0);
    checkOpenBorderEdge("open edge 4:3 1x", 4.0f / 3.0f, 1024, 768, 1.0f, 0);
    checkOpenBorderEdge("open edge 16:9 (cropped)", 16.0f / 9.0f, 1920, 1080, 0.0f, 0);
    checkOpenBorderEdge("open edge 16:9 1x (cropped)", 16.0f / 9.0f, 1920, 1080, 1.0f, 0);
}
