/* rd_blur_aura_box_test.inc: rd_blur_test.c's case (z), the aura's feedback
 * read of SCENE into FEED128 in two passes (port/render/rd_aura_box.c,
 * [dev] aura_box), included after (x), whose frames it draws (qInit, QH,
 * Q_Z_FACE, X_FRAMES).
 *
 * At three scales of the Enhanced preset (NTSC, 448 lines, W 512):
 *   2x            sceneScale 2: the box 8 x 8 SCENE texels a FEED128 texel;
 *   phone         the PowerVR phone's 2364 x 1080 scene (sceneWidth,
 *                 sceneHeight: 4.617 x 2.411 here, the box's edges at a
 *                 fraction of a texel that moves by a 32nd from column to
 *                 column, odd counts of taps on both axes);
 *   1.5x          sceneScale 1.5: 6 x 6, the edges on half texels;
 * the aura's three feedback modes (X_FRAMES frames each) over a picture
 * with hard edges and gradients and, every other frame, noise (makeImage),
 * with a nearer band, run twice from a fresh renderer: the two-pass form
 * forced off (rd__aura_box_force(0)) and on (1).  Every frame's FEED128 is
 * the same bytes both ways, and SCENE's and DISPLAY's hashes the same; the
 * run with it on drew reads in two passes (rd__aura_box_counts) and none
 * in one, the run with it off none in two; no validation error; every
 * pipeline created is in the reachable set (checkPipelines: the two keys
 * enumerated while it is on). */

#define Z_FRAMES (3 * X_FRAMES)

static uint8_t s_zFeed[Z_FRAMES][128 * 128 * 4];
static uint64_t s_zScene[Z_FRAMES], s_zDisplay[Z_FRAMES];

static uint64_t zHash(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

/* the target's bytes hashed (0 when it cannot be read) */
static uint64_t zTargetHash(RdTargetId id, uint8_t *buf, size_t size)
{
    uint32_t w = 0, h = 0;
    rhi_wait_frame();
    if (!rd__read_target(rd_target(id), buf, size, &w, &h)) {
        return 0;
    }
    return zHash(buf, (size_t)w * h * 4);
}

static void zInit(const char *what, float scale, uint32_t sw, uint32_t sh)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = gsFrame.aspect;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = scale;
    st.sceneWidth = sw;
    st.sceneHeight = sh;
    if (!rd_init((uint32_t)gsFrame.w, QH, &st, NULL)) {
        CHECK(0, "(z) rd_init %s", what);
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    makeImage(7, 0);
    s_imgTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");
}

/* one run of the frames; on: the two-pass form; returns the reads drawn in
 * two passes and (*one) in one */
static uint64_t zRun(const char *what, float scale, uint32_t sw, uint32_t sh, int on,
                     uint64_t *one, uint8_t *buf, size_t size)
{
    rd_shutdown();
    rd__aura_box_force(on);
    zInit(what, scale, sw, sh);
    uint64_t two0 = 0, one0 = 0;
    rd__aura_box_counts(&two0, &one0);
    clearAll();
    for (int n = 0; n < Z_FRAMES; n++) {
        const int mode = 1 + n / X_FRAMES;
        setStage(0, mode, (n & 1) ? 0x40 : 0x20);
        GlobalTimer = 0;
        makeImage((uint32_t)(n + 11), n & 1);
        rd_update_texture(s_imgTex, s_img);
        dl_SetDLPriority(0);
        putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 0, QH, 0, 1);
        putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 180, 260, Q_Z_FACE,
                 1);
        FullScreenEffectBefore();
        dl_SetDLPriority(8);
        putImage(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), W, QH, 100 + n * 20,
                 200 + n * 20, 0, 0);
        FullScreenEffectAfter();
        dl_Swap();
        rhi_wait_idle();
        uint32_t w = 0, h = 0;
        uint8_t *feed = on ? buf : s_zFeed[n];
        rhi_wait_frame();
        if (!rd__read_target(rd_target(RD_TARGET_FEED128), feed, sizeof(s_zFeed[n]), &w, &h) ||
            w != 128 || h != 128) {
            CHECK(0, "%s frame %d: read FEED128 (%ux%u)", what, n, w, h);
            continue;
        }
        const uint64_t hs = zTargetHash(RD_TARGET_SCENE, buf + 128 * 128 * 4, size - 128 * 128 * 4);
        const uint64_t hd =
            zTargetHash(RD_TARGET_DISPLAY, buf + 128 * 128 * 4, size - 128 * 128 * 4);
        if (!on) {
            s_zScene[n] = hs;
            s_zDisplay[n] = hd;
            continue;
        }
        int differ = 0, first = -1;
        for (int i = 0; i < 128 * 128 * 4; i++) {
            if (feed[i] != s_zFeed[n][i]) {
                first = first < 0 ? i : first;
                differ++;
            }
        }
        CHECK(differ == 0,
              "%s frame %d mode %d: FEED128 differs from the one-pass read in %d channels (the "
              "first at (%d,%d) ch %d: %u against %u)",
              what, n, mode, differ, first < 0 ? 0 : (first / 4) % 128,
              first < 0 ? 0 : (first / 4) / 128, first < 0 ? 0 : first & 3,
              first < 0 ? 0u : feed[first], first < 0 ? 0u : s_zFeed[n][first]);
        CHECK(hs != 0 && hs == s_zScene[n] && hd != 0 && hd == s_zDisplay[n],
              "%s frame %d mode %d: SCENE or DISPLAY differs from the one-pass run", what, n,
              mode);
    }
    uint64_t two1 = 0, one1 = 0;
    rd__aura_box_counts(&two1, &one1);
    *one = one1 - one0;
    CHECK(rhi_vk_validation_error_count() == 0, "%s: %u validation errors", what,
          rhi_vk_validation_error_count());
    checkPipelines(); /* the two keys enumerated while it is on */
    return two1 - two0;
}

static void checkAuraBox(void)
{
    static const struct {
        const char *what;
        float scale;
        uint32_t sw, sh;
    } kZ[3] = {{"(z) 2x", 2.0f, 0, 0}, {"(z) phone 2364x1080", 0.0f, 2364, 1080},
               {"(z) 1.5x", 1.5f, 0, 0}};

    const int screenHeight = ScreenHeight, status0 = systemStatus[0], status5 = systemStatus[5];
    ScreenHeight = QH;
    systemStatus[0] = 0;
    systemStatus[5] = 0; /* not paused: the read is drawn */
    rd_shutdown();
    cpuFree();
    const size_t size = (size_t)4096 * 4096 * 4;
    uint8_t *buf = malloc(size);
    CHECK(buf != NULL, "(z) buffer");
    for (int k = 0; buf && k < 3; k++) {
        uint64_t oneOff = 0, oneOn = 0;
        const uint64_t twoOff =
            zRun(kZ[k].what, kZ[k].scale, kZ[k].sw, kZ[k].sh, 0, &oneOff, buf, size);
        const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
        const float sx = sc ? sc->sx : 0.0f, sy = sc ? sc->sy : 0.0f;
        const uint64_t twoOn =
            zRun(kZ[k].what, kZ[k].scale, kZ[k].sw, kZ[k].sh, 1, &oneOn, buf, size);
        if (!rd__aura_box_on()) {
            printf("  %s: skipped (the two-pass form's shaders are not in this build)\n",
                   kZ[k].what);
            continue;
        }
        printf("  %s (SCENE %.4gx%.4g): %d frames, FEED128 the same bytes both ways; %llu "
               "reads in two passes (off: %llu), %llu in one while on\n",
               kZ[k].what, (double)sx, (double)sy, Z_FRAMES, (unsigned long long)twoOn,
               (unsigned long long)twoOff, (unsigned long long)oneOn);
        CHECK(sx > 1.0f && sy > 1.0f, "%s: SCENE above 1x (%gx%g)", kZ[k].what, (double)sx,
              (double)sy);
        CHECK(twoOn > 0 && oneOn == 0 && twoOff == 0,
              "%s: %llu reads in two passes with it on (want some, %llu in one), %llu with it "
              "off (want 0)",
              kZ[k].what, (unsigned long long)twoOn, (unsigned long long)oneOn,
              (unsigned long long)twoOff);
    }
    free(buf);
    rd__aura_box_force(-1);
    rd_shutdown();
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
    systemStatus[5] = status5;
}
