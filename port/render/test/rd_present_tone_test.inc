/* rd_present_test.c's colour adjustment cases (Options > Picture: Gamma,
 * Picture brightness, Contrast, Saturation; rd_tone.h, tone.hlsli):
 * included by rd_present_test.c after checkEffectsDepth, so they share its
 * helpers (exactRun, depthRun, reachable); a file of their own keeps it
 * under the repository's file size cap.
 *
 *   the settings: 0 and 50 are neutral (rd__tone_active false), the
 *   factors value / 50 in tone.hlsli's order, 0 as 1.0;
 *   neutral: every row at 50 presents the rich frame byte for byte as
 *   checkOriginal's present (the box blit stays blit_ps);
 *   the arithmetic: exactRun at 4:3 (Full pixel on, one texel a pixel, the
 *   present DISPLAY byte for byte, checkExactWindow) presented at 1280 x
 *   720 with one row moved: every box pixel within 1 level of PCSX2's
 *   shade boost in double precision on the neutral present's k / 255
 *   (gamma 25 and 80, brightness 25 and 80, contrast 25 and 80, saturation
 *   1 and 80), the bars black;
 *   the keys: blit_tone_ps on both outputs in the start-up set in both
 *   blend modes;
 *   the effects depth (checkToneDepth): blit_depth_tone_ps's colour is
 *   blit_tone_ps's, byte for byte. */

/* PCSX2's ContrastSaturationBrightness (bin/resources/shaders/dx11/
 * shadeboost.fx) in double precision, its parameters value / 50
 * (GSDevice::ShadeBoost), with tone.hlsli's 1e-6 clamp before the power;
 * t[4]: gamma, brightness, contrast, saturation (s_runTone's order) */
static void toneReference(const uint8_t in[3], const uint8_t t[4], double out[3])
{
    const double gam = (t[0] ? t[0] : 50) / 50.0, brt = (t[1] ? t[1] : 50) / 50.0,
                 con = (t[2] ? t[2] : 50) / 50.0, sat = (t[3] ? t[3] : 50) / 50.0;
    double b[3];
    for (int c = 0; c < 3; c++) {
        b[c] = (double)in[c] / 255.0 * brt;
    }
    const double inten = 0.2125 * b[0] + 0.7154 * b[1] + 0.0721 * b[2];
    for (int c = 0; c < 3; c++) {
        const double sc = inten + (b[c] - inten) * sat;
        double cc = 0.5 + (sc - 0.5) * con;
        cc = cc < 1e-6 ? 1e-6 : cc;
        double v = pow(cc, 1.0 / gam);
        v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
        out[c] = v * 255.0;
    }
}

static void checkToneSettings(void)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    CHECK(!rd__tone_active_in(&s), "tone: a zeroed RdSettings is the game's picture");
    s.toneGamma = s.toneBrightness = s.toneContrast = s.toneSaturation = 50;
    CHECK(!rd__tone_active_in(&s), "tone: every row at 50 is the game's picture");
    s.toneBrightness = 0;
    CHECK(!rd__tone_active_in(&s), "tone: 0 and 50 mixed are the game's picture");
    float f[4];
    rd__tone_params_in(&s, f);
    CHECK(f[0] == 1.0f && f[1] == 1.0f && f[2] == 1.0f && f[3] == 1.0f,
          "tone: the neutral factors are 1 (%g %g %g %g)", (double)f[0], (double)f[1], (double)f[2],
          (double)f[3]);
    const uint8_t one[4][4] = {
        {51, 50, 50, 50}, {50, 49, 50, 50}, {50, 50, 1, 50}, {50, 50, 50, 100}};
    for (int i = 0; i < 4; i++) {
        s.toneGamma = one[i][0];
        s.toneBrightness = one[i][1];
        s.toneContrast = one[i][2];
        s.toneSaturation = one[i][3];
        CHECK(rd__tone_active_in(&s), "tone: case %d (one row moved) is on", i);
    }
    /* tone.hlsli's order: brightness, contrast, saturation, gamma */
    s.toneGamma = 25;
    s.toneBrightness = 100;
    s.toneContrast = 75;
    s.toneSaturation = 1;
    rd__tone_params_in(&s, f);
    CHECK(f[0] == 2.0f && f[1] == 1.5f && f[2] == 0.02f && f[3] == 0.5f,
          "tone: the factors in tone.hlsli's order (%g %g %g %g)", (double)f[0], (double)f[1],
          (double)f[2], (double)f[3]);
}

/* one row moved against the neutral exact run n (outW x outH): the box
 * within 1 level of the reference, the bars black and the same as n's */
static void checkToneCase(const char *name, const ExactRun *n, uint32_t outW, uint32_t outH,
                          const uint8_t t[4])
{
    ExactRun a;
    memcpy(s_runTone, t, sizeof(s_runTone));
    const bool ok = exactRun(4.0f / 3.0f, outW, outH, 1, &a);
    memset(s_runTone, 0, sizeof(s_runTone));
    if (!ok) {
        CHECK(0, "tone: %s: readback", name);
        return;
    }
    const RhiRect *b = &n->box;
    uint32_t bad = 0, barLit = 0, changed = 0, worstX = 0, worstY = 0;
    double worst = 0.0;
    for (uint32_t y = 0; y < outH; y++) {
        for (uint32_t x = 0; x < outW; x++) {
            const size_t o = ((size_t)y * outW + x) * 4;
            const int in = (int32_t)x >= b->x && (int32_t)x < b->x + (int32_t)b->w &&
                           (int32_t)y >= b->y && (int32_t)y < b->y + (int32_t)b->h;
            if (!in) {
                barLit += (a.out[o] | a.out[o + 1] | a.out[o + 2]) != 0 ||
                          memcmp(&a.out[o], &n->out[o], 3) != 0;
                continue;
            }
            double ref[3];
            toneReference(&n->out[o], t, ref);
            int over = 0;
            for (int c = 0; c < 3; c++) {
                const double d = fabs((double)a.out[o + c] - ref[c]);
                over |= d > 1.0;
                if (d > worst) {
                    worst = d;
                    worstX = x;
                    worstY = y;
                }
            }
            bad += over;
            changed += memcmp(&a.out[o], &n->out[o], 3) != 0;
        }
    }
    printf("  tone: %s: %u box pixels changed, %u off the reference by more than a level "
           "(worst %.3f at %u,%u), %u bar pixels lit or changed\n",
           name, changed, bad, worst, worstX, worstY, barLit);
    CHECK(bad == 0, "tone: %s: %u box pixels more than a level off the reference (worst %.3f)",
          name, bad, worst);
    CHECK(barLit == 0, "tone: %s: %u bar pixels lit or changed", name, barLit);
    /* the frame is about half black, which gamma, brightness and saturation
     * keep black */
    CHECK(changed > b->w * b->h / 8, "tone: %s: the picture changed (%u pixels)", name, changed);
    exactFree(&a);
}

static void checkTone(void)
{
    checkToneSettings();
    /* every row at 50: checkOriginal's present, byte for byte */
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.toneGamma = s.toneBrightness = s.toneContrast = s.toneSaturation = 50;
    FrameHashes h;
    if (!richFrame(&s, &h)) {
        CHECK(0, "tone: rd_init");
        return;
    }
    CHECK(s_presentOriginal && h.present == s_presentOriginal,
          "tone: every row at 50 is the present without the rows (%016llx, %016llx)",
          (unsigned long long)h.present, (unsigned long long)s_presentOriginal);

    /* the arithmetic, on the exact window's flat columns, copied band and
     * Gouraud triangle (every level from dark to bright) */
    const uint32_t W = 1280, H = 720;
    ExactRun n;
    if (!exactRun(4.0f / 3.0f, W, H, 1, &n)) {
        CHECK(0, "tone: the neutral readback");
        return;
    }
    CHECK(n.box.x == 160 && n.box.w == 960 && n.box.h == 720, "tone: the box is 960 x 720 at 160");

    static const struct {
        const char *name;
        uint8_t t[4]; /* gamma, brightness, contrast, saturation */
    } kCases[] = {
        {"gamma 25", {25, 50, 50, 50}},      {"gamma 80", {80, 50, 50, 50}},
        {"brightness 25", {50, 25, 50, 50}}, {"brightness 80", {50, 80, 50, 50}},
        {"contrast 25", {50, 50, 25, 50}},   {"contrast 80", {50, 50, 80, 50}},
        {"saturation 1", {50, 50, 50, 1}},   {"saturation 80", {50, 50, 50, 80}},
    };

    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        checkToneCase(kCases[i].name, &n, W, H, kCases[i].t);
    }
    exactFree(&n);

    /* the box blit's keys, both outputs, both blend modes */
    RdSettings k0 = originalSettings();
    if (!rd_init(512, 512, &k0, NULL)) {
        CHECK(0, "tone keys: rd_init");
        return;
    }
    for (int m = 0; m < 2; m++) {
        for (int f = 0; f < 2; f++) {
            const RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_BLIT_TONE,
                                                f ? RHI_FMT_BGRA8_UNORM : RHI_FMT_RGBA8_UNORM);
            CHECK(reachable(&k, m != 0), "tone keys: blit_tone_ps %s is not in the start-up set%s",
                  f ? "BGRA8" : "RGBA8", m ? " (two-pass blend)" : "");
        }
    }
    rd_shutdown();
}

/* The colour adjustment with the effects depth (checkEffectsDepth's frame,
 * forced, 1280 x 720): blit_depth_tone_ps draws the colour blit_tone_ps
 * draws without the depth, and the depth is still written; the picture is
 * not the neutral one */
static void checkToneDepth(void)
{
    static const uint8_t kTone[4] = {60, 55, 45, 70};
    const uint32_t W = 1280, H = 720;
    DepthRun plain, off, on;
    rd__force_effects_depth(true);
    const bool ranPlain = depthRun(W, H, 0, &plain);
    memcpy(s_runTone, kTone, sizeof(s_runTone));
    const bool ran = ranPlain && depthRun(W, H, 0, &off) && depthRun(W, H, 1, &on);
    memset(s_runTone, 0, sizeof(s_runTone));
    rd__force_effects_depth(false);
    CHECK(ran, "tone depth: rd_init");
    if (!ran) {
        return;
    }
    printf("  tone depth: present %016llx neutral, %016llx toned, %016llx toned with the depth\n",
           (unsigned long long)plain.present, (unsigned long long)off.present,
           (unsigned long long)on.present);
    CHECK(off.present != 0 && off.present == on.present && off.display == on.display,
          "tone depth: the toned picture is the same with the effects depth");
    CHECK(off.present != plain.present, "tone depth: the toned picture is not the neutral one");
    const char *noD32 = getenv("ICO_VK_FAKE_NO_D32F_ATTACHMENT");
    if (noD32 && noD32[0] && noD32[0] != '0') {
        CHECK(!on.depthOk, "tone depth: drawn although the device has no D32F attachment");
        return;
    }
    CHECK(on.depthOk, "tone depth: the output-size depth readback");
}
