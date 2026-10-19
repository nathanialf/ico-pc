/* rd_gsbase_test.c: GsBase.c's host path on rd.
 *
 * GsBase.c, GifPacket.c, DisplayList.c and DmaPacket.c compiled as the
 * window build compiles them (ICO_HOST, ICO_RD), with the rest of the game
 * stubbed below.  The test drives the real gsb_InitGSSystem,
 * gsb_SyncGSSystem (gsb_PostEffect) and gsb_UpdateGSSystem; the "game" draws
 * a synthetic scene into list 0 of each frame through rd.
 *
 * Recording (no device needed):
 *   bg       the frame head's clear takes the BG colour current at the flip
 *            that kicks the frame (changed mid-tick), not at the frame's start
 *   keep     an fbKeep frame keeps the head copy in list 11 (list 0's is
 *            NOPed), a full frame the copy in list 0 (list 11's is NOPed);
 *            the reduction tint is 128 in a keep frame
 *   parity   no half offset on any flip: the frame head's TARGET offset is
 *            RD_TARGET_OFFSET alone whether GS_CSR.FIELD is 0, 1 or
 *            alternating per flip, and through gsb_ResetGSSystem
 *   leak     the post passes leave their state: fade -> ALPHA 0x44, ABE,
 *            PABE 0; keep -> TEXA 80/80, DISPLAY bound
 *   mask     FBMSK's extent: FRAME.FBMSK masked in list 10 holds
 *            for a draw after it and ends at the anti-alias pass's FRAME
 *            write or the reduction's
 *   vu       gsb_MakeCommonMatrix's VU block and the frame camera
 *   zscale   rd__gs_depth: 0xFFFFFF9B and 0xFFFFFFFF apart under PSMZ32, and
 *            Z 17 apart at 2^24 (the depth grows with Z)
 *   photo    photo mode's camera in the game's matrices
 *            (port/game/photo_view.c): gsb_PushView / gsb_PopView put back
 *            the eleven matrices, vsParam, the focus distance and the zoom
 *            easing byte for byte (a zoom 2 camera and a reflection pass in
 *            between); entering with the camera unchanged leaves the
 *            matrices bit-identical and the frame's camera the game's; at
 *            zoom k the projection is gsb_SetVSMatrix at d k bit for bit and
 *            ico_photo_camera's within 1e-5, ico_photo_fov_now within
 *            1e-3 degrees; leaving puts everything back with a camera cut;
 *            after a puddle pass enter rebuilds +0x240; at 16:9 (the native
 *            frame 688 wide, latched at a flip) the photo camera's +0x240 is
 *            the native one, the frame's camera carries the aspect 16:9, and
 *            leave puts the 16:9 matrices back; a stage change or the game
 *            running again drops the save
 *   veil     photo mode's Deja vu (photo_view.c ico_photo_view_veil, from
 *            gsb_PostEffect): with photo mode open on Deja vu in a stage
 *            other than 40, one reg_DispObj a frame of the stage's
 *            sandstorm object (model 1614 among the kind-35 objects) with
 *            two nodes of deja_lightning.bga's values (held in the camera's
 *            space, fades 0.9 and -0.95, depths 0.72907 and 0.74207, scale
 *            0.0039999) under the tick's full-screen 500 unit pair; the
 *            object's own nodes, the matrices and the display list
 *            priority given back; no film grain with it.  Nothing at all
 *            (no call, the frame's commands the same as photo mode's
 *            without it) with the effect off or 1 .. 4, in stage 40,
 *            without the object, or with photo mode closed
 *   reel     Deja vu's film reel (photo_view.c): a row of
 *            film_noise_l4.bga's shape in the arena (noiseE, noiseB, noiseE
 *            and sandstorm nodes under CAMERA_PARENT, then the CAMERA),
 *            row 70 asked for; the first photo tick starts it
 *            (bga_SetFrame), each later tick runs it once
 *            (bga_CalcAnimation, mode 1, looping) on the reel's own frames,
 *            and the game's frames, mode and node records are the same
 *            after every tick and draw; the draws are noiseB then noiseE
 *            (model order), each once with the evaluated nodes (fade,
 *            camera-held, scale), under the tick's 500 unit pair, before
 *            the veil, the priority put back; the drawn fade follows the
 *            envelope's own frame tick by tick and wraps at its length
 *            (the evaluator here stands in for the game's); nothing with
 *            the effect off or 1 .. 4, in stage 40, with photo mode closed;
 *            the veil alone without the row or with a node the reel cannot
 *            put back; started again on reopening
 *   cull     the native width (gsb_SetVSMatrixSub's window half-width
 *            gsFrame.w / 2) and gsbHostCullMargin: with the Original
 *            framerate at 4:3 +0x240 and the 500 unit pair's +0x680 are as
 *            gsb_SetVSMatrixSub made them; with pictures blended between
 *            ticks both scales are divided by 1.12; at 16:9 (W = 688) x is
 *            the native one, 688 / 512 narrower (and by 1.12 more when
 *            blended), y untouched; the frame's camera carries the aspect;
 *            the matrices that place anything never change
 *   strips   GifPacket.c's CPU strips at 512, 688 and 2560: isInScreen's
 *            PS2 compares (0 .. 0xFFF0) at every width; a triangle reaching
 *            the camera's picture with a vertex past them dropped at every
 *            width; one wholly beside the picture with a vertex past even
 *            a window scaled by W / 512 drawn on the wide frames (XYZ2_HI) and
 *            dropped at 512 as the PS2; one with a vertex behind the eye
 *            dropped
 * On a Vulkan device (exit 77 without one, after the recording checks):
 *   camera   rd__camera_probe (FrameCB through camera_probe_ps) against the
 *            C products and sceVu0RotTransPers through matrixptr+0x100
 *            (GS window X/Y within 1/16 pixel)
 *   depth    depth-tested UI Z above 2^24 on SCENE (PSMZ32 scale)
 *   half     a frame flipped on field 0 samples each row's own texel (no
 *            half offset)
 *   clear    SCENE cleared to the flip's BG colour
 *   decoder  GifPacket.c at the native widths: FRAME / XYOFFSET / SCISSOR past 2047, full-frame
 *            UV sprites past 1024 texels, XYZ2_HI, the scaled work-buffer classes
 *   posts    keep, fade, brightness, anti-alias (each level alone within
 *            1 LSB, both 2), film noise and letterbox against CPU
 *            references of the GS within 1 LSB
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eeregs.h>
#include <libvu0.h>
#include "hlsl_shim.h"
#include "gs_math.hlsli"
#include "rd_internal.h"
#include "vk/rhi_vk.h"
/* the game's side */
#include "typedef.h"
#include "main.h"
#include "Basic.h"
#include "DisplayList.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "BgAnimation.h"
#include "StageAnimation.h"
#include "eeword.h"
#include "video_options.h"
#include "photo_mode.h"
#include "photo_view.h"
#include "../../core/region.h" /* the scene layout (la_game_demo) */

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------- what the files import */
int systemStatus[12];

sceGsDBuff db;

StageSetting GlobalStageSetting;

PadState pad[16];

int buffer_ID, frame_count, stage_no, GlobalTimer, game_pause;

int screen_offset_x, screen_offset_y, optionScreenMode, odd_even;

char *matrixptr;

int current_layout_id, gFlagGameClear;

int fadeStatus, fadeContinue;

float fadeSpeed;

unsigned char fadeColor[4];

int staffRollStartFlag;

float staffRollCenterOffsetX;

int debug_font_flag, debug_snapshot_num, debug_snapshot_reserve;

int debug_zoom_per = 100;

const StgPre stageData[1];

void *ios_partition_common;

struct DmaChan *dmaVif;

__attribute__((aligned(16))) volatile unsigned char ico_hw_gs[0x2000];

static __attribute__((aligned(16))) char s_spr[0x800];

void *iosMallocDebug(void *part, int size, const char *file, int line)
{
    (void)part;
    (void)file;
    (void)line;
    return calloc(1, (size_t)size);
}

void iosFree(void *p)
{
    free(p);
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_PrintfDummy(int x, int y, unsigned int c, const char *fmt, ...)
{
    (void)x;
    (void)y;
    (void)c;
    (void)fmt;
}

void debug_Printf(int x, int y, unsigned int c, const char *fmt, ...)
{
    (void)x;
    (void)y;
    (void)c;
    (void)fmt;
}

void debug_FlushFont(void) {}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

void ico_assert(const char *file, int line, const char *e)
{
    printf("assert %s:%d %s\n", file, line, e);
    abort();
}

int debugSceOpen(const char *name, int mode)
{
    (void)name;
    (void)mode;
    return -1;
}

int debugSceClose(int fd)
{
    (void)fd;
    return 0;
}

int sceRead(int fd, void *p, int n)
{
    (void)fd;
    (void)p;
    (void)n;
    return 0;
}

int sceWrite(int fd, const void *p, int n)
{
    (void)fd;
    (void)p;
    (void)n;
    return 0;
}

int sceLseek(int fd, int o, int w)
{
    (void)fd;
    (void)o;
    (void)w;
    return 0;
}

int sceCdReadClock(void *c)
{
    (void)c;
    return 1;
}

void mc_Reset(void) {}

float GetTableSin(short angle)
{
    (void)angle;
    return 0.0f;
}

float GetTableCos(short angle)
{
    (void)angle;
    return 1.0f;
}

void FlushCache(int op)
{
    (void)op;
}

void sceDmaSend(void *ch, void *addr)
{
    (void)ch;
    (void)addr;
}

int sceGsSyncV(int mode)
{
    (void)mode;
    return 0;
}

int sceGsSyncPath(int mode, unsigned short t)
{
    (void)mode;
    (void)t;
    return 0;
}

void sceGsResetGraph(short mode, short inter, short omode, short ffmd)
{
    (void)mode;
    (void)inter;
    (void)omode;
    (void)ffmd;
}

void sceGsResetPath(void) {}

void sceGsSetDefDBuff(sceGsDBuff *d, short psm, short w, short h, short ztst, short zpsm,
                      short flag)
{
    (void)d;
    (void)psm;
    (void)w;
    (void)h;
    (void)ztst;
    (void)zpsm;
    (void)flag;
}

void sceGsSetDefDispEnv(sceGsDispEnv *d, short psm, short w, short h, short dx, short dy)
{
    (void)d;
    (void)psm;
    (void)w;
    (void)h;
    (void)dx;
    (void)dy;
}

void sceGsSetHalfOffset(void *draw, short x, short y, short half)
{
    (void)draw;
    (void)x;
    (void)y;
    (void)half;
}

int sceGsSwapDBuff(void *d, int id)
{
    (void)d;
    (void)id;
    return 0;
}

void dma_init(void) {}

void matrix_init(void) {}

void tex_Init(void) {}

void tex_ResetVram(void) {}

void tex_UpdateMipMapLevel(float l)
{
    (void)l;
}

void tex_RemakeRegistersSampleMin(void) {}

int tex_GetTWTH(int n)
{
    int k = 0;
    while ((1 << k) < n) {
        k++;
    }
    return k;
}

int tex_GetTextureNo(const char *name)
{
    (void)name;
    return 0;
}

/* Texture.c's tex_setTexReg packet for the film-noise texture: a raw TEX0
   A+D pair (TBP 0x1A00, 64 x 64 PSMCT32, TCC 1, MODULATE) in the list
   current at the call, as tex_TransTexture writes it on a texture in VRAM */
#define NOISE_TBP 0x1A00

/* the calls: gsb_filmNoise's, one a frame with the grain */
static int s_grainTex;

int tex_TransTexture(int id, int ret)
{
    (void)id;
    s_grainTex++;
    gif_StartPacketPri(dl_GetPri());
    *PacketBufferStruct.ptr.d++ =
        (unsigned long long)NOISE_TBP | (1ull << 14) | (6ull << 26) | (6ull << 30) | (1ull << 34);
    *PacketBufferStruct.ptr.d++ = 6;
    gif_EndPacket();
    return ret;
}

void resetmallocseki(void) {}

void pac_Init(void) {}

void reg_Init(void) {}

void shadow_Init(void) {}

void shadow_Reset(void) {}

void shadow_Draw(void) {}

int shadow_Tool(void)
{
    return 0;
}

/* >= 0: the display list priority the fog leaves (the veil's check) */
static int s_fogPri = -1;

void fog_DrawFog(void)
{
    if (s_fogPri >= 0) {
        dl_SetDLPriority(s_fogPri);
    }
}

int fog_FogTool(void)
{
    return 0;
}

void light_ResetLight(void) {}

int light_Tool(void)
{
    return 0;
}

void FullScreenEffectAfter(void) {}

void FullScreenEffectBefore(void) {}

/* the display list priority current when gsb_PostEffect reaches the
   motion blur (the veil's draw comes just before it) */
static int s_blurPri = -1;

void MotionBlur(void)
{
    s_blurPri = dl_GetPri();
}

/* counts the calls: the bars leave the blur alone while they are up */
static int s_blurCalls;

void SetMotionBlur(int v)
{
    (void)v;
    s_blurCalls++;
}

void staffRollMain(void) {}

void stage_SetLoopFlag(int g, int f)
{
    (void)g;
    (void)f;
}

/* photo_view.c's veil: the stage's objects of kind 35 (gobj.c) and the
   object drawn (RegistPacket.c reg_DispObj), as the call sees them */
static GObj *s_kindHead;
static int s_kindAsked = -1;

void *isysGObjSearchFromObjKindID_begin(int kind)
{
    s_kindAsked = kind;
    return s_kindHead;
}

void *isysGObjSearchFromObjKindID_next(GObj *g)
{
    return g->kindNext;
}

static struct {
    int calls;
    Sub15C *o;
    int nodeNum;
    struct DObjNode nodes[2];
    float mtx[2][16];
    float screen500[32];
    int pri; /* the display list priority at the call */
} s_disp;

/* every reg_DispObj of a frame in order (the reel's checks): the object,
   its nodes and matrices as the call sees them */
#define DISP_LOG 16

static struct {
    Sub15C *o;
    int nodeNum;
    struct DObjNode nodes[4];
    float mtx[4][16];
    float screen500[32];
} s_dispLog[DISP_LOG];

static int s_dispLogN;

void reg_DispObj(Sub15C *o)
{
    if (s_dispLogN < DISP_LOG) {
        const int n = o->nodeNum < 4 ? o->nodeNum : 4;
        s_dispLog[s_dispLogN].o = o;
        s_dispLog[s_dispLogN].nodeNum = o->nodeNum;
        memcpy(s_dispLog[s_dispLogN].nodes, o->nodes, (size_t)n * sizeof(struct DObjNode));
        memcpy(s_dispLog[s_dispLogN].mtx, (const void *)o->nodeMtx, (size_t)n * 64);
        memcpy(s_dispLog[s_dispLogN].screen500, matrixptr + 0x640, sizeof(s_dispLog[0].screen500));
        s_dispLogN++;
    }
    s_disp.calls++;
    s_disp.o = o;
    s_disp.nodeNum = o->nodeNum;
    if (o->nodeNum == 2) {
        memcpy(s_disp.nodes, o->nodes, sizeof(s_disp.nodes));
        memcpy(s_disp.mtx, (const void *)o->nodeMtx, sizeof(s_disp.mtx));
    }
    memcpy(s_disp.screen500, matrixptr + 0x640, sizeof(s_disp.screen500));
    s_disp.pri = dl_GetPri();
    dl_SetDLPriority(5); /* the dissolve's list, as reg_dispMObj leaves it */
}

void UpdateHandCameraLimitP(void) {}

void UpdateHandCameraLimitV(void) {}

void UpdateZoomMaxVallInDemo(void) {}

/* the EE word arena (eeword.h): the reel's stage animation lives in it */
static unsigned char s_arena[1 << 16] __attribute__((aligned(16)));
static size_t s_arenaTop = 16;
unsigned char *ico_arena_cached_base = s_arena;

unsigned char *ico_arena_base(void)
{
    return s_arena;
}

int ico_arena_contains(const void *p, __SIZE_TYPE__ n)
{
    const unsigned char *c = (const unsigned char *)p;
    return c >= s_arena && c + n <= s_arena + sizeof(s_arena);
}

static void *arenaAlloc(size_t n)
{
    void *p = &s_arena[s_arenaTop];
    s_arenaTop = (s_arenaTop + n + 15) & ~(size_t)15;
    memset(p, 0, n);
    return p;
}

/* photo_view.c's reel: StageAnimation.c's row lookup and BgAnimation.c's
   evaluator, as the call sees them.  The evaluator here stands in for the
   game's (bga_CalcObject and bga_calcEnvelope) with its shape: each node's
   fade from its type 0 envelope's frame (reelFade), its place held in the
   camera's space (flags 1 and 2, the position, a scale matrix), then every
   envelope and the node's motion stepped by the header's step and wrapped to
   0 past their lengths when looping; bga_SetFrame puts every frame at the
   start and evaluates without the header's step */
static BgaHeader *s_reelHead;
static int s_reelKeyAsked = -1;
static int s_setFrames, s_calcs, s_calcModeBad, s_quatDepth;

BgaHeader *stage_HostBgaHeader(int key)
{
    s_reelKeyAsked = key;
    return s_reelHead;
}

void PushQuaternion(void)
{
    s_quatDepth++;
}

void PopQuaternion(void)
{
    s_quatDepth--;
}

/* the fade an envelope frame gives: -0.6 + 0.01 f (between -1 and 1 for
   the frames used: subtracted early, added late) */
static float reelFade(float f)
{
    return -0.6f + 0.01f * f;
}

static void stubCalcObject(BgaDObjEnt *d, float dt, int loop)
{
    for (; d != 0; d = ICO_EEPTR(BgaDObjEnt *, d->sibling)) {
        BgaPtMotion *m = (BgaPtMotion *)&d->motion;
        BgaEnvEnt *e = ICO_EEPTR(BgaEnvEnt *, d->env);
        if (d->u.obj != 0 && d->type == 10) {
            Sub15C *o = ICO_EEPTR(Sub15C *, d->u.obj);
            struct DObjNode *w = &o->nodes[d->num];
            float *mt = (float *)(o->nodeMtx + (ICO_WORD)d->num * 64);
            if (e != 0 && e->type == 0) {
                w->fade = reelFade(ICO_EEPTR(BgaExtMotion *, e->data)->frame);
                w->flags.ll |= 1;
            }
            w->flags.ll |= 2;
            w->pos[0] = 0.1f * (float)d->num - 0.2f + 0.001f * m->frame;
            w->pos[1] = 0.05f;
            w->pos[2] = 1.05f;
            w->pos[3] = 1.0f;
            memset(mt, 0, 64);
            mt[0] = mt[5] = mt[10] = 0.6f;
            mt[15] = 1.0f;
        }
        for (; e != 0 && e->data != 0; e++) {
            BgaExtMotion *x = ICO_EEPTR(BgaExtMotion *, e->data);
            x->frame += dt;
            if ((float)x->len < x->frame) {
                x->frame = loop ? 0.0f : (float)x->len;
            }
        }
        if (d->child != 0) {
            stubCalcObject(ICO_EEPTR(BgaDObjEnt *, d->child), dt, loop);
        }
        m->frame += dt;
        if ((float)m->len < m->frame) {
            m->frame = loop ? 0.0f : (float)m->len;
        }
    }
}

void bga_CalcAnimation(BgaHeader *p, int loop, int reset)
{
    s_calcs++;
    s_calcModeBad += p->mode != 1;
    for (int i = 0; BGA_ROOT(p, i) != 0; i++) {
        stubCalcObject(BGA_ROOT(p, i), p->step, loop);
    }
    if (!reset) {
        p->frame += p->step;
        if (p->end < p->frame) {
            p->frame = 0.0f;
        }
    }
}

static void stubResetCounters(BgaDObjEnt *d, float f)
{
    for (; d != 0; d = ICO_EEPTR(BgaDObjEnt *, d->sibling)) {
        BgaEnvEnt *e = ICO_EEPTR(BgaEnvEnt *, d->env);
        ((BgaPtMotion *)&d->motion)->frame = f;
        for (; e != 0 && e->data != 0; e++) {
            ICO_EEPTR(BgaExtMotion *, e->data)->frame = f;
        }
        if (d->child != 0) {
            stubResetCounters(ICO_EEPTR(BgaDObjEnt *, d->child), f);
        }
    }
}

void bga_SetFrame(BgaHeader *p, int frame, int mode, int loop)
{
    (void)frame;
    s_setFrames++;
    p->frame = p->start;
    p->mode = (signed char)mode;
    for (int i = 0; BGA_ROOT(p, i) != 0; i++) {
        stubResetCounters(BGA_ROOT(p, i), p->frame);
    }
    bga_CalcAnimation(p, loop, 1);
}

/* ------------------------------------------------------------- helpers */

static void setField(int f)
{
    if (f) {
        *GS_CSR |= 1ull << 13;
    } else {
        *GS_CSR &= ~(1ull << 13);
    }
}

/* one scheduler frame boundary: gsb_PostEffect, then the flip */
static void tick(void)
{
    gsb_SyncGSSystem();
    gsb_UpdateGSSystem(0);
}

static const RdCmd *firstOf(const RdFrame *f, int list, uint8_t type)
{
    for (uint32_t i = 0; i < f->lists[list].count; i++) {
        if (f->lists[list].cmds[i].type == type) {
            return &f->lists[list].cmds[i];
        }
    }
    return NULL;
}

static int halfOf(const RdFrame *f, int keep)
{
    const RdCmd *t = firstOf(f, keep ? 11 : 0, RDC_TARGET);
    return t ? (t->b[0] & RD_TARGET_HALF_Y) != 0 : -1;
}

/* column-major m[c * 4 + r] applied to v */
static void apply(float *o, const float *m, const float *v)
{
    for (int r = 0; r < 4; r++) {
        o[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r] * v[3];
    }
}

static void viewMatrix(float *m)
{
    /* a rotation about y by 0.5 rad and x by 0.25 rad, then a translation */
    const float cy = cosf(0.5f), sy = sinf(0.5f), cx = cosf(0.25f), sx = sinf(0.25f);
    const float r[3][3] = {{cy, 0.0f, -sy}, {sy * sx, cx, cy * sx}, {sy * cx, -sx, cy * cx}};
    memset(m, 0, 64);
    for (int row = 0; row < 3; row++) {
        for (int c = 0; c < 3; c++) {
            m[c * 4 + row] = r[row][c];
        }
    }
    m[12] = 30.0f;
    m[13] = -120.0f;
    m[14] = 900.0f;
    m[15] = 1.0f;
}

/* ------------------------------------------------------- recording checks */

static void checkBgTiming(void)
{
    gsb_SetBGColor(&db, 10, 20, 30);
    tick(); /* opens a frame whose head has 10, 20, 30 */
    gsb_SetBGColor(&db, 40, 50, 60);
    tick(); /* the flip kicks it: the PS2 cleared with 40, 50, 60 */
    const RdFrame *f = rd__last_frame();
    const RdCmd *c = f ? firstOf(f, 0, RDC_CLEAR) : NULL;
    CHECK(c && c->b[0] == 40 && c->b[1] == 50 && c->b[2] == 60 && c->b[3] == 0x80,
          "bg: the head clear has the flip's colour (got %u %u %u %u)", c ? c->b[0] : 0,
          c ? c->b[1] : 0, c ? c->b[2] : 0, c ? c->b[3] : 0);
    CHECK(c && c->u[0] == RD_TARGET_SCENE + 1 && c->b[4] == 1 && c->u[1] == 0,
          "bg: SCENE colour and depth, Z 0");
    CHECK(f && !firstOf(f, 11, RDC_CLEAR), "bg: list 11's head copy is NOPed in a full frame");
    CHECK(f && f->lists[0].count > 0 && f->lists[11].count > 0, "bg: lists recorded");
}

static void checkKeep(void)
{
    GlobalStageSetting.reductionCol[0] = 100;
    GlobalStageSetting.reductionCol[1] = 110;
    GlobalStageSetting.reductionCol[2] = 120;
    tick();
    fbKeep = 1;
    tick(); /* gsb_Reduction computes 128 now; the frame it kicks replays 11..12 */
    fbKeep = 0;
    const RdFrame *f = rd__last_frame();
    if (!f) {
        CHECK(0, "keep: a frame");
        return;
    }
    CHECK(f->keep == 1, "keep: the frame closed with keep");
    const RdCmd *c = firstOf(f, 11, RDC_CLEAR);
    CHECK(c && c->u[0] == RD_TARGET_SCENE + 1, "keep: the head clear is in list 11");
    CHECK(!firstOf(f, 0, RDC_CLEAR), "keep: list 0's head copy is NOPed");
    /* the keep sprite after the head in list 11 */
    int seenClear = 0, keepAfter = 0;
    for (uint32_t i = 0; i < f->lists[11].count; i++) {
        const RdCmd *k = &f->lists[11].cmds[i];
        seenClear |= k->type == RDC_CLEAR;
        if (seenClear && k->type == RDC_SCREEN) {
            keepAfter = 1;
        }
    }
    CHECK(keepAfter, "keep: the keep sprite follows the head");
    /* the reduction in list 12: its tinted sprite's colour (an RdPostRec
     * of kind RD_POST_REDUCTION) */
    const RdCmd *last = NULL;
    for (uint32_t i = 0; i < f->lists[12].count; i++) {
        const RdCmd *k = &f->lists[12].cmds[i];
        if (k->type == RDC_POST_STUB && k->b[0] == RD_POST_REDUCTION) {
            last = k;
        }
    }
    RdPostRec red;
    memset(&red, 0, sizeof(red));
    if (last) {
        memcpy(&red, f->payload + last->u[1], sizeof(red));
    }
    CHECK(last && red.rgba[0] == 128 && red.rgba[1] == 128 && red.rgba[2] == 128,
          "keep: reduction tint 128 (got %u %u %u)", red.rgba[0], red.rgba[1], red.rgba[2]);
    /* the keep pass leaks TEXA 80/80 and DISPLAY into what follows */
    RdStateBlock s = f->startState;
    int texaOk = 0;
    for (uint32_t i = 0; i < f->lists[11].count; i++) {
        rd__apply_state(&s, &f->lists[11].cmds[i]);
        if (f->lists[11].cmds[i].type == RDC_SCREEN && i > 0) {
            texaOk = s.ds.texa == RD_TEXA_80_80 && s.ds.texEnabled;
        }
    }
    CHECK(texaOk, "keep: TEXA 80/80 and a texture in force at the keep sprite");
    tick(); /* a full frame again */
}

/* the frame head's SCENE target: RD_TARGET_OFFSET alone (1), never with
   RD_TARGET_HALF_Y (3, the half-pixel marker of the PCSX2 dump pairing) */
static int headOffsetOf(const RdFrame *f)
{
    const RdCmd *t = firstOf(f, 0, RDC_TARGET);
    return t ? t->b[0] : -1;
}

static void checkParity(void)
{
    /* GS_CSR.FIELD constant 0 (the host loop's flips with the frame step 2
       from field 0), alternating per flip (a skipped flip), constant 1, and
       through gsb_ResetGSSystem: no half offset on any flip */
    int bad = 0, first = -1;
    setField(0);
    gsb_Init(&db);
    for (int i = 0; i < 16; i++) {
        if (i >= 4 && i < 12) {
            setField(i & 1);
        } else if (i >= 12) {
            setField(1);
        }
        if (i == 8) {
            gsb_ResetGSSystem();
        }
        tick();
        const int o = headOffsetOf(rd__last_frame());
        if (o != RD_TARGET_OFFSET && bad++ == 0) {
            first = i;
        }
    }
    CHECK(bad == 0,
          "parity: the frame head's TARGET offset is %d (no half pixel) on every flip at either "
          "field (%d flips differ, the first %d)",
          RD_TARGET_OFFSET, bad, first);
    setField(0);
    tick();
    CHECK(halfOf(rd__last_frame(), 0) == 0, "parity: field 0 -> no half offset");
}

static void checkLeak(void)
{
    fadeStatus = 1;
    fadeSpeed = 100.0f;
    fadeColor[0] = 200;
    fadeColor[1] = 100;
    fadeColor[2] = 0;
    tick();
    fadeStatus = 0;
    const RdFrame *f = rd__last_frame();
    if (!f) {
        CHECK(0, "leak: a frame");
        return;
    }
    /* the fade sprite's state, found in list 11 */
    RdStateBlock s = f->startState;
    int found = 0;
    for (int l = 0; l < RD_LIST_COUNT && !found; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            rd__apply_state(&s, &f->lists[l].cmds[i]);
            if (l == 11 && f->lists[l].cmds[i].type == RDC_SCREEN) {
                const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + f->lists[l].cmds[i].u[0]);
                if (v[1].rgba[0] == 200 && v[1].rgba[3] == 50) {
                    found = 1;
                    CHECK(s.ds.abe == 1 && s.ds.pabe == 0 && s.ds.zwrite == RD_ZWRITE_OFF &&
                              s.ds.test.ztst == RD_ZTST_ALWAYS && !s.ds.texEnabled,
                          "leak: fade state ABE %u PABE %u", s.ds.abe, s.ds.pabe);
                }
            }
        }
    }
    CHECK(found, "leak: the fade sprite (colour 200, alpha 50) in list 11");
    /* what list 11 leaves for list 12: the fade's ALPHA 0x44 with ABE */
    CHECK(found && (s.ds.blend == RD_BLEND_LERP_AS || s.ds.blend == RD_BLEND_LERP_AS_ALT) &&
              s.ds.abe == 1,
          "leak: ALPHA 0x44 and ABE in force at the end of list 11 (%u, %u)", (unsigned)s.ds.blend,
          s.ds.abe);
    tick();
}

static float s_view[16];

static void checkVu(void)
{
    viewMatrix(s_view);
    game_pause = 1;
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 512.0f);
    memcpy(matrixptr + 0x80, s_view, 64);
    gsb_MakeCommonMatrix();
    const RdVuCommon *b = rd_get_vu_common();
    CHECK(b != NULL, "vu: a block");
    if (b) {
        CHECK(memcmp(b->screenView, matrixptr + 0x100, 64) == 0, "vu: qw 4..7 = +0x100");
        CHECK(memcmp(b->viewport, matrixptr + 0x340, 64) == 0, "vu: qw 8..11 = +0x340");
        CHECK(memcmp(b->invView, matrixptr + 0x380, 64) == 0, "vu: qw 12..15 = +0x380");
        CHECK(b->unitW[3] == 1.0f && b->clip[0] == 4095.0f && b->clip[3] == 16777215.0f &&
                  b->giftag[0] == 0x8000 && b->giftag[1] == 0x302EC000u && b->giftag[2] == 0x512,
              "vu: the constant head");
    }
    const RdFrame *f = rd__rec_frame();
    CHECK(f && f->hasCamera, "vu: the frame has a camera");
    if (f) {
        CHECK(memcmp(f->camera.view, s_view, 64) == 0, "vu: camera view = +0x80");
        CHECK(memcmp(f->camera.proj, matrixptr + 0xC0, 64) == 0, "vu: camera proj = +0xC0");
        CHECK(f->camera.aspect == 4.0f / 3.0f && f->camera.nearZ == 2.0f &&
                  f->camera.farZ == 262144.0f,
              "vu: camera aspect and planes");
    }
}

static void checkZScale(void)
{
    /* the depth grows with GS Z (gs_z_to_depth); PSMZ32 is
       z * 2^-33 on a float depth buffer (the top 2^16 values apart in
       [1 - 2^-8, 1)), z * 2^-32 on D24S8 */
    const float s32 = 1.0f / 4294967296.0f, s32f = 1.0f / 8589934592.0f, s24 = 1.0f / 16777216.0f;
    const float sScene = rd__target_z_scale(RD_TARGET_SCENE + 1);
    CHECK(sScene == s32f || sScene == s32, "zscale: SCENE is PSMZ32");
    CHECK(rd__gs_depth(0xFFFFFF9Bu, s32f) < rd__gs_depth(0xFFFFFFFFu, s32f),
          "zscale: 0xFFFFFF9B and 0xFFFFFFFF apart");
    CHECK(rd__gs_depth(0, s32f) == 0.0f && rd__gs_depth(0xFFFFFFFFu, s32f) < 1.0f &&
              rd__gs_depth(0xFFFEFFFFu, s32f) < rd__gs_depth(0xFFFF0000u, s32f),
          "zscale: 32-bit ends and the top band");
    CHECK(rd__gs_depth(16999983u, s32f) < rd__gs_depth(17000000u, s32f),
          "zscale: 17 apart at 2^24 (the Queen's layers)");
    CHECK(gs_z_to_depth(0xFFFFFF9Bu, s32f) == rd__gs_depth(0xFFFFFF9Bu, s32f) &&
              gs_z_to_depth(17000000u, s32f) == rd__gs_depth(17000000u, s32f),
          "zscale: CPU and shader formula agree");
    for (uint32_t z = 0; z < 0x1000000u; z += 4099u) {
        if (rd__gs_depth(z, s24) != (float)z / 16777216.0f) {
            CHECK(0, "zscale: 24-bit exact at %u", z);
            break;
        }
    }
    CHECK(rd__gs_depth(0x1000000u, s24) == rd__gs_depth(0xFFFFFFu, s24),
          "zscale: 24-bit clamps above zmax");
}

/* ------------------------------------------------------- photo mode */

/* the eleven matrices gsb_PushView saves (GsBase.c lists their writers) */
static const int s_slot[11] = {0x80,  0xC0,  0x1C0, 0x240, 0x340, 0x640,
                               0x680, 0x100, 0x200, 0x280, 0x380};

typedef struct Snap {
    float m[11][16];
    int focus;
    int hasCam;
    RdCamera cam;
} Snap;

static void snap(Snap *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 11; i++) {
        memcpy(s->m[i], matrixptr + s_slot[i], 64);
    }
    s->focus = currentFocusDistance;
    const RdFrame *f = rd__rec_frame();
    if (f && f->hasCamera) {
        s->hasCam = 1;
        s->cam = f->camera;
    }
}

/* the first slot that differs, -1 when all eleven are byte-identical */
static int slotDiff(const Snap *a, const Snap *b)
{
    for (int i = 0; i < 11; i++) {
        if (memcmp(a->m[i], b->m[i], 64) != 0) {
            return s_slot[i];
        }
    }
    return -1;
}

static int camSame(const Snap *a, const Snap *b)
{
    return a->hasCam && b->hasCam && memcmp(&a->cam, &b->cam, sizeof(a->cam)) == 0;
}

/* the game's camera at distance d, as camera-root.c MakeCameraMatrix
   leaves it */
#define PHOTO_D 512.37f

static void gameCamAt(float d)
{
    viewMatrix(s_view);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, d);
    memcpy(matrixptr + 0x80, s_view, 64);
    gsb_MakeCommonMatrix();
}

/* puddle.c drawAreaSetup / drawAreaRestore's matrices: its 230 x 230 view
   at the integer distance, five slots put back */
static void puddlePass(void)
{
    static const int kept[5] = {0xC0, 0x1C0, 0x100, 0x200, 0x340};
    float save[5][16];
    for (int i = 0; i < 5; i++) {
        memcpy(save[i], matrixptr + kept[i], 64);
    }
    gsb_SetVSMatrix(0xE6, 0xE6, (float)currentFocusDistance);
    for (int i = 0; i < 5; i++) {
        memcpy(matrixptr + kept[i], save[i], 64);
    }
    vsWidth = ScreenWidth;
    vsHeight = ScreenHeight;
}

/* n Main ticks of photo mode with pad p: the layout's update, then the
   game's tick hook */
static void photoTicks(const IcoPhotoPad *p, int n)
{
    for (int i = 0; i < n; i++) {
        ico_photo_update(p);
        ico_photo_view_tick();
    }
}

static IcoPhotoPad photoPad(unsigned held)
{
    IcoPhotoPad p;
    memset(&p, 0, sizeof(p));
    memset(p.ana, 128, sizeof(p.ana));
    p.held = held;
    return p;
}

static int relNear(float a, float b, float rel)
{
    const float m = fmaxf(fabsf(a), fabsf(b));
    return fabsf(a - b) <= rel * m + 1e-6f;
}

/* GsBase.c's save on its own: a pop puts back the eleven matrices, the
   focus distance, vsParam (the camera's zoom and planes) and zoomCurrent
   (proved by the next eased gsb_SetVSMatrix giving the same zoom) */
static void checkPushPop(void)
{
    gsb_SetZoom(1.25f, 500.0f); /* every call eases half way */
    gameCamAt(PHOTO_D);
    Snap s0, a, b, c;
    snap(&s0);
    gsb_PushView();
    gameCamAt(PHOTO_D); /* zoomCurrent eased once more */
    snap(&a);
    gsb_PopView();
    /* a photo camera: zoom 2, a turned view, then the reflections' pass */
    float turned[16];
    memcpy(turned, s_view, 64);
    turned[12] += 300.0f;
    memcpy(matrixptr + 0x80, turned, 64);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, PHOTO_D * 2.0f);
    gsb_MakeCommonMatrix();
    puddlePass();
    gsb_PopView();
    snap(&b);
    CHECK(slotDiff(&b, &s0) == -1, "push/pop: matrixptr+0x%X put back", slotDiff(&b, &s0));
    CHECK(b.focus == s0.focus, "push/pop: currentFocusDistance %d (want %d)", b.focus, s0.focus);
    CHECK(gsb_ViewFocus() == PHOTO_D, "push/pop: gsb_ViewFocus %.4f", (double)gsb_ViewFocus());
    CHECK(vsWidth == ScreenWidth && vsHeight == ScreenHeight, "push/pop: vsWidth, vsHeight");
    gsb_MakeCommonMatrix(); /* vsParam's zoom and planes into the camera */
    snap(&b);
    CHECK(camSame(&b, &s0), "push/pop: the camera (vsParam's zoom %.6f, want %.6f)",
          (double)b.cam.zoom, (double)s0.cam.zoom);
    gameCamAt(PHOTO_D); /* eased from the restored zoomCurrent */
    snap(&c);
    CHECK(camSame(&c, &a) && slotDiff(&c, &a) == -1,
          "push/pop: zoomCurrent put back (zoom %.6f, want %.6f)", (double)c.cam.zoom,
          (double)a.cam.zoom);
    /* back to an unzoomed camera for the cells below */
    gsb_SetZoom(1.0f, 1000.0f);
    gameCamAt(PHOTO_D);
    gameCamAt(PHOTO_D);
}

static void checkPhotoView(void)
{
    IcoVideoOptions o;
    ico_video_defaults(&o);
    ico_video_set(&o);
    tick(); /* a frame open for the cameras */
    systemStatus[5] = 1;
    stage_no = 5;
    ico_photo_reset();
    ico_photo_set_tick_hz(25);
    checkPushPop();

    /* enter with the camera unchanged: the eleven matrices bit-identical,
       the frame's camera the game's */
    Snap g, p, z, x;
    gameCamAt(PHOTO_D);
    snap(&g);
    CHECK(g.hasCam, "photo: the game's frame has a camera");
    CHECK(gsb_ViewFocus() == PHOTO_D, "photo: gsb_ViewFocus is the camera's float distance (%.4f)",
          (double)gsb_ViewFocus());
    ico_photo_enter();
    ico_photo_view_tick();
    snap(&p);
    CHECK(ico_photo_view_saved(), "photo: entered");
    CHECK(slotDiff(&p, &g) == -1, "photo: enter leaves matrixptr+0x%X as it was", slotDiff(&p, &g));
    CHECK(camSame(&p, &g), "photo: enter, the frame's camera is the game's");
    /* the photo camera not moved yet: the game's camera rule (no
       rd_free_camera) */
    CHECK(rd__rec_frame() && !rd__rec_frame()->freeCamera,
          "photo: enter, the game camera's rule (not a free camera)");
    CHECK(p.focus == g.focus, "photo: enter, currentFocusDistance");
    const float fov0 = ico_photo_fov_deg(&g.cam);
    CHECK(fabsf(ico_photo_fov_now() - fov0) < 1e-3f, "photo: the field of view %.4f (want %.4f)",
          (double)ico_photo_fov_now(), (double)fov0);

    /* zoom k (R2 held two seconds, about 2): the projection is the game's
       gsb_SetVSMatrix at d k, bit for bit, and ico_photo_camera's within
       1e-5; the view stays */
    IcoPhotoPad pp = photoPad(ICO_PHOTO_R2);
    photoTicks(&pp, 50);
    snap(&z);
    IcoPhotoState st;
    ico_photo_get(&st);
    const float k = st.zoom;
    /* e^0.7 at [photo] stick_speed 1; whatever the config, zoomed in */
    CHECK(k > 1.5f && k < 5.4f, "photo: zoom %.4f", (double)k);
    CHECK(ico_photo_view_saved(), "photo: still saved while zooming");
    CHECK(fabsf(ico_photo_fov_now() - ico_photo_fov_deg(&z.cam)) < 1e-3f,
          "photo: ico_photo_fov_now %.4f, the frame's %.4f", (double)ico_photo_fov_now(),
          (double)ico_photo_fov_deg(&z.cam));
    RdCamera game0;
    memset(&game0, 0, sizeof(game0));
    memcpy(game0.view, g.m[0], 64);
    memcpy(game0.proj, g.m[1], 64);
    RdCamera ov;
    CHECK(ico_photo_camera(&ov, &game0), "photo: ico_photo_camera");
    int bad = -1;
    for (int i = 0; i < 16; i++) {
        if (!relNear(ov.proj[i], z.cam.proj[i], 1e-5f)) {
            bad = i;
        }
    }
    CHECK(bad < 0, "photo: proj[%d] %.6f, ico_photo_camera's %.6f", bad,
          (double)(bad < 0 ? 0.0f : z.cam.proj[bad]), (double)(bad < 0 ? 0.0f : ov.proj[bad]));
    CHECK(memcmp(z.m[0], ov.view, 64) == 0, "photo: +0x80 is ico_photo_camera's view");
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, PHOTO_D * k);
    Snap r;
    snap(&r);
    for (int i = 1; i <= 6; i++) {
        CHECK(memcmp(r.m[i], z.m[i], 64) == 0, "photo: matrixptr+0x%X is gsb_SetVSMatrix at d k",
              s_slot[i]);
    }
    CHECK(z.focus == (int)(PHOTO_D * k), "photo: the reflections' distance %d", z.focus);

    {
        /* zoomed: a free camera; the aspect sweep (an aspect forced) keeps
           the game camera's rule, and the free camera is back after it */
        RdFrame *fr = (RdFrame *)rd__rec_frame();
        const IcoPhotoPad still = photoPad(0);
        CHECK(fr && fr->freeCamera, "photo: zoomed, a free camera");
        if (fr) {
            fr->freeCamera = 0;
            ico_video_frame_force_aspect(16.0f / 9.0f);
            photoTicks(&still, 1);
            CHECK(!fr->freeCamera, "photo: the sweep keeps the game camera's rule");
            ico_video_frame_force_aspect(0.0f);
            photoTicks(&still, 1);
            CHECK(fr->freeCamera, "photo: after the sweep, a free camera again");
        }
    }

    /* leave: everything back byte for byte, a camera cut */
    const unsigned cut0 = ico_video_cut_serial();
    ico_photo_exit();
    ico_photo_view_tick();
    snap(&x);
    CHECK(!ico_photo_view_saved(), "photo: left");
    CHECK(slotDiff(&x, &g) == -1, "photo: leave puts matrixptr+0x%X back", slotDiff(&x, &g));
    CHECK(camSame(&x, &g), "photo: leave, the frame's camera the game's again");
    CHECK(x.focus == g.focus, "photo: leave, currentFocusDistance");
    CHECK(ico_video_cut_serial() == cut0 + 1, "photo: leave cuts the camera");
    ico_photo_view_tick();
    CHECK(!ico_photo_view_saved(), "photo: off, nothing saved");

    /* after a reflection pass (+0x240, +0x640, +0x680 and vsParam at 230 x
       230): enter rebuilds the full-screen camera; leave gives back what
       the game left */
    puddlePass();
    Snap pud;
    snap(&pud);
    CHECK(memcmp(pud.m[3], g.m[3], 64) != 0, "puddle: +0x240 at the reflection's size");
    ico_photo_enter();
    ico_photo_view_tick();
    snap(&p);
    CHECK(slotDiff(&p, &g) == -1, "puddle: enter rebuilds matrixptr+0x%X", slotDiff(&p, &g));
    CHECK(camSame(&p, &g), "puddle: enter, the game's camera");
    ico_photo_exit();
    ico_photo_view_tick();
    snap(&x);
    CHECK(slotDiff(&x, &pud) == -1, "puddle: leave gives back matrixptr+0x%X as the game left it",
          slotDiff(&x, &pud));
    gameCamAt(PHOTO_D);

    /* 16:9: the photo camera's cull is the native frame's, as the game's */
    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    tick(); /* the flip latches the native frame */
    CHECK(gsFrame.w == 688, "16:9: the native frame 688 wide (%d)", gsFrame.w);
    const float kw = (float)gsFrame.w / 512.0f;
    gameCamAt(PHOTO_D);
    Snap wide;
    snap(&wide);
    CHECK(relNear(wide.m[3][0] * kw, g.m[3][0], 1e-6f),
          "16:9: +0x240's x scale the native one, 688 / 512 narrower (%g -> %g)", (double)g.m[3][0],
          (double)wide.m[3][0]);
    CHECK(wide.hasCam && wide.cam.aspect == 16.0f / 9.0f, "16:9: the frame's camera aspect %g",
          (double)wide.cam.aspect);
    ico_photo_enter();
    ico_photo_view_tick();
    snap(&p);
    CHECK(slotDiff(&p, &wide) == -1, "16:9: enter leaves matrixptr+0x%X as it was",
          slotDiff(&p, &wide));
    CHECK(camSame(&p, &wide), "16:9: enter, the frame's camera is the game's (aspect %g)",
          (double)p.cam.aspect);
    pp = photoPad(ICO_PHOTO_R2);
    photoTicks(&pp, 10);
    snap(&p);
    ico_photo_get(&st);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, PHOTO_D * st.zoom);
    Snap n;
    snap(&n);
    CHECK(memcmp(n.m[3], p.m[3], 64) == 0, "16:9: the photo camera's +0x240 is the native one");
    ico_video_defaults(&o);
    ico_video_set(&o);
    tick(); /* back to the 4:3 frame */
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, PHOTO_D * st.zoom);
    snap(&n);
    CHECK(relNear(p.m[3][0] * kw, n.m[3][0], 1e-6f) && memcmp(&p.m[3][1], &n.m[3][1], 60) == 0,
          "16:9: +0x240's x scale at zoom %.3f 688 / 512 narrower (%g -> %g), the rest 4:3's",
          (double)st.zoom, (double)n.m[3][0], (double)p.m[3][0]);
    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    tick();
    ico_photo_exit();
    ico_photo_view_tick();
    snap(&x);
    CHECK(slotDiff(&x, &wide) == -1, "16:9: leave puts matrixptr+0x%X back", slotDiff(&x, &wide));
    CHECK(camSame(&x, &wide), "16:9: leave, the frame's camera the game's again (aspect %g)",
          (double)x.cam.aspect);
    ico_video_defaults(&o);
    ico_video_set(&o);
    tick();
    gameCamAt(PHOTO_D);

    /* a stage change (or the game running again) drops the save: the
       game's new camera is kept */
    ico_photo_enter();
    pp = photoPad(ICO_PHOTO_R2);
    photoTicks(&pp, 10);
    snap(&z);
    CHECK(slotDiff(&z, &g) != -1, "stage: the zoomed camera in the matrices");
    stage_no = 6;
    ico_photo_exit();
    ico_photo_view_tick();
    snap(&x);
    CHECK(!ico_photo_view_saved(), "stage: the save dropped");
    CHECK(slotDiff(&x, &z) == -1, "stage: nothing put back (matrixptr+0x%X)", slotDiff(&x, &z));
    stage_no = 5;
    gameCamAt(PHOTO_D);
    ico_photo_enter();
    photoTicks(&pp, 10);
    snap(&z);
    systemStatus[5] = 0; /* unpaused under photo mode */
    ico_photo_view_tick();
    snap(&x);
    CHECK(!ico_photo_view_saved() && slotDiff(&x, &z) == -1, "unpaused: the save dropped");
    ico_photo_view_tick();
    CHECK(!ico_photo_view_saved(), "unpaused: no enter while the game runs");
    ico_photo_exit();
    ico_photo_reset();
    stage_no = 0;
    gameCamAt(PHOTO_D);
}

/* The cull's native width and margin (GsBase.c gsb_SetVSMatrixSub's
   window half-width gsFrame.w / 2, gsbHostCullMargin) on the two
   projections that cull: +0x240 (slot 3) and the 500 unit pair's +0x680
   (slot 6).  The 4:3 Original-framerate camera is the reference: the
   matrices exactly as gsb_SetVSMatrixSub computes them. */
static void checkCullScales(const char *what, const Snap *s, const Snap *raw, float kx, float ky)
{
    static const int cull[2] = {3, 6};
    for (int i = 0; i < 2; i++) {
        const float *a = s->m[cull[i]], *r = raw->m[cull[i]];
        const int sx = kx == 1.0f ? a[0] == r[0] : relNear(a[0] * kx, r[0], 1e-6f);
        const int sy = ky == 1.0f ? a[5] == r[5] : relNear(a[5] * ky, r[5], 1e-6f);
        int rest = 1;
        for (int j = 0; j < 16; j++) {
            rest &= j == 0 || j == 5 || a[j] == r[j];
        }
        CHECK(sx && sy && rest,
              "cull %s: matrixptr+0x%X x scale %g (4:3 Original %g, over %g), y %g (%g, "
              "over %g), the rest as computed",
              what, s_slot[cull[i]], (double)a[0], (double)r[0], (double)kx, (double)a[5],
              (double)r[5], (double)ky);
    }
    /* the view, the screen matrices, the viewport and their products */
    static const int placed[8] = {0, 1, 2, 4, 5, 7, 8, 10};
    for (int i = 0; i < 8; i++) {
        CHECK(memcmp(s->m[placed[i]], raw->m[placed[i]], 64) == 0,
              "cull %s: matrixptr+0x%X byte-identical", what, s_slot[placed[i]]);
    }
}

static void checkCull(void)
{
    IcoVideoOptions o;
    Snap raw, s;
    const float m = 1.0f + 0.12f;
    /* 16:9's native frame: 16 ceil(32 x 4/3) = 688 */
    const float k = 688.0f / 512.0f;

    gsb_SetZoom(1.0f, 1000.0f); /* no easing between the cameras below */
    ico_video_defaults(&o);
    o.framerate = ICO_FRAMERATE_ORIGINAL;
    ico_video_set(&o);
    gameCamAt(PHOTO_D);
    gameCamAt(PHOTO_D);
    snap(&raw);
    /* as gsb_SetVSMatrixSub builds them: +0x240 the +0x1C0 projection's
       1500 unit screen over the half screen, +0x680 a 500 unit one */
    const float hw = (float)(ScreenWidth / 2), hh = (float)(ScreenHeight / 2);
    CHECK(relNear(raw.m[3][0], raw.m[2][0] * 1500.0f / hw, 1e-5f) &&
              relNear(raw.m[3][5], raw.m[2][5] * 1500.0f / hh, 1e-5f),
          "cull 4:3 Original: +0x240 as computed (%g %g)", (double)raw.m[3][0],
          (double)raw.m[3][5]);
    CHECK(relNear(raw.m[6][0], 500.0f / hw, 1e-5f) && relNear(raw.m[6][5], 500.0f / hh, 1e-5f),
          "cull 4:3 Original: +0x680 as computed (%g %g)", (double)raw.m[6][0],
          (double)raw.m[6][5]);
    CHECK(!ico_video_interpolate(), "cull: the Original framerate blends nothing");

    o.framerate = ICO_FRAMERATE_UNCAPPED;
    ico_video_set(&o);
    CHECK(ico_video_interpolate(), "cull: uncapped blends between ticks");
    gameCamAt(PHOTO_D);
    snap(&s);
    checkCullScales("4:3 uncapped", &s, &raw, m, m);

    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    tick(); /* the flip latches the native frame */
    CHECK(gsFrame.w == 688 && gsFrame.aspect == 16.0f / 9.0f,
          "cull 16:9: the native frame %d wide, aspect %g", gsFrame.w, (double)gsFrame.aspect);
    gameCamAt(PHOTO_D);
    snap(&s);
    checkCullScales("16:9 uncapped", &s, &raw, k * m, m);
    /* +0x240 as gsb_SetVSMatrixSub builds it at the native half-width 344 */
    CHECK(relNear(s.m[3][0] * m, raw.m[2][0] * 1500.0f / 344.0f, 1e-5f),
          "cull 16:9 uncapped: +0x240 from the half-width 344 (%g)", (double)s.m[3][0]);
    CHECK(s.hasCam && s.cam.aspect == 16.0f / 9.0f && memcmp(s.cam.proj, raw.m[1], 64) == 0,
          "cull 16:9: the frame's camera, aspect %g, proj the 4:3 +0xC0", (double)s.cam.aspect);

    o.framerate = 60;
    ico_video_set(&o);
    tick();
    gameCamAt(PHOTO_D);
    snap(&s);
    checkCullScales("16:9 at 60", &s, &raw, k * m, m);

    o.framerate = ICO_FRAMERATE_ORIGINAL;
    ico_video_set(&o);
    tick();
    gameCamAt(PHOTO_D);
    snap(&s);
    checkCullScales("16:9 Original", &s, &raw, k, 1.0f);

    o.aspect = ICO_ASPECT_4_3;
    ico_video_set(&o);
    tick();
    CHECK(gsFrame.w == 512 && gsFrame.aspect == 4.0f / 3.0f, "cull 4:3: the frame 512 wide (%d)",
          gsFrame.w);
    gameCamAt(PHOTO_D);
    snap(&s);
    CHECK(slotDiff(&s, &raw) == -1, "cull 4:3 Original again: matrixptr+0x%X differs",
          slotDiff(&s, &raw));

    ico_video_defaults(&o);
    ico_video_set(&o);
    tick();
    gameCamAt(PHOTO_D);
}

/* --------------------------------------------------------- device checks */

#define W 512
#define H 512

static uint8_t s_img[W * H * 4];

static RdTex s_tex;

static RdTex s_noise;

static uint8_t s_noiseImg[64 * 64 * 4];

static RdTex resolver(unsigned long long tex0, int list)
{
    (void)list;
    return (tex0 & 0x3FFF) == NOISE_TBP ? s_noise : (RdTex){0};
}

/* the scene: smooth gradients (neighbours differ by at most 1), alpha 0x80 */
static void makeScene(void)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = &s_img[(y * W + x) * 4];
            p[0] = (uint8_t)(x / 2);
            p[1] = (uint8_t)(y / 2);
            p[2] = (uint8_t)((x + y) / 4);
            p[3] = 0x80;
        }
    }
    /* film grain: 2 (|i - 32| + |j - 32|), periodic, even, alpha 0x80 */
    for (int j = 0; j < 64; j++) {
        for (int i = 0; i < 64; i++) {
            uint8_t *p = &s_noiseImg[(j * 64 + i) * 4];
            const int v = 2 * (abs(i - 32) + abs(j - 32));
            p[0] = (uint8_t)v;
            p[1] = (uint8_t)(128 - v);
            p[2] = (uint8_t)(v / 2 * 2);
            p[3] = 0x80;
        }
    }
}

/* what the game draws: the scene 1:1 into SCENE (list 0, after the head) */
static void drawScene(RdTex t, RdFilter filter)
{
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    dl_SetDLPriority(0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_sampler(filter, filter, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    const int32_t o = (2048 - 256) * 16;
    v[0].x = v[0].y = o;
    v[1].x = v[1].y = o + 512 * 16;
    v[0].s = v[0].t = 8.0f;
    v[1].s = v[1].t = 512.0f * 16.0f + 8.0f;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, grey, 4);
    memcpy(v[1].rgba, grey, 4);
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

static uint8_t *readTarget(RdTargetId id, uint32_t *w, uint32_t *h)
{
    static uint8_t buf[W * H * 4];
    if (!rd__read_target(rd_target(id), buf, sizeof(buf), w, h)) {
        CHECK(0, "readback of target %d", (int)id);
        return NULL;
    }
    return buf;
}

typedef int (*RefFn)(int x, int y, int k, void *user);

/* compares SCENE's RGB with ref within tol; returns the worst error */
static int compareScene(const char *what, RefFn ref, void *user, int tol)
{
    uint32_t w, h;
    const uint8_t *s = readTarget(RD_TARGET_SCENE, &w, &h);
    if (!s || w != W || h != H) {
        CHECK(0, "%s: SCENE readback", what);
        return 999;
    }
    int worst = 0, bad = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            for (int k = 0; k < 3; k++) {
                const int want = ref(x, y, k, user);
                const int e = abs((int)s[(y * W + x) * 4 + k] - want);
                worst = e > worst ? e : worst;
                if (e > tol && bad++ < 8) {
                    printf("FAIL %s (%d,%d) ch %d: got %u, expected %d\n", what, x, y, k,
                           s[(y * W + x) * 4 + k], want);
                }
            }
        }
    }
    if (bad) {
        failures++;
    }
    printf("  %s: worst channel error %d LSB (tolerance %d)\n", what, worst, tol);
    return worst;
}

static int scenePx(int x, int y, int k)
{
    return s_img[(y * W + x) * 4 + k];
}

/* GS blend (A - B) * C >> 7 + D, COLCLAMP 1 */
static int gsLerp(int cs, int cd, int f)
{
    return gs_blend_ch(cs, cd, f, cd, 1u);
}

static void checkCamera(void)
{
    const RdFrame *f = rd__rec_frame();
    if (!f || !f->hasCamera) {
        CHECK(0, "camera: no camera in the open frame");
        return;
    }
    static const float pts[3][4] = {
        {100.0f, -50.0f, 800.0f, 1.0f}, {-300.0f, 40.0f, 200.0f, 1.0f}, {5.0f, 7.0f, -60.0f, 1.0f}};
    for (int n = 0; n < 3; n++) {
        float out[3][4];
        if (!rd__camera_probe(&f->camera, pts[n], out)) {
            CHECK(0, "camera: probe");
            return;
        }
        float v[4], sv[4], cl[4];
        apply(v, f->camera.view, pts[n]);
        apply(cl, f->camera.proj, v);
        apply(sv, (const float *)(matrixptr + 0x100), pts[n]);
        for (int i = 0; i < 4; i++) {
            const float tol = 1e-5f * (1.0f + fabsf(sv[i]));
            CHECK(fabsf(out[0][i] - v[i]) <= 1e-5f * (1.0f + fabsf(v[i])),
                  "camera %d: g_view row %d: %g vs %g", n, i, out[0][i], v[i]);
            CHECK(fabsf(out[1][i] - cl[i]) <= tol, "camera %d: g_proj row %d: %g vs %g", n, i,
                  out[1][i], cl[i]);
            CHECK(fabsf(out[2][i] - sv[i]) <= tol, "camera %d: g_viewProj row %d: %g vs %g", n, i,
                  out[2][i], sv[i]);
        }
        /* GS window coordinates: sceVu0RotTransPers through +0x100 */
        int32_t r[4];
        sceVu0RotTransPers(r, matrixptr + 0x100, (void *)pts[n], 1);
        const float gx = out[2][0] / out[2][3] * 16.0f, gy = out[2][1] / out[2][3] * 16.0f;
        CHECK(fabsf(gx - (float)r[0]) <= 1.0f && fabsf(gy - (float)r[1]) <= 1.0f,
              "camera %d: GS X/Y %g,%g vs rotTransPers %d,%d (1/16 px)", n, gx, gy, r[0], r[1]);
        const float gz = out[2][2] / out[2][3];
        CHECK(fabsf(gz - (float)r[2]) <= 1e-5f * fabsf((float)r[2]) + 1.0f,
              "camera %d: GS Z %g vs %d", n, gz, r[2]);
        if (n == 0) {
            printf("  camera: point 0 -> GS %.4f, %.4f, Z %.0f (rotTransPers %d/16, %d/16, %d)\n",
                   gx / 16.0f, gy / 16.0f, gz, r[0], r[1], r[2]);
        }
    }
}

static void checkDepthAndClear(void)
{
    static const uint8_t red[4] = {200, 0, 0, 0x80}, blue[4] = {0, 0, 200, 0x80},
                         green[4] = {0, 200, 0, 0x80};
    gsb_SetBGColor(&db, 33, 66, 99);
    dl_SetDLPriority(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_texture_off();
    const int32_t o = (2048 - 256) * 16;

    struct {
        int x;
        uint32_t za, zb;
        const uint8_t *b;
    } rg[2] = {{16, 0xFFFFFFFFu, 0xFFFFFF9Bu, blue}, {64, 0xFFFFFF9Bu, 0xFFFFFFFFu, green}};

    for (int i = 0; i < 2; i++) {
        RdScreenVtx v[2];
        memset(v, 0, sizeof(v));
        v[0].x = o + rg[i].x * 16;
        v[0].y = o + 16 * 16;
        v[1].x = v[0].x + 32 * 16;
        v[1].y = v[0].y + 32 * 16;
        rd_test_gs(RD_TEST_Z_ALWAYS);
        rd_z_write(1);
        v[0].z = v[1].z = rg[i].za;
        memcpy(v[0].rgba, red, 4);
        memcpy(v[1].rgba, red, 4);
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
        rd_test_gs(RD_TEST_Z_GEQUAL);
        rd_z_write(0);
        v[0].z = v[1].z = rg[i].zb;
        memcpy(v[0].rgba, rg[i].b, 4);
        memcpy(v[1].rgba, rg[i].b, 4);
        /* depth-tested 2D prims are WORLD-tagged in the game (CPU-projected
           strips, glows); the Z values are the UI's, above 2^24 */
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    tick();
    uint32_t w, h;
    const uint8_t *s = readTarget(RD_TARGET_SCENE, &w, &h);
    if (!s) {
        return;
    }
    const uint8_t *a = &s[(32 * W + 32) * 4], *b = &s[(32 * W + 80) * 4],
                  *c = &s[(300 * W + 300) * 4];
    CHECK(a[0] == 200 && a[2] == 0,
          "depth: GEQUAL 0xFFFFFF9B over 0xFFFFFFFF fails on PSMZ32 (got %u,%u,%u)", a[0], a[1],
          a[2]);
    CHECK(b[1] == 200 && b[0] == 0,
          "depth: GEQUAL 0xFFFFFFFF over 0xFFFFFF9B passes (got %u,%u,%u)", b[0], b[1], b[2]);
    CHECK(c[0] == 33 && c[1] == 66 && c[2] == 99 && c[3] == 0x80,
          "clear: SCENE cleared to the flip's BG colour (got %u,%u,%u,%u)", c[0], c[1], c[2], c[3]);
}

static void checkHalf(RdTex rows)
{
    /* field 0 for three flips (the PS2's half-offset case): the frame still
       closes without the half offset, and every row samples its own texel */
    setField(0);
    for (int i = 0; i < 3; i++) {
        drawScene(rows, RD_FILTER_LINEAR);
        tick();
    }
    CHECK(halfOf(rd__last_frame(), 0) == 0, "half: field 0, the frame has no half offset");
    uint32_t w, h;
    const uint8_t *s = readTarget(RD_TARGET_SCENE, &w, &h);
    if (s) {
        /* G = 4y within each 64-row period, not rows y and y+1 averaged */
        int bad = 0;
        for (int y = 0; y < H; y++) {
            const int want = (y % 64) * 4;
            const int got = s[(y * W + 100) * 4 + 1];
            if (abs(got - want) > 1 && bad++ < 4) {
                printf("FAIL half: row %d G %d, expected %d\n", y, got, want);
            }
        }
        failures += bad != 0;
    }
    setField(1);
    tick();
    tick();
    tick();
}

/* ---- keep */

typedef struct KeepRef {
    const uint8_t *disp;
} KeepRef;

static int bilinear(const uint8_t *img, int w, int h, double u, double v, int k)
{
    /* texel centres at i + 0.5, CLAMP */
    double fx = u - 0.5, fy = v - 0.5;
    int x0 = (int)floor(fx), y0 = (int)floor(fy);
    double ax = fx - x0, ay = fy - y0;
    int xs[2] = {x0, x0 + 1}, ys[2] = {y0, y0 + 1};
    for (int i = 0; i < 2; i++) {
        xs[i] = xs[i] < 0 ? 0 : (xs[i] >= w ? w - 1 : xs[i]);
        ys[i] = ys[i] < 0 ? 0 : (ys[i] >= h ? h - 1 : ys[i]);
    }
    double t = (1 - ax) * (1 - ay) * img[(ys[0] * w + xs[0]) * 4 + k] +
               ax * (1 - ay) * img[(ys[0] * w + xs[1]) * 4 + k] +
               (1 - ax) * ay * img[(ys[1] * w + xs[0]) * 4 + k] +
               ax * ay * img[(ys[1] * w + xs[1]) * 4 + k];
    return (int)floor(t + 0.5);
}

static int keepRef(int x, int y, int k, void *user)
{
    const KeepRef *r = user;
    /* the sprite spans -0.75 .. W + 1.25 px with UV 0.5 .. W + 0.5 (and
       half the lines); the GS samples pixel (x, y) at its integer point */
    const double u = 0.5 + (x + 0.75) * (double)W / (W + 2);
    const double v = 0.5 + (y + 0.75) * (double)(H / 2) / (H + 2);
    return (int)gs_tfx_mod((uint32_t)bilinear(r->disp, W, H / 2, u, v, k), 112u);
}

static void checkKeepPixels(void)
{
    GlobalStageSetting.reductionCol[0] = GlobalStageSetting.reductionCol[1] =
        GlobalStageSetting.reductionCol[2] = 128;
    drawScene(s_tex, RD_FILTER_NEAREST);
    tick();
    uint32_t w, h;
    static uint8_t disp[W * (H / 2) * 4];
    const uint8_t *d = readTarget(RD_TARGET_DISPLAY, &w, &h);
    if (!d || w != W || h != H / 2) {
        CHECK(0, "keep: DISPLAY");
        return;
    }
    memcpy(disp, d, sizeof(disp));
    fbKeep = 1;
    tick();
    fbKeep = 0;
    KeepRef r = {disp};
    compareScene("keep", keepRef, &r, 1);
}

/* ---- fade, brightness */

static int fadeRef(int x, int y, int k, void *user)
{
    (void)user;
    return gsLerp(fadeColor[k], scenePx(x, y, k), 50);
}

static void checkFade(void)
{
    fadeStatus = 1;
    fadeSpeed = 100.0f;
    fadeColor[0] = 200;
    fadeColor[1] = 100;
    fadeColor[2] = 7;
    drawScene(s_tex, RD_FILTER_NEAREST);
    tick();
    fadeStatus = 0;
    compareScene("fade", fadeRef, NULL, 1);
}

static int brightRef(int x, int y, int k, void *user)
{
    (void)user;
    return gsLerp(255, scenePx(x, y, k), 12);
}

static void checkBrightness(void)
{
    systemStatus[11] = 12;
    drawScene(s_tex, RD_FILTER_NEAREST);
    tick();
    systemStatus[11] = 0;
    compareScene("brightness", brightRef, NULL, 1);
}

/* ---- anti-alias */

typedef struct AaRef {
    int lv0, lv1;
    const uint8_t *aa0, *aa1;
} AaRef;

static int aaRef(int x, int y, int k, void *user)
{
    const AaRef *r = user;
    int c = scenePx(x, y, k);
    if (r->lv1) {
        /* AA1 128 -> 512: u = x/4 + 0.3125, nearest: texel (x + 1) >> 2, clamped */
        int tx = (x + 1) >> 2, ty = (y + 1) >> 2;
        tx = tx > 127 ? 127 : tx;
        ty = ty > 127 ? 127 : ty;
        c = gsLerp(r->aa1[(ty * 128 + tx) * 4 + k], c, r->lv1);
    }
    if (r->lv0) {
        /* AA0 256 -> 512: u = x/2 + 0.375: texel x >> 1 */
        c = gsLerp(r->aa0[((y >> 1) * 256 + (x >> 1)) * 4 + k], c, r->lv0);
    }
    return c;
}

static void checkAa(int lv0, int lv1, int tol)
{
    char what[64];
    GlobalStageSetting.antiLevel0 = lv0;
    GlobalStageSetting.antiLevel1 = lv1;
    drawScene(s_tex, RD_FILTER_NEAREST); /* TEX1 nearest leaks into list 10 */
    tick();
    GlobalStageSetting.antiLevel0 = GlobalStageSetting.antiLevel1 = 0;
    static uint8_t aa0[256 * 256 * 4], aa1[128 * 128 * 4];
    uint32_t w, h;
    const uint8_t *a = readTarget(RD_TARGET_AA0, &w, &h);
    if (!a || w != 256) {
        CHECK(0, "aa: AA0");
        return;
    }
    memcpy(aa0, a, sizeof(aa0));
    a = readTarget(RD_TARGET_AA1, &w, &h);
    if (!a || w != 128) {
        CHECK(0, "aa: AA1");
        return;
    }
    memcpy(aa1, a, sizeof(aa1));
    /* the downsamples: nearest at u = 2x + 0.75 is texel 2x */
    int bad = 0;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            for (int k = 0; k < 3; k++) {
                bad += aa0[(y * 256 + x) * 4 + k] != scenePx(2 * x, 2 * y, k);
                if (lv1 && x < 128 && y < 128) {
                    bad += aa1[(y * 128 + x) * 4 + k] != aa0[(2 * y * 256 + 2 * x) * 4 + k];
                }
            }
        }
    }
    CHECK(bad == 0, "aa %d/%d: downsampled levels differ from SCENE[2x][2y] at %d channels", lv0,
          lv1, bad);
    AaRef r = {lv0, lv1, aa0, aa1};
    snprintf(what, sizeof(what), "anti-alias %d/%d", lv0, lv1);
    compareScene(what, aaRef, &r, tol);
}

/* ---- film noise */

static int noiseRef(int x, int y, int k, void *user)
{
    (void)user;
    /* ST 0..4 over 512 px on a 64-texel REPEAT texture: u = x / 2, linear;
       the even grain makes every bilinear average an integer */
    double fx = x / 2.0 - 0.5, fy = y / 2.0 - 0.5;
    int x0 = (int)floor(fx), y0 = (int)floor(fy);
    double ax = fx - x0, ay = fy - y0;
    double t = 0.0;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            const int tx = (x0 + i) & 63, ty = (y0 + j) & 63;
            t += (i ? ax : 1 - ax) * (j ? ay : 1 - ay) * s_noiseImg[(ty * 64 + tx) * 4 + k];
        }
    }
    const int cs = (int)gs_tfx_mod((uint32_t)floor(t + 0.5), 0x80u);
    /* As = texture alpha 0x80 times the vertex alpha 64 */
    return gsLerp(cs, scenePx(x, y, k), 64);
}

static void checkFilmNoise(void)
{
    gFlagGameClear = 1;
    optionScreenMode = 1;
    GlobalStageSetting.targetCol[0][0] = GlobalStageSetting.targetCol[0][1] =
        GlobalStageSetting.targetCol[0][2] = 128;
    GlobalStageSetting.targetCol[0][3] = 64;
    GlobalStageSetting.grainScale = 4.0f;
    drawScene(s_tex, RD_FILTER_LINEAR); /* TEX1 linear leaks into the grain */
    dl_SetDLPriority(11);
    tick();
    gFlagGameClear = 0;
    optionScreenMode = 0;
    compareScene("film noise", noiseRef, NULL, 1);
}

static int plainRef(int x, int y, int k, void *user)
{
    (void)user;
    return scenePx(x, y, k);
}

/* photo mode's own Film effect: with the game not cleared and its Options
   row off, photo mode set to 1 (Right) draws the grain of mode 1; photo
   mode on with the effect off draws none even in a cleared game with the
   row on; and leaving photo mode gives the game's own setting back */
static void checkFilmNoisePhoto(void)
{
    GlobalStageSetting.targetCol[0][0] = GlobalStageSetting.targetCol[0][1] =
        GlobalStageSetting.targetCol[0][2] = 128;
    GlobalStageSetting.targetCol[0][3] = 64;
    GlobalStageSetting.grainScale = 4.0f;
    gFlagGameClear = 0;
    optionScreenMode = 0;
    ico_photo_enter();
    CHECK(ico_photo_film() == 0, "photo film: off at enter (%d)", ico_photo_film());
    drawScene(s_tex, RD_FILTER_LINEAR);
    dl_SetDLPriority(11);
    tick();
    compareScene("photo film off, game not cleared: no grain", plainRef, NULL, 1);
    IcoPhotoPad right = photoPad(0);
    right.pressed = ICO_PHOTO_RIGHT;
    ico_photo_update(&right);
    CHECK(ico_photo_film() == 1, "photo film: Right gives mode 1 (%d)", ico_photo_film());
    drawScene(s_tex, RD_FILTER_LINEAR);
    dl_SetDLPriority(11);
    tick();
    compareScene("photo film 1, game not cleared: the grain", noiseRef, NULL, 1);
    ico_photo_exit();
    drawScene(s_tex, RD_FILTER_LINEAR);
    dl_SetDLPriority(11);
    tick();
    compareScene("photo mode left, game not cleared: no grain", plainRef, NULL, 1);
    /* cleared, the Options row on, photo mode open and its effect off */
    gFlagGameClear = 1;
    optionScreenMode = 1;
    ico_photo_enter();
    for (int i = 0; i < ICO_PHOTO_FILM_MODES && ico_photo_film() != 0; i++) {
        ico_photo_update(&right);
    }
    CHECK(ico_photo_film() == 0, "photo film: cycled to off (%d)", ico_photo_film());
    drawScene(s_tex, RD_FILTER_LINEAR);
    dl_SetDLPriority(11);
    tick();
    compareScene("photo film off, game cleared with the row on: no grain", plainRef, NULL, 1);
    ico_photo_exit();
    drawScene(s_tex, RD_FILTER_LINEAR);
    dl_SetDLPriority(11);
    tick();
    compareScene("photo mode left, game cleared with the row on: the game's grain", noiseRef, NULL,
                 1);
    gFlagGameClear = 0;
    optionScreenMode = 0;
}

/* ---- letterbox */

static int s_bandLevel;

static int letterRef(int x, int y, int k, void *user)
{
    (void)user;
    const int c = scenePx(x, y, k);
    return (y <= 57 || y >= 455) ? gsLerp(0, c, s_bandLevel) : c;
}

static void checkLetterbox(void)
{
    current_layout_id = ico_region_cur()->layout_scene; /* la_game_demo: 55 */
    for (int i = 0; i < 10; i++) {
        drawScene(s_tex, RD_FILTER_NEAREST);
        tick();
    }
    s_bandLevel = 25; /* 10 steps of 2.5 */
    compareScene("letterbox", letterRef, NULL, 1);
    current_layout_id = 0;
    for (int i = 0; i < 12; i++) {
        tick();
    }
}

static void checkPipelines(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_REACHABLE_MAX];
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    const uint32_t c = rd__pipeline_count();
    for (uint32_t i = 0; i < c; i++) {
        const RdPipeKeyInt *k = rd__pipeline_key_at(i);
        if (k->fs == RD_FS_CAMERA_PROBE) {
            continue; /* test only */
        }
        int found = 0;
        for (uint32_t j = 0; j < n && j < RD_PIPELINE_REACHABLE_MAX; j++) {
            found |= rd__pipe_key_equal(k, &keys[j]);
        }
        CHECK(found, "pipeline %u (blend %u fmt %u/%u z %u/%u) not in the enumerated set", i,
              k->gs.blend, k->colorFmt, k->depthFmt, k->gs.ztst, k->gs.zwrite);
    }
}

/* ------------------------------------------------------------------ main */

/* FBMSK's extent: a FRAME.FBMSK left masked in list 10 (as
 * darkVolume.c's PSMCT24 composite leaves it) holds for the draws after it
 * until the next FRAME write: the anti-alias pass's or the reduction's
 * (rd_raw_test checks the frame head's) */
static RdStateBlock s_maskAt[3];
static int s_maskSeen[3];

static void maskWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    (void)user, (void)index;
    int slot = -1;
    if (list == 10 && c->type == RDC_TEXTURE_OFF && !s_maskSeen[0]) {
        slot = 0; /* the list-10 state after the mask */
    } else if (list == 10 && c->type == RDC_SCREEN && st->color == RD_TARGET_AA0 + 1) {
        slot = 1; /* the anti-alias downsample */
    } else if (list == 12 && c->type == RDC_POST_STUB && c->b[0] == RD_POST_REDUCTION) {
        slot = 2;
    }
    if (slot >= 0 && !s_maskSeen[slot]) {
        s_maskSeen[slot] = 1;
        s_maskAt[slot] = *st;
    }
}

static void maskFrame(const RdFrame *f)
{
    memset(s_maskSeen, 0, sizeof(s_maskSeen));
    RdStateBlock st = f->startState;
    rd__walk(f, 0, &st, maskWalk, NULL);
}

static void checkMask(void)
{
    for (int aa = 1; aa >= 0; aa--) {
        tick(); /* opens the frame */
        dl_SetDLPriority(10);
        rd_color_mask(0xFF000000u); /* the composite's FRAME PSMCT24 */
        rd_texture_off();           /* what a list-10 draw after it would draw with */
        GlobalStageSetting.antiLevel0 = aa ? 0x40 : 0;
        tick(); /* closes it: anti-alias (list 10), reduction (list 12) */
        GlobalStageSetting.antiLevel0 = 0;
        const RdFrame *f = rd__last_frame();
        if (!f) {
            CHECK(0, "mask: a frame");
            return;
        }
        maskFrame(f);
        CHECK(s_maskSeen[0] && s_maskAt[0].ds.colorMask == 0x7,
              "mask: list 10 after the composite keeps alpha (mask %x)", s_maskAt[0].ds.colorMask);
        if (aa) {
            CHECK(s_maskSeen[1] && s_maskAt[1].ds.colorMask == 0xF && s_maskAt[1].ds.fbmsk == 0,
                  "mask: the anti-alias pass's FRAME write ends it (mask %x)",
                  s_maskAt[1].ds.colorMask);
        }
        CHECK(s_maskSeen[2] && s_maskAt[2].ds.colorMask == 0xF,
              "mask: the reduction writes DISPLAY's alpha (mask %x)", s_maskAt[2].ds.colorMask);
    }
}

/* Options > Effects > Screen softening off: the stage's levels
   record no anti-alias pass (rd_post.c expands RD_POST_AA_DOWNSAMPLE and
   RD_POST_AA_COMPOSITE into list-10 sprites drawing into or sampling AA0
   and AA1); back on, its four sprites */
static int s_aaSprites;

static void aaWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    (void)user, (void)index;
    if (list != 10 || c->type != RDC_SCREEN) {
        return;
    }
    const uint32_t aa0 = rd_target(RD_TARGET_AA0).id, aa1 = rd_target(RD_TARGET_AA1).id;
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    const int samples = t && t->kind == RD_TEXKIND_TARGET && (t->target == aa0 || t->target == aa1);
    s_aaSprites += st->color == aa0 || st->color == aa1 || samples;
}

static int aaRecords(const RdFrame *f)
{
    s_aaSprites = 0;
    RdStateBlock st = f->startState;
    rd__walk(f, 0, &st, aaWalk, NULL);
    return s_aaSprites;
}

static void checkSofteningOff(void)
{
    IcoVideoOptions o;
    for (int on = 0; on <= 1; on++) {
        ico_video_get(&o);
        o.effectSoftening = on;
        ico_video_set(&o);
        CHECK(ico_video_effect_softening() == on, "softening: the switch reads %d", on);
        tick(); /* opens the frame */
        GlobalStageSetting.antiLevel0 = 0x40;
        GlobalStageSetting.antiLevel1 = 0x20;
        tick(); /* closes it: anti-alias (list 10) */
        GlobalStageSetting.antiLevel0 = GlobalStageSetting.antiLevel1 = 0;
        const RdFrame *f = rd__last_frame();
        if (!f) {
            CHECK(0, "softening: a frame");
            return;
        }
        const int n = aaRecords(f);
        CHECK(n == (on ? 4 : 0), "softening %s: %d anti-alias sprites (want %d)", on ? "on" : "off",
              n, on ? 4 : 0);
    }
}

/* Options > Effects > Cinematic bars.  On layout 55 the level ramps
   up and list 11 records the two bars (two full-screen sprites); with the
   switch off it records two fewer, at once if the switch flips mid-cutscene.
   The motion blur is left alone while the level is above 0 in both states
   and restored once it is back to 0 (10 steps down from 25: ticks 10 to 12
   of the 12 call it), with the switch back on */
static int fullScreenSprites(void)
{
    const RdFrame *f = rd__last_frame();
    int n = 0;
    for (uint32_t i = 0; f && i < f->lists[11].count; i++) {
        const RdCmd *c = &f->lists[11].cmds[i];
        n += c->type == RDC_SCREEN && c->b[1] == RD_SPACE_FULLFRAME;
    }
    return n;
}

static void checkCinematicBars(void)
{
    IcoVideoOptions o;
    int n[2] = {0, 0};

    for (int on = 1; on >= 0; on--) {
        ico_video_get(&o);
        o.effectCinematicBars = on;
        ico_video_set(&o);
        CHECK(ico_video_effect_cinematic_bars() == on, "bars: the switch reads %d", on);
        current_layout_id = ico_region_cur()->layout_scene; /* la_game_demo: 55 */
        s_blurCalls = 0;
        for (int i = 0; i < 10; i++) {
            tick();
        }
        CHECK(rd__last_frame() != NULL, "bars: a frame");
        n[on] = fullScreenSprites();
        CHECK(s_blurCalls == 0, "bars %d: the motion blur is left alone while up (%d calls)", on,
              s_blurCalls);
        current_layout_id = 0;
        s_blurCalls = 0;
        for (int i = 0; i < 12; i++) {
            tick();
        }
        CHECK(s_blurCalls == 3, "bars %d: the blur is restored once the level is 0 (%d calls)", on,
              s_blurCalls);
    }
    CHECK(n[1] == n[0] + 2, "bars: 2 fewer full-screen sprites when off (%d on, %d off)", n[1],
          n[0]);

    /* switching mid-cutscene applies at once */
    ico_video_get(&o);
    o.effectCinematicBars = 1;
    ico_video_set(&o);
    current_layout_id = ico_region_cur()->layout_scene; /* la_game_demo: 55 */
    for (int i = 0; i < 5; i++) {
        tick();
    }
    const int before = fullScreenSprites();
    o.effectCinematicBars = 0;
    ico_video_set(&o);
    tick();
    CHECK(fullScreenSprites() == before - 2, "bars: switched off mid-cutscene (%d -> %d)", before,
          fullScreenSprites());
    o.effectCinematicBars = 1;
    ico_video_set(&o);
    tick();
    CHECK(fullScreenSprites() == before, "bars: switched back on mid-cutscene (%d)",
          fullScreenSprites());
    current_layout_id = 0;
    for (int i = 0; i < 12; i++) {
        tick();
    }
}

/* ------------------------------------------------ the GS register decoder
   (GifPacket.c) at the native frame widths.  One packet per width writes a
   draw environment (SCENE), a narrower SCISSOR, a full-frame UV sprite, a
   vertex with XYZ2_HI, and the work buffers at the widths the native frame gives them
   (W/2 x 128, W/4 x 128, W/2 x 256), a sprite after each so rd sees every
   environment.  gsFrame.w is set by hand as the flip would latch it.  At
   512 the expectations are literals (what the decoder gave before the
   native frame), so the 4:3 output is byte-identical. */
#define DEC_LIST 5

static void decSprite(int x0, int y0, int x1, int y1)
{
    gif_SetGsReg(0x00, 6);
    gif_SetGsReg(0x05,
                 (unsigned long long)(x0 & 0xFFFF) | ((unsigned long long)(y0 & 0xFFFF) << 16));
    gif_SetGsReg(0x05,
                 (unsigned long long)(x1 & 0xFFFF) | ((unsigned long long)(y1 & 0xFFFF) << 16));
}

static void checkDecoderAt(int fw)
{
    const int fh = 448;
    const int hw = fw / 2, qw = fw / 4;

    tick();
    gsFrame.w = fw;
    gsFrame.fbw = (fw + 63) / 64;
    gif_HostSetFrameWidth(fw); /* as GsBase.c's latch hands it to the decoder */
    gif_StartPacketPri(DEC_LIST);
    gif_SetDrawEnviroment(0x800, 0, (unsigned int)fw, (unsigned int)fh, 1, 0);
    gif_SetGsReg(0x40,
                 ((unsigned long long)(fw - 100) << 16) | ((unsigned long long)(fh - 1) << 48));
    /* the full-frame UV sprite: UV (8, 8) to (8 + fw * 16, 8 + fh * 16) */
    gif_SetGsReg(0x00, 6 | 0x100); /* sprite, FST */
    gif_SetGsReg(0x03, 8ull | (8ull << 16));
    gif_SetGsReg(0x05, (unsigned long long)((2048 - hw) * 16) |
                           ((unsigned long long)((2048 - fh / 2) * 16) << 16));
    gif_SetGsReg(0x03,
                 (unsigned long long)(8 + fw * 16) | ((unsigned long long)(8 + fh * 16) << 16));
    gif_SetGsReg(0x05, (unsigned long long)((2048 + hw) * 16) |
                           ((unsigned long long)((2048 + fh / 2) * 16) << 16));
    /* a vertex past 0xFFFF: XYZ2_HI 0xFFFF / 0x0001 on x 0x1234, y 2 */
    gif_SetGsReg(0x00, 6);
    gif_SetGsReg(0x7E, 0xFFFFull | (1ull << 16));
    gif_SetGsReg(0x05, 0x1234ull | (2ull << 16));
    gif_SetGsReg(0x05, 0x1244ull | (0x12ull << 16));
    gif_SetDrawEnviroment(0x2800, 0, (unsigned int)hw, 128, 0, 0); /* WORK0 */
    decSprite(0, 0, 16, 16);
    gif_SetDrawEnviroment(0x2C00, 0, (unsigned int)qw, 128, 0, 0); /* AA1 */
    decSprite(0, 0, 16, 16);
    gif_SetDrawEnviroment(0x2C00, 0, (unsigned int)hw, 256, 0, 0); /* WORK1 */
    decSprite(0, 0, 16, 16);
    gif_EndPacket();
    tick();

    const RdFrame *f = rd__last_frame();
    if (!f) {
        CHECK(0, "decoder %d: a frame", fw);
        return;
    }
    const RdCmdList *l = &f->lists[DEC_LIST];
    uint32_t tgt[8][2], nt = 0, nsc = 0;
    int32_t sc[4][4];
    RdScreenVtx vtx[16];
    uint32_t nv = 0;

    for (uint32_t i = 0; i < l->count; i++) {
        const RdCmd *c = &l->cmds[i];
        if (c->type == RDC_TARGET && nt < 8) {
            tgt[nt][0] = c->u[0];
            tgt[nt][1] = c->u[2];
            nt++;
        } else if (c->type == RDC_SCISSOR && nsc < 4) {
            for (int k = 0; k < 4; k++) {
                sc[nsc][k] = (int32_t)c->u[k];
            }
            nsc++;
        } else if (c->type == RDC_SCREEN && c->b[0] == RD_PRIM_SPRITES) {
            const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
            for (uint32_t k = 0; k < c->u[1] && nv < 16; k++) {
                vtx[nv++] = v[k];
            }
        }
    }
    CHECK(nt == 4, "decoder %d: %u targets recorded, expected 4", fw, nt);
    CHECK(nv == 10, "decoder %d: %u sprite vertices recorded, expected 10", fw, nv);
    if (nt != 4 || nv != 10) {
        return;
    }
    /* FRAME / XYOFFSET to the native width */
    CHECK(tgt[0][0] == rd_target(RD_TARGET_SCENE).id && tgt[0][1] == (uint32_t)(fw | (fh << 16)),
          "decoder %d: SCENE target size %08x", fw, tgt[0][1]);
    /* SCISSOR past the GS's 11 bits */
    CHECK(nsc == 1 && sc[0][0] == 0 && sc[0][1] == 0 && sc[0][2] == fw - 100 && sc[0][3] == fh - 1,
          "decoder %d: scissor %u records, x1 %d (expected %d)", fw, nsc, nsc ? sc[0][2] : -1,
          fw - 100);
    /* the full-frame UV sprite keeps its extents */
    CHECK(vtx[0].s == 8.0f && vtx[0].t == 8.0f, "decoder %d: UV origin %g %g", fw, (double)vtx[0].s,
          (double)vtx[0].t);
    CHECK(vtx[1].s == (float)(8 + fw * 16) && vtx[1].t == (float)(8 + fh * 16),
          "decoder %d: UV extent %g %g (expected %d %d)", fw, (double)vtx[1].s, (double)vtx[1].t,
          8 + fw * 16, 8 + fh * 16);
    CHECK(vtx[0].x == (2048 - hw) * 16 && vtx[1].x == (2048 + hw) * 16,
          "decoder %d: sprite x %d..%d", fw, vtx[0].x, vtx[1].x);
    /* XYZ2_HI: signed 32-bit (hi << 16) | lo; the next vertex is the plain 16 bits */
    CHECK(vtx[2].x == (int32_t)0xFFFF1234u && vtx[2].y == 0x10002 && vtx[3].x == 0x1244 &&
              vtx[3].y == 0x12,
          "decoder %d: XYZ2_HI vertex %d %d, then %d %d", fw, vtx[2].x, vtx[2].y, vtx[3].x,
          vtx[3].y);
    /* the work buffers the frame scales: W/2 x 128 WORK0, W/4 x 128 AA1, W/2 x 256 WORK1 */
    CHECK(tgt[1][0] == rd_target(RD_TARGET_WORK0).id && tgt[1][1] == (uint32_t)(hw | (128 << 16)),
          "decoder %d: %dx128 is WORK0 (target %u size %08x)", fw, hw, tgt[1][0], tgt[1][1]);
    CHECK(tgt[2][0] == rd_target(RD_TARGET_AA1).id && tgt[2][1] == (uint32_t)(qw | (128 << 16)),
          "decoder %d: %dx128 on TBP 0x2C00 is AA1 (target %u)", fw, qw, tgt[2][0]);
    CHECK(tgt[3][0] == rd_target(RD_TARGET_WORK1).id && tgt[3][1] == (uint32_t)(hw | (256 << 16)),
          "decoder %d: %dx256 on TBP 0x2C00 is WORK1 (target %u)", fw, hw, tgt[3][0]);
    if (fw == 512) {
        /* the 4:3 literals, as the decoder gave them before the native frame */
        CHECK(tgt[0][1] == 0x01C00200u && sc[0][2] == 412 && vtx[1].s == 8200.0f &&
                  vtx[1].t == 7176.0f && vtx[0].x == 28672 && vtx[1].x == 36864 &&
                  tgt[1][1] == 0x00800100u && tgt[2][1] == 0x00800080u && tgt[3][1] == 0x01000100u,
              "decoder 512: the 4:3 decode differs from the recorded expectation");
    }
}

/* TEX0 on TBP 0x2C00 reads AA1 at the quarter-frame width's TW and WORK1 at
   the half-frame's */
static void checkDecoderTex(int fw)
{
    tick();
    gsFrame.w = fw;
    gsFrame.fbw = (fw + 63) / 64;
    gif_HostSetFrameWidth(fw); /* as GsBase.c's latch hands it to the decoder */
    for (int pass = 0; pass < 2; pass++) {
        const int tw = ico_frame_tw(pass ? fw / 2 : fw / 4);
        gif_StartPacketPri(DEC_LIST);
        gif_SetGsReg(0x06, 0x2C00ull | (1ull << 14) | ((unsigned long long)tw << 26) |
                               (7ull << 30) | (1ull << 34));
        gif_SetGsReg(0x00, 6 | 0x10 | 0x100); /* sprite, TME, FST */
        gif_SetGsReg(0x03, 0);
        gif_SetGsReg(0x05, 0x8000ull | (0x8000ull << 16));
        gif_SetGsReg(0x03, 16ull | (16ull << 16));
        gif_SetGsReg(0x05, 0x8100ull | (0x8100ull << 16));
        gif_EndPacket();
        tick();
        const RdFrame *f = rd__last_frame();
        const RdCmd *t = f ? firstOf(f, DEC_LIST, RDC_TEXTURE) : NULL;
        const RdTexRec *r = t ? rd__tex_rec(t->u[0]) : NULL;
        const RdTargetId want = pass ? RD_TARGET_WORK1 : RD_TARGET_AA1;

        CHECK(r && r->kind == RD_TEXKIND_TARGET && r->target == rd_target(want).id,
              "decoder %d: TEX0 0x2C00 TW %d reads %s", fw, tw, pass ? "WORK1" : "AA1");
    }
}

static void checkDecoder(void)
{
    static const int widths[] = {512, 688, 1376, 2048, 2560};

    for (unsigned int i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        checkDecoderAt(widths[i]);
        checkDecoderTex(widths[i]);
    }
    gsFrame.w = ICO_FRAME_BASE_W;
    gsFrame.fbw = ICO_FRAME_BASE_W / 64;
    gif_HostSetFrameWidth(ICO_FRAME_BASE_W);
    tick();
}

/* ------------------------------------- the CPU strips' window (GifPacket.c)
   isInScreen keeps the PS2's compares, 0 .. 0xFFF0, at every native width;
   on a frame wider than 4:3 a strip triangle with a failing vertex is
   kicked only when its three Z values are in range (in front of the eye)
   and its x extent lies wholly beside the camera's picture (GS X 1792 ..
   2304: gifHostBeside), its vertex past 16 bits through XYZ2_HI.  One
   gif_Draw2DStripG of three vertices (pixels, Z) per case; returns the
   triangles recorded, the first one's vertices in out. */
static int stripCase(int fw, const int px[3][3], RdScreenVtx out[3])
{
    int v[3][4];
    GifColor col[3];
    int tris = 0;

    tick();
    gsFrame.w = fw;
    gsFrame.fbw = (fw + 63) / 64;
    gif_HostSetFrameWidth(fw);
    for (int i = 0; i < 3; i++) {
        v[i][0] = px[i][0] * 16;
        v[i][1] = px[i][1] * 16;
        v[i][2] = px[i][2];
        v[i][3] = 0;
        col[i].r = col[i].g = col[i].b = 0x80;
        col[i].a = 0x80;
    }
    gif_StartPacketPri(DEC_LIST);
    gif_SetDrawEnviroment(0x800, 0, (unsigned int)fw, 448, 1, 0);
    gif_Draw2DStripG(&v[0][0], col, 3, 0);
    gif_EndPacket();
    tick();
    const RdFrame *f = rd__last_frame();
    if (!f) {
        return -1;
    }
    const RdCmdList *l = &f->lists[DEC_LIST];
    for (uint32_t i = 0; i < l->count; i++) {
        const RdCmd *c = &l->cmds[i];
        if (c->type == RDC_SCREEN && c->b[0] == RD_PRIM_TRIANGLES) {
            const RdScreenVtx *sv = (const RdScreenVtx *)(f->payload + c->u[0]);
            if (tris == 0 && c->u[1] >= 3) {
                memcpy(out, sv, 3 * sizeof(*sv));
            }
            tris += (int)(c->u[1] / 3);
        }
    }
    return tris;
}

static void checkStripWindow(void)
{
    static const int widths[] = {512, 688, 2560};
    /* all three inside: drawn at every width */
    static const int in[3][3] = {
        {2000, 2000, 0x100000}, {2100, 2000, 0x100000}, {2050, 2100, 0x100000}};
    /* reaching the picture, a vertex at x 4200 (67200 > 0xFFF0, inside
       the window the port scaled by W / 512 before): dropped everywhere */
    static const int reach[3][3] = {
        {2000, 2000, 0x100000}, {2100, 2000, 0x100000}, {4200, 2100, 0x100000}};
    /* wholly beside the picture, a vertex at x 5600 (89600, past even the
       scaled window at 688): drawn on a wide frame, through XYZ2_HI */
    static const int beside[3][3] = {
        {2400, 2000, 0x100000}, {2500, 2100, 0x100000}, {5600, 2050, 0x100000}};
    /* the same with that vertex's Z negative (behind the eye): dropped */
    static const int behind[3][3] = {
        {2400, 2000, 0x100000}, {2500, 2100, 0x100000}, {5600, 2050, -16}};

    for (unsigned int i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        const int fw = widths[i];
        RdScreenVtx t[3];
        int n;
        volatile int p[4] = {0xFFF0, 0x8000, 0x100000, 0};

        /* the PS2's compares at every width */
        CHECK(_IsInScreen(p) == 1, "strips %d: x 0xFFF0 passes", fw);
        p[0] = 0xFFF1;
        CHECK(_IsInScreen(p) == 0, "strips %d: x 0xFFF1 fails", fw);
        p[0] = -1;
        CHECK(_IsInScreen(p) == 0, "strips %d: x -1 fails", fw);
        n = stripCase(fw, in, t);
        CHECK(n == 1 && t[0].x == 32000 && t[2].y == 33600,
              "strips %d: the inside triangle drawn (%d, x %d)", fw, n, n > 0 ? t[0].x : -1);
        n = stripCase(fw, reach, t);
        CHECK(n == 0,
              "strips %d: the triangle reaching the picture with a vertex past 0xFFF0 is "
              "dropped (%d)",
              fw, n);
        n = stripCase(fw, beside, t);
        if (fw == ICO_FRAME_BASE_W) {
            /* 4:3: the PS2's drop (the rule is off; the packet is the PS2's) */
            CHECK(n == 0,
                  "strips 512: the triangle past 0xFFF0 is dropped as the PS2 drops it "
                  "(%d)",
                  n);
        } else {
            CHECK(n == 1 && t[2].x == 89600 && t[2].y == 2050 * 16 && t[0].x == 2400 * 16,
                  "strips %d: the triangle wholly beside the picture is drawn, its far vertex "
                  "at x %d (want 89600)",
                  fw, n, n > 0 ? t[2].x : -1);
        }
        n = stripCase(fw, behind, t);
        CHECK(n == 0,
              "strips %d: a triangle beside the picture with a vertex behind the eye is "
              "dropped (%d)",
              fw, n);
    }
    gsFrame.w = ICO_FRAME_BASE_W;
    gsFrame.fbw = ICO_FRAME_BASE_W / 64;
    gif_HostSetFrameWidth(ICO_FRAME_BASE_W);
    tick();
}

/* Outside the out-of-the-box state
   (gif_HostExtended: a frame wider than 4:3, or 4:3 with the extended
   state set as GsBase.c's latch sets it for any preset but Original) a
   strip triangle with a failing vertex and its Z in range that reaches the
   picture is drawn when its strip has a passing triangle (not a card); a
   strip none of whose triangles pass (a card) stays dropped, and a
   triangle with a vertex behind the eye too.  gif_DrawPolyF4's quad is one
   strip of two.  Out of the box both stay the PS2's (one triangle, no
   quad).  One gif_Draw2DStripG of n vertices; returns the triangles, the
   last kicked one's vertices in out. */
extern float ico_current_matrix[4][4]; /* port/math: rotTransPers's matrix */

static int stripCaseN(int fw, int ext, const int (*px)[3], int n, RdScreenVtx out[3])
{
    int v[8][4];
    GifColor col[8];
    int tris = 0;

    tick();
    gsFrame.w = fw;
    gsFrame.fbw = (fw + 63) / 64;
    gif_HostSetFrameWidth(fw);
    gif_HostSetExtended(ext);
    for (int i = 0; i < n; i++) {
        v[i][0] = px[i][0] * 16;
        v[i][1] = px[i][1] * 16;
        v[i][2] = px[i][2];
        v[i][3] = 0;
        col[i].r = col[i].g = col[i].b = 0x80;
        col[i].a = 0x80;
    }
    gif_StartPacketPri(DEC_LIST);
    gif_SetDrawEnviroment(0x800, 0, (unsigned int)fw, 448, 1, 0);
    if (n == 4 && px[0][2] == -1) {
        /* gif_DrawPolyF4: the identity matrix, so a point (x, y, z, 1) is
           projected to 16 x, 16 y, 16 z */
        float p[4][4];
        memset(ico_current_matrix, 0, sizeof(ico_current_matrix));
        for (int i = 0; i < 4; i++) {
            ico_current_matrix[i][i] = 1.0f;
            p[i][0] = (float)px[i][0];
            p[i][1] = (float)px[i][1];
            p[i][2] = 4096.0f;
            p[i][3] = 1.0f;
        }
        gif_DrawPolyF4(p[0], p[1], p[2], p[3], 0x80, 0x80, 0x80, 0x80, 0);
    } else {
        gif_Draw2DStripG(&v[0][0], col, n, 0);
    }
    gif_EndPacket();
    gif_HostSetExtended(0);
    tick();
    const RdFrame *f = rd__last_frame();
    if (!f) {
        return -1;
    }
    const RdCmdList *l = &f->lists[DEC_LIST];
    for (uint32_t i = 0; i < l->count; i++) {
        const RdCmd *c = &l->cmds[i];
        if (c->type == RDC_SCREEN && c->b[0] == RD_PRIM_TRIANGLES) {
            const RdScreenVtx *sv = (const RdScreenVtx *)(f->payload + c->u[0]);
            if (c->u[1] >= 3) {
                memcpy(out, sv + c->u[1] - 3, 3 * sizeof(*sv));
            }
            tris += (int)(c->u[1] / 3);
        }
    }
    return tris;
}

static void checkStripCards(void)
{
    static const struct {
        int fw, ext;
    } kStates[] = {{512, 0}, {512, 1}, {688, 0}, {2560, 0}};

    /* a wall: triangle 2 passes, triangle 3 has a vertex at x 4200 (67200
       > 0xFFF0) and reaches the picture */
    static const int wall[4][3] = {{2000, 2000, 0x100000},
                                   {2100, 2000, 0x100000},
                                   {2050, 2100, 0x100000},
                                   {4200, 2100, 0x100000}};
    /* a card: both triangles have a vertex past 0xFFF0 */
    static const int card[4][3] = {{2000, 2000, 0x100000},
                                   {2100, 2000, 0x100000},
                                   {4200, 2050, 0x100000},
                                   {4300, 2100, 0x100000}};
    /* the wall with that vertex behind the eye (Z negative) */
    static const int eye[4][3] = {
        {2000, 2000, 0x100000}, {2100, 2000, 0x100000}, {2050, 2100, 0x100000}, {4200, 2100, -16}};
    /* gif_DrawPolyF4 (z -1 picks it): the wall's quad and the card's */
    static const int quad[4][3] = {
        {2000, 2000, -1}, {2100, 2000, -1}, {2050, 2100, -1}, {4200, 2100, -1}};
    static const int quadCard[4][3] = {
        {2000, 2000, -1}, {2100, 2000, -1}, {4200, 2050, -1}, {4300, 2100, -1}};

    for (unsigned int i = 0; i < sizeof(kStates) / sizeof(kStates[0]); i++) {
        const int fw = kStates[i].fw, ext = kStates[i].ext;
        const int box = fw == ICO_FRAME_BASE_W && !ext; /* out of the box */
        RdScreenVtx t[3];
        int n = stripCaseN(fw, ext, wall, 4, t);
        if (box) {
            CHECK(n == 1,
                  "strip cards %d/%d: out of the box the wall's failing triangle is "
                  "dropped as the PS2 drops it (%d)",
                  fw, ext, n);
        } else {
            CHECK(n == 2 && t[2].x == 67200,
                  "strip cards %d/%d: the wall's triangle reaching the picture with a vertex past "
                  "0xFFF0 is drawn, its far vertex at x %d (want 67200; %d triangles)",
                  fw, ext, n > 0 ? t[2].x : -1, n);
        }
        n = stripCaseN(fw, ext, card, 4, t);
        CHECK(n == 0, "strip cards %d/%d: a strip none of whose triangles pass is dropped (%d)", fw,
              ext, n);
        n = stripCaseN(fw, ext, eye, 4, t);
        CHECK(n == 1,
              "strip cards %d/%d: the triangle with a vertex behind the eye is dropped (%d)", fw,
              ext, n);
        n = stripCaseN(fw, ext, quad, 4, t);
        CHECK(box ? n == 0 : n == 2,
              "strip cards %d/%d: gif_DrawPolyF4's quad with one passing triangle: %d triangles "
              "(want %d)",
              fw, ext, n, box ? 0 : 2);
        n = stripCaseN(fw, ext, quadCard, 4, t);
        CHECK(n == 0, "strip cards %d/%d: gif_DrawPolyF4's quad as a card is dropped (%d)", fw, ext,
              n);
    }
    gsFrame.w = ICO_FRAME_BASE_W;
    gsFrame.fbw = ICO_FRAME_BASE_W / 64;
    gif_HostSetFrameWidth(ICO_FRAME_BASE_W);
    tick();
}

/* the film grain's texture bound since the last call (gsb_filmNoise) */
static int grainPosts(void)
{
    const int n = s_grainTex;
    s_grainTex = 0;
    return n;
}

/* the commands of the last frame, every list's count */
static uint32_t frameCommands(void)
{
    const RdFrame *f = rd__last_frame();
    uint32_t n = 0;
    for (int l = 0; f && l < RD_LIST_COUNT; l++) {
        n += f->lists[l].count;
    }
    return n;
}

static void checkDejaVeil(void)
{
    static GObj g[2];
    static Sub15C d[2];
    static struct DObjNode ownNodes[3];
    static float ownMtx[3][16];
    static char model[64], light[64];
    memset(g, 0, sizeof(g));
    memset(d, 0, sizeof(d));
    /* another stage object first, then the sandstorm one */
    g[0].dobj = &d[0];
    g[0].kindNext = &g[1];
    d[0].modelId = 1552;
    d[0].model = (struct PObjModel *)(void *)model;
    d[0].lightMtx = (struct LightMatrix *)(void *)light;
    d[0].dispType = 2;
    g[1].dobj = &d[1];
    d[1].modelId = 1614;
    d[1].model = (struct PObjModel *)(void *)model;
    d[1].lightMtx = (struct LightMatrix *)(void *)light;
    d[1].dispType = 2;
    d[1].nodeNum = 3;
    d[1].nodes = ownNodes;
    d[1].nodeMtx = (ICO_WORD)ownMtx;
    s_kindHead = &g[0];

    IcoVideoOptions o;
    ico_video_defaults(&o);
    ico_video_set(&o);
    GlobalStageSetting.targetCol[0][3] = 64;
    GlobalStageSetting.grainScale = 4.0f;
    gFlagGameClear = 0;
    optionScreenMode = 0;
    tick();
    systemStatus[5] = 1;
    stage_no = 11;
    ico_photo_reset();
    ico_photo_set_tick_hz(25);
    gameCamAt(PHOTO_D);
    ico_photo_enter();
    const IcoPhotoPad right = {0, ICO_PHOTO_RIGHT, {128, 128, 128, 128}};
    const IcoPhotoPad still = {0, 0, {128, 128, 128, 128}};
    float pair[32], other[32], after[32];
    /* off, 1 .. 4: no call, the frame as photo mode's without the hook */
    uint32_t plain = 0;
    for (int m = 0; m < ICO_PHOTO_FILM_DEJA; m++) {
        ico_photo_update(m == 0 ? &still : &right);
        ico_photo_view_tick();
        s_disp.calls = 0;
        grainPosts();
        tick();
        const int grain = grainPosts();
        CHECK(ico_photo_film() == m && !ico_photo_deja() && s_disp.calls == 0,
              "veil: film %d draws no veil (%d calls)", m, s_disp.calls);
        CHECK(grain == (m > 0), "veil: film %d, the grain %d times", m, grain);
        if (m == 0) {
            plain = frameCommands();
        }
    }
    /* Deja vu: one call a frame, the stage's sandstorm object with the
       stage's two nodes, under the tick's full screen pair; a reflection's
       pair (another one in +0x640 at the post effect) put back after */
    ico_photo_update(&right);
    ico_photo_view_tick();
    memcpy(pair, matrixptr + 0x640, sizeof(pair));
    for (int i = 0; i < 32; i++) {
        other[i] = (float)(i + 1) * 0.5f;
    }
    memcpy(matrixptr + 0x640, other, sizeof(other));
    memset(&s_disp, 0, sizeof(s_disp));
    s_blurPri = -1;
    s_fogPri = 3;
    grainPosts();
    gsb_SyncGSSystem();
    s_fogPri = -1;
    memcpy(after, matrixptr + 0x640, sizeof(after));
    gsb_UpdateGSSystem(0);
    CHECK(ico_photo_deja() && s_disp.calls == 1 && s_disp.o == &d[1] && s_kindAsked == 35,
          "veil: Deja vu draws the stage's sandstorm object once (%d calls, kind %d)", s_disp.calls,
          s_kindAsked);
    CHECK(s_disp.nodeNum == 2, "veil: two nodes (%d)", s_disp.nodeNum);
    static const float z[2] = {0.72907043f, 0.74207032f}, fade[2] = {0.9f, -0.95f};
    int nodesOk = 1;
    for (int i = 0; i < 2; i++) {
        const struct DObjNode *n = &s_disp.nodes[i];
        nodesOk &= n->flags.ll == 3 && n->fade == fade[i] && n->alpha == 1.0f &&
                   n->pos[0] == 0.0f && n->pos[1] == 0.0f && n->pos[2] == z[i] && n->pos[3] == 1.0f;
        for (int k = 0; k < 16; k++) {
            const float want = k == 15 ? 1.0f : (k % 5 == 0 ? 0.0039999341f : 0.0f);
            nodesOk &= s_disp.mtx[i][k] == want;
        }
        /* reg_dispMObj's dissolve: FIX (1 - |fade|) x 96, added for a fade
           above 0 and subtracted below (RegistPacket.c reg_setDissolve) */
        const float a = 1.0f - (1.0f - n->fade) * n->alpha;
        const int fix = (int)((a < 0.0f ? a + 1.0f : 1.0f - a) * 96.0f);
        nodesOk &= fix == (i == 0 ? 9 : 4) && (a > 0.0f) == (i == 0);
    }
    CHECK(nodesOk, "veil: the nodes of deja_lightning.bga, held in the camera's space (FIX 9 "
                   "added, FIX 4 subtracted)");
    CHECK(memcmp(s_disp.screen500, pair, sizeof(pair)) == 0,
          "veil: drawn under the tick's full screen 500 unit pair");
    CHECK(memcmp(after, other, sizeof(other)) == 0, "veil: +0x640 and +0x680 put back");
    CHECK(d[1].nodeNum == 3 && d[1].nodes == ownNodes && d[1].nodeMtx == (ICO_WORD)ownMtx,
          "veil: the object's own nodes given back");
    CHECK(s_disp.pri == 3 && s_blurPri == 3,
          "veil: the display list priority put back (%d before, %d after)", s_disp.pri, s_blurPri);
    const int grainDeja = grainPosts();
    CHECK(grainDeja == 0, "veil: no film grain with Deja vu (%d)", grainDeja);
    memcpy(matrixptr + 0x640, pair, sizeof(pair));
    /* stage 40 draws the veil itself */
    stage_no = 40;
    s_disp.calls = 0;
    tick();
    CHECK(s_disp.calls == 0, "veil: nothing in stage 40 (%d calls)", s_disp.calls);
    stage_no = 11;
    ico_photo_view_tick();
    /* no sandstorm object in the stage */
    g[0].kindNext = NULL;
    s_disp.calls = 0;
    tick();
    CHECK(s_disp.calls == 0, "veil: nothing without the stage's object (%d calls)", s_disp.calls);
    CHECK(frameCommands() == plain,
          "veil: without the object the frame is photo mode's without it (%u commands, %u)",
          frameCommands(), plain);
    g[0].kindNext = &g[1];
    /* photo mode closed: Deja vu kept for the next time, nothing drawn */
    ico_photo_exit();
    ico_photo_view_tick();
    s_disp.calls = 0;
    grainPosts();
    tick();
    CHECK(ico_photo_film() == ICO_PHOTO_FILM_DEJA && s_disp.calls == 0 && grainPosts() == 0,
          "veil: photo mode closed, nothing (%d calls)", s_disp.calls);
    /* the saved choice back to off for the checks after this one */
    ico_photo_enter();
    ico_photo_update(&right);
    CHECK(ico_photo_film() == 0, "veil: Right after Deja vu is off (%d)", ico_photo_film());
    ico_photo_exit();
    ico_photo_view_tick();
    s_kindHead = NULL;
    systemStatus[5] = 0;
    stage_no = 0;
    ico_photo_reset();
}

/* The reel's stage animation: film_noise_l4.bga's shape (row 70) in the
   arena: CAMERA_PARENT holding a noiseE node, a noiseB node, a second noiseE
   node and a sandstorm node, then the CAMERA; their objects (noiseE with two
   nodes, noiseB and sandstorm with one).  Every frame and node record starts
   at values of its own, the game's, which the reel must leave as they are */
typedef struct {
    BgaHeader *head;
    BgaDObjEnt *node[6]; /* CAMERA_PARENT, E0, B, E1, sandstorm, CAMERA */
    BgaExtMotion *env[6];
    Sub15C *obj[3]; /* noiseE, noiseB, sandstorm */
} ReelData;

static struct DObjNode s_reelNodes[3][2];
static float s_reelMtx[3][2][16];
static float s_reelQuat[3][2][4];
static char s_reelModel[64], s_reelLight[64];

static BgaDObjEnt *reelNodeAt(ReelData *r, int i, int type, int num, const char *name, int obj,
                              unsigned len, int envType, unsigned envLen)
{
    BgaDObjEnt *d = arenaAlloc(sizeof(BgaDObjEnt));
    d->type = (unsigned short)type;
    d->num = (unsigned short)num;
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->u.obj = obj >= 0 ? ico_eew(r->obj[obj]) : 0;
    BgaPtMotion *m = (BgaPtMotion *)&d->motion;
    m->n = 1;
    m->len = len;
    m->frame = 10.0f * (float)i + 2.0f;
    if (envType >= 0) {
        BgaEnvEnt *e = arenaAlloc(2 * sizeof(BgaEnvEnt));
        BgaExtMotion *x = arenaAlloc(sizeof(BgaExtMotion));
        x->n = 1;
        x->len = envLen;
        x->frame = 10.0f * (float)i + 1.0f;
        e[0].type = (unsigned short)envType;
        e[0].data = ico_eew(x);
        d->env = ico_eew(e);
        r->env[i] = x;
    }
    r->node[i] = d;
    return d;
}

static void reelBuild(ReelData *r)
{
    static const int model[3] = {1613, 1610, 1614};
    s_arenaTop = 16;
    memset(r, 0, sizeof(*r));
    for (int k = 0; k < 3; k++) {
        Sub15C *o = arenaAlloc(sizeof(Sub15C));
        o->modelId = model[k];
        o->model = (struct PObjModel *)(void *)s_reelModel;
        o->lightMtx = (struct LightMatrix *)(void *)s_reelLight;
        o->dispType = 2;
        o->nodeNum = k == 0 ? 2 : 1;
        o->nodes = s_reelNodes[k];
        o->nodeMtx = (ICO_WORD)s_reelMtx[k];
        o->nodeQuat = (ICO_WORD)s_reelQuat[k];
        r->obj[k] = o;
        for (int n = 0; n < 2; n++) {
            memset(&s_reelNodes[k][n], 0x40 + k * 2 + n, sizeof(s_reelNodes[k][n]));
            for (int c = 0; c < 16; c++) {
                s_reelMtx[k][n][c] = (float)(100 * k + 10 * n + c);
            }
            for (int c = 0; c < 4; c++) {
                s_reelQuat[k][n][c] = (float)(-100 * k - 10 * n - c);
            }
        }
    }
    BgaDObjEnt *cp = reelNodeAt(r, 0, 10, 0, "CAMERA_PARENT", -1, 0, -1, 0);
    BgaDObjEnt *e0 = reelNodeAt(r, 1, 10, 0, "noiseE", 0, 90, 0, 132);
    BgaDObjEnt *b = reelNodeAt(r, 2, 10, 0, "noiseB", 1, 90, 0, 91);
    BgaDObjEnt *e1 = reelNodeAt(r, 3, 10, 1, "noiseE", 0, 90, 0, 111);
    BgaDObjEnt *ss = reelNodeAt(r, 4, 10, 0, "sandstorm", 2, 0, 0, 0);
    BgaDObjEnt *cam = reelNodeAt(r, 5, 2, 0, "CAMERA", -1, 0, 1, 0);
    cp->child = ico_eew(e0);
    e0->sibling = ico_eew(b);
    b->sibling = ico_eew(e1);
    e1->sibling = ico_eew(ss);
    cp->sibling = 0;
    IcoEEWord *roots = arenaAlloc(3 * sizeof(IcoEEWord));
    roots[0] = ico_eew(cp);
    roots[1] = ico_eew(cam);
    BgaHeader *h = arenaAlloc(sizeof(BgaHeader));
    memcpy(h->magic, "BGA", 4);
    h->group = 867;
    h->mode = -1;
    h->roots = ico_eew(roots);
    h->start = 1.0f;
    h->end = 90.0f;
    h->step = 1.0f;
    h->frame = 37.0f;
    r->head = h;
}

/* the game's side of the row: the frames and the objects' node records */
typedef struct {
    float head;
    signed char mode;
    float motion[6], env[6];
    struct DObjNode nodes[3][2];
    float mtx[3][2][16], quat[3][2][4];
} ReelGame;

static void reelGame(const ReelData *r, ReelGame *g)
{
    memset(g, 0, sizeof(*g));
    g->head = r->head->frame;
    g->mode = r->head->mode;
    for (int i = 0; i < 6; i++) {
        g->motion[i] = ((BgaPtMotion *)&r->node[i]->motion)->frame;
        g->env[i] = r->env[i] ? r->env[i]->frame : 0.0f;
    }
    memcpy(g->nodes, s_reelNodes, sizeof(g->nodes));
    memcpy(g->mtx, s_reelMtx, sizeof(g->mtx));
    memcpy(g->quat, s_reelQuat, sizeof(g->quat));
}

static int reelGameSame(const ReelData *r, const ReelGame *a)
{
    ReelGame b;
    reelGame(r, &b);
    int same = a->head == b.head && a->mode == b.mode &&
               memcmp(a->nodes, b.nodes, sizeof(a->nodes)) == 0 &&
               memcmp(a->mtx, b.mtx, sizeof(a->mtx)) == 0 &&
               memcmp(a->quat, b.quat, sizeof(a->quat)) == 0;
    for (int i = 0; i < 6; i++) {
        same &= a->motion[i] == b.motion[i] && a->env[i] == b.env[i];
    }
    for (int k = 0; k < 3; k++) {
        same &= r->obj[k]->nodes == s_reelNodes[k] &&
                r->obj[k]->nodeMtx == (ICO_WORD)s_reelMtx[k] && r->obj[k]->nodeNum == (k ? 1 : 2);
    }
    return same;
}

/* one photo tick and its frame: the post effect with the reel's draws
   logged */
static void reelFrame(void)
{
    ico_photo_view_tick();
    s_dispLogN = 0;
    s_disp.calls = 0;
    tick();
}

/* reg_setDissolve's FIX for a node's fade (alpha 1), and whether it adds */
static int reelFix(float fade, int *adds)
{
    *adds = fade > 0.0f;
    return (int)((fade < 0.0f ? fade + 1.0f : 1.0f - fade) * 96.0f);
}

static void checkDejaReel(void)
{
    static ReelData r;
    static GObj g;
    ReelGame game;
    reelBuild(&r);
    /* the veil's sandstorm object is the row's (kind 35) */
    memset(&g, 0, sizeof(g));
    g.dobj = r.obj[2];
    s_kindHead = &g;
    s_reelHead = r.head;

    IcoVideoOptions o;
    ico_video_defaults(&o);
    ico_video_set(&o);
    gFlagGameClear = 0;
    optionScreenMode = 0;
    tick();
    systemStatus[5] = 1;
    stage_no = 11;
    ico_photo_reset();
    ico_photo_set_tick_hz(25);
    gameCamAt(PHOTO_D);
    ico_photo_enter();
    const IcoPhotoPad right = {0, ICO_PHOTO_RIGHT, {128, 128, 128, 128}};
    const IcoPhotoPad still = {0, 0, {128, 128, 128, 128}};
    /* off and 1 .. 4: the row never evaluated, no draw */
    s_setFrames = s_calcs = 0;
    for (int m = 0; m < ICO_PHOTO_FILM_DEJA; m++) {
        ico_photo_update(m == 0 ? &still : &right);
        reelFrame();
        CHECK(!ico_photo_deja() && s_dispLogN == 0, "reel: film %d draws nothing (%d calls)", m,
              s_dispLogN);
    }
    CHECK(s_setFrames == 0 && s_calcs == 0, "reel: no evaluation without Deja vu (%d, %d)",
          s_setFrames, s_calcs);

    /* Deja vu: the first tick starts the reel (bga_SetFrame from the
       start, looping), the game's frames and node records put back */
    ico_photo_update(&right);
    reelGame(&r, &game);
    s_reelKeyAsked = -1;
    ico_photo_view_tick();
    CHECK(ico_photo_deja() && s_reelKeyAsked == 70 && s_setFrames == 1 && s_calcs == 1 &&
              s_calcModeBad == 0 && s_quatDepth == 0,
          "reel: the first tick starts row 70 (key %d, %d starts, %d runs, %d without mode 1, "
          "quaternion depth %d)",
          s_reelKeyAsked, s_setFrames, s_calcs, s_calcModeBad, s_quatDepth);
    CHECK(reelGameSame(&r, &game), "reel: the game's frames and node records put back (start)");
    float pair[32], other[32];
    memcpy(pair, matrixptr + 0x640, sizeof(pair));
    for (int i = 0; i < 32; i++) {
        other[i] = (float)(i + 1) * 0.25f;
    }
    memcpy(matrixptr + 0x640, other, sizeof(other));
    s_dispLogN = 0;
    s_disp.calls = 0;
    s_fogPri = 3;
    s_blurPri = -1;
    gsb_SyncGSSystem();
    s_fogPri = -1;
    CHECK(memcmp(matrixptr + 0x640, other, sizeof(other)) == 0,
          "reel: +0x640 and +0x680 put back after the draws");
    gsb_UpdateGSSystem(0);
    memcpy(matrixptr + 0x640, pair, sizeof(pair));
    /* the noise objects in model order (noiseB 1610 before noiseE 1613,
       the tree has noiseE first), each once with its own node count, then
       the veil on the sandstorm object */
    CHECK(s_dispLogN == 3 && s_dispLog[0].o == r.obj[1] && s_dispLog[1].o == r.obj[0] &&
              s_dispLog[2].o == r.obj[2] && s_dispLog[0].nodeNum == 1 &&
              s_dispLog[1].nodeNum == 2 && s_dispLog[2].nodeNum == 2,
          "reel: noiseB, noiseE, then the veil (%d calls)", s_dispLogN);
    int under = 1;
    for (int i = 0; i < s_dispLogN; i++) {
        under &= memcmp(s_dispLog[i].screen500, pair, sizeof(pair)) == 0;
    }
    CHECK(under, "reel: drawn under the tick's full screen 500 unit pair");
    CHECK(s_blurPri == 3, "reel: the display list priority put back (%d)", s_blurPri);
    /* the evaluated nodes at the start (envelope frame 1): faded, held in
       the camera's space, subtracted at FIX (1 - 0.59) x 96 */
    int adds = 0;
    const struct DObjNode *nb = &s_dispLog[0].nodes[0];
    CHECK(nb->fade == reelFade(1.0f) && (nb->flags.ll & 3) == 3 && reelFix(nb->fade, &adds) == 39 &&
              !adds && s_dispLog[0].mtx[0][0] == 0.6f,
          "reel: noiseB at the start (fade %g, flags %llx)", (double)nb->fade,
          (unsigned long long)nb->flags.ll);
    CHECK(reelGameSame(&r, &game), "reel: the objects' own nodes given back after the draw");
    const float fade0 = nb->fade;

    /* ticks: the reel steps once a tick through the game's evaluator (no
       second start), the drawn fades follow the envelope frames, the game's
       state is never changed */
    int same = 1, follows = 1;
    float last = fade0;
    for (int t = 1; t <= 95; t++) {
        reelFrame();
        same &= reelGameSame(&r, &game);
        const struct DObjNode *b = &s_dispLog[0].nodes[0];
        /* noiseB's envelope (91 frames) from 1, past 91 back to 0 */
        const float f = 1.0f + (float)t <= 91.0f ? 1.0f + (float)t : (float)t - 91.0f;
        follows &= s_dispLogN == 3 && b->fade == reelFade(f);
        if (t == 10) {
            CHECK(b->fade != fade0 && b->fade > last,
                  "reel: the fade moves between ticks (%g at the start, %g ten ticks in)",
                  (double)fade0, (double)b->fade);
        }
        last = b->fade;
    }
    CHECK(s_setFrames == 1 && s_calcs == 96 && s_calcModeBad == 0 && s_quatDepth == 0,
          "reel: one start, one step a tick (%d starts, %d runs)", s_setFrames, s_calcs);
    CHECK(follows, "reel: the drawn fade follows the reel's own envelope frame, wrapping at 91");
    CHECK(same, "reel: the game's frames and node records never changed");

    /* photo mode in stage 40, which draws its own: nothing evaluated or
       drawn */
    ico_photo_exit();
    ico_photo_view_tick();
    stage_no = 40;
    ico_photo_enter();
    const int calcs = s_calcs;
    reelFrame();
    reelFrame();
    CHECK(ico_photo_deja() && s_dispLogN == 0 && s_calcs == calcs && s_setFrames == 1,
          "reel: nothing in stage 40 (%d calls, %d runs)", s_dispLogN, s_calcs - calcs);
    ico_photo_exit();
    ico_photo_view_tick();
    stage_no = 11;
    ico_photo_enter();
    ico_photo_view_tick();
    /* in another stage again: the reel starts from the start */
    CHECK(s_setFrames == 2, "reel: started again in stage 11 (%d)", s_setFrames);

    /* a row the reel cannot put back (a particle node): no reel, the veil
       alone, the row untouched */
    r.node[4]->type = 13;
    reelGame(&r, &game);
    reelFrame();
    CHECK(s_dispLogN == 1 && s_dispLog[0].o == r.obj[2] && reelGameSame(&r, &game),
          "reel: a node it cannot put back, the veil alone (%d calls)", s_dispLogN);
    r.node[4]->type = 10;
    /* no row 70 in the stage: the veil alone */
    s_reelHead = NULL;
    reelFrame();
    CHECK(s_dispLogN == 1 && s_dispLog[0].o == r.obj[2],
          "reel: without the row, the veil alone (%d calls)", s_dispLogN);
    s_reelHead = r.head;
    /* neither: nothing */
    s_kindHead = NULL;
    s_reelHead = NULL;
    reelFrame();
    CHECK(s_dispLogN == 0, "reel: without the row or the sheet, nothing (%d calls)", s_dispLogN);
    s_kindHead = &g;
    s_reelHead = r.head;
    reelFrame();
    CHECK(s_dispLogN == 3, "reel: back with the row (%d calls)", s_dispLogN);

    /* photo mode closed: nothing; reopened: the reel starts again */
    const int starts = s_setFrames;
    ico_photo_exit();
    reelFrame();
    CHECK(s_dispLogN == 0 && s_setFrames == starts, "reel: photo mode closed, nothing (%d calls)",
          s_dispLogN);
    ico_photo_enter();
    reelFrame();
    CHECK(ico_photo_deja() && s_dispLogN == 3 && s_setFrames == starts + 1,
          "reel: reopened with Deja vu, started again (%d calls, %d starts)", s_dispLogN,
          s_setFrames - starts);
    /* the saved choice back to off for the checks after this one */
    ico_photo_update(&right);
    CHECK(ico_photo_film() == 0, "reel: Right after Deja vu is off (%d)", ico_photo_film());
    ico_photo_exit();
    ico_photo_view_tick();
    s_kindHead = NULL;
    s_reelHead = NULL;
    systemStatus[5] = 0;
    stage_no = 0;
    ico_photo_reset();
}

static void boot(void)
{
    matrixptr = s_spr;
    memset(s_spr, 0, sizeof(s_spr));
    systemStatus[0] = 1; /* PAL: 512 x 512 */
    systemStatus[1] = 2;
    GlobalStageSetting.viewScale = 100;
    game_pause = 1;
    setField(1);
    gsb_InitGSSystem();
}

static void recordingChecks(void)
{
    checkBgTiming();
    checkKeep();
    checkParity();
    checkLeak();
    checkMask();
    checkSofteningOff();
    checkCinematicBars();
    checkVu();
    checkZScale();
    checkPhotoView();
    checkDejaVeil();
    checkDejaReel();
    checkCull();
    checkDecoder();
    checkStripWindow();
    checkStripCards();
}

int main(void)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    const int device = rd_init(512, 512, &st, NULL);
    if (!device && !rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    if (device) {
        printf("rd_gsbase_test: adapter %s\n", rhi_adapter_name());
    }
    boot();
    recordingChecks();
    if (!device) {
        rd_shutdown();
        if (failures) {
            printf("rd_gsbase_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_gsbase_test: recording ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    makeScene();
    s_tex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "gsbase scene");
    s_noise = rd_create_texture(64, 64, s_noiseImg, RD_TEXA_80_80, "gsbase grain");
    gif_HostSetTex0Resolver(resolver);
    static uint8_t rowsImg[W * H * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = &rowsImg[(y * W + x) * 4];
            p[0] = p[2] = 0;
            p[1] = (uint8_t)((y % 64) * 4);
            p[3] = 0x80;
        }
    }
    RdTex rows = rd_create_texture(W, H, rowsImg, RD_TEXA_80_80, "gsbase rows");
    checkCamera();
    checkDepthAndClear();
    checkHalf(rows);
    checkKeepPixels();
    checkFade();
    checkBrightness();
    checkAa(64, 0, 1);
    checkAa(0, 96, 1);
    checkAa(64, 96, 2);
    checkFilmNoise();
    checkFilmNoisePhoto();
    checkLetterbox();
    checkPipelines();
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    rd_destroy_texture(rows);
    rd_shutdown();
    if (failures) {
        printf("rd_gsbase_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_gsbase_test: ok\n");
    return 0;
}
