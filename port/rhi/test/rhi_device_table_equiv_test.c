/* rhi_device_table_equiv_test.c: the device table changes no behaviour
 * (port/rhi/rhi_device_table.h; docs/port/DEVICE_TABLE.md).
 *
 * rhi_device_table_ref.inc keeps a copy of every rule the table replaces as
 * it read at int/v0117 df714b3f4.  This test evaluates the table on every
 * identity of a matrix built over every device that sent a log
 * (build-host/tmp/v0116/design/DEVICES.md) and the edge values around each
 * rule's bound (the Vulkan versions around 1.3.215, the driver ids 0, 8, 14,
 * 18 and the rest, the driver texts that do and do not name Mesa, Turnip or
 * the B-Series in any case, the Adreno device ids beside the 740's), on
 * every backend and OS, MoltenVK or not, a driver package or not, and
 * compares each choice, as the call sites will compose it with their run
 * facts, with the copy's answer, value and words byte for byte.  On a
 * reduced set (the logged devices and the test devices: lavapipe, llvmpipe
 * on OpenGL, WARP) it also runs every combination of the tests'
 * environment fakes and of the profile's run inputs.
 *
 * The copies' fidelity: the pure files the copies come from (rhi_pass_row.c,
 * rhi_driver_version.c, rhi_tile_rule.c, rhi_qprio.c, memory_floor.c,
 * present_fallback.c) are linked in and compared with their copy over the
 * whole matrix; the copies of statics inside large files are compared with
 * their source lines by the DT1 hand-back's diff.
 *
 * Also: every row is in force on some identity, every override link
 * decides some identity, and no identity has two matching rows that differ
 * without a link. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory_floor.h"
#include "present_fallback.h"
#include "rhi_device_table.h"
#include "rhi_driver_version.h"
#include "rhi_pass_row.h"
#include "rhi_qprio.h"
#include "rhi_tile_rule.h"

#include "rhi_device_table_ref.inc"

static unsigned long s_checks, s_failures;

static void describe(const RefIn *in, char *out, size_t cap)
{
    snprintf(out, cap,
             "backend %d os %d mvk %d custom %d vendor 0x%X device 0x%X id %u api 0x%X name '%s' "
             "text '%s' fakes vendor=%s passrow=%s qprio=%s adreno=%s serial=%s noprepass=%s "
             "compactarm=%s",
             in->backend, in->os, in->moltenvk, in->customDriver, (unsigned)in->vendor,
             (unsigned)in->device, (unsigned)in->driverId, (unsigned)in->api,
             in->name ? in->name : "(null)", in->text ? in->text : "(null)",
             in->fakeVendor ? in->fakeVendor : "-", in->fakePassRow ? in->fakePassRow : "-",
             in->fakeQprio ? in->fakeQprio : "-", in->fakeAdreno ? in->fakeAdreno : "-",
             in->fakeSerial ? in->fakeSerial : "-", in->fakeNoPrepass ? in->fakeNoPrepass : "-",
             in->compactArm ? in->compactArm : "-");
}

static void fail(const RefIn *in, const char *what, long oldV, long newV)
{
    s_failures++;
    if (s_failures <= 40) {
        char d[768];
        describe(in, d, sizeof d);
        printf("FAIL %s: old %ld, table %ld (%s)\n", what, oldV, newV, d);
    }
}

#define SAME(in, what, oldV, newV)                                                                 \
    do {                                                                                           \
        const long o_ = (long)(oldV), n_ = (long)(newV);                                           \
        s_checks++;                                                                                \
        if (o_ != n_) {                                                                            \
            fail((in), (what), o_, n_);                                                            \
        }                                                                                          \
    } while (0)

static void sameWords(const RefIn *in, const char *what, const char *oldW, const char *newW)
{
    s_checks++;
    if ((oldW == NULL) != (newW == NULL) || (oldW && strcmp(oldW, newW) != 0)) {
        s_failures++;
        if (s_failures <= 40) {
            char d[768];
            describe(in, d, sizeof d);
            printf("FAIL %s words: old '%s', table '%s' (%s)\n", what, oldW ? oldW : "(null)",
                   newW ? newW : "(null)", d);
        }
    }
}

/* ------------------------------------------- the new side, as DT2-DT4 read it */

/* the identity and the fakes a backend hands the table (the Vulkan backend
   reads the fakes; ICO_RD_VU_COMPACT_ARM is the renderer's, any backend) */
static void newEval(const RefIn *in, RhiDeviceChoices *c)
{
    RhiDeviceId id;
    RhiDeviceFakes fk;
    memset(&id, 0, sizeof id);
    memset(&fk, 0, sizeof fk);
    id.backend = (uint8_t)in->backend;
    id.os = (uint8_t)in->os;
    id.moltenvk = in->moltenvk;
    id.customDriver = in->customDriver;
    id.vendorId = in->vendor;
    id.deviceId = in->device;
    id.driverId = in->driverId;
    id.apiVersion = in->api;
    id.deviceName = in->name;
    id.driverText = in->text;
    if (in->backend == RHI_DT_VULKAN) {
        const char *fv = in->fakeVendor;
        if (fv && fv[0]) {
            char *end = NULL;
            const unsigned long v = strtoul(fv, &end, 0);
            if (end != fv && *end == '\0' && v <= 0xFFFFFFFFul) {
                fk.vendorSet = 1;
                fk.vendor = (uint32_t)v;
            }
        }
        fk.passRowQualcomm = in->fakePassRow && strcmp(in->fakePassRow, "qualcomm") == 0;
        fk.queuePriority = rhi_vk_qprio_fake(in->fakeQprio) != RHI_QPRIO_FAKE_NONE;
        fk.adreno = in->fakeAdreno && in->fakeAdreno[0] && in->fakeAdreno[0] != '0';
        fk.serial = in->fakeSerial && in->fakeSerial[0] == '1';
        fk.noPrepass = in->fakeNoPrepass && in->fakeNoPrepass[0] && in->fakeNoPrepass[0] != '0';
    }
    fk.compactArm = in->compactArm && strcmp(in->compactArm, "1") == 0;
    rhi_device_table_eval(&id, &fk, c);
}

/* rd_pipeline.c rd__vu_set_choose read from VU_FALLBACK_PLAIN */
static RefVuSet newVuSet(const RhiDeviceChoices *c)
{
    RefVuSet o;
    o.kind = "extended set (no device-table row, no Turnip driver, no lost start, no [dev] "
             "setting asks for the plain one)";
    o.brief = "";
    o.noBeside = false;
    const int32_t v = c->value[RHI_DC_VU_FALLBACK_PLAIN];
    if (v == RHI_DT_VUSET_TABLE) {
        o.kind = "plain set by the device table";
        o.brief = rhi_device_choice_reason(c, RHI_DC_VU_FALLBACK_PLAIN);
        o.noBeside = true;
    } else if (v == RHI_DT_VUSET_TURNIP) {
        o.kind = "plain set";
        o.brief = rhi_device_choice_reason(c, RHI_DC_VU_FALLBACK_PLAIN);
        o.noBeside = true;
    }
    return o;
}

/* ----------------------------------------------------------- the comparison */

static unsigned long s_rowApplied[RHI_DT_MAX_ROWS];
static unsigned long s_linkUsed[RHI_DT_MAX_ROWS][RHI_DT_MAX_ROWS];
static unsigned long s_conflicts;

static void noteRows(const RefIn *in, const RhiDeviceChoices *c)
{
    size_t n;
    const RhiDeviceRow *rows = rhi_device_table_rows(&n);
    for (size_t r = 0; r < n; r++) {
        s_rowApplied[r] += c->applied[r];
    }
    /* a link decided: the winner and a matched row it beats with another value */
    for (int k = 0; k < RHI_DC_COUNT; k++) {
        const int w = c->row[k];
        if (w < 0 || c->faked[k]) {
            continue;
        }
        for (size_t r = 0; r < n; r++) {
            if ((int)r == w || !c->matched[r]) {
                continue;
            }
            for (uint8_t s = 0; s < rows[r].settingCount; s++) {
                if (rows[r].settings[s].choice == k && rows[r].settings[s].value != c->value[k]) {
                    s_linkUsed[w][r]++;
                }
            }
        }
    }
    if (c->conflictChoice >= 0) {
        s_conflicts++;
        if (s_conflicts <= 10) {
            char d[768];
            describe(in, d, sizeof d);
            printf("FAIL conflict on %s: rows %s and %s differ without an override link (%s)\n",
                   rhi_device_choice_name((RhiDeviceChoice)c->conflictChoice),
                   rows[c->conflictA].id, rows[c->conflictB].id, d);
        }
    }
}

/* the live pure functions against their copies */
static void live(const RefIn *in)
{
    const int windows = in->os == RHI_DT_WINDOWS;
    const int android = in->os == RHI_DT_ANDROID;
    sameWords(in, "live rhi_vk_pass_row",
              rhi_vk_pass_row(in->vendor, in->driverId, in->api, windows),
              ref_pass_row_intel(in->vendor, in->driverId, in->api, windows));
    sameWords(in, "live rhi_vk_pass_row_qualcomm",
              rhi_vk_pass_row_qualcomm(in->vendor, in->driverId, android),
              ref_pass_row_qualcomm(in->vendor, in->driverId, android));
    sameWords(in, "live rhi_vk_qprio_row", rhi_vk_qprio_row(in->vendor, in->driverId, android),
              ref_qprio_row(in->vendor, in->driverId, android));
    SAME(in, "live rhi_driver_is_qualcomm_proprietary",
         rhi_driver_is_qualcomm_proprietary(in->vendor, in->driverId),
         ref_qcom_prop(in->vendor, in->driverId));
    {
        const char *live = rhi_driver_compiler_serial(in->vendor, in->text, 1, 1);
        const char *copy =
            in->vendor == 0x1010u && in->text != NULL && ref_has_word(in->text, "b-series")
                ? "PowerVR B-Series"
                : NULL;
        sameWords(in, "live rhi_driver_compiler_serial", live, copy);
    }
    sameWords(in, "live rhi_tile_vendor_name", rhi_tile_vendor_name(in->vendor),
              ref_tile_vendor_name(in->vendor));
    SAME(in, "live rhi_tile_phone", rhi_tile_phone(android, in->moltenvk, windows),
         ref_tile_phone_rule(android, in->moltenvk, windows));
    SAME(in, "live ico_memory_floor_driver",
         ico_memory_floor_driver(in->vendor, in->driverId, in->text),
         ref_floor_driver(in->vendor, in->driverId, in->text));
    SAME(in, "live ico_present_fallback_driver_eligible",
         ico_present_fallback_driver_eligible(in->vendor, in->driverId, in->text),
         ref_fallback_eligible(in->vendor, in->driverId, in->text));
    SAME(in, "live ico_present_fallback_qualcomm_own",
         ico_present_fallback_qualcomm_own(in->vendor, in->driverId, in->text),
         ref_qualcomm_own(in->vendor, in->driverId, in->text));
    SAME(in, "live ico_present_fallback_start",
         ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, android, 0, in->vendor, in->driverId,
                                    in->text),
         ref_fifo_start(in));
    SAME(in, "live ico_present_fallback_gate",
         ico_present_fallback_gate(0, 1, 1, in->vendor, in->driverId, in->text,
                                   ICO_PRESENT_FALLBACK_AUTO),
         !ref_fallback_eligible(in->vendor, in->driverId, in->text) ? ICO_PRESENT_FALLBACK_GATE_NO
         : ref_qualcomm_own(in->vendor, in->driverId, in->text) ? ICO_PRESENT_FALLBACK_GATE_YES
                                                                : ICO_PRESENT_FALLBACK_GATE_OTHER);
}

/* every choice on one identity; withRun: the profile's run inputs too */
static void compare(const RefIn *in, bool withRun)
{
    RhiDeviceChoices c;
    newEval(in, &c);
    noteRows(in, &c);
    const int32_t *v = c.value;
    const bool vulkan = in->backend == RHI_DT_VULKAN;
    const bool tiler = ref_lim_tiler(in);
    const bool phone = ref_tile_phone(in);

    SAME(in, "TILER", tiler, v[RHI_DC_TILER]);
    SAME(in, "TILE_PHONE", phone, v[RHI_DC_TILE_PHONE]);
    SAME(in, "PROFILE_CLASS", ref_classOf(in), v[RHI_DC_PROFILE_CLASS]);
    SAME(in, "TILE_CLASS", ref_profile_tile_class(in), v[RHI_DC_TILE_CLASS]);
    SAME(in, "DYNAMIC_STATE_OFF", ref_dynamic_tiler_device(in), v[RHI_DC_DYNAMIC_STATE_OFF]);
    SAME(in, "MEMORY_BUDGET_OFF", ref_memory_budget_off(in), v[RHI_DC_MEMORY_BUDGET_OFF]);

    if (vulkan) {
        const char *intelRow, *qcomRow;
        ref_pass_rows(in, &intelRow, &qcomRow);
        SAME(in, "PASS_OBJECTS",
             intelRow  ? RHI_DT_PASS_INTEL
             : qcomRow ? RHI_DT_PASS_QUALCOMM
                       : RHI_DT_PASS_NONE,
             v[RHI_DC_PASS_OBJECTS]);
        sameWords(in, "PASS_OBJECTS", intelRow ? intelRow : qcomRow,
                  rhi_device_choice_reason(&c, RHI_DC_PASS_OBJECTS));
        /* the subpass class with the run's passRowQcom (vk_device.c: the
           Qualcomm row applied), every way it can come out */
        for (int legacy = 0; legacy < 2; legacy++) {
            for (int ov = -1; ov <= 1; ov++) {
                const bool qcomApplied = ref_pass_row_qcom_applied(qcomRow, legacy != 0, ov);
                const int t = v[RHI_DC_SUBPASS_CLASS];
                const int composed = t == RHI_DT_SUBPASS_INTEL ? RHI_DT_SUBPASS_INTEL
                                     : qcomApplied             ? RHI_DT_SUBPASS_QCOM
                                                               : t;
                SAME(in, "SUBPASS_CLASS", ref_subpass_class(in, qcomApplied), composed);
            }
        }
        const char *q = ref_qprio(in, rhi_vk_qprio_fake(in->fakeQprio));
        SAME(in, "QUEUE_PRIORITY_HIGH", q != NULL, v[RHI_DC_QUEUE_PRIORITY_HIGH]);
        sameWords(in, "QUEUE_PRIORITY_HIGH", q,
                  rhi_device_choice_reason(&c, RHI_DC_QUEUE_PRIORITY_HIGH));
    } else {
        SAME(in, "PASS_OBJECTS off Vulkan", RHI_DT_PASS_NONE, v[RHI_DC_PASS_OBJECTS]);
        SAME(in, "QUEUE_PRIORITY_HIGH off Vulkan", 0, v[RHI_DC_QUEUE_PRIORITY_HIGH]);
    }
    SAME(in, "ATTACHMENT_READ_OFF", ref_attachment_read_off(in), v[RHI_DC_ATTACHMENT_READ_OFF]);
    {
        const RefVuPlain p = ref_vu_plain_only(in);
        SAME(in, "VU_FALLBACK_PLAIN table", p.plain != NULL,
             v[RHI_DC_VU_FALLBACK_PLAIN] == RHI_DT_VUSET_TABLE);
        sameWords(in, "CAREFUL_PIPELINES", p.careful,
                  v[RHI_DC_CAREFUL_PIPELINES]
                      ? rhi_device_choice_reason(&c, RHI_DC_CAREFUL_PIPELINES)
                      : NULL);
        sameWords(in, "VU_PREPASS_OFF", p.noPrepass,
                  v[RHI_DC_VU_PREPASS_OFF] ? rhi_device_choice_reason(&c, RHI_DC_VU_PREPASS_OFF)
                                           : NULL);
        SAME(in, "COMPUTE_RECORD_OFF (no row)", 0, v[RHI_DC_COMPUTE_RECORD_OFF]);
        const RefVuSet o = ref_vu_set(p.plain, ref_driver_is_turnip(in));
        const RefVuSet n = newVuSet(&c);
        sameWords(in, "vu_set kind", o.kind, n.kind);
        sameWords(in, "vu_set brief", o.brief, n.brief);
        SAME(in, "vu_set noBeside", o.noBeside, n.noBeside);
    }
    {
        const char *s = ref_compiler_serial(in);
        SAME(in, "COMPILER_SERIAL", s != NULL, v[RHI_DC_COMPILER_SERIAL]);
        sameWords(in, "COMPILER_SERIAL", s,
                  v[RHI_DC_COMPILER_SERIAL] ? rhi_device_choice_reason(&c, RHI_DC_COMPILER_SERIAL)
                                            : NULL);
    }
    SAME(in, "PIPE_ROBUST_ASK", ref_pipe_robust_ask(in), v[RHI_DC_PIPE_ROBUST_ASK]);
    SAME(in, "FIFO_FROM_START", ref_fifo_start(in), v[RHI_DC_FIFO_FROM_START]);
    SAME(in, "FALLBACK_ELIGIBLE", ref_fallback_eligible(in->vendor, in->driverId, in->text),
         v[RHI_DC_FALLBACK_ELIGIBLE]);
    SAME(in, "FALLBACK_SWITCH",
         ref_qualcomm_own(in->vendor, in->driverId, in->text) ? RHI_DT_SWITCH_YES
                                                              : RHI_DT_SWITCH_OTHER,
         v[RHI_DC_FALLBACK_SWITCH]);
    SAME(in, "MEMORY_FLOOR_CLASS", ref_floor_driver(in->vendor, in->driverId, in->text),
         v[RHI_DC_MEMORY_FLOOR_CLASS]);
    SAME(in, "TURNIP_NOTE", ref_turnip_note(in), v[RHI_DC_TURNIP_NOTE]);
    SAME(in, "SKIN_CHECK_DEFAULT", ref_profile_adreno(in), v[RHI_DC_SKIN_CHECK_DEFAULT]);
    SAME(in, "SKIN_AS_BEFORE", phone, v[RHI_DC_SKIN_AS_BEFORE]);
    {
        const int cls = ref_classOf(in);
        const bool tile = cls != RHI_DT_CLASS_DESKTOP && cls != RHI_DT_CLASS_D3D12 &&
                          cls != RHI_DT_CLASS_OPENGL && cls != RHI_DT_CLASS_D3D11;
        SAME(in, "BLUR_FOLD_OFF", cls == RHI_DT_CLASS_MALI, v[RHI_DC_BLUR_FOLD_OFF]);
        SAME(in, "TILE_COLUMNS", tile, v[RHI_DC_TILE_COLUMNS]);
    }
    {
        const int tc = ref_profile_tile_class(in);
        for (int passArea = 0; passArea < 2; passArea++) {
            const RefLamps o = ref_lamps(tc, phone, vulkan, passArea != 0);
            SAME(in, "LAMPS_MS", o.ms, v[RHI_DC_LAMPS_MS] && vulkan);
            SAME(in, "LAMPS_FILLPASTE", o.fillpaste, v[RHI_DC_LAMPS_FILLPASTE] && vulkan);
            SAME(in, "LAMPS_AREA", o.area, v[RHI_DC_LAMPS_AREA] && vulkan && passArea);
        }
        SAME(in, "FILL11_CLASS", ref_fill11_class(tc, phone), v[RHI_DC_FILL11_CLASS]);
        SAME(in, "SHADOW_TILE_CLASS", tc != 0, v[RHI_DC_SHADOW_TILE_CLASS] != 0);
    }
    SAME(in, "STENCIL_CUT", !ref_profile_adreno(in) && ref_profile_powervr(in),
         v[RHI_DC_STENCIL_CUT]);
    {
        const bool powervr = ref_profile_powervr(in);
        const bool measured = powervr && ref_vu_class_measured(in->name);
        SAME(in, "VU_COMPUTE_CLASS class", powervr,
             v[RHI_DC_VU_COMPUTE_CLASS] != RHI_DT_VUCOMP_NONE);
        SAME(in, "VU_COMPUTE_CLASS measured", measured,
             v[RHI_DC_VU_COMPUTE_CLASS] == RHI_DT_VUCOMP_MEASURED);
    }
    SAME(in, "NO_VOLUME_CLASS", ref_no_volume_base(in), v[RHI_DC_NO_VOLUME_CLASS]);
    SAME(in, "NO_VOLUME_BREAK", ref_no_volume_break(in), v[RHI_DC_NO_VOLUME_BREAK]);
    {
        const int cls = ref_cmp_class_of(in);
        SAME(in, "COMPACTION_CLASS", cls, v[RHI_DC_COMPACTION_CLASS]);
        SAME(in, "COMPACTION_DEFAULT", ref_cmp_default(cls), v[RHI_DC_COMPACTION_DEFAULT]);
    }
    SAME(in, "MSAA_MAX4", tiler, v[RHI_DC_MSAA_MAX4]);
    SAME(in, "SCENE_CAP_4096", tiler, v[RHI_DC_SCENE_CAP_4096]);
    SAME(in, "MSAA_BUDGET", tiler, v[RHI_DC_MSAA_BUDGET]);
    SAME(in, "SHOT_LATE", tiler, v[RHI_DC_SHOT_LATE]);
    SAME(in, "LATE_PASS_TILER", tiler && vulkan, v[RHI_DC_LATE_PASS_TILER]);
    SAME(in, "UBER_AUTO", tiler, v[RHI_DC_UBER_AUTO]);
    SAME(in, "FOG_ORDER_TILER", tiler, v[RHI_DC_FOG_ORDER_TILER]);
    SAME(in, "CULL_FRUSTUM", tiler, v[RHI_DC_CULL_FRUSTUM]);

    if (withRun) {
        for (int bits = 0; bits < 16; bits++) {
            RefRun run = {(bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0};
            const RefProfile o = ref_profile_init(in, &run);
            /* rd__profile_init read from the table */
            const bool tile = v[RHI_DC_TILE_COLUMNS] != 0;
            const bool desk = v[RHI_DC_PROFILE_CLASS] == RHI_DT_CLASS_DESKTOP && run.localRead;
            const bool desktopLocal = desk && run.windowed;
            const bool local = (tile || desktopLocal) && run.localRead;
            SAME(in, "profile tiler", o.tiler, v[RHI_DC_TILER]);
            SAME(in, "profile desktopLocal", o.desktopLocal, desktopLocal);
            SAME(in, "profile fogLocal", o.fogLocal, local && !run.localReadSubpass);
            SAME(in, "profile dateStencil", o.dateStencil, local);
            SAME(in, "profile blurFold", o.blurFold, !v[RHI_DC_BLUR_FOLD_OFF]);
            SAME(in, "profile glowFold", o.glowFold, !tile);
            SAME(in, "profile uintViewWork", o.uintViewWork, !tile && run.uintView);
            SAME(in, "profile uintViewScene", o.uintViewScene, !tile && run.uintView);
            SAME(in, "profile fxDstLocal", o.fxDstLocal, local && !run.localReadSubpass);
            SAME(in, "profile auraBox", o.auraBox, tile);
            SAME(in, "profile passArea", o.passArea, tile);
            SAME(in, "profile passCut", o.passCut, tile);
            SAME(in, "profile shadowMerge", o.shadowMerge, tile);
            SAME(in, "profile sheetBake2x", o.sheetBake2x, tile);
        }
    }
}

/* ------------------------------------------------------------- the matrix */

#define API(a, b, c) (((uint32_t)(a) << 22) | ((uint32_t)(b) << 12) | (uint32_t)(c))

static const uint32_t kApis[] = {API(1, 0, 61),  API(1, 1, 177), API(1, 3, 214),
                                 API(1, 3, 215), API(1, 3, 216), API(1, 4, 344)};
static const uint32_t kDriverIds[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 13, 14, 18};
static const char *const kTexts[] = {
    NULL,
    "",
    "device 5143:43050a01; driver Qualcomm Technologies Inc. Adreno Vulkan Driver Driver Build: "
    "bad34dcf83, I1064ae0a1f, 1764326394 Date: 11/28/25 Compiler Version: E031.41.03.64 Driver "
    "Branch:, version 512.676.1",
    "device 5143:43050a01; driver turnip Mesa driver Mesa 26.3.0-devel (git-bbdd688), version "
    "26.3.0, Mesa 26.3.0-devel",
    "device 5143:43050a01; driver PurpleVK public driver, version 26.3.0",
    "Mesa 25.3.0",
    "TURNIP",
    "device 1010:36104046; driver PowerVR B-Series Vulkan Driver 1.15@6133110, version 1.15.0",
    "pOwErVr b-SeRiEs",
    "device 1010:71061212; driver PowerVR D-Series Vulkan Driver 26.1@7036548, version 1.693.3716",
    "device 10de:2204; driver NVIDIA 610.88, version 610.88",
    "device 8086:1912; driver Intel Corporation Intel driver, version 101.2125",
    "device 10005:0000; driver llvmpipe Mesa 25.0.7-2 (LLVM 19.1.7), version 0.0.1",
    "mEsA",
};

typedef struct VendorSet {
    uint32_t vendor;
    const uint32_t *devices;
    size_t deviceCount;
    const char *const *names;
    size_t nameCount;
    bool allApis; /* the Vulkan version is read for this vendor (Intel's row) */
} VendorSet;

static const uint32_t kQcomDevices[] = {0x43050A01u, 0x43050A00u, 0x43050B00u,
                                        0x43051401u, 0x07030001u, 0u};
static const uint32_t kOneDevice[] = {0u};
static const uint32_t kAdrenoLikeDevice[] = {0x43050A01u, 0u}; /* the id on another vendor */
static const char *const kQcomNames[] = {"Adreno (TM) 740", NULL, ""};
static const char *const kArmNames[] = {"Mali-G52 MC2", "Mali-G52", "Mali-G57 MC2",
                                        "Mali-G31 MP2", NULL,       ""};
static const char *const kImgNames[] = {"PowerVR D-Series DXT-48-1536 MC1",
                                        "PowerVR D-Series DXT-48-1536", "PowerVR BXM-8-256", NULL,
                                        ""};
static const char *const kAppleNames[] = {"Apple M2 Pro", "Apple A10 GPU", NULL, ""};
static const char *const kIntelNames[] = {"Intel(R) HD Graphics 530", NULL, ""};
static const char *const kNvidiaNames[] = {"NVIDIA GeForce RTX 3090", "Mali-G52 MC2", NULL};
static const char *const kAmdNames[] = {"AMD Radeon RX 7800 XT (RADV NAVI32)", NULL};
static const char *const kOtherNames[] = {"llvmpipe (LLVM 19.1.7, 256 bits)",
                                          "PowerVR D-Series DXT-48-1536 MC1", NULL};

#define NN(a) (sizeof(a) / sizeof((a)[0]))

static const VendorSet kVendors[] = {
    {0x5143u, kQcomDevices, NN(kQcomDevices), kQcomNames, NN(kQcomNames), false},
    {0x13B5u, kOneDevice, 1, kArmNames, NN(kArmNames), false},
    {0x1010u, kOneDevice, 1, kImgNames, NN(kImgNames), false},
    {0x144Du, kOneDevice, 1, kOtherNames, NN(kOtherNames), false},
    {0x106Bu, kOneDevice, 1, kAppleNames, NN(kAppleNames), false},
    {0x14E4u, kOneDevice, 1, kOtherNames, NN(kOtherNames), false},
    {0x8086u, kAdrenoLikeDevice, NN(kAdrenoLikeDevice), kIntelNames, NN(kIntelNames), true},
    {0x10DEu, kOneDevice, 1, kNvidiaNames, NN(kNvidiaNames), false},
    {0x1002u, kOneDevice, 1, kAmdNames, NN(kAmdNames), false},
    {0x10005u, kOneDevice, 1, kOtherNames, NN(kOtherNames), false},
    {0x1414u, kOneDevice, 1, kOtherNames, NN(kOtherNames), false},
    {0u, kAdrenoLikeDevice, NN(kAdrenoLikeDevice), kOtherNames, NN(kOtherNames), true},
};

static const int kBackends[] = {RHI_DT_VULKAN, RHI_DT_D3D12, RHI_DT_OPENGL, RHI_DT_D3D11};

static unsigned long matrix(void)
{
    unsigned long n = 0;
    for (size_t vi = 0; vi < NN(kVendors); vi++) {
        const VendorSet *vs = &kVendors[vi];
        const size_t apis = vs->allApis ? NN(kApis) : 2u;
        for (size_t di = 0; di < vs->deviceCount; di++) {
            for (size_t ni = 0; ni < vs->nameCount; ni++) {
                for (size_t ii = 0; ii < NN(kDriverIds); ii++) {
                    for (size_t ti = 0; ti < NN(kTexts); ti++) {
                        for (size_t ai = 0; ai < apis; ai++) {
                            for (int os = 0; os < RHI_DT_OS_COUNT; os++) {
                                for (size_t bi = 0; bi < NN(kBackends); bi++) {
                                    for (int mvk = 0; mvk < 2; mvk++) {
                                        for (int cust = 0; cust < (os == RHI_DT_ANDROID ? 2 : 1);
                                             cust++) {
                                            RefIn in;
                                            memset(&in, 0, sizeof in);
                                            in.backend = kBackends[bi];
                                            in.os = os;
                                            in.moltenvk = mvk != 0;
                                            in.customDriver = cust != 0;
                                            in.vendor = vs->vendor;
                                            in.device = vs->devices[di];
                                            in.driverId = kDriverIds[ii];
                                            in.api = vs->allApis ? kApis[ai]
                                                     : ai == 0   ? API(1, 1, 177)
                                                                 : API(1, 4, 344);
                                            in.name = vs->names[ni];
                                            in.text = kTexts[ti];
                                            compare(&in, false);
                                            live(&in);
                                            n++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return n;
}

/* the logged devices (DEVICES.md) and the test devices */
typedef struct Known {
    const char *tag;
    int backend, os;
    bool moltenvk;
    uint32_t vendor, device, driverId, api;
    const char *name, *text;
} Known;

static const Known kKnown[] = {
    {"M1", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x13B5u, 0x74021000u, 9, API(1, 1, 177),
     "Mali-G52 MC2",
     "device 13b5:74021000; driver Mali-G52 MC2 "
     "v1.r32p1-01eac0.2a893c04ca0026c2e6802dbe7d7af5c5, version 32.1.0"},
    {"M2", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x13B5u, 0x72120000u, 9, API(1, 3, 213),
     "Mali-G52", "device 13b5:72120000; driver Mali-G52 v1.r38p1, version 38.1.0"},
    {"Q1", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x5143u, 0x43050A01u, 8, API(1, 3, 128),
     "Adreno (TM) 740", NULL},
    {"Q2", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x5143u, 0x43050A01u, 8, API(1, 3, 128),
     "Adreno (TM) 740",
     "device 5143:43050a01; driver Qualcomm Technologies Inc. Adreno Vulkan "
     "Driver, version 512.676.53"},
    {"Q3", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x5143u, 0x43030B00u, 8, API(1, 3, 284),
     "Adreno (TM) 710",
     "device 5143:43030b00; driver Qualcomm Technologies Inc. Adreno Vulkan "
     "Driver, version 512.800.80"},
    {"Q4", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x5143u, 0x44010000u, 8, API(1, 3, 284),
     "Adreno (TM) 810",
     "device 5143:44010000; driver Qualcomm Technologies Inc. Adreno Vulkan "
     "Driver, version 512.800.0"},
    {"Q5", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x5143u, 0x05000500u, 0, API(1, 0, 61),
     "Adreno (TM) 505", "device 5143:5000500; driver (unnamed), version 512.0.0"},
    {"T1", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x5143u, 0x43050A01u, 18, API(1, 4, 363),
     "Turnip Adreno (TM) 740",
     "device 5143:43050a01; driver turnip Mesa driver Mesa 26.3.0-devel (git-bbdd688), version "
     "26.3.0, Mesa 26.3.0-devel"},
    {"T2", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x5143u, 0x43050A01u, 18, API(1, 4, 359),
     "Turnip Adreno (TM) 740",
     "device 5143:43050a01; driver PurpleVK public driver, version 26.3.0"},
    {"P1", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x1010u, 0x36104046u, 7, API(1, 1, 170),
     "PowerVR B-Series BXM-8-256",
     "device 1010:36104046; driver PowerVR B-Series Vulkan Driver 1.15@6133110, version 1.15.0"},
    {"P2", RHI_DT_VULKAN, RHI_DT_ANDROID, false, 0x1010u, 0x71061212u, 7, API(1, 4, 344),
     "PowerVR D-Series DXT-48-1536 MC1",
     "device 1010:71061212; driver PowerVR D-Series Vulkan Driver 26.1@7036548, version "
     "1.693.3716"},
    {"I1", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x8086u, 0x1912u, 5, API(1, 3, 215),
     "Intel(R) HD Graphics 530",
     "device 8086:1912; driver Intel Corporation Intel driver, version 101.2125"},
    {"I1 id 0", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x8086u, 0x1912u, 0, API(1, 3, 215),
     "Intel(R) HD Graphics 530", "device 8086:1912; driver (unnamed), version 101.2125"},
    {"N1", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x2184u, 4, API(1, 4, 312),
     "NVIDIA GeForce GTX 1660", "device 10de:2184; driver NVIDIA 617.14, version 617.14"},
    {"N2 D3D11", RHI_DT_D3D11, RHI_DT_WINDOWS, false, 0x10DEu, 0x1380u, 0, 0,
     "NVIDIA GeForce GTX 750 Ti", NULL},
    {"N2 GL", RHI_DT_OPENGL, RHI_DT_WINDOWS, false, 0x10DEu, 0, 0, 0, "NVIDIA GeForce GTX 750 Ti",
     NULL},
    {"N6", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x2204u, 4, API(1, 4, 341),
     "NVIDIA GeForce RTX 3090", "device 10de:2204; driver NVIDIA 610.88, version 610.88"},
    {"A1", RHI_DT_VULKAN, RHI_DT_LINUX, false, 0x1002u, 0x747Eu, 3, API(1, 4, 318),
     "AMD Radeon RX 7800 XT (RADV NAVI32)",
     "device 1002:747e; driver radv Mesa 25.3.0, version 25.3.0, Mesa 25.3.0"},
    {"A3", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x1002u, 0x7590u, 1, API(1, 4, 315),
     "AMD Radeon RX 9060 XT",
     "device 1002:7590; driver AMD proprietary driver 26.9.2, version 2.0"},
    {"X1", RHI_DT_VULKAN, RHI_DT_MACOS, true, 0x106Bu, 0x1A020209u, 14, API(1, 2, 296),
     "Apple M2 Pro", "device 106b:1a020209; driver MoltenVK 1.4.2, version 0.2.2019"},
    {"X1 Intel Mac", RHI_DT_VULKAN, RHI_DT_MACOS, true, 0x8086u, 0x3E9Bu, 14, API(1, 2, 296),
     "Intel(R) UHD Graphics 630", "device 8086:3e9b; driver MoltenVK 1.4.2, version 0.2.2019"},
    {"iOS", RHI_DT_VULKAN, RHI_DT_IOS, true, 0x106Bu, 0x0u, 14, API(1, 2, 296), "Apple A15 GPU",
     NULL},
    {"lavapipe", RHI_DT_VULKAN, RHI_DT_LINUX, false, 0x10005u, 0, 13, API(1, 4, 305),
     "llvmpipe (LLVM 19.1.7, 256 bits)",
     "device 10005:0000; driver llvmpipe Mesa 25.0.7-2 (LLVM 19.1.7), version 0.0.1, Mesa 25.0.7"},
    {"llvmpipe GL", RHI_DT_OPENGL, RHI_DT_LINUX, false, 0, 0, 0, 0,
     "llvmpipe (LLVM 19.1.7, 256 bits)", NULL},
    {"WARP", RHI_DT_D3D12, RHI_DT_WINDOWS, false, 0, 0, 0, 0, "Microsoft Basic Render Driver",
     NULL},
    {"WARP D3D11", RHI_DT_D3D11, RHI_DT_WINDOWS, false, 0x1414u, 0x8Cu, 0, 0,
     "Microsoft Basic Render Driver", NULL},
};

static unsigned long fakes(void)
{
    static const char *const fv[] = {NULL, "0x13B5", "0x5143", "0x1010", "0x10DE", "junk"};
    static const char *const pr[] = {NULL, "qualcomm", "other"};
    static const char *const qp[] = {NULL, "grant", "junk"};
    static const char *const onOff[] = {NULL, "0", "1"};
    unsigned long n = 0;
    for (size_t k = 0; k < NN(kKnown); k++) {
        for (int custom = 0; custom < 2; custom++) {
            for (size_t a = 0; a < NN(fv); a++) {
                for (size_t b = 0; b < NN(pr); b++) {
                    for (size_t q = 0; q < NN(qp); q++) {
                        for (int bits = 0; bits < 81; bits++) { /* 4 switches of 3 */
                            for (int lim = 0; lim < 2; lim++) {
                                RefIn in;
                                memset(&in, 0, sizeof in);
                                in.backend = kKnown[k].backend;
                                in.os = kKnown[k].os;
                                /* ICO_VK_FAKE_LIMITS=moltenvk: RhiLimits.moltenvk */
                                in.moltenvk = kKnown[k].moltenvk ||
                                              (lim && kKnown[k].backend == RHI_DT_VULKAN);
                                in.customDriver = custom != 0;
                                in.vendor = kKnown[k].vendor;
                                in.device = kKnown[k].device;
                                in.driverId = kKnown[k].driverId;
                                in.api = kKnown[k].api;
                                in.name = kKnown[k].name;
                                in.text = kKnown[k].text;
                                in.fakeVendor = fv[a];
                                in.fakePassRow = pr[b];
                                in.fakeQprio = qp[q];
                                in.fakeAdreno = onOff[bits % 3];
                                in.fakeSerial = onOff[(bits / 3) % 3];
                                in.fakeNoPrepass = onOff[(bits / 9) % 3];
                                in.compactArm = onOff[(bits / 27) % 3];
                                compare(&in, bits == 0 && b == 0 && q == 0);
                                n++;
                            }
                        }
                    }
                }
            }
        }
    }
    return n;
}

int main(void)
{
    const unsigned long ids = matrix();
    const unsigned long faked = fakes();
    size_t rowCount;
    const RhiDeviceRow *rows = rhi_device_table_rows(&rowCount);
    for (size_t r = 0; r < rowCount; r++) {
        s_checks++;
        if (s_rowApplied[r] == 0) {
            s_failures++;
            printf("FAIL row %s is in force on no identity of the matrix\n", rows[r].id);
        }
        /* each override link decides an identity */
        const char *s = rows[r].overrides;
        while (s && *s) {
            while (*s == ' ') {
                s++;
            }
            const size_t k = strcspn(s, " ");
            if (k == 0) {
                break;
            }
            char id[64];
            snprintf(id, sizeof id, "%.*s", (int)k, s);
            const int o = rhi_device_table_find(id);
            s_checks++;
            if (o < 0) {
                s_failures++;
                printf("FAIL row %s overrides %s, which is no row\n", rows[r].id, id);
            } else if (s_linkUsed[r][o] == 0) {
                s_failures++;
                printf("FAIL row %s's link over %s decides no identity of the matrix\n", rows[r].id,
                       id);
            }
            s += k;
        }
    }
    s_checks++;
    if (s_conflicts != 0) {
        s_failures++;
        printf("FAIL %lu identities with an unlinked conflict\n", s_conflicts);
    }
    printf("rhi_device_table_equiv: %lu identities, %lu with the tests' fakes, %lu checks, %lu "
           "failures\n",
           ids, faked, s_checks, s_failures);
    return s_failures ? 1 : 0;
}
