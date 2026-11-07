/* rd_present_test.c's Upscaling: FSR 3.1 row cases (issue 99;
 * rd_upscale_temporal.c): included by rd_present_test.c after the FSR 1.0
 * cases.  The temporal upscaler is not in this version, so:
 *
 *   rd_upscale_temporal_supported() is false (and says why once);
 *   the quality ratios 1.0 / 1.5 / 1.7 / 2.0, an unknown value Native AA;
 *   the starting ratio is 1.0 under the Original preset and at a scene scale
 *   of 1 or less, the quality's above it;
 *   FSR 3.1 is never in force here (not supported), so the display line says
 *   "upscaling fsr3 off (not available)", or the CRT filter's words, and
 *   asked for with Downsampling "Downsampling is on" (the line is logged
 *   again when that changes); no other value adds words;
 *   MSAA/SSAA are not turned off for it (rd_aa_off_for_temporal false);
 *   asked for with a quality set, the present is checkOriginal's byte for
 *   byte (the row changes nothing where it cannot run). */

#include "rd_upscale_temporal.h"

static void checkFsr3Rows(void)
{
    CHECK(!rd_upscale_temporal_supported(), "fsr3: not supported in this version");
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
    s.preset = RD_PRESET_ENHANCED;
    CHECK(!rd__upscale_temporal_on(&s, false), "fsr3: not in force where it is not supported");
    char note[96];
    bool changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, note, sizeof(note));
    CHECK(strcmp(note, ", upscaling fsr3 off (not available)") == 0, "fsr3: the line (%s)", note);
    CHECK(changed, "fsr3: first call is a change");
    changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, note, sizeof(note));
    CHECK(!changed, "fsr3: the same settings are not a change");
    changed = rd__upscale_temporal_apply(&s, true, false, 2.0f, note, sizeof(note));
    CHECK(strcmp(note, ", upscaling fsr3 off (the CRT filter draws its own box)") == 0,
          "fsr3: under the CRT filter (%s)", note);
    s.downsample = 1;
    changed = rd__upscale_temporal_apply(&s, false, true, 2.0f, note, sizeof(note));
    CHECK(strcmp(note, ", upscaling fsr3 off (Downsampling is on)") == 0,
          "fsr3: with Downsampling (%s)", note);
    s.downsample = 0;
    s.upscale = RD_UPSCALE_FSR1;
    changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, note, sizeof(note));
    CHECK(note[0] == '\0' && changed, "fsr3: FSR 1.0 and Off add no words (%s)", note);
    s.upscale = RD_UPSCALE_OFF;
    changed = rd__upscale_temporal_apply(&s, false, false, 2.0f, note, sizeof(note));
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
}
