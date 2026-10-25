/* rd_crack_fill.c: the crack filler above 1x (issue 26; docs/port/RENDER.md,
 * "Crack filling above 1x").
 *
 * The VU truncates every vertex to the GS's 1/16 pixel grid, each part of
 * the world through its own node matrix, so two triangles that meet on the
 * PS2 can leave a crack of 1/16 GS pixel between them (DIVERGENCES.md
 * VD-20; at a scale that is not a whole number the GPU's second snap moves
 * it, VD-19).  A pixel whose centre falls into it keeps what lies behind:
 * the clear's depth, GS Z 0, which the fog's table takes as its entry 0
 * (fog_lut.hlsl), a dot of the full fog colour, or a farther surface, a dot
 * of it at its own, deeper fog.  At 1x that is the PS2's own picture.  Above 1x a
 * crack catches texel centres along its whole length, and the dots run
 * along the seams of walls and floors (Goblan's 4K screenshots of v0.8.4).
 *
 * Under the Enhanced preset with SCENE's scale above 1 on either axis, the
 * fog of SCENE (rd_replay.c doFog, after the pass before it ends and before
 * the fog reads the depth) is preceded by one pass over SCENE with its
 * depth attached: crack_fill_ps (blit.hlsl) reads a copy of SCENE's colour
 * (its snapshot texture, rd__replay_take_snap) and of its depth (the fog's
 * depth copy object, rd__depth_copy_take: the copy path's own, made here on
 * the other paths).  A texel in a run of at most R texels along its row or
 * its column that has at each end a texel in front of it (written, and
 * nearer by more than 1 / RD_CRACK_BEHIND of its own depth) is a crack when
 * no draw covered it (depth 0), or when it is drawn behind both ends by a
 * step in depth: the ends within half the drop of each other (two parts of
 * one surface) and the texel behind each end's surface carried on to it by
 * more than the share (a concave fold between two faces stays).  The crack
 * takes the nearer end's colour and depth (SV_Depth, test ALWAYS); every
 * other texel is discarded.  R on an axis is ceil(scale / 2) above 1x, at
 * most 3, else 0: the 1/16 pixel crack is under one texel wide up to 16x,
 * and a slit of a GS pixel or more (2 texels at 2x) is the game's and
 * stays.  The depth goes with the colour so the fog paints the neighbour's
 * fog level, and the draws after the fog (the lists' Z tests) see a surface
 * there.  Exact copies: no filtering, no colour of its own.
 *
 * What the cracks hold: the clear's depth where nothing lies behind the
 * seam (the sky of the castle's courtyard at 4K: 727 texels of the fog's
 * entry 0 in each issue-91 dump), a farther surface where something does
 * (a wall behind a pillar's seam: tens of texels a frame in the corpus
 * rooms at 2x, measured with a fog table that writes each texel's depth
 * index).  The corpus rooms at a tick and a whole scale have no texel of
 * depth 0 at all after the world's draws (the clear's colour patched to
 * magenta shows on none before the fog, and the table's entry 0 patched to
 * magenta on none after it): their dark texels of the clear's RGB 1,1,1
 * are the walls' mortar, not cracks.
 *
 * Not drawn: at scale 1 on both axes, under the Original preset, on any
 * replay with MSAA asked for (the fog then reads the multisampled scene or
 * the resolve's smallest sample, DIVERGENCES.md VD-22), in the fog's
 * self-test, on a fog of another target than SCENE, without a sampleable
 * depth, and with [dev] crack_fill = false (ICO_RD_CRACK_FILL=0; the replay
 * tool's --no-crack-fill).
 *
 * The fog's local read (RD_FOG_LOCAL, a tile-based GPU's path: the fog
 * draws in the scene pass, reading the depth as an input attachment): the
 * copies end the scene pass, and the fill is drawn in the pass begun again
 * over SCENE and its depth, both loaded, the depth readable
 * (rd__replay_begin_pass, as bindDraw begins it), which is left open: the
 * fog keeps it (doFog) and its local-read barrier holds the fill's depth
 * writes before the fog reads them.  The layout of a readable depth
 * (RENDERING_LOCAL_READ, vk_cmd.c rhi_cmd_begin_render_pass) is an
 * attachment the depth tests write, as every scene draw in that pass
 * does, so the fill's test-ALWAYS writes are allowed there.  SCENE's colour
 * and depth are stored and loaded once fewer than with a fill pass of its
 * own (about 36 MB a replay at 1664 x 896 where the depth and stencil take
 * 8 bytes a texel).  A pass begun without the depth readable is ended
 * after the fill, as on the other paths (its line once).
 *
 * On by default on every device class (RdProfile.crackFill); [dev]
 * crack_fill = false (ICO_RD_CRACK_FILL=0) turns it off, the A/B.  Its
 * GPU time is the effect "crack fill" (RD_PERF_POST_CRACK: the copies, the
 * count and the draw) in the 10 s "effects:" line, the replay tool's
 * --stats and --pass-times.
 *
 * The counters: the filled pixels are counted on the GPU only for the
 * tests and the replay tool's --stats (rd__crack_fill_count_all), never in
 * the game: crack_count_ps runs the same test over 16 x 16 texels a texel
 * into a small RGBA8 target (n & 255, n >> 8), copied into a readback
 * buffer and summed once the GPU is past that replay (RHI_FRAMES_IN_FLIGHT
 * replays later, as the fog's edge probe).  A missing count pipeline
 * leaves the fill drawn, uncounted.
 * The lines:
 *   rd: crack fill: on above 1x (scale 2x2, reach 1x1 texels: a run of
 *       texels no draw covered, or drawn over 1/64 of the depth behind both
 *       its ends, with a texel in front at each end takes the nearer end);
 *       [dev] crack_fill = false turns it off      (start, a new scale;
 *       " (drawn inside the scene pass with the local fog)" after it once
 *       a fill was drawn so, and again without it when one is not)
 *   rd: crack fill: T replays filled (scale SXxSY; reach RXxRY texels; not
 *       counted in the game[; F inside the scene pass with the local
 *       fog])                                      (10 s summary)
 *   rd: crack fill: N pixels filled over M replays counted of T filled
 *       (max K a replay; scale SXxSY; reach RXxRY texels[; F inside ...])
 *       (the same, counting: the tests, the replay tool's --stats)
 *   rd: crack fill: the pass after the copies was not begun with the depth
 *       readable: the fill ends it and the fog begins it again (reported
 *       once)                                      (events)
 *   rd: crack fill: N fogged replays above 1x not filled (...)   (the same)
 *   rd: crack fill: not drawn ...: <reason> (reported once)   (events) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* crack_count_ps's block (blit.hlsl CRACK_BLOCK) */
#define CRACK_BLOCK 16u

enum { SKIP_MSAA, SKIP_DEPTH, SKIP_PIPES, SKIP_MEMORY, SKIP_PASS, SKIP_COUNT };

static const char *const kSkipWhy[SKIP_COUNT] = {
    ("MSAA is asked for (its resolve gives a crack's pixel the surfaces beside it unless "
     "more than half its samples lie in the crack: DIVERGENCES.md VD-22)"),
    "the scene's depth cannot be sampled on this device",
    "its shaders or pipelines are missing",
    "no memory for the copies of the scene's colour and depth",
    "the scene pass the fog keeps could not be begun after the copies",
};

/* why the switch is what it is (the state line) */
enum { WHY_PROFILE, WHY_DEV, WHY_FORCED };

#define FOLD_TEXT " (drawn inside the scene pass with the local fog)"

static struct {
    bool enabled; /* ICO_RD_CRACK_FILL, the profile, or the replay tool's switch */
    int why;      /* WHY_* */
    bool countAll;
    bool fold; /* the last fill drawn in the pass the local fog keeps */
    /* the count's target and readback (one in flight) */
    RhiTexture countTex;
    RhiState countState;
    uint32_t cw, ch;
    RhiBuffer buf;
    uint64_t bufSize;
    bool pending;
    uint32_t pendingReplay, pendingW, pendingH, pendingPitch;
    /* since start */
    uint64_t filled, counted, pixels, skipped, folded;
    uint32_t last, max;
    float sx, sy;
    uint32_t rx, ry;
    /* the 10 s block's (rd__crack_fill_block) */
    uint64_t bFilled, bCounted, bPixels, bFolded, bSkip[SKIP_COUNT];
    uint32_t bMax;
    bool skipSaid[SKIP_COUNT];
    char noted[384]; /* the last state line */
} s_cf;

/* the replay tool's --no-crack-fill and the tests: -1 none, else the switch */
static int s_forced = -1;

uint32_t rd__crack_fill_reach(float scale)
{
    if (!(scale > 1.0f)) {
        return 0u;
    }
    const float r = ceilf(scale * 0.5f);
    return r >= (float)RD_CRACK_REACH_MAX ? RD_CRACK_REACH_MAX : (uint32_t)r;
}

static RdTargetRec *sceneRec(void)
{
    return rd__target_rec(RD_TARGET_SCENE + 1u);
}

/* the state line, logged when it changes */
static void note(void)
{
    if (!g_rd.hasDevice) {
        return;
    }
    const RdTargetRec *t = sceneRec();
    const float sx = t ? t->sx : 1.0f, sy = t ? t->sy : 1.0f;
    char line[sizeof(s_cf.noted)];
    /* on by [dev] where the profile has it off (no class today) */
    const char *dev = s_cf.enabled && s_cf.why == WHY_DEV && !g_rd.profile.crackFill
                          ? " (on by [dev] crack_fill = true)"
                          : "";
    if (!s_cf.enabled) {
        snprintf(line, sizeof(line), "crack fill: off (%s)",
                 s_cf.why == WHY_FORCED ? "the replay tool's --no-crack-fill"
                 : s_cf.why == WHY_DEV  ? "[dev] crack_fill = false, ICO_RD_CRACK_FILL=0"
                                        : "the device profile");
    } else if (g_rd.settings.preset != RD_PRESET_ENHANCED) {
        snprintf(line, sizeof(line),
                 "crack fill: on above 1x, not drawn under the Original preset; [dev] "
                 "crack_fill = false turns it off%s",
                 dev);
    } else if (!(sx > 1.0f || sy > 1.0f)) {
        snprintf(line, sizeof(line),
                 "crack fill: on above 1x, not drawn at scale %.4gx%.4g (the GS's own grid); "
                 "[dev] crack_fill = false turns it off%s",
                 (double)sx, (double)sy, dev);
    } else {
        snprintf(line, sizeof(line),
                 "crack fill: on above 1x (scale %.4gx%.4g, reach %ux%u texels: a run of texels "
                 "no draw covered, or drawn over 1/%u of the depth behind both its ends, with a "
                 "texel in front at each end takes the nearer end); [dev] crack_fill = false "
                 "turns it off%s%s",
                 (double)sx, (double)sy, rd__crack_fill_reach(sx), rd__crack_fill_reach(sy),
                 RD_CRACK_BEHIND, s_cf.fold ? FOLD_TEXT : "", dev);
    }
    if (strcmp(line, s_cf.noted) != 0) {
        snprintf(s_cf.noted, sizeof(s_cf.noted), "%s", line);
        rd__log("%s", line);
    }
}

void rd__crack_fill_init(void)
{
    const RhiTexture tex = s_cf.countTex;
    const RhiBuffer buf = s_cf.buf;
    const bool countAll = s_cf.countAll;
    memset(&s_cf, 0, sizeof(s_cf));
    s_cf.countTex = tex; /* freed by rd__crack_fill_shutdown */
    s_cf.buf = buf;
    s_cf.countAll = countAll;
    /* the replay tool's switch, else [dev] crack_fill (0, false, off or no:
     * off; any other word: on), else the device profile's default */
    const char *env = getenv("ICO_RD_CRACK_FILL");
    if (s_forced >= 0) {
        s_cf.enabled = s_forced != 0;
        s_cf.why = WHY_FORCED;
    } else if (env && env[0]) {
        s_cf.enabled = !(strcmp(env, "0") == 0 || strcmp(env, "false") == 0 ||
                         strcmp(env, "off") == 0 || strcmp(env, "no") == 0);
        s_cf.why = WHY_DEV;
    } else {
        s_cf.enabled = g_rd.profile.crackFill;
        s_cf.why = WHY_PROFILE;
    }
}

void rd__crack_fill_scene_made(void)
{
    s_cf.pending = false; /* a count at the old size is dropped */
    note();
}

void rd__crack_fill_set_enabled(bool on)
{
    s_forced = on ? 1 : 0;
    s_cf.enabled = on;
    s_cf.why = WHY_FORCED;
    note();
}

void rd__crack_fill_count_all(bool on)
{
    s_cf.countAll = on;
}

static void countFree(void)
{
    if (s_cf.countTex.id && g_rd.hasDevice) {
        rhi_destroy_texture(s_cf.countTex);
    }
    if (s_cf.buf.id && g_rd.hasDevice) {
        rhi_destroy_buffer(s_cf.buf);
    }
    s_cf.countTex = (RhiTexture){0};
    s_cf.buf = (RhiBuffer){0};
    s_cf.countState = RHI_STATE_UNDEFINED;
    s_cf.cw = s_cf.ch = 0;
    s_cf.bufSize = 0;
    s_cf.pending = false;
}

void rd__crack_fill_shutdown(void)
{
    countFree();
}

/* the count of a replay the GPU is past (now: the caller waited for it) */
static void harvest(bool now)
{
    if (!s_cf.pending || (!now && g_rd.replayCounter < s_cf.pendingReplay + RHI_FRAMES_IN_FLIGHT)) {
        return;
    }
    s_cf.pending = false;
    const uint8_t *m = (const uint8_t *)rhi_map_buffer(s_cf.buf);
    if (!m) {
        return;
    }
    uint32_t n = 0;
    for (uint32_t y = 0; y < s_cf.pendingH; y++) {
        const uint8_t *row = m + (size_t)y * s_cf.pendingPitch;
        for (uint32_t x = 0; x < s_cf.pendingW; x++) {
            n += row[x * 4u] + 256u * row[x * 4u + 1u];
        }
    }
    rhi_unmap_buffer(s_cf.buf);
    s_cf.counted++;
    s_cf.pixels += n;
    s_cf.last = n;
    s_cf.max = n > s_cf.max ? n : s_cf.max;
    s_cf.bCounted++;
    s_cf.bPixels += n;
    s_cf.bMax = n > s_cf.bMax ? n : s_cf.bMax;
}

void rd__crack_fill_wait(void)
{
    if (s_cf.pending && g_rd.hasDevice) {
        rhi_wait_idle();
        harvest(true);
    }
}

void rd__crack_fill_counts(RdCrackCounts *out)
{
    memset(out, 0, sizeof(*out));
    out->filled = s_cf.filled;
    out->counted = s_cf.counted;
    out->pixels = s_cf.pixels;
    out->last = s_cf.last;
    out->max = s_cf.max;
    out->skipped = s_cf.skipped;
    out->folded = s_cf.folded;
    out->sx = s_cf.sx;
    out->sy = s_cf.sy;
    out->rx = s_cf.rx;
    out->ry = s_cf.ry;
    out->enabled = s_cf.enabled;
}

void rd__crack_fill_block(void)
{
    note();
    char fold[80] = "";
    if (s_cf.bFolded) {
        snprintf(fold, sizeof(fold), "; %llu inside the scene pass with the local fog",
                 (unsigned long long)s_cf.bFolded);
    }
    if (s_cf.bFilled && !s_cf.bCounted) {
        rd__log("crack fill: %llu replays filled (scale %.4gx%.4g; reach %ux%u texels; not counted "
                "in the game%s)",
                (unsigned long long)s_cf.bFilled, (double)s_cf.sx, (double)s_cf.sy, s_cf.rx,
                s_cf.ry, fold);
    } else if (s_cf.bFilled) {
        rd__log("crack fill: %llu pixels filled over %llu replays counted of %llu filled (max %u a "
                "replay; scale %.4gx%.4g; reach %ux%u texels%s)",
                (unsigned long long)s_cf.bPixels, (unsigned long long)s_cf.bCounted,
                (unsigned long long)s_cf.bFilled, s_cf.bMax, (double)s_cf.sx, (double)s_cf.sy,
                s_cf.rx, s_cf.ry, fold);
    }
    const uint64_t *k = s_cf.bSkip;
    /* MSAA is a choice (its own line once a run); the others are not */
    if (k[SKIP_DEPTH] | k[SKIP_PIPES] | k[SKIP_MEMORY] | k[SKIP_PASS]) {
        rd__log("crack fill: %llu fogged replays above 1x not filled (%llu under MSAA, %llu "
                "without a sampleable depth, %llu without its pipelines, %llu without memory for "
                "its copies, %llu with the fog's pass not begun)",
                (unsigned long long)(k[SKIP_MSAA] + k[SKIP_DEPTH] + k[SKIP_PIPES] + k[SKIP_MEMORY] +
                                     k[SKIP_PASS]),
                (unsigned long long)k[SKIP_MSAA], (unsigned long long)k[SKIP_DEPTH],
                (unsigned long long)k[SKIP_PIPES], (unsigned long long)k[SKIP_MEMORY],
                (unsigned long long)k[SKIP_PASS]);
    }
    s_cf.bFilled = s_cf.bCounted = s_cf.bPixels = s_cf.bFolded = 0;
    s_cf.bMax = 0;
    memset(s_cf.bSkip, 0, sizeof(s_cf.bSkip));
}

static void skip(int why, const RdTargetRec *tc)
{
    s_cf.skipped++;
    s_cf.bSkip[why]++;
    if (!s_cf.skipSaid[why]) {
        s_cf.skipSaid[why] = true;
        rd__log("crack fill: not drawn on SCENE at scale %.4gx%.4g: %s (reported once)",
                (double)tc->sx, (double)tc->sy, kSkipWhy[why]);
    }
}

/* the count's target (cw x ch) and its readback buffer */
static bool countEnsure(uint32_t cw, uint32_t ch, uint32_t pitch)
{
    if (s_cf.countTex.id && s_cf.cw == cw && s_cf.ch == ch &&
        s_cf.bufSize >= (uint64_t)pitch * ch) {
        return true;
    }
    countFree();
    s_cf.countTex = rhi_create_texture(&(RhiTextureDesc){cw, ch, 1, RHI_FMT_RGBA8_UNORM,
                                                         RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                         "rd crack fill count", 0});
    s_cf.countState = RHI_STATE_UNDEFINED;
    s_cf.bufSize = (uint64_t)pitch * ch;
    s_cf.buf = rhi_create_buffer(&(RhiBufferDesc){s_cf.bufSize, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                                  RHI_MEM_READBACK, "rd crack fill count"});
    if (!s_cf.countTex.id || !s_cf.buf.id) {
        countFree();
        return false;
    }
    s_cf.cw = cw;
    s_cf.ch = ch;
    return true;
}

/* one triangle over a w x h pass already begun: the pipeline of key k,
 * t1 and t2 (t1's aspect a1), the reach in DrawCB.param.xy */
static void fullscreen(RhiCommandList cl, const RdPipeKeyInt *k, RhiPipeline pipe, uint32_t w,
                       uint32_t h, RhiTexture t1, RhiViewAspect a1, RhiTexture t2)
{
    rd__replay_viewport(w, h);
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.param[0] = (float)s_cf.rx;
    cb.param[1] = (float)s_cf.ry;
    cb.param[2] = 1.0f / (float)RD_CRACK_BEHIND;
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = t1;
    b[0].aspect = a1;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = t2;
    b[2].aspect = t2.id == g_rd.dummy.id ? RHI_ASPECT_COLOR : RHI_ASPECT_DEPTH;
    const RhiBindGroup g = rd__replay_bind_group(g_rd.layoutTex, b, 3);
    if (!g.id) {
        return;
    }
    rd__set_pipeline(cl, pipe, k);
    rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g);
    rd__replay_draw(cl, 3, 0, 1);
}

/* the count (the tests and --stats only, with none in flight): reads the
 * depth copy zc alone, so it goes before the fill (whose pass the local
 * fog keeps open) */
static void countPass(Replay *r, RhiCommandList cl, RhiPipeline pCount, const RdPipeKeyInt *kCount,
                      RhiTexture zc, uint32_t w, uint32_t h)
{
    if (!s_cf.countAll || !pCount.id || s_cf.pending) {
        return;
    }
    const uint32_t scene = r->st.color;
    const uint32_t cw = (w + CRACK_BLOCK - 1u) / CRACK_BLOCK,
                   ch = (h + CRACK_BLOCK - 1u) / CRACK_BLOCK;
    const uint32_t a = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint32_t pitch = (cw * 4u + a - 1u) / a * a;
    if (!countEnsure(cw, ch, pitch)) {
        return; /* the fill stands; this replay is not counted */
    }
    rd__transition(cl, s_cf.countTex, &s_cf.countState, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, scene, cw, ch, RD_PERF_POST_CRACK);
    rhi_cmd_checkpoint(
        cl, rd__replay_pass_push(RD_PASSK_PASS, scene, cw, ch, 0, 0, 0, 0, RD_PERF_POST_CRACK));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_cf.countTex;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every texel is written */
    p.colorCount = 1;
    p.width = cw;
    p.height = ch;
    rhi_cmd_begin_render_pass(cl, &p);
    fullscreen(cl, kCount, pCount, cw, ch, zc, RHI_ASPECT_DEPTH, g_rd.dummy);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, s_cf.countTex, &s_cf.countState, RHI_STATE_COPY_SRC);
    rhi_cmd_copy_texture_to_buffer(cl, s_cf.countTex, RHI_ASPECT_COLOR, (RhiRect){0, 0, cw, ch},
                                   s_cf.buf, 0, pitch);
    s_cf.pending = true;
    s_cf.pendingReplay = g_rd.replayCounter;
    s_cf.pendingW = cw;
    s_cf.pendingH = ch;
    s_cf.pendingPitch = pitch;
    g_rdPerf.crackCountPasses++;
}

/* the copies, the count and the fill; fold: the fill in the pass the local
 * fog keeps.  Returns whether the fill was drawn so (the pass left open) */
static bool fillWork(Replay *r, RdTargetRec *tc, RdDepthCopy *copy, RhiPipeline pFill,
                     const RdPipeKeyInt *kFill, RhiPipeline pCount, const RdPipeKeyInt *kCount,
                     bool fold, int fogPost)
{
    const uint32_t w = tc->tw, h = tc->th;
    /* the pass before ends (the local read's scene pass among them); SCENE's
     * colour into its snapshot, its depth into the copy */
    if (!rd__replay_take_snap(r, tc, (RhiRect){0, 0, w, h})) {
        skip(SKIP_MEMORY, tc);
        return false;
    }
    RhiCommandList cl = rd__replay_cl();
    const RhiTexture zc = rd__depth_copy_take(cl, tc, copy, "rd fog depth");
    if (!zc.id) {
        skip(SKIP_MEMORY, tc);
        return false;
    }
    s_cf.sx = tc->sx;
    s_cf.sy = tc->sy;
    s_cf.rx = rd__crack_fill_reach(tc->sx);
    s_cf.ry = rd__crack_fill_reach(tc->sy);
    const uint32_t scene = r->st.color;
    countPass(r, cl, pCount, kCount, zc, w, h);

    if (fold) {
        /* the pass the fog keeps: SCENE and its depth loaded, the depth
         * readable (the stencil as beginPass has it), begun and counted as
         * the fog's (the fill's draw is timed apart by the effect) */
        r->perfPost = fogPost;
        rd__replay_begin_pass(r, tc, tc, scene, scene, RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD, 0.0f,
                              false);
        r->perfPost = RD_PERF_POST_CRACK;
        if (!r->passOpen) {
            skip(SKIP_PASS, tc);
            return false; /* not begun: nothing drawn */
        }
        fold = r->passColor == scene && r->passDepth == scene && r->passDepthReadable;
        if (!fold) {
            static bool logged; /* once a run */
            if (!logged) {
                logged = true;
                rd__log("crack fill: the pass after the copies was not begun with the depth "
                        "readable: the fill ends it and the fog begins it again (reported once)");
            }
        }
        fullscreen(cl, kFill, pFill, w, h, tc->snap, RHI_ASPECT_COLOR, zc);
        if (!fold) {
            rd__replay_end_pass(r);
        }
    } else {
        /* the fill: SCENE and its depth, loaded and stored, stencil untouched */
        rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
        rd__transition(cl, tc->depth, &tc->depthState, RHI_STATE_DEPTH_WRITE);
        rd__perf_pass(cl, RD_PERF_PASS_RENDER, scene, w, h, RD_PERF_POST_CRACK);
        rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, scene, w, h, scene, w, h, 0,
                                                    RD_PERF_POST_CRACK));
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = tc->color;
        p.color[0].load = RHI_LOAD_LOAD;
        p.colorCount = 1;
        p.depth.texture = tc->depth;
        p.depth.depthLoad = RHI_LOAD_LOAD;
        p.depth.stencilLoad = RHI_LOAD_LOAD;
        p.width = w;
        p.height = h;
        rhi_cmd_begin_render_pass(cl, &p);
        fullscreen(cl, kFill, pFill, w, h, tc->snap, RHI_ASPECT_COLOR, zc);
        rhi_cmd_end_render_pass(cl);
    }
    r->writeSerial++; /* SCENE's colour and alpha changed (DATE's snapshot) */
    g_rdPerf.crackFillPasses++;
    s_cf.filled++;
    s_cf.bFilled++;
    return fold;
}

void rd__crack_fill(Replay *r, RdTargetRec *tc, const RdTargetRec *tz, RdDepthCopy *copy, bool msaa,
                    bool local)
{
    harvest(false);
    /* SCENE's own fog above 1x under Enhanced; not the fog's self-test
     * (rd__fog_exact without the probe), which measures the depth read */
    if (!s_cf.enabled || !g_rd.hasDevice || g_rd.settings.preset != RD_PRESET_ENHANCED || !tc ||
        tz != tc || r->st.color != RD_TARGET_SCENE + 1u || !(tc->sx > 1.0f || tc->sy > 1.0f) ||
        (rd__fog_exact() && !rd__fog_probe_wanted())) {
        return;
    }
    if (msaa) {
        skip(SKIP_MSAA, tc);
        return;
    }
    if (!tc->withDepth || !tc->depth.id || !tc->depthSampled || !tc->color.id ||
        tc->format != RHI_FMT_RGBA8_UNORM) {
        skip(SKIP_DEPTH, tc);
        return;
    }
    const RdPipeKeyInt kFill = rd__crack_fill_key(0), kCount = rd__crack_fill_key(1);
    const RhiPipeline pFill =
        g_rd.fs[RD_FS_CRACK_FILL].id ? rd__get_pipeline(&kFill) : (RhiPipeline){0};
    /* the count's pipeline only where counting (the tests, --stats): a
     * missing one leaves the fill drawn, uncounted */
    const RhiPipeline pCount = s_cf.countAll && g_rd.fs[RD_FS_CRACK_COUNT].id
                                   ? rd__get_pipeline(&kCount)
                                   : (RhiPipeline){0};
    if (!pFill.id) {
        if (!rd__pipeline_gathering()) { /* a gathering replay names the keys only */
            skip(SKIP_PIPES, tc);
        }
        return;
    }
    /* the filler's GPU work is its own effect (RD_PERF_POST_CRACK), the
     * fog's again after it */
    const int fogPost = r->perfPost;
    r->perfPost = RD_PERF_POST_CRACK;
    rd__replay_perf_post(r, RD_PERF_POST_CRACK);
    const uint64_t filled = s_cf.filled;
    const bool fold = fillWork(r, tc, copy, pFill, &kFill, pCount, &kCount,
                               local && (tc->dstUsage & RD_DST_USAGE_LOCAL_DEPTH) != 0, fogPost);
    r->perfPost = fogPost;
    rd__replay_perf_post(r, fogPost);
    if (s_cf.filled == filled) {
        return; /* not drawn (skipped for memory) */
    }
    s_cf.folded += fold;
    s_cf.bFolded += fold;
    if (fold != s_cf.fold) {
        s_cf.fold = fold;
        note(); /* the state line says where the fill is drawn */
    }
}
