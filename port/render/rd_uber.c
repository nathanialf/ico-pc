/* rd_uber.c: GS state as shader data where the device orders the colour
 * read (g_rd.uber; docs/port/PIPELINES.md "GS state as data").
 *
 * The pipeline key carries what the GPU's fixed-function stages do for a
 * draw: the blend path (one canonical mode a path, rd_pipeline.c
 * canonicalBlend), the colour write mask, DATE's stencil modes, the
 * two-pass blend's halves.  Multiplied out over the families they make the
 * start-up set (1193 keys on a PowerVR DXT-48 phone: VU 264 = 11 vertex
 * shaders x 2 Z writes x 4 blend paths x 3 pixel shaders, DATE 514, 2D
 * 361).  The attachment read (rd_dst_read.c: the colour attachment read as
 * an input attachment in rasterization order) computes the GS's blend in
 * the fragment shader from the pixel it reads; with it in force for every
 * draw it can draw, those fields leave the key:
 *   blend   every pass of a planned draw on a single-sampled RGBA8 target
 *           through a twin with the read (rd__fs_has_dstatt) is drawn
 *           through that twin, blending off (RD_BLEND_COUNT): the LERPs and
 *           the other forms the exact blend took, and also the
 *           premultiplied adds and subtracts and the unblended passes
 *           (RdDrawPass.csOnly: the register 0x00, Cs), so one pipeline
 *           draws every ALPHA register (rd_dst_read.c rd__expand_dst);
 *   mask    the mask in DrawCB's WMASK bits, the shader writing a masked
 *           channel's Cd back; the cache's key takes the mask 0xF
 *           (rd__uber_normalise, through rd_pipeline.c drawKey and the
 *           start-up set): a UNORM8 texel read and written back is the
 *           same byte;
 *   DATE    the test against the alpha the fragment reads
 *           (ICO_DF_DATE_DST, common.hlsli GS_DATE_SNAP) instead of the R8
 *           snapshot or the stencil bit (rd_replay.c prepareDrawEx takes
 *           neither: rd__uber_date), so DATE has no keys and no setup
 *           passes; each fragment sees the earlier primitives' writes, the
 *           draw's own included (rasterization order): the GS's DATE,
 *           primitive by primitive, where the snapshot and the stencil bit
 *           hold the alpha from before the draw's first primitive (a DATE
 *           draw whose primitives overlap and change the alpha's MSB:
 *           rd_date_selftest.c's fifth sample); PRIM.AA1's per-primitive
 *           DATE draws (rd_replay_aa1.inc aa1DatePlan) are not needed then;
 *   nodual  no two-pass blend for these passes (no blending to split).
 * What stays in the key: the Z test and write, the formats, the topology
 * and AA1, the vertex shader, the pixel shader's family, the shadow
 * volume's stencil; and the keys this path never draws stay out of the
 * set (the plain twins of every key it takes, the DATE stencil keys):
 * rd_dst_read.c rd__enumerate_uber.  Left on the fixed-function path: the
 * MSAA epoch's passes (no read of a multisampled attachment), targets other
 * than RGBA8, the presentation overlay (rd_present.c: drawn on the output
 * with its own plan), the post and blit keys, the shadow counts.
 *
 * The pass: every pass on an RGBA8 target made for the read begins
 * readable (rd__exact_readable) and takes a pending clear as its load op
 * (the way DATE's stencil path takes it), with a COLOR local-read barrier
 * after the clear so the first read sees the load op's write (rd_replay.c
 * beginPass); the exact mode's own clear pass is not needed.
 *
 * The picture is the attachment exact mode's, the GS's integer
 * arithmetic (the premultiplied subtracts too, one level under the
 * fixed-function blend's where Cs*F is not a multiple of 128), and the
 * draw merge and the fused scissor pair (rd_replay_vu.c) join draws whose
 * keys became one: a merged draw is one draw in primitive order, the read
 * ordered within it.
 *
 * The device rule: Vulkan with the rasterization-order attachment read
 * and local read (RhiLimits.rasterOrderAttachment: never on Qualcomm's own
 * driver, issue 89, nor on PowerVR or under MoltenVK, which lack the
 * order); [dev] uber = auto (the default: on a tile-based GPU that has it,
 * Mali with the ARM extension, Adreno on Turnip), on (any device that has
 * it: lavapipe, which the tests use) or off; a [dev] blend other than
 * attachment (fixed, storage: the start-up retry's fixed blend among them)
 * turns it off.  The blend's self-test (rd_blend_selftest.c) checks the
 * read first: a failure under auto turns it off (rd__uber_off), the
 * fixed-function blend drawing; DATE's self-test checks the per-primitive
 * test, a failure leaving DATE to the snapshot (g_rd.uberDate).
 *
 * The log: the profile's start line ("GS state as shader data (raster-order
 * read)" or why not), "pipelines: GS state as shader data: on (...)" at
 * rd_init, the start-up set's "N keys in the start-up set instead of M"
 * (rd__uber_set_line) and the 10 s summary's pipeline cache by family
 * (rd__uber_perf_note, rd_perf.c rd_perf_backend_text). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "shader_consts.h"
#include "shaders_gen.h"

_Static_assert(RD_KF_COUNT == RD_PERF_PIPE_FAMILIES,
               "RdPerfRecord.pipeFamily holds one count per RdKeyFamily");

/* the last decision's words (rd__uber_wanted) */
static char s_why[160];

int rd__uber_asked_env(void)
{
    const char *e = getenv("ICO_RD_UBER");
    if (!e || !e[0] || strcmp(e, "auto") == 0) {
        return -1;
    }
    if (strcmp(e, "1") == 0 || strcmp(e, "on") == 0 || strcmp(e, "true") == 0 ||
        strcmp(e, "yes") == 0) {
        return 1;
    }
    if (strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "false") == 0 ||
        strcmp(e, "no") == 0) {
        return 0;
    }
    return -2; /* another word: auto (rd__uber_init says so) */
}

bool rd__uber_wanted(const char **why)
{
    const int ask = rd__uber_asked_env();
    const char *blend = getenv("ICO_RD_BLEND");
    const bool blendOther =
        blend && blend[0] && strcmp(blend, "attachment") != 0 && strcmp(blend, "exact") != 0;
    bool on = false;
    if (ask == 0) {
        snprintf(s_why, sizeof s_why, "off ([dev] uber = off)");
    } else if (blendOther) {
        snprintf(s_why, sizeof s_why, "off ([dev] blend = %.32s)", blend);
    } else if (rhi_backend() != RHI_BACKEND_VULKAN) {
        snprintf(s_why, sizeof s_why, "off (%s: the read in rasterization order is Vulkan's)",
                 rhi_backend_kind_name(rhi_backend()));
    } else {
        const RhiLimits *lim = rhi_limits();
        if (!lim->rasterOrderAttachment) {
            snprintf(s_why, sizeof s_why, "off (%.96s)",
                     lim->rasterOrderOff ? lim->rasterOrderOff
                                         : "no rasterization-order attachment read");
        } else if (!lim->localRead) {
            snprintf(s_why, sizeof s_why, "off (no local read)");
        } else if (ask < 0 && !lim->tiler) {
            snprintf(s_why, sizeof s_why,
                     "off (auto: tile-based GPUs only; [dev] uber = on draws with it here)");
        } else {
            on = true;
            snprintf(s_why, sizeof s_why, "on (raster-order read%s)",
                     ask > 0 ? ", [dev] uber = on" : "");
        }
    }
    if (why) {
        *why = s_why;
    }
    return on;
}

void rd__uber_init(void)
{
    const char *why = "off (no device)";
    const bool want = g_rd.hasDevice && rd__uber_wanted(&why);
    g_rd.uber = want && g_rd.dstMode == RD_BLEND_EXACT_ATTACHMENT;
    /* the A/B switches against the attachment read's own pictures (the
     * premultiplied forms on the blender, DATE through the snapshot or the
     * stencil bit: what the attachment read alone draws) */
    const char *ab = getenv("ICO_RD_UBER_DATE");
    g_rd.uberDate = g_rd.uber && !(ab && ab[0] == '0');
    ab = getenv("ICO_RD_UBER_PREMUL");
    g_rd.uberPremul = !(ab && ab[0] == '0');
    if (want && !g_rd.uber) {
        why = "off (the attachment read's shaders or layout were not made)";
    }
    if (rd__uber_asked_env() == -2) {
        rd__log("pipelines: ICO_RD_UBER=%s is not auto, on or off: auto", getenv("ICO_RD_UBER"));
    }
    if (g_rd.uber && (!g_rd.uberDate || !g_rd.uberPremul)) {
        rd__log(
            "pipelines: GS state as shader data: an A/B:%s%s",
            g_rd.uberDate ? ""
                          : " DATE through the snapshot or the stencil bit (ICO_RD_UBER_DATE=0)",
            g_rd.uberPremul
                ? ""
                : " the premultiplied adds and subtracts on the blender (ICO_RD_UBER_PREMUL=0)");
    }
    rd__log("pipelines: GS state as shader data: %s%s", why,
            g_rd.uber ? ": the blend, the write mask and DATE in the shader, the colour read in "
                        "rasterization order"
                      : "");
}

const char *rd__uber_profile_words(void)
{
    const char *why = "";
    static char w[200];
    if (rd__uber_wanted(&why)) {
        snprintf(w, sizeof w, "GS state as shader data (raster-order read)");
    } else {
        snprintf(w, sizeof w, "GS state in the pipelines (shader data %s)", why);
    }
    return w;
}

bool rd__uber_on(void)
{
    return g_rd.uber && g_rd.dstMode == RD_BLEND_EXACT_ATTACHMENT;
}

void rd__uber_off(const char *why)
{
    if (!g_rd.uber) {
        return;
    }
    g_rd.uber = false;
    g_rd.uberDate = false;
    const char *e = getenv("ICO_RD_BLEND");
    if (g_rd.dstMode == RD_BLEND_EXACT_ATTACHMENT && !(e && strcmp(e, "attachment") == 0)) {
        /* the attachment read was GS state as shader data's, not [dev] blend's */
        g_rd.dstMode = RD_BLEND_EXACT_OFF;
    }
    rd__log("pipelines: GS state as shader data: off (%s); the blend %s", why,
            rd__blend_exact_name(g_rd.dstMode));
}

bool rd__fs_has_dstatt(uint8_t fs)
{
    if (rd__fs_has_dst(fs)) {
        return true;
    }
    /* vu_np_ps's own twin (GS state as shader data alone); without a
     * device (the CPU tests' plans) assumed */
    return fs == RD_FS_VU_NP && (!g_rd.hasDevice || g_rd.fsDstAtt[RD_FS_VU_NP].id != 0);
}

bool rd__uber_np(void)
{
    return rd__uber_on() && rd__fs_has_dstatt(RD_FS_VU_NP);
}

void rd__make_dst_twins(uint8_t fs, const char *name, bool storage, bool attach)
{
    char twin[64];
    if (rd__fs_has_dst(fs)) {
        if (storage) {
            snprintf(twin, sizeof(twin), "%s_dstrov", name);
            g_rd.fsDstRov[fs] = rd__make_shader(twin);
        }
        if (attach) {
            snprintf(twin, sizeof(twin), "%s_dstatt", name);
            g_rd.fsDstAtt[fs] = rd__make_shader(twin);
        }
        return;
    }
    /* vu_np_ps's attachment twin, for GS state as shader data only (a
     * table without it leaves the VU draws their planes under it,
     * rd_pixel_plan.c rd__vu_pixel_fs) */
    if (attach && fs == RD_FS_VU_NP && rd__uber_wanted(NULL)) {
        snprintf(twin, sizeof(twin), "%s_dstatt", name);
        if (ico_find_shader(twin)) {
            g_rd.fsDstAtt[fs] = rd__make_shader(twin);
        }
    }
}

bool rd__uber_key_ok(const RdPipeKeyInt *k)
{
    return k->dst == RHI_DST_READ_NONE && k->colorFmt == RHI_FMT_RGBA8_UNORM &&
           rd__fs_has_dstatt(k->fs) && k->gs.nodual == 0 && k->gs.stencil == RD_STENCIL_OFF &&
           k->gs.msaa <= 1;
}

bool rd__uber_normal(const RdPipeKeyInt *k)
{
    return g_rd.uber && k->dst == RHI_DST_READ_ATTACHMENT && k->gs.colorMask != 0xF;
}

void rd__uber_normalise(RdPipeKeyInt *k)
{
    if (rd__uber_normal(k)) {
        k->gs.colorMask = 0xF; /* the mask is the shader's (DrawCB WMASK) */
    }
}

bool rd__uber_date(const RdTargetRec *tc, bool msaa)
{
    return rd__uber_on() && g_rd.uberDate && !msaa && tc && tc->format == RHI_FMT_RGBA8_UNORM &&
           (tc->dstUsage & RD_DST_USAGE_ATTACHMENT) != 0;
}

/* the exact blend's attachment read: every pass on an RGBA8 target made
 * for it is begun readable (outside the MSAA epoch) */
bool rd__exact_readable(const RdTargetRec *c)
{
    return g_rd.dstMode == RD_BLEND_EXACT_ATTACHMENT && (c->dstUsage & RD_DST_USAGE_ATTACHMENT) &&
           c->format == RHI_FMT_RGBA8_UNORM;
}

/* readable: the exact blend's attachment read, or DATE through the stencil
 * bit (RdProfile.dateStencil: its setup reads the alpha, rd__replay_date_take),
 * on an RGBA8 target made for it (rd_core.c: the scene class) */
bool rd__pass_wants_readable(const RdTargetRec *c)
{
    return rd__exact_readable(c) ||
           (g_rd.profile.dateStencil && (c->dstUsage & RD_DST_USAGE_LOCAL_DATE) &&
            c->format == RHI_FMT_RGBA8_UNORM);
}

void rd__uber_set_line(uint32_t n)
{
    if (!rd__uber_on()) {
        return;
    }
    /* the set the device would make without it: the fixed-function blend's */
    static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
    const uint8_t keepMode = g_rd.dstMode;
    g_rd.uber = false;
    g_rd.dstMode = RD_BLEND_EXACT_OFF;
    const uint32_t m = rd__enumerate_reachable(keys, RD_PIPELINE_CACHE_MAX);
    g_rd.dstMode = keepMode;
    g_rd.uber = true;
    rd__log("pipelines: GS state as shader data (the colour read in order: %u keys in the "
            "start-up set instead of %u)",
            n, m);
}

void rd__uber_perf_note(RdPerfRecord *r)
{
    /* the cache by family, counted again when it grew (a pipeline made) */
    static uint32_t s_counted = UINT32_MAX;
    static uint16_t s_fam[RD_KF_COUNT];
    const uint32_t n = rd__pipeline_count();
    if (n != s_counted) {
        s_counted = n;
        memset(s_fam, 0, sizeof(s_fam));
        for (uint32_t i = 0; i < n; i++) {
            const RdPipeKeyInt *k = rd__pipeline_key_at(i);
            if (k) {
                s_fam[rd__pipe_key_family(k)]++;
            }
        }
    }
    memcpy(r->pipeFamily, s_fam, sizeof(r->pipeFamily));
    r->uber = (uint8_t)rd__uber_on();
}
