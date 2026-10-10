#include "debug.h"
#include "Basic.h"
#include "GsBase.h"
#include "Packet.h"
#include "RegistPacket.h"
#include "Shadow.h"
#include "StageAnimation.h"
#include "Texture.h"
#include "staticBlur.h"
#include <libvu0.h>
#include <eekernel.h>
#include <sifdev.h>
#include <stdio.h>
#include "typedef.h"
#include "gflag.h"
#include <eeregs.h>
#include "layout_texture.h"
#include "staffroll.h"
#include "camera-root.h"
#include <eekernel.h>
#include "ZFog.h"
#include "Light.h"
#include "Matrix.h"
#include "DmaPacket.h"
#include "DisplayList.h"
#include "main.h"
#include <libgraph.h>
#include <libcdvd.h>

#ifdef ICO_RD

#include "GifHost.h"
#include "rd.h"

#endif

/* string.h: memset's count is a size_t, and strlen and strcmp (the tool
   lock and log files) have no declaration otherwise, so their results
   would be read as int */
#include <string.h>
/* PC port: the presenter's blended camera (slerped rotation, lerped eye),
   shared with port/render/rd_interp.c so the cull below builds the same
   half-way camera the pictures are drawn with; header only, in every
   build */
#include "../../../port/render/rd_camera_blend.h"

/* GsBase.c's globals, tentative definitions: the frame buffer flags, the
   screen centre and size, then the XYOFFSET adjustment and the frame size
   gsb_SetVSMatrix records. */
int fbKeep;

int fbClear;

float center_X;

float center_Y;

int ScreenWidth;

int ScreenHeight;

int currentScreenWidth;

int currentScreenHeight;

int screenOffsetX; /* derived name */

int screenOffsetY; /* derived name */

int vsWidth; /* derived name */

int vsHeight; /* derived name */

/* The stage lock state, the word gsb_Init clears (nothing reads it), the GS
   system flag, the zoom easing (target, current, speed) and the last
   projection distance gsb_SetVSMatrix was given. */
static int otherEditingLocked = 0; /* derived name */

static int editingSettings = 0; /* derived name */

static int gsInitState = 0; /* derived name */

static int gsSystemReady = 0; /* derived name */

static float zoomTarget = 1.0f; /* derived name */

static float zoomCurrent = 1.0f; /* derived name */

static float zoomSpeed = 1000.0f; /* derived name */

int currentFocusDistance = 1;

/* PC port (photo mode, issue 14): the projection distance of the last
   full-screen gsb_SetVSMatrix, as a float (currentFocusDistance keeps only
   its integer part, and the reflections' calls in puddle.c and pool.c pass
   their own sizes); gsb_ViewFocus reads it.  Recorded only: nothing of the
   game reads it. */
static float gsbViewD = 0.0f;

#ifdef ICO_RD

/* PC port (R2c): the frame lifecycle, camera and VU block on rd; defined
   after gsb_PostEffect */
static void gsbHostResetHalf(void);
static void gsbHostCommon(void);

#endif

/* PC port (renderer wave 7, R7a): the widescreen cull hook, in every port
   build (the headless one too, so a headless run proves the widened cull
   leaves the game's logic alone); defined after gsb_SetVSMatrix */
static void gsbHostWidenCull(float *projHalf);
/* PC port: the cull against the cameras of the blended pictures, defined
   with gsb_ClipBox: the camera gsb_MakeCommonMatrix built, the camera at
   each flip, and forgetting it at a stage's start */
static void gsbHostCullBuilt(void);
static void gsbHostCullFlip(void);
static void gsbHostCullForget(void);
/* port/game/video_options.c: the presentation's aspect / (4/3), 1 in the
   Original preset */
extern float ico_video_wide_x(void);
/* port/game/video_options.c: 1 when pictures are blended between ticks (a
   framerate other than Original) */
extern int ico_video_interpolate(void);
/* port/game/video_options.c: the count of the game's hard camera cuts and
   stage changes (ico_video_camera_cut) */
extern unsigned ico_video_cut_serial(void);
/* port/game/video_options.c: the Screen softening switch (issue 11), 1 = on */
extern int ico_video_effect_softening(void);
/* port/game/video_options.c: the Cinematic bars switch (issue 27), 1 = on */
extern int ico_video_effect_cinematic_bars(void);

/* Point the double buffer's two display and two draw environments at the
 * frame this stage draws into: the low nine bits of each frame word carry the
 * buffer base in 64-word units and each draw env's ZBUF carries the zbuffer
 * base, its psm nibble and the mask bit. */
void gsb_SetFrame(sceGsDBuff *db, int a1, int a2, int psm, short zbp)
{
    sceGsDispEnv *disp1 = &db->disp[1];
    long long zb = ((long long)zbp << 32) | 0xC0;
    short h = (short)(unsigned short)ScreenHeight / 2;
    short w = ScreenWidth;

    *(int *)&disp1->dispfb &= ~0x1FF;
    *(int *)&db->disp[0].dispfb &= ~0x1FF;
    /* the four register words are rewritten through GifPkWord's union view */
    ((GifPkWord *)&db->draw1.frame)->d = (((GifPkWord *)&db->draw1.frame)->d & ~0x1FF) | 0x40;
    ((GifPkWord *)&db->draw0.frame)->d = (((GifPkWord *)&db->draw0.frame)->d & ~0x1FF) | 0x40;
    ((GifPkWord *)&db->draw1.zbuf)->d = ((long long)(psm & 0xF) << 24) | zb;
    ((GifPkWord *)&db->draw0.zbuf)->d = ((long long)(psm & 0xF) << 24) | zb;
    sceGsSetDefDispEnv(&db->disp[0], 0, w, h, 0, 0);
    sceGsSetDefDispEnv(disp1, 0, w, h, 0, 0);
}

/* Bring the GS up for the frame size the stage record asks for: 512 by 448
 * interlaced, 512 by 512 in the tall mode and 512 by 448 otherwise, then set
 * the double buffer up, hand the first frame over and put the view scale back
 * to one. */
void gsb_Init(void *db)
{
    int omode = 2;

    sceGsSyncV(0);
    fbClear = 1;
    gsInitState = 0;
    switch (systemStatus[0]) {
    case 0:
        ScreenWidth = 0x200;
        ScreenHeight = 0x1C0;
        break;
    case 1:
        ScreenWidth = 0x200;
        ScreenHeight = 0x200;
        omode = 3;
        break;
    }
    sceGsResetGraph(0, systemStatus[1] == 1, omode, 1);
    sceGsSetDefDBuff(db, 0, ScreenWidth, ScreenHeight, 2, 0x30, fbClear);
    gsb_SetFrame(db, 0, 0, 0x30, 2);
    sceGsSyncV(0);
    buffer_ID = 0;
    FlushCache(0);
    sceGsSwapDBuff(db, buffer_ID);
    while (sceGsSyncPath(1, 0) != 0) {
        debug_StdPrintfDummy("wait gs init\n");
    }
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 512.0f);
    zoomTarget = 1.0f;
    zoomSpeed = 1000.0f;
    zoomCurrent = 1.0f;
#ifdef ICO_RD
    /* PC port (R2a, R2c): the 50/60 Hz switch changes the frame size; rd's
       scene-sized targets follow.  sceGsSetDefDBuff wrote both draw
       environments' XYOFFSET without the half offset. */
    {
        static int rdW, rdH; /* port */

        if (rdW != ScreenWidth || rdH != ScreenHeight) {
            rdW = ScreenWidth;
            rdH = ScreenHeight;
            rd_reset_scene((unsigned int)ScreenWidth, (unsigned int)ScreenHeight);
        }
        gsbHostResetHalf();
    }
#endif
}

/* A frame buffer clear packet for the whole screen, 48 doublewords: a GIF tag
   of 23 A+D writes and the register pairs.  Nothing reads it: the send that
   copied it is gone from the retail body. */
static const long long clearFramePacket[48] = {
    /* derived name */
    0x1000000000008017LL,
    0xE,
    0,
    0x4A,
    0x8000000048LL,
    0x42,
    0x700000007000LL,
    0x18,
    0x30000,
    0x47,
    0x130000000LL,
    0x4E,
    0x200000002000000LL,
    0x40,
    0,
    1,
    0x80000,
    0x4C,
    0x106,
    0,
    0xFFFFFFFF70007000LL,
    5,
    0xFFFFFFFF90009000LL,
    5,
    0x81000,
    0x4C,
    0x106,
    0,
    0xFFFFFFFF70007000LL,
    5,
    0xFFFFFFFF90009000LL,
    5,
    0x82000,
    0x4C,
    0x106,
    0,
    0xFFFFFFFF70007000LL,
    5,
    0xFFFFFFFF90009000LL,
    5,
    0x83000,
    0x4C,
    0x106,
    0,
    0xFFFFFFFF70007000LL,
    5,
    0xFFFFFFFF90009000LL,
    5,
};

inline void gsb_ClearFrameBuffer(void)
{
    volatile int local[96];
}

/* the reduction tint gsb_Reduction picks each frame and packs into the
   sprite colour */
static int reductionRed; /* derived name */

static int reductionGreen; /* derived name */

static int reductionBlue; /* derived name */

/* Reduce the frame into the feedback area: one 22 qword GIF packet, built on
 * the stack and sent down the GIF channel by hand, that first clears the
 * half height frame in black and then draws it back over itself through the
 * texture at 0x800 in the reduction tint, and last the tint this stage's
 * current target asks for. */
void gsb_Reduction(void)
{
    long long pk[44] = {
        0x1000000000008015LL,
        0xE,
        0,
        0x4A,
        0x8000000048LL,
        0x42,
        (long long)((ScreenWidth >> 6) & 0x3F) << 16,
        0x4C,
        ((long long)(0x800 - ScreenWidth / 2) << 4) | ((long long)(0x800 - ScreenHeight / 4) << 36),
        0x18,
        ((long long)(ScreenWidth - 1) << 16) | ((long long)(ScreenHeight / 2 - 1) << 48),
        0x40,
        0x30000,
        0x47,
        0x1300000C0LL,
        0x4E,
        0x106,
        0,
        0,
        1,
        (long long)(-ScreenWidth / 2 * 16 + 0x8000 - 4) |
            ((long long)(-ScreenHeight / 4 * 16 + 0x8000 - 4) << 16) | (-1LL << 32),
        5,
        (long long)(-ScreenWidth / 2 * 16 + 0x8000 + ScreenWidth * 16 - 4) |
            ((long long)(-ScreenHeight / 4 * 16 + 0x8000 + ScreenHeight / 2 * 16 - 4) << 16) |
            (-1LL << 32),
        5,
        ((long long)(ScreenWidth - 3) << 16) | 2 |
            ((long long)(systemStatus[0] == 0 ? 2 : 8) << 32) |
            ((long long)(ScreenHeight / 2 - 1 - (systemStatus[0] == 0 ? 2 : 8)) << 48),
        0x40,
        ((long long)(ScreenWidth / 64) << 14) | 0x664000800LL,
        6,
        0x60,
        0x14,
        0x116,
        0,
        (long long)reductionRed |
            ((long long)reductionBlue << 16 | (long long)reductionGreen << 8 | 0x80000000LL),
        1,
        0x80008,
        3,
        (long long)(-ScreenWidth / 2 * 16 + 0x8000 - 4) |
            ((long long)(-ScreenHeight / 4 * 16 + 0x8000 - 4) << 16) | (-1LL << 32),
        5,
        (long long)(ScreenWidth * 16 + 8) | ((long long)(ScreenHeight * 16 + 8) << 16),
        3,
        (long long)(-ScreenWidth / 2 * 16 + 0x8000 + ScreenWidth * 16 - 4) |
            ((long long)(-ScreenHeight / 4 * 16 + 0x8000 + ScreenHeight / 2 * 16 - 4) << 16) |
            (-1LL << 32),
        5,
        ((long long)ScreenWidth << 16) | ((long long)ScreenHeight << 48),
        0x40,
    };

    sceGsSyncPath(0, 0);
    FlushCache(0);
    sceGsSyncPath(0, 0);
    if (pad[0].flags & 0x20) {
        debug_StdPrintfDummy("Film Noise:%d\n", optionScreenMode);
    }
    if (optionScreenMode) {
        reductionRed = fbKeep ? 128 : GlobalStageSetting.targetCol[optionScreenMode - 1][0];
        reductionGreen = fbKeep ? 128 : GlobalStageSetting.targetCol[optionScreenMode - 1][1];
        reductionBlue = fbKeep ? 128 : GlobalStageSetting.targetCol[optionScreenMode - 1][2];
    } else {
        reductionRed = fbKeep ? 128 : GlobalStageSetting.reductionCol[0];
        reductionGreen = fbKeep ? 128 : GlobalStageSetting.reductionCol[1];
        reductionBlue = fbKeep ? 128 : GlobalStageSetting.reductionCol[2];
    }
}

/* the colour the kept frame is drawn back in */
static const unsigned char keepFrameColor[4] = {112, 112, 112, 128}; /* derived name */

/* as in GifPacket.h, which this TU does not include */
extern void gif_EndPacket(void);
/* as in GifPacket.h, which this file does not include */
extern void gif_SetGsReg(long long reg, long long data);
/* as in GifPacket.h, which this TU does not include */
extern void gif_StartPacketPriPath1(int pri);

/* A rectangle in 16ths of a pixel, the form the sprite corners are written
   in. */
typedef struct { /* field names derived */
    int x;
    int y;
    int w;
    int h;
} GsbRect; /* derived name */

/* The GS A+D writer, the payload word then the register word, a macro as in
 * Shadow.c and Texture.c. */
#define setGsReg(reg, val) /* derived name */                                                      \
    {                                                                                              \
        *PacketBufferStruct.ptr.d++ = (val);                                                       \
        *PacketBufferStruct.ptr.d++ = (reg);                                                       \
    }
/* RGBAQ packed from a four-byte colour, as Shadow.c packs it */
#define GIF_RGBA(c) /* derived name */                                                             \
    ((long long)(c)[0] | ((long long)(c)[1] << 8) | ((long long)(c)[2] << 16) |                    \
     ((long long)(c)[3] << 24))
/* The textured sprite at depth 0: PRIM, RGBAQ, then a UV and an XYZ2 pair for
 * each corner of the rect r (x, y, w, h in sixteenths) and its texture rect
 * uv, the far corner as x + fx with fx = w + 0x8000.  Shadow.c's spriteUV
 * with the depth left out; a MACRO for the same reason. */
#define spriteUV(r, uv, col, prim) /* derived name */                                              \
    {                                                                                              \
        setGsReg(0x00, prim);                                                                      \
        setGsReg(0x01, GIF_RGBA(col));                                                             \
        setGsReg(0x03, (long long)(uv)[0] | ((long long)(uv)[1] << 16));                           \
        setGsReg(0x05, (long long)((r)[0] + 0x8000) | ((long long)((r)[1] + 0x8000) << 16));       \
        setGsReg(0x03, (long long)((uv)[0] + (uv)[2]) | ((long long)((uv)[1] + (uv)[3]) << 16));   \
        {                                                                                          \
            int fx = (r)[2] + 0x8000;                                                              \
            int fy = (r)[3] + 0x8000;                                                              \
                                                                                                   \
            setGsReg(0x05, (long long)((r)[0] + fx) | ((long long)((r)[1] + fy) << 16));           \
        }                                                                                          \
    }
/* The untextured sprite at depth z: PRIM, RGBAQ and the two XYZ2 corners of
 * the rect x, y, w, h, the far corner as x + fx with fx = w + 0x8000.
 * Shadow.c's spriteRect with gif_MakeSpriteNoTexture's parameters; a MACRO
 * for the same reason. */
#define spriteRect(x, y, w, h, z, col, prim) /* derived name */                                    \
    {                                                                                              \
        setGsReg(0x00, prim);                                                                      \
        setGsReg(0x01, GIF_RGBA(col));                                                             \
        setGsReg(0x05,                                                                             \
                 (long long)((x) + 0x8000) | ((long long)((y) + 0x8000) << 16) | ((z) << 32));     \
        {                                                                                          \
            int fx = (w) + 0x8000;                                                                 \
            int fy = (h) + 0x8000;                                                                 \
                                                                                                   \
            setGsReg(0x05, (long long)((x) + fx) | ((long long)((y) + fy) << 16) | ((z) << 32));   \
        }                                                                                          \
    }

/* Draw the whole screen back over itself as one sprite in the kept colour. */
static void gsb_KeepFrameBuffer(void)
{
    GsbRect r0 = {-(ScreenWidth >> 1) * 16 - 12, -(ScreenHeight >> 1) * 16 - 12,
                  ScreenWidth * 16 + 32, ScreenHeight * 16 + 32};
    GsbRect r1 = {8, 8, ScreenWidth * 16, ScreenHeight / 2 * 16};

#ifdef ICO_RD
    /* PC port (R2c): the same register writes and sprite as
       rd_post(RD_POST_KEEP), in list 11, in the environment in force */
    (void)r0;
    (void)r1;
    dl_SetDLPriority(11);
    rd_post(RD_POST_KEEP, (RdPostParams *)0);
#else
    gif_StartPacketPriPath1(11);
    gif_SetGsReg(0x47, 0x30000);
    gif_SetGsReg(0x4E, 0x1300000C0LL);
    gif_SetGsReg(0x4A, 0);
    gif_SetGsReg(0x3B, 0x8000000080LL);
    gif_SetGsReg(6, ((long long)(ScreenWidth / 64) << 14) | (0xC482LL << 19));
    spriteUV(&r0.x, &r1.x, keepFrameColor, 0x116);
    gif_EndPacket();
#endif
}

/* the fade level gsb_fade steps from 0 to 128 and gsb_PostEffect prints,
   then one word nothing reads or writes */
static float fadeLevel; /* derived name */

static int gsbUnusedWord; /* derived name */

/* as in GifPacket.h, which this TU does not include */
extern void gif_StartPacketPri(int pri);
/* as in GifPacket.h, which this file does not include */
extern void gif_SetDrawEnviroment(unsigned long long fbp, unsigned long long psm, unsigned int w,
                                  unsigned int h, int useoffset, int clear);

/* The fade overlay: step the fade level by half the speed each frame, clamp
 * it to 0 to 128, stop or hand over to the continue state at the ends, and
 * draw the whole screen as one sprite in the fade colour.  The two end tests
 * are `&&` chains. */
static void gsb_fade(void)
{
    GsbRect r = {-(ScreenWidth >> 1) * 16, -(ScreenHeight >> 1) * 16, ScreenWidth * 16,
                 ScreenHeight * 16};

    switch (fadeStatus) {
    case 1:
        if (0.0f < fadeSpeed) {
            fadeLevel = 0.0f;
            fadeStatus = 2;
        } else if (fadeSpeed < 0.0f) {
            fadeStatus = 2;
            fadeLevel = 144.0f;
        }
        /* FALLTHROUGH */
    case 2:
        fadeLevel = fadeLevel + fadeSpeed * 0.5f;
        break;
    case 3:
        break;
    default:
        goto clear;
    }
    if (0.0f < fadeSpeed && 128.0f <= fadeLevel) {
        if (fadeContinue != 0) {
            fadeStatus = 3;
        } else {
            fadeStatus = 0;
        }
    } else if (fadeSpeed < 0.0f && fadeLevel < 0.0f) {
        if (fadeContinue != 0) {
            fadeStatus = 3;
        } else {
            fadeStatus = 0;
        }
    }
    if (128.0f <= fadeLevel) {
        fadeColor[3] = 128;
    } else if (fadeLevel < 0.0f) {
        fadeColor[3] = 0;
    } else {
        fadeColor[3] = fadeLevel;
    }
#ifdef ICO_RD
    /* PC port (R2c): rd_post(RD_POST_FADE) records the same writes and
       sprite (the scene environment, TEST, ZBUF, PABE, ALPHA 0x44, PRIM
       0x446) in list 11 */
    (void)r;
    dl_SetDLPriority(0xB);
    {
        RdPostParams pp;

        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = fadeColor[0];
        pp.rgba[1] = fadeColor[1];
        pp.rgba[2] = fadeColor[2];
        pp.rgba[3] = fadeColor[3];
        rd_post(RD_POST_FADE, &pp);
    }
#else
    gif_StartPacketPri(0xB);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_SetGsReg(0x47, 0x30000);
    gif_SetGsReg(0x4E, 0x1300000C0LL);
    setGsReg(0x49, 0);
    setGsReg(0x42, 0x44);
    spriteRect(r.x, r.y, r.w, r.h, -1LL, fadeColor, 0x446);
    gif_EndPacket();
#endif
    if (debug_font_flag & 1) {
        debug_Printf(0x208, ScreenHeight / 2 - 8, 0xCCCCCC00, "F");
    }
    return;
clear:
    fadeColor[3] = 0;
    fadeStatus = 0;
}

void gsb_SetMotionBlur(void)
{
    int i = optionScreenMode;

    if (i == 0) {
        SetMotionBlur(GlobalStageSetting.motionBlur);
    } else {
        SetMotionBlur(GlobalStageSetting.subMotionBlur[i - 1]);
    }
}

/* gsb_scissorOnDemo's state: the demo state it last saw, the band level and
   its step */
static int scissorLastState = 54; /* derived name */

static float scissorLevel = 0.0f; /* derived name */

static float scissorStep = 0.0f; /* derived name */

/* as in GifPacket.h, which this TU does not include */
extern void gif_EndPacketPath1(void);

/* The letterbox the demo scenes fade in: two black bars, top and bottom,
 * whose alpha eases to 128 while the scene is state 55 and back to 0
 * otherwise.  While the bars are visible they are drawn and the motion blur
 * is left alone; once they are gone the stage record's blur setting is
 * restored.  Each bar is drawn by a macro inside the two-iteration loop,
 * its first corner written before fx and fy are formed. */
static void gsb_scissorOnDemo(void)
{
    GsbRect r[2] = {
        {-(ScreenWidth >> 1) * 16, -(ScreenHeight >> 1) * 16 - 4, ScreenWidth * 16, 58 * 16},
        {-(ScreenWidth >> 1) * 16, ((ScreenHeight >> 1) - 58) * 16 + 4, ScreenWidth * 16, 58 * 16}};
    unsigned char col[4];
    int i;

    if (current_layout_id == 55 && scissorLastState != current_layout_id) {
        scissorStep = 2.5f;
    } else if (scissorLastState != current_layout_id) {
        scissorStep = -2.5f;
    }
    scissorLastState = current_layout_id;

    scissorLevel = scissorLevel + scissorStep;
    if (scissorLevel <= 0.0f) {
        scissorLevel = 0.0f;
        scissorStep = 0.0f;
    }
    if (128.0f <= scissorLevel) {
        scissorLevel = 128.0f;
        scissorStep = 0.0f;
    }
    if (0.0f < scissorLevel && scissorLevel <= 128.0f) {
        if (debug_font_flag & 1) {
            debug_Printf(0x21C, ScreenHeight / 2 - 8, 0xCCCCCC00, "D");
        }
        dl_SetDLPriority(11);
        /* PC port (issue 27): Options > Effects > Cinematic bars off skips the bars
           (and the subtitle dimming that rides on them); the ramp above still runs */
        if (ico_video_effect_cinematic_bars()) {
#ifdef ICO_RD
            /* PC port (R2c): rd_post(RD_POST_LETTERBOX): the scene
           environment, TEST 0x30000, Z write on, PABE 0, ALPHA 0x64 with
           FIX = the level, the two 58-line bars */
            (void)r;
            (void)col;
            (void)i;
            {
                RdPostParams pp;

                memset(&pp, 0, sizeof(pp));
                pp.fix = (unsigned char)(int)scissorLevel;
                pp.lines = 58;
                rd_post(RD_POST_LETTERBOX, &pp);
            }
#else
            gif_StartPacketPriPath1(dl_GetPri());
            memset(col, 0, 4);
            col[3] = 0x80;
            gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
            gif_SetGsReg(0x47, 0x30000);
            gif_SetGsReg(0x4E, 0x300000C0);
            gif_SetGsReg(0x49, 0);
            gif_SetGsReg(0x42, ((long long)(int)scissorLevel << 32) | 0x64);
            for (i = 0; i < 2; i++) {
                spriteRect(r[i].x, r[i].y, r[i].w, r[i].h, -1LL, col, 0x446);
            }
            gif_EndPacketPath1();
#endif
        }
    } else {
        SetMotionBlur(GlobalStageSetting.motionBlur);
    }
}

/* as in GifPacket.h, which this file does not include */
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);
/* as in GifPacket.h, which this file does not include */
extern void gif_MakeSpriteNoTexture(int x, int y, int w, int h, long long z, unsigned char *col,
                                    int prim);

/* Darken the whole frame by the stage record's brightness step: a full screen
 * white sprite in destination-alpha blend whose alpha is the step, clamped to
 * 0 to 15 and skipped at 0. */
static void gsb_controlBrightness(void)
{
    int v = systemStatus[0x2C / 4];

    if (v < 0) {
        v = systemStatus[0x2C / 4] = 0;
    }
    if (v >= 0x10) {
        v = systemStatus[0x2C / 4] = 0xF;
    }
    if (v != 0) {
        if (debug_font_flag & 1) {
            debug_Printf(0x212, ScreenHeight / 2 - 8, 0xCCCCCC00, "B");
        }
#ifdef ICO_RD
        /* PC port (R2c): rd_post(RD_POST_BRIGHTNESS): TEST 0x30000, Z
           write off, gif_SetAlpha(1, 7, 0), the white sprite with the
           corners the GS receives from gif_MakeSpriteNoTexture */
        dl_SetDLPriority(0xB);
        {
            RdPostParams pp;

            memset(&pp, 0, sizeof(pp));
            pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 0xFF;
            pp.rgba[3] = (unsigned char)systemStatus[0x2C / 4];
            rd_post(RD_POST_BRIGHTNESS, &pp);
        }
#else
        gif_StartPacketPri(0xB);
        {
            unsigned char col[4] = {0xFF, 0xFF, 0xFF, systemStatus[0x2C / 4]};

            gif_SetGsReg(0x47, 0x30000);
            gif_SetGsReg(0x4E, 0x1300000C0LL);
            gif_SetAlpha(1, 7, 0);
            gif_MakeSpriteNoTexture((0x800 - ScreenWidth / 2) << 4, (0x800 - ScreenHeight / 2) << 4,
                                    ScreenWidth << 4, ScreenHeight << 4, 0xFFFFFFFE, col, 1);
            gif_EndPacketPath1();
        }
#endif
    }
}

/* A colour as the sprite family takes it, four bytes in RGBA order; the same
   record as Texture.c's TexColor. */
typedef struct { /* field names derived */
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} GsbColor; /* derived name */

/* as in GifPacket.h, which this TU does not include */
extern void gif_SetZTest(int on);
/* as in GifPacket.h, which this TU does not include */
extern void gif_SetZWrite(int on);
/* as in GifPacket.h, which this TU does not include, with GsbRect and GsbColor
 * for its GifRect and GifColor */
extern void gif_SpriteSensitiveOrg(GsbRect *r, long long z, GsbRect *uv, GsbColor *col, int prim);

/* Soften the frame's edges: the frame is reduced to a 256 square copy and,
 * when the second level is on, a 128 square one, and each level the stage
 * record (or the current sub target's row) turns on is blended back over the
 * 512 square buffer through the sensitive sprite.  Every TEX0 write is one
 * expression in field order, as Texture.c spells it. */
static void gsb_antiAlias(void)
{
    GsbColor col = {128, 128, 128, 128};
    GsbRect s0 = {4, 4, 8192, 8192};
    GsbRect s1 = {4, 4, 4096, 4096};
    GsbRect s2 = {4, 4, 2048, 2048};
    GsbRect d0 = {-4100, -4100, 8192, 8192};
    GsbRect d1 = {-2052, -2052, 4096, 4096};
    GsbRect d2 = {-1028, -1028, 2048, 2048};
    int lv[2];

    /* PC port (issue 11): Options > Effects > Screen softening off */
    if (!ico_video_effect_softening()) {
        return;
    }
    if (optionScreenMode == 0) {
        lv[0] = GlobalStageSetting.antiLevel0;
        lv[1] = GlobalStageSetting.antiLevel1;
    } else {
        lv[0] = GlobalStageSetting.antiLevel[optionScreenMode].a;
        lv[1] = GlobalStageSetting.antiLevel[optionScreenMode].b;
    }
    if (lv[0] == 0 && lv[1] == 0) {
        return;
    }
#ifdef ICO_RD
    /* PC port (R2c): rd_post(RD_POST_AA_DOWNSAMPLE) then
       rd_post(RD_POST_AA_COMPOSITE) record this function's writes and
       sprites in list 10, in the same order */
    (void)col;
    (void)s0;
    (void)s1;
    (void)s2;
    (void)d0;
    (void)d1;
    (void)d2;
    dl_SetDLPriority(10);
    {
        RdPostParams pp;

        memset(&pp, 0, sizeof(pp));
        pp.lines = lv[1] != 0 ? 2 : 1;
        rd_post(RD_POST_AA_DOWNSAMPLE, &pp);
        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = (unsigned char)lv[0];
        pp.rgba[1] = (unsigned char)lv[1];
        rd_post(RD_POST_AA_COMPOSITE, &pp);
    }
    return;
#endif
    gif_StartPacketPri(10);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetDrawEnviroment(0x2800, 0, 256, 256, 0, 0);
    gif_SetGsReg(6, 0x800 | ((long long)8 << 14) | ((long long)tex_GetTWTH(512) << 26) |
                        ((long long)tex_GetTWTH(512) << 30) | ((long long)1 << 34));
    gif_SetAlpha(0, 2, 128);
    gif_SpriteSensitiveOrg(&d1, 0, &s0, &col, 0);
    if (lv[1] != 0) {
        gif_SetGsReg(6, 0x2800 | ((long long)4 << 14) | ((long long)tex_GetTWTH(256) << 26) |
                            ((long long)tex_GetTWTH(256) << 30) | ((long long)1 << 34));
        gif_SetDrawEnviroment(0x2C00, 0, 128, 128, 0, 0);
        gif_SpriteSensitiveOrg(&d2, 0, &s1, &col, 0);
    }
    gif_SetDrawEnviroment(0x800, 0, 512, 512, 1, 0);
    if (lv[1] != 0) {
        gif_SetAlpha(1, 2, lv[1]);
        gif_SetGsReg(6, 0x2C00 | ((long long)2 << 14) | ((long long)tex_GetTWTH(128) << 26) |
                            ((long long)tex_GetTWTH(128) << 30) | ((long long)1 << 34));
        gif_SpriteSensitiveOrg(&d0, 0, &s2, &col, 1);
    }
    if (lv[0] != 0) {
        gif_SetAlpha(1, 2, lv[0]);
        gif_SetGsReg(6, 0x2800 | ((long long)4 << 14) | ((long long)tex_GetTWTH(256) << 26) |
                            ((long long)tex_GetTWTH(256) << 30) | ((long long)1 << 34));
        gif_SpriteSensitiveOrg(&d0, 0, &s1, &col, 1);
    }
    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_SetDrawEnviroment(0x800, 0, ScreenWidth, ScreenHeight, 1, 0);
    gif_EndPacket();
}

static void gsb_setNormalReg(int ctx)
{
    dl_SetDLPriority(ctx);
    gif_StartPacketPriPath1(dl_GetPri());
    gif_SetGsReg(0x47, 0x50000);
    gif_SetGsReg(0x4E, 0x300000C0);
    gif_SetGsReg(0x4A, 0);
    gif_SetGsReg(0x3B, 0x8000000080LL);
    gif_EndPacketPath1();
}

static void gsb_setSemitransReg(int ctx)
{
    dl_SetDLPriority(ctx);
    gif_StartPacketPriPath1(dl_GetPri());
    gif_SetGsReg(0x47, 0x5140D);
    gif_SetGsReg(0x4E, 0x300000C0);
    gif_SetGsReg(0x4A, 0);
    gif_SetGsReg(0x3B, 0x810000807FLL);
    gif_EndPacketPath1();
}

static void gsb_setSpecularReg(int ctx)
{
    dl_SetDLPriority(ctx);
    gif_StartPacketPriPath1(dl_GetPri());
    gif_SetGsReg(0x47, 0x5C000);
    gif_SetGsReg(0x4E, 0x1300000C0LL);
    gif_SetGsReg(0x4A, 0);
    gif_SetGsReg(0x3B, 0x8000000080LL);
    gif_EndPacketPath1();
}

static void gsb_setParticleReg(int ctx)
{
    dl_SetDLPriority(ctx);
    gif_StartPacketPriPath1(dl_GetPri());
    gif_SetGsReg(0x47, 0x50000);
    gif_SetGsReg(0x4E, 0x1300000C0LL);
    gif_SetGsReg(0x4A, 0);
    gif_SetGsReg(0x3B, 0x8000000080LL);
    gif_EndPacketPath1();
}

/* the head of the common matrix packet: the three constant rows of the VU
   parameter block (the unit w, the clip extents and a zero row) and the GIF
   tag of the strip the microcode sends */
static const struct { /* field names derived */
    sceVu0FVECTOR row[3];
    sceVu0IVECTOR tag;
} commonMatrixHead = {
    /* derived name */
    {{0.0f, 0.0f, 0.0f, 1.0f}, {4095.0f, 4095.0f, 0.0f, 16777215.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {0x8000, 0x302EC000, 0x512, 0},
};

/* Build the frame's common matrix packet: the three view matrices the stage
 * needs, the inverse of the view, and the display list that uploads them to
 * VU memory, opened once for each of the thirteen list priorities. */
void gsb_MakeCommonMatrix(void)
{
    GifPkWord *p;
    GifPkWord *q;
    int i;

    if (game_pause == 0) {
        return;
    }
    i = 0;
    _MulMatrix(matrixptr + 0x100, matrixptr + 0xC0, matrixptr + 0x80);
    _MulMatrix(matrixptr + 0x200, matrixptr + 0x1C0, matrixptr + 0x80);
    _MulMatrix(matrixptr + 0x280, matrixptr + 0x240, matrixptr + 0x80);
    /* PC port: the cull's matrix and the view and projection it is made of */
    gsbHostCullBuilt();
    _InversMatrix(matrixptr + 0x380, matrixptr + 0x80);
    p = (GifPkWord *)PacketBufferStruct.ptr.d;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.dma.c = (char *)p;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.tail.c = (char *)p;
    p[0].d = 0x10000011;
    PacketBufferStruct.ptr.c = (char *)p + 8;
    p[1].w[0] = 0x13000000;
    PacketBufferStruct.ptr.c = (char *)p + 0xC;
    PacketBufferStruct.gif.c = (char *)p + 0xC;
    p[1].w[1] = 0x6C100000;
    PacketBufferStruct.ptr.c = (char *)p + 0x10;
    _CopyMatrix((char *)p + 0x10, &commonMatrixHead);
    PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    _CopyMatrix(PacketBufferStruct.ptr.d, matrixptr + 0x100);
    PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    _CopyMatrix(PacketBufferStruct.ptr.d, matrixptr + 0x340);
    PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    _CopyMatrix(PacketBufferStruct.ptr.d, matrixptr + 0x380);
    q = (GifPkWord *)PacketBufferStruct.ptr.d;
    PacketBufferStruct.ptr.c = (char *)q + 0x40;
    q[8].w[0] = 0x13000000;
    PacketBufferStruct.ptr.c = (char *)q + 0x44;
    q[8].w[1] = 0;
    PacketBufferStruct.ptr.c = (char *)q + 0x48;
    q[9].w[0] = 0;
    PacketBufferStruct.ptr.c = (char *)q + 0x4C;
    q[9].w[1] = 0;
    PacketBufferStruct.ptr.c = (char *)q + 0x50;
    PacketBufferStruct.tail.c = (char *)q + 0x50;
    q[10].d = 0x60000000;
    PacketBufferStruct.ptr.c = (char *)q + 0x58;
    q[11].w[0] = 0;
    PacketBufferStruct.ptr.c = (char *)q + 0x5C;
    q[11].w[1] = 0;
    PacketBufferStruct.ptr.c = (char *)q + 0x60;
    do {
        dl_SetDLPriority(i);
        i++;
        dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
        dl_CloseDma();
    } while (i < 0xD);
#ifdef ICO_RD
    gsbHostCommon();
#endif
}

/* Open the frame's first display list: one DMA tag, the two VIF words that
 * hand path 1 to the GS, a second tag for the register run, and then the
 * default register set for each of the thirteen contexts. */
static void gsb_SetGsDefault(void)
{
    DpkCtl *d = &PacketBufferStruct;
    GifPkWord *p = (GifPkWord *)d->ptr.d;

    d->gif.c = 0;
    d->tail.c = (char *)p;
    d->dma.c = (char *)p;
    d->end.c = 0;
    p[0].d = 0x10000000;
    d->ptr.c = (char *)p + 8;
    p[1].w[0] = 0x3000100;
    d->ptr.c = (char *)p + 0xC;
    p[1].w[1] = 0x2000180;
    d->ptr.c = (char *)p + 0x10;
    d->tail.c = (char *)p + 0x10;
    p[2].d = 0x60000000;
    d->ptr.c = (char *)p + 0x18;
    p[3].w[0] = 0;
    d->ptr.c = (char *)p + 0x1C;
    p[3].w[1] = 0;
    d->ptr.c = (char *)p + 0x20;
    dl_SetDLPriority(0);
    dl_OpenDma(5, d->dma.c, 0);
    dl_CloseDma();
    gsb_MakeCommonMatrix();
    gsb_setNormalReg(0);
    gsb_setSemitransReg(1);
    gsb_setSemitransReg(2);
    gsb_setParticleReg(6);
    gsb_setSpecularReg(4);
    gsb_setNormalReg(7);
    gsb_setNormalReg(8);
    gsb_setNormalReg(9);
    gsb_setNormalReg(0xA);
    gsb_setNormalReg(0xB);
    gsb_setNormalReg(0xC);
}

/* Lay the film grain texture over the frame: the noise texture at texture
 * slot 10, drawn as one full screen sprite whose colour comes from the
 * stage record's grain tint for this target and whose UV step is the
 * record's grain scale, passed as raw bits in both halves of the register. */
static void gsb_filmNoise(void)
{
    int n = tex_GetTextureNo("sandstorm_spr");
    float scale;

    if (n < 0) {
        return;
    }
    scale = GlobalStageSetting.grainScale;
    tex_TransTexture(n, 0xA);
#ifdef ICO_RD
    /* PC port (R2c): one PRIM write lets the decoder bind the TEX0
       tex_TransTexture wrote (sandstorm_spr, through the texture resolver),
       as DisplayFont.c does; rd_post(RD_POST_FILM_NOISE) records the rest
       (CLAMP 0, ZBUF, TEST, PABE, ALPHA 0x44, the grain sprite) */
    gif_StartPacketPriPath1(dl_GetPri());
    gif_SetGsReg(0, 0x56);
    gif_EndPacketPath1();
    {
        RdPostParams pp;

        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 0x80;
        pp.rgba[3] = (unsigned char)GlobalStageSetting.targetCol[optionScreenMode - 1][3];
        pp.scalar[0] = scale;
        rd_post(RD_POST_FILM_NOISE, &pp);
    }
    return;
#endif
    gif_StartPacketPriPath1(dl_GetPri());
    gif_SetGsReg(8, 0);
    gif_SetGsReg(0x4E, 0x1300000C0LL);
    gif_SetGsReg(0x47, 0x30000);
    gif_SetGsReg(0x49, 0);
    gif_SetGsReg(0x42, 0x44);
    gif_SetGsReg(0, 0x56);
    gif_SetGsReg(1, ((long long)GlobalStageSetting.targetCol[optionScreenMode - 1][3] << 24) |
                        0x3F80000000808080LL);
    gif_SetGsReg(2, 0);
    gif_SetGsReg(5, 0xFFFFFFFF70007000LL);
    gif_SetGsReg(2, ((long long)*(unsigned int *)&scale << 32) | *(unsigned int *)&scale);
    gif_SetGsReg(5, 0xFFFFFFFF90009000LL);
    gif_EndPacketPath1();
}

/* the stage animation group each film noise target loops, -1 for none */
static const int filmNoiseGroup[] = {-1, 67, 68, 69, 70}; /* derived name */

inline void gsb_ResetFilmNoise(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        if (filmNoiseGroup[i] != -1) {
            if (i == optionScreenMode) {
                stage_SetLoopFlag(filmNoiseGroup[i], 1);
                debug_StdPrintfDummy("set film noise %d\n", optionScreenMode);
            } else {
                stage_SetLoopFlag(filmNoiseGroup[i], 0);
                debug_StdPrintfDummy("clear film noise %d\n", optionScreenMode);
            }
        }
    }
}

/* set by gsb_UpdateGSSystem once the frame is up, tested by gsb_PostEffect */
static int postEffectReady = 0; /* derived name */

/* Everything the frame still owes after the scene is drawn: the debug read
 * outs, the full screen effect, the shadow and fog passes, the motion blur,
 * the anti alias pass, the film grain and the brightness step, the kept frame
 * buffer, the staff roll and last the fade and the demo scissor. */
static int gsb_PostEffect(void)
{
    if (debug_font_flag & 1) {
        debug_Printf(0xA, ScreenHeight / 2 - 8, 0xCCCCCC00, "LID:%3d / FADE%d:%3.0f(%d)",
                     current_layout_id, fadeStatus, fadeLevel, fadeColor[3]);
    }
    if (systemStatus[0x18 / 4] != 0 && (debug_font_flag & 1)) {
        debug_Printf(0x230, ScreenHeight / 2 - 8, 0xCCCCCC00, "L");
    }
    if (systemStatus[0x14 / 4] != 0 && (debug_font_flag & 1)) {
        debug_Printf(0x23A, ScreenHeight / 2 - 8, 0xCCCCCC00, "P");
    }
    FullScreenEffectAfter();
    if (postEffectReady != 0) {
        shadow_Draw();
    }
    fog_DrawFog();
    MotionBlur();
    gsb_antiAlias();
    if (gFlagGameClear > 0 && optionScreenMode != 0) {
        gsb_filmNoise();
    }
    gsb_controlBrightness();
    if (fbKeep != 0) {
        gsb_KeepFrameBuffer();
        if (debug_font_flag & 1) {
            debug_Printf(0x226, ScreenHeight / 2 - 8, 0xCCCCCC00, "K");
        }
    }
    if (staffRollStartFlag != 0) {
        staffRollMain();
    }
    gsb_fade();
    gsb_scissorOnDemo();
    return fbKeep;
}

#ifdef ICO_RD

/* PC port (R2c): the flip on rd.  On the PS2 gsb_UpdateGSSystem runs, in
   this order: gsb_Reduction (SCENE, as the frame kicked one flip earlier
   left it, into DISPLAY, with the tint the previous call computed);
   sceGsSwapDBuff (the display environment, then over GIF path 3 the draw
   environment of db.draw[buffer_ID] and its clear packet, whose RGBAQ
   gsb_SetBGColor wrote at db+0x100 / db+0x1F0); sceGsSetHalfOffset, which
   rewrites the XYOFFSET of the draw environment just sent, so it takes
   effect at the flip after next; then dl_Swap kicks the lists the frame
   recorded since the previous call.  So a frame's lists draw over a clear
   to the BG colour current at the flip that kicks them (not at the flip
   that opened them), in a draw environment whose half offset the field bit
   decided two flips earlier.

   rd records the head when the frame opens (gsbHostFrameHead: the draw
   environment, the clear, at the head of lists 0 and 11) and patches the
   colour and the half offset at the flip (gsbHostFlip), before rd_end_frame
   keeps the copy of the first list the frame replays. */

/* the half-offset bit each draw environment of db carries (sceGsSetHalfOffset
   writes the GS register image, a no-op on the host) */
static unsigned char gsbHostHalf[2]; /* port */

static void gsbHostResetHalf(void)
{
    gsbHostHalf[0] = gsbHostHalf[1] = 0;
}

/* the field bit the PS2 reads from GS_CSR (bit 13): the host loop
   (port/platform/host_loop.c) alternates it every vsync.  odd_even keeps
   its host value 0; only the renderer reads this. */
static int gsbHostField(void)
{
    return (int)((*GS_CSR >> 13) & 1);
}

static void gsbHostFrameHead(void)
{
    RdFrameHead h;

    if (!rd_frame_open()) {
        return;
    }
    gif_HostFlush();
    memset(&h, 0, sizeof(h));
    gsb_GetBGColor(h.rgba);
    h.z = 0;
    h.gsW = (unsigned int)ScreenWidth;
    h.gsH = (unsigned int)ScreenHeight;
    h.halfY = 0; /* gsbHostFlip sets what the flip sends */
    h.clear = fbClear != 0;
    rd_frame_head(&h);
}

/* between sceGsSwapDBuff and sceGsSetHalfOffset: what the flip sent */
static void gsbHostFlip(void)
{
    unsigned char bg[4];

    gsb_GetBGColor(bg);
    rd_frame_flip(bg, gsbHostHalf[buffer_ID & 1]);
}

/* sceGsSetHalfOffset(draw, ..., odd_even == 0) with the field bit */
static void gsbHostSetHalfOffset(void)
{
    gsbHostHalf[buffer_ID & 1] = gsbHostField() == 0;
}

/* PC port (R2a, R2c): gsb_Reduction's packet (SCENE into the half-height
   DISPLAY, tinted, border-cropped) as rd's reduction pass at the end of
   list 12 of the frame dl_Swap is about to close; the tint is the one
   gsb_Reduction computed this call, which on the PS2 is the tint of the
   reduction of the frame this dl_Swap kicks. */
static void gsbHostReduction(void)
{
    RdPostParams pp;

    if (!rd_frame_open()) {
        return;
    }
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = (unsigned char)reductionRed;
    pp.rgba[1] = (unsigned char)reductionGreen;
    pp.rgba[2] = (unsigned char)reductionBlue;
    pp.rgba[3] = 0x80;
    dl_SetDLPriority(12);
    rd_post(RD_POST_REDUCTION, &pp);
}

#endif

/* gsb_InitGSSystem's first call brings every module up */
static int firstGsInit = 1; /* derived name */

void gsb_InitGSSystem(void)
{
    screen_offset_y = 0;
    screen_offset_x = 0;
    /* PC port: a stage's first tick has no earlier camera to cull against */
    gsbHostCullForget();
    if (firstGsInit != 0) {
        sceGsResetPath();
        sceVpu0Reset();
        debug_StdPrintfDummy("dma init\n");
        dma_init();
        debug_StdPrintfDummy("matrix init\n");
        matrix_init();
        debug_StdPrintfDummy("texture init\n");
        tex_Init();
        debug_StdPrintfDummy("gs init\n");
        sceGsSyncV(0);
        gsb_Init(&db);
        sceGsSyncV(0);
        dl_Init();
#ifdef ICO_RD
        gsbHostFrameHead();
#endif
        firstGsInit = 0;
    } else {
        debug_StdPrintfDummy("dma init\n");
        dma_init();
        debug_StdPrintfDummy("texture init\n");
        tex_Init();
    }
    resetmallocseki();
    pac_Init();
    reg_Init();
    shadow_Init();
    gsSystemReady = 1;
    screenOffsetX = screenOffsetY = 0;
}

/* Six zero words between gsb_InitGSSystem's flag and gsb_SyncGSSystem's
   counter: nothing reads or writes them. */
static int gsbUnused0 = 0; /* derived name */

static int gsbUnused1 = 0; /* derived name */

static int gsbUnused2 = 0; /* derived name */

static int gsbUnused3 = 0; /* derived name */

static int gsbUnused4 = 0; /* derived name */

static int gsbUnused5 = 0; /* derived name */

inline int gsb_ResetSnap(void) {}

inline int gsb_TakeSnap(void) {}

/* the frames gsb_SyncGSSystem has waited on the GS */
static int syncRetry = 0; /* derived name */

inline int gsb_SyncGSSystem(void)
{
    if (sceGsSyncPath(1, 0)) {
        syncRetry++;
        if (syncRetry >= 11) {
            debug_StdPrintfDummy("reset gs\n");
            gsb_ResetGSSystem();
            syncRetry = 0;
        }
        return 1;
    }
    syncRetry = 0;
    gsb_PostEffect();
    return 0;
}

/* One frame boundary: take the field parity from the GS CSR, run the
 * reduction pass, flip the double buffer and hand the display list back
 * either swapped or cleared, then reopen the frame's register set. */
void gsb_UpdateGSSystem(int keep)
{
    sceGsDrawEnv *draw;

    odd_even = 0; /* GS_CSR field bit: no GS on the host */
    gsb_Reduction();
    if (gsSystemReady == 0) {
        dl_Clear();
        return;
    }
    debug_FlushFont();
    frame_count++;
    buffer_ID = frame_count & 1;
    FlushCache(0);
    sceGsSwapDBuff(&db, buffer_ID);
#ifdef ICO_RD
    gsbHostFlip();
#endif
    /* PC port: the camera of the tick just ended, the next tick's cull's
       earlier camera */
    gsbHostCullFlip();
    if (buffer_ID != 0) {
        draw = &db.draw1;
    } else {
        draw = &db.draw0;
    }
    sceGsSetHalfOffset(draw, (short)(int)((float)screen_offset_x + 2048.0f),
                       (short)(int)((float)screen_offset_y + 2048.0f), odd_even == 0);
#ifdef ICO_RD
    gsbHostSetHalfOffset();
#endif
    tex_ResetVram();
#ifdef ICO_RD
    if (keep == 0) {
        gsbHostReduction();
    }
#endif
    if (keep == 0) {
        dl_Swap();
    } else {
        dl_Clear();
    }
#ifdef ICO_RD
    gsbHostFrameHead();
#endif
    gsb_SetGsDefault();
    postEffectReady = 0;
    shadow_Reset();
    postEffectReady = 1;
    FullScreenEffectBefore();
    currentScreenWidth = GlobalTimer;
    light_ResetLight();
}

/* Reset the GS between stages: reopen the paths, reset the VU0 and the DMA,
 * put the graphics mode back, take the field parity out of the GS CSR and
 * flip the double buffer, then re-open the frame's display list. */
void gsb_ResetGSSystem(void)
{
    sceGsDrawEnv *draw;

    sceGsResetPath();
    sceVpu0Reset();
    dma_init();
    sceGsResetGraph(0, systemStatus[1] == 1, (unsigned short)systemStatus[0] + 2, 1);
    /* PC port: no earlier camera across a reset between stages */
    gsbHostCullForget();
    frame_count++;
    buffer_ID = frame_count & 1;
    odd_even = 0; /* GS_CSR field bit: no GS on the host */
    FlushCache(0);
    sceGsSwapDBuff(&db, buffer_ID);
#ifdef ICO_RD
    gsbHostFlip();
#endif
    if (buffer_ID != 0) {
        draw = &db.draw1;
    } else {
        draw = &db.draw0;
    }
    sceGsSetHalfOffset(draw, (short)(int)((float)screen_offset_x + 2048.0f),
                       (short)(int)((float)screen_offset_y + 2048.0f), odd_even == 0);
#ifdef ICO_RD
    gsbHostSetHalfOffset();
#endif
    tex_ResetVram();
    dl_Swap();
#ifdef ICO_RD
    gsbHostFrameHead();
#endif
    gsb_SetGsDefault();
}

/* PC port: the zoom vs[0] is 0 before the first stage sets the view scale
   (vsync 6); the EE's div.s gives +-Fmax there, IEEE gives Inf and then NaN
   in 0 * Inf. */
#define VS_DIV(a, b) ps2_div((a), (b))

/* the 1500 unit screen the projection proj is scaled to */
static const float vsScreenSize[] = {1500.0f, 1500.0f, 0.0f, 0.0f}; /* derived name */

/* Build the view matrices from the record gsb_SetVSMatrix fills (vs[0] the
 * zoom, vs[1] and vs[2] the aspect terms, vs[3] and vs[4] the centre, vs[5]
 * and vs[6] the depth range, vs[7] and vs[8] the near and far planes): the
 * screen matrix screen, the perspective projection proj for the 1500 unit
 * screen, the one projHalf for the half size screen, the viewport viewport,
 * and the pair built on a 500 unit screen at matrixptr+0x640 and +0x680.  The 500 unit pair's
 * scale terms are locals of their own. */
static void gsb_SetVSMatrixSub(float *screen, float *proj, float *projHalf, float *viewport,
                               float *vs)
{
    sceVu0FVECTOR v = {ScreenWidth / 2, ScreenHeight / 2, 0.0f, 0.0f};
    float m0[16];
    float m1[16];
    float sx;
    float sy;
    float cx;
    float cy;
    float zn;
    float zf;
    float rx;
    float ry;

    sx = VS_DIV(vs[7] * vsScreenSize[0], vs[0]);
    sy = VS_DIV(vs[7] * vsScreenSize[1], vs[0]);

    cx = VS_DIV(vs[7] * v[0], vs[0]);
    cy = VS_DIV(vs[7] * v[1], vs[0]);

    zn = (-vs[6] * vs[7] + vs[5] * vs[8]) / (-vs[7] + vs[8]);

    zf = vs[8] * vs[7] * (-vs[5] + vs[6]) / (-vs[7] + vs[8]);

    _UnitMatrix(screen);
    screen[0] = vs[0];
    screen[5] = vs[0];
    screen[10] = 0.0f;
    screen[15] = 0.0f;
    screen[14] = 1.0f;
    screen[11] = 1.0f;

    _UnitMatrix(m0);
    /* clang-format off */
    m0[0] = vs[1]; m0[5] = vs[2]; m0[10] = zf;
    m0[12] = vs[3]; m0[13] = vs[4]; m0[14] = zn;
    /* clang-format on */
    _MulMatrix(screen, m0, screen);

    _UnitMatrix(proj);
    proj[0] = (vs[7] + vs[7]) / (sx + sx);
    proj[5] = (vs[7] + vs[7]) / (sy + sy);
    proj[10] = (vs[8] + vs[7]) / (vs[8] - vs[7]);
    proj[14] = vs[8] * vs[7] * -2.0f / (vs[8] - vs[7]);
    proj[11] = 1.0f;
    proj[15] = 0.0f;

    _UnitMatrix(viewport);
    viewport[0] = vs[0] * vs[1] * sx / vs[7];
    viewport[5] = vs[0] * vs[2] * sy / vs[7];
    viewport[10] = (-vs[6] + vs[5]) * 0.5f;
    viewport[12] = vs[3];
    viewport[13] = vs[4];
    viewport[14] = (vs[6] + vs[5]) * 0.5f;
    viewport[15] = 1.0f;

    _UnitMatrix(projHalf);
    projHalf[0] = (vs[7] + vs[7]) / (cx + cx);
    projHalf[5] = (vs[7] + vs[7]) / (cy + cy);
    projHalf[10] = (vs[8] + vs[7]) / (vs[8] - vs[7]);
    projHalf[14] = vs[8] * vs[7] * -2.0f / (vs[8] - vs[7]);
    projHalf[11] = 1.0f;
    projHalf[15] = 0.0f;

    _UnitMatrix(m1);
    m1[0] = 500.0f;
    m1[5] = 500.0f;
    m1[10] = 0.0f;
    m1[15] = 0.0f;
    m1[14] = 1.0f;
    m1[11] = 1.0f;

    _UnitMatrix(m0);
    /* clang-format off */
    m0[0] = vs[1]; m0[5] = vs[2]; m0[10] = zf;
    m0[12] = vs[3]; m0[13] = vs[4]; m0[14] = zn;
    /* clang-format on */
    _MulMatrix(matrixptr + 0x640, m0, m1);

    rx = vs[7] * v[0] / 500.0f;
    ry = vs[7] * v[1] / 500.0f;
    _UnitMatrix(m1);
    m1[0] = (vs[7] + vs[7]) / (rx + rx);
    m1[5] = (vs[7] + vs[7]) / (ry + ry);
    m1[10] = (vs[8] + vs[7]) / (vs[8] - vs[7]);
    m1[14] = vs[8] * vs[7] * -2.0f / (vs[8] - vs[7]);
    m1[11] = 1.0f;
    m1[15] = 0.0f;
    _CopyMatrix(matrixptr + 0x680, m1);
}

/* The view record gsb_SetVSMatrixSub builds the view and screen matrices
 * from: the zoom, the two aspect terms, the centre, and the near and far
 * planes. */
static float vsParam[10]; /* derived name */

/* Set the view and screen matrices for a frame of w by h at depth d: the
 * centre is the screen middle less the staff roll offset, the zoom eases
 * towards its target by a thousandth of the step, and the view record at
 * vsParam carries the centre, the aspect terms and the near and far
 * planes gsb_SetVSMatrixSub builds the matrices from. */
void gsb_SetVSMatrix(int w, int h, float d)
{
    float zoom;

    center_X = center_Y = 2048.0f;
    if (d == 0.0f) {
        d = (float)currentFocusDistance;
    } else {
        currentFocusDistance = d;
    }
    /* PC port (photo mode): the full-screen camera's distance */
    if (w == ScreenWidth && h == ScreenHeight) {
        gsbViewD = d;
    }
    vsWidth = w;
    vsHeight = h;
    if (zoomTarget != zoomCurrent) {
        zoomCurrent = zoomCurrent + (zoomTarget - zoomCurrent) * zoomSpeed * 0.001f;
    }
    if (staffRollStartFlag != 0) {
        center_X = center_X - staffRollCenterOffsetX;
    }
    zoom = (float)GlobalStageSetting.viewScale * zoomCurrent * d * (float)debug_zoom_per *
           (float)ScreenWidth / 640.0f / 100.0f / 100.0f;
    vsParam[0] = zoom;
    if (debug_snapshot_reserve != 0 || (pad[1].now & 0x800) != 0) {
        vsParam[0] = zoom * (float)debug_snapshot_num / 100.0f;
    }
    tex_UpdateMipMapLevel((float)GlobalStageSetting.viewScale * zoomCurrent *
                          (float)debug_zoom_per * (float)ScreenWidth / 640.0f / 100.0f);
    vsParam[3] = center_X;
    vsParam[4] = center_Y;
    vsParam[5] = 1.0f;
    vsParam[6] = 536870880.0f;
    vsParam[7] = 2.0f;
    vsParam[1] = (float)w / (float)ScreenWidth;
    vsParam[8] = 262144.0f;
    vsParam[2] =
        (float)ScreenHeight * 4.0f / ((float)ScreenWidth * 3.0f) * (float)h / (float)ScreenHeight;
    gsb_SetVSMatrixSub((float *)(matrixptr + 0xC0), (float *)(matrixptr + 0x1C0),
                       (float *)(matrixptr + 0x240), (float *)(matrixptr + 0x340), vsParam);
    gsbHostWidenCull((float *)(matrixptr + 0x240));
    gsbHostWidenCull((float *)(matrixptr + 0x680));
}

/* PC port: the widescreen and in-between cull.  Two projections cull:
   projHalf (matrixptr+0x240), the projection of the visible screen that
   +0x280 (gsb_MakeCommonMatrix) and RegistPacket.c's per-object +0x300 are
   built from, and the 500 unit pair's +0x680, which RegistPacket.c's
   reg_setMMatrixPacket uses in place of +0x240 for the parts that follow
   the camera (node flag 2).  gsb_ClipBox culls against the current matrix
   made from them.  Both are widened here, never a matrix that places
   anything on screen.
     - The width: a wide output divides the x scale by how much wider than
       4:3 it is, (aspect) / (4/3), from the display options
       (port/game/video_options.c: 1 in the Original preset, 4/3 at 16:9,
       1.2 at 16:10), so objects at the sides are not culled.
     - The margin: when pictures are blended between ticks (a framerate
       other than Original) the x and y scales are divided by a further
       1 + GSB_CULL_MARGIN.  A blended picture only draws what both ticks
       drew (rd_interp.c), so a part culled at the newer tick vanished one
       tick early at the screen's edges while the camera turned; with the
       margin a part just past the edge is still drawn at the tick (the
       scissor keeps it off the tick's own picture).  0.12 makes the
       culled frustum's half-width and half-height 1.12 times the
       screen's, the same share of the picture at every aspect.  The
       margin alone covers a small turn only: gsb_ClipBox also culls
       against the earlier cameras of the blended pictures (the block
       before it), each frustum with this margin.
   The renderer's own projection widens in rd (rd__fill_camera_cb, the
   replay's g_space); the gameplay matrices +0x80 and +0xC0
   (IsPointIsInScreen and the screen tests) never change.  puddle.c and
   pool.c call gsb_SetVSMatrix for their reflection views too, so the
   reflections' cull widens by the same factors: it only ever adds objects
   to a reflection, whose +0xC0 stays 4:3 as well; the renderer widens the
   render-to-texture block and its draws (rd_core.c rd__target_scale_of), so
   those objects show at the sides of a wide frame.  A part the widened
   frustum holds whole but that crosses the screen's edge is drawn by the
   VU program for parts inside (code 34) instead of the one with the
   per-triangle region test (code 32); its vertices are well inside the
   GS's 0..4094 window, which is all that test drops.  At width 1 with the
   Original framerate (the Original preset's picture) the matrices are
   left exactly as computed. */
#define GSB_CULL_MARGIN 0.12f

static float gsbHostWideX(void)
{
    return ico_video_wide_x();
}

static void gsbHostWidenCull(float *projHalf)
{
    float k = gsbHostWideX();
    float m = ico_video_interpolate() ? GSB_CULL_MARGIN : 0.0f;

    if (k != 1.0f || m != 0.0f) {
        projHalf[0] = projHalf[0] / (k * (1.0f + m));
    }
    if (m != 0.0f) {
        projHalf[5] = projHalf[5] / (1.0f + m);
    }
}

/* PC port (photo mode, issue 14): one saved copy of every piece of the
   frame's camera that the camera update writes and later code reads, so
   photo mode (port/game/photo_view.c) can draw the paused game from its
   own camera and give the game its camera back unchanged.  The matrices
   at matrixptr and who writes them:
     +0x80   the view (camera-root.c MakeCameraMatrix / SetCameraMatrix)
     +0xC0   the screen matrix (gsb_SetVSMatrixSub's screen)
     +0x1C0  the projection on the 1500 unit screen (its proj)
     +0x240  the projection of the visible screen (its projHalf, widened
             by gsbHostWidenCull)
     +0x340  the viewport (its viewport)
     +0x640  the 500 unit pair's screen matrix (gsb_SetVSMatrixSub)
     +0x680  the 500 unit pair's projection (gsb_SetVSMatrixSub, widened
             by gsbHostWidenCull)
     +0x100  +0xC0 x +0x80 (gsb_MakeCommonMatrix)
     +0x200  +0x1C0 x +0x80 (gsb_MakeCommonMatrix)
     +0x280  +0x240 x +0x80 (gsb_MakeCommonMatrix)
     +0x380  the inverse of +0x80 (gsb_MakeCommonMatrix)
   and gsb_SetVSMatrix's record: vsParam, currentFocusDistance, vsWidth
   and vsHeight, center_X and center_Y, zoomCurrent (eased at every call)
   and gsbViewD.  The other matrixptr slots (+0x40, +0x140, +0x180,
   +0x300, +0x400 .. +0x4C0) are scratch each draw rewrites.  A full copy,
   not the five puddle.c's drawAreaRestore puts back: its reflection pass
   leaves +0x240, +0x640, +0x680 and vsParam at its 230 x 230 view. */
static const int gsbViewSlot[11] = {0x80,  0xC0,  0x1C0, 0x240, 0x340, 0x640,
                                    0x680, 0x100, 0x200, 0x280, 0x380};

static struct {
    float m[11][16];
    float vs[10];
    int focus;
    int w;
    int h;
    float cx;
    float cy;
    float zoom;
    float d;
} gsbViewSave;

/* Save the camera (the list above) into the one save slot, replacing what
   it held. */
void gsb_PushView(void)
{
    int i;

    for (i = 0; i < 11; i++) {
        memcpy(gsbViewSave.m[i], matrixptr + gsbViewSlot[i], sizeof(gsbViewSave.m[i]));
    }
    memcpy(gsbViewSave.vs, vsParam, sizeof(gsbViewSave.vs));
    gsbViewSave.focus = currentFocusDistance;
    gsbViewSave.w = vsWidth;
    gsbViewSave.h = vsHeight;
    gsbViewSave.cx = center_X;
    gsbViewSave.cy = center_Y;
    gsbViewSave.zoom = zoomCurrent;
    gsbViewSave.d = gsbViewD;
}

/* Put the saved camera back, byte for byte (the slot keeps it).  The
   renderer's copy of the camera follows at the next gsb_MakeCommonMatrix. */
void gsb_PopView(void)
{
    int i;

    for (i = 0; i < 11; i++) {
        memcpy(matrixptr + gsbViewSlot[i], gsbViewSave.m[i], sizeof(gsbViewSave.m[i]));
    }
    memcpy(vsParam, gsbViewSave.vs, sizeof(vsParam));
    currentFocusDistance = gsbViewSave.focus;
    vsWidth = gsbViewSave.w;
    vsHeight = gsbViewSave.h;
    center_X = gsbViewSave.cx;
    center_Y = gsbViewSave.cy;
    zoomCurrent = gsbViewSave.zoom;
    gsbViewD = gsbViewSave.d;
}

/* The distance of the last full-screen gsb_SetVSMatrix (the camera's
   gsb_SetVSMatrix(ScreenWidth, ScreenHeight, d) gives the same matrices
   again); currentFocusDistance before any. */
float gsb_ViewFocus(void)
{
    return gsbViewD != 0.0f ? gsbViewD : (float)currentFocusDistance;
}

#ifdef ICO_RD

/* PC port (R2c): gsb_MakeCommonMatrix's VU1 parameter block (the 16
   quadwords its packet unpacks to VU1 memory 0..15) and the frame's camera,
   from the scratchpad matrices as the packet copied them. */
static void gsbHostCommon(void)
{
    RdVuCommon b;
    RdCamera cam;
    int k;

    memset(&b, 0, sizeof(b));
    for (k = 0; k < 4; k++) {
        b.unitW[k] = commonMatrixHead.row[0][k];
        b.clip[k] = commonMatrixHead.row[1][k];
        b.zero[k] = commonMatrixHead.row[2][k];
        b.giftag[k] = (unsigned int)commonMatrixHead.tag[k];
    }
    _CopyMatrix(b.screenView, matrixptr + 0x100);
    _CopyMatrix(b.viewport, matrixptr + 0x340);
    _CopyMatrix(b.invView, matrixptr + 0x380);
    rd_set_vu_common(&b);
    memset(&cam, 0, sizeof(cam));
    _CopyMatrix(cam.view, matrixptr + 0x80);
    _CopyMatrix(cam.proj43, matrixptr + 0xC0);
    cam.zoom = vsParam[0];
    cam.aspect43 = 4.0f / 3.0f;
    cam.nearZ = vsParam[7];
    cam.farZ = vsParam[8];
    rd_set_camera(&cam);
}

#endif

/* PC port: the cull against every camera a blended picture is drawn with.
   With pictures blended between ticks (ico_video_interpolate) the
   presenter draws each tick's parts through cameras between the camera of
   the tick before and this tick's (rd_interp.c camSetup: the rotation
   slerped, the eye lerped, up to RD_INTERP_CAMERA_TURN degrees and
   RD_INTERP_CAMERA_MOVE units apart, rd_camera_blend.h).  A part culled against
   this tick's camera alone is missing from those pictures wherever the
   earlier cameras would show it: at the screen's edges while the camera
   turns or moves, worse the wider the picture (issue 29).  So while
   pictures are blended a box is culled only when it is outside three
   cameras: this tick's, the tick before's and the half-way one, built with
   the presenter's own blend (port/render/rd_camera_blend.h, the same
   functions rd_interp.c uses), each frustum with GSB_CULL_MARGIN.  Three
   turned frustums with the margin hold everything an in-between turn
   shows, but three eyes do not hold what an in-between eye shows: moving
   300 units across and up, the corner of the picture between two of the
   eyes sees parts within some 1000 units that no one of the three sees.
   So each camera also tests the box moved along the eye's path: the
   camera at eye e and an in-between eye e' see the same thing when the
   box moves by e - e', so the box is swept over that range (the hull of
   its corners at the two ends, which holds every place in between), for
   this tick's camera from the box to the box plus the eye's step d, for
   the tick before's from the box to the box minus d, for the half-way
   one from minus to plus half the step.  An in-between camera's turn is
   then held by one of the three turns, its eye by the sweep.

   The cull's matrix C is +0x280 (+0x240 x +0x80, gsb_MakeCommonMatrix),
   and RegistPacket.c culls with C x model (or +0x240 x a view-space matrix
   for the billboards, the same thing for a point of the world), so a
   corner r = C x model x v is seen by another camera C' as
   (C' x C^-1) x r: two matrices per camera, not per box.
     - gsbCullBuilt: the C gsb_MakeCommonMatrix made last, with the view
       (+0x80) and projection (+0x240) it made it from.
     - gsbCullPrev: the C in force at the flip (gsb_UpdateGSSystem), the
       camera of the tick the presenter blends from next.  Taken at the
       flip, not in gsb_MakeCommonMatrix, which photo mode calls twice on
       leaving and which returns at once while game_pause is 0.  Forgotten
       at gsb_InitGSSystem and gsb_ResetGSSystem: a stage's first tick has
       no earlier camera.
     - gsbCullR: R_prev = C_prev x C^-1 and R_mid = C_mid x C^-1, with
       C_mid the half-way projection (lerped, as the presenter's for the
       easing zoom) times the half-way view, and each camera's sweep, its
       C times the eye's step (w 0); built the first time a box is culled
       under a +0x280 other than the one they were built for.  The
       half-way camera and the sweep need both views; when +0x280 is not
       the one gsb_MakeCommonMatrix made (gsb_PopView put a saved one back)
       only R_prev is used, without the sweep.
   The result is exactly the PS2's (one camera) with the Original
   framerate, on a stage's first tick, while the camera stands still (C
   bitwise the tick before's: a still camera must not change a result
   through rounding), and for the parts locked to the camera (node flag 2:
   RegistPacket.c reg_setMMatrixPacket culls them through the 500 unit
   pair's +0x680; they move with the camera, so its earlier places mean
   nothing to them).  It is also one camera, as the pictures are, across a
   cut the presenter does not blend over: a hard cut or stage change the
   game signalled since the flip (ico_video_camera_cut), and a camera that
   turned more than RD_INTERP_CAMERA_TURN or whose eye moved more than
   RD_INTERP_CAMERA_MOVE since the tick before (rdcb_camera_jump, the
   presenter's own rule on the same two views).  Without that limit a cut
   kept everything in the swath between the two eyes for one tick, more
   packets than the banks hold.  The sweep is never longer than
   RD_INTERP_CAMERA_MOVE either.  When the views are not known (gsb_PopView
   put a saved +0x280 back) the step cannot be measured: the tick before's
   camera is added without the sweep, at most one more frustum. */
typedef struct GsbCullCam {
    int valid;
    int hasVP; /* v and p are the view and projection c was made from */
    float c[16];
    float v[16];
    float p[16];
} GsbCullCam; /* port */

static GsbCullCam gsbCullBuilt; /* port */

static GsbCullCam gsbCullPrev; /* port */

/* ico_video_cut_serial at the flip: a cut since is a different value */
static unsigned int gsbCullCutSerial; /* port */

/* bumped by every change of gsbCullPrev, so gsbCullR is rebuilt */
static unsigned int gsbCullGen = 1; /* port */

static struct {
    unsigned int gen; /* the gsbCullGen they were built under; 0 never */
    float cur[16];    /* the +0x280 they were built for */
    int views;        /* 1: this tick's camera only; 2: and R_prev; 3: and R_mid */
    double r[2][16];  /* R_prev, R_mid */
    double c[2][16];  /* C_prev, C_mid (the water dots test world points) */
    /* per camera (this tick's, the tick before's, the half-way one), the
       two ends of the sweep added to a corner in its clip space: 0 and
       C d; 0 and -C_prev d; +-C_mid d / 2 (all 0 without the sweep) */
    float sweep[3][2][4];
} gsbCullR; /* port */

/* 1 while RegistPacket.c culls a part locked to the camera */
static int gsbCullLocked; /* port */

/* the cameras the last gsb_ClipBox tested (the tests read it) */
static int gsbCullLastViews = 1; /* port */

static void gsbHostCullBuilt(void)
{
    memcpy(gsbCullBuilt.c, matrixptr + 0x280, sizeof(gsbCullBuilt.c));
    memcpy(gsbCullBuilt.v, matrixptr + 0x80, sizeof(gsbCullBuilt.v));
    memcpy(gsbCullBuilt.p, matrixptr + 0x240, sizeof(gsbCullBuilt.p));
    gsbCullBuilt.valid = 1;
    gsbCullBuilt.hasVP = 1;
}

static void gsbHostCullFlip(void)
{
    if (matrixptr == 0) {
        return;
    }
    memcpy(gsbCullPrev.c, matrixptr + 0x280, sizeof(gsbCullPrev.c));
    gsbCullPrev.hasVP =
        gsbCullBuilt.valid && memcmp(gsbCullBuilt.c, gsbCullPrev.c, sizeof(gsbCullPrev.c)) == 0;
    if (gsbCullPrev.hasVP) {
        memcpy(gsbCullPrev.v, gsbCullBuilt.v, sizeof(gsbCullPrev.v));
        memcpy(gsbCullPrev.p, gsbCullBuilt.p, sizeof(gsbCullPrev.p));
    }
    gsbCullPrev.valid = 1;
    gsbCullCutSerial = ico_video_cut_serial();
    gsbCullGen++;
}

static void gsbHostCullForget(void)
{
    gsbCullPrev.valid = 0;
    gsbCullGen++;
}

void gsb_HostCullCameraLocked(int on)
{
    gsbCullLocked = on != 0;
}

int gsb_HostCullViewsUsed(void)
{
    return gsbCullLastViews;
}

static void gsbHostLoad16(const float *f, double *d)
{
    int i;

    for (i = 0; i < 16; i++) {
        d[i] = f[i];
    }
}

/* The cameras a box is culled against now (1, 2 or 3), gsbCullR built
   for the +0x280 in force when it was not yet */
static int gsbHostCullViews(void)
{
    const float *cur = (const float *)(matrixptr + 0x280);
    double c[16], ic[16];

    if (!ico_video_interpolate() || !gsbCullPrev.valid) {
        return 1;
    }
    if (ico_video_cut_serial() != gsbCullCutSerial) {
        return 1; /* a cut since the flip: the pictures snap to this tick */
    }
    if (gsbCullR.gen == gsbCullGen && memcmp(gsbCullR.cur, cur, sizeof(gsbCullR.cur)) == 0) {
        return gsbCullR.views;
    }
    gsbCullR.gen = gsbCullGen;
    memcpy(gsbCullR.cur, cur, sizeof(gsbCullR.cur));
    gsbCullR.views = 1;
    if (memcmp(gsbCullPrev.c, cur, sizeof(gsbCullPrev.c)) == 0) {
        return 1; /* the camera stands still */
    }
    gsbHostLoad16(cur, c);
    if (!rdcb_invert4d(c, ic)) {
        return 1;
    }
    if (gsbCullPrev.hasVP && gsbCullBuilt.valid &&
        memcmp(gsbCullBuilt.c, cur, sizeof(gsbCullBuilt.c)) == 0 &&
        rdcb_camera_jump(gsbCullPrev.v, gsbCullBuilt.v)) {
        return 1; /* past the presenter's limits: the pictures snap */
    }
    gsbHostLoad16(gsbCullPrev.c, gsbCullR.c[0]);
    rdcb_mul4d(gsbCullR.c[0], ic, gsbCullR.r[0]);
    memset(gsbCullR.sweep, 0, sizeof(gsbCullR.sweep));
    gsbCullR.views = 2;
    if (gsbCullPrev.hasVP && gsbCullBuilt.valid &&
        memcmp(gsbCullBuilt.c, cur, sizeof(gsbCullBuilt.c)) == 0) {
        double vp[16], vc[16], vt[16], pp[16], pc[16], pt[16], ivp[16], ivc[16];

        gsbHostLoad16(gsbCullPrev.v, vp);
        gsbHostLoad16(gsbCullBuilt.v, vc);
        gsbHostLoad16(gsbCullPrev.p, pp);
        gsbHostLoad16(gsbCullBuilt.p, pc);
        if (rdcb_blend_view(vp, vc, 0.5, vt) && rdcb_invert4d(vp, ivp) && rdcb_invert4d(vc, ivc)) {
            /* the eye's step: the inverse views' translations, at most
               RD_INTERP_CAMERA_MOVE long */
            double d[4] = {ivc[12] - ivp[12], ivc[13] - ivp[13], ivc[14] - ivp[14], 0.0};
            const double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const double *cam[3] = {c, gsbCullR.c[0], gsbCullR.c[1]};
            static const double ends[3][2] = {{0.0, 1.0}, {0.0, -1.0}, {-0.5, 0.5}};
            int v;
            int e;
            int i;

            if (len > (double)RD_INTERP_CAMERA_MOVE) {
                for (i = 0; i < 3; i++) {
                    d[i] *= (double)RD_INTERP_CAMERA_MOVE / len;
                }
            }

            rdcb_blend_proj(pp, pc, 0.5, pt);
            rdcb_mul4d(pt, vt, gsbCullR.c[1]);
            rdcb_mul4d(gsbCullR.c[1], ic, gsbCullR.r[1]);
            for (v = 0; v < 3; v++) {
                for (i = 0; i < 4; i++) {
                    const double cd =
                        cam[v][i] * d[0] + cam[v][4 + i] * d[1] + cam[v][8 + i] * d[2];

                    for (e = 0; e < 2; e++) {
                        gsbCullR.sweep[v][e][i] = (float)(cd * ends[v][e]);
                    }
                }
            }
            gsbCullR.views = 3;
        }
    }
    return gsbCullR.views;
}

/* gsb_clipCorner's flags for a transformed corner r against |cw| */
static int gsbHostClipFlags(const float *r, float cw)
{
    float w = __builtin_fabsf(cw);
    int f = 0;

    f |= (r[0] > w) << 0;
    f |= (r[0] < -w) << 1;
    f |= (r[1] > w) << 2;
    f |= (r[1] < -w) << 3;
    f |= (r[2] > w) << 4;
    f |= (r[2] < -w) << 5;
    return f;
}

/* o = the double matrix m applied to the float r (all four fields) */
static void gsbHostApply(float *o, const double *m, const float *r)
{
    int i;

    for (i = 0; i < 4; i++) {
        o[i] = (float)(m[i] * r[0] + m[4 + i] * r[1] + m[8 + i] * r[2] + m[12 + i] * r[3]);
    }
}

/* gsb_ClipBox under views cameras: 0 when every camera has a plane all
   eight corners (at both ends of its sweep) are outside, -1 when no camera
   clips any of them, else 2 when one is closer than the near plane under
   any camera (the presenter's cameras lie between them, so one of the
   pictures can cross it), 1 when none is */
static int gsbHostClipBoxViews(const float *p, int views)
{
    int all[3] = {0x3F, 0x3F, 0x3F};
    int any[3] = {0, 0, 0};
    int nearCorner = 0;
    int culled = 1;
    int inside = 1;
    int i;
    int v;

    for (i = 0; i < 8; p += 4, i++) {
        float r[4];

        ico_apply_matrix_w1(r, (const float (*)[4])ico_current_matrix, p);
        for (v = 0; v < views; v++) {
            float o[4];
            int e;

            if (v == 0) {
                memcpy(o, r, sizeof(o));
            } else {
                gsbHostApply(o, gsbCullR.r[v - 1], r);
            }
            for (e = 0; e < 2; e++) {
                const float *a = gsbCullR.sweep[v][e];
                float s[4];
                int f;

                s[0] = o[0] + a[0];
                s[1] = o[1] + a[1];
                s[2] = o[2] + a[2];
                s[3] = o[3] + a[3];
                f = gsbHostClipFlags(s, s[3]);
                all[v] &= f;
                any[v] |= f;
                nearCorner |= gsbHostClipFlags(s, 0.99f) & 0x20;
            }
        }
    }
    for (v = 0; v < views; v++) {
        culled &= (all[v] & 0x2F) != 0;
        inside &= (any[v] & 0x2F) == 0;
    }
    if (culled) {
        return 0;
    }
    if (inside) {
        return -1;
    }
    return nearCorner ? 2 : 1;
}

/* PC port: the water dots' window (waterDot.c DispWaterDot), in the GS's
   1/16 pixels about the centre 32768: GSB_DOT_MARGIN pixels past the
   picture's sides and its top and bottom.  At width 1 the sides are 400
   pixels out, the PS2's window; the PS2's top and bottom were 200 pixels,
   inside the picture (dots near the top and bottom edges were not drawn).
   The widest picture (ICO_WIDE_X_MAX 5) keeps the window inside the 16
   bits the dot's x is packed in: 32768 + (1280 + 144) x 16 = 55552. */
#define GSB_DOT_MARGIN 144.0f

/* 1 when the world point pos is in front of the cull matrix c and inside
   its x and y planes */
static int gsbHostPointIn(const double *c, const float *pos)
{
    double r[4];
    int i;

    for (i = 0; i < 4; i++) {
        r[i] = c[i] * pos[0] + c[4 + i] * pos[1] + c[8 + i] * pos[2] + c[12 + i];
    }
    return r[3] > 0.0 && fabs(r[0]) <= r[3] && fabs(r[1]) <= r[3];
}

int gsb_HostDotVisible(const int *ip, const float *pos)
{
    const float k = gsbHostWideX();
    const int hx = (int)(((float)(ScreenWidth / 2) * k + GSB_DOT_MARGIN) * 16.0f);
    const int hy = (int)(((float)(ScreenHeight / 2) + GSB_DOT_MARGIN) * 16.0f);
    int views;
    int v;

    if (ip[0] >= 32768 - hx && ip[0] <= 32768 + hx && ip[1] >= 32768 - hy && ip[1] <= 32768 + hy) {
        return 1;
    }
    /* seen by an earlier camera of the blended pictures (the cull's
       frustum, which carries GSB_CULL_MARGIN); only where the tick's GS
       position still packs into 16 bits */
    if (ip[0] < 0 || ip[0] > 0xFFFF || ip[1] < 0 || ip[1] > 0xFFFF) {
        return 0;
    }
    views = gsbHostCullViews();
    for (v = 1; v < views; v++) {
        if (gsbHostPointIn(gsbCullR.c[v - 1], pos)) {
            return 1;
        }
    }
    return 0;
}

/* PC port: lineManager.c _getLine's x clip, k times the 4:3 half-width
   (exactly the PS2's vsWidth * 0.5 at width 1).  At width 5 a 512 wide
   view reaches 2048 +- 1280, inside the GS's 0..4095. */
float gsb_HostLineHalfWidth(void)
{
    return (float)vsWidth * 0.5f * gsbHostWideX();
}

/* PC port: Shadow.c's edge clip, +-ScreenWidth about the centre on the
   PS2, k times that on a wide picture, at most 2047 pixels so a clipped
   end stays inside the 16-bit GS window (2048 +- 2047) */
float gsb_HostShadowClipX(void)
{
    float x = (float)ScreenWidth * gsbHostWideX();

    return x > 2047.0f ? 2047.0f : x;
}

/* Clip a box against the current matrix: transform its eight corners with the
 * matrix in $vf4 to $vf7 and read the clip flags out of $vi18.  All eight
 * corners outside one plane gives 0, no corner clipped at all gives -1, and
 * the second pass re-clips against a 0.99 w so a corner that only just crosses
 * the near plane still counts as visible: 2 when it does, 1 when it does not. */

/* gsb_ClipBox's VU0 step on the host: the corner through the current
 * matrix with w taken as 1 (vmaddw by vf0w), then vclipw.xyz against |cw|
 * (cw is the transformed w, or the constant 0.99 of the second pass):
 * +x, -x, +y, -y, +z, -z in bits 0-5.  Only those bits are tested, so the
 * older judgments VU0 keeps above them are left out. */
static int gsb_clipCorner(const float *v, int useConst) /* derived name */
{
    float r[4];
    float w;
    int f = 0;

    ico_apply_matrix_w1(r, (const float (*)[4])ico_current_matrix, v);
    w = __builtin_fabsf(useConst ? 0.99f : r[3]);
    f |= (r[0] > w) << 0;
    f |= (r[0] < -w) << 1;
    f |= (r[1] > w) << 2;
    f |= (r[1] < -w) << 3;
    f |= (r[2] > w) << 4;
    f |= (r[2] < -w) << 5;
    return f;
}

int gsb_ClipBox(float *p)
{
    int all = 0x3F;
    int any = 0;
    int c = 0;
    float *q = p;
    int i;

    /* PC port: while pictures are blended, the cameras they are drawn
       with (above) */
    if (!gsbCullLocked) {
        int views = gsbHostCullViews();

        gsbCullLastViews = views;
        if (views > 1) {
            return gsbHostClipBoxViews(p, views);
        }
    } else {
        gsbCullLastViews = 1;
    }
    for (i = 0; i < 8; q += 4, i++) {
        int cf;

        cf = gsb_clipCorner(q, 0);
        all &= cf;
        any |= cf;
    }
    if (all & 0x2F) {
        return 0;
    }
    if ((any & 0x2F) == 0) {
        return -1;
    }
    for (i = 0; i < 8; p += 4, i++) {
        int cf;

        cf = gsb_clipCorner(p, 1);
        c |= cf;
    }
    return (c & 0x20) ? 2 : 1;
}

/* libcdvd.h leaves sceCdCLOCK incomplete; its body, in BCD */
typedef struct sceCdCLOCK {
    unsigned char stat;
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char pad;
    unsigned char day;
    unsigned char month;
    unsigned char year;
} sceCdCLOCK;

inline int gsb_LoadStageSettings(void)
{
    char buf[256];
    int fd;
    sprintf(buf, "object/stagesetting/%s.ssb", stageData[stage_no].key2);
    fd = debugSceOpen(buf, 1);
    if (fd < 0) {
        debug_StdPrintfDummy("gsb_LoadStageSettings: host file open error.\n");
    } else {
        debug_StdPrintfDummy("Load stage settings file. %s\n", buf);
        sceRead(fd, &GlobalStageSetting, 0x1D0);
        debugSceClose(fd);
    }
    return -1;
}

/* Scratch for the editing log: first the log file's name, then the line
 * appended to it. */
static char logBuf[256]; /* derived name */

static void appendLogFile(void)
{
    sceCdCLOCK clock;
    int fd;

    sceCdReadClock(&clock);
    sprintf(logBuf, "object/stagesetting/change.txt");
    fd = debugSceOpen(logBuf, 0x302);
    if (fd < 0) {
        debug_StdPrintfDummy("change.txt open error.\n");
        return;
    }
    sprintf(logBuf, "%04x/%02x/%02x %02x:%02x:%02x : stage %s  edit by %s\n", clock.year | 0x2000,
            clock.month, clock.day, clock.hour, clock.minute, clock.second,
            stageData[stage_no].key2, "horagai");
    sceLseek(fd, 0, 2);
    sceWrite(fd, logBuf, strlen(logBuf));
    debugSceClose(fd);
    debug_StdPrintfDummy(logBuf);
}

inline int gsb_SaveStageSettings(void)
{
    char buf[256];
    int fd;
    if (otherEditingLocked == 0) {
        sprintf(buf, "object/stagesetting/%s.ssb", stageData[stage_no].key2);
        fd = debugSceOpen(buf, 0x602);
        if (fd < 0) {
            debug_StdPrintfDummy("gsb_SaveStageSettings: host file open error.\n");
            return -1;
        }
        sceWrite(fd, &GlobalStageSetting, 0x1D0);
        debug_StdPrintfDummy("Save stage settings file. %s\n", buf);
        debugSceClose(fd);
        appendLogFile();
    }
    return -1;
}

/* one row of the film noise debug menu (the same shape ico2/seki/src/ZFog.c
   carries for the fog tool): a label, the word it edits, whether that word is
   a float, its range, its default and its step, and the callback to run once
   the value has moved. */
typedef struct GsbToolItem { /* field names derived */
    char *name;              /* 0x00 */
    void *val;               /* 0x04 */
    int isFloat;             /* 0x08 */
    float min;               /* 0x0C */
    float max;               /* 0x10 */
    float def;               /* 0x14 */
    float step;              /* 0x18 */
    int (*fn)();             /* 0x1C */
} GsbToolItem;               /* derived name */

/* the four pages of seven rows, one page per render target, each row naming
   a word of the stage record */
static const GsbToolItem filmNoiseItems[4][7] = {
    /* derived name */
    {
        {" HighLight Color R ", &GlobalStageSetting.targetCol[0][0], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color G ", &GlobalStageSetting.targetCol[0][1], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color B ", &GlobalStageSetting.targetCol[0][2], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" Noise Level       ", &GlobalStageSetting.targetCol[0][3], 0, 0.0f, 255.0f, 8.0f, 1.0f,
         0},
        {" Motion Blur       ", &GlobalStageSetting.subMotionBlur[0], 0, 0.0f, 127.0f, 32.0f, 1.0f,
         0},
        {" AntiLevel0        ", &GlobalStageSetting.antiLevel[0].a, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
        {" AntiLevel1        ", &GlobalStageSetting.antiLevel[0].b, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
    },
    {
        {" HighLight Color R ", &GlobalStageSetting.targetCol[1][0], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color G ", &GlobalStageSetting.targetCol[1][1], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color B ", &GlobalStageSetting.targetCol[1][2], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" Noise Level       ", &GlobalStageSetting.targetCol[1][3], 0, 0.0f, 255.0f, 16.0f, 1.0f,
         0},
        {" Motion Blur       ", &GlobalStageSetting.subMotionBlur[1], 0, 0.0f, 127.0f, 32.0f, 1.0f,
         0},
        {" AntiLevel0        ", &GlobalStageSetting.antiLevel[1].a, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
        {" AntiLevel1        ", &GlobalStageSetting.antiLevel[1].b, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
    },
    {
        {" HighLight Color R ", &GlobalStageSetting.targetCol[2][0], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color G ", &GlobalStageSetting.targetCol[2][1], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color B ", &GlobalStageSetting.targetCol[2][2], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" Noise Level       ", &GlobalStageSetting.targetCol[2][3], 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
        {" Motion Blur       ", &GlobalStageSetting.subMotionBlur[2], 0, 0.0f, 127.0f, 32.0f, 1.0f,
         0},
        {" AntiLevel0        ", &GlobalStageSetting.antiLevel[2].a, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
        {" AntiLevel1        ", &GlobalStageSetting.antiLevel[2].b, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
    },
    {
        {" HighLight Color R ", &GlobalStageSetting.targetCol[3][0], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color G ", &GlobalStageSetting.targetCol[3][1], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" HighLight Color B ", &GlobalStageSetting.targetCol[3][2], 0, 0.0f, 255.0f, 128.0f, 1.0f,
         0},
        {" Noise Level       ", &GlobalStageSetting.targetCol[3][3], 0, 0.0f, 255.0f, 32.0f, 1.0f,
         0},
        {" Motion Blur       ", &GlobalStageSetting.subMotionBlur[3], 0, 0.0f, 127.0f, 32.0f, 1.0f,
         0},
        {" AntiLevel0        ", &GlobalStageSetting.antiLevel[3].a, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
        {" AntiLevel1        ", &GlobalStageSetting.antiLevel[3].b, 0, 0.0f, 255.0f, 24.0f, 1.0f,
         0},
    },
};

/* the unselected and selected row colours, as ZFog's fogRowColor */
static const unsigned int filmNoiseRowColor[] = {0xFFFFFF00, 0xFF000000}; /* derived name */

/* the word a boolean row prints */
static char *filmNoiseOnOffText[] = {"Off", "On"}; /* derived name */

static int filmNoiseRow = 0; /* derived name */ /* the highlighted row */

/* The film noise page of the debug menu: seven editable words of the stage
 * record for the target this page names, the pad keys that walk and change
 * them, the key that dumps the page to the log, and the key that copies this
 * target's tint and blur over the main ones. */
static int gsb_FilmNoiseTool(int target)
{
    int i;
    int ret = 0;
    int page = target;

    debug_PrintfDummy(10, 30, 0xFF800000, "Film Noise Pattern %d", target);

    for (i = 0; i < 7; i++) {
        if (filmNoiseItems[target][i].min == 0.0f && filmNoiseItems[target][i].max == 1.0f &&
            filmNoiseItems[target][i].isFloat == 0) {
            debug_PrintfDummy(18, (i + 1) * 8 + 30, filmNoiseRowColor[(filmNoiseRow == i) ? 1 : 0],
                              "%s : %s", filmNoiseItems[target][i].name,
                              filmNoiseOnOffText[*(int *)filmNoiseItems[target][i].val]);
        } else if (filmNoiseItems[target][i].isFloat == 0) {
            debug_PrintfDummy(18, (i + 1) * 8 + 30, filmNoiseRowColor[(filmNoiseRow == i) ? 1 : 0],
                              "%s : %d", filmNoiseItems[target][i].name,
                              *(int *)filmNoiseItems[target][i].val);
        } else {
            debug_PrintfDummy(18, (i + 1) * 8 + 30, filmNoiseRowColor[(filmNoiseRow == i) ? 1 : 0],
                              "%s : %f", filmNoiseItems[target][i].name,
                              *(float *)filmNoiseItems[target][i].val);
        }
    }

    if (pad[0].rep & 0x4000) {
        if (++filmNoiseRow >= 7) {
            filmNoiseRow = 0;
        }
    }
    if (pad[0].rep & 0x1000) {
        if (--filmNoiseRow < 0) {
            filmNoiseRow = 6;
        }
    }
    if (pad[0].rep & 0x2000) {
        if (filmNoiseItems[page][filmNoiseRow].isFloat == 0) {
            int v = (float)*(int *)filmNoiseItems[page][filmNoiseRow].val +
                    filmNoiseItems[page][filmNoiseRow].step;

            *(int *)filmNoiseItems[page][filmNoiseRow].val = v;
            if (filmNoiseItems[page][filmNoiseRow].max < (float)v) {
                *(int *)filmNoiseItems[page][filmNoiseRow].val =
                    filmNoiseItems[page][filmNoiseRow].min;
            }
        } else {
            float v = *(float *)filmNoiseItems[page][filmNoiseRow].val +
                      filmNoiseItems[page][filmNoiseRow].step;

            *(float *)filmNoiseItems[page][filmNoiseRow].val = v;
            if (filmNoiseItems[page][filmNoiseRow].max < v) {
                *(float *)filmNoiseItems[page][filmNoiseRow].val =
                    filmNoiseItems[page][filmNoiseRow].min;
            }
        }
        if (filmNoiseItems[page][filmNoiseRow].fn != 0) {
            filmNoiseItems[page][filmNoiseRow].fn(0);
        }
    }
    if (pad[0].rep & 0x8000) {
        if (filmNoiseItems[page][filmNoiseRow].isFloat == 0) {
            int v = (float)*(int *)filmNoiseItems[page][filmNoiseRow].val -
                    filmNoiseItems[page][filmNoiseRow].step;

            *(int *)filmNoiseItems[page][filmNoiseRow].val = v;
            if ((float)v < filmNoiseItems[page][filmNoiseRow].min) {
                *(int *)filmNoiseItems[page][filmNoiseRow].val =
                    filmNoiseItems[page][filmNoiseRow].max;
            }
        } else {
            float v = *(float *)filmNoiseItems[page][filmNoiseRow].val -
                      filmNoiseItems[page][filmNoiseRow].step;

            *(float *)filmNoiseItems[page][filmNoiseRow].val = v;
            if (v < filmNoiseItems[page][filmNoiseRow].min) {
                *(float *)filmNoiseItems[page][filmNoiseRow].val =
                    filmNoiseItems[page][filmNoiseRow].max;
            }
        }
        if (filmNoiseItems[page][filmNoiseRow].fn != 0) {
            filmNoiseItems[page][filmNoiseRow].fn(0);
        }
    }
    if (pad[0].flags & 0x10) {
        if (filmNoiseItems[page][filmNoiseRow].isFloat == 0) {
            *(int *)filmNoiseItems[page][filmNoiseRow].val = filmNoiseItems[page][filmNoiseRow].def;
        } else {
            *(float *)filmNoiseItems[page][filmNoiseRow].val =
                filmNoiseItems[page][filmNoiseRow].def;
        }
    }
    if (pad[0].flags & 0x20) {
        for (i = 0; i < 7; i++) {
            if (filmNoiseItems[page][i].min == 0.0f && filmNoiseItems[page][i].max == 1.0f &&
                filmNoiseItems[page][i].isFloat == 0) {
                debug_StdPrintfDummy("StageSetting %s => %s\n", filmNoiseItems[page][i].name,
                                     filmNoiseOnOffText[*(int *)filmNoiseItems[page][i].val]);
            } else if (filmNoiseItems[page][i].isFloat == 0) {
                debug_StdPrintfDummy("StageSetting %s => %d\n", filmNoiseItems[page][i].name,
                                     *(int *)filmNoiseItems[page][i].val);
            } else {
                debug_StdPrintfDummy("StageSetting %s => %f\n", filmNoiseItems[page][i].name,
                                     *(float *)filmNoiseItems[page][i].val);
            }
        }
        ret = 1;
    }
    if (pad[0].flags & 0x80) {
        GlobalStageSetting.targetCol[target][0] = GlobalStageSetting.reductionCol[0];
        GlobalStageSetting.targetCol[target][1] = GlobalStageSetting.reductionCol[1];
        GlobalStageSetting.targetCol[target][2] = GlobalStageSetting.reductionCol[2];
        GlobalStageSetting.subMotionBlur[target] = GlobalStageSetting.motionBlur;
        GlobalStageSetting.antiLevel[target].a = GlobalStageSetting.antiLevel0;
        GlobalStageSetting.antiLevel[target].b = GlobalStageSetting.antiLevel1;
    }
    if (pad[0].flags & 0x40) {
        ret = -1;
    }
    if (ret != 0) {
        filmNoiseRow = 0;
    }
    return ret;
}

/* the twenty one rows of the stage setting page, each naming a word of the
   stage record (the words at 0xE4..0xF0, 0xF8, 0x104..0x11C and 0x180..0x190
   are still padding in typedef.h's StageSetting) */
static const GsbToolItem stageSettingItems[] = {
    /* derived name */
    {" HighLight Color R   ", &GlobalStageSetting.reductionCol[0], 0, 0.0f, 255.0f, 128.0f, 1.0f,
     0},
    {" HighLight Color G   ", &GlobalStageSetting.reductionCol[1], 0, 0.0f, 255.0f, 128.0f, 1.0f,
     0},
    {" HighLight Color B   ", &GlobalStageSetting.reductionCol[2], 0, 0.0f, 255.0f, 128.0f, 1.0f,
     0},
    {" Zoom Offset         ", &GlobalStageSetting.viewScale, 0, 5e+01f, 4e+02f, 1e+02f, 1.0f, 0},
    {" Def Tex Sample Mode ", &GlobalStageSetting.texSampleMode, 0, 0.0f, 5.0f, 5.0f, 1.0f,
     tex_RemakeRegistersSampleMin},
    {" Post Effect         ", &GlobalStageSetting.postEffect, 0, 0.0f, 8.0f, 0.0f, 1.0f, 0},
    {" Feedback Effect     ", &GlobalStageSetting.feedbackEffect, 0, 0.0f, 3.0f, 0.0f, 1.0f, 0},
    {" Feedback Effect R   ", &GlobalStageSetting.feedbackCol[0], 0, 0.0f, 255.0f, 0.0f, 1.0f, 0},
    {" Feedback Effect G   ", &GlobalStageSetting.feedbackCol[1], 0, 0.0f, 255.0f, 0.0f, 1.0f, 0},
    {" Feedback Effect B   ", &GlobalStageSetting.feedbackCol[2], 0, 0.0f, 255.0f, 0.0f, 1.0f, 0},
    {" Feedback Effect A   ", &GlobalStageSetting.feedbackCol[3], 0, 0.0f, 255.0f, 0.0f, 1.0f, 0},
    {" DepthField Level    ", &GlobalStageSetting.depthFieldLevel, 0, 0.0f, 1e+03f, 1e+02f, 1.0f,
     0},
    {" DepthField Start    ", &GlobalStageSetting.depthFieldStart, 0, 0.0f, 2e+04f, 2e+03f, 2e+01f,
     0},
    {" DepthField Width    ", &GlobalStageSetting.depthFieldWidth, 0, 0.0f, 2e+04f, 1e+04f, 2e+01f,
     0},
    {" HandCamera Limit P  ", &GlobalStageSetting.handCameraLimitP, 0, 0.0f, 1.8e+02f, 1.2e+02f,
     1.0f, UpdateHandCameraLimitP},
    {" HandCamera Limit V  ", &GlobalStageSetting.handCameraLimitV, 0, 0.0f, 9e+01f, 8e+01f, 1.0f,
     UpdateHandCameraLimitV},
    {" ZOOM MAX IN DEMO    ", &GlobalStageSetting.zoomMaxInDemo, 0, 0.0f, 3e+02f, 2e+02f, 1.0f,
     UpdateZoomMaxVallInDemo},
    {" Motion Blur         ", &GlobalStageSetting.motionBlur, 0, 0.0f, 127.0f, 32.0f, 1.0f, 0},
    {" AntiLevel0          ", &GlobalStageSetting.antiLevel0, 0, 0.0f, 255.0f, 0.0f, 1.0f, 0},
    {" AntiLevel1          ", &GlobalStageSetting.antiLevel1, 0, 0.0f, 255.0f, 0.0f, 1.0f, 0},
    {" Film Noise Tex Rep  ", &GlobalStageSetting.grainScale, 1, 1.0f, 8.0f, 6.0f, 0.1f, 0},
};

/* the unselected and selected row colours, as filmNoiseRowColor */
static const unsigned int stageSettingRowColor[] = {0xFFFFFF00, 0xFF000000}; /* derived name */

/* the stage setting page's own copy of the same pair */
static char *stageSettingOnOffText[] = {"Off", "On"}; /* derived name */

static int stageSettingRow = 0; /* derived name */ /* the highlighted row */

/* The stage setting page of the debug menu: twenty one editable words of the
 * stage record, the pad keys that walk and change them, and the key that dumps
 * the page to the log. */
int gsb_StageSettingTool(void)
{
    int i;
    int ret = 0;

    debug_PrintfDummy(10, 30, 0xFF800000, "StageSetting Tool");

    for (i = 0; i < 21; i++) {
        if (stageSettingItems[i].min == 0.0f && stageSettingItems[i].max == 1.0f &&
            stageSettingItems[i].isFloat == 0) {
            debug_PrintfDummy(18, (i + 1) * 8 + 30,
                              stageSettingRowColor[(stageSettingRow == i) ? 1 : 0], "%s : %s",
                              stageSettingItems[i].name,
                              stageSettingOnOffText[*(int *)stageSettingItems[i].val]);
        } else if (stageSettingItems[i].isFloat == 0) {
            debug_PrintfDummy(18, (i + 1) * 8 + 30,
                              stageSettingRowColor[(stageSettingRow == i) ? 1 : 0], "%s : %d",
                              stageSettingItems[i].name, *(int *)stageSettingItems[i].val);
        } else {
            debug_PrintfDummy(18, (i + 1) * 8 + 30,
                              stageSettingRowColor[(stageSettingRow == i) ? 1 : 0], "%s : %f",
                              stageSettingItems[i].name, *(float *)stageSettingItems[i].val);
        }
    }

    if (pad[0].rep & 0x4000) {
        if (++stageSettingRow >= 21) {
            stageSettingRow = 0;
        }
    }
    if (pad[0].rep & 0x1000) {
        if (--stageSettingRow < 0) {
            stageSettingRow = 20;
        }
    }
    if (pad[0].rep & 0x2000) {
        if (stageSettingItems[stageSettingRow].isFloat == 0) {
            int v = (float)*(int *)stageSettingItems[stageSettingRow].val +
                    stageSettingItems[stageSettingRow].step;

            *(int *)stageSettingItems[stageSettingRow].val = v;
            if (stageSettingItems[stageSettingRow].max < (float)v) {
                *(int *)stageSettingItems[stageSettingRow].val =
                    stageSettingItems[stageSettingRow].min;
            }
        } else {
            float v = *(float *)stageSettingItems[stageSettingRow].val +
                      stageSettingItems[stageSettingRow].step;

            *(float *)stageSettingItems[stageSettingRow].val = v;
            if (stageSettingItems[stageSettingRow].max < v) {
                *(float *)stageSettingItems[stageSettingRow].val =
                    stageSettingItems[stageSettingRow].min;
            }
        }
        if (stageSettingItems[stageSettingRow].fn != 0) {
            stageSettingItems[stageSettingRow].fn(0);
        }
    }
    if (pad[0].rep & 0x8000) {
        if (stageSettingItems[stageSettingRow].isFloat == 0) {
            int v = (float)*(int *)stageSettingItems[stageSettingRow].val -
                    stageSettingItems[stageSettingRow].step;

            *(int *)stageSettingItems[stageSettingRow].val = v;
            if ((float)v < stageSettingItems[stageSettingRow].min) {
                *(int *)stageSettingItems[stageSettingRow].val =
                    stageSettingItems[stageSettingRow].max;
            }
        } else {
            float v = *(float *)stageSettingItems[stageSettingRow].val -
                      stageSettingItems[stageSettingRow].step;

            *(float *)stageSettingItems[stageSettingRow].val = v;
            if (v < stageSettingItems[stageSettingRow].min) {
                *(float *)stageSettingItems[stageSettingRow].val =
                    stageSettingItems[stageSettingRow].max;
            }
        }
        if (stageSettingItems[stageSettingRow].fn != 0) {
            stageSettingItems[stageSettingRow].fn(0);
        }
    }
    if (pad[0].flags & 0x10) {
        if (stageSettingItems[stageSettingRow].isFloat == 0) {
            *(int *)stageSettingItems[stageSettingRow].val = stageSettingItems[stageSettingRow].def;
        } else {
            *(float *)stageSettingItems[stageSettingRow].val =
                stageSettingItems[stageSettingRow].def;
        }
    }
    if (pad[0].flags & 0x20) {
        for (i = 0; i < 21; i++) {
            if (stageSettingItems[i].min == 0.0f && stageSettingItems[i].max == 1.0f &&
                stageSettingItems[i].isFloat == 0) {
                debug_StdPrintfDummy("StageSetting %s => %s\n", stageSettingItems[i].name,
                                     stageSettingOnOffText[*(int *)stageSettingItems[i].val]);
            } else if (stageSettingItems[i].isFloat == 0) {
                debug_StdPrintfDummy("StageSetting %s => %d\n", stageSettingItems[i].name,
                                     *(int *)stageSettingItems[i].val);
            } else {
                debug_StdPrintfDummy("StageSetting %s => %f\n", stageSettingItems[i].name,
                                     *(float *)stageSettingItems[i].val);
            }
        }
        ret = 1;
    }
    if (pad[0].flags & 0x40) {
        ret = -1;
    }
    if (ret != 0) {
        stageSettingRow = 0;
    }
    return ret;
}

/* The stage lock file's name, and the owner name read back out of it. */
static char lockFileName[256]; /* derived name */

static char lockOwner[72]; /* derived name */

static void updateOtherEditingLockFlag(void)
{
    char buf[256];
    int fd;

    sprintf(lockFileName, "object/stagesetting/%s.lock", stageData[stage_no].key2);
    otherEditingLocked = 0;
    fd = debugSceOpen(lockFileName, 1);
    if (fd >= 0) {
        sceRead(fd, buf, 0x100);
        debugSceClose(fd);
        sscanf(buf, "%s\n", lockOwner);
    }
    if (fd < 0 || strcmp("nouser", lockOwner) == 0) {
        debug_StdPrintfDummy("no lock\n");
        editingSettings = 0;
    } else if (strcmp("horagai", lockOwner) != 0) {
        debug_StdPrintfDummy("lock by \"%s\"\n", lockOwner);
        otherEditingLocked = 1;
    } else {
        editingSettings = 1;
    }
}

/* the lock file's name, built at the head of updateOtherEditingLockFlag,
 * createLockFile and removeLockFile */
static inline char *makeLockFileName(void) /* derived name */
{
    sprintf(lockFileName, "object/stagesetting/%s.lock", stageData[stage_no].key2);
    return lockFileName;
}

static int createLockFile(void)
{
    char buf[256];
    char *name = makeLockFileName();
    int fd = debugSceOpen(name, 0x602);
    if (fd < 0) {
        debug_StdPrintfDummy("cant create lock file\n");
        return 0;
    }
    sprintf(buf, "%s\n", "horagai");
    sceWrite(fd, buf, strlen(buf) + 1);
    debugSceClose(fd);
    debug_StdPrintfDummy(" create lock file \"%s\" by %s\n", name, buf);
    editingSettings = 1;
    return 1;
}

static int removeLockFile(void)
{
    char buf[256];
    char *name = makeLockFileName();
    int fd = debugSceOpen(name, 0x602);
    if (fd < 0) {
        debug_StdPrintfDummy("cant remove lock file\n");
        return 0;
    }
    sprintf(buf, "%s\n", "nouser");
    sceWrite(fd, buf, strlen(buf) + 1);
    debugSceClose(fd);
    debug_StdPrintfDummy(" remove lock file \"%s\" by %s\n", name, "horagai");
    editingSettings = 0;
    return 1;
}

typedef struct { /* field names derived */
    char *name;  /* 0x0 */
    int (*fn)(); /* 0x4 */
    int arg;     /* 0x8 */
} GsbMenuItem;   /* derived name */

/* The background colour the display list is cleared to, one component per
 * word of an integer quadword: gsb_SetBGColor writes the four words and
 * gsb_GetBGColor reads them back as bytes. */
static sceVu0IVECTOR bgColor; /* derived name */

inline void gsb_SetBGColor(void *db, int r, int g, int b)
{
    unsigned long long bg = ((long long)b << 16) | ((long long)g << 8);
    unsigned long long v = r | 0x3F80000000000000ULL;
    v |= bg;
    bgColor[0] = r;
    v |= 0x80000000;
    bgColor[1] = g;
    bgColor[2] = b;
    bgColor[3] = 0x80;
    *(unsigned long long *)((char *)db + 0x1F0) = v;
    *(unsigned long long *)((char *)db + 0x100) = v;
}

inline void gsb_GetBGColor(unsigned char *col)
{
    col[0] = bgColor[0];
    col[1] = bgColor[1];
    col[2] = bgColor[2];
    col[3] = bgColor[3];
}

inline void gsb_SetZoom(float target, float speed)
{
    zoomTarget = target;
    zoomSpeed = speed;
}

inline int lockOtherEditing(void)
{
    updateOtherEditingLockFlag();
    if (otherEditingLocked != 0) {
        return -1;
    }
    createLockFile();
    gsb_LoadStageSettings();
    return -1;
}

inline int unlockOtherEditing(void)
{
    updateOtherEditingLockFlag();
    if (otherEditingLocked != 0) {
        return -1;
    }
    gsb_LoadStageSettings();
    removeLockFile();
    return -1;
}

static GsbMenuItem lockedMenu[] = {
    {"LOCK OTHER EDITING", lockOtherEditing, 0},
}; /* derived name */

static GsbMenuItem stageSettingMenu[] = {
    {"Light Tool", light_Tool, 0},
    {"Shadow Tool", shadow_Tool, 0},
    {"Fog Tool", fog_FogTool, 0},
    {"Film Noise 1", gsb_FilmNoiseTool, 0},
    {"Film Noise 2", gsb_FilmNoiseTool, 1},
    {"Film Noise 3", gsb_FilmNoiseTool, 2},
    {"Film Noise 4", gsb_FilmNoiseTool, 3},
    {"Other Settings", gsb_StageSettingTool, 0},
    {"Load Settings", gsb_LoadStageSettings, 0},
    {"Save Settings", gsb_SaveStageSettings, 0},
    {"UnLock Quit", unlockOtherEditing, 0},
}; /* derived name */

/* the unselected and selected row colours, as filmNoiseRowColor */
static const unsigned int menuRowColor[] = {0xFFFFFF00, 0xFF000000}; /* derived name */

/* the menu row under the cursor and the page it opened, -1 for none */
static int menuCursor = 0; /* derived name */

static int menuSelected = -1; /* derived name */

int gsb_StageSetting(void)
{
    int i;
    editingSettings = 1;
    if (menuSelected >= 0) {
        if (stageSettingMenu[menuSelected].fn != 0) {
            int r = stageSettingMenu[menuSelected].fn(stageSettingMenu[menuSelected].arg);
            if (r == -1) {
                menuSelected = r;
            }
            return 0;
        }
    }
    if (editingSettings) {
        for (i = 0; i < 11; i++) {
            debug_PrintfDummy(18, (i + 1) * 8 + 0x1E, menuRowColor[(menuCursor == i) ? 1 : 0], "%s",
                              stageSettingMenu[i].name);
        }
        if (pad[0].rep & 0x4000) {
            menuCursor++;
            if (menuCursor >= 11)
                menuCursor = 0;
        }
        if (pad[0].rep & 0x1000) {
            menuCursor--;
            if (menuCursor < 0)
                menuCursor = 10;
        }
        if (pad[0].flags & 0x20) {
            menuSelected = menuCursor;
        }
    } else {
        debug_PrintfDummy(26, 22, 0xFFFFFFFF, "NO ONE EDITS THIS STAGE'S SETTING.");
        debug_PrintfDummy(18, 38, menuRowColor[1], "%s", lockedMenu[0].name);
        if (pad[0].flags & 0x20) {
            lockedMenu[0].fn(1);
        }
    }
    return (pad[0].flags & 0x40) ? -1 : 0;
}
