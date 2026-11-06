/* rd_vu_skin_check.c: the skinned draws' bone-block check in the game (S2)
 * and its box check (S3) (rd_vu_skin_check.h; docs/port/VU_SKIN_CHECK.md;
 * issue 163).
 *
 * The pick (rd__vu_skin_pick, doVuDraw): on an Adreno's Vulkan device
 * with a window (rd__skin_check_default; [dev] vu_skin_check = true
 * anywhere), at most every 100 ms, the next skinned command of the replay in a round
 * robin over the replay's skinned commands (Yorda's 26 draws and Ico's are
 * all covered within a few seconds).  The CPU's side is kept then: VU
 * memory 0..35 from the payload, the stream's vertices of the draw's
 * batches (at most RD_SKIN_MAXV) from the mesh, and their positions
 * through vu1_ref's cluster program (rd__skin_reference).
 *
 * The note (rd__vu_skin_note, vuDraw after the draw, in its pass): the
 * 3,840 VuBoneCB bytes the ring holds at the draw's offset (what cbCopy
 * wrote), the draw's VuCB and group 1, its b1, b3 offsets; and the box
 * check (S3): the command drawn again right there through its own key
 * with the colour writes and the Z writes off (the fixed-function mask:
 * the key without the destination read, so GS state as shader data cannot
 * put the mask in the shader), nothing written, inside an occlusion query
 * whose scissors cover the draw's scissor less the CPU's box of its
 * vertices widened by 2 GS pixels.  Only where the box is closed (every
 * vertex in front of the eye) and the pass is an ordinary one; its
 * pipeline is compiled in the background first (no hitch).
 *
 * The end (rd__vu_skin_end, the replay after its last pass): one pass on
 * the check's own RGBA8_UINT target, three probes of the command side by
 * side, each a column of 16 texels a vertex row (vu_common.hlsli
 * vu_probe_out through FrameCB's exact-viewport form: column 16 c, row v -
 * first): (0) the draw's VuCB with ICO_VU_PROBE, the draw's group 1 and
 * the SAME VuBoneCB offset, through the vertex module the draw's key took
 * (the program entry's, the plain set's where the fallback is plain); (c1)
 * the same with the CPU's bytes at a fresh ring offset of another 4 KB page
 * phase; (c2) where robust access is on and the device offers
 * VK_EXT_pipeline_robustness, the probe pipeline with uniform-buffer
 * robustness off.  Then the target and the ring's 3,840 bytes at the
 * draw's offset copied into the slot's readback buffer.  Through an index
 * list of fields 0..2 only (GS X, Y, Z): 9 indices a vertex.
 *
 * The harvest (rd__vu_skin_harvest, rd__wait_frame): once the slot's fence
 * has let the replay go (the replay FIF - 1 later, before it writes the
 * ring), the GPU's copy and the ring as the CPU reads it now are compared
 * with the bytes kept, each probe with the CPU's positions; a wrong vertex
 * is explained by the bone address whose zero matrix gives its position
 * (rd__skin_judge).  Lines on events only (the first 8, then every 100th),
 * the counts in the 10 s block.
 *
 * The twins (issue 123: the probe above is one draw now and then, drawn
 * after the picture through a pipeline of its own, and saw nothing in a
 * run whose pictures lost Yorda's shin): where they are on
 * (rd__vu_skin_twins_on: Turnip by default, mode 2 anywhere), EVERY
 * skinned draw of the replay (up to RD_SKIN_TWINS_MAX) is drawn twice more
 * right after itself, inside its own pass, with nothing written, each in
 * an occlusion query: A with the draw's own bone offset, B with the same
 * bytes copied at note time to a fresh ring offset of another 4 KB page
 * phase (the probe's c1 block).  On a draw that reads its attachment (GS
 * state as shader data, the exact blend: the Turnip reporter's profile)
 * both go through the draw's OWN pipeline with a DrawCB whose write mask
 * (WMASK) is cleared: the shader then writes back the pixel it read
 * (gs_math.hlsli gs_dst_channel), value for value in rasterization order;
 * elsewhere through the box check's pipeline (colour and Z writes off).
 * Only Z-write-off draws take the own pipeline (its Z write cannot be
 * turned off); none in a storage, DATE, stencil or MSAA pass.  The two
 * counts are equal unless the bone read depends on the offset: "twins
 * differ" names the command, both counts and page phases, and makes the
 * command the probe's target for the next 8 replays (the targeted probe,
 * whatever the clock and the schedule).  The first failure of the run is
 * captured once (rd_vu_skin_capture.c: a sidecar, and a frame dump asked
 * of the window). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_mesh.h"
#include "rd_replay.h"
#include "rd_vu_skin_check.h"
#include "shader_consts.h"

bool g_rdSkinArm;
bool g_rdSkinTwin;

/* the scheduled probe's interval (skInterval): 100 ms, 0 (one a replay)
 * on Turnip, ICO_RD_VU_SKIN_INTERVAL_MS over both ("off": the targeted
 * probes alone) */
#define SK_INTERVAL_MS 100.0
/* a check's probe columns: the draw's block, c1, c2, and column 3 for the
 * ring-read module (rd__vu_skin_ring_module; drawn only where it exists) */
#define SK_COLS 4u
#define SK_TWINS_MAX RD_SKIN_TWINS_MAX
#define SK_TWIN_Q0 RD_SKIN_CHECKS_ALL /* twin t's queries: SK_TWIN_Q0 + 3 t + 0, 1, 2 */
#define SK_QUERIES RD_SKIN_QUERIES
#define SK_TARGET_REPLAYS 8u /* the targeted probe after a twin event */
/* mode 2: the checks one replay holds (16 x 3 x 80 texels wide); check i's
 * box query is occlusion query i */
#define SK_CHECKS_ALL RD_SKIN_CHECKS_ALL
#define SK_INDEX_VERTS 8192u
#define SK_BONE_BYTES ((uint32_t)sizeof(IcoVuBoneCB))
#define SK_VS_COUNT 3u /* RD_VS_VU_SKIN, _SKIN_SPEC, _SKIN_DEBUG */

/* --------------------------------------------------------- the switch */

static int s_set = -1;     /* rd__vu_skin_check_set */
static int s_env = -2;     /* ICO_RD_VU_SKIN_CHECK (rd__skin_check_env); -2 not read */
static int s_dev = -1;     /* the device's default (an Adreno with a window): -1 not read */
static uint32_t s_allFrom; /* mode 2: the first skinned command of the replay checked */

/* the mode, read once a run (skMode runs for every skinned command) */
static int skMode(void)
{
    if (s_set >= 0) {
        return s_set;
    }
    if (s_env == -2) {
        s_env = rd__skin_check_env(getenv("ICO_RD_VU_SKIN_CHECK"));
    }
    if (s_dev < 0) {
        s_dev = rd__skin_check_default(g_rd.windowed, rd__profile_adreno(), -1);
    }
    return s_env >= 0 ? s_env : s_dev;
}

void rd__vu_skin_check_set(int mode)
{
    s_set = mode < 0 ? -1 : (mode > 2 ? 2 : mode);
}

const char *rd__vu_skin_check_why(void)
{
    const int m = skMode();
    if (s_set >= 0) {
        return m ? "on by the tests' or the tool's switch"
                 : "off by the tests' or the tool's switch";
    }
    if (s_env == 0) {
        return "[dev] vu_skin_check = false (ICO_RD_VU_SKIN_CHECK=0)";
    }
    if (s_env == 2) {
        return "ICO_RD_VU_SKIN_CHECK=2: every skinned command of every replay";
    }
    if (s_env == 1) {
        return "[dev] vu_skin_check = true (ICO_RD_VU_SKIN_CHECK=1)";
    }
    if (m) {
        return rd__driver_is_turnip(rhi_limits())
                   ? "on by default on an Adreno with a window (Turnip); [dev] vu_skin_check = "
                     "false turns it off"
                   : "on by default on an Adreno with a window (Qualcomm's driver); [dev] "
                     "vu_skin_check = false turns it off";
    }
    return g_rd.windowed ? "not an Adreno: ICO_RD_VU_SKIN_CHECK=1 / [dev] vu_skin_check = true "
                           "turns it on"
                         : "off without a window: ICO_RD_VU_SKIN_CHECK=1 turns it on";
}

bool rd__vu_skin_check_on(void)
{
    return g_rd.inited && g_rd.hasDevice && rhi_backend() == RHI_BACKEND_VULKAN && skMode() != 0 &&
           !rhi_device_lost();
}

static int s_twinSet = -1; /* rd__vu_skin_twins_set */
static int s_turnip = -1;  /* the device is Turnip: -1 not read */

static bool onTurnip(void)
{
    if (s_turnip < 0) {
        s_turnip = rd__driver_is_turnip(rhi_limits()) ? 1 : 0;
    }
    return s_turnip == 1;
}

void rd__vu_skin_twins_set(int on)
{
    s_twinSet = on < 0 ? -1 : on ? 1 : 0;
}

bool rd__vu_skin_twins_on(void)
{
    const int m = skMode();
    if (m == 0) {
        return false;
    }
    if (s_twinSet >= 0) {
        return s_twinSet == 1;
    }
    return m == 2 || onTurnip();
}

uint32_t rd__vu_skin_queries(void)
{
    return rd__vu_skin_twins_on() ? SK_QUERIES : RD_SKIN_CHECKS_ALL;
}

RhiPipeline rd__vu_skin_ring_module(uint8_t vs)
{
    /* B2's ring-read module is not built: column 3 is not drawn */
    (void)vs;
    return (RhiPipeline){0};
}

/* the scheduled probe's interval in ms: -1 none (the targeted alone) */
static double s_interval = -2.0;

static double skInterval(void)
{
    if (s_interval > -2.0) {
        return s_interval;
    }
    const char *e = getenv("ICO_RD_VU_SKIN_INTERVAL_MS");
    if (e && e[0]) {
        s_interval = strcmp(e, "off") == 0 ? -1.0 : strtod(e, NULL);
        s_interval = s_interval < 0.0 ? -1.0 : s_interval;
    } else {
        s_interval = onTurnip() ? 0.0 : SK_INTERVAL_MS;
    }
    return s_interval;
}

/* ICO_RD_VU_SKIN_SABOTAGE=A[,ring] (tests): the bone at VU address A read
 * as zeros by the check's own probe (the draw's bytes right: a wrong
 * uniform read, H1's form), or with ",ring" zeroed in the ring after the
 * bytes were kept (the draw and the probe both read zeros: H2's form) */
static struct {
    bool read;
    uint32_t addr;
    bool ring;
    uint32_t fill; /* the ring's bone filled with: zeros, or ",nan" a NaN */
} s_sab;

static void sabRead(void)
{
    if (s_sab.read) {
        return;
    }
    s_sab.read = true;
    const char *e = getenv("ICO_RD_VU_SKIN_SABOTAGE");
    if (!e || !e[0]) {
        return;
    }
    const unsigned long a = strtoul(e, NULL, 10);
    if (a >= 16u && a <= 252u && (a & 3u) == 0) {
        s_sab.addr = (uint32_t)a;
        /* ",nan": the ring's bone made NaN after the bytes were kept (the
         * v0.10.4 picture's form: every triangle on it dropped) */
        s_sab.fill = strstr(e, "nan") != NULL ? 0x7FC00000u : 0u;
        s_sab.ring = strstr(e, "ring") != NULL || s_sab.fill != 0u;
        rd__log("VU skin check: ICO_RD_VU_SKIN_SABOTAGE: the bone at address %u read as %s %s",
                s_sab.addr, s_sab.fill ? "NaN" : "zeros",
                s_sab.ring ? "in the ring after the draw's bytes were kept (the draw, its twin A "
                             "and the probe)"
                           : "by the check's probe of the draw's block alone");
    }
}

/* ------------------------------------------------------- the pipelines */

static struct {
    RhiShader ps; /* vu_probe_ps */
    bool psTried;
    RhiPipeline probe[SK_VS_COUNT];
    bool probeTried[SK_VS_COUNT];
    /* c2: uniform robustness off, compiled in the background */
    RhiPipelineDesc roDesc[SK_VS_COUNT];
    RhiPipeline ro[SK_VS_COUNT];
    RhiPipelineJobs *roJob;
    bool roBegun, roDone;
    RhiBindGroupLayout lay[3];
    RhiBuffer idx; /* fields 0..2 of SK_INDEX_VERTS vertices: 9 indices a vertex */
    bool idxTried;
} s_p;

static uint8_t vsOf(uint32_t i)
{
    return (uint8_t)(RD_VS_VU_SKIN + i);
}

/* the module a program entry's key draws through (rd_pipeline.c
 * pipelineDesc: the fallback's, the plain set's on a device of the table) */
static RhiShader moduleOf(uint8_t vs)
{
    return g_rd.vsFallback[vs].id ? g_rd.vsFallback[vs] : g_rd.vs[vs];
}

static bool probeDesc(uint32_t i, bool robustOff, RhiPipelineDesc *d)
{
    if (!s_p.psTried) {
        s_p.psTried = true;
        s_p.ps = rd__make_shader("vu_probe_ps");
    }
    memset(d, 0, sizeof(*d));
    d->vertex = moduleOf(vsOf(i));
    d->fragment = s_p.ps;
    if (!d->vertex.id || !d->fragment.id) {
        return false;
    }
    s_p.lay[0] = g_rd.layoutFrame;
    s_p.lay[1] = g_rd.layoutVu;
    s_p.lay[2] = g_rd.layoutTex;
    d->layouts = s_p.lay;
    d->layoutCount = 3;
    d->topology = RHI_TOPO_TRIANGLE_LIST;
    d->cullNone = true;
    d->blend[0] = (RhiBlendState){.writeMask = 0xF};
    d->colorFormats[0] = RHI_FMT_RGBA8_UINT;
    d->colorCount = 1;
    d->depthFormat = RHI_FMT_UNKNOWN;
    d->debugName = robustOff ? "rd vu skin probe (uniform robustness off)" : "rd vu skin probe";
    d->uniformRobustOff = robustOff;
    return true;
}

/* the three probe pipelines at once (the start-up probe; the check's first
 * use where that did not run): false when one could not be made */
static bool probesMake(void)
{
    RhiPipelineDesc d[SK_VS_COUNT];
    RhiPipeline out[SK_VS_COUNT];
    uint32_t want[SK_VS_COUNT], n = 0;
    for (uint32_t i = 0; i < SK_VS_COUNT; i++) {
        if (s_p.probeTried[i]) {
            continue;
        }
        s_p.probeTried[i] = true;
        if (probeDesc(i, false, &d[n])) {
            want[n++] = i;
        }
    }
    if (n) {
        memset(out, 0, sizeof(out));
        (void)rhi_create_pipelines(d, n, out, NULL);
        for (uint32_t k = 0; k < n; k++) {
            s_p.probe[want[k]] = out[k];
        }
    }
    for (uint32_t i = 0; i < SK_VS_COUNT; i++) {
        if (!s_p.probe[i].id) {
            return false;
        }
    }
    return true;
}

/* the counts: the run's and the 10 s block's */
static struct {
    uint64_t checks, wrong, blocks, reread, box, boxOut;
    /* the lines' own budgets (the first 8, then every 100th, each): a
     * wrong check's (its mismatch and variants lines), a check whose bytes
     * differed with every vertex right, a box check's samples outside, so
     * the many blocks of an H2 run never use up a wrong check's lines */
    uint64_t wrongEvents, bytesEvents, boxEvents;
    uint64_t drawn, boxDrawn; /* the checks recorded (probed) and their box checks drawn */
    /* the twins: drawn, of them through the draw's own pipeline (its
     * attachment read), read, whose counts differed; their events' line
     * budget; the checks targeted after a twin event */
    uint64_t twinsDrawn, twinsOwn, twins, twinsDiffer, twinEvents, targeted;
} s_run;

static struct {
    uint64_t checks, wrong, blocks, reread, box, boxOut, c1Right, c2Right, c2Run;
    /* box checks not drawn: their pipeline not made (the background
     * compile not begun or not done: one job at a time, the memory rule),
     * of them with no slot left for the key */
    uint64_t boxNot, boxFull;
    int32_t maxRight;
    /* the twins read, whose counts differed (the first at frame
     * firstDiffer), not drawn (their pipeline not made yet) */
    uint64_t twins, twinsDiffer, twinsNot;
    uint32_t firstDiffer;
    bool anyDiffer;
} s_blk;

/* the box check's pipelines: the draw's key without its writes, compiled
 * in the background */
#define SK_BOX_PIPES RD_VU_SKIN_BOX_PIPES

typedef struct SkBoxPipe {
    bool used, failed;
    RdPipeKeyInt key; /* rd__pipeline_key_norm's: the pass's sample count in it */
    RhiPipelineDesc desc;
    RhiBindGroupLayout lay[4];
    RhiPipelineJobs *job;
    RhiPipeline pipe;
} SkBoxPipe;

static SkBoxPipe s_box[SK_BOX_PIPES];

/* ----------------------------------------- the background compiles' rule
 * Issue 161's memory rule for the check's own compiles (c2's three, the
 * box check's): one job at a time; each begun only when the window's
 * memory function (rd.h rd_vu_skin_background: diag_host.h
 * ico_diag_background_memory, as the background finisher asks it) says
 * neither the system's low-memory signal (2: none for the run) nor the
 * free-memory floor now (1: asked again at the next check), and none at
 * all where the start-up set stopped for memory on Qualcomm's own driver
 * (window_pipe_finish.c: that driver's compiler ran the device out of
 * memory).  Until a pipeline exists its part is not drawn (the box check,
 * c2), said once. */
static RdBackgroundMemoryFn s_memFn;
static char s_noBg[192]; /* why none is compiled in the background: "" allowed */
static bool s_memStop;   /* the system's signal: none for the run */
static bool s_bgSaid[3]; /* the lines said: the rule, the signal, the floor */

void rd_vu_skin_background(RdBackgroundMemoryFn memory, const char *noBackground)
{
    s_memFn = memory;
    snprintf(s_noBg, sizeof(s_noBg), "%s", noBackground ? noBackground : "");
}

static bool jobBusy(void)
{
    if (s_p.roJob) {
        return true;
    }
    for (int i = 0; i < SK_BOX_PIPES; i++) {
        if (s_box[i].job) {
            return true;
        }
    }
    return false;
}

/* whether a background compile may begin now (wait: the tool's and the
 * tests', taken at once: always) */
static bool bgMay(bool wait)
{
    if (wait) {
        return true;
    }
    if (jobBusy()) {
        return false;
    }
    if (s_noBg[0]) {
        if (!s_bgSaid[0]) {
            s_bgSaid[0] = true;
            rd__log("VU skin check: its box check and its c2 variant are not compiled in the "
                    "background (%s): the check runs without them",
                    s_noBg);
        }
        return false;
    }
    if (s_memStop) {
        return false;
    }
    if (s_memFn) {
        char why[128] = "";
        const int m = s_memFn(why, sizeof(why));
        if (m >= 2) {
            s_memStop = true;
            if (!s_bgSaid[1]) {
                s_bgSaid[1] = true;
                rd__log("VU skin check: no background compile for the rest of the run (%s): the "
                        "box checks and the c2 variant not made yet are not drawn",
                        why);
            }
            return false;
        }
        if (m == 1) {
            if (!s_bgSaid[2]) {
                s_bgSaid[2] = true;
                rd__log("VU skin check: a background compile waits for memory (%s): the box "
                        "check or the c2 variant not made yet is not drawn until then",
                        why);
            }
            return false;
        }
    }
    return true;
}

/* c2's pipelines: begun in the background once (bgMay); wait: the tool's
 * and the tests' (taken now) */
static void roPoll(bool wait)
{
    if (!rhi_limits()->uniformRobustOff || s_p.roDone) {
        return;
    }
    if (!s_p.roBegun) {
        if (!bgMay(wait)) {
            return;
        }
        s_p.roBegun = true;
        uint32_t n = 0;
        for (uint32_t i = 0; i < SK_VS_COUNT; i++) {
            if (!probeDesc(i, true, &s_p.roDesc[i])) {
                s_p.roDone = true;
                return;
            }
            n++;
        }
        s_p.roJob = rhi_create_pipelines_begin(s_p.roDesc, n, 1, true);
        if (!s_p.roJob) {
            s_p.roDone = true;
            return;
        }
    }
    if (s_p.roJob && rhi_create_pipelines_end(s_p.roJob, wait, s_p.ro, NULL, NULL)) {
        s_p.roJob = NULL;
        s_p.roDone = true;
    }
}

static bool indexMake(void)
{
    if (s_p.idxTried) {
        return s_p.idx.id != 0;
    }
    s_p.idxTried = true;
    const uint64_t bytes = (uint64_t)SK_INDEX_VERTS * 9u * 4u;
    s_p.idx = rhi_create_buffer(
        &(RhiBufferDesc){bytes, RHI_BUF_INDEX, RHI_MEM_UPLOAD, "rd vu skin probe index"});
    uint32_t *m = s_p.idx.id ? rhi_map_buffer(s_p.idx) : NULL;
    if (!m) {
        if (s_p.idx.id) {
            rhi_destroy_buffer(s_p.idx);
        }
        s_p.idx = (RhiBuffer){0};
        return false;
    }
    /* vertex v's fields 0..2 (GS X, Y, Z): SV_VertexID (v * 16 + f) * 3 + c
     * (vu_common.hlsli vu_probe_vertex) */
    for (uint32_t v = 0; v < SK_INDEX_VERTS; v++) {
        for (uint32_t f = 0; f < 3u; f++) {
            for (uint32_t c = 0; c < 3u; c++) {
                m[(v * 3u + f) * 3u + c] = (v * ICO_VU_PROBE_FIELDS + f) * 3u + c;
            }
        }
    }
    rhi_unmap_buffer(s_p.idx);
    return true;
}

RhiPipeline rd__vu_skin_probe_pipeline(uint8_t vs, bool robustOff)
{
    if (vs < RD_VS_VU_SKIN || vs >= RD_VS_VU_SKIN + SK_VS_COUNT) {
        return (RhiPipeline){0};
    }
    const uint32_t i = (uint32_t)(vs - RD_VS_VU_SKIN);
    if (robustOff) {
        roPoll(skMode() == 2);
        return s_p.ro[i];
    }
    if (!s_p.probeTried[i]) {
        (void)probesMake();
    }
    return s_p.probe[i];
}

RhiBuffer rd__vu_skin_index_buffer(void)
{
    (void)indexMake();
    return s_p.idx;
}

static bool keyEq(const RdPipeKeyInt *a, const RdPipeKeyInt *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

/* the box check's pipeline of key k: begun in the background the first
 * time, {0} until it is made (wait: taken now) */
static RhiPipeline boxPipe(const RdPipeKeyInt *raw, bool wait)
{
    /* the key as the pass makes it: a key checked first in an MSAA pass
     * and again in a single-sampled one (or the other way) is two
     * pipelines, as rd__get_pipeline's cache holds it (review2 S8: a
     * multisampled pipeline bound in a single-sampled pass) */
    RdPipeKeyInt norm;
    rd__pipeline_key_norm(raw, &norm);
    const RdPipeKeyInt *k = &norm;
    SkBoxPipe *e = NULL;
    for (int i = 0; i < SK_BOX_PIPES && !e; i++) {
        if (s_box[i].used && keyEq(&s_box[i].key, k)) {
            e = &s_box[i];
        }
    }
    if (!e) {
        for (int i = 0; i < SK_BOX_PIPES && !e; i++) {
            if (!s_box[i].used) {
                e = &s_box[i];
            }
        }
        if (!e) {
            s_blk.boxFull++; /* full: the keys after the first 16 go unchecked */
            return (RhiPipeline){0};
        }
        if (!bgMay(wait)) {
            return (RhiPipeline){0}; /* another job in flight, or the memory rule */
        }
        memset(e, 0, sizeof(*e));
        e->used = true;
        e->key = *k;
        if (!rd__pipeline_desc(raw, &e->desc, e->lay)) { /* drawKey(raw): norm's */
            e->failed = true;
            return (RhiPipeline){0};
        }
        /* the description's layouts are this entry's (pipelineDesc points
         * them at its own array: copied into e->lay above) */
        e->desc.layouts = e->lay;
        e->desc.debugName = "rd vu skin box check";
        e->job = rhi_create_pipelines_begin(&e->desc, 1, 1, true);
        if (!e->job) {
            e->pipe = rhi_create_pipeline(&e->desc);
            e->failed = !e->pipe.id;
        }
    }
    if (e->job && rhi_create_pipelines_end(e->job, wait, &e->pipe, NULL, NULL)) {
        e->job = NULL;
        e->failed = !e->pipe.id;
    }
    return e->pipe;
}

/* the job in flight taken when done (each check: one job at a time, so
 * the next key's may begin) */
static void jobsPoll(void)
{
    for (int i = 0; i < SK_BOX_PIPES; i++) {
        SkBoxPipe *e = &s_box[i];
        if (e->job && rhi_create_pipelines_end(e->job, false, &e->pipe, NULL, NULL)) {
            e->job = NULL;
            e->failed = !e->pipe.id;
        }
    }
}

/* ------------------------------------------------------------ the checks */

typedef struct SkCheck {
    /* the command */
    uint32_t frame, list, index, meshId, meshVertices;
    char mesh[24];
    int code;
    uint8_t vs;
    uint32_t first, n; /* the probed vertices of the stream */
    /* the CPU's side */
    float mem[36][4];
    float bones[240][4]; /* the ring's bytes at the draw's offset when it was recorded */
    float (*stream)[4];  /* n * 5 qwords */
    uint32_t streamCap;
    RdSkinBatch *batches;
    uint32_t batchCount, batchCap;
    RdSkinPos *ref; /* RD_SKIN_MAXV */
    /* the draw */
    IcoVuCB vcb;
    RhiBindGroup g1;
    uint64_t dOff, bonesOff, c1Off, sabOff;
    bool c2;       /* c2 drawn */
    bool boxRun;   /* its box check drawn */
    bool boxNoDst; /* ... without the draw's destination read (its _dstatt link) */
    uint8_t boxFs; /* the key's fragment entry (rd__fs_name) */
    RhiRect box;   /* the box in texels */
    bool c3;       /* column 3 (the ring-read module) drawn */
    bool targeted; /* picked as the target of a twin event */
    /* the harvest's verdict, for the twins of the same draw: read, wrong
     * vertices, the address named, of them dropped (non-finite) */
    bool judged;
    uint32_t probeWrong, probeAddr, probeDropped;
} SkCheck;

/* a twinned draw (rd__vu_skin_note): its command, the two bone offsets,
 * the fragment entry the twins went through, whether that was the draw's
 * own pipeline (its attachment read), C drawn (the ring read, B2), and
 * the slot's check of the same draw (-1: none) */
typedef struct SkTwin {
    uint32_t frame, list, index, meshId, meshVertices;
    char mesh[24];
    uint64_t bonesOff, freshOff;
    uint8_t fs;
    bool own, c;
    int32_t check;
} SkTwin;

typedef struct SkSlot {
    bool pending; /* recorded, not read yet */
    uint32_t counter, rhiSlot, ringId;
    uint32_t n, cap;
    SkCheck *checks;
    RhiTexture tex;
    RhiState texState;
    uint32_t texW, texH;
    RhiBuffer rb;
    uint64_t rbSize, rowsOff;
    uint32_t pitch;
    SkTwin twins[SK_TWINS_MAX];
    uint32_t nTwins;
} SkSlot;

static SkSlot s_slots[RHI_FRAMES_IN_FLIGHT];

/* the pick's state: the vertex-weighted schedule (the vertices of the
 * replay's skinned commands seen so far, the last replay's total, the
 * cursor advanced by the golden ratio of the total after each probe) */
static SkCheck *s_cur;
static uint32_t s_seenReplay, s_seen;
static uint64_t s_seenV, s_lastV, s_cursorV;
static bool s_pickedThis;
static double s_lastMs;
/* the command g_rdSkinTwin was set for (rd__vu_skin_pick) */
static SkTwin s_twinCmd;

/* the targeted probe: the mesh and list of the last twin event's command
 * for the replays left */
static struct {
    uint32_t meshId, list, left;
} s_target;

/* the capture, once a run: done; wanted after a twin event whose command
 * was not probed (taken at the targeted check's harvest, or with the
 * twin's data alone after the deadline replay) */
static struct {
    bool done, want;
    uint32_t deadline;
    SkTwin twin;
    uint64_t counts[3];
} s_cap;

static RdSkinSummary *s_collect; /* rd__vu_skin_check_frame's */
static bool s_boxDstSaid;        /* the box check's attachment-read line, once a run */

static bool sayNth(uint64_t n)
{
    return n <= 8u || n % 100u == 0u;
}

/* the check's batches and buffers for n vertices and nb batches */
static bool checkReserve(SkCheck *k, uint32_t n, uint32_t nb)
{
    if (!k->ref) {
        k->ref = malloc(sizeof(RdSkinPos) * RD_SKIN_MAXV);
    }
    if (k->streamCap < n) {
        free(k->stream);
        k->stream = malloc(sizeof(float[4]) * 5u * n);
        k->streamCap = k->stream ? n : 0;
    }
    if (k->batchCap < nb) {
        free(k->batches);
        k->batches = malloc(sizeof(RdSkinBatch) * nb);
        k->batchCap = k->batches ? nb : 0;
    }
    return k->ref && k->stream && k->batches;
}

static void checkFree(SkCheck *k)
{
    free(k->stream);
    free(k->batches);
    free(k->ref);
    memset(k, 0, sizeof(*k));
}

static RdSkinCase caseOf(const SkCheck *k)
{
    RdSkinCase c;
    c.code = k->code;
    c.mem = (const float (*)[4])k->mem;
    c.bones = (const float (*)[4])k->bones;
    c.stream = (const float (*)[4])k->stream;
    c.qpv = 5;
    c.batches = k->batches;
    c.batchCount = k->batchCount;
    c.count = k->n;
    return c;
}

/* the command's list and index in f */
static void cmdPlace(const RdFrame *f, const RdCmd *c, uint32_t *list, uint32_t *index)
{
    *list = *index = UINT32_MAX;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        if (cl->count && c >= cl->cmds && c < cl->cmds + cl->count) {
            *list = (uint32_t)l;
            *index = (uint32_t)(c - cl->cmds);
            return;
        }
    }
}

/* the CPU's side of command c (its mesh m) into k: false when the command
 * cannot be checked (not a cluster program, a stream of another layout, a
 * range past the index list) */
static bool checkBuild(SkCheck *k, const RdFrame *f, const RdCmd *c, const RdMeshRec *m)
{
    RdVuPayload p;
    if (!f->payload || (uint64_t)c->u[1] + c->u[2] > f->payloadSize ||
        c->u[2] < sizeof(p) + sizeof(RdVuBlock)) {
        return false;
    }
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    if ((p.prog != RD_PROG_SKIN && p.prog != RD_PROG_SKIN_SPEC) || m->qwPerVertex != 5u ||
        !m->stream || !m->batches || p.firstBatch >= m->batchCount) {
        return false;
    }
    uint32_t last = p.firstBatch + p.batchCount;
    if (last > m->batchCount || p.batchCount == 0) {
        last = m->batchCount;
    }
    const uint32_t first = m->batches[p.firstBatch].firstVertex;
    uint32_t n = 0, nb = 0;
    for (uint32_t b = p.firstBatch; b < last; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        if (br->firstVertex < first || br->firstVertex - first + br->vertexCount > RD_SKIN_MAXV ||
            (uint64_t)br->firstVertex + br->vertexCount > m->vertexCount) {
            break; /* the rest past the probe's rows: the draw's first batches are checked */
        }
        const uint32_t end = br->firstVertex - first + br->vertexCount;
        n = end > n ? end : n;
        nb++;
    }
    if (n == 0 || first + n > SK_INDEX_VERTS || !checkReserve(k, n, nb)) {
        return false;
    }
    k->frame = f->number;
    cmdPlace(f, c, &k->list, &k->index);
    k->meshId = c->u[0];
    k->meshVertices = m->vertexCount;
    snprintf(k->mesh, sizeof(k->mesh), "%s", m->name[0] ? m->name : "?");
    k->code = p.code;
    k->vs = p.prog == RD_PROG_SKIN ? RD_VS_VU_SKIN
            : p.code == 24         ? RD_VS_VU_SKIN_DEBUG
                                   : RD_VS_VU_SKIN_SPEC;
    k->first = first;
    k->n = n;
    memcpy(k->mem, f->payload + c->u[1] + sizeof(p), sizeof(k->mem));
    memcpy(k->stream, m->stream[(size_t)first * 5u], (size_t)n * 5u * 16u);
    k->batchCount = nb;
    for (uint32_t b = 0; b < nb; b++) {
        const RdVuBatchRec *br = &m->batches[p.firstBatch + b];
        k->batches[b].first = br->firstVertex - first;
        k->batches[b].count = br->vertexCount;
    }
    k->c2 = k->boxRun = k->c3 = k->targeted = k->judged = false;
    k->probeWrong = k->probeAddr = k->probeDropped = 0;
    return true;
}

void rd__vu_skin_pick(const RdFrame *f, const RdCmd *c, const RdMeshRec *m)
{
    g_rdSkinArm = false;
    g_rdSkinTwin = false;
    s_cur = NULL;
    if (!f || !c || !m || !rd__vu_skin_check_on()) {
        return;
    }
    if (s_seenReplay != g_rd.replayCounter) {
        if (s_seenReplay != 0) {
            s_lastV = s_seenV;
        }
        s_seen = 0;
        s_seenV = 0;
        s_seenReplay = g_rd.replayCounter;
        s_pickedThis = false;
    }
    const uint32_t at = s_seen++;
    const uint64_t vBefore = s_seenV;
    s_seenV += m->vertexCount;
    SkSlot *sl = &s_slots[rd__ring_slot()];
    if (sl->pending || g_rdWin.first || rd__pipeline_gathering() || rd__vu_prepass_running()) {
        return; /* the slot's last check not read yet; the bisect; a naming replay */
    }
    uint32_t list, index;
    cmdPlace(f, c, &list, &index);
    /* the twins: every skinned draw of the replay (vuDraw draws it on its
     * own, out of the draw merge, and rd__vu_skin_note twins it) */
    if (rd__vu_skin_twins_on() && sl->nTwins < SK_TWINS_MAX && rhi_occlusion_supported()) {
        memset(&s_twinCmd, 0, sizeof(s_twinCmd));
        s_twinCmd.frame = f->number;
        s_twinCmd.list = list;
        s_twinCmd.index = index;
        s_twinCmd.meshId = c->u[0];
        s_twinCmd.meshVertices = m->vertexCount;
        snprintf(s_twinCmd.mesh, sizeof(s_twinCmd.mesh), "%s", m->name[0] ? m->name : "?");
        g_rdSkinTwin = true;
    }
    bool targeted = false;
    if (skMode() == 2) {
        if (at < s_allFrom || sl->n >= SK_CHECKS_ALL) {
            return;
        }
    } else {
        if (s_pickedThis || sl->n != 0) {
            return;
        }
        if (s_target.left) {
            /* a twin event's command, whatever the clock and the schedule;
             * nothing else while it is the target */
            if (c->u[0] != s_target.meshId || list != s_target.list) {
                return;
            }
            targeted = true;
        } else {
            const double iv = skInterval();
            if (iv < 0.0 || (s_lastMs != 0.0 && iv > 0.0 && rd__now_ms() - s_lastMs < iv)) {
                return;
            }
            /* vertex-weighted: the command whose vertices hold the cursor
             * (of the last replay's total), so a big mesh is probed as
             * often as its share of the skinned vertices */
            if (s_lastV != 0 && vBefore + m->vertexCount <= s_cursorV % s_lastV) {
                return;
            }
        }
    }
    if (sl->cap <= sl->n) {
        const uint32_t cap = skMode() == 2 ? SK_CHECKS_ALL : 1u;
        if (cap <= sl->n) {
            return;
        }
        SkCheck *nc = realloc(sl->checks, sizeof(SkCheck) * cap);
        if (!nc) {
            return;
        }
        memset(nc + sl->cap, 0, sizeof(SkCheck) * (cap - sl->cap));
        sl->checks = nc;
        sl->cap = cap;
    }
    SkCheck *k = &sl->checks[sl->n];
    if (!checkBuild(k, f, c, m) || !rd__vu_skin_probe_pipeline(k->vs, false).id) {
        return; /* not a check this probe can make */
    }
    k->targeted = targeted;
    s_cur = k;
    g_rdSkinArm = true;
}

/* GS 12.4 X into the target's texel column (rd_vuprepass_selftest.c
 * blockRect's mapping), the window's origin ox */
static double texelX(const RdTargetRec *t, double gx)
{
    return t->ox > 0.0f ? gx * t->sx - t->ox : gx * t->tw / t->w;
}

static double texelY(const RdTargetRec *t, double gy)
{
    return gy * t->th / t->h;
}

static RhiRect clipRect(int64_t x0, int64_t y0, int64_t x1, int64_t y1, const RhiRect *sc)
{
    const int64_t sx0 = sc->x, sy0 = sc->y, sx1 = sc->x + (int64_t)sc->w,
                  sy1 = sc->y + (int64_t)sc->h;
    x0 = x0 < sx0 ? sx0 : x0;
    y0 = y0 < sy0 ? sy0 : y0;
    x1 = x1 > sx1 ? sx1 : x1;
    y1 = y1 > sy1 ? sy1 : y1;
    if (x1 <= x0 || y1 <= y0) {
        return (RhiRect){0, 0, 0, 0};
    }
    return (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

/* a block into the ring at an offset of ua (~0ull: full) */
static uint64_t ringPut(const void *data, uint64_t size, uint64_t align)
{
    const uint64_t off = rd__ring_alloc(size, align);
    if (off != ~0ull) {
        memcpy(g_rd.ringMap[rd__ring_slot()] + off, data, size);
    }
    return off;
}

/* twin B's block and the probe's c1: the bytes at a fresh ring offset
 * whose 4 KB page phase is not the draw's (the ring is host-coherent and
 * append-only within a replay: rd_replay.c rd__ring_alloc) */
static uint64_t ringFresh(const void *bytes, uint64_t drawOff, uint64_t ua)
{
    const uint64_t base = rd__ring_alloc(SK_BONE_BYTES + 4096u + ua, ua);
    if (base == ~0ull) {
        return ~0ull;
    }
    /* half a page on where the alignment allows, else the next aligned
     * offset: never the draw's phase */
    uint64_t off = base + (ua <= 2048u ? 2048u / ua * ua : 0u);
    if (((off ^ drawOff) & 4095u) == 0u) {
        off += ua;
    }
    memcpy(g_rd.ringMap[rd__ring_slot()] + off, bytes, SK_BONE_BYTES);
    return off;
}

/* S3, inside the draw's pass right after it */
static void boxCheck(Replay *r, const DrawSetup *ds, const RdSkinDraw *d, SkCheck *k, uint32_t at)
{
    const RdTargetRec *tc = ds->tc;
    if (!rhi_occlusion_supported() || !r->passOpen || r->passStorage || r->dateStNow || !tc ||
        !tc->w || !tc->h || d->key->gs.stencil != 0 || d->key->gs.date != RD_DATE_OFF ||
        at >= SK_CHECKS_ALL) {
        return;
    }
    /* the box over the CPU's positions: closed only when every vertex of
     * the draw's batches is in front of the eye (a clipped triangle's
     * corner behind it reaches anywhere) */
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (uint32_t i = 0; i < k->n; i++) {
        const RdSkinPos *p = &k->ref[i];
        if (!p->inBatch) {
            continue;
        }
        if (!p->compared) {
            return;
        }
        x0 = p->x < x0 ? p->x : x0;
        y0 = p->y < y0 ? p->y : y0;
        x1 = p->x > x1 ? p->x : x1;
        y1 = p->y > y1 ? p->y : y1;
    }
    if (x0 > x1) {
        return;
    }
    /* GS window pixels (the frame's XYOFFSET, rd_replay.c frameRefresh),
     * widened by 2 GS pixels, rounded outward to texels */
    const double ox = 2048.0 - (double)(r->st.gsW >> 1), oy = 2048.0 - (double)(r->st.gsH >> 1);
    const double gx0 = x0 / 16.0 - ox - 2.0, gx1 = x1 / 16.0 - ox + 2.0;
    const double gy0 = y0 / 16.0 - oy - 2.0, gy1 = y1 / 16.0 - oy + 2.0;
    const int64_t bx0 = (int64_t)floor(texelX(tc, gx0)), bx1 = (int64_t)ceil(texelX(tc, gx1));
    const int64_t by0 = (int64_t)floor(texelY(tc, gy0)), by1 = (int64_t)ceil(texelY(tc, gy1));
    RhiRect sc = r->sc;
    const RhiRect all = {0, 0, tc->tw, tc->th};
    sc = clipRect(sc.x, sc.y, (int64_t)sc.x + sc.w, (int64_t)sc.y + sc.h, &all);
    const int64_t sx0 = sc.x, sy0 = sc.y, sx1 = sc.x + (int64_t)sc.w, sy1 = sc.y + (int64_t)sc.h;
    const RhiRect band[4] = {
        clipRect(sx0, sy0, bx0, sy1, &sc), /* left of the box */
        clipRect(bx1, sy0, sx1, sy1, &sc), /* right */
        clipRect(bx0, sy0, bx1, by0, &sc), /* above */
        clipRect(bx0, by1, bx1, sy1, &sc), /* below */
    };
    /* the draw's own key, nothing written: no destination read (the mask in
     * the fixed function, not in GS state as shader data), colour mask 0,
     * Z write off */
    RdPipeKeyInt key = *d->key;
    key.dst = RHI_DST_READ_NONE;
    key.gs.colorMask = 0;
    key.gs.zwrite = RD_ZWRITE_OFF;
    const RhiPipeline p = boxPipe(&key, skMode() == 2);
    if (!p.id) {
        s_blk.boxNot++;
        return;
    }
    const RhiCommandList cl = rd__replay_cl();
    rd__set_pipeline(cl, p, &key);
    rd__bind_uniform(cl, 0, r->frameBG);
    const uint32_t offs[3] = {(uint32_t)d->dOff, (uint32_t)d->vOff, (uint32_t)d->bonesOff};
    rhi_cmd_set_bind_group_offsets(cl, 1, d->g1, offs, 3);
    rhi_cmd_set_bind_group(cl, 2, d->g2);
    if (d->indexBuf.id) {
        rhi_cmd_set_index_buffer(cl, d->indexBuf, d->indexOff, true);
    }
    rhi_cmd_begin_occlusion(cl, at);
    for (int i = 0; i < 4; i++) {
        if (!band[i].w || !band[i].h) {
            continue;
        }
        rhi_cmd_set_scissor(cl, &band[i]);
        if (d->indexBuf.id) {
            rhi_cmd_draw_indexed(cl, d->count, d->first, 0, 1);
        } else {
            rhi_cmd_draw(cl, d->count, d->first, 1);
        }
    }
    rhi_cmd_end_occlusion(cl, at);
    /* the draw's state back for whatever the replay records next */
    rhi_cmd_set_scissor(cl, &r->sc);
    rd__set_pipeline(cl, d->pipe, d->key);
    k->boxRun = true;
    k->boxNoDst = d->key->dst != RHI_DST_READ_NONE;
    k->boxFs = d->key->fs;
    s_run.boxDrawn++;
    if (k->boxNoDst && !s_boxDstSaid) {
        /* the deep dive's last row (the pipeline's link with its
         * attachment-read fragment shader) is not what this box sees: said
         * once, so a clean box under GS state as shader data is read as
         * the vertex placement alone */
        s_boxDstSaid = true;
        rd__log("VU skin check: the box check of a draw that reads its attachment (GS state as "
                "shader data, or the exact blend) is drawn through %s without that read (with "
                "it the write mask is the shader's and colour would be written): it checks the "
                "vertex placement through the draw's vertex module and pass, not the draw's "
                "link with its attachment-read fragment shader",
                rd__fs_name(k->boxFs));
    }
    k->box = clipRect(bx0, by0, bx1, by1, &all);
}

/* ICO_RD_VU_SKIN_SABOTAGE=A,ring (or ,nan): the bone at A in the ring at
 * off zeroed (or made NaN) after its bytes were kept */
static void sabRing(uint8_t *ring, uint64_t off)
{
    uint8_t *b = ring + off + (size_t)(s_sab.addr - 16u) * 16u;
    for (uint32_t i = 0; i < 16u; i++) {
        memcpy(b + 4u * i, &s_sab.fill, 4);
    }
}

/* The twins of a draw, inside its pass right after it: A at the draw's
 * bone offset, B at fresh (the same bytes, another page phase), each in an
 * occlusion query (twin t's SK_TWIN_Q0 + 3 t and + 1), nothing written.
 * Through the draw's own pipeline where it reads its attachment and writes
 * no Z (a DrawCB copy with the write mask cleared: the shader writes back
 * the pixel it read); else through the box check's pipeline (the key with
 * no destination read, colour mask 0, Z write off), once it is made. */
static void twinDraw(Replay *r, const RdSkinDraw *d, SkSlot *sl, uint64_t fresh, int32_t check)
{
    const RdPipeKeyInt *dk = d->key;
    if (!rhi_occlusion_supported() || !r->passOpen || r->passStorage || r->dateStNow ||
        g_rd.passSamples > 1 || dk->gs.stencil != 0 || dk->gs.date != RD_DATE_OFF ||
        dk->dst == RHI_DST_READ_STORAGE || sl->nTwins >= SK_TWINS_MAX || fresh == ~0ull) {
        return;
    }
    const uint32_t slot = rd__ring_slot();
    const bool own = dk->dst == RHI_DST_READ_ATTACHMENT && dk->gs.zwrite == RD_ZWRITE_OFF;
    RdPipeKeyInt key = *dk;
    RhiPipeline p = d->pipe;
    uint64_t dOff = d->dOff;
    if (own) {
        /* the draw's DrawCB with WMASK cleared (rd_dst_read.c dstPass puts
         * the mask there): every channel the read value (gs_math.hlsli
         * gs_dst_channel), UNORM8 k / 255 back as k (common.hlsli
         * gs_dst_out_reg), in rasterization order after the draw's own
         * writes: the pixel stays what the draw left */
        IcoDrawCB cb;
        memcpy(&cb, g_rd.ringMap[slot] + d->dOff, sizeof(cb));
        cb.mode[0] &= ~(15u << ICO_DF_WMASK_SHIFT);
        dOff = rd__draw_cb_offset(&cb);
        if (dOff == ~0ull) {
            return;
        }
    } else {
        key.dst = RHI_DST_READ_NONE;
        key.gs.colorMask = 0;
        key.gs.zwrite = RD_ZWRITE_OFF;
        /* taken at once in the tool's and the tests' replays (mode 2, and
         * rd__vu_skin_check_replays: each harvested at once anyway) */
        p = boxPipe(&key, skMode() == 2 || s_collect != NULL);
        if (!p.id) {
            s_blk.twinsNot++;
            return;
        }
    }
    const uint32_t t = sl->nTwins++;
    SkTwin *tw = &sl->twins[t];
    *tw = s_twinCmd;
    tw->bonesOff = d->bonesOff;
    tw->freshOff = fresh;
    tw->fs = dk->fs;
    tw->own = own;
    tw->c = false; /* C, the ring read's twin: B2 */
    tw->check = check;
    const RhiCommandList cl = rd__replay_cl();
    rd__set_pipeline(cl, p, own ? dk : &key);
    rd__bind_uniform(cl, 0, r->frameBG);
    rhi_cmd_set_bind_group(cl, 2, d->g2);
    /* the attachment read's group 3 stays as the draw bound it (the same
     * pipeline; rasterization order needs no barrier between the draws:
     * rhi.h rasterOrderAttachment) */
    if (d->indexBuf.id) {
        rhi_cmd_set_index_buffer(cl, d->indexBuf, d->indexOff, true);
    }
    rhi_cmd_set_scissor(cl, &r->sc);
    for (uint32_t q = 0; q < 2u; q++) {
        const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)d->vOff,
                                  (uint32_t)(q == 0 ? d->bonesOff : fresh)};
        rhi_cmd_set_bind_group_offsets(cl, 1, d->g1, offs, 3);
        rhi_cmd_begin_occlusion(cl, SK_TWIN_Q0 + 3u * t + q);
        if (d->indexBuf.id) {
            rhi_cmd_draw_indexed(cl, d->count, d->first, 0, 1);
        } else {
            rhi_cmd_draw(cl, d->count, d->first, 1);
        }
        rhi_cmd_end_occlusion(cl, SK_TWIN_Q0 + 3u * t + q);
    }
    /* the draw's state back for whatever the replay records next */
    const uint32_t back[3] = {(uint32_t)d->dOff, (uint32_t)d->vOff, (uint32_t)d->bonesOff};
    rhi_cmd_set_bind_group_offsets(cl, 1, d->g1, back, 3);
    if (!own) {
        rd__set_pipeline(cl, d->pipe, dk);
    }
    s_run.twinsDrawn++;
    s_run.twinsOwn += own;
}

void rd__vu_skin_note(Replay *r, const DrawSetup *ds, const RdSkinDraw *d)
{
    const bool arm = g_rdSkinArm;
    /* a twin only of a cluster program's draw (an arm left by a skinned
     * command that drew nothing never twins another program's draw) */
    const bool twin =
        g_rdSkinTwin && d && d->vs >= RD_VS_VU_SKIN && d->vs < RD_VS_VU_SKIN + SK_VS_COUNT;
    g_rdSkinArm = false;
    g_rdSkinTwin = false;
    SkCheck *k = s_cur;
    s_cur = NULL;
    const uint32_t slot = rd__ring_slot();
    SkSlot *sl = &s_slots[slot];
    /* a draw through a merge build's table reads VuCB and its bones through
     * that table, not the b3 offset: neither checked nor twinned */
    if (!d || (g_rd.vuMerge && g_rd.vsMerge[d->vs].id)) {
        return;
    }
    uint8_t *ring = g_rd.ringMap[slot];
    if (!ring || d->bonesOff + SK_BONE_BYTES > g_rd.ringCap[slot] || !d->g1.id) {
        return;
    }
    sabRead();
    bool check = arm && k && sl->n < sl->cap && k == &sl->checks[sl->n] && d->vs == k->vs;
    if (check) {
        memcpy(k->bones, ring + d->bonesOff, SK_BONE_BYTES);
        /* the CPU's positions from the bytes the draw's block holds */
        const RdSkinCase kc = caseOf(k);
        check = rd__skin_reference(&kc, 0, k->ref);
    }
    if (!check && !twin) {
        return;
    }
    /* twin B's block and the probe's c1: the bytes as the draw's block
     * holds them now, before anything below writes the ring */
    const uint64_t ua = rhi_limits()->uniformAlign ? rhi_limits()->uniformAlign : 256u;
    const uint64_t fresh = ringFresh(ring + d->bonesOff, d->bonesOff, ua);
    if (check) {
        memcpy(&k->vcb, d->vcb, sizeof(k->vcb));
        k->g1 = d->g1;
        k->dOff = d->dOff;
        k->bonesOff = d->bonesOff;
        k->sabOff = d->bonesOff;
        k->c1Off = fresh;
    }
    if (s_sab.addr && s_sab.ring) {
        /* H2's form: the bytes under the GPU are not the ones kept (the
         * draw, its twin A and the probe's column 0 read them) */
        sabRing(g_rd.ringMap[slot], d->bonesOff);
    }
    if (twin) {
        twinDraw(r, d, sl, fresh, check ? (int32_t)sl->n : -1);
    }
    if (check) {
        boxCheck(r, ds, d, k, sl->n);
        sl->n++;
        s_pickedThis = true;
    }
}

uint64_t rd__vu_skin_ring_bytes(void)
{
    if (!rd__vu_skin_check_on()) {
        return 0;
    }
    const uint64_t ua = rhi_limits()->uniformAlign ? rhi_limits()->uniformAlign : 256u;
    const uint64_t one = SK_COLS * (sizeof(IcoFrameCB) + ua) + 2u * (sizeof(IcoVuCB) + ua) +
                         2u * (SK_BONE_BYTES + 4096u + ua);
    /* each twin: B's block at a fresh page phase, its own DrawCB copy (the
     * write mask cleared) */
    const uint64_t twin = SK_BONE_BYTES + 4096u + ua + sizeof(IcoDrawCB) + ua;
    return (skMode() == 2 ? SK_CHECKS_ALL : 1u) * one +
           (rd__vu_skin_twins_on() ? SK_TWINS_MAX * twin : 0u) + 4096u;
}

/* the slot's target and readback buffer for cols columns of RD_SKIN_MAXV
 * rows and n bone blocks */
static bool slotEnsure(SkSlot *sl, uint32_t cols, uint32_t n)
{
    const uint32_t w = ICO_VU_PROBE_FIELDS * cols, h = RD_SKIN_MAXV;
    const RhiLimits *lim = rhi_limits();
    const uint32_t a = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1u;
    const uint32_t pitch = (w * 4u + a - 1u) / a * a;
    const uint64_t rowsOff = ((uint64_t)n * SK_BONE_BYTES + 255u) & ~(uint64_t)255u;
    const uint64_t need = rowsOff + (uint64_t)pitch * h;
    if (sl->tex.id && sl->texW >= w && sl->rb.id && sl->rbSize >= need && sl->pitch == pitch) {
        sl->rowsOff = rowsOff;
        return true;
    }
    if (sl->tex.id) {
        rhi_destroy_texture(sl->tex);
    }
    if (sl->rb.id) {
        rhi_destroy_buffer(sl->rb);
    }
    sl->tex = rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UINT,
                                                   RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                   "rd vu skin probe", 0});
    sl->texState = RHI_STATE_UNDEFINED;
    sl->rb = rhi_create_buffer(&(RhiBufferDesc){need, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                                RHI_MEM_READBACK, "rd vu skin probe readback"});
    if (!sl->tex.id || !sl->rb.id) {
        if (sl->tex.id) {
            rhi_destroy_texture(sl->tex);
        }
        if (sl->rb.id) {
            rhi_destroy_buffer(sl->rb);
        }
        sl->tex = (RhiTexture){0};
        sl->rb = (RhiBuffer){0};
        sl->texW = sl->texH = 0;
        sl->rbSize = 0;
        return false;
    }
    sl->texW = w;
    sl->texH = h;
    sl->pitch = pitch;
    sl->rbSize = need;
    sl->rowsOff = rowsOff;
    return true;
}

void rd__vu_skin_end(void)
{
    g_rdSkinArm = false;
    s_cur = NULL;
    g_rdSkinTwin = false;
    const uint32_t slot = rd__ring_slot();
    SkSlot *sl = &s_slots[slot];
    if (s_target.left) {
        s_target.left--; /* the targeted probe's replays */
    }
    if (sl->pending || (sl->n == 0 && sl->nTwins == 0)) {
        return;
    }
    const uint32_t n = sl->n;
    if (n == 0) {
        /* the twins alone: their queries read with the slot */
        sl->pending = true;
        sl->counter = g_rd.replayCounter;
        sl->rhiSlot = rhi_frame_slot();
        sl->ringId = g_rd.ring[slot].id;
        return;
    }
    const RhiCommandList cl = rd__replay_cl();
    const uint64_t ua = rhi_limits()->uniformAlign ? rhi_limits()->uniformAlign : 256u;
    if (!cl.id || !indexMake() || !slotEnsure(sl, n * SK_COLS, n)) {
        sl->n = 0;
        sl->nTwins = 0;
        return;
    }
    jobsPoll();
    roPoll(skMode() == 2);
    RhiBinding fb;
    memset(&fb, 0, sizeof(fb));
    fb.slot = 0;
    fb.type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC;
    fb.buffer = g_rd.ring[slot];
    fb.offset = 0;
    fb.size = sizeof(IcoFrameCB);
    const RhiBindGroup g0 = rd__replay_bind_group(g_rd.layoutFrame, &fb, 1);
    const RhiBindGroup g2 =
        rd__tex_group(g_rd.dummy, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                              RD_WRAP_CLAMP));
    if (!g0.id || !g2.id) {
        sl->n = 0;
        sl->nTwins = 0;
        return;
    }
    /* each check's blocks first: c1's bytes, the probe's VuCB, its FrameCBs */
    uint64_t vOff[SK_CHECKS_ALL], fOff[SK_CHECKS_ALL][SK_COLS];
    bool ok = true;
    for (uint32_t i = 0; i < n && ok; i++) {
        SkCheck *k = &sl->checks[i];
        /* c1's block is twin B's, put at the note (rd__vu_skin_note) */
        if (s_sab.addr && !s_sab.ring) {
            /* H1's form: the draw's block right, the probe's read wrong */
            float b[240][4];
            memcpy(b, k->bones, sizeof(b));
            memset(b[s_sab.addr - 16u], 0, 64);
            k->sabOff = ringPut(b, SK_BONE_BYTES, ua);
        }
        IcoVuCB v = k->vcb;
        v.draw[2] |= ICO_VU_PROBE;
        vOff[i] = ringPut(&v, sizeof(v), ua);
        for (uint32_t c = 0; c < SK_COLS; c++) {
            IcoFrameCB fc;
            memset(&fc, 0, sizeof(fc));
            fc.target[0] = (float)sl->texW;
            fc.target[1] = (float)sl->texH;
            fc.target[2] = 1.0f / (float)sl->texW;
            fc.target[3] = 1.0f / (float)sl->texH;
            fc.frame[0] = 1.0f;
            /* the exact viewport's form of vu_probe_out: texel (16 col + field,
             * vertex - first) of the whole target */
            fc.vp[0] = 2.0f / (float)sl->texW;
            fc.vp[1] = 2.0f / (float)sl->texH;
            fc.misc[2] = 1.0f;
            fc.misc[3] = 1.0f;
            fc.vpo[0] = (float)(ICO_VU_PROBE_FIELDS * (i * SK_COLS + c));
            fc.vpo[1] = -(float)k->first;
            fOff[i][c] = ringPut(&fc, sizeof(fc), ua);
            ok = ok && fOff[i][c] != ~0ull;
        }
        ok = ok && k->c1Off != ~0ull && k->sabOff != ~0ull && vOff[i] != ~0ull;
    }
    if (!ok) {
        sl->n = 0;
        sl->nTwins = 0;
        return;
    }
    rd__transition(cl, sl->tex, &sl->texState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    rp.color[0] =
        (RhiColorAttachment){sl->tex, RHI_LOAD_CLEAR, {0, 0, 0, 0}, RHI_STORE_STORE, false};
    rp.colorCount = 1;
    rp.width = sl->texW;
    rp.height = sl->texH;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_set_index_buffer(cl, s_p.idx, 0, true);
    for (uint32_t i = 0; i < n; i++) {
        SkCheck *k = &sl->checks[i];
        const RhiPipeline main = rd__vu_skin_probe_pipeline(k->vs, false);
        const RhiPipeline ro = rd__vu_skin_probe_pipeline(k->vs, true);
        const RhiPipeline ringP = rd__vu_skin_ring_module(k->vs);
        k->c2 = ro.id != 0;
        k->c3 = ringP.id != 0;
        for (uint32_t c = 0; c < SK_COLS; c++) {
            const RhiPipeline p = c == 3 ? ringP : c == 2 ? ro : main;
            if (!p.id) {
                continue;
            }
            /* column 3 reads the draw's bones through the ring (B2: its
             * offset in VuCB's mark lane), at the draw's own offset here */
            const uint64_t bones = c == 0 ? k->sabOff : c == 1 ? k->c1Off : k->bonesOff;
            const uint32_t f0 = (uint32_t)fOff[i][c];
            const uint32_t offs[3] = {(uint32_t)k->dOff, (uint32_t)vOff[i], (uint32_t)bones};
            rd__set_pipeline(cl, p, NULL);
            rhi_cmd_set_bind_group_offsets(cl, 0, g0, &f0, 1);
            rhi_cmd_set_bind_group_offsets(cl, 1, k->g1, offs, 3);
            rhi_cmd_set_bind_group(cl, 2, g2);
            rhi_cmd_draw_indexed(cl, k->n * 9u, k->first * 9u, 0, 1);
        }
    }
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, sl->tex, &sl->texState, RHI_STATE_COPY_SRC);
    rhi_cmd_copy_texture_to_buffer(cl, sl->tex, RHI_ASPECT_COLOR,
                                   (RhiRect){0, 0, sl->texW, sl->texH}, sl->rb, sl->rowsOff,
                                   sl->pitch);
    for (uint32_t i = 0; i < n; i++) {
        /* the bytes the GPU sees at the draw's offset, after its draws */
        rhi_cmd_copy_buffer(cl, g_rd.ring[slot], sl->checks[i].bonesOff, sl->rb,
                            (uint64_t)i * SK_BONE_BYTES, SK_BONE_BYTES);
    }
    sl->pending = true;
    s_run.drawn += n;
    sl->counter = g_rd.replayCounter;
    sl->rhiSlot = rhi_frame_slot();
    sl->ringId = g_rd.ring[slot].id;
    /* the schedule's cursor on by the golden ratio of the last total: the
     * next probe lands in another part of the skinned vertices */
    s_cursorV += s_lastV ? (uint64_t)((double)s_lastV * 0.6180339887) + 1u : 1u;
    s_lastMs = rd__now_ms();
}

/* ------------------------------------------------------------ the reading */

static int varState(const SkCheck *k, const RdSkinVerdict *v, bool run)
{
    (void)k;
    return !run ? RD_SKIN_VAR_NOT_RUN : v->wrong ? RD_SKIN_VAR_WRONG : RD_SKIN_VAR_RIGHT;
}

static const char *varWords(int s)
{
    return s == RD_SKIN_VAR_RIGHT ? "right" : s == RD_SKIN_VAR_WRONG ? "wrong" : "not run";
}

/* the probe of column col of check i from the readback rows */
static void probeRead(const SkSlot *sl, const uint8_t *map, uint32_t i, uint32_t col,
                      int32_t (*got)[3], uint32_t n)
{
    for (uint32_t v = 0; v < n; v++) {
        const uint8_t *row = map + sl->rowsOff + (size_t)v * sl->pitch;
        for (uint32_t f = 0; f < 3u; f++) {
            memcpy(&got[v][f], row + ((size_t)ICO_VU_PROBE_FIELDS * (i * SK_COLS + col) + f) * 4u,
                   4);
        }
    }
}

static void moduleName(uint8_t vs, char *out, size_t size)
{
    const char *nm = rd__vs_name(vs);
    const size_t len = strlen(nm);
    if (g_rd.vsPlain[vs].id && g_rd.vsFallback[vs].id == g_rd.vsPlain[vs].id && len > 3 &&
        strcmp(nm + len - 3, "_vs") == 0) {
        snprintf(out, size, "%.*s_plain_vs", (int)(len - 3), nm);
    } else {
        snprintf(out, size, "%s", nm);
    }
}

/* the twin's two counts (C's too: UINT64_MAX not drawn) from the slot's
 * samples; false when A or B was not read */
static bool twinCounts(const SkTwin *tw, uint32_t t, const uint64_t *samples, uint32_t ns,
                       uint64_t out[3])
{
    const uint32_t q = SK_TWIN_Q0 + 3u * t;
    out[0] = out[1] = out[2] = UINT64_MAX;
    if (!samples || q + 1u >= ns || samples[q] == RHI_OCCLUSION_NONE ||
        samples[q + 1u] == RHI_OCCLUSION_NONE) {
        return false;
    }
    out[0] = samples[q];
    out[1] = samples[q + 1u];
    if (tw->c && q + 2u < ns) {
        out[2] = samples[q + 2u];
    }
    return true;
}

/* A and B differ: by count where the device's queries are precise, else
 * only as zero or not (rhi.h: a count is then exact only so) */
static bool twinsDiffer(const uint64_t c[3])
{
    if (rhi_limits()->occlusionPrecise) {
        return c[0] != c[1];
    }
    return (c[0] != 0u) != (c[1] != 0u);
}

/* The run's capture, once (rd_vu_skin_capture.c): the sidecar from what
 * the harvest holds (k NULL: the twin's alone), then the window asked for
 * a frame dump of the next present */
static void captureWrite(const SkCheck *k, const SkTwin *tw, const uint64_t tc[3],
                         const uint8_t *gpu, const uint8_t *ringNow, const int32_t *rows,
                         const RdSkinVerdict *v0, const char *decision, bool differ)
{
    if (s_cap.done) {
        return;
    }
    s_cap.done = true;
    s_cap.want = false;
    static RdSkinCapture c;
    memset(&c, 0, sizeof(c));
    RdSkinCaptureHeader *h = &c.h;
    const SkTwin none = {.check = -1};
    if (!tw) {
        tw = &none;
    }
    h->frame = k ? k->frame : tw->frame;
    h->list = k ? k->list : tw->list;
    h->index = k ? k->index : tw->index;
    h->meshId = k ? k->meshId : tw->meshId;
    h->meshVertices = k ? k->meshVertices : tw->meshVertices;
    snprintf(h->mesh, sizeof(h->mesh), "%s", k ? k->mesh : tw->mesh);
    snprintf(h->fs, sizeof(h->fs), "%s", tw->meshId || tw->bonesOff ? rd__fs_name(tw->fs) : "");
    snprintf(h->reads, sizeof(h->reads), "%s", decision ? decision : "");
    h->bonesOff = k ? k->bonesOff : tw->bonesOff;
    h->freshOff = k ? k->c1Off : tw->freshOff;
    h->twinA = tc ? tc[0] : UINT64_MAX;
    h->twinB = tc ? tc[1] : UINT64_MAX;
    h->twinC = tc ? tc[2] : UINT64_MAX;
    h->flags = (k ? 1u : 0u) | (tc ? 2u : 0u) | (ringNow ? 4u : 0u) | (v0 && v0->wrong ? 8u : 0u) |
               (differ ? 16u : 0u) | (k && k->targeted ? 32u : 0u);
    if (k) {
        h->code = k->code;
        h->probeFirst = k->first;
        h->probeVertices = k->n;
        h->probeCols = SK_COLS;
        c.cpu = k->bones;
        h->cpuBytes = SK_BONE_BYTES;
        c.gpu = gpu;
        h->gpuBytes = gpu ? SK_BONE_BYTES : 0u;
        c.vcb = &k->vcb;
        h->vcbBytes = (uint32_t)sizeof(k->vcb);
        c.mem = k->mem;
        h->memBytes = (uint32_t)sizeof(k->mem);
        c.stream = k->stream;
        h->streamBytes = k->n * 5u * 16u;
        c.rows = rows;
        h->rowsBytes = rows ? SK_COLS * RD_SKIN_MAXV * 3u * 4u : 0u;
    } else if (ringNow) {
        /* the twin's alone: B's block holds the bytes the draw's held at
         * the note (the CPU's), the draw's offset the ring as it is now */
        c.cpu = ringNow + tw->freshOff;
        h->cpuBytes = SK_BONE_BYTES;
    }
    if (ringNow) {
        c.ring = ringNow + h->bonesOff;
        h->ringBytes = SK_BONE_BYTES;
    }
    if (v0) {
        h->wrong = v0->wrong;
        h->dropped = v0->dropped;
        h->addr = v0->addr;
        h->nanAddr = v0->nanAddr;
    }
    char path[1100];
    if (rd__vu_skin_capture_write(rd__vu_skin_capture_dir(), &c, path, sizeof(path))) {
        rd__log("VU skin check: captured %s and asked for a frame dump", path);
        rd__vu_skin_capture_ask(path);
    } else {
        rd__log("VU skin check: no sidecar written (%s); asked for a frame dump", path);
        rd__vu_skin_capture_ask("");
    }
}

static void readCheck(const SkSlot *sl, const uint8_t *map, uint32_t i, bool intact,
                      const uint8_t *ringNow, const uint64_t *samples, uint32_t nSamples)
{
    const SkCheck *k = &sl->checks[i];
    static int32_t got[SK_COLS][RD_SKIN_MAXV][3];
    for (uint32_t c = 0; c < SK_COLS; c++) {
        probeRead(sl, map, i, c, got[c], k->n);
    }
    const RdSkinCase kc = caseOf(k);
    RdSkinVerdict v[SK_COLS];
    rd__skin_judge(&kc, k->ref, (const int32_t (*)[3])got[0], &v[0]);
    rd__skin_judge(&kc, k->ref, (const int32_t (*)[3])got[1], &v[1]);
    if (k->c2) {
        rd__skin_judge(&kc, k->ref, (const int32_t (*)[3])got[2], &v[2]);
    } else {
        memset(&v[2], 0, sizeof(v[2]));
    }
    if (k->c3) {
        rd__skin_judge(&kc, k->ref, (const int32_t (*)[3])got[3], &v[3]);
    } else {
        memset(&v[3], 0, sizeof(v[3]));
    }
    /* the address named: the zero matrix's, else the non-finite one's
     * (every vertex on it dropped), else the first wrong vertex's lane x */
    const uint32_t named = v[0].addr ? v[0].addr : v[0].nanAddr ? v[0].nanAddr : v[0].lanes[0];
    SkCheck *km = &sl->checks[i];
    km->judged = true;
    km->probeWrong = v[0].wrong;
    km->probeAddr = v[0].wrong ? named : 0u;
    km->probeDropped = v[0].dropped;
    uint32_t q0 = 0, q1 = 0, r0 = 0, r1 = 0;
    const bool copyDiff =
        rd__skin_bytes_differ(map + (size_t)i * SK_BONE_BYTES, k->bones, SK_BONE_BYTES, &q0, &q1);
    const bool reread = intact && ringNow;
    const bool rereadDiff =
        reread && rd__skin_bytes_differ(ringNow + k->bonesOff, k->bones, SK_BONE_BYTES, &r0, &r1);
    const int c1 = varState(k, &v[1], true), c2 = varState(k, &v[2], k->c2);
    const char *decision = rd__skin_decision(copyDiff, rereadDiff, v[0].wrong != 0, c1, c2);
    /* the box check */
    const uint32_t qi = i;
    const bool boxRead = k->boxRun && samples && qi < nSamples && samples[qi] != RHI_OCCLUSION_NONE;
    const uint64_t outside = boxRead ? samples[qi] : 0u;
    /* the counts */
    s_run.checks++;
    s_blk.checks++;
    s_run.targeted += k->targeted;
    s_run.wrong += v[0].wrong != 0;
    s_blk.wrong += v[0].wrong != 0;
    s_run.blocks += copyDiff;
    s_blk.blocks += copyDiff;
    s_run.reread += rereadDiff;
    s_blk.reread += rereadDiff;
    s_run.box += boxRead;
    s_blk.box += boxRead;
    s_run.boxOut += outside != 0;
    s_blk.boxOut += outside != 0;
    s_blk.c1Right += v[0].wrong && c1 == RD_SKIN_VAR_RIGHT;
    s_blk.c2Right += v[0].wrong && c2 == RD_SKIN_VAR_RIGHT;
    s_blk.c2Run += k->c2;
    s_blk.maxRight = v[0].maxRightXY > s_blk.maxRight ? v[0].maxRightXY : s_blk.maxRight;
    if (s_collect) {
        RdSkinSummary *o = s_collect;
        o->checks++;
        o->wrong += v[0].wrong != 0;
        o->blocksDiffer += copyDiff;
        o->rereadDiffer += rereadDiff;
        o->boxChecks += boxRead;
        o->boxOutside += outside != 0;
        o->c1Right += v[0].wrong && c1 == RD_SKIN_VAR_RIGHT;
        o->c2Right += v[0].wrong && c2 == RD_SKIN_VAR_RIGHT;
        o->c2Run += k->c2;
        o->targeted += k->targeted;
        o->targetedWrong += k->targeted && v[0].wrong;
        o->dropped += v[0].dropped;
        /* the address and the words of the last wrong check; an event
         * whose probe drew right (its bytes differed, nothing it reads
         * moved: a block whose changed bone the mesh does not use) gives
         * its words only while no wrong check has */
        if (v[0].wrong) {
            o->lastAddr = v[0].addr ? v[0].addr : v[0].nanAddr;
            snprintf(o->decision, sizeof(o->decision), "%s", decision);
        } else if ((copyDiff || rereadDiff) && o->wrong == 0) {
            snprintf(o->decision, sizeof(o->decision), "%s", decision);
        }
    }
    char place[48];
    if (k->list != UINT32_MAX) {
        snprintf(place, sizeof(place), "%u:%u", k->list, k->index);
    } else {
        snprintf(place, sizeof(place), "?");
    }
    if (v[0].wrong || copyDiff || rereadDiff) {
        const uint64_t ev = v[0].wrong ? ++s_run.wrongEvents : ++s_run.bytesEvents;
        if (sayNth(ev)) {
            char cw[48], rw[48], mod[48];
            rd__skin_bytes_words(copyDiff, q0, q1, cw, sizeof(cw));
            if (reread) {
                rd__skin_bytes_words(rereadDiff, r0, r1, rw, sizeof(rw));
            } else {
                snprintf(rw, sizeof(rw), "not read (the ring's slot was used again)");
            }
            moduleName(k->vs, mod, sizeof(mod));
            if (v[0].wrong) {
                /* the address named: the one whose zero matrix gives the
                 * most wrong vertices, else the one whose non-finite matrix
                 * drops the most, else the first wrong vertex's lane x */
                const uint32_t a = named;
                char drop[128] = "";
                if (v[0].dropped) {
                    /* a non-finite or huge read: the vertex's X, Y or Z
                     * saturated, its triangles dropped (issue 123's
                     * v0.10.4 picture) */
                    if (v[0].nanAddr) {
                        snprintf(drop, sizeof(drop),
                                 "; reads as non-finite / dropped: %u of them (a non-finite "
                                 "matrix at %u drops %u)",
                                 v[0].dropped, v[0].nanAddr, v[0].nanExplained);
                    } else {
                        snprintf(drop, sizeof(drop), "; reads as non-finite / dropped: %u of them",
                                 v[0].dropped);
                    }
                }
                rd__log("VU skin check: frame %u command %s mesh %s #%u (%u vertices): %u "
                        "vertices wrong of %u, address %u: got (%.1f, %.1f, %d) want (%.1f, "
                        "%.1f, %d) (a zero matrix at %u gives it: %s; it gives %u of them%s); "
                        "the bone block as the GPU copied it: %s; as the CPU reads it after the "
                        "fence: %s; code %d through %s (wrong check %llu%s)",
                        k->frame, place, k->mesh, k->meshId, k->meshVertices, v[0].wrong,
                        v[0].compared, a, v[0].got[0] / 16.0, v[0].got[1] / 16.0, v[0].got[2],
                        v[0].want.x / 16.0, v[0].want.y / 16.0, v[0].want.z, a,
                        v[0].explains ? "yes" : "no", v[0].explained, drop, cw, rw, k->code, mod,
                        (unsigned long long)ev,
                        k->targeted ? "; the probe targeted after a twin event" : "");
                const uint64_t phase = k->c1Off & 4095u;
                const char *c2w =
                    k->c2 ? varWords(c2)
                    : !rhi_limits()->robustText || strncmp(rhi_limits()->robustText, "on", 2) != 0
                        ? "not run (robust access off)"
                    : rhi_limits()->uniformRobustOff
                        ? "not run (its pipeline not made yet)"
                        : "not offered (no VK_EXT_pipeline_robustness)";
                rd__log("VU skin check: frame %u command %s variants: (c1) the block at a fresh "
                        "ring offset (page phase %llu, the draw's %llu): %s (%u wrong); (c2) "
                        "uniform robustness off: %s (%u wrong); reads as: %s",
                        k->frame, place, (unsigned long long)phase,
                        (unsigned long long)(k->bonesOff & 4095u), varWords(c1), v[1].wrong, c2w,
                        v[2].wrong, decision);
            } else {
                rd__log("VU skin check: frame %u command %s mesh %s #%u (%u vertices): every "
                        "vertex right (%u compared); the bone block as the GPU copied it: %s; as "
                        "the CPU reads it after the fence: %s; reads as: %s (bytes event %llu)",
                        k->frame, place, k->mesh, k->meshId, k->meshVertices, v[0].compared, cw, rw,
                        decision, (unsigned long long)ev);
            }
        }
    }
    if (outside && sayNth(++s_run.boxEvents)) {
        rd__log("VU skin check: frame %u command %s drew %llu samples outside its box (texels "
                "%d,%d %ux%u, the CPU's vertices + 2 GS pixels; through %s%s; the probe %s) (box "
                "event %llu)",
                k->frame, place, (unsigned long long)outside, k->box.x, k->box.y, k->box.w,
                k->box.h, rd__fs_name(k->boxFs),
                k->boxNoDst ? " without the draw's attachment read" : "",
                v[0].wrong ? "wrong too" : "right", (unsigned long long)s_run.boxEvents);
    }
    /* the capture: the run's first wrong check, or the check targeted
     * after the twin event the capture waits for */
    if (!s_cap.done &&
        (v[0].wrong || (s_cap.want && k->targeted && k->meshId == s_cap.twin.meshId))) {
        const SkTwin *tw = NULL;
        uint64_t tc[3];
        bool haveTc = false;
        for (uint32_t t = 0; t < sl->nTwins && !tw; t++) {
            if (sl->twins[t].check == (int32_t)i) {
                tw = &sl->twins[t];
                haveTc = twinCounts(tw, t, samples, nSamples, tc);
            }
        }
        if (!haveTc && s_cap.want) {
            memcpy(tc, s_cap.counts, sizeof(tc));
            haveTc = true;
        }
        captureWrite(k, tw ? tw : (s_cap.want ? &s_cap.twin : NULL), haveTc ? tc : NULL,
                     map + (size_t)i * SK_BONE_BYTES, reread ? ringNow : NULL, &got[0][0][0], &v[0],
                     decision, haveTc && twinsDiffer(tc));
    }
}

/* twin t of the slot: its counts, and on a difference the event line, the
 * targeted probe and the capture */
static void readTwin(const SkSlot *sl, uint32_t t, const uint64_t *samples, uint32_t ns,
                     const uint8_t *ringNow)
{
    const SkTwin *tw = &sl->twins[t];
    uint64_t c[3];
    if (!twinCounts(tw, t, samples, ns, c)) {
        return;
    }
    s_run.twins++;
    s_blk.twins++;
    if (s_collect) {
        s_collect->twins++;
        s_collect->twinsOwn += tw->own;
    }
    if (!twinsDiffer(c)) {
        return;
    }
    s_run.twinsDiffer++;
    s_blk.twinsDiffer++;
    if (!s_blk.anyDiffer) {
        s_blk.anyDiffer = true;
        s_blk.firstDiffer = tw->frame;
    }
    if (s_collect) {
        s_collect->twinsDiffer++;
    }
    /* the probe's target for the next replays */
    s_target.meshId = tw->meshId;
    s_target.list = tw->list;
    s_target.left = SK_TARGET_REPLAYS;
    const SkCheck *k =
        tw->check >= 0 && (uint32_t)tw->check < sl->n ? &sl->checks[tw->check] : NULL;
    char probe[64];
    const char *reads;
    if (k && k->judged && k->probeWrong) {
        snprintf(probe, sizeof(probe), "wrong at address %u", k->probeAddr);
        reads = "the draw's bone read at its own offset (the probe of the same bytes is wrong "
                "too: its lines above)";
    } else if (k && k->judged) {
        snprintf(probe, sizeof(probe), "right");
        reads = "the scene pass's execution: the twins differ inside the pass while the probe "
                "of the same bytes drew right outside it";
    } else {
        snprintf(probe, sizeof(probe), "not probed");
        reads = "the bone read inside the scene pass differs between the draw's offset and the "
                "same bytes at a fresh one; the probe of this command follows in the next "
                "replays";
    }
    if (sayNth(++s_run.twinEvents)) {
        char place[48], cw[48] = "";
        if (tw->list != UINT32_MAX) {
            snprintf(place, sizeof(place), "%u:%u", tw->list, tw->index);
        } else {
            snprintf(place, sizeof(place), "?");
        }
        if (c[2] != UINT64_MAX) {
            snprintf(cw, sizeof(cw), ", C (ring read) %llu samples", (unsigned long long)c[2]);
        }
        rd__log("VU skin check: frame %u command %s mesh %s #%u (%u vertices): twins differ: A "
                "(the draw's offset, page phase %llu) %llu samples, B (fresh, phase %llu) %llu "
                "samples%s; through %s%s; the probe of this command: %s; reads as: %s (twin "
                "event %llu)",
                tw->frame, place, tw->mesh, tw->meshId, tw->meshVertices,
                (unsigned long long)(tw->bonesOff & 4095u), (unsigned long long)c[0],
                (unsigned long long)(tw->freshOff & 4095u), (unsigned long long)c[1], cw,
                rd__fs_name(tw->fs),
                tw->own ? " with the draw's attachment read (its own pipeline)"
                        : " with colour and Z writes off",
                probe, reads, (unsigned long long)s_run.twinEvents);
    }
    if (s_cap.done) {
        return;
    }
    if (k && k->judged) {
        /* probed in the same replay (its harvest above captured a wrong
         * one): the check's CPU side with the twins' counts */
        captureWrite(k, tw, c, NULL, ringNow, NULL, NULL, reads, true);
        return;
    }
    if (!s_cap.want) {
        /* wait for the targeted probe's harvest (its rows and the GPU's
         * copy), at most the targeted replays and the slots in flight */
        s_cap.want = true;
        s_cap.twin = *tw;
        memcpy(s_cap.counts, c, sizeof(c));
        s_cap.deadline = g_rd.replayCounter + SK_TARGET_REPLAYS + RHI_FRAMES_IN_FLIGHT + 1u;
    }
}

static void readSlot(SkSlot *sl, uint32_t slot, bool intact, bool occ)
{
    static uint64_t samples[SK_QUERIES];
    uint32_t ns = 0;
    bool anyBox = sl->nTwins != 0;
    for (uint32_t i = 0; i < sl->n; i++) {
        anyBox = anyBox || sl->checks[i].boxRun;
    }
    if (anyBox && occ) {
        ns = rhi_read_occlusion(samples, SK_QUERIES);
    }
    const uint8_t *ringNow = intact ? g_rd.ringMap[slot] : NULL;
    const uint8_t *map = sl->n && sl->rb.id ? rhi_map_buffer(sl->rb) : NULL;
    if (map) {
        for (uint32_t i = 0; i < sl->n; i++) {
            readCheck(sl, map, i, intact, ringNow, ns ? samples : NULL, ns);
        }
        rhi_unmap_buffer(sl->rb);
    }
    for (uint32_t t = 0; t < sl->nTwins; t++) {
        readTwin(sl, t, ns ? samples : NULL, ns, ringNow);
    }
    if (s_cap.want && !s_cap.done && g_rd.replayCounter >= s_cap.deadline) {
        /* no targeted probe read in time: the twin's data alone */
        captureWrite(NULL, &s_cap.twin, s_cap.counts, NULL, NULL, NULL, NULL,
                     "twins differ, the command not probed", true);
    }
    sl->pending = false;
    sl->n = 0;
    sl->nTwins = 0;
}

void rd__vu_skin_harvest(void)
{
    if (!g_rd.hasDevice) {
        return;
    }
    for (uint32_t s = 0; s < RHI_FRAMES_IN_FLIGHT; s++) {
        SkSlot *sl = &s_slots[s];
        /* the replay FIF - 1 after the check's waits for its frame: done */
        if (!sl->pending || g_rd.replayCounter + 1u < sl->counter + RHI_FRAMES_IN_FLIGHT) {
            continue;
        }
        const bool intact = g_rd.replayCounter + 1u == sl->counter + RHI_FRAMES_IN_FLIGHT &&
                            s == g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT &&
                            g_rd.ring[s].id == sl->ringId;
        readSlot(sl, s, intact, rhi_frame_slot() == sl->rhiSlot);
    }
}

/* the last replay's check now (the tool, the tests): its frame slot
 * recycled as rd__occlusion_take does, the ring untouched since */
static void harvestNow(void)
{
    const uint32_t s = (g_rd.replayCounter + RHI_FRAMES_IN_FLIGHT - 1u) % RHI_FRAMES_IN_FLIGHT;
    SkSlot *sl = &s_slots[s];
    if (!sl->pending || sl->counter != g_rd.replayCounter) {
        return;
    }
    for (uint32_t k = 0; k < RHI_FRAMES_IN_FLIGHT; k++) {
        rd__wait_frame();
        if (rhi_frame_slot() == sl->rhiSlot) {
            break;
        }
    }
    rhi_wait_idle();
    readSlot(sl, s, g_rd.ring[s].id == sl->ringId, rhi_frame_slot() == sl->rhiSlot);
}

void rd__vu_skin_block(void)
{
    if (!s_blk.checks && !s_blk.twins && !s_blk.twinsNot) {
        return;
    }
    char twins[160] = "";
    if (s_blk.twins || s_blk.twinsNot) {
        char first[48] = "";
        if (s_blk.anyDiffer) {
            snprintf(first, sizeof(first), " (first at frame %u)", s_blk.firstDiffer);
        }
        snprintf(twins, sizeof(twins),
                 "; %llu twins, %llu differ%s, %llu not drawn (their "
                 "pipeline not made yet)",
                 (unsigned long long)s_blk.twins, (unsigned long long)s_blk.twinsDiffer, first,
                 (unsigned long long)s_blk.twinsNot);
    }
    rd__log("VU skin check: %llu checks, %llu wrong, %llu blocks whose bytes differed (%llu "
            "changed after the draw); %llu box checks, %llu with samples outside the box, %llu "
            "not drawn (their pipeline not made yet; %llu with no slot left for the key); "
            "variants on the wrong ones: c1 right %llu, c2 right %llu (c2 drawn in %llu checks); "
            "largest difference on a right vertex %d/16 pixel%s",
            (unsigned long long)s_blk.checks, (unsigned long long)s_blk.wrong,
            (unsigned long long)s_blk.blocks, (unsigned long long)s_blk.reread,
            (unsigned long long)s_blk.box, (unsigned long long)s_blk.boxOut,
            (unsigned long long)s_blk.boxNot, (unsigned long long)s_blk.boxFull,
            (unsigned long long)s_blk.c1Right, (unsigned long long)s_blk.c2Right,
            (unsigned long long)s_blk.c2Run, s_blk.maxRight, twins);
    memset(&s_blk, 0, sizeof(s_blk));
}

int rd__vu_skin_check_frame(const RdFrame *f, int keep, RdSkinSummary *out)
{
    memset(out, 0, sizeof(*out));
    if (!f || !g_rd.hasDevice || rhi_backend() != RHI_BACKEND_VULKAN) {
        return -1;
    }
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            out->commands += f->lists[l].cmds[i].type == RDC_SKINNED;
        }
    }
    const int saved = s_set;
    s_set = 2;
    s_collect = out;
    /* the box and twin queries' pools before any recording (no
     * first-begin stall) */
    (void)rhi_occlusion_prepare(rd__vu_skin_queries());
    int rc = 0;
    /* the replays of the commands from s_allFrom on, SK_CHECKS_ALL each */
    for (uint32_t from = 0; from < out->commands || from == 0; from += SK_CHECKS_ALL) {
        s_allFrom = from;
        if (!rd__replay_frame(f, keep, false)) {
            rc = -1;
            break;
        }
        harvestNow();
        if (out->commands == 0) {
            break;
        }
    }
    s_set = saved;
    s_collect = NULL;
    s_allFrom = 0;
    return rc < 0 ? rc : (int)out->wrong;
}

int rd__vu_skin_check_replays(const RdFrame *f, int keep, uint32_t replays, RdSkinSummary *out)
{
    memset(out, 0, sizeof(*out));
    if (!f || !g_rd.hasDevice || rhi_backend() != RHI_BACKEND_VULKAN) {
        return -1;
    }
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            out->commands += f->lists[l].cmds[i].type == RDC_SKINNED;
        }
    }
    s_collect = out;
    (void)rhi_occlusion_prepare(rd__vu_skin_queries());
    int rc = 0;
    for (uint32_t i = 0; i < replays; i++) {
        if (!rd__replay_frame(f, keep, false)) {
            rc = -1;
            break;
        }
        harvestNow();
    }
    s_collect = NULL;
    return rc < 0 ? rc : (int)out->wrong;
}

void rd__vu_skin_shutdown(void)
{
    if (s_run.drawn) {
        /* the run's own end: what it drew and what it read (a check
         * recorded in the last replays is not read: its slot not
         * recycled), the A/B's proof that the check ran
         * (rd_vu_skin_check_ab) */
        rd__log("VU skin check: at the end of the run: %llu checks drawn (%llu box checks drawn), "
                "%llu read, %llu wrong, %llu blocks whose bytes differed, %llu box checks with "
                "samples outside",
                (unsigned long long)s_run.drawn, (unsigned long long)s_run.boxDrawn,
                (unsigned long long)s_run.checks, (unsigned long long)s_run.wrong,
                (unsigned long long)s_run.blocks, (unsigned long long)s_run.boxOut);
    }
    if (s_run.twinsDrawn) {
        /* the A/B's proof that the attachment-read draws were twinned
         * through their own pipelines (rd_vu_skin_check_ab, attach) */
        rd__log("VU skin check: twins at the end of the run: %llu drawn (%llu through the draw's "
                "own pipeline with its attachment read), %llu read, %llu differ; %llu checks "
                "targeted after a twin event",
                (unsigned long long)s_run.twinsDrawn, (unsigned long long)s_run.twinsOwn,
                (unsigned long long)s_run.twins, (unsigned long long)s_run.twinsDiffer,
                (unsigned long long)s_run.targeted);
    }
    if (g_rd.hasDevice) {
        if (s_p.roJob) {
            (void)rhi_create_pipelines_end(s_p.roJob, true, s_p.ro, NULL, NULL);
        }
        for (int i = 0; i < SK_BOX_PIPES; i++) {
            if (s_box[i].job) {
                (void)rhi_create_pipelines_end(s_box[i].job, true, &s_box[i].pipe, NULL, NULL);
            }
            if (s_box[i].pipe.id) {
                rhi_destroy_pipeline(s_box[i].pipe);
            }
        }
        for (uint32_t i = 0; i < SK_VS_COUNT; i++) {
            if (s_p.probe[i].id) {
                rhi_destroy_pipeline(s_p.probe[i]);
            }
            if (s_p.ro[i].id) {
                rhi_destroy_pipeline(s_p.ro[i]);
            }
        }
        if (s_p.ps.id) {
            rhi_destroy_shader(s_p.ps);
        }
        if (s_p.idx.id) {
            rhi_destroy_buffer(s_p.idx);
        }
        for (uint32_t s = 0; s < RHI_FRAMES_IN_FLIGHT; s++) {
            if (s_slots[s].tex.id) {
                rhi_destroy_texture(s_slots[s].tex);
            }
            if (s_slots[s].rb.id) {
                rhi_destroy_buffer(s_slots[s].rb);
            }
        }
    }
    for (uint32_t s = 0; s < RHI_FRAMES_IN_FLIGHT; s++) {
        for (uint32_t i = 0; i < s_slots[s].cap; i++) {
            checkFree(&s_slots[s].checks[i]);
        }
        free(s_slots[s].checks);
    }
    memset(s_slots, 0, sizeof(s_slots));
    memset(&s_p, 0, sizeof(s_p));
    memset(s_box, 0, sizeof(s_box));
    memset(&s_run, 0, sizeof(s_run));
    memset(&s_blk, 0, sizeof(s_blk));
    memset(&s_sab, 0, sizeof(s_sab));
    s_cur = NULL;
    g_rdSkinArm = false;
    s_seenReplay = s_seen = 0;
    s_seenV = s_lastV = s_cursorV = 0;
    memset(&s_target, 0, sizeof(s_target));
    memset(&s_cap, 0, sizeof(s_cap));
    memset(&s_twinCmd, 0, sizeof(s_twinCmd));
    g_rdSkinTwin = false;
    s_turnip = -1;
    s_interval = -2.0;
    s_pickedThis = false;
    s_lastMs = 0.0;
    s_env = -2;
    s_dev = -1;
    s_memStop = false;
    memset(s_bgSaid, 0, sizeof(s_bgSaid));
    s_boxDstSaid = false;
}
