/* rd_blur_strength_test.inc: rd_blur_test.c's case (t), Options > Effects >
 * Motion blur strength (RdSettings.motionBlurStrength, issue 181; rd_internal.h
 * rd__blur_strength_fix), included after (s), whose sBar, sRecord, sTick,
 * sMean and sMode it uses.
 *
 * CPU (checkStrengthFix, with the recording checks): FIX_eff =
 * round(FIX x strength / 100), half up, FIX itself at 100 and for a
 * RdSettings value of 0 or over 100 (rd__blur_strength_pct).
 *
 * On the device (checkMotionBlurStrength): (s b)'s white bar, still for 6
 * ticks and then 8 pixels on, at the game's FIX 32, under Original at one
 * present a tick and under Smooth at four, DISPLAY read after every present
 * of the last tick:
 *   1  at 100 % the presents are the bytes of the zeroed setting's (0, the
 *      game's own FIX: as before the option), and no sprite is counted as
 *      drawn at a lower FIX;
 *   2  at 50 % the presents are the bytes of the game's own sprite recorded
 *      at FIX 16 (the FIX halved in the draw: its pipeline key and DrawCB,
 *      and under Smooth the k-root taken from it), every sprite counted,
 *      the last one's FIX 32 drawn as 16;
 *   3  the trail shorter: Original's ghost (the 8 columns the bar
 *      uncovered) after the tick within 1 of the GS formula's
 *      (Cs - Cd) FIX / 128 + Cd run over the 7 ticks (blendCh, ALPHA 0x64)
 *      at FIX 16 and at FIX 32, Smooth's within 2 of Original's at the same
 *      strength (as (s b)), and 50 %'s at most 60 % of 100 %'s in both. */

/* the GS's value of the bar's columns after 6 still ticks at fix (DISPLAY
 * cleared to 0 first), then of the uncovered columns after the 7th: the
 * motion blur (Cs the DISPLAY before, Cd SCENE) and an identity reduction */
static int tGhostModel(int fix)
{
    int v = 0;
    for (int t = 0; t < 6; t++) {
        v = blendCh(0x64, v, 255, 0, 0, fix, 1);
    }
    return blendCh(0x64, v, 0, 0, 0, fix, 1);
}

static void checkStrengthFix(void)
{
    static const struct {
        uint32_t fix, pct;
        uint8_t want;
    } k[] = {{32, 100, 32}, {32, 0, 32},  {32, 255, 32},  {32, 50, 16},   {32, 10, 3},
             {32, 90, 29},  {32, 70, 22}, {0, 50, 0},     {128, 50, 64},  {128, 10, 13},
             {4, 10, 0},    {5, 10, 1},   {255, 50, 128}, {300, 100, 255}};

    int bad = 0;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        const uint8_t got = rd__blur_strength_fix(k[i].fix, k[i].pct);
        if (got != k[i].want) {
            bad++;
            printf("  FIX %u at %u %%: %u, want %u\n", k[i].fix, k[i].pct, got, k[i].want);
        }
    }
    CHECK(bad == 0, "(t) FIX_eff = round(FIX x strength / 100): %d cases wrong", bad);
    const uint8_t keep = g_rd.settings.motionBlurStrength;
    static const uint8_t kSet[5] = {0, 10, 50, 100, 200};
    static const uint32_t kPct[5] = {100, 10, 50, 100, 100};
    for (int i = 0; i < 5; i++) {
        g_rd.settings.motionBlurStrength = kSet[i];
        CHECK(rd__blur_strength_pct() == kPct[i], "(t) setting %u: strength %u %% (want %u)",
              kSet[i], rd__blur_strength_pct(), kPct[i]);
    }
    g_rd.settings.motionBlurStrength = keep;
    CHECK(tGhostModel(32) == 63 && tGhostModel(16) == 31,
          "(t) the GS formula's ghost: %d at FIX 32, %d at FIX 16", tGhostModel(32),
          tGhostModel(16));
    printf("  (t) Motion blur strength: FIX_eff cases ok; the GS formula's ghost %d at FIX 32, "
           "%d at FIX 16\n",
           tGhostModel(32), tGhostModel(16));
}

/* (s b)'s bar at the recorded fix and strength pct, n presents a tick under
 * smooth, after a tick without the motion blur; DISPLAY after each present of the last tick into each (n images);
 * the sprites counted at a lower FIX into *lowered */
static bool tBarRun(int smooth, int fix, uint8_t pct, int n, uint8_t *each, uint64_t *lowered)
{
    uint64_t c0 = 0, c1 = 0;
    sMode(smooth);
    g_rd.settings.motionBlurStrength = pct;
    rd__blur_strength_counts(&c0, NULL, NULL);
    /* a tick without the motion blur first: every run's first tick then
       interpolates from the same frame, whatever the run before drew */
    sBar(200);
    sRecord(0, 1);
    bool ok = sTick(n, NULL, NULL);
    for (int t = 0; t < 7 && ok; t++) {
        sBar(t < 6 ? 200 : 208);
        sRecord(fix, t == 0);
        ok = sTick(n, t == 6 ? each : NULL, NULL);
    }
    rd__blur_strength_counts(&c1, NULL, NULL);
    *lowered = c1 - c0;
    g_rd.settings.motionBlurStrength = 0;
    return ok;
}

static void checkMotionBlurStrength(void)
{
    const uint8_t interpolate = g_rd.settings.interpolate;
    g_rd.settings.interpolate = 1; /* rd_end_frame leaves the replays to the presents */
    uint8_t *ref = malloc(4 * S_SIZE), *got = malloc(4 * S_SIZE);
    if (!ref || !got) {
        CHECK(0, "(t) memory");
        free(ref);
        free(got);
        g_rd.settings.interpolate = interpolate;
        return;
    }
    double ghost[2][2] = {{0.0, 0.0}, {0.0, 0.0}}; /* [smooth][50 %, 100 %] */
    for (int smooth = 0; smooth <= 1; smooth++) {
        const int n = smooth ? 4 : 1;
        const size_t bytes = (size_t)n * S_SIZE;
        const char *way = smooth ? "Smooth, 4 presents a tick" : "Original";
        uint64_t lowered = 0, lowRef = 0;
        uint8_t fix = 0, eff = 0;

        /* 1: 100 % is the game's own FIX, byte for byte */
        bool ok = tBarRun(smooth, 32, 0, n, ref, &lowRef);
        ok = ok && tBarRun(smooth, 32, 100, n, got, &lowered);
        CHECK(ok && memcmp(ref, got, bytes) == 0 && lowRef == 0 && lowered == 0,
              "(t1) %s: at 100 %% the presents are the game's own FIX's bytes (%s; %llu, %llu "
              "sprites at a lower FIX)",
              way, ok ? (memcmp(ref, got, bytes) == 0 ? "the same" : "different") : "failed",
              (unsigned long long)lowRef, (unsigned long long)lowered);
        ghost[smooth][1] = ok ? sMean(got + (size_t)(n - 1) * S_SIZE, 202, 206, NULL) : 0.0;

        /* 2: 50 % of FIX 32 is the game's sprite at FIX 16 */
        ok = tBarRun(smooth, 16, 0, n, ref, &lowRef);
        ok = ok && tBarRun(smooth, 32, 50, n, got, &lowered);
        rd__blur_strength_counts(NULL, &fix, &eff);
        CHECK(ok && memcmp(ref, got, bytes) == 0,
              "(t2) %s: at 50 %% the presents are the bytes of the sprite recorded at FIX 16 (%s)",
              way, ok ? "different" : "failed");
        CHECK(ok && lowRef == 0 && lowered == (uint64_t)(7 * n) && fix == 32 && eff == 16,
              "(t2) %s: %llu sprites at a lower FIX (want %d), the last FIX %u drawn as %u", way,
              (unsigned long long)lowered, 7 * n, fix, eff);
        ghost[smooth][0] = ok ? sMean(got + (size_t)(n - 1) * S_SIZE, 202, 206, NULL) : 0.0;
    }

    /* 3: the trail shorter, as the GS formula has it */
    const int m50 = tGhostModel(16), m100 = tGhostModel(32);
    CHECK(fabs(ghost[0][0] - m50) <= 1.0 && fabs(ghost[0][1] - m100) <= 1.0,
          "(t3) Original's ghost after the tick: %.2f at 50 %% (the GS formula %d), %.2f at 100 %% "
          "(%d)",
          ghost[0][0], m50, ghost[0][1], m100);
    CHECK(fabs(ghost[1][0] - ghost[0][0]) <= 2.0 && fabs(ghost[1][1] - ghost[0][1]) <= 2.0,
          "(t3) Smooth's ghost within 2 of Original's: %.2f / %.2f at 50 %%, %.2f / %.2f at 100 %%",
          ghost[1][0], ghost[0][0], ghost[1][1], ghost[0][1]);
    CHECK(ghost[0][1] > 40.0 && ghost[0][0] <= 0.6 * ghost[0][1] &&
              ghost[1][0] <= 0.6 * ghost[1][1],
          "(t3) 50 %%'s trail fainter: Original %.2f against %.2f, Smooth %.2f against %.2f",
          ghost[0][0], ghost[0][1], ghost[1][0], ghost[1][1]);
    printf("  (t) Motion blur strength: 100 %% the game's bytes, 50 %% the bytes of FIX 16; the "
           "ghost after a tick Original %.1f / %.1f (the GS formula %d / %d), Smooth %.1f / %.1f "
           "at 50 / 100 %%\n",
           ghost[0][0], ghost[0][1], m50, m100, ghost[1][0], ghost[1][1]);

    sMode(0);
    rd__blur_smooth_free("rd_blur_test: the case's end");
    g_rd.settings.motionBlurStrength = 0;
    g_rd.settings.interpolate = interpolate;
    SetMotionBlur(0);
    makeImage(1, 0); /* the picture the cases after this one start from */
    rd_update_texture(s_imgTex, s_img);
    free(ref);
    free(got);
}
