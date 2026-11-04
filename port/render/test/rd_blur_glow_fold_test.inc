/* rd_blur_glow_fold_test.inc: rd_blur_test.c's case (k), the glow's mask
 * folded into its reduction (port/render/rd_glow_fold.c, [dev] glow_fold;
 * docs/port/RENDER_POST.md, "The glow's mask in its reduction"), included
 * after (z), whose helpers it shares (putImageAt, cpuFree, QH, Q_Z_FACE).
 *
 * The scan without a device is checked in (r) (checkGlowFoldPlan: the
 * chain planned on a recorded frame, kept with the shine drawn into WORK2
 * between the mask and the reduction).  On the device, post modes 1..7 as
 * (e) records them but without the shine (so the fold applies wherever the
 * glow is drawn), without and with the sun's fans drawn into WORK2 before
 * the mask (s_fanWork2: an untextured sprite each; then the mask alone is
 * folded and WORK2 read where it keeps a texel), each frame drawn with the fold forced on
 * (rd__glow_fold_force(1)) and the same recorded frame replayed with it
 * forced off, at four settings:
 *   1x      Original, PAL 512 x 512: the reduction the GS bilinear;
 *   2x      Enhanced 2x: the reduction the above-1x box, SCENE and WORK2
 *           4 x 4 texels a GS pixel;
 *   phone   Enhanced at the PowerVR phone's 2364 x 1080 scene with the
 *           full height (4.617 x 2.109 texels a GS pixel: the mask's
 *           bilinear and the box at fractions of a texel);
 *   wide    the phone's 848-pixel frame (NTSC, 448 lines) at 2364 x 1080
 *           with the full height and Full pixel, aspect 2.189: WORK2 and
 *           SCENE the frame's exact width, their crop's left-out texels a
 *           fraction (the mask pass's vpo.z), the crop printed;
 * at 1x and 2x every target against the CPU model exactly both ways (with
 * the fold every target but WORK2, which it leaves undrawn: WORK0, the
 * reduction's, the mask's own bytes reduced); at every setting every
 * target but WORK2 the same bytes with the fold and without it, the fold
 * counted in every frame that draws the glow (RdPerfRecord.glowFolded) and
 * none kept, none with it off; then at 1x and 2x (e)'s own frame, the
 * shine drawn into WORK2 after the mask, kept for the lamps' reason.  Every pipeline
 * created enumerated; no validation error. */

static uint8_t *s_gfBuf[2][RD_TARGET_COUNT];
static size_t s_gfSize[2][RD_TARGET_COUNT];

/* every compared target but WORK2 into s_gfBuf[which] (size 0: not read) */
static void gfRead(int which)
{
    for (size_t k = 0; k < sizeof(kCompared) / sizeof(kCompared[0]); k++) {
        const int id = kCompared[k];
        if (id == RD_TARGET_WORK2) {
            continue;
        }
        const RdTargetRec *t = rd__target_rec(rd_target((RdTargetId)id).id);
        const size_t n = t && t->color.id ? (size_t)t->tw * t->th * 4u : 0u;
        uint8_t *p = s_gfBuf[which][id];
        if (n > s_gfSize[which][id] || !p) {
            free(p);
            p = n ? malloc(n) : NULL;
            s_gfBuf[which][id] = p;
        }
        uint32_t w = 0, h = 0;
        rhi_wait_frame();
        s_gfSize[which][id] =
            p && rd__read_target(rd_target((RdTargetId)id), p, n, &w, &h) && (size_t)w * h * 4u == n
                ? n
                : 0u;
    }
}

/* the bytes of every target read both ways that differ; the first three
 * printed */
static int gfDiffer(const char *what)
{
    int differ = 0;
    for (size_t k = 0; k < sizeof(kCompared) / sizeof(kCompared[0]); k++) {
        const int id = kCompared[k];
        if (id == RD_TARGET_WORK2 || !s_gfSize[0][id]) {
            continue;
        }
        if (s_gfSize[1][id] != s_gfSize[0][id]) {
            CHECK(0, "%s: %s read one way only", what, kTargetNames[id]);
            continue;
        }
        int bad = 0;
        for (size_t i = 0; i < s_gfSize[0][id]; i++) {
            if (s_gfBuf[0][id][i] != s_gfBuf[1][id][i]) {
                if (bad < 3) {
                    printf("  %s %s byte %zu: fold %u, drawn %u\n", what, kTargetNames[id], i,
                           s_gfBuf[0][id][i], s_gfBuf[1][id][i]);
                }
                bad++;
            }
        }
        CHECK(bad == 0, "%s: %s: %d bytes differ with the fold", what, kTargetNames[id], bad);
        differ += bad;
    }
    return differ;
}

static void gfFree(void)
{
    for (int a = 0; a < 2; a++) {
        for (int i = 0; i < RD_TARGET_COUNT; i++) {
            free(s_gfBuf[a][i]);
            s_gfBuf[a][i] = NULL;
            s_gfSize[a][i] = 0;
        }
    }
}

/* a renderer at the setting (preset, scene scale or size, full height,
 * Full pixel) for the current gsFrame, the model with it */
static bool gfInit(const char *what, int preset, float scale, uint32_t sw, uint32_t sh, int full,
                   int fullPixel)
{
    rd_shutdown();
    cpuFree();
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = (uint8_t)preset;
    st.aspect = gsFrame.aspect;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = scale;
    st.sceneWidth = sw;
    st.sceneHeight = sh;
    st.fullHeightScene = (uint8_t)full;
    st.fullPixel = (uint8_t)fullPixel;
    if (!rd_init((uint32_t)gsFrame.w, (uint32_t)ScreenHeight, &st, NULL)) {
        CHECK(0, "(k) rd_init %s", what);
        return false;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    makeImage(1, 0);
    s_imgTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");
    cpuInit();
    return true;
}

/* one frame of post mode post (the sun on) at the frame's width, its glow
 * with nothing drawn into WORK2 between the mask and the reduction: drawn
 * with the fold, then replayed without it; model: both against the CPU
 * model.  The folds counted into *folds, the bytes that differ into
 * *differ */
static void gfFrame(const char *cfg, int post, int model, int fans, uint32_t *folds, int *differ)
{
    char what[96], again[128];
    snprintf(what, sizeof(what), "(k) %s post mode %d%s", cfg, post, fans ? ", the fans" : "");
    snprintf(again, sizeof(again), "%s, the fold off", what);
    setStage(post, 0, 64);
    makeImage((uint32_t)post, 0);
    rd_update_texture(s_imgTex, s_img);
    clearAll(); /* in the frame: each replay of it starts from the same targets */
    if (gsFrame.w == W && ScreenHeight == H) {
        putScene();
    } else {
        dl_SetDLPriority(0);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), gsFrame.w, ScreenHeight,
                   0, ScreenHeight, 0, 1);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), gsFrame.w, ScreenHeight,
                   ScreenHeight / 3, ScreenHeight / 2, Q_Z_FACE, 1);
    }
    s_fanDraws = 0;
    s_fanWork2 = fans; /* the sun's fans drawn into WORK2 before the mask */
    FullScreenEffectBefore();
    s_fanWork2 = 0;
    FullScreenEffectAfter();
    rd__glow_fold_force(1);
    if (model) {
        (void)runFrame(what, failures < 10);
    } else {
        dl_Swap();
        rhi_wait_idle();
    }
    const uint32_t on = g_rdPerf.glowFolded, kept = g_rdPerf.glowKept, why = g_rdPerf.glowWhy;
    const uint32_t read = g_rdPerf.glowFoldRead;
    gfRead(0);
    const RdFrame *f = rd__last_frame();
    rd__glow_fold_force(0);
    rhi_wait_idle();
    CHECK(rd__replay_frame(f, 0, false), "%s: replay", again);
    rhi_wait_idle();
    const uint32_t off = g_rdPerf.glowFolded;
    if (model) {
        (void)compareAll(again, failures < 10); /* WORK2 too: drawn */
    }
    gfRead(1);
    const int d = gfDiffer(what);
    CHECK(kept == 0, "%s: the glow's mask drawn into WORK2 (%s)", what, rd_glow_fold_why_text(why));
    CHECK(off == 0, "%s: folded with the fold forced off", again);
    CHECK(!on || read == (uint32_t)(fans && s_fanDraws > 0), "%s: the fold %s WORK2 as drawn", what,
          read ? "read" : "did not read");
    printf("  %s: %s, %d bytes differ from the chain drawn\n", what,
           on ? "folded" : (kept ? "kept" : "no glow"), d);
    *folds += on;
    *differ += d;
    rd__glow_fold_force(-1);
}

/* the seven frames at the setting in force; model: against the CPU model
 * (1x, 2x; above 1x its own kernels, s_scaledRun) and (e)'s shine frame */
static void glowFoldSetting(const char *name, int model)
{
    s_scaledRun = g_rd.sceneSx > 1.0f || g_rd.sceneSy > 1.0f;
    const RdTargetRec *w2 = rd__target_rec(rd_target(RD_TARGET_WORK2).id);
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    uint32_t folds = 0;
    int differ = 0;
    for (int post = 1; post <= 7 && failures <= 20; post++) {
        gfFrame(name, post, model, 0, &folds, &differ);
        gfFrame(name, post, model, 1, &folds, &differ); /* the sun's fans into WORK2 */
    }
    if (model) {
        /* (e)'s frame (against the model): the shine drawn into WORK2
           after the mask */
        rd__glow_fold_force(1);
        (void)effectsFrame(1, 1);
        CHECK(g_rdPerf.glowFolded == 0 && g_rdPerf.glowKept == 1 &&
                  g_rdPerf.glowWhy == RD_GLOW_FOLD_WHY_LAMPS,
              "(k) %s: the shine into WORK2 between the mask and the reduction: %u folded, "
              "%u kept (%s)",
              name, g_rdPerf.glowFolded, g_rdPerf.glowKept,
              rd_glow_fold_why_text(g_rdPerf.glowWhy));
        rd__glow_fold_force(-1);
    }
    InitializeStaticBlur(); /* sun off */
    CHECK(folds > 0, "(k) %s: no frame folded the glow's mask", name);
    printf("  (k) the glow fold at %s (WORK2 %ux%u texels, crop %.3f texels): %u frames "
           "folded, %d bytes differ\n",
           name, w2 ? w2->tw : 0, w2 ? w2->th : 0, w2 ? (double)w2->ox : 0.0, folds, differ);
    checkPipelines(); /* the fold's key is the alpha alone's (rd__fx_enumerate) */
    CHECK(rhi_vk_validation_error_count() == 0, "(k) %s: %u validation errors", name,
          rhi_vk_validation_error_count());
    s_scaledRun = 0;
}

static void checkGlowFold(void)
{
    static const struct {
        const char *name;
        int preset;
        float scale;
        uint32_t sw, sh;
        int full, fullPixel, wide, model;
    } kSet[4] = {
        {"1x", RD_PRESET_ORIGINAL, 0.0f, 0, 0, 0, 0, 0, 1},
        {"2x", RD_PRESET_ENHANCED, 2.0f, 0, 0, 0, 0, 0, 1},
        {"phone", RD_PRESET_ENHANCED, 0.0f, 2364, 1080, 1, 0, 0, 0},
        {"wide", RD_PRESET_ENHANCED, 0.0f, 2364, 1080, 1, 1, 1, 0},
    };

    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    for (int s = 0; s < 4; s++) {
        if (kSet[s].wide) {
            ScreenHeight = QH;
            systemStatus[0] = 0; /* NTSC */
            setFrameWidth(848);
            gsFrame.aspect = 2364.0f / 1080.0f; /* the phone's: the exact width under 848 */
            gsFrame.wExact = 384.0f * gsFrame.aspect;
        }
        if (!gfInit(kSet[s].name, kSet[s].preset, kSet[s].scale, kSet[s].sw, kSet[s].sh,
                    kSet[s].full, kSet[s].fullPixel)) {
            continue;
        }
        if (!g_rd.profile.fxPlan || !g_rd.fs[RD_FS_FX_FRAME].id) {
            printf("  (k) the glow fold at %s: skipped (no fx plan or fx_frame_ps)\n",
                   kSet[s].name);
        } else {
            glowFoldSetting(kSet[s].name, kSet[s].model);
        }
        if (kSet[s].wide) {
            setFrameWidth(W);
            ScreenHeight = screenHeight;
            systemStatus[0] = status0;
        }
    }
    gfFree();
    rd_shutdown();
    cpuFree();
}

/* (r) the scan without a device (rd__glow_fold_plan) on the frame just
 * recorded (post mode 7 with the sun; its fans draw nothing here): the
 * fill, the mask and the reduction found in that order, the fill's colour
 * (0, 0, 0, 192, or grey 128 for the bloom), the mask's test (ATST LESS,
 * AREF 129, AFAIL KEEP) and RGBAQ's alpha 128; the flare with the sun's
 * fans drawn into WORK2 before the mask (s_fanWork2): the mask alone
 * planned (maskOnly); then the flare recorded with the shine drawn into
 * WORK2 after the mask: kept, the lamps' reason */
static void checkGlowFoldPlan(const RdFrame *f)
{
    RdGlowFoldPlan pl;
    uint32_t why = 0;
    const char *v = rd__glow_fold_plan(f, 0, &pl, &why);
    CHECK(v == NULL && pl.on && !pl.maskOnly && pl.fillOrd < pl.maskOrd &&
              pl.maskOrd < pl.reductionOrd,
          "(r) the glow fold: post mode 7 not planned: %s", v ? v : "planned out of order");
    const uint32_t rgb = pl.fill & 0xFFFFFFu;
    CHECK(v != NULL || ((pl.fill >> 24) == 192u && (rgb == 0u || rgb == 0x808080u)),
          "(r) the glow fold: the fill %08X, not (0, 0, 0, 192) or grey 128", pl.fill);
    CHECK(v != NULL || ((pl.test & 255u) == 128u && ((pl.test >> 8) & 255u) == 129u &&
                        ((pl.test >> 16) & 15u) == RD_ATST_LESS && ((pl.test >> 20) & 1u) &&
                        ((pl.test >> 24) & 3u) == RD_AFAIL_KEEP),
          "(r) the glow fold: the mask's test %08X, not A 128, ATST LESS, AREF 129, AFAIL KEEP",
          pl.test);
    printf("  (r) the glow fold: post mode 7 planned (fill %u, mask %u, reduction %u; fill "
           "%08X, test %08X, flags %X)\n",
           pl.fillOrd, pl.maskOrd, pl.reductionOrd, pl.fill, pl.test, pl.flags);
    /* the sun's fans drawn into WORK2 between the fill and the mask: the
       mask alone folded, WORK2 read where it keeps a texel */
    setStage(1, 0, 64);
    s_fanDraws = 0;
    s_fanWork2 = 1;
    FullScreenEffectBefore();
    s_fanWork2 = 0;
    FullScreenEffectAfter();
    dl_Swap();
    v = rd__glow_fold_plan(rd__last_frame(), 0, &pl, &why);
    CHECK(s_fanDraws == 0 || (v == NULL && pl.maskOnly == 1),
          "(r) the glow fold: the sun's fans into WORK2 before the mask: %s (mask only %d)",
          v ? (v[0] ? v : "no glow") : "planned", pl.maskOnly);
    printf("  (r) the glow fold: with %d fans drawn into WORK2, %s\n", s_fanDraws,
           v ? "kept" : (pl.maskOnly ? "the mask alone folded" : "fill and mask folded"));
    setStage(1, 0, 64);
    FullScreenEffectBefore();
    dl_SetDLPriority(7);
    putImage(rd_target(RD_TARGET_WORK2), rd_target(RD_TARGET_SCENE), W, H, 200, 260, 0, 0);
    FullScreenEffectAfter();
    dl_Swap();
    v = rd__glow_fold_plan(rd__last_frame(), 0, &pl, &why);
    CHECK(v != NULL && v[0] && why == RD_GLOW_FOLD_WHY_LAMPS,
          "(r) the glow fold: the shine into WORK2 after the mask: %s (%s)",
          v ? (v[0] ? v : "no glow") : "planned", rd_glow_fold_why_text(why));
}
