/* rd_blur_smooth_test.inc: rd_blur_test.c's case (s), Options > Effects >
 * Motion blur: Smooth (rd_blur_smooth.c, RdSettings.motionBlurSmooth),
 * included after (n), whose N_DW, N_DH it uses.
 *
 * The motion blur at FIX 32 with no other effect and the reduction's tint
 * 0x80 (the loop's P an identity inside the picture), each tick presented
 * N times as the presenter does (rd__blur_smooth_arm, rd__interp_frame at
 * alpha (k + 1) / N, the first present the tick's first), DISPLAY read back
 * after the presents:
 *   a  a still picture of flat 64 x 64 blocks, 8 ticks at N = 1, 4 and 10:
 *      Smooth's last DISPLAY within 1 of Original's inside the blocks (both
 *      settle on the picture: Original a level under it, its floor);
 *   b  a white bar 32 wide on black, still for 6 ticks, then 8 pixels to
 *      the right, at N = 4: Original's four presents of that tick draw one
 *      DISPLAY (DISPLAY_HELD), Smooth's four each a new one (the trail
 *      fading present by present), and after the tick the ghost the bar
 *      left (the 8 columns it uncovered) within 2 of Original's: FIX' 91
 *      four times keeps the share FIX 32 keeps once (65 against 63);
 *   c  the bar for 4 ticks, a tick without the motion blur (the game's
 *      alpha 0) with the bar elsewhere, then the blur again with the bar
 *      moved on, at N = 1: the tick after the gap is a restart (counted),
 *      its picture holds no ghost of either place the bar was (black there,
 *      byte for byte), where Original's holds the gap tick's;
 * then case (s2) on its own device: Enhanced 2x with the full-height scene,
 * the accumulator and its RGBA8 target at SCENE's texture size.  The
 * accumulator's pipelines are its own (rd_blur_smooth.c), so every shard's
 * checkPipelines finds the created set inside the reachable one as before
 * (d). */

#define S_SIZE ((size_t)N_DW * N_DH * 4)

/* flat 64 x 64 blocks, alpha 0x80 */
static void sBlocks(void)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = &s_img[(y * W + x) * 4];
            const uint32_t h = hash32((uint32_t)((y >> 6) * 64 + (x >> 6)) + 77u);
            p[0] = (uint8_t)(40 + (h & 0xFF) * 3 / 4);
            p[1] = (uint8_t)(40 + ((h >> 8) & 0xFF) * 3 / 4);
            p[2] = (uint8_t)(40 + ((h >> 16) & 0xFF) * 3 / 4);
            p[3] = 0x80;
        }
    }
    rd_update_texture(s_imgTex, s_img);
}

/* black, a white bar over columns x0 .. x0 + 31, alpha 0x80 */
static void sBar(int x0)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = &s_img[(y * W + x) * 4];
            const uint8_t v = x >= x0 && x < x0 + 32 ? 255 : 0;
            p[0] = p[1] = p[2] = v;
            p[3] = 0x80;
        }
    }
    rd_update_texture(s_imgTex, s_img);
}

/* one tick: the picture, the motion blur at fix (none at 0, the game's
 * alpha 0), the reduction untinted; clear: SCENE, DISPLAY and DISPLAY_HELD
 * to black first (the run's first tick) */
static void sRecord(int fix, int clear)
{
    setStage(0, 0, 0);
    GlobalTimer = 0;
    currentScreenWidth = 0;
    if (clear) {
        static const uint8_t zero[4] = {0, 0, 0, 0};
        dl_SetDLPriority(0);
        rd_clear_target(rd_target(RD_TARGET_SCENE), zero, 1, 0);
        rd_clear_target(rd_target(RD_TARGET_DISPLAY), zero, 0, 0);
        rd_clear_target(rd_target(RD_TARGET_DISPLAY_HELD), zero, 0, 0);
    }
    putScene();
    /* the flare's filter and wrap as the motion blur inherits them (m) */
    dl_SetDLPriority(7);
    rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    SetMotionBlur(fix);
    MotionBlur();
    putReductionTint(0x80, 0x80, 0x80);
}

static bool sReadDisplay(uint8_t *out)
{
    uint32_t w = 0, h = 0;
    return rd__read_target(rd_target(RD_TARGET_DISPLAY), out, S_SIZE, &w, &h) && w == N_DW &&
           h == N_DH;
}

/* the tick just recorded closed and presented n times; DISPLAY after
 * present k into each[k] (each NULL: none), after the last into last (NULL:
 * none); false on a failure */
static bool sTick(int n, uint8_t *each, uint8_t *last)
{
    dl_Swap(); /* closed, not replayed: interpolate is on */
    for (int k = 0; k < n; k++) {
        const RdFrame *f = rd__interp_frame(rd__prev_frame(), rd__last_frame(),
                                            (float)(k + 1) / (float)n, k == 0, NULL);
        rd__blur_smooth_arm(k == 0, (uint32_t)n);
        if (!f || !rd__replay_frame(f, 0, false)) {
            return false;
        }
        rhi_wait_idle();
        if (each && !sReadDisplay(each + (size_t)k * S_SIZE)) {
            return false;
        }
    }
    return !last || sReadDisplay(last);
}

/* the mean of channel 0 over DISPLAY columns x0 .. x1 - 1, rows 12 .. N_DH - 13 */
static double sMean(const uint8_t *d, int x0, int x1, int *maxV)
{
    double sum = 0.0;
    int n = 0, m = 0;
    for (int y = 12; y < N_DH - 12; y++) {
        for (int x = x0; x < x1; x++) {
            const int v = d[((size_t)y * N_DW + (size_t)x) * 4];
            sum += v;
            m = v > m ? v : m;
            n++;
        }
    }
    if (maxV) {
        *maxV = m;
    }
    return n ? sum / n : 0.0;
}

/* Smooth on or off for a run of its own: the accumulator freed, the trail
 * starting again */
static void sMode(int smooth)
{
    rd__blur_smooth_free("rd_blur_test: a new run");
    g_rd.settings.motionBlurSmooth = (uint8_t)smooth;
}

static void checkMotionBlurSmooth(void)
{
    const uint8_t interpolate = g_rd.settings.interpolate;
    g_rd.settings.interpolate = 1; /* rd_end_frame leaves the replays to the presents */
    uint8_t *ref = malloc(S_SIZE), *got = malloc(S_SIZE);
    uint8_t *orig4 = malloc(4 * S_SIZE), *smooth4 = malloc(4 * S_SIZE);
    if (!ref || !got || !orig4 || !smooth4) {
        CHECK(0, "(s) memory");
        free(ref);
        free(got);
        free(orig4);
        free(smooth4);
        g_rd.settings.interpolate = interpolate;
        return;
    }
    uint64_t drawn0 = 0, drawn1 = 0, restarts0 = 0, restarts1 = 0;

    /* (a) a still picture */
    static const int kN[3] = {1, 4, 10};
    for (int i = 0; i < 3; i++) {
        const int n = kN[i];
        int ok = 1;
        for (int smooth = 0; smooth <= 1; smooth++) {
            sMode(smooth);
            sBlocks();
            rd__blur_smooth_counts(&drawn0, NULL, NULL);
            for (int t = 0; t < 8 && ok; t++) {
                sRecord(32, t == 0);
                ok = sTick(n, NULL, smooth ? got : ref);
            }
            rd__blur_smooth_counts(&drawn1, NULL, NULL);
            CHECK(drawn1 - drawn0 == (uint64_t)(smooth ? 8 * n : 0),
                  "(s a) N %d %s: %llu sprites through the accumulator", n,
                  smooth ? "Smooth" : "Original", (unsigned long long)(drawn1 - drawn0));
        }
        CHECK(ok, "(s a) N %d: the presents and the readbacks", n);
        int worst = 0, over = 0, inside = 0;
        for (int y = 12; ok && y < N_DH - 12; y++) {
            for (int x = 12; x < N_DW - 12; x++) {
                const int bx = x & 63, by = (2 * y) & 63;
                if (bx < 4 || bx > 59 || by < 4 || by > 59) {
                    continue; /* a block's edge: the filters mix two blocks there */
                }
                inside++;
                for (int c = 0; c < 3; c++) {
                    const size_t o = ((size_t)y * N_DW + (size_t)x) * 4 + (size_t)c;
                    const int d = abs((int)got[o] - (int)ref[o]);
                    worst = d > worst ? d : worst;
                    over += d > 1;
                }
            }
        }
        CHECK(ok && inside > N_DW * N_DH / 2 && over == 0,
              "(s a) N %d: a still picture under Smooth %d channels more than 1 from Original's "
              "(the most %d, %d pixels compared)",
              n, over, worst, inside);
        printf("  (s a) still picture, %2d presents a tick: Smooth against Original at most %d "
               "apart (%d pixels inside the blocks)\n",
               n, worst, inside);
    }

    /* (b) the bar moved 8 pixels at a tick of 4 presents */
    double ghost[2] = {0.0, 0.0};
    int sameO = 0, differS = 0, okB = 1;
    for (int smooth = 0; smooth <= 1; smooth++) {
        sMode(smooth);
        for (int t = 0; t < 7 && okB; t++) {
            sBar(t < 6 ? 200 : 208);
            sRecord(32, t == 0);
            okB = sTick(4, t == 6 ? (smooth ? smooth4 : orig4) : NULL, NULL);
        }
        const uint8_t *last = (smooth ? smooth4 : orig4) + 3 * S_SIZE;
        ghost[smooth] = sMean(last, 202, 206, NULL);
    }
    CHECK(okB, "(s b) the presents and the readbacks");
    for (int k = 1; okB && k < 4; k++) {
        sameO += memcmp(orig4, orig4 + (size_t)k * S_SIZE, S_SIZE) == 0;
        differS +=
            memcmp(smooth4 + (size_t)(k - 1) * S_SIZE, smooth4 + (size_t)k * S_SIZE, S_SIZE) != 0;
    }
    CHECK(okB && sameO == 3, "(s b) Original: the tick's four presents draw one DISPLAY (%d of 3)",
          sameO);
    CHECK(okB && differS == 3,
          "(s b) Smooth: each of the tick's presents moves the trail on (%d of "
          "3 differ from the one before)",
          differS);
    CHECK(okB && ghost[0] > 40.0 && fabs(ghost[1] - ghost[0]) <= 2.0,
          "(s b) the ghost after the tick: Smooth %.2f, Original %.2f (within 2)", ghost[1],
          ghost[0]);
    {
        const double g0 = okB ? sMean(smooth4, 202, 206, NULL) : 0.0;
        printf("  (s b) the bar 8 pixels on at 4 presents a tick: Original's 4 presents one "
               "DISPLAY (%d of 3 equal), Smooth's ghost %.1f after the first present, %.1f after "
               "the tick (Original %.1f)\n",
               sameO, g0, ghost[1], ghost[0]);
    }

    /* (c) a tick without the motion blur, then the blur again */
    double gapGhost[2] = {0.0, 0.0}, oldGhost[2] = {0.0, 0.0}, bar[2] = {0.0, 0.0};
    int gapMax[2] = {0, 0}, oldMax[2] = {0, 0}, okC = 1;
    for (int smooth = 0; smooth <= 1; smooth++) {
        sMode(smooth);
        for (int t = 0; t < 6 && okC; t++) {
            sBar(t < 4 ? 200 : t == 4 ? 260 : 320);
            sRecord(t == 4 ? 0 : 32, t == 0);
            if (t == 5) {
                rd__blur_smooth_counts(NULL, &restarts0, NULL);
            }
            okC = sTick(1, NULL, t == 5 ? got : NULL);
            if (t == 5) {
                rd__blur_smooth_counts(NULL, &restarts1, NULL);
            }
        }
        gapGhost[smooth] = sMean(got, 262, 290, &gapMax[smooth]);
        oldGhost[smooth] = sMean(got, 202, 230, &oldMax[smooth]);
        bar[smooth] = sMean(got, 324, 348, NULL);
        if (smooth) {
            CHECK(restarts1 - restarts0 == 1, "(s c) the sprite after the gap restarts (%llu)",
                  (unsigned long long)(restarts1 - restarts0));
        }
    }
    CHECK(okC, "(s c) the presents and the readbacks");
    CHECK(okC && gapMax[1] == 0 && oldMax[1] == 0 && bar[1] >= 250.0,
          "(s c) Smooth after the gap: no ghost (the gap tick's place at most %d, the place "
          "before at most %d), the bar %.1f",
          gapMax[1], oldMax[1], bar[1]);
    CHECK(okC && gapGhost[0] > 40.0,
          "(s c) Original after the gap: the gap tick's picture as its ghost (%.1f)", gapGhost[0]);
    printf("  (s c) after a tick without the motion blur: Smooth restarts (ghost %.1f, %.1f; bar "
           "%.1f), Original keeps the gap tick's picture (ghost %.1f)\n",
           gapGhost[1], oldGhost[1], bar[1], gapGhost[0]);

    sMode(0);
    rd__blur_smooth_free("rd_blur_test: the case's end");
    g_rd.settings.interpolate = interpolate;
    SetMotionBlur(0);
    makeImage(1, 0); /* the picture the cases after this one start from */
    rd_update_texture(s_imgTex, s_img);
    free(ref);
    free(got);
    free(orig4);
    free(smooth4);
}

/* (s2) Enhanced 2x with the full-height scene: the pair and its target at
 * SCENE's texture size (DISPLAY is twice its GS height there, SCENE is not
 * doubled: the accumulator follows SCENE) */
static void checkMotionBlurSmoothScaled(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 4.0f / 3.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = 2.0f;
    st.fullHeightScene = 1;
    st.interpolate = 1;
    st.motionBlurSmooth = 1;
    if (!rd_init(W, H, &st, NULL)) {
        CHECK(0, "(s2) rd_init at 2x");
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    makeImage(5, 0);
    s_imgTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");
    int ok = 1;
    for (int t = 0; t < 3 && ok; t++) {
        nRecord(32);
        dl_Swap();
        for (int k = 0; k < 2 && ok; k++) {
            const RdFrame *f = rd__interp_frame(rd__prev_frame(), rd__last_frame(),
                                                (float)(k + 1) / 2.0f, k == 0, NULL);
            rd__blur_smooth_arm(k == 0, 2);
            ok = f && rd__replay_frame(f, 0, false);
        }
    }
    rhi_wait_idle();
    uint32_t acc[2], res[2];
    rd__blur_smooth_sizes(acc, res);
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const RdTargetRec *dp = rd__target_rec(rd_target(RD_TARGET_DISPLAY).id);
    uint64_t drawn = 0, restarts = 0;
    rd__blur_smooth_counts(&drawn, &restarts, NULL);
    CHECK(ok && sc && dp && sc->tw == 2 * W && sc->th == 2 * H && acc[0] == sc->tw &&
              acc[1] == sc->th && res[0] == sc->tw && res[1] == sc->th && dp->th == sc->th,
          "(s2) 2x full height: SCENE %u x %u, the accumulator %u x %u, its target %u x %u, "
          "DISPLAY %u x %u",
          sc ? sc->tw : 0, sc ? sc->th : 0, acc[0], acc[1], res[0], res[1], dp ? dp->tw : 0,
          dp ? dp->th : 0);
    CHECK(drawn == 6 && restarts == 1, "(s2) %llu sprites through the accumulator, %llu restarts",
          (unsigned long long)drawn, (unsigned long long)restarts);
    printf("  (s2) Enhanced 2x, full height: SCENE %u x %u, the accumulator and its target %u x "
           "%u and %u x %u, DISPLAY %u x %u\n",
           sc ? sc->tw : 0, sc ? sc->th : 0, acc[0], acc[1], res[0], res[1], dp ? dp->tw : 0,
           dp ? dp->th : 0);
    CHECK(rhi_vk_validation_error_count() == 0, "(s2) %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    uint32_t after[2], afterRes[2];
    rd__blur_smooth_sizes(after, afterRes);
    CHECK(after[0] == 0 && afterRes[0] == 0, "(s2) rd_shutdown frees the accumulator (%u, %u)",
          after[0], afterRes[0]);
}
