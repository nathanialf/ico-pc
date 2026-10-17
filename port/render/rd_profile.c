/* rd_profile.c: the device profile (rd_internal.h RdProfile;
 * docs/port/RENDER.md "The device profile").
 *
 * One renderer, one table: after rd__gpu_init the profile is decided once
 * from RhiLimits (tiler, localRead, vendorId, moltenvk, uintView, the
 * driver text) and the backend, and every vendor-dependent choice in the
 * renderer reads a flag of it; no other file of the renderer branches on a
 * vendor.  The device classes and their defaults:
 *
 *   flag          desktop  D3D12   Adreno    Adreno   Mali  PowerVR  Apple /
 *                 Vulkan           Qualcomm  Turnip                  MoltenVK
 *   blend         fixed-function on every class; [dev] blend opts into an
 *                 exact mode (rd_dst_read.c)
 *   fogLocal      off      off     on where the device has local read
 *   dateStencil   off      off     on where the device has local read
 *   storePlan     off on every class: the store plan is not in the renderer
 *                 (docs/port/RENDER.md "The device profile"); [dev]
 *                 store_plan sets the flag, which nothing reads
 *   blurFold      off      off     on        on       off   on       on
 *   robust access off: the Vulkan backend's default (vk_device.c;
 *                 gpu_robust = true turns it on); D3D12 its own bounds
 *   localRead     what the device offers (RhiLimits.localRead; D3D12 none)
 *   uintViewWork  on       on      off       off      off   off      off
 *   uintViewScene on       on      off       off      off   off      off
 *                 (both only where the backend has the view,
 *                 RhiLimits.uintView)
 *   carefulSet    false here (the start-up set's careful policy is
 *                 RdPipelineSetPolicy's, rd_pipeline.c)
 *
 * The Mali row keeps the blur fold off: the G52 is bound by its ALU, which
 * the fold does not spare.  A tile-based GPU of another vendor (Samsung,
 * Broadcom, any Android GPU RhiLimits.tiler counts) takes the PowerVR row.
 * A flag is only a decision: its mechanism reads it where it lives (the
 * fog's local path, DATE through the stencil bit, the store plan, the
 * folded blur, the UINT views), and a flag whose mechanism is not in the
 * renderer changes nothing.
 *
 * The [dev] keys override one flag at a time, through the environment the
 * host exports (port/platform/host_config.c): ICO_RD_FOG_PATH=local
 * (fogLocal on; inplace, copy or buffer: off), ICO_RD_DATE_PATH=stencil|
 * snapshot, ICO_RD_STORE_PLAN=1|0, ICO_RD_BLUR_FOLD=1|0, ICO_RD_UINT_VIEW=
 * 1|work|0 (both views, the work buffers' only, none).  A flag a [dev] key
 * set carries "([dev])" in the lines; one the device cannot do stays off
 * and gets a line of its own.  A self-test that finds its mechanism wrong
 * turns the flag off (rd__profile_self_test_off: "(self-test)").
 *
 *   rd: profile: tile-based GPU (Imagination, Vulkan): blend fixed-function;
 *     fog local on; DATE stencil; depth stores planned; blur fold on; robust
 *     access off (the default; gpu_robust = true turns it on); local read
 *     yes; uint views off; pipelines the start-up set
 *
 * and the summary's words (rd__profile_text, after the blend's):
 *   tile-based GPU (Imagination, Vulkan): fog local on, DATE stencil, depth
 *   stores planned, blur fold on, local read yes, uint views off
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

/* the device classes of the table */
typedef enum ProfileClass {
    PC_DESKTOP,
    PC_D3D12,
    PC_ADRENO_QUALCOMM,
    PC_ADRENO_TURNIP,
    PC_MALI,
    PC_POWERVR,
    PC_APPLE,
    PC_TILER, /* another tile-based GPU: the PowerVR row */
} ProfileClass;

#define VENDOR_AMD 0x1002u
#define VENDOR_NVIDIA 0x10DEu
#define VENDOR_INTEL 0x8086u
#define VENDOR_QUALCOMM 0x5143u
#define VENDOR_ARM 0x13B5u
#define VENDOR_IMAGINATION 0x1010u
#define VENDOR_APPLE 0x106Bu
#define VENDOR_SAMSUNG 0x144Du
#define VENDOR_BROADCOM 0x14E4u
#define VENDOR_MESA 0x10005u /* VK_VENDOR_ID_MESA: lavapipe */
#define VENDOR_MICROSOFT 0x1414u

/* NULL for 0 (unknown: the D3D12 backend) */
static const char *vendorName(uint32_t v, char *buf, size_t n)
{
    switch (v) {
    case VENDOR_AMD:
        return "AMD";
    case VENDOR_NVIDIA:
        return "NVIDIA";
    case VENDOR_INTEL:
        return "Intel";
    case VENDOR_QUALCOMM:
        return "Qualcomm";
    case VENDOR_ARM:
        return "Arm";
    case VENDOR_IMAGINATION:
        return "Imagination";
    case VENDOR_APPLE:
        return "Apple";
    case VENDOR_SAMSUNG:
        return "Samsung";
    case VENDOR_BROADCOM:
        return "Broadcom";
    case VENDOR_MESA:
        return "Mesa";
    case VENDOR_MICROSOFT:
        return "Microsoft";
    case 0:
        return NULL;
    default:
        snprintf(buf, n, "vendor 0x%04x", (unsigned)v);
        return buf;
    }
}

/* s holds word, ASCII letters in any case */
static bool hasWord(const char *s, const char *word)
{
    const size_t n = strlen(word);
    for (; s && *s; s++) {
        size_t i = 0;
        while (i < n && s[i] && (s[i] | 0x20) == (word[i] | 0x20)) {
            i++;
        }
        if (i == n) {
            return true;
        }
    }
    return false;
}

static ProfileClass classOf(const RhiLimits *lim)
{
    if (rhi_backend() == RHI_BACKEND_D3D12) {
        return PC_D3D12;
    }
    if (lim->moltenvk) {
        return PC_APPLE;
    }
    switch (lim->vendorId) {
    case VENDOR_QUALCOMM:
        /* Mesa's Turnip reports Qualcomm's vendor id and names itself in
         * the driver text (VkPhysicalDeviceDriverProperties.driverName
         * "turnip Mesa driver", RhiLimits.driverText); Qualcomm's own
         * driver does not */
        return hasWord(lim->driverText, "turnip") ? PC_ADRENO_TURNIP : PC_ADRENO_QUALCOMM;
    case VENDOR_ARM:
        return PC_MALI;
    case VENDOR_IMAGINATION:
        return PC_POWERVR;
    default:
        return lim->tiler ? PC_TILER : PC_DESKTOP;
    }
}

/* ICO_<name>: -1 unset, else 1 for 1/true/on/yes, 0 for anything else */
static int envBool(const char *name)
{
    const char *e = getenv(name);
    if (!e || !e[0]) {
        return -1;
    }
    return strcmp(e, "1") == 0 || strcmp(e, "true") == 0 || strcmp(e, "on") == 0 ||
           strcmp(e, "yes") == 0;
}

/* " ([dev])", " (self-test)" or "" after a flag of bits */
static const char *mark(const RdProfile *p, unsigned bits)
{
    if (p->selfTestOff & bits) {
        return " (self-test)";
    }
    return (p->devSet & bits) ? " ([dev])" : "";
}

#define UINT_BITS ((unsigned)RD_PROFILE_UINT_WORK | (unsigned)RD_PROFILE_UINT_SCENE)

static const char *uintWords(const RdProfile *p)
{
    return p->uintViewWork && p->uintViewScene ? "work and scene"
           : p->uintViewWork                   ? "work"
           : p->uintViewScene                  ? "scene"
                                               : "off";
}

/* the summary's words, as in force */
static void makeText(RdProfile *p)
{
    snprintf(p->text, sizeof p->text,
             "%s: fog local %s%s, DATE %s%s, depth stores %s%s, blur fold %s%s, local read %s, "
             "uint views %s%s",
             p->cls, p->fogLocal ? "on" : "off", mark(p, RD_PROFILE_FOG_LOCAL),
             p->dateStencil ? "stencil" : "snapshot", mark(p, RD_PROFILE_DATE_STENCIL),
             p->storePlan ? "planned" : "kept", mark(p, RD_PROFILE_STORE_PLAN),
             p->blurFold ? "on" : "off", mark(p, RD_PROFILE_BLUR_FOLD), p->localRead ? "yes" : "no",
             uintWords(p), mark(p, UINT_BITS));
}

void rd__profile_init(void)
{
    RdProfile *p = &g_rd.profile;
    const RhiLimits *lim = rhi_limits();
    memset(p, 0, sizeof(*p));
    const ProfileClass c = classOf(lim);
    const bool d3d12 = c == PC_D3D12;
    const bool tile = c != PC_DESKTOP && c != PC_D3D12;
    p->tiler = lim->tiler;
    p->localRead = lim->localRead;

    /* the table (the file comment) */
    p->fogLocal = tile && p->localRead;
    p->dateStencil = tile && p->localRead;
    p->storePlan = false; /* the store plan is not implemented: nothing reads the flag */
    p->blurFold = tile && c != PC_MALI;
    p->uintViewWork = !tile && lim->uintView;
    p->uintViewScene = !tile && lim->uintView;
    p->carefulSet = false;

    /* the [dev] keys, through the host's environment */
    const char *notHere = NULL; /* a [dev] key asked for what the device lacks */
    const char *fog = getenv("ICO_RD_FOG_PATH");
    if (fog && strcmp(fog, "local") == 0) {
        p->fogLocal = p->localRead;
        p->devSet |= RD_PROFILE_FOG_LOCAL;
        notHere = p->localRead ? notHere : "[dev] fog_path = local (no local read)";
    } else if (fog && (strcmp(fog, "inplace") == 0 || strcmp(fog, "copy") == 0 ||
                       strcmp(fog, "buffer") == 0)) {
        p->fogLocal = false;
        p->devSet |= RD_PROFILE_FOG_LOCAL;
    }
    const char *date = getenv("ICO_RD_DATE_PATH");
    if (date && strcmp(date, "stencil") == 0) {
        p->dateStencil = p->localRead;
        p->devSet |= RD_PROFILE_DATE_STENCIL;
        notHere = p->localRead ? notHere : "[dev] date_path = stencil (no local read)";
    } else if (date && strcmp(date, "snapshot") == 0) {
        p->dateStencil = false;
        p->devSet |= RD_PROFILE_DATE_STENCIL;
    }
    const int store = envBool("ICO_RD_STORE_PLAN");
    if (store >= 0) {
        p->storePlan = store == 1;
        p->devSet |= RD_PROFILE_STORE_PLAN;
    }
    const int fold = envBool("ICO_RD_BLUR_FOLD");
    if (fold >= 0) {
        p->blurFold = fold == 1;
        p->devSet |= RD_PROFILE_BLUR_FOLD;
    }
    const char *uv = getenv("ICO_RD_UINT_VIEW");
    if (uv && uv[0]) {
        const bool work = strcmp(uv, "work") == 0;
        const bool all = !work && envBool("ICO_RD_UINT_VIEW") == 1;
        p->uintViewWork = (work || all) && lim->uintView;
        p->uintViewScene = all && lim->uintView;
        p->devSet |= UINT_BITS;
        notHere = (work || all) && !lim->uintView ? "[dev] uint_view (no UINT view on this backend)"
                                                  : notHere;
    }

    /* the class's name: "tile-based GPU (Imagination, Vulkan)" */
    {
        char vbuf[24];
        const char *vendor = vendorName(lim->vendorId, vbuf, sizeof vbuf);
        const char *driver = c == PC_ADRENO_QUALCOMM ? "Qualcomm's driver, "
                             : c == PC_ADRENO_TURNIP ? "Turnip, "
                             : c == PC_APPLE         ? "MoltenVK, "
                                                     : "";
        snprintf(p->cls, sizeof p->cls, "%s GPU (%s%s%s%s)", tile ? "tile-based" : "immediate-mode",
                 vendor ? vendor : "", vendor ? ", " : "", driver, d3d12 ? "D3D12" : "Vulkan");
    }
    makeText(p);

    /* the start line: the blend as rd__dst_init will set it (the ask read
     * now, as rd__gpu_init read it), robust access as the backend made the
     * device */
    const int asked = rd__dst_asked_env();
    const bool exact = (asked == RD_BLEND_EXACT_ATTACHMENT && lim->rasterOrderAttachment) ||
                       (asked == RD_BLEND_EXACT_STORAGE && (lim->fragmentInterlock || lim->rov));
    rd__log("profile: %s: blend %s%s; fog local %s%s; DATE %s%s; depth stores %s%s; blur fold "
            "%s%s; robust access %s; local read %s; uint views %s%s; pipelines the start-up "
            "set%s",
            p->cls, exact ? rd__blend_exact_name(asked) : "fixed-function", exact ? " ([dev])" : "",
            p->fogLocal ? "on" : "off", mark(p, RD_PROFILE_FOG_LOCAL),
            p->dateStencil ? "stencil" : "snapshot", mark(p, RD_PROFILE_DATE_STENCIL),
            p->storePlan ? "planned" : "kept", mark(p, RD_PROFILE_STORE_PLAN),
            p->blurFold ? "on" : "off", mark(p, RD_PROFILE_BLUR_FOLD),
            lim->robustText ? lim->robustText
            : d3d12         ? "D3D12's own"
                            : "off",
            d3d12          ? "n/a"
            : p->localRead ? "yes"
                           : "no",
            uintWords(p), mark(p, UINT_BITS), p->carefulSet ? " (careful)" : "");
    if (notHere) {
        rd__log("profile: %s: not on this device, the flag stays off", notHere);
    }
}

void rd__profile_self_test_off(unsigned flag)
{
    RdProfile *p = &g_rd.profile;

    static const struct {
        unsigned bit;
        size_t offset;
        const char *name;
    } kFlags[] = {
        {RD_PROFILE_FOG_LOCAL, offsetof(RdProfile, fogLocal), "fog local"},
        {RD_PROFILE_DATE_STENCIL, offsetof(RdProfile, dateStencil), "DATE stencil"},
        {RD_PROFILE_STORE_PLAN, offsetof(RdProfile, storePlan), "depth store plan"},
        {RD_PROFILE_BLUR_FOLD, offsetof(RdProfile, blurFold), "blur fold"},
        {RD_PROFILE_UINT_WORK, offsetof(RdProfile, uintViewWork), "uint view (work)"},
        {RD_PROFILE_UINT_SCENE, offsetof(RdProfile, uintViewScene), "uint view (scene)"},
    };

    for (size_t i = 0; i < sizeof kFlags / sizeof kFlags[0]; i++) {
        bool *f = (bool *)((char *)p + kFlags[i].offset);
        if ((flag & kFlags[i].bit) && *f) {
            *f = false;
            p->selfTestOff |= (uint16_t)kFlags[i].bit;
            rd__log("profile: %s off (self-test)", kFlags[i].name);
        }
    }
    makeText(p);
}

const char *rd__profile_text(void)
{
    return g_rd.profile.text;
}
