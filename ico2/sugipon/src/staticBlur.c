#include "staticBlur.h"
#include "debug.h"
#include "Primitive.h"
#include "Texture.h"
#include <string.h>
#include <libvu0.h>
#include <stdio.h>
#include "GsBase.h"
#include "main.h"
#include "Matrix.h"

static void blur(int n, void *col);

/* workBase: the GS block addresses of the work buffers Work0..Work3 the pass
   names use (FullScreenEffectBefore rewrites them).  sunScreen: calcSun's
   projected sun, integer screen coordinates.  sunView: the sun direction in
   view space, its z the in-front test.  sunDir: the world sun direction
   GetSunWorldPos hands out.  sunScreenOffset: the drawing-offset correction
   calcSun adds after the projection.  The int[4] runs are sprite rectangles
   in 12.4 fixed point and the float[4] points get their z written before the
   projection that yields a Z value. */
static int workBase[4] = {0x2800, 0x2C00, 0x3000, 0x3400}; /* derived name */

static int sunScreen[4] = {0, 0, 0, 0}; /* derived name */

static float sunView[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float sunDir[4] = {1.0f, -1.5f, -1.0f, 1.0f}; /* derived name */

static float sunScreenOffset[4] = {-2048.0f, -2048.0f, 0.0f, 0.0f}; /* derived name */

static int work0ToFeedBackRect[4] = {-1024, -1024, 2048, 2048}; /* derived name */

static int feedBackToWork0Rect[4] = {-1024, -1024, 2048, 2048}; /* derived name */

static float subWork1Point[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static int eyeGhostRect[4] = {-2048, -1024, 4096, 2048}; /* derived name */

static int eyeShrinkRect[4] = {-2048, -1024, 1024, 512}; /* derived name */

static int eyeTintRect[4] = {-2048, -1024, 4096, 2048}; /* derived name */

static int copyToWorkRect[4] = {-2044, -2044, 4096, 4096}; /* derived name */

static float pasteToFBPoint[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

/* PC port: the native frame (port/include/ico_frame.h, GsBase.c gsFrame;
 * 512 wide at 4:3).  Every extent of this file is one of:
 *   FRAME    the frame-sized buffers (SCENE 0x800, the aura's work buffer 1
 *            at the frame's size, work buffer 2, DISPLAY): gsFrame.w wide,
 *            their TEX0 TBW gsFrame.fbw and TW ico_frame_tw(gsFrame.w);
 *   WORK     the work buffers, sized with the frame per 512 columns
 *            (rd_core.c namedTargetDesc): 256-wide ones (Work0, Work1, Work3,
 *            work buffer 2's pad) SB_X(256) = W / 2, 128-wide ones (the
 *            feedback area 0x3F00, the aura's taps in Work0) SB_X(128) =
 *            W / 4, their heights the PS2's; a rectangle or UV range drawn
 *            over such a buffer takes its x BASE (the buffer's left edge,
 *            -SB_X(2048) for a 256-wide one) and its x EXTENT through SB_X,
 *            while a DELTA in the buffer's own pixels (the taps' +-10/16
 *            px, half-texel and border offsets of 4, 8, 16 or 24, blurUv)
 *            stays as it is, so a kernel spans the buffer pixels it spans on
 *            the PS2 and a buffer pixel covers two (or four) screen pixels
 *            at every aspect; the SHRINKS of the glow and depth-of-field
 *            passes (rm, um, cur, pre) are a scale about the buffer's left
 *            edge, so they keep the PS2's slope and are anchored at the 4:3
 *            part's left edge (sbShrinkAt, sbShrinkSpan); TEX0's TBW and TW
 *            are SB_TEX0_W of the 4:3 width;
 *   ADDRESS  every TBP and FBP (workBase, 0x3F00, work buffer 2's pad
 *            SB_WORK2_PAD_FBP) stays the PS2's: the host maps them to
 *            targets by address and class.
 * SB_X(v) is v at 4:3, so every rectangle, size and register word below is
 * the PS2's there (SB_TEX0_W(256) is 0x20010000, SB_TEX0_W(128) 0x1C008000,
 * SB_TEX0_W(512) 0x24020000). */
#define SB_X(v) ico_frame_x(&gsFrame, (v))
#define SB_TEX0_W(v) ico_frame_tex0_w(&gsFrame, (v))
/* ADDRESS: work buffer 2's pad, the block past the 4:3 frame's pages */
#define SB_WORK2_PAD_FBP (workBase[2] + ScreenWidth * ScreenHeight / 64) /* ADDRESS */

/* a WORK rectangle the PS2 wrote as {base, y, extent, h} (in 1/16 pixel)
   with x base -bx + dx: the base and the extent at the frame's width, dx
   (a delta in the buffer's pixels) kept */
static void sbWorkRect(int *o, int bx, int dx, int y, int w, int h) /* port name */
{
    o[0] = -SB_X(bx) + dx;
    o[1] = y;
    o[2] = SB_X(w);
    o[3] = h;
}

/* PC port: the glow's and the depth of field's shrink passes (blurPass,
 * depthFieldPass).  The PS2 draws a 256-wide work buffer into its twin as
 * one sprite whose rect (or UV range) is d sixteenths shorter than the
 * other's, so texel u = x * base / (base - d) (plus the half-texel and
 * border offsets): a scale about the buffer's LEFT edge, whose sub-pixel
 * phase x * d / (16 * 256) is the game's fixed blur pattern across the
 * picture.  On a wider buffer the same delta about the left edge would put
 * a different phase on every 4:3 column (equal only at the centre).  So the
 * shrink keeps the PS2's slope (span * d / base, base the PS2's span of
 * 4096 or 4064 sixteenths) and is anchored at the 4:3 part's left edge,
 * a = W / 4 - 128 buffer pixels: the rect or UV start moves right by
 * a * 16 * d / base sixteenths, so buffer column a + x samples texel
 * a + (the PS2's texel for x), the PS2's picture in the 4:3 columns.  At
 * 4:3 a is 0 and span is base: the start offset is 0 and the shrink is d,
 * the PS2's numbers. */
static int sbShrinkAt(int d, int base) /* port name */
{
    const int a = SB_X(256) / 2 - 128;

    return (a * 16 * d + base / 2) / base;
}

/* the span (1/16 px) shrunk by d at the PS2's slope: span - span * d / base */
static int sbShrinkSpan(int span, int d, int base) /* port name */
{
    return span - (int)(((long long)span * d + base / 2) / base);
}

/* as in GifPacket.h, which this file does not include */
extern void gif_EndPacket(void);
/* as in GifPacket.h, which this file does not include */
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);
/* as in GifPacket.h, which this file does not include */
extern void gif_SetDrawEnviroment(unsigned long long fbp, unsigned long long psm, unsigned int w,
                                  unsigned int h, int useoffset, int clear);
/* as in GifPacket.h, which this file does not include */
extern void gif_SetGsReg(long long reg, long long data);
/* as in GifPacket.h, which this file does not include */
extern void gif_SetZTest(int on);
/* as in GifPacket.h, which this file does not include */
extern void gif_SetZWrite(int on);
/* as in GifPacket.h, which this file does not include */
extern void gif_SpriteSensitiveOrg(void *r, long long z, void *uv, void *col, int prim);
/* as in GifPacket.h, which this file does not include */
extern void gif_StartPacketPri(int pri);
/* port/game/video_options.c: the Glow, Depth of field and Motion blur
   switches, 1 = on */
extern int ico_video_effect_glow(void);
extern int ico_video_effect_depth_of_field(void);
extern int ico_video_effect_motion_blur(void);
/* port/game/photo_mode.c: 1 while photo mode shows the paused game from
   its own camera, when the aura's feedback follows the picture
   as it does in play */
extern int ico_photo_active(void);
/* port/game/photo_mode.c: 1 on the tick that left photo mode, when the
   feedback area takes the game camera's picture once (the pause keeps it
   otherwise, and it held the photo camera's) */
extern int ico_photo_left(void);

#ifdef ICO_RD

/* PC port: the
 * window build records this file's packets on rd itself instead of through
 * GifPacket.c's register decoder, whose generic mapping of the work buffers
 * cannot be right here (the same TBP is a 256 x 128, a 256 x 256 and a
 * scene-sized buffer by turns; the scene-sized ones share the scene's Z
 * buffer; TFX HIGHLIGHT; the motion blur and aura feedback need the GS
 * integer blend).  The gif_* calls below are renamed to sbHost*, which
 * write the same registers in the same order as rd state (so the state the
 * passes leave behind is the GS's) and send every sprite as rd_post of the
 * kind of the effect it belongs to (SB_KIND), drawn by rd in the GS
 * integer arithmetic.  The game code below is unchanged; the packets are
 * still opened and closed through GifPacket.c, so the list selection and
 * the packet bookkeeping stay. */
#include "rd.h"

/* GifPacket.h, which this file does not include: the native frame width
   the GS register decoder reads (512 at 4:3) */
extern int gif_HostFrameWidth(void);

static RdPostKind sbKind = RD_POST_FLARE; /* the effect the next sprites belong to */

static int sbMotionDrawn; /* MotionBlur drew since the last FullScreenEffectBefore */

static unsigned long long sbTex0; /* TEX0_1 as last written */

static unsigned int sbOnce;

static void sbHostOnce(int bit, const char *msg, unsigned long long v)
{
    if ((sbOnce & (1u << bit)) == 0) {
        sbOnce |= 1u << bit;
        fprintf(stderr, "staticBlur: %s (0x%llx; reported once)\n", msg, v);
    }
}

/* FRAME (the block address gif_SetDrawEnviroment takes) and the frame
 * size to the target standing for that VRAM, and the depth buffer bound
 * with it: the scene-sized buffers are drawn with ZBUF 0xC0, the scene's
 * Z buffer, as everything is; the small ones only with Z ALWAYS and ZMSK */
static RdTarget sbHostFrame(unsigned int tbp, unsigned int w, RdTarget *depth)
{
    RdTargetId id;

    /* the work buffers by workBase, not by address: the first
       FullScreenEffectAfter of a run comes before any
       FullScreenEffectBefore, with workBase's initial 0x2C00 and 0x3400 */
    *depth = (RdTarget){0};
    if (tbp == 0x800) {
        id = RD_TARGET_SCENE;
        *depth = rd_target(RD_TARGET_SCENE);
    } else if (tbp == 0) {
        id = RD_TARGET_DISPLAY;
    } else if (tbp == (unsigned int)workBase[0]) {
        /* the taps' 128-wide (W / 4) view of Work0 or the 256-wide (W / 2) */
        id = w <= (unsigned int)SB_X(128) ? RD_TARGET_AURA_TAP : RD_TARGET_WORK0;
    } else if (tbp == (unsigned int)workBase[1]) {
        /* FRAME: the aura's frame-sized view of Work1, or the W / 2 one */
        id = w >= (unsigned int)gsFrame.w ? RD_TARGET_AURA_WORK : RD_TARGET_WORK1;
        if (id == RD_TARGET_AURA_WORK) {
            *depth = rd_target(RD_TARGET_SCENE);
        }
    } else if (tbp == (unsigned int)workBase[2]) {
        id = RD_TARGET_WORK2;
        *depth = rd_target(RD_TARGET_SCENE);
    } else if (tbp == (unsigned int)workBase[3]) {
        id = RD_TARGET_WORK3;
    } else if (tbp == 0x3F00) {
        id = RD_TARGET_FEED128;
    } else if (tbp == (unsigned int)SB_WORK2_PAD_FBP) {
        /* ADDRESS: fillWork2's block past work buffer 2, the PS2's */
        id = RD_TARGET_WORK2_PAD;
    } else {
        sbHostOnce(0, "FRAME at a block with no work buffer: drawn into SCENE", tbp);
        id = RD_TARGET_SCENE;
        *depth = rd_target(RD_TARGET_SCENE);
    }
    return rd_target(id);
}

/* TEX0 to the target view it reads: TBP and TBW pick the buffer as FRAME
 * does, PSMCT24 reads the RGB24 view (TEXA applied) */
static RdTex sbHostTexture(unsigned long long tex0)
{
    unsigned int tbp = (unsigned int)(tex0 & 0x3FFF);
    unsigned int tbw = (unsigned int)((tex0 >> 14) & 0x3F);
    unsigned int psm = (unsigned int)((tex0 >> 20) & 0x3F);
    RdTargetId id;

    if (tbp == 0) {
        id = RD_TARGET_DISPLAY;
    } else if (tbp == 0x800) {
        id = RD_TARGET_SCENE;
    } else if (tbp == (unsigned int)workBase[0]) {
        /* TBW (W / 4 + 63) / 64 (2 at 4:3) for the taps' view, else Work0 */
        id = tbw <= (unsigned int)((SB_X(128) + 63) / 64) ? RD_TARGET_AURA_TAP : RD_TARGET_WORK0;
    } else if (tbp == (unsigned int)workBase[1]) {
        /* TBW gsFrame.fbw (8 at 4:3) for the frame-sized view */
        id = tbw >= (unsigned int)gsFrame.fbw ? RD_TARGET_AURA_WORK : RD_TARGET_WORK1;
    } else if (tbp == (unsigned int)workBase[2]) {
        id = RD_TARGET_WORK2;
    } else if (tbp == (unsigned int)workBase[3]) {
        id = RD_TARGET_WORK3;
    } else if (tbp == 0x3F00) {
        id = RD_TARGET_FEED128;
    } else {
        sbHostOnce(1, "TEX0 at a block with no work buffer: untextured", tex0);
        return (RdTex){0};
    }
    return rd_target_texture(rd_target(id), psm == 1 ? RD_VIEW_RGB24_TA0 : RD_VIEW_RGBA);
}

static void sbHostStartPacketPri(int pri)
{
    gif_StartPacketPri(pri);
}

static void sbHostEndPacket(void)
{
    gif_EndPacket();
}

static void sbHostSetGsReg(long long reg, long long data)
{
    unsigned long long d = (unsigned long long)data;

    switch ((unsigned int)reg & 0xFF) {
    case 0x06: /* TEX0_1: bound with the next sprite's PRIM */
        sbTex0 = d;
        break;
    case 0x08: /* CLAMP_1 */
    {
        RdSamplerWrap w = rd_wrap_from_gs(d);

        rd_sampler_wrap((RdWrap)w.s, (RdWrap)w.t);
        break;
    }
    case 0x14: /* TEX1_1 */
    {
        unsigned int mmin = (unsigned int)((d >> 6) & 7);

        rd_sampler_filter((d >> 5) & 1 ? RD_FILTER_LINEAR : RD_FILTER_NEAREST,
                          mmin == 1 || mmin == 4 || mmin == 5 ? RD_FILTER_LINEAR
                                                              : RD_FILTER_NEAREST);
        break;
    }
    case 0x3B: /* TEXA: 0x8000000080, the one value this file writes */
        rd_tex_a(((d >> 15) & 1) ? RD_TEXA_80_80_AEM : RD_TEXA_80_80);
        if ((d & 0xFF) != 0x80 || ((d >> 32) & 0xFF) != 0x80) {
            sbHostOnce(2, "TEXA other than 0x80/0x80", d);
        }
        break;
    case 0x47: /* TEST_1 */
        rd_test_gs(d);
        break;
    case 0x4A: /* FBA_1 */
        rd_fba((int)(d & 1));
        break;
    default:
        sbHostOnce(3, "register write not recorded", (unsigned long long)reg);
        break;
    }
}

static void sbHostSetAlpha(long long alpha, long long mode, long long fix)
{
    /* PABE, then ALPHA_1 from alphaTable (GifPacket.c): the RdBlend values
       are the table's modes */
    rd_pabe(alpha == 0);
    rd_blend_func((RdBlend)((unsigned long long)mode < 12 ? mode : 0), (uint8_t)fix);
}

static void sbHostSetDrawEnviroment(unsigned long long fbp, unsigned long long psm, unsigned int w,
                                    unsigned int h, int useoffset, int clear)
{
    RdTarget depth;
    RdTarget t = sbHostFrame((unsigned int)fbp, w, &depth);

    (void)psm; /* always 0: PSMCT32 */
    if (clear) {
        sbHostOnce(4, "gif_SetDrawEnviroment with clear: not recorded", fbp);
    }
    rd_color_mask(0); /* FRAME.FBMSK 0 */
    rd_set_target(t, depth, w, h, useoffset ? RD_TARGET_OFFSET : 0);
}

static void sbHostSetZTest(int on)
{
    rd_test_gs(on ? 0x50000 : 0x30000);
}

static void sbHostSetZWrite(int on)
{
    rd_z_write(on);
}

/* gif_SpriteSensitiveOrg: PRIM (prim << 6) | 0x116 with a UV rectangle (TME,
   FST, flat) or | 0x406 without, RGBAQ, then the two vertices as
   gif_MakeSprite and makeSpriteNoTexture pack them */
static void sbHostSprite(void *rect, long long z, void *uvRect, void *colour, int prim)
{
    const int *r = rect;
    const int *uv = uvRect;
    const unsigned char *col = colour;
    long long a = (long long)(r[0] + 0x8000) | ((long long)(r[1] + 0x8000) << 16) | (z << 32);
    long long b =
        (long long)(r[0] + r[2] + 0x8000) | ((long long)(r[1] + r[3] + 0x8000) << 16) | (z << 32);
    RdPostParams p;

    memset(&p, 0, sizeof(p));
    if (uv) {
        RdTex t = sbHostTexture(sbTex0);
        unsigned int tfx = (unsigned int)((sbTex0 >> 35) & 3);

        if (t.id) {
            rd_texture(t, tfx == 1 ? RD_TEXFN_DECAL : RD_TEXFN_MODULATE,
                       ((sbTex0 >> 34) & 1) ? RD_TCC_RGBA : RD_TCC_RGB);
        } else {
            rd_texture_off();
        }
        /* PC port: UV's 14 bits at 4:3, 16 on a wide frame, as the GS
           register decoder reads them (GifPacket.c) */
        const int m = gif_HostFrameWidth() > 512 ? 0xFFFF : 0x3FFF;

        p.uv[0] = (float)(uv[0] & m);
        p.uv[1] = (float)(uv[1] & m);
        p.uv[2] = (float)((uv[0] + uv[2]) & m);
        p.uv[3] = (float)((uv[1] + uv[3]) & m);
        p.scalar[0] = (float)(1 << ((sbTex0 >> 26) & 0xF));
        p.scalar[1] = (float)(1 << ((sbTex0 >> 30) & 0xF));
        p.lines = tfx;
    } else {
        rd_texture_off();
    }
    rd_abe(prim & 1);
    rd_gouraud(0);
    p.rect[0] = (float)(a & 0xFFFF);
    p.rect[1] = (float)((a >> 16) & 0xFFFF);
    p.rect[2] = (float)(b & 0xFFFF);
    p.rect[3] = (float)((b >> 16) & 0xFFFF);
    p.z = (uint32_t)((unsigned long long)b >> 32);
    memcpy(p.rgba, col, 4);
    p.scalar[2] = 1.0f; /* the frame-time factor: one frame (Original) */
    p.exactInt = 1;
    sbMotionDrawn |= sbKind == RD_POST_MOTION_BLUR;
    rd_post(sbKind, &p);
}

#define gif_StartPacketPri sbHostStartPacketPri
#define gif_EndPacket sbHostEndPacket
#define gif_SetGsReg sbHostSetGsReg
#define gif_SetAlpha sbHostSetAlpha
#define gif_SetDrawEnviroment sbHostSetDrawEnviroment
#define gif_SetZTest sbHostSetZTest
#define gif_SetZWrite sbHostSetZWrite
#define gif_SpriteSensitiveOrg sbHostSprite
/* the effect the sprites that follow belong to (rd_post's kind) */
#define SB_KIND(k) (sbKind = (k))
#define SB_FLARE_KIND(mode) ((mode) & 2 ? RD_POST_BLOOM : RD_POST_FLARE)
#else
#define SB_KIND(k)
#endif

typedef struct { /* field names derived */
    int x, y, w, h;
} SprUV; /* derived name */

typedef struct { /* field names derived */
    unsigned char f[4];
} SprCol; /* derived name */

/* the texture rectangle of a whole 256x128 work buffer, half a texel in
   from each edge, read by eyeBlur and pasteFullScreenFlare */
static const SprUV workBufferUv = {8, 8, 4088, 2040}; /* derived name */

/* PC port: workBufferUv over the W / 2 wide buffer: the base 8 and the
   -8 at the far edge are half texels (DELTA), the 4096 the extent (WORK) */
static SprUV sbWorkBufferUv(void) /* port name */
{
    SprUV uv = workBufferUv;

    uv.w = SB_X(4096) - (4096 - workBufferUv.w);
    return uv;
}

/* The post effect and aura feed modes FullScreenEffect* dispatch on and the
   requests the stage parameters make, the flare switch, the two sun fans,
   the sun switch, the depth fade start and width, the aura subtraction depth,
   then the sprite colours of the passes. */
static int postMode = 1; /* derived name */

static int postModeRequest = 1; /* derived name */

static int flareOn = 1; /* derived name */

static int feedMode = 2; /* derived name */

static int feedModeRequest = 0; /* derived name */

static Fan2D *sunGlowFan = 0; /* derived name */

static Fan2D *sunCoreFan = 0; /* derived name */

static int sunOn = 0; /* derived name */

static float depthFadeStart = 100.0f; /* derived name */

static float depthFadeWidth = 400.0f; /* derived name */

static float auraInspireZ = 200.0f; /* derived name */

static SprCol blurPassCol = {{128, 128, 128, 0}}; /* derived name */

static SprCol clearCol = {{0, 0, 0, 0}}; /* derived name */

static SprCol fillZCol = {{0, 0, 0, 192}}; /* derived name */

static SprCol alphaCopyCol = {{0, 0, 0, 128}}; /* derived name */

static SprCol whiteCol = {{128, 128, 128, 128}}; /* derived name */

static SprCol flareCol = {{139, 136, 132, 40}}; /* derived name */

static SprCol auraClearCol = {{0, 0, 0, 0}}; /* derived name */

/* One pass of the blur, inlined twice into blur: the buffer fb drawn from
 * the texture buffer tex as one sprite, the sprite rect or the texture rect
 * shrunk by rm or um. */
static inline void blurPass(int fb, int tex, int rm, int um, void *col) /* derived name */
{
    gif_SetDrawEnviroment(fb, 0, SB_X(256), 128, 0, 0);

    gif_SetGsReg(6, tex | SB_TEX0_W(256) | 0x15C0000000LL);
    gif_SetGsReg(0x14, 0x60);
    gif_SetGsReg(8, 5);
    gif_SetAlpha(1, 4, 0);
    {
        /* WORK: the shrinks rm and um at the PS2's slope, anchored at the
           4:3 part's left edge (sbShrinkAt); the y shrink as the PS2's */
        int rect[4] = {-SB_X(2048) + sbShrinkAt(rm, 4096), -1024,
                       sbShrinkSpan(SB_X(4096), rm, 4096), 2048 - rm};
        int uv[4] = {8 + sbShrinkAt(um, 4096), 8, sbShrinkSpan(SB_X(4096), um, 4096), 2048 - um};

        gif_SpriteSensitiveOrg(rect, 0, uv, col, 0);
    }
}

/* the first pass shrinks the sprite by n + 6 in the blur colour, the second
 * shrinks the texture back in the caller's colour */
static void blur(int n, void *col)
{
    blurPass(workBase[1], workBase[0], n + 6, 0, &blurPassCol);
    blurPass(workBase[0], workBase[1], 0, n + 6, col);
}

static void auraInspireBefore(void)
{
    int rect[4] = {-gsFrame.w / 2 * 16, -ScreenHeight / 2 * 16, gsFrame.w * 16, ScreenHeight * 16};

    SB_KIND(RD_POST_AURA);
    gif_StartPacketPri(8);
    gif_SetGsReg(8, 5);

    gif_SetDrawEnviroment(workBase[1], 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);

    gif_SetAlpha(0, 2, 0);
    gif_SpriteSensitiveOrg(rect, 0, (void *)0, &auraClearCol, 0);

    gif_SetZWrite(1);
    gif_SetZTest(1);

    gif_SetAlpha(1, 7, 0);
    gif_SetDrawEnviroment(workBase[1], 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetGsReg(8, 5);
    gif_SetGsReg(0x4A, 0);
    gif_EndPacket();
}

/* the twelve texture and rectangle coordinates the two blur sprites are
   built from.  PC port: no code writes them (zero in every build); each is a
   DELTA added to a base or an extent the frame's width scales: [0] [1] UV
   base, [2] [3] UV extent, [4] [5] rect base (reduceCopy...); [6] [7] UV
   base, [8] [9] UV extent, [10] [11] rect base (copyAlphaChannel...) */
static int blurUv[12]; /* derived name */

/* the blur sprite's RGBA */
static SprCol blurCol; /* derived name */

static void reduceCopyAlphaChannelOfWork1ToWork0(void)
{
    /* FRAME UV (the aura's buffer), WORK rect (Work0, W / 2 x 128) */
    int suv[4] = {blurUv[0] + 16, blurUv[1] + 16, gsFrame.w * 16 + blurUv[2],
                  ScreenHeight * 16 + blurUv[3]};
    int srect[4] = {blurUv[4] - SB_X(2048), blurUv[5] - PAL_NTSCAUG6JUN26(1032, 1024), SB_X(4096),
                    systemStatus[0] == 0 ? 1792 : 2048};

    gif_SetGsReg(6, workBase[1] | SB_TEX0_W(512) | 0x640000000LL);

    gif_SetDrawEnviroment(workBase[0], 0, SB_X(256), 128, 0, 0);

    gif_SetAlpha(1, 5, 0);
    gif_SpriteSensitiveOrg(srect, 0, suv, &alphaCopyCol, 1);
}

static void copyAlphaChannelOfWork0ToFeedBackArea(void)
{
    /* WORK: Work0 (W / 2) into the feedback area (W / 4) */
    int suv[4] = {blurUv[6], blurUv[7], blurUv[8] + SB_X(4096), blurUv[9] + 2048};
    int srect[4] = {blurUv[10] - SB_X(1024), blurUv[11] - 1024, SB_X(2048), 2048};

    gif_SetGsReg(6, workBase[0] | SB_TEX0_W(256) | 0x5C0000000LL);

    gif_SetDrawEnviroment(0x3F00, 0, SB_X(128), 128, 0, 0);

    gif_SetAlpha(1, 5, 0);
    gif_SpriteSensitiveOrg(srect, 0, suv, &alphaCopyCol, 1);
}

static void pasteFeedBackAreaToFB(int *rect) /* derived name */
{
    /* WORK UV (the feedback area, W / 4), over the frame */
    int suv[4] = {8, 8, SB_X(2048), systemStatus[0] ? 2048 : 1792};

    /* 0x5DC00BF00 at 4:3 */
    gif_SetGsReg(6, 0x3F00 | SB_TEX0_W(128) | 0x5C0000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetAlpha(1, 7, 0);
    gif_SpriteSensitiveOrg(rect, 0, suv, &blurCol, 1);
}

static void copyCurrentFBToFeedBackArea(void)
{
    /* the August preview does not clear the feedback area's bottom strip */
    if (!ICO_AUG6JUN26 && systemStatus[0] == 0) {
        SprUV a = {-SB_X(1024), 768, SB_X(2048), 256};

        gif_SetDrawEnviroment(0x3F00, 0, SB_X(128), 128, 0, 0);

        gif_SetAlpha(0, 2, 0);
        gif_SpriteSensitiveOrg(&a, 0, (void *)0, &clearCol, 0);
    }
    {
        /* FRAME UV (SCENE), WORK rect (the feedback area) */
        int suv[4] = {8, 8, gsFrame.w * 16, ScreenHeight * 16};
        int srect[4] = {-SB_X(1024), -1024, SB_X(2048), systemStatus[0] ? 2048 : 1792};

        gif_SetGsReg(6, SB_TEX0_W(512) | 0x640000800LL);

        gif_SetDrawEnviroment(0x3F00, 0, SB_X(128), 128, 0, 0);

        gif_SetAlpha(0, 2, 0);
        gif_SpriteSensitiveOrg(srect, 0, suv, &whiteCol, 0);
    }
}

static void blendWork0ToWork1(void)
{
    /* WORK UV (the taps, W / 4), FRAME rect (the aura's buffer) */
    SprUV a = {8, 8, SB_X(2048), 2048};
    int srect[4] = {-gsFrame.w / 2 * 16, -ScreenHeight / 2 * 16, gsFrame.w * 16, ScreenHeight * 16};

    gif_SetGsReg(6, workBase[0] | SB_TEX0_W(128) | 0x5C0000000LL);

    gif_SetDrawEnviroment(workBase[1], 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetAlpha(1, 0, blurCol.f[3]);
    gif_SpriteSensitiveOrg(srect, 0, &a, &blurCol, 1);
}

static void copyFeedBackAreaToWork0(void) /* derived name */
{
    SprUV a = {-SB_X(1024), -1024, SB_X(2048), 2048};

    gif_SetDrawEnviroment(workBase[0], 0, SB_X(128), 128, 0, 0);

    gif_SetAlpha(0, 2, 0);
    gif_SpriteSensitiveOrg(&a, 0, (void *)0, &clearCol, 0);
}

/* PC port: x is the tap's DELTA from the buffer's left edge -1024 (WORK),
   +-10/16 px in the buffer's pixels */
static void addTap(int x, int y) /* derived name */
{
    SprUV a = {8, 8, SB_X(2048), 2048};
    int srect[4] = {-SB_X(1024) + (x + 1024), y, SB_X(2048), 2048};

    /* 0x5DC00BF00 at 4:3 */
    gif_SetGsReg(6, 0x3F00 | SB_TEX0_W(128) | 0x5C0000000LL);

    gif_SetDrawEnviroment(workBase[0], 0, SB_X(128), 128, 0, 0);

    gif_SetAlpha(1, 0, 32);
    gif_SpriteSensitiveOrg(srect, 0, &a, &whiteCol, 1);
}

static void parallelAddFeedBackAreaToWork0(void)
{
    addTap(-1014, -1014);
    addTap(-1014, -1034);
    addTap(-1034, -1014);
    addTap(-1034, -1034);
}

static void blurBlendFeedBackAreaToWork1(void)
{
    copyFeedBackAreaToWork0();
    parallelAddFeedBackAreaToWork0();
    blendWork0ToWork1();
}

static void pasteWork0ToFeedBackArea(int *uv) /* derived name */
{
    int r[4];

    sbWorkRect(r, -work0ToFeedBackRect[0], 0, work0ToFeedBackRect[1], work0ToFeedBackRect[2],
               work0ToFeedBackRect[3]);
    gif_SetGsReg(6, workBase[1] | SB_TEX0_W(512) | 0x640000000LL);

    gif_SetDrawEnviroment(0x3F00, 0, SB_X(128), 128, 0, 0);

    gif_SetAlpha(0, 2, 0);
    gif_SpriteSensitiveOrg(r, 0, uv, &whiteCol, 0);
}

static void pasteFeedBackAreaToWork0(void) /* derived name */
{
    int r[4];

    sbWorkRect(r, -feedBackToWork0Rect[0], 0, feedBackToWork0Rect[1], feedBackToWork0Rect[2],
               feedBackToWork0Rect[3]);
    gif_SetDrawEnviroment(0x3F00, 0, SB_X(128), 128, 0, 0);

    gif_SetAlpha(0, 2, 0);
    gif_SpriteSensitiveOrg(r, 0, (void *)0, &alphaCopyCol, 0);
}

static void pasteWork1ToFB(int *rect, int *uv) /* derived name */
{
    gif_SetGsReg(6, workBase[1] | SB_TEX0_W(512) | 0x640000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetAlpha(1, 5, 0);
    gif_SpriteSensitiveOrg(rect, 0, uv, &alphaCopyCol, 1);
}

static void testAA(int *rect)
{
    int srect[4] = {-gsFrame.w / 5 * 16, -ScreenHeight / 5 * 16, gsFrame.w / 4 * 16,
                    ScreenHeight / 4 * 16};

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    {
        unsigned char c1[4] = {0, 0, 0, 0};
        unsigned char c2[4] = {0, 0, 0, 128};

        gif_SetAlpha(1, 5, 0);
        gif_SpriteSensitiveOrg(rect, 0, (void *)0, c1, 1);
        gif_SpriteSensitiveOrg(srect, 0, (void *)0, c2, 1);
    }
}

static void addWork1ToFB(int *rect, int *uv) /* derived name */
{
    gif_SetGsReg(6, workBase[1] | SB_TEX0_W(512) | 0x640000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetAlpha(1, 0, 128);
    gif_SpriteSensitiveOrg(rect, 0, uv, &whiteCol, 1);
}

static void addWork1ToFBWithZ(int *rect, int *uv) /* derived name */
{
    gif_SetGsReg(6, workBase[1] | SB_TEX0_W(512) | 0x640000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetAlpha(1, 0, 128);
    gif_SetGsReg(0x47, 0x34000);
    gif_SpriteSensitiveOrg(rect, 0, uv, &whiteCol, 1);
    gif_SetZTest(0);
}

static void subWork1ToCurrentFB(int *rect, int *uv)
{
    float v[4];

    gif_SetZTest(1);
    gif_SetZWrite(0);

    gif_SetGsReg(6, workBase[1] | SB_TEX0_W(512) | 0x640000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    subWork1Point[2] = auraInspireZ;
    _ApplyMatrix(v, matrixptr + 192, subWork1Point);

    gif_SetAlpha(1, 1, 128);
    gif_SpriteSensitiveOrg(rect, (int)(v[2] * 16.0f / v[3]), uv, &whiteCol, 1);

    gif_SetZTest(0);
    gif_SetZWrite(0);
}

static void auraInspireAfter(int mode)
{
    /* halfRect and halfUv are read only by the DEBUG build's half-height
       preview of work buffer 1 */
    int halfRect[4] = {-gsFrame.w / 2 * 16, -ScreenHeight / 4 * 16, gsFrame.w * 16,
                       ScreenHeight / 2 * 16};
    int uv[4] = {8, 8, gsFrame.w * 16, ScreenHeight * 16};
    int rect[4] = {-gsFrame.w / 2 * 16, -ScreenHeight / 2 * 16, gsFrame.w * 16, ScreenHeight * 16};
    int halfUv[4] = {8, 28, gsFrame.w * 16, ScreenHeight / 2 * 16};

    SB_KIND(RD_POST_AURA);
    gif_StartPacketPri(8);
    gif_SetGsReg(0x4A, 0);
    gif_SetGsReg(8, 5);
    gif_SetGsReg(0x14, 0x60);

    gif_SetZTest(0);
    gif_SetZWrite(0);

#ifdef DEBUG
    if (mode == 4) {
        gif_SetGsReg(6, workBase[1] | SB_TEX0_W(512) | 0x640000000LL);
        gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);
        gif_SetAlpha(0, 2, 0);
        gif_SpriteSensitiveOrg(halfRect, 0, halfUv, &whiteCol, 0);
    }
#endif
    switch (mode) {
    case 2:
        reduceCopyAlphaChannelOfWork1ToWork0();
        copyAlphaChannelOfWork0ToFeedBackArea();
        pasteFeedBackAreaToFB(rect);
        if (systemStatus[5] == 0 || ico_photo_active() || ico_photo_left()) {
            copyCurrentFBToFeedBackArea();
        }
        break;
    case 1:
        blurBlendFeedBackAreaToWork1();
        addWork1ToFB(rect, uv);
        if (systemStatus[5] == 0 || ico_photo_active() || ico_photo_left()) {
            pasteWork0ToFeedBackArea(uv);
        }
        break;
    case 3:
        pasteWork1ToFB(rect, uv);
        blurBlendFeedBackAreaToWork1();
        addWork1ToFBWithZ(rect, uv);
        if (systemStatus[5] == 0 || ico_photo_active() || ico_photo_left()) {
            pasteWork0ToFeedBackArea(uv);
        }
        break;
    }
    if (!ICO_NTSCAUG6JUN26) {
        if (GlobalTimer) {
            pasteFeedBackAreaToWork0();
        }
    }

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_EndPacket();
}

static void cleanUpFB(int *rect)
{
    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 5, 0);
    gif_SpriteSensitiveOrg(rect, 0, (void *)0, &clearCol, 1);

    gif_SetZTest(1);
    gif_SpriteSensitiveOrg(rect, 1, (void *)0, &fillZCol, 1);
    gif_SetAlpha(0, 4, 0);
    gif_SetZWrite(1);
    gif_SetGsReg(0x47, 0x5000D);
}

static void fillWork2(int mode, int *rect)
{
    gif_SetZTest(0);
    gif_SetZWrite(0);

    /* the 2001 builds clear no strip past work buffer 2 */
    if (!ICO_NTSCAUG6JUN26) {
        unsigned char col[4] = {0, 0, 0, 192};
        SprUV r = {-SB_X(2048), -512, SB_X(4096), 1024};

        gif_SetAlpha(1, 2, 128);
        /* ADDRESS: the block past work buffer 2, the PS2's; WORK: W / 2 x 64 */
        gif_SetDrawEnviroment(SB_WORK2_PAD_FBP, 0, SB_X(256), 64, 0, 0);

        gif_SpriteSensitiveOrg(&r, 0, (void *)0, col, 1);
    }
    {
        unsigned char col2[4] = {0, 0, 0, 192};

        if (mode & 2) {
            col2[0] = col2[1] = col2[2] = 128;
        }
        gif_SetAlpha(1, 2, 0);
        gif_SetDrawEnviroment(workBase[2], 0, gsFrame.w, ScreenHeight, 0, 0);

        gif_SpriteSensitiveOrg(rect, 0, (void *)0, col2, 0);
    }
    gif_SetZWrite(1);
    gif_SetZTest(1);
}

static void dispSun(void)
{
    if (0.0f < sunView[2] && -gsFrame.w < sunScreen[0] && sunScreen[0] < gsFrame.w &&
        -ScreenHeight < sunScreen[1] && sunScreen[1] < ScreenHeight) {
        float v[4];

        sceVu0ITOF0Vector(v, sunScreen);
        gif_StartPacketPri(7);
        gif_SetAlpha(1, 5, 0);
        gif_EndPacket();

        prim_SetFan2D(sunGlowFan, 40.0f, v, 0x505050C0U, 0xC0U);
        prim_DispFan2D(sunGlowFan, 0);
        prim_SetFan2D(sunCoreFan, 15.0f, v, 0xFFFFFFC0U, 0xFFFFFFC0U);
        prim_DispFan2D(sunCoreFan, 0);
    }
}

static void pasteBackLightShadowToFB(int *rect)
{
    gif_SetGsReg(6, workBase[2] | SB_TEX0_W(512) | 0x640000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 2, 96);
    gif_SetGsReg(0x47, 0x30815);

    {
        int suv[4] = {8, 8, gsFrame.w * 16 - 8, ScreenHeight * 16 - 8};
        SprCol col = {{128, 128, 128, 128}};

        gif_SpriteSensitiveOrg(rect, 0, suv, &col, 1);
    }
    gif_SetZWrite(1);
    gif_SetGsReg(0x47, 0x5000D);
}

static void makeMaskPatternToWork2(int *rect, int *uv)
{
    gif_SetGsReg(6, SB_TEX0_W(512) | 0x640000800LL);
    gif_SetGsReg(0x14, 0x60);

    gif_SetDrawEnviroment(workBase[2], 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(0, 4, 0);
    gif_SetGsReg(0x47, 0x30815);

    gif_SpriteSensitiveOrg(rect, 0, uv, &alphaCopyCol, 0);

    gif_SetZWrite(1);
    gif_SetGsReg(0x47, 0x5000D);
}

void makeFullScreenFlareBefore(int mode)
{
    int rect[4] = {-gsFrame.w / 2 * 16, -ScreenHeight / 2 * 16, gsFrame.w * 16, ScreenHeight * 16};

    int uv[4] = {0, 0, gsFrame.w * 16, ScreenHeight * 16};

    SB_KIND(SB_FLARE_KIND(mode));
    gif_StartPacketPri(7);

    gif_SetGsReg(8, 5);

    cleanUpFB(rect);

    fillWork2(mode, rect);
    gif_EndPacket();

    if (sunOn) {
        dispSun();
    }

    gif_StartPacketPri(7);
    makeMaskPatternToWork2(rect, uv);
    gif_EndPacket();
}

static int eyeBlurAlpha = 150; /* derived name */

static void reduceWork2ToWork0(void)
{
    gif_SetGsReg(6, workBase[2] | SB_TEX0_W(512) | 0x640000000LL);
    gif_SetGsReg(0x14, 0x60);

    gif_SetDrawEnviroment(workBase[0], 0, SB_X(256), 128, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    {
        SprUV r = {-SB_X(2048), -1024, SB_X(4096), 2048};

        int uv[4] = {64, 64, gsFrame.w * 16, ScreenHeight * 16};
        SprCol col = {{128, 128, 128, 128}};

        gif_SpriteSensitiveOrg(&r, 0, uv, &col, 0);
    }
    gif_SetZWrite(1);
    gif_SetGsReg(0x47, 0x5000D);
}

static void eyeBlur(int alpha, SprCol *col, int mode)
{
    int rect[4] = {-gsFrame.w / 2 * 16, -ScreenHeight / 2 * 16, gsFrame.w * 16, ScreenHeight * 16};

    SB_KIND(RD_POST_EYE_BLUR);
    gif_SetGsReg(0x14, 0x60);

    gif_SetDrawEnviroment(workBase[3], 0, SB_X(256), 128, 0, 0);

    gif_SetGsReg(6, workBase[0] | SB_TEX0_W(256) | 0x5C0000000LL);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    {
        SprUV a = {-SB_X(2048), -1024, SB_X(4096), 2048};

        SprUV b = sbWorkBufferUv();

        unsigned char c[4] = {alpha, alpha, alpha, 128};

        gif_SetAlpha(1, 7, 0);
        gif_SpriteSensitiveOrg(&a, 0, &b, c, 0);
    }

    if ((mode & 2) == 0 && sunOn && 0.0f < sunView[2] && -gsFrame.w < sunScreen[0] &&
        sunScreen[0] < gsFrame.w && -ScreenHeight < sunScreen[1] && sunScreen[1] < ScreenHeight) {
        int arr[4];
        int ghostRect[4];
        int shrinkRect[4];
        int tintRect[4];
        SprCol c1;
        SprCol c2;
        SprUV d;
        int i;

        gif_SetGsReg(0x14, 0x60);

        gif_SetDrawEnviroment(workBase[3], 0, SB_X(256), 128, 0, 0);

        gif_SetGsReg(8, 5);

        /* PC port: WORK, the buffers W / 2 across; the sun's position in
           them is over the frame's width (x0) and the +16 and -1 are DELTAS */
        sbWorkRect(ghostRect, -eyeGhostRect[0], 0, eyeGhostRect[1], eyeGhostRect[2],
                   eyeGhostRect[3]);
        sbWorkRect(shrinkRect, -eyeShrinkRect[0], 0, eyeShrinkRect[1], eyeShrinkRect[2],
                   eyeShrinkRect[3]);
        sbWorkRect(tintRect, -eyeTintRect[0], 0, eyeTintRect[1], eyeTintRect[2], eyeTintRect[3]);
        for (i = 1; i < 5; i++) {
            int x0 = (sunScreen[0] + gsFrame.w / 2) * SB_X(4096) / gsFrame.w;
            int y0 = (sunScreen[1] + ScreenHeight / 2) * 2048 / ScreenHeight;
            int x = x0 * i / 5;
            int y = y0 * i / 5;
            int x1 = SB_X(4096) + 16 - (SB_X(4096) - x0) * i / 5;
            int y1 = 2064 - (2048 - y0) * i / 5;

            arr[0] = x;
            arr[1] = y;
            arr[2] = x1 - x;
            arr[3] = y1 - y;
            c1 = (SprCol){{128, 128, 128, 128}};

            if (0 < x && x < SB_X(4096) - 1 && 0 < y && y < 4095 && 0 < x1 && x1 < SB_X(4096) - 1 &&
                0 < y1 && y1 < 4095) {
                gif_SetAlpha(1, 0, alpha / (i + 1));
                gif_SpriteSensitiveOrg(ghostRect, 0, arr, &c1, 1);
            }
        }

        gif_SetGsReg(0x14, 0x60);

        gif_SetGsReg(6, workBase[3] | SB_TEX0_W(256) | 0x5C0000000LL);

        gif_SetDrawEnviroment(workBase[1], 0, SB_X(256), 128, 0, 0);
        {
            int x0 = (sunScreen[0] + gsFrame.w / 2) * SB_X(4096) / gsFrame.w;
            int y0 = (sunScreen[1] + ScreenHeight / 2) * 2048 / ScreenHeight;
            int px = x0 * 0.9f;
            int py = y0 * 0.9f;
            int px1 = (float)SB_X(4096) - (SB_X(4096) - x0) * 0.9f;
            int py1 = 2048.0f - (2048 - y0) * 0.9f;

            arr[0] = px;
            arr[1] = py;
            arr[2] = px1 - px;
            arr[3] = py1 - py;
            c2 = (SprCol){{128, 128, 128, 128}};

            gif_SetAlpha(1, 4, 0);
            gif_SpriteSensitiveOrg(shrinkRect, 0, arr, &c2, 0);
        }

        gif_SetGsReg(0x14, 0x40);

        gif_SetGsReg(6, workBase[1] | SB_TEX0_W(256) | 0x5C0000000LL);

        gif_SetDrawEnviroment(workBase[3], 0, SB_X(256), 128, 0, 0);
        /* the centre of the shrunk sun, (W / 16 + 0.5, 16.5) in Work1 */
        d = (SprUV){SB_X(512) + 8, 264, 0, 0};
        {
            int rate = 10;
            unsigned char cc[4] = {(col->f[0] - 128) * (col->f[3] * rate) / 128,
                                   (col->f[1] - 128) * (col->f[3] * rate) / 128,
                                   (col->f[2] - 128) * (col->f[3] * rate) / 128, col->f[3] + 128};

            gif_SetAlpha(1, 5, 0);
            gif_SpriteSensitiveOrg(tintRect, 0, &d, cc, 1);
        }

        if (mode & 1) {
            gif_SetGsReg(0x14, 0x40);

            gif_SetGsReg(6, workBase[1] | SB_TEX0_W(256) | 0x5C0000000LL);

            gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

            gif_SetZTest(0);
            gif_SetZWrite(0);
            gif_SetAlpha(1, 1, 96);
            gif_SetGsReg(0x47, 0x34003);
            {
                SprUV e = {SB_X(512) + 8, 264, 0, 0};

                gif_SpriteSensitiveOrg(rect, 0, &e, &whiteCol, 1);
            }
            gif_SetZWrite(1);
            gif_SetGsReg(0x47, 0x5000D);
        }

        gif_SetGsReg(0x14, 0x60);
        gif_SetZWrite(1);
        gif_SetZTest(1);
    }
}

static void pasteWork0ToFB(void) /* derived name */
{
    SB_KIND(postMode == 4 || postMode == 5 ? RD_POST_BLOOM : RD_POST_FLARE);
    gif_SetDrawEnviroment(workBase[0], 0, SB_X(256), 128, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 0, 0);
    {
        SprUV r = {-SB_X(2048), -1024, SB_X(4096), 2048};

        gif_SpriteSensitiveOrg(&r, 0, (void *)0, &alphaCopyCol, 1);
    }
    gif_SetZWrite(1);
    gif_SetZTest(1);
}

void makeFullScreenFlareAfter(int mode)
{
    SB_KIND(SB_FLARE_KIND(mode));
    gif_StartPacketPri(7);

    reduceWork2ToWork0();

    gif_SetZTest(0);
    gif_SetZWrite(0);
    {
        SprCol col = flareCol;
        int i;

        if ((mode & 2) == 0) {
            col.f[3] = 0;
        } else {
            col.f[3] = col.f[3] / 10;
        }
        for (i = 0; i < 10; i++) {
            blur(i + 10, &col);
        }
    }
    gif_SetZTest(1);
    gif_SetZWrite(1);
    {
        SprCol col = flareCol;

        eyeBlur(eyeBlurAlpha, &col, mode);
    }

    pasteWork0ToFB();

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, PAL_NTSCAUG6JUN26(0, 1), 0);

    gif_EndPacket();
}

static void pasteFullScreenFlare(void)
{
    int rect[4] = {-gsFrame.w / 2 * 16, -ScreenHeight / 2 * 16, gsFrame.w * 16, ScreenHeight * 16};
    SprUV uv;
    SprCol col;

    SB_KIND(postMode == 4 || postMode == 5 ? RD_POST_BLOOM : RD_POST_FLARE);
    gif_StartPacketPri(7);

    gif_SetGsReg(6, workBase[3] | SB_TEX0_W(256) | 0x5C0000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 0, 128);

    uv = sbWorkBufferUv();
    col = (SprCol){{128, 128, 128, 128}};
    gif_SpriteSensitiveOrg(rect, 0, &uv, &col, 1);

    gif_SetZWrite(1);
    gif_SetGsReg(0x47, 0x5000D);

    gif_EndPacket();
}

static SprCol copyToWorkCol = {{128, 128, 128, 0}}; /* derived name */

static SprCol copyToWork2Col = {{128, 128, 128, 0}}; /* derived name */

static SprCol pasteToFBCol = {{128, 128, 128, 128}}; /* derived name */

static SprCol depthPassCol = {{128, 128, 128, 0}}; /* derived name */

/* One blend pass of the depth-of-field chain: shrink the work buffer named by
   `n` into its twin, taking the previous pass's shrink (`pre`) as the source
   rectangle and this pass's (`cur`) as the destination. */
static inline int depthFieldPass(int n, int cur, int pre) /* derived name */
{
    int tex = workBase[n];

    gif_SetDrawEnviroment(workBase[1 - n], 0, SB_X(256), 128, 0, 0);
    gif_SetGsReg(6, tex | SB_TEX0_W(256) | 0x15C0000000LL);

    if (pre < cur) {
        gif_SetGsReg(0x14, 0x40);
    } else {
        gif_SetGsReg(0x14, 0x20);
    }
    gif_SetGsReg(8, 5);
    gif_SetAlpha(1, 4, 0);

    {
        /* WORK: the one-pixel border (16, -32) is a DELTA; the shrinks cur
           and pre of the bordered span (the PS2's 4064) at the PS2's slope,
           anchored at the 4:3 part's left edge (sbShrinkAt); the y shrink
           as the PS2's */
        int rect[4] = {-SB_X(2048) + 16 + sbShrinkAt(cur, 4064), -1008,
                       sbShrinkSpan(SB_X(4096) - 32, cur, 4064), 2016 - cur};
        int uv[4] = {24 + sbShrinkAt(pre, 4064), 24, sbShrinkSpan(SB_X(4096) - 32, pre, 4064),
                     2016 - pre};

        gif_SpriteSensitiveOrg(rect, 0, uv, &depthPassCol, 0);
    }
    return cur;
}

static void copyToWork(void)
{
    gif_SetGsReg(6, SB_TEX0_W(512) | 0x640000800LL);

    gif_SetDrawEnviroment(workBase[1], 0, SB_X(256), 256, 0, 0);

    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(0, 4, 0);
    gif_SetGsReg(0x47, 0);
    gif_SetGsReg(0x14, 0x60);

    {
        int uv[4] = {8, 8, gsFrame.w * 16, 8192};
        int r[4];

        sbWorkRect(r, 2048, copyToWorkRect[0] + 2048, copyToWorkRect[1], copyToWorkRect[2],
                   copyToWorkRect[3]);
        gif_SpriteSensitiveOrg(r, 0, uv, &copyToWorkCol, 0);
    }
    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_SetGsReg(0x47, 0x5000D);
}

static void copyToWork2(void)
{
    gif_SetDrawEnviroment(workBase[0], 0, SB_X(256), 128, 0, 0);
    gif_SetGsReg(6, workBase[1] | SB_TEX0_W(256) | 0x1600000000LL);
    gif_SetGsReg(0x14, 0x60);
    gif_SetAlpha(1, 4, 0);
    gif_SetZTest(0);
    gif_SetZWrite(0);

    {
        SprUV src = {-SB_X(2048) + 4, -1020, SB_X(4096), 2048};
        SprUV dst = {8, 8, SB_X(4096), 4096};

        gif_SpriteSensitiveOrg(&src, 0, &dst, &copyToWork2Col, 0);
    }
}

static void pasteToFB(float z, float rate)
{
    gif_SetGsReg(6, workBase[0] | SB_TEX0_W(256) | 0x5C0000000LL);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetGsReg(0x14, 0x60);
    gif_SetZWrite(0);
    gif_SetGsReg(0x47, 0x50000);
    gif_SetAlpha(1, 2, (int)(rate * 128.0f));

    {
        int rect[4] = {-gsFrame.w / 2 * 16 - 4, -ScreenHeight / 2 * 16 - 4, gsFrame.w * 16,
                       ScreenHeight * 16};
        int uv[4] = {16, 16, SB_X(4096), systemStatus[0] == 0 ? 1792 : 2048};
        float v[4];

        pasteToFBPoint[2] = z;
        _ApplyMatrix(v, matrixptr + 192, pasteToFBPoint);

        if (rate == 1.0f) {
            gif_SpriteSensitiveOrg(rect, (int)(v[2] * 16.0f / v[3]), uv, &pasteToFBCol, 0);
        } else {
            gif_SpriteSensitiveOrg(rect, (int)(v[2] * 16.0f / v[3]), uv, &pasteToFBCol, 1);
        }
    }
    gif_SetGsReg(0x47, 0x5000D);
}

void depthField(float depth, float width, float rate)
{
    int w = 50;
    int cur;
    int pre;
    int i;

    SB_KIND(RD_POST_DOF);
    gif_StartPacketPri(7);
    gif_SetGsReg(8, 5);

    copyToWork();
    copyToWork2();

    gif_SetZTest(0);
    gif_SetZWrite(0);

    pre = 0;
    cur = w;
    pre = depthFieldPass(0, cur, pre);
    cur = w * 4 / 5;
    pre = depthFieldPass(1, cur, pre);
    cur = w * 3 / 5;
    pre = depthFieldPass(0, cur, pre);
    cur = w * 2 / 5;
    pre = depthFieldPass(1, cur, pre);
    cur = w * 1 / 5;
    pre = depthFieldPass(0, cur, pre);
    cur = 0;
    pre = depthFieldPass(1, cur, pre);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    for (i = 1; i < 4; i++) {
        pasteToFB(depth + width * i * 0.25f, i * 0.25f);
    }
    pasteToFB(depth + width, 1.0f);
    if (ICO_NTSC) {
        gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    }

    if (ICO_AUG6JUN26) {
        gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    }
    gif_EndPacket();
}

void GetSunWorldPos(float *pos)
{
    _NormalizeVector(pos, sunDir);
}

static int motionBlurAlpha = 0; /* derived name */

/* frames the motion blur still skips after a non-linear camera move */
static int motionBlurSkip = 0; /* derived name */

static void MotionBlur_aug6jun26(void)
{
    SprCol col = {{128, 128, 128, 128}};
    SprUV uv = {4, 4, gsFrame.w * 16, ScreenHeight / 2 * 16};
    SprUV rect = {-gsFrame.w / 2 * 16 - 4, -ScreenHeight / 2 * 16 - 4, gsFrame.w * 16,
                  ScreenHeight * 16};

    if (motionBlurAlpha == 0) {
        return;
    }
    /* PC port: Options > Effects > Motion blur off */
    if (!ico_video_effect_motion_blur()) {
        return;
    }
    if (motionBlurSkip != 0) {
        motionBlurSkip--;
        return;
    }
    if (NonLinearCameraMove != 0) {
        motionBlurSkip = 1;
    }
    if (debug_font_flag & 1) {
        debug_Printf(300, 40, 0xFFFFFF00, "MBLUR %d", motionBlurAlpha);
    }

    SB_KIND(RD_POST_MOTION_BLUR);
    gif_StartPacketPri(7);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetGsReg(6, SB_TEX0_W(512) | 0x640100000LL);
    gif_SetGsReg(0x3B, 0x8000000080LL);
    gif_SetGsReg(0x47, 0x3000C);

    gif_SetZWrite(0);
    gif_SetAlpha(1, 2, motionBlurAlpha);
    gif_SpriteSensitiveOrg(&rect, -1, &uv, &col, 1);
    gif_SetZWrite(1);

    gif_SetGsReg(0x47, 0x5000D);
    gif_EndPacket();
}

void MotionBlur(void)
{
    if (ICO_AUG6JUN26) {
        MotionBlur_aug6jun26();
        return;
    }
    SprCol col = {{128, 128, 128, 128}};
    SprUV uv = {4, 4, gsFrame.w * 16, ScreenHeight / 2 * 16};
    SprUV rect = {-gsFrame.w / 2 * 16 - 4, -ScreenHeight / 2 * 16 - 4, gsFrame.w * 16,
                  ScreenHeight * 16};

    if (motionBlurAlpha == 0) {
        return;
    }
    /* PC port: Options > Effects > Motion blur off */
    if (!ico_video_effect_motion_blur()) {
        return;
    }
    if (currentScreenWidth != 0) {
        return;
    }
    if (debug_font_flag & 1) {
        debug_Printf(300, 40, 0xFFFFFF00, "MBLUR %d", motionBlurAlpha);
    }

    SB_KIND(RD_POST_MOTION_BLUR);
    gif_StartPacketPri(7);

    gif_SetDrawEnviroment(0x800, 0, gsFrame.w, ScreenHeight, 0, 0);

    gif_SetGsReg(6, SB_TEX0_W(512) | 0x640100000LL);
    gif_SetGsReg(0x3B, 0x8000000080LL);
    gif_SetGsReg(0x47, 0x3000C);

    gif_SetZWrite(0);
    gif_SetAlpha(1, 2, motionBlurAlpha);
    gif_SpriteSensitiveOrg(&rect, -1, &uv, &col, 1);
    gif_SetZWrite(1);

    gif_SetGsReg(0x47, 0x5000D);
    gif_EndPacket();
}

static void calcSun(void)
{
    float buf[4];
    _NormalizeVector(buf, sunDir);
    _ScaleVector(buf, buf, 1000000.0f);
    buf[3] = 1.0f;
    _ApplyMatrix(buf, matrixptr + 256, buf);
    /* PC port: the view matrix is still zero on the stage's first tick, so
       buf[3] is 0; the EE's division gives Fmax, and Fmax * 0 is 0, where
       IEEE gives Inf * 0 = NaN. */
    _ScaleVectorXYZ(buf, buf, ps2_div(1.0f, buf[3]));
    _AddVectorXYZ(buf, buf, sunScreenOffset);
    _FTOI0Vector(sunScreen, buf);
    _ApplyMatrix(sunView, matrixptr + 128, sunDir);
}

static int colorSettingItem = 0; /* derived name */

/* one step of colorSetting, inlined at all four switch arms */
static inline void colorSettingStep(unsigned char *p, int d) /* derived name */
{
    int v = *p;

    v += d;
    if (v < 0) {
        v = 0;
    }
    if (v > 255) {
        v = 255;
    }
    *p = v;
}

static void colorSetting(void)
{
    char buf[256];
    int f = pad[1].rep;
    int d;

    if (f & 0x1000) {
        colorSettingItem--;
    }
    if (f & 0x4000) {
        colorSettingItem++;
    }
    if (colorSettingItem >= 4) {
        colorSettingItem = 0;
    }
    if (colorSettingItem < 0) {
        colorSettingItem = 3;
    }
    d = 0;
    if (f & 0x2000) {
        d = 1;
    }
    if (f & 0x8000) {
        d = -1;
    }

    switch (colorSettingItem) {
    case 0:
    default:
        sprintf(buf, "BLUR R: %d", flareCol.f[0]);
        colorSettingStep(&flareCol.f[0], d);
        break;
    case 1:
        sprintf(buf, "BLUR G: %d", flareCol.f[1]);
        colorSettingStep(&flareCol.f[1], d);
        break;
    case 2:
        sprintf(buf, "BLUR B: %d", flareCol.f[2]);
        colorSettingStep(&flareCol.f[2], d);
        break;
    case 3:
        sprintf(buf, "BLUR BASE: %d", flareCol.f[3]);
        colorSettingStep(&flareCol.f[3], d);
        break;
    }
    if (debug_font_flag & 1) {
        debug_Printf(440, 40, 0xFFFFFF00, buf);
    }
}

static int postInfoTimer = 0; /* derived name */

static int postInfoLast = -1; /* derived name */

static void dispPostInfo(void)
{
    char buf[256];

    if (postInfoLast != postMode) {
        postInfoTimer = 0;
    }
    postInfoLast = postMode;
    if (postInfoTimer < 60) {
        postInfoTimer++;
    }
    if (((postInfoTimer >> 1) & 1) == 0) {
        switch (postMode) {
        case 0:
            sprintf(buf, "NOEFFECT");
            break;
        case 1:
            sprintf(buf, "SBLUR");
            break;
        case 2:
            sprintf(buf, "DEPTH");
            break;
        case 3:
            sprintf(buf, "SBLUR+DEPTH");
            break;
        case 4:
            sprintf(buf, "GLOW");
            break;
        case 5:
            sprintf(buf, "GLOW+DEPTH");
            break;
        case 6:
            sprintf(buf, "BLSBLUR");
            break;
        case 7:
            sprintf(buf, "BLSBLUR+DEPTH");
            break;
        case 8:
            sprintf(buf, "NO ACTION");
            break;
        }
        if (debug_font_flag & 1) {
            debug_Printf(440, 20, 0xFFFFFF00, "BLUR: %s", buf);
        }
    }
}

static int feedInfoTimer = 0; /* derived name */

static int feedInfoLast = -1; /* derived name */

static void dispFeedInfo(void)
{
    char buf[256];

    if (feedInfoLast != feedMode) {
        feedInfoTimer = 0;
    }
    feedInfoLast = feedMode;
    if (feedInfoTimer < 60) {
        feedInfoTimer++;
    }
    if (((feedInfoTimer >> 1) & 1) == 0) {
        switch (feedMode) {
        default:
            sprintf(buf, "NOEFFECT");
            break;
        case 1:
            sprintf(buf, "AURA");
            break;
        case 2:
            sprintf(buf, "MIRAGE");
            break;
        case 3:
            sprintf(buf, "AURA V2");
            break;
        }
        if (debug_font_flag & 1) {
            debug_Printf(440, 30, 0xFFFFFF00, "FEED: %s", buf);
        }
    }
}

/* PC port: the post mode with Options > Effects' Glow or Depth
 * of field off.  The modes are 1, 4, 6 flare (or bloom) alone, 2 depth of
 * field alone, 3, 5, 7 flare (or bloom) and depth of field, 8 none.  Glow
 * off drops the flare half (1, 4, 6 -> 8; 3, 5, 7 -> 2), then Depth of field
 * off drops the depth half (2 -> 8; 3 -> 1, 5 -> 4, 7 -> 6).  Each half is
 * self-contained: makeFullScreenFlareBefore's FB alpha (cleanUpFB) feeds
 * only its own mask and paste, depthField makes its own copies of the
 * frame and reads its alpha nowhere in its colour, and the latched postMode
 * keeps FullScreenEffectAfter in step with this frame's Before.  What the
 * frame's alpha holds afterwards (read by the aura's feedback mode 3
 * through DATE) is then the scene's own, as in a stage without the flare. */
static int sbEffectMode(int m)
{
    if (!ico_video_effect_glow()) {
        if (m == 1 || m == 4 || m == 6) {
            m = 8;
        } else if (m == 3 || m == 5 || m == 7) {
            m = 2;
        }
    }
    if (!ico_video_effect_depth_of_field()) {
        if (m == 2) {
            m = 8;
        } else if (m == 3) {
            m = 1;
        } else if (m == 5 || m == 7) {
            m = m - 1;
        }
    }
    return m;
}

void FullScreenEffectBefore(void)
{
    if (debug_fullscreen_effect == 0) {
        return;
    }

    postModeRequest = sbEffectMode(GlobalStageSetting.postEffect);
    feedModeRequest = GlobalStageSetting.feedbackEffect;

    blurCol.f[0] = GlobalStageSetting.feedbackCol[0];
    blurCol.f[1] = GlobalStageSetting.feedbackCol[1];
    blurCol.f[2] = GlobalStageSetting.feedbackCol[2];
    blurCol.f[3] = GlobalStageSetting.feedbackCol[3];

    if (postMode != postModeRequest) {
        postMode = postModeRequest;
    }
    if (feedMode != feedModeRequest) {
        feedMode = feedModeRequest;
    }

    dispPostInfo();
    dispFeedInfo();
#ifdef ICO_RD
    /* PC port: which effects run, logged when that changes (the
       count is FullScreenEffectBefore's calls, one per frame; "drawn" says
       whether MotionBlur drew in the frame before, which GlobalTimer
       frames skip, and is not a change of its own) */
    {
        static int seen[5] = {-1, -1, -1, -1, -1};
        static unsigned int calls;
        int now[5] = {postMode, feedMode, flareOn, sunOn, motionBlurAlpha};

        calls++;
        if (memcmp(now, seen, sizeof(now)) != 0) {
            fprintf(stderr,
                    "staticBlur: frame %u: postEffect %d feedbackEffect %d colour %d %d %d %d "
                    "flare %d sun %d motionBlur %d (drawn %d) depthField %d/%d\n",
                    calls, postMode, feedMode, (int)GlobalStageSetting.feedbackCol[0],
                    (int)GlobalStageSetting.feedbackCol[1], (int)GlobalStageSetting.feedbackCol[2],
                    (int)GlobalStageSetting.feedbackCol[3], flareOn, sunOn, motionBlurAlpha,
                    sbMotionDrawn, GlobalStageSetting.depthFieldStart,
                    GlobalStageSetting.depthFieldWidth);
            memcpy(seen, now, sizeof(now));
        }
        sbMotionDrawn = 0;
    }
#endif

    if (sunOn)
        if (debug_font_flag & 1)
            debug_Printf(250, 40, 0xFFFFFF00, "SUN");

    if (ICO_AUG6JUN26) {
        colorSetting(); /* the August preview sets the effect colours each frame here */
    }
    workBase[0] = 0x2800;
    workBase[1] = 0x2A00;
    workBase[2] = 0x2E00;
    workBase[3] = 0x3000;

    if (!ICO_NTSCAUG6JUN26) {
        if (ICO_JPNKORCN) {
            tex_LockHeadTBP(0x3800, 8);
        } else {
            if (systemStatus[0] == 1) {
                tex_LockHeadTBP(0x3A00, 8);
            } else {
                tex_LockHeadTBP(0x3800, 8);
            }
        }
    }

    if (sunOn) {
        calcSun();
    }

    switch (postMode) {
    case 1:
    case 3:
        if (flareOn) {
            makeFullScreenFlareBefore(0);
        }
        break;
    case 4:
    case 5:
        if (flareOn) {
            makeFullScreenFlareBefore(2);
        }
        break;
    case 6:
    case 7:
        if (flareOn) {
            makeFullScreenFlareBefore(1);
        }
        break;
    case 2:
    case 8:
        break;
    }

    if (feedMode) {
        auraInspireBefore();
    }
}

void FullScreenEffectAfter(void)
{
    if (debug_fullscreen_effect == 0) {
        return;
    }

    switch (postMode) {
    case 1:
        if (flareOn) {
            makeFullScreenFlareAfter(0);
            pasteFullScreenFlare();
        }
        break;
    case 2:
        depthField(depthFadeStart, depthFadeWidth, 1.0f);
        break;
    case 3:
        if (flareOn) {
            makeFullScreenFlareAfter(0);
        }
        depthField(depthFadeStart, depthFadeWidth, 1.0f);
        if (flareOn) {
            pasteFullScreenFlare();
        }
        break;
    case 4:
        if (flareOn) {
            makeFullScreenFlareAfter(2);
            pasteFullScreenFlare();
        }
        break;
    case 5:
        if (flareOn) {
            makeFullScreenFlareAfter(2);
        }
        depthField(depthFadeStart, depthFadeWidth, 1.0f);
        if (flareOn) {
            pasteFullScreenFlare();
        }
        break;
    case 6:
        if (flareOn) {
            makeFullScreenFlareAfter(1);
            pasteFullScreenFlare();
        }
        break;
    case 7:
        if (flareOn) {
            makeFullScreenFlareAfter(1);
        }
        depthField(depthFadeStart, depthFadeWidth, 1.0f);
        if (flareOn) {
            pasteFullScreenFlare();
        }
        break;
    case 8:
        break;
    }

    if (feedMode) {
        auraInspireAfter(feedMode);
    }

    depthFadeStart = GlobalStageSetting.depthFieldStart;
    depthFadeWidth = GlobalStageSetting.depthFieldWidth;

    if (!ICO_NTSCAUG6JUN26) {
        tex_UnlockHeadTBP(7);
        tex_UnlockHeadTBP(8);
    }
}

/* declared here: matrixDrive.h is not included */
extern float ZeroPoint[4];
extern void CopyVector(float *dst, float *src);

/* a file-static copy of _initStaticBlur, which InitStaticBlur inlines;
   _initStaticBlur keeps its own body, which holds 80.0f across both calls */
static inline void initStaticBlur(void) /* derived name */
{
    sunGlowFan = prim_InitFan2D(16, 80.0f, ZeroPoint, 0xFFFFFF80u, 0);
    sunCoreFan = prim_InitFan2D(16, 80.0f, ZeroPoint, 0xFFFFFF80u, 0);
}

/* No caller exists.  The binary copies sunDir from a1 as it arrives, so the
   direction is the second parameter; a0 is not read. */
int InitStaticBlur(int unused, float *dir)
{
    CopyVector(sunDir, dir);
    sunDir[3] = 0;
    initStaticBlur();
    sunOn = 1;
    return 0;
}

void StaticBlur(void) {}

void StaticBlurDL(void) {}

void SetMotionBlur(int val)
{
    motionBlurAlpha = val;
}

void SetStaticBlur(int x)
{
    GlobalStageSetting.postEffect = x;
}

void SetDepthFadeParam(float start, float width, int level)
{
    GlobalStageSetting.depthFieldStart = (int)start;
    GlobalStageSetting.depthFieldWidth = (int)width;
    GlobalStageSetting.depthFieldLevel = level;
}

void SetAuraInspireParam(float z)
{
    auraInspireZ = z;
}

void InitializeStaticBlur(void)
{
    sunOn = 0;
}

void _initStaticBlur(void)
{
    sunGlowFan = prim_InitFan2D(16, 80.0f, ZeroPoint, 0xFFFFFF80u, 0);
    sunCoreFan = prim_InitFan2D(16, 80.0f, ZeroPoint, 0xFFFFFF80u, 0);
}

void SetAuraEffect(void) {}
