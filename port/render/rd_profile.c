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
 *   fogLocal      on (1)   off     on where the device has local read
 *   dateStencil   on (1)   off     on where the device has local read
 *   storePlan     off on every class: the store plan is not in the renderer
 *                 (docs/port/RENDER.md "The device profile"); [dev]
 *                 store_plan sets the flag, which nothing reads
 *   blurFold      on       on      on        on       off   on       on
 *   robust access off: the Vulkan backend's default (vk_device.c;
 *                 gpu_robust = true turns it on); D3D12 and D3D11 their
 *                 own bounds; OpenGL the backend's RhiLimits.robustText
 *   localRead     what the device offers (RhiLimits.localRead; none on
 *                 D3D12, D3D11 and OpenGL)
 *   uintViewWork  on       on      off       off      off   off      off
 *   uintViewScene on       on      off       off      off   off      off
 *                 (both only where the backend has the view,
 *                 RhiLimits.uintView)
 *   carefulSet    false here (the start-up set's careful policy is
 *                 RdPipelineSetPolicy's, rd_pipeline.c)
 *   alphaPlan     on on every class (rd_alpha_plan.c)
 *   fxPlan        on on every class (rd_fx_plan.c)
 *   fxDstLocal    on (1)   off     on where the device has local read
 *                 (fx_local_ps, rd_fx_plan.c)
 *   vuPlanesFrac  on on every class: the VU pixel side without the planes
 *                 at a fractional scale (rd_pixel_plan.c)
 *   crackFill     on on every class (the crack filler above 1x,
 *                 rd_crack_fill.c: on the local read's path it draws in
 *                 the pass the fog then keeps, so a tile-based GPU pays
 *                 its copies and one pass; [dev] crack_fill = true or
 *                 false sets it, read there, and the filler's own line
 *                 says which)
 *   shadowOne     on on every class whose backend reads a depth-stencil's
 *                 stencil through a view (RhiLimits.stencilView, with
 *                 depthSampled): the shadow count resolved in one draw
 *                 (rd_replay.c shadowCountOne), else the seven
 *                 stencil-tested draws and the box reduction; its
 *                 self-test (rd_shadow_selftest.c) compares the two
 *
 * (1) The desktop's local reads: on where the device has local
 * read (VK_KHR_dynamic_rendering_local_read or Vulkan 1.4's) and the
 * renderer runs with a window (the game), guarded by the cost probe
 * (rd_local_probe.c), which runs once after the self-tests and turns the
 * three off where the local reads draw slower than the snapshots
 * ("(probe)").  The worry it answers: a readable attachment sits in the
 * local-read layout for the whole scene pass, which may cost an
 * immediate-mode GPU's colour and depth compression.  A headless run (the
 * replay tool, the tests: no window) keeps the snapshots unless
 * ICO_RD_LOCAL_PROBE is set: 1 the game's default and its probe (where the
 * self-tests run), 0 the local reads without the probe.  Where the device
 * has no local read the three stay off.
 *
 * The OpenGL and D3D11 classes take the desktop column whatever the
 * vendor (neither backend reports a tiler: gl_device.c, d3d11_device.c);
 * what differs from desktop Vulkan comes from their limits, not the class:
 * no UINT view on D3D11 (uintView false: both uint views off), the VU
 * vertex pre-pass off on D3D11 (colorBytesPerPixel 32, rd_replay_vu.c
 * rd__vu_prepass_init) and on on OpenGL (0), no local read on either
 * (docs/port/BACKENDS.md), so none of the local reads.
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
 * 1|work|0 (both views, the work buffers' only, none), ICO_RD_ALPHA_PLAN=
 * 1|0, ICO_RD_FX_PLAN=1|0, ICO_RD_FX_DST_LOCAL=1|0, ICO_RD_VU_PLANES=1|0,
 * ICO_RD_SHADOW_ONE=1|0 (1 asks for the one draw, held to the view).  A flag a [dev] key
 * set carries "([dev])" in the lines; one the device cannot do stays off
 * and gets a line of its own.  A self-test that finds its mechanism wrong
 * turns the flag off (rd__profile_self_test_off: "(self-test)"); the
 * desktop's probe turns the local reads off where they are slower
 * (rd__profile_probe_off: "(probe)").  A [dev] key that sets any of the
 * three local-read flags on the desktop class leaves the probe out.
 *
 *   rd: profile: tile-based GPU (Imagination, Vulkan): blend fixed-function;
 *     fog local on; DATE stencil; depth stores planned; blur fold on; robust
 *     access off (the default; gpu_robust = true turns it on); local read
 *     yes; uint views off; alpha plan on; fx plan on; fx local read on;
 *     VU planes whole scales only; shadow count one draw; pipelines the
 *     start-up set
 *
 * and the summary's words (rd__profile_text, after the blend's):
 *   tile-based GPU (Imagination, Vulkan): fog local on, DATE stencil, depth
 *   stores planned, blur fold on, local read yes, uint views off, alpha
 *   plan on, fx plan on, fx local on, VU planes whole scales only, shadow
 *   count one draw
 *
 * On the desktop class with local read both end with the desktop's local
 * reads ("; desktop local read on (the probe follows the self-tests)" in
 * the start line, ", desktop local read kept (probe 0.412 vs 0.530 ms)" in
 * the summary once the probe ran; rd_local_probe.c lists the words).
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
    PC_OPENGL, /* the desktop column on the OpenGL backend */
    PC_D3D11,  /* the desktop column on the D3D11 backend */
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

/* NULL for 0 (unknown: the D3D12 backend; OpenGL when the GL_VENDOR and
 * GL_RENDERER strings name none of AMD, NVIDIA or Intel, as llvmpipe) */
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
    switch (rhi_backend()) {
    case RHI_BACKEND_D3D12:
        return PC_D3D12;
    case RHI_BACKEND_OPENGL:
        return PC_OPENGL;
    case RHI_BACKEND_D3D11:
        return PC_D3D11;
    default:
        break;
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
    if (p->probeOff & bits) {
        return " (probe)";
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

/* the VU planes' words: vuPlanesFrac drops them at a fractional scale */
static const char *planesWords(const RdProfile *p)
{
    return p->vuPlanesFrac ? "whole scales only" : "every scale";
}

/* the shadow count's words: "one draw" or "seven draws" */
static const char *shadowWords(const RdProfile *p)
{
    return p->shadowOne ? "one draw" : "seven draws";
}

/* the summary's words, as in force */
static void makeText(RdProfile *p)
{
    snprintf(p->text, sizeof p->text,
             "%s: fog local %s%s, DATE %s%s, depth stores %s%s, blur fold %s%s, local read %s, "
             "uint views %s%s, alpha plan %s%s, fx plan %s%s, fx local %s%s, VU planes %s%s, "
             "shadow count %s%s%s%s",
             p->cls, p->fogLocal ? "on" : "off", mark(p, RD_PROFILE_FOG_LOCAL),
             p->dateStencil ? "stencil" : "snapshot", mark(p, RD_PROFILE_DATE_STENCIL),
             p->storePlan ? "planned" : "kept", mark(p, RD_PROFILE_STORE_PLAN),
             p->blurFold ? "on" : "off", mark(p, RD_PROFILE_BLUR_FOLD), p->localRead ? "yes" : "no",
             uintWords(p), mark(p, UINT_BITS), p->alphaPlan ? "on" : "off",
             mark(p, RD_PROFILE_ALPHA_PLAN), p->fxPlan ? "on" : "off", mark(p, RD_PROFILE_FX_PLAN),
             p->fxDstLocal ? "on" : "off", mark(p, RD_PROFILE_FX_DST_LOCAL), planesWords(p),
             mark(p, RD_PROFILE_VU_PLANES), shadowWords(p), mark(p, RD_PROFILE_SHADOW_ONE),
             p->localWords[0] ? ", desktop local read " : "", p->localWords);
}

void rd__profile_init(void)
{
    RdProfile *p = &g_rd.profile;
    const RhiLimits *lim = rhi_limits();
    memset(p, 0, sizeof(*p));
    const ProfileClass c = classOf(lim);
    /* D3D12 and D3D11: no local read in the API ("n/a") and the API's own
     * bounds on buffer access (both report robustText NULL) */
    const bool d3d = c == PC_D3D12 || c == PC_D3D11;
    const bool tile = c != PC_DESKTOP && c != PC_D3D12 && c != PC_OPENGL && c != PC_D3D11;
    p->tiler = lim->tiler;
    p->localRead = lim->localRead;
    /* the desktop's local reads (the file comment's (1)): with a window, or
       as ICO_RD_LOCAL_PROBE asks in a headless run */
    const bool desk = c == PC_DESKTOP && p->localRead;
    const int probeEnv = envBool("ICO_RD_LOCAL_PROBE");
    p->desktopLocal = desk && (g_rd.windowed || probeEnv >= 0);
    p->localProbe = p->desktopLocal && probeEnv != 0;
    const bool local = (tile || p->desktopLocal) && p->localRead;

    /* the table (the file comment) */
    p->fogLocal = local;
    p->dateStencil = local;
    p->storePlan = false; /* the store plan is not implemented: nothing reads the flag */
    p->blurFold = c != PC_MALI;
    p->uintViewWork = !tile && lim->uintView;
    p->uintViewScene = !tile && lim->uintView;
    p->carefulSet = false;
    p->alphaPlan = true;
    p->fxPlan = true;
    p->fxDstLocal = local;
    p->vuPlanesFrac = true;
    p->crackFill = true; /* rd_crack_fill.c reads ICO_RD_CRACK_FILL over it */
    const bool stencilView = lim->stencilView && lim->depthSampled;
    p->shadowOne = stencilView;

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
    const int alpha = envBool("ICO_RD_ALPHA_PLAN");
    if (alpha >= 0) {
        p->alphaPlan = alpha == 1;
        p->devSet |= RD_PROFILE_ALPHA_PLAN;
    }
    const int fx = envBool("ICO_RD_FX_PLAN");
    if (fx >= 0) {
        p->fxPlan = fx == 1;
        p->devSet |= RD_PROFILE_FX_PLAN;
    }
    const int fxLocal = envBool("ICO_RD_FX_DST_LOCAL");
    if (fxLocal >= 0) {
        p->fxDstLocal = fxLocal == 1 && p->localRead;
        p->devSet |= RD_PROFILE_FX_DST_LOCAL;
        notHere = fxLocal == 1 && !p->localRead ? "[dev] fx_dst_local (no local read)" : notHere;
    }
    const int planes = envBool("ICO_RD_VU_PLANES");
    if (planes >= 0) {
        p->vuPlanesFrac = planes == 1;
        p->devSet |= RD_PROFILE_VU_PLANES;
    }
    const int shadow = envBool("ICO_RD_SHADOW_ONE");
    if (shadow >= 0) {
        p->shadowOne = shadow == 1 && stencilView;
        p->devSet |= RD_PROFILE_SHADOW_ONE;
        notHere = shadow == 1 && !stencilView ? "[dev] shadow_one (no stencil view on this backend)"
                                              : notHere;
    }

    /* the desktop's local-read words before the probe (rd_local_probe.c
       sets them after it) */
    if (desk) {
        const bool any = p->fogLocal || p->dateStencil || p->fxDstLocal;
        if (p->devSet & RD_PROFILE_LOCAL_BITS) {
            p->localProbe = false; /* the [dev] keys decide: no probe */
            snprintf(p->localWords, sizeof p->localWords, "%s ([dev])", any ? "on" : "off");
        } else if (!p->desktopLocal) {
            snprintf(p->localWords, sizeof p->localWords,
                     "off (no window: the game's probe not run; ICO_RD_LOCAL_PROBE=1 asks for "
                     "it)");
        } else if (!p->localProbe) {
            snprintf(p->localWords, sizeof p->localWords, "on (not probed: ICO_RD_LOCAL_PROBE=0)");
        } else {
            snprintf(p->localWords, sizeof p->localWords, "on (the probe follows the self-tests)");
        }
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
                 vendor ? vendor : "", vendor ? ", " : "", driver,
                 rhi_backend_kind_name(rhi_backend()));
    }
    makeText(p);

    /* the start line: the blend as rd__dst_init will set it (the ask read
     * now, as rd__gpu_init read it), robust access as the backend made the
     * device */
    const int asked = rd__dst_asked_env();
    const bool exact = (asked == RD_BLEND_EXACT_ATTACHMENT && lim->rasterOrderAttachment) ||
                       (asked == RD_BLEND_EXACT_STORAGE && (lim->fragmentInterlock || lim->rov));
    rd__log("profile: %s: blend %s%s; fog local %s%s; DATE %s%s; depth stores %s%s; blur fold "
            "%s%s; robust access %s; local read %s; uint views %s%s; alpha plan %s%s; fx plan "
            "%s%s; fx local read %s%s; VU planes %s%s; shadow count %s%s; pipelines the start-up "
            "set%s%s%s",
            p->cls, exact ? rd__blend_exact_name(asked) : "fixed-function", exact ? " ([dev])" : "",
            p->fogLocal ? "on" : "off", mark(p, RD_PROFILE_FOG_LOCAL),
            p->dateStencil ? "stencil" : "snapshot", mark(p, RD_PROFILE_DATE_STENCIL),
            p->storePlan ? "planned" : "kept", mark(p, RD_PROFILE_STORE_PLAN),
            p->blurFold ? "on" : "off", mark(p, RD_PROFILE_BLUR_FOLD),
            lim->robustText ? lim->robustText
            : c == PC_D3D11 ? "D3D11's own"
            : d3d           ? "D3D12's own"
                            : "off",
            d3d            ? "n/a"
            : p->localRead ? "yes"
                           : "no",
            uintWords(p), mark(p, UINT_BITS), p->alphaPlan ? "on" : "off",
            mark(p, RD_PROFILE_ALPHA_PLAN), p->fxPlan ? "on" : "off", mark(p, RD_PROFILE_FX_PLAN),
            p->fxDstLocal ? "on" : "off", mark(p, RD_PROFILE_FX_DST_LOCAL), planesWords(p),
            mark(p, RD_PROFILE_VU_PLANES), shadowWords(p), mark(p, RD_PROFILE_SHADOW_ONE),
            p->carefulSet ? " (careful)" : "", p->localWords[0] ? "; desktop local read " : "",
            p->localWords);
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
        {RD_PROFILE_ALPHA_PLAN, offsetof(RdProfile, alphaPlan), "alpha plan"},
        {RD_PROFILE_FX_PLAN, offsetof(RdProfile, fxPlan), "fx plan"},
        {RD_PROFILE_FX_DST_LOCAL, offsetof(RdProfile, fxDstLocal), "fx local read"},
        {RD_PROFILE_VU_PLANES, offsetof(RdProfile, vuPlanesFrac), "VU planes rule"},
        {RD_PROFILE_SHADOW_ONE, offsetof(RdProfile, shadowOne), "shadow count one draw"},
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

void rd__profile_local_words(const char *words)
{
    RdProfile *p = &g_rd.profile;
    snprintf(p->localWords, sizeof p->localWords, "%s", words ? words : "");
    p->localProbe = false;
    makeText(p);
}

void rd__profile_probe_off(void)
{
    RdProfile *p = &g_rd.profile;

    static const struct {
        unsigned bit;
        size_t offset;
        const char *name;
    } kLocal[] = {
        {RD_PROFILE_FOG_LOCAL, offsetof(RdProfile, fogLocal), "fog local"},
        {RD_PROFILE_DATE_STENCIL, offsetof(RdProfile, dateStencil), "DATE stencil"},
        {RD_PROFILE_FX_DST_LOCAL, offsetof(RdProfile, fxDstLocal), "fx local read"},
    };

    for (size_t i = 0; i < sizeof kLocal / sizeof kLocal[0]; i++) {
        bool *f = (bool *)((char *)p + kLocal[i].offset);
        if (*f) {
            *f = false;
            p->probeOff |= (uint16_t)kLocal[i].bit;
            rd__log("profile: %s off (probe)", kLocal[i].name);
        }
    }
    makeText(p);
}

const char *rd__profile_text(void)
{
    return g_rd.profile.text;
}
