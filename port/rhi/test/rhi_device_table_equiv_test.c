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
 * rhi_driver_version.c, rhi_tile_rule.c, memory_floor.c, present_fallback.c) are linked in and compared with their copy over the
 * whole matrix; the copies of statics inside large files are compared with
 * their source lines by the DT1 hand-back's diff.  Those files read the
 * table now, so the comparison is the proof of the move.  The platform's
 * other reads (the present fallback's gate and start on every argument, the
 * window's swapchain question, the Android build's stock driver hint, the
 * attempt ledger's words where no row holds, attempt_ledger.c linked in)
 * are compared with copies taken at int/v0117 c91c64d9d (platform()).
 *
 * Also: every row is in force on some identity, every override link
 * decides some identity, and no identity has two matching rows that differ
 * without a link.
 *
 * v0.11.8 (issues 215 and 123) took the queue's high global priority off:
 * the choice QUEUE_PRIORITY_HIGH, its fake and rhi_qprio.c are gone.  Every
 * other comparison is as it was against v0.11.7's copies, so the test shows
 * the table's only change; the identities v0.11.7's rule asked the priority
 * on are counted (none would be a matrix that missed the row) and no
 * choice of the table may name a priority. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "memory_floor.h"
#include "present_fallback.h"
#include "rd_device_rows.h"
#include "rhi_device_table.h"
#include "rhi_driver_version.h"
#include "rhi_pass_row.h"
#include "rhi_tile_rule.h"

#include "rhi_device_table_ref.inc"

static unsigned long s_checks, s_failures;

static void describe(const RefIn *in, char *out, size_t cap)
{
    snprintf(out, cap,
             "backend %d os %d mvk %d custom %d vendor 0x%X device 0x%X id %u api 0x%X name '%s' "
             "text '%s' fakes vendor=%s passrow=%s adreno=%s serial=%s noprepass=%s "
             "compactarm=%s",
             in->backend, in->os, in->moltenvk, in->customDriver, (unsigned)in->vendor,
             (unsigned)in->device, (unsigned)in->driverId, (unsigned)in->api,
             in->name ? in->name : "(null)", in->text ? in->text : "(null)",
             in->fakeVendor ? in->fakeVendor : "-", in->fakePassRow ? in->fakePassRow : "-",
             in->fakeAdreno ? in->fakeAdreno : "-", in->fakeSerial ? in->fakeSerial : "-",
             in->fakeNoPrepass ? in->fakeNoPrepass : "-", in->compactArm ? in->compactArm : "-");
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
   reads the fakes, vk_device_table.c, through rhi_device_fakes_parse;
   ICO_RD_VU_COMPACT_ARM is the renderer's, any backend) */
static void newEval(const RefIn *in, RhiDeviceChoices *c)
{
    RhiDeviceId id;
    RhiDeviceFakes fk;
    RhiDeviceFakeEnv env;
    memset(&id, 0, sizeof id);
    memset(&env, 0, sizeof env);
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
        /* the Vulkan backend's reads (vk_device_table.c) */
        env.vendor = in->fakeVendor;
        env.passRow = in->fakePassRow;
        env.adreno = in->fakeAdreno;
        env.serial = in->fakeSerial;
        env.noPrepass = in->fakeNoPrepass;
    }
    env.compactArm = in->compactArm;
    rhi_device_fakes_parse(&env, &fk);
    rhi_device_table_eval(&id, &fk, c);
}

/* the renderer's current evaluation (rhi_device_table_current): the
   backend's, with the Vulkan fakes on Vulkan and never
   ICO_RD_VU_COMPACT_ARM (the renderer reads that one at its rule,
   rd_vu_compact.c) */
static void currentEval(const RefIn *in, RhiDeviceChoices *c)
{
    RefIn k = *in;
    k.compactArm = NULL;
    newEval(&k, c);
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

/* rd_pipeline.c rd__vu_set_choose as the call site reads it now: the
   backend's RhiLimits.vuFallbackPlain (the VU rows' plain), then the
   Turnip test on the limits (rd__driver_is_turnip) with the turnip row's
   reason as the brief */
static RefVuSet callSiteVuSet(const RefIn *in, const char *vuFallbackPlain)
{
    RefVuSet o;
    o.kind = "extended set (no device-table row, no Turnip driver, no lost start, no [dev] "
             "setting asks for the plain one)";
    o.brief = "";
    o.noBeside = false;
    RhiLimits lim;
    memset(&lim, 0, sizeof lim);
    lim.vendorId = in->vendor;
    lim.driverId = in->driverId;
    lim.driverText = in->text;
    lim.vuFallbackPlain = vuFallbackPlain;
    if (lim.vuFallbackPlain) {
        o.kind = "plain set by the device table";
        o.brief = lim.vuFallbackPlain;
        o.noBeside = true;
    } else if (rd__driver_is_turnip(&lim)) {
        o.kind = "plain set";
        const char *b = rd_device_rows_reason("turnip", RHI_DC_VU_FALLBACK_PLAIN);
        o.brief = b != NULL ? b : "";
        o.noBeside = true;
    }
    return o;
}

/* ----------------------------------------------------------- the comparison */

static unsigned long s_rowApplied[RHI_DT_MAX_ROWS];
static unsigned long s_linkUsed[RHI_DT_MAX_ROWS][RHI_DT_MAX_ROWS];
static unsigned long s_conflicts;
/* the identities v0.11.7's queue priority rule asked high on (ref_qprio_row
   on Vulkan): the priority taken off in v0.11.8 */
static unsigned long s_qprioOld;

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
    /* the renderer's pure predicates (rd_device_rows.c) */
    {
        RhiLimits lim;
        memset(&lim, 0, sizeof lim);
        lim.vendorId = in->vendor;
        lim.driverId = in->driverId;
        lim.driverText = in->text;
        SAME(in, "live rd__driver_is_turnip", ref_driver_is_turnip(in), rd__driver_is_turnip(&lim));
        SAME(in, "live rd__profile_vendor_adreno",
             ref_profile_vendor_adreno(in->vendor, in->moltenvk),
             rd__profile_vendor_adreno(in->vendor, in->moltenvk));
        SAME(in, "live rd__vu_class_measured", ref_vu_class_measured(in->name),
             rd__vu_class_measured(in->name));
    }
    const int windows = in->os == RHI_DT_WINDOWS;
    const int android = in->os == RHI_DT_ANDROID;
    sameWords(in, "live rhi_vk_pass_row",
              rhi_vk_pass_row(in->vendor, in->driverId, in->api, windows),
              ref_pass_row_intel(in->vendor, in->driverId, in->api, windows));
    sameWords(in, "live rhi_vk_pass_row_qualcomm",
              rhi_vk_pass_row_qualcomm(in->vendor, in->driverId, android),
              ref_pass_row_qualcomm(in->vendor, in->driverId, android));
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

/* The renderer's reads as its call sites compose them now (DT3:
 * rd_profile.c, rd_lamps.c, rd_fill11.c, rd_shadow_tile.c,
 * rd_stencil_selftest.c, rd_vu_record.c, rd_no_volume.c, rd_vu_compact.c,
 * rd_vu_skin_check.c, rd_pipeline.c, rd_core.c, rd_present.c, rd_uber.c,
 * rd_fog_path.c), each through rd_device_rows_value on the current
 * evaluation, against the old rule's copy; dev: the renderer has a device
 * (g_rd.hasDevice, the limits pointer the rule tested).
 * have: the evaluation newEval made (reused when it carries no
 * ICO_RD_VU_COMPACT_ARM, NULL: made here); liveLimits false: no device (in
 * is ref_no_device's, stale an evaluation the backend may have left) */
static void rendererReads(const RefIn *in, const RhiDeviceChoices *have, bool liveLimits,
                          const RhiDeviceChoices *stale)
{
    RhiDeviceChoices own;
    const RhiDeviceChoices *cur = stale;
    if (liveLimits) {
        if (have == NULL || in->compactArm != NULL) {
            currentEval(in, &own);
            have = &own;
        }
        cur = have;
    }
    const bool vulkan = in->backend == RHI_DT_VULKAN;
#define RV(ch) rd_device_rows_value(cur, in->backend, liveLimits, (ch))
    const int32_t cls = RV(RHI_DC_PROFILE_CLASS);
    SAME(in, "rd classOf", ref_classOf(in), cls);
    SAME(in, "rd__profile_adreno", ref_profile_adreno(in), rd_device_rows_adreno(cls));
    SAME(in, "rd__profile_powervr", ref_profile_powervr(in), cls == RHI_DT_CLASS_POWERVR);
    SAME(in, "rd__profile_tile_class", ref_profile_tile_class(in), RV(RHI_DC_TILE_CLASS));
    SAME(in, "rd profile tiler", ref_lim_tiler(in), RV(RHI_DC_TILER) != 0);
    SAME(in, "rd profile tile columns",
         cls != RHI_DT_CLASS_DESKTOP && cls != RHI_DT_CLASS_D3D12 && cls != RHI_DT_CLASS_OPENGL &&
             cls != RHI_DT_CLASS_D3D11,
         RV(RHI_DC_TILE_COLUMNS) != 0);
    SAME(in, "rd profile blurFold", ref_classOf(in) != RHI_DT_CLASS_MALI,
         !RV(RHI_DC_BLUR_FOLD_OFF));
    for (int dev = 0; dev < 2; dev++) {
        /* rd_lamps.c rd__lamps_init (tilePhone read through the limits on a
           device, vulkan with the device) and the 4-sample pass's class */
        const bool vk = dev && vulkan;
        for (int passArea = 0; passArea < 2; passArea++) {
            const RefLamps o =
                ref_lamps(ref_profile_tile_class(in), dev && ref_tile_phone(in), vk, passArea != 0);
            SAME(in, "rd lamps ms", o.ms, RV(RHI_DC_LAMPS_MS) && vk);
            SAME(in, "rd lamps fillpaste", o.fillpaste, RV(RHI_DC_LAMPS_FILLPASTE) && vk);
            SAME(in, "rd lamps area", o.area, RV(RHI_DC_LAMPS_AREA) && vk && passArea);
        }
        SAME(in, "rd lamps ms powervr", ref_lamps_ms_powervr(in, dev != 0),
             dev && RV(RHI_DC_LAMPS_MS) != 0 && vulkan);
        SAME(in, "rd vu compact dxt", ref_cmp_dxt(in, dev != 0),
             dev && RV(RHI_DC_COMPACTION_CLASS) == RHI_DT_CMP_DXT);
        bool pv, ms;
        ref_vu_record_class(in, dev != 0, &pv, &ms);
        const int32_t vcc = dev ? RV(RHI_DC_VU_COMPUTE_CLASS) : 0;
        SAME(in, "rd vu record powervr", pv, vcc != RHI_DT_VUCOMP_NONE);
        SAME(in, "rd vu record measured", ms, vcc == RHI_DT_VUCOMP_MEASURED);
        SAME(in, "rd no volume classBase", ref_no_volume_base_dev(in, dev != 0),
             dev && RV(RHI_DC_NO_VOLUME_CLASS) != 0);
        SAME(in, "rd no volume breakMeasured",
             ref_no_volume_base_dev(in, dev != 0) && ref_no_volume_break(in),
             dev && RV(RHI_DC_NO_VOLUME_CLASS) != 0 && RV(RHI_DC_NO_VOLUME_BREAK) != 0);
        SAME(in, "rd fog tiler", ref_fog_tiler(in, dev != 0),
             dev && RV(RHI_DC_FOG_ORDER_TILER) != 0);
        SAME(in, "rd scene cap", dev && ref_lim_tiler(in), dev && RV(RHI_DC_SCENE_CAP_4096) != 0);
        SAME(in, "rd MSAA budget", dev && ref_lim_tiler(in), dev && RV(RHI_DC_MSAA_BUDGET) != 0);
    }
    SAME(in, "rd fill11 classOk", ref_fill11_class(ref_profile_tile_class(in), ref_tile_phone(in)),
         RV(RHI_DC_FILL11_CLASS));
    SAME(in, "rd shadow tile class", ref_profile_tile_class(in), RV(RHI_DC_SHADOW_TILE_CLASS));
    {
        const int why = rd_device_rows_adreno(cls) ? 1 : !RV(RHI_DC_STENCIL_CUT) ? 2 : 0;
        SAME(in, "rd stencil cut why", ref_stencil_off_why(in), why);
        SAME(in, "rd stencil cut want", !ref_profile_adreno(in) && ref_profile_powervr(in),
             RV(RHI_DC_STENCIL_CUT) != 0);
    }
    SAME(in, "rd skin check default", ref_profile_adreno(in), RV(RHI_DC_SKIN_CHECK_DEFAULT) != 0);
    SAME(in, "rd skin as before", ref_tile_phone(in), RV(RHI_DC_SKIN_AS_BEFORE) != 0);
    SAME(in, "rd MSAA max 4", ref_lim_tiler(in), RV(RHI_DC_MSAA_MAX4) != 0);
    SAME(in, "rd shot late", ref_lim_tiler(in), RV(RHI_DC_SHOT_LATE) != 0);
    SAME(in, "rd uber auto", ref_lim_tiler(in), RV(RHI_DC_UBER_AUTO) != 0);
    {
        const RefVuSet o = ref_vu_set(ref_vu_plain_only(in).plain, ref_driver_is_turnip(in));
        const RefVuSet n =
            callSiteVuSet(in, liveLimits && cur != NULL ? rhi_device_vu_rows(cur).plain : NULL);
        sameWords(in, "rd vu set kind", o.kind, n.kind);
        sameWords(in, "rd vu set brief", o.brief, n.brief);
        SAME(in, "rd vu set noBeside", o.noBeside, n.noBeside);
    }
    /* every choice: the live limits read the evaluation */
    for (int ch = 0; liveLimits && ch < RHI_DC_COUNT; ch++) {
        SAME(in, "rd_device_rows_value", cur->value[ch], RV((RhiDeviceChoice)ch));
    }
#undef RV
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
        /* the fakes' parse (rhi_device_fakes_parse, vk_device_table.c) gives
           the words' flags the call sites print as each parsed them before:
           the tile rule's " (ICO_VK_FAKE_VENDOR)", the pass row's
           " (ICO_VK_FAKE_PASS_ROW=qualcomm)", the attachment read's
           "(ICO_VK_FAKE_ADRENO)" */
        {
            RhiDeviceFakeEnv env;
            RhiDeviceFakes fk;
            memset(&env, 0, sizeof env);
            env.vendor = in->fakeVendor;
            env.passRow = in->fakePassRow;
            env.adreno = in->fakeAdreno;
            rhi_device_fakes_parse(&env, &fk);
            bool faked;
            const uint32_t fv = ref_fake_vendor(in, &faked);
            SAME(in, "ICO_VK_FAKE_VENDOR parsed", faked, fk.vendorSet);
            SAME(in, "ICO_VK_FAKE_VENDOR's vendor", faked ? fv : 0u, fk.vendor);
            SAME(in, "ICO_VK_FAKE_PASS_ROW parsed",
                 in->fakePassRow && strcmp(in->fakePassRow, "qualcomm") == 0, fk.passRowQualcomm);
            SAME(in, "ICO_VK_FAKE_ADRENO parsed",
                 in->fakeAdreno && in->fakeAdreno[0] && in->fakeAdreno[0] != '0', fk.adreno);
        }
        const char *intelRow, *qcomRow;
        ref_pass_rows(in, &intelRow, &qcomRow);
        SAME(in, "PASS_OBJECTS",
             intelRow  ? RHI_DT_PASS_INTEL
             : qcomRow ? RHI_DT_PASS_QUALCOMM
                       : RHI_DT_PASS_NONE,
             v[RHI_DC_PASS_OBJECTS]);
        sameWords(in, "PASS_OBJECTS", intelRow ? intelRow : qcomRow,
                  rhi_device_choice_reason(&c, RHI_DC_PASS_OBJECTS));
        /* vk_device.c vkr_pass_row_apply's two rows (rhi_device_pass_rows) */
        {
            const char *ni, *nq;
            rhi_device_pass_rows(&c, &ni, &nq);
            sameWords(in, "vkr_pass_row_apply intelRow", intelRow, ni);
            sameWords(in, "vkr_pass_row_apply qcomRow", qcomRow, nq);
        }
        /* the subpass class with the run's passRowQcom (vk_device.c
           vkr_subpass_read_decide: the Qualcomm row applied), every way it
           can come out, as the call site composes it
           (rhi_device_subpass_class) */
        for (int legacy = 0; legacy < 2; legacy++) {
            for (int ov = -1; ov <= 1; ov++) {
                const bool qcomApplied = ref_pass_row_qcom_applied(qcomRow, legacy != 0, ov);
                SAME(in, "SUBPASS_CLASS", ref_subpass_class(in, qcomApplied),
                     rhi_device_subpass_class(&c, qcomApplied));
            }
        }
        /* v0.11.7's vk_qprio.c row (taken off in v0.11.8): counted where
           its rule asked; the table has no such choice */
        if (ref_qprio_row(in->vendor, in->driverId, REF_ANDROID(in)) != NULL) {
            s_qprioOld++;
        }
    } else {
        SAME(in, "PASS_OBJECTS off Vulkan", RHI_DT_PASS_NONE, v[RHI_DC_PASS_OBJECTS]);
    }
    SAME(in, "ATTACHMENT_READ_OFF", ref_attachment_read_off(in), v[RHI_DC_ATTACHMENT_READ_OFF]);
    {
        /* vk_device.c vkr_vu_plain_only (rhi_device_vu_rows) on Vulkan;
           the other backends set none of the three */
        const RefVuPlain p = ref_vu_plain_only(in);
        SAME(in, "VU_FALLBACK_PLAIN table", p.plain != NULL,
             v[RHI_DC_VU_FALLBACK_PLAIN] == RHI_DT_VUSET_TABLE);
        const RhiDeviceVuRows rows = rhi_device_vu_rows(&c);
        sameWords(in, "vkr_vu_plain_only plain", p.plain, rows.plain);
        sameWords(in, "CAREFUL_PIPELINES", p.careful, rows.careful);
        sameWords(in, "VU_PREPASS_OFF", p.noPrepass, rows.noPrepass);
        SAME(in, "COMPUTE_RECORD_OFF (no row)", 0, v[RHI_DC_COMPUTE_RECORD_OFF]);
        sameWords(in, "COMPUTE_RECORD_OFF reason (no row)", NULL, rows.noComputeRecord);
        const RefVuSet o = ref_vu_set(p.plain, ref_driver_is_turnip(in));
        const RefVuSet n = newVuSet(&c);
        sameWords(in, "vu_set kind", o.kind, n.kind);
        sameWords(in, "vu_set brief", o.brief, n.brief);
        SAME(in, "vu_set noBeside", o.noBeside, n.noBeside);
    }
    {
        const char *s = ref_compiler_serial(in);
        SAME(in, "COMPILER_SERIAL", s != NULL, v[RHI_DC_COMPILER_SERIAL]);
        /* vk_pipeline.c vkr_compile_serial_init (rhi_device_choice_why) */
        sameWords(in, "COMPILER_SERIAL", s, rhi_device_choice_why(&c, RHI_DC_COMPILER_SERIAL));
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
    rendererReads(in, &c, true, NULL);

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
    /* DT3: the rest of DEVICES.md's logged GPUs (device lines from their
       logs; the driver ids NVIDIA's 4 and RADV's 3 by the driver's name) */
    {"N3", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x1B82u, 4, API(1, 4, 312),
     "NVIDIA GeForce GTX 1070 Ti", "device 10de:1b82; driver NVIDIA 581.80, version 581.80"},
    {"N4", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x1F82u, 4, API(1, 4, 351),
     "NVIDIA GeForce GTX 1650", "device 10de:1f82; driver NVIDIA 617.42, version 617.42"},
    {"N5", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x28E0u, 4, API(1, 4, 341),
     "NVIDIA GeForce RTX 4060 Laptop GPU",
     "device 10de:28e0; driver NVIDIA 610.74, version 610.74"},
    {"N6 3050", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x2584u, 4, API(1, 4, 303),
     "NVIDIA GeForce RTX 3050", "device 10de:2584; driver NVIDIA 577.00, version 577.00"},
    {"N6 2000 Ada", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x28B8u, 4, API(1, 4, 341),
     "NVIDIA RTX 2000 Ada Generation Laptop GPU",
     "device 10de:28b8; driver NVIDIA 610.88, version 610.88"},
    {"N6 4090", RHI_DT_VULKAN, RHI_DT_WINDOWS, false, 0x10DEu, 0x2684u, 4, API(1, 4, 351),
     "NVIDIA GeForce RTX 4090", "device 10de:2684; driver NVIDIA 617.14, version 617.14"},
    {"A2", RHI_DT_VULKAN, RHI_DT_LINUX, false, 0x1002u, 0x15BFu, 3, API(1, 4, 354),
     "AMD Ryzen Z1 Extreme (RADV PHOENIX)",
     "device 1002:15bf; driver radv Mesa 26.2.4, version 26.2.4, Mesa 26.2.4"},
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
    static const char *const onOff[] = {NULL, "0", "1"};
    unsigned long n = 0;
    for (size_t k = 0; k < NN(kKnown); k++) {
        for (int custom = 0; custom < 2; custom++) {
            for (size_t a = 0; a < NN(fv); a++) {
                for (size_t b = 0; b < NN(pr); b++) {
                    for (int bits = 0; bits < 81; bits++) { /* 4 switches of 3 */
                        for (int lim = 0; lim < 2; lim++) {
                            RefIn in;
                            memset(&in, 0, sizeof in);
                            in.backend = kKnown[k].backend;
                            in.os = kKnown[k].os;
                            /* ICO_VK_FAKE_LIMITS=moltenvk: RhiLimits.moltenvk */
                            in.moltenvk =
                                kKnown[k].moltenvk || (lim && kKnown[k].backend == RHI_DT_VULKAN);
                            in.customDriver = custom != 0;
                            in.vendor = kKnown[k].vendor;
                            in.device = kKnown[k].device;
                            in.driverId = kKnown[k].driverId;
                            in.api = kKnown[k].api;
                            in.name = kKnown[k].name;
                            in.text = kKnown[k].text;
                            in.fakeVendor = fv[a];
                            in.fakePassRow = pr[b];
                            in.fakeAdreno = onOff[bits % 3];
                            in.fakeSerial = onOff[(bits / 3) % 3];
                            in.fakeNoPrepass = onOff[(bits / 9) % 3];
                            in.compactArm = onOff[(bits / 27) % 3];
                            compare(&in, bits == 0 && b == 0);
                            n++;
                        }
                    }
                }
            }
        }
    }
    return n;
}

/* the pure predicates' remaining arguments (the matrix ties them to the
   OS): rhi_tile_phone on every combination, rhi_tile_vendor, the serial
   rule's unused Vulkan version */
static void edges(void)
{
    RefIn none;
    memset(&none, 0, sizeof none);
    for (int bits = 0; bits < 8; bits++) {
        const bool a = (bits & 1) != 0, i = (bits & 2) != 0, f = (bits & 4) != 0;
        SAME(&none, "live rhi_tile_phone (every combination)", ref_tile_phone_rule(a, i, f),
             rhi_tile_phone(a, i, f));
    }
    static const uint32_t vendors[] = {0x5143u, 0x13B5u, 0x1010u,    0x144Du, 0x106Bu,
                                       0x14E4u, 0x8086u, 0x10DEu,    0x1002u, 0x10005u,
                                       0x1414u, 0u,      0xFFFFFFFFu};
    for (size_t k = 0; k < sizeof vendors / sizeof vendors[0]; k++) {
        SAME(&none, "live rhi_tile_vendor", ref_tile_vendor_name(vendors[k]) != NULL,
             rhi_tile_vendor(vendors[k]));
        for (uint32_t major = 0; major < 3; major++) {
            for (uint32_t minor = 0; minor < 5; minor++) {
                const char *copy = vendors[k] == 0x1010u ? "PowerVR B-Series" : NULL;
                sameWords(&none, "live rhi_driver_compiler_serial (any Vulkan version)", copy,
                          rhi_driver_compiler_serial(vendors[k], "PowerVR B-Series Vulkan Driver",
                                                     major, minor));
            }
        }
    }
}

/* DT3: the renderer's reads on the logged devices at every Vulkan version of
   the matrix's edges (each device's own and 1.0.61 to 1.4.344), on every
   backend and OS, MoltenVK or not, a driver package or not, with the
   profile's run inputs */
static unsigned long rendererKnown(void)
{
    unsigned long n = 0;
    for (size_t k = 0; k < NN(kKnown); k++) {
        for (size_t ai = 0; ai <= NN(kApis); ai++) {
            for (int os = 0; os < RHI_DT_OS_COUNT; os++) {
                for (size_t bi = 0; bi < NN(kBackends); bi++) {
                    for (int bits = 0; bits < 4; bits++) {
                        RefIn in;
                        memset(&in, 0, sizeof in);
                        in.backend = kBackends[bi];
                        in.os = os;
                        in.moltenvk = (bits & 1) != 0;
                        in.customDriver = (bits & 2) != 0;
                        in.vendor = kKnown[k].vendor;
                        in.device = kKnown[k].device;
                        in.driverId = kKnown[k].driverId;
                        in.api = ai < NN(kApis) ? kApis[ai] : kKnown[k].api;
                        in.name = kKnown[k].name;
                        in.text = kKnown[k].text;
                        compare(&in, true);
                        live(&in);
                        n++;
                    }
                }
            }
        }
    }
    return n;
}

/* DT3: no device (the backend's limits zeroed: before rhi_init, after
   rhi_shutdown, a failed start): the renderer's reads give what the old
   rules gave on the zeroed limits, whatever evaluation a device before left
   (none, or each logged device's on each OS) */
static unsigned long noDevice(void)
{
    unsigned long n = 0;
    for (size_t bi = 0; bi < NN(kBackends); bi++) {
        const RefIn none = ref_no_device(kBackends[bi]);
        rendererReads(&none, NULL, false, NULL);
        n++;
        for (size_t k = 0; k < NN(kKnown); k++) {
            for (int os = 0; os < RHI_DT_OS_COUNT; os++) {
                RefIn was;
                memset(&was, 0, sizeof was);
                was.backend = kKnown[k].backend;
                was.os = os;
                was.moltenvk = kKnown[k].moltenvk;
                was.vendor = kKnown[k].vendor;
                was.device = kKnown[k].device;
                was.driverId = kKnown[k].driverId;
                was.api = kKnown[k].api;
                was.name = kKnown[k].name;
                was.text = kKnown[k].text;
                RhiDeviceChoices stale;
                currentEval(&was, &stale);
                rendererReads(&none, NULL, false, &stale);
                n++;
            }
        }
    }
    return n;
}

/* rhi_device_table_choice (the pure predicates' one-choice read) and
   rhi_device_table_start (the backends' one evaluation) give what the whole
   evaluation gives, every choice, value and reason, on the logged devices on
   every backend and OS, MoltenVK or not, a driver package or not */
static unsigned long choices(void)
{
    unsigned long n = 0;
    for (size_t k = 0; k < NN(kKnown); k++) {
        for (int os = 0; os < RHI_DT_OS_COUNT; os++) {
            for (size_t bi = 0; bi < NN(kBackends); bi++) {
                for (int bits = 0; bits < 4; bits++) {
                    RhiDeviceId id;
                    memset(&id, 0, sizeof id);
                    id.backend = (uint8_t)kBackends[bi];
                    id.os = (uint8_t)os;
                    id.moltenvk = (bits & 1) != 0;
                    id.customDriver = (bits & 2) != 0;
                    id.vendorId = kKnown[k].vendor;
                    id.deviceId = kKnown[k].device;
                    id.driverId = kKnown[k].driverId;
                    id.apiVersion = kKnown[k].api;
                    id.deviceName = kKnown[k].name;
                    id.driverText = kKnown[k].text;
                    RhiDeviceChoices all;
                    rhi_device_table_eval(&id, NULL, &all);
                    const RhiDeviceChoices *cur = rhi_device_table_start(&id, NULL, NULL, NULL);
                    RefIn in;
                    memset(&in, 0, sizeof in);
                    in.backend = id.backend;
                    in.os = id.os;
                    in.moltenvk = id.moltenvk;
                    in.customDriver = id.customDriver;
                    in.vendor = id.vendorId;
                    in.device = id.deviceId;
                    in.driverId = id.driverId;
                    in.api = id.apiVersion;
                    in.name = id.deviceName;
                    in.text = id.driverText;
                    s_checks++;
                    if (cur != rhi_device_table_current() || memcmp(cur, &all, sizeof all) != 0) {
                        fail(&in, "rhi_device_table_start", 0, 1);
                    }
                    for (int ch = 0; ch < RHI_DC_COUNT; ch++) {
                        const char *why = NULL;
                        const int32_t one = rhi_device_table_choice(&id, (RhiDeviceChoice)ch, &why);
                        SAME(&in, rhi_device_choice_name((RhiDeviceChoice)ch), all.value[ch], one);
                        sameWords(&in, rhi_device_choice_name((RhiDeviceChoice)ch),
                                  rhi_device_choice_reason(&all, (RhiDeviceChoice)ch), why);
                    }
                    n++;
                }
            }
        }
    }
    return n;
}

/* DT4: the platform's reads.  The pure predicates (memory_floor.c,
   present_fallback.c) are compared with their copies on every identity of
   the matrix in live(); here, on every vendor, driver id and driver text of
   the matrix: the gate and FIFO from the start on every combination of
   their other arguments, the window's swapchain question
   (window_present_fallback.c fifo_start_now) and the Android build's stock
   driver hint (gpu_driver_android.c on_stock_qualcomm) as those call sites
   compose them now, and the attempt ledger's words where no row holds
   (attempt_ledger.c, linked in) */
static int windowFifoQuestion(int mode, int androidBuild, unsigned vendorId,
                              unsigned limitsDriverId, const char *driverText)
{
    /* window_fifo_set hands the question over where
       ico_present_fallback_start_asked says so; fifo_start_now's answer */
    if (!ico_present_fallback_start_asked((IcoPresentFallbackMode)mode, androidBuild)) {
        return -1;
    }
    return ico_present_fallback_start((IcoPresentFallbackMode)mode, androidBuild, 0, vendorId,
                                      limitsDriverId, driverText);
}

static int onStockQualcomm(int osBuild, int customDriver, bool haveLimits, uint32_t vendorId)
{
    /* gpu_driver_android.c on_stock_qualcomm (osBuild: RHI_DT_OS_BUILD) */
    RhiDeviceId id;
    memset(&id, 0, sizeof id);
    id.os = (uint8_t)osBuild;
    id.customDriver = (uint8_t)(customDriver != 0);
    id.vendorId = haveLimits ? vendorId : 0u;
    return rhi_device_table_choice(&id, RHI_DC_TURNIP_NOTE, NULL) != 0;
}

static unsigned long platform(void)
{
    unsigned long n = 0;
    _Static_assert(ICO_PRESENT_FALLBACK_AUTO == 0 && ICO_PRESENT_FALLBACK_OFF == 1 &&
                       ICO_PRESENT_FALLBACK_FIFO == 2 && ICO_PRESENT_FALLBACK_GATE_NO == 0 &&
                       ICO_PRESENT_FALLBACK_GATE_YES == 1 && ICO_PRESENT_FALLBACK_GATE_KEPT == 2 &&
                       ICO_PRESENT_FALLBACK_GATE_OTHER == 3,
                   "the copies' numbers are present_fallback.h's");
    for (size_t vi = 0; vi < NN(kVendors); vi++) {
        for (size_t ii = 0; ii < NN(kDriverIds); ii++) {
            for (size_t ti = 0; ti < NN(kTexts); ti++) {
                RefIn in;
                memset(&in, 0, sizeof in);
                in.vendor = kVendors[vi].vendor;
                in.driverId = kDriverIds[ii];
                in.text = kTexts[ti];
                const unsigned v = in.vendor, d = in.driverId;
                const char *t = in.text;
                for (int mode = 0; mode < 3; mode++) {
                    for (int bits = 0; bits < 8; bits++) {
                        const int vsync = bits & 1, mailbox = (bits >> 1) & 1,
                                  vulkan = (bits >> 2) & 1;
                        SAME(&in, "live ico_present_fallback_gate (every argument)",
                             ref_fallback_gate(vsync, mailbox, vulkan, v, d, t, mode),
                             ico_present_fallback_gate(vsync, mailbox, vulkan, v, d, t,
                                                       (IcoPresentFallbackMode)mode));
                    }
                    for (int bits = 0; bits < 4; bits++) {
                        const int android = bits & 1, vsync = (bits >> 1) & 1;
                        SAME(&in, "live ico_present_fallback_start (every argument)",
                             ref_fallback_start(mode, android, vsync, v, d, t),
                             ico_present_fallback_start((IcoPresentFallbackMode)mode, android,
                                                        vsync, v, d, t));
                    }
                    for (int android = 0; android < 2; android++) {
                        /* the limits' driver id beside the rhi's vendor and text */
                        SAME(&in, "window_present_fallback.c fifo_start_now",
                             ref_window_fifo_question(mode, android, v, d, t),
                             windowFifoQuestion(mode, android, v, d, t));
                    }
                }
                n++;
            }
        }
        for (int bits = 0; bits < 4; bits++) {
            RefIn in;
            memset(&in, 0, sizeof in);
            in.os = RHI_DT_ANDROID;
            in.vendor = kVendors[vi].vendor;
            in.customDriver = (bits & 1) != 0;
            const bool haveLimits = (bits & 2) != 0;
            SAME(&in, "gpu_driver_android.c on_stock_qualcomm",
                 ref_on_stock(in.customDriver, haveLimits, in.vendor),
                 onStockQualcomm(RHI_DT_ANDROID, in.customDriver, haveLimits, in.vendor));
        }
    }
    /* the ledger's words: the table's, and what the deciders write with them */
    RefIn none;
    memset(&none, 0, sizeof none);
    sameWords(&none, "na words NO_VOLUME_CLASS", REF_NA_NO_VOLUME,
              rhi_device_choice_na_words(RHI_DC_NO_VOLUME_CLASS));
    sameWords(&none, "na words PASS_OBJECTS", REF_NA_PASS_OBJECTS,
              rhi_device_choice_na_words(RHI_DC_PASS_OBJECTS));
    sameWords(&none, "na words VU_COMPUTE_CLASS", REF_NA_VU_COMPUTE,
              rhi_device_choice_na_words(RHI_DC_VU_COMPUTE_CLASS));
    sameWords(&none, "na words COMPACTION_CLASS", REF_NA_COMPACTION,
              rhi_device_choice_na_words(RHI_DC_COMPACTION_CLASS));
    ico_attempt_reset();
    /* another class than the window's, the tests' switch unset or 0 for it */
    ico_attempt_decide_no_volume_break(-1, 0, 0, 1, NULL);
    SAME(&none, "ledger no-volume break state", ICO_ATT_NA,
         ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK));
    sameWords(&none, "ledger no-volume break", REF_NA_NO_VOLUME,
              ico_attempt_why_of(ICO_ATT_NO_VOLUME_BREAK));
    /* no pass row */
    ico_attempt_decide_pass_objects(NULL);
    SAME(&none, "ledger pass objects state", ICO_ATT_NA, ico_attempt_state_of(ICO_ATT_ADRENO_A));
    sameWords(&none, "ledger pass objects", REF_NA_PASS_OBJECTS,
              ico_attempt_why_of(ICO_ATT_ADRENO_A));
    /* vertex and compute stores, the tests' switch unset, not the PowerVR class */
    SAME(&none, "ledger VU compute off", 0, ico_attempt_decide_vu_compute(0, 1, 1, 0, 0, -1, 1));
    SAME(&none, "ledger VU compute state", ICO_ATT_NA,
         ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS));
    sameWords(&none, "ledger VU compute", REF_NA_VU_COMPUTE,
              ico_attempt_why_of(ICO_ATT_VU_COMPUTE_CLASS));
    /* the compute record, another device's class, the switch unset, the
       self-test passed or not run */
    for (int selftest = -1; selftest <= 1; selftest += 2) {
        SAME(&none, "ledger compaction off", 0,
             ico_attempt_decide_compaction(1, NULL, ICO_ATT_CMP_OTHER, -1, 1, 1, selftest));
        SAME(&none, "ledger compaction state", ICO_ATT_OFF_DEFAULT,
             ico_attempt_state_of(ICO_ATT_COMPACTION));
        sameWords(&none, "ledger compaction", REF_NA_COMPACTION,
                  ico_attempt_why_of(ICO_ATT_COMPACTION));
    }
    ico_attempt_reset();
    return n;
}

int main(void)
{
    const unsigned long ids = matrix();
    const unsigned long faked = fakes();
    edges();
    const unsigned long one = choices();
    const unsigned long rk = rendererKnown();
    const unsigned long nd = noDevice();
    const unsigned long pf = platform();
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
    /* the queue's priority taken off (v0.11.8): v0.11.7's rule asked on
       some identity of the matrix (Qualcomm's own driver on Android), and no
       choice of the table names a priority any more */
    s_checks++;
    if (s_qprioOld == 0) {
        s_failures++;
        printf("FAIL v0.11.7's queue priority rule asked on no identity of the matrix\n");
    }
    for (int ch = 0; ch < RHI_DC_COUNT; ch++) {
        s_checks++;
        if (strstr(rhi_device_choice_name((RhiDeviceChoice)ch), "PRIORITY") != NULL) {
            s_failures++;
            printf("FAIL choice %s names a priority\n",
                   rhi_device_choice_name((RhiDeviceChoice)ch));
        }
    }
    s_checks++;
    if (s_conflicts != 0) {
        s_failures++;
        printf("FAIL %lu identities with an unlinked conflict\n", s_conflicts);
    }
    printf(
        "rhi_device_table_equiv: %lu identities, %lu with the tests' fakes, %lu one choice at "
        "a time, %lu renderer identities at the edge versions, %lu without a device, %lu "
        "platform drivers, %lu where the old rule asked the queue's high priority (none now), %lu "
        "checks, %lu failures\n",
        ids, faked, one, rk, nd, pf, s_qprioOld, s_checks, s_failures);
    return s_failures ? 1 : 0;
}
