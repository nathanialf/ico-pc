/* rd_post.c: rd_post.
 *
 * The post kinds that are plain GS sprites in the original are recorded as
 * the register writes and sprites the original routine issues, through the
 * same rd_* calls the seki layer uses, in the original's order.  They
 * therefore leak state into the rest of the list exactly as the original
 * packets do (the fade leaves ALPHA 0x44 and PABE 0 behind, the keep pass
 * leaves TEXA 80/80 and DISPLAY bound, the anti-alias pass PABE 0 and the
 * last level's FIX, ...), and they replay through the one screen-prim path
 * with the GS sampling rules.  Registers the original does not write (TEX1,
 * CLAMP, FBA in most passes) are left as they are, so they leak in.  PRIM is
 * recorded as its three state bits (ABE, TME through rd_texture /
 * rd_texture_off, IIP).  Every pass that writes FRAME records FBMSK 0 with
 * it (rd_color_mask), so a mask left by an earlier FRAME write
 * (darkVolume.c's PSMCT24 composite) ends there, as on the GS.  Geometry
 * and register values are taken from ico2/seki/src/GsBase.c:
 *
 *   REDUCTION     gsb_Reduction: clear DISPLAY black, then SCENE drawn into
 *                 it at half height, bilinear (TEX1 0x60), tinted, inside the
 *                 border-crop scissor (2 px left/right; 8 lines top/bottom
 *                 for a 512-line scene, 2 for 448); both sprites are
 *                 RdPostRecs drawn through the GS sprite model (rd_blur.c)
 *   KEEP          gsb_KeepFrameBuffer: DISPLAY read as PSMCT24 (TEXA 80/80)
 *                 drawn back over the whole scene at 112 grey, PRIM 0x116
 *   FADE          gsb_fade: gif_SetDrawEnviroment(0x800, ...), full-scene
 *                 sprite, ALPHA 0x44, PABE 0, PRIM 0x446, the fade colour
 *   LETTERBOX     gsb_scissorOnDemo: two 58-line black bars, ALPHA 0x64 with
 *                 FIX = the band level, Z write on
 *   BRIGHTNESS    gsb_controlBrightness: white sprite, alpha = the step,
 *                 gif_SetAlpha(1, 7, 0), PRIM 0x446, with the corners the GS
 *                 receives (see postBrightness)
 *   AA_DOWNSAMPLE gsb_antiAlias, first half: SCENE (512 x 512) into AA0
 *                 (256 x 256), then with params->lines >= 2 AA0 into AA1
 *                 (128 x 128); TEST 0x30000, Z write off, PABE 1, ALPHA 0x64
 *                 FIX 0x80 (gif_SetAlpha(0, 2, 128)), no ABE
 *   AA_COMPOSITE  gsb_antiAlias, second half: into SCENE (512 x 512, the
 *                 original's literal), AA1 at FIX params->rgba[1] when it is
 *                 not 0, then AA0 at FIX params->rgba[0] when it is not 0,
 *                 each a LERP_FIX sprite with ABE (PABE 0); then Z write on,
 *                 TEST 0x50000 and the SCENE environment at the scene size
 *   FILM_NOISE    gsb_filmNoise: CLAMP 0 (REPEAT), Z write off, TEST
 *                 0x30000, PABE 0, ALPHA 0x44, PRIM 0x56 (sprite, TME, ABE,
 *                 STQ), RGBAQ 128 grey with alpha params->rgba[3] and Q 1,
 *                 ST 0,0 to params->scalar[0] (the grain scale) at window
 *                 0x7000..0x9000; the texture is the one bound (the caller
 *                 lets the decoder bind sandstorm_spr's TEX0 through the
 *                 resolver), or rd_target_texture(params->src) when given
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
 * FOG is fog_DrawFog's textured sprite alone: ZFog.c's host
 * path records the packet's register writes as rd state around it (TEST,
 * ZBUF, ALPHA, FBA, TEX0 as SCENE's depth view, TEX1, PRIM), so they leak
 * as on the GS; this records the sprite and the CLUT (an RdPostRec in an
 * RDC_POST_STUB), and rd_replay.c's doFog draws it through fog_lut_ps with
 * the state in force (rd.h).
 *
 * SHADOW_RESOLVE, BLUR and PRESENT_BLIT are reserved numbers that no
 * caller issues: rd_post records nothing for them (logged once).  Any other
 * unknown kind falls to postStub: recorded, and skipped at replay.
 *
 * MOTION_BLUR, DOF, FLARE, BLOOM, AURA, EYE_BLUR are one
 * staticBlur.c sprite each, recorded by rd_blur.c (rd__post_blur) with the
 * register writes around it recorded by staticBlur.c's host path.
 */
#include <string.h>
#include "rd_internal.h"

/* The RdKey the next post sprites carry, so the presenter
 * can interpolate the fade's level and the letterbox bars between ticks
 * (rd_interp.c); 0 (snap) for every other post sprite (film noise,
 * brightness, keep, reduction, anti-alias). */
static RdKey s_spriteKey;

static const char kPostKeyTag; /* the key's object: a fixed address of this file */

#define POST_KEY(kind, n) RD_KEY(&kPostKeyTag, (kind), (n))

/* A sprite with the vertices exactly as the GS receives them (12.4 window
 * coordinates, Z, UV in 12.4 texels). */
static void spriteRaw(int32_t x0, int32_t y0, uint32_t z0, int32_t x1, int32_t y1, uint32_t z1,
                      const uint8_t rgba[4], int32_t u0, int32_t v0, int32_t u1, int32_t v1)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = x0;
    v[0].y = y0;
    v[1].x = x1;
    v[1].y = y1;
    v[0].z = z0;
    v[1].z = z1;
    v[0].s = (float)u0;
    v[0].t = (float)v0;
    v[1].s = (float)u1;
    v[1].t = (float)v1;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, rgba, 4);
    memcpy(v[1].rgba, rgba, 4);
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_FULLSCREEN, 1, s_spriteKey);
}

static void sprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t z,
                   const uint8_t rgba[4], int32_t u0, int32_t v0, int32_t u1, int32_t v1)
{
    spriteRaw(x0, y0, z, x1, y1, z, rgba, u0, v0, u1, v1);
}

/* PRIM's state bits: ABE and IIP (TME is the caller's rd_texture /
 * rd_texture_off). */
static void prim(int abe, int iip)
{
    rd__rec_abe(abe);
    rd_gouraud(iip);
}

static RdTarget orDefault(RdTarget t, RdTargetId d)
{
    return t.id ? t : rd_target(d);
}

static void targetSize(RdTarget t, uint32_t *w, uint32_t *h)
{
    RdTargetRec *r = rd__target_rec(t.id);
    *w = r ? r->w : 1;
    *h = r ? r->h : 1;
}

/* GS window coordinate (12.4) of the target's pixel x for a centred
 * XYOFFSET of a w-wide target: 0x8000 is 2048.0. */
static int32_t win(int32_t px16, uint32_t w)
{
    return 0x8000 - (int32_t)(w / 2) * 16 + px16;
}

/* The op the present folds into the deferred text before this
 * pass (rd_core.c rd__deferred_text_op; nothing without such text) */
static void textOp(RdPostKind kind, const uint8_t rgba[4], uint8_t fix, uint32_t lines, RdKey key)
{
    RdTextOp op;
    memset(&op, 0, sizeof(op));
    if (rgba) {
        memcpy(op.rgba, rgba, 4);
    }
    op.fix = fix;
    op.lines = lines;
    rd__deferred_text_op((uint8_t)kind, &op, key);
}

/* One reduction sprite: an RdPostRec of kind RD_POST_REDUCTION,
 * drawn by rd_replay.c's doBlurSprite through fx_sprite_ps in the GS
 * integer arithmetic (rd_blur.c), with the state block in force: corners
 * and UV as the GS receives them (12.4), the TEX0 size 2^TW x 2^TH of
 * gsb_Reduction's TEX0 (TW = TH = 9), MODULATE (TFX 0). */
static void reductionSprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t rgba[4],
                            int32_t u0, int32_t v0, int32_t u1, int32_t v1, int textured)
{
    RdPostParams q;
    memset(&q, 0, sizeof(q));
    q.rect[0] = (float)x0;
    q.rect[1] = (float)y0;
    q.rect[2] = (float)x1;
    q.rect[3] = (float)y1;
    q.uv[0] = (float)u0;
    q.uv[1] = (float)v0;
    q.uv[2] = (float)u1;
    q.uv[3] = (float)v1;
    memcpy(q.rgba, rgba, 4);
    q.z = 0xFFFFFFFFu;
    q.scalar[0] = q.scalar[1] = 512.0f;
    q.scalar[2] = 1.0f;
    if (!textured) {
        rd__post_blur(RD_POST_REDUCTION, &q);
        return;
    }
    /* the hardware sprite a scaled replay draws instead (rd_blur.c
     * rd__blur_screen_fallback): the plain screen sprite, then mirrored */
    RdScreenVtx v[4];
    memset(v, 0, sizeof(v));
    for (int m = 0; m < 2; m++) {
        RdScreenVtx *w = &v[2 * m];
        w[0].x = x0;
        w[0].y = y0;
        w[1].x = x1;
        w[1].y = y1;
        w[0].z = w[1].z = q.z;
        w[0].s = (float)(u0 - 8 * m);
        w[0].t = (float)v0;
        w[1].s = (float)(u1 - 8 * m);
        w[1].t = (float)v1;
        w[0].q = w[1].q = 1.0f;
        memcpy(w[0].rgba, rgba, 4);
        memcpy(w[1].rgba, rgba, 4);
    }
    rd__post_blur_verts(RD_POST_REDUCTION, &q, v);
}

/* gsb_Reduction.  Both sprites go through the GS sprite model rather than
 * the hardware sampler: the bilinear SCENE read is the GS's 4-bit fractions
 * with the sum shifted down (rd_blur.c), so the reduction, and with it the
 * motion blur loop that feeds DISPLAY back into SCENE, is the GS integer
 * result.  On a scaled target the textured sprite is drawn as the hardware
 * sprite instead (rd__blur_screen_fallback).  The textured sprite samples
 * SCENE at u = x + 0.75 (corners at -0.25 px, UV 0.5 at the corner),
 * v = 2y + 1; with the mirror on, the replay samples at u = x + 0.25
 * instead, the mirror image (rd__blur_uv_rect). */
static void postReduction(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    const int32_t crop = p->lines ? (int32_t)p->lines : (H >= 512 ? 8 : 2);
    RdTarget src = orDefault(p->src, RD_TARGET_SCENE);
    RdTarget dst = orDefault(p->dst, RD_TARGET_DISPLAY);
    static const uint8_t kBlack[4] = {0, 0, 0, 0};
    const uint8_t tint[4] = {p->rgba[0], p->rgba[1], p->rgba[2], 0x80};
    /* FBA 0, ALPHA 0x8000000048, FRAME FBP 0 (FBMSK 0), XYOFFSET centred on
     * W x H/2, SCISSOR W x H/2, TEST 0x30000, ZBUF mask on */
    rd_color_mask(0);
    rd_set_target(dst, (RdTarget){0}, (uint32_t)W, (uint32_t)(H / 2), 0);
    rd_fba(0);
    rd_blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 0); /* PRIM 0x106 / 0x116: no ABE */
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_texture_off();
    /* corners at -0.25 px: covers pixels 0..W-1, 0..H/2-1 */
    const int32_t x0 = -W / 2 * 16 + 0x8000 - 4, y0 = -H / 4 * 16 + 0x8000 - 4;
    const int32_t x1 = x0 + W * 16, y1 = y0 + H / 2 * 16;
    reductionSprite(x0, y0, x1, y1, kBlack, 0, 0, 0, 0, 0);
    rd__rec_scissor(2, crop, W - 3, H / 2 - 1 - crop);
    rd_texture(rd_target_texture(src, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd__rec_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    /* UV 0.5 .. W+0.5, 0.5 .. H+0.5 */
    reductionSprite(x0, y0, x1, y1, tint, 8, 8, W * 16 + 8, H * 16 + 8, 1);
    rd__rec_scissor(0, 0, W, H);
    textOp(RD_POST_REDUCTION, tint, 0, 0, POST_KEY(RD_POST_REDUCTION, 0));
}

static void postKeep(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    RdTarget src = orDefault(p->src, RD_TARGET_DISPLAY);
    static const uint8_t kKeep[4] = {112, 112, 112, 128};
    const uint8_t *col = (p->rgba[0] | p->rgba[1] | p->rgba[2] | p->rgba[3]) ? p->rgba : kKeep;
    if (p->dst.id) {
        rd_color_mask(0);
        rd_set_target(p->dst, p->dst.id == RD_TARGET_SCENE + 1 ? p->dst : (RdTarget){0},
                      (uint32_t)W, (uint32_t)H, RD_TARGET_OFFSET);
    }
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_fba(0);
    rd_tex_a(RD_TEXA_80_80);
    /* TEX0: TBP 0, PSMCT24, TCC 1, MODULATE */
    rd_texture(rd_target_texture(src, RD_VIEW_RGB24_TA0), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    prim(0, 0); /* PRIM 0x116 */
    /* r0 = {-(W/2)*16 - 12, -(H/2)*16 - 12, W*16 + 32, H*16 + 32},
     * r1 = {8, 8, W*16, H/2*16} */
    const int32_t x0 = -(W / 2) * 16 - 12 + 0x8000, y0 = -(H / 2) * 16 - 12 + 0x8000;
    sprite(x0, y0, x0 + W * 16 + 32, y0 + H * 16 + 32, 0, col, 8, 8, 8 + W * 16, 8 + H / 2 * 16);
    textOp(RD_POST_KEEP, NULL, 0, 0, 0);
}

/* gif_SetDrawEnviroment(0x800, 0, W, H, 1, 0): FRAME (FBMSK 0), SCISSOR,
 * XYOFFSET of the scene; the decoder binds the scene's depth with it */
static void sceneEnv(RdTarget dst, int32_t W, int32_t H)
{
    rd_color_mask(0);
    rd_set_target(dst, dst.id == RD_TARGET_SCENE + 1 ? dst : (RdTarget){0}, (uint32_t)W,
                  (uint32_t)H, RD_TARGET_OFFSET);
}

static void postFade(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    sceneEnv(orDefault(p->dst, RD_TARGET_SCENE), W, H);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_pabe(0);
    rd_blend(RD_BLEND_LERP_AS, 0, 1); /* ALPHA 0x44 (FIX 0); PRIM 0x446 sets ABE */
    rd_texture_off();
    rd_gouraud(0);
    const int32_t x0 = -(W / 2) * 16 + 0x8000, y0 = -(H / 2) * 16 + 0x8000;
    RdFrame *f = rd__rec_frame();
    if (f && f->fade < 1u + p->rgba[3]) {
        f->fade = 1u + p->rgba[3]; /* the fade edge (rd_interp.c snaps on it) */
    }
    s_spriteKey = POST_KEY(RD_POST_FADE, 0);
    sprite(x0, y0, x0 + W * 16, y0 + H * 16, 0xFFFFFFFFu, p->rgba, 0, 0, 0, 0);
    s_spriteKey = 0;
    textOp(RD_POST_FADE, p->rgba, 0, 0, POST_KEY(RD_POST_FADE, 0));
}

static void postLetterbox(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    const int32_t lines = p->lines ? (int32_t)p->lines : 58;
    static const uint8_t kBar[4] = {0, 0, 0, 0x80};
    sceneEnv(orDefault(p->dst, RD_TARGET_SCENE), W, H);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(1); /* ZBUF 0x300000C0 */
    rd_pabe(0);
    rd_blend(RD_BLEND_LERP_FIX, p->fix, 1); /* ALPHA 0x64 | level << 32; PRIM 0x446 */
    rd_texture_off();
    rd_gouraud(0);
    const int32_t x0 = -(W / 2) * 16 + 0x8000;
    const int32_t yTop = -(H / 2) * 16 - 4 + 0x8000;
    const int32_t yBot = ((H / 2) - lines) * 16 + 4 + 0x8000;
    s_spriteKey = POST_KEY(RD_POST_LETTERBOX, 0);
    sprite(x0, yTop, x0 + W * 16, yTop + lines * 16, 0xFFFFFFFFu, kBar, 0, 0, 0, 0);
    s_spriteKey = POST_KEY(RD_POST_LETTERBOX, 1);
    sprite(x0, yBot, x0 + W * 16, yBot + lines * 16, 0xFFFFFFFFu, kBar, 0, 0, 0, 0);
    s_spriteKey = 0;
    textOp(RD_POST_LETTERBOX, kBar, p->fix, (uint32_t)lines, POST_KEY(RD_POST_LETTERBOX, 0));
}

/* gsb_controlBrightness passes gif_MakeSpriteNoTexture corners that are
 * already absolute ((0x800 - W/2) << 4) and the helper adds the 0x8000
 * window origin again (GIF_XY), so the GS receives the first corner at
 * 0xF000 (3840.0) and a far corner whose 17-bit X carries into Y and whose
 * Y carries into Z: (256.0, 256.0625) for a 512 x 512 scene, Z 0xFFFFFFFF.
 * Both corners are recorded as the GS gets them; the rectangle between them
 * covers the whole scene.  Whether the GS draws a sprite whose second corner
 * lies above and left of the first is not documented in the sources this
 * port uses; rd draws it. */
static void postBrightness(const RdPostParams *p)
{
    const int32_t W = (int32_t)g_rd.gsW, H = (int32_t)g_rd.gsH;
    const uint8_t col[4] = {0xFF, 0xFF, 0xFF, p->rgba[3]};
    if (p->dst.id) {
        sceneEnv(p->dst, W, H);
    }
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_pabe(0);                           /* gif_SetAlpha(1, ...) */
    rd_blend(RD_BLEND_LERP_AS_ALT, 0, 1); /* mode 7: ALPHA 0x44; PRIM 0x446 sets ABE */
    rd_texture_off();
    rd_gouraud(0);
    const int64_t x = (int64_t)((0x800 - W / 2) << 4), y = (int64_t)((0x800 - H / 2) << 4);
    const int64_t w = (int64_t)W << 4, h = (int64_t)H << 4;
    const uint64_t z = 0xFFFFFFFEull;
    /* GIF_XY(x, y, z) and GIF_XY0(x + fx, y + fy, z), fx = w + 0x8000 */
    const uint64_t a = (uint64_t)(x + 0x8000) | ((uint64_t)(y + 0x8000) << 16) | (z << 32);
    const uint64_t b = (uint64_t)(x + w + 0x8000) | ((uint64_t)(y + h + 0x8000) << 16) | (z << 32);
    spriteRaw((int32_t)(a & 0xFFFF), (int32_t)((a >> 16) & 0xFFFF), (uint32_t)(a >> 32),
              (int32_t)(b & 0xFFFF), (int32_t)((b >> 16) & 0xFFFF), (uint32_t)(b >> 32), col, 0, 0,
              0, 0);
    textOp(RD_POST_BRIGHTNESS, col, 0, 0, POST_KEY(RD_POST_BRIGHTNESS, 0));
}

/* gif_MakeSprite(x, y, w, h, 0, uv, col, prim) as gif_SpriteSensitiveOrg
 * calls it: corners GIF_XY(x, y) and x + w + 0x8000, UV and UV + size */
static void makeSprite(int32_t x, int32_t y, int32_t w, int32_t h, const int32_t uv[4],
                       const uint8_t col[4])
{
    sprite(x + 0x8000, y + 0x8000, x + w + 0x8000, y + h + 0x8000, 0, col, uv[0], uv[1],
           uv[0] + uv[2], uv[1] + uv[3]);
}

static const uint8_t kAaCol[4] = {128, 128, 128, 128};

static void postAaDownsample(const RdPostParams *p)
{
    static const int32_t s0[4] = {4, 4, 8192, 8192}, s1[4] = {4, 4, 4096, 4096};
    RdTarget scene = orDefault(p->src, RD_TARGET_SCENE);
    rd_test_gs(RD_TEST_Z_ALWAYS); /* gif_SetZTest(0) */
    rd_z_write(0);                /* gif_SetZWrite(0) */
    /* gif_SetDrawEnviroment(0x2800, 0, 256, 256, 0, 0): FBMSK 0 */
    rd_color_mask(0);
    rd_set_target(rd_target(RD_TARGET_AA0), (RdTarget){0}, 256, 256, 0);
    /* TEX0 0x800, TBW 8, 512 x 512, TCC 1, MODULATE */
    rd_texture(rd_target_texture(scene, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_pabe(1); /* gif_SetAlpha(0, 2, 128) */
    rd_blend_func(RD_BLEND_LERP_FIX, 128);
    prim(0, 0); /* PRIM 0x116 */
    makeSprite(-2052, -2052, 4096, 4096, s0, kAaCol);
    if (p->lines >= 2) {
        /* TEX0 0x2800, TBW 4, 256 x 256; gif_SetDrawEnviroment(0x2C00, 0, 128, 128, 0, 0) */
        rd_texture(rd_target_texture(rd_target(RD_TARGET_AA0), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
                   RD_TCC_RGBA);
        rd_color_mask(0);
        rd_set_target(rd_target(RD_TARGET_AA1), (RdTarget){0}, 128, 128, 0);
        makeSprite(-1028, -1028, 2048, 2048, s1, kAaCol);
    }
}

static void postAaComposite(const RdPostParams *p)
{
    static const int32_t s1[4] = {4, 4, 4096, 4096}, s2[4] = {4, 4, 2048, 2048};
    RdTarget scene = orDefault(p->dst, RD_TARGET_SCENE);
    const uint8_t lv0 = p->rgba[0], lv1 = p->rgba[1];
    /* gif_SetDrawEnviroment(0x800, 0, 512, 512, 1, 0): 512 lines whatever
     * the scene size, as the original writes it */
    sceneEnv(scene, 512, 512);
    if (lv1) {
        rd_pabe(0); /* gif_SetAlpha(1, 2, lv1) */
        rd_blend_func(RD_BLEND_LERP_FIX, lv1);
        /* TEX0 0x2C00, TBW 2, 128 x 128 */
        rd_texture(rd_target_texture(rd_target(RD_TARGET_AA1), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
                   RD_TCC_RGBA);
        prim(1, 0); /* PRIM 0x156 */
        makeSprite(-4100, -4100, 8192, 8192, s2, kAaCol);
    }
    if (lv0) {
        rd_pabe(0);
        rd_blend_func(RD_BLEND_LERP_FIX, lv0);
        rd_texture(rd_target_texture(rd_target(RD_TARGET_AA0), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
                   RD_TCC_RGBA);
        prim(1, 0);
        makeSprite(-4100, -4100, 8192, 8192, s1, kAaCol);
    }
    rd_z_write(1);                /* gif_SetZWrite(1) */
    rd_test_gs(RD_TEST_Z_GEQUAL); /* gif_SetZTest(1) */
    sceneEnv(scene, (int32_t)g_rd.gsW, (int32_t)g_rd.gsH);
}

static void postFilmNoise(const RdPostParams *p)
{
    const uint8_t col[4] = {0x80, 0x80, 0x80, p->rgba[3]};
    const float scale = p->scalar[0];
    rd_sampler_wrap(RD_WRAP_REPEAT, RD_WRAP_REPEAT); /* CLAMP_1 0 */
    rd_z_write(0);                                   /* ZBUF 0x1300000C0 */
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_pabe(0);
    rd_blend_func(RD_BLEND_LERP_AS, 0); /* ALPHA 0x44 */
    if (p->src.id) {
        rd_texture(rd_target_texture(p->src, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    }
    prim(1, 0); /* PRIM 0x56: sprite, TME, ABE, ST */
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    /* ST 0,0 at XYZ2 0xFFFFFFFF70007000; ST scale,scale at 0xFFFFFFFF90009000 */
    v[0].x = v[0].y = 0x7000;
    v[1].x = v[1].y = 0x9000;
    v[0].z = v[1].z = 0xFFFFFFFFu;
    v[0].s = v[0].t = 0.0f;
    v[1].s = v[1].t = scale;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, col, 4);
    memcpy(v[1].rgba, col, 4);
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_FULLSCREEN, 0, 0);
}

static void postComposite(const RdPostParams *p)
{
    if (!p->src.id || !p->dst.id) {
        rd__log("RD_POST_COMPOSITE_FIX needs src and dst");
        return;
    }
    uint32_t dw, dh, sw, sh;
    targetSize(p->dst, &dw, &dh);
    targetSize(p->src, &sw, &sh);
    RdTarget depth = p->dst.id == RD_TARGET_SCENE + 1 ? p->dst : (RdTarget){0};
    rd_color_mask(0);
    rd_set_target(p->dst, depth, dw, dh, 0);
    rd_blend((RdBlend)p->blend, p->fix, 1);
    if (p->exactInt) {
        RdCmd *c = rd__push(RDC_EXACT_BLEND);
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
    rd_texture(rd_target_texture(p->src, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    const int32_t x0 = win((int32_t)(r[0] * 16.0f), dw), y0 = win((int32_t)(r[1] * 16.0f), dh);
    sprite(x0, y0, x0 + (int32_t)(r[2] * 16.0f), y0 + (int32_t)(r[3] * 16.0f), 0xFFFFFFFFu, col,
           (int32_t)(uv[0] * 16.0f), (int32_t)(uv[1] * 16.0f), (int32_t)((uv[0] + uv[2]) * 16.0f),
           (int32_t)((uv[1] + uv[3]) * 16.0f));
}

static void postCopy(const RdPostParams *p)
{
    RdFrame *f = rd__rec_frame();
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
    uint32_t off = rd__frame_payload(f, &r, sizeof(r));
    RdCmd *c = rd__push(RDC_COPY);
    if (c) {
        c->u[0] = p->src.id;
        c->u[1] = p->dst.id;
        c->u[2] = off;
    }
}

static void postStub(RdPostKind kind, const RdPostParams *p)
{
    RdFrame *f = rd__rec_frame();
    if (!f) {
        rd__push(RDC_POST_STUB);
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
    r.lutOffset = p->lut ? rd__frame_payload(f, p->lut, 256 * 4) : ~0u;
    uint32_t off = rd__frame_payload(f, &r, sizeof(r));
    RdCmd *c = rd__push(RDC_POST_STUB);
    if (c) {
        c->b[0] = (uint8_t)kind;
        c->u[1] = off;
        c->u[2] = (uint32_t)sizeof(r);
    }
}

static void postFog(const RdPostParams *p)
{
    if (!p->lut) {
        rd__log("RD_POST_FOG needs the 256-entry LUT");
        return;
    }
    postStub(RD_POST_FOG, p);
}

void rd_post(RdPostKind kind, const RdPostParams *params)
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
    case RD_POST_AA_DOWNSAMPLE:
        postAaDownsample(params);
        break;
    case RD_POST_AA_COMPOSITE:
        postAaComposite(params);
        break;
    case RD_POST_FILM_NOISE:
        postFilmNoise(params);
        break;
    case RD_POST_COMPOSITE_FIX:
        postComposite(params);
        break;
    case RD_POST_COPY:
        postCopy(params);
        break;
    case RD_POST_FOG:
        postFog(params);
        break;
    /* staticBlur.c's sprites, recorded by rd_blur.c and
     * replayed by rd_replay.c's doBlurSprite */
    case RD_POST_MOTION_BLUR:
    case RD_POST_DOF:
    case RD_POST_FLARE:
    case RD_POST_BLOOM:
    case RD_POST_AURA:
    case RD_POST_EYE_BLUR:
        rd__post_blur(kind, params);
        break;
    case RD_POST_SHADOW_RESOLVE:
    case RD_POST_BLUR:
    case RD_POST_PRESENT_BLIT:
        /* reserved numbers: no caller issues them and the replay has no
         * model, so nothing is recorded */
        rd__log_once(RD_ONCE_POST_RESERVED, "rd_post kind %u is reserved: not recorded",
                     (unsigned)kind);
        break;
    default:
        postStub(kind, params);
        break;
    }
}
