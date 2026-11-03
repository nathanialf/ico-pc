/* rd_crack_fill.c: the crack filler above 1x (issue 26; docs/port/RENDER_POST.md,
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
 * fog of SCENE (rd_replay.c doFog) arms the fill (rd__crack_fill) and the
 * walk draws it after lists 4-6 (the fog's own list and the two after it),
 * from the start of list 7 on where rd__crack_fill_place puts it
 * (rd__crack_fill_at before each command; before the first command that
 * may read SCENE or write its depth, after the glow's cleanUpFB with RGB
 * alone, or as the first draw of the aura's paste pass: "The place" below):
 * the pass open there ends with SCENE's colour copied into its snapshot texture
 * (rd__replay_take_snap), and one pass over SCENE's colour alone draws
 * crack_fill_ps (blit.hlsl), which reads that copy and SCENE's depth
 * itself, sampled where it is (RHI_STATE_DEPTH_READ; no depth attachment).
 * A texel in a run of at most R texels along its row or its column that
 * has at each end a texel in front of it (written, and nearer by more than
 * 1 / RD_CRACK_BEHIND of its own depth) is a crack when no draw covered it
 * (depth 0), or when it is drawn behind both ends by a step in depth: the
 * ends within half the drop of each other (two parts of one surface) and
 * the texel behind each end's surface carried on to it by more than the
 * share (a concave fold between two faces stays).  The crack takes the
 * nearer end's colour, as the fog and lists 4-6 left it; every other
 * texel is discarded, and the depth is not written.  R on an axis is
 * ceil(scale / 2) above 1x, at most 3, else 0: the 1/16 pixel crack is
 * under one texel wide up to 16x, and a slit of a GS pixel or more (2
 * texels at 2x) is the game's and stays.  An exact copy: no filtering, no
 * colour of its own.
 *
 * Where it is drawn (DIVERGENCES.md VD-20 holds the measured bound): until
 * v0.10.0 the fill was drawn before the fog with the colour and the depth,
 * so the crack took the fog at its own place in the fog's dither and lists
 * 4-6 tested and blended against the filled texel.  Drawn after lists 4-6
 * a crack holds its neighbour's finished colour: the neighbour's dither
 * cell, and lists 4-6's draws as they fell on the neighbour; the draws
 * after list 7 that test the depth (the glow's mask, staticBlur.c
 * cleanUpFB) see the crack's own depth.  The reason is the tile-based
 * GPU's pass break: before the fog the copies ended the scene pass in the
 * middle (SCENE's colour and depth stored, copied and loaded again, about
 * 60 MB at 2366x1080 besides the depth's copy); after lists 4-6 the pass
 * ends where it ends anyway, the depth is read in place (no copy), and the
 * fill's pass holds SCENE's colour alone.  The store plan keeps the depth's
 * store for that read (rd_store_plan.c: a fog that may arm the fill
 * touches SCENE's depth again at list 7).
 *
 * The place (docs/port/RENDER_POST.md "The fill's place after list 7"): the
 * fill needs a pass of its own only where nothing else breaks SCENE's pass.
 * State, and staticBlur sprites into other targets that neither read SCENE
 * nor write its depth (the aura's AURA_WORK, WORK0, FEED128), touch none of
 * its inputs or texels: it waits past them.  The glow's cleanUpFB writes
 * SCENE's alpha alone (rd__fx_fold: RGB kept for every destination), the
 * first sprite every texel's (alphaCovers): the fill after them with RGB
 * alone (RD_CRACK_KEY_RGB) is the same bytes, and the two sprites stay in
 * the fog's pass (rd_fx_plan.c fxAttachDepth).  The aura's paste
 * (pasteFeedBackAreaToFB, FEED128 over SCENE by the local read): the fill
 * is the first draw of the paste's own pass (rd__crack_fill_in_pass from
 * rd_fx_plan.c fxDraw), no pass of its own.  Anything else: the fill before
 * it.  The store plan's scan asks rd__crack_fill_place the same, so the
 * depth's store is kept up to the fill's read; the walk may draw it
 * earlier (an MSAA epoch, the first cleanUpFB sprite not over all of SCENE,
 * a paste its pass cannot take), never later.  ICO_RD_CRACK_PLACE=7 (the
 * tests) pins it to the start of list 7.
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
 * Not drawn: at scale 1 on both axes, under the Original preset, while
 * the MSAA epoch is still open at the fog (a frame without a shadow: the
 * fog may run per sample on SCENE_MS, and the filler's snapshot of SCENE
 * would close the epoch first; DIVERGENCES.md VD-22), in the fog's
 * self-test, on a fog of another target than SCENE, without a sampleable
 * depth, and with [dev] crack_fill = false (ICO_RD_CRACK_FILL=0; the replay
 * tool's --no-crack-fill).
 *
 * After the MSAA resolve (issue 26 with MSAA on: the epoch closed before
 * the fog, in gameplay at the shadow count's pass in list 3,
 * docs/port/MSAA.md): the fill runs on the resolved SCENE as it does
 * without MSAA.  msaa_resolve_ps wrote SCENE's single-sampled colour and
 * depth (SCENE's own textures, so depthSampled is as SCENE was made); a
 * pixel more than half of whose samples held the epoch's clear depth took
 * the smallest of all its samples, the clear's: the game clears SCENE's
 * depth to GS Z 0, which is 0.0 (rd__gs_depth), the "unwritten" of
 * crack_front, so such a pixel is the crack it is at 1x.  Any other pixel
 * took the smallest covered sample: a farther surface seen through a
 * seam (any sample on it) gives the pixel its depth, the discontinuity
 * form's crack.  Gaps onto the clear the resolve already closed are
 * surfaces to the filler and stay.
 *
 * On by default on every device class (RdProfile.crackFill); [dev]
 * crack_fill = false (ICO_RD_CRACK_FILL=0) turns it off, the A/B.  Its
 * GPU time is the effect "crack fill" (RD_PERF_POST_CRACK: the colour's
 * copy, the count and the fill's pass) in the 10 s "effects:" line, the
 * replay tool's --stats and --pass-times.
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
 *       its ends, with a texel in front at each end takes the nearer end's
 *       colour after lists 4-6);
 *       [dev] crack_fill = false turns it off      (start, a new scale;
 *       " (drawn after the MSAA resolve)" after it once a fill was drawn
 *       so, and again without it when one is not)
 *   rd: crack fill: T replays filled (scale SXxSY; reach RXxRY texels; not
 *       counted in the game[; F after lists 4-6[, K of them in the aura's
 *       paste pass][, G after the glow's alpha]][; A after the MSAA
 *       resolve])                                  (10 s summary)
 *   rd: crack fill: N pixels filled over M replays counted of T filled
 *       (max K a replay; scale SXxSY; reach RXxRY texels[; F after ...]
 *       [; A after ...])
 *       (the same, counting: the tests, the replay tool's --stats)
 *   rd: crack fill: N fogged replays above 1x not filled (...)   (the same)
 *   rd: crack fill: not drawn ...: <reason> (reported once)   (events)
 *   rd: crack fill: drawn as the first draw of the aura's paste pass ...,
 *       rd: crack fill: drawn after the glow's sprites that write SCENE's
 *       alpha alone ... (reported once each)
 *   rd: crack fill: the aura's paste cannot take the fill as its pass's
 *       first draw (<why>) ..., rd: crack fill: the glow's first sprite of
 *       SCENE's alpha does not write every texel's alpha ... (the first 8,
 *       then every 100th) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* crack_count_ps's block (blit.hlsl CRACK_BLOCK) */
#define CRACK_BLOCK 16u

enum { SKIP_MSAA, SKIP_DEPTH, SKIP_PIPES, SKIP_MEMORY, SKIP_COUNT };

static const char *const kSkipWhy[SKIP_COUNT] = {
    ("the MSAA epoch is still open at the fog (a frame without a shadow: the fog may read the "
     "multisampled scene; the filler runs after the MSAA resolve: DIVERGENCES.md VD-22)"),
    "the scene's depth cannot be sampled on this device",
    "its shaders or pipelines are missing",
    "no memory for the copy of the scene's colour",
};

/* why the switch is what it is (the state line) */
enum { WHY_PROFILE, WHY_DEV, WHY_FORCED };

#define MSAA_TEXT " (drawn after the MSAA resolve)"

static struct {
    bool enabled; /* ICO_RD_CRACK_FILL, the profile, or the replay tool's switch */
    int why;      /* WHY_* */
    bool countAll;
    bool general;  /* tests: the general shader at a reach of 1 */
    int said1;     /* crack_fill1_ps's event: -1 not said, else 0/1 */
    bool resolved; /* the last fill drawn on the MSAA resolve's picture */
    /* the count's target and readback (one in flight) */
    RhiTexture countTex;
    RhiState countState;
    uint32_t cw, ch;
    RhiBuffer buf;
    uint64_t bufSize;
    bool pending;
    uint32_t pendingReplay, pendingW, pendingH, pendingPitch;
    /* since start */
    uint64_t filled, counted, pixels, skipped, late, afterMsaa;
    uint32_t last, max;
    float sx, sy;
    uint32_t rx, ry;
    /* the 10 s block's (rd__crack_fill_block) */
    uint64_t bFilled, bCounted, bPixels, bLate, bAfterMsaa, bSkip[SKIP_COUNT];
    uint32_t bMax;
    bool skipSaid[SKIP_COUNT];
    char noted[448]; /* the last state line */
} s_cf;

/* the fill's place: armed by rd__crack_fill at the fog, drawn from list 7
 * on of the same replay (rd__crack_fill_at, before each command; at the
 * latest rd__crack_fill_late at the walk's end) where rd__crack_fill_place
 * says.  resolved the MSAA epoch closed before the fog; alpha the glow's
 * alpha-only sprites drawn over SCENE since list 7 (the fill writes RGB
 * alone after them); paste the fill armed for the first draw of the aura's
 * paste pass (command pasteOrd, taken by rd__crack_fill_in_pass) */
static struct {
    bool armed, resolved, alpha, paste;
    uint32_t replay, pasteOrd;
} s_late;

/* the places taken since start and in the 10 s block: in the aura's paste
 * pass, after the glow's alpha; the paste not taken in its pass (why said
 * the first 8 then every 100th), the glow's first alpha sprite not over the
 * whole of SCENE */
static struct {
    uint64_t pasted, alpha, bPasted, bAlpha;
    uint32_t notPasted, notCovered;
} s_place;

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
                 "texel in front at each end takes the nearer end's colour after lists 4-6); [dev] "
                 "crack_fill = false turns it off%s%s",
                 (double)sx, (double)sy, rd__crack_fill_reach(sx), rd__crack_fill_reach(sy),
                 RD_CRACK_BEHIND, s_cf.resolved ? MSAA_TEXT : "", dev);
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
    memset(&s_place, 0, sizeof(s_place));
    memset(&s_late, 0, sizeof(s_late));
    s_cf.said1 = -1;
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

void rd__crack_fill_force_general(bool on)
{
    s_cf.general = on;
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
    out->late = s_cf.late;
    out->afterMsaa = s_cf.afterMsaa;
    out->pasted = s_place.pasted;
    out->afterAlpha = s_place.alpha;
    out->sx = s_cf.sx;
    out->sy = s_cf.sy;
    out->rx = s_cf.rx;
    out->ry = s_cf.ry;
    out->enabled = s_cf.enabled;
}

void rd__crack_fill_block(void)
{
    note();
    char fold[192] = "";
    int at = 0;
    if (s_cf.bLate) {
        at = snprintf(fold, sizeof(fold), "; %llu after lists 4-6", (unsigned long long)s_cf.bLate);
    }
    if (s_place.bPasted && at >= 0 && (size_t)at < sizeof(fold)) {
        at += snprintf(fold + at, sizeof(fold) - (size_t)at,
                       ", %llu of them in the aura's paste pass",
                       (unsigned long long)s_place.bPasted);
    }
    if (s_place.bAlpha && at >= 0 && (size_t)at < sizeof(fold)) {
        at += snprintf(fold + at, sizeof(fold) - (size_t)at, ", %llu after the glow's alpha",
                       (unsigned long long)s_place.bAlpha);
    }
    if (s_cf.bAfterMsaa && at >= 0 && (size_t)at < sizeof(fold)) {
        snprintf(fold + at, sizeof(fold) - (size_t)at, "; %llu after the MSAA resolve",
                 (unsigned long long)s_cf.bAfterMsaa);
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
    if (k[SKIP_DEPTH] | k[SKIP_PIPES] | k[SKIP_MEMORY]) {
        rd__log("crack fill: %llu fogged replays above 1x not filled (%llu in an open MSAA epoch, "
                "%llu without a sampleable depth, %llu without its pipelines, %llu without memory "
                "for the colour's copy)",
                (unsigned long long)(k[SKIP_MSAA] + k[SKIP_DEPTH] + k[SKIP_PIPES] + k[SKIP_MEMORY]),
                (unsigned long long)k[SKIP_MSAA], (unsigned long long)k[SKIP_DEPTH],
                (unsigned long long)k[SKIP_PIPES], (unsigned long long)k[SKIP_MEMORY]);
    }
    s_cf.bFilled = s_cf.bCounted = s_cf.bPixels = s_cf.bLate = s_cf.bAfterMsaa = 0;
    s_place.bPasted = s_place.bAlpha = 0;
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
 * depth z alone (SCENE's, sampled where it is), before the fill's pass */
static void countPass(RhiCommandList cl, RhiPipeline pCount, const RdPipeKeyInt *kCount,
                      RhiTexture z, uint32_t w, uint32_t h)
{
    if (!s_cf.countAll || !pCount.id || s_cf.pending) {
        return;
    }
    const uint32_t scene = RD_TARGET_SCENE + 1u;
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
    fullscreen(cl, kCount, pCount, cw, ch, z, RHI_ASPECT_DEPTH, g_rd.dummy);
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

/* ICO_RD_CRACK_PLACE=7 (the tests' A/B): the fill at the start of list 7,
 * its place before the move, never later; rd__crack_fill_set_place over it */
static int s_pinSet = -1;

void rd__crack_fill_set_place(int pinned)
{
    s_pinSet = pinned;
}

static bool placePinned(void)
{
    static int s_pinEnv = -1;
    if (s_pinSet >= 0) {
        return s_pinSet == 1;
    }
    if (s_pinEnv < 0) {
        const char *e = getenv("ICO_RD_CRACK_PLACE");
        s_pinEnv = e && strcmp(e, "7") == 0;
    }
    return s_pinEnv == 1;
}

int rd__crack_fill_place(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool *alpha)
{
    if (placePinned()) {
        return RD_CRACK_PLACE_HERE;
    }
    if (c->type == RDC_NOP || c->type == RDC_AA1 ||
        (c->type > RDC_NOP && c->type <= RDC_STATE_LAST)) {
        return RD_CRACK_PLACE_HOLD; /* state: the GPU sees none of it */
    }
    /* a staticBlur sprite (rd__blur_post_sprite) other than the reduction;
     * any other command (a draw, a clear, a copy, the fog, a shadow) may
     * read or write SCENE: the fill before it */
    const uint8_t kind = c->b[0];
    if (c->type != RDC_POST_STUB || kind == RD_POST_REDUCTION || !rd__is_blur_kind(kind) ||
        !f->payload || (uint64_t)c->u[1] + sizeof(RdPostRec) > f->payloadSize) {
        return RD_CRACK_PLACE_HERE;
    }
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const uint32_t scene = RD_TARGET_SCENE + 1u;
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
    if ((d->texEnabled && !t) ||
        (t && t->kind == RD_TEXKIND_TARGET && rd__target_rec(t->target) == sceneRec()) ||
        (st->depth == scene && d->zwrite == RD_ZWRITE_ON) || d->test.date != RD_DATE_OFF) {
        return RD_CRACK_PLACE_HERE; /* reads SCENE, writes its depth, or reads an alpha */
    }
    if (st->color != scene) {
        /* another target (the aura's AURA_WORK, WORK0, FEED128): the fill's
         * inputs and its texels untouched; after the glow's alpha the mask
         * that reads SCENE follows, so no further */
        return *alpha ? RD_CRACK_PLACE_HERE : RD_CRACK_PLACE_HOLD;
    }
    /* the glow's cleanUpFB: an untextured sprite whose blend keeps RGB for
     * every Cd and Ad (rd__fx_fold), no alpha test: it writes the alpha
     * alone, so the fill after it with RGB alone is the same bytes (the
     * first one over the whole of SCENE: rd__crack_fill_at checks) */
    if (!d->texEnabled && d->abe && !(d->test.ate && d->test.atst != RD_ATST_ALWAYS)) {
        uint8_t ident = 0, rgb[3];
        if (rd__fx_fold(d, p.rgba, &ident, rgb) && (ident & 7u) == 7u) {
            *alpha = true;
            return RD_CRACK_PLACE_HOLD;
        }
    }
    /* the aura's paste (pasteFeedBackAreaToFB): FEED128 blended into SCENE
     * through its destination, no Z test */
    if (!*alpha && kind == RD_POST_AURA && t && t->kind == RD_TEXKIND_TARGET && d->abe &&
        !(d->test.zte && d->test.ztst != RD_ZTST_ALWAYS)) {
        return RD_CRACK_PLACE_PASTE;
    }
    return RD_CRACK_PLACE_HERE;
}

/* The snapshot and the count: SCENE's colour into its snapshot (the pass
 * before ends there), its depth to be sampled where it is (no depth
 * attachment in the fill's pass, so the depth is never sampled while it is
 * bound; the next pass that binds it takes it back to DEPTH_WRITE,
 * beginPass, as after the fog's read in place; its store at the pass
 * before is kept by the store plan, rd_store_plan.c: the fill's read is a
 * touch where rd__crack_fill_place puts it) */
static bool fillTake(Replay *r, RdTargetRec *tc, RhiPipeline pCount, const RdPipeKeyInt *kCount)
{
    const uint32_t w = tc->tw, h = tc->th;
    if (!rd__replay_take_snap(r, tc, (RhiRect){0, 0, w, h})) {
        skip(SKIP_MEMORY, tc);
        return false;
    }
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, tc->depth, &tc->depthState, RHI_STATE_DEPTH_READ);
    s_cf.sx = tc->sx;
    s_cf.sy = tc->sy;
    s_cf.rx = rd__crack_fill_reach(tc->sx);
    s_cf.ry = rd__crack_fill_reach(tc->sy);
    countPass(cl, pCount, kCount, tc->depth, w, h);
    return true;
}

/* the fill drawn (the pass open on SCENE's colour) and counted */
static void fillDraw(Replay *r, RdTargetRec *tc, RhiPipeline pFill, const RdPipeKeyInt *kFill)
{
    fullscreen(rd__replay_cl(), kFill, pFill, tc->tw, tc->th, tc->snap, RHI_ASPECT_COLOR,
               tc->depth);
    r->writeSerial++; /* SCENE's colour (and alpha) changed (DATE's snapshot) */
    s_cf.filled++;
    s_cf.bFilled++;
}

/* the snapshot, the count and the fill in a pass of SCENE's colour alone,
 * loaded and stored */
static bool fillWork(Replay *r, RdTargetRec *tc, RhiPipeline pFill, const RdPipeKeyInt *kFill,
                     RhiPipeline pCount, const RdPipeKeyInt *kCount)
{
    if (!fillTake(r, tc, pCount, kCount)) {
        return false;
    }
    RhiCommandList cl = rd__replay_cl();
    const uint32_t w = tc->tw, h = tc->th, scene = RD_TARGET_SCENE + 1u;
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, scene, w, h, RD_PERF_POST_CRACK);
    rhi_cmd_checkpoint(
        cl, rd__replay_pass_push(RD_PASSK_PASS, scene, w, h, 0, 0, 0, 0, RD_PERF_POST_CRACK));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
    fillDraw(r, tc, pFill, kFill);
    rhi_cmd_end_render_pass(cl);
    g_rdPerf.crackFillPasses++;
    return true;
}

void rd__crack_fill(Replay *r, RdTargetRec *tc, const RdTargetRec *tz, int msaa)
{
    harvest(false);
    /* SCENE's own fog above 1x under Enhanced; not the fog's self-test
     * (rd__fog_exact without the probe), which measures the depth read */
    if (!s_cf.enabled || !g_rd.hasDevice || g_rd.settings.preset != RD_PRESET_ENHANCED || !tc ||
        tz != tc || r->st.color != RD_TARGET_SCENE + 1u || !(tc->sx > 1.0f || tc->sy > 1.0f) ||
        (rd__fog_exact() && !rd__fog_probe_wanted())) {
        return;
    }
    if (msaa == RD_CRACK_MSAA_OPEN) {
        skip(SKIP_MSAA, tc);
        return;
    }
    if (!tc->withDepth || !tc->depth.id || !tc->depthSampled || !tc->color.id ||
        tc->format != RHI_FMT_RGBA8_UNORM) {
        skip(SKIP_DEPTH, tc);
        return;
    }
    s_late.armed = true;
    s_late.alpha = s_late.paste = false;
    s_late.replay = g_rd.replayCounter;
    s_late.resolved = msaa == RD_CRACK_MSAA_RESOLVED;
}

bool rd__crack_fill_may(const RdStateBlock *st)
{
    const RdTargetRec *tc = sceneRec();
    return s_cf.enabled && g_rd.hasDevice && g_rd.settings.preset == RD_PRESET_ENHANCED && tc &&
           st->color == RD_TARGET_SCENE + 1u && (tc->sx > 1.0f || tc->sy > 1.0f);
}

/* the fill's pipelines: false (said, skipped) when there are none */
static bool fillPipes(RdTargetRec *tc, RdPipeKeyInt *kFill, RhiPipeline *pFill,
                      RdPipeKeyInt *kCount, RhiPipeline *pCount)
{
    /* a reach of at most 1 texel on both axes takes crack_fill1_ps (the
     * same texels, the four neighbours read first); after the glow's alpha
     * its RGB alone (the colour mask) */
    const bool one = !s_cf.general && g_rd.fs[RD_FS_CRACK_FILL1].id &&
                     rd__crack_fill_reach(tc->sx) <= 1u && rd__crack_fill_reach(tc->sy) <= 1u;
    const int rgb = s_late.alpha ? RD_CRACK_KEY_RGB : 0;
    *kFill = rd__crack_fill_key((one ? RD_CRACK_KEY_FILL1 : RD_CRACK_KEY_FILL) | rgb);
    *kCount = rd__crack_fill_key(RD_CRACK_KEY_COUNT);
    *pFill = g_rd.fs[one ? RD_FS_CRACK_FILL1 : RD_FS_CRACK_FILL].id ? rd__get_pipeline(kFill)
                                                                    : (RhiPipeline){0};
    if (pFill->id && s_cf.said1 != (int)one) {
        s_cf.said1 = one;
        rd__log("crack fill: %s (scale %.4gx%.4g, reach %ux%u texels)",
                one ? "the reach-1 shader (crack_fill1_ps: the four neighbours first)"
                    : "the general shader (crack_fill_ps)",
                (double)tc->sx, (double)tc->sy, rd__crack_fill_reach(tc->sx),
                rd__crack_fill_reach(tc->sy));
    }
    /* the count's pipeline only where counting (the tests, --stats): a
     * missing one leaves the fill drawn, uncounted */
    *pCount = s_cf.countAll && g_rd.fs[RD_FS_CRACK_COUNT].id ? rd__get_pipeline(kCount)
                                                             : (RhiPipeline){0};
    if (!pFill->id) {
        if (!rd__pipeline_gathering()) { /* a gathering replay names the keys only */
            skip(SKIP_PIPES, tc);
        }
        return false;
    }
    return true;
}

/* the counters of a fill drawn (in a pass of its own or the paste's) */
static void fillCounted(bool pasted)
{
    const bool resolved = s_late.resolved;
    s_cf.late++;
    s_cf.bLate++;
    s_cf.afterMsaa += resolved;
    s_cf.bAfterMsaa += resolved;
    if (pasted) {
        s_place.pasted++;
        s_place.bPasted++;
    }
    if (s_late.alpha) {
        s_place.alpha++;
        s_place.bAlpha++;
        static bool s_said;
        if (!s_said) {
            s_said = true;
            rd__log("crack fill: drawn after the glow's sprites that write SCENE's alpha alone "
                    "(staticBlur.c cleanUpFB), its RGB alone: the sprites stay in the fog's pass "
                    "(reported once)");
        }
    }
    if (resolved != s_cf.resolved) {
        s_cf.resolved = resolved;
        note(); /* the state line says on what the fill is drawn */
    }
}

void rd__crack_fill_late(Replay *r, bool msaaOpen)
{
    if (!s_late.armed) {
        return;
    }
    s_late.armed = s_late.paste = false;
    RdTargetRec *tc = sceneRec();
    if (s_late.replay != g_rd.replayCounter || !tc || !tc->depth.id || !tc->color.id) {
        return; /* armed in another replay (one that ended before list 7) */
    }
    if (msaaOpen) {
        skip(SKIP_MSAA, tc); /* an epoch opened after the fog: SCENE_MS holds the picture */
        return;
    }
    RdPipeKeyInt kFill, kCount;
    RhiPipeline pFill, pCount;
    if (!fillPipes(tc, &kFill, &pFill, &kCount, &pCount)) {
        return;
    }
    /* the filler's GPU work is its own effect (RD_PERF_POST_CRACK), the
     * list's again after it */
    const int post = r->perfPost;
    r->perfPost = RD_PERF_POST_CRACK;
    rd__replay_perf_post(r, RD_PERF_POST_CRACK);
    const bool drawn = fillWork(r, tc, pFill, &kFill, pCount, &kCount);
    r->perfPost = post;
    rd__replay_perf_post(r, post);
    if (drawn) {
        fillCounted(false);
    }
}

/* the paste's pass cannot take the fill: why (NULL: it can).  The pass the
 * paste's draw begins on SCENE's colour alone, readable for its local read
 * (rd_fx_plan.c fxDraw), whole (no pass area), no MSAA epoch */
static const char *pasteWhyNot(const Replay *r, const RdTargetRec *tc)
{
    RhiRect a;
    if (!g_rd.profile.fxPlan || !g_rd.profile.fxDstLocal || !g_rd.fs[RD_FS_FX_LOCAL].id ||
        !g_rd.layoutLocal.id || g_rd.dstMode != RD_BLEND_EXACT_OFF) {
        return "the paste reads its destination through a snapshot on this device";
    }
    if (rd__replay_msaa_want() > 1u) {
        return "the MSAA epoch";
    }
    if (!rd__pass_wants_readable(tc)) {
        return "the scene's passes are not readable on this device";
    }
    if (r->passOpen && r->passColor == RD_TARGET_SCENE + 1u) {
        return "a pass on the scene is open before it";
    }
    if (rd__pass_area_on() &&
        rd__pass_area_rect(RD_TARGET_SCENE + 1u, tc, g_rdStoreOrd, &a, NULL)) {
        return "the paste's pass holds a part of the picture only";
    }
    return NULL;
}

static void notPasted(const char *why)
{
    s_place.notPasted++;
    if (s_place.notPasted <= 8u || s_place.notPasted % 100u == 0u) {
        rd__log("crack fill: the aura's paste cannot take the fill as its pass's first draw "
                "(%s): its own pass before the paste (the first 8, then every 100th: %u so far)",
                why, s_place.notPasted);
    }
}

/* the glow's first alpha sprite (rd__crack_fill_place said HOLD and alpha):
 * the fill after it only when it writes the alpha of every texel of SCENE
 * (no Z or alpha test, the whole alpha unmasked, over the whole target:
 * doBlurSpriteDraw's blurCoversTarget), so the alpha the fill would have
 * written before it is overwritten wherever it lies */
static bool alphaCovers(const Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdTargetRec *tc = rd__target_rec(r->st.color);
    const RdDrawState *d = &r->st.ds;
    if (!tc || !(d->colorMask & 8u) || d->fbmsk != 0 ||
        (d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) ||
        (d->test.ate && d->test.atst != RD_ATST_ALWAYS)) {
        return false;
    }
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    RdTargetRec *td = rd__target_rec(r->st.depth);
    if (td && (!td->withDepth || !td->depth.id || td->tw != tc->tw || td->th != tc->th)) {
        td = NULL;
    }
    int useDepth = 0;
    const RdPipeKeyInt key =
        rd__blur_key(&r->st, tc->format, td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN, &useDepth, 0);
    /* as doBlurSpriteDraw: the sprite's scissor without the previous draw's
     * mirror */
    Replay q = *r;
    q.mirror = 0;
    return rd__replay_blur_covers(&q, tc, &p, d, &key, useDepth);
}

void rd__crack_fill_at(Replay *r, const RdFrame *f, const RdCmd *c, int list, bool msaaOpen)
{
    if (!s_late.armed || list < RD_CRACK_LATE_LIST) {
        return;
    }
    if (s_late.paste) {
        /* armed for the paste before: its pass did not take it (the paste
         * drew nothing) */
        rd__crack_fill_late(r, msaaOpen);
        return;
    }
    int place = RD_CRACK_PLACE_HERE;
    bool alpha = s_late.alpha;
    if (!msaaOpen && rd__replay_msaa_want() <= 1u && s_late.replay == g_rd.replayCounter) {
        place = rd__crack_fill_place(f, c, &r->st, &alpha);
    }
    if (place == RD_CRACK_PLACE_HOLD && alpha && !s_late.alpha && !alphaCovers(r, f, c)) {
        place = RD_CRACK_PLACE_HERE; /* the alpha the fill writes would show: before it */
        alpha = false;
        s_place.notCovered++;
        if (s_place.notCovered <= 8u || s_place.notCovered % 100u == 0u) {
            rd__log("crack fill: the glow's first sprite of SCENE's alpha does not write every "
                    "texel's alpha: the fill before it, in its own pass (the first 8, then every "
                    "100th: %u so far)",
                    s_place.notCovered);
        }
    }
    if (place == RD_CRACK_PLACE_HOLD) {
        s_late.alpha = alpha;
        return;
    }
    if (place == RD_CRACK_PLACE_PASTE) {
        const RdTargetRec *tc = sceneRec();
        const char *why = tc ? pasteWhyNot(r, tc) : "no scene";
        if (!why) {
            s_late.paste = true; /* rd__crack_fill_in_pass, from the paste's draw */
            s_late.pasteOrd = g_rdStoreOrd;
            return;
        }
        notPasted(why);
    }
    rd__crack_fill_late(r, msaaOpen);
}

bool rd__crack_fill_paste_due(void)
{
    return s_late.armed && s_late.paste && s_late.pasteOrd == g_rdStoreOrd;
}

void rd__crack_fill_paste_late(Replay *r, const char *why)
{
    if (!rd__crack_fill_paste_due()) {
        return;
    }
    notPasted(why);
    rd__crack_fill_late(r, false);
}

void rd__crack_fill_in_pass(Replay *r, uint32_t tdId)
{
    if (!rd__crack_fill_paste_due()) {
        return;
    }
    RdTargetRec *tc = sceneRec();
    if (tdId != 0 || !tc || r->st.color != RD_TARGET_SCENE + 1u ||
        (r->passOpen && r->passColor == RD_TARGET_SCENE + 1u)) {
        rd__crack_fill_paste_late(r, "the paste's pass binds the scene's depth or is open");
        return;
    }
    s_late.armed = s_late.paste = false;
    if (s_late.replay != g_rd.replayCounter || !tc->depth.id || !tc->color.id) {
        return;
    }
    RdPipeKeyInt kFill, kCount;
    RhiPipeline pFill, pCount;
    if (!fillPipes(tc, &kFill, &pFill, &kCount, &pCount)) {
        return;
    }
    /* the snapshot and the count are the fill's effect; the pass is the
     * paste's (the aura's), begun as the paste's bindDraw would begin it:
     * SCENE's colour loaded, no depth; the paste's draw then lands in it */
    const int post = r->perfPost;
    r->perfPost = RD_PERF_POST_CRACK;
    rd__replay_perf_post(r, RD_PERF_POST_CRACK);
    const bool taken = fillTake(r, tc, pCount, &kCount);
    r->perfPost = post;
    rd__replay_perf_post(r, post);
    if (!taken) {
        return;
    }
    rd__replay_begin_pass(r, tc, NULL, RD_TARGET_SCENE + 1u, 0, RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD,
                          0.0f, false);
    fillDraw(r, tc, pFill, &kFill);
    fillCounted(true);
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("crack fill: drawn as the first draw of the aura's paste pass on SCENE (the "
                "paste's own pass, its draw after it), not in a pass of its own (reported once)");
    }
}
