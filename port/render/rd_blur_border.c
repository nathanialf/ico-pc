/* rd_blur_border.c: the replay of the post sprites (RDC_POST_STUB of the
 * rd__is_blur_kind kinds, rd_replay.c's list loop) and the reduction's
 * border with it open.
 *
 * rd__blur_post_sprite takes every staticBlur sprite and both of the
 * reduction's: a staticBlur pair the profile folds goes to
 * rd__replay_blur_fold (rd_replay_tiler.c), every other sprite to
 * rd__replay_blur_sprite_draw (rd_replay.c doBlurSpriteDraw).  The
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
 */
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
