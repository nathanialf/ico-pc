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

#include "rd_lamps.h"
#include "rd_replay.h"
#include "rd_upscale.h"

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
    /* the late pass (rd_late_pass.c): the snapshot taken where the fill
     * would be drawn (rd__crack_fill_place's TAKE), its draw held to the
     * aura's paste pass; repeats SCENE's border repeats done by then */
    bool taken;
    /* FILLPASTE (rd_lamps.h): held without the snapshot, which is taken where
     * the fill lands (by the swap there: the fill, the paste and list 11's
     * draws one pass) or where it is flushed; the scan proved nothing between
     * writes SCENE's colour or depth (the lamps plan: the mask skipped, the
     * lamps on WORK2's own depth) */
    bool deferred;
    bool glow; /* the paste armed is the glow's (the fx plan's premultiplied add) */
    uint64_t repeats;
    int flushWhy; /* why it is drawn in a pass of its own after all (RD_LATE_WHY_*) */
} s_late;

/* the places taken since start and in the 10 s block: in the aura's paste
 * pass, after the glow's alpha; the paste not taken in its pass (why said
 * the first 8 then every 100th), the glow's first alpha sprite not over the
 * whole of SCENE */
static struct {
    uint64_t pasted, alpha, bPasted, bAlpha;
    uint32_t notPasted, notCovered;
    /* held fills drawn after SCENE's border repeat, its blocks again */
    uint64_t border, bBorder;
} s_place;

/* the replay tool's --no-crack-fill and the tests: -1 none, else the switch */
static int s_forced = -1;

/* crack_fill_ps's DrawCB.mode.x (blit.hlsl CRACK_MODE_*), set by the draws
 * around fullscreenAt: COPY a texel that is no crack takes its own
 * snapshot texel (the swap: the attachment holds nothing yet), KEEP_A with
 * it every texel's alpha its own snapshot texel's (the fill writes RGB alone
 * after the glow's alpha), EARLY a texel with neither immediate neighbour in
 * front on an axis left at once (the general shader at a reach of at most 2:
 * CRACKTAP) */
#define CRACK_MODE_COPY 1u
#define CRACK_MODE_KEEP_A 2u
#define CRACK_MODE_EARLY 4u
static uint32_t s_mode;

/* FILLSWAP: SCENE's colour swapped with its snapshot texture where the fill
 * takes its snapshot (rd__replay_blur_swap), the fill drawn over every
 * texel; on only where the start-up self-test passed (rd__crack_swap_selftest:
 * 0 not run, 1 passed, 2 failed) */
enum {
    SWAP_WHY_NONE,
    SWAP_WHY_OFF,   /* ICO_RD_CRACK_SWAP=0 */
    SWAP_WHY_TEST,  /* the self-test not passed (not run, failed) */
    SWAP_WHY_HELD,  /* the late pass holds the draw past the snapshot (rd_late_pass.c) */
    SWAP_WHY_SPLIT, /* the upscaler's split (the display layer owns SCENE's snapshot) */
    SWAP_WHY_MSAA,  /* an MSAA epoch wanted */
    SWAP_WHY_EXACT, /* the opted-in exact blend */
    SWAP_WHY_PIPE,  /* the fill's whole-mask pipeline missing */
    SWAP_WHY_COUNT
};

static const char *const kSwapWhy[SWAP_WHY_COUNT] = {
    "-",
    "switched off",
    "its self-test not passed",
    "the draw held past the snapshot",
    "the upscaler's split",
    "an MSAA epoch",
    "the exact blend",
    "its pipeline missing",
};

static struct {
    int test;      /* 0 not run, 1 passed, 2 failed */
    int force;     /* rd__crack_swap_force: -1 the environment's */
    int env;       /* ICO_RD_CRACK_SWAP: -1 not read, 0 off, 1 on */
    bool swapping; /* inside the fill's own swap (rd__crack_fill_snap's hook stands down) */
    uint64_t bSwapped, bWhy[SWAP_WHY_COUNT];
} s_swap = {0, -1, -1, false, 0, {0}};

/* CRACKTAP: ICO_RD_CRACK_TAP2=0 the general shader's early leave off */
static int s_tap2 = -1, s_tap2Force = -1;

/* FILL11 (rd_fill11.c): a fill drawn where it is due (fillAt's place)
 * whose snapshot is taken there and whose draw is held to list 11's first
 * screen draw on SCENE, the first draw of that pass over a scratch depth
 * (kTwin, pTwin: its key's depth twin), or drawn in a pass of its own after
 * all (kPlain, pPlain: the key it would have been drawn with); swap the
 * snapshot taken by the swap, alpha and resolved s_late's at the take.
 * s_at: the place fillAt asks rd__crack_fill_late to draw at (list -1:
 * another caller), its frame and command */
static struct {
    bool pending, swap, alpha, resolved;
    uint32_t replay;
    RdPipeKeyInt kPlain, kTwin;
    RhiPipeline pPlain, pTwin;
} s_f11;

static struct {
    int list;
    const RdFrame *f;
    const RdCmd *c;
} s_at = {-1, NULL, NULL};

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
    memset(&s_f11, 0, sizeof(s_f11));
    rd__fill11_init();   /* the fill in list 11's pass (rd_fill11.c) */
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
    s_f11.pending = false;
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

static void stripFree(void);

void rd__crack_fill_shutdown(void)
{
    countFree();
    stripFree(); /* FILLPASTE's border strips */
    memset(&s_f11, 0, sizeof(s_f11));
    rd__fill11_shutdown(); /* its scratch depth and self-test (rd_fill11.c) */
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
    char fold[640] = "";
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
    if (s_place.bBorder && at >= 0 && (size_t)at < sizeof(fold)) {
        at += snprintf(fold + at, sizeof(fold) - (size_t)at,
                       ", %llu held past SCENE's border repeat (its columns filled from their "
                       "source columns)",
                       (unsigned long long)s_place.bBorder);
    }
    if (s_cf.bAfterMsaa && at >= 0 && (size_t)at < sizeof(fold)) {
        at += snprintf(fold + at, sizeof(fold) - (size_t)at, "; %llu after the MSAA resolve",
                       (unsigned long long)s_cf.bAfterMsaa);
    }
    /* FILLSWAP: the snapshots the swap took; the copies' reasons */
    uint64_t copied = 0;
    for (int k = 1; k < SWAP_WHY_COUNT; k++) {
        copied += s_swap.bWhy[k];
    }
    if ((s_swap.bSwapped || copied) && at >= 0 && (size_t)at < sizeof(fold)) {
        at += snprintf(fold + at, sizeof(fold) - (size_t)at,
                       "; %llu snapshots by the swap, %llu copied",
                       (unsigned long long)s_swap.bSwapped, (unsigned long long)copied);
        for (int k = 1; k < SWAP_WHY_COUNT && at >= 0 && (size_t)at < sizeof(fold); k++) {
            if (s_swap.bWhy[k]) {
                at += snprintf(fold + at, sizeof(fold) - (size_t)at, " (%llu %s)",
                               (unsigned long long)s_swap.bWhy[k], kSwapWhy[k]);
            }
        }
    }
    s_swap.bSwapped = 0;
    memset(s_swap.bWhy, 0, sizeof(s_swap.bWhy));
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
    s_place.bPasted = s_place.bAlpha = s_place.bBorder = 0;
    s_cf.bMax = 0;
    memset(s_cf.bSkip, 0, sizeof(s_cf.bSkip));
    rd__fill11_block(); /* the fills drawn in list 11's pass (rd_fill11.c) */
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
 * t1 and t2 (t1's aspect a1), the reach in DrawCB.param.xy; drawn inside
 * scissor sc, each texel x evaluated at texel x + off of its row
 * (DrawCB.param.w, crack_fill_ps: a held fill's border blocks, else 0) */
static void fullscreenAt(RhiCommandList cl, const RdPipeKeyInt *k, RhiPipeline pipe, uint32_t w,
                         uint32_t h, RhiTexture t1, RhiViewAspect a1, RhiTexture t2, RhiRect sc,
                         int32_t off)
{
    rd__replay_viewport(w, h);
    rhi_cmd_set_scissor(cl, &sc);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.param[0] = (float)s_cf.rx;
    cb.param[1] = (float)s_cf.ry;
    cb.param[2] = 1.0f / (float)RD_CRACK_BEHIND;
    cb.param[3] = (float)off; /* a whole number under 2^24: exact */
    cb.mode[0] = s_mode;
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

static void fullscreen(RhiCommandList cl, const RdPipeKeyInt *k, RhiPipeline pipe, uint32_t w,
                       uint32_t h, RhiTexture t1, RhiViewAspect a1, RhiTexture t2)
{
    fullscreenAt(cl, k, pipe, w, h, t1, a1, t2, (RhiRect){0, 0, w, h}, 0);
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
    if (rd__cmd_is_state(c->type)) {
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
         * that reads SCENE follows, so no further: here, or the snapshot
         * here and the draw held (TAKE: rd_late_pass.c) */
        return *alpha ? RD_CRACK_PLACE_TAKE : RD_CRACK_PLACE_HOLD;
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

/* A held fill drawn (the pass open on SCENE's colour) after SCENE's border
 * repeat (rd_blur_border.c sceneRepeat) copied the source columns 2 and
 * W - 3 over the outer ones: without the hold the fill was drawn first and
 * the repeat copied the filled columns, its snapshot (taken before the
 * repeat, as without the hold) holding the outer columns as the fog and
 * lists 4-6 left them, which the fill reads as neighbours.  So the fill is
 * drawn over the columns the repeat left (scissored), and each block the
 * repeat copied is drawn again with the fill evaluated at its source
 * texel (crack_fill_ps's offset): a crack there writes the source's fill,
 * any other texel keeps the copy, the source's colour.  RGB alone (after
 * the glow's alpha): the alpha is the copy's either way.  Without a repeat
 * since the snapshot, the fill as fillDraw draws it */
/* FILLPASTE: SCENE's outer columns as they were at the fill's place (a
 * border repeat between the place and the landing overwrites them, and the
 * fill near the border reads them): K texels a side, put back into the
 * snapshot where the deferred fill takes it */
static struct {
    RhiTexture tex;
    RhiState state;
    uint32_t k, tw, th;
    bool saved;
} s_strip;

static void stripFree(void)
{
    if (s_strip.tex.id && g_rd.hasDevice) {
        rd__retire_texture(s_strip.tex);
    }
    memset(&s_strip, 0, sizeof(s_strip));
}

static bool stripSave(RhiCommandList cl, RdTargetRec *tc)
{
    s_strip.saved = false;
    const uint32_t k0 = (uint32_t)ceilf(2.0f * tc->sx) + 4u;
    const uint32_t k = k0 * 4u < tc->tw ? k0 : tc->tw / 4u;
    if (!k) {
        return false;
    }
    if (!s_strip.tex.id || s_strip.k != k || s_strip.th != tc->th) {
        stripFree();
        s_strip.tex = rhi_create_texture(
            &(RhiTextureDesc){2u * k, tc->th, 1, RHI_FMT_RGBA8_UNORM,
                              RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST,
                              "rd crack fill border strips", 0});
        if (!s_strip.tex.id) {
            return false;
        }
        s_strip.k = k;
        s_strip.th = tc->th;
    }
    s_strip.tw = tc->tw;
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_strip.tex, &s_strip.state, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, tc->color, (RhiRect){0, 0, k, tc->th}, s_strip.tex, 0, 0);
    rhi_cmd_copy_texture(cl, tc->color, (RhiRect){(int32_t)(tc->tw - k), 0, k, tc->th}, s_strip.tex,
                         k, 0);
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
    s_strip.saved = true;
    return true;
}

/* the strips back into the snapshot the deferred fill reads (outside any
 * pass; the snapshot left in SHADER_READ) */
static void stripRestore(RhiCommandList cl, RdTargetRec *tc)
{
    if (!s_strip.saved || s_strip.tw != tc->tw || s_strip.th != tc->th || !tc->snap.id) {
        return;
    }
    const uint32_t k = s_strip.k;
    rd__transition(cl, s_strip.tex, &s_strip.state, RHI_STATE_COPY_SRC);
    rd__transition(cl, tc->snap, &tc->snapState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, s_strip.tex, (RhiRect){0, 0, k, tc->th}, tc->snap, 0, 0);
    rhi_cmd_copy_texture(cl, s_strip.tex, (RhiRect){(int32_t)k, 0, k, tc->th}, tc->snap, tc->tw - k,
                         0);
    rd__transition(cl, tc->snap, &tc->snapState, RHI_STATE_SHADER_READ);
    s_strip.saved = false;
}

/* FILLPASTE: the fill whose snapshot was deferred is being drawn (landed or
 * flushed), and its snapshot taken by the swap: SCENE's border blocks repeated
 * since the fill's place are on the snapshot's texture, not SCENE's */
static bool s_drawDeferred, s_drawSwapped;

static void fillDrawHeld(Replay *r, RdTargetRec *tc, RhiPipeline pFill, const RdPipeKeyInt *kFill,
                         bool held)
{
    const RdBorderBlocks *b = rd__scene_border_blocks();
    /* the blocks repeated since the fill's place: drawn from their source
     * columns (the fill's value there, as the repeat after the fill gives
     * them); for a deferred fill taken by the swap the repeat wrote the
     * texture that is now the snapshot */
    const uint32_t blockTex = s_drawDeferred && s_drawSwapped ? tc->snap.id : tc->color.id;
    if (!(held || s_drawDeferred) || b->serial <= s_late.repeats ||
        b->replay != g_rd.replayCounter || b->tex != blockTex || b->over) {
        if (s_drawDeferred && b->replay == g_rd.replayCounter && b->serial > s_late.repeats &&
            (b->over || b->tex != blockTex)) {
            rd__log("crack fill: FILLPASTE: SCENE's border repeated while the fill waited and its "
                    "blocks not known (%s): the border's texels not the fill's at its place",
                    b->over ? "too many blocks" : "another texture");
        }
        fillDraw(r, tc, pFill, kFill);
        return;
    }
    RhiCommandList cl = rd__replay_cl();
    const uint32_t th = tc->th;
    if (b->x1 > b->x0) {
        fullscreenAt(cl, kFill, pFill, tc->tw, th, tc->snap, RHI_ASPECT_COLOR, tc->depth,
                     (RhiRect){b->x0, 0, (uint32_t)(b->x1 - b->x0), th}, 0);
    }
    for (uint32_t k = 0; k < b->n; k++) {
        fullscreenAt(cl, kFill, pFill, tc->tw, th, tc->snap, RHI_ASPECT_COLOR, tc->depth,
                     (RhiRect){b->b[k].x, 0, (uint32_t)b->b[k].w, th}, b->b[k].off);
    }
    r->writeSerial++; /* SCENE's colour changed (DATE's snapshot) */
    const uint32_t draws = (b->x1 > b->x0 ? 1u : 0u) + b->n;
    g_rdPerf.latePassBlockDraws += draws > 1u ? draws - 1u : 0u;
    s_cf.filled++;
    s_cf.bFilled++;
    s_place.border++;
    s_place.bBorder++;
}

static bool swapOn(void)
{
    if (s_swap.force >= 0) {
        return s_swap.force != 0;
    }
    if (s_swap.env < 0) {
        /* the tests' A/B only (TESTING.md): no config key */
        const char *e = getenv("ICO_RD_CRACK_SWAP");
        s_swap.env = !(e && e[0] == '0');
    }
    return s_swap.env != 0;
}

void rd__crack_swap_force(int on)
{
    s_swap.force = on < 0 ? -1 : on != 0;
}

int rd__crack_swap_test(void)
{
    return s_swap.test;
}

bool rd__crack_swap_on(void)
{
    return swapOn();
}

const char *rd__crack_swap_why_text(uint32_t why)
{
    return why < SWAP_WHY_COUNT ? kSwapWhy[why] : "?";
}

static bool tap2On(void)
{
    if (s_tap2Force >= 0) {
        return s_tap2Force != 0;
    }
    if (s_tap2 < 0) {
        const char *e = getenv("ICO_RD_CRACK_TAP2");
        s_tap2 = !(e && e[0] == '0');
    }
    return s_tap2 != 0;
}

void rd__crack_tap2_force(int on)
{
    s_tap2Force = on < 0 ? -1 : on != 0;
}

bool rd__crack_tap2_on(void)
{
    return tap2On();
}

/* why SCENE's colour is not swapped with its snapshot at this fill's take
 * (SWAP_WHY_NONE: it is) */
static int swapWhyNot(void)
{
    if (!swapOn()) {
        return SWAP_WHY_OFF;
    }
    if (s_swap.test != 1) {
        return SWAP_WHY_TEST;
    }
    if (rd__upscale_point_on()) {
        return SWAP_WHY_SPLIT;
    }
    /* the present from SCENE takes no refusal: its plan reads SCENE's colour
     * texture at the reduction, after this swap, and the present reads
     * SCENE's record's colour when it draws; nothing after the reduction may
     * touch SCENE (rd_present_scene.c planPair), and a later replay settles
     * an owed DISPLAY before its first command, before its own swap */
    if (rd__replay_msaa_want() > 1u) {
        return SWAP_WHY_MSAA;
    }
    if (g_rd.dstMode != RD_BLEND_EXACT_OFF) {
        return SWAP_WHY_EXACT;
    }
    return SWAP_WHY_NONE;
}

static void swapCounted(int why, const RdTargetRec *tc)
{
    if (why == SWAP_WHY_NONE) {
        g_rdPerf.crackSwaps++;
        g_rdPerf.crackSwapTexels += tc->tw * tc->th;
        s_swap.bSwapped++;
        static bool s_said;
        if (!s_said) {
            s_said = true;
            rd__log("crack fill: SCENE's colour swapped with its snapshot texture where the fill "
                    "takes its snapshot, the fill drawn over every texel (a texel that is no crack "
                    "its own): no copy of the picture, no load (reported once)");
        }
        return;
    }
    g_rdPerf.crackSwapCopied++;
    g_rdPerf.crackSwapWhy = (uint32_t)why;
    s_swap.bWhy[why]++;
}

/* the swap's key and pipeline: the fill's with the colour mask whole (the
 * attachment holds nothing before it: every channel written) */
static bool swapPipe(const RdPipeKeyInt *kFill, RdPipeKeyInt *k, RhiPipeline *p)
{
    const bool general = kFill->fs == RD_FS_CRACK_FILL;
    *k = rd__crack_fill_key(general ? RD_CRACK_KEY_FILL : RD_CRACK_KEY_FILL1);
    *p = rd__get_pipeline(k);
    return p->id != 0;
}

/* CRACKTAP: the general shader at a reach of at most 2 on both axes leaves a
 * texel with neither immediate neighbour in front on an axis (blit.hlsl:
 * a run's end lies at distance 1 there) */
static uint32_t tapMode(const RdPipeKeyInt *kFill)
{
    if (kFill->fs != RD_FS_CRACK_FILL || s_cf.rx > 2u || s_cf.ry > 2u || !tap2On()) {
        return 0u;
    }
    g_rdPerf.crackTap2++;
    return CRACK_MODE_EARLY;
}

/* the snapshot by the swap: SCENE's colour becomes its snapshot (read in
 * place), the snapshot texture its colour (rd_replay.c blurSwapTarget: the
 * pass ended, the old colour readable); the depth and the count as
 * fillTake's */
static bool fillTakeSwap(Replay *r, RdTargetRec *tc, RhiPipeline pCount, const RdPipeKeyInt *kCount)
{
    s_swap.swapping = true;
    const bool ok = rd__replay_blur_swap(r, tc);
    s_swap.swapping = false;
    if (!ok) {
        return false;
    }
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, tc->depth, &tc->depthState, RHI_STATE_DEPTH_READ);
    s_cf.sx = tc->sx;
    s_cf.sy = tc->sy;
    s_cf.rx = rd__crack_fill_reach(tc->sx);
    s_cf.ry = rd__crack_fill_reach(tc->sy);
    countPass(cl, pCount, kCount, tc->depth, tc->tw, tc->th);
    return true;
}

/* why this fill does not swap (SWAP_WHY_NONE: it does): not held, allowed,
 * its pipeline there */
static int swapChoice(bool held, const RdPipeKeyInt *kFill, RdPipeKeyInt *kS, RhiPipeline *pS)
{
    int why = held ? SWAP_WHY_HELD : swapWhyNot();
    if (why == SWAP_WHY_NONE && !swapPipe(kFill, kS, pS)) {
        why = SWAP_WHY_PIPE;
    }
    return why;
}

/* whether this fill swaps (counted) */
static bool swapDecide(const RdTargetRec *tc, bool held, const RdPipeKeyInt *kFill,
                       RdPipeKeyInt *kS, RhiPipeline *pS)
{
    const int why = swapChoice(held, kFill, kS, pS);
    swapCounted(why, tc);
    return why == SWAP_WHY_NONE;
}

/* the snapshot (or the one a held fill took), the count and the fill in a
 * pass of SCENE's colour alone, loaded and stored (with the swap: not
 * loaded, every texel drawn) */
static bool fillWork(Replay *r, RdTargetRec *tc, RhiPipeline pFill, const RdPipeKeyInt *kFill,
                     RhiPipeline pCount, const RdPipeKeyInt *kCount, bool held)
{
    RhiCommandList cl = rd__replay_cl();
    RdPipeKeyInt kS;
    RhiPipeline pS = {0};
    const bool swap = swapDecide(tc, held, kFill, &kS, &pS);
    if (held) {
        /* the snapshot taken where the fill was due; the depth back to its
         * sampled state (a pass may have bound it since: a test only).  A
         * clear pending on another target stays pending for its next pass
         * to take as its load op (rd_replay.c endPassFor, the clear carry):
         * the commands the fill was held past touch none of SCENE's
         * textures (rd__late_pass_class flushes at a clear of SCENE's colour
         * or depth), so no pending clear is SCENE's, and the fill reads and
         * writes nothing else; ended with endPass, the clear was recorded
         * here as a pass of its own (the crack fill's class: the opening's
         * WORK2 clear before the lamps) */
        rd__replay_end_pass_for(r, tc->depth.id);
        rd__transition(cl, tc->depth, &tc->depthState, RHI_STATE_DEPTH_READ);
    } else if (swap ? !fillTakeSwap(r, tc, pCount, kCount) : !fillTake(r, tc, pCount, kCount)) {
        return false;
    }
    s_drawSwapped = swap;
    if (s_drawDeferred) {
        stripRestore(cl, tc); /* the border as it was at the fill's place */
    }
    const uint32_t w = tc->tw, h = tc->th, scene = RD_TARGET_SCENE + 1u;
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, scene, w, h, RD_PERF_POST_CRACK);
    rhi_cmd_checkpoint(
        cl, rd__replay_pass_push(RD_PASSK_PASS, scene, w, h, 0, 0, 0, 0, RD_PERF_POST_CRACK));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    p.color[0].load = swap ? RHI_LOAD_DONT_CARE : RHI_LOAD_LOAD; /* the swap writes every texel */
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
    s_mode =
        tapMode(kFill) | (swap ? CRACK_MODE_COPY | (s_late.alpha ? CRACK_MODE_KEEP_A : 0u) : 0u);
    fillDrawHeld(r, tc, swap ? pS : pFill, swap ? &kS : kFill, held);
    s_mode = 0;
    s_drawDeferred = s_drawSwapped = false;
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
    s_late.alpha = s_late.paste = s_late.taken = s_late.deferred = s_late.glow = false;
    s_late.flushWhy = 0;
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

static bool fill11Take(Replay *r, RdTargetRec *tc, RhiPipeline pFill, const RdPipeKeyInt *kFill,
                       RhiPipeline pCount, const RdPipeKeyInt *kCount, int list, const RdFrame *f,
                       const RdCmd *c);

void rd__crack_fill_late(Replay *r, bool msaaOpen)
{
    /* the place fillAt names (another caller: none) */
    const int atList = s_at.list;
    const RdFrame *atF = s_at.f;
    const RdCmd *atC = s_at.c;
    s_at.list = -1;
    if (rd__crack_fill11_pending()) {
        rd__crack_fill11_own(r, RD_FILL11_WHY_END); /* a fill held for list 11's pass first */
    }
    if (!s_late.armed) {
        return;
    }
    const bool held = s_late.taken, deferred = s_late.deferred;
    s_drawDeferred = deferred;
    if (held || deferred) {
        /* the held fill in a pass of its own after all: why (the walk's end
         * or an armed paste that drew another way unless said) */
        rd__late_pass_flushed(s_late.flushWhy ? s_late.flushWhy
                              : s_late.paste  ? RD_LATE_WHY_PASTE
                                              : RD_LATE_WHY_END);
    }
    if (deferred) {
        /* its snapshot taken here (nothing between wrote SCENE's colour or
         * depth: the same inputs as at its place) */
        rd__lamps_fillpaste_noted(false, s_late.flushWhy ? s_late.flushWhy : RD_LATE_WHY_END);
    }
    s_late.flushWhy = 0;
    s_late.armed = s_late.paste = s_late.taken = s_late.deferred = s_late.glow = false;
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
    /* FILL11: the snapshot here, the draw held to list 11's pass
     * (rd_fill11.c), counted when drawn */
    const bool kept = !held && !deferred && atList >= 0 &&
                      fill11Take(r, tc, pFill, &kFill, pCount, &kCount, atList, atF, atC);
    const bool drawn = !kept && fillWork(r, tc, pFill, &kFill, pCount, &kCount, held);
    s_drawDeferred = s_drawSwapped = false;
    r->perfPost = post;
    rd__replay_perf_post(r, post);
    if (drawn) {
        fillCounted(false);
    }
}

/* FILL11's take: where rd__fill11_take lets the draw wait and its depth twin
 * is there, the snapshot (by the swap where allowed, counted) and the count
 * as fillWork takes them, the draw held (true; also when the snapshot could
 * not be taken: not drawn, as fillWork) */
static bool fill11Take(Replay *r, RdTargetRec *tc, RhiPipeline pFill, const RdPipeKeyInt *kFill,
                       RhiPipeline pCount, const RdPipeKeyInt *kCount, int list, const RdFrame *f,
                       const RdCmd *c)
{
    if (!rd__fill11_take(r, f, c, list, tc)) {
        return false;
    }
    RdPipeKeyInt kS;
    RhiPipeline pS = {0};
    const int why = swapChoice(false, kFill, &kS, &pS);
    const bool swap = why == SWAP_WHY_NONE;
    const RdPipeKeyInt kTwin = rd__fill11_twin(swap ? &kS : kFill);
    const RhiPipeline pTwin = rd__get_pipeline(&kTwin);
    if (!pTwin.id) {
        rd__fill11_refused(RD_FILL11_NOT_PIPE);
        return false;
    }
    swapCounted(why, tc);
    if (swap ? !fillTakeSwap(r, tc, pCount, kCount) : !fillTake(r, tc, pCount, kCount)) {
        return true;
    }
    s_f11.pending = true;
    s_f11.replay = g_rd.replayCounter;
    s_f11.swap = swap;
    s_f11.alpha = s_late.alpha;
    s_f11.resolved = s_late.resolved;
    s_f11.kPlain = swap ? kS : *kFill;
    s_f11.pPlain = swap ? pS : pFill;
    s_f11.kTwin = kTwin;
    s_f11.pTwin = pTwin;
    return true;
}

bool rd__crack_fill11_pending(void)
{
    return s_f11.pending && s_f11.replay == g_rd.replayCounter;
}

bool rd__crack_fill11_copy(void)
{
    return s_f11.swap;
}

/* the held fill's DrawCB.mode, as fillWork sets it for its key */
static uint32_t fill11Mode(void)
{
    return tapMode(&s_f11.kPlain) |
           (s_f11.swap ? CRACK_MODE_COPY | (s_f11.alpha ? CRACK_MODE_KEEP_A : 0u) : 0u);
}

/* fillCounted under the take's s_late.alpha and resolved */
static void fill11Counted(void)
{
    const bool alpha = s_late.alpha, resolved = s_late.resolved;
    s_late.alpha = s_f11.alpha;
    s_late.resolved = s_f11.resolved;
    fillCounted(false);
    s_late.alpha = alpha;
    s_late.resolved = resolved;
}

void rd__crack_fill11_land(Replay *r)
{
    RdTargetRec *tc = sceneRec();
    if (!rd__crack_fill11_pending() || !tc) {
        s_f11.pending = false;
        return;
    }
    s_f11.pending = false;
    s_mode = fill11Mode();
    fillDraw(r, tc, s_f11.pTwin, &s_f11.kTwin); /* the open pass: list 11's, its first draw */
    s_mode = 0;
    fill11Counted();
}

void rd__crack_fill11_own(Replay *r, int why)
{
    RdTargetRec *tc = sceneRec();
    if (!rd__crack_fill11_pending() || !tc || !tc->color.id || !tc->depth.id) {
        s_f11.pending = false;
        return;
    }
    s_f11.pending = false;
    /* fillWork's pass, its snapshot taken at the fill's place: the open
     * pass (on another target: nothing since touched SCENE) ended, a clear
     * pending on another target kept for its next pass (the clear carry) */
    const int post = r->perfPost;
    r->perfPost = RD_PERF_POST_CRACK;
    rd__replay_perf_post(r, RD_PERF_POST_CRACK);
    RhiCommandList cl = rd__replay_cl();
    rd__replay_end_pass_for(r, tc->color.id);
    rd__transition(cl, tc->depth, &tc->depthState, RHI_STATE_DEPTH_READ);
    const uint32_t w = tc->tw, h = tc->th, scene = RD_TARGET_SCENE + 1u;
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, scene, w, h, RD_PERF_POST_CRACK);
    rhi_cmd_checkpoint(
        cl, rd__replay_pass_push(RD_PASSK_PASS, scene, w, h, 0, 0, 0, 0, RD_PERF_POST_CRACK));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    p.color[0].load = s_f11.swap ? RHI_LOAD_DONT_CARE : RHI_LOAD_LOAD;
    if (s_f11.swap && rd__fill11_poison()) {
        /* the tests (rd_fill11.h): a texel the fill leaves unwritten shows */
        p.color[0].load = RHI_LOAD_CLEAR;
        p.color[0].clear[0] = p.color[0].clear[2] = p.color[0].clear[3] = 1.0f;
    }
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
    s_mode = fill11Mode();
    fillDraw(r, tc, s_f11.pPlain, &s_f11.kPlain);
    s_mode = 0;
    rhi_cmd_end_render_pass(cl);
    g_rdPerf.crackFillPasses++;
    r->perfPost = post;
    rd__replay_perf_post(r, post);
    fill11Counted();
    rd__fill11_flushed(why);
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

/* the glow's paste's pass cannot take a held fill: why (NULL: it can).  The
 * fx plan draws the paste by its premultiplied add in a pass of SCENE's
 * colour alone (rd_fx_plan.c fxPlanDraw: the tile class's profile, the
 * shader, no exact blend, no MSAA epoch), whole (no pass area), no pass on
 * SCENE open before it */
static const char *glowWhyNot(const Replay *r, const RdTargetRec *tc)
{
    RhiRect a;
    if (!g_rd.profile.fxPlan || !g_rd.fs[RD_FS_FX_FRAME].id || g_rd.dstMode != RD_BLEND_EXACT_OFF ||
        rd__uber_on()) {
        return "the glow's paste is not drawn by the premultiplied add on this device";
    }
    if (rd__replay_msaa_want() > 1u) {
        return "the MSAA epoch";
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

bool rd__crack_fill_glow_due(void)
{
    return rd__crack_fill_paste_due() && s_late.glow;
}

/* The snapshot taken now and the draw held (rd_late_pass.c): true when
 * that is done, or when the fill cannot be drawn at all (its pipelines or
 * the snapshot's memory: skipped, said, as rd__crack_fill_late does) */
static bool takeHeld(Replay *r, bool msaaOpen)
{
    RdTargetRec *tc = sceneRec();
    if (msaaOpen || s_late.replay != g_rd.replayCounter || !tc || !tc->depth.id || !tc->color.id ||
        !s_late.alpha || !rd__late_pass_hold_wanted()) {
        if (!msaaOpen && s_late.replay == g_rd.replayCounter && tc && s_late.alpha &&
            rd__late_pass_no_land()) {
            rd__late_pass_not_held(); /* no paste in this replay takes it (counted) */
        }
        return false;
    }
    RdPipeKeyInt kFill, kCount;
    RhiPipeline pFill, pCount;
    if (!fillPipes(tc, &kFill, &pFill, &kCount, &pCount)) {
        s_late.armed = false;
        return true;
    }
    /* FILLPASTE: under the lamps plan (the mask skipped: no sprite samples
     * SCENE before the paste, so no border repeat; the lamps on WORK2's own
     * depth) nothing between writes SCENE's colour or depth (the hold's
     * class flushes at any that does): the snapshot is taken where the fill
     * lands or is flushed, from the same SCENE */
    if (rd__lamps_fillpaste_on() && !(rd__lamps_bits() & RD_LAMPS_FILLHOLD)) {
        if (rd__glow_light_plan() != RD_GLOW_LIGHT_LAMPS) {
            /* the hold passed the lamps for FILLPASTE alone: another plan
             * would only hold it by a copy; drawn here, as without the hold */
            return false; /* the caller draws it at its place */
        }
        /* SCENE's outer columns saved here: a border repeat before the
         * snapshot overwrites them, and the fill near the border reads them
         * (the title's lamps frame: SCENE, DISPLAY and PRESENT apart without,
         * round 20); put back into the snapshot where it is taken */
        rd__replay_end_pass_for(r, tc->color.id);
        if (!stripSave(rd__replay_cl(), tc)) {
            return false; /* drawn at its place */
        }
        s_late.deferred = true;
        s_late.repeats = rd__scene_border_repeat_count();
        return true;
    }
    const int post = r->perfPost;
    r->perfPost = RD_PERF_POST_CRACK;
    rd__replay_perf_post(r, RD_PERF_POST_CRACK);
    const bool taken = fillTake(r, tc, pCount, &kCount);
    r->perfPost = post;
    rd__replay_perf_post(r, post);
    if (!taken) {
        s_late.armed = false;
        return true;
    }
    s_late.taken = true;
    s_late.repeats = rd__scene_border_repeat_count();
    return true;
}

/* command c with the fill held: it waits, it lands in the aura's paste
 * pass, or it is drawn now in a pass of its own (rd_late_pass.c) */
static void heldAt(Replay *r, const RdFrame *f, const RdCmd *c, bool msaaOpen)
{
    int why = RD_LATE_WHY_NONE;
    int k = RD_LATE_FLUSH;
    if (msaaOpen || rd__replay_msaa_want() > 1u) {
        why = RD_LATE_WHY_MSAA;
    } else {
        k = rd__late_pass_class(f, c, &r->st, rd__glow_light_skips(c) ? 1 : 0, &why);
    }
    if (k == RD_LATE_HOLD) {
        return;
    }
    if (k == RD_LATE_LAND_AURA || k == RD_LATE_LAND_GLOW) {
        const RdTargetRec *tc = sceneRec();
        const bool glow = k == RD_LATE_LAND_GLOW;
        const char *w = !tc ? "no scene" : glow ? glowWhyNot(r, tc) : pasteWhyNot(r, tc);
        if (!w) {
            s_late.paste = true; /* rd__crack_fill_in_pass, from the paste's draw */
            s_late.glow = glow;
            s_late.pasteOrd = g_rdStoreOrd;
            return;
        }
        if (!glow) {
            notPasted(w);
        }
        why = RD_LATE_WHY_PASTE;
    }
    s_late.flushWhy = why;
    rd__crack_fill_late(r, msaaOpen);
}

bool rd__crack_fill_lands_in_defer(void)
{
    return s_late.armed && s_late.deferred && s_late.paste && s_late.glow &&
           s_late.pasteOrd == rd__fx_defer_ord() && s_late.replay == g_rd.replayCounter;
}

void rd__crack_fill_flush_held(Replay *r, int why)
{
    if (!s_late.armed || !(s_late.taken || s_late.deferred) ||
        s_late.replay != g_rd.replayCounter) {
        return;
    }
    s_late.flushWhy = why;
    rd__crack_fill_late(r, false);
}

void rd__crack_fill_snap(Replay *r, const RdTargetRec *tc)
{
    if (s_swap.swapping) {
        return; /* the fill's own swap (FILLSWAP) */
    }
    if (tc && tc == sceneRec() && rd__crack_fill11_pending()) {
        rd__crack_fill11_own(r, RD_FILL11_WHY_SNAP); /* FILL11's held fill first */
    }
    if (s_late.armed && (s_late.taken || s_late.deferred) && tc && tc == sceneRec()) {
        rd__crack_fill_flush_held(r, RD_LATE_WHY_SNAP);
    }
    if (tc && tc == sceneRec() && rd__fx_defer_on()) {
        rd__fx_defer_draw(r, false); /* the deferred glow paste before SCENE is read */
    }
}

void rd__crack_fill_unarm_glow(void)
{
    if (s_late.armed && (s_late.taken || s_late.deferred) && s_late.paste && s_late.glow) {
        s_late.paste = s_late.glow = false; /* held on, to the aura's paste */
    }
}

static void fillAt(Replay *r, const RdFrame *f, const RdCmd *c, int list, bool msaaOpen);

void rd__crack_fill_at(Replay *r, const RdFrame *f, const RdCmd *c, int list, bool msaaOpen)
{
    rd__late_pass_cmd(c); /* the command walked (rd_late_pass.c's list 11 join) */
    /* FILLPASTE: the command that ends the fill's hold also draws the
     * deferred glow paste early (list 11's first draw into SCENE, rc30):
     * the fill armed for that paste first, so it lands in the paste's pass
     * ahead of it (rd__fx_defer_draw: the paste drawn at its own ordinal,
     * rd__crack_fill_in_pass at its first draw) instead of a pass of its
     * own before it; the order fill, paste, list 11 as before.  Where this
     * command does not end the deferral the fill waits on unarmed */
    if (list >= RD_CRACK_LATE_LIST && s_late.armed && s_late.deferred && !s_late.paste &&
        rd__fx_defer_on()) {
        s_late.paste = s_late.glow = true;
        s_late.pasteOrd = rd__fx_defer_ord();
        rd__late_pass_defer_at(r, f, c);
        /* list 11's first draw into SCENE that ends the hold: the deferral
         * goes on past it (list 11's join draws the paste early in that
         * draw's own preparation, rd_late_pass.c), but the hold would flush
         * the fill first (rc33's v746: list 11 command 26, a sprite of the
         * display layer): the paste drawn here instead, at the same place in
         * the order (before that draw), with the fill landing first in its
         * pass; the draw then joins the pass as it would have */
        if (rd__fx_defer_on() && list == 11 && !rd__cmd_is_state(c->type) && c->type != RDC_NOP) {
            int why = RD_LATE_WHY_NONE;
            if (rd__late_pass_class(f, c, &r->st, 0, &why) != RD_LATE_HOLD) {
                rd__fx_defer_draw(r, false);
            }
        }
        if (rd__fx_defer_on()) {
            s_late.paste = s_late.glow = false; /* the deferral goes on */
        }
    }
    fillAt(r, f, c, list, msaaOpen);
    if (list >= RD_CRACK_LATE_LIST) {
        rd__fill11_at(r, f, c, list);    /* a held fill waits, lands or is drawn (rd_fill11.c) */
        rd__late_pass_defer_at(r, f, c); /* the deferred glow paste, after the fill */
    }
}

static void fillAt(Replay *r, const RdFrame *f, const RdCmd *c, int list, bool msaaOpen)
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
    if (s_late.taken || s_late.deferred) {
        heldAt(r, f, c, msaaOpen);
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
    if (place == RD_CRACK_PLACE_TAKE) {
        if (takeHeld(r, msaaOpen)) {
            return; /* the snapshot here, the draw held (rd_late_pass.c) */
        }
        place = RD_CRACK_PLACE_HERE;
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
    /* drawn before c: or its draw held to list 11's pass (rd_fill11.c) */
    s_at.list = list;
    s_at.f = f;
    s_at.c = c;
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
    const bool held = s_late.taken, glow = s_late.glow, deferred = s_late.deferred;
    s_late.armed = s_late.paste = s_late.taken = s_late.deferred = s_late.glow = false;
    if (deferred) {
        /* FILLPASTE lands: the snapshot and the count here (by the swap: the
         * pass below not loaded, the fill writing every texel first, the
         * paste and list 11's draws after it in the same pass) */
        rd__lamps_fillpaste_noted(true, 0);
    }
    if (s_late.replay != g_rd.replayCounter || !tc->depth.id || !tc->color.id) {
        return;
    }
    RdPipeKeyInt kFill, kCount;
    RhiPipeline pFill, pCount;
    if (!fillPipes(tc, &kFill, &pFill, &kCount, &pCount)) {
        return;
    }
    RdPipeKeyInt kS;
    RhiPipeline pS = {0};
    const bool swap = swapDecide(tc, held, &kFill, &kS, &pS);
    if (held) {
        /* the snapshot taken where the fill was due (rd_late_pass.c); the
         * depth back to its sampled state outside any pass */
        rd__replay_end_pass(r);
        rd__transition(rd__replay_cl(), tc->depth, &tc->depthState, RHI_STATE_DEPTH_READ);
    } else {
        /* the snapshot and the count are the fill's effect; the pass is the
         * paste's (the aura's), begun as the paste's bindDraw would begin
         * it: SCENE's colour loaded, no depth (with the swap not loaded:
         * the fill writes every texel first); the paste's draw then lands
         * in it */
        const int post = r->perfPost;
        r->perfPost = RD_PERF_POST_CRACK;
        rd__replay_perf_post(r, RD_PERF_POST_CRACK);
        const bool taken =
            swap ? fillTakeSwap(r, tc, pCount, &kCount) : fillTake(r, tc, pCount, &kCount);
        r->perfPost = post;
        rd__replay_perf_post(r, post);
        if (!taken) {
            return;
        }
        if (deferred) {
            stripRestore(rd__replay_cl(), tc); /* the border as it was at the fill's place */
        }
    }
    /* FILLPASTE under ICO_RD_LAMPS_POISON: the pass not loaded cleared to
     * magenta first, so a texel the swap's fill left would show */
    static const float kPoison[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    const bool poison = deferred && swap && rd__lamps_poison();
    rd__replay_begin_pass(r, tc, NULL, RD_TARGET_SCENE + 1u, 0,
                          poison ? RHI_LOAD_CLEAR
                          : swap ? RHI_LOAD_DONT_CARE
                                 : RHI_LOAD_LOAD,
                          poison ? kPoison : NULL, RHI_LOAD_LOAD, 0.0f, false);
    s_mode =
        tapMode(&kFill) | (swap ? CRACK_MODE_COPY | (s_late.alpha ? CRACK_MODE_KEEP_A : 0u) : 0u);
    s_drawDeferred = deferred;
    s_drawSwapped = swap;
    fillDrawHeld(r, tc, swap ? pS : pFill, swap ? &kS : &kFill, held);
    s_drawDeferred = s_drawSwapped = false;
    s_mode = 0;
    fillCounted(!glow); /* the glow's paste pass is not the aura's */
    if (held) {
        rd__late_pass_held_drawn(tc, glow);
    }
    static bool s_said;
    if (!glow && !s_said) {
        s_said = true;
        rd__log("crack fill: drawn as the first draw of the aura's paste pass on SCENE (the "
                "paste's own pass, its draw after it), not in a pass of its own (reported once)");
    }
}

/* ---------------------------------------------------- FILLSWAP's self-test
 * The swap rests on a texel of the snapshot surviving Load, the float and
 * the store into an RGBA8_UNORM attachment unchanged (crack_fill_ps's COPY
 * path: k / 255 written back as k).  So, on the device, before the swap is
 * used: a 256 x 2 source of every level in every channel (each row a
 * permutation of 0..255 a channel), made as SCENE's colour is made (the
 * same usage), drawn through both fill shaders' no-crack path (the depth
 * the dummy texture: every texel alike, so no texel is a crack) with COPY,
 * one of them with KEEP_A, into a target of the same usage, read back and
 * compared texel for texel.  Any difference, or a part that cannot run: the
 * swap stays off (logged). */
#define SW_W 256u
#define SW_H 2u

static uint8_t swapLevel(uint32_t x, uint32_t y, uint32_t ch)
{
    static const uint32_t mul[2][4] = {{1u, 255u, 7u, 13u}, {3u, 5u, 11u, 1u}};
    static const uint32_t add[2][4] = {{0u, 255u, 0u, 0u}, {0u, 1u, 2u, 255u}};
    return (uint8_t)((x * mul[y][ch] + add[y][ch]) & 255u);
}

static bool swapDraw(RhiCommandList cl, RhiTexture src, RhiTexture dst, RhiState *dstState,
                     const RdPipeKeyInt *k, uint32_t mode)
{
    const RhiPipeline pipe = rd__get_pipeline(k);
    if (!pipe.id) {
        return false;
    }
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.width = SW_W;
    p.height = SW_H;
    rhi_cmd_begin_render_pass(cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)SW_W, (float)SW_H, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    const RhiRect all = {0, 0, SW_W, SW_H};
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
        rd__bind_uniform(cl, 0, rd__frame_group(SW_W, SW_H, 0.0f, 0.0f));
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(cl, 2, g);
        rhi_cmd_draw(cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst, dstState, RHI_STATE_COPY_SRC);
    return g.id != 0;
}

static bool swapSelftest(void);

bool rd__crack_swap_selftest(void)
{
    const bool ok = swapSelftest();
    (void)rd__fill11_selftest(); /* the fill's depth twins (rd_fill11.c), logged there */
    (void)rd__lamps_selftest();  /* the lamps' pass area (rd_lamps.c), logged there */
    return ok;
}

static bool swapSelftest(void)
{
    s_swap.test = 0;
    const RdTargetRec *tc = sceneRec();
    if (!g_rd.inited || !g_rd.hasDevice || !s_cf.enabled || !tc ||
        tc->format != RHI_FMT_RGBA8_UNORM || !g_rd.fs[RD_FS_CRACK_FILL].id) {
        return true; /* no fill, or none here: nothing to check */
    }
    const RhiFormat fmt = tc->format;
    const uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC |
                           RHI_TEX_COPY_DST | rd__dst_target_usage(fmt, NULL) |
                           (tc->uintView ? (uint32_t)RHI_TEX_UINT_VIEW : 0u);
    const uint32_t a = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint32_t pitch = (SW_W * 4u + a - 1u) / a * a;
    RhiTexture src = rhi_create_texture(
        &(RhiTextureDesc){SW_W, SW_H, 1, fmt, usage, "rd crack swap test source", 0});
    RhiTexture dst[2] = {
        rhi_create_texture(&(RhiTextureDesc){SW_W, SW_H, 1, fmt, usage, "rd crack swap test", 0}),
        rhi_create_texture(&(RhiTextureDesc){SW_W, SW_H, 1, fmt, usage, "rd crack swap test", 0})};
    RhiBuffer up = rhi_create_buffer(&(RhiBufferDesc){(uint64_t)pitch * SW_H, RHI_BUF_COPY_SRC,
                                                      RHI_MEM_UPLOAD, "rd crack swap test"});
    uint8_t *m = up.id ? (uint8_t *)rhi_map_buffer(up) : NULL;
    const char *why = NULL;
    int bad = -1;
    uint8_t got[4] = {0}, want[4] = {0};
    if (!src.id || !dst[0].id || !dst[1].id || !m) {
        why = "no memory for its textures";
    } else {
        for (uint32_t y = 0; y < SW_H; y++) {
            for (uint32_t x = 0; x < SW_W; x++) {
                for (uint32_t ch = 0; ch < 4u; ch++) {
                    m[(size_t)y * pitch + x * 4u + ch] = swapLevel(x, y, ch);
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
            /* the dummy (swapDraw's depth slot) uploaded and in SHADER_READ
             * first: under FSR 3.1 no replay or self-test before this one has
             * done it (rd_replay_tool_fsr3: the validation layer's "rd dummy
             * ... current layout is VK_IMAGE_LAYOUT_UNDEFINED") */
            rd__upload_textures(cl);
            RhiState ss = RHI_STATE_UNDEFINED, ds[2] = {RHI_STATE_UNDEFINED, RHI_STATE_UNDEFINED};
            rd__transition(cl, src, &ss, RHI_STATE_COPY_DST);
            rhi_cmd_copy_buffer_to_texture(cl, up, 0, pitch, src, 0, (RhiRect){0, 0, SW_W, SW_H});
            rd__transition(cl, src, &ss, RHI_STATE_SHADER_READ);
            const RdPipeKeyInt k0 = rd__crack_fill_key(RD_CRACK_KEY_FILL);
            const RdPipeKeyInt k1 = rd__crack_fill_key(
                g_rd.fs[RD_FS_CRACK_FILL1].id ? RD_CRACK_KEY_FILL1 : RD_CRACK_KEY_FILL);
            const bool drawn =
                swapDraw(cl, src, dst[0], &ds[0], &k0, CRACK_MODE_COPY | CRACK_MODE_EARLY) &&
                swapDraw(cl, src, dst[1], &ds[1], &k1, CRACK_MODE_COPY | CRACK_MODE_KEEP_A);
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
            if (!drawn) {
                why = "its pipelines missing";
            }
        }
        static uint8_t back[SW_W * SW_H * 4u + 4096u];
        for (int t = 0; t < 2 && !why; t++) {
            uint32_t rp = 0;
            if (!rhi_readback_texture(dst[t], RHI_ASPECT_COLOR, back, sizeof(back), &rp) ||
                rp < SW_W * 4u || (size_t)rp * SW_H > sizeof(back)) {
                why = "its readback failed";
                break;
            }
            for (uint32_t i = 0; i < SW_W * SW_H && bad < 0; i++) {
                const uint32_t x = i % SW_W, y = i / SW_W;
                for (uint32_t ch = 0; ch < 4u; ch++) {
                    got[ch] = back[(size_t)y * rp + x * 4u + ch];
                    want[ch] = swapLevel(x, y, ch);
                }
                if (memcmp(got, want, 4) != 0) {
                    bad = (int)i + t * (int)(SW_W * SW_H);
                }
            }
        }
    }
    if (m) {
        rhi_unmap_buffer(up);
    }
    rhi_wait_idle();
    for (int t = 0; t < 2; t++) {
        if (dst[t].id) {
            rhi_destroy_texture(dst[t]);
        }
    }
    if (src.id) {
        rhi_destroy_texture(src);
    }
    if (up.id) {
        rhi_destroy_buffer(up);
    }
    s_swap.test = !why && bad < 0 ? 1 : 2;
    if (s_swap.test == 1) {
        rd__log("crack fill: swap: on (self-test: %u levels a channel through both fill shaders "
                "read back unchanged): SCENE's colour swapped with its snapshot where the fill "
                "takes it%s",
                256u, swapOn() ? "" : "; ICO_RD_CRACK_SWAP=0 keeps the copy");
    } else if (why) {
        rd__log("crack fill: swap: off (self-test not run: %s): the snapshot copied as before",
                why);
    } else {
        rd__log("crack fill: swap: off (self-test failed at texel %d: %02X%02X%02X%02X for "
                "%02X%02X%02X%02X): the snapshot copied as before",
                bad, got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    }
    return s_swap.test == 1;
}
