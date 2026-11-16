/* rhi_device_table_ref.inc: static copies of every rule the device table
 * replaces, as they read at int/v0117 df714b3f4 (after MALI179's merge),
 * for rhi_device_table_equiv_test.c.  Each copy names its source file and
 * lines there; the build's macros (__ANDROID__, _WIN32, ICO_IOS) and
 * rhi_backend() are parameters (RefIn), the tests' environment variables
 * are RefIn's strings (NULL unset), parsed exactly as the source does.
 *
 * Kept as they are: when DT2 to DT4 move a call site onto the table, its
 * copy here stays the proof of the old behaviour.  Do not "fix" a copy.
 *
 * One change since (v0.11.8, issues 215 and 123): the queue's high global
 * priority was taken off with its choice (QUEUE_PRIORITY_HIGH), its fake
 * (ICO_VK_FAKE_QUEUE_PRIORITY) and rhi_qprio.c.  RefIn's fakeQprio and the
 * copy of vk_qprio.c's row (ref_qprio) went with them; ref_qprio_row stays
 * as it was, and the test counts the identities it asked on (the only
 * behaviour the table no longer has). */

typedef struct RefIn {
    int backend; /* RHI_DT_VULKAN, ...: rhi_backend() */
    int os;      /* RHI_DT_<os>: the build's macros */
    bool moltenvk;
    bool customDriver;
    uint32_t vendor, device, driverId, api;
    const char *name; /* rhi_adapter_name() / VkPhysicalDeviceProperties.deviceName */
    const char *text; /* RhiLimits.driverText */
    /* the tests' environment (NULL unset) */
    const char *fakeVendor;    /* ICO_VK_FAKE_VENDOR */
    const char *fakePassRow;   /* ICO_VK_FAKE_PASS_ROW */
    const char *fakeAdreno;    /* ICO_VK_FAKE_ADRENO */
    const char *fakeSerial;    /* ICO_VK_FAKE_SERIAL */
    const char *fakeNoPrepass; /* ICO_VK_FAKE_NO_PREPASS */
    const char *compactArm;    /* ICO_RD_VU_COMPACT_ARM */
} RefIn;

#define REF_VULKAN(in) ((in)->backend == RHI_DT_VULKAN)
#define REF_ANDROID(in) ((in)->os == RHI_DT_ANDROID)

/* port/rhi/vk/vk_device.c:3184-3201 vkr_is_tiler */
static bool ref_vkr_is_tiler(uint32_t vendor)
{
    switch (vendor) {
    case 0x5143: /* Qualcomm */
    case 0x13B5: /* ARM */
    case 0x1010: /* Imagination */
    case 0x144D: /* Samsung */
    case 0x106B: /* Apple */
    case 0x14E4: /* Broadcom */
        return true;
    default:
        return false;
    }
}

/* port/rhi/vk/vk_device.c:3346-3352 (vkr_fill_limits: o->tiler, every
   Android GPU); gl_device.c:366, d3d11_device.c:532, d3d12_device.c:755 set
   tiler = false */
static bool ref_lim_tiler(const RefIn *in)
{
    if (!REF_VULKAN(in)) {
        return false;
    }
    bool tiler = ref_vkr_is_tiler(in->vendor);
    if (REF_ANDROID(in)) {
        tiler = true;
    }
    return tiler;
}

/* port/rhi/rhi_tile_rule.c:88-106 rhi_tile_vendor_name */
static const char *ref_tile_vendor_name(uint32_t vendorId)
{
    switch (vendorId) {
    case 0x5143:
        return "Qualcomm";
    case 0x13B5:
        return "Arm";
    case 0x1010:
        return "Imagination";
    case 0x144D:
        return "Samsung";
    case 0x106B:
        return "Apple";
    case 0x14E4:
        return "Broadcom";
    default:
        return NULL;
    }
}

/* port/rhi/rhi_tile_rule.c:9-12 rhi_tile_phone */
static bool ref_tile_phone_rule(bool android, bool ios, bool fakedTileVendor)
{
    return android || ios || fakedTileVendor;
}

/* ICO_VK_FAKE_VENDOR's parse, as vk_device.c:1573-1583 (vkr_tile_rule_apply)
   and :1783-1791 (vkr_subpass_read_decide) write it */
static uint32_t ref_fake_vendor(const RefIn *in, bool *faked)
{
    uint32_t vendor = in->vendor;
    const char *fake = in->fakeVendor;
    *faked = false;
    if (fake && fake[0]) {
        char *end = NULL;
        const unsigned long v = strtoul(fake, &end, 0);
        if (end != fake && *end == '\0' && v <= 0xFFFFFFFFul) {
            vendor = (uint32_t)v;
            *faked = true;
        }
    }
    return vendor;
}

/* port/rhi/vk/vk_device.c:1571-1595 vkr_tile_rule_apply's phone (RhiLimits
   .tilePhone, vk_device.c:3409; other backends leave it false) */
static bool ref_tile_phone(const RefIn *in)
{
    if (!REF_VULKAN(in)) {
        return false;
    }
    bool faked;
    const uint32_t vendor = ref_fake_vendor(in, &faked);
    const char *name = ref_tile_vendor_name(vendor);
    const bool android = REF_ANDROID(in);
    const bool ios = in->os == RHI_DT_IOS && in->moltenvk; /* #ifdef ICO_IOS */
    return ref_tile_phone_rule(android, ios, faked && name != NULL);
}

/* port/rhi/vk/vk_device.c:2666-2670 the dynamic state's tilerDevice */
static bool ref_dynamic_tiler_device(const RefIn *in)
{
    if (!REF_VULKAN(in)) {
        return false;
    }
    return REF_ANDROID(in) ? true : ref_vkr_is_tiler(in->vendor);
}

/* port/rhi/vk/vk_device.c:2582 the memory budget's !g_vkr.tile.phone */
static bool ref_memory_budget_off(const RefIn *in)
{
    return REF_VULKAN(in) && ref_tile_phone(in);
}

/* port/rhi/rhi_driver_version.c:38-42 rhi_driver_is_qualcomm_proprietary */
static int ref_qcom_prop(uint32_t vendorId, uint32_t driverId)
{
    return vendorId == 0x5143u && (driverId == 8u || driverId == 0u);
}

/* port/rhi/rhi_pass_row.c:7-22 rhi_vk_pass_row */
static const char *ref_pass_row_intel(uint32_t vendorId, uint32_t driverId, uint32_t apiVersion,
                                      int windowsBuild)
{
    if (vendorId != 0x8086u || apiVersion > ((1u << 22) | (3u << 12) | 215u)) {
        return NULL;
    }
    const int own = driverId != 0 ? driverId == 5u : windowsBuild != 0;
    if (!own) {
        return NULL;
    }
    return "Intel's Windows driver at Vulkan 1.3.215 or older (the final drivers for 6th to 10th "
           "gen graphics crash in vkCmdBeginRendering)";
}

/* port/rhi/rhi_pass_row.c:24-36 rhi_vk_pass_row_qualcomm */
static const char *ref_pass_row_qualcomm(uint32_t vendorId, uint32_t driverId, int android)
{
    if (!android || vendorId != 0x5143u || (driverId != 8u && driverId != 0)) {
        return NULL;
    }
    return "Qualcomm's own driver (issue 123: its GPU hangs inside a pass begun with "
           "vkCmdBeginRendering; VkRenderPass objects are the driver's oldest path, the same "
           "picture)";
}

/* port/rhi/rhi_pass_row.c:38-48 rhi_vk_qprio_row (v0.11.7; removed in v0.11.8:
   rhi_device_table_equiv_test.c counts where it asked) */
static const char *ref_qprio_row(uint32_t vendorId, uint32_t driverId, int android)
{
    if (!android || vendorId != 0x5143u || (driverId != 8u && driverId != 0)) {
        return NULL;
    }
    return "Qualcomm's own driver on Android, issue 123: the system's own GPU work at a lower "
           "priority does not preempt the game's";
}

/* port/rhi/vk/vk_device.c:1643-1672 vkr_pass_row_apply's two rows (the
   Vulkan backend alone; VKR_LEGACY_PASSES 1) */
static void ref_pass_rows(const RefIn *in, const char **intelRow, const char **qcomRow)
{
    *intelRow = *qcomRow = NULL;
    if (!REF_VULKAN(in)) {
        return;
    }
    const int windows = in->os == RHI_DT_WINDOWS;
    *intelRow = ref_pass_row_intel(in->vendor, in->driverId, in->api, windows);
    const char *fakeRow = in->fakePassRow;
    const bool fakeQcom = fakeRow && strcmp(fakeRow, "qualcomm") == 0;
    const int android = REF_ANDROID(in);
    *qcomRow = *intelRow ? NULL
               : fakeQcom ? ref_pass_row_qualcomm(0x5143u, 8u, 1)
                          : ref_pass_row_qualcomm(in->vendor, in->driverId, android);
}

/* port/rhi/vk/vk_device.c:1681-1703 the row applied (passRowQcom): the
   device's own legacyPasses (deviceLegacy: no dynamic rendering) returns
   first, ICO_VK_LEGACY_PASSES=1 or 0 (ov) takes the path without it */
static bool ref_pass_row_qcom_applied(const char *qcomRow, bool deviceLegacy, int ov)
{
    if (deviceLegacy) {
        return false;
    }
    if (ov == 1) {
        return false;
    }
    if (qcomRow && ov == 0) {
        return false;
    }
    return qcomRow != NULL;
}

/* port/rhi/vk/vk_device.c:1781-1797 vkr_subpass_read_decide's class */
static int ref_subpass_class(const RefIn *in, bool passRowQcom)
{
    const char *intelRow, *qcomRow;
    ref_pass_rows(in, &intelRow, &qcomRow);
    const bool passRowHit = intelRow != NULL;
    bool faked;
    const uint32_t vendor = ref_fake_vendor(in, &faked);
    return passRowHit ? RHI_DT_SUBPASS_INTEL
           : passRowQcom || ref_qcom_prop(vendor, in->driverId) ? RHI_DT_SUBPASS_QCOM
           : vendor == 0x13B5u                                  ? RHI_DT_SUBPASS_ARM
                                                                : RHI_DT_SUBPASS_OTHER;
}

/* port/rhi/vk/vk_device.c:2284-2289 the attachment read's qualcomm term */
static bool ref_attachment_read_off(const RefIn *in)
{
    if (!REF_VULKAN(in)) {
        return false;
    }
    const char *fakeQc = in->fakeAdreno;
    const bool fakeAdreno = fakeQc && fakeQc[0] && fakeQc[0] != '0';
    return fakeAdreno || ref_qcom_prop(in->vendor, in->driverId);
}

/* port/rhi/vk/vk_device.c:3266-3315 kVuPlainOnly and vkr_vu_plain_only
   (RhiLimits.vuFallbackPlain, carefulPipelines, vuPrepassOff) */
static const struct {
    uint32_t vendor, deviceMask, device;
    bool qualcomm;
    const char *reason;
    const char *careful;
    const char *noPrepass;
    const char *noComputeRecord;
} kRefVuPlainOnly[] = {
    {0x5143u, 0xFFFFFFFFu, 0x43050A01u, true, "Adreno 740, issue 107",
     "Adreno 740, issues 93 and 107", "Adreno 740 on Qualcomm's driver, issue 123", NULL},
};

typedef struct RefVuPlain {
    const char *plain, *careful, *noPrepass;
} RefVuPlain;

static RefVuPlain ref_vu_plain_only(const RefIn *in)
{
    RefVuPlain o = {NULL, NULL, NULL};
    if (!REF_VULKAN(in)) {
        return o;
    }
    const uint32_t v = in->vendor, d = in->device;
    {
        const char *fake = in->fakeNoPrepass;
        if (fake && fake[0] && fake[0] != '0') {
            o.noPrepass = "ICO_VK_FAKE_NO_PREPASS";
        }
    }
    for (size_t i = 0; i < sizeof(kRefVuPlainOnly) / sizeof(kRefVuPlainOnly[0]); i++) {
        if (kRefVuPlainOnly[i].vendor == v &&
            (d & kRefVuPlainOnly[i].deviceMask) == kRefVuPlainOnly[i].device &&
            (!kRefVuPlainOnly[i].qualcomm || ref_qcom_prop(v, in->driverId))) {
            o.plain = kRefVuPlainOnly[i].reason;
            if (kRefVuPlainOnly[i].careful != NULL) {
                o.careful = kRefVuPlainOnly[i].careful;
            }
            if (kRefVuPlainOnly[i].noPrepass != NULL) {
                o.noPrepass = kRefVuPlainOnly[i].noPrepass;
            }
            return o;
        }
    }
    return o;
}

/* port/rhi/rhi_driver_version.c:70-84 findNoCase; memory_floor.c:14-29,
   present_fallback.c:24-39 has_word and rd_profile.c:242-255 hasWord are the
   same test */
static int ref_has_word(const char *s, const char *word)
{
    const size_t n = strlen(word);
    for (; s != NULL && *s != '\0'; s++) {
        size_t i = 0;
        while (i < n && s[i] != '\0' && (s[i] | 0x20) == (word[i] | 0x20)) {
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

/* port/rhi/rhi_driver_version.c:197-213 rhi_driver_compiler_serial with
   vk_pipeline.c:45-53 vkr_compile_serial_init's fake */
static const char *ref_compiler_serial(const RefIn *in)
{
    if (!REF_VULKAN(in)) {
        return NULL;
    }
    const char *why = NULL;
    if (in->vendor == 0x1010u && in->text != NULL && ref_has_word(in->text, "b-series")) {
        why = "PowerVR B-Series";
    }
    const char *fake = in->fakeSerial;
    if (why == NULL && fake != NULL && fake[0] == '1') {
        why = "ICO_VK_FAKE_SERIAL";
    }
    return why;
}

/* port/rhi/vk/vk_device.c:2491-2494 the skin check's device default
   (ICO_RD_VU_SKIN_CHECK unset), the pipeline robustness ask's term */
static bool ref_pipe_robust_ask(const RefIn *in)
{
    return REF_VULKAN(in) && in->vendor == 0x5143u;
}

/* port/platform/present_fallback.c:41-46 */
static int ref_fallback_eligible(unsigned vendorId, unsigned driverId, const char *driverText)
{
    return vendorId != 0 && driverId != 18u && !ref_has_word(driverText, "mesa") &&
           !ref_has_word(driverText, "turnip");
}

/* port/platform/present_fallback.c:48-52 */
static int ref_qualcomm_own(unsigned vendorId, unsigned driverId, const char *driverText)
{
    return vendorId == 0x5143u && ref_fallback_eligible(vendorId, driverId, driverText);
}

/* port/platform/present_fallback.c:79-84 ico_present_fallback_start (mode
   auto, vsync off) */
static int ref_fifo_start(const RefIn *in)
{
    return REF_ANDROID(in) && ref_qualcomm_own(in->vendor, in->driverId, in->text);
}

/* port/platform/memory_floor.c:31-42 ico_memory_floor_driver */
static int ref_floor_driver(unsigned vendor_id, unsigned driver_id, const char *driver_text)
{
    if (vendor_id == 0u) {
        return 0;
    }
    if (vendor_id == 0x5143u && driver_id != 18u && !ref_has_word(driver_text, "mesa") &&
        !ref_has_word(driver_text, "turnip")) {
        return 1;
    }
    return 2;
}

/* port/platform/android/gpu_driver_android.c:822-823 s_onStock (an Android
   build) */
static bool ref_turnip_note(const RefIn *in)
{
    return REF_ANDROID(in) && !in->customDriver && in->vendor == 0x5143u;
}

/* port/render/rd_profile.c:262-266 rd__driver_is_turnip */
static bool ref_driver_is_turnip(const RefIn *in)
{
    return in->driverId == 18u || ref_has_word(in->text, "turnip");
}

/* port/render/rd_profile.c:283-314 classOf with rd__profile_vendor_adreno
   (:318-321) */
static int ref_classOf(const RefIn *in)
{
    switch (in->backend) {
    case RHI_DT_D3D12:
        return RHI_DT_CLASS_D3D12;
    case RHI_DT_OPENGL:
        return RHI_DT_CLASS_OPENGL;
    case RHI_DT_D3D11:
        return RHI_DT_CLASS_D3D11;
    default:
        break;
    }
    if (!in->moltenvk && in->vendor == 0x5143u) {
        return ref_driver_is_turnip(in) ? RHI_DT_CLASS_ADRENO_TURNIP
                                        : RHI_DT_CLASS_ADRENO_QUALCOMM;
    }
    if (in->moltenvk) {
        return RHI_DT_CLASS_APPLE;
    }
    switch (in->vendor) {
    case 0x13B5u:
        return RHI_DT_CLASS_MALI;
    case 0x1010u:
        return RHI_DT_CLASS_POWERVR;
    default:
        return ref_lim_tiler(in) ? RHI_DT_CLASS_TILER : RHI_DT_CLASS_DESKTOP;
    }
}

/* port/render/rd_profile.c:323-331 rd__profile_adreno, :335-339
   rd__profile_powervr, :342-358 rd__profile_tile_class */
static bool ref_profile_adreno(const RefIn *in)
{
    const int c = ref_classOf(in);
    return c == RHI_DT_CLASS_ADRENO_QUALCOMM || c == RHI_DT_CLASS_ADRENO_TURNIP;
}

static bool ref_profile_powervr(const RefIn *in)
{
    return ref_classOf(in) == RHI_DT_CLASS_POWERVR;
}

static int ref_profile_tile_class(const RefIn *in)
{
    switch (ref_classOf(in)) {
    case RHI_DT_CLASS_POWERVR:
        return 1;
    case RHI_DT_CLASS_DESKTOP:
    case RHI_DT_CLASS_D3D12:
    case RHI_DT_CLASS_OPENGL:
    case RHI_DT_CLASS_D3D11:
        return 2;
    default:
        return 0;
    }
}

/* port/render/rd_profile.c:455-519 rd__profile_init's class part, with the
   device's facts as inputs (ICO_RD_LOCAL_PROBE unset: probeEnv -1) */
typedef struct RefRun {
    bool uintView, localRead, localReadSubpass, windowed;
} RefRun;

typedef struct RefProfile {
    bool tiler, desktopLocal, fogLocal, dateStencil, blurFold, glowFold, uintViewWork,
        uintViewScene, fxDstLocal, auraBox, passArea, passCut, shadowMerge, sheetBake2x;
} RefProfile;

static RefProfile ref_profile_init(const RefIn *in, const RefRun *run)
{
    RefProfile p;
    memset(&p, 0, sizeof p);
    const int c = ref_classOf(in);
    const bool tile = c != RHI_DT_CLASS_DESKTOP && c != RHI_DT_CLASS_D3D12 &&
                      c != RHI_DT_CLASS_OPENGL && c != RHI_DT_CLASS_D3D11;
    p.tiler = ref_lim_tiler(in);
    const bool localRead = run->localRead;
    const bool desk = c == RHI_DT_CLASS_DESKTOP && localRead;
    const int probeEnv = -1;
    p.desktopLocal = desk && (run->windowed || probeEnv >= 0);
    const bool local = (tile || p.desktopLocal) && localRead;
    const bool subpass = run->localReadSubpass;
    p.fogLocal = local && !subpass;
    p.dateStencil = local;
    p.blurFold = c != RHI_DT_CLASS_MALI;
    p.glowFold = !tile;
    p.uintViewWork = !tile && run->uintView;
    p.uintViewScene = !tile && run->uintView;
    p.fxDstLocal = local && !subpass;
    p.auraBox = tile;
    p.passArea = tile;
    p.passCut = tile;
    p.shadowMerge = tile;
    p.sheetBake2x = tile;
    return p;
}

/* port/render/rd_lamps.c:307-345 rd__lamps_default_bits' class part (the
   build's RD_LAMPS_MS_DEFAULT and RD_LAMPS_FILLPASTE_DEFAULT taken as 1: the
   call site keeps them) */
typedef struct RefLamps {
    bool ms, fillpaste, area;
} RefLamps;

static RefLamps ref_lamps(int tileClass, bool tilePhone, bool vulkan, bool passArea)
{
    RefLamps b = {false, false, false};
    if (tileClass == 1 && vulkan) {
        b.ms = true;
    }
    if (tileClass == 1 && vulkan) {
        b.fillpaste = true;
    }
    const bool cls = tileClass == 1 || (tileClass == 2 && !tilePhone);
    if (cls && vulkan && passArea) {
        b.area = true;
    }
    return b;
}

/* port/render/rd_fill11.c:185-193 classOk */
static int ref_fill11_class(int c, bool tilePhone)
{
    if (c == 1) {
        return 1;
    }
    return c == 2 && !tilePhone ? 2 : 0;
}

/* port/render/rd_vu_class.c:31-34 rd__vu_class_measured */
static bool ref_vu_class_measured(const char *name)
{
    return name != NULL && strstr(name, "PowerVR D-Series DXT-48-1536") != NULL;
}

/* port/render/rd_vu_compact.c:165-172 rd_vu_cmp_class (as MALI179 left it) */
static int ref_cmp_class(const char *name, uint32_t vendor, bool moltenvk, bool dxt, bool byTests)
{
    if (byTests ||
        (!moltenvk && vendor == 0x13B5u && name != NULL && strstr(name, "Mali-G52") != NULL)) {
        return RHI_DT_CMP_G52;
    }
    return dxt ? RHI_DT_CMP_DXT : RHI_DT_CMP_OTHER;
}

/* port/render/rd_vu_compact.c:290-306 the class's inputs: dxt
   (rd__profile_powervr() && rd__vu_class_measured(name)), byTests
   (ICO_RD_VU_COMPACT_ARM "1"); port/platform/attempt_ledger.c:595-598
   cmpClassDefault with RD_VU_COMPACT_DEFAULT and RD_VU_COMPACT_ARM_DEFAULT */
static int ref_cmp_class_of(const RefIn *in)
{
    const bool byTests = in->compactArm != NULL && strcmp(in->compactArm, "1") == 0;
    const bool dxt = ref_profile_powervr(in) && ref_vu_class_measured(in->name);
    return ref_cmp_class(in->name, in->vendor, in->moltenvk, dxt, byTests);
}

static int ref_cmp_default(int cls)
{
    return cls == RHI_DT_CMP_G52 ? RD_VU_COMPACT_ARM_DEFAULT
           : cls == RHI_DT_CMP_DXT ? RD_VU_COMPACT_DEFAULT
                                   : 0;
}

/* port/render/rd_no_volume.c:498-518 classBase, breakMeasured */
static bool ref_no_volume_base(const RefIn *in)
{
    return REF_VULKAN(in) && (in->moltenvk || ref_profile_powervr(in));
}

static bool ref_no_volume_break(const RefIn *in)
{
    return ref_no_volume_base(in) && ref_profile_powervr(in) && ref_vu_class_measured(in->name);
}

/* port/render/rd_pipeline.c:3605-3664 rd__vu_set_choose (envPlain,
   envExtended false, no hostWhy): kind, brief, noBeside */
typedef struct RefVuSet {
    const char *kind, *brief;
    bool noBeside;
} RefVuSet;

static RefVuSet ref_vu_set(const char *vuFallbackPlain, bool turnip)
{
    RefVuSet o;
    o.kind = "extended set (no device-table row, no Turnip driver, no lost start, no [dev] "
             "setting asks for the plain one)";
    o.brief = "";
    o.noBeside = false;
    if (vuFallbackPlain) {
        o.kind = "plain set by the device table";
        o.brief = vuFallbackPlain;
        o.noBeside = true;
    } else if (turnip) {
        o.kind = "plain set";
        o.brief = "Turnip driver, issue 163";
        o.noBeside = true;
    }
    return o;
}

/* the RhiLimits.tiler readers (each copies the tiler term alone; the rest
   of each condition stays at its call site):
     port/render/rd_core.c:2186-2188 MSAA: !(l->tiler && n > 4u)
     port/render/rd_present.c:458-461 the 4096 cap: rhi_limits()->tiler
     port/render/rd_present.c:474-481 the MSAA budget: rhi_limits()->tiler
     port/render/rd_present.c:2340-2349 shotLateOn: return rhi_limits()->tiler
     port/render/rd_late_pass.c:260-262 g_rd.profile.tiler && Vulkan
     port/render/rd_uber.c:134-136 ask < 0 && !lim->tiler: off
     port/render/rd_fog_path.c:206, :214-215 rhi_limits()->tiler
     port/render/rd_vis.c:637-642 g_rd.profile.tiler (= RhiLimits.tiler)
   ref_lim_tiler above is each of them. */

/* ------------------------------------------------------------------ DT3
 * The renderer's rules as they read at int/v0117 6839bd7b8 (after DT2's
 * merge), where the copies above did not hold them whole. */

/* port/render/rd_profile.c:318-321 rd__profile_vendor_adreno */
static bool ref_profile_vendor_adreno(uint32_t vendorId, bool moltenvk)
{
    return !moltenvk && vendorId == 0x5143u;
}

/* port/render/rd_stencil_selftest.c:132-141 the cut's off words' order
   (an Adreno class first, then not the PowerVR class): which of the two
   says why (0 neither) */
static int ref_stencil_off_why(const RefIn *in)
{
    if (ref_profile_adreno(in)) {
        return 1;
    }
    if (!ref_profile_powervr(in)) {
        return 2;
    }
    return 0;
}

/* port/render/rd_lamps.c:1450-1451 the 4-sample pass's class on a device
   (dl: g_rd.hasDevice) */
static bool ref_lamps_ms_powervr(const RefIn *in, bool dl)
{
    return dl && ref_profile_tile_class(in) == 1 && REF_VULKAN(in);
}

/* port/render/rd_vu_compact.c:329 the DXT (lim != NULL) */
static bool ref_cmp_dxt(const RefIn *in, bool lim)
{
    return lim && ref_profile_powervr(in) && ref_vu_class_measured(in->name);
}

/* port/render/rd_vu_record.c:230-231 the PowerVR class's rule's inputs
   (lim != NULL) */
static void ref_vu_record_class(const RefIn *in, bool lim, bool *powervr, bool *measured)
{
    *powervr = lim && ref_profile_powervr(in);
    *measured = *powervr && ref_vu_class_measured(in->name);
}

/* port/render/rd_no_volume.c:498-513 classBase and breakMeasured with
   g_rd.hasDevice */
static bool ref_no_volume_base_dev(const RefIn *in, bool hasDevice)
{
    return hasDevice && ref_no_volume_base(in);
}

/* port/render/rd_fog_path.c:206 the desktop local order's tiler term and
   :214 the tiler term (hasDevice) */
static bool ref_fog_tiler(const RefIn *in, bool hasDevice)
{
    return hasDevice && ref_lim_tiler(in);
}

/* The old rules on no device (before rhi_init, after rhi_shutdown: the
   backend's limits zeroed, rhi_backend() the backend): rd_profile.c classOf
   read vendor 0, moltenvk false, tiler false, the driver id 0 and no text;
   the readers of RhiLimits.tiler and .tilePhone read false */
static RefIn ref_no_device(int backend)
{
    RefIn in;
    memset(&in, 0, sizeof in);
    in.backend = backend;
    in.os = RHI_DT_LINUX; /* no OS row: the zeroed limits' tiler false on every OS */
    return in;
}

/* ---- DT4: the platform's call sites, as they read at int/v0117 c91c64d9d
   (before they read the table) ---- */

/* port/platform/present_fallback.c:54-72 ico_present_fallback_gate (the
   mode's values auto 0, off 1, fifo 2; the gate's no 0, yes 1, kept 2, other
   3: present_fallback.h) */
static int ref_fallback_gate(int vsync, int mailbox, int vulkan, unsigned vendorId,
                             unsigned driverId, const char *driverText, int mode)
{
    if (vsync || !mailbox || !vulkan || !ref_fallback_eligible(vendorId, driverId, driverText) ||
        mode == 2) {
        return 0;
    }
    if (mode == 1) {
        return 2;
    }
    return ref_qualcomm_own(vendorId, driverId, driverText) ? 1 : 3;
}

/* port/platform/present_fallback.c:74-84 ico_present_fallback_start_asked
   and ico_present_fallback_start */
static int ref_fallback_start(int mode, int android, int vsync, unsigned vendorId,
                              unsigned driverId, const char *driverText)
{
    return (mode == 0 && android) && !vsync && ref_qualcomm_own(vendorId, driverId, driverText);
}

/* port/platform/window_present_fallback.c:24-29 qualcomm_own_now, the
   question window_fifo_set (:52-54) hands the rhi where
   ico_present_fallback_start_asked(s_mode, ICO_PRESENT_FALLBACK_ANDROID);
   -1: no question handed over (the first swapchain as the options say) */
static int ref_window_fifo_question(int mode, int androidBuild, unsigned vendorId,
                                    unsigned limitsDriverId, const char *driverText)
{
    if (!(mode == 0 && androidBuild)) {
        return -1;
    }
    return ref_qualcomm_own(vendorId, limitsDriverId, driverText);
}

/* port/platform/android/gpu_driver_android.c:822-823 s_onStock (the Android
   build's file), rhi_limits() NULL or not */
static bool ref_on_stock(bool customDriver, bool haveLimits, uint32_t vendorId)
{
    return !customDriver && haveLimits && vendorId == 0x5143u;
}

/* port/platform/attempt_ledger.c: the words of a row no device rule holds */
/* :393 ico_attempt_decide_no_volume_break */
#define REF_NA_NO_VOLUME "not the PowerVR or MoltenVK class under Vulkan"
/* :519 ico_attempt_decide_pass_objects */
#define REF_NA_PASS_OBJECTS "not Qualcomm's own driver"
/* :557 ico_attempt_decide_vu_compute */
#define REF_NA_VU_COMPUTE "not the PowerVR class under Vulkan"
/* :632-633 ico_attempt_decide_compaction */
#define REF_NA_COMPACTION                                                                          \
    "off by default: measured on the Mali-G52 only (and on the PowerVR D-Series "                  \
    "DXT-48-1536's compute record)"
