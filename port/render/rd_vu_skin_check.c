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
 * the counts in the 10 s block. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_mesh.h"
#include "rd_replay.h"
#include "rd_vu_skin_check.h"
#include "shader_consts.h"

bool g_rdSkinArm;

#define SK_INTERVAL_MS 100.0
#define SK_COLS 3u        /* a check's probes: the draw's block, c1, c2 */
#define SK_CHECKS_ALL 80u /* mode 2: the checks one replay holds (16 x 3 x 80 texels wide) */
#define SK_INDEX_VERTS 8192u
#define SK_QUERY0 (RHI_MAX_OCCLUSION - 1u) /* check i's box query: SK_QUERY0 - i */
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

/* ICO_RD_VU_SKIN_SABOTAGE=A[,ring] (tests): the bone at VU address A read
 * as zeros by the check's own probe (the draw's bytes right: a wrong
 * uniform read, H1's form), or with ",ring" zeroed in the ring after the
 * bytes were kept (the draw and the probe both read zeros: H2's form) */
static struct {
    bool read;
    uint32_t addr;
    bool ring;
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
        s_sab.ring = strstr(e, "ring") != NULL;
        rd__log("VU skin check: ICO_RD_VU_SKIN_SABOTAGE: the bone at address %u read as zeros %s",
                s_sab.addr,
                s_sab.ring ? "in the ring after the draw's bytes were kept (the draw and the probe)"
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

/* the box check's pipelines: the draw's key without its writes, compiled
 * in the background */
#define SK_BOX_PIPES 16

typedef struct SkBoxPipe {
    bool used, failed;
    RdPipeKeyInt key;
    RhiPipelineDesc desc;
    RhiBindGroupLayout lay[4];
    RhiPipelineJobs *job;
    RhiPipeline pipe;
} SkBoxPipe;

static SkBoxPipe s_box[SK_BOX_PIPES];

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

/* c2's pipelines: begun in the background once; wait: the tool's and the
 * tests' (taken now) */
static void roPoll(bool wait)
{
    if (!rhi_limits()->uniformRobustOff || s_p.roDone) {
        return;
    }
    if (!s_p.roBegun) {
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
static RhiPipeline boxPipe(const RdPipeKeyInt *k, bool wait)
{
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
            return (RhiPipeline){0}; /* full: the keys after the first 16 go unchecked */
        }
        memset(e, 0, sizeof(*e));
        e->used = true;
        e->key = *k;
        if (!rd__pipeline_desc(k, &e->desc, e->lay)) {
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
    bool c2;     /* c2 drawn */
    bool boxRun; /* its box check drawn */
    RhiRect box; /* the box in texels */
} SkCheck;

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
} SkSlot;

static SkSlot s_slots[RHI_FRAMES_IN_FLIGHT];

/* the pick's state */
static SkCheck *s_cur;
static uint32_t s_seenReplay, s_seen, s_lastCount, s_rr;
static bool s_pickedThis;
static double s_lastMs;

/* the counts: the run's and the 10 s block's */
static struct {
    uint64_t checks, wrong, blocks, reread, box, boxOut, events, boxEvents;
} s_run;

static struct {
    uint64_t checks, wrong, blocks, reread, box, boxOut, c1Right, c2Right, c2Run;
    int32_t maxRight;
} s_blk;

static RdSkinSummary *s_collect; /* rd__vu_skin_check_frame's */

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
    k->c2 = k->boxRun = false;
    return true;
}

void rd__vu_skin_pick(const RdFrame *f, const RdCmd *c, const RdMeshRec *m)
{
    g_rdSkinArm = false;
    s_cur = NULL;
    if (!f || !c || !m || !rd__vu_skin_check_on()) {
        return;
    }
    if (s_seenReplay != g_rd.replayCounter) {
        if (s_seenReplay != 0) {
            s_lastCount = s_seen;
        }
        s_seen = 0;
        s_seenReplay = g_rd.replayCounter;
        s_pickedThis = false;
    }
    const uint32_t at = s_seen++;
    SkSlot *sl = &s_slots[rd__ring_slot()];
    if (sl->pending || g_rdWin.first || rd__pipeline_gathering() || rd__vu_prepass_running()) {
        return; /* the slot's last check not read yet; the bisect; a naming replay */
    }
    if (skMode() == 2) {
        if (at < s_allFrom || sl->n >= SK_CHECKS_ALL) {
            return;
        }
    } else {
        if (s_pickedThis || (s_lastMs != 0.0 && rd__now_ms() - s_lastMs < SK_INTERVAL_MS)) {
            return;
        }
        const uint32_t target = s_lastCount ? s_rr % s_lastCount : 0u;
        if (at < target || sl->n != 0) {
            return;
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
    rhi_cmd_begin_occlusion(cl, SK_QUERY0 - at);
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
    rhi_cmd_end_occlusion(cl, SK_QUERY0 - at);
    /* the draw's state back for whatever the replay records next */
    rhi_cmd_set_scissor(cl, &r->sc);
    rd__set_pipeline(cl, d->pipe, d->key);
    k->boxRun = true;
    k->box = clipRect(bx0, by0, bx1, by1, &all);
}

void rd__vu_skin_note(Replay *r, const DrawSetup *ds, const RdSkinDraw *d)
{
    g_rdSkinArm = false;
    SkCheck *k = s_cur;
    s_cur = NULL;
    const uint32_t slot = rd__ring_slot();
    SkSlot *sl = &s_slots[slot];
    if (!k || !d || sl->n >= sl->cap || k != &sl->checks[sl->n] || d->vs != k->vs ||
        (g_rd.vuMerge && g_rd.vsMerge[d->vs].id)) {
        return; /* not the picked draw, or one drawn through a merge build's table */
    }
    const uint8_t *ring = g_rd.ringMap[slot];
    if (!ring || d->bonesOff + SK_BONE_BYTES > g_rd.ringCap[slot] || !d->g1.id) {
        return;
    }
    sabRead();
    memcpy(k->bones, ring + d->bonesOff, SK_BONE_BYTES);
    /* the CPU's positions from the bytes the draw's block holds */
    const RdSkinCase kc = caseOf(k);
    if (!rd__skin_reference(&kc, 0, k->ref)) {
        return;
    }
    memcpy(&k->vcb, d->vcb, sizeof(k->vcb));
    k->g1 = d->g1;
    k->dOff = d->dOff;
    k->bonesOff = d->bonesOff;
    k->sabOff = d->bonesOff;
    if (s_sab.addr && s_sab.ring) {
        /* H2's form: the bytes under the GPU are not the ones kept */
        memset(g_rd.ringMap[slot] + d->bonesOff + (size_t)(s_sab.addr - 16u) * 16u, 0, 64);
    }
    boxCheck(r, ds, d, k, sl->n);
    sl->n++;
    s_pickedThis = true;
}

uint64_t rd__vu_skin_ring_bytes(void)
{
    if (!rd__vu_skin_check_on()) {
        return 0;
    }
    const uint64_t ua = rhi_limits()->uniformAlign ? rhi_limits()->uniformAlign : 256u;
    const uint64_t one = SK_COLS * (sizeof(IcoFrameCB) + ua) + 2u * (sizeof(IcoVuCB) + ua) +
                         2u * (SK_BONE_BYTES + 4096u + ua);
    return (skMode() == 2 ? SK_CHECKS_ALL : 1u) * one + 4096u;
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

/* a block into the ring at an offset of ua (~0ull: full) */
static uint64_t ringPut(const void *data, uint64_t size, uint64_t align)
{
    const uint64_t off = rd__ring_alloc(size, align);
    if (off != ~0ull) {
        memcpy(g_rd.ringMap[rd__ring_slot()] + off, data, size);
    }
    return off;
}

/* c1's block: the CPU's bytes at a fresh ring offset whose 4 KB page phase
 * is not the draw's */
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

void rd__vu_skin_end(void)
{
    g_rdSkinArm = false;
    s_cur = NULL;
    const uint32_t slot = rd__ring_slot();
    SkSlot *sl = &s_slots[slot];
    if (sl->pending || sl->n == 0) {
        return;
    }
    const uint32_t n = sl->n;
    const RhiCommandList cl = rd__replay_cl();
    const uint64_t ua = rhi_limits()->uniformAlign ? rhi_limits()->uniformAlign : 256u;
    if (!cl.id || !indexMake() || !slotEnsure(sl, n * SK_COLS, n)) {
        sl->n = 0;
        return;
    }
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
        return;
    }
    /* each check's blocks first: c1's bytes, the probe's VuCB, its FrameCBs */
    uint64_t vOff[SK_CHECKS_ALL], fOff[SK_CHECKS_ALL][SK_COLS];
    bool ok = true;
    for (uint32_t i = 0; i < n && ok; i++) {
        SkCheck *k = &sl->checks[i];
        k->c1Off = ringFresh(k->bones, k->bonesOff, ua);
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
        k->c2 = ro.id != 0;
        for (uint32_t c = 0; c < SK_COLS; c++) {
            const RhiPipeline p = c == 2 ? ro : main;
            if (!p.id) {
                continue;
            }
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
    sl->counter = g_rd.replayCounter;
    sl->rhiSlot = rhi_frame_slot();
    sl->ringId = g_rd.ring[slot].id;
    s_rr++;
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
    uint32_t q0 = 0, q1 = 0, r0 = 0, r1 = 0;
    const bool copyDiff =
        rd__skin_bytes_differ(map + (size_t)i * SK_BONE_BYTES, k->bones, SK_BONE_BYTES, &q0, &q1);
    const bool reread = intact && ringNow;
    const bool rereadDiff =
        reread && rd__skin_bytes_differ(ringNow + k->bonesOff, k->bones, SK_BONE_BYTES, &r0, &r1);
    const int c1 = varState(k, &v[1], true), c2 = varState(k, &v[2], k->c2);
    const char *decision = rd__skin_decision(copyDiff, rereadDiff, v[0].wrong != 0, c1, c2);
    /* the box check */
    const uint32_t qi = SK_QUERY0 - i;
    const bool boxRead = k->boxRun && samples && qi < nSamples && samples[qi] != RHI_OCCLUSION_NONE;
    const uint64_t outside = boxRead ? samples[qi] : 0u;
    /* the counts */
    s_run.checks++;
    s_blk.checks++;
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
        /* the address and the words of the last wrong check; an event
         * whose probe drew right (its bytes differed, nothing it reads
         * moved: a block whose changed bone the mesh does not use) gives
         * its words only while no wrong check has */
        if (v[0].wrong) {
            o->lastAddr = v[0].addr;
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
        if (sayNth(++s_run.events)) {
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
                 * most wrong vertices, else the first wrong vertex's lane x */
                const uint32_t a = v[0].addr ? v[0].addr : v[0].lanes[0];
                rd__log("VU skin check: frame %u command %s mesh %s #%u (%u vertices): %u "
                        "vertices wrong of %u, address %u: got (%.1f, %.1f, %d) want (%.1f, "
                        "%.1f, %d) (a zero matrix at %u gives it: %s; it gives %u of them); the "
                        "bone block as the GPU copied it: %s; as the CPU reads it after the "
                        "fence: %s; code %d through %s (event %llu)",
                        k->frame, place, k->mesh, k->meshId, k->meshVertices, v[0].wrong,
                        v[0].compared, a, v[0].got[0] / 16.0, v[0].got[1] / 16.0, v[0].got[2],
                        v[0].want.x / 16.0, v[0].want.y / 16.0, v[0].want.z, a,
                        v[0].explains ? "yes" : "no", v[0].explained, cw, rw, k->code, mod,
                        (unsigned long long)s_run.events);
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
                        "the CPU reads it after the fence: %s; reads as: %s (event %llu)",
                        k->frame, place, k->mesh, k->meshId, k->meshVertices, v[0].compared, cw, rw,
                        decision, (unsigned long long)s_run.events);
            }
        }
    }
    if (outside && sayNth(++s_run.boxEvents)) {
        rd__log("VU skin check: frame %u command %s drew %llu samples outside its box (texels "
                "%d,%d %ux%u, the CPU's vertices + 2 GS pixels; the probe %s) (event %llu)",
                k->frame, place, (unsigned long long)outside, k->box.x, k->box.y, k->box.w,
                k->box.h, v[0].wrong ? "wrong too" : "right", (unsigned long long)s_run.boxEvents);
    }
}

static void readSlot(SkSlot *sl, uint32_t slot, bool intact, bool occ)
{
    const uint8_t *map = sl->rb.id ? rhi_map_buffer(sl->rb) : NULL;
    if (map) {
        static uint64_t samples[RHI_MAX_OCCLUSION];
        uint32_t ns = 0;
        bool anyBox = false;
        for (uint32_t i = 0; i < sl->n; i++) {
            anyBox = anyBox || sl->checks[i].boxRun;
        }
        if (anyBox && occ) {
            ns = rhi_read_occlusion(samples, RHI_MAX_OCCLUSION);
        }
        const uint8_t *ringNow = intact ? g_rd.ringMap[slot] : NULL;
        for (uint32_t i = 0; i < sl->n; i++) {
            readCheck(sl, map, i, intact, ringNow, ns ? samples : NULL, ns);
        }
        rhi_unmap_buffer(sl->rb);
    }
    sl->pending = false;
    sl->n = 0;
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
    if (!s_blk.checks) {
        return;
    }
    rd__log("VU skin check: %llu checks, %llu wrong, %llu blocks whose bytes differed (%llu "
            "changed after the draw); %llu box checks, %llu with samples outside the box; "
            "variants on the wrong ones: c1 right %llu, c2 right %llu (c2 drawn in %llu checks); "
            "largest difference on a right vertex %d/16 pixel",
            (unsigned long long)s_blk.checks, (unsigned long long)s_blk.wrong,
            (unsigned long long)s_blk.blocks, (unsigned long long)s_blk.reread,
            (unsigned long long)s_blk.box, (unsigned long long)s_blk.boxOut,
            (unsigned long long)s_blk.c1Right, (unsigned long long)s_blk.c2Right,
            (unsigned long long)s_blk.c2Run, s_blk.maxRight);
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

void rd__vu_skin_shutdown(void)
{
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
    s_seenReplay = s_seen = s_lastCount = s_rr = 0;
    s_pickedThis = false;
    s_lastMs = 0.0;
    s_env = -2;
    s_dev = -1;
}
