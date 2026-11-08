/* rd_present_test.c's Upscaling: FSR 1.0 cases (issue 99; rd_fsr1.c,
 * fsr1.hlsl over AMD's ffx_a.h and ffx_fsr1.h): included by
 * rd_present_test.c after the colour adjustment's cases, so they share its
 * helpers (the rich frame, originalSettings, reachable, s_presentOriginal);
 * a file of their own keeps it under the repository's file size cap.
 *
 *   the constants: rd__fsr1_easu_con (AMD's FsrEasuConOffset) at 512 x 512
 *   into 960 x 720 against the values worked by hand (the texel steps are
 *   powers of two, exact; the scale is 8/15 and 32/45 rounded once), with
 *   an offset of 7.5; rd__fsr1_rcas_con (FsrRcasCon) at 0, 1, 2 and 0.4
 *   stops (the float and AMD's truncated halves), a denormal half and one
 *   below it; rd__fsr1_stops (the Sharpness row: 2 (1 - s / 100), half a
 *   stop more past 2x a side, clamped at 100);
 *   off: upscale 0 with a sharpness set presents the rich frame as
 *   checkOriginal did, byte for byte (the present hash gate), and notes no
 *   pass;
 *   on: the rich frame at 1280 x 720 (the 960 x 720 box at x 160): the
 *   pass noted 512x512 -> 960x720, the pillars black as off, at least a
 *   quarter of the box's pixels changed, the box exactly the RCAS output
 *   one texel a pixel; mirrored, the box the RCAS output flipped;
 *   the colour adjustment: one row moved still changes the box and leaves
 *   the pillars black;
 *   sharpness 0 and 100 differ;
 *   16:9 with Full pixel off (688 GS pixels, the border mask): the bands
 *   outside the mask black, the pass taking the cropped columns (512 + the
 *   crop's at each side) into the box;
 *   Downsampling at 2x: the downsampled 512 x 256 picture, line-doubled,
 *   upscaled (the phone's case);
 *   the "window" resolution and the CRT filter: on is off byte for byte;
 *   the keys: both passes in the start-up set at Upscaling FSR 1.0 (both
 *   blend modes), neither with Upscaling Off. */

#include "rd_fsr1.h"

static uint32_t fsrBits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static float fsrFloat(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static void checkFsr1Constants(void)
{
    uint32_t c[4][4];
    rd__fsr1_easu_con(c, 512.0f, 512.0f, 512.0f, 512.0f, 960.0f, 720.0f, 0.0f, 0.0f);
    /* the scale: 512 * fl(1 / 960) = fl(8 / 15) (a power of two times a
     * rounded value is that value's rounding), 512 * fl(1 / 720) = fl(32 / 45) */
    CHECK(c[0][0] == fsrBits((float)(8.0 / 15.0)) && c[0][1] == fsrBits((float)(32.0 / 45.0)),
          "fsr1: EASU con0 scale (%08x %08x)", c[0][0], c[0][1]);
    CHECK(fabs((double)fsrFloat(c[0][2]) - (4.0 / 15.0 - 0.5)) < 1e-7 &&
              fabs((double)fsrFloat(c[0][3]) - (16.0 / 45.0 - 0.5)) < 1e-7,
          "fsr1: EASU con0 offset (%.9g %.9g)", (double)fsrFloat(c[0][2]),
          (double)fsrFloat(c[0][3]));
    /* 1 / 512 = 2^-9: 0x3B000000; 2 / 512 0x3B800000; 4 / 512 0x3C000000 */
    const uint32_t want[3][4] = {{0x3B000000u, 0x3B000000u, 0x3B000000u, 0xBB000000u},
                                 {0xBB000000u, 0x3B800000u, 0x3B000000u, 0x3B800000u},
                                 {0x00000000u, 0x3C000000u, 0u, 0u}};
    CHECK(memcmp(&c[1][0], want, sizeof(want)) == 0,
          "fsr1: EASU con1..con3 (%08x %08x %08x %08x / %08x %08x %08x %08x / %08x %08x)", c[1][0],
          c[1][1], c[1][2], c[1][3], c[2][0], c[2][1], c[2][2], c[2][3], c[3][0], c[3][1]);
    /* the 16:9 crop: a 497-column viewport at 7.5 of a 512-texel input */
    rd__fsr1_easu_con(c, 497.0f, 448.0f, 512.0f, 448.0f, 1280.0f, 720.0f, 7.5f, 0.0f);
    CHECK(fabs((double)fsrFloat(c[0][0]) - 497.0 / 1280.0) < 1e-7 &&
              fabs((double)fsrFloat(c[0][2]) - (0.5 * 497.0 / 1280.0 - 0.5 + 7.5)) < 1e-6 &&
              fabs((double)fsrFloat(c[0][3]) - (0.5 * 448.0 / 720.0 - 0.5)) < 1e-7 &&
              c[1][0] == 0x3B000000u,
          "fsr1: EASU with the offset (%.9g %.9g %.9g)", (double)fsrFloat(c[0][0]),
          (double)fsrFloat(c[0][2]), (double)fsrFloat(c[0][3]));
    uint32_t r[4];

    static const struct {
        float stops;
        uint32_t f, h;
    } rc[] = {{0.0f, 0x3F800000u, 0x3C003C00u}, /* 1.0: half 0x3C00 */
              {1.0f, 0x3F000000u, 0x38003800u}, /* 0.5 */
              {2.0f, 0x3E800000u, 0x34003400u}, /* 0.25 */
              /* 2^-20: the half's denormal 0x0010 (AMD's table, base[107]) */
              {20.0f, 0x35800000u, 0x00100010u},
              /* 2^-30: below the smallest half denormal, 0 */
              {30.0f, 0x30800000u, 0x00000000u}};

    for (size_t i = 0; i < sizeof(rc) / sizeof(rc[0]); i++) {
        rd__fsr1_rcas_con(r, rc[i].stops);
        CHECK(r[0] == rc[i].f && r[1] == rc[i].h && r[2] == 0 && r[3] == 0,
              "fsr1: RCAS at %g stops (%08x %08x)", (double)rc[i].stops, r[0], r[1]);
    }
    /* 0.4 stops (Sharpness 80): 2^-0.4 = 0.75785828; the half truncated:
     * 1.51571656 x 2^-1, mantissa 0.51571656 x 1024 = 528.09 -> 0x3800 + 0x210 */
    rd__fsr1_rcas_con(r, 0.4f);
    CHECK(fabs((double)fsrFloat(r[0]) - 0.7578582832551990) < 1e-6 && r[1] == 0x3A103A10u,
          "fsr1: RCAS at 0.4 stops (%.9g, %08x)", (double)fsrFloat(r[0]), r[1]);
    CHECK(fabsf(rd__fsr1_stops(80, 1.875f) - 0.4f) < 1e-6f && rd__fsr1_stops(0, 1.0f) == 2.0f &&
              rd__fsr1_stops(100, 1.0f) == 0.0f && rd__fsr1_stops(150, 1.0f) == 0.0f &&
              fabsf(rd__fsr1_stops(80, 2.5f) - 0.9f) < 1e-6f && rd__fsr1_stops(100, 2.0f) == 0.0f,
          "fsr1: the Sharpness row's stops (%g %g %g %g)", (double)rd__fsr1_stops(80, 1.875f),
          (double)rd__fsr1_stops(0, 1.0f), (double)rd__fsr1_stops(100, 1.0f),
          (double)rd__fsr1_stops(80, 2.5f));
}

#define FSR_W 1280
#define FSR_H 720

/* the rich frame (gw x 512, the noise scene) presented with s at
 * FSR_W x FSR_H into out; note: the pass (rd__last_present_fsr1, zeros when
 * none), rcas (may be NULL): the RCAS output read back (box size); box, mask
 * (may be NULL): the presentation box and the border mask's rectangle in it
 * (the box itself where there is no mask) */
static bool fsrRun(const RdSettings *s, uint32_t gw, uint8_t *out, uint32_t note[4], uint8_t *rcas,
                   RhiRect *box, RhiRect *mask)
{
    memset(note, 0, 4 * sizeof(uint32_t));
    if (!rd_init(gw, 512, s, NULL)) {
        return false;
    }
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t w = 0, h = 0, ow = 0;
    bool ok = rd__read_present(out, (size_t)FSR_W * FSR_H * 4, &w, &h) && w == FSR_W && h == FSR_H;
    if (!rd__last_present_fsr1(note)) {
        memset(note, 0, 4 * sizeof(uint32_t));
    }
    RhiRect b;
    ok = rd__last_present_box(&ow, &h, &b) && ok;
    if (box) {
        *box = b;
    }
    if (mask && !rd__border_mask(&b, rd__frame_wexact(), g_rd.gsH, mask)) {
        *mask = b;
    }
    if (rcas && note[2]) {
        ok = rd__read_fsr1(rcas, (size_t)FSR_W * FSR_H * 4, &w, &h) && w == note[2] &&
             h == note[3] && ok;
    }
    CHECK(rhi_vk_validation_error_count() == 0, "fsr1: %u validation errors",
          rhi_vk_validation_error_count());
    rd_destroy_texture(t);
    rd_shutdown();
    return ok;
}

static uint8_t s_fsrOff[FSR_W * FSR_H * 4], s_fsrOn[FSR_W * FSR_H * 4],
    s_fsrOther[FSR_W * FSR_H * 4], s_fsrRcas[FSR_W * FSR_H * 4];

/* pixels of a and b that differ inside box, and bytes outside it not 0 in a */
static void fsrCompare(const uint8_t *a, const uint8_t *b, const RhiRect *box, uint32_t *inside,
                       uint32_t *outsideLit)
{
    *inside = *outsideLit = 0;
    for (uint32_t y = 0; y < FSR_H; y++) {
        for (uint32_t x = 0; x < FSR_W; x++) {
            const size_t i = ((size_t)y * FSR_W + x) * 4;
            const bool in = (int32_t)x >= box->x && x < (uint32_t)box->x + box->w &&
                            (int32_t)y >= box->y && y < (uint32_t)box->y + box->h;
            if (in) {
                *inside += memcmp(a + i, b + i, 3) != 0;
            } else {
                *outsideLit += (a[i] | a[i + 1] | a[i + 2]) != 0;
            }
        }
    }
}

/* the box of out against the RCAS output (box size), flipped across when
 * mirror: the RGB of the pixels that differ */
static uint32_t fsrBoxVsRcas(const uint8_t *out, const uint8_t *rcas, const RhiRect *box,
                             int mirror)
{
    uint32_t bad = 0;
    for (uint32_t y = 0; y < box->h; y++) {
        for (uint32_t x = 0; x < box->w; x++) {
            const uint32_t sx = mirror ? box->w - 1u - x : x;
            const uint8_t *o =
                out + (((size_t)(box->y + (int32_t)y)) * FSR_W + (size_t)(box->x + (int32_t)x)) * 4;
            const uint8_t *r = rcas + ((size_t)y * box->w + sx) * 4;
            bad += memcmp(o, r, 3) != 0;
        }
    }
    return bad;
}

static void checkFsr1(void)
{
    checkFsr1Constants();
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.outputWidth = FSR_W;
    s.outputHeight = FSR_H;
    uint32_t note[4], note2[4];
    RhiRect box, box2;
    /* off, with a sharpness: no pass, and the 960 x 720 present is
     * checkOriginal's byte for byte */
    {
        RdSettings o = originalSettings();
        o.upscaleSharpness = 80;
        FrameHashes h;
        if (richFrame(&o, &h)) {
            CHECK(h.present == s_presentOriginal,
                  "fsr1 off: the present is checkOriginal's byte for byte (%016llx, %016llx)",
                  (unsigned long long)h.present, (unsigned long long)s_presentOriginal);
        }
        uint32_t n[4];
        CHECK(!rd__last_present_fsr1(n), "fsr1 off: no pass noted");
    }
    if (!fsrRun(&s, 512, s_fsrOff, note, NULL, &box, NULL)) {
        CHECK(0, "fsr1: the run without it");
        return;
    }
    CHECK(note[2] == 0, "fsr1 off at 1280x720: no pass (%u)", note[2]);
    CHECK(box.x == 160 && box.y == 0 && box.w == 960 && box.h == 720,
          "fsr1: the 4:3 box of 1280 x 720 (%d,%d %ux%u)", box.x, box.y, box.w, box.h);
    /* on */
    s.upscale = RD_UPSCALE_FSR1;
    s.upscaleSharpness = 80;
    if (!fsrRun(&s, 512, s_fsrOn, note, s_fsrRcas, &box2, NULL)) {
        CHECK(0, "fsr1: the run with it");
        return;
    }
    CHECK(note[0] == 512 && note[1] == 512 && note[2] == 960 && note[3] == 720,
          "fsr1 on: the pass 512x512 -> 960x720 (%ux%u -> %ux%u)", note[0], note[1], note[2],
          note[3]);
    uint32_t inside = 0, lit = 0;
    fsrCompare(s_fsrOn, s_fsrOff, &box, &inside, &lit);
    printf("  fsr1: %u of %u box pixels changed from the bilinear stretch\n", inside,
           box.w * box.h);
    CHECK(lit == 0, "fsr1 on: %u pillar pixels not black", lit);
    CHECK(inside >= box.w * box.h / 4, "fsr1 on: only %u box pixels changed", inside);
    CHECK(fsrBoxVsRcas(s_fsrOn, s_fsrRcas, &box, 0) == 0,
          "fsr1 on: the box is the RCAS output one texel a pixel");
    /* mirrored: the RCAS output flipped (the box blit's nearest flip) */
    {
        RdSettings m = s;
        m.mirror = 1;
        if (fsrRun(&m, 512, s_fsrOther, note, s_fsrRcas, NULL, NULL)) {
            const uint32_t bad = fsrBoxVsRcas(s_fsrOther, s_fsrRcas, &box, 1);
            CHECK(note[2] == 960 && bad == 0,
                  "fsr1 mirror: the box is the RCAS output flipped (%u pixels differ)", bad);
        }
    }
    /* the colour adjustment: still the box blit's, the pillars black */
    {
        RdSettings t = s;
        t.toneContrast = 80;
        if (fsrRun(&t, 512, s_fsrOther, note, NULL, NULL, NULL)) {
            fsrCompare(s_fsrOther, s_fsrOn, &box, &inside, &lit);
            CHECK(note[2] == 960 && inside > 0 && lit == 0,
                  "fsr1 tone: the box adjusted (%u pixels), the pillars black (%u)", inside, lit);
        }
    }
    /* the sharpness: 0 and 100 differ */
    {
        RdSettings a = s, b = s;
        a.upscaleSharpness = 0;
        b.upscaleSharpness = 100;
        if (fsrRun(&a, 512, s_fsrOther, note, NULL, NULL, NULL) &&
            fsrRun(&b, 512, s_fsrOn, note, NULL, NULL, NULL)) {
            fsrCompare(s_fsrOther, s_fsrOn, &box, &inside, &lit);
            CHECK(inside > 0, "fsr1: sharpness 0 and 100 present the same picture");
        }
    }
    /* the CRT filter: on is off */
    {
        RdSettings c = s, d = s;
        rd_crt_settings(&c, RD_CRT_CONSUMER, 1.0f);
        rd_crt_settings(&d, RD_CRT_CONSUMER, 1.0f);
        d.upscale = RD_UPSCALE_OFF;
        if (fsrRun(&c, 512, s_fsrOther, note, NULL, NULL, NULL) &&
            fsrRun(&d, 512, s_fsrOn, note2, NULL, NULL, NULL)) {
            CHECK(memcmp(s_fsrOther, s_fsrOn, sizeof(s_fsrOn)) == 0,
                  "fsr1 under the CRT filter: the picture without it, byte for byte");
        }
    }
    /* the "window" resolution: on is off */
    {
        RdSettings w = s, x = s;
        w.preset = x.preset = RD_PRESET_ENHANCED;
        w.sceneScale = x.sceneScale = 0.0f;
        x.upscale = RD_UPSCALE_OFF;
        if (fsrRun(&w, 512, s_fsrOther, note, NULL, NULL, NULL)) {
            CHECK(note[2] == 0, "fsr1 at the window resolution: no pass (%u)", note[2]);
            if (fsrRun(&x, 512, s_fsrOn, note, NULL, NULL, NULL)) {
                CHECK(memcmp(s_fsrOther, s_fsrOn, sizeof(s_fsrOn)) == 0,
                      "fsr1 at the window resolution: the picture without it, byte for byte");
            }
        }
    }
    /* Downsampling at 2x: the GS-size picture (512 x 256, line-doubled) upscaled */
    {
        RdSettings d = s;
        d.sceneScale = 2.0f;
        d.downsample = 1;
        if (fsrRun(&d, 512, s_fsrOther, note, NULL, NULL, NULL)) {
            CHECK(note[0] == 512 && note[1] == 512 && note[2] == 960 && note[3] == 720,
                  "fsr1 after Downsampling: 512x512 -> 960x720 (%ux%u -> %ux%u)", note[0], note[1],
                  note[2], note[3]);
        }
    }
    /* 16:9, Full pixel off (the border mask over the picture): outside the
     * mask black, as without it; the pass taking the frame's columns */
    {
        RdSettings w = s, x = s;
        w.aspect = x.aspect = 16.0f / 9.0f;
        w.borderMask = x.borderMask = 1;
        x.upscale = RD_UPSCALE_OFF;
        const uint32_t fw = (uint32_t)ico_frame_width_for(16.0f / 9.0f);
        RhiRect wb, wm;
        if (fsrRun(&w, fw, s_fsrOther, note, NULL, &wb, &wm) &&
            fsrRun(&x, fw, s_fsrOn, note2, NULL, NULL, NULL)) {
            CHECK(note[0] == fw && note[2] == wb.w && note[3] == wb.h && note2[2] == 0,
                  "fsr1 16:9: the pass %ux%u -> %ux%u (the box %ux%u)", note[0], note[1], note[2],
                  note[3], wb.w, wb.h);
            CHECK(wm.w < wb.w && wm.h < wb.h, "fsr1 16:9: a border mask (%ux%u in %ux%u)", wm.w,
                  wm.h, wb.w, wb.h);
            fsrCompare(s_fsrOther, s_fsrOn, &wm, &inside, &lit);
            CHECK(lit == 0, "fsr1 16:9: %u pixels outside the mask not black", lit);
            CHECK(inside > 0, "fsr1 16:9: the picture inside the mask unchanged");
        }
    }
    /* the keys */
    {
        RdSettings o = originalSettings();
        if (rd_init(512, 512, &o, NULL)) {
            const RdPipeKeyInt e = rd__post_key(RD_VS_BLIT, RD_FS_FSR1_EASU, RHI_FMT_RGBA8_UNORM);
            const RdPipeKeyInt r = rd__post_key(RD_VS_BLIT, RD_FS_FSR1_RCAS, RHI_FMT_RGBA8_UNORM);
            CHECK(!reachable(&e, false) && !reachable(&r, false),
                  "fsr1: EASU and RCAS in the start-up set with Upscaling Off");
            rd_shutdown();
        }
        o.upscale = RD_UPSCALE_FSR1;
        if (rd_init(512, 512, &o, NULL)) {
            const RdPipeKeyInt e = rd__post_key(RD_VS_BLIT, RD_FS_FSR1_EASU, RHI_FMT_RGBA8_UNORM);
            const RdPipeKeyInt r = rd__post_key(RD_VS_BLIT, RD_FS_FSR1_RCAS, RHI_FMT_RGBA8_UNORM);
            for (int m = 0; m < 2; m++) {
                CHECK(reachable(&e, m != 0) && reachable(&r, m != 0),
                      "fsr1: EASU and RCAS not in the start-up set at FSR 1.0%s",
                      m ? " (two-pass blend)" : "");
            }
            CHECK(rd__pipe_key_family(&e) == RD_KF_CRT && rd__pipe_key_family(&r) == RD_KF_CRT,
                  "fsr1: the present-stage filters' family");
            rd_shutdown();
        }
    }
}
