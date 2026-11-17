/*
 * port/platform/test/attempt_ledger_test.c
 *
 * The attempt ledger (attempt_ledger.h): every row "not reported" at the
 * start, a state with its why, the serial moving only on a change, the
 * block line's counts taken per block (the next block starts from 0), a
 * static row, rows not reported left out of the block line, the stalls
 * clause with and without an Adreno row and the block's stalls on that row's
 * second count, the high queue priority's row removed (v0.11.8: its start
 * words, left out of the blocks), out-of-range ids ignored and a short
 * buffer cut and terminated.
 */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int starts(const char *got, const char *want)
{
    if (strncmp(got, want, strlen(want)) != 0) {
        printf("got:  %s\nwant: %s...\n", got, want);
        return 0;
    }
    return 1;
}

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

/* the deciders as each device class's owners call them: no row at all left
   "not reported", each class's no-volume break state with its reason, the
   Adreno attempt on Qualcomm's own driver alone */
typedef struct DeviceClass {
    const char *name;
    int faster, pin;
    const char *noPin;
    int dateMode, dateSetting, box, skip;
    int nvEnv, nvClass, nvDefault;
    const char *nvGate;
    IcoAttemptState wantCores, wantNoVolume;
    const char *wantNoVolumeWhy;
    const char *passRow; /* RhiLimits.passObjectsRow (NULL: not Qualcomm's driver) */
    /* the VU record by compute (stores, powervr) and the turn fold
       (turned): the facts as rd_vu_record.c and rd_turn_fold.c have them */
    int vuStores, vuPowervr, turned;
    IcoAttemptState wantVu, wantTurn;
    /* the subpass read (rd_subpass_probe.c): RhiLimits.subpassRoute, the
       read in force, refused, the device's or the probe's words */
    int spRoute, spOn, spRefused;
    const char *spWhy;
    IcoAttemptState wantSubpass;
    /* off by default on the class this release (RhiLimits.subpassOffDefault) */
    int spOff;
} DeviceClass;

static void test_classes(void)
{
    static const DeviceClass k[] = {
        {"Mali-G52 (2 + 6)",
         2,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         0,
         0,
         NULL,
         ICO_ATT_ON,
         ICO_ATT_NA,
         "not the PowerVR or MoltenVK class under Vulkan",
         NULL,
         0,
         0,
         1,
         ICO_ATT_NA,
         ICO_ATT_OFF_DEFAULT,
         1,
         0,
         0,
         "off on this driver this release: a new kind of GPU work at start-up on a driver that "
         "lost the device before; one pass a picture",
         ICO_ATT_OFF_DEFAULT,
         1},
        {"Pixel (PowerVR D, 1 + 5 over 2)",
         6,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         1,
         1,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_ON,
         "the PowerVR D-Series DXT-48-1536",
         NULL,
         1,
         1,
         1,
         ICO_ATT_ON,
         ICO_ATT_OFF_DEFAULT,
         0,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         0},
        {"PowerVR B (no local read)",
         2,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         1,
         1,
         "the composites' DATE by the R8 snapshot or the shader's read",
         ICO_ATT_ON,
         ICO_ATT_NA,
         "the composites' DATE by the R8 snapshot or the shader's read",
         NULL,
         1,
         1,
         1,
         ICO_ATT_OFF_DEFAULT,
         ICO_ATT_OFF_DEFAULT,
         1,
         0,
         0,
         "no GPU timestamps: the probe cannot time it, so DATE keeps its R8 snapshot",
         ICO_ATT_NA,
         0},
        {"Adreno 740 on Qualcomm's driver (4 + 3 + 1)",
         4,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_NA,
         "not the PowerVR or MoltenVK class under Vulkan",
         "Qualcomm's own driver: every render pass a VkRenderPass object",
         1,
         0,
         1,
         ICO_ATT_NA,
         ICO_ATT_OFF_DEFAULT,
         1,
         0,
         0,
         "off on this driver this release: a new kind of GPU work at start-up on a driver that "
         "lost the device before; one pass a picture",
         ICO_ATT_OFF_DEFAULT,
         1},
        {"Adreno 740 on Turnip",
         4,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_NA,
         "not the PowerVR or MoltenVK class under Vulkan",
         NULL,
         1,
         0,
         1,
         ICO_ATT_NA,
         ICO_ATT_OFF_DEFAULT,
         0,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         0},
        {"desktop with MSAA",
         0,
         0,
         "only on Android",
         1,
         1,
         1,
         1,
         -1,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_NA,
         "not the PowerVR or MoltenVK class under Vulkan",
         NULL,
         1,
         0,
         0,
         ICO_ATT_NA,
         ICO_ATT_NA,
         0,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         0},
        {"a Mac (MoltenVK), the break forced",
         0,
         0,
         "only on Android",
         1,
         0,
         1,
         1,
         1,
         1,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_ON,
         "ICO_RD_NO_VOLUME_BREAK=1",
         NULL,
         1,
         0,
         0,
         ICO_ATT_NA,
         ICO_ATT_NA,
         0,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         0},
    };

    /* the record sized from need, k's classes in order: rows made at
       start-up, the memory bound's, the run's need (rd_early_start_rows),
       the row's state.  The Mali at v0.11.5's start (1175 MB available,
       floor 641 MB: 1024 rows within the bound, Original frame rate: 512);
       the Pixel interpolated (1024 = 1024); the PowerVR B's bound 256 under
       its need; the Adreno 740 under its floor (nothing made); the desktop
       Original frame rate; the Mac interpolated */
    static const struct {
        uint32_t made, bound, need;
        IcoAttemptState want;
    } kRec[] = {
        {512, 1024, 512, ICO_ATT_ON},   {1024, 1024, 1024, ICO_ATT_NA},
        {256, 256, 512, ICO_ATT_NA},    {0, 0, 512, ICO_ATT_NA},
        {0, 0, 1024, ICO_ATT_NA},       {512, 1024, 512, ICO_ATT_ON},
        {1024, 1024, 1024, ICO_ATT_NA},
    };

    /* the rows the record held before the start's plan: 256 in every class
       above (the start's record), so a class with nothing made is the
       memory's reason; the record already at the need is checked below */

    /* the facts the shipped defaults read, k's classes in order: the
       device is the PowerVR the VU record by compute and the no-volume
       break were shown on (rd_vu_class.h rd__vu_class_measured: the
       Pixel's D-Series DXT-48-1536), the build is Android's (the own
       timer's reach, rd_as_made_timer.h) */
    static const struct {
        int measured, android;
    } kShip[] = {
        {0, 1}, {1, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 0}, {0, 0},
    };

    /* the index compaction, k's classes in order: the record written by
       compute dispatches (else the path that holds), the device's class
       (ICO_ATT_CMP_*), the row's state.  The Mali-G52 the compute record (no
       vertex stores), its class by name, its self-test passed; the Pixel
       the compute record by the PowerVR rule (on by default on its DXT),
       the DXT's class, its self-test passed; every other class the vertex
       stores */
    static const struct {
        int compute;
        const char *notWhy;
        int cls;
        IcoAttemptState want;
    } kCmp[] = {
        {1, NULL, ICO_ATT_CMP_G52, ICO_ATT_ON},
        {1, NULL, ICO_ATT_CMP_DXT, ICO_ATT_ON},
        {0, "record path stores: vertex shader stores", ICO_ATT_CMP_OTHER, ICO_ATT_NA},
        {0, "record path stores: vertex shader stores", ICO_ATT_CMP_OTHER, ICO_ATT_NA},
        {0, "record path stores: vertex shader stores", ICO_ATT_CMP_OTHER, ICO_ATT_NA},
        {0, "record path stores: vertex shader stores", ICO_ATT_CMP_OTHER, ICO_ATT_NA},
        {0, "record path stores: vertex shader stores", ICO_ATT_CMP_OTHER, ICO_ATT_NA},
    };

    /* the in-between pictures for the GPU's predicted end, k's classes in
       order: the window presents pictures as made (vsync off, uncapped,
       the render thread on), a GPU span and first submit came back, 600
       measurements without one; the default off (RD_PACE_GPU_DEFAULT 0).
       The Mali-G52's reporter at the Original frame rate (v0.11.5 log:
       "framerate original"): paced; the Pixel uncapped with vsync off:
       off by default; the PowerVR B without GPU timestamps: no slot clock;
       the Adreno 740 on either driver uncapped: off by default; the
       desktop with vsync on: paced; the Mac uncapped: off by default.  The
       own timer: not applicable where paced, on as made on Android, off by
       default as made elsewhere (the Mac) */
    static const struct {
        int asMade, clock, waited;
        IcoAttemptState want;
    } kPace[] = {
        {0, 0, 0, ICO_ATT_NA},          {1, 1, 0, ICO_ATT_OFF_DEFAULT}, {1, 0, 1, ICO_ATT_NA},
        {1, 1, 0, ICO_ATT_OFF_DEFAULT}, {1, 1, 0, ICO_ATT_OFF_DEFAULT}, {0, 1, 0, ICO_ATT_NA},
        {1, 1, 0, ICO_ATT_OFF_DEFAULT},
    };

    CHECK(sizeof kRec / sizeof kRec[0] == sizeof k / sizeof k[0]);
    CHECK(sizeof kCmp / sizeof kCmp[0] == sizeof k / sizeof k[0]);
    CHECK(sizeof kPace / sizeof kPace[0] == sizeof k / sizeof k[0]);
    CHECK(sizeof kShip / sizeof kShip[0] == sizeof k / sizeof k[0]);
    for (size_t c = 0; c < sizeof k / sizeof k[0]; c++) {
        const DeviceClass *d = &k[c];
        ico_attempt_reset();
        ico_attempt_decide_cores(d->faster, d->pin, d->noPin);
        ico_attempt_decide_capture();
        /* the flat outputs: the shipped build; the VU pre-pass off by the
           device table on the Adreno 740 under Qualcomm's own driver (the
           classes with the pass row; issue 123's v0.11.6 log :103), on
           elsewhere here */
        ico_attempt_decide_flat0(1, d->passRow == NULL);
        CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) ==
              (d->passRow == NULL ? ICO_ATT_ON : ICO_ATT_NA));
        ico_attempt_decide_date_one(d->dateMode, d->dateSetting);
        ico_attempt_decide_empty_skip(d->box, d->skip);
        ico_attempt_decide_no_volume_break(d->nvEnv, d->nvClass, kShip[c].measured, d->nvDefault,
                                           d->nvGate);
        ico_attempt_decide_pass_objects(d->passRow);
        /* the VU record by compute with the shipped default (1) */
        CHECK(ico_attempt_decide_vu_compute(0, d->vuStores, 1, d->vuPowervr, kShip[c].measured, -1,
                                            1) == (d->wantVu == ICO_ATT_ON ? 1 : 0));
        CHECK(ico_attempt_decide_turn_fold(d->turned, -1, 0, -1) == 0);
        ico_attempt_decide_rec_need(1, kRec[c].made, kRec[c].bound, kRec[c].need, 256u);
        CHECK(ico_attempt_decide_compaction(kCmp[c].compute, kCmp[c].notWhy, kCmp[c].cls, -1, 1, 1,
                                            kCmp[c].compute ? 1 : -1) ==
              (kCmp[c].want == ICO_ATT_ON));
        ico_attempt_decide_subpass(d->spRoute, d->spOn, d->spRefused, d->spOff, d->spWhy);
        ico_attempt_decide_pace_gpu(kPace[c].asMade, kPace[c].clock, kPace[c].waited, 0);
        /* the own timer as shipped: drawn on Android */
        ico_attempt_decide_as_made_timer(kPace[c].asMade, kShip[c].android, NULL);
        CHECK(ico_attempt_state_of(ICO_ATT_AS_MADE_TIMER) == (!kPace[c].asMade ? ICO_ATT_NA
                                                              : kShip[c].android
                                                                  ? ICO_ATT_ON
                                                                  : ICO_ATT_OFF_DEFAULT));
        /* the screen's refresh: every as-made class here gives the display's
           times back (Android's swapchain and MoltenVK offer
           VK_GOOGLE_display_timing), so it follows the GPU model's row; as
           shipped it is drawn on Android only: on there where that row is
           off by default, off by default on the Mac (computed and reported),
           not applicable where paced or without a slot clock */
        ico_attempt_decide_pace_screen(kPace[c].asMade,
                                       kPace[c].clock ? 1 : (kPace[c].waited ? 0 : -1), 1,
                                       kShip[c].android, NULL);
        CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) ==
              (kPace[c].want == ICO_ATT_OFF_DEFAULT && kShip[c].android ? ICO_ATT_ON
                                                                        : kPace[c].want));
        /* the even cadence: the same classes as the screen's rule (it aims
           by its fit), off by default with the reason */
        ico_attempt_decide_pace_even(kPace[c].asMade,
                                     kPace[c].clock ? 1 : (kPace[c].waited ? 0 : -1), 1, 0);
        CHECK(ico_attempt_state_of(ICO_ATT_PACE_EVEN) == kPace[c].want);
        CHECK(kPace[c].want != ICO_ATT_OFF_DEFAULT ||
              strcmp(ico_attempt_why_of(ICO_ATT_PACE_EVEN),
                     "three phone rcs did not land pictures on their refresh") == 0);
        /* the glow's paste by its own entry: every class here is a Vulkan
           device (the module made) whose self-test passed (rd_paste_spec.c) */
        CHECK(ico_attempt_decide_paste_spec(1, -1, 1, 1) == 1);
        /* the shadow volumes' stencil-only entry: likewise a Vulkan device
           whose self-test passed (rd_volume_fs.c) */
        CHECK(ico_attempt_decide_volume_fs(1, -1, 1, 1) == 1);
        /* the shadow composites' own entry: likewise a dual-source Vulkan
           device whose self-test passed (rd_comp_spec.c) */
        CHECK(ico_attempt_decide_comp_spec(1, 1, -1, 1, 1) == 1);
        /* the two-pass blend in one pass (rd_onepass.c): the Mali-G52
           (class 0) has no dual-source blend, its self-test passed; every
           other class blends with dual sources: not applicable */
        CHECK(ico_attempt_decide_onepass(c == 0, -1, 1, c == 0 ? 1 : -1) == (c == 0 ? 1 : 0));
        CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == (c == 0 ? ICO_ATT_ON : ICO_ATT_NA));
        /* the old robust-access line: the Mali-G52 reporter's config.toml
           held [dev] gpu_robust = true without the mark (issue 179's v0.11.5
           log, line 15), removed at the start; no other class's did */
        ico_attempt_decide_robust_line(c == 0, 1);
        CHECK(ico_attempt_state_of(ICO_ATT_ROBUST_LINE) == (c == 0 ? ICO_ATT_ON : ICO_ATT_NA));
        /* no capture after a stall: the skin check runs on an Adreno with a
           window (Qualcomm's driver here), off on the other classes */
        ico_attempt_decide_skin_skip(d->passRow != NULL);
        CHECK(ico_attempt_state_of(ICO_ATT_SKIN_RESET_SKIP) ==
              (d->passRow != NULL ? ICO_ATT_ON : ICO_ATT_NA));
        /* the picture-health report (issue 179): every class here is a
           Vulkan device, counting */
        ico_attempt_decide_picture_health(1);
        CHECK(ico_attempt_state_of(ICO_ATT_PICTURE_HEALTH) == ICO_ATT_ON);
        /* the pins let go at one picture a tick (pin_release.h): on where
           the keeper's rule for three or more faster cores is in force (the
           Android classes here, as the Pixel), not applicable elsewhere */
        ico_attempt_decide_pin_release(kShip[c].android, kShip[c].android ? 1 : 0);
        CHECK(ico_attempt_state_of(ICO_ATT_PIN_RELEASE) ==
              (kShip[c].android ? ICO_ATT_ON : ICO_ATT_NA));
        /* the shadow count from the stencil (rd_shadow_count.c): every class
           here reads the stencil through its view (the count in one draw) */
        ico_attempt_decide_shadow_count(1);
        CHECK(ico_attempt_state_of(ICO_ATT_SHADOW_COUNT) == ICO_ATT_ON);
        /* the start-up set sized by the memory fall (set_size.h): on where the
           free-memory floor applies (the Android classes here), not
           applicable elsewhere */
        ico_attempt_decide_set_size(kShip[c].android);
        CHECK(ico_attempt_state_of(ICO_ATT_SET_SIZE) ==
              (kShip[c].android ? ICO_ATT_ON : ICO_ATT_NA));
        /* the camera rule for static meshes on the plain set (rd_pipeline.c):
           the classes with Qualcomm's pass row are the Adreno 740 on its own
           driver, which keeps the plain set (the device table's row); on
           there under the camera rule, not applicable elsewhere and under
           region_rule = ps2 */
        ico_attempt_decide_plain_camera(d->passRow != NULL, 0);
        CHECK(ico_attempt_state_of(ICO_ATT_PLAIN_CAMERA) == ICO_ATT_NA);
        ico_attempt_decide_plain_camera(d->passRow != NULL, 1);
        CHECK(ico_attempt_state_of(ICO_ATT_PLAIN_CAMERA) ==
              (d->passRow != NULL ? ICO_ATT_ON : ICO_ATT_NA));
        /* the paused scene's hold (rd_hold.c): every class here is a Vulkan
           device; the classes are not told whether list 11's draws join (the
           device's fx plan), so on, as the reporter's Adreno 740 */
        ico_attempt_decide_scene_hold(NULL);
        CHECK(ico_attempt_state_of(ICO_ATT_SCENE_HOLD) == ICO_ATT_ON);
        /* the whole start line, its last row included, in the window's 4 KB
           parts (window_perf_rank.c ledger_lines, ico_attempt_start_part):
           one part, or two since the v0.11.7 rows, each whole rows */
        {
            static char big[8192];
            const size_t n = ico_attempt_start_text(big, sizeof big);
            CHECK(n + 1u < sizeof big);
            CHECK(strstr(big, "; the old robust-access line taken off ") != NULL);
            CHECK(strstr(big, "; no capture after a stall ") != NULL);
            CHECK(strstr(big, "; picture health on (counting: 16 of 32 3D pictures unfinished "
                              "are said once)") != NULL);
            CHECK(strstr(big, "; pins let go at one picture a tick ") != NULL);
            int row = 0, parts = 0;
            char part[4096];
            while (row < ICO_ATT_COUNT && parts < 4) {
                const int from = row;
                const size_t m = ico_attempt_start_part(part, sizeof part, &row);
                CHECK(m + 1u < sizeof part && row > from);
                if (parts == 0 && row == ICO_ATT_COUNT) {
                    CHECK(m == n && strcmp(part, big) == 0);
                }
                parts++;
            }
            CHECK(row == ICO_ATT_COUNT && parts >= 1 && parts <= 2);
        }
        for (int i = 0; i < ICO_ATT_COUNT; i++) {
            if (ico_attempt_state_of(i) == ICO_ATT_NOT_SET) {
                printf("FAIL %s: %s not reported\n", d->name, ico_attempt_name(i));
                failures++;
            }
        }
        CHECK(ico_attempt_state_of(ICO_ATT_FEW_CORES) == d->wantCores);
        CHECK(ico_attempt_state_of(ICO_ATT_PIPE_SET_CORES) == d->wantCores);
        CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == d->wantNoVolume);
        CHECK(ico_attempt_state_of(ICO_ATT_ADRENO_A) == (d->passRow ? ICO_ATT_ON : ICO_ATT_NA));
        /* the high queue priority: removed on every device (v0.11.8) */
        CHECK(ico_attempt_state_of(ICO_ATT_ADRENO_B) == ICO_ATT_REMOVED);
        CHECK(ico_attempt_state_of(ICO_ATT_BLEND_CONST) == ICO_ATT_NOT_BUILT);
        CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_ON);
        CHECK(ico_attempt_state_of(ICO_ATT_CHAIN3) == ICO_ATT_NOT_BUILT);
        CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_ON);
        CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_ON);
        if (ico_attempt_state_of(ICO_ATT_SUBPASS) != d->wantSubpass) {
            printf("FAIL %s: subpass read %s\n", d->name,
                   ico_attempt_state_word(ico_attempt_state_of(ICO_ATT_SUBPASS)));
            failures++;
        }
        /* the row's why: the owner's words on the route, the fixed words off it */
        {
            const char *w = ico_attempt_why_of(ICO_ATT_SUBPASS);
            const char *want = d->spRoute ? d->spWhy
                                          : "not a VkRenderPass path without local read (dynamic "
                                            "rendering's local read, or another backend)";
            if (w == NULL || strcmp(w, want) != 0) {
                printf("FAIL %s: subpass read why %s\n", d->name, w ? w : "(none)");
                failures++;
            }
        }
        if (ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) != d->wantVu ||
            ico_attempt_state_of(ICO_ATT_TURN_FOLD) != d->wantTurn) {
            printf("FAIL %s: VU compute %s, turn fold %s\n", d->name,
                   ico_attempt_state_word(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS)),
                   ico_attempt_state_word(ico_attempt_state_of(ICO_ATT_TURN_FOLD)));
            failures++;
        }
        CHECK(ico_attempt_state_of(ICO_ATT_REC_NEED) == kRec[c].want);
        CHECK(ico_attempt_state_of(ICO_ATT_COMPACTION) == kCmp[c].want);
        if (ico_attempt_state_of(ICO_ATT_PACE_GPU) != kPace[c].want) {
            printf("FAIL %s: GPU's predicted end %s\n", d->name,
                   ico_attempt_state_word(ico_attempt_state_of(ICO_ATT_PACE_GPU)));
            failures++;
        }
        char b[8192], want[256];
        (void)ico_attempt_start_text(b, sizeof b);
        snprintf(want, sizeof want, "; no-volume break %s (%s)",
                 ico_attempt_state_word(d->wantNoVolume), d->wantNoVolumeWhy);
        if (strstr(b, want) == NULL) {
            printf("FAIL %s: %s not in %s\n", d->name, want, b);
            failures++;
        }
    }
    /* the subpass read on the Intel driver row: probed (on for this start's
       probe); the Mali's start line says off by default with the words */
    ico_attempt_decide_subpass(1, 1, 0, 0, "no verdict for this device and driver yet");
    CHECK(ico_attempt_state_of(ICO_ATT_SUBPASS) == ICO_ATT_ON);
    ico_attempt_decide_subpass(1, 0, 0, 1,
                               "off on this driver this release: a new kind of GPU work at "
                               "start-up on a driver that lost the device before; one pass a "
                               "picture");
    {
        char b[8192];
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; subpass read off by default (off on this driver this release: a new "
                        "kind of GPU work at start-up on a driver that lost the device before; "
                        "one pass a picture);") != NULL);
    }
    /* off by default never over on or refused */
    ico_attempt_decide_subpass(1, 0, 1, 1, "refused");
    CHECK(ico_attempt_state_of(ICO_ATT_SUBPASS) == ICO_ATT_REFUSED);
    /* the DATE row's and the empty row's other states */
    ico_attempt_decide_date_one(1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_DATE_ONE) == ICO_ATT_NA);
    ico_attempt_decide_date_one(0, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_DATE_ONE) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_decide_empty_skip(0, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_EMPTY_SKIP) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_decide_no_volume_break(0, 1, 1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_decide_no_volume_break(-1, 1, 1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == ICO_ATT_ON);
    /* the class but not the device it was shown on (another PowerVR, a Mac):
       off by default with the reason; the define 0: off by default */
    ico_attempt_decide_no_volume_break(-1, 1, 0, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == ICO_ATT_OFF_DEFAULT &&
          strcmp(ico_attempt_why_of(ICO_ATT_NO_VOLUME_BREAK),
                 "shown clean only on the PowerVR D-Series DXT-48-1536's title and pause menu; "
                 "not tried on this device") == 0);
    ico_attempt_decide_no_volume_break(-1, 1, 1, 0, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_decide_cores(0, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_FEW_CORES) == ICO_ATT_NA);
    /* the VU record by compute: the tests' switch, the default flipped, a
       mode asked by name, no compute stores */
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 0, 0, 1, 0) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_ON);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 1, 1, -1, 1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_VU_COMPUTE_CLASS),
                 "the PowerVR D-Series DXT-48-1536: compute dispatches in place of the vertex "
                 "stores") == 0);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 1, 0, -1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 1, 1, -1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 1, 1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_vu_compute(1, 1, 1, 1, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 0, 1, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    /* the turn fold: on only with its self-test passed */
    CHECK(ico_attempt_decide_turn_fold(1, 1, 0, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_ON);
    CHECK(ico_attempt_decide_turn_fold(1, -1, 1, 1) == 1);
    CHECK(ico_attempt_decide_turn_fold(1, 1, 0, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_REFUSED);
    CHECK(ico_attempt_decide_turn_fold(1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_turn_fold(1, 0, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_turn_fold(0, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_NA);
    CHECK(ico_attempt_why_of(ICO_ATT_COUNT) == NULL);
    /* the index compaction's other states: the tests' switch both ways,
       refused by the self-test (over the switch), the Mali-G52's define
       off, the DXT's define off, another device off by default (measured
       on the Mali-G52 only) and on by the switch, the self-test not run (off
       but under the switch), not the compute record with and without its
       words; the candidate (tried by the self-test); its count */
    const int G = ICO_ATT_CMP_G52, D = ICO_ATT_CMP_DXT, O = ICO_ATT_CMP_OTHER;
    CHECK(ico_attempt_decide_compaction(1, NULL, G, 0, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMPACTION) == ICO_ATT_OFF_DEFAULT);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION),
                 "ICO_RD_VU_COMPACT=0: the tests' switch") == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, O, 1, 0, 0, -1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION),
                 "ICO_RD_VU_COMPACT=1: the tests' switch") == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, O, 1, 0, 0, 1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION),
                 "ICO_RD_VU_COMPACT=1, its start-up self-test passed") == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, G, 1, 1, 1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMPACTION) == ICO_ATT_REFUSED);
    CHECK(ico_attempt_decide_compaction(1, NULL, G, -1, 1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMPACTION) == ICO_ATT_OFF_DEFAULT);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION),
                 "off by default on the Mali-G52: it blacked out the picture there in v0.11.6 "
                 "(issue 179; RD_VU_COMPACT_ARM_DEFAULT 0)") == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, D, -1, 0, 1, 1) == 0);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION),
                 "off by default on the PowerVR D-Series DXT-48-1536 (RD_VU_COMPACT_DEFAULT 0)") ==
          0);
    CHECK(ico_attempt_decide_compaction(1, NULL, D, -1, 1, 0, 1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION),
                 "the PowerVR D-Series DXT-48-1536, its start-up self-test passed") == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, O, -1, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMPACTION) == ICO_ATT_OFF_DEFAULT);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION),
                 "off by default: measured on the Mali-G52 only (and on the PowerVR D-Series "
                 "DXT-48-1536's compute record)") == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, G, -1, 1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMPACTION) == ICO_ATT_OFF_DEFAULT);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION), "its start-up self-test not run (yet)") ==
          0);
    CHECK(ico_attempt_decide_compaction(1, NULL, D, -1, 1, 1, -1) == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, G, 1, 1, 1, -1) == 1);
    CHECK(ico_attempt_decide_compaction(0, NULL, G, 1, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMPACTION) == ICO_ATT_NA);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_COMPACTION), "not the compute record") == 0);
    CHECK(ico_attempt_compaction_candidate(1, G, -1, 0, 1) == 1);
    CHECK(ico_attempt_compaction_candidate(1, G, -1, 1, 0) == 0);
    CHECK(ico_attempt_compaction_candidate(1, D, -1, 1, 0) == 1);
    CHECK(ico_attempt_compaction_candidate(1, O, -1, 1, 1) == 0);
    CHECK(ico_attempt_compaction_candidate(1, O, 1, 0, 0) == 1);
    CHECK(ico_attempt_compaction_candidate(1, G, 0, 1, 1) == 0);
    CHECK(ico_attempt_compaction_candidate(0, G, 1, 1, 1) == 0);
    CHECK(ico_attempt_decide_compaction(1, NULL, G, -1, 1, 1, 1) == 1);
    ico_attempt_fire(ICO_ATT_COMPACTION, 9866);
    {
        char b[8192];
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; index compaction on (the Mali-G52, its start-up self-test passed)") !=
              NULL);
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; index compaction on 9866 triangles left out") != NULL);
    }
    /* the GPU's predicted end: as made without a clock yet leaves the row
       as it was; the clock turns it on; its counts in the block line */
    ico_attempt_decide_pace_gpu(0, 0, 0, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_NA);
    ico_attempt_decide_pace_gpu(1, 0, 0, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_NA);
    ico_attempt_decide_pace_gpu(1, 1, 0, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_OFF_DEFAULT &&
          strcmp(ico_attempt_why_of(ICO_ATT_PACE_GPU),
                 "off by default (the owner's phone felt worse with it in rc3; kept for "
                 "measurement)") == 0);
    ico_attempt_decide_pace_gpu(1, 1, 1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_ON);
    ico_attempt_fire(ICO_ATT_PACE_GPU, 598);
    ico_attempt_fire2(ICO_ATT_PACE_GPU, 3);
    {
        char b[8192];
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; in-between pictures for the GPU's predicted end on (ICO_RD_PACE_GPU=1: "
                        "each picture drawn for its GPU work's predicted end)") != NULL);
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; in-between pictures for the GPU's predicted end on 598 pictures "
                        "placed (3 at the bound)") != NULL);
    }
    ico_attempt_decide_pace_gpu(1, 0, 1, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_NA);
    ico_attempt_decide_as_made_timer(1, 0, "not Android: the hand-over rule kept");
    CHECK(ico_attempt_state_of(ICO_ATT_AS_MADE_TIMER) == ICO_ATT_OFF_DEFAULT &&
          strcmp(ico_attempt_why_of(ICO_ATT_AS_MADE_TIMER),
                 "not Android: the hand-over rule kept") == 0);
    ico_attempt_decide_as_made_timer(1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_AS_MADE_TIMER) == ICO_ATT_ON);
    ico_attempt_fire(ICO_ATT_AS_MADE_TIMER, 600);
    ico_attempt_fire2(ICO_ATT_AS_MADE_TIMER, 4);
    {
        char b[8192];
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; in-between pictures on their own timer on 600 pictures timed (4 held "
                        "in their frame's tick)") != NULL);
    }
    CHECK(strstr(ico_attempt_why_of(ICO_ATT_PACE_GPU), "no GPU span or slot clock") != NULL);
    /* the screen's refresh: no display times; then on with its counts */
    ico_attempt_decide_pace_screen(1, 1, 0, 0, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_NA &&
          strstr(ico_attempt_why_of(ICO_ATT_PACE_SCREEN), "gives no times back") != NULL);
    ico_attempt_decide_pace_screen(1, 0, 1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_NA &&
          strstr(ico_attempt_why_of(ICO_ATT_PACE_SCREEN), "no GPU end") != NULL);
    ico_attempt_decide_pace_screen(1, 1, 1, 0, "not Android: measured on Android phones only");
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_OFF_DEFAULT &&
          strcmp(ico_attempt_why_of(ICO_ATT_PACE_SCREEN),
                 "not Android: measured on Android phones only") == 0);
    ico_attempt_decide_pace_screen(1, 1, 1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_ON);
    ico_attempt_fire(ICO_ATT_PACE_SCREEN, 700);
    ico_attempt_fire2(ICO_ATT_PACE_SCREEN, 2);
    {
        char b[8192];
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; in-between pictures for the screen's refresh on 700 pictures placed (2 "
                        "held at their frame's edges)") != NULL);
    }
    /* not known yet (no display time and under 600 pictures): left as it was */
    ico_attempt_decide_pace_screen(1, 1, -1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_SCREEN) == ICO_ATT_ON);
    /* the even cadence: paced, no display times, no clock, on with its
       counts, not known */
    ico_attempt_decide_pace_even(0, 1, 1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_EVEN) == ICO_ATT_NA &&
          strstr(ico_attempt_why_of(ICO_ATT_PACE_EVEN), "paced here") != NULL);
    ico_attempt_decide_pace_even(1, 1, 0, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_EVEN) == ICO_ATT_NA &&
          strstr(ico_attempt_why_of(ICO_ATT_PACE_EVEN), "gives no times back") != NULL);
    ico_attempt_decide_pace_even(1, 0, 1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_EVEN) == ICO_ATT_NA &&
          strstr(ico_attempt_why_of(ICO_ATT_PACE_EVEN), "no GPU end") != NULL);
    ico_attempt_decide_pace_even(1, 1, 1, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_EVEN) == ICO_ATT_OFF_DEFAULT &&
          strcmp(ico_attempt_why_of(ICO_ATT_PACE_EVEN),
                 "three phone rcs did not land pictures on their refresh") == 0);
    ico_attempt_decide_pace_even(1, 1, 1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_EVEN) == ICO_ATT_ON);
    ico_attempt_fire(ICO_ATT_PACE_EVEN, 600);
    ico_attempt_fire2(ICO_ATT_PACE_EVEN, 3);
    {
        char b[8192];
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; an even number of refreshes a picture on 600 pictures aimed (3 late for "
                        "their refresh)") != NULL);
    }
    ico_attempt_decide_pace_even(1, 1, -1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_EVEN) == ICO_ATT_ON);
    /* the record's row: the pre-pass off; on with its growths counted */
    ico_attempt_decide_rec_need(0, 512, 1024, 512, 256);
    CHECK(ico_attempt_state_of(ICO_ATT_REC_NEED) == ICO_ATT_NA);
    /* nothing made: the record already at the need (the owner's Pixel in
       v0.11.7-rc1: 1024 rows held, the need 1024, 774 MB above the floor),
       and the memory's reason where it held less */
    {
        char b[8192];

        ico_attempt_decide_rec_need(1, 0, 0, 1024, 1024);
        CHECK(ico_attempt_state_of(ICO_ATT_REC_NEED) == ICO_ATT_NA);
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; VU record sized from need not applicable here (the record already "
                        "held the run's need at start-up: nothing to make)") != NULL);
        ico_attempt_decide_rec_need(1, 0, 0, 1024, 256);
        CHECK(ico_attempt_state_of(ICO_ATT_REC_NEED) == ICO_ATT_NA);
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; VU record sized from need not applicable here (nothing made at "
                        "start-up: the free memory under its floor or the system's low-memory "
                        "signal, or no larger record within the memory bound)") != NULL);
    }
    ico_attempt_decide_rec_need(1, 512, 1024, 512, 256);
    CHECK(ico_attempt_state_of(ICO_ATT_REC_NEED) == ICO_ATT_ON);
    ico_attempt_fire(ICO_ATT_REC_NEED, 2);
    {
        char b[8192];
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; VU record sized from need on (made at the run's need, less than the "
                        "memory bound allowed)") != NULL);
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; VU record sized from need on 2 growths in play") != NULL);
    }
    /* the shadow volumes' stencil-only entry, each state (rd_volume_fs.c):
       the tests' 0, another backend, the build's default 0, a refusal, not
       run yet, the switch's 1 */
    CHECK(ico_attempt_decide_volume_fs(1, 0, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_volume_fs(0, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_NA);
    CHECK(ico_attempt_decide_volume_fs(1, -1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_volume_fs(1, -1, 1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_REFUSED);
    CHECK(ico_attempt_decide_volume_fs(1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_volume_fs(1, 1, 0, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_ON);
    ico_attempt_fire(ICO_ATT_VOLUME_FS, 8);
    ico_attempt_fire2(ICO_ATT_VOLUME_FS, 2);
    {
        char b[8192];
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; the shadow volumes by the stencil-only entry on (ICO_RD_VOLUME_FS=1, "
                        "self-test passed)") != NULL);
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; the shadow volumes by the stencil-only entry on 8 volume draws (2 kept "
                        "on sprite_ps)") != NULL);
    }
    /* the shadow composites' own entry, each state (rd_comp_spec.c): the
       tests' 0, another backend, the two-pass blend, the build's default 0,
       a refusal, not run yet, the switch's 1 */
    CHECK(ico_attempt_decide_comp_spec(1, 1, 0, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_comp_spec(0, 1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_NA);
    CHECK(ico_attempt_decide_comp_spec(1, 0, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_NA);
    CHECK(ico_attempt_decide_comp_spec(1, 1, -1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_comp_spec(1, 1, -1, 1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_REFUSED);
    CHECK(ico_attempt_decide_comp_spec(1, 1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_comp_spec(1, 1, 1, 0, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_ON);
    ico_attempt_fire(ICO_ATT_COMP_SPEC, 9);
    ico_attempt_fire2(ICO_ATT_COMP_SPEC, 1);
    {
        char b[8192];
        (void)ico_attempt_start_text(b, sizeof b);
        CHECK(strstr(b, "; the shadow composites by their own entry on (ICO_RD_COMP_SPEC=1, "
                        "self-test passed)") != NULL);
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, "; the shadow composites by their own entry on 9 draws (1 kept on "
                        "sprite_ps)") != NULL);
    }
    ico_attempt_reset();
}

/* the old robust-access row's three states, its start words and its
   block words (a static row) */
static void test_robust_line(void)
{
    char b[4096];

    ico_attempt_reset();
    ico_attempt_decide_robust_line(0, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_ROBUST_LINE) == ICO_ATT_NA);
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "; the old robust-access line taken off not applicable here (no [dev] "
                    "gpu_robust = true without the game's mark in config.toml)") != NULL);
    ico_attempt_decide_robust_line(1, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_ROBUST_LINE) == ICO_ATT_REFUSED);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_ROBUST_LINE),
                 "config.toml could not be written: robust access stays on, the next start "
                 "tries again") == 0);
    /* a hand-written form the strip cannot take out: refused, nothing
       written, to be deleted by hand */
    ico_attempt_decide_robust_line(1, -1);
    CHECK(ico_attempt_state_of(ICO_ATT_ROBUST_LINE) == ICO_ATT_REFUSED);
    CHECK(strstr(ico_attempt_why_of(ICO_ATT_ROBUST_LINE), "delete the line by hand") != NULL);
    const uint32_t s0 = ico_attempt_serial();
    ico_attempt_decide_robust_line(1, 1);
    CHECK(ico_attempt_serial() == s0 + 1u);
    CHECK(ico_attempt_state_of(ICO_ATT_ROBUST_LINE) == ICO_ATT_ON);
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "; the old robust-access line taken off on ([dev] gpu_robust = true without "
                    "the game's mark removed from config.toml: robust access off from this "
                    "start)") != NULL);
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: the old robust-access line taken off on (static)"));
    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_ROBUST_LINE) == ICO_ATT_NOT_SET);
}

/* no capture after a stall (issue 123, v0.11.7): its two states, its start
   words, and its block count per block (the checks skipped after a reset) */
static void test_skin_skip(void)
{
    char b[4096];

    ico_attempt_reset();
    ico_attempt_decide_skin_skip(0);
    CHECK(ico_attempt_state_of(ICO_ATT_SKIN_RESET_SKIP) == ICO_ATT_NA);
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "; no capture after a stall not applicable here (the skin check is off "
                    "here)") != NULL);
    ico_attempt_decide_skin_skip(1);
    CHECK(ico_attempt_state_of(ICO_ATT_SKIN_RESET_SKIP) == ICO_ATT_ON);
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "; no capture after a stall on (a skin check read after a GPU reset is "
                    "skipped, not captured)") != NULL);
    ico_attempt_fire(ICO_ATT_SKIN_RESET_SKIP, 1u);
    ico_attempt_fire(ICO_ATT_SKIN_RESET_SKIP, 2u);
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: no capture after a stall on 3 checks skipped after a reset"));
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: no capture after a stall on 0 checks skipped after a reset"));
    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_SKIN_RESET_SKIP) == ICO_ATT_NOT_SET);
}

/* the picture-health report (issue 179, v0.11.7): its row's states and its
   counts per block */
static void test_health(void)
{
    char b[4096];

    ico_attempt_reset();
    ico_attempt_decide_picture_health(0);
    CHECK(ico_attempt_state_of(ICO_ATT_PICTURE_HEALTH) == ICO_ATT_NA);
    ico_attempt_decide_picture_health(1);
    CHECK(ico_attempt_state_of(ICO_ATT_PICTURE_HEALTH) == ICO_ATT_ON);
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "picture health on (counting: 16 of 32 3D pictures unfinished are said "
                    "once)") != NULL);
    ico_attempt_fire(ICO_ATT_PICTURE_HEALTH, 3u);
    ico_attempt_fire2(ICO_ATT_PICTURE_HEALTH, 1u);
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: picture health on 3 3D pictures with stamps unwritten (1 GPU "
                     "clock restarts)"));
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: picture health on 0 3D pictures with stamps unwritten (0 GPU "
                     "clock restarts)"));
    ico_attempt_reset();
}

/* the flat outputs where the VU pre-pass is off (issue 123: the Adreno
   740's ledger said "on" for a shader that never ran there), and the
   capture row's words (slow-scene dumps alone) */
static void test_flat0_capture(void)
{
    char b[4096];

    ico_attempt_reset();
    ico_attempt_flat0_reach(1); /* nothing before the renderer said */
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_NOT_SET);
    ico_attempt_decide_flat0(1, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_NA);
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "; flat outputs at the provoking corner not applicable here (the VU pre-pass "
                    "is off here: vu_main_vs, the shader it changes, draws only through the "
                    "pre-pass's record);") != NULL);
    /* the pre-pass on (a later block's look): on again */
    ico_attempt_flat0_reach(1);
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_ON);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_FLAT0),
                 "every device and backend with the VU pre-pass") == 0);
    /* the tests' twins: off by default, not applicable without the
       pre-pass, the twins remembered across */
    ico_attempt_decide_flat0(0, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_flat0_reach(0);
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_NA);
    ico_attempt_flat0_reach(1);
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_OFF_DEFAULT);
    /* the capture row counts the slow-scene dumps alone */
    ico_attempt_reset();
    ico_attempt_decide_capture();
    ico_attempt_fire(ICO_ATT_CAPTURE_TWO_STEP, 1);
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: slow-scene capture in two steps on 1 slow-scene captures (0 "
                     "grown here)"));
    ico_attempt_reset();
}

/* the start line in parts (ico_attempt_start_part): every row's reason
   600 bytes long, so the rows outgrow the window's 4 KB buffer: each part
   fits, holds whole rows from where the last stopped, and every row comes
   once in order; one row past a whole small buffer is cut and passed */
static void test_start_parts(void)
{
    static char why[601];
    char b[4096];

    memset(why, 'w', 600);
    why[600] = '\0';
    ico_attempt_reset();
    for (int i = 0; i < ICO_ATT_COUNT; i++) {
        ico_attempt_state(i, ICO_ATT_ON, why);
    }
    int row = 0, parts = 0, seen = 0;
    while (row < ICO_ATT_COUNT && parts < 100) {
        const int from = row;
        const size_t n = ico_attempt_start_part(b, sizeof b, &row);
        CHECK(n + 1u <= sizeof b && n == strlen(b) && n > 0 && b[n - 1u] == ')');
        CHECK(row > from);
        CHECK(starts(b, parts == 0 ? "attempts at the start: "
                                   : "attempts at the start "
                                     "(continued): "));
        for (int i = from; i < row; i++) {
            CHECK(strstr(b, ico_attempt_name(i)) != NULL);
        }
        seen += row - from;
        parts++;
    }
    /* 25 rows of over 600 bytes: more than one part */
    CHECK(row == ICO_ATT_COUNT && seen == ICO_ATT_COUNT && parts >= 2);
    /* the removed row among every other row at a 600-byte reason (longer
       than any decider's words): its start words come whole in one part,
       each part within the window's 4 KB buffer */
    ico_attempt_reset();
    for (int i = 0; i < ICO_ATT_COUNT; i++) {
        if (i != ICO_ATT_ADRENO_B) {
            ico_attempt_state(i, ICO_ATT_ON, why);
        }
    }
    row = 0;
    parts = 0;
    int removedSeen = 0;
    while (row < ICO_ATT_COUNT && parts < 100) {
        const int from = row;
        const size_t n = ico_attempt_start_part(b, sizeof b, &row);
        CHECK(n + 1u <= sizeof b && n == strlen(b) && row > from);
        if (strstr(b, "Qualcomm queue at high priority removed (taken off, issues 215 and 123)") !=
            NULL) {
            removedSeen++;
        }
        parts++;
    }
    CHECK(row == ICO_ATT_COUNT && removedSeen == 1);
    /* a row over a whole buffer: cut there, the next part from the row after */
    char s[64];
    row = 0;
    size_t n = ico_attempt_start_part(s, sizeof s, &row);
    CHECK(row == 1 && n == sizeof s - 1u && n == strlen(s));
    n = ico_attempt_start_part(s, sizeof s, &row);
    CHECK(row == 2 && starts(s, "attempts at the start (continued):"));
    /* past the rows, or no row: nothing */
    row = ICO_ATT_COUNT;
    CHECK(ico_attempt_start_part(s, sizeof s, &row) == 0 && s[0] == '\0');
    CHECK(ico_attempt_start_part(s, sizeof s, NULL) == 0);
    ico_attempt_reset();
}

/* the pins let go at one picture a tick (pin_release.h): not applicable off
   Android, under the few-cores rule and with nothing pinned, each with its
   words; on under the rule for three or more faster cores; its two counts in
   the block line, taken per block */
static void test_pin_release(void)
{
    char b[4096];

    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_PIN_RELEASE) == ICO_ATT_NOT_SET);
    ico_attempt_decide_pin_release(0, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PIN_RELEASE) == ICO_ATT_NA &&
          strcmp(ico_attempt_why_of(ICO_ATT_PIN_RELEASE), "Android's core placement only") == 0);
    ico_attempt_decide_pin_release(1, 2);
    CHECK(ico_attempt_state_of(ICO_ATT_PIN_RELEASE) == ICO_ATT_NA &&
          strcmp(ico_attempt_why_of(ICO_ATT_PIN_RELEASE),
                 "one or two cores faster than the slowest: the few-cores rule keeps its "
                 "pins") == 0);
    ico_attempt_decide_pin_release(1, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PIN_RELEASE) == ICO_ATT_NA &&
          strcmp(ico_attempt_why_of(ICO_ATT_PIN_RELEASE),
                 "nothing pinned here (the cores of one speed, or [dev] big_cores = false)") == 0);
    ico_attempt_decide_pin_release(1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PIN_RELEASE) == ICO_ATT_ON);
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "; pins let go at one picture a tick on (a second at one picture a tick with "
                    "a picture's CPU raised lets the render and game threads run on any faster "
                    "core until 10 s pass without one)") != NULL);
    ico_attempt_fire(ICO_ATT_PIN_RELEASE, 3u);
    ico_attempt_fire2(ICO_ATT_PIN_RELEASE, 2u);
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: pins let go at one picture a tick on 3 times let go (2 taken "
                     "again)"));
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: pins let go at one picture a tick on 0 times let go (0 taken "
                     "again)"));
    ico_attempt_reset();
}

/* the window's ledger buffer (window_perf_rank.c: static char text[4096]):
   the start line is printed in parts of it (ico_attempt_start_part), the
   block line into it */
#define WINDOW_LEDGER_TEXT 4096

/* the part of the start line holding row id, in a buffer of cap bytes */
static int start_part_of(int id, char *b, size_t cap)
{
    int row = 0, parts = 0;
    while (row < ICO_ATT_COUNT && parts < 8) {
        const int from = row;
        (void)ico_attempt_start_part(b, cap, &row);
        if (id >= from && id < row) {
            return 1;
        }
        parts++;
    }
    b[0] = '\0';
    return 0;
}

/* the two-pass blend in one pass (rd_onepass.c): each state with its words,
   the start words in the window's parts, the two counts in the block line,
   taken per block */
static void test_onepass(void)
{
    static char b[WINDOW_LEDGER_TEXT];

    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_NOT_SET);
    CHECK(strcmp(ico_attempt_name(ICO_ATT_ONEPASS), "the two-pass blend in one pass") == 0);
    CHECK(ico_attempt_decide_onepass(1, 0, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_OFF_DEFAULT &&
          strcmp(ico_attempt_why_of(ICO_ATT_ONEPASS), "ICO_RD_ONEPASS=0: the tests' switch") == 0);
    CHECK(ico_attempt_decide_onepass(0, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_NA &&
          strcmp(ico_attempt_why_of(ICO_ATT_ONEPASS),
                 "dual-source blending: no two-pass blend here") == 0);
    CHECK(ico_attempt_decide_onepass(1, -1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_onepass(1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_OFF_DEFAULT &&
          strcmp(ico_attempt_why_of(ICO_ATT_ONEPASS), "its self-test not run yet") == 0);
    CHECK(ico_attempt_decide_onepass(1, -1, 1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_REFUSED);
    CHECK(start_part_of(ICO_ATT_ONEPASS, b, sizeof b));
    CHECK(strstr(b, "the two-pass blend in one pass refused by its self-test (the one-pass "
                    "pairs' bytes differ from the two passes' on this device)") != NULL);
    CHECK(ico_attempt_decide_onepass(1, 1, 0, 1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_ONEPASS), "ICO_RD_ONEPASS=1, self-test passed") == 0);
    CHECK(ico_attempt_decide_onepass(1, -1, 1, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_ON);
    CHECK(start_part_of(ICO_ATT_ONEPASS, b, sizeof b));
    CHECK(strstr(b, "the two-pass blend in one pass on (self-test passed: the same bytes as the "
                    "two passes)") != NULL);
    ico_attempt_fire(ICO_ATT_ONEPASS, 811u);
    ico_attempt_fire2(ICO_ATT_ONEPASS, 40u);
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: the two-pass blend in one pass on 811 pairs (40 kept two-pass)"));
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(text_is(b, "attempts: the two-pass blend in one pass on 0 pairs (0 kept two-pass)"));
    ico_attempt_reset();
}

int main(void)
{
    test_pin_release();
    test_onepass();
    test_classes();
    test_robust_line();
    test_skin_skip();
    test_flat0_capture();
    test_health();
    test_start_parts();
    char b[8192];

    ico_attempt_reset();
    /* nothing reported: the start line names every row */
    (void)ico_attempt_start_text(b, sizeof(b));
    CHECK(starts(b, "attempts at the start: few-cores rule not reported; pipeline set on every "
                    "core not reported; "));
    CHECK(strstr(b, "; index compaction not reported") != NULL);
    CHECK(strstr(b, "; blend constant not in this build") != NULL);
    CHECK(strstr(b, "; the glow's paste by its own entry not reported") != NULL);
    CHECK(strstr(b, "; the glow chain in three passes not in this build") != NULL);
    CHECK(strstr(b, "; the shadow volumes by the stencil-only entry not reported") != NULL);
    CHECK(strstr(b, "; subpass read not reported") != NULL);
    /* the high queue priority's row: removed from the start, with its words */
    CHECK(
        strstr(b, "; Qualcomm queue at high priority removed (taken off, issues 215 and 123); ") !=
        NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_SUBPASS) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_state_of(ICO_ATT_BLEND_CONST) == ICO_ATT_NOT_BUILT);
    /* and the block line is empty, with no stalls asked */
    CHECK(ico_attempt_block_text(b, sizeof(b), -1) == 0 && b[0] == '\0');
    CHECK(ico_attempt_block_text(b, sizeof(b), 0) > 0 &&
          text_is(b, "attempts: stalls 0 in the block"));

    /* a state with its why; the serial moves once */
    const uint32_t s0 = ico_attempt_serial();
    ico_attempt_state(ICO_ATT_FEW_CORES, ICO_ATT_ON, "2 cores faster than the slowest");
    CHECK(ico_attempt_serial() == s0 + 1u);
    ico_attempt_state(ICO_ATT_FEW_CORES, ICO_ATT_ON, "2 cores faster than the slowest");
    CHECK(ico_attempt_serial() == s0 + 1u); /* the same state: no change */
    ico_attempt_state(ICO_ATT_FLAT0, ICO_ATT_ON, "every device");
    ico_attempt_state(ICO_ATT_CAPTURE_TWO_STEP, ICO_ATT_ON, NULL);
    ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_OFF_DEFAULT, "a phone rc turns it on");
    CHECK(ico_attempt_serial() == s0 + 4u);
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_ON);
    CHECK(ico_attempt_state_of(-1) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_state_of(ICO_ATT_COUNT) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_name(ICO_ATT_COUNT) == NULL);
    CHECK(strcmp(ico_attempt_name(ICO_ATT_DATE_ONE), "one DATE set-up") == 0);
    (void)ico_attempt_start_text(b, sizeof(b));
    CHECK(starts(
        b, "attempts at the start: few-cores rule on (2 cores faster than the slowest); "
           "pipeline set on every core not reported; slow-scene capture in two steps on; flat "
           "outputs at the provoking corner on (every device); one DATE set-up not "
           "reported; empty composites skipped not reported; no-volume break off by "
           "default (a phone rc turns it on); "));

    /* the block's counts; out-of-range ids ignored */
    ico_attempt_fire(ICO_ATT_FEW_CORES, 3);
    ico_attempt_fire(ICO_ATT_CAPTURE_TWO_STEP, 1);
    ico_attempt_fire2(ICO_ATT_CAPTURE_TWO_STEP, 0);
    ico_attempt_fire(-1, 5);
    ico_attempt_fire(ICO_ATT_COUNT, 5);
    ico_attempt_fire2(ICO_ATT_COUNT, 5);
    ico_attempt_fire(ICO_ATT_DATE_ONE, 9); /* counted, not printed: not reported */
    (void)ico_attempt_block_text(b, sizeof(b), -1);
    CHECK(text_is(b, "attempts: few-cores rule on 3 threads moved; slow-scene capture in two "
                     "steps on 1 slow-scene captures (0 grown here); flat outputs at the provoking "
                     "corner on (static); no-volume break off by default 0 pictures"));
    /* the next block starts from 0 */
    ico_attempt_fire(ICO_ATT_FEW_CORES, 2);
    (void)ico_attempt_block_text(b, sizeof(b), 4);
    CHECK(starts(b, "attempts: few-cores rule on 2 threads moved; slow-scene capture in two "
                    "steps on 0 slow-scene captures (0 grown here); "));
    CHECK(strstr(b, "; stalls 4 in the block") != NULL && strstr(b, "(the Adreno attempt") == NULL);
    /* reported late: counts since the last block (9 before, 1 since) */
    ico_attempt_state(ICO_ATT_DATE_ONE, ICO_ATT_ON, "every device");
    ico_attempt_fire(ICO_ATT_DATE_ONE, 1);
    ico_attempt_fire2(ICO_ATT_DATE_ONE, 2);
    (void)ico_attempt_block_text(b, sizeof(b), -1);
    CHECK(strstr(b, "; one DATE set-up on 1 shared (2 retaken by the guard)") != NULL);

    /* the Adreno row: the stalls clause says whether it is on */
    ico_attempt_decide_pass_objects(NULL);
    (void)ico_attempt_block_text(b, sizeof(b), 2);
    CHECK(strstr(b, "; stalls 2 in the block (the Adreno attempt off)") != NULL);
    /* the block's stalls on the Adreno row's second count, whatever its
       state (the 4 of the block before were that block's) */
    CHECK(strstr(b, "Qualcomm passes as VkRenderPass objects not applicable here 0 passes (2 "
                    "stalls in the block)") != NULL);
    ico_attempt_decide_pass_objects("Qualcomm's own driver");
    ico_attempt_fire(ICO_ATT_ADRENO_A, 120);
    (void)ico_attempt_block_text(b, sizeof(b), 1);
    CHECK(strstr(b, "; stalls 1 in the block (the Adreno attempt on)") != NULL);
    CHECK(strstr(b, "Qualcomm passes as VkRenderPass objects on 120 passes (1 stalls in the "
                    "block)") != NULL);
    CHECK(strstr(b, "Qualcomm queue at high priority") == NULL); /* removed: left out */

    /* the high queue priority's row (v0.11.7's second attempt at issue
       123, removed in v0.11.8): "removed" with its words at the start,
       never in a block; the stalls clause names the Adreno row alone and
       the stalls are counted on it, one block at a time */
    CHECK(ico_attempt_state_of(ICO_ATT_ADRENO_B) == ICO_ATT_REMOVED);
    CHECK(strcmp(ico_attempt_state_word(ICO_ATT_REMOVED), "removed") == 0);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_ADRENO_B), ICO_ATT_QPRIO_REMOVED_WHY) == 0);
    (void)ico_attempt_start_text(b, sizeof(b));
    CHECK(
        strstr(b, "; Qualcomm queue at high priority removed (taken off, issues 215 and 123); ") !=
        NULL);
    (void)ico_attempt_block_text(b, sizeof(b), 3);
    CHECK(strstr(b, "Qualcomm passes as VkRenderPass objects on 0 passes (3 stalls in the "
                    "block)") != NULL);
    CHECK(strstr(b, "; stalls 3 in the block (the Adreno attempt on)") != NULL);
    CHECK(strstr(b, "Qualcomm queue at high priority") == NULL);
    CHECK(strstr(b, "priority") == NULL);
    /* the next block counts its own stalls only; none asked (-1): 0 */
    (void)ico_attempt_block_text(b, sizeof(b), -1);
    CHECK(strstr(b, "Qualcomm passes as VkRenderPass objects on 0 passes (0 stalls in the "
                    "block)") != NULL);
    CHECK(strstr(b, "; stalls") == NULL);
    ico_attempt_decide_pass_objects(NULL);
    (void)ico_attempt_block_text(b, sizeof(b), 0);
    CHECK(strstr(b, "; stalls 0 in the block (the Adreno attempt off)") != NULL);
    CHECK(strstr(b, "Qualcomm passes as VkRenderPass objects not applicable here 0 passes (0 "
                    "stalls in the block)") != NULL);
    /* a reset keeps the row removed, with its words */
    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_ADRENO_B) == ICO_ATT_REMOVED);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_ADRENO_B), ICO_ATT_QPRIO_REMOVED_WHY) == 0);
    CHECK(strcmp(ico_attempt_state_word(ICO_ATT_REFUSED), "refused by its self-test") == 0);
    CHECK(strcmp(ico_attempt_state_word(ICO_ATT_NA), "not applicable here") == 0);

    /* a short buffer: cut, terminated, the length what it holds */
    {
        char t[24];
        const size_t n = ico_attempt_block_text(t, sizeof(t), 1);
        CHECK(n == strlen(t) && n == sizeof(t) - 1u);
        const size_t m = ico_attempt_start_text(t, sizeof(t));
        CHECK(m == strlen(t) && m == sizeof(t) - 1u);
        CHECK(ico_attempt_start_text(t, 0) == 0);
    }

    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_FEW_CORES) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_block_text(b, sizeof(b), -1) == 0);

    if (failures) {
        printf("attempt_ledger_test: %d failures\n", failures);
        return 1;
    }
    printf("attempt_ledger_test: ok\n");
    return 0;
}
