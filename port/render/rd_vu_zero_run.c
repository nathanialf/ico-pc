/* rd_vu_zero_run.c: the zero-area cull in the renderer (rd_vu_zero.h): its
 * state for the run, the pre-pass entries' lattice words, the draws'
 * counts, the record's checks read back and the lines.
 *
 * The state: off (ICO_RD_VU_ZERO=0, the loss ladder, the install's mark, a
 * failed self-test or check), held (until the start-up self-test passes: no
 * word is given, no module or pipeline of the cull exists, the record
 * buffer is made as without it) or on.  ICO_RD_VU_ZERO=1 (tests) starts on
 * and checks every entry of every replay.
 *
 * Trying it (rd__vu_zero_try_begin, the self-test's first step): the mark
 * "trying" written and synced to ICO_VU_ZERO_MARK's file (the platform's,
 * port/platform/vu_zero_mark.h, read at the next start), then the rule
 * stage's zero build of the record's mode and batches made (one module) and
 * the record buffers made again copyable for the self-test's read back; at
 * its end they are made again as without the cull; a failed self-test
 * writes "failed" and releases the module and its pipeline;
 * RD_VU_ZERO_PROBATION pictures drawn with it write "on".
 *
 * The checks (the self-test's and the tests' ICO_RD_VU_ZERO=1 alone; none
 * in play): the slot's record buffer is copied after pass T for every entry
 * (rd__vu_zero_copy: slot A's flags of its kicks, planes 1 and 2 of its
 * vertices) into the frame slot's readback buffer, read once the GPU is
 * done (rd__vu_zero_collect_now, or the slot coming round) and compared
 * triangle by triangle with the CPU's answer (rd_vu_zero_check).
 *
 *   rd: VU zero-area cull: off for this start (the loss ladder: the previous
 *     start lost the graphics device, ICO_LOSS_ROW 1; nothing written to
 *     the settings)
 *   rd: VU zero-area cull: off for the run: the GPU's answer differs from
 *     the CPU's in a check of replay N (triangle slot j: the GPU culled it,
 *     the CPU keeps it (...))
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h> /* _commit */
#else
#include <unistd.h> /* fsync */
#endif

#include "rd_internal.h"
#include "rd_jitter.h"
#include "rd_vu_record.h"
#include "rd_vu_zero.h"
#include "shader_consts.h"

enum { Z_HELD, Z_ON, Z_OFF };

/* ICO_RD_VU_ZERO_SABOTAGE (tests): the GPU's word half a texel off, the
   run-time check's answers from no word (every culled triangle differs), no
   record copied */
enum { SB_NONE, SB_LATTICE, SB_CHECK, SB_MISSING };

/* one entry's copy in a slot's readback buffer: its slot A words at aAt,
   its vertices' planes 1 and 2 at v1At and v2At (bytes) */
typedef struct ZCopy {
    uint64_t aAt, v1At, v2At;
    uint32_t kicks, mode, word;
} ZCopy;

typedef struct ZSlot {
    RhiBuffer rb;
    uint64_t bytes, used;
    ZCopy *copies;
    uint32_t n, cap;
    uint32_t replay; /* g_rd.replayCounter of its copies */
} ZSlot;

static struct {
    int state;
    bool forced; /* ICO_RD_VU_ZERO=1 */
    bool held;   /* rd__vu_zero_hold: the self-test's frames */
    int heldState;
    int sabotage;
    bool heldAll;
    char why[384];
    bool tried;          /* rd__vu_zero_try_begin ran, not released */
    bool copyNow;        /* the record buffers copyable (the self-test's read back) */
    int madeKind;        /* the module made: zeroKind() when it was, -1 none */
    RhiShader module;    /* its module */
    uint32_t variants;   /* modules of the cull made this process (the tests) */
    uint32_t pictures;   /* pictures drawn with a word since the self-test */
    bool markedOn;       /* "on" written */
    char markPath[1024]; /* ICO_VU_ZERO_MARK: the platform's mark file, "" none */
    ZSlot slot[RHI_FRAMES_IN_FLIGHT];
    RdVuZeroCheck took; /* since rd__vu_zero_take */
} s_z;

/* the counts: the block's (the window's 10 s line takes them) and the
   run's (the replay tool's stats); written on the render thread, read from
   the window's (atomics) */
typedef struct ZCounts {
    uint64_t draws, tris, refused[RD_VU_ZERO_REASONS], shared;
    uint64_t pictures, slots, culled, mismatches;
} ZCounts;

static ZCounts s_block, s_run;

static void add(uint64_t *a, uint64_t *b, uint64_t v)
{
    __atomic_fetch_add(a, v, __ATOMIC_RELAXED);
    __atomic_fetch_add(b, v, __ATOMIC_RELAXED);
}

static void setOff(const char *why)
{
    s_z.state = Z_OFF;
    snprintf(s_z.why, sizeof(s_z.why), "%s", why);
}

/* off for the run (a failed self-test or check): kept through a renderer
   made again in the same process (rd__vu_zero_init) */
static char s_runOff[384];

static void setRunOff(const char *why)
{
    setOff(why);
    snprintf(s_runOff, sizeof(s_runOff), "%s", s_z.why);
}

/* the mark (port/platform/vu_zero_mark.h): written whole, synced where
   asked (trying, failed: before the step they guard; on: not synced, a
   lost "on" only keeps a "trying" the next start reads with the run's end) */
static bool writeMark(const char *word, bool sync)
{
    if (s_z.markPath[0] == '\0') {
        return true; /* no platform's mark (the replay tool, the tests) */
    }
    FILE *f = fopen(s_z.markPath, "wb");
    if (f == NULL) {
        rd__log("VU zero-area cull: its mark %s could not be written (%s)", s_z.markPath, word);
        return false;
    }
    bool ok = fprintf(f, "%s\n", word) > 0;
    ok = fflush(f) == 0 && ok;
    if (sync) {
#ifdef _WIN32
        (void)_commit(_fileno(f));
#else
        (void)fsync(fileno(f));
#endif
    }
    return fclose(f) == 0 && ok;
}

void rd__vu_zero_init(void)
{
    memset(&s_z.took, 0, sizeof(s_z.took));
    s_z.tried = false;
    s_z.copyNow = false;
    s_z.madeKind = -1;
    s_z.module = (RhiShader){0}; /* freed with the renderer's sets */
    s_z.pictures = 0;
    s_z.markedOn = false;
    const char *mark = getenv("ICO_VU_ZERO_MARK");
    snprintf(s_z.markPath, sizeof(s_z.markPath), "%s", mark != NULL ? mark : "");
    const char *block = getenv("ICO_VU_ZERO_BLOCK");
    s_z.held = false;
    s_z.forced = false;
    s_z.state = Z_HELD;
    snprintf(s_z.why, sizeof(s_z.why), "until its start-up self-test passes");
    const char *sb = getenv("ICO_RD_VU_ZERO_SABOTAGE");
    s_z.sabotage = SB_NONE;
    if (sb != NULL && strcmp(sb, "lattice") == 0) {
        s_z.sabotage = SB_LATTICE;
    } else if (sb != NULL && strcmp(sb, "check") == 0) {
        s_z.sabotage = SB_CHECK;
    } else if (sb != NULL && strcmp(sb, "missing") == 0) {
        s_z.sabotage = SB_MISSING;
    }
    if (s_z.sabotage != SB_NONE) {
        static const char *const kWhat[] = {"", "the rule stage given a lattice half a texel off",
                                            "the run-time check's answers from no lattice",
                                            "no record copied for the checks"};
        rd__log("VU zero-area cull: ICO_RD_VU_ZERO_SABOTAGE: %s", kWhat[s_z.sabotage]);
    }
    const char *env = getenv("ICO_RD_VU_ZERO");
    const char *row = getenv("ICO_LOSS_ROW");
    const long lost = row != NULL ? strtol(row, NULL, 10) : 0;
    if (s_runOff[0] != '\0') {
        setOff(s_runOff); /* said when it was turned off */
    } else if (block != NULL && block[0] != '\0') {
        /* the install's mark (port/platform/vu_zero_mark.h): a run of this
           version ended while the cull was tried, or its self-test failed */
        char why[320];
        snprintf(why, sizeof(why), "on this install (%.280s)", block);
        setOff(why);
        rd__log("VU zero-area cull: off %s", s_z.why);
    } else if (env != NULL && strcmp(env, "0") == 0) {
        setOff("ICO_RD_VU_ZERO=0");
    } else if (lost > 0) {
        /* the loss ladder (port/rhi/rhi_tile_rule.h; port/platform/
           diag_host.c counts a lost device and a hung driver): every
           device, this start only */
        char why[256];
        snprintf(why, sizeof(why),
                 "for this start (the loss ladder: %ld start%s in a row before this one lost the "
                 "graphics device or hung its driver, ICO_LOSS_ROW %ld; nothing written to the "
                 "settings)",
                 lost, lost == 1 ? "" : "s", lost);
        setOff(why);
        rd__log("VU zero-area cull: off %s", s_z.why);
    } else if (env != NULL && strcmp(env, "1") == 0) {
        s_z.state = Z_ON;
        s_z.forced = true;
        snprintf(s_z.why, sizeof(s_z.why), "ICO_RD_VU_ZERO=1");
        rd__log("VU zero-area cull: on without its self-test (ICO_RD_VU_ZERO=1): every entry "
                "of every replay checked against the CPU's answers");
    }
}

bool rd__vu_zero_on(void)
{
    return s_z.state == Z_ON;
}

const char *rd__vu_zero_state_text(void)
{
    return s_z.state == Z_ON ? "on" : s_z.state == Z_HELD ? "held" : "off";
}

bool rd__vu_zero_copyable(void)
{
    return s_z.copyNow || s_z.forced;
}

/* the self-test's read back over: the record buffers made again as without
   the cull (at the self-test's rows, before the play record is made) */
static void copyEnd(void)
{
    if (s_z.copyNow) {
        s_z.copyNow = false;
        if (!s_z.forced) {
            rd__vu_rec_usage_changed();
        }
    }
}

/* the record's mode and batches as a number (the module they take) */
static int zeroKind(void)
{
    return (rd__vu_record_compute()    ? 4
            : rd__vu_record_buffered() ? 2
                                       : 0) +
           (g_rd.vuPreBatch ? 1 : 0);
}

/* the set the pipelines read the zero build of kind from (rd_pipeline.c
   pipelineDesc: the targets' g_rd.vs / vsBatch, the stores', the compute's) */
static RhiShader *zeroSlot(int kind)
{
    switch (kind) {
    case 0:
        return &g_rd.vs[RD_VS_VU_TRI_RULE_ZERO];
    case 1:
        return &g_rd.vsBatch[RD_VS_VU_TRI_RULE_ZERO];
    case 2:
        return &g_rdRec.vsStore[RD_VS_VU_TRI_RULE_ZERO];
    case 3:
        return &g_rdRec.vsBatchStore[RD_VS_VU_TRI_RULE_ZERO];
    case 4:
        return &g_rdRec.csCompute[RD_VS_VU_TRI_RULE_ZERO];
    default:
        return &g_rdRec.csBatchCompute[RD_VS_VU_TRI_RULE_ZERO];
    }
}

static const char *const kZeroNames[6] = {
    "vu_tri_rule_zero_pre_vs",         "vu_tri_rule_zero_batch_pre_vs",
    "vu_tri_rule_zero_store_pre_vs",   "vu_tri_rule_zero_batch_store_pre_vs",
    "vu_tri_rule_zero_compute_pre_cs", "vu_tri_rule_zero_batch_compute_pre_cs"};

/* the zero build of the record's mode now exists (made, and the mode not
   changed since: a fall back between gives no word) */
static bool zeroReady(void)
{
    if (s_z.forced && s_z.madeKind < 0 && s_z.state == Z_ON) {
        (void)rd__vu_zero_try_begin(); /* the tests' =1: made at the first need */
    }
    return s_z.madeKind >= 0 && s_z.madeKind == zeroKind() && zeroSlot(s_z.madeKind)->id != 0;
}

bool rd__vu_zero_try_begin(void)
{
    if (s_z.state == Z_OFF || !g_rd.hasDevice) {
        return false;
    }
    if (s_z.madeKind >= 0) {
        return zeroSlot(s_z.madeKind)->id != 0;
    }
    /* the mark first: a crash or a hang inside the compile or the
       self-test leaves it for the next start (port/platform/vu_zero_mark.h) */
    if (!writeMark("trying", true)) {
        return false; /* no way back after a crash without it: not tried */
    }
    const int kind = zeroKind();
    RhiShader *slot = zeroSlot(kind);
    *slot = rd__make_shader(kZeroNames[kind]);
    s_z.variants++;
    if (!slot->id) {
        rd__log("VU zero-area cull: %s could not be made", kZeroNames[kind]);
        return false;
    }
    s_z.madeKind = kind;
    s_z.module = *slot;
    s_z.tried = true;
    s_z.copyNow = true;
    rd__vu_rec_usage_changed(); /* the record buffers made again copyable */
    rd__log("VU zero-area cull: trying it: %s made (1 module; its one pipeline with the "
            "self-test's, at the start), the record buffers copyable for the self-test's read "
            "back alone",
            kZeroNames[kind]);
    return true;
}

/* the module and its pipeline released, the buffers made again as before */
static void zeroRelease(void)
{
    if (s_z.madeKind >= 0 && g_rd.hasDevice) {
        rhi_wait_idle();
        (void)rd__pipeline_drop_vs(RD_VS_VU_TRI_RULE_ZERO);
        RhiShader *slot = zeroSlot(s_z.madeKind);
        if (slot->id) {
            rhi_destroy_shader(*slot);
        }
        *slot = (RhiShader){0};
    }
    s_z.madeKind = -1;
    s_z.module = (RhiShader){0};
    if (s_z.tried) {
        s_z.tried = false;
        copyEnd();
        rd__log("VU zero-area cull: its module and pipeline released, the record buffers made "
                "again as without it");
    }
}

void rd__vu_zero_start_line(void)
{
    rd__log("VU zero-area cull: %s, %u modules of it made, %u of its pipelines alive",
            rd__vu_zero_state_text(), (unsigned)s_z.variants,
            (unsigned)rd__pipeline_count_vs(RD_VS_VU_TRI_RULE_ZERO));
}

uint32_t rd__vu_zero_variants_made(void)
{
    return s_z.variants;
}

uint8_t rd__vu_zero_rule_vs(uint32_t word)
{
    /* with the cull on every entry takes the zero build (an entry without a
       word skips the test: its slots are the build's without it), so the
       pre-pass's batches are the ones it had: one rule stage key, the same
       draws and dispatches; else the build without it */
    (void)word;
    return s_z.state == Z_ON && zeroReady() ? (uint8_t)RD_VS_VU_TRI_RULE_ZERO
                                            : (uint8_t)RD_VS_VU_TRI_RULE_PRE;
}

uint32_t rd__vu_zero_entry_keep(uint32_t word, bool skinned, bool velocity, bool probe, int *why)
{
    if (word == 0u) {
        return 0u;
    }
    /* every reader of the entry's slot A culls a marked triangle (the
       velocity pass's VELOCITY, the skin check's twins and box on their own
       targets: not tested) */
    if (velocity) {
        *why = RD_VU_ZERO_VELOCITY;
    } else if (skinned || probe) {
        *why = RD_VU_ZERO_ENTRY;
    } else {
        return word;
    }
    return 0u;
}

void rd__vu_zero_picture(bool culling)
{
    if (!culling || s_z.state != Z_ON || s_z.held || s_z.markedOn) {
        return;
    }
    if (++s_z.pictures >= RD_VU_ZERO_PROBATION) {
        s_z.markedOn = true;
        (void)writeMark("on", false);
        rd__log("VU zero-area cull: %u pictures drawn with it since its self-test: its mark "
                "says on",
                (unsigned)RD_VU_ZERO_PROBATION);
    }
}

uint32_t rd__vu_zero_entry_word(const RdStateBlock *st, int *why)
{
    *why = RD_VU_ZERO_OFF;
    if (s_z.state != Z_ON || !zeroReady()) {
        return 0;
    }
    const RdTargetRec *tc = rd__target_rec(st->color);
    if (tc == NULL || !tc->color.id) {
        *why = RD_VU_ZERO_MAPPING;
        return 0;
    }
    /* multisampled passes (the MSAA epoch; the lamps' pass into WORK2 may
       be one, rd_lamps.h): the sample points are not the texel centres */
    if (rd__aa_mode() >= RD_AA_MSAA2 || st->color == rd_target(RD_TARGET_WORK2).id) {
        *why = RD_VU_ZERO_MSAA;
        return 0;
    }
    /* rd_replay.c rd__replay_vu_frame's FrameCB: origin and target */
    float oy = 2048.0f - (float)(st->gsH >> 1);
    if (st->useOffset & RD_TARGET_HALF_Y) {
        oy += 0.5f;
    }
    float jitter[2];
    RdVuZeroMap m;
    memset(&m, 0, sizeof(m));
    m.w = tc->w;
    m.h = tc->h;
    m.tw = tc->tw;
    m.th = tc->th;
    m.sx = tc->sx;
    m.sy = tc->sy;
    m.cropOx = tc->ox;
    m.originX = 2048.0f - (float)(st->gsW >> 1);
    m.originY = oy;
    m.extentMax = g_rd.hasDevice ? rhi_limits()->maxRenderTargetSize : 16384u;
    m.exact = true;   /* the pre-pass runs in replayFrame's lists (the exact viewport) */
    m.noHalf = false; /* the MSAA epoch's draws are refused at the draw (passSamples) */
    m.jitter = rd__frame_jitter_get(jitter);
    m.offGrid = rd__vu_off_grid() && g_rd.settings.preset == RD_PRESET_ENHANCED &&
                (tc->sx > 1.0f || tc->sy > 1.0f);
    return rd_vu_zero_word(&m, why);
}

uint32_t rd__vu_zero_gpu_word(uint32_t word)
{
    return word != 0u && s_z.sabotage == SB_LATTICE ? word ^ 8u : word;
}

void rd__vu_zero_shared_given_up(void)
{
    add(&s_block.shared, &s_run.shared, 1u);
}

void rd__vu_zero_draw(bool allowed, int why, uint32_t tris)
{
    if (allowed) {
        add(&s_block.draws, &s_run.draws, 1u);
        add(&s_block.tris, &s_run.tris, tris);
    } else if (why >= 0 && why < RD_VU_ZERO_REASONS) {
        add(&s_block.refused[why], &s_run.refused[why], 1u);
    }
}

/* ------------------------------------------------------------ the checks */

static void fault(const char *what)
{
    if (s_z.state == Z_OFF) {
        return;
    }
    char why[384];
    snprintf(why, sizeof(why), "for the run: %.360s", what);
    setRunOff(why);
    rd__log("VU zero-area cull: off %s", s_z.why);
    (void)writeMark("failed", true);
}

void rd__vu_zero_fault_msaa(void)
{
    fault("a draw of a culling entry in a multisampled pass");
}

/* the slot's copies read (their frame done) */
static void slotRead(uint32_t slot)
{
    ZSlot *z = &s_z.slot[slot];
    if (z->n == 0u || !z->rb.id) {
        z->n = 0;
        return;
    }
    const uint8_t *m = rhi_map_buffer(z->rb);
    if (m == NULL) {
        z->n = 0;
        return;
    }
    RdVuZeroCheck c;
    memset(&c, 0, sizeof(c));
    for (uint32_t i = 0; i < z->n; i++) {
        const ZCopy *k = &z->copies[i];
        rd_vu_zero_check((const uint32_t *)(m + k->aAt), (const uint32_t *)(m + k->v1At),
                         (const uint32_t *)(m + k->v2At), k->kicks, k->mode, k->word, &c);
    }
    rhi_unmap_buffer(z->rb);
    z->n = 0;
    add(&s_block.pictures, &s_run.pictures, 1u);
    add(&s_block.slots, &s_run.slots, c.slots);
    add(&s_block.culled, &s_run.culled, c.culled);
    add(&s_block.mismatches, &s_run.mismatches, c.mismatches);
    s_z.took.slots += c.slots;
    s_z.took.eligible += c.eligible;
    s_z.took.culled += c.culled;
    if (c.mismatches && s_z.took.mismatches++ == 0u) {
        snprintf(s_z.took.first, sizeof(s_z.took.first), "%s", c.first);
    }
    if (c.mismatches && !s_z.held) {
        char what[320];
        snprintf(what, sizeof(what),
                 "the GPU's answer differs from the CPU's in %llu of %llu triangle slots of "
                 "replay %u (%.180s)",
                 (unsigned long long)c.mismatches, (unsigned long long)c.slots, (unsigned)z->replay,
                 c.first);
        fault(what);
    }
}

void rd__vu_zero_slot_begin(uint32_t slot)
{
    if (slot < RHI_FRAMES_IN_FLIGHT) {
        slotRead(slot);
    }
}

void rd__vu_zero_collect_now(void)
{
    bool any = false;
    for (uint32_t s = 0; s < RHI_FRAMES_IN_FLIGHT; s++) {
        any = any || s_z.slot[s].n != 0u;
    }
    if (!any || !g_rd.hasDevice) {
        return;
    }
    rhi_wait_idle();
    for (uint32_t s = 0; s < RHI_FRAMES_IN_FLIGHT; s++) {
        slotRead(s);
    }
}

bool rd__vu_zero_check_now(bool *all)
{
    *all = false;
    if (s_z.state != Z_ON) {
        return false;
    }
    /* the self-test's frames and the tests' =1 alone: no read back in play */
    if (s_z.forced || (s_z.held && s_z.heldAll)) {
        *all = true;
        return true;
    }
    return false;
}

/* the slot's readback buffer holds at least bytes (grown: a copy not read
   yet is lost with it) */
static bool slotRoom(ZSlot *z, uint64_t bytes)
{
    if (z->bytes >= bytes) {
        return true;
    }
    if (z->rb.id) {
        rhi_destroy_buffer(z->rb);
    }
    uint64_t size = 65536u;
    while (size < bytes) {
        size *= 2u;
    }
    z->rb = rhi_create_buffer(&(RhiBufferDesc){size, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                               RHI_MEM_READBACK, "rd vu zero-area checks"});
    z->bytes = z->rb.id ? size : 0u;
    z->n = 0;
    z->used = 0;
    return z->rb.id != 0;
}

void rd__vu_zero_copy(RhiCommandList cl, uint32_t slot, RhiBuffer rec, uint64_t plane,
                      uint32_t base, uint32_t count, uint32_t slotA, uint32_t mode, uint32_t word)
{
    if (slot >= RHI_FRAMES_IN_FLIGHT || !rec.id || count < 3u || s_z.state != Z_ON ||
        s_z.sabotage == SB_MISSING) {
        return;
    }
    ZSlot *z = &s_z.slot[slot];
    if (z->n != 0u && z->replay != g_rd.replayCounter) {
        z->n = 0; /* an earlier replay's, never read (no frame between) */
    }
    if (z->n == 0u) {
        z->used = 0;
        z->replay = g_rd.replayCounter;
    }
    const bool all = s_z.forced || (s_z.held && s_z.heldAll);
    if (!all) {
        return; /* no read back in play (rd__vu_zero_check_now) */
    }
    const uint32_t kicks = count - 2u;
    const uint64_t aBytes = (uint64_t)kicks * 16u, vBytes = (uint64_t)(kicks + 2u) * 16u;
    const uint64_t need = z->used + aBytes + 2u * vBytes;
    if (z->n == z->cap) {
        const uint32_t cap = z->cap ? z->cap * 2u : 64u;
        ZCopy *grown = realloc(z->copies, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        z->copies = grown;
        z->cap = cap;
    }
    /* every entry (the tests', the self-test's): room for the whole record's
       three planes at the first, which every entry's copies fit in */
    if (z->n == 0u && !slotRoom(z, all && need < 48u * plane ? 48u * plane : need)) {
        return;
    }
    if (need > z->bytes) {
        return; /* this replay's copies fill it: the rest unchecked */
    }
    ZCopy *k = &z->copies[z->n];
    k->aAt = z->used;
    k->v1At = k->aAt + aBytes;
    k->v2At = k->v1At + vBytes;
    k->kicks = kicks;
    k->mode = mode;
    k->word = s_z.sabotage == SB_CHECK && !s_z.held ? 0u : word;
    /* plane 0's slot A texels, planes 1 and 2 of the vertex texels */
    rhi_cmd_copy_buffer(cl, rec, (uint64_t)slotA * 16u, z->rb, k->aAt, aBytes);
    rhi_cmd_copy_buffer(cl, rec, (plane + base) * 16u, z->rb, k->v1At, vBytes);
    rhi_cmd_copy_buffer(cl, rec, (2u * plane + base) * 16u, z->rb, k->v2At, vBytes);
    z->used = need;
    z->n++;
}

/* --------------------------------------------------- the self-test's hold */

void rd__vu_zero_hold(bool on)
{
    if (!s_z.held) {
        s_z.heldState = s_z.state;
    }
    s_z.held = true;
    s_z.heldAll = on;
    s_z.state = on ? Z_ON : Z_HELD;
}

void rd__vu_zero_hold_end(void)
{
    if (s_z.held) {
        s_z.state = s_z.heldState;
    }
    s_z.held = false;
    s_z.heldAll = false;
}

void rd__vu_zero_take(RdVuZeroCheck *out)
{
    *out = s_z.took;
    memset(&s_z.took, 0, sizeof(s_z.took));
}

void rd__vu_zero_verdict(bool pass, const char *why)
{
    if (s_z.state == Z_OFF || s_z.forced) {
        return; /* off before it (the environment, the ladder), or the tests' own */
    }
    if (pass) {
        s_z.state = Z_ON;
        s_z.why[0] = '\0';
        s_z.pictures = 0;
        copyEnd(); /* play's record buffers as without the cull */
    } else {
        char w[200];
        snprintf(w, sizeof(w), "for the run (its self-test: %.150s)", why);
        setRunOff(w);
        if (s_z.tried) {
            (void)writeMark("failed", true); /* the install's: off until the version changes */
        }
        zeroRelease();
    }
}

void rd__vu_zero_selftest_skip(const char *why)
{
    if (s_z.state == Z_HELD) {
        char w[200];
        snprintf(w, sizeof(w), "for the run (self-test not run: %.140s)", why);
        setOff(w); /* not a fault: a renderer made again tests it */
        rd__log("VU zero-area cull: off %s", s_z.why);
    }
}

/* -------------------------------------------------------------- the lines */

static uint64_t takeN(uint64_t *v)
{
    return __atomic_exchange_n(v, 0u, __ATOMIC_RELAXED);
}

size_t rd_vu_zero_text(char *out, size_t cap, unsigned n)
{
    if (cap == 0) {
        return 0;
    }
    ZCounts c;
    c.draws = takeN(&s_block.draws);
    c.tris = takeN(&s_block.tris);
    for (int i = 0; i < RD_VU_ZERO_REASONS; i++) {
        c.refused[i] = takeN(&s_block.refused[i]);
    }
    c.shared = takeN(&s_block.shared);
    c.pictures = takeN(&s_block.pictures);
    c.slots = takeN(&s_block.slots);
    c.culled = takeN(&s_block.culled);
    c.mismatches = takeN(&s_block.mismatches);
    if (s_z.state != Z_ON) {
        return (size_t)snprintf(out, cap, "; zero-area cull %s (%s)", rd__vu_zero_state_text(),
                                s_z.why);
    }
    const double d = n ? (double)n : 1.0;
    int at = snprintf(out, cap, "; zero-area cull: %.0f draws a picture culling, %.0f triangles",
                      (double)c.draws / d, (double)c.tris / d);
    bool any = false;
    for (int i = 0; i < RD_VU_ZERO_REASONS && at > 0 && (size_t)at < cap; i++) {
        if (c.refused[i] != 0u) {
            at += snprintf(out + at, cap - (size_t)at, "%s%s %.0f",
                           any ? ", " : "; refused a picture: ", rd_vu_zero_reason_name(i),
                           (double)c.refused[i] / d);
            any = true;
        }
    }
    if (c.shared != 0u && at > 0 && (size_t)at < cap) {
        at += snprintf(out + at, cap - (size_t)at, "; shared lists given up a picture: %.0f",
                       (double)c.shared / d);
    }
    if (c.pictures != 0u && at > 0 && (size_t)at < cap) {
        at += snprintf(out + at, cap - (size_t)at,
                       "; checked %llu pictures: %llu of %llu triangle slots culled, %llu differ",
                       (unsigned long long)c.pictures, (unsigned long long)c.culled,
                       (unsigned long long)c.slots, (unsigned long long)c.mismatches);
    }
    return at < 0 ? 0u : ((size_t)at < cap ? (size_t)at : cap - 1u);
}

int rd__vu_zero_stats_text(char *out, size_t cap)
{
    rd__vu_zero_collect_now();
    const ZCounts *c = &s_run;
    char refused[256];
    int at = 0;
    refused[0] = '\0';
    for (int i = 0; i < RD_VU_ZERO_REASONS && at >= 0 && (size_t)at < sizeof(refused); i++) {
        at += snprintf(refused + at, sizeof(refused) - (size_t)at, "%s%s %llu", i ? ", " : "",
                       rd_vu_zero_reason_name(i), (unsigned long long)c->refused[i]);
    }
    return snprintf(out, cap,
                    "VU zero-area cull %s%s%s%s: %llu of %llu triangle slots culled over %llu "
                    "pictures checked, %llu differ; %llu draws culling (%llu triangles, %llu "
                    "shared lists given up); refused: %s; zero-area modules made %u",
                    rd__vu_zero_state_text(), s_z.why[0] ? " (" : "", s_z.why,
                    s_z.why[0] ? ")" : "", (unsigned long long)c->culled,
                    (unsigned long long)c->slots, (unsigned long long)c->pictures,
                    (unsigned long long)c->mismatches, (unsigned long long)c->draws,
                    (unsigned long long)c->tris, (unsigned long long)c->shared, refused,
                    (unsigned)s_z.variants);
}
