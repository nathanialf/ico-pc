/* rd_state_pass_area_test.inc: rd_state_test.c's "pass area" case, the
 * boxes and the run of a pass's render area (port/render/rd_pass_area.c),
 * on the record-only renderer (no device: SCENE's record is given its
 * scale, crop and ids by hand, and put back after).
 *
 * One frame in list 8 on SCENE: a screen sprite S1 at GS (100,100)-(140,160)
 * from the frame's corner, the aura's sprite (RD_POST_AURA) over the rect
 * (300,200)-(400,264), a sprite on FEED128 (no depth: another target, the
 * end of SCENE's pass), a sprite S2 at (20,300)-(60,340) and a VU grid on
 * SCENE.  At three SCENE records:
 *   1x        512 x 448, scale 1;
 *   phone     848 x 448 GS (the phone's Resolution Auto frame, cropped by
 *             the present at 16:9) at 2364 x 1080 texels, 2.788 x 2.411;
 *   full      Full pixel: 1024 x 448 GS at 2.411 on both axes, 2364 texels
 *             wide, the frame's columns from ox = 52.45 texels in;
 * every texel the sprite's GS pixels cover (the GS pixel of texel px is
 * floor((px + ox) / sx)) and every texel fx_sprite_ps keeps for the aura's
 * rect (its GS pixel inside the rect) lies in the command's box
 * (rd__pass_area_cmd, kind 1), and the box reaches at most the pad (two GS
 * pixels past the edges, the far edge's own GS pixel counted) and a texel
 * past them; the FEED128 sprite and
 * the grid are the whole target (kind 2), a clear ends any pass (kind 3),
 * S1 under DATE is the whole target.  After rd__pass_area_scan: the pass
 * begun at S1 holds S1's box and the aura's (rd__pass_area_of, the union)
 * and ends before the FEED128 sprite; its area (rd__pass_area_rect) is that
 * union out to whole 32 x 32 tiles; a pass begun at S2 has none (the grid
 * follows it on SCENE) and neither has one begun at the grid; why
 * (rd__pass_area_why): none at S1, a 3D draw at S2 and at the grid.
 *
 * The phone's frame (testPassAreaPaste, the F12 dumps of 2026-11-02's
 * list 8 and list 11 as the game records them): the aura's paste of
 * FEED128 onto SCENE is the frame's rect (staticBlur.c pasteFeedBackAreaToFB
 * and addWork1ToFB draw `rect`, the frame from -w/2 to w/2: in the rc4 dump
 * list 8 command 55, rect (25984,29184)-(39552,36352) at 848 x 448, its UV
 * FEED128's 212 x 112 texels), then FEED128's next sprite; list 11 a
 * full-frame sprite (command 26: (25984,29152)-(39552,36384)).  Both are
 * boxes (kind 1) holding the whole target, and a pass begun at either gets
 * no area because its draws cover the picture (RD_AREA_WHY_LARGE): the
 * cause of rc4's "0.0 passes with an area".  The reasons' words
 * (rd_perf_why_text) for one replay and a mean over two.  The switch's
 * words (rd__pass_area_pick) as rd__aura_box_pick's. */

typedef struct PaSeen {
    const RdFrame *f;
    uint32_t base[RD_LIST_COUNT];
    int n;
    uint32_t ord[8];
    RdCmd cmd[8];
    RdStateBlock st[8];
} PaSeen;

static void onPaCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    PaSeen *w = user;
    if (list != 8 || w->n >= 8 ||
        (c->type != RDC_SCREEN && c->type != RDC_POST_STUB && c->type != RDC_GRID)) {
        return;
    }
    w->ord[w->n] = w->base[list] + index;
    w->cmd[w->n] = *c;
    w->st[w->n] = *s;
    w->n++;
}

static RdScreenVtx paVtx(uint32_t gsW, uint32_t gsH, int x, int y)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - (int)gsW / 2 + x) * 16;
    v.y = (2048 - (int)gsH / 2 + y) * 16;
    v.z = 0x1000u;
    v.q = 1.0f;
    v.rgba[0] = v.rgba[1] = v.rgba[2] = v.rgba[3] = 0x80;
    return v;
}

/* the texels of GS pixels g0 .. g1 - 1 on one axis (scale s, crop o): the
 * first and the last texel px whose GS pixel is inside, as fx_sprite_ps
 * maps it (floor((px + o) / s)) or as the rasteriser samples a sprite's
 * edges at the texel's centre (floor((px + 0.5 + o) / s)); false when
 * none */
static bool paTexels(int g0, int g1, float s, float o, uint32_t size, int *lo, int *hi)
{
    *lo = INT32_MAX;
    *hi = INT32_MIN;
    for (int px = 0; px < (int)size; px++) {
        const int g = (int)floorf(((float)px + o) / s);
        const int gc = (int)floorf(((float)px + 0.5f + o) / s);
        if ((g >= g0 && g < g1) || (gc >= g0 && gc < g1)) {
            *lo = px < *lo ? px : *lo;
            *hi = px > *hi ? px : *hi;
        }
    }
    return *lo <= *hi;
}

/* box holds texels lo..hi on both axes and reaches at most pad GS pixels
 * and a texel past GS pixels g0..g1 - 1 */
static void paCheckBox(const char *what, const char *at, RhiRect box, int gx0, int gx1, int gy0,
                       int gy1, const RdTargetRec *t, int pad)
{
    int lx, hx, ly, hy;
    const bool any = paTexels(gx0, gx1, t->sx, t->ox, t->tw, &lx, &hx) &&
                     paTexels(gy0, gy1, t->sy, 0.0f, t->th, &ly, &hy);
    CHECK(any, "pass area %s %s: the GS pixels cover texels", at, what);
    if (!any) {
        return;
    }
    CHECK(box.w && box.h && box.x <= lx && box.y <= ly && box.x + (int)box.w - 1 >= hx &&
              box.y + (int)box.h - 1 >= hy,
          "pass area %s %s: box (%d,%d) %ux%u misses the covered texels (%d..%d, %d..%d)", at, what,
          box.x, box.y, box.w, box.h, lx, hx, ly, hy);
    const int mx0 = (int)floorf((float)(gx0 - pad) * t->sx - t->ox) - 1;
    /* the box's last GS pixel is g1 + pad (the vertex or rect edge, 12.4,
     * rounded up, then padded): its texels end at (g1 + pad + 1) * s */
    const int mx1 = (int)ceilf((float)(gx1 + pad + 1) * t->sx - t->ox) + 1;
    const int my0 = (int)floorf((float)(gy0 - pad) * t->sy) - 1;
    const int my1 = (int)ceilf((float)(gy1 + pad + 1) * t->sy) + 1;
    CHECK(box.x >= mx0 && box.y >= my0 && box.x + (int)box.w <= mx1 && box.y + (int)box.h <= my1,
          "pass area %s %s: box (%d,%d) %ux%u past the pad's (%d..%d, %d..%d)", at, what, box.x,
          box.y, box.w, box.h, mx0, mx1, my0, my1);
}

static void paGrid(void)
{
    static float qw[3 * RD_VU_QW_GRID + 4][4];
    RdVuGridDraw d;
    memset(&d, 0, sizeof(d));
    d.qw = (const float (*)[4])qw;
    d.strips = 1;
    d.stripLen = 3;
    d.code = 20;
    rd_draw_vu_grid(&d, 0);
}

static void testPassAreaAt(const char *at, uint32_t gsW, uint32_t gsH, uint32_t tw, uint32_t th,
                           float sx, float sy, float ox)
{
    RdTargetRec *t = rd__target_rec(RD_TARGET_SCENE + 1u);
    CHECK(t != NULL, "pass area %s: SCENE's record", at);
    if (!t) {
        return;
    }
    const RdTargetRec keep = *t;
    t->w = gsW;
    t->h = gsH;
    t->tw = tw;
    t->th = th;
    t->sx = sx;
    t->sy = sy;
    t->ox = ox;
    t->withDepth = 1;
    if (!t->color.id) {
        t->color.id = 0x7F000001u; /* stand-ins: no device, nothing made */
    }
    if (!t->depth.id) {
        t->depth.id = 0x7F000002u;
    }
    const RdTarget scene = rd_target(RD_TARGET_SCENE), feed = rd_target(RD_TARGET_FEED128);
    rd_begin_frame();
    rd_select_list(8);
    rd_set_target(scene, scene, gsW, gsH, 0);
    rd_scissor(0, 0, (int32_t)gsW - 1, (int32_t)gsH - 1);
    rd_texture_off();
    {
        RdScreenVtx q[2] = {paVtx(gsW, gsH, 100, 100), paVtx(gsW, gsH, 140, 160)};
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_WORLD, 0, 0);
    }
    {
        RdPostParams p;
        memset(&p, 0, sizeof(p));
        p.rect[0] = (float)((2048 - (int)gsW / 2 + 300) * 16);
        p.rect[1] = (float)((2048 - (int)gsH / 2 + 200) * 16);
        p.rect[2] = (float)((2048 - (int)gsW / 2 + 400) * 16);
        p.rect[3] = (float)((2048 - (int)gsH / 2 + 264) * 16);
        p.scalar[2] = 1.0f;
        p.exactInt = 1;
        rd_post(RD_POST_AURA, &p);
    }
    rd_set_target(feed, feed, 212, 128, 0); /* FEED128 has no depth */
    rd_scissor(0, 0, 211, 127);
    {
        RdScreenVtx q[2] = {paVtx(212, 128, 0, 0), paVtx(212, 128, 20, 20)};
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_WORLD, 0, 0);
    }
    rd_set_target(scene, scene, gsW, gsH, 0);
    rd_scissor(0, 0, (int32_t)gsW - 1, (int32_t)gsH - 1);
    {
        RdScreenVtx q[2] = {paVtx(gsW, gsH, 20, 300), paVtx(gsW, gsH, 60, 340)};
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_WORLD, 0, 0);
    }
    paGrid();
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    PaSeen w;
    memset(&w, 0, sizeof(w));
    w.f = f;
    for (int l = 1; l < RD_LIST_COUNT; l++) {
        w.base[l] = w.base[l - 1] + f->lists[l - 1].count;
    }
    RdStateBlock st = f->startState;
    rd__walk(f, 0, &st, onPaCmd, &w);
    CHECK(w.n == 5, "pass area %s: %d of list 8's draws walked (5)", at, w.n);
    if (w.n != 5) {
        *t = keep;
        return;
    }
    RhiRect box[5];
    int kind[5];
    for (int i = 0; i < 5; i++) {
        kind[i] = rd__pass_area_cmd(f, &w.cmd[i], &w.st[i], w.ord[i], &box[i]);
    }
    CHECK(kind[0] == 1 && kind[1] == 1 && kind[2] == 2 && kind[3] == 1 && kind[4] == 2,
          "pass area %s: kinds %d %d %d %d %d (want 1 1 2 1 2: S1, the aura, FEED128's sprite, "
          "S2, the grid)",
          at, kind[0], kind[1], kind[2], kind[3], kind[4]);
    paCheckBox("S1", at, box[0], 100, 140, 100, 160, t, 2);
    paCheckBox("the aura's rect", at, box[1], 300, 400, 200, 264, t, 2);
    paCheckBox("S2", at, box[3], 20, 60, 300, 340, t, 2);
    {
        /* S1 under DATE, and a clear */
        RdStateBlock d = w.st[0];
        d.ds.test.date = (uint8_t)(RD_DATE_OFF + 1);
        RhiRect b;
        CHECK(rd__pass_area_cmd(f, &w.cmd[0], &d, w.ord[0], &b) == 2,
              "pass area %s: S1 under DATE is the whole target", at);
        RdCmd clr;
        memset(&clr, 0, sizeof(clr));
        clr.type = RDC_CLEAR;
        CHECK(rd__pass_area_cmd(f, &clr, &w.st[0], w.ord[0], &b) == 3,
              "pass area %s: a clear ends a pass's commands", at);
    }
    /* the run: the pass begun at S1 holds S1 and the aura, ends before the
     * FEED128 sprite */
    rd__pass_area_force(1);
    rd__pass_area_scan(f, 0);
    RhiRect u, a;
    uint32_t end = 0;
    const bool got = rd__pass_area_of(RD_TARGET_SCENE + 1u, t, w.ord[0], &u, &end);
    const int ux0 = box[0].x < box[1].x ? box[0].x : box[1].x;
    const int uy0 = box[0].y < box[1].y ? box[0].y : box[1].y;
    const int ux1 = box[0].x + (int)box[0].w > box[1].x + (int)box[1].w ? box[0].x + (int)box[0].w
                                                                        : box[1].x + (int)box[1].w;
    const int uy1 = box[0].y + (int)box[0].h > box[1].y + (int)box[1].h ? box[0].y + (int)box[0].h
                                                                        : box[1].y + (int)box[1].h;
    CHECK(got && u.x == ux0 && u.y == uy0 && u.x + (int)u.w == ux1 && u.y + (int)u.h == uy1 &&
              end == w.ord[2],
          "pass area %s: the pass at S1 holds (%d,%d) %ux%u ending before command %u; want the "
          "union (%d,%d)-(%d,%d) ending before %u",
          at, u.x, u.y, u.w, u.h, end, ux0, uy0, ux1, uy1, w.ord[2]);
    if (got && rd__pass_area_rect(RD_TARGET_SCENE + 1u, t, w.ord[0], &a, NULL)) {
        const int T = (int)RD_PASS_AREA_TILE;
        const int ax1 = (ux1 + T - 1) / T * T, ay1 = (uy1 + T - 1) / T * T;
        CHECK(a.x == ux0 / T * T && a.y == uy0 / T * T &&
                  a.x + (int)a.w == (ax1 < (int)tw ? ax1 : (int)tw) &&
                  a.y + (int)a.h == (ay1 < (int)th ? ay1 : (int)th),
              "pass area %s: the area (%d,%d) %ux%u is not the union out to whole tiles", at, a.x,
              a.y, a.w, a.h);
        printf("pass area %s: S1 (%d,%d) %ux%u, the aura (%d,%d) %ux%u: area (%d,%d) %ux%u of "
               "%ux%u\n",
               at, box[0].x, box[0].y, box[0].w, box[0].h, box[1].x, box[1].y, box[1].w, box[1].h,
               a.x, a.y, a.w, a.h, tw, th);
    } else {
        CHECK(false, "pass area %s: no tile-aligned area for the pass at S1", at);
    }
    CHECK(!rd__pass_area_of(RD_TARGET_SCENE + 1u, t, w.ord[3], &u, NULL),
          "pass area %s: a pass begun at S2, the grid after it, has an area", at);
    CHECK(!rd__pass_area_of(RD_TARGET_SCENE + 1u, t, w.ord[4], &u, NULL),
          "pass area %s: a pass begun at the grid has an area", at);
    CHECK(rd__pass_area_why(RD_TARGET_SCENE + 1u, t, w.ord[0]) == -1 &&
              rd__pass_area_why(RD_TARGET_SCENE + 1u, t, w.ord[3]) == RD_AREA_WHY_VU &&
              rd__pass_area_why(RD_TARGET_SCENE + 1u, t, w.ord[4]) == RD_AREA_WHY_VU,
          "pass area %s: why %d at S1 (-1), %d at S2 and %d at the grid (%d, a 3D draw)", at,
          rd__pass_area_why(RD_TARGET_SCENE + 1u, t, w.ord[0]),
          rd__pass_area_why(RD_TARGET_SCENE + 1u, t, w.ord[3]),
          rd__pass_area_why(RD_TARGET_SCENE + 1u, t, w.ord[4]), RD_AREA_WHY_VU);
    rd__pass_area_force(-1);
    rd__pass_area_scan(NULL, 0); /* nothing held */
    *t = keep;
}

/* the phone's list 8 paste and list 11 fade, full-frame (see the top) */
static void testPassAreaPaste(const char *at, uint32_t gsW, uint32_t gsH, uint32_t tw, uint32_t th,
                              float sx, float sy, float ox)
{
    RdTargetRec *t = rd__target_rec(RD_TARGET_SCENE + 1u);
    CHECK(t != NULL, "pass area paste %s: SCENE's record", at);
    if (!t) {
        return;
    }
    const RdTargetRec keep = *t;
    t->w = gsW;
    t->h = gsH;
    t->tw = tw;
    t->th = th;
    t->sx = sx;
    t->sy = sy;
    t->ox = ox;
    t->withDepth = 1;
    if (!t->color.id) {
        t->color.id = 0x7F000001u;
    }
    if (!t->depth.id) {
        t->depth.id = 0x7F000002u;
    }
    const RdTarget scene = rd_target(RD_TARGET_SCENE), feed = rd_target(RD_TARGET_FEED128);
    const int ox16 = (2048 - (int)gsW / 2) * 16, oy16 = (2048 - (int)gsH / 2) * 16;
    rd_begin_frame();
    rd_select_list(8);
    rd_set_target(scene, scene, gsW, gsH, 0);
    rd_scissor(0, 0, (int32_t)gsW - 1, (int32_t)gsH - 1);
    {
        /* pasteFeedBackAreaToFB: FEED128 (212 x 112 texels at 848 GS wide)
         * over the frame's rect, RGBA 64,64,64,128 */
        RdPostParams p;
        memset(&p, 0, sizeof(p));
        p.rect[0] = (float)ox16;
        p.rect[1] = (float)oy16;
        p.rect[2] = (float)(ox16 + (int)gsW * 16);
        p.rect[3] = (float)(oy16 + (int)gsH * 16);
        p.uv[0] = 8.0f;
        p.uv[1] = 8.0f;
        p.uv[2] = (float)(gsW / 4u * 16u + 8u);
        p.uv[3] = 1800.0f;
        p.rgba[0] = p.rgba[1] = p.rgba[2] = 64;
        p.rgba[3] = 128;
        p.scalar[2] = 1.0f;
        p.exactInt = 1;
        rd_post(RD_POST_AURA, &p);
    }
    rd_set_target(feed, feed, 212, 128, 0); /* the next sprite: FEED128 */
    rd_scissor(0, 0, 211, 127);
    {
        RdScreenVtx q[2] = {paVtx(212, 128, 0, 0), paVtx(212, 128, 20, 20)};
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_WORLD, 0, 0);
    }
    rd_select_list(11);
    rd_set_target(scene, scene, gsW, gsH, 0);
    rd_scissor(0, 0, (int32_t)gsW - 1, (int32_t)gsH - 1);
    {
        /* list 11's fade: the frame, two rows past it on each side */
        RdScreenVtx q[2] = {paVtx(gsW, gsH, 0, -2), paVtx(gsW, gsH, (int)gsW, (int)gsH + 2)};
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_FULLFRAME, 0, 0);
    }
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    /* the paste, FEED128's sprite and the fade: lists 8 and 11's draws */
    uint32_t ord[3], n = 0, base = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const uint8_t ty = f->lists[l].cmds[i].type;
            if ((l == 8 || l == 11) && (ty == RDC_POST_STUB || ty == RDC_SCREEN) && n < 3) {
                ord[n++] = base + i;
            }
        }
        base += f->lists[l].count;
    }
    CHECK(n == 3, "pass area paste %s: %u draws in lists 8 and 11 (3)", at, n);
    if (n == 3) {
        rd__pass_area_force(1);
        rd__pass_area_scan(f, 0);
        RhiRect u;
        uint32_t end = 0;
        const bool got = rd__pass_area_of(RD_TARGET_SCENE + 1u, t, ord[0], &u, &end);
        CHECK(got && u.x <= 0 && u.y <= 0 && u.x + (int)u.w >= (int)tw &&
                  u.y + (int)u.h >= (int)th && end == ord[1],
              "pass area paste %s: the paste's box (%d,%d) %ux%u, ending before %u: want the "
              "whole %ux%u, ending before %u",
              at, u.x, u.y, u.w, u.h, end, tw, th, ord[1]);
        const int whyPaste = rd__pass_area_why(RD_TARGET_SCENE + 1u, t, ord[0]);
        const int whyFade = rd__pass_area_why(RD_TARGET_SCENE + 1u, t, ord[2]);
        CHECK(whyPaste == RD_AREA_WHY_LARGE && whyFade == RD_AREA_WHY_LARGE,
              "pass area paste %s: why %d at the paste, %d at the fade (%d: its draws cover the "
              "picture)",
              at, whyPaste, whyFade, RD_AREA_WHY_LARGE);
        printf("pass area paste %s: the paste's box (%d,%d) %ux%u of %ux%u: no area (why %d)\n", at,
               u.x, u.y, u.w, u.h, tw, th, whyPaste);
        rd__pass_area_force(-1);
        rd__pass_area_scan(NULL, 0);
    }
    *t = keep;
}

/* rd_perf_why_text's words */
static void testPassAreaWords(void)
{
    uint64_t area[RD_AREA_WHY_COUNT], last[RD_LAST_WHY_COUNT];
    memset(area, 0, sizeof(area));
    memset(last, 0, sizeof(last));
    char text[512];
    rd_perf_why_text(text, sizeof text, 0, area, 1.0);
    CHECK(text[0] == '\0', "pass area words: none kept whole gives '%s', want nothing", text);
    area[RD_AREA_WHY_VU] = 2;
    area[RD_AREA_WHY_LARGE] = 1;
    rd_perf_why_text(text, sizeof text, 0, area, 1.0);
    CHECK(strcmp(text, "3 passes kept whole (2 a 3D draw, 1 its draws cover the picture)") == 0,
          "pass area words, one replay: '%s'", text);
    rd_perf_why_text(text, sizeof text, 0, area, 2.0);
    CHECK(strcmp(text, "1.5 passes kept whole a replay (1.0 a 3D draw, 0.5 its draws cover the "
                       "picture)") == 0,
          "pass area words, two replays: '%s'", text);
    last[RD_LAST_WHY_DATE] = 1;
    last[RD_LAST_WHY_POST] = 3;
    rd_perf_why_text(text, sizeof text, 1, last, 2.0);
    CHECK(strcmp(text, "2.0 last stores kept a replay (0.5 a DATE draw, 1.5 a post sprite)") == 0,
          "last store words, two replays: '%s'", text);
    char small[24];
    rd_perf_why_text(small, sizeof small, 1, last, 2.0);
    CHECK(strlen(small) < sizeof small, "last store words cut to the buffer: '%s'", small);
}

static void testPassArea(void)
{
    CHECK(rd__pass_area_pick(NULL, 1) == 1 && rd__pass_area_pick("", 0) == 0 &&
              rd__pass_area_pick("auto", 1) == 1 && rd__pass_area_pick("on", 0) == 1 &&
              rd__pass_area_pick("1", 0) == 1 && rd__pass_area_pick("off", 1) == 0 &&
              rd__pass_area_pick("false", 1) == 0 && rd__pass_area_pick("maybe", 1) == 1 &&
              rd__pass_area_pick("maybe", 0) == 0,
          "pass area: the switch's words");
    testPassAreaAt("1x", 512, 448, 512, 448, 1.0f, 1.0f, 0.0f);
    testPassAreaAt("phone", 848, 448, 2364, 1080, 2364.0f / 848.0f, 1080.0f / 448.0f, 0.0f);
    testPassAreaAt("full", 1024, 448, 2364, 1080, 1080.0f / 448.0f, 1080.0f / 448.0f,
                   (1024.0f * (1080.0f / 448.0f) - 2364.0f) * 0.5f);
    testPassAreaPaste("1x", 512, 448, 512, 448, 1.0f, 1.0f, 0.0f);
    testPassAreaPaste("phone", 848, 448, 2364, 1080, 2364.0f / 848.0f, 1080.0f / 448.0f, 0.0f);
    testPassAreaPaste("full", 1024, 448, 2364, 1080, 1080.0f / 448.0f, 1080.0f / 448.0f,
                      (1024.0f * (1080.0f / 448.0f) - 2364.0f) * 0.5f);
    testPassAreaWords();
}
