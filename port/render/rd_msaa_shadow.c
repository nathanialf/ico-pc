/* rd_msaa_shadow.c: the shadow count drawn in the MSAA epoch, on the
 * multisampled scene's own depth and stencil (docs/port/MSAA.md, "The shadow
 * count in the epoch"; issue 209).
 *
 * Shadow.c draws its count in list 3 (rd_shadow.c): a reset of the stencil,
 * the volumes (INCR/DECR of the stencil's bits 0-5 where they pass the
 * GEQUAL test against the scene's depth, no colour written), then the
 * resolve that turns the stencil into the count's colours.  Up to v0.11.2
 * under MSAA the first volume's pass closed the epoch (rd_replay.c
 * msaaPass): msaa_resolve_ps wrote SCENE's single-sampled colour and depth
 * from SCENE_MS (the depth by the shader, its load DONT_CARE, the stencil
 * loaded and kept), and the volumes were then tested against that
 * shader-written depth in a pass of their own, the count read from SCENE's
 * stencil after them.  That pass sequence exists only with MSAA on, and two
 * drivers failed on it: issue 182 (an RTX 4060 laptop, NVIDIA 610.74: a
 * device loss at the first frame with a shadow) and issue 209 (an Adreno 740
 * under a Turnip build, "PurpleVK" 26.3.0-devel: no character shadows at
 * MSAA 2x or 4x, the rest of the picture right).  On lavapipe, with the
 * validation layer and its synchronisation checks, both drivers' profiles
 * draw the same count with and without MSAA (rd_msaa, rd_msaa_tiler).
 *
 * Here the epoch closes at the reset (msaaEnd, RD_MSAA_CLOSE_COUNTED: SCENE
 * resolved as before, at the same point of the frame), and the count is
 * drawn on SCENE_MS itself, whose colour the resolve has just read and
 * nothing reads again this replay: one pass on SCENE_MS's colour (cleared to
 * 0) and depth (the depth loaded, the stencil cleared to 0: the reset), the
 * volumes and then the seven stencil-tested draws in it at the epoch's
 * sample count, so each volume is tested at every sample against the depth
 * the scene's own draws wrote there (rasterised, never a shader's), and the
 * count's colours are written per sample.  The pass ends and SCENE_MS's
 * colour is resolved into the count target by the colour resolve
 * (rhi_cmd_resolve_color: each pixel the mean of its samples), which
 * shadowReduce then box-averages to the GS size above 1x as before.  No
 * depth the resolve shader wrote is depth-tested before the count is read,
 * no target is added (the count uses the epoch's own pair), and every pass
 * is of a kind the epoch draws every frame.  A pixel whose samples all hold
 * one count (the inside of a shadow) has the count's bytes of the frame
 * without anti-aliasing; a pixel on a shadow's edge has the mean of its
 * samples' counts, so the shadow's edge is anti-aliased like the scene's.
 *
 * The count's pass is begun again (its colour and stencil loaded: both are
 * stored) when a command between the reset and the count ended it.  The
 * count is drawn after the epoch's resolve as before (a line says why) when
 * the epoch has closed before the reset, the reset is on another depth than
 * SCENE's, the count target is not SCENE's size and format, or the
 * self-test below chose the old way.
 *
 * The self-test (rd__msaa_shadow_selftest, from rd_fog_path.c at a change of
 * the sample count or of SCENE's size with MSAA asked for): a frame of its
 * own on SCENE (never recorded, never presented): list 0 a ground at GS Z
 * 0x40000000 over the left two thirds of the frame, the clear (Z 0)
 * elsewhere; list 3 the count on WORK2 (SCENE's size: Shadow.c's count
 * target is a temporary of it) with SCENE's depth, a reset,
 * front faces at Z 0x60000000 (increments) and back faces at Z 0x20000000
 * (decrements) under GEQUAL: two overlapping volumes over the ground (counts
 * 1 and 2: the back faces fail behind the ground) and one over the clear
 * (count 0: both faces pass).  It is replayed without anti-aliasing (the
 * reference), in the epoch, and, when that differs, after the epoch's
 * resolve; a way passes when its count equals the reference's at every
 * texel whose 3x3 is one count there, and the reference's centres hold the
 * counts 2, 1 and 0.  The epoch's way stays when it passes; else the
 * resolve's when it passes; else the epoch's, with the line saying the
 * shadows may be missing (Anti-aliasing Off or a supersampling choice draws
 * them).  ICO_RD_MSAA_SHADOW=0|1 (the tests' way, never a player's) forces a
 * way; ICO_RD_MSAA_SHADOW_SABOTAGE=1 clears the epoch's stencil to 1 instead
 * of 0, so its self-test fails (ctest rd_msaa_shadow_sabotage).
 *
 * The lines:
 *   rd: shadow count: under MSAA Nx drawn in the epoch (self-test: ...) on
 *       SCENE WxH at TWxTH texels                         (each self-test)
 *   rd: shadow count: under MSAA Nx drawn in the epoch on the multisampled
 *       scene's own depth and stencil: ... (reported once a count)
 *   rd: shadow count: under MSAA Nx after the epoch's resolve in frame F:
 *       <why> (the first 8, then every 100th: K so far)
 *   rd: shadow count: the epoch's count pass begun again in frame F: ...
 *       (the first 8, then every 100th)
 *   the replay tool: --stats "shadow count: ... M in the MSAA epoch"
 *       (rd__msaa_shadow_counts) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

static struct {
    int forced; /* the tests' switch: -1 none, else 0 or 1 */
    int chosen; /* the self-test's way: 1 the epoch, 0 after the resolve */
    /* the count's window: open from the reset to the count */
    bool on, begun;
    uint32_t replay, color, depth, serial, samples;
    /* the lines */
    uint32_t declined, again, saidSamples;
    bool inTest;
    /* the self-test last run: its count and SCENE's texels */
    uint32_t testedN, testedTw, testedTh;
    /* the counts drawn in the epoch in replay countReplay */
    uint32_t counted, countReplay;
} s_mss = {.forced = -1, .chosen = 1};

static int envMode(void)
{
    const char *e = getenv("ICO_RD_MSAA_SHADOW");
    return e && (strcmp(e, "0") == 0 || strcmp(e, "1") == 0) ? e[0] - '0' : -1;
}

static bool sabotaged(void)
{
    const char *e = getenv("ICO_RD_MSAA_SHADOW_SABOTAGE");
    return e && strcmp(e, "1") == 0;
}

int rd__msaa_shadow_mode(void)
{
    if (s_mss.forced >= 0) {
        return s_mss.forced;
    }
    const int e = envMode();
    return e >= 0 ? e : s_mss.chosen;
}

void rd__msaa_shadow_force(int mode)
{
    s_mss.forced = mode < 0 ? -1 : (mode ? 1 : 0);
}

uint32_t rd__msaa_shadow_counts(void)
{
    return s_mss.countReplay == g_rd.replayCounter ? s_mss.counted : 0u;
}

void rd__msaa_shadow_retest(void)
{
    s_mss.testedN = s_mss.testedTw = s_mss.testedTh = 0;
}

void rd__msaa_shadow_free(void)
{
    s_mss.on = false; /* SCENE_MS goes: no count open on it */
}

static void declined(const char *why)
{
    s_mss.declined++;
    if (s_mss.declined <= 8u || s_mss.declined % 100u == 0u) {
        rd__log("shadow count: under MSAA %ux after the epoch's resolve in frame %u: %s (the "
                "first 8, then every 100th: %u so far)",
                rd__replay_msaa_want(), g_rdPerf.frame, why, s_mss.declined);
    }
}

/* the count's pass: SCENE_MS's colour and depth, the colour and the stencil
 * cleared the first time (the reset), loaded when begun again */
static bool begin(Replay *r)
{
    RhiTexture t[2];
    RhiState *st[2];
    uint32_t n;
    (void)rd__msaa_pair(t, st, &n);
    RdTargetRec *tc = rd__target_rec(s_mss.color);
    const RdTargetRec *ts = rd__target_rec(RD_TARGET_SCENE + 1u);
    if (!t[0].id || !t[1].id || n != s_mss.samples || !tc || !ts || tc->tw != ts->tw ||
        tc->th != ts->th) {
        return false;
    }
    rd__replay_end_pass(r);
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, t[0], st[0], RHI_STATE_RENDER_TARGET);
    rd__transition(cl, t[1], st[1], RHI_STATE_DEPTH_WRITE);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p)); /* the clears 0, both aspects stored */
    p.color[0].texture = t[0];
    p.color[0].load = s_mss.begun ? RHI_LOAD_LOAD : RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.depth.texture = t[1];
    p.depth.depthLoad = RHI_LOAD_LOAD;
    p.depth.stencilLoad = s_mss.begun ? RHI_LOAD_LOAD : RHI_LOAD_CLEAR;
    p.depth.clearStencil = sabotaged() ? 1u : 0u;
    p.width = tc->tw;
    p.height = tc->th;
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, s_mss.color, tc->tw, tc->th, r->perfPost);
    rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, s_mss.color, tc->tw, tc->th,
                                                s_mss.depth, tc->tw, tc->th, 0, r->perfPost));
    rhi_cmd_begin_render_pass(cl, &p);
    rd__replay_viewport(tc->tw, tc->th);
    r->passOpen = 1;
    r->passColor = s_mss.color;
    r->passDepth = s_mss.depth;
    r->passSerial++;
    r->passStorage = r->passReadable = r->passDepthReadable = 0;
    r->writeSerial++;
    g_rd.passSamples = (uint8_t)n; /* the volumes' and the seven draws' pipelines */
    s_mss.serial = r->passSerial;
    s_mss.begun = true;
    return true;
}

bool rd__msaa_shadow_reset(Replay *r, RdTargetRec *tc, RdTargetRec *td)
{
    s_mss.on = false;
    if (rd__replay_msaa_want() <= 1u) {
        return false; /* no MSAA this replay */
    }
    RhiTexture t[2];
    RhiState *st[2];
    uint32_t n;
    const bool open = rd__msaa_pair(t, st, &n);
    const RdTargetRec *ts = rd__target_rec(RD_TARGET_SCENE + 1u);
    const char *why = NULL;
    if (rd__msaa_shadow_mode() == 0) {
        why = s_mss.inTest ? "the self-test's replay of the count after the resolve"
              : s_mss.forced >= 0 || envMode() >= 0
                  ? "the tests' switch (ICO_RD_MSAA_SHADOW=0)"
                  : "the self-test found the count in the epoch wrong on this device";
    } else if (!open) {
        why = "the epoch closed before the reset (the scene read or copied, or no 3D draw yet)";
    } else if (r->st.depth != RD_TARGET_SCENE + 1u || td != ts) {
        why = "the reset is on another depth than the scene's";
    } else if (tc->tw != ts->tw || tc->th != ts->th || tc->format != RHI_FMT_RGBA8_UNORM) {
        why = "the count target's size or format is not the scene's";
    } else if (tc == ts) {
        why = "the count target is the scene's own colour";
    }
    if (why) {
        declined(why);
        return false;
    }
    s_mss.on = true;
    s_mss.begun = false;
    s_mss.replay = g_rd.replayCounter;
    s_mss.color = r->st.color;
    s_mss.depth = r->st.depth;
    s_mss.samples = n;
    /* the epoch closes here: SCENE resolved before its pair is the count's */
    rd__msaa_close(r);
    if (!begin(r)) {
        s_mss.on = false;
        declined("its pass could not begin");
        return false;
    }
    return true;
}

bool rd__msaa_shadow_pass(Replay *r)
{
    if (!s_mss.on) {
        return false;
    }
    if (s_mss.replay != g_rd.replayCounter) {
        s_mss.on = false; /* a window a replay left open */
        return false;
    }
    if (r->st.color != s_mss.color || r->st.depth != s_mss.depth) {
        return false; /* another target's draws: theirs */
    }
    if (r->passOpen && r->passSerial == s_mss.serial) {
        return true;
    }
    s_mss.again++;
    if (s_mss.again <= 8u || s_mss.again % 100u == 0u) {
        rd__log("shadow count: the epoch's count pass begun again in frame %u (a command between "
                "the reset and the count ended it): its colour and stencil loaded (the first 8, "
                "then every 100th: %u so far)",
                g_rdPerf.frame, s_mss.again);
    }
    if (begin(r)) {
        return true;
    }
    s_mss.on = false;
    rd__log("shadow count: the epoch's count pass could not begin again in frame %u: the count "
            "from here after the epoch's resolve, without the volumes drawn so far",
            g_rdPerf.frame);
    return false;
}

void rd__msaa_shadow_done(Replay *r, RdTargetRec *tc)
{
    if (!s_mss.on) {
        return;
    }
    s_mss.on = false;
    RhiTexture t[2];
    RhiState *st[2];
    uint32_t n;
    (void)rd__msaa_pair(t, st, &n);
    if (!t[0].id || n != s_mss.samples) {
        return;
    }
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, t[0], st[0], RHI_STATE_COPY_SRC);
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_COPY_DST);
    rhi_cmd_resolve_color(cl, t[0], tc->color);
    r->writeSerial++;
    if (s_mss.countReplay != g_rd.replayCounter) {
        s_mss.countReplay = g_rd.replayCounter;
        s_mss.counted = 0;
    }
    s_mss.counted++;
    const uint32_t bit = s_mss.samples < 32u ? 1u << s_mss.samples : 0u;
    if (bit && !(s_mss.saidSamples & bit)) {
        s_mss.saidSamples |= bit;
        rd__log("shadow count: under MSAA %ux drawn in the epoch on the multisampled scene's own "
                "depth and stencil (%ux%u, %u samples): the epoch resolved at the reset, then the "
                "reset as the pass's stencil clear, the volumes tested at each sample against "
                "the depth the scene drew, the seven stencil-tested draws per sample into its "
                "colour, resolved into the count; no depth the resolve wrote is tested before "
                "the count (reported once a count)",
                s_mss.samples, tc->tw, tc->th, s_mss.samples);
    }
}

/* -------------------------------------------- the count target's size
 * Issue 209's log :923 ("shadow command without a depth-stencil target of
 * the colour target's size: skipped") at the switch to 4x supersampling
 * (5.95349 a GS pixel, SCENE 4096x2667): a frame recorded before a scale
 * change holds its count target (rd_shadow_count_target, a temporary of the
 * scene's GS size) at the texels of the scale it was made at, and replayed
 * after the change (the targets made again) its texels are not SCENE's, so
 * every shadow command of the frame was skipped.  Such a temporary is made
 * again here at the scale in force (its contents are the count's own: the
 * reset, the volumes and the resolve write what is read), with a line. */
bool rd__shadow_size_agree(RdTargetRec *tc, const RdTargetRec *td)
{
    static uint32_t s_remade, s_refused;
    if (tc->tw == td->tw && tc->th == td->th) {
        return true;
    }
    RdTargetRec want = *tc;
    rd__target_scale_of(&want, -1);
    const bool fits =
        !tc->named && tc->live && !tc->withDepth && want.tw == td->tw && want.th == td->th;
    if (!fits) {
        s_refused++;
        if (s_refused <= 8u || s_refused % 100u == 0u) {
            rd__log("shadow count: the count target is %ux%u texels (GS %ux%u, %s), its depth "
                    "target %ux%u (GS %ux%u): the shadow command skipped (the first 8, then every "
                    "100th: %u so far)",
                    tc->tw, tc->th, tc->w, tc->h, tc->named ? "named" : "a temporary", td->tw,
                    td->th, td->w, td->h, s_refused);
        }
        return false;
    }
    const uint32_t ow = tc->tw, oh = tc->th;
    rd__target_destroy_gpu(tc);
    tc->sx = want.sx;
    tc->sy = want.sy;
    tc->tw = want.tw;
    tc->th = want.th;
    tc->ox = want.ox;
    const bool made = rd__target_create_gpu(tc, "temp target");
    tc->clearPending = 1;
    s_remade++;
    if (s_remade <= 8u || s_remade % 100u == 0u) {
        rd__log("shadow count: the count target made at %ux%u texels before the scale changed "
                "is made again at the scene's %ux%u (a frame recorded before the change, "
                "replayed after it)%s (the first 8, then every 100th: %u so far)",
                ow, oh, tc->tw, tc->th, made ? "" : ": it could not be made, the count skipped",
                s_remade);
    }
    return made && tc->color.id != 0;
}

/* ------------------------------------------------------------ the self-test */

#define MST_GROUND 0x40000000u /* the ground's GS Z: the clear is 0 */
#define MST_FRONT 0x60000000u  /* the volumes' front faces, in front of the ground */
#define MST_BACK 0x20000000u   /* their back faces, behind it */

/* the column x/16 of a w-wide frame in 1/16 GS pixels from its left edge,
 * seven sixteenths past it (sub-pixel edges) */
static int32_t colX(uint32_t w, uint32_t num, uint32_t den)
{
    return (2048 - (int32_t)(w / 2)) * 16 + (int32_t)(num * w * 16u / den) + 7;
}

static int32_t rowY(uint32_t h, uint32_t num, uint32_t den)
{
    return (2048 - (int32_t)(h / 2)) * 16 + (int32_t)(num * h * 16u / den) + 5;
}

static void quad(RdScreenVtx *o, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t z)
{
    const int32_t xs[6] = {x0, x1, x0, x0, x1, x1}, ys[6] = {y0, y0, y1, y1, y0, y1};
    for (int i = 0; i < 6; i++) {
        memset(&o[i], 0, sizeof(o[i]));
        o[i].x = xs[i];
        o[i].y = ys[i];
        o[i].z = z;
        o[i].q = 1.0f;
    }
}

/* the volumes: [x0, x1) of eighths, rows 1/8 to 7/8 */
static const uint8_t kVol[3][2] = {{1, 4}, {2, 5}, {6, 7}};

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t work, uint32_t w, uint32_t h,
                       uint32_t step)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    static const uint8_t kClear[4] = {30, 60, 90, 0};
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1; /* the depth too, to Z 0 */
        c->u[1] = 0;
    }
    ok = ok && c && rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_push1(f, 0, RDC_ABE, 0) &&
         rd__st_test(f, 0, RD_TEST_Z_ALWAYS) && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) &&
         rd__st_push1(f, 0, RDC_FBA, 0) && rd__st_push1(f, 0, RDC_SHADE, 0);
    /* the ground: one sprite over columns [0, 2w/3), the whole height */
    RdScreenVtx g[2];
    memset(g, 0, sizeof(g));
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    for (int k = 0; k < 2; k++) {
        g[k].x = (ox + (k ? (int32_t)(w * 2u / 3u) : 0)) * 16;
        g[k].y = (oy + (k ? (int32_t)h : 0)) * 16;
        g[k].z = MST_GROUND;
        g[k].q = 1.0f;
        g[k].rgba[0] = 0x60;
        g[k].rgba[1] = 0x50;
        g[k].rgba[2] = 0x40;
        g[k].rgba[3] = 0x80;
    }
    c = ok ? rd__st_push(f, 0, RDC_SCREEN) : NULL;
    if (c) {
        c->b[0] = RD_PRIM_SPRITES;
        c->b[1] = RD_SPACE_FULLFRAME;
        c->u[0] = rd__frame_payload(f, g, (uint32_t)sizeof(g));
        c->u[1] = 2;
    }
    const uint8_t b0 = (uint8_t)(step == 8 ? 8 : 0);
    RdCmd *rc = NULL;
    /* list 3: the count on WORK2 (SCENE's size) with SCENE's depth, as
     * Shadow.c's count target */
    RdCmd *tg = ok && c ? rd__st_push(f, 3, RDC_TARGET) : NULL;
    if (tg) {
        tg->u[0] = work;
        tg->u[1] = scene;
        tg->u[2] = (w & 0xFFFF) | ((h & 0xFFFF) << 16);
        tg->b[0] = RD_TARGET_OFFSET;
    }
    ok = ok && tg && rd__st_push(f, 3, RDC_TEXTURE_OFF) && rd__st_push1(f, 3, RDC_ABE, 0) &&
         rd__st_test(f, 3, RD_TEST_Z_GEQUAL) && rd__st_push1(f, 3, RDC_ZWRITE, RD_ZWRITE_OFF) &&
         (rc = rd__st_push(f, 3, RDC_SHADOW_RESET)) != NULL;
    if (rc) {
        rc->b[0] = b0;
    }
    RdScreenVtx v[3 * 12];
    for (int i = 0; i < 3; i++) {
        const int32_t x0 = colX(w, kVol[i][0], 8), x1 = colX(w, kVol[i][1], 8), y0 = rowY(h, 1, 8),
                      y1 = rowY(h, 7, 8);
        quad(&v[i * 6], x0, y0, x1, y1, MST_FRONT);     /* the increments first */
        quad(&v[18 + i * 6], x0, y0, x1, y1, MST_BACK); /* then the decrements */
    }
    const uint32_t off = rd__frame_payload(f, v, (uint32_t)sizeof(v));
    RdCmd *s = ok ? rd__st_push(f, 3, RDC_SHADOW_STRIP) : NULL;
    if (s) {
        s->b[0] = RD_SHADOW_TRIS;
        s->u[0] = 18;
        s->u[1] = off;
        s->u[2] = (uint32_t)sizeof(v);
        s->u[3] = 18;
    }
    RdCmd *rv = s ? rd__st_push(f, 3, RDC_SHADOW_RESOLVE) : NULL;
    if (rv) {
        rv->b[0] = b0;
    }
    return rv != NULL;
}

/* one replay of f, the count (on work) at its GS size into px; SCENE and
 * the count's target from their clears */
static bool run(const RdFrame *f, uint32_t scene, uint32_t work, uint8_t *px, size_t size,
                uint32_t w, uint32_t h)
{
    uint32_t rw = 0, rh = 0;
    RdTargetRec *t = rd__target_rec(scene), *k = rd__target_rec(work);
    if (t) {
        t->clearPending = 1;
    }
    if (k) {
        k->clearPending = 1;
    }
    return rd__replay_frame(f, 0, false) && rd__shadow_count_read(work, px, size, &rw, &rh) &&
           rw == w && rh == h;
}

static bool uniform3x3(const uint8_t *a, uint32_t w, uint32_t h, uint32_t x, uint32_t y)
{
    const uint8_t *c = &a[((size_t)y * w + x) * 4u];
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            const int64_t xx = (int64_t)x + dx, yy = (int64_t)y + dy;
            if (xx < 0 || yy < 0 || xx >= (int64_t)w || yy >= (int64_t)h) {
                continue;
            }
            if (memcmp(&a[((size_t)yy * w + (size_t)xx) * 4u], c, 4) != 0) {
                return false;
            }
        }
    }
    return true;
}

/* got against the reference away from its edges: the texels compared and
 * those that differ, the first of them */
static void compare(const uint8_t *ref, const uint8_t *got, uint32_t w, uint32_t h, size_t *inner,
                    size_t *diff, size_t *first)
{
    *inner = *diff = 0;
    *first = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            if (!uniform3x3(ref, w, h, x, y)) {
                continue;
            }
            (*inner)++;
            const size_t i = (size_t)y * w + x;
            if (memcmp(&ref[i * 4u], &got[i * 4u], 4) != 0 && !(*diff)++) {
                *first = i;
            }
        }
    }
}

/* the reference's centres: counts 2 (both volumes over the ground), 1 (one)
 * and 0 (over the clear) */
static int centres(const uint8_t *ref, uint32_t w, uint32_t h, uint32_t step)
{
    static const uint8_t kAt[3][2] = {{6, 2}, {9, 1}, {13, 0}}; /* sixteenths of w, count */
    int ok = 0;
    for (int i = 0; i < 3; i++) {
        const uint32_t x = kAt[i][0] * w / 16u, y = h / 2u;
        uint8_t want[4];
        rd__shadow_table_texel(kAt[i][1], step, want);
        ok += memcmp(&ref[((size_t)y * w + x) * 4u], want, 4) == 0;
    }
    return ok;
}

static void say(char *out, size_t n, const char *way, bool done, size_t inner, size_t diff,
                size_t first, uint32_t w, const uint8_t *ref, const uint8_t *got)
{
    if (!done) {
        snprintf(out, n, "%s: not drawn or not read back", way);
    } else if (diff) {
        const uint8_t *a = &got[first * 4u], *b = &ref[first * 4u];
        snprintf(out, n,
                 "%s differs at %zu of %zu texels away from edges, the first (%zu,%zu): "
                 "%02X%02X%02X%02X for %02X%02X%02X%02X",
                 way, diff, inner, first % w, first / w, a[0], a[1], a[2], a[3], b[0], b[1], b[2],
                 b[3]);
    } else {
        snprintf(out, n,
                 "%s equal to the count without anti-aliasing at %zu texels away from "
                 "edges",
                 way, inner);
    }
}

bool rd__msaa_shadow_selftest(uint32_t samples)
{
    if (!g_rd.inited || !g_rd.hasDevice || samples <= 1u) {
        return true;
    }
    if (g_rd.msaaBlocked) {
        rd__log("shadow count: under MSAA %ux: self-test not run (MSAA is held off: the fog's "
                "depth path cannot read the multisampled depth)",
                samples);
        return true;
    }
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id, work = rd_target(RD_TARGET_WORK2).id;
    RdTargetRec *t = rd__target_rec(scene), *k = rd__target_rec(work);
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM ||
        !k || !k->color.id || k->tw != t->tw || k->th != t->th || k->w != t->w || k->h != t->h ||
        k->format != RHI_FMT_RGBA8_UNORM) {
        rd__log("shadow count: under MSAA %ux: self-test: no SCENE and WORK2 of one size to run "
                "it on",
                samples);
        return false;
    }
    if (s_mss.testedN == samples && s_mss.testedTw == t->tw && s_mss.testedTh == t->th) {
        return s_mss.chosen == 1; /* run for this count and size already */
    }
    s_mss.testedN = samples;
    s_mss.testedTw = t->tw;
    s_mss.testedTh = t->th;
    const uint32_t w = t->w, h = t->h, step = rd__shadow_disc_step();
    const size_t size = (size_t)w * h * 4u;
    uint8_t *ref = malloc(size), *epoch = malloc(size), *after = malloc(size);
    static RdFrame f;
    if (!ref || !epoch || !after || !buildFrame(&f, scene, work, w, h, step)) {
        free(ref);
        free(epoch);
        free(after);
        rd__frame_free(&f);
        rd__log("shadow count: under MSAA %ux: self-test: could not build its frame", samples);
        return false;
    }
    const bool wasBlocked = g_rd.msaaBlocked;
    const int wasForced = s_mss.forced;
    s_mss.inTest = true;
    g_rd.msaaBlocked = true; /* the reference: no epoch */
    const bool doneRef = run(&f, scene, work, ref, size, w, h);
    g_rd.msaaBlocked = false;
    s_mss.forced = 1;
    const bool doneEpoch = run(&f, scene, work, epoch, size, w, h);
    const bool tookEpoch =
        doneEpoch && rd__msaa_shadow_counts() > 0 && g_rdPerf.msaaSamples == samples;
    size_t inner = 0, diffE = 0, firstE = 0, diffA = 0, firstA = 0, innerA = 0;
    if (doneRef && doneEpoch) {
        compare(ref, epoch, w, h, &inner, &diffE, &firstE);
    }
    const int nCentres = doneRef ? centres(ref, w, h, step) : 0;
    const bool epochOk =
        doneRef && tookEpoch && diffE == 0 && inner > (size_t)w * h / 4u && nCentres == 3;
    bool doneAfter = false, afterOk = false;
    if (!epochOk) {
        s_mss.forced = 0;
        doneAfter = run(&f, scene, work, after, size, w, h);
        if (doneRef && doneAfter) {
            compare(ref, after, w, h, &innerA, &diffA, &firstA);
        }
        afterOk =
            doneRef && doneAfter && diffA == 0 && innerA > (size_t)w * h / 4u && nCentres == 3;
    }
    s_mss.forced = wasForced;
    s_mss.inTest = false;
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&f);
    rd__shadow_count_forget(); /* the count's target was WORK2 */
    t->clearPending = 1;       /* the next replay starts SCENE and WORK2 from their clears */
    k->clearPending = 1;
    s_mss.chosen = epochOk || !afterOk ? 1 : 0;
    char a[200], b[200] = "";
    say(a, sizeof a, "in the epoch", tookEpoch, inner, diffE, firstE, w, ref, epoch);
    if (!tookEpoch && doneEpoch) {
        snprintf(a, sizeof a, "in the epoch: not taken (the epoch closed before the reset)");
    }
    if (!epochOk) {
        say(b, sizeof b, "; after the epoch's resolve", doneAfter, innerA, diffA, firstA, w, ref,
            after);
    }
    const char *verdict = epochOk   ? "drawn in the epoch"
                          : afterOk ? "drawn after the epoch's resolve"
                                    : "drawn in the epoch, but neither way matched: the shadows "
                                      "may be missing with MSAA on this device (Anti-aliasing Off "
                                      "or a supersampling choice draws them)";
    rd__log("shadow count: under MSAA %ux %s (self-test: %s%s%s; the count without "
            "anti-aliasing %s, %d of 3 centres right) on SCENE %ux%u at %ux%u texels%s",
            samples, verdict, a, b, sabotaged() ? "; ICO_RD_MSAA_SHADOW_SABOTAGE" : "",
            doneRef ? "read" : "not read", nCentres, w, h, t->tw, t->th,
            rd__msaa_shadow_mode() != s_mss.chosen ? " (the tests' switch decides)" : "");
    free(ref);
    free(epoch);
    free(after);
    return epochOk || afterOk;
}
