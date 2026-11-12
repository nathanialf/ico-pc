/* rd_fill11.c: the crack fill and list 11 in one pass on SCENE, the same
 * bytes (rd_fill11.h; docs/port/RENDER_POST.md, "The crack fill in list 11's
 * pass"; the scratch depth: docs/port/WORK_BUFFERS.md).
 *
 * The title's pictures without glow (the light plan's DARK) and without an
 * aura draw the crack fill in a full-size pass of its own on SCENE (its
 * colour alone, by the swap not loaded, stored) and then list 11 in another
 * (SCENE's colour loaded and stored, its depth attached: Z ALWAYS, no Z
 * write).  Here the fill's snapshot is taken where it is due, as without
 * this, and its draw waits (rd_crack_fill.c holds it) past the commands
 * that neither read nor write SCENE's colour, depth or snapshot until list
 * 11's first screen draw on SCENE; that draw begins one pass on SCENE's
 * colour with a scratch depth attached, the fill is its first draw and list
 * 11's draws follow with their own pipelines.
 *
 * Why the bytes are the same:
 *   - the fill: its snapshot (or the swap) and the count are taken where they
 *     were; between the snapshot and its draw nothing that runs on the GPU
 *     reads or writes SCENE's colour, its depth (bound in any pass, sampled,
 *     cleared, copied) or its snapshot texture (classOf: a command is waited
 *     past only when it is state, a NOP, overlay text, a draw the light plan
 *     leaves undrawn, or a clear, copy, draw or sprite on another target that
 *     binds no SCENE depth and samples no SCENE; SCENE's border repeat draws
 *     the fill first, rd__fill11_border), so the fill reads the same snapshot
 *     and depth and writes the same texels, and every command in between
 *     reads what it read before;
 *   - its pipeline: the same shader, blend and colour mask with a depth
 *     attachment of D32F_S8 present, Z ALWAYS and no Z write (rd__fill11_twin):
 *     no depth test can fail, nothing is written to the depth, the fragment
 *     shader does not read it; the start-up self-test checks the twins on the
 *     device (rd__fill11_selftest);
 *   - list 11's draws: the same pipelines (a key with depthFmt D32F_S8: the
 *     scratch has SCENE's format and size and one sample), the same FrameCB
 *     (their tdId stays SCENE's: the Z scale), the same order, and each one
 *     neither tests nor writes the depth (Z test off or ALWAYS, Z write off,
 *     no DATE through the stencil, the snapshot or the shader's read, no
 *     PRIM.AA1, no shadow window live on SCENE's depth, not the wrap path):
 *     their colour does not depend on what the attached depth holds.  A draw
 *     that does is drawn in a pass with SCENE's depth (the merged pass ends
 *     first: rd__fill11_draw), and the landing is not taken where the look
 *     ahead (laterOk) finds one before list 11's pass would end anyway, so
 *     the passes stay today's there;
 *   - SCENE's depth is not attached in the merged pass and not written by it:
 *     it holds what the last pass that wrote it stored (the store plan keeps
 *     that store for the fill's read, rd_store_plan.c's late touch at the
 *     fill's place; no pass between binds it), the fill samples it
 *     (RHI_STATE_DEPTH_READ, as in the fill's own pass: rhi.h forbids
 *     sampling a depth bound read-only, hence the scratch);
 *   - the pass is whole (refused where rd_pass_area.c would give list 11's
 *     pass an area), single-sampled (no MSAA epoch wanted), not a storage
 *     pass and no exact blend, its colour readable as beginPass would begin
 *     it (rd__pass_wants_readable).
 * An unrecognised form keeps today's passes: the fill is drawn in its own
 * pass before the first command it cannot wait past (rd_crack_fill.c
 * rd__crack_fill11_own), the same bytes as at its place.
 *
 * Counted (this replay's for --stats, the 10 s block's for the line): fills
 * drawn in list 11's pass and their colour bytes, held fills drawn in a pass
 * of their own after all and why, fills not held and why, merged passes a
 * draw that uses SCENE's depth ended.  The lines:
 *   rd: crack fill: with list 11: on (self-test ...; the PowerVR class|a
 *       desktop class|ICO_RD_FILL11=1): ...      (start, a change)
 *   rd: crack fill: with list 11: off (why)      (start, a change)
 *   rd: crack fill: with list 11: N drawn in list 11's pass ... (10 s)
 *   rd: crack fill: drawn as the first draw of list 11's pass over a
 *       scratch depth ... (reported once) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_fill11.h"
#include "rd_replay.h"
#include "rd_shadow_tile.h" /* rd__profile_tile_class */
#include "rd_upscale.h"     /* rd__upscale_point_on */

/* Off by default in this release: on the owner's PowerVR phone (rc14, DXT-48-1536) the right
 * part of the title's picture flickered black from the first picture the fill landed in list
 * 11's pass, and the cause was not found by reading (docs/port/RENDER_POST.md).
 * ICO_RD_FILL11=1 turns it on for the tests; 1 here would put it back on the PowerVR and desktop
 * classes where the self-test passes */
#define RD_FILL11_DEFAULT 0

/* crack_fill_ps's DrawCB.mode.x (blit.hlsl CRACK_MODE_*, rd_crack_fill.c) */
#define F11_MODE_COPY 1u
#define F11_MODE_KEEP_A 2u
#define F11_MODE_EARLY 4u

static const char *const kNot[RD_FILL11_NOT_COUNT] = {
    [RD_FILL11_NOT_NEXT] = "the next command reads or writes SCENE",
    [RD_FILL11_NOT_LIST] = "due after list 11",
    [RD_FILL11_NOT_MSAA] = "the MSAA epoch",
    [RD_FILL11_NOT_BLEND] = "the exact blend or GS state as shader data",
    [RD_FILL11_NOT_SPLIT] = "the upscaler's split",
    [RD_FILL11_NOT_WINDOW] = "a pass window",
    [RD_FILL11_NOT_DEFER] = "a glow paste deferred",
    [RD_FILL11_NOT_MEMORY] = "no memory for the scratch depth",
    [RD_FILL11_NOT_PIPE] = "its depth twin missing",
};

static const char *const kWhy[RD_FILL11_WHY_COUNT] = {
    [RD_FILL11_WHY_CMD] = "a command that reads or writes SCENE",
    [RD_FILL11_WHY_DEPTH] = "SCENE's depth bound, written or DATE",
    [RD_FILL11_WHY_BORDER] = "SCENE's border repeat",
    [RD_FILL11_WHY_AREA] = "list 11's pass holding a part of the picture",
    [RD_FILL11_WHY_LATER] = "a later draw of list 11 that uses SCENE's depth",
    [RD_FILL11_WHY_DRAW] = "list 11's first draw taking DATE or the stencil",
    [RD_FILL11_WHY_SNAP] = "SCENE's snapshot taken again",
    [RD_FILL11_WHY_END] = "the walk's end",
};

/* the switch: env ICO_RD_FILL11 (-2 not read, -1 unset, 0, 1), force the
 * tests' (-1 none), test the self-test (0 not run, 1 passed, 2 failed) */
static struct {
    int env, force, test;
    char words[192];
    char said[640];
} s_sw = {-2, -1, 0, "", ""};

/* the scratch depth: D32F_S8 at SCENE's texture size, a depth attachment
 * alone, never loaded or stored (its contents are never read) */
static struct {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
} s_scr;

/* the merged pass open (its pass serial, 0 none) and the command a held
 * fill lands at (g_rdStoreOrd, UINT32_MAX none) */
static struct {
    uint32_t serial, replay;
    uint32_t land, landReplay;
} s_open = {0, 0, UINT32_MAX, 0};

/* this replay's counts (--stats, the tests) and the 10 s block's */
static struct {
    uint32_t replay;
    RdFill11Counts c;
    uint64_t texels;
    uint32_t why[RD_FILL11_WHY_COUNT], not_[RD_FILL11_NOT_COUNT];
} s_rep;

static struct {
    uint64_t landed, texels, flushed, refused, split;
    uint64_t why[RD_FILL11_WHY_COUNT], not_[RD_FILL11_NOT_COUNT];
} s_blk;

static RdTargetRec *sceneRec(void)
{
    return rd__target_rec(RD_TARGET_SCENE + 1u);
}

static bool isScene(uint32_t id)
{
    const RdTargetRec *s = sceneRec();
    return id != 0 && s != NULL && rd__target_rec(id) == s;
}

static void rep(void)
{
    if (s_rep.replay != g_rd.replayCounter) {
        memset(&s_rep, 0, sizeof(s_rep));
        s_rep.replay = g_rd.replayCounter;
    }
}

/* ---------------------------------------------------------------- switch */

static int envSwitch(void)
{
    if (s_sw.env == -2) {
        /* the tests' A/B only (TESTING.md): no config key */
        const char *e = getenv("ICO_RD_FILL11");
        s_sw.env = !e || !e[0] ? -1 : strcmp(e, "0") == 0 ? 0 : strcmp(e, "1") == 0 ? 1 : -1;
        if (e && e[0] && s_sw.env < 0) {
            rd__log("crack fill: with list 11: ICO_RD_FILL11=%.16s is not 0 or 1: the class's", e);
        }
    }
    return s_sw.force >= 0 ? s_sw.force : s_sw.env;
}

/* 1 the PowerVR class, 2 a desktop class, 0 any other (the Arm, Qualcomm
 * and Apple classes, another tile-based phone GPU) */
static int classOk(void)
{
    const RhiLimits *lim = rhi_limits();
    const int c = rd__profile_tile_class();
    if (c == 1) {
        return 1;
    }
    return c == 2 && lim != NULL && !lim->tilePhone ? 2 : 0;
}

/* the self-test and the start-up set's twins: wherever the default and the
 * class take it or ICO_RD_FILL11 is set either way (both runs of an A/B
 * make the same start-up set and frames); with the default off nothing new
 * runs on a device without the variable */
static bool tried(void)
{
    return envSwitch() >= 0 || (RD_FILL11_DEFAULT != 0 && classOk() != 0);
}

bool rd__fill11_may(void)
{
    const int e = envSwitch();
    return e == 1 || (e < 0 && RD_FILL11_DEFAULT != 0 && classOk() != 0);
}

bool rd__fill11_on(void)
{
    return g_rd.hasDevice && s_sw.test == 1 && rd__fill11_may();
}

int rd__fill11_test(void)
{
    return s_sw.test;
}

/* the state line, when it changes */
static void note(void)
{
    if (!g_rd.hasDevice) {
        return;
    }
    char line[sizeof(s_sw.said)];
    const int e = envSwitch();
    const char *src = s_sw.force >= 0 ? "the tests' switch" : "ICO_RD_FILL11";
    if (rd__fill11_on()) {
        snprintf(line, sizeof(line),
                 "crack fill: with list 11: on (self-test %s; %s): the fill's draw held from its "
                 "place to list 11's first screen draw on SCENE where nothing between touches "
                 "SCENE, both in one pass over a scratch depth of SCENE's size, list 11's draws "
                 "with their own pipelines (the same bytes)",
                 s_sw.words,
                 e == 1           ? (s_sw.force >= 0 ? "the tests' switch" : "ICO_RD_FILL11=1")
                 : classOk() == 1 ? "the PowerVR class"
                                  : "a desktop class");
    } else if (e == 0) {
        snprintf(line, sizeof(line), "crack fill: with list 11: off (%s%s; self-test %s)", src,
                 s_sw.force >= 0 ? "" : "=0", s_sw.test ? s_sw.words : "not run");
    } else if (e < 0 && RD_FILL11_DEFAULT == 0) {
        snprintf(line, sizeof(line),
                 "crack fill: with list 11: off (the default in this release: the PowerVR "
                 "phone's title flickered black with it on, cause not found; ICO_RD_FILL11=1 "
                 "turns it on for the tests)");
    } else if (!rd__fill11_may()) {
        snprintf(line, sizeof(line),
                 "crack fill: with list 11: off (not the PowerVR or a desktop class: off on the "
                 "Arm, Qualcomm and Apple classes and other tile-based phones in this release, "
                 "issues 123, 179, 211)");
    } else {
        snprintf(line, sizeof(line), "crack fill: with list 11: off (self-test %s)",
                 s_sw.words[0] ? s_sw.words : "not run");
    }
    if (strcmp(line, s_sw.said) != 0) {
        snprintf(s_sw.said, sizeof(s_sw.said), "%s", line);
        rd__log("%s", line);
    }
}

void rd__fill11_force(int on)
{
    s_sw.force = on < 0 ? -1 : on != 0;
    note();
}

void rd__fill11_init(void)
{
    /* the scratch is kept (freed by rd__fill11_shutdown, as the count's
     * target); the self-test runs again for this renderer */
    s_sw.test = 0;
    s_sw.words[0] = '\0';
    s_sw.said[0] = '\0';
    s_open.serial = 0;
    s_open.land = UINT32_MAX;
    memset(&s_rep, 0, sizeof(s_rep));
    memset(&s_blk, 0, sizeof(s_blk));
}

static void scratchFree(void)
{
    if (s_scr.tex.id && g_rd.hasDevice) {
        rhi_destroy_texture(s_scr.tex);
    }
    memset(&s_scr, 0, sizeof(s_scr));
}

void rd__fill11_shutdown(void)
{
    /* g_rd.replayCounter starts again at rd_init: nothing of this
     * renderer's (the scratch's handle, the merged pass's serial, the
     * self-test's verdict) may reach the next one */
    scratchFree();
    s_sw.test = 0;
    s_sw.words[0] = '\0';
    s_sw.said[0] = '\0';
    s_open.serial = s_open.replay = s_open.landReplay = 0;
    s_open.land = UINT32_MAX;
    memset(&s_rep, 0, sizeof(s_rep));
}

static bool scratchEnsure(uint32_t w, uint32_t h)
{
    if (s_scr.tex.id && s_scr.w == w && s_scr.h == h) {
        return true;
    }
    scratchFree();
    s_scr.tex = rhi_create_texture(
        &(RhiTextureDesc){w, h, 1, RHI_FMT_D32F_S8, RHI_TEX_DEPTH_STENCIL, "rd fill11 depth", 0});
    if (!s_scr.tex.id) {
        return false;
    }
    s_scr.state = RHI_STATE_UNDEFINED;
    s_scr.w = w;
    s_scr.h = h;
    rd__log("crack fill: with list 11: the scratch depth made (%ux%u, D32F_S8, %.1f MB; never "
            "loaded or stored)",
            w, h, (double)w * h * 5.0 / 1048576.0);
    return true;
}

/* ------------------------------------------------------------ the keys */

RdPipeKeyInt rd__fill11_twin(const RdPipeKeyInt *k)
{
    RdPipeKeyInt t = *k;
    t.depthFmt = RHI_FMT_D32F_S8;
    t.gs.ztst = RD_ZTST_ALWAYS;
    t.gs.zwrite = RD_ZWRITE_OFF;
    return t;
}

static const int kKinds[4] = {RD_CRACK_KEY_FILL, RD_CRACK_KEY_FILL | RD_CRACK_KEY_RGB,
                              RD_CRACK_KEY_FILL1, RD_CRACK_KEY_FILL1 | RD_CRACK_KEY_RGB};

uint32_t rd__fill11_enumerate(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    if (!tried() || !g_rd.fs[RD_FS_CRACK_FILL].id) {
        return n;
    }
    for (int i = 0; i < 4; i++) {
        if ((kKinds[i] & 3) == RD_CRACK_KEY_FILL1 && !g_rd.fs[RD_FS_CRACK_FILL1].id) {
            continue;
        }
        const RdPipeKeyInt k = rd__crack_fill_key(kKinds[i]);
        const RdPipeKeyInt t = rd__fill11_twin(&k);
        n = rd__add_pipe_key(out, max, n, &t);
    }
    return n;
}

/* ------------------------------------------------------------- self-test */

#define T_W 256u
#define T_H 2u

/* rd_crack_fill.c swapLevel's: each row a permutation of 0..255 a channel */
static uint8_t level(uint32_t x, uint32_t y, uint32_t ch)
{
    static const uint32_t mul[2][4] = {{1u, 255u, 7u, 13u}, {3u, 5u, 11u, 1u}};
    static const uint32_t add[2][4] = {{0u, 255u, 0u, 0u}, {0u, 1u, 2u, 255u}};
    return (uint8_t)((x * mul[y][ch] + add[y][ch]) & 255u);
}

/* the other pattern a loaded target starts from: the rows swapped, the
 * channels turned by one */
static uint8_t level2(uint32_t x, uint32_t y, uint32_t ch)
{
    return level(x, 1u - y, (ch + 1u) & 3u);
}

/* one draw of key k (a twin) through the fill's no-crack path over src
 * (the depth slot the dummy: every texel alike, so no texel is a crack) into
 * dst, loaded or not, in a pass with depth attached (DONT_CARE both ways) */
static bool testDraw(RhiCommandList cl, RhiTexture src, RhiTexture dst, RhiState *dstState,
                     RhiTexture depth, RhiState *depthState, const RdPipeKeyInt *k, uint32_t mode,
                     RhiLoadOp load)
{
    const RhiPipeline pipe = rd__get_pipeline(k);
    if (!pipe.id) {
        return false;
    }
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    rd__transition(cl, depth, depthState, RHI_STATE_DEPTH_WRITE);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = load;
    p.colorCount = 1;
    p.depth.texture = depth;
    p.depth.depthLoad = RHI_LOAD_DONT_CARE;
    p.depth.stencilLoad = RHI_LOAD_DONT_CARE;
    p.depth.store = RHI_STORE_DONT_CARE;
    p.depth.stencilStore = RHI_STORE_DONT_CARE;
    p.width = T_W;
    p.height = T_H;
    rhi_cmd_begin_render_pass(cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)T_W, (float)T_H, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    const RhiRect all = {0, 0, T_W, T_H};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.param[0] = 1.0f; /* a reach of 1: the test runs, finds no crack */
    cb.param[1] = 1.0f;
    cb.param[2] = 1.0f / (float)RD_CRACK_BEHIND;
    cb.mode[0] = mode;
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = src;
    b[0].aspect = RHI_ASPECT_COLOR;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = g_rd.dummy;
    b[2].aspect = RHI_ASPECT_COLOR;
    const RhiBindGroup g = rd__replay_bind_group(g_rd.layoutTex, b, 3);
    if (g.id) {
        rd__set_pipeline(cl, pipe, k);
        rd__bind_uniform(cl, 0, rd__frame_group(T_W, T_H, 0.0f, 0.0f));
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(cl, 2, g);
        rhi_cmd_draw(cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst, dstState, RHI_STATE_COPY_SRC);
    return g.id != 0;
}

/* the four cases: the general twin with COPY and the early leave, the
 * reach-1 twin with COPY and KEEP_A (both write every texel: the swap's
 * path, read back as the source), each twin with RGB alone without COPY over
 * a target loaded with the other pattern (every texel discarded: read back
 * as that pattern) */
#define T_CASES 4

bool rd__fill11_selftest(void)
{
    s_sw.test = 0;
    s_sw.words[0] = '\0';
    const RdTargetRec *tc = sceneRec();
    RdCrackCounts cc;
    rd__crack_fill_counts(&cc);
    if (!g_rd.inited || !g_rd.hasDevice || !tried()) {
        note();
        return true;
    }
    if (!cc.enabled || !tc || tc->format != RHI_FMT_RGBA8_UNORM || !g_rd.fs[RD_FS_CRACK_FILL].id) {
        snprintf(s_sw.words, sizeof(s_sw.words), "not run: %s",
                 !cc.enabled ? "the crack fill off" : "no fill on this scene or backend");
        note();
        return true;
    }
    const RhiFormat fmt = tc->format;
    const uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC |
                           RHI_TEX_COPY_DST | rd__dst_target_usage(fmt, NULL) |
                           (tc->uintView ? (uint32_t)RHI_TEX_UINT_VIEW : 0u);
    const uint32_t a = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint32_t pitch = (T_W * 4u + a - 1u) / a * a;
    const uint64_t half = (uint64_t)pitch * T_H;
    RhiTexture src =
        rhi_create_texture(&(RhiTextureDesc){T_W, T_H, 1, fmt, usage, "rd fill11 test source", 0});
    RhiTexture depth = rhi_create_texture(&(RhiTextureDesc){
        T_W, T_H, 1, RHI_FMT_D32F_S8, RHI_TEX_DEPTH_STENCIL, "rd fill11 test depth", 0});
    RhiTexture dst[T_CASES];
    bool made = src.id && depth.id;
    for (int t = 0; t < T_CASES; t++) {
        dst[t] =
            rhi_create_texture(&(RhiTextureDesc){T_W, T_H, 1, fmt, usage, "rd fill11 test", 0});
        made = made && dst[t].id;
    }
    RhiBuffer up = rhi_create_buffer(
        &(RhiBufferDesc){half * 2u, RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "rd fill11 test"});
    uint8_t *m = up.id ? (uint8_t *)rhi_map_buffer(up) : NULL;
    const char *why = NULL;
    int bad = -1;
    uint8_t got[4] = {0}, want[4] = {0};
    if (!made || !m) {
        why = "no memory for its textures";
    } else {
        for (uint32_t y = 0; y < T_H; y++) {
            for (uint32_t x = 0; x < T_W; x++) {
                for (uint32_t ch = 0; ch < 4u; ch++) {
                    m[(size_t)y * pitch + x * 4u + ch] = level(x, y, ch);
                    m[half + (size_t)y * pitch + x * 4u + ch] = level2(x, y, ch);
                }
            }
        }
        rhi_unmap_buffer(up);
        m = NULL;
        RhiCommandList cl =
            rd__begin_own_frame(64u * 1024u) ? rhi_begin_commands() : (RhiCommandList){0};
        if (!cl.id) {
            why = "no command list";
        } else {
            /* the dummy (the depth slot) uploaded and in SHADER_READ first:
             * nothing before this frame may have done it (the swap's
             * self-test's note, rd_crack_fill.c) */
            rd__upload_textures(cl);
            RhiState ss = RHI_STATE_UNDEFINED, zs = RHI_STATE_UNDEFINED;
            RhiState ds[T_CASES];
            for (int t = 0; t < T_CASES; t++) {
                ds[t] = RHI_STATE_UNDEFINED;
            }
            rd__transition(cl, src, &ss, RHI_STATE_COPY_DST);
            rhi_cmd_copy_buffer_to_texture(cl, up, 0, pitch, src, 0, (RhiRect){0, 0, T_W, T_H});
            rd__transition(cl, src, &ss, RHI_STATE_SHADER_READ);
            for (int t = 2; t < T_CASES; t++) {
                rd__transition(cl, dst[t], &ds[t], RHI_STATE_COPY_DST);
                rhi_cmd_copy_buffer_to_texture(cl, up, half, pitch, dst[t], 0,
                                               (RhiRect){0, 0, T_W, T_H});
            }
            const bool one = g_rd.fs[RD_FS_CRACK_FILL1].id != 0;
            const int kind[T_CASES] = {
                RD_CRACK_KEY_FILL, one ? RD_CRACK_KEY_FILL1 : RD_CRACK_KEY_FILL,
                RD_CRACK_KEY_FILL | RD_CRACK_KEY_RGB,
                (one ? RD_CRACK_KEY_FILL1 : RD_CRACK_KEY_FILL) | RD_CRACK_KEY_RGB};
            const uint32_t mode[T_CASES] = {F11_MODE_COPY | F11_MODE_EARLY,
                                            F11_MODE_COPY | F11_MODE_KEEP_A, 0u, 0u};
            bool drawn = true;
            for (int t = 0; t < T_CASES && drawn; t++) {
                const RdPipeKeyInt k0 = rd__crack_fill_key(kind[t]);
                const RdPipeKeyInt k = rd__fill11_twin(&k0);
                drawn = testDraw(cl, src, dst[t], &ds[t], depth, &zs, &k, mode[t],
                                 t < 2 ? RHI_LOAD_DONT_CARE : RHI_LOAD_LOAD);
            }
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
            if (!drawn) {
                why = "its pipelines missing";
            }
        }
        static uint8_t back[T_W * T_H * 4u + 4096u];
        for (int t = 0; t < T_CASES && !why && bad < 0; t++) {
            uint32_t rp = 0;
            if (!rhi_readback_texture(dst[t], RHI_ASPECT_COLOR, back, sizeof(back), &rp) ||
                rp < T_W * 4u || (size_t)rp * T_H > sizeof(back)) {
                why = "its readback failed";
                break;
            }
            for (uint32_t i = 0; i < T_W * T_H && bad < 0; i++) {
                const uint32_t x = i % T_W, y = i / T_W;
                for (uint32_t ch = 0; ch < 4u; ch++) {
                    got[ch] = back[(size_t)y * rp + x * 4u + ch];
                    want[ch] = t < 2 ? level(x, y, ch) : level2(x, y, ch);
                }
                if (memcmp(got, want, 4) != 0) {
                    bad = (int)i + t * (int)(T_W * T_H);
                }
            }
        }
    }
    if (m) {
        rhi_unmap_buffer(up);
    }
    rhi_wait_idle();
    for (int t = 0; t < T_CASES; t++) {
        if (dst[t].id) {
            rhi_destroy_texture(dst[t]);
        }
    }
    if (src.id) {
        rhi_destroy_texture(src);
    }
    if (depth.id) {
        rhi_destroy_texture(depth);
    }
    if (up.id) {
        rhi_destroy_buffer(up);
    }
    s_sw.test = !why && bad < 0 ? 1 : 2;
    if (s_sw.test == 1) {
        snprintf(s_sw.words, sizeof(s_sw.words),
                 "passed: %d draws of %u texels through the fill's depth twins read back "
                 "unchanged",
                 T_CASES, T_W * T_H);
    } else if (why) {
        snprintf(s_sw.words, sizeof(s_sw.words), "not run: %s", why);
    } else {
        snprintf(s_sw.words, sizeof(s_sw.words),
                 "failed at texel %d: %02X%02X%02X%02X for %02X%02X%02X%02X", bad, got[0], got[1],
                 got[2], got[3], want[0], want[1], want[2], want[3]);
    }
    note();
    return s_sw.test == 1;
}

/* --------------------------------------------------------------- the walk */

enum { K_WAIT, K_LAND, K_FLUSH };

/* a screen command the replay does not draw: deferred text (drawn on the
 * output), a fan the light plan skips, a sprite the fades fold applies at
 * the reduction (rd__glow_light_skip includes rd__fades_fold_skip) */
static bool screenUndrawn(const RdCmd *c, uint32_t ord)
{
    return (g_rd.deferText && c->b[3] == RD_SCREEN_TEXT_QUADS) || rd__glow_light_skip(ord);
}

/* a draw under st on SCENE with SCENE's depth bound that neither tests nor
 * writes the depth and reads nothing of SCENE (the join's checks,
 * rd_late_pass.c, at the state's level; the wrap path draws through a pass of
 * its own) */
static bool stateQualifies(const RdStateBlock *st, int *why)
{
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
    if ((d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) || d->zwrite == RD_ZWRITE_ON ||
        d->test.date != RD_DATE_OFF || st->aa1) {
        *why = RD_FILL11_WHY_DEPTH;
        return false;
    }
    *why = RD_FILL11_WHY_CMD;
    return isScene(st->color) && isScene(st->depth) && !(d->texEnabled && !t) &&
           !(t && t->kind == RD_TEXKIND_TARGET && isScene(t->target)) && !rd__wrap_applies(st);
}

/* the merged pass from list 11's command c to the end of today's list 11
 * pass: every later command up to the first one that ends that pass anyway
 * (a draw, sprite or copy on another target, the reduction, the walk's end)
 * is state, a NOP, overlay text, a screen draw not drawn, or a screen draw of
 * list 11 on SCENE that qualifies.  False (keep today's passes) at a draw on
 * SCENE that does not, at a clear (it may stay pending in today's pass), and
 * at any command of another kind on SCENE */
static bool laterOk(const Replay *r, const RdFrame *f, const RdCmd *c)
{
    const RdCmdList *l11 = &f->lists[11];
    if (c < l11->cmds || c >= l11->cmds + l11->count) {
        return false;
    }
    RdStateBlock st = r->st;
    uint32_t ord = g_rdStoreOrd + 1u;
    for (int l = 11; l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        const uint32_t i0 = l == 11 ? (uint32_t)(c - list->cmds) + 1u : 0u;
        for (uint32_t i = i0; i < list->count; i++, ord++) {
            const RdCmd *e = &list->cmds[i];
            if (rd__cmd_is_state(e->type)) {
                (void)rd__apply_state(&st, e);
                continue;
            }
            int why;
            switch (e->type) {
            case RDC_NOP:
            case RDC_OVERLAY_TEXT:
                continue;
            case RDC_SCREEN:
                if (screenUndrawn(e, ord)) {
                    continue;
                }
                if (!isScene(st.color)) {
                    return true; /* another target: its own pass, as today */
                }
                if (l == 11 && stateQualifies(&st, &why)) {
                    continue;
                }
                return false;
            case RDC_POST_STUB:
            case RDC_MESH:
            case RDC_SKINNED:
            case RDC_GRID:
            case RDC_PARTICLES:
                return !isScene(st.color);
            case RDC_COPY:
                return !isScene(e->u[0]) && !isScene(e->u[1]);
            default:
                return false;
            }
        }
    }
    return true;
}

/* what a held fill does at command c of list (state r->st before c's own
 * state): waits past it, lands at it (list 11's first screen draw on SCENE),
 * or is drawn before it in a pass of its own (*why) */
static int classOf(const Replay *r, const RdFrame *f, const RdCmd *c, int list, int *why)
{
    const RdStateBlock *st = &r->st;
    *why = RD_FILL11_WHY_CMD;
    if (rd__cmd_is_state(c->type)) {
        /* the upscaler reads SCENE at its marker */
        return c->type == RDC_UPSCALE_POINT && rd__upscale_point_on() ? K_FLUSH : K_WAIT;
    }
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT: /* collected by the present */
        return K_WAIT;
    case RDC_SCREEN:
        if (screenUndrawn(c, g_rdStoreOrd)) {
            return K_WAIT;
        }
        if (list == 11 && isScene(st->color)) {
            RhiRect a;
            if (!stateQualifies(st, why)) {
                return K_FLUSH;
            }
            if (rd__pass_area_on() &&
                rd__pass_area_rect(st->color, sceneRec(), g_rdStoreOrd, &a, NULL)) {
                *why = RD_FILL11_WHY_AREA;
                return K_FLUSH;
            }
            if (!laterOk(r, f, c)) {
                *why = RD_FILL11_WHY_LATER;
                return K_FLUSH;
            }
            return K_LAND;
        }
        break;
    case RDC_POST_STUB:
        if (rd__glow_light_undrawn(c)) {
            return K_WAIT; /* skipped or a lazy clear (SCENE's border repeat: its hook) */
        }
        break;
    default:
        break;
    }
    /* the late pass's class (rd_late_pass.c): clears and copies of other
     * targets, draws and sprites into other targets; and here besides no
     * SCENE depth bound at all (a pass that binds it may drop its store,
     * which the held fill reads later) and no SCENE sampled in any form (the
     * swap left SCENE's colour texture undefined until the fill) */
    int lw = RD_LATE_WHY_NONE;
    if (rd__late_pass_class(f, c, st, 0, &lw) != RD_LATE_HOLD) {
        *why = lw == RD_LATE_WHY_DEPTH ? RD_FILL11_WHY_DEPTH : RD_FILL11_WHY_CMD;
        return K_FLUSH;
    }
    if (c->type == RDC_SCREEN || c->type == RDC_POST_STUB ||
        (c->type >= RDC_MESH && c->type <= RDC_PARTICLES)) {
        const RdDrawState *d = &st->ds;
        const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
        if (isScene(st->depth)) {
            *why = RD_FILL11_WHY_DEPTH;
            return K_FLUSH;
        }
        if (isScene(st->color) || (d->texEnabled && !t) ||
            (t && t->kind == RD_TEXKIND_TARGET && isScene(t->target))) {
            return K_FLUSH;
        }
    }
    return K_WAIT;
}

bool rd__fill11_take(const Replay *r, const RdFrame *f, const RdCmd *c, int list,
                     const RdTargetRec *scene)
{
    if (!rd__fill11_on() || !r || !f || !c || !scene) {
        return false;
    }
    int no = -1, why = 0;
    if (list > 11) {
        no = RD_FILL11_NOT_LIST;
    } else if (rd__replay_msaa_want() > 1u) {
        no = RD_FILL11_NOT_MSAA;
    } else if (g_rd.dstMode != RD_BLEND_EXACT_OFF || rd__uber_on()) {
        no = RD_FILL11_NOT_BLEND;
    } else if (rd__upscale_point_on()) {
        no = RD_FILL11_NOT_SPLIT;
    } else if (g_rdWin.first) {
        no = RD_FILL11_NOT_WINDOW;
    } else if (rd__fx_defer_on()) {
        no = RD_FILL11_NOT_DEFER;
    } else if (classOf(r, f, c, list, &why) == K_FLUSH) {
        no = RD_FILL11_NOT_NEXT; /* drawn here anyway: no hold */
    } else if (!scratchEnsure(scene->tw, scene->th)) {
        no = RD_FILL11_NOT_MEMORY;
    }
    if (no >= 0) {
        rd__fill11_refused(no);
        return false;
    }
    return true;
}

/* the merged pass holds command c: state, NOPs, overlay text and list
 * 11's screen draws on SCENE (rd__fill11_draw decides each draw) */
static bool keeps(const Replay *r, const RdCmd *c, int list)
{
    if (rd__cmd_is_state(c->type)) {
        return c->type != RDC_UPSCALE_POINT || !rd__upscale_point_on();
    }
    return c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT ||
           (c->type == RDC_SCREEN && list == 11 && isScene(r->st.color));
}

static bool mergedOpen(const Replay *r)
{
    return s_open.serial != 0 && s_open.replay == g_rd.replayCounter && r->passOpen &&
           r->passSerial == s_open.serial;
}

void rd__fill11_at(Replay *r, const RdFrame *f, const RdCmd *c, int list)
{
    if (s_open.serial) {
        if (mergedOpen(r)) {
            if (keeps(r, c, list)) {
                return;
            }
            rd__replay_end_pass(r); /* where today's list 11 pass ends (laterOk) */
        }
        s_open.serial = 0;
    }
    s_open.land = UINT32_MAX;
    if (!rd__crack_fill11_pending()) {
        return;
    }
    int why = RD_FILL11_WHY_CMD;
    const int k = classOf(r, f, c, list, &why);
    if (k == K_LAND) {
        s_open.land = g_rdStoreOrd; /* rd__fill11_draw at its draw */
        s_open.landReplay = g_rd.replayCounter;
    } else if (k == K_FLUSH) {
        rd__crack_fill11_own(r, why);
    }
}

/* the draw ds of r's state qualifies for the merged pass */
static bool drawQualifies(const Replay *r, const DrawSetup *ds)
{
    const RdTargetRec *sc = sceneRec();
    int why;
    return sc && ds->tc == sc && sc->withDepth && sc->depth.id && ds->tdId == r->st.depth &&
           isScene(ds->tdId) && ds->depthFmt == RHI_FMT_D32F_S8 && stateQualifies(&r->st, &why) &&
           !ds->dateStencil && !r->dateStNow && !r->dateDstNow && !r->tileNow &&
           !rd__stencil_live(rd__replay_stencil(), sc->depth.id) && g_rd.passSamples <= 1u;
}

/* the merged pass begun on SCENE's colour with the scratch depth standing
 * for SCENE's (r->passDepth: the draws' tdId, so bindDrawEx keeps them in
 * it), as beginPass begins list 11's pass but for the depth: the open pass
 * ended first (its pending clears recorded, as beginPass's endPass does),
 * the colour readable as rd__pass_wants_readable has it */
static void land(Replay *r, const DrawSetup *ds)
{
    RdTargetRec *tc = ds->tc;
    const uint32_t cid = r->st.color, did = ds->tdId;
    rd__replay_end_pass(r);
    RhiCommandList cl = rd__replay_cl();
    const bool readable = rd__pass_wants_readable(tc);
    rd__transition(cl, tc->color, &tc->colorState,
                   readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
    rd__transition(cl, tc->depth, &tc->depthState, RHI_STATE_DEPTH_READ); /* the fill samples it */
    rd__transition(cl, s_scr.tex, &s_scr.state, RHI_STATE_DEPTH_WRITE);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    /* the swap's fill writes every texel first */
    p.color[0].load = rd__crack_fill11_copy() ? RHI_LOAD_DONT_CARE : RHI_LOAD_LOAD;
    p.color[0].readable = readable;
    p.colorCount = 1;
    p.depth.texture = s_scr.tex;
    p.depth.depthLoad = RHI_LOAD_DONT_CARE;
    p.depth.stencilLoad = RHI_LOAD_DONT_CARE;
    p.depth.store = RHI_STORE_DONT_CARE;
    p.depth.stencilStore = RHI_STORE_DONT_CARE;
    p.width = tc->tw;
    p.height = tc->th;
    /* counted as a pass without a depth the plan holds (its pass counter) */
    RhiLoadOp dl = RHI_LOAD_DONT_CARE, sl = RHI_LOAD_DONT_CARE;
    RhiStoreOp dst = RHI_STORE_DONT_CARE, sst = RHI_STORE_DONT_CARE;
    float cd = 0.0f;
    uint8_t cs = 0;
    rd__store_plan_pass(NULL, 0, &dl, &sl, &dst, &sst, &cd, &cs);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, cid, tc->tw, tc->th, r->perfPost);
    /* whole: classOf refused a command where the area applies */
    rd__pass_area_begin(&p, tc, cid, r->passSerial + 1u, false, false);
    if (p.area.w || p.area.h) {
        p.area = (RhiRect){0, 0, 0, 0};
        static bool s_area;
        if (!s_area) {
            s_area = true;
            rd__log("crack fill: with list 11: the merged pass was given an area the check before "
                    "it did not see: drawn whole (reported once)");
        }
    }
    rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, cid, tc->tw, tc->th, did, tc->tw,
                                                tc->th, 0, r->perfPost));
    rhi_cmd_begin_render_pass(cl, &p);
    rd__replay_viewport(tc->tw, tc->th);
    r->passOpen = 1;
    r->passColor = cid;
    r->passDepth = did;
    r->passSerial++;
    r->passStorage = 0;
    r->passReadable = readable;
    r->passDepthReadable = 0;
    g_rd.passSamples = 1;
    s_open.serial = r->passSerial;
    s_open.replay = g_rd.replayCounter;
    rd__crack_fill11_land(r);
    rep();
    s_rep.c.landed++;
    s_rep.texels += (uint64_t)tc->tw * tc->th;
    s_blk.landed++;
    s_blk.texels += (uint64_t)tc->tw * tc->th;
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("crack fill: drawn as the first draw of list 11's pass on SCENE over a scratch "
                "depth (frame %u), not in a pass of its own: SCENE's colour stored once, list "
                "11's draws with their own pipelines (reported once)",
                g_rdPerf.frame);
    }
}

void rd__fill11_draw(Replay *r, DrawSetup *ds)
{
    if (mergedOpen(r)) {
        /* a draw on SCENE with its depth that uses the depth: a pass of its
         * own with SCENE's depth (laterOk keeps this from the dumps' forms) */
        if (r->passColor == r->st.color && ds->tdId == r->passDepth && !drawQualifies(r, ds)) {
            rd__replay_end_pass(r);
            s_open.serial = 0;
            rep();
            s_rep.c.split++;
            s_blk.split++;
        }
        return;
    }
    if (s_open.land != g_rdStoreOrd || s_open.landReplay != g_rd.replayCounter ||
        !rd__crack_fill11_pending()) {
        return;
    }
    s_open.land = UINT32_MAX;
    if (!drawQualifies(r, ds) || (r->passOpen && isScene(r->passColor))) {
        rd__crack_fill11_own(r, RD_FILL11_WHY_DRAW);
        return;
    }
    land(r, ds);
}

void rd__fill11_border(Replay *r)
{
    if (rd__crack_fill11_pending()) {
        rd__crack_fill11_own(r, RD_FILL11_WHY_BORDER); /* before the repeat copies */
    }
}

/* ------------------------------------------------------------ counters */

void rd__fill11_flushed(int why)
{
    if (why < 0 || why >= RD_FILL11_WHY_COUNT) {
        why = RD_FILL11_WHY_CMD;
    }
    rep();
    s_rep.c.flushed++;
    s_rep.c.why = (uint32_t)why;
    s_rep.why[why]++;
    s_blk.flushed++;
    s_blk.why[why]++;
}

void rd__fill11_refused(int why)
{
    if (why < 0 || why >= RD_FILL11_NOT_COUNT) {
        why = RD_FILL11_NOT_NEXT;
    }
    rep();
    s_rep.c.refused++;
    s_rep.not_[why]++;
    s_blk.refused++;
    s_blk.not_[why]++;
}

void rd__fill11_replay(RdFill11Counts *out)
{
    if (s_rep.replay != g_rd.replayCounter) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = s_rep.c;
}

/* " (n why, ...)" of the non-zero counts n[k] into out at *at */
static void reasons(char *out, size_t size, size_t *at, const uint64_t *n, const char *const *w,
                    int count)
{
    bool first = true;
    for (int k = 0; k < count; k++) {
        if (!n[k] || *at >= size) {
            continue;
        }
        const int m = snprintf(out + *at, size - *at, "%s%llu %s", first ? " (" : ", ",
                               (unsigned long long)n[k], w[k]);
        if (m > 0) {
            *at = *at + (size_t)m < size ? *at + (size_t)m : size - 1;
        }
        first = false;
    }
    if (!first && *at < size) {
        const int m = snprintf(out + *at, size - *at, ")");
        if (m > 0) {
            *at = *at + (size_t)m < size ? *at + (size_t)m : size - 1;
        }
    }
}

void rd__fill11_block(void)
{
    note();
    if (!s_blk.landed && !s_blk.flushed && !s_blk.refused && !s_blk.split) {
        return;
    }
    char why[384] = "", not_[384] = "";
    size_t a = 0, b = 0;
    reasons(why, sizeof(why), &a, s_blk.why, kWhy, RD_FILL11_WHY_COUNT);
    reasons(not_, sizeof(not_), &b, s_blk.not_, kNot, RD_FILL11_NOT_COUNT);
    rd__log("crack fill: with list 11: %llu drawn in list 11's pass over the scratch depth (%.1f "
            "MB of colour not loaded and as many not stored), %llu held and drawn in a pass of "
            "their own after all%s, %llu kept in their own pass%s; %llu merged passes ended by a "
            "draw that uses SCENE's depth",
            (unsigned long long)s_blk.landed, (double)s_blk.texels * 4.0 / 1048576.0,
            (unsigned long long)s_blk.flushed, why, (unsigned long long)s_blk.refused, not_,
            (unsigned long long)s_blk.split);
    memset(&s_blk, 0, sizeof(s_blk));
}

void rd__fill11_stats(char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    const bool now = s_rep.replay == g_rd.replayCounter;
    const RdFill11Counts c = now ? s_rep.c : (RdFill11Counts){0, 0, 0, 0, 0};
    uint64_t why[RD_FILL11_WHY_COUNT], no[RD_FILL11_NOT_COUNT];
    for (int k = 0; k < RD_FILL11_WHY_COUNT; k++) {
        why[k] = now ? s_rep.why[k] : 0u;
    }
    for (int k = 0; k < RD_FILL11_NOT_COUNT; k++) {
        no[k] = now ? s_rep.not_[k] : 0u;
    }
    const int m = snprintf(
        out, size,
        "crack fill with list 11 %s (self-test %s): %u drawn in list 11's pass (%.2f MB of "
        "colour not loaded and as many not stored), %u drawn in a pass of their own",
        rd__fill11_on() ? "on" : "off",
        s_sw.test == 1   ? "passed"
        : s_sw.test == 2 ? "failed"
                         : "not run",
        c.landed, now ? (double)s_rep.texels * 4.0 / 1048576.0 : 0.0, c.flushed);
    size_t at = m > 0 && (size_t)m < size ? (size_t)m : size - 1;
    reasons(out, size, &at, why, kWhy, RD_FILL11_WHY_COUNT);
    if (at < size) {
        const int n = snprintf(out + at, size - at, ", %u kept in their own pass", c.refused);
        if (n > 0) {
            at = at + (size_t)n < size ? at + (size_t)n : size - 1;
        }
    }
    reasons(out, size, &at, no, kNot, RD_FILL11_NOT_COUNT);
    if (at < size) {
        snprintf(out + at, size - at, "; %u merged passes ended by a draw that uses SCENE's depth",
                 c.split);
    }
}
