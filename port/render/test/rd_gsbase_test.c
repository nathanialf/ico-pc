/* rd_gsbase_test.c: GsBase.c's host path on rd (renderer wave 2, R2c).
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
 *   parity   the half offset follows GS_CSR.FIELD two flips late: constant
 *            when the field is the same at every flip (frame step 2), one
 *            per buffer when it alternates per flip
 *   leak     the post passes leave their state: fade -> ALPHA 0x44, ABE,
 *            PABE 0; keep -> TEXA 80/80, DISPLAY bound
 *   mask     FBMSK's extent (R-POST): FRAME.FBMSK masked in list 10 holds
 *            for a draw after it and ends at the anti-alias pass's FRAME
 *            write or the reduction's
 *   vu       gsb_MakeCommonMatrix's VU block and the frame camera
 *   zscale   rd__GsDepth: 0xFFFFFF9B and 0xFFFFFFFF apart under PSMZ32
 * On a Vulkan device (exit 77 without one, after the recording checks):
 *   camera   rd__CameraProbe (FrameCB through camera_probe_ps) against the
 *            C products and sceVu0RotTransPers through matrixptr+0x100
 *            (GS window X/Y within 1/16 pixel)
 *   depth    depth-tested UI Z above 2^24 on SCENE (PSMZ32 scale)
 *   half     a frame with the half offset samples between rows
 *   clear    SCENE cleared to the flip's BG colour
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
#include "video_options.h"

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

int tex_TransTexture(int id, int ret)
{
    (void)id;
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

void fog_DrawFog(void) {}

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

void MotionBlur(void) {}

void SetMotionBlur(int v)
{
    (void)v;
}

void staffRollMain(void) {}

void stage_SetLoopFlag(int g, int f)
{
    (void)g;
    (void)f;
}

void UpdateHandCameraLimitP(void) {}

void UpdateHandCameraLimitV(void) {}

void UpdateZoomMaxVallInDemo(void) {}

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
    const RdFrame *f = rd__LastFrame();
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
    const RdFrame *f = rd__LastFrame();
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
    /* the reduction in list 12: its tinted sprite's colour (R-POST: an
     * RdPostRec of kind RD_POST_REDUCTION) */
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
        rd__ApplyState(&s, &f->lists[11].cmds[i]);
        if (f->lists[11].cmds[i].type == RDC_SCREEN && i > 0) {
            texaOk = s.ds.texa == RD_TEXA_80_80 && s.ds.texEnabled;
        }
    }
    CHECK(texaOk, "keep: TEXA 80/80 and a texture in force at the keep sprite");
    tick(); /* a full frame again */
}

static void checkParity(void)
{
    int got[8];
    setField(0);
    gsb_Init(&db); /* both draw environments without the half offset */
    for (int i = 0; i < 4; i++) {
        tick();
        got[i] = halfOf(rd__LastFrame(), 0);
    }
    CHECK(got[0] == 0 && got[1] == 0 && got[2] == 1 && got[3] == 1,
          "parity: constant field 0 -> half offset from the third flip on (%d %d %d %d)", got[0],
          got[1], got[2], got[3]);
    for (int i = 0; i < 4; i++) {
        setField(i & 1 ? 0 : 1);
        tick();
        got[4 + i] = halfOf(rd__LastFrame(), 0);
    }
    CHECK(got[4] == 1 && got[5] == 1 && got[6] == 0 && got[7] == 1,
          "parity: a field alternating per flip -> one parity per buffer (%d %d %d %d)", got[4],
          got[5], got[6], got[7]);
    setField(1); /* field 1: no half offset, for the pixel checks */
    tick();
    tick();
    tick();
    CHECK(halfOf(rd__LastFrame(), 0) == 0, "parity: field 1 -> no half offset");
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
    const RdFrame *f = rd__LastFrame();
    if (!f) {
        CHECK(0, "leak: a frame");
        return;
    }
    /* the fade sprite's state, found in list 11 */
    RdStateBlock s = f->startState;
    int found = 0;
    for (int l = 0; l < RD_LIST_COUNT && !found; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            rd__ApplyState(&s, &f->lists[l].cmds[i]);
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
    const RdVuCommon *b = rd_GetVuCommon();
    CHECK(b != NULL, "vu: a block");
    if (b) {
        CHECK(memcmp(b->screenView, matrixptr + 0x100, 64) == 0, "vu: qw 4..7 = +0x100");
        CHECK(memcmp(b->viewport, matrixptr + 0x340, 64) == 0, "vu: qw 8..11 = +0x340");
        CHECK(memcmp(b->invView, matrixptr + 0x380, 64) == 0, "vu: qw 12..15 = +0x380");
        CHECK(b->unitW[3] == 1.0f && b->clip[0] == 4095.0f && b->clip[3] == 16777215.0f &&
                  b->giftag[0] == 0x8000 && b->giftag[1] == 0x302EC000u && b->giftag[2] == 0x512,
              "vu: the constant head");
    }
    const RdFrame *f = rd__RecFrame();
    CHECK(f && f->hasCamera, "vu: the frame has a camera");
    if (f) {
        CHECK(memcmp(f->camera.view, s_view, 64) == 0, "vu: camera view = +0x80");
        CHECK(memcmp(f->camera.proj43, matrixptr + 0xC0, 64) == 0, "vu: camera proj43 = +0xC0");
        CHECK(f->camera.aspect43 == 4.0f / 3.0f && f->camera.nearZ == 2.0f &&
                  f->camera.farZ == 262144.0f,
              "vu: camera aspect and planes");
    }
}

static void checkZScale(void)
{
    const float s32 = 1.0f / 4294967296.0f, s24 = 1.0f / 16777216.0f;
    CHECK(rd__TargetZScale(RD_TARGET_SCENE + 1) == s32, "zscale: SCENE is PSMZ32");
    CHECK(rd__GsDepth(0xFFFFFF9Bu, s32) > rd__GsDepth(0xFFFFFFFFu, s32),
          "zscale: 0xFFFFFF9B and 0xFFFFFFFF apart");
    CHECK(rd__GsDepth(0, s32) == 1.0f && rd__GsDepth(0xFFFFFFFFu, s32) == s32,
          "zscale: 32-bit ends");
    CHECK(gs_z_to_depth(0xFFFFFF9Bu, s32) == rd__GsDepth(0xFFFFFF9Bu, s32),
          "zscale: CPU and shader formula agree");
    for (uint32_t z = 0; z < 0x1000000u; z += 4099u) {
        if (rd__GsDepth(z, s24) != 1.0f - (float)z / 16777216.0f) {
            CHECK(0, "zscale: 24-bit exact at %u", z);
            break;
        }
    }
    CHECK(rd__GsDepth(0x1000000u, s24) == 0.0f, "zscale: 24-bit clamps above zmax");
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
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_PABE(0);
    rd_FBA(0);
    rd_Sampler(filter, filter, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
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
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

static uint8_t *readTarget(RdTargetId id, uint32_t *w, uint32_t *h)
{
    static uint8_t buf[W * H * 4];
    if (!rd__ReadTarget(rd_Target(id), buf, sizeof(buf), w, h)) {
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
    const RdFrame *f = rd__RecFrame();
    if (!f || !f->hasCamera) {
        CHECK(0, "camera: no camera in the open frame");
        return;
    }
    static const float pts[3][4] = {
        {100.0f, -50.0f, 800.0f, 1.0f}, {-300.0f, 40.0f, 200.0f, 1.0f}, {5.0f, 7.0f, -60.0f, 1.0f}};
    for (int n = 0; n < 3; n++) {
        float out[3][4];
        if (!rd__CameraProbe(&f->camera, pts[n], out)) {
            CHECK(0, "camera: probe");
            return;
        }
        float v[4], sv[4], cl[4];
        apply(v, f->camera.view, pts[n]);
        apply(cl, f->camera.proj43, v);
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
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_TextureOff();
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
        rd_TestGs(RD_TEST_Z_ALWAYS);
        rd_ZWrite(1);
        v[0].z = v[1].z = rg[i].za;
        memcpy(v[0].rgba, red, 4);
        memcpy(v[1].rgba, red, 4);
        rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
        rd_TestGs(RD_TEST_Z_GEQUAL);
        rd_ZWrite(0);
        v[0].z = v[1].z = rg[i].zb;
        memcpy(v[0].rgba, rg[i].b, 4);
        memcpy(v[1].rgba, rg[i].b, 4);
        /* depth-tested 2D prims are WORLD-tagged in the game (CPU-projected
           strips, glows); the Z values are the UI's, above 2^24 */
        rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
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
    /* field 0 for three flips: the third frame closes with the half offset */
    setField(0);
    for (int i = 0; i < 3; i++) {
        drawScene(rows, RD_FILTER_LINEAR);
        tick();
    }
    CHECK(halfOf(rd__LastFrame(), 0) == 1, "half: the frame carried the half offset");
    uint32_t w, h;
    const uint8_t *s = readTarget(RD_TARGET_SCENE, &w, &h);
    if (s) {
        /* rows y and y+1 averaged (G = 4y within each 64-row period) */
        int bad = 0;
        for (int y = 0; y < H - 1; y++) {
            if (y % 64 == 63) {
                continue;
            }
            const int want = ((y % 64) * 4 + ((y + 1) % 64) * 4) / 2;
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
    current_layout_id = 55;
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
    static RdPipeKeyInt keys[512];
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    const uint32_t c = rd__PipelineCount();
    for (uint32_t i = 0; i < c; i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        if (k->fs == RD_FS_CAMERA_PROBE) {
            continue; /* test only */
        }
        int found = 0;
        for (uint32_t j = 0; j < n && j < 512; j++) {
            found |= rd__PipeKeyEqual(k, &keys[j]);
        }
        CHECK(found, "pipeline %u (blend %u fmt %u/%u z %u/%u) not in the enumerated set", i,
              k->gs.blend, k->colorFmt, k->depthFmt, k->gs.ztst, k->gs.zwrite);
    }
}

/* ------------------------------------------------------------------ main */

/* FBMSK's extent (R-POST): a FRAME.FBMSK left masked in list 10 (as
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
    rd__Walk(f, 0, &st, maskWalk, NULL);
}

static void checkMask(void)
{
    for (int aa = 1; aa >= 0; aa--) {
        tick(); /* opens the frame */
        dl_SetDLPriority(10);
        rd_ColorMask(0xFF000000u); /* the composite's FRAME PSMCT24 */
        rd_TextureOff();           /* what a list-10 draw after it would draw with */
        GlobalStageSetting.antiLevel0 = aa ? 0x40 : 0;
        tick(); /* closes it: anti-alias (list 10), reduction (list 12) */
        GlobalStageSetting.antiLevel0 = 0;
        const RdFrame *f = rd__LastFrame();
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

/* issue 11: Options > Effects > Screen softening off: the stage's levels
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
    const uint32_t aa0 = rd_Target(RD_TARGET_AA0).id, aa1 = rd_Target(RD_TARGET_AA1).id;
    const RdTexRec *t = st->ds.texEnabled ? rd__TexRec(st->tex) : NULL;
    const int samples = t && t->kind == RD_TEXKIND_TARGET && (t->target == aa0 || t->target == aa1);
    s_aaSprites += st->color == aa0 || st->color == aa1 || samples;
}

static int aaRecords(const RdFrame *f)
{
    s_aaSprites = 0;
    RdStateBlock st = f->startState;
    rd__Walk(f, 0, &st, aaWalk, NULL);
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
        const RdFrame *f = rd__LastFrame();
        if (!f) {
            CHECK(0, "softening: a frame");
            return;
        }
        const int n = aaRecords(f);
        CHECK(n == (on ? 4 : 0), "softening %s: %d anti-alias sprites (want %d)", on ? "on" : "off",
              n, on ? 4 : 0);
    }
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
    checkVu();
    checkZScale();
}

int main(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    const int device = rd_Init(512, 512, &st, NULL);
    if (!device && !rd__InitRecordOnly(512, 512)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    if (device) {
        printf("rd_gsbase_test: adapter %s\n", rhi_AdapterName());
    }
    boot();
    recordingChecks();
    if (!device) {
        rd_Shutdown();
        if (failures) {
            printf("rd_gsbase_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_gsbase_test: recording ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    makeScene();
    s_tex = rd_CreateTexture(W, H, s_img, RD_TEXA_80_80, "gsbase scene");
    s_noise = rd_CreateTexture(64, 64, s_noiseImg, RD_TEXA_80_80, "gsbase grain");
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
    RdTex rows = rd_CreateTexture(W, H, rowsImg, RD_TEXA_80_80, "gsbase rows");
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
    checkLetterbox();
    checkPipelines();
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_DestroyTexture(rows);
    rd_Shutdown();
    if (failures) {
        printf("rd_gsbase_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_gsbase_test: ok\n");
    return 0;
}
