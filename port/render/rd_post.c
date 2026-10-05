/* rd_post.c: rd_Post.
 *
 * The post kinds that are plain GS sprites in the original are recorded as
 * the register writes and sprites the original routine issues, through the
 * same rd_* calls the seki layer uses.  They therefore leak state into the
 * rest of the list exactly as the original packets do (the fade leaves
 * ALPHA 0x44 and PABE 0 behind, the keep pass leaves TEXA 80/80, ...), and
 * they replay through the one screen-prim path with the GS sampling rules.
 * Geometry is taken from ico2/seki/src/GsBase.c:
 *
 *   REDUCTION   gsb_Reduction (:213): clear DISPLAY black, then SCENE drawn
 *               into it at half height, bilinear (TEX1 0x60), tinted, inside
 *               the border-crop scissor (2 px left/right; 8 lines top/bottom
 *               for a 512-line scene, 2 for 448)
 *   KEEP        gsb_KeepFrameBuffer (:360): DISPLAY read as PSMCT24 (TEXA
 *               80/80) drawn back over the whole scene at 112 grey
 *   FADE        gsb_fade (:391): full-scene sprite, LERP_AS, fade colour
 *   LETTERBOX   gsb_scissorOnDemo (:479): two 58-line black bars, LERP_FIX
 *               with FIX = the band level, Z write on
 *   BRIGHTNESS  gsb_controlBrightness (:534): white sprite, alpha = step,
 *               gif_SetAlpha(1, 7, 0)
 *
 * COMPOSITE_FIX records ALPHA (params->blend, params->fix), TEX0 (src) and
 * one sprite covering params->rect (x, y, w, h in GS pixels of dst; zero =
 * all of dst) from params->uv (x, y, w, h in texels of src; zero = all of
 * src); TEST, ZBUF and the filter are whatever the list has in force, as
 * for the game's own sprite.  With params->exactInt it records the ALPHA
 * write and an RDC_EXACT_BLEND instead: the replayer runs the GS integer
 * blend through blend_int on RGBA8_UINT copies of both targets and copies
 * the result back (rd_replay.c), which requires src and dst of equal size.
 *
 * COPY is gif_MoveImage: params->uv (x, y) in src to params->rect (x, y,
 * w, h) in dst, a texture copy.
 *
 * The remaining kinds (anti-alias, fog, shadow resolve, blur, film noise)
 * are recorded with their parameters and stop at replay (waves 2-5).
 */
#include <string.h>
#include "rd_internal.h"

static void sprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t z,
                   const uint8_t rgba[4], int32_t u0, int32_t v0, int32_t u1, int32_t v1)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = x0;
    v[0].y = y0;
    v[1].x = x1;
    v[1].y = y1;
    v[0].z = v[1].z = z;
    v[0].s = (float)u0;
    v[0].t = (float)v0;
    v[1].s = (float)u1;
    v[1].t = (float)v1;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, rgba, 4);
    memcpy(v[1].rgba, rgba, 4);
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_FULLSCREEN, 1, 0);
}

static RdTarget orDefault(RdTarget t, RdTargetId d)
{
    return t.id ? t : rd_Target(d);
}

static void targetSize(RdTarget t, uint32_t *w, uint32_t *h)
{
    RdTargetRec *r = rd__TargetRec(t.id);
    *w = r ? r->w : 1;
    *h = r ? r->h : 1;
}

/* GS window coordinate (12.4) of the target's pixel x for a centred
 * XYOFFSET of a w-wide target: 0x8000 is 2048.0. */
static int32_t win(int32_t px16, uint32_t w)
{
    return 0x8000 - (int32_t)(w / 2) * 16 + px16;
}

static void postReduction(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    const int32_t crop = p->lines ? (int32_t)p->lines : (H >= 512 ? 8 : 2);
    RdTarget src = orDefault(p->src, RD_TARGET_SCENE);
    RdTarget dst = orDefault(p->dst, RD_TARGET_DISPLAY);
    static const uint8_t kBlack[4] = {0, 0, 0, 0};
    const uint8_t tint[4] = {p->rgba[0], p->rgba[1], p->rgba[2], 0x80};
    /* FBA 0, ALPHA 0x8000000048, FRAME FBP 0, XYOFFSET centred on W x H/2,
     * SCISSOR W x H/2, TEST 0x30000, ZBUF mask on */
    rd_SetTarget(dst, (RdTarget){0}, (uint32_t)W, (uint32_t)(H / 2), 0);
    rd_FBA(0);
    rd_Blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 0); /* PRIM 0x106 / 0x116: no ABE */
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_TextureOff();
    /* corners at -0.25 px: covers pixels 0..W-1, 0..H/2-1 */
    const int32_t x0 = -W / 2 * 16 + 0x8000 - 4, y0 = -H / 4 * 16 + 0x8000 - 4;
    const int32_t x1 = x0 + W * 16, y1 = y0 + H / 2 * 16;
    sprite(x0, y0, x1, y1, 0xFFFFFFFFu, kBlack, 0, 0, 0, 0);
    rd__RecScissor(2, crop, W - 3, H / 2 - 1 - crop);
    rd_Texture(rd_TargetTexture(src, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd__RecFilter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    /* UV 0.5 .. W+0.5, 0.5 .. H+0.5 */
    sprite(x0, y0, x1, y1, 0xFFFFFFFFu, tint, 8, 8, W * 16 + 8, H * 16 + 8);
    rd__RecScissor(0, 0, W, H);
}

static void postKeep(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    RdTarget src = orDefault(p->src, RD_TARGET_DISPLAY);
    static const uint8_t kKeep[4] = {112, 112, 112, 128};
    const uint8_t *col = (p->rgba[0] | p->rgba[1] | p->rgba[2] | p->rgba[3]) ? p->rgba : kKeep;
    if (p->dst.id) {
        rd_SetTarget(p->dst, p->dst.id == RD_TARGET_SCENE + 1 ? p->dst : (RdTarget){0}, (uint32_t)W,
                     (uint32_t)H, 1);
    }
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_FBA(0);
    rd_TexA(RD_TEXA_80_80);
    /* TEX0: TBP 0, PSMCT24, TCC 1, MODULATE */
    rd_Texture(rd_TargetTexture(src, RD_VIEW_RGB24_TA0), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd__RecABE(0); /* PRIM 0x116 */
    /* r0 = {-(W/2)*16 - 12, -(H/2)*16 - 12, W*16 + 32, H*16 + 32},
     * r1 = {8, 8, W*16, H/2*16} */
    const int32_t x0 = -(W / 2) * 16 - 12 + 0x8000, y0 = -(H / 2) * 16 - 12 + 0x8000;
    sprite(x0, y0, x0 + W * 16 + 32, y0 + H * 16 + 32, 0, col, 8, 8, 8 + W * 16, 8 + H / 2 * 16);
}

static void postFade(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    RdTarget dst = orDefault(p->dst, RD_TARGET_SCENE);
    /* gif_SetDrawEnviroment(0x800, 0, W, H, 1, 0) */
    rd_SetTarget(dst, dst.id == RD_TARGET_SCENE + 1 ? dst : (RdTarget){0}, (uint32_t)W, (uint32_t)H,
                 1);
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_PABE(0);
    rd_Blend(RD_BLEND_LERP_AS, 0, 1); /* ALPHA 0x44 (FIX 0); PRIM 0x446 sets ABE */
    rd_TextureOff();
    const int32_t x0 = -(W / 2) * 16 + 0x8000, y0 = -(H / 2) * 16 + 0x8000;
    sprite(x0, y0, x0 + W * 16, y0 + H * 16, 0xFFFFFFFFu, p->rgba, 0, 0, 0, 0);
}

static void postLetterbox(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    const int32_t lines = p->lines ? (int32_t)p->lines : 58;
    RdTarget dst = orDefault(p->dst, RD_TARGET_SCENE);
    static const uint8_t kBar[4] = {0, 0, 0, 0x80};
    rd_SetTarget(dst, dst.id == RD_TARGET_SCENE + 1 ? dst : (RdTarget){0}, (uint32_t)W, (uint32_t)H,
                 1);
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(1); /* ZBUF 0x300000C0 */
    rd_PABE(0);
    rd_Blend(RD_BLEND_LERP_FIX, p->fix, 1);
    rd_TextureOff();
    const int32_t x0 = -(W / 2) * 16 + 0x8000;
    const int32_t yTop = -(H / 2) * 16 - 4 + 0x8000;
    const int32_t yBot = ((H / 2) - lines) * 16 + 4 + 0x8000;
    sprite(x0, yTop, x0 + W * 16, yTop + lines * 16, 0xFFFFFFFFu, kBar, 0, 0, 0, 0);
    sprite(x0, yBot, x0 + W * 16, yBot + lines * 16, 0xFFFFFFFFu, kBar, 0, 0, 0, 0);
}

static void postBrightness(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    const uint8_t col[4] = {0xFF, 0xFF, 0xFF, p->rgba[3]};
    if (p->dst.id) {
        rd_SetTarget(p->dst, p->dst.id == RD_TARGET_SCENE + 1 ? p->dst : (RdTarget){0}, (uint32_t)W,
                     (uint32_t)H, 1);
    }
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_Blend(RD_BLEND_LERP_AS_ALT, 0, 1);
    rd_TextureOff();
    /* gif_MakeSpriteNoTexture((0x800 - W/2) << 4, (0x800 - H/2) << 4, W << 4, H << 4,
     * 0xFFFFFFFE, ...): absolute window coordinates */
    const int32_t x0 = (0x800 - W / 2) << 4, y0 = (0x800 - H / 2) << 4;
    sprite(x0, y0, x0 + (W << 4), y0 + (H << 4), 0xFFFFFFFEu, col, 0, 0, 0, 0);
}

static void postComposite(const RdPostParams *p)
{
    if (!p->src.id || !p->dst.id) {
        rd__Log("RD_POST_COMPOSITE_FIX needs src and dst");
        return;
    }
    uint32_t dw, dh, sw, sh;
    targetSize(p->dst, &dw, &dh);
    targetSize(p->src, &sw, &sh);
    RdTarget depth = p->dst.id == RD_TARGET_SCENE + 1 ? p->dst : (RdTarget){0};
    rd_SetTarget(p->dst, depth, dw, dh, 0);
    rd_Blend((RdBlend)p->blend, p->fix, 1);
    if (p->exactInt) {
        RdCmd *c = rd__Push(RDC_EXACT_BLEND);
        if (c) {
            c->u[0] = p->src.id;
            c->u[1] = p->dst.id;
        }
        return;
    }
    float r[4] = {p->rect[0], p->rect[1], p->rect[2], p->rect[3]};
    float uv[4] = {p->uv[0], p->uv[1], p->uv[2], p->uv[3]};
    if (r[2] <= 0.0f || r[3] <= 0.0f) {
        r[0] = r[1] = 0.0f;
        r[2] = (float)dw;
        r[3] = (float)dh;
    }
    if (uv[2] <= 0.0f || uv[3] <= 0.0f) {
        uv[0] = uv[1] = 0.0f;
        uv[2] = (float)sw;
        uv[3] = (float)sh;
    }
    static const uint8_t kGrey[4] = {0x80, 0x80, 0x80, 0x80};
    const uint8_t *col = (p->rgba[0] | p->rgba[1] | p->rgba[2] | p->rgba[3]) ? p->rgba : kGrey;
    rd_Texture(rd_TargetTexture(p->src, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    const int32_t x0 = win((int32_t)(r[0] * 16.0f), dw), y0 = win((int32_t)(r[1] * 16.0f), dh);
    sprite(x0, y0, x0 + (int32_t)(r[2] * 16.0f), y0 + (int32_t)(r[3] * 16.0f), 0xFFFFFFFFu, col,
           (int32_t)(uv[0] * 16.0f), (int32_t)(uv[1] * 16.0f), (int32_t)((uv[0] + uv[2]) * 16.0f),
           (int32_t)((uv[1] + uv[3]) * 16.0f));
}

static void postCopy(const RdPostParams *p)
{
    RdFrame *f = rd__RecFrame();
    if (!f || !p->src.id || !p->dst.id) {
        return;
    }
    uint32_t sw, sh;
    targetSize(p->src, &sw, &sh);
    RdCopyRec r;
    r.srcX = (int32_t)p->uv[0];
    r.srcY = (int32_t)p->uv[1];
    r.dstX = (int32_t)p->rect[0];
    r.dstY = (int32_t)p->rect[1];
    r.w = p->rect[2] > 0.0f ? (uint32_t)p->rect[2] : sw;
    r.h = p->rect[3] > 0.0f ? (uint32_t)p->rect[3] : sh;
    uint32_t off = rd__FramePayload(f, &r, sizeof(r));
    RdCmd *c = rd__Push(RDC_COPY);
    if (c) {
        c->u[0] = p->src.id;
        c->u[1] = p->dst.id;
        c->u[2] = off;
    }
}

static void postStub(RdPostKind kind, const RdPostParams *p)
{
    RdFrame *f = rd__RecFrame();
    if (!f) {
        rd__Push(RDC_POST_STUB);
        return;
    }
    RdPostRec r;
    memset(&r, 0, sizeof(r));
    r.src = p->src.id;
    r.dst = p->dst.id;
    r.srcView = (uint32_t)p->srcView;
    memcpy(r.rgba, p->rgba, 4);
    r.fix = p->fix;
    r.blend = p->blend;
    r.abe = p->abe;
    r.exactInt = p->exactInt;
    r.z = p->z;
    memcpy(r.rect, p->rect, sizeof(r.rect));
    memcpy(r.uv, p->uv, sizeof(r.uv));
    memcpy(r.scalar, p->scalar, sizeof(r.scalar));
    r.lines = p->lines;
    r.lutOffset = p->lut ? rd__FramePayload(f, p->lut, 256 * 4) : ~0u;
    uint32_t off = rd__FramePayload(f, &r, sizeof(r));
    RdCmd *c = rd__Push(RDC_POST_STUB);
    if (c) {
        c->b[0] = (uint8_t)kind;
        c->u[1] = off;
        c->u[2] = (uint32_t)sizeof(r);
    }
}

void rd_Post(RdPostKind kind, const RdPostParams *params)
{
    RdPostParams zero;
    if (!params) {
        memset(&zero, 0, sizeof(zero));
        params = &zero;
    }
    switch (kind) {
    case RD_POST_REDUCTION:
        postReduction(params);
        break;
    case RD_POST_KEEP:
        postKeep(params);
        break;
    case RD_POST_FADE:
        postFade(params);
        break;
    case RD_POST_LETTERBOX:
        postLetterbox(params);
        break;
    case RD_POST_BRIGHTNESS:
        postBrightness(params);
        break;
    case RD_POST_COMPOSITE_FIX:
        postComposite(params);
        break;
    case RD_POST_COPY:
        postCopy(params);
        break;
    default:
        postStub(kind, params);
        break;
    }
}
