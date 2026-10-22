/* rd_blur_border.c: the replay of the post sprites (RDC_POST_STUB of the
 * rd__is_blur_kind kinds, rd_replay.c's list loop) and the reduction's
 * border with it open.
 *
 * rd__blur_post_sprite takes every staticBlur sprite and both of the
 * reduction's: a staticBlur pair the profile folds goes to
 * rd__replay_blur_fold (rd_replay_tiler.c), every other sprite to
 * rd__replay_blur_sprite_draw (rd_replay.c doBlurSpriteDraw); the motion
 * blur's under Motion blur: Smooth goes to rd__blur_smooth_sprite
 * (rd_blur_smooth.c) first.  The
 * reduction's textured sprite with the border open (rd__replay_border_open:
 * Full pixel on, or the border mask on a cropped frame) is drawn with the
 * scissor the whole target and both axes on CLAMP, and where the frame is
 * its exact width (rd_present.c rd__frame_cropped false: 4:3, 48:9, every
 * aspect whose 384 x aspect is a whole number of GS pixels, 2340 x 1080
 * among them) its two outer columns a side are drawn again as the column
 * next to them (rd__blur_border_strips, rd_blur.c).
 *
 * Why: the PS2 never shows GS columns 0, 1, W - 2 and W - 1 (the
 * reduction's scissor 2 .. W - 3, rd_post.c postReduction, and the TV's
 * overscan past that), and the game does not always draw them: a room may
 * leave the top of column 0 uncovered, which the depth fog then paints with
 * its table's entry 0 (the colour at Z 0, near white in a lit room;
 * rd_fog_path.c), and the glow spreads it twenty pixels in (issue 129: a
 * bright band down the left edge of a 2340 x 1080 phone, in the room with
 * the hanging chain).  Repeating the neighbour shows the picture the game
 * drew instead of what it left.  On a cropped frame (16:9 and the other
 * aspects whose exact width is under gsW) the presenter leaves those
 * columns out already, so nothing is drawn again there and the picture is
 * byte for byte what it was.  The top and bottom rows the reduction's
 * scissor held black stay what SCENE holds (docs/port/RENDER.md).
 *
 * The glow reads SCENE before the reduction (list 7's flare and bloom
 * reduce SCENE into the work buffers), so the fog-painted columns also
 * spread about ten GS pixels into the picture, on a cropped frame (16:9)
 * as at the exact width (issue 26's latest picture: 4K, 16:9).  So a
 * fogged frame's SCENE has its two outer GS columns a side made copies of
 * columns 2 and W - 3 (rd__scene_border_arm from doFog, done by
 * sceneRepeat before the frame's first post sprite that reads SCENE):
 * texel copies through a small texture of their own, every row, the
 * colour alone, wherever the border is open or the frame cropped
 * (rd__scene_border_state).  With the border closed (Full pixel off at 4:3
 * and at an exact width such as 48:9: the GS's own DISPLAY with its
 * border, the PS2's picture, what the gate's Original 4:3 hashes hold)
 * SCENE is left as the game drew it.  The strips above stay: the
 * reduction's column 2 blends a quarter of column 3, so its outer columns
 * drawn from SCENE's repeat alone would read column 2 without that
 * quarter.
 */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "rd_replay.h"

/* rd__set_blur_border_repeat (rd_internal.h, tests): the strips drawn */
static bool s_borderRepeat = true;

void rd__set_blur_border_repeat(bool on)
{
    s_borderRepeat = on;
}

/* the strips drawn since start (rd_log_frame_state's line) */
static uint64_t s_stripDraws;

uint64_t rd__blur_border_strip_count(void)
{
    return s_stripDraws;
}

/* ------------------------------------------------- SCENE's outer columns */

/* rd__set_scene_border_repeat (rd_internal.h, tests): the repeat done */
static bool s_sceneRepeat = true;

void rd__set_scene_border_repeat(bool on)
{
    s_sceneRepeat = on;
}

/* the repeats done since start (rd_log_frame_state's line) */
static uint64_t s_sceneRepeats;

uint64_t rd__scene_border_repeat_count(void)
{
    return s_sceneRepeats;
}

int rd__scene_border_state(const RdTargetRec *scene)
{
    if (scene && scene->ox > 0.0f) {
        return RD_BORDER_WINDOW;
    }
    if (rd__frame_cropped()) {
        return RD_BORDER_CROPPED;
    }
    return rd__replay_border_open() ? RD_BORDER_OPEN : RD_BORDER_CLOSED;
}

/* the replay (g_rd.replayCounter) whose fog asked for the repeat; 0 none */
static uint64_t s_armed;

void rd__scene_border_arm(const Replay *r)
{
    if (r->st.color == RD_TARGET_SCENE + 1u) {
        s_armed = g_rd.replayCounter;
    }
}

/* the copies' own texture: the two source columns' texels side by side,
 * SCENE's rows (an RHI copy cannot read and write one texture) */
static struct {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
    RhiFormat format;
} s_cols;

void rd__scene_border_free(void)
{
    if (s_cols.tex.id) {
        rhi_destroy_texture(s_cols.tex);
    }
    memset(&s_cols, 0, sizeof(s_cols));
}

static bool colsEnsure(uint32_t w, uint32_t h, RhiFormat format)
{
    if (s_cols.tex.id && s_cols.w >= w && s_cols.h == h && s_cols.format == format) {
        return true;
    }
    rd__scene_border_free();
    s_cols.tex = rhi_create_texture(
        &(RhiTextureDesc){w, h, 1, format, RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST,
                          "rd border columns", 0});
    if (!s_cols.tex.id) {
        return false;
    }
    s_cols.state = RHI_STATE_UNDEFINED;
    s_cols.w = w;
    s_cols.h = h;
    s_cols.format = format;
    return true;
}

/* The first texel of GS column x on t: the first whose point (texel t is
 * GS t / sx from the frame's left, less the Full pixel crop:
 * rd__gs_texel_x) lies at or right of x, clamped to the texture */
static int32_t columnTexel(const RdTargetRec *t, int32_t x)
{
    const double v = ceil((double)x * (double)t->sx - (double)t->ox - 1e-4);
    return v < 0.0 ? 0 : (v > (double)t->tw ? (int32_t)t->tw : (int32_t)v);
}

/* tmp's block [m, m + cw) (column 2's or W - 3's texels) copied to dst at
 * x, cut to dst's texels [d0, d1) */
static void copyBlock(RhiCommandList cl, RhiTexture dst, int32_t x, int32_t d0, int32_t d1,
                      int32_t m, int32_t cw, uint32_t th)
{
    int32_t sx = m, w = cw;
    if (x < d0) {
        sx += d0 - x;
        w -= d0 - x;
        x = d0;
    }
    if (x + w > d1) {
        w = d1 - x;
    }
    if (w > 0) {
        rhi_cmd_copy_texture(cl, s_cols.tex, (RhiRect){sx, 0, w, th}, dst, x, 0);
    }
}

/* dst's texels [d0, d1) filled with copies of tmp's block [m, m + cw), laid
 * from the neighbour column outwards: right to left from d1 on the left
 * (the block's last texel next to column 2), left to right from d0 on the
 * right, the outermost copy cut at the edge.  At a whole scale that is the
 * column twice, texel for texel */
static void fillFrom(RhiCommandList cl, RhiTexture dst, int32_t d0, int32_t d1, int32_t m,
                     int32_t cw, uint32_t th, int right)
{
    if (right) {
        for (int32_t x = d0; x < d1; x += cw) {
            copyBlock(cl, dst, x, d0, d1, m, cw, th);
        }
    } else {
        for (int32_t x = d1 - cw; x + cw > d0; x -= cw) {
            copyBlock(cl, dst, x, d0, d1, m, cw, th);
        }
    }
}

/* SCENE's GS columns 0, 1 and W - 2, W - 1 made copies of columns 2 and
 * W - 3, texel for texel, every row (the file's header).  Outside any
 * pass; SCENE's colour is left in COPY_DST (a later read moves it, which
 * also closes an open MSAA epoch first: the copies read the resolved
 * picture). */
static void sceneRepeat(Replay *r)
{
    RdTargetRec *t = rd__target_rec(RD_TARGET_SCENE + 1u);
    const int32_t W = (int32_t)r->st.gsW;
    if (!t || !t->color.id || W < 6 || t->w != (uint32_t)W || t->th == 0) {
        return;
    }
    const int state = rd__scene_border_state(t);
    if (state == RD_BORDER_CLOSED) {
        return; /* the GS's own border: the PS2's picture */
    }
    /* Full pixel's window (ox > 0) holds no texel of them at the usual
       crops (texel 0 is GS ox / sx, past column 2); any it holds are
       repeated as at ox 0 */
    const int32_t a2 = columnTexel(t, 2), a3 = columnTexel(t, 3);
    const int32_t bR = columnTexel(t, W - 3), aR = columnTexel(t, W - 2);
    const int32_t cwL = a3 - a2, cwR = aR - bR, tw = (int32_t)t->tw;
    const bool left = a2 > 0 && cwL > 0, right = aR < tw && cwR > 0;
    if (!left && !right) {
        static bool logged; /* once a run */
        if (!logged && state == RD_BORDER_WINDOW) {
            logged = true;
            rd__log("border: the frame's outer two columns are not in SCENE (Full pixel's window "
                    "holds the visible columns only): nothing to repeat (reported once)");
        }
        return;
    }
    if (rd__win_muted()) {
        return;
    }
    const uint32_t th = t->th;
    if (!colsEnsure((uint32_t)(cwL + cwR), th, t->format)) {
        return;
    }
    rd__replay_end_pass(r);
    const RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_cols.tex, &s_cols.state, RHI_STATE_COPY_DST);
    if (left) {
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){a2, 0, cwL, th}, s_cols.tex, 0, 0);
    }
    if (right) {
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){bR, 0, cwR, th}, s_cols.tex, cwL, 0);
    }
    rd__transition(cl, s_cols.tex, &s_cols.state, RHI_STATE_COPY_SRC);
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_DST);
    if (left) {
        fillFrom(cl, t->color, 0, a2, 0, cwL, th, 0);
    }
    if (right) {
        fillFrom(cl, t->color, aR, tw, cwL, cwR, th, 1);
    }
    s_sceneRepeats++;
    static bool logged; /* once a run */
    if (!logged) {
        logged = true;
        rd__log("border: the frame's outer two columns a side repeat columns 2 and %d on SCENE "
                "after the fog (%d GS pixels; the glow and the present read them)",
                (int)(W - 3), (int)W);
    }
}

/* the repeat a fog of this replay asked for, before the post sprite about
 * to be drawn when it reads SCENE (the glow's first reduction, the depth
 * of field's, at the latest the reduction into DISPLAY): there SCENE's
 * pass ends anyway, so the repeat costs no pass of its own on a tiler's
 * local fog read, and an open MSAA epoch is closed where it would be */
static void sceneRepeatDue(Replay *r)
{
    if (!s_armed || s_armed != g_rd.replayCounter || !r->st.ds.texEnabled) {
        return;
    }
    const RdTexRec *tx = rd__tex_rec(r->st.tex);
    if (!tx || tx->kind != RD_TEXKIND_TARGET ||
        rd__target_rec(tx->target) != rd__target_rec(RD_TARGET_SCENE + 1u)) {
        return;
    }
    s_armed = 0;
    if (s_sceneRepeat) {
        sceneRepeat(r);
    }
}

/* The two strips of the reduction's textured sprite c (its record p, the
 * frame f), each through rd__replay_blur_sprite_draw as a sprite of its
 * own: a record (and, for the hardware sprite a scaled target draws, its
 * four vertices) in a payload of their own, the recorded frame untouched.
 * The scissor and the wrap are the caller's (the whole target, CLAMP). */
typedef struct StripPayload {
    RdPostRec p;
    RdScreenVtx v[4]; /* the plain pair, then the mirrored one (rd_post.c reductionSprite) */
} StripPayload;

static void drawStrips(Replay *r, const RdFrame *f, const RdCmd *c)
{
    static StripPayload s_buf;
    static RdFrame s_sf;
    RdPostRec p, strip[2];
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    if (!rd__blur_border_strips(&p, p.uv, (int32_t)r->st.gsW, strip)) {
        return;
    }
    RdScreenVtx v[4], sv[8];
    const bool hw = p.lutOffset != ~0u;
    if (hw) {
        memcpy(v, f->payload + p.lutOffset, sizeof(v));
        rd__blur_border_verts(v, (int32_t)r->st.gsW, sv);
    }
    static bool s_logged;
    if (!s_logged) {
        s_logged = true;
        rd__log("display: the border is open at the frame's exact width (%u GS pixels): its "
                "two outer columns a side repeat columns 2 and %u (reported once)",
                (unsigned)r->st.gsW, (unsigned)r->st.gsW - 3u);
    }
    for (int k = 0; k < 2; k++) {
        memset(&s_buf, 0, sizeof(s_buf));
        s_buf.p = strip[k];
        s_buf.p.lutOffset = hw ? (uint32_t)offsetof(StripPayload, v) : ~0u;
        if (hw) {
            memcpy(s_buf.v, &sv[4 * k], sizeof(s_buf.v));
        }
        s_sf = *f;
        s_sf.payload = (uint8_t *)&s_buf;
        s_sf.payloadSize = (uint32_t)sizeof(s_buf);
        RdCmd sc = *c;
        sc.u[1] = 0;
        sc.u[2] = (uint32_t)sizeof(RdPostRec);
        rd__replay_blur_sprite_draw(r, &s_sf, &sc);
        s_stripDraws++;
    }
}

/* The reduction's textured sprite with the border open: the game draws it
 * inside a scissor two columns and a few rows in from the frame's edge (the
 * black border), and with the wrap it never sets to CLAMP, so the last
 * column also blends in a quarter of the first.  Here the scissor is the
 * whole target and both axes clamp, so the border shows the picture SCENE
 * holds under it and the edges read their own texels; at the frame's exact
 * width the outer two columns a side are then drawn again as columns 2 and
 * W - 3 (drawStrips).  The recorded scissor and wrap stay in the frame (and
 * in the dumps); this replaces them for these draws and puts them back
 * after them, whichever way they return.  The black clear sprite before it
 * is not textured and covers everything already. */
void rd__blur_post_sprite(Replay *r, const RdFrame *f, const RdCmd *c)
{
    sceneRepeatDue(r); /* SCENE's outer columns before the first read of it */
    if (c->b[0] == RD_POST_MOTION_BLUR && rd__blur_smooth_sprite(r, f, c)) {
        return; /* Motion blur: Smooth (rd_blur_smooth.c) */
    }
    if (rd__replay_blur_fold(r, f, c)) {
        return; /* a pair folded into one pass (rd_replay_tiler.c) */
    }
    if (!rd__replay_border_open() || c->b[0] != RD_POST_REDUCTION || !r->st.ds.texEnabled) {
        rd__replay_blur_sprite_draw(r, f, c);
        return;
    }
    const RdTargetRec *tc = rd__target_rec(r->st.color);
    if (!tc) {
        rd__replay_blur_sprite_draw(r, f, c);
        return;
    }
    int32_t scissor[4];
    memcpy(scissor, r->st.scissor, sizeof(scissor));
    const RdSamplerWrap wrap = r->st.ds.wrap;
    r->st.scissor[0] = 0;
    r->st.scissor[1] = 0;
    r->st.scissor[2] = (int32_t)tc->w - 1;
    r->st.scissor[3] = (int32_t)tc->h - 1;
    r->st.ds.wrap.s = RD_WRAP_CLAMP;
    r->st.ds.wrap.t = RD_WRAP_CLAMP;
    rd__replay_blur_sprite_draw(r, f, c);
    /* the frame at its exact width: nothing of it is cropped, so the
     * columns the PS2 hid are shown; a cropped frame (16:9) leaves them
     * out at present and is drawn as it was */
    if (s_borderRepeat && !rd__frame_cropped()) {
        drawStrips(r, f, c);
    }
    memcpy(r->st.scissor, scissor, sizeof(scissor));
    r->st.ds.wrap = wrap;
}
