/* rhi_device_table.c: the device table's rows, its matcher and its lines
 * (rhi_device_table.h; docs/port/DEVICE_TABLE.md).  Pure: no Vulkan, no
 * I/O, no environment.
 *
 * Every value below is today's behaviour, moved verbatim from the rule that
 * holds it (the rule's file is named in the row's comment); the proof is
 * port/rhi/test/rhi_device_table_equiv_test.c, which keeps a copy of each
 * old rule and compares on every identity of its matrix. */
#include "rhi_device_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK (1u << RHI_DT_VULKAN)
#define OS_ANDROID (1u << RHI_DT_ANDROID)
#define OS_IOS (1u << RHI_DT_IOS)
#define N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* ------------------------------------------------------------------ rows */

/* vk_device.c vkr_is_tiler before the table, RhiLimits.tiler (rd_profile.c's tile-based
   vendors outside Android too: rhi_tile_rule.c rhi_tile_vendor_name) */
static const RhiDeviceSetting kTileVendors[] = {
    {RHI_DC_TILER, 1, "a tile-based GPU", NULL, "",
     "commit a54e2f896 (the fog's copy on tile-based GPUs)", "v0.5.0",
     "never: these vendors' GPUs are tile-based by design"},
};

/* vk_device.c vkr_fill_limits (every Android GPU) and rhi_tile_rule.c
   rhi_tile_phone (an Android build) */
static const RhiDeviceSetting kAndroid[] = {
    {RHI_DC_TILER, 1, "a tile-based GPU (every Android GPU)", NULL, "", "commit 6ba1c875a",
     "v0.5.2", "never: a phone GPU missing from the vendor list would take the desktop defaults"},
    {RHI_DC_TILE_PHONE, 1,
     "a tile-based phone GPU: v0.11.2's wide barriers and single submission wait, the loss rung",
     NULL, "issues 179 and 211",
     "commit 6fb557768; the Mali-G52 and the Adreno 710 lost the device in the first 3D scene "
     "on v0.11.3 and not on v0.11.2 (v0114/evidence/179, v0114/evidence/211)",
     "v0.11.4", "a phone log without a loss under v0.11.3's scopes and wait"},
};

/* rhi_tile_rule.c rhi_tile_phone (iOS through MoltenVK) */
static const RhiDeviceSetting kIos[] = {
    {RHI_DC_TILE_PHONE, 1,
     "a tile-based phone GPU: v0.11.2's wide barriers and single submission wait, the loss rung",
     NULL, "issues 179 and 211", "commit 6fb557768 (Apple's phone GPUs taken with Android's)",
     "v0.11.4", "an iOS log under v0.11.3's scopes and wait"},
};

/* the rules keyed on RhiLimits.tiler: vk_device.c (dynamic state),
   rd_core.c (MSAA), rd_present.c (scene cap, MSAA budget, late shot),
   rd_late_pass.c, rd_uber.c, rd_fog_path.c, rd_vis.c.  The late pass reads
   the profile's tiler and the backend (Vulkan): RhiLimits.tiler is set on
   Vulkan alone, so the backend term is that */
static const RhiDeviceSetting kTiler[] = {
    {RHI_DC_DYNAMIC_STATE_OFF, 1, "dynamic depth and blend state off by default", NULL, "",
     "commit b6aed5556: a PowerVR phone's pictures shook between ticks with it on and were "
     "steady with it off",
     "v0.7.10", "a tile-based phone steady between ticks with [dev] dynamic_state = true"},
    {RHI_DC_MSAA_MAX4, 1, "MSAA up to 4 samples (a memory limit on what Options offer)", NULL, "",
     "commit aaf47d91d", "v0.6.0", "a queried memory budget in place of the class"},
    {RHI_DC_SCENE_CAP_4096, 1,
     "the scene at most 4096 pixels a side (a memory limit: phones share memory with the system)",
     NULL, "", "commit ab60630ce", "v0.5.1", "a queried memory budget in place of the class"},
    {RHI_DC_MSAA_BUDGET, 1, "the MSAA scene's scale halved to its memory budget (a limit)", NULL,
     "", "commit 522a08a16 (RD_MSAA_TILER_BUDGET)", "v0.6.0",
     "a queried memory budget in place of the class"},
    {RHI_DC_SHOT_LATE, 1, "the state picture's late shot", NULL, "issue 179", "commit 771a52f71",
     "v0.11.5", "a phone log whose state pictures cost nothing with the old path"},
    {RHI_DC_LATE_PASS_TILER, 1, "list 11's late pass (RD_LATE_DEFAULT) under Vulkan", NULL, "",
     "commit 7a78f02b4", "v0.11.5", "a tile-based phone's pass times without it"},
    {RHI_DC_UBER_AUTO, 1, "the uber shader on under auto", NULL, "", "commit 01eff8f7c", "v0.10.0",
     "a desktop log where it pays"},
    {RHI_DC_FOG_ORDER_TILER, 1, "the tile-based fog order", NULL, "", "commit 336bc4e00", "v0.5.2",
     "never: a tile-based GPU's copy path first"},
    {RHI_DC_CULL_FRUSTUM, 1, "cull frustum by default", NULL, "issue 179",
     "commit fa98a57ba: the occlusion test cost a phone's CPU more than it saved", "v0.11.2",
     "a phone log where the occlusion test saves time"},
};

/* vk_device.c (the memory budget's ask), rd_vu_skin_check.c skAsBefore */
static const RhiDeviceSetting kTilePhone[] = {
    {RHI_DC_MEMORY_BUDGET_OFF, 1, "VK_EXT_memory_budget not asked", NULL, "",
     "commit 1859caa76 (a phone's free memory is the system's own measure)", "v0.11.5",
     "never: Android's MemAvailable is the measure there"},
    {RHI_DC_SKIN_AS_BEFORE, 1, "the skin check issues v0.11.2's work", NULL, "issue 211",
     "commit cf4256f8d", "v0.11.4", "a phone log without a loss under the v0.11.3 check"},
};

/* rhi_pass_row.c rhi_vk_pass_row; vk_device.c vkr_subpass_read_decide */
static const RhiDeviceSetting kIntelWin[] = {
    {RHI_DC_PASS_OBJECTS, RHI_DT_PASS_INTEL,
     "render passes as VkRenderPass objects, the store passes on a colour target",
     "Intel's Windows driver at Vulkan 1.3.215 or older (the final drivers for 6th to 10th "
     "gen graphics crash in vkCmdBeginRendering)",
     "", "an HD 530 on 101.2125 crashed in vkCmdBeginRendering (v0113/evidence/igpu)", "v0.11.3",
     "never: Intel ships no newer driver for 6th to 10th gen graphics"},
    {RHI_DC_SUBPASS_CLASS, RHI_DT_SUBPASS_INTEL, "the subpass read probed", NULL, "",
     "commit 7a717025c (RHI_SUBPASS_PROBE_INTEL_DEFAULT)", "v0.11.6",
     "never while the row holds the passes"},
};

/* rhi_pass_row.c rhi_vk_pass_row_qualcomm */
static const RhiDeviceSetting kQualcommAndroid[] = {
    {RHI_DC_PASS_OBJECTS, RHI_DT_PASS_QUALCOMM, "render passes as VkRenderPass objects",
     "Qualcomm's own driver (issue 123: its GPU hangs inside a pass begun with "
     "vkCmdBeginRendering; VkRenderPass objects are the driver's oldest path, the same "
     "picture)",
     "issue 123",
     "the Adreno 740 (512.676) and Adreno 710 (512.800) hang inside a pass begun with "
     "vkCmdBeginRendering (v0117/evidence/123)",
     "v0.11.6", "the reporter's next log: no hang inside a pass"},
};

/* rhi_driver_version.c rhi_driver_is_qualcomm_proprietary: vk_device.c's
   attachment read and subpass class */
static const RhiDeviceSetting kQualcomm[] = {
    {RHI_DC_ATTACHMENT_READ_OFF, 1, "the rasterization-order attachment read not offered", NULL,
     "issue 89", "an Adreno 830 on 512.800.72 drew wrong colours through it (commit 92347c556)",
     "v0.7.8", "a Qualcomm driver that draws issue 89's frames right (no version known)"},
    {RHI_DC_SUBPASS_CLASS, RHI_DT_SUBPASS_QCOM, "the subpass read off by default", NULL,
     "issue 123", "commit 7a717025c", "v0.11.6", "a probe on Qualcomm's driver without a loss"},
};

/* memory_floor.c ico_memory_floor_driver (vendor 0), present_fallback.c
   ico_present_fallback_driver_eligible (vendor 0) */
static const RhiDeviceSetting kVendorUnknown[] = {
    {RHI_DC_MEMORY_FLOOR_CLASS, RHI_DT_FLOOR_UNKNOWN,
     "the free-memory floor's cautious rule (the driver not known)", NULL, "issue 161",
     "commit f4b238e57", "v0.10.0", "never: no vendor id names no driver"},
    {RHI_DC_FALLBACK_ELIGIBLE, 0, "the present fallback's switch not taken", NULL, "issue 123",
     "commit d8624d3ba", "v0.10.2", "never: no vendor id names no driver"},
};

/* memory_floor.c ico_memory_floor_driver, present_fallback.c
   ico_present_fallback_qualcomm_own (the gate's yes) */
static const RhiDeviceSetting kQualcommText[] = {
    {RHI_DC_MEMORY_FLOOR_CLASS, RHI_DT_FLOOR_QUALCOMM,
     "the free-memory floor at 17 % up to 2048 MB, its stop kept to the next start", NULL,
     "issue 161", "commit f4b238e57 (v096/evidence/161)", "v0.10.0",
     "a Qualcomm driver log that starts without the system ending the app"},
    {RHI_DC_FALLBACK_SWITCH, RHI_DT_SWITCH_YES, "the present fallback's switch to FIFO allowed",
     NULL, "issue 123",
     "commit d8624d3ba; on a Mali-G52 the switch lengthened the blocks (issue 179)", "v0.10.2",
     "a Qualcomm driver log whose mailbox presents do not block"},
};

/* present_fallback.c ico_present_fallback_start (an Android build) */
static const RhiDeviceSetting kQualcommTextAndroid[] = {
    {RHI_DC_FIFO_FROM_START, 1, "FIFO from the first swapchain with vsync off", NULL, "issue 123",
     "commit fc627fcb2", "v0.10.2", "a Qualcomm driver log whose mailbox presents do not block"},
};

/* present_fallback.c ico_present_fallback_driver_eligible */
static const RhiDeviceSetting kMesaDrivers[] = {
    {RHI_DC_FALLBACK_ELIGIBLE, 0, "the present fallback's switch not taken (a Mesa driver)", NULL,
     "", "commit d8624d3ba", "v0.10.2", "a Mesa driver log whose mailbox presents block"},
};

/* vk_device.c: the skin check's default when ICO_RD_VU_SKIN_CHECK is unset
   (vendor 0x5143, no MoltenVK test) */
static const RhiDeviceSetting kQualcommVendor[] = {
    {RHI_DC_PIPE_ROBUST_ASK, 1,
     "VK_EXT_pipeline_robustness asked for the skin check (with robust access on)", NULL,
     "issue 163", "commit 68d97aa77", "v0.10.4", "the skin check's end"},
};

/* rd_profile.c classOf, rd_vu_skin_check.c rd__skin_check_default's input */
static const RhiDeviceSetting kAdreno[] = {
    {RHI_DC_PROFILE_CLASS, RHI_DT_CLASS_ADRENO_QUALCOMM, "profile class Adreno, Qualcomm's driver",
     NULL, "", "commit 85e9bc65e", "v0.7.10", "never: a class"},
    {RHI_DC_SKIN_CHECK_DEFAULT, 1, "the skin check on with a window", NULL, "issue 163",
     "commit 68d97aa77", "v0.10.4", "an Adreno log whose checks never fire"},
};

/* rd_profile.c classOf's Turnip split (rd__driver_is_turnip) */
static const RhiDeviceSetting kTurnipAdreno[] = {
    {RHI_DC_PROFILE_CLASS, RHI_DT_CLASS_ADRENO_TURNIP, "profile class Adreno, Turnip", NULL, "",
     "commit 712e015fa", "v0.9.7", "never: a class"},
};

/* rd_pipeline.c rd__vu_set_choose's Turnip branch */
static const RhiDeviceSetting kTurnip[] = {
    {RHI_DC_VU_FALLBACK_PLAIN, RHI_DT_VUSET_TURNIP, "the VU fallback's plain set",
     "Turnip driver, issue 163", "issue 163",
     "a skinned draw's vertex showed as a sliver through the extended set (v0105/evidence/123)",
     "v0.9.7", "a Mesa version a device test shows free of the sliver with the extended set"},
};

/* vk_device.c kVuPlainOnly before the table (read by vkr_vu_plain_only
 * now), the table's seed: the Adreno 740 by the whole chip id both devices
 * report (0x43050A01, the issue 93 and 107 logs), nothing near it (Mesa's
 * freedreno device list, src/freedreno/common/freedreno_devices.py, gives
 * 0x43050A00 to the Adreno A32, 0x43050B00 to the 740v3 and 0x43051401 to
 * the Adreno 750, none of them seen failing), Qualcomm's own driver alone
 * (Turnip on the same GPU is another compiler).  ICO_RD_VU_EXTENDED=1,
 * [dev] vu_prepass and [dev] pipeline_set stay over it at their call sites.
 *   The plain set: on the Adreno 740 (AYN Thor, issue 107, and AYN Odin 2
 *   Portal, issue 93; driver 512.676.53, compiler E031.41.03.47) the whole
 *   device froze, needing a reboot, while the driver compiled the first
 *   pipelines with a VU vertex shader of v0.7.5 to v0.7.7 (46,470 to 93,407
 *   words); v0.6.1's (20,975 to 32,709 words) built (docs/port/SHADERS.md,
 *   The size history).  The plain variants are 10,011 to 23,305 words, the
 *   record path's largest module about 20,000 (an estimate until the build
 *   measures it).
 *   The careful set: with v0.7.7 both devices died inside the 2D half of
 *   the blind set (its first 327 keys at Original 4:3 with the fixed-function
 *   blend, before any VU key): the Odin 2 Portal at 201 of 527 finished, the
 *   process shrinking from 194 MB to 47 MB until the system ended it (issue
 *   93), the Thor at 307 or later with the whole device frozen (issue 107);
 *   the same since v0.5.3, while v0.5.2 and v0.6.1 started.  No one module
 *   explains it (font_sheet_ps and yuv_ps were as large in v0.6.1); memory
 *   pressure from the parallel compiles is the hypothesis the careful policy
 *   answers (inferred, not measured).
 *   The pre-pass off: under Qualcomm's own driver (issue 123, a Xiaomi Pad
 *   6S Pro on driver 512.676.1, compiler E031.41.03.64) v0.11.0 is the first
 *   build to run the pre-pass there, and the record path's modules have one
 *   build on every device (vu_tri_rule_pre_vs 17,289 words,
 *   vu_tri_tick_pre_vs 16,627: the card test and the tick cameras the plain
 *   set exists to keep from this compiler).  Its self-test passed on its
 *   one-row frame, then the first scene's 8 replays drew a full-screen noise
 *   picture and submission 369 never finished (the GPU hang at 25.5 s).
 *   Turnip on the same GPU keeps the pre-pass ("runs better and no
 *   graphical glitches"). */
static const RhiDeviceSetting kAdreno740[] = {
    {RHI_DC_VU_FALLBACK_PLAIN, RHI_DT_VUSET_TABLE, "the VU fallback's plain set",
     "Adreno 740, issue 107", "issue 107",
     "the whole device froze compiling v0.7.5 to v0.7.7's VU vertex shaders (driver 512.676.53, "
     "compiler E031.41.03.47; commit d3f88bd03)",
     "v0.7.8", "a driver newer than 512.676 building the extended set"},
    {RHI_DC_CAREFUL_PIPELINES, 1, "the careful start-up set", "Adreno 740, issues 93 and 107",
     "issues 93 and 107",
     "both devices died inside the blind set's 2D half with v0.7.7 (commit 4dd2f86c7)", "v0.7.8",
     "a driver newer than 512.676 making the start-up set in parallel"},
    {RHI_DC_VU_PREPASS_OFF, 1, "the VU pre-pass off", "Adreno 740 on Qualcomm's driver, issue 123",
     "issue 123",
     "the first scene's replays drew noise and submission 369 never finished (driver 512.676.1, "
     "compiler E031.41.03.64; commit 85adff65e)",
     "v0.11.1",
     "a driver newer than 512.676 (compiler E031.41.03.64) running the pre-pass without a hang"},
};

/* gpu_driver_android.c ico_gpu_driver_android_device_up (s_onStock) */
static const RhiDeviceSetting kStockQualcommAndroid[] = {
    {RHI_DC_TURNIP_NOTE, 1, "the hang and loss boxes name a Turnip package", NULL, "",
     "UI_STR_GPU_HANG_TURNIP", "v0.9.5", "never: a hint, no renderer choice"},
};

/* vk_device.c vkr_subpass_read_decide (the vendor, ICO_VK_FAKE_VENDOR's) */
static const RhiDeviceSetting kArm[] = {
    {RHI_DC_SUBPASS_CLASS, RHI_DT_SUBPASS_ARM, "the subpass read off by default", NULL, "issue 179",
     "commit 7a717025c", "v0.11.6", "a probe on an Arm driver without a loss"},
};

/* rd_profile.c classOf and the blur fold's row */
static const RhiDeviceSetting kMali[] = {
    {RHI_DC_PROFILE_CLASS, RHI_DT_CLASS_MALI, "profile class Mali", NULL, "", "commit 85e9bc65e",
     "v0.7.10", "never: a class"},
    {RHI_DC_BLUR_FOLD_OFF, 1, "the blur fold off (the G52 is bound by its ALU)", NULL, "",
     "commit cab17538a", "v0.9.4", "a Mali log where the fold saves time"},
};

/* rd_vu_compact.c rd_vu_cmp_class (no backend test: as the rule reads its
   arguments) */
static const RhiDeviceSetting kMaliG52[] = {
    {RHI_DC_COMPACTION_CLASS, RHI_DT_CMP_G52, "the index compaction's Mali-G52 class", NULL,
     "issue 179", "commit 67dedc5d5", "v0.11.6", "never: a class"},
#if RD_VU_COMPACT_ARM_DEFAULT
    {RHI_DC_COMPACTION_DEFAULT, 1, "the index compaction on where its self-test passes",
     "RD_VU_COMPACT_ARM_DEFAULT 1", "issue 179", "an rc build's RD_VU_COMPACT_ARM_DEFAULT 1",
     "v0.11.7", "the cause of v0.11.6's black pictures found"},
#else
    {RHI_DC_COMPACTION_DEFAULT, 0, "the index compaction off by default",
     "off by default on the Mali-G52: it blacked out the picture there in v0.11.6 (issue 179; "
     "RD_VU_COMPACT_ARM_DEFAULT 0)",
     "issue 179",
     "the moto g24 power's v0.11.6 pictures black from the first 3D picture on "
     "(v0117/evidence/179)",
     "v0.11.7", "the cause found, or a G52 log of complete pictures with it on"},
#endif
};

/* rd_profile.c classOf, rd_lamps.c rd__lamps_default_bits' class part,
   rd_stencil_selftest.c, rd_vu_record.c (the PowerVR class's rule),
   rd_no_volume.c classBase */
static const RhiDeviceSetting kPowervr[] = {
    {RHI_DC_PROFILE_CLASS, RHI_DT_CLASS_POWERVR, "profile class PowerVR", NULL, "",
     "commit 85e9bc65e", "v0.7.10", "never: a class"},
    {RHI_DC_TILE_CLASS, 1, "tile class 1 (the shadow tiles' and fill11's PowerVR class)", NULL, "",
     "commit 71578e502", "v0.11.5", "never: a class"},
    {RHI_DC_LAMPS_MS, 1, "the lamps' 4-sample pass's class (RD_LAMPS_MS_DEFAULT decides)", NULL, "",
     "commit f02f7b75a (the form the owner accepted on the PowerVR phone)", "v0.11.5",
     "another class measured with it"},
    {RHI_DC_LAMPS_FILLPASTE, 1,
     "the crack fill in the paste's pass's class (RD_LAMPS_FILLPASTE_DEFAULT decides)", NULL, "",
     "commit 7c69c3735", "v0.11.5", "another class measured with it"},
    {RHI_DC_LAMPS_AREA, 1, "the lamps' area's class", NULL, "", "commit 653ad33bb", "v0.11.5",
     "another class measured with it"},
    {RHI_DC_STENCIL_CUT, 1, "stencil outside the shadow window cut where its self-test passes",
     NULL, "", "commit e6409f79b", "v0.11.5", "another class measured with it"},
    {RHI_DC_VU_COMPUTE_CLASS, RHI_DT_VUCOMP_CLASS,
     "the VU record by compute's class (off by default but on the measured device)", NULL, "",
     "commit 2aea87a7b", "v0.11.6", "a B-Series or Rogue log with it on"},
    {RHI_DC_NO_VOLUME_CLASS, 1, "the no-volume window's class (RD_NO_VOLUME_DEFAULT decides)", NULL,
     "", "commit f575b4d3a", "v0.11.6", "the window measured on the class"},
};

/* vk_pipeline.c vkr_compile_serial_init (rhi_driver_compiler_serial) */
static const RhiDeviceSetting kPowervrB[] = {
    {RHI_DC_COMPILER_SERIAL, 1, "pipeline compiles one at a time, a 60 s pre-game limit",
     "PowerVR B-Series", "issue 188",
     "Imagination: the B-Series compiler is not thread-safe (BXM-8-256, 1.15@6133110; "
     "commit aad38ac8c)",
     "v0.10.2", "a B-Series driver Imagination says is thread-safe"},
};

/* rd_vu_record.c (rd__vu_class_measured), rd_no_volume.c breakMeasured,
   rd_vu_compact.c's dxt */
static const RhiDeviceSetting kPowervrDxt[] = {
    {RHI_DC_VU_COMPUTE_CLASS, RHI_DT_VUCOMP_MEASURED,
     "the VU record by compute (RD_VU_COMPUTE_CLASS_DEFAULT decides)", NULL, "",
     "the fastest opening measured on the owner's Pixel 10 Pro Fold (commit 2aea87a7b)", "v0.11.6",
     "never: measured"},
    {RHI_DC_NO_VOLUME_BREAK, 1, "the no-volume break mode's device", NULL, "", "commit f575b4d3a",
     "v0.11.6", "never: measured"},
    {RHI_DC_COMPACTION_CLASS, RHI_DT_CMP_DXT, "the index compaction's DXT class", NULL, "",
     "commit 67dedc5d5", "v0.11.6", "never: a class"},
    {RHI_DC_COMPACTION_DEFAULT, RD_VU_COMPACT_DEFAULT,
     "the index compaction's default (RD_VU_COMPACT_DEFAULT; on where its self-test passes)", NULL,
     "", "commit 67dedc5d5", "v0.11.6", "never: measured"},
};

/* rd_profile.c classOf (MoltenVK), rd_no_volume.c classBase */
static const RhiDeviceSetting kMoltenvk[] = {
    {RHI_DC_PROFILE_CLASS, RHI_DT_CLASS_APPLE, "profile class Apple", NULL, "", "commit 85e9bc65e",
     "v0.7.10", "never: a class"},
    {RHI_DC_NO_VOLUME_CLASS, 1, "the no-volume window's class (RD_NO_VOLUME_DEFAULT decides)", NULL,
     "", "commit f575b4d3a", "v0.11.6", "the window measured on a Mac or an iPhone"},
};

/* rd_profile.c classOf's last branch (RhiLimits.tiler) */
static const RhiDeviceSetting kOtherTiler[] = {
    {RHI_DC_PROFILE_CLASS, RHI_DT_CLASS_TILER, "profile class another tile-based GPU", NULL, "",
     "commit 85e9bc65e", "v0.7.10", "never: a class"},
};

/* rd_profile.c rd__profile_init's tile = a class not desktop */
static const RhiDeviceSetting kTileClasses[] = {
    {RHI_DC_TILE_COLUMNS, 1,
     "the tile-based columns: glow fold off, uint views off, local reads where offered, aura "
     "box, pass area, pass cut, shadow merge, menu text baked at scale 2",
     NULL, "", "commits 85e9bc65e, 5c97fa012 and the v0.10.0 to v0.10.1 profile rows", "v0.7.10",
     "each column on a measurement of its own"},
};

#define TILE_CLASS_BITS                                                                            \
    ((1u << RHI_DT_CLASS_ADRENO_QUALCOMM) | (1u << RHI_DT_CLASS_ADRENO_TURNIP) |                   \
     (1u << RHI_DT_CLASS_MALI) | (1u << RHI_DT_CLASS_POWERVR) | (1u << RHI_DT_CLASS_APPLE) |       \
     (1u << RHI_DT_CLASS_TILER))

static const RhiDeviceRow kRows[] = {
    {"tile-vendors",
     "Vulkan on a Qualcomm, Arm, Imagination, Samsung, Apple or Broadcom GPU",
     {.backends = VK,
      .vendorCount = 6,
      .vendors = {0x5143u, 0x13B5u, 0x1010u, 0x144Du, 0x106Bu, 0x14E4u}},
     "",
     kTileVendors,
     N(kTileVendors)},
    {"android",
     "Vulkan on Android",
     {.backends = VK, .oses = OS_ANDROID},
     "",
     kAndroid,
     N(kAndroid)},
    {"ios",
     "Vulkan on iOS through MoltenVK",
     {.backends = VK, .oses = OS_IOS, .moltenvk = RHI_DT_YES},
     "",
     kIos,
     N(kIos)},
    {"tiler", "a tile-based GPU", {.tiler = RHI_DT_YES}, "", kTiler, N(kTiler)},
    {"tile-phone",
     "a tile-based phone GPU",
     {.tilePhone = RHI_DT_YES},
     "",
     kTilePhone,
     N(kTilePhone)},
    {"intel-windows-1.3.215",
     "Vulkan 1.3.215 or older on Intel's Windows driver",
     {.backends = VK,
      .vendorCount = 1,
      .vendors = {0x8086u},
      .driver = RHI_DT_DRV_INTEL_WIN_ID,
      .apiMax = (1u << 22) | (3u << 12) | 215u},
     "",
     kIntelWin,
     N(kIntelWin)},
    {"qualcomm-android",
     "Vulkan on Android, Qualcomm's own driver by its id",
     {.backends = VK, .oses = OS_ANDROID, .driver = RHI_DT_DRV_QCOM_ID},
     "",
     kQualcommAndroid,
     N(kQualcommAndroid)},
    {"qualcomm",
     "Vulkan, Qualcomm's own driver by its id",
     {.backends = VK, .driver = RHI_DT_DRV_QCOM_ID},
     "",
     kQualcomm,
     N(kQualcomm)},
    {"vendor-unknown",
     "no vendor id (no device yet, or the D3D12 backend)",
     {.vendorCount = 1, .vendors = {0u}},
     "",
     kVendorUnknown,
     N(kVendorUnknown)},
    {"qualcomm-text",
     "Qualcomm's own driver by its text",
     {.driver = RHI_DT_DRV_QCOM_TEXT},
     "",
     kQualcommText,
     N(kQualcommText)},
    {"qualcomm-text-android",
     "Android, Qualcomm's own driver by its text",
     {.oses = OS_ANDROID, .driver = RHI_DT_DRV_QCOM_TEXT},
     "",
     kQualcommTextAndroid,
     N(kQualcommTextAndroid)},
    {"mesa-drivers",
     "a Mesa driver (driver id 18, or Mesa or Turnip in the driver text)",
     {.driver = RHI_DT_DRV_MESA_TEXT},
     "",
     kMesaDrivers,
     N(kMesaDrivers)},
    {"qualcomm-vendor",
     "Vulkan, vendor 0x5143",
     {.backends = VK, .vendorCount = 1, .vendors = {0x5143u}},
     "",
     kQualcommVendor,
     N(kQualcommVendor)},
    {"adreno",
     "Vulkan, vendor 0x5143, not MoltenVK",
     {.backends = VK, .vendorCount = 1, .vendors = {0x5143u}, .moltenvk = RHI_DT_NO},
     "other-tiler",
     kAdreno,
     N(kAdreno)},
    {"turnip-adreno",
     "Vulkan, vendor 0x5143, not MoltenVK, Turnip",
     {.backends = VK,
      .vendorCount = 1,
      .vendors = {0x5143u},
      .moltenvk = RHI_DT_NO,
      .driver = RHI_DT_DRV_TURNIP},
     "adreno other-tiler",
     kTurnipAdreno,
     N(kTurnipAdreno)},
    {"turnip",
     "Turnip (driver id 18, or Turnip in the driver text)",
     {.driver = RHI_DT_DRV_TURNIP},
     "",
     kTurnip,
     N(kTurnip)},
    {"adreno-740-qualcomm",
     "Vulkan, Qualcomm's own driver by its id on device 0x43050A01",
     {.backends = VK,
      .vendorCount = 1,
      .vendors = {0x5143u},
      .deviceMask = 0xFFFFFFFFu,
      .device = 0x43050A01u,
      .driver = RHI_DT_DRV_QCOM_ID},
     "turnip",
     kAdreno740,
     N(kAdreno740)},
    {"stock-qualcomm-android",
     "Android, no driver package, vendor 0x5143",
     {.oses = OS_ANDROID, .vendorCount = 1, .vendors = {0x5143u}, .customDriver = RHI_DT_NO},
     "",
     kStockQualcommAndroid,
     N(kStockQualcommAndroid)},
    {"arm",
     "Vulkan, vendor 0x13B5",
     {.backends = VK, .vendorCount = 1, .vendors = {0x13B5u}},
     "",
     kArm,
     N(kArm)},
    {"mali",
     "Vulkan, vendor 0x13B5, not MoltenVK",
     {.backends = VK, .vendorCount = 1, .vendors = {0x13B5u}, .moltenvk = RHI_DT_NO},
     "other-tiler",
     kMali,
     N(kMali)},
    {"mali-g52",
     "vendor 0x13B5, not MoltenVK, a device name with \"Mali-G52\"",
     {.vendorCount = 1, .vendors = {0x13B5u}, .moltenvk = RHI_DT_NO, .nameHas = "Mali-G52"},
     "",
     kMaliG52,
     N(kMaliG52)},
    {"powervr",
     "Vulkan, vendor 0x1010, not MoltenVK",
     {.backends = VK, .vendorCount = 1, .vendors = {0x1010u}, .moltenvk = RHI_DT_NO},
     "other-tiler",
     kPowervr,
     N(kPowervr)},
    {"powervr-b-series",
     "Vulkan, vendor 0x1010, \"b-series\" in the driver text",
     {.backends = VK, .vendorCount = 1, .vendors = {0x1010u}, .textHas = "b-series"},
     "",
     kPowervrB,
     N(kPowervrB)},
    {"powervr-dxt-48-1536",
     "Vulkan, vendor 0x1010, not MoltenVK, a device name with \"PowerVR D-Series DXT-48-1536\"",
     {.backends = VK,
      .vendorCount = 1,
      .vendors = {0x1010u},
      .moltenvk = RHI_DT_NO,
      .nameHas = "PowerVR D-Series DXT-48-1536"},
     "powervr",
     kPowervrDxt,
     N(kPowervrDxt)},
    {"moltenvk",
     "Vulkan through MoltenVK",
     {.backends = VK, .moltenvk = RHI_DT_YES},
     "other-tiler",
     kMoltenvk,
     N(kMoltenvk)},
    {"other-tiler",
     "Vulkan, a tile-based GPU of no class row",
     {.backends = VK, .tiler = RHI_DT_YES},
     "",
     kOtherTiler,
     N(kOtherTiler)},
    {"tile-classes",
     "a tile-based profile class",
     {.profileClasses = TILE_CLASS_BITS},
     "",
     kTileClasses,
     N(kTileClasses)},
};

#define ROW_COUNT ((int)(sizeof(kRows) / sizeof(kRows[0])))

_Static_assert(sizeof(kRows) / sizeof(kRows[0]) <= RHI_DT_MAX_ROWS, "RHI_DT_MAX_ROWS");

const RhiDeviceRow *rhi_device_table_rows(size_t *count)
{
    if (count != NULL) {
        *count = (size_t)ROW_COUNT;
    }
    return kRows;
}

int rhi_device_table_find(const char *id)
{
    for (int i = 0; id != NULL && i < ROW_COUNT; i++) {
        if (strcmp(kRows[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int32_t rhi_device_table_row_value(const char *id, RhiDeviceChoice choice, int32_t fallback)
{
    const int r = rhi_device_table_find(id);
    if (r < 0) {
        return fallback;
    }
    for (uint8_t s = 0; s < kRows[r].settingCount; s++) {
        if (kRows[r].settings[s].choice == (uint16_t)choice) {
            return kRows[r].settings[s].value;
        }
    }
    return fallback;
}

/* --------------------------------------------------------------- choices */

static const char *const kChoiceNames[RHI_DC_COUNT] = {
    [RHI_DC_TILER] = "TILER",
    [RHI_DC_TILE_PHONE] = "TILE_PHONE",
    [RHI_DC_PROFILE_CLASS] = "PROFILE_CLASS",
    [RHI_DC_TILE_CLASS] = "TILE_CLASS",
    [RHI_DC_DYNAMIC_STATE_OFF] = "DYNAMIC_STATE_OFF",
    [RHI_DC_MEMORY_BUDGET_OFF] = "MEMORY_BUDGET_OFF",
    [RHI_DC_PASS_OBJECTS] = "PASS_OBJECTS",
    [RHI_DC_SUBPASS_CLASS] = "SUBPASS_CLASS",
    [RHI_DC_ATTACHMENT_READ_OFF] = "ATTACHMENT_READ_OFF",
    [RHI_DC_VU_FALLBACK_PLAIN] = "VU_FALLBACK_PLAIN",
    [RHI_DC_CAREFUL_PIPELINES] = "CAREFUL_PIPELINES",
    [RHI_DC_VU_PREPASS_OFF] = "VU_PREPASS_OFF",
    [RHI_DC_COMPUTE_RECORD_OFF] = "COMPUTE_RECORD_OFF",
    [RHI_DC_COMPILER_SERIAL] = "COMPILER_SERIAL",
    [RHI_DC_PIPE_ROBUST_ASK] = "PIPE_ROBUST_ASK",
    [RHI_DC_FIFO_FROM_START] = "FIFO_FROM_START",
    [RHI_DC_FALLBACK_ELIGIBLE] = "FALLBACK_ELIGIBLE",
    [RHI_DC_FALLBACK_SWITCH] = "FALLBACK_SWITCH",
    [RHI_DC_MEMORY_FLOOR_CLASS] = "MEMORY_FLOOR_CLASS",
    [RHI_DC_TURNIP_NOTE] = "TURNIP_NOTE",
    [RHI_DC_SKIN_CHECK_DEFAULT] = "SKIN_CHECK_DEFAULT",
    [RHI_DC_SKIN_AS_BEFORE] = "SKIN_AS_BEFORE",
    [RHI_DC_BLUR_FOLD_OFF] = "BLUR_FOLD_OFF",
    [RHI_DC_TILE_COLUMNS] = "TILE_COLUMNS",
    [RHI_DC_LAMPS_MS] = "LAMPS_MS",
    [RHI_DC_LAMPS_FILLPASTE] = "LAMPS_FILLPASTE",
    [RHI_DC_LAMPS_AREA] = "LAMPS_AREA",
    [RHI_DC_FILL11_CLASS] = "FILL11_CLASS",
    [RHI_DC_SHADOW_TILE_CLASS] = "SHADOW_TILE_CLASS",
    [RHI_DC_STENCIL_CUT] = "STENCIL_CUT",
    [RHI_DC_VU_COMPUTE_CLASS] = "VU_COMPUTE_CLASS",
    [RHI_DC_NO_VOLUME_CLASS] = "NO_VOLUME_CLASS",
    [RHI_DC_NO_VOLUME_BREAK] = "NO_VOLUME_BREAK",
    [RHI_DC_COMPACTION_CLASS] = "COMPACTION_CLASS",
    [RHI_DC_COMPACTION_DEFAULT] = "COMPACTION_DEFAULT",
    [RHI_DC_MSAA_MAX4] = "MSAA_MAX4",
    [RHI_DC_SCENE_CAP_4096] = "SCENE_CAP_4096",
    [RHI_DC_MSAA_BUDGET] = "MSAA_BUDGET",
    [RHI_DC_SHOT_LATE] = "SHOT_LATE",
    [RHI_DC_LATE_PASS_TILER] = "LATE_PASS_TILER",
    [RHI_DC_UBER_AUTO] = "UBER_AUTO",
    [RHI_DC_FOG_ORDER_TILER] = "FOG_ORDER_TILER",
    [RHI_DC_CULL_FRUSTUM] = "CULL_FRUSTUM",
};

const char *rhi_device_choice_name(RhiDeviceChoice choice)
{
    return (unsigned)choice < RHI_DC_COUNT && kChoiceNames[choice] ? kChoiceNames[choice] : "?";
}

const char *rhi_device_choice_value_name(RhiDeviceChoice choice, int32_t value)
{
    static const char *const cls[RHI_DT_CLASS_COUNT] = {
        "desktop",       "d3d12", "opengl",  "d3d11", "adreno_qualcomm",
        "adreno_turnip", "mali",  "powervr", "apple", "tiler"};
    switch (choice) {
    case RHI_DC_PROFILE_CLASS:
        return value >= 0 && value < RHI_DT_CLASS_COUNT ? cls[value] : "?";
    case RHI_DC_PASS_OBJECTS:
        return value == RHI_DT_PASS_INTEL      ? "intel"
               : value == RHI_DT_PASS_QUALCOMM ? "qualcomm"
                                               : "none";
    case RHI_DC_SUBPASS_CLASS:
        return value == RHI_DT_SUBPASS_INTEL  ? "intel"
               : value == RHI_DT_SUBPASS_ARM  ? "arm"
               : value == RHI_DT_SUBPASS_QCOM ? "qcom"
                                              : "other";
    case RHI_DC_VU_FALLBACK_PLAIN:
        return value == RHI_DT_VUSET_TABLE    ? "table"
               : value == RHI_DT_VUSET_TURNIP ? "turnip"
                                              : "extended";
    case RHI_DC_FALLBACK_SWITCH:
        return value == RHI_DT_SWITCH_YES ? "yes" : "other";
    case RHI_DC_MEMORY_FLOOR_CLASS:
        return value == RHI_DT_FLOOR_UNKNOWN    ? "unknown"
               : value == RHI_DT_FLOOR_QUALCOMM ? "qualcomm"
                                                : "other";
    case RHI_DC_VU_COMPUTE_CLASS:
        return value == RHI_DT_VUCOMP_CLASS      ? "class"
               : value == RHI_DT_VUCOMP_MEASURED ? "measured"
                                                 : "none";
    case RHI_DC_COMPACTION_CLASS:
        return value == RHI_DT_CMP_G52 ? "g52" : value == RHI_DT_CMP_DXT ? "dxt" : "other";
    case RHI_DC_TILE_CLASS:
    case RHI_DC_FILL11_CLASS:
    case RHI_DC_SHADOW_TILE_CLASS:
        return value == 1 ? "1" : value == 2 ? "2" : "0";
    default:
        return value ? "yes" : "no";
    }
}

const char *rhi_device_choice_default_words(RhiDeviceChoice choice)
{
    switch (choice) {
    case RHI_DC_PROFILE_CLASS:
        return "by backend: desktop (Vulkan), d3d12, opengl, d3d11";
    case RHI_DC_TILE_CLASS:
        return "2 on the desktop classes, else 0";
    case RHI_DC_LAMPS_AREA:
        return "yes on tile class 2 off a tile-based phone";
    case RHI_DC_FILL11_CLASS:
        return "1 on tile class 1, 2 on tile class 2 off a tile-based phone, else 0";
    case RHI_DC_SHADOW_TILE_CLASS:
        return "TILE_CLASS";
    case RHI_DC_FALLBACK_ELIGIBLE:
        return "yes";
    case RHI_DC_FALLBACK_SWITCH:
        return "other";
    case RHI_DC_MEMORY_FLOOR_CLASS:
        return "other";
    case RHI_DC_PASS_OBJECTS:
    case RHI_DC_VU_COMPUTE_CLASS:
        return "none";
    case RHI_DC_SUBPASS_CLASS:
    case RHI_DC_COMPACTION_CLASS:
        return "other";
    case RHI_DC_VU_FALLBACK_PLAIN:
        return "extended";
    case RHI_DC_COMPACTION_DEFAULT:
        return "the class's: RD_VU_COMPACT_ARM_DEFAULT on g52, RD_VU_COMPACT_DEFAULT on dxt, "
               "else no";
    default:
        return "no";
    }
}

const char *rhi_device_choice_na_words(RhiDeviceChoice choice)
{
    /* attempt_ledger.c's phrases before the table, verbatim (the ledger's
       tests and docs/port/ANDROID_DIAGNOSTICS.md hold them) */
    switch (choice) {
    case RHI_DC_PASS_OBJECTS:
        return "not Qualcomm's own driver";
    case RHI_DC_VU_COMPUTE_CLASS:
        return "not the PowerVR class under Vulkan";
    case RHI_DC_NO_VOLUME_CLASS:
        return "not the PowerVR or MoltenVK class under Vulkan";
    case RHI_DC_COMPACTION_CLASS:
        return "off by default: measured on the Mali-G52 only (and on the PowerVR D-Series "
               "DXT-48-1536's compute record)";
    default:
        return NULL;
    }
}

/* ------------------------------------------------------------- the match */

int rhi_device_text_has(const char *s, const char *word)
{
    /* ASCII letters in any case, as memory_floor.c, present_fallback.c,
       rd_profile.c and rhi_driver_version.c test (each byte | 0x20) */
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

int rhi_device_driver_test(int test, const RhiDeviceId *id)
{
    const uint32_t v = id->vendorId, d = id->driverId;
    switch (test) {
    case RHI_DT_DRV_QCOM_ID:
        return v == 0x5143u && (d == 8u || d == 0u);
    case RHI_DT_DRV_QCOM_TEXT:
        return v == 0x5143u && d != 18u && !rhi_device_text_has(id->driverText, "mesa") &&
               !rhi_device_text_has(id->driverText, "turnip");
    case RHI_DT_DRV_MESA_TEXT:
        return d == 18u || rhi_device_text_has(id->driverText, "mesa") ||
               rhi_device_text_has(id->driverText, "turnip");
    case RHI_DT_DRV_TURNIP:
        return d == 18u || rhi_device_text_has(id->driverText, "turnip");
    case RHI_DT_DRV_INTEL_WIN_ID:
        return d != 0u ? d == 5u : id->os == RHI_DT_WINDOWS;
    default:
        return 1;
    }
}

static int tri(uint8_t want, int have)
{
    return want == RHI_DT_ANY || (want == RHI_DT_YES) == (have != 0);
}

static int rowMatches(const RhiDeviceRow *r, const RhiDeviceId *id, const int32_t *v)
{
    const RhiDeviceMatch *m = &r->match;
    if (m->backends != 0u && (id->backend >= 32u || (m->backends & (1u << id->backend)) == 0u)) {
        return 0;
    }
    if (m->oses != 0u && (id->os >= 32u || (m->oses & (1u << id->os)) == 0u)) {
        return 0;
    }
    if (m->vendorCount != 0u) {
        int found = 0;
        for (uint8_t i = 0; i < m->vendorCount && i < RHI_DT_MAX_VENDORS; i++) {
            found = found || m->vendors[i] == id->vendorId;
        }
        if (!found) {
            return 0;
        }
    }
    if (m->deviceMask != 0u && (id->deviceId & m->deviceMask) != m->device) {
        return 0;
    }
    if (m->driver != RHI_DT_DRV_ANY && !rhi_device_driver_test(m->driver, id)) {
        return 0;
    }
    if (m->apiMax != 0u && id->apiVersion > m->apiMax) {
        return 0;
    }
    if (m->nameHas != NULL &&
        (id->deviceName == NULL || strstr(id->deviceName, m->nameHas) == NULL)) {
        return 0;
    }
    if (m->textHas != NULL && !rhi_device_text_has(id->driverText, m->textHas)) {
        return 0;
    }
    if (!tri(m->moltenvk, id->moltenvk) || !tri(m->customDriver, id->customDriver)) {
        return 0;
    }
    if (!tri(m->tiler, v[RHI_DC_TILER]) || !tri(m->tilePhone, v[RHI_DC_TILE_PHONE])) {
        return 0;
    }
    if (m->profileClasses != 0u && (v[RHI_DC_PROFILE_CLASS] < 0 || v[RHI_DC_PROFILE_CLASS] >= 32 ||
                                    (m->profileClasses & (1u << v[RHI_DC_PROFILE_CLASS])) == 0u)) {
        return 0;
    }
    return 1;
}

/* the row a lists b among the rows it beats */
static int overrides(const RhiDeviceRow *a, const RhiDeviceRow *b)
{
    const char *s = a->overrides;
    const size_t n = strlen(b->id);
    while (s != NULL && *s != '\0') {
        while (*s == ' ') {
            s++;
        }
        const size_t k = strcspn(s, " ");
        if (k == n && strncmp(s, b->id, n) == 0) {
            return 1;
        }
        s += k;
    }
    return 0;
}

static int32_t defaultOf(int c, const RhiDeviceId *id, const int32_t *v)
{
    switch (c) {
    case RHI_DC_PROFILE_CLASS:
        return id->backend == RHI_DT_D3D12    ? RHI_DT_CLASS_D3D12
               : id->backend == RHI_DT_OPENGL ? RHI_DT_CLASS_OPENGL
               : id->backend == RHI_DT_D3D11  ? RHI_DT_CLASS_D3D11
                                              : RHI_DT_CLASS_DESKTOP;
    case RHI_DC_TILE_CLASS: {
        const int32_t k = v[RHI_DC_PROFILE_CLASS];
        return k == RHI_DT_CLASS_DESKTOP || k == RHI_DT_CLASS_D3D12 || k == RHI_DT_CLASS_OPENGL ||
                       k == RHI_DT_CLASS_D3D11
                   ? 2
                   : 0;
    }
    case RHI_DC_LAMPS_AREA:
        return v[RHI_DC_TILE_CLASS] == 2 && !v[RHI_DC_TILE_PHONE];
    case RHI_DC_FILL11_CLASS:
        return v[RHI_DC_TILE_CLASS] == 1                            ? 1
               : v[RHI_DC_TILE_CLASS] == 2 && !v[RHI_DC_TILE_PHONE] ? 2
                                                                    : 0;
    case RHI_DC_SHADOW_TILE_CLASS:
        return v[RHI_DC_TILE_CLASS];
    case RHI_DC_FALLBACK_ELIGIBLE:
        return 1;
    case RHI_DC_FALLBACK_SWITCH:
        return RHI_DT_SWITCH_OTHER;
    case RHI_DC_MEMORY_FLOOR_CLASS:
        return RHI_DT_FLOOR_OTHER;
    case RHI_DC_COMPACTION_DEFAULT:
        return v[RHI_DC_COMPACTION_CLASS] == RHI_DT_CMP_G52   ? RD_VU_COMPACT_ARM_DEFAULT
               : v[RHI_DC_COMPACTION_CLASS] == RHI_DT_CMP_DXT ? RD_VU_COMPACT_DEFAULT
                                                              : 0;
    default:
        return 0;
    }
}

typedef struct Resolved {
    int32_t value;
    int16_t row, setting;
    int16_t conflictA, conflictB;
} Resolved;

/* choice c on id over the rows (cache: each row's match, -1 not tried yet,
   reading only the facts resolved before c in v) */
static void resolve(int c, const RhiDeviceId *id, const int32_t *v, int8_t *cache, Resolved *out)
{
    int16_t candRow[RHI_DT_MAX_ROWS], candSet[RHI_DT_MAX_ROWS];
    int n = 0;
    out->conflictA = out->conflictB = -1;
    for (int r = 0; r < ROW_COUNT; r++) {
        for (uint8_t s = 0; s < kRows[r].settingCount; s++) {
            if (kRows[r].settings[s].choice != (uint16_t)c) {
                continue;
            }
            if (cache[r] < 0) {
                cache[r] = (int8_t)rowMatches(&kRows[r], id, v);
            }
            if (cache[r] && n < RHI_DT_MAX_ROWS) {
                candRow[n] = (int16_t)r;
                candSet[n] = (int16_t)s;
                n++;
            }
            break; /* a row sets a choice once */
        }
    }
    if (n == 0) {
        out->value = defaultOf(c, id, v);
        out->row = out->setting = -1;
        return;
    }
    /* the first candidate that beats every candidate of another value */
    for (int w = 0; w < n; w++) {
        const int32_t wv = kRows[candRow[w]].settings[candSet[w]].value;
        int beats = 1;
        for (int o = 0; o < n && beats; o++) {
            const int32_t ov = kRows[candRow[o]].settings[candSet[o]].value;
            if (ov != wv && !overrides(&kRows[candRow[w]], &kRows[candRow[o]])) {
                beats = 0;
            }
        }
        if (beats) {
            out->value = wv;
            out->row = candRow[w];
            out->setting = candSet[w];
            return;
        }
    }
    /* no winner: the table's error (the tests fail on it); the first row's */
    out->value = kRows[candRow[0]].settings[candSet[0]].value;
    out->row = candRow[0];
    out->setting = candSet[0];
    for (int o = 1; o < n; o++) {
        if (kRows[candRow[o]].settings[candSet[o]].value != out->value) {
            out->conflictA = candRow[0];
            out->conflictB = candRow[o];
            break;
        }
    }
}

static int tileVendor(uint32_t vendor)
{
    /* the tile-vendors row's list (rhi_tile_rule.c rhi_tile_vendor_name's) */
    const int t = rhi_device_table_find("tile-vendors");
    const RhiDeviceMatch *m = &kRows[t >= 0 ? t : 0].match;
    for (uint8_t i = 0; i < m->vendorCount && i < RHI_DT_MAX_VENDORS; i++) {
        if (m->vendors[i] == vendor) {
            return 1;
        }
    }
    return 0;
}

/* the row index and setting index that set c to value (the first), -1 none */
static void setterOf(int c, int32_t value, int16_t *row, int16_t *setting)
{
    *row = *setting = -1;
    for (int r = 0; r < ROW_COUNT; r++) {
        for (uint8_t s = 0; s < kRows[r].settingCount; s++) {
            if (kRows[r].settings[s].choice == (uint16_t)c && kRows[r].settings[s].value == value) {
                *row = (int16_t)r;
                *setting = (int16_t)s;
                return;
            }
        }
    }
}

static void fake(RhiDeviceChoices *o, int c, int32_t value, int16_t row, int16_t setting,
                 uint8_t which)
{
    if (o->value[c] == value && o->row[c] == row) {
        return;
    }
    o->value[c] = value;
    o->row[c] = row;
    o->setting[c] = setting;
    o->faked[c] = which;
}

void rhi_device_table_eval(const RhiDeviceId *id, const RhiDeviceFakes *fk, RhiDeviceChoices *o)
{
    int8_t cache[RHI_DT_MAX_ROWS];
    memset(o, 0, sizeof(*o));
    memset(cache, -1, sizeof cache);
    o->conflictChoice = o->conflictA = o->conflictB = -1;
    for (int c = 0; c < RHI_DC_COUNT; c++) {
        Resolved r;
        resolve(c, id, o->value, cache, &r);
        o->value[c] = r.value;
        o->row[c] = r.row;
        o->setting[c] = r.setting;
        if (r.conflictA >= 0 && o->conflictChoice < 0) {
            o->conflictChoice = (int16_t)c;
            o->conflictA = r.conflictA;
            o->conflictB = r.conflictB;
        }
        if (fk == NULL) {
            continue;
        }
        int16_t fr, fs;
        switch (c) {
        case RHI_DC_TILE_PHONE:
            /* vk_device.c vkr_tile_rule_apply: a tile-based vendor's phone */
            if (fk->vendorSet && tileVendor(fk->vendor) && !o->value[c]) {
                fake(o, c, 1, -1, -1, RHI_DT_FAKE_VENDOR);
            }
            break;
        case RHI_DC_SUBPASS_CLASS:
            /* vk_device.c vkr_subpass_read_decide: the Intel row on the real
               ids, the Qualcomm and Arm parts on the faked vendor (the rows
               that set the class read no derived fact) */
            if (fk->vendorSet && o->value[c] != RHI_DT_SUBPASS_INTEL) {
                RhiDeviceId f = *id;
                int8_t fcache[RHI_DT_MAX_ROWS];
                Resolved fr2;
                f.vendorId = fk->vendor;
                memset(fcache, -1, sizeof fcache);
                resolve(c, &f, o->value, fcache, &fr2);
                if (fr2.value != RHI_DT_SUBPASS_QCOM && fr2.value != RHI_DT_SUBPASS_ARM) {
                    fr2.value = RHI_DT_SUBPASS_OTHER;
                    fr2.row = fr2.setting = -1;
                }
                fake(o, c, fr2.value, fr2.row, fr2.setting, RHI_DT_FAKE_VENDOR);
            }
            break;
        case RHI_DC_PASS_OBJECTS:
            if (fk->passRowQualcomm && o->value[c] != RHI_DT_PASS_INTEL) {
                setterOf(c, RHI_DT_PASS_QUALCOMM, &fr, &fs);
                fake(o, c, RHI_DT_PASS_QUALCOMM, fr, fs, RHI_DT_FAKE_PASS_ROW);
            }
            break;
        case RHI_DC_ATTACHMENT_READ_OFF:
            if (fk->adreno && !o->value[c]) {
                fake(o, c, 1, -1, -1, RHI_DT_FAKE_ADRENO);
            }
            break;
        case RHI_DC_COMPILER_SERIAL:
            if (fk->serial && !o->value[c]) {
                fake(o, c, 1, -1, -1, RHI_DT_FAKE_SERIAL);
            }
            break;
        case RHI_DC_VU_PREPASS_OFF:
            if (fk->noPrepass && !o->value[c]) {
                fake(o, c, 1, -1, -1, RHI_DT_FAKE_NO_PREPASS);
            }
            break;
        case RHI_DC_COMPACTION_CLASS:
            if (fk->compactArm && o->value[c] != RHI_DT_CMP_G52) {
                fake(o, c, RHI_DT_CMP_G52, -1, -1, RHI_DT_FAKE_COMPACT_ARM);
            }
            break;
        case RHI_DC_COMPACTION_DEFAULT:
            /* the class the fake gave takes its own default (attempt_ledger.c
               cmpClassDefault: the Mali-G52's), whatever row matched */
            if (o->faked[RHI_DC_COMPACTION_CLASS] == RHI_DT_FAKE_COMPACT_ARM) {
                const int g = rhi_device_table_find("mali-g52");
                fr = fs = -1;
                for (uint8_t s = 0; g >= 0 && s < kRows[g].settingCount; s++) {
                    if (kRows[g].settings[s].choice == RHI_DC_COMPACTION_DEFAULT) {
                        fr = (int16_t)g;
                        fs = (int16_t)s;
                    }
                }
                fake(o, c, RD_VU_COMPACT_ARM_DEFAULT, fr, fs, RHI_DT_FAKE_COMPACT_ARM);
            }
            break;
        default:
            break;
        }
    }
    for (int r = 0; r < ROW_COUNT; r++) {
        if (cache[r] < 0) {
            /* a row no choice asked about yet (every row sets one, so none) */
            cache[r] = (int8_t)rowMatches(&kRows[r], id, o->value);
        }
        o->matched[r] = (uint8_t)(cache[r] != 0);
        o->rowsMatched = (uint8_t)(o->rowsMatched + o->matched[r]);
        for (uint8_t s = 0; o->matched[r] && !o->applied[r] && s < kRows[r].settingCount; s++) {
            const RhiDeviceSetting *st = &kRows[r].settings[s];
            o->applied[r] = (uint8_t)(o->value[st->choice] == st->value);
        }
        o->rowsApplied = (uint8_t)(o->rowsApplied + o->applied[r]);
    }
}

int32_t rhi_device_table_choice(const RhiDeviceId *id, RhiDeviceChoice choice, const char **reason)
{
    if (reason != NULL) {
        *reason = NULL;
    }
    if ((unsigned)choice >= RHI_DC_COUNT) {
        return 0;
    }
    if (choice == RHI_DC_COMPACTION_DEFAULT) {
        /* its default reads COMPACTION_CLASS, not a fact: the whole table */
        RhiDeviceChoices all;
        rhi_device_table_eval(id, NULL, &all);
        if (reason != NULL) {
            *reason = rhi_device_choice_reason(&all, choice);
        }
        return all.value[choice];
    }
    /* the facts before it (a row matches on nothing else, and no other
       choice's default reads more), then the choice */
    int32_t v[RHI_DC_COUNT];
    int8_t cache[RHI_DT_MAX_ROWS];
    memset(v, 0, sizeof v);
    memset(cache, -1, sizeof cache);
    Resolved r;
    for (int c = 0; c <= RHI_DC_TILE_CLASS && c < (int)choice; c++) {
        resolve(c, id, v, cache, &r);
        v[c] = r.value;
    }
    resolve((int)choice, id, v, cache, &r);
    if (reason != NULL && r.row >= 0 && r.setting >= 0) {
        *reason = kRows[r.row].settings[r.setting].reason;
    }
    return r.value;
}

const char *rhi_device_choice_reason(const RhiDeviceChoices *c, RhiDeviceChoice choice)
{
    if ((unsigned)choice >= RHI_DC_COUNT) {
        return NULL;
    }
    if (c->faked[choice] == RHI_DT_FAKE_SERIAL) {
        return "ICO_VK_FAKE_SERIAL";
    }
    if (c->faked[choice] == RHI_DT_FAKE_NO_PREPASS) {
        return "ICO_VK_FAKE_NO_PREPASS";
    }
    if (c->row[choice] < 0 || c->setting[choice] < 0) {
        return NULL;
    }
    return kRows[c->row[choice]].settings[c->setting[choice]].reason;
}

/* ------------------------------------------------------------- the lines */

static const char *osName(uint8_t os)
{
    switch (os) {
    case RHI_DT_WINDOWS:
        return "Windows";
    case RHI_DT_MACOS:
        return "macOS";
    case RHI_DT_ANDROID:
        return "Android";
    case RHI_DT_IOS:
        return "iOS";
    default:
        return "Linux";
    }
}

static const char *fakeName(uint8_t f)
{
    switch (f) {
    case RHI_DT_FAKE_VENDOR:
        return "ICO_VK_FAKE_VENDOR";
    case RHI_DT_FAKE_PASS_ROW:
        return "ICO_VK_FAKE_PASS_ROW=qualcomm";
    case RHI_DT_FAKE_ADRENO:
        return "ICO_VK_FAKE_ADRENO";
    case RHI_DT_FAKE_SERIAL:
        return "ICO_VK_FAKE_SERIAL";
    case RHI_DT_FAKE_NO_PREPASS:
        return "ICO_VK_FAKE_NO_PREPASS";
    case RHI_DT_FAKE_COMPACT_ARM:
        return "ICO_RD_VU_COMPACT_ARM";
    default:
        return "";
    }
}

/* appends to out[*at..cap) (snprintf's cut: *at stops at cap - 1) */
static void add(char *out, size_t cap, size_t *at, const char *fmt, const char *a, const char *b)
{
    if (*at + 1u >= cap) {
        return;
    }
    const int k = snprintf(out + *at, cap - *at, fmt, a, b);
    if (k > 0) {
        *at += (size_t)k < cap - *at ? (size_t)k : cap - *at - 1u;
    }
}

size_t rhi_device_table_header(char *out, size_t cap, const RhiDeviceId *id,
                               const RhiDeviceChoices *c)
{
    if (cap == 0) {
        return 0;
    }
    char ids[96], api[48], count[64];
    if (id->backend == RHI_DT_VULKAN) {
        snprintf(ids, sizeof ids, "device %04x:%04x; driver ID %u", (unsigned)id->vendorId,
                 (unsigned)id->deviceId, (unsigned)id->driverId);
    } else {
        /* no driver id outside Vulkan */
        snprintf(ids, sizeof ids, "device %04x:%04x", (unsigned)id->vendorId,
                 (unsigned)id->deviceId);
    }
    if (id->backend == RHI_DT_VULKAN) {
        snprintf(api, sizeof api, "Vulkan %u.%u.%u", (unsigned)(id->apiVersion >> 22) & 0x7Fu,
                 (unsigned)(id->apiVersion >> 12) & 0x3FFu, (unsigned)id->apiVersion & 0xFFFu);
    } else {
        snprintf(api, sizeof api, "%s",
                 id->backend == RHI_DT_D3D12    ? "D3D12"
                 : id->backend == RHI_DT_OPENGL ? "OpenGL"
                                                : "D3D11");
    }
    size_t at = 0;
    out[0] = '\0';
    add(out, cap, &at, "device table: %s (%s", id->deviceName ? id->deviceName : "an unnamed GPU",
        ids);
    if (id->driverVersion != NULL && id->driverVersion[0] != '\0') {
        add(out, cap, &at, "%s%s", ", version ", id->driverVersion);
    }
    add(out, cap, &at, "; %s; %s): ", api, osName(id->os));
    if (c->rowsApplied == 0u) {
        snprintf(count, sizeof count, "%d", ROW_COUNT);
        add(out, cap, &at, "none of the table's %s rows%s", count,
            ": every choice as capability and self-tests decide");
        return at;
    }
    snprintf(count, sizeof count, "%u of the table's %d", (unsigned)c->rowsApplied, ROW_COUNT);
    add(out, cap, &at, "%s rows%s", count, ":");
    int first = 1;
    for (int r = 0; r < ROW_COUNT; r++) {
        if (c->applied[r]) {
            add(out, cap, &at, "%s%s", first ? " " : ", ", kRows[r].id);
            first = 0;
        }
    }
    first = 1;
    for (int r = 0; r < ROW_COUNT; r++) {
        if (c->matched[r] && !c->applied[r]) {
            add(out, cap, &at, "%s%s", first ? "; overridden here: " : ", ", kRows[r].id);
            first = 0;
        }
    }
    return at;
}

size_t rhi_device_table_row_line(char *out, size_t cap, const RhiDeviceChoices *c, int row)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (row < 0 || row >= ROW_COUNT) {
        return 0;
    }
    const RhiDeviceRow *r = &kRows[row];
    size_t at = 0;
    add(out, cap, &at, "device table: %s (%s): ", r->id, r->matchWords);
    for (uint8_t s = 0; s < r->settingCount; s++) {
        const RhiDeviceSetting *st = &r->settings[s];
        add(out, cap, &at, "%s%s (", s ? "; " : "", st->what);
        if (st->issue[0] != '\0') {
            add(out, cap, &at, "%s%s", st->issue, ", ");
        }
        add(out, cap, &at, "%s%s", st->since, "");
        const int k = st->choice;
        if (c->value[k] != st->value && c->row[k] >= 0 && c->row[k] != row) {
            add(out, cap, &at, "; overridden by %s%s", kRows[c->row[k]].id, "");
        } else if (c->value[k] != st->value && c->faked[k] != RHI_DT_FAKE_NONE) {
            add(out, cap, &at, "; replaced by %s%s", fakeName(c->faked[k]), ", a test");
        }
        add(out, cap, &at, "; revisit: %s%s", st->revisit, ")");
    }
    return at;
}

size_t rhi_device_table_summary(char *out, size_t cap, const RhiDeviceChoices *c)
{
    if (cap == 0) {
        return 0;
    }
    /* the count alone: the ids are the start-up header's (the summary line's
       room, rhi_device_table.h) */
    char buf[RHI_DT_SUMMARY_MAX];
    if (c->rowsApplied == 0u) {
        snprintf(buf, sizeof buf, "device table rows: none of %d", ROW_COUNT);
    } else {
        snprintf(buf, sizeof buf, "device table rows: %u of %d (the start lines name them)",
                 (unsigned)c->rowsApplied, ROW_COUNT);
    }
    const int k = snprintf(out, cap, "%s", buf);
    return k < 0 ? 0 : (size_t)k < cap ? (size_t)k : cap - 1u;
}

static char s_noted[RHI_DT_SUMMARY_MAX];
static int s_haveNote;

void rhi_device_table_note(const RhiDeviceChoices *c)
{
    if (c == NULL) {
        s_haveNote = 0;
        s_noted[0] = '\0';
        return;
    }
    rhi_device_table_summary(s_noted, sizeof s_noted, c);
    s_haveNote = 1;
}

const char *rhi_device_table_noted(void)
{
    return s_haveNote ? s_noted : NULL;
}

/* ------------------------------------------------- the backends' reads */

void rhi_device_fakes_parse(const RhiDeviceFakeEnv *env, RhiDeviceFakes *out)
{
    memset(out, 0, sizeof(*out));
    if (env == NULL) {
        return;
    }
    const char *fv = env->vendor;
    if (fv && fv[0]) {
        char *end = NULL;
        const unsigned long v = strtoul(fv, &end, 0);
        if (end != fv && *end == '\0' && v <= 0xFFFFFFFFul) {
            out->vendorSet = 1;
            out->vendor = (uint32_t)v;
        }
    }
    out->passRowQualcomm = env->passRow && strcmp(env->passRow, "qualcomm") == 0;
    out->adreno = env->adreno && env->adreno[0] && env->adreno[0] != '0';
    out->serial = env->serial && env->serial[0] == '1';
    out->noPrepass = env->noPrepass && env->noPrepass[0] && env->noPrepass[0] != '0';
    out->compactArm = env->compactArm && strcmp(env->compactArm, "1") == 0;
}

const char *rhi_device_choice_why(const RhiDeviceChoices *c, RhiDeviceChoice choice)
{
    if ((unsigned)choice >= RHI_DC_COUNT || c->value[choice] == 0) {
        return NULL;
    }
    return rhi_device_choice_reason(c, choice);
}

void rhi_device_pass_rows(const RhiDeviceChoices *c, const char **intelRow, const char **qcomRow)
{
    const int32_t v = c->value[RHI_DC_PASS_OBJECTS];
    const char *why = rhi_device_choice_reason(c, RHI_DC_PASS_OBJECTS);
    *intelRow = v == RHI_DT_PASS_INTEL ? why : NULL;
    *qcomRow = v == RHI_DT_PASS_QUALCOMM ? why : NULL;
}

int rhi_device_subpass_class(const RhiDeviceChoices *c, int passRowQcom)
{
    const int32_t t = c->value[RHI_DC_SUBPASS_CLASS];
    return t == RHI_DT_SUBPASS_INTEL ? RHI_DT_SUBPASS_INTEL
           : passRowQcom             ? RHI_DT_SUBPASS_QCOM
                                     : (int)t;
}

RhiDeviceVuRows rhi_device_vu_rows(const RhiDeviceChoices *c)
{
    RhiDeviceVuRows o;
    o.plain = c->value[RHI_DC_VU_FALLBACK_PLAIN] == RHI_DT_VUSET_TABLE
                  ? rhi_device_choice_reason(c, RHI_DC_VU_FALLBACK_PLAIN)
                  : NULL;
    o.careful = rhi_device_choice_why(c, RHI_DC_CAREFUL_PIPELINES);
    o.noPrepass = rhi_device_choice_why(c, RHI_DC_VU_PREPASS_OFF);
    o.noComputeRecord = rhi_device_choice_why(c, RHI_DC_COMPUTE_RECORD_OFF);
    return o;
}

/* ---------------------------------------------- the device's evaluation */

static RhiDeviceChoices s_current;
static int s_haveCurrent;

const RhiDeviceChoices *rhi_device_table_start(const RhiDeviceId *id, const RhiDeviceFakes *fakes,
                                               RhiDeviceLineFn emit, void *user)
{
    static char line[RHI_DT_LINE_MAX]; /* a device's start: one thread */
    rhi_device_table_eval(id, fakes, &s_current);
    s_haveCurrent = 1;
    rhi_device_table_note(&s_current);
    if (emit != NULL) {
        rhi_device_table_header(line, sizeof line, id, &s_current);
        emit(line, user);
        for (int r = 0; r < ROW_COUNT; r++) {
            if (s_current.applied[r]) {
                rhi_device_table_row_line(line, sizeof line, &s_current, r);
                emit(line, user);
            }
        }
    }
    return &s_current;
}

const RhiDeviceChoices *rhi_device_table_current(void)
{
    return s_haveCurrent ? &s_current : NULL;
}
