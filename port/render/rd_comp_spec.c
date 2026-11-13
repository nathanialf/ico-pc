/* rd_comp_spec.c: the shadow composites and levels drawn by their own entry,
 * sprite_comp_ps (rd_comp_spec.h says what and why; docs/port/RENDER_POST.md,
 * "The shadow composites by their own entry").
 *
 *   rd_comp_spec_key_form, rd_comp_spec_flags_why  the gate, pure
 *   rd__comp_spec_note   rd_pixel_plan.c rd__fill_draw_cb_extra: a draw of the
 *                        forms counted, and noted for its run where the gate
 *                        takes it
 *   rd__comp_spec_turn   rd_pipeline.c rd__set_pipeline: the twin's pipeline
 *                        bound when the noted draw's run is drawn, counted per
 *                        replay, run and 10 s block, and in the attempt ledger
 *   rd__comp_spec_selftest  the forms drawn both ways on SCENE, compared byte
 *                        for byte (the file's end)
 *
 * The module is made here (rd__comp_spec_init, from rd__lamps_init after
 * rd__gpu_init) into g_rd.fs[RD_FS_SPRITE_COMP], an entry past the name
 * table's count (rd_internal.h RD_FS_SLOTS), as rd_paste_spec.c does:
 * rd_replay.c is at its size cap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_bench.h" /* the bench's DATE set-up switch, defined here */
#include "rd_comp_spec.h"
#include "rd_replay.h"
#include "shader_consts.h"
#include "shaders_gen.h"

_Static_assert(RD_COMP_SPEC_READ ==
                   ((uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_DECAL | (uint32_t)ICO_DF_TCC_RGBA |
                    (uint32_t)ICO_DF_FBA | (uint32_t)ICO_DF_PABE | (uint32_t)ICO_DF_FIX_FACTOR |
                    (uint32_t)ICO_DF_PREMUL | (uint32_t)ICO_DF_DATE | (uint32_t)ICO_DF_C1_DST |
                    (uint32_t)ICO_DF_GS_LOD | (uint32_t)ICO_DF_NEAREST | (uint32_t)ICO_DF_ABOVE1X |
                    (uint32_t)ICO_DF_FOLD),
               "RD_COMP_SPEC_READ is sprite_ps's flags");
_Static_assert(RD_COMP_SPEC_FIXED == ((uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_TCC_RGBA),
               "RD_COMP_SPEC_FIXED is sprite.hlsl SPRITE_COMP_FLAGS");

static const char *const kWhy[RD_COMP_SPEC_WHY_COUNT] = {
    [RD_COMP_SPEC_WHY_NONE] = "drawn by sprite_comp_ps",
    [RD_COMP_SPEC_WHY_OFF] = "off (ICO_RD_COMP_SPEC=0 or the build's RD_COMP_SPEC_DEFAULT 0)",
    [RD_COMP_SPEC_WHY_MODULE] = "no sprite_comp_ps module (made under Vulkan alone)",
    [RD_COMP_SPEC_WHY_TEST] = "its self-test has not passed",
    [RD_COMP_SPEC_WHY_DEVICE] = "no dual-source blend, the exact blend or a multisampled pass",
    [RD_COMP_SPEC_WHY_FLAGS] = ("a flag sprite_comp_ps holds otherwise (DATE by the snapshot, "
                                "DECAL, above 1x, GS levels, nearest, FBA, PABE, FIX)"),
    [RD_COMP_SPEC_WHY_TEXFMT] = "the texel not RGBA32",
    [RD_COMP_SPEC_WHY_DRAW] = "drawn outside a screen run of its own (none in the shadow chain)",
    [RD_COMP_SPEC_WHY_PIPE] = "its pipeline not made",
};

const char *rd__comp_spec_why_text(int why)
{
    return why >= 0 && why < RD_COMP_SPEC_WHY_COUNT ? kWhy[why] : "?";
}

/* the draws noted this replay, waiting for their run's draw (a run holds
 * one command's key and DrawCB; a few are open at most between a note and
 * its flush) */
#define CS_PEND 8

static struct {
    int env;      /* rd__comp_spec_env, read at init */
    int test;     /* 0 not run, 1 passed, 2 failed or not run with a reason */
    bool testing; /* inside the self-test's replays */
    int testArm;  /* there: 0 sprite_ps, 1 sprite_comp_ps */
    char words[224];
    uint32_t replay; /* g_rd.replayCounter of rTaken, rSeen, rNoted */
    uint32_t rTaken, rSeen, rNoted;
    uint64_t taken, seen; /* since rd_init, the self-test's replays left out */
    int lastWhy;
    bool said[RD_COMP_SPEC_WHY_COUNT], firstSaid;
    char stateSaid[384];

    struct {
        uint32_t replay, ord;
        bool live;
        RdPipeKeyInt key;
    } pend[CS_PEND];

    unsigned nPend, nextPend;
} s_cs = {.env = -1};

/* the 10 s block's counts (__atomic: the render thread adds, the window's
 * line takes) */
static struct {
    uint64_t taken, seen, why[RD_COMP_SPEC_WHY_COUNT];
} s_blk;

/* the bench's variant (rd__comp_spec_force): -1 none, 0, 1; kept apart from
 * s_cs, which each renderer's init clears */
static int s_csForce = -1;

int rd__comp_spec_env(const char *env)
{
    const char *e = env ? env : getenv("ICO_RD_COMP_SPEC"); /* the tests only: no config key */
    if (!e || !e[0]) {
        return -1;
    }
    if (strcmp(e, "sabotage") == 0) {
        return 2;
    }
    return e[0] == '0' ? 0 : 1;
}

/* asked for: the bench's variant, the switch, else the build's default */
static bool wanted(void)
{
    if (s_csForce >= 0) {
        return s_csForce == 1;
    }
    return s_cs.env >= 1 || (s_cs.env < 0 && RD_COMP_SPEC_DEFAULT);
}

/* the device's blend as the twin needs it: dual-source, the fixed-function
 * blend (no exact blend, no GS state as shader data) */
static bool deviceOk(void)
{
    return !g_rd.noDual && g_rd.dstMode == RD_BLEND_EXACT_OFF;
}

int rd_comp_spec_key_form(const RdPipeKeyInt *k)
{
    const RdPipelineKey *g = &k->gs;
    if (k->fs != RD_FS_SPRITE || k->dst != RHI_DST_READ_NONE || g->nodual || g->aa1 || g->msaa ||
        g->program != RD_PROG_SCREEN || (k->vs != RD_VS_SPRITE_UI && k->vs != RD_VS_SPRITE_WORLD) ||
        g->prim != RD_PRIM_TRIANGLES || k->colorFmt != RHI_FMT_RGBA8_UNORM || g->afailSplit != 0 ||
        g->colorMask != 0xF) {
        return RD_COMP_SPEC_FORM_NONE;
    }
    if (g->blend == RD_BLEND_LERP_AS && g->atst == RD_ATST_GREATER && g->zwrite == RD_ZWRITE_OFF &&
        (g->stencil == RD_STENCIL_OFF || g->stencil == RD_STENCIL_DATE_TEST0 ||
         g->stencil == RD_STENCIL_DATE_TEST1)) {
        return RD_COMP_SPEC_FORM_COMPOSITE;
    }
    if (g->blend == RD_BLEND_COUNT && g->atst == RD_ATST_ALWAYS && k->depthFmt == RHI_FMT_UNKNOWN &&
        g->stencil == RD_STENCIL_OFF) {
        return RD_COMP_SPEC_FORM_LEVEL;
    }
    return RD_COMP_SPEC_FORM_NONE;
}

int rd_comp_spec_flags_why(uint32_t mode0, uint32_t mode1)
{
    if ((mode0 & RD_COMP_SPEC_READ) != RD_COMP_SPEC_FIXED) {
        return RD_COMP_SPEC_WHY_FLAGS;
    }
    if ((mode1 >> 8) != 0u) {
        return RD_COMP_SPEC_WHY_TEXFMT; /* gs_math.hlsli TEXFMT_RGBA32 is 0 */
    }
    return RD_COMP_SPEC_WHY_NONE;
}

RdPipeKeyInt rd__comp_spec_twin(const RdPipeKeyInt *general)
{
    RdPipeKeyInt k = *general;
    if (k.fs == RD_FS_SPRITE) {
        k.fs = RD_FS_SPRITE_COMP;
    }
    return k;
}

uint32_t rd__comp_spec_keys(RdPipeKeyInt *out, uint32_t max, uint32_t n, uint32_t n0)
{
    if (!g_rd.fs[RD_FS_SPRITE_COMP].id || !g_rd.fs[RD_FS_SPRITE].id || !wanted() || !deviceOk()) {
        return n;
    }
    for (uint32_t i = 0; i < n0 && i < max; i++) {
        if (rd_comp_spec_key_form(&out[i]) != RD_COMP_SPEC_FORM_NONE) {
            const RdPipeKeyInt t = rd__comp_spec_twin(&out[i]);
            n = rd__add_pipe_key(out, max, n, &t);
        }
    }
    return n;
}

bool rd__comp_spec_on(void)
{
    return g_rd.fs[RD_FS_SPRITE_COMP].id && wanted() && s_cs.test == 1 && deviceOk();
}

static void blkAdd(uint64_t *v, uint64_t n)
{
    __atomic_fetch_add(v, n, __ATOMIC_RELAXED);
}

static void replayRoll(void)
{
    if (s_cs.replay == g_rd.replayCounter) {
        return;
    }
    /* the last replay's notes its runs never drew (a draw outside a run) */
    for (unsigned i = 0; i < CS_PEND; i++) {
        if (s_cs.pend[i].live && !s_cs.testing) {
            s_cs.lastWhy = RD_COMP_SPEC_WHY_DRAW;
            blkAdd(&s_blk.why[RD_COMP_SPEC_WHY_DRAW], 1);
        }
        s_cs.pend[i].live = false;
    }
    s_cs.nPend = 0;
    s_cs.replay = g_rd.replayCounter;
    s_cs.rTaken = s_cs.rSeen = s_cs.rNoted = 0;
}

static void kept(int why)
{
    s_cs.lastWhy = why;
    blkAdd(&s_blk.why[why], 1);
    if (why != RD_COMP_SPEC_WHY_OFF && why != RD_COMP_SPEC_WHY_MODULE) {
        ico_attempt_fire2(ICO_ATT_COMP_SPEC, 1);
    }
    if (why != RD_COMP_SPEC_WHY_OFF && !s_cs.said[why]) {
        s_cs.said[why] = true;
        rd__log("composites: a shadow composite or level kept sprite_ps: %s (frame %u, command "
                "%u; reported once for this reason)",
                kWhy[why], g_rd.replayCounter, g_rdStoreOrd);
    }
}

void rd__comp_spec_note(const RdPipeKeyInt *key, const struct IcoDrawCB *cb)
{
    if (!key || !cb || rd_comp_spec_key_form(key) == RD_COMP_SPEC_FORM_NONE) {
        return;
    }
    replayRoll();
    s_cs.rSeen++;
    int why = RD_COMP_SPEC_WHY_NONE;
    if (s_cs.testing) {
        /* the self-test's replays: arm 0 sprite_ps, arm 1 sprite_comp_ps
         * wherever the module and the gate's words take it; not counted for
         * the run or the block */
        if (s_cs.testArm == 0 || !g_rd.fs[RD_FS_SPRITE_COMP].id || !deviceOk() ||
            g_rd.passSamples > 1 ||
            rd_comp_spec_flags_why(cb->mode[0], cb->mode[1]) != RD_COMP_SPEC_WHY_NONE) {
            return;
        }
    } else {
        s_cs.seen++;
        blkAdd(&s_blk.seen, 1);
        if (!wanted()) {
            why = RD_COMP_SPEC_WHY_OFF;
        } else if (!g_rd.fs[RD_FS_SPRITE_COMP].id) {
            why = RD_COMP_SPEC_WHY_MODULE;
        } else if (s_cs.test != 1) {
            why = RD_COMP_SPEC_WHY_TEST;
        } else if (!deviceOk() || g_rd.passSamples > 1) {
            why = RD_COMP_SPEC_WHY_DEVICE; /* rd__get_pipeline would turn it multisampled */
        } else {
            why = rd_comp_spec_flags_why(cb->mode[0], cb->mode[1]);
        }
        if (why != RD_COMP_SPEC_WHY_NONE) {
            kept(why);
            return;
        }
    }
    /* noted: the run this command opens (or joins) draws it */
    s_cs.rNoted++;
    unsigned at = s_cs.nextPend;
    s_cs.nextPend = (s_cs.nextPend + 1u) % CS_PEND;
    if (s_cs.pend[at].live && !s_cs.testing) {
        kept(RD_COMP_SPEC_WHY_DRAW); /* an older note never drawn in a run */
    }
    s_cs.pend[at].live = true;
    s_cs.pend[at].replay = g_rd.replayCounter;
    s_cs.pend[at].ord = g_rdStoreOrd;
    s_cs.pend[at].key = *key;
    s_cs.nPend = s_cs.nPend < CS_PEND ? s_cs.nPend + 1u : CS_PEND;
}

RhiPipeline rd__comp_spec_turn(RhiPipeline p, const RdPipeKeyInt *k)
{
    if (!s_cs.nPend || !g_rdDrawOwner || !p.id || !k || k->fs != RD_FS_SPRITE) {
        return p;
    }
    int hit = -1;
    for (unsigned i = 0; i < CS_PEND && hit < 0; i++) {
        if (s_cs.pend[i].live && s_cs.pend[i].replay == g_rd.replayCounter &&
            s_cs.pend[i].ord + 1u == g_rdDrawOwner &&
            memcmp(&s_cs.pend[i].key, k, sizeof(*k)) == 0) {
            hit = (int)i;
        }
    }
    if (hit < 0) {
        return p;
    }
    s_cs.pend[hit].live = false;
    s_cs.nPend--;
    const RdPipeKeyInt t = rd__comp_spec_twin(k);
    const RhiPipeline tp = rd__get_pipeline(&t);
    if (!tp.id) {
        if (!s_cs.testing) {
            kept(RD_COMP_SPEC_WHY_PIPE);
        }
        return p;
    }
    s_cs.rTaken++;
    if (s_cs.testing) {
        return tp;
    }
    s_cs.taken++;
    blkAdd(&s_blk.taken, 1);
    ico_attempt_fire(ICO_ATT_COMP_SPEC, 1);
    if (!s_cs.firstSaid) {
        s_cs.firstSaid = true;
        rd__log("composites: a shadow composite or level drawn by sprite_comp_ps, frame %u "
                "(command %u; reported once)",
                g_rd.replayCounter, g_rdDrawOwner - 1u);
    }
    return tp;
}

/* the state's line, when it changes, and the ledger's row */
static void stateNote(void)
{
    char line[sizeof(s_cs.stateSaid)];
    const char *how =
        s_csForce == 0   ? "the bench's variant: off"
        : s_csForce == 1 ? "the bench's variant: on"
        : s_cs.env == 0  ? "ICO_RD_COMP_SPEC=0"
        : s_cs.env == 1  ? "ICO_RD_COMP_SPEC=1"
        : s_cs.env == 2 ? "ICO_RD_COMP_SPEC=sabotage"
                        : (RD_COMP_SPEC_DEFAULT ? "the build's default" : "RD_COMP_SPEC_DEFAULT 0");
    if (!wanted()) {
        snprintf(line, sizeof(line), "off (%s)", how);
    } else if (!g_rd.fs[RD_FS_SPRITE_COMP].id) {
        snprintf(line, sizeof(line), "off (%s): %s", how,
                 !g_rd.hasDevice ? "no device"
                 : rhi_backend() != RHI_BACKEND_VULKAN
                     ? "sprite_comp_ps is made under Vulkan alone; sprite_ps draws them"
                     : "no sprite_comp_ps module in the shader table");
    } else if (!deviceOk()) {
        snprintf(line, sizeof(line), "off (%s): %s; sprite_ps draws them", how,
                 g_rd.noDual ? "no dual-source blend (the two-pass blend keeps sprite_ps)"
                             : "the exact blend in force");
    } else if (s_cs.test == 1) {
        snprintf(line, sizeof(line), "on (%s; self-test %s)", how, s_cs.words);
    } else {
        snprintf(line, sizeof(line), "off until its self-test passes (%s; self-test %s)", how,
                 s_cs.words[0] ? s_cs.words : "not run yet");
    }
    if (strcmp(line, s_cs.stateSaid) != 0) {
        snprintf(s_cs.stateSaid, sizeof(s_cs.stateSaid), "%s", line);
        rd__log("composites: the shadow composites and levels by their own entry "
                "(sprite_comp_ps, sprite_ps's path for them alone): %s",
                line);
    }
    (void)ico_attempt_decide_comp_spec(g_rd.hasDevice && g_rd.fs[RD_FS_SPRITE_COMP].id != 0,
                                       deviceOk() ? 1 : 0, s_cs.env < 0 ? -1 : (s_cs.env ? 1 : 0),
                                       RD_COMP_SPEC_DEFAULT,
                                       s_cs.test == 1 ? 1 : (s_cs.test == 2 ? 0 : -1));
}

/* the bench's variant without DATE's stencil set-ups (rd_bench.h) */
static bool s_noDateSetup;

void rd__date_setup_bench_skip(int skip)
{
    s_noDateSetup = skip != 0;
}

bool rd__date_setup_bench_skipped(void)
{
    return s_noDateSetup;
}

void rd__comp_spec_force(int v)
{
    s_csForce = v < 0 ? -1 : (v ? 1 : 0);
    stateNote();
}

void rd__comp_spec_init(void)
{
    if (g_rd.fs[RD_FS_SPRITE_COMP].id && g_rd.hasDevice) {
        rhi_destroy_shader(g_rd.fs[RD_FS_SPRITE_COMP]); /* made again without a shutdown */
    }
    g_rd.fs[RD_FS_SPRITE_COMP] = (RhiShader){0};
    memset(&s_cs, 0, sizeof(s_cs));
    s_cs.env = rd__comp_spec_env(NULL);
    /* Vulkan alone (rd_paste_spec.c's rule): the other backends keep sprite_ps */
    if (g_rd.hasDevice && rhi_backend() == RHI_BACKEND_VULKAN && s_cs.env != 0 &&
        g_rd.fs[RD_FS_SPRITE].id && ico_find_shader(RD_COMP_SPEC_NAME)) {
        g_rd.fs[RD_FS_SPRITE_COMP] = rd__make_shader(RD_COMP_SPEC_NAME);
    }
    stateNote();
}

void rd__comp_spec_shutdown(void)
{
    if (g_rd.fs[RD_FS_SPRITE_COMP].id && g_rd.hasDevice) {
        rhi_destroy_shader(g_rd.fs[RD_FS_SPRITE_COMP]);
    }
    g_rd.fs[RD_FS_SPRITE_COMP] = (RhiShader){0};
}

int rd__comp_spec_test_state(const char **words)
{
    if (words) {
        *words = s_cs.words[0] ? s_cs.words : "not run";
    }
    return s_cs.test;
}

void rd__comp_spec_counts(uint64_t *taken, uint64_t *seen, int *lastWhy)
{
    *taken = s_cs.taken;
    *seen = s_cs.seen;
    *lastWhy = s_cs.lastWhy;
}

void rd__comp_spec_replay(uint32_t *taken, uint32_t *seen)
{
    const bool now = s_cs.replay == g_rd.replayCounter;
    *taken = now ? s_cs.rTaken : 0u;
    *seen = now ? s_cs.rSeen : 0u;
}

void rd__comp_spec_line(void)
{
    const uint64_t taken = __atomic_exchange_n(&s_blk.taken, 0, __ATOMIC_RELAXED);
    const uint64_t seen = __atomic_exchange_n(&s_blk.seen, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_COMP_SPEC_WHY_COUNT];
    for (int i = 0; i < RD_COMP_SPEC_WHY_COUNT; i++) {
        w[i] = __atomic_exchange_n(&s_blk.why[i], 0, __ATOMIC_RELAXED);
    }
    if (seen == 0) {
        return;
    }
    char why[768] = "";
    size_t at = 0;
    for (int i = 1; i < RD_COMP_SPEC_WHY_COUNT; i++) {
        if (!w[i]) {
            continue;
        }
        const int m = snprintf(why + at, sizeof(why) - at, "%s%llu %s", at ? ", " : " (",
                               (unsigned long long)w[i], kWhy[i]);
        if (m < 0 || (size_t)m >= sizeof(why) - at) {
            break;
        }
        at += (size_t)m;
    }
    fprintf(stderr,
            "window: composites: %llu shadow composites and levels by sprite_comp_ps of %llu, "
            "%llu kept on sprite_ps%s%s\n",
            (unsigned long long)taken, (unsigned long long)seen,
            (unsigned long long)(seen > taken ? seen - taken : 0u), why, at ? ")" : "");
}

/* ------------------------------------------------------------ the self-test
 * A frame of its own on SCENE at its real size and scale (never recorded,
 * never presented; rd_fog_path.c's helpers), list 0, each band a GS row
 * range over the whole width:
 *   the destination  a DECAL sprite from a random 64 x 32 texture over the
 *                    frame, nearest, no blend (a random colour and alpha under
 *                    every band; sprite_ps draws it both ways: DECAL)
 *   bands 1-4        the composites' form: MODULATE with TCC, the bilinear
 *                    filter, the LERP (Cs - Cd) As + Cd, Z ALWAYS without
 *                    write, ATST GREATER: 1 the game's (vertex 128 x 4, AREF
 *                    0, a 106 x 64 source magnified as shadow.c's SHADOW2,
 *                    UVs from a quarter texel), 2 another vertex colour and
 *                    the source clamped, 3 AREF 40 and a 424 x 256 source, 4
 *                    under DATE (DATM 0: the stencil bit's test on the stencil
 *                    path, the snapshot's in the shader elsewhere, where the
 *                    gate keeps it on sprite_ps both ways)
 *   band 5           the levels' form: the same sampling without blend,
 *                    alpha test or depth (SCENE bound without its depth)
 * The sources' alphas: a third 0 (the alpha test's discard), the rest up to
 * 255 (some past 0x80).  Replayed with the forms by sprite_ps (arm 0) and by
 * sprite_comp_ps (arm 1), each from SCENE's clear, SCENE read back each time.
 * Passed: every byte equal, arm 1 drew by sprite_comp_ps every draw the gate
 * took (at least the four bands without DATE), arm 0 none.  MSAA held off.
 * ICO_RD_COMP_SPEC=sabotage: arm 1's frame draws band 2 with its vertex
 * alpha one lower, so it must fail. */
#define CS_BANDS 6u

static uint8_t csLevel(uint32_t i, uint32_t seed)
{
    uint32_t x = i * 2654435761u + seed * 0x9E3779B9u + 0x7F4A7C15u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return (uint8_t)(x >> 24);
}

/* a random RGBA8 texture, a third of its alphas 0 when holes */
static RdTex csTexture(uint32_t w, uint32_t h, uint32_t seed, bool holes, const char *name)
{
    uint8_t *px = malloc((size_t)w * h * 4u);
    if (!px) {
        return (RdTex){0};
    }
    for (uint32_t i = 0; i < w * h * 4u; i++) {
        px[i] = csLevel(i, seed);
    }
    if (holes) {
        for (uint32_t i = 0; i < w * h; i++) {
            if (csLevel(i, seed + 7u) < 85u) {
                px[i * 4u + 3u] = 0;
            }
        }
    }
    const RdTex t = rd_create_texture(w, h, px, RD_TEXA_80_80, name);
    free(px);
    return t;
}

static bool csTex(RdFrame *f, RdTex t, RdTexFn fn, RdFilter filter, RdWrap wrap)
{
    RdCmd *c = rd__st_push(f, 0, RDC_TEXTURE);
    if (c) {
        c->u[0] = t.id;
        c->b[0] = (uint8_t)fn;
        c->b[1] = RD_TCC_RGBA;
    }
    c = c ? rd__st_push(f, 0, RDC_FILTER) : NULL;
    if (c) {
        c->b[0] = (uint8_t)filter;
        c->b[1] = (uint8_t)filter;
    }
    c = c ? rd__st_push(f, 0, RDC_WRAP) : NULL;
    if (c) {
        c->b[0] = (uint8_t)wrap;
        c->b[1] = (uint8_t)wrap;
    }
    return c != NULL;
}

static bool csBlend(RdFrame *f, int abe)
{
    RdCmd *c = rd__st_push(f, 0, RDC_BLEND);
    if (c) {
        c->b[0] = RD_BLEND_LERP_AS;
        c->b[1] = 0;
        c->b[2] = (uint8_t)abe;
    }
    return c != NULL;
}

/* one sprite over GS rows y0..y1 of the w-wide frame, the source's texels
 * (u0, v0)-(u1, v1) in sixteenths, the vertex colour rgba */
static bool csSprite(RdFrame *f, uint32_t w, uint32_t h, uint32_t y0, uint32_t y1,
                     const uint8_t rgba[4], int32_t u0, int32_t v0, int32_t u1, int32_t v1)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    const int32_t ox = 2048 - (int32_t)(w / 2u), oy = 2048 - (int32_t)(h / 2u);
    for (int k = 0; k < 2; k++) {
        v[k].x = (ox + (k ? (int32_t)w : 0)) * 16;
        v[k].y = (oy + (int32_t)(k ? y1 : y0)) * 16;
        v[k].s = (float)(k ? u1 : u0);
        v[k].t = (float)(k ? v1 : v0);
        v[k].q = 1.0f;
        memcpy(v[k].rgba, rgba, 4);
    }
    RdCmd *c = rd__st_push(f, 0, RDC_SCREEN);
    if (c) {
        c->b[0] = RD_PRIM_SPRITES;
        c->b[1] = RD_SPACE_WORLD; /* shadow.c's sprites */
        c->b[2] = 1;              /* UV in 12.4 texels */
        c->u[0] = rd__frame_payload(f, v, (uint32_t)sizeof(v));
        c->u[1] = 2;
    }
    return c != NULL;
}

static uint32_t bandRow(uint32_t h, uint32_t b)
{
    return h * b / CS_BANDS;
}

static bool csFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, RdTex dst, RdTex s1,
                    RdTex s2, bool sabotage)
{
    static const uint8_t kGame[4] = {128, 128, 128, 128}, kWhite[4] = {128, 128, 128, 128};
    const uint8_t kOther[4] = {200, 90, 40, (uint8_t)(sabotage ? 99 : 100)};
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = rd__st_target(f, 0, scene, w, h) && rd__st_push1(f, 0, RDC_FBA, 0) &&
              rd__st_push1(f, 0, RDC_PABE, 0) && rd__st_push1(f, 0, RDC_COLCLAMP, 1) &&
              rd__st_push1(f, 0, RDC_SHADE, 0) && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) &&
              rd__st_test(f, 0, RD_TEST_Z_ALWAYS) && csBlend(f, 0);
    /* the destination: DECAL, nearest, over the whole frame */
    ok = ok && csTex(f, dst, RD_TEXFN_DECAL, RD_FILTER_NEAREST, RD_WRAP_REPEAT) &&
         csSprite(f, w, h, 0, h, kWhite, 0, 0, 64 * 16, 32 * 16);
    /* the composites' form (GS TEST: ATE, ATST GREATER, AREF, AFAIL KEEP, ZTE,
     * ZTST ALWAYS; 0x4000 DATE) */
    ok = ok && csBlend(f, 1) && rd__st_test(f, 0, 0x3000Du) &&
         csTex(f, s1, RD_TEXFN_MODULATE, RD_FILTER_LINEAR, RD_WRAP_REPEAT) &&
         csSprite(f, w, h, bandRow(h, 1), bandRow(h, 2), kGame, 4, 4, 106 * 16 + 4, 64 * 16 + 4);
    ok = ok && csTex(f, s1, RD_TEXFN_MODULATE, RD_FILTER_LINEAR, RD_WRAP_CLAMP) &&
         csSprite(f, w, h, bandRow(h, 2), bandRow(h, 3), kOther, 4, 4, 106 * 16 + 4, 56 * 16 + 4);
    ok = ok && rd__st_test(f, 0, 0x3000Du | (40u << 4)) &&
         csTex(f, s2, RD_TEXFN_MODULATE, RD_FILTER_LINEAR, RD_WRAP_CLAMP) &&
         csSprite(f, w, h, bandRow(h, 3), bandRow(h, 4), kGame, 4, 4, 424 * 16 + 4, 224 * 16 + 4);
    ok = ok && rd__st_test(f, 0, 0x3400Du) &&
         csTex(f, s1, RD_TEXFN_MODULATE, RD_FILTER_LINEAR, RD_WRAP_CLAMP) &&
         csSprite(f, w, h, bandRow(h, 4), bandRow(h, 5), kGame, 4, 4, 106 * 16 + 4, 56 * 16 + 4);
    /* the levels' form: SCENE without its depth, no blend, no alpha test */
    RdCmd *t = ok ? rd__st_push(f, 0, RDC_TARGET) : NULL;
    if (t) {
        t->u[0] = scene;
        t->u[1] = 0;
        t->u[2] = (w & 0xFFFF) | ((h & 0xFFFF) << 16);
        t->b[0] = RD_TARGET_OFFSET;
    }
    ok = ok && t && csBlend(f, 0) && rd__st_test(f, 0, RD_TEST_Z_ALWAYS) &&
         csTex(f, s2, RD_TEXFN_MODULATE, RD_FILTER_LINEAR, RD_WRAP_CLAMP) &&
         csSprite(f, w, h, bandRow(h, 5), h, kGame, 4, 4, 424 * 16 + 4, 256 * 16 + 4);
    return ok;
}

/* where two readbacks differ: the count, the first texel */
static size_t diffCount(const uint8_t *a, const uint8_t *b, size_t texels, size_t *first)
{
    size_t n = 0;
    *first = 0;
    for (size_t i = 0; i < texels; i++) {
        if (memcmp(&a[i * 4u], &b[i * 4u], 4) != 0) {
            if (!n++) {
                *first = i;
            }
        }
    }
    return n;
}

bool rd__comp_spec_selftest(void)
{
    s_cs.test = 0;
    s_cs.words[0] = '\0';
    if (!g_rd.inited || !g_rd.hasDevice || !g_rd.fs[RD_FS_SPRITE_COMP].id ||
        !g_rd.fs[RD_FS_SPRITE].id || !wanted() || !deviceOk()) {
        stateNote();
        return true; /* not asked or not offered here: nothing to check */
    }
    s_cs.test = 2;
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    RdTargetRec *t = scene ? rd__target_rec(scene) : NULL;
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM ||
        t->w < CS_BANDS * 4u || t->h < CS_BANDS * 4u) {
        snprintf(s_cs.words, sizeof(s_cs.words), "not run: no SCENE target to run it on");
        rd__log("composites: self-test not run (no SCENE target to run it on): sprite_ps draws "
                "the shadow composites and levels");
        stateNote();
        return false;
    }
    const uint32_t w = t->w, h = t->h;
    const size_t texels = (size_t)t->tw * t->th, size = texels * 4u;
    uint8_t *px[2] = {malloc(size), malloc(size)};
    const RdTex dst = csTexture(64, 32, 1u, false, "rd composite self-test destination");
    const RdTex s1 = csTexture(106, 64, 2u, true, "rd composite self-test level");
    const RdTex s2 = csTexture(424, 256, 3u, true, "rd composite self-test level 0");
    const bool sabotage = s_cs.env == 2;
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true;
    uint32_t took[2] = {0, 0}, noted[2] = {0, 0};
    const char *err = NULL;
    if (!px[0] || !px[1]) {
        err = "no memory for its readbacks";
    } else if (!dst.id || !s1.id || !s2.id) {
        err = "no memory for its textures";
    }
    for (int arm = 0; arm < 2 && !err; arm++) {
        static RdFrame f;
        memset(&f, 0, sizeof(f)); /* the last frame was freed: nothing of it held */
        if (!csFrame(&f, scene, w, h, dst, s1, s2, sabotage && arm == 1)) {
            err = "could not build its frame";
            rd__frame_free(&f);
            break;
        }
        uint32_t rw = 0, rh = 0;
        t->clearPending = 1; /* each replay from SCENE's clear */
        s_cs.testing = true;
        s_cs.testArm = arm;
        const bool done = rd__replay_frame(&f, 0, false);
        s_cs.testing = false;
        took[arm] = s_cs.replay == g_rd.replayCounter ? s_cs.rTaken : 0u;
        noted[arm] = s_cs.replay == g_rd.replayCounter ? s_cs.rNoted : 0u;
        rd__frame_free(&f);
        if (!done || !rd__read_target(rd_target(RD_TARGET_SCENE), px[arm], size, &rw, &rh) ||
            (size_t)rw * rh != texels) {
            err = "replay or readback error";
        }
    }
    s_cs.testing = false;
    for (unsigned i = 0; i < CS_PEND; i++) {
        s_cs.pend[i].live = false; /* nothing of the self-test's replays waits for a run */
    }
    s_cs.nPend = 0;
    g_rd.msaaBlocked = wasBlocked;
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    if (dst.id) {
        rd_destroy_texture(dst);
    }
    if (s1.id) {
        rd_destroy_texture(s1);
    }
    if (s2.id) {
        rd_destroy_texture(s2);
    }
    size_t nd = 0, first = 0;
    if (!err) {
        nd = diffCount(px[0], px[1], texels, &first);
    }
    /* the four bands without DATE always pass the gate; the DATE band where
     * DATE is the stencil bit's test */
    const bool tookAll = !err && took[1] >= 4u && took[1] == noted[1] && took[0] == 0;
    if (err) {
        snprintf(s_cs.words, sizeof(s_cs.words), "not run: %s", err);
        rd__log("composites: self-test not run (%s): sprite_ps draws the shadow composites and "
                "levels",
                err);
    } else if (!tookAll) {
        snprintf(s_cs.words, sizeof(s_cs.words),
                 "not run: the draws not taken as asked, %u of %u noted by sprite_comp_ps, %u in "
                 "the sprite_ps replay",
                 took[1], noted[1], took[0]);
        rd__log("composites: self-test %s: sprite_ps draws the shadow composites and levels",
                s_cs.words);
    } else if (nd) {
        const uint8_t *a = &px[1][first * 4u], *b = &px[0][first * 4u];
        snprintf(s_cs.words, sizeof(s_cs.words), "failed at texel %zu", first);
        rd__log("composites: self-test failed: SCENE differs at %zu of %zu texels, the first "
                "(%zu,%zu): %02X%02X%02X%02X for %02X%02X%02X%02X%s: sprite_ps draws the shadow "
                "composites and levels",
                nd, texels, first % t->tw, first / t->tw, a[0], a[1], a[2], a[3], b[0], b[1], b[2],
                b[3], sabotage ? " (ICO_RD_COMP_SPEC=sabotage)" : "");
    } else {
        s_cs.test = 1;
        /* no brackets in the words: the tool's stats line holds them in its own */
        snprintf(s_cs.words, sizeof(s_cs.words),
                 "passed: SCENE the same at %zu texels of %ux%u, %u draws of the two forms by "
                 "each entry",
                 texels, t->tw, t->th, took[1]);
        rd__log("composites: the shadow composites and levels by their own entry (self-test: "
                "SCENE the same at %zu texels, %ux%u at %ux%u texels, %u composite and level "
                "draws each way): the same bytes",
                texels, w, h, t->tw, t->th, took[1]);
    }
    free(px[0]);
    free(px[1]);
    stateNote();
    return s_cs.test == 1;
}
