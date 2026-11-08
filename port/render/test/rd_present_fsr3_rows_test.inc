/* rd_present_test.c's Upscaling: FSR 3.1 cases (issue 99;
 * rd_upscale_temporal.c, rd_fsr3.c): included by rd_present_test.c after
 * the FSR 1.0 cases.
 *
 * Without a device, and with FSR 3.1 turned off by the test's switch
 * (rd__fsr3_test_unsupported: the row's path on a device that cannot run
 * it):
 *   the quality ratios 1.0 / 1.5 / 1.7 / 2.0, an unknown value Native AA;
 *   the starting ratio is 1.0 under the Original preset and at a display
 *   scale of 1 or less, the quality's above it;
 *   not supported, FSR 3.1 is never in force, so the display line says
 *   "upscaling fsr3 off (not available: <why>)", or the CRT filter's words,
 *   and asked for with Downsampling "Downsampling is on" (the line is
 *   logged again when that changes); no other value adds words;
 *   MSAA/SSAA are not turned off for it (rd_aa_off_for_temporal false);
 *   asked for with a quality set, the present is checkOriginal's byte for
 *   byte (the row changes nothing where it cannot run).
 * On a device that runs it (Vulkan 1.2 with the FidelityFX extensions, a
 * build with the upscaler; otherwise the reason is printed and these are
 * skipped, except that a build with the upscaler on a Vulkan 1.2 device
 * that refuses it fails, rd__fsr3_not_expected):
 *   off       Enhanced 2x with Downsampling: FSR 3.1 asked is not in force
 *             (one or the other) and the present is the one with Upscaling
 *             Off byte for byte; no context made, nothing dispatched;
 *   on        Enhanced 2x, Quality (1.5): three rich frames, three
 *             dispatches by FSR 3.1 (the hook's name), one context of
 *             SCENE's render size (683 or so texels a side) into UPSCALED
 *             1024 x 1024 (display-sized: 512 x 2), UPSCALED and the
 *             present not black, the anti-aliasing off for it, the jitter's
 *             phase count FSR's (ceil(8 x 1.5^2) = 18 or near);
 *   resize    the same at 3x: a second context (UPSCALED 1536 x 1536), the
 *             first destroyed once the pictures that used it finished (a few
 *             replays later);
 *   shutdown  rd_shutdown destroys every context (made == destroyed: the
 *             asan run sees FidelityFX's memory given back); no validation
 *             error in the run. */

#include "rd_fsr3.h"
#include "rd_upscale.h"
#include "rd_upscale_temporal.h"

static void checkFsr3RowsUnsupported(void)
{
    rd__fsr3_test_unsupported(true);
    CHECK(!rd_upscale_temporal_supported(), "fsr3: the test's switch turns it off");
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.upscale = RD_UPSCALE_FSR3;
    s.upscaleQuality = RD_UPSCALE_Q_PERFORMANCE;
    s.preset = RD_PRESET_ENHANCED;
    CHECK(!rd__upscale_temporal_on(&s, false), "fsr3: not in force where it is not supported");
    char note[256];
    bool changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, NULL, note, sizeof(note));
    CHECK(strncmp(note, ", upscaling fsr3 off (not available: ", 37) == 0, "fsr3: the line (%s)",
          note);
    CHECK(changed, "fsr3: first call is a change");
    changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, NULL, note, sizeof(note));
    CHECK(!changed, "fsr3: the same settings are not a change");
    changed = rd__upscale_temporal_apply(&s, true, false, 2.0f, NULL, note, sizeof(note));
    CHECK(strcmp(note, ", upscaling fsr3 off (the CRT filter draws its own box)") == 0,
          "fsr3: under the CRT filter (%s)", note);
    s.downsample = 1;
    changed = rd__upscale_temporal_apply(&s, false, true, 2.0f, NULL, note, sizeof(note));
    CHECK(strcmp(note, ", upscaling fsr3 off (Downsampling is on)") == 0,
          "fsr3: with Downsampling (%s)", note);
    s.downsample = 0;
    s.upscale = RD_UPSCALE_FSR1;
    changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, NULL, note, sizeof(note));
    CHECK(note[0] == '\0' && changed, "fsr3: FSR 1.0 and Off add no words (%s)", note);
    s.upscale = RD_UPSCALE_OFF;
    changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, NULL, note, sizeof(note));
    CHECK(note[0] == '\0' && changed, "fsr3: Off adds no words (%s)", note);
    {
        RdSettings o = originalSettings();
        o.upscale = RD_UPSCALE_FSR3;
        o.upscaleQuality = RD_UPSCALE_Q_QUALITY;
        o.upscaleSharpness = 80;
        FrameHashes h;
        if (richFrame(&o, &h)) {
            CHECK(h.present == s_presentOriginal,
                  "fsr3 asked, not supported: the present is checkOriginal's byte for byte "
                  "(%016llx, %016llx)",
                  (unsigned long long)h.present, (unsigned long long)s_presentOriginal);
            CHECK(!rd_aa_off_for_temporal(), "fsr3: anti-aliasing is not turned off");
        }
    }
    rd__fsr3_test_unsupported(false);
}

static RdSettings fsr3Settings(float scale, uint8_t upscale, uint8_t downsample)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = scale;
    s.outputWidth = 960;
    s.outputHeight = 720;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    s.upscale = upscale;
    s.upscaleQuality = RD_UPSCALE_Q_QUALITY;
    s.upscaleSharpness = 80;
    s.downsample = downsample;
    return s;
}

/* the lit texels of an RGBA8 picture (any colour channel non-zero) */
static uint32_t fsr3Lit(const uint8_t *p, uint32_t w, uint32_t h)
{
    uint32_t n = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        n += (p[i * 4] | p[i * 4 + 1] | p[i * 4 + 2]) != 0;
    }
    return n;
}

static void checkFsr3Device(void)
{
    /* off: not in force under Downsampling, the present Off's */
    FrameHashes off, asked;
    const RdSettings sOff = fsr3Settings(2.0f, RD_UPSCALE_OFF, 1);
    const RdSettings sAsked = fsr3Settings(2.0f, RD_UPSCALE_FSR3, 1);
    const RdFsr3Stats b0 = *rd__fsr3_stats();
    if (richFrame(&sOff, &off) && richFrame(&sAsked, &asked)) {
        CHECK(off.present && off.present == asked.present && off.display == asked.display,
              "fsr3 off: asked under Downsampling, the present is Off's (%016llx, %016llx)",
              (unsigned long long)asked.present, (unsigned long long)off.present);
        CHECK(rd__fsr3_stats()->made == b0.made && rd__fsr3_stats()->dispatched == b0.dispatched,
              "fsr3 off: no context, no dispatch");
    }

    /* on */
    RdSettings s = fsr3Settings(2.0f, RD_UPSCALE_FSR3, 0);
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    if (!rd_upscale_temporal_supported()) {
        /* skipped only where it cannot be expected (no FidelityFX in the
           build, not Vulkan, below 1.2); a build with it on lavapipe 1.2+
           that refuses it fails */
        const char *notExpected = rd__fsr3_not_expected();
        CHECK(notExpected != NULL,
              "fsr3: the build has FSR 3.1 on a Vulkan 1.2 device but it is not available (%s)",
              rd_upscale_temporal_why());
        printf("  fsr3: not available here (%s; %s): the device cases are skipped\n",
               notExpected ? notExpected : "expected", rd_upscale_temporal_why());
        rd_shutdown();
        return;
    }
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    const RdFsr3Stats b1 = *rd__fsr3_stats();
    for (int i = 0; i < 3; i++) {
        recordRichFrame(t, 1);
    }
    const RdFsr3Stats *fs = rd__fsr3_stats();
    const RdUpscaleStats *us = rd__upscale_stats();
    CHECK(rd__upscale_by_fsr3() && strcmp(rd__upscale_hook_name(), "FSR 3.1") == 0,
          "fsr3 on: the hook is FSR 3.1 (%s)", rd__upscale_hook_name());
    CHECK(fs->made == b1.made + 1u && fs->dispatched == b1.dispatched + 3u &&
              fs->failed == b1.failed && fs->refused == b1.refused,
          "fsr3 on: contexts %u -> %u, dispatched %u -> %u, failed %u, refused %u", b1.made,
          fs->made, b1.dispatched, fs->dispatched, fs->failed - b1.failed,
          fs->refused - b1.refused);
    CHECK(us->outW == 1024u && us->outH == 1024u && us->colorW < us->outW &&
              us->colorW * 3u + 6u >= us->outW * 2u && fs->renderW == us->colorW &&
              fs->displayW == us->outW,
          "fsr3 on: SCENE %ux%u -> UPSCALED %ux%u (context %ux%u -> %ux%u)", us->colorW, us->colorH,
          us->outW, us->outH, fs->renderW, fs->renderH, fs->displayW, fs->displayH);
    CHECK(fs->phases >= 16 && fs->phases <= 20 && rd__upscale_phases() == (uint32_t)fs->phases,
          "fsr3 on: the phase count %d (the jitter's %u)", (int)fs->phases, rd__upscale_phases());
    CHECK(fs->memoryMb > 0.0 && fs->version[0] == '3', "fsr3 on: GPU memory %.1f MB, FFX %s",
          fs->memoryMb, fs->version);
    CHECK(rd_aa_off_for_temporal(), "fsr3 on: the anti-aliasing is off for it");
    static uint8_t up[1536 * 1536 * 4];
    uint32_t uw = 0, uh = 0, dw = 0, dh = 0, pw = 0, ph = 0;
    if (rd__upscale_read(up, sizeof(up), &uw, &uh)) {
        CHECK(fsr3Lit(up, uw, uh) > uw * uh / 4u, "fsr3 on: UPSCALED %ux%u is black", uw, uh);
    } else {
        CHECK(0, "fsr3 on: UPSCALED not read back");
    }
    (void)readTarget(RD_TARGET_DISPLAY, &dw, &dh);
    CHECK(dw == 1024u, "fsr3 on: DISPLAY %ux%u is the display's size", dw, dh);
    static uint8_t out[960 * 720 * 4];
    if (rd__read_present(out, sizeof(out), &pw, &ph)) {
        CHECK(fsr3Lit(out, pw, ph) > pw * ph / 4u, "fsr3 on: the present %ux%u is black", pw, ph);
    } else {
        CHECK(0, "fsr3 on: the present not read back");
    }
    printf("  fsr3: %ux%u -> %ux%u, phase count %d, FFX %s, GPU memory %.1f MB\n", fs->renderW,
           fs->renderH, fs->displayW, fs->displayH, (int)fs->phases, fs->version, fs->memoryMb);

    /* resize: a new context, the old one destroyed once its pictures are done */
    s.sceneScale = 3.0f;
    rd_set_settings(&s);
    recordRichFrame(t, 1);
    CHECK(fs->made == b1.made + 2u && us->outW == 1536u && fs->displayW == 1536u,
          "fsr3 resize: contexts %u, UPSCALED %ux%u", fs->made - b1.made, us->outW, us->outH);
    for (int i = 0; i < 2 * RHI_FRAMES_IN_FLIGHT + 2; i++) {
        recordRichFrame(t, 1);
    }
    CHECK(fs->destroyed >= b1.destroyed + 1u && fs->made - fs->destroyed == 1u,
          "fsr3 resize: %u made, %u destroyed (the first one is given back)", fs->made,
          fs->destroyed);
    rd_destroy_texture(t);
    rd_shutdown();
    CHECK(fs->made == fs->destroyed, "fsr3 shutdown: %u contexts made, %u destroyed", fs->made,
          fs->destroyed);
    CHECK(rhi_vk_validation_error_count() == 0, "fsr3: %u validation errors",
          rhi_vk_validation_error_count());
}

static void checkFsr3Rows(void)
{
    CHECK(rd__upscale_quality_ratio(RD_UPSCALE_Q_NATIVE) == 1.0f &&
              rd__upscale_quality_ratio(RD_UPSCALE_Q_QUALITY) == 1.5f &&
              rd__upscale_quality_ratio(RD_UPSCALE_Q_BALANCED) == 1.7f &&
              rd__upscale_quality_ratio(RD_UPSCALE_Q_PERFORMANCE) == 2.0f &&
              rd__upscale_quality_ratio(99) == 1.0f,
          "fsr3: the quality ratios");
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.upscale = RD_UPSCALE_FSR3;
    s.upscaleQuality = RD_UPSCALE_Q_PERFORMANCE;
    s.preset = RD_PRESET_ENHANCED;
    CHECK(rd__upscale_temporal_ratio(&s, 2.0f) == 2.0f, "fsr3: Performance above 1x is 2.0");
    CHECK(rd__upscale_temporal_ratio(&s, 1.0f) == 1.0f, "fsr3: Native AA only at 1x");
    s.preset = RD_PRESET_ORIGINAL;
    CHECK(rd__upscale_temporal_ratio(&s, 2.0f) == 1.0f, "fsr3: Native AA only under Original");
    checkFsr3RowsUnsupported();
    checkFsr3Device();
}
