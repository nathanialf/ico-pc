/* rd_pass_area.c: the pass area (docs/port/WORK_BUFFERS.md, "The pass area";
 * [dev] pass_area = auto|on|off, ICO_RD_PASS_AREA).
 *
 * A tile-based GPU loads every loaded attachment of a render pass into its
 * tiles when the pass begins and writes every stored one back when it
 * ends, over the pass's render area.  SCENE at the PowerVR phone's
 * Resolution Auto (2364 x 1080) is 10.2 MB of colour and 10.2 to 20.4 MB
 * of depth and stencil each way, so a pass on it whose draws leave tile
 * columns or rows untouched still moves the whole picture.  With the
 * render area (RhiRenderPassDesc.area; Vulkan VkRenderingInfo /
 * VkRenderPassBeginInfo renderArea) only the tiles inside it are loaded
 * and stored.
 *
 * Not the aura's paste: the pass "aura SCENE 2366x1080 1 draws" the area
 * was aimed at draws FEED128 (212 x 112 texels, its texture) over the
 * frame's whole rect (staticBlur.c pasteFeedBackAreaToFB and addWork1ToFB
 * draw `rect`, the frame; the phone's F12 dump of 2026-11-02 11:42, list 8
 * command 55: (25984,29184)-(39552,36352) at 848 x 448), and list 11's
 * pass begins with a full-frame sprite: on the stage 6 frames no pass on
 * SCENE spares a tile, which the 10 s line's kept-whole clause says
 * ("its draws cover the picture", RD_AREA_WHY_LARGE).
 *
 * Exact by construction.  The Vulkan specification (VkRenderPassBeginInfo,
 * and VkRenderingInfo the same: "The effects of attachment load, store, and
 * multisample resolve operations are restricted to the pixels whose x and
 * y coordinates fall within the render area on all attachments"; "The
 * application must ensure (using scissor if necessary) that all rendering
 * is contained within the render area")
 * leaves every pixel outside the area as memory holds it.  So the bytes
 * equal the whole pass's when no draw of the pass touches a pixel outside
 * the area: such a pixel is then loaded and stored unchanged by the whole
 * pass (LOAD and STORE), left as it is by the area pass; a DONT_CARE load
 * or store (the store plan's depth, rd_store_plan.c) leaves it undefined
 * in the one and as it was in the other, and nothing reads it (that is
 * why the plan dropped it).  A CLEAR load clears the whole attachment, so
 * a pass that takes a clear (a pending clear as its load op, a shadow
 * reset as its stencil's, the store plan's poison) has no area.  The RHI
 * cuts every scissor to the area (vk_cmd.c rhi_cmd_set_scissor), so a
 * wrong area would show as missing pixels on every device, lavapipe too
 * (rd_pass_area_ab).
 *
 * Which draws a pass holds, before it begins: the store plan's rule
 * (rd_store_plan.c's header) is that a pass begun while command k is
 * replayed (g_rdStoreOrd) holds the work of command k and later ones only
 * (a screen run or a VU merge is flushed into the pass it was recorded in;
 * a fold reads ahead, never back).  The scan (rd__pass_area_scan_cmd, in
 * rd__store_plan_scan's one walk of the lists before the replay's walk,
 * beside the pass cut's and the store plan's; rd__pass_area_scan alone in
 * the tests) reads the lists with their state once and gives every command
 * a kind:
 *   NONE   draws nothing in the walk: a state command, a NOP, the overlay's
 *          text, a text item the present draws (g_rd.deferText), a sprite
 *          the fades fold leaves to the reduction (rd__fades_fold_skip);
 *   BOX    a 2D draw on its target (st.color) whose texels lie in a box
 *          known now: a screen-prim command (rd__screen_area: its
 *          vertices' box padded by two GS pixels, three with PRIM.AA1's
 *          fringe, through the draw's mirror and texel scale, in its
 *          scissor; a UI command takes the box with and without the mirror;
 *          a full-frame sprite the box widened to the frame's width, as
 *          extendFullFrame may widen it), and the aura's sprite
 *          (RD_POST_AURA, its paste into SCENE: fx_rect_vs covers the
 *          target and fx_sprite_ps keeps the GS pixels inside the rect, so
 *          the rect padded by two GS pixels through gsRectToTexels in the
 *          scissor bindDraw sets, the box blurSnapArea takes for its
 *          snapshot); an empty box draws nothing;
 *   WHOLE  a draw whose texels only the GPU knows, or a part of whose work
 *          reaches past the box: the VU commands (meshes, skinned, grids,
 *          particles: vertex work decides their coverage), the shadow
 *          commands, the fog (and the crack filler drawn in its pass: a
 *          fill reaches up to its reach past every uncovered texel run,
 *          anywhere in the target), every other post sprite (the
 *          reduction's open border, the glow's edge strips, the blur fold,
 *          Motion blur: Smooth draw past or beside their rect), a screen
 *          command under DATE (its stencil setup draws over the scissor,
 *          rd_replay_tiler.c rd__replay_date_take) or COLCLAMP 0's wrap
 *          (doScreenWrap), and a command whose target, payload or rect
 *          cannot be read;
 *   BREAK  anything else (a clear, a copy, an exact blend, a command the
 *          walk does not replay): it ends any pass's commands.
 * A pass beginning on target c at command k (rd__pass_area_begin, from
 * rd_replay.c beginPass) takes the union of the BOX commands on c from k
 * on, up to (not with) E, the first command after k that is BREAK or a
 * draw on another target; a WHOLE command on c in between, or command k
 * itself not a BOX on c (the pass begun for something the scan did not
 * see: a fold's draw, a VU flush, a DATE pass), and the pass has no area.
 * The walk ends a pass that is still open when it reaches command E
 * (rd__pass_area_due, at the top of rd_replay.c's command loop), so
 * whatever E and later commands draw on c goes to a pass of its own, with
 * its own area: the area holds the pass's draws whatever the walk does.
 * That end is normally where the walk ends the pass anyway (E draws on
 * another target or records outside a pass); it is counted
 * (RdPerfRecord.passAreaEnds) and costs one pass more when it is not.
 *
 * Where: a render pass beginPass begins on a target with its own depth
 * (SCENE, the target of the paste and of list 11's sprites; FEED128 and
 * the work buffers are small targets already), single-sampled (not in
 * the MSAA epoch), not a storage pass, without a CLEAR load, never under
 * the replay tool's pass window (it reads targets mid-frame; the store
 * plan is off there too).  The area is the union aligned out to whole 32 x
 * 32 tiles (RD_PASS_AREA_TILE, the PowerVR and Mali tile; the render area
 * granularity's multiple on the tilers measured), clamped to the target;
 * it is taken when it is at least a tile column or row narrower than the
 * target, else the pass keeps the whole target.  On by default on the
 * tile-based classes (RdProfile.passArea); D3D11, D3D12 and OpenGL ignore
 * the area (rhi.h), desktop Vulkan has it off ([dev] pass_area = on
 * turns it on anywhere, the tests' way: lavapipe then clips to it).
 *
 * The lines:
 *   rd: pass area: on|off (<why>): ...             (start)
 *   rd: profile: ...; pass area: on|off            (the profile line)
 *   rd: pass area: SCENE in list 8 (the aura's sprite): WxH at X,Y of
 *       2364x1080 (N of 2516 tiles ...)            (the first 8 kinds a run)
 *   window: per replay ..., N passes with an area (M % of the picture's
 *       tiles loaded, K ended early); W passes kept whole a replay (A its
 *       draws cover the picture, B a 3D draw, ...)  (the 10 s summary: the
 *       passes on a target with its own depth begun whole, by the first
 *       reason that holds, rd.h RD_AREA_WHY_*, RdPerfRecord.passAreaWhy)
 *   the replay tool: --stats "pass area on|off: N passes with an area ...;
 *       W passes kept whole (...)", --pass-times "... area WxH at X,Y" on a
 *       pass that has one. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

static int32_t floorDiv16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

static bool rectMeet(RhiRect a, RhiRect b, RhiRect *out)
{
    const int64_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    const int64_t ax1 = (int64_t)a.x + a.w, bx1 = (int64_t)b.x + b.w;
    const int64_t ay1 = (int64_t)a.y + a.h, by1 = (int64_t)b.y + b.h;
    const int64_t x1 = ax1 < bx1 ? ax1 : bx1, y1 = ay1 < by1 ? ay1 : by1;
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    *out = (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
    return true;
}

/* ------------------------------------------- GS pixels to texels (rd_replay.c) */

/* GS pixels x0..x1, y0..y1 (inclusive, after XYOFFSET) of target tc in its
 * texels: the UI flip when mirror (GS pixel p is target pixel w - 1 - p),
 * clipped to the target, then the texel scale: GS pixels x0..x1 cover texels
 * floor(x0 * sx) .. ceil((x1 + 1) * sx) - 1.  A draw's box may start
 * outside (its vertices off the target's edge).  False when nothing is
 * left. */
bool rd__gs_rect_texels(const RdTargetRec *tc, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        int mirror, RhiRect *out)
{
    if (mirror) {
        const int32_t a = (int32_t)tc->w - 1 - x1, b = (int32_t)tc->w - 1 - x0;
        x0 = a;
        x1 = b;
    }
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= (int32_t)tc->w ? (int32_t)tc->w - 1 : x1;
    y1 = y1 >= (int32_t)tc->h ? (int32_t)tc->h - 1 : y1;
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    if (tc->sx != 1.0f || tc->sy != 1.0f) {
        int32_t a = (int32_t)((float)x0 * tc->sx), b = (int32_t)((float)y0 * tc->sy);
        int32_t c = (int32_t)((float)(x1 + 1) * tc->sx + 0.999f) - 1;
        int32_t d = (int32_t)((float)(y1 + 1) * tc->sy + 0.999f) - 1;
        if (tc->ox > 0.0f) {
            /* Full pixel: the frame's columns from ox texels in
             * (rd__gs_texel_x); the GS pixels outside the window clip */
            a = (int32_t)floorf(rd__gs_texel_x(tc, (float)x0));
            c = (int32_t)ceilf(rd__gs_texel_x(tc, (float)(x1 + 1)) - 0.001f) - 1;
            a = a < 0 ? 0 : a;
            if (c < a) {
                return false;
            }
        }
        x0 = a;
        y0 = b;
        x1 = c >= (int32_t)tc->tw ? (int32_t)tc->tw - 1 : c;
        y1 = d >= (int32_t)tc->th ? (int32_t)tc->th - 1 : d;
        if (x1 < x0) {
            return false;
        }
    }
    *out = (RhiRect){x0, y0, (uint32_t)(x1 - x0 + 1), (uint32_t)(y1 - y0 + 1)};
    return true;
}

/* the scissor (SCISSOR_1, inclusive GS pixels) in texels, clipped to the
 * target first; a flipped UI draw clips where its scissor lands after the
 * flip. */
bool rd__scissor_texels(const RdTargetRec *tc, const RdStateBlock *st, int mirror, RhiRect *sc)
{
    int32_t x0 = st->scissor[0] < 0 ? 0 : st->scissor[0];
    int32_t y0 = st->scissor[1] < 0 ? 0 : st->scissor[1];
    int32_t x1 = st->scissor[2] >= (int32_t)tc->w ? (int32_t)tc->w - 1 : st->scissor[2];
    int32_t y1 = st->scissor[3] >= (int32_t)tc->h ? (int32_t)tc->h - 1 : st->scissor[3];
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    return rd__gs_rect_texels(tc, x0, y0, x1, y1, mirror, sc);
}

/* Every fragment of a screen-prim command lies in its vertices' bounding
 * box give or take a pixel: sprite corners snapped up to whole pixels
 * (snapAxis), points drawn as one-pixel quads, lines reaching the pixels
 * next to them, the mirror's sixteenth of a texel and the half-line offset
 * of a field target; AA1 adds a one-pixel fringe outside every edge
 * (aa1Fringe).  The pad covers all of them, and convVtx's clamp to the
 * 12.4 range is applied to the box as to the vertices. */
bool rd__screen_area(const RdTargetRec *tc, const RdStateBlock *st, const RdScreenVtx *v,
                     uint32_t n, int mirror, int aa1, RhiRect *area)
{
    if (!tc || n == 0) {
        return false;
    }
    int32_t lx = v[0].x, hx = v[0].x, ly = v[0].y, hy = v[0].y;
    for (uint32_t i = 1; i < n; i++) {
        lx = v[i].x < lx ? v[i].x : lx;
        hx = v[i].x > hx ? v[i].x : hx;
        ly = v[i].y < ly ? v[i].y : ly;
        hy = v[i].y > hy ? v[i].y : hy;
    }
    lx = lx < 0 ? 0 : (lx > 0xFFFF ? 0xFFFF : lx);
    hx = hx < 0 ? 0 : (hx > 0xFFFF ? 0xFFFF : hx);
    ly = ly < 0 ? 0 : (ly > 0xFFFF ? 0xFFFF : ly);
    hy = hy < 0 ? 0 : (hy > 0xFFFF ? 0xFFFF : hy);
    const int32_t pad = aa1 ? 3 : 2;
    const int32_t ox = 2048 - (int32_t)(st->gsW >> 1), oy = 2048 - (int32_t)(st->gsH >> 1);
    RhiRect box, sc;
    if (!rd__gs_rect_texels(tc, floorDiv16(lx) - ox - pad, floorDiv16(ly) - oy - pad,
                            -floorDiv16(-hx) - ox + pad, -floorDiv16(-hy) - oy + pad, mirror,
                            &box) ||
        !rd__scissor_texels(tc, st, mirror, &sc)) {
        return false;
    }
    return rectMeet(box, sc, area);
}

/* ------------------------------------------------------------- the switch */

static struct {
    int forced; /* tests: -1 none, else 0 or 1 */
    bool on;
    char why[160];
    /* the scan: a kind, target and box a command (by ordinal), for the
     * replay replayCounter; valid while that replay walks */
    struct PaCmd *cmd;
    uint32_t n, cap, replay;
    bool valid;
    /* the open pass with an area: rd_replay.c's passSerial, the command it
     * ends before, its colour target and depth texture; ended: the walk
     * ended it there before a draw on another target (endFor, endDepth:
     * the next pass begun on the same ones is the one pass more) */
    bool open, ended;
    uint32_t openSerial, openEnd, openFor, openDepth, endFor, endDepth;
    /* since start: passes with an area, their tiles and their targets',
     * passes ended early; the passes on a target with its own depth begun
     * whole, by why (rd.h RD_AREA_WHY_*) */
    uint64_t passes, tiles, targetTiles, ends;
    uint64_t whyRun[RD_AREA_WHY_COUNT];
    /* the kinds named in the log (target, list, command type), the first 8 */
    uint32_t said[8][3];
    uint32_t nSaid;
} s_pa = {.forced = -1};

enum { PA_NONE = 0, PA_BOX, PA_WHOLE, PA_BREAK };

typedef struct PaCmd {
    uint32_t target; /* st.color */
    uint8_t kind;    /* PA_* */
    uint8_t type;    /* RdCmdType (the log's word) */
    uint8_t whole;   /* PA_WHOLE: its RD_AREA_WHY_* (VU, FOG, SHADOW, POST, SCREEN) */
    uint32_t tw, th; /* the target's texture size at the scan */
    RhiRect box;     /* PA_BOX: the texels it can touch; w 0: none */
} PaCmd;

int rd__pass_area_pick(const char *env, int def)
{
    if (!env || !env[0] || strcmp(env, "auto") == 0) {
        return def ? 1 : 0;
    }
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "on") == 0 ||
        strcmp(env, "yes") == 0) {
        return 1;
    }
    if (strcmp(env, "0") == 0 || strcmp(env, "false") == 0 || strcmp(env, "off") == 0 ||
        strcmp(env, "no") == 0) {
        return 0;
    }
    return def ? 1 : 0; /* another word: auto (the start line says so) */
}

static bool decide(void)
{
    const char *env = getenv("ICO_RD_PASS_AREA");
    const bool def = g_rd.profile.passArea;
    if (s_pa.forced >= 0) {
        snprintf(s_pa.why, sizeof s_pa.why, "the tests' switch");
        return s_pa.forced != 0;
    }
    const int want = rd__pass_area_pick(env, def);
    const bool auto_ = !env || !env[0] || strcmp(env, "auto") == 0;
    const bool known = auto_ || rd__pass_area_pick(env, 0) == rd__pass_area_pick(env, 1);
    if (!known) {
        snprintf(s_pa.why, sizeof s_pa.why,
                 "ICO_RD_PASS_AREA=%.32s is not auto, on or off: the device profile's, %s", env,
                 def ? "a tile-based GPU" : "an immediate-mode GPU");
    } else if (!auto_) {
        snprintf(s_pa.why, sizeof s_pa.why, "[dev] pass_area = %s, ICO_RD_PASS_AREA=%.32s",
                 want ? "on" : "off", env);
    } else if (def) {
        snprintf(s_pa.why, sizeof s_pa.why,
                 "a tile-based GPU's default; [dev] pass_area = off keeps every pass whole");
    } else {
        snprintf(s_pa.why, sizeof s_pa.why,
                 "an immediate-mode GPU's default; [dev] pass_area = on gives the passes areas");
    }
    return want != 0;
}

const char *rd__pass_area_profile_words(void)
{
    char why[sizeof s_pa.why];
    memcpy(why, s_pa.why, sizeof why);
    const bool on = decide();
    memcpy(s_pa.why, why, sizeof why); /* rd__pass_area_init keeps its own */
    return on ? "pass area: on" : "pass area: off";
}

static void note(void)
{
    rd__log("pass area: %s (%s)%s", s_pa.on ? "on" : "off", s_pa.why,
            s_pa.on ? ": a pass on the picture whose draws are all flat sprites and shapes "
                      "with a known place loads and stores only the 32 x 32 tiles they touch "
                      "(the same picture)"
                    : "");
}

void rd__pass_area_init(void)
{
    s_pa.on = decide();
    s_pa.valid = s_pa.open = false;
    s_pa.passes = s_pa.tiles = s_pa.targetTiles = s_pa.ends = 0;
    memset(s_pa.whyRun, 0, sizeof(s_pa.whyRun));
    s_pa.nSaid = 0;
    note();
}

bool rd__pass_area_on(void)
{
    return s_pa.on;
}

void rd__pass_area_force(int mode)
{
    s_pa.forced = mode < 0 ? -1 : (mode ? 1 : 0);
    s_pa.on = decide();
    note();
}

void rd__pass_area_counts(uint64_t *passes, uint64_t *tiles, uint64_t *targetTiles, uint64_t *ends)
{
    if (passes) {
        *passes = s_pa.passes;
    }
    if (tiles) {
        *tiles = s_pa.tiles;
    }
    if (targetTiles) {
        *targetTiles = s_pa.targetTiles;
    }
    if (ends) {
        *ends = s_pa.ends;
    }
}

void rd__pass_area_why_counts(uint64_t why[RD_AREA_WHY_COUNT])
{
    memcpy(why, s_pa.whyRun, sizeof(s_pa.whyRun));
}

/* ------------------------------------------------------- one command's box */

/* the texels of target tc in GS pixels x0..x1, y0..y1 (12.4 window
 * coordinates, after XYOFFSET: as the vertices and the rect hold them)
 * padded by pad GS pixels, in the scissor: false when nothing is left */
static bool rectBox(const RdTargetRec *tc, const RdStateBlock *st, int32_t x0, int32_t y0,
                    int32_t x1, int32_t y1, int32_t pad, RhiRect *out)
{
    const int32_t ox = 16 * (2048 - (int32_t)(st->gsW >> 1));
    const int32_t oy = 16 * (2048 - (int32_t)(st->gsH >> 1));
    RhiRect box, sc;
    return rd__gs_rect_texels(tc, floorDiv16(x0 - ox) - pad, floorDiv16(y0 - oy) - pad,
                              -floorDiv16(-(x1 - ox)) + pad, -floorDiv16(-(y1 - oy)) + pad, 0,
                              &box) &&
           rd__scissor_texels(tc, st, 0, &sc) && rectMeet(box, sc, out);
}

static void boxAdd(RhiRect *u, const RhiRect *b)
{
    if (!b->w || !b->h) {
        return;
    }
    if (!u->w || !u->h) {
        *u = *b;
        return;
    }
    const int64_t x0 = u->x < b->x ? u->x : b->x, y0 = u->y < b->y ? u->y : b->y;
    const int64_t ux1 = (int64_t)u->x + u->w, bx1 = (int64_t)b->x + b->w;
    const int64_t uy1 = (int64_t)u->y + u->h, by1 = (int64_t)b->y + b->h;
    const int64_t x1 = ux1 > bx1 ? ux1 : bx1, y1 = uy1 > by1 ? uy1 : by1;
    *u = (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

/* a screen-prim command (doScreen) */
static int screenKind(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, uint32_t ord,
                      const RdTargetRec *tc, RhiRect *box)
{
    if ((g_rd.deferText && c->b[3] == RD_SCREEN_TEXT_QUADS) || rd__fades_fold_skip(ord)) {
        return PA_NONE; /* drawn on the output, or by the reduction */
    }
    if (rd__wrap_applies(st) || st->ds.test.date != RD_DATE_OFF) {
        return PA_WHOLE;
    }
    uint32_t n = c->u[1];
    if (n == 0) {
        return PA_BOX; /* nothing drawn */
    }
    if (!f->payload || (uint64_t)c->u[0] + (uint64_t)n * sizeof(RdScreenVtx) > f->payloadSize) {
        return PA_WHOLE;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    RdScreenVtx wide[2];
    if (c->b[1] == RD_SPACE_FULLFRAME && c->b[0] == RD_PRIM_SPRITES) {
        /* extendFullFrame may move a sprite's edges out to the frame's:
         * the box from x = ox to ox + w GS pixels at the least */
        int32_t lx = v[0].x, hx = v[0].x, ly = v[0].y, hy = v[0].y;
        for (uint32_t i = 1; i < n; i++) {
            lx = v[i].x < lx ? v[i].x : lx;
            hx = v[i].x > hx ? v[i].x : hx;
            ly = v[i].y < ly ? v[i].y : ly;
            hy = v[i].y > hy ? v[i].y : hy;
        }
        const int32_t ox = 2048 - (int32_t)(st->gsW >> 1);
        lx = lx < ox * 16 ? lx : ox * 16;
        hx = hx > (ox + (int32_t)tc->w) * 16 ? hx : (ox + (int32_t)tc->w) * 16;
        wide[0] = v[0];
        wide[1] = v[0];
        wide[0].x = lx;
        wide[0].y = ly;
        wide[1].x = hx;
        wide[1].y = hy;
        v = wide;
        n = 2;
    }
    const int aa1 = st->aa1 != 0; /* the fringe's pad on any prim: one pixel more */
    RhiRect a = {0, 0, 0, 0}, b = {0, 0, 0, 0};
    if (!rd__screen_area(tc, st, v, n, 0, aa1, &a)) {
        a = (RhiRect){0, 0, 0, 0};
    }
    if (c->b[1] == RD_SPACE_UI && rd__screen_area(tc, st, v, n, 1, aa1, &b)) {
        boxAdd(&a, &b); /* the mirror's flip (mirrorUi) or not */
    }
    *box = a;
    return PA_BOX;
}

/* the aura's sprite (doBlurSpriteDraw: fx_rect_vs, fx_sprite_ps's coverage) */
static int auraKind(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, const RdTargetRec *tc,
                    RhiRect *box)
{
    if (!f->payload || (uint64_t)c->u[1] + sizeof(RdPostRec) > f->payloadSize ||
        st->ds.test.date != RD_DATE_OFF) {
        return PA_WHOLE;
    }
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    if (rd__blur_screen_fallback(c->b[0], &p, st) != ~0u) {
        return PA_WHOLE; /* a hardware sprite (the reduction's only) */
    }
    for (int k = 0; k < 4; k++) {
        if (!(p.rect[k] >= -1048576.0f && p.rect[k] <= 1048576.0f)) {
            return PA_WHOLE; /* NaN or past the 12.4 range */
        }
    }
    const int32_t x0 = (int32_t)p.rect[0], y0 = (int32_t)p.rect[1];
    const int32_t x1 = (int32_t)p.rect[2], y1 = (int32_t)p.rect[3];
    *box = (RhiRect){0, 0, 0, 0};
    if (x1 > x0 && y1 > y0 && !rectBox(tc, st, x0, y0, x1, y1, 2, box)) {
        *box = (RhiRect){0, 0, 0, 0};
    }
    return PA_BOX;
}

int rd__pass_area_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, uint32_t ord,
                      RhiRect *box)
{
    *box = (RhiRect){0, 0, 0, 0};
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return PA_NONE;
    case RDC_SCREEN:
    case RDC_POST_STUB: {
        const bool aura = c->type == RDC_POST_STUB && c->b[0] == RD_POST_AURA;
        if (c->type == RDC_POST_STUB && !aura) {
            return c->b[0] == RD_POST_FOG || rd__is_blur_kind(c->b[0]) ? PA_WHOLE : PA_BREAK;
        }
        /* a box only where an area may be taken: a target with a depth */
        const RdTargetRec *tc = rd__target_rec(st->color);
        if (!tc || !tc->color.id || !tc->withDepth || !tc->depth.id) {
            return PA_WHOLE;
        }
        return aura ? auraKind(f, c, st, tc, box) : screenKind(f, c, st, ord, tc, box);
    }
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
        return PA_WHOLE;
    default:
        return PA_BREAK; /* a clear, a copy, an exact blend, the unreplayed */
    }
}

/* ---------------------------------------------------------------- the scan */

/* a PA_WHOLE command's reason (rd.h RD_AREA_WHY_*) */
static int wholeWhy(const RdCmd *c)
{
    switch (c->type) {
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
        return RD_AREA_WHY_VU;
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
        return RD_AREA_WHY_SHADOW;
    case RDC_POST_STUB:
        return c->b[0] == RD_POST_FOG ? RD_AREA_WHY_FOG : RD_AREA_WHY_POST;
    default:
        return RD_AREA_WHY_SCREEN; /* DATE, the wrap, a target or payload not read */
    }
}

bool rd__pass_area_scan_begin(const RdFrame *f, int keep)
{
    s_pa.valid = s_pa.open = s_pa.ended = false;
    s_pa.n = 0;
    if (!s_pa.on || !f || g_rdWin.first) {
        return false; /* off, or a pass window (it reads targets mid-frame) */
    }
    uint32_t total = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        total += f->lists[l].count;
    }
    if (total > s_pa.cap) {
        const uint32_t cap = total + total / 4u + 64u;
        PaCmd *p = realloc(s_pa.cmd, (size_t)cap * sizeof(*p));
        if (!p) {
            return false; /* no memory: no areas this replay */
        }
        s_pa.cmd = p;
        s_pa.cap = cap;
    }
    return true;
}

void rd__pass_area_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                            uint32_t ord)
{
    PaCmd *e = &s_pa.cmd[ord];
    memset(e, 0, sizeof(*e));
    e->type = c->type;
    if (state) {
        return; /* PA_NONE */
    }
    e->target = st->color;
    const RdTargetRec *tc = rd__target_rec(st->color);
    e->tw = tc ? tc->tw : 0u;
    e->th = tc ? tc->th : 0u;
    e->kind = (uint8_t)rd__pass_area_cmd(f, c, st, ord, &e->box);
    e->whole = (uint8_t)wholeWhy(c);
}

void rd__pass_area_scan_end(uint32_t n)
{
    s_pa.n = n;
    s_pa.replay = g_rd.replayCounter;
    s_pa.valid = true;
}

void rd__pass_area_scan(const RdFrame *f, int keep)
{
    if (!rd__pass_area_scan_begin(f, keep)) {
        return;
    }
    RdStateBlock st = f->startState;
    uint32_t ord = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++, ord++) {
            const RdCmd *c = &list->cmds[i];
            const bool state = rd__apply_state(&st, c);
            rd__pass_area_scan_cmd(f, c, &st, state, ord);
        }
    }
    rd__pass_area_scan_end(ord);
}

/* rd__pass_area_of with why (RD_AREA_WHY_*) set when it is false */
static bool areaOf(uint32_t cid, const RdTargetRec *c, uint32_t k, RhiRect *area, uint32_t *end,
                   int *why)
{
    *why = RD_AREA_WHY_SCAN;
    if (!s_pa.valid || s_pa.replay != g_rd.replayCounter || !c || k >= s_pa.n) {
        return false;
    }
    const PaCmd *e = &s_pa.cmd[k];
    if (e->target != cid || e->tw != c->tw || e->th != c->th) {
        return false; /* begun for another target than the scan's command */
    }
    if (e->kind != PA_BOX) {
        /* begun for work the scan cannot bound */
        *why = e->kind == PA_WHOLE ? e->whole : RD_AREA_WHY_BEGIN;
        return false;
    }
    RhiRect u = e->box;
    uint32_t i = k + 1u;
    for (; i < s_pa.n; i++) {
        e = &s_pa.cmd[i];
        if (e->kind == PA_NONE) {
            continue;
        }
        if (e->kind == PA_BREAK || e->target != cid) {
            break; /* the pass's commands end here at the latest */
        }
        if (e->kind == PA_WHOLE || e->tw != c->tw || e->th != c->th) {
            *why = e->kind == PA_WHOLE ? e->whole : RD_AREA_WHY_SCAN;
            return false;
        }
        boxAdd(&u, &e->box);
    }
    if (!u.w || !u.h) {
        *why = RD_AREA_WHY_EMPTY;
        return false; /* nothing drawn: the pass as it is */
    }
    *area = u;
    if (end) {
        *end = i;
    }
    return true;
}

bool rd__pass_area_of(uint32_t cid, const RdTargetRec *c, uint32_t k, RhiRect *area, uint32_t *end)
{
    int why;
    return areaOf(cid, c, k, area, end, &why);
}

/* the area aligned out to whole tiles, clamped to w x h */
static RhiRect tileAlign(RhiRect a, uint32_t w, uint32_t h)
{
    const int64_t t = RD_PASS_AREA_TILE;
    int64_t x0 = a.x > 0 ? a.x : 0, y0 = a.y > 0 ? a.y : 0;
    int64_t x1 = (int64_t)a.x + a.w, y1 = (int64_t)a.y + a.h;
    x0 = x0 / t * t;
    y0 = y0 / t * t;
    x1 = (x1 + t - 1) / t * t;
    y1 = (y1 + t - 1) / t * t;
    x1 = x1 < (int64_t)w ? x1 : (int64_t)w;
    y1 = y1 < (int64_t)h ? y1 : (int64_t)h;
    if (x1 <= x0 || y1 <= y0) {
        return (RhiRect){0, 0, w, h};
    }
    return (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

/* rd__pass_area_rect with why (RD_AREA_WHY_*) set when it is false */
static bool areaRect(uint32_t cid, const RdTargetRec *c, uint32_t k, RhiRect *area, uint32_t *end,
                     int *why)
{
    RhiRect u;
    *why = RD_AREA_WHY_SCAN;
    if (!c || !areaOf(cid, c, k, &u, end, why)) {
        return false;
    }
    const RhiRect a = tileAlign(u, c->tw, c->th);
    if (a.w + RD_PASS_AREA_TILE > c->tw && a.h + RD_PASS_AREA_TILE > c->th) {
        *why = RD_AREA_WHY_LARGE;
        return false; /* not a tile column or row narrower: nothing to spare */
    }
    *area = a;
    return true;
}

bool rd__pass_area_rect(uint32_t cid, const RdTargetRec *c, uint32_t k, RhiRect *area,
                        uint32_t *end)
{
    int why;
    return areaRect(cid, c, k, area, end, &why);
}

int rd__pass_area_why(uint32_t cid, const RdTargetRec *c, uint32_t k)
{
    RhiRect a;
    int why;
    return areaRect(cid, c, k, &a, NULL, &why) ? -1 : why;
}

/* a pass on a target with its own depth begun whole, by why */
static void keptWhole(int why)
{
    if (why >= 0 && why < RD_AREA_WHY_COUNT) {
        if (g_rdPerf.passAreaWhy[why] < UINT16_MAX) {
            g_rdPerf.passAreaWhy[why]++;
        }
        s_pa.whyRun[why]++;
    }
}

static uint32_t tilesOf(uint32_t w, uint32_t h)
{
    return ((w + RD_PASS_AREA_TILE - 1u) / RD_PASS_AREA_TILE) *
           ((h + RD_PASS_AREA_TILE - 1u) / RD_PASS_AREA_TILE);
}

static const char *cmdWord(uint8_t type)
{
    return type == RDC_POST_STUB ? "the aura's sprite" : "flat sprites and shapes";
}

void rd__pass_area_begin(RhiRenderPassDesc *p, const RdTargetRec *c, uint32_t cid, uint32_t serial,
                         bool ms, bool storage)
{
    if (s_pa.ended && cid == s_pa.endFor && p->depth.texture.id == s_pa.endDepth && !storage) {
        /* the pass the walk would have kept on: ended at its area's end,
         * begun again (rd__pass_area_due) */
        g_rdPerf.passAreaEnds++;
        s_pa.ends++;
    }
    s_pa.open = s_pa.ended = false;
    p->area = (RhiRect){0, 0, 0, 0};
    if (!s_pa.on || !c || !c->withDepth || !c->depth.id) {
        return; /* off, or a target without its own depth: whole, not counted */
    }
    /* why the pass is whole, the first that holds (RD_AREA_WHY_*: the 10 s
     * line's "kept whole" clause, the tool's --stats) */
    if (!s_pa.valid) {
        keptWhole(RD_AREA_WHY_SCAN);
        return;
    }
    if (ms) {
        keptWhole(RD_AREA_WHY_MSAA);
        return;
    }
    if (storage || p->colorCount == 0) {
        keptWhole(RD_AREA_WHY_STORAGE);
        return;
    }
    if (p->color[0].load == RHI_LOAD_CLEAR ||
        (p->depth.texture.id &&
         (p->depth.depthLoad == RHI_LOAD_CLEAR || p->depth.stencilLoad == RHI_LOAD_CLEAR))) {
        keptWhole(RD_AREA_WHY_CLEAR);
        return;
    }
    const uint32_t k = g_rdStoreOrd;
    RhiRect a;
    uint32_t end = 0;
    int why = RD_AREA_WHY_SCAN;
    if (!areaRect(cid, c, k, &a, &end, &why)) {
        keptWhole(why);
        return;
    }
    p->area = a;
    s_pa.open = true;
    s_pa.openSerial = serial;
    s_pa.openEnd = end;
    s_pa.openFor = cid;
    s_pa.openDepth = p->depth.texture.id;
    const uint32_t tiles = tilesOf(a.w, a.h), all = tilesOf(c->tw, c->th);
    g_rdPerf.passAreas++;
    g_rdPerf.passAreaTiles += tiles;
    g_rdPerf.passAreaTargetTiles += all;
    s_pa.passes++;
    s_pa.tiles += tiles;
    s_pa.targetTiles += all;
    rd__perf_pass_area(&a);
    /* the first 8 kinds a run (target, list, the first command's type) */
    const uint32_t list = (uint32_t)rd__replay_list(), type = s_pa.cmd[k].type;
    for (uint32_t i = 0; i < s_pa.nSaid; i++) {
        if (s_pa.said[i][0] == cid && s_pa.said[i][1] == list && s_pa.said[i][2] == type) {
            return;
        }
    }
    if (s_pa.nSaid < 8u) {
        s_pa.said[s_pa.nSaid][0] = cid;
        s_pa.said[s_pa.nSaid][1] = list;
        s_pa.said[s_pa.nSaid][2] = type;
        s_pa.nSaid++;
        rd__log("pass area: %s in list %d (%s): %ux%u at %d,%d of %ux%u (%u of %u tiles loaded "
                "and stored; the first 8 such passes a run are named%s)",
                rd__target_name(cid), (int)list, cmdWord((uint8_t)type), a.w, a.h, a.x, a.y, c->tw,
                c->th, tiles, all, s_pa.nSaid == 8u ? ", this is the 8th" : "");
    }
}

bool rd__pass_area_due(uint32_t ord, uint32_t serial)
{
    if (!s_pa.open || s_pa.openSerial != serial || ord < s_pa.openEnd) {
        return false;
    }
    s_pa.open = false;
    /* a draw on another target or an action outside the pass (a copy, a
     * clear) ends it there anyway; one pass more only when the next pass
     * begun is on the same targets (an empty draw elsewhere between) */
    const PaCmd *e = ord < s_pa.n ? &s_pa.cmd[ord] : NULL;
    s_pa.ended = e && e->kind != PA_BREAK;
    s_pa.endFor = s_pa.openFor;
    s_pa.endDepth = s_pa.openDepth;
    return true;
}
