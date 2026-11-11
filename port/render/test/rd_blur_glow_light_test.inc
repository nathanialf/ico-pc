/* rd_blur_glow_light_test.inc: rd_blur_test.c's case glow_light, the glow
 * drawn only where it can add light (port/render/rd_glow_light.c;
 * docs/port/RENDER_POST.md, "The glow where it adds light"), included after
 * the glow fold's case, whose helpers it shares (gfInit, putImageAt, QH,
 * Q_Z_FACE, s_fanWork2).
 *
 * rd_blur_test runs every other case with the plan off
 * (rd__glow_light_force(0) in main: their frames are compared with the CPU
 * GS model in every channel, and the plan may change the glow's alpha).
 * Here each frame is drawn with the plan off against the CPU model (at 1x
 * and 2x, as (k) does), then the same recorded frame replayed with the plan
 * on, at three settings (1x Original, Enhanced 2x, the PowerVR phone's 2364 x
 * 1080 with the full height), and the plan's targets read back
 * (rd__read_target, which draws the plan's lazy clears first):
 *   dark, sky      post mode 1, the sun off, the scene's top band left at
 *                  the clear's Z (the background cleanUpFB marks): DARK, RGB
 *                  the chain's in every target, the alpha too but on WORK1,
 *                  WORK2, WORK3 and SCENE; at most one pass begun by the
 *                  chain (WORK1's zero draw), none when replayed again;
 *   dark, no sky   the frame covered at the face's Z: DARK, every byte the
 *                  chain's;
 *   sun, fans off  the sun on (the eye blur's ghosts, shrink and tint), its
 *                  fans drawn into WORK2 off the picture (s_fanWork2 = 2):
 *                  DARK, as the dark frame with the sky;
 *   fans on        the fans over the frame's centre (s_fanWork2 = 1): WHOLE
 *                  for the fans' reason, every byte the chain's;
 *   lamps          the shine drawn into WORK2 after the mask (case (e)'s), the
 *                  sun off: LAMPS, as the dark frame with the sky, one glow
 *                  pass fewer;
 *   grey           post mode 4 (the bloom's fill RGB 128): WHOLE for the grey
 *                  fill's reason (its blur passes also add RGBAQ's alpha, a
 *                  form the scan refuses: the fill is judged first);
 *   DATE reader    the dark frame with a DATE sprite into SCENE after the
 *                  paste: WHOLE, SCENE's alpha read;
 * each frame ending with a sprite that rewrites SCENE's alpha whole, as the
 * aura's paste does in play (else the plan refuses: SCENE's alpha not
 * rewritten).  At 1x the plan is off and every byte the chain's. */

static uint8_t *s_glBuf[2][RD_TARGET_COUNT];
static size_t s_glSize[2][RD_TARGET_COUNT];

static const int kGlTargets[] = {RD_TARGET_SCENE,   RD_TARGET_DISPLAY,  RD_TARGET_WORK0,
                                 RD_TARGET_WORK1,   RD_TARGET_WORK2,    RD_TARGET_WORK3,
                                 RD_TARGET_FEED128, RD_TARGET_WORK2_PAD};

static void glRead(int which)
{
    for (size_t k = 0; k < sizeof(kGlTargets) / sizeof(kGlTargets[0]); k++) {
        const int id = kGlTargets[k];
        const RdTargetRec *t = rd__target_rec(rd_target((RdTargetId)id).id);
        const size_t n = t && t->color.id ? (size_t)t->tw * t->th * 4u : 0u;
        uint8_t *p = s_glBuf[which][id];
        if (n > s_glSize[which][id] || !p) {
            free(p);
            p = n ? malloc(n) : NULL;
            s_glBuf[which][id] = p;
        }
        uint32_t w = 0, h = 0;
        rhi_wait_frame();
        s_glSize[which][id] =
            p && rd__read_target(rd_target((RdTargetId)id), p, n, &w, &h) && (size_t)w * h * 4u == n
                ? n
                : 0u;
    }
}

/* the bytes the plan (0) and the chain (1) leave apart: RGB anywhere, the
 * alpha outside WORK1, WORK2, WORK3 and SCENE (or anywhere without
 * alphaFree); the first three printed */
static void glDiffer(const char *what, int alphaFree, int *rgbBad, int *alphaBad)
{
    *rgbBad = *alphaBad = 0;
    for (size_t k = 0; k < sizeof(kGlTargets) / sizeof(kGlTargets[0]); k++) {
        const int id = kGlTargets[k];
        if (!s_glSize[0][id] && !s_glSize[1][id]) {
            continue;
        }
        if (s_glSize[0][id] != s_glSize[1][id]) {
            CHECK(0, "%s: %s read one way only", what, kTargetNames[id]);
            continue;
        }
        const int free = alphaFree && (id == RD_TARGET_WORK1 || id == RD_TARGET_WORK2 ||
                                       id == RD_TARGET_WORK3 || id == RD_TARGET_SCENE);
        int rgb = 0, a = 0;
        for (size_t i = 0; i < s_glSize[0][id]; i++) {
            if (s_glBuf[0][id][i] == s_glBuf[1][id][i]) {
                continue;
            }
            const int alpha = (i & 3u) == 3u;
            if (alpha && free) {
                continue;
            }
            if ((alpha ? a : rgb) < 3) {
                printf("  %s %s byte %zu: plan %u, chain %u\n", what, kTargetNames[id], i,
                       s_glBuf[0][id][i], s_glBuf[1][id][i]);
            }
            if (alpha) {
                a++;
            } else {
                rgb++;
            }
        }
        CHECK(rgb == 0, "%s: %s: %d RGB bytes differ with the plan", what, kTargetNames[id], rgb);
        CHECK(a == 0, "%s: %s: %d alpha bytes differ with the plan", what, kTargetNames[id], a);
        *rgbBad += rgb;
        *alphaBad += a;
    }
}

/* the frame's kinds */
enum { GL_DARK_SKY, GL_DARK, GL_SUN_OFF, GL_FANS_ON, GL_LAMPS, GL_GREY, GL_DATE, GL_KINDS };

static const char *const kGlKind[GL_KINDS] = {
    "dark, sky", "dark, no sky", "sun, fans off", "fans on", "lamps", "grey", "DATE reader"};

/* an untextured full-frame sprite into SCENE: alpha a, the RGB kept
 * (Cs * As + Cd with Cs 0), DATE (DATM 0) when date */
static void glSceneSprite(uint8_t a, int date)
{
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), (uint32_t)gsFrame.w,
                  (uint32_t)ScreenHeight, 0);
    rd_test_gs(date ? 0x34000 : 0x30000);
    rd_z_write(0);
    rd_blend(RD_BLEND_CS_AS_ADD_CD, 0, 1);
    rd_fba(0);
    rd_pabe(0);
    rd_texture_off();
    rd_gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)(0x8000 - gsFrame.w / 2 * 16);
    p.rect[1] = (float)(0x8000 - ScreenHeight / 2 * 16);
    p.rect[2] = (float)(0x8000 + gsFrame.w / 2 * 16);
    p.rect[3] = (float)(0x8000 + ScreenHeight / 2 * 16);
    p.rgba[3] = a;
    p.scalar[0] = p.scalar[1] = 1024.0f;
    p.scalar[2] = 1.0f;
    rd_post(RD_POST_FLARE, &p);
}

/* one frame of kind k: drawn with the plan off (against the CPU model with
 * model), replayed with it on; the plan wanted (RD_GLOW_LIGHT_*, why) */
static void glFrame(const char *cfg, int k, int model, int above1x)
{
    char what[128];
    snprintf(what, sizeof(what), "(glow_light) %s %s", cfg, kGlKind[k]);
    const int post = k == GL_GREY ? 4 : 1;
    setStage(post, 0, 64);
    makeImage((uint32_t)post, 0);
    rd_update_texture(s_imgTex, s_img);
    if (k == GL_SUN_OFF || k == GL_FANS_ON) {
        InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    } else {
        InitializeStaticBlur(); /* the sun off */
    }
    clearAll();
    if (k == GL_DARK) {
        dl_SetDLPriority(0);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), gsFrame.w, ScreenHeight,
                   0, ScreenHeight, Q_Z_FACE, 1);
    } else if (gsFrame.w == W && ScreenHeight == H) {
        putScene();
    } else {
        dl_SetDLPriority(0);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), gsFrame.w, ScreenHeight,
                   0, ScreenHeight, 0, 0);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), gsFrame.w, ScreenHeight,
                   ScreenHeight / 3, ScreenHeight, Q_Z_FACE, 1);
    }
    s_fanDraws = 0;
    s_fanWork2 = k == GL_SUN_OFF ? 2 : (k == GL_FANS_ON ? 1 : 0);
    FullScreenEffectBefore();
    s_fanWork2 = 0;
    if (k == GL_LAMPS) {
        dl_SetDLPriority(7);
        putImage(rd_target(RD_TARGET_WORK2), rd_target(RD_TARGET_SCENE), W, H, 200, 260, 0, 0);
    }
    FullScreenEffectAfter();
    dl_SetDLPriority(8);
    if (k == GL_DATE) {
        glSceneSprite(0, 1); /* reads SCENE's alpha's MSB */
    }
    glSceneSprite(0, 0); /* SCENE's alpha rewritten, as the aura's paste does */
    rd__glow_light_force(0);
    if (model) {
        (void)runFrame(what, failures < 10);
    } else {
        dl_Swap();
        rhi_wait_idle();
    }
    const uint32_t passesChain = g_rdPerf.glowPasses;
    glRead(1);
    const RdFrame *f = rd__last_frame();
    rd__glow_light_force(1);
    rhi_wait_idle();
    CHECK(rd__replay_frame(f, 0, false), "%s: replay", what);
    rhi_wait_idle();
    const uint32_t plan = g_rdPerf.glowLight, why = g_rdPerf.glowLightWhy;
    const uint32_t passes = g_rdPerf.glowPasses, chainPasses = g_rdPerf.glowLightPasses;
    glRead(0);
    uint32_t want = RD_GLOW_LIGHT_DARK, wantWhy = RD_GLOW_LIGHT_WHY_NONE;
    if (!above1x) {
        want = RD_GLOW_LIGHT_OFF;
    } else if (k == GL_FANS_ON) {
        want = RD_GLOW_LIGHT_WHOLE;
        wantWhy = RD_GLOW_LIGHT_WHY_FANS;
    } else if (k == GL_LAMPS) {
        want = RD_GLOW_LIGHT_LAMPS;
    } else if (k == GL_GREY) {
        want = RD_GLOW_LIGHT_WHOLE;
        wantWhy = RD_GLOW_LIGHT_WHY_GREY;
    } else if (k == GL_DATE) {
        want = RD_GLOW_LIGHT_WHOLE;
        wantWhy = RD_GLOW_LIGHT_WHY_SCENE;
    }
    if (k == GL_FANS_ON && s_fanDraws == 0) {
        want = above1x ? RD_GLOW_LIGHT_DARK : RD_GLOW_LIGHT_OFF; /* no sun on screen: no fans */
        wantWhy = RD_GLOW_LIGHT_WHY_NONE;
    }
    CHECK(plan == want && (want != RD_GLOW_LIGHT_WHOLE || why == wantWhy),
          "%s: plan %s (%s), want %s (%s)", what, rd_glow_light_text(plan),
          rd_glow_light_why_text(why), rd_glow_light_text(want), rd_glow_light_why_text(wantWhy));
    /* the alpha may differ where WORK2 kept the fill (the marked
       background) under DARK and LAMPS only */
    const int alphaFree =
        (plan == RD_GLOW_LIGHT_DARK || plan == RD_GLOW_LIGHT_LAMPS) && k != GL_DARK;
    int rgb = 0, a = 0;
    glDiffer(what, alphaFree, &rgb, &a);
    uint32_t again = 0;
    if (plan == RD_GLOW_LIGHT_DARK) {
        /* the passes the dark chain begins (RdPerfRecord.glowLightPasses):
         * WORK1's zero draw over the first blur pass's rect, its glow edge
         * strip and the sun's shrink, in one pass on WORK1, because the
         * replay before drew the chain whole and WORK1 holds its alpha;
         * WORK2, WORK2_PAD, WORK3 and WORK0 are lazy clears (no pass: the
         * reads above draw them).  The other glow passes of the frame are
         * cleanUpFB's two sprites and the test's SCENE sprite after the
         * paste, not the chain's: fewer than the chain's in total */
        CHECK(chainPasses <= 1u, "%s: %u passes in the dark chain (want at most WORK1's)", what,
              chainPasses);
        CHECK(passes < passesChain, "%s: %u glow passes under DARK, the chain %u", what, passes,
              passesChain);
        /* again with the plan: WORK1's region is zero already (no replay
           wrote WORK1 since), so the chain begins no pass; the sun's shrink
           (its rect not the first pass's) still one */
        rhi_wait_idle();
        CHECK(rd__replay_frame(f, 0, false), "%s: replay again", what);
        rhi_wait_idle();
        again = g_rdPerf.glowLightPasses;
        const uint32_t againPlan = g_rdPerf.glowLight;
        glRead(0);
        CHECK(againPlan == RD_GLOW_LIGHT_DARK, "%s again: plan %s", what,
              rd_glow_light_text(againPlan));
        CHECK(again <= (k == GL_SUN_OFF ? 1u : 0u),
              "%s again: %u passes in the dark chain (want %s)", what, again,
              k == GL_SUN_OFF ? "at most the shrink's" : "none");
        int rgb2 = 0, a2 = 0;
        char what2[160];
        snprintf(what2, sizeof(what2), "%s again", what);
        glDiffer(what2, alphaFree, &rgb2, &a2);
        rgb += rgb2;
        a += a2;
    } else if (plan == RD_GLOW_LIGHT_LAMPS) {
        CHECK(passes + 1u <= passesChain, "%s: %u glow passes under LAMPS, the chain %u", what,
              passes, passesChain);
    } else {
        CHECK(passes <= passesChain, "%s: %u glow passes, the chain %u", what, passes, passesChain);
    }
    rd__glow_light_force(-1);
    printf("  %s: %s%s%s%s; glow %u -> %u passes (%u in the chain, %u replayed again); %d RGB "
           "and %d alpha bytes apart\n",
           what, rd_glow_light_text(plan), plan == RD_GLOW_LIGHT_WHOLE ? " (" : "",
           plan == RD_GLOW_LIGHT_WHOLE ? rd_glow_light_why_text(why) : "",
           plan == RD_GLOW_LIGHT_WHOLE ? ")" : "", passesChain, passes, chainPasses, again, rgb, a);
}

static void checkGlowLight(void)
{
    static const struct {
        const char *name;
        int preset;
        float scale;
        uint32_t sw, sh;
        int full, model, above1x;
    } kSet[3] = {
        {"1x", RD_PRESET_ORIGINAL, 0.0f, 0, 0, 0, 1, 0},
        {"2x", RD_PRESET_ENHANCED, 2.0f, 0, 0, 0, 1, 1},
        {"phone", RD_PRESET_ENHANCED, 0.0f, 2364, 1080, 1, 0, 1},
    };

    for (int s = 0; s < 3; s++) {
        if (!gfInit(kSet[s].name, kSet[s].preset, kSet[s].scale, kSet[s].sw, kSet[s].sh,
                    kSet[s].full, 0)) {
            continue;
        }
        s_scaledRun = g_rd.sceneSx > 1.0f || g_rd.sceneSy > 1.0f;
        for (int k = 0; k < GL_KINDS && failures <= 20; k++) {
            glFrame(kSet[s].name, k, kSet[s].model, kSet[s].above1x);
        }
        s_scaledRun = 0;
        InitializeStaticBlur(); /* sun off */
        CHECK(rhi_vk_validation_error_count() == 0, "(glow_light) %s: %u validation errors",
              kSet[s].name, rhi_vk_validation_error_count());
    }
    for (int a = 0; a < 2; a++) {
        for (int i = 0; i < RD_TARGET_COUNT; i++) {
            free(s_glBuf[a][i]);
            s_glBuf[a][i] = NULL;
            s_glSize[a][i] = 0;
        }
    }
    rd__glow_light_force(0);
    rd_shutdown();
    cpuFree();
}
