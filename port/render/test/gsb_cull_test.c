/* gsb_cull_test.c: the game's cull against the cameras of the blended
 * pictures (GsBase.c gsb_ClipBox) and the windows that scale with the
 * picture's width.
 *
 * GsBase.c, GifPacket.c, DisplayList.c and DmaPacket.c compiled as the
 * window build compiles them (ICO_HOST, ICO_RD), with the display options
 * (port/game/video_options.c) and the rest of the game stubbed below, as
 * rd_present_test has them; rd records only (no device is needed).  The
 * cameras are set as the game sets them (gsb_SetVSMatrix, the view at
 * matrixptr+0x80, gsb_MakeCommonMatrix) and a tick ends with the game's
 * flip (gsb_UpdateGSSystem).  The NTSC frame (512 x 448), whose cull has
 * the least room above and below the picture.
 *
 *   moving   at the native widths W = 512 (4:3), 1376 (32:9) and 2560 (the
 *            widest: aspect "auto" in a window wider than 60:9, clamped to
 *            ICO_ASPECT_MAX), the frame latched at the flip (GsBase.c
 *            gsFrame) and the cull's projections built from W / 2, pairs of
 *            cameras a tick apart, the eye moved up to 300 units in any
 *            direction and the view turned up to 30 degrees (yaw and
 *            pitch), as far as the presenter blends (rd_camera_blend.h
 *            RD_INTERP_CAMERA_MOVE, RD_INTERP_CAMERA_TURN); boxes around
 *            points the pictures at t = 0.25, 0.5 and 0.75 show (the turn
 *            slerped and the eye lerped, as rd_interp.c camSetup, computed
 *            here on their own), many of them at the picture's edges and
 *            corners: none is culled (gsb_ClipBox 0).  A step past half
 *            those limits culls nothing (0
 *            cameras): every box is kept, those outside every frustum too;
 *            within half of them three cameras cull.  The worst miss (how
 *            far inside the in-between picture the shown point of a culled
 *            box was, as a share of the half-width or half-height) is
 *            printed, 0 when there is none, with how many boxes only an
 *            in-between picture showed and how many lay outside the three
 *            cameras' own frustums (kept by the eye's sweep alone).  Boxes
 *            whose bounding sphere, grown by the eye's step, lies past one
 *            plane of each of the three cull frustums are culled; -1 (no
 *            per-triangle test) only for a box every corner of which is
 *            inside all three cull frustums.
 *   corner   the case three cameras alone miss: the eye moving 149 units
 *            along the view's +x and +y together, a box in the picture's
 *            -x, +y corner (left, and down on the screen) at t = 0.25, 120
 *            units out, outside the three cameras' frustums: kept by the
 *            eye's sweep.
 *   cut      a cut the presenter does not blend over: the eye moved 2000
 *            units and the view turned 90 degrees, or a hard cut the game
 *            signalled (ico_video_camera_cut) since the flip: one camera,
 *            every result the PS2's.  At the limits: a step of 149 units or
 *            a turn of 14 degrees three cameras, 151 to 299 units or 16 to
 *            29 degrees none (every box kept), 301 units or 31 degrees
 *            one.
 *   margin   a box every corner of which is past
 *            a plane by less than |w| / 65536 + 1 is kept (the PS2 dropped
 *            it), one past it by more is dropped; "every result the PS2's"
 *            above is the PS2's test with this margin.
 *   still    the camera the same matrix bitwise as the tick before: one
 *            camera, every result the PS2's single-camera test.
 *   first    a stage's first tick (gsb_InitGSSystem) and after
 *            gsb_ResetGSSystem: one camera.
 *   original the Original framerate (nothing blended): one camera, while
 *            the camera moves.
 *   rates    every framerate but Original (30, 60, 120, 144, uncapped, and
 *            the row changed from uncapped to 60 between ticks): a moving
 *            camera culls against the three cameras; why one camera was
 *            used (gsb_HostCullOneWhy, F12's cull line): the Original
 *            framerate, a step past the limits, the camera standing still
 *            (a second box too), a signalled cut, no earlier camera, a
 *            part locked to the camera.
 *   locked   a part locked to the camera (gsb_HostCullCameraLocked, node
 *            flag 2): one camera.
 *   dots     the water dots' window (gsb_HostDotVisible) at W = 512, 1376,
 *            2560: (W / 2 + 144) pixels across and (224 + 144) up and down
 *            about the centre, 400 across at 4:3 (the PS2's), its edges
 *            in, one 1/16 pixel past them out, the widest inside 16 bits;
 *            a dot only the tick before's camera saw, its GS position
 *            outside the window but inside 16 bits, kept while the
 *            camera moves, and not past 16 bits.
 *   lines    lineManager.c's x clip (gsb_HostLineHalfWidth): W / 2 pixels,
 *            exactly 256 at 4:3, with the Original framerate; times 1.12
 *            (1 + GSB_CULL_MARGIN, the cull's) while pictures are blended;
 *            the same after a reflection's gsb_SetVSMatrix(230, 230) (the
 *            frame's width, not vsWidth).
 *   bank     a step past half the limits keeps every box
 *            only while the packet bank is at most half of the EE's 512 KB
 *            full (DmaPacket.c dpk_HostFill), else three cameras cull;
 *            dpk_HostRoom refuses an object within 64 KB of the pad's end.
 *   shadows  Shadow.c's edge clip (gsb_HostShadowClipX): W pixels, at most
 *            2046 (GSB_SHADOW_CLIP_MAX): a clipped end
 *            at 2048 -+ the clip packs into Shadow.c's range test (0x11 ..
 *            0xFFEF sixteenths); 2047 does not.
 *   stage    (--stage DISC, ctest gsb_cull_stage; 77 without the disc image)
 *            st18a's parts from the disc, culled as the game culls
 *            them, at the camera of the reporter's dump i76 (the parts it
 *            drops are the ones the dump's log names) and over 768 camera
 *            steps about it at 16:9 and 48:9 (orbits, the eye alone, the
 *            view alone, the zoom easing): every part a picture between the
 *            two ticks shows (t = 1/16 .. 15/16, rdcb_blend_view) is
 *            registered by this tick; culled with one camera instead, some
 *            are left to the tick before's draws (the sweep's control).
 *            The triangles the region rule drops for a vertex behind the
 *            eye at the dump's camera are counted (none reach its picture).
 *            A capture at the place (frame 22117, 16:9,
 *            framerate 60, the camera standing still): one camera, the
 *            log's drops, and no frame pixel whose nearest st18a surface
 *            is a dropped part's (checkStageCapture).  Two captures
 *            there while the camera turned (frames
 *            4219 and 4481; the camera stands at one eye and only turns):
 *            at each tick and at the picture between the ticks the second
 *            F12's PNG shows, every pixel's nearest surface is a drawn
 *            part's and nothing is dropped behind the eye; over the turn
 *            between the two views every part a picture shows is
 *            registered (checkStageSlit).  The parts' strips end at a
 *            count under 3, as the game reads them (checkStageLoader:
 *            14,382 triangles in frame 4219's drawn st18a_p1 parts, as
 *            its meshes).
 *            --stage DISC --eye (not a ctest, about a minute) also
 *            rasterises every picture between the ticks with and without
 *            the triangles the rule drops for a vertex behind the
 *            in-between eye, and prints the holes (eyePicture).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eeregs.h>
#include <libvu0.h>
#include "rd_camera_blend.h"
#include "rd_internal.h"
#include "df_pack.h"
#include "vfs.h"
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

/* GsBase.c's June 2001 demo arm of gsb_MakeCommonMatrix (and the 2001 builds' pause test) name
   these; the European path this test runs never reaches them */
int graphics_ready;

void GetSunWorldPos(float *pos)
{
    pos[0] = pos[1] = pos[2] = 0.0f;
    pos[3] = 1.0f;
}

void _SetCameraMatrix(void *dst, void *pos, void *dir, void *up)
{
    (void)dst;
    (void)pos;
    (void)dir;
    (void)up;
}

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

/* port/game/photo_view.c: photo mode's Deja vu veil (not linked here; these
   frames never open photo mode) */
void ico_photo_view_veil(void) {}

void stage_SetLoopFlag(int g, int f)
{
    (void)g;
    (void)f;
}

void UpdateHandCameraLimitP(void) {}

void UpdateHandCameraLimitV(void) {}

void UpdateZoomMaxVallInDemo(void) {}

/* ------------------------------------------------------------ cameras */

#define PI 3.14159265358979323846
#define DEG (PI / 180.0)

/* the near plane gsb_SetVSMatrix puts at view z 2 (vsParam[7]); a point
   the test calls drawn is at least this far in front of the eye */
#define DRAWN_NEAR 2.5

/* xorshift: the same cameras and boxes every run */
static uint32_t s_rng = 0x2545F491u;

static double rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (double)s_rng / 4294967296.0;
}

static double rndIn(double a, double b)
{
    return a + (b - a) * rnd();
}

/* a camera: its turn (a unit quaternion x, y, z, w, camera to world) and
   its eye; it looks along its own +z */
typedef struct Cam {
    double q[4];
    double e[3];
} Cam;

static void qmul(const double *a, const double *b, double *o)
{
    const double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const double y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const double z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x;
    o[1] = y;
    o[2] = z;
    o[3] = w;
}

/* yaw about the world's y, then pitch about the camera's x (radians) */
static void camTurn(Cam *c, double yaw, double pitch)
{
    const double qy[4] = {0.0, sin(yaw / 2), 0.0, cos(yaw / 2)};
    const double qx[4] = {sin(pitch / 2), 0.0, 0.0, cos(pitch / 2)};
    qmul(qy, qx, c->q);
}

/* r[row][col], camera to world: column 2 is where the camera looks */
static void rotOf(const double *q, double r[3][3])
{
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    r[0][0] = 1 - 2 * (y * y + z * z);
    r[0][1] = 2 * (x * y - z * w);
    r[0][2] = 2 * (x * z + y * w);
    r[1][0] = 2 * (x * y + z * w);
    r[1][1] = 1 - 2 * (x * x + z * z);
    r[1][2] = 2 * (y * z - x * w);
    r[2][0] = 2 * (x * z - y * w);
    r[2][1] = 2 * (y * z + x * w);
    r[2][2] = 1 - 2 * (x * x + y * y);
}

static double qdot(const double *a, const double *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

/* the turn between two cameras, degrees */
static double turnDeg(const Cam *a, const Cam *b)
{
    const double d = fabs(qdot(a->q, b->q));
    return 2.0 * acos(d > 1.0 ? 1.0 : d) / DEG;
}

/* the picture at t between a and b, as the presenter draws it: the turn
   slerped the short way, the eye lerped (rd_interp.c camSetup) */
static Cam camAt(const Cam *a, const Cam *b, double t)
{
    Cam c;
    double bq[4] = {b->q[0], b->q[1], b->q[2], b->q[3]};
    double d = qdot(a->q, bq);
    double wa = 1.0 - t, wb = t, n = 0.0;
    if (d < 0.0) {
        d = -d;
        for (int i = 0; i < 4; i++) {
            bq[i] = -bq[i];
        }
    }
    if (d < 0.999999) {
        const double th = acos(d);
        wa = sin((1.0 - t) * th) / sin(th);
        wb = sin(t * th) / sin(th);
    }
    for (int i = 0; i < 4; i++) {
        c.q[i] = wa * a->q[i] + wb * bq[i];
        n += c.q[i] * c.q[i];
    }
    for (int i = 0; i < 4; i++) {
        c.q[i] /= sqrt(n);
    }
    for (int i = 0; i < 3; i++) {
        c.e[i] = (1.0 - t) * a->e[i] + t * b->e[i];
    }
    return c;
}

/* world to view, column-major (v[col * 4 + row]) */
static void viewOf(const Cam *c, double *v)
{
    double r[3][3];
    rotOf(c->q, r);
    memset(v, 0, 16 * sizeof(double));
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            v[col * 4 + row] = r[col][row];
        }
        v[12 + row] = -(r[0][row] * c->e[0] + r[1][row] * c->e[1] + r[2][row] * c->e[2]);
    }
    v[15] = 1.0;
}

/* o = m (p, 1) */
static void xform(const double *m, const double *p, double *o)
{
    for (int r = 0; r < 4; r++) {
        o[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
    }
}

/* the game's camera: gsb_SetVSMatrix, the view at +0x80, then
   gsb_MakeCommonMatrix (camera-root.c's order) */
static void setCamera(const Cam *c)
{
    double v[16];
    float f[16];
    viewOf(c, v);
    for (int i = 0; i < 16; i++) {
        f[i] = (float)v[i];
    }
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 512.0f);
    memcpy(matrixptr + 0x80, f, sizeof(f));
    gsb_MakeCommonMatrix();
}

/* one scheduler frame boundary: gsb_PostEffect, then the flip */
static void tick(void)
{
    gsb_SyncGSSystem();
    gsb_UpdateGSSystem(0);
}

/* the screen matrix (+0xC0) and the cull's projection (+0x240) in force */
static double s_screen[16], s_cull[16];

static void readProjections(void)
{
    for (int i = 0; i < 16; i++) {
        s_screen[i] = ((const float *)(matrixptr + 0xC0))[i];
        s_cull[i] = ((const float *)(matrixptr + 0x240))[i];
    }
}

/* the picture's half-width and half-height in GS pixels: the native
   frame's (gsFrame.w / 2) */
static double halfW(void)
{
    return (double)(gsFrame.w / 2);
}

static double halfH(void)
{
    return (double)(ScreenHeight / 2);
}

/* how far inside the picture camera c draws the world point p, as a share
   of the half-width or half-height, the smaller; negative outside it or
   too close to the eye */
static double drawnDepth(const Cam *c, const double *p)
{
    double v[16], q[4], g[4];
    viewOf(c, v);
    xform(v, p, q);
    if (q[2] < DRAWN_NEAR) {
        return -1.0;
    }
    xform(s_screen, q, g);
    const double gx = fabs(g[0] / g[3] - 2048.0) / halfW();
    const double gy = fabs(g[1] / g[3] - 2048.0) / halfH();
    return 1.0 - (gx > gy ? gx : gy);
}

/* 1 when the world point p is inside camera c's cull frustum (+0x240 x the
   view: |x|, |y| <= w, z >= -w) within a relative tolerance tol */
static int inCull(const Cam *c, const double *p, double tol)
{
    double v[16], q[4], k[4];
    viewOf(c, v);
    xform(v, p, q);
    xform(s_cull, q, k);
    const double w = fabs(k[3]) * (1.0 + tol);
    return k[3] > 0.0 && fabs(k[0]) <= w && fabs(k[1]) <= w && k[2] >= -w;
}

/* 1 when a ball (centre p, radius r) is wholly past one plane of camera
   c's cull frustum: behind its near plane, or wholly in front of the eye
   and past a side plane (gsb_ClipBox compares x and y with |w|, which
   behind the eye is not the side plane's other half) */
static int ballOutside(const Cam *c, const double *p, double r)
{
    double v[16], q[4];
    viewOf(c, v);
    xform(v, p, q);
    /* view space: |x| <= tx z, |y| <= ty z, z >= the near plane */
    const double tx = 1.0 / s_cull[0], ty = 1.0 / s_cull[5];
    const double nearZ = -s_cull[14] / (s_cull[10] + 1.0);
    const double nx = sqrt(1.0 + tx * tx), ny = sqrt(1.0 + ty * ty);
    /* gsb_ClipBox drops only past the margin |w| GSB_DROP_REL +
       GSB_DROP_ABS in clip units (GsBase.c gsbHostDropFlags; |w| at most
       the ball's farthest depth): at most that many view units past a side
       plane (tx / nx < 1) and half of it past the near plane (1 + s10 > 2),
       and the near plane's gap behind the eye (-s14) must exceed it */
    const double clipM = (fabs(q[2]) + r) * GSB_DROP_REL + GSB_DROP_ABS;
    const double m = clipM * fmax(1.0, fmax(tx / nx, ty / ny));
    if (clipM >= -s_cull[14]) {
        return 0;
    }
    r += m;
    if (nearZ - q[2] > r) {
        return 1;
    }
    return q[2] - r > 0.0 && ((q[0] - tx * q[2]) / nx > r || (-q[0] - tx * q[2]) / nx > r ||
                              (q[1] - ty * q[2]) / ny > r || (-q[1] - ty * q[2]) / ny > r);
}

/* a world box (centre, half-size) as gsb_ClipBox's eight corners */
typedef struct Box {
    float v[8][4];
} Box;

static Box boxOf(const double *c, double h)
{
    Box b;
    for (int i = 0; i < 8; i++) {
        b.v[i][0] = (float)(c[0] + ((i & 1) ? h : -h));
        b.v[i][1] = (float)(c[1] + ((i & 2) ? h : -h));
        b.v[i][2] = (float)(c[2] + ((i & 4) ? h : -h));
        b.v[i][3] = 1.0f;
    }
    return b;
}

static void cornerOf(const Box *b, int i, double *p)
{
    p[0] = b->v[i][0];
    p[1] = b->v[i][1];
    p[2] = b->v[i][2];
}

/* gsb_ClipBox on a world box: the current matrix +0x280 x a unit model
   (RegistPacket.c's +0x300) */
static int clipBox(Box *b)
{
    memcpy(ico_current_matrix, matrixptr + 0x280, sizeof(ico_current_matrix));
    gsb_HostCullCameraLocked(0);
    return gsb_ClipBox(&b->v[0][0]);
}

/* the PS2's test (gsb_ClipBox's own code) on the current matrix, with the
   port's drop margin (a box is dropped
   only when every corner is past a plane by |w| GSB_DROP_REL +
   GSB_DROP_ABS, GsBase.h): the reference
   for the cases with one camera */
static int refClip(const Box *b)
{
    int all = 0x3F, any = 0, nearC = 0;
    for (int i = 0; i < 8; i++) {
        float r[4];
        ico_apply_matrix_w1(r, (const float (*)[4])ico_current_matrix, b->v[i]);
        const float w = fabsf(r[3]);
        const float wd = w * (1.0f + GSB_DROP_REL) + GSB_DROP_ABS;
        int f = 0, d = 0;
        f |= (r[0] > w) << 0;
        f |= (r[0] < -w) << 1;
        f |= (r[1] > w) << 2;
        f |= (r[1] < -w) << 3;
        f |= (r[2] > w) << 4;
        f |= (r[2] < -w) << 5;
        d |= (r[0] > wd) << 0;
        d |= (r[0] < -wd) << 1;
        d |= (r[1] > wd) << 2;
        d |= (r[1] < -wd) << 3;
        d |= (r[2] > wd) << 4;
        d |= (r[2] < -wd) << 5;
        all &= d;
        any |= f;
        nearC |= r[2] < -0.99f;
    }
    if (all & 0x2F) {
        return 0;
    }
    if ((any & 0x2F) == 0) {
        return -1;
    }
    return nearC ? 2 : 1;
}

/* the display options: framerate (ICO_FRAMERATE_*) and the picture's width
   through aspect "auto" and a window of winW x winH; a flip latches the
   native frame for them (gsFrame), as the game's next tick sees it */
static void setOptions(int framerate, int winW, int winH)
{
    IcoVideoOptions o;
    ico_video_defaults(&o);
    o.framerate = framerate;
    o.aspect = ICO_ASPECT_AUTO;
    ico_video_set(&o);
    ico_video_set_window(winW, winH);
    tick();
}

/* a random camera, its pitch within 20 degrees of level */
static Cam randomCam(void)
{
    Cam c;
    camTurn(&c, rndIn(-PI, PI), rndIn(-20.0, 20.0) * DEG);
    for (int i = 0; i < 3; i++) {
        c.e[i] = rndIn(-1000.0, 1000.0);
    }
    return c;
}

/* the camera a tick after a: the eye moved up to 300 units, the view
   turned up to 30 degrees; kind 0 both, 1 the move only, 2 the turn only */
static Cam nextCam(const Cam *a, double yaw0, double pitch0, int kind)
{
    Cam b = *a;
    if (kind != 1) {
        do {
            camTurn(&b, yaw0 + rndIn(-30.0, 30.0) * DEG, pitch0 + rndIn(-20.0, 20.0) * DEG);
        } while (turnDeg(a, &b) > 30.0);
    }
    if (kind != 2) {
        double d[3], n;
        do {
            for (int i = 0; i < 3; i++) {
                d[i] = rndIn(-1.0, 1.0);
            }
            n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        } while (n < 0.1 || n > 1.0);
        const double s = rndIn(0.0, 300.0) / n;
        for (int i = 0; i < 3; i++) {
            b.e[i] = a->e[i] + d[i] * s;
        }
    }
    return b;
}

static double stepOf(const Cam *a, const Cam *b)
{
    const double x = b->e[0] - a->e[0], y = b->e[1] - a->e[1], z = b->e[2] - a->e[2];
    return sqrt(x * x + y * y + z * z);
}

/* the camera a tick after a, within half the limits (three cameras): the
   view turned 10 degrees from (yaw0, pitch0), the eye 100 units on */
static Cam smallStep(const Cam *a, double yaw0, double pitch0)
{
    Cam b = *a;
    camTurn(&b, yaw0 + 10.0 * DEG, pitch0);
    b.e[0] += 60.0;
    b.e[1] += 20.0;
    b.e[2] += 77.0;
    return b;
}

/* ------------------------------------------------------------ moving */

#define PAIRS 240
#define BOXES 120
#define OUTSIDE 40

static void checkMoving(const char *what, int winW, int winH, int wantW)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, winW, winH);
    const int fw = gsFrame.w;
    CHECK(fw == wantW, "moving %s: the native width %d, want %d", what, fw, wantW);
    int tested = 0, missed = 0, between = 0, sweepNeeded = 0, outside = 0, outsideKept = 0;
    int outsideSpan = 0, outsideSpanCulled = 0;
    int minusBad = 0, threeViews = 0, keepViews = 0, oneViews = 0;
    double worst = 0.0;

    for (int pair = 0; pair < PAIRS; pair++) {
        const double yaw0 = rndIn(-PI, PI), pitch0 = rndIn(-20.0, 20.0) * DEG;
        Cam a;
        camTurn(&a, yaw0, pitch0);
        for (int i = 0; i < 3; i++) {
            a.e[i] = rndIn(-1000.0, 1000.0);
        }
        const Cam b = nextCam(&a, yaw0, pitch0, pair % 3);
        const Cam mid = camAt(&a, &b, 0.5);
        setCamera(&a);
        tick();
        setCamera(&b);
        readProjections();
        const double tdx = halfW() / s_screen[0], tdy = halfH() / s_screen[5];

        for (int n = 0; n < BOXES; n++) {
            static const double ts[3] = {0.25, 0.5, 0.75};
            const Cam c = camAt(&a, &b, ts[n % 3]);
            double r[3][3];
            rotOf(c.q, r);
            /* a point the picture at t shows: half of them at an edge or
               a corner */
            const double z = exp(rndIn(log(4.0), log(4000.0)));
            double fx = rndIn(-0.999, 0.999), fy = rndIn(-0.999, 0.999);
            if (n & 1) {
                fx = (fx < 0.0 ? -1.0 : 1.0) * rndIn(0.9, 0.999);
            }
            if (n & 2) {
                fy = (fy < 0.0 ? -1.0 : 1.0) * rndIn(0.9, 0.999);
            }
            const double vv[3] = {fx * tdx * z, fy * tdy * z, z};
            double p[3], centre[3];
            for (int i = 0; i < 3; i++) {
                p[i] = c.e[i] + r[i][0] * vv[0] + r[i][1] * vv[1] + r[i][2] * vv[2];
            }
            const double h = rndIn(0.5, 40.0);
            for (int i = 0; i < 3; i++) {
                centre[i] = p[i] + rndIn(-0.9, 0.9) * h;
            }
            Box bx = boxOf(centre, h);
            if (drawnDepth(&c, p) < 0.0) {
                continue; /* a corner case of the float box: skip it */
            }
            const int res = clipBox(&bx);
            threeViews += gsb_HostCullViewsUsed() == 3;
            keepViews += gsb_HostCullViewsUsed() == 0;
            oneViews += gsb_HostCullViewsUsed() == 1;
            tested++;
            /* shown at the ends? needed the eye's sweep? */
            int ends = drawnDepth(&a, p) >= 0.0 || drawnDepth(&b, p) >= 0.0;
            int unswept = 0;
            for (int i = 0; i < 8; i++) {
                double q[3];
                cornerOf(&bx, i, q);
                ends |= drawnDepth(&a, q) >= 0.0 || drawnDepth(&b, q) >= 0.0;
                unswept |= inCull(&a, q, 0.0) || inCull(&mid, q, 0.0) || inCull(&b, q, 0.0);
            }
            between += !ends;
            sweepNeeded += !unswept;
            if (res == 0) {
                const double g = drawnDepth(&c, p);
                missed++;
                if (g > worst) {
                    worst = g;
                }
                if (missed <= 5) {
                    printf("  %s: missed a box at t %.2f, %.0f units out, %.3f inside the "
                           "picture (turn %.1f, step %.0f)\n",
                           what, ts[n % 3], z, g, turnDeg(&a, &b), stepOf(&a, &b));
                }
            }
            if (res == -1) {
                for (int i = 0; i < 8; i++) {
                    double q[3];
                    cornerOf(&bx, i, q);
                    if (!inCull(&a, q, 1e-4) || !inCull(&mid, q, 1e-4) || !inCull(&b, q, 1e-4)) {
                        minusBad++;
                        break;
                    }
                }
            }
        }
        /* boxes past one plane of each of the three cull frustums, by more
           than the eye's step */
        const double step = stepOf(&a, &b);
        for (int n = 0; n < OUTSIDE; n++) {
            double centre[3];
            for (int i = 0; i < 3; i++) {
                centre[i] = 0.5 * (a.e[i] + b.e[i]) + rndIn(-2500.0, 2500.0);
            }
            const double h = rndIn(0.5, 40.0);
            const double rr = h * sqrt(3.0) + step + 1.0;
            if (!ballOutside(&a, centre, rr) || !ballOutside(&mid, centre, rr) ||
                !ballOutside(&b, centre, rr)) {
                continue;
            }
            Box bx = boxOf(centre, h);
            const int res = clipBox(&bx);
            if (gsb_HostCullViewsUsed() == 0) {
                /* a step past half the limits: every box kept (item 5) */
                outsideSpan++;
                outsideSpanCulled += res == 0;
                continue;
            }
            outside++;
            outsideKept += res != 0;
        }
    }
    printf("gsb_cull_test: %s (W %d): %d boxes the in-between pictures show, %d of them "
           "only those, %d outside the three cameras' frustums (kept by the eye's sweep); "
           "culled %d, the worst %.4f of the half-width inside\n",
           what, fw, tested, between, sweepNeeded, missed, worst);
    CHECK(missed == 0, "moving %s: %d boxes an in-between picture shows culled (worst %.4f)", what,
          missed, worst);
    CHECK(tested > PAIRS * BOXES / 2 && between > 0,
          "moving %s: %d boxes tested, %d only in between", what, tested, between);
    /* within half the limits three cameras, past them none (item 5) */
    CHECK(threeViews > 0 && keepViews > 0 && threeViews + keepViews + oneViews == tested,
          "moving %s: three cameras for %d, none (every box kept) for %d, one for %d of %d boxes",
          what, threeViews, keepViews, oneViews, tested);
    CHECK(outside > 0 && outsideKept == 0,
          "moving %s: %d of %d boxes outside every cull frustum kept", what, outsideKept, outside);
    CHECK(outsideSpan > 0 && outsideSpanCulled == 0,
          "moving %s: %d of %d boxes outside every cull frustum culled on a step past half the "
          "limits",
          what, outsideSpanCulled, outsideSpan);
    CHECK(minusBad == 0, "moving %s: %d boxes -1 with a corner outside a cull frustum", what,
          minusBad);
}

/* the eye moving 149 units (just inside half the limits, where three
   cameras cull) along the view's +x and +y together, a box in the
   picture's -x, +y corner at t = 0.25, 120 units out (the step's quarter,
   26 units across and up, against the cull's 12 % margin there): outside
   the three
   cameras' frustums (the tick before's misses it above or below, the
   half-way and this tick's at the side), inside the swept ones */
static void checkCorner(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 640, 480);
    Cam a, b;
    camTurn(&a, 0.0, 0.0);
    a.e[0] = a.e[1] = a.e[2] = 0.0;
    b = a;
    b.e[0] = b.e[1] = 149.0 / sqrt(2.0);
    const Cam c = camAt(&a, &b, 0.25), mid = camAt(&a, &b, 0.5);
    setCamera(&a);
    tick();
    setCamera(&b);
    readProjections();
    const double tdx = halfW() / s_screen[0], tdy = halfH() / s_screen[5];
    const double centre[3] = {c.e[0] - 0.98 * tdx * 120.0, c.e[1] + 0.98 * tdy * 120.0, 120.0};
    Box bx = boxOf(centre, 1.0);
    int seen = 0;
    for (int i = 0; i < 8; i++) {
        double q[3];
        cornerOf(&bx, i, q);
        seen |= inCull(&a, q, 0.0) || inCull(&mid, q, 0.0) || inCull(&b, q, 0.0);
    }
    CHECK(drawnDepth(&c, centre) > 0.0, "corner: the picture at t 0.25 shows the box");
    CHECK(!seen, "corner: the three cameras' frustums miss the box (the case the sweep is for)");
    const int res = clipBox(&bx);
    CHECK(res != 0 && gsb_HostCullViewsUsed() == 3, "corner: kept (%d, %d cameras)", res,
          gsb_HostCullViewsUsed());
}

/* ------------------------------------------------- one camera, the PS2's */

/* random boxes around the camera: gsb_ClipBox the PS2's test, one camera */
static void checkOneCamera(const char *what, const Cam *c)
{
    int bad = 0, views = 0;
    for (int n = 0; n < 400; n++) {
        double centre[3];
        for (int i = 0; i < 3; i++) {
            centre[i] = c->e[i] + rndIn(-1500.0, 1500.0);
        }
        Box bx = boxOf(centre, rndIn(0.5, 200.0));
        const int res = clipBox(&bx);
        views |= gsb_HostCullViewsUsed() != 1;
        bad += res != refClip(&bx);
    }
    CHECK(bad == 0 && views == 0, "%s: %d results not the PS2's test, %s", what, bad,
          views ? "more than one camera" : "one camera");
}

/* the cameras gsb_ClipBox uses for a box ahead of camera c */
static int viewsAhead(const Cam *c)
{
    double r[3][3], p[3];
    rotOf(c->q, r);
    for (int i = 0; i < 3; i++) {
        p[i] = c->e[i] + r[i][2] * 500.0;
    }
    Box bx = boxOf(p, 5.0);
    clipBox(&bx);
    return gsb_HostCullViewsUsed();
}

/* the cameras for a tick from a to b (the eye moved by step along the
   view's x, the view turned by yaw degrees) */
static int viewsAcross(double step, double yaw)
{
    Cam a, b;
    camTurn(&a, 0.4, 0.05);
    a.e[0] = 120.0;
    a.e[1] = 40.0;
    a.e[2] = -300.0;
    b = a;
    camTurn(&b, 0.4 + yaw * DEG, 0.05);
    {
        double r[3][3];
        rotOf(a.q, r);
        for (int i = 0; i < 3; i++) {
            b.e[i] = a.e[i] + r[i][0] * step;
        }
    }
    setCamera(&a);
    tick();
    setCamera(&b);
    return viewsAhead(&b);
}

static void checkCut(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 3200, 900);
    /* a cutscene's cut: the eye 2000 units on, the view turned 90 degrees */
    {
        Cam a, b;
        camTurn(&a, 0.2, 0.0);
        a.e[0] = a.e[1] = a.e[2] = 0.0;
        b = a;
        camTurn(&b, 0.2 + 90.0 * DEG, 0.0);
        b.e[0] = 2000.0;
        setCamera(&a);
        tick();
        setCamera(&b);
        checkOneCamera("a cut (2000 units, 90 degrees)", &b);
    }
    /* the limits: RD_INTERP_CAMERA_MOVE 300 units, RD_INTERP_CAMERA_TURN 30
       degrees (a cut past them: one camera); past half of them nothing is
       culled (0 cameras, every box kept) */
    CHECK(viewsAcross(149.0, 0.0) == 3, "cut: a step of 149 units culls against 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(151.0, 0.0) == 0, "cut: a step of 151 units culls nothing (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(299.0, 0.0) == 0, "cut: a step of 299 units culls nothing (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(301.0, 0.0) == 1, "cut: a step of 301 units culls against 1 camera (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(0.0, 14.0) == 3, "cut: a turn of 14 degrees culls against 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(0.0, 16.0) == 0, "cut: a turn of 16 degrees culls nothing (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(0.0, 29.0) == 0, "cut: a turn of 29 degrees culls nothing (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(0.0, 31.0) == 1, "cut: a turn of 31 degrees culls against 1 camera (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(149.0, 14.0) == 3, "cut: both inside half the limits: 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(299.0, 29.0) == 0, "cut: both inside the limits, past half: none (%d)",
          gsb_HostCullViewsUsed());
    /* a hard cut the game signalled: one camera until the next flip */
    {
        Cam a, b;
        camTurn(&a, -0.3, 0.0);
        a.e[0] = a.e[1] = a.e[2] = 0.0;
        b = smallStep(&a, -0.3, 0.0);
        setCamera(&a);
        tick();
        setCamera(&b);
        CHECK(viewsAhead(&b) == 3, "cut: a step inside the limits: 3 cameras (%d)",
              gsb_HostCullViewsUsed());
        ico_video_camera_cut();
        checkOneCamera("a signalled cut", &b);
        tick();
        setCamera(&a);
        CHECK(viewsAhead(&a) == 3, "cut: the tick after a signalled cut: 3 cameras again (%d)",
              gsb_HostCullViewsUsed());
    }
}

static void checkStill(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 3200, 900);
    const Cam a = randomCam();
    setCamera(&a);
    tick();
    setCamera(&a);
    checkOneCamera("still", &a);
}

static void checkFirst(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 3200, 900);
    Cam a = randomCam();
    double yaw = 0.3, pitch = 0.1;
    camTurn(&a, yaw, pitch);
    /* a stage's first tick */
    gsb_InitGSSystem();
    const Cam b = smallStep(&a, yaw, pitch);
    setCamera(&b);
    checkOneCamera("first tick", &b);
    /* the next tick has the tick before's camera */
    tick();
    setCamera(&a);
    Box bx;
    {
        double r[3][3], p[3];
        rotOf(a.q, r);
        for (int i = 0; i < 3; i++) {
            p[i] = a.e[i] + r[i][2] * 500.0;
        }
        bx = boxOf(p, 5.0);
    }
    clipBox(&bx);
    CHECK(gsb_HostCullViewsUsed() == 3, "first tick: the second tick culls against 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    /* a reset between stages forgets it */
    gsb_ResetGSSystem();
    setCamera(&b);
    checkOneCamera("after a reset", &b);
}

static void checkOriginal(void)
{
    setOptions(ICO_FRAMERATE_ORIGINAL, 3200, 900);
    Cam a = randomCam();
    camTurn(&a, 1.0, 0.0);
    const Cam b = nextCam(&a, 1.0, 0.0, 0);
    setCamera(&a);
    tick();
    setCamera(&b);
    checkOneCamera("Original framerate", &b);
}

/* A capture at the st18a place (frame 22117, its cull
   line without "/5"): the cameras follow "the pictures are blended between
   ticks" (video_options.c ico_video_interpolate, the window's rule
   window_host.c rs->interpolate: every framerate but Original), never the
   framerate row's value: a cap of 30, 60, 120 or 144 presents per second
   culls a moving camera's step against the three cameras as uncapped does,
   from the tick after the row changed as on any tick; Original one.  And
   gsb_HostCullOneWhy names why one camera was used, for F12's line. */
static void checkFramerates(void)
{
    static const int kRates[] = {30, 60, 120, 144, ICO_FRAMERATE_UNCAPPED};
    for (size_t i = 0; i < sizeof(kRates) / sizeof(kRates[0]); i++) {
        setOptions(kRates[i], 3840, 2160);
        CHECK(ico_video_interpolate(), "framerate %d: pictures blended", kRates[i]);
        const int views = viewsAcross(100.0, 5.0);
        CHECK(views == 3 && gsb_HostCullOneWhy() == GSB_CULL_ONE_NOT,
              "framerate %d: a moving camera culls against 3 cameras (%d, why %d)", kRates[i],
              views, gsb_HostCullOneWhy());
    }
    /* the row changed from uncapped to 60 between two ticks */
    setOptions(ICO_FRAMERATE_UNCAPPED, 3840, 2160);
    {
        IcoVideoOptions o;
        ico_video_get(&o);
        o.framerate = 60;
        ico_video_set(&o);
    }
    CHECK(viewsAcross(100.0, 5.0) == 3, "framerate uncapped to 60: 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    setOptions(ICO_FRAMERATE_ORIGINAL, 3840, 2160);
    CHECK(viewsAcross(100.0, 5.0) == 1 && gsb_HostCullOneWhy() == GSB_CULL_ONE_ORIGINAL,
          "framerate Original: one camera, why the Original framerate (%d, why %d)",
          gsb_HostCullViewsUsed(), gsb_HostCullOneWhy());

    /* why one camera, at 60 */
    setOptions(60, 3840, 2160);
    CHECK(viewsAcross(301.0, 0.0) == 1 && gsb_HostCullOneWhy() == GSB_CULL_ONE_JUMP,
          "why: a step past the limits (%d, why %d)", gsb_HostCullViewsUsed(),
          gsb_HostCullOneWhy());
    CHECK(viewsAcross(0.0, 0.0) == 1 && gsb_HostCullOneWhy() == GSB_CULL_ONE_STILL,
          "why: the camera standing still (%d, why %d)", gsb_HostCullViewsUsed(),
          gsb_HostCullOneWhy());
    /* the cached cameras keep their why (a second box under the same
       +0x280) */
    {
        Cam a;
        camTurn(&a, 0.4, 0.05);
        a.e[0] = 120.0;
        a.e[1] = 40.0;
        a.e[2] = -300.0;
        CHECK(viewsAhead(&a) == 1 && gsb_HostCullOneWhy() == GSB_CULL_ONE_STILL,
              "why: the camera standing still, a second box (%d, why %d)", gsb_HostCullViewsUsed(),
              gsb_HostCullOneWhy());
        ico_video_camera_cut();
        CHECK(viewsAhead(&a) == 1 && gsb_HostCullOneWhy() == GSB_CULL_ONE_CUT,
              "why: a signalled cut (%d, why %d)", gsb_HostCullViewsUsed(), gsb_HostCullOneWhy());
        gsb_ResetGSSystem();
        setCamera(&a);
        CHECK(viewsAhead(&a) == 1 && gsb_HostCullOneWhy() == GSB_CULL_ONE_FIRST,
              "why: no earlier camera (%d, why %d)", gsb_HostCullViewsUsed(), gsb_HostCullOneWhy());
        tick();
    }
}

static void checkLocked(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 640, 480);
    Cam a = randomCam();
    camTurn(&a, -0.7, 0.05);
    const Cam b = smallStep(&a, -0.7, 0.05);
    setCamera(&a);
    tick();
    setCamera(&b);
    double r[3][3], p[3];
    rotOf(b.q, r);
    for (int i = 0; i < 3; i++) {
        p[i] = b.e[i] + r[i][2] * 300.0;
    }
    Box bx = boxOf(p, 10.0);
    memcpy(ico_current_matrix, matrixptr + 0x280, sizeof(ico_current_matrix));
    gsb_HostCullCameraLocked(1);
    const int locked = gsb_ClipBox(&bx.v[0][0]);
    const int lockedViews = gsb_HostCullViewsUsed();
    const int lockedWhy = gsb_HostCullOneWhy();
    const int ref = refClip(&bx);
    gsb_HostCullCameraLocked(0);
    gsb_ClipBox(&bx.v[0][0]);
    CHECK(lockedViews == 1 && locked == ref && lockedWhy == GSB_CULL_ONE_LOCKED,
          "locked: one camera, the PS2's result (%d cameras, %d, %d, why %d)", lockedViews, locked,
          ref, lockedWhy);
    CHECK(gsb_HostCullViewsUsed() == 3, "locked: a world part again culls against 3 cameras (%d)",
          gsb_HostCullViewsUsed());
}

/* ------------------------------------------------------------- windows */

/* the dot window's half-width and half-height, 1/16 pixels */
static int dotHalfX(void)
{
    return (gsFrame.w / 2 + 144) * 16;
}

static int dotHalfY(void)
{
    return (int)(((float)(ScreenHeight / 2) + 144.0f) * 16.0f);
}

static void checkDotWindow(const char *what, int winW, int winH, int wantHx)
{
    /* the camera stands still: the window alone */
    setOptions(ICO_FRAMERATE_UNCAPPED, winW, winH);
    const Cam a = randomCam();
    setCamera(&a);
    tick();
    setCamera(&a);
    const float far[3] = {1e9f, 1e9f, 1e9f}; /* a world point nobody sees */
    const int hx = dotHalfX(), hy = dotHalfY();
    CHECK(wantHx < 0 || hx == wantHx, "dots %s: half-width %d, want %d", what, hx, wantHx);
    CHECK(32768 + hx <= 0xFFFF, "dots %s: the window inside 16 bits (%d)", what, 32768 + hx);
    const int in[4][2] = {
        {32768 + hx, 32768}, {32768 - hx, 32768}, {32768, 32768 + hy}, {32768, 32768 - hy}};
    const int out[4][2] = {{32768 + hx + 1, 32768},
                           {32768 - hx - 1, 32768},
                           {32768, 32768 + hy + 1},
                           {32768, 32768 - hy - 1}};
    for (int i = 0; i < 4; i++) {
        const int ipIn[4] = {in[i][0], in[i][1], 0, 0};
        const int ipOut[4] = {out[i][0], out[i][1], 0, 0};
        CHECK(gsb_HostDotVisible(ipIn, far) == 1, "dots %s: edge %d in (%d, %d)", what, i, in[i][0],
              in[i][1]);
        CHECK(gsb_HostDotVisible(ipOut, far) == 0, "dots %s: past edge %d out (%d, %d)", what, i,
              out[i][0], out[i][1]);
    }
}

/* a dot the tick before's camera saw at the side of its picture, which
   this tick's camera, turned away, puts outside the window */
static void checkDotEarlier(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 640, 480);
    Cam a;
    camTurn(&a, 0.0, 0.0);
    a.e[0] = a.e[1] = a.e[2] = 0.0;
    /* the dot 1000 units out, 0.55 of its distance to the left */
    double r[3][3], p[3];
    rotOf(a.q, r);
    for (int i = 0; i < 3; i++) {
        p[i] = a.e[i] + r[i][0] * -550.0 + r[i][2] * 1000.0;
    }
    const float pos[4] = {(float)p[0], (float)p[1], (float)p[2], 1.0f};
    int best[4] = {0, 0, 0, 0}, found = 0;
    for (int s = -1; s <= 1; s += 2) {
        Cam b = a;
        camTurn(&b, s * 28.0 * DEG, 0.0);
        setCamera(&a);
        tick();
        setCamera(&b);
        /* waterDot.c's projection: +0x100 x pos, divided by w, 12.4 */
        const float *m = (const float *)(matrixptr + 0x100);
        float v[4];
        for (int i = 0; i < 4; i++) {
            v[i] = m[i] * pos[0] + m[4 + i] * pos[1] + m[8 + i] * pos[2] + m[12 + i];
        }
        const int ip[4] = {(int)(v[0] / v[3] * 16.0f), (int)(v[1] / v[3] * 16.0f), 0, 0};
        if (v[3] > 0.0f && ip[0] >= 0 && ip[0] <= 0xFFFF && abs(ip[0] - 32768) > dotHalfX()) {
            memcpy(best, ip, sizeof(best));
            found = 1;
            CHECK(gsb_HostDotVisible(ip, pos) == 1,
                  "dots: a dot the tick before's camera saw kept (GS x %d)", ip[0]);
            const int past[4] = {-16, ip[1], 0, 0};
            CHECK(gsb_HostDotVisible(past, pos) == 0, "dots: not where its x does not fit 16 bits");
            break;
        }
    }
    CHECK(found, "dots: a turn that puts the dot outside the window (%d)", best[0]);
}

static void checkLines(const char *what, int framerate, int winW, int winH, float want)
{
    setOptions(framerate, winW, winH);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 512.0f);
    /* exact with the Original framerate; within float rounding of the
     * margin's product otherwise */
    const float tol = framerate == ICO_FRAMERATE_ORIGINAL ? 0.0f : 1e-4f * want;
    const float hw = gsb_HostLineHalfWidth();
    CHECK(fabsf(hw - want) <= tol && 2048.0f + hw <= 4095.0f, "lines %s: half-width %g, want %g",
          what, (double)hw, (double)want);
    gsb_SetVSMatrix(0xE6, 0xE6, 512.0f); /* puddle.c's reflection view */
    const float hr = gsb_HostLineHalfWidth();
    CHECK(fabsf(hr - want) <= tol, "lines %s: half-width %g under a 230 x 230 view, want %g", what,
          (double)hr, (double)want);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 512.0f);
}

static void checkShadows(const char *what, int winW, int winH, float want)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, winW, winH);
    const float x = gsb_HostShadowClipX();
    CHECK(fabsf(x - want) <= 1e-3f * want && 2048.0f + x < 4096.0f,
          "shadows %s: edge clip %g, want %g", what, (double)x, (double)want);
    /* a clipped end, as emitVolumeStrip packs it (ftoi4), passes its range
       test (volumeVertsOutOfRange: 0x11 .. 0xFFEF) */
    const int lo = (int)((2048.0f - x) * 16.0f), hi = (int)((2048.0f + x) * 16.0f);
    CHECK(lo >= 0x11 && hi <= 0xFFEF, "shadows %s: clipped ends at %d and %d sixteenths", what, lo,
          hi);
}

/* ------------------------------------------------------------ bank fill */

/* A step past half the limits keeps every box only while the
   packet bank is at most half of the EE's size full; past it the three
   cameras cull again.  And dpk_HostRoom refuses an object (before any
   write) once the bank is within its reserve of the host pad's end */
static void checkBankFill(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 640, 480);
    CHECK(viewsAcross(200.0, 0.0) == 0,
          "bank: a 200 unit step with an empty bank keeps every box "
          "(%d)",
          gsb_HostCullViewsUsed());
    char *const keep = PacketBufferStruct.ptr.c;
    char *const base = (char *)PacketBufferStruct.buf[PacketBufferStruct.cur];
    const double far[3] = {0.0, 0.0, -100000.0};
    Box bx = boxOf(far, 1.0);
    PacketBufferStruct.ptr.c = base + DPK_EE_SIZE / 2 + 16;
    const int res = clipBox(&bx);
    CHECK(gsb_HostCullViewsUsed() == 3 && res == 0,
          "bank: past half of the EE's bank the same step culls against three cameras (%d "
          "cameras, %d)",
          gsb_HostCullViewsUsed(), res);
    CHECK(dpk_HostRoom() == 1, "bank: room for an object at half a bank");
    PacketBufferStruct.ptr.c = base + DPK_EE_SIZE + 0x80000 - 0x10000 + 16;
    CHECK(dpk_HostRoom() == 0, "bank: no room within the reserve of the pad's end (fill %ld)",
          dpk_HostFill());
    PacketBufferStruct.ptr.c = keep;
    clipBox(&bx);
    CHECK(gsb_HostCullViewsUsed() == 0, "bank: back under half, every box kept again (%d)",
          gsb_HostCullViewsUsed());
}

/* ------------------------------------------------------------- margin */

/* gsb_ClipBox drops a box only when every corner
   is past a plane by the margin.  The current matrix the identity (a
   corner's clip coordinates are its own, w 1), one camera (locked) */
static void checkMargin(void)
{
    setOptions(ICO_FRAMERATE_ORIGINAL, 640, 480);

    static const struct {
        float x0, x1;
        int dropped;
    } cases[] = {
        {1.0005f, 1.5f, 0}, /* past x = w by less than the margin: kept */
        {1.5f, 2.0f, 0},    /* every corner within w (1 + 2^-16) + 1 */
        {2.01f, 3.0f, 1},   /* past it: dropped */
        {-1.5f, -1.0005f, 0}, {-3.0f, -2.01f, 1},
    };

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        Box b;
        for (int i = 0; i < 8; i++) {
            b.v[i][0] = (i & 1) ? cases[k].x1 : cases[k].x0;
            b.v[i][1] = (i & 2) ? 0.5f : -0.5f;
            b.v[i][2] = (i & 4) ? 0.5f : -0.5f;
            b.v[i][3] = 1.0f;
        }
        memset(ico_current_matrix, 0, sizeof(ico_current_matrix));
        for (int i = 0; i < 4; i++) {
            ico_current_matrix[i][i] = 1.0f;
        }
        gsb_HostCullCameraLocked(1);
        const int res = gsb_ClipBox(&b.v[0][0]);
        gsb_HostCullCameraLocked(0);
        CHECK((res == 0) == cases[k].dropped && (cases[k].dropped || res == 1),
              "margin: x %g .. %g: %d, want %s", (double)cases[k].x0, (double)cases[k].x1, res,
              cases[k].dropped ? "0 (dropped)" : "1 (kept, clipped)");
    }
}

/* A step past half the limits, a box behind the eye of
   every camera: kept, 2 (a corner closer than the near plane) */
static void checkSpanNear(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 640, 480);
    Cam a, b;
    camTurn(&a, 0.0, 0.0);
    a.e[0] = a.e[1] = a.e[2] = 0.0;
    b = a;
    camTurn(&b, 20.0 * DEG, 0.0);
    setCamera(&a);
    tick();
    setCamera(&b);
    const double behind[3] = {0.0, 0.0, -400.0};
    Box bx = boxOf(behind, 5.0);
    const int res = clipBox(&bx);
    CHECK(gsb_HostCullViewsUsed() == 0 && res == 2,
          "span: a box behind the eye on a 20 degree step is kept with the clipping program "
          "(%d, %d cameras)",
          res, gsb_HostCullViewsUsed());
}

/* ------------------------------------------------- the stage, with the disc
 *
 * st18a's parts as the game culls them (the object's box,
 * then each part's: RegistPacket.c reg_setNMatrixPacket and the part cull,
 * the boxes the vertex extents PObj.c MakeBoundingBox makes at load; the
 * stage is placed at the identity) against the camera of the reporter's
 * dump i76 (frame 5553, 16:9 Enhanced, uncapped) and cameras orbiting
 * about points in front of it, the tick before's and this tick's as the
 * game sets them.  Every picture the presenter draws between the two
 * (t = 1/16 .. 15/16, the views blended by rdcb_blend_view, rd_interp.c
 * camSetup's own function) is tested against every triangle of every part:
 * a part with a triangle in a picture must have been registered by this
 * tick, or by the tick before and passed by the presenter's gate for a draw
 * of the tick before alone (rd_interp.c prevAlone: not wholly inside this
 * tick's cull frustum, mayBeCulledAtCur). */

typedef struct StagePart {
    float box[8][4]; /* PObj.c MakeBoundingBox's corners */
    double reach;    /* the farthest vertex from the model's origin */
    int tris;
    float (*tri)[3][3];
} StagePart;

typedef struct StageModel {
    char name[16];
    float bb[8][4]; /* the object's box */
    int parts;
    StagePart *part;
} StageModel;

#define STAGE_MODELS 8

static StageModel s_stage[STAGE_MODELS];

static int s_stageCount;

static uint32_t le32(const unsigned char *f, uint32_t at)
{
    return (uint32_t)f[at] | (uint32_t)f[at + 1] << 8 | (uint32_t)f[at + 2] << 16 |
           (uint32_t)f[at + 3] << 24;
}

static int le16s(const unsigned char *f, uint32_t at)
{
    return (int)(int16_t)(uint16_t)(f[at] | f[at + 1] << 8);
}

static void boxCorners(float (*out)[4], const float *mn, const float *mx)
{
    for (int l = 0; l < 8; l++) {
        out[l][0] = (l & 1) ? mn[0] : mx[0];
        out[l][1] = (l & 2) ? mn[1] : mx[1];
        out[l][2] = (l & 4) ? mn[2] : mx[2];
        out[l][3] = 1.0f;
    }
}

/* a p2o's parts: the vertex table (rec + 0x90) and the strips (rec +
   0x100: head records, a count then that many 16-byte vertex records whose
   position index is at +4; a count under 3 ends them, as the game reads
   them: Packet.c pac_makeStrip's while (n >= 3), the list's own end a
   count of 0, pac_makeShapeTable's while (n != 0)); 0 when the file is
   not one.
   Walking on to a count of -1 read past the list's zero record into the
   strip table and the next part's header: st18a had 144,253 triangles
   instead of its 28,843 (with an area 100,948 instead of 28,842), the
   extra ones of no model, among them long slivers across the courtyard
   that no draw has (the capture frame 4219's drawn st18a_p1 parts
   hold 14,382 triangles, its VU meshes' 43,146 indices;
   checkStageLoader) */
static int loadModel(StageModel *m, const unsigned char *f, uint32_t size)
{
    if (size < 0x18 || memcmp(f, "PS2O", 4) != 0) {
        return 0;
    }
    const uint32_t tbl = le32(f, 4), n = le32(f, 8);
    float gmn[3] = {16777215.0f, 16777215.0f, 16777215.0f};
    float gmx[3] = {-16777215.0f, -16777215.0f, -16777215.0f};
    m->part = calloc(n ? n : 1, sizeof(StagePart));
    if (!m->part) {
        return 0;
    }
    m->parts = (int)n;
    for (uint32_t o = 0; o < n; o++) {
        StagePart *sp = &m->part[o];
        if (tbl + o * 4 + 4 > size) {
            return 0;
        }
        const uint32_t rec = le32(f, tbl + o * 4);
        if (rec + 0x180 > size || memcmp(f + rec + 0x80, "OBJH", 4) != 0) {
            return 0;
        }
        const uint32_t vtx = le32(f, rec + 0x90), vc = le32(f, rec + 0x94);
        const uint32_t strips = le32(f, rec + 0x100), sc = le32(f, rec + 0x104);
        float mn[3] = {16777215.0f, 16777215.0f, 16777215.0f};
        float mx[3] = {-16777215.0f, -16777215.0f, -16777215.0f};
        if (vtx + vc * 16 > size) {
            return 0;
        }
        for (uint32_t j = 0; j < vc; j++) {
            float q[4];
            memcpy(q, f + vtx + j * 16, sizeof(q));
            for (int a = 0; a < 3; a++) {
                mn[a] = q[a] < mn[a] ? q[a] : mn[a];
                mx[a] = mx[a] < q[a] ? q[a] : mx[a];
                gmn[a] = q[a] < gmn[a] ? q[a] : gmn[a];
                gmx[a] = gmx[a] < q[a] ? q[a] : gmx[a];
            }
            sp->reach = fmax(sp->reach,
                             sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2]));
        }
        boxCorners(sp->box, mn, mx);
        /* the strips' triangles: every three in a row */
        for (int pass = 0; pass < 2; pass++) {
            int t = 0;
            for (uint32_t s = 0; s < sc && strips + s * 4 + 4 <= size; s++) {
                uint32_t at = le32(f, strips + s * 4);
                while (at + 16 <= size && le16s(f, at) >= 3) {
                    const int k = le16s(f, at);
                    for (int i = 2; i < k && at + 16 + (uint32_t)i * 16 + 16 <= size; i++) {
                        const int ix[3] = {le16s(f, at + 16 + (uint32_t)(i - 2) * 16 + 4),
                                           le16s(f, at + 16 + (uint32_t)(i - 1) * 16 + 4),
                                           le16s(f, at + 16 + (uint32_t)i * 16 + 4)};
                        if (ix[0] < 0 || ix[1] < 0 || ix[2] < 0 || (uint32_t)ix[0] >= vc ||
                            (uint32_t)ix[1] >= vc || (uint32_t)ix[2] >= vc) {
                            continue;
                        }
                        if (pass == 1) {
                            for (int v = 0; v < 3; v++) {
                                memcpy(sp->tri[t][v], f + vtx + (uint32_t)ix[v] * 16,
                                       3 * sizeof(float));
                            }
                        }
                        t++;
                    }
                    at += 16 + (uint32_t)(k > 0 ? k : 0) * 16;
                }
            }
            if (pass == 0) {
                sp->tris = t;
                sp->tri = calloc(t ? (size_t)t : 1u, sizeof(*sp->tri));
                if (!sp->tri) {
                    return 0;
                }
            }
        }
    }
    boxCorners(m->bb, gmn, gmx);
    return 1;
}

/* st18a's models from the disc image: 0 without one */
static int loadStage(const char *disc)
{
    IcoVfs *vfs = disc && disc[0] ? ico_vfs_mount(&ico_vfs_iso9660, disc) : NULL;
    if (!vfs) {
        return 0;
    }
    for (int k = 1; k <= STAGE_MODELS && s_stageCount < STAGE_MODELS; k++) {
        char path[64];
        IcoDfMember mem;
        unsigned char *f;
        snprintf(path, sizeof(path), "object/sdf/st18a/model/st18a_p%d.p2o", k);
        if (ico_df_find_member(vfs, path, &mem) != 0) {
            continue;
        }
        f = malloc(mem.size ? mem.size : 1);
        if (!f || ico_df_read_member(vfs, &mem, f) != 0) {
            free(f);
            continue;
        }
        StageModel *m = &s_stage[s_stageCount];
        snprintf(m->name, sizeof(m->name), "st18a_p%d", k);
        if (loadModel(m, f, (uint32_t)mem.size)) {
            s_stageCount++;
        } else {
            CHECK(0, "stage: %s is not a model", path);
        }
        free(f);
    }
    ico_vfs_unmount(vfs);
    return s_stageCount;
}

/* the camera of the dump i76 (frame 5553): its view (world to view,
   column-major) and zoom, from the dump's header (rd_dump.c, RdCamera) */
static const float kI76View[16] = {0.0271272995f, 0.507210255f,   -0.861343563f, 0.0f,
                                   0.0f,          0.861662924f,   0.507398367f,  0.0f,
                                   0.999629378f,  -0.0137643469f, 0.0233745873f, 0.0f,
                                   2508.62744f,   -922.40686f,    1694.52332f,   1.0f};

#define I76_ZOOM 382.074646f

/* a Cam from a world to view matrix: its rotation (orthonormalised through
   the quaternion) and its eye */
static Cam camOfView(const float *v)
{
    double r[3][3], q[4];
    Cam c;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            r[col][row] = v[col * 4 + row]; /* camera to world */
        }
    }
    rdcb_quat_from_rot((const double (*)[3])r, q);
    const double n = sqrt(qdot(q, q));
    for (int i = 0; i < 4; i++) {
        c.q[i] = q[i] / n;
    }
    for (int j = 0; j < 3; j++) {
        c.e[j] = -(v[j * 4 + 0] * v[12] + v[j * 4 + 1] * v[13] + v[j * 4 + 2] * v[14]);
    }
    return c;
}

/* c turned by ang radians about the world axis u (unit) through p: the eye
   carried round, the view turned with it */
static Cam orbit(const Cam *c, const double *u, const double *p, double ang)
{
    const double s = sin(ang / 2), qr[4] = {u[0] * s, u[1] * s, u[2] * s, cos(ang / 2)};
    double r[3][3], d[3];
    Cam o;
    qmul(qr, c->q, o.q);
    rotOf(qr, r);
    for (int i = 0; i < 3; i++) {
        d[i] = c->e[i] - p[i];
    }
    for (int i = 0; i < 3; i++) {
        o.e[i] = p[i] + r[i][0] * d[0] + r[i][1] * d[1] + r[i][2] * d[2];
    }
    return o;
}

/* gsb_SetVSMatrix's distance for the zoom z (vsParam[0], the screen
   matrix's scale: the distance times the stage's view scale and the
   game's zoom factors) */
static float distanceFor(float z)
{
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 1000.0f);
    return z * 1000.0f / ((const float *)(matrixptr + 0xC0))[0];
}

/* the game's camera with the zoom z (camera-root.c MakeCameraMatrix) */
static void setCameraZoom(const Cam *c, float z)
{
    double v[16];
    float f[16];
    viewOf(c, v);
    for (int i = 0; i < 16; i++) {
        f[i] = (float)v[i];
    }
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, distanceFor(z));
    memcpy(matrixptr + 0x80, f, sizeof(f));
    gsb_MakeCommonMatrix();
}

/* which parts the game registers under the camera in force: the object's
   box, then the part's (gsb_ClipBox 0 drops); the count kept */
static int registerStage(unsigned char *kept, int oneCamera)
{
    int n = 0, at = 0;
    /* one camera: the cull of a part locked to the camera (the PS2's
       test), the control below */
    gsb_HostCullCameraLocked(oneCamera);
    for (int m = 0; m < s_stageCount; m++) {
        memcpy(ico_current_matrix, matrixptr + 0x280, sizeof(ico_current_matrix));
        const int obj = gsb_ClipBox(&s_stage[m].bb[0][0]) != 0;
        for (int j = 0; j < s_stage[m].parts; j++, at++) {
            memcpy(ico_current_matrix, matrixptr + 0x280, sizeof(ico_current_matrix));
            kept[at] = obj && gsb_ClipBox(&s_stage[m].part[j].box[0][0]) != 0;
            n += kept[at];
        }
    }
    gsb_HostCullCameraLocked(0);
    return n;
}

/* 1 when the triangle has a piece inside the picture of the view v (world
   to view): view z past DRAWN_NEAR and |x| <= tx z (1 - 1e-6), |y| <= ty z
   (1 - 1e-6), clipped plane by plane (a touch on an edge is not a piece) */
static int triInPicture(const double *v, const float (*tri)[3], double tx, double ty)
{
    double poly[2][12][3];
    int n = 3, cur = 0;
    for (int k = 0; k < 3; k++) {
        const double p[3] = {tri[k][0], tri[k][1], tri[k][2]};
        double o[4];
        xform(v, p, o);
        poly[0][k][0] = o[0];
        poly[0][k][1] = o[1];
        poly[0][k][2] = o[2];
    }
    tx *= 1.0 - 1e-6;
    ty *= 1.0 - 1e-6;
    for (int pl = 0; pl < 5; pl++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            const double *a = poly[cur][i], *b = poly[cur][(i + 1) % n];
            double da, db;
#define PLANE(q)                                                                                   \
    (pl == 0   ? (q)[2] - DRAWN_NEAR                                                               \
     : pl == 1 ? tx * (q)[2] - (q)[0]                                                              \
     : pl == 2 ? tx * (q)[2] + (q)[0]                                                              \
     : pl == 3 ? ty * (q)[2] - (q)[1]                                                              \
               : ty * (q)[2] + (q)[1])
            da = PLANE(a);
            db = PLANE(b);
#undef PLANE
            if (da >= 0.0 && m < 12) {
                memcpy(poly[!cur][m++], a, sizeof(poly[0][0]));
            }
            if ((da >= 0.0) != (db >= 0.0) && m < 12) {
                const double s = da / (da - db);
                for (int c = 0; c < 3; c++) {
                    poly[!cur][m][c] = a[c] + s * (b[c] - a[c]);
                }
                m++;
            }
        }
        n = m;
        cur = !cur;
        if (n < 3) {
            return 0;
        }
    }
    /* a piece with an area */
    double area = 0.0;
    for (int i = 1; i + 1 < n; i++) {
        double e1[3], e2[3], c[3];
        for (int k = 0; k < 3; k++) {
            e1[k] = poly[cur][i][k] - poly[cur][0][k];
            e2[k] = poly[cur][i + 1][k] - poly[cur][0][k];
        }
        c[0] = e1[1] * e2[2] - e1[2] * e2[1];
        c[1] = e1[2] * e2[0] - e1[0] * e2[2];
        c[2] = e1[0] * e2[1] - e1[1] * e2[0];
        area += sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    }
    return area > 1e-6;
}

/* rd_interp.c mayBeCulledAtCur for a part placed at the identity: the
   sphere about the world's origin holding its vertices is not wholly
   inside this tick's cull frustum (the screen matrix S = Pc Vc, GS 2048 +-
   1.12 times the half width and height, the near plane at nearZ) */
static int gatePasses(const double *sc, double reach, double gsW, double gsH, double nz)
{
    const double m = 1.0 + (double)RD_CULL_MARGIN, hw = 0.5 * gsW * m, hh = 0.5 * gsH * m;
    double pl[5][4];
    for (int k = 0; k < 4; k++) {
        const double x = sc[k * 4], y = sc[k * 4 + 1], w = sc[k * 4 + 3];
        pl[0][k] = x - (2048.0 - hw) * w;
        pl[1][k] = (2048.0 + hw) * w - x;
        pl[2][k] = y - (2048.0 - hh) * w;
        pl[3][k] = (2048.0 + hh) * w - y;
        pl[4][k] = w;
    }
    pl[4][3] -= nz;
    for (int i = 0; i < 5; i++) {
        const double n = sqrt(pl[i][0] * pl[i][0] + pl[i][1] * pl[i][1] + pl[i][2] * pl[i][2]);
        if (!(n > 0.0) || pl[i][3] < reach * n) {
            return 1;
        }
    }
    return 0;
}

typedef struct SweepStats {
    int pairs, snapped, keepAll, threeCams, pictures, hidden, gaps, regCur, regOne;
    double worstT;
} SweepStats;

static int stagePartCount(void)
{
    int n = 0;
    for (int m = 0; m < s_stageCount; m++) {
        n += s_stage[m].parts;
    }
    return n;
}

/* --------------------------------- behind the eye, between the ticks
 *
 * The region rule (vu_common.hlsli vu_region_rule): a triangle with a
 * vertex behind the eye (clip w <= 0) whose GPU-clipped extent reaches the
 * camera's picture (GS 2048 -+ 256 across, -+ half the height down) is not
 * drawn.  The PS2 tested it at the tick's camera only; the presenter tests
 * it at the in-between camera, whose eye is on the straight line between
 * the two ticks' eyes.  For every picture between the ticks, st18a's
 * triangles rasterised at the native frame's pixels (the nearest surface,
 * 1 / w), every triangle drawn (clipped), then without the triangles the
 * rule drops at the in-between camera: a pixel whose nearest surface is
 * farther there (or missing) is a hole.  A pop: a hole the rule kept only
 * for a triangle it drops at both ticks' cameras (vu_region_rule under
 * ICO_VU_TICKS) does not have; of them, the holes of triangles the rule
 * drops at neither tick (no picture the PS2 drew between the two ticks
 * had them).  The shown-tick rule: a tick shows a triangle only when the
 * rule does not drop it at that tick's camera AND what the GPU draws of it
 * there reaches the frame (the native frame's columns and rows; at 4:3
 * the picture), the rule vu_region_rule has: the pixels it closes
 * (holes of the in-between rule it draws over), and the pixels an
 * either-tick rule would add of triangles neither tick showed (a flash of
 * what the PS2 hid at both ticks).  --stage DISC
 * --eye (not a ctest: about a minute; with --eye the sweep also runs at
 * 4:3, where the frame is the picture). */

typedef struct EyeWorst {
    int pixels, step, model, part;
    double t;
    char label[64];
} EyeWorst;

#define EYE_WORST 6

typedef struct EyeStats {
    long pictures;    /* pictures between the ticks */
    long withDrop;    /* of them, with a triangle the in-between rule drops */
    long withPop;     /* of them, with a pop */
    long withNeither; /* of them, with a pop of a triangle dropped at neither tick */
    long holes;       /* pixels the in-between rule leaves farther than every triangle drawn */
    long pops;        /* pixels it leaves farther than the both-ticks rule */
    long popsNeither; /* of them, of triangles the rule drops at neither tick */
    long dropT;       /* triangles the in-between rule drops, summed over the pictures */
    long dropBoth;    /* of them, dropped at both ticks' cameras too */
    long dropNoTick;  /* of them, dropped at neither tick's camera */
    long dropUnseen;  /* of them, not dropped at a tick but shown at neither (off its frame) */
    long closedS1;    /* pixels the shown-tick rule draws over the in-between rule's holes */
    long unseen;      /* pixels the either-tick rule adds of triangles neither tick showed */
    long withUnseen;  /* pictures with such pixels */
    EyeWorst worst[EYE_WORST], worstNeither[EYE_WORST], worstUnseen[EYE_WORST];
} EyeStats;

static EyeStats s_eye;
static int s_eyeOn, s_eyeWanted; /* --eye */
static int s_eyeStep;
static char s_eyeLabel[64];

/* st18a's triangles with an area, flat, with their model and part */
static float (*s_tri)[3][3];
static unsigned short (*s_triOf)[2];
static int s_triCount;

static int flattenStage(void)
{
    int n = 0;
    for (int m = 0; m < s_stageCount; m++) {
        for (int j = 0; j < s_stage[m].parts; j++) {
            n += s_stage[m].part[j].tris;
        }
    }
    s_tri = calloc(n ? (size_t)n : 1u, sizeof(*s_tri));
    s_triOf = calloc(n ? (size_t)n : 1u, sizeof(*s_triOf));
    if (!s_tri || !s_triOf) {
        return 0;
    }
    s_triCount = 0;
    for (int m = 0; m < s_stageCount; m++) {
        for (int j = 0; j < s_stage[m].parts; j++) {
            const StagePart *sp = &s_stage[m].part[j];
            for (int q = 0; q < sp->tris; q++) {
                const float (*v)[3] = sp->tri[q];
                double e1[3], e2[3], c[3];
                for (int k = 0; k < 3; k++) {
                    e1[k] = (double)v[1][k] - v[0][k];
                    e2[k] = (double)v[2][k] - v[0][k];
                }
                c[0] = e1[1] * e2[2] - e1[2] * e2[1];
                c[1] = e1[2] * e2[0] - e1[0] * e2[2];
                c[2] = e1[0] * e2[1] - e1[1] * e2[0];
                if (c[0] * c[0] + c[1] * c[1] + c[2] * c[2] <= 0.0) {
                    continue; /* a strip's turn: no area, nothing drawn */
                }
                memcpy(s_tri[s_triCount], v, sizeof(s_tri[0]));
                s_triOf[s_triCount][0] = (unsigned short)m;
                s_triOf[s_triCount][1] = (unsigned short)j;
                s_triCount++;
            }
        }
    }
    return s_triCount;
}

/* h = m (p, 1) for the triangle's corners */
static void triClip(const double *m, const float (*v)[3], double (*h)[4])
{
    for (int k = 0; k < 3; k++) {
        const double p[3] = {v[k][0], v[k][1], v[k][2]};
        xform(m, p, h[k]);
    }
}

/* a convex polygon in clip space cut to dot(pl, h) >= 0 */
static int cutPoly(double (*p)[4], int n, const double *pl)
{
    double q[12][4];
    int m = 0;
    for (int i = 0; i < n; i++) {
        const double *a = p[i], *b = p[(i + 1) % n];
        const double da = pl[0] * a[0] + pl[1] * a[1] + pl[2] * a[2] + pl[3] * a[3];
        const double db = pl[0] * b[0] + pl[1] * b[1] + pl[2] * b[2] + pl[3] * b[3];
        if (da >= 0.0 && m < 12) {
            memcpy(q[m++], a, sizeof(q[0]));
        }
        if ((da >= 0.0) != (db >= 0.0) && m < 12) {
            const double s = da / (da - db);
            for (int c = 0; c < 4; c++) {
                q[m][c] = a[c] + s * (b[c] - a[c]);
            }
            m++;
        }
    }
    memcpy(p, q, (size_t)m * sizeof(q[0]));
    return m;
}

/* the GPU's depth clip, 0 <= GS Z <= 2^32 (vu_homogeneous_position: z =
   16 h.z / 2^32, 0 <= z <= w) */
static int cutDepth(double (*p)[4], int n)
{
    static const double zLo[4] = {0.0, 0.0, 1.0, 0.0};
    static const double zHi[4] = {0.0, 0.0, -16.0, 4294967296.0};
    n = cutPoly(p, n, zLo);
    return n >= 3 ? cutPoly(p, n, zHi) : n;
}

/* vu_region_rule's drop for a triangle of the extended picture (every
   vertex in front of the eye is drawn, clipped): a vertex behind the eye
   (w <= 0) and the GPU-clipped extent reaching the picture
   (vu_cut_reaches_picture) */
static int eyeDrops(const double (*h)[4])
{
    if (!(h[0][3] <= 0.0 || h[1][3] <= 0.0 || h[2][3] <= 0.0)) {
        return 0;
    }
    double p[12][4];
    memcpy(p, h, 3 * sizeof(p[0]));
    const double hh = halfH();
    const double top[4] = {0.0, -1.0, 0.0, 2048.0 + hh};      /* y <= (2048 + hh) w */
    const double bottom[4] = {0.0, 1.0, 0.0, -(2048.0 - hh)}; /* y >= (2048 - hh) w */
    int n = cutDepth(p, 3);
    if (n >= 3) {
        n = cutPoly(p, n, top);
    }
    if (n >= 3) {
        n = cutPoly(p, n, bottom);
    }
    if (n < 3) {
        return 0;
    }
    double lo = 1e30, hi = -1e30;
    for (int i = 0; i < n; i++) {
        const double x = p[i][3] > 0.0 ? p[i][0] / p[i][3] : (p[i][0] >= 0.0 ? 1e30 : -1e30);
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    return lo < 2048.0 + 256.0 && hi > 2048.0 - 256.0;
}

/* whether the GPU-clipped extent of the triangle h reaches the native frame
   (fw x fh about GS 2048, 2048: vu_common.hlsli vu_frame_ndc), cut to its
   rows first (vu_cut_reaches) */
static int frameReach(const double (*h)[4], int fw, int fh)
{
    double p[12][4];
    memcpy(p, h, 3 * sizeof(p[0]));
    const double top[4] = {0.0, -1.0, 0.0, 2048.0 + fh * 0.5};
    const double bottom[4] = {0.0, 1.0, 0.0, -(2048.0 - fh * 0.5)};
    int n = cutDepth(p, 3);
    if (n >= 3) {
        n = cutPoly(p, n, top);
    }
    if (n >= 3) {
        n = cutPoly(p, n, bottom);
    }
    if (n < 3) {
        return 0;
    }
    double lo = 1e30, hi = -1e30;
    for (int i = 0; i < n; i++) {
        const double x = p[i][3] > 0.0 ? p[i][0] / p[i][3] : (p[i][0] >= 0.0 ? 1e30 : -1e30);
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    return lo < 2048.0 + fw * 0.5 && hi > 2048.0 - fw * 0.5;
}

#define EYE_MAX_W 2560
#define EYE_MAX_H 448

/* the nearest 1 / w: every triangle; the both-ticks rule; the in-between
   rule; the both-ticks rule less the triangles dropped at neither tick;
   the shown-tick rule */
static float s_dAll[EYE_MAX_W * EYE_MAX_H], s_dNew[EYE_MAX_W * EYE_MAX_H],
    s_dNow[EYE_MAX_W * EYE_MAX_H], s_dNei[EYE_MAX_W * EYE_MAX_H], s_dS1[EYE_MAX_W * EYE_MAX_H];
static int s_idNew[EYE_MAX_W * EYE_MAX_H];
static unsigned char *s_flag; /* bit 0: dropped at t, bit 1: at the tick before, bit 2: at this
                                 tick; bit 3: shown at a tick (not dropped there, on its frame) */
static double (*s_h)[3][4];

/* the polygon's pixels (centres) inside the tile x0..x1, y0..y1 (frame
   pixels, the frame's left top at GS fx, fy): 1 / w into the buffers the
   mask names (1 all, 2 new, 4 now, 8 nei, 16 the shown-tick rule), the nearest kept */
static void rasterPoly(double (*p)[4], int n, double fx, double fy, int fw, int x0, int y0, int x1,
                       int y1, int mask, int id)
{
    double s[12][3];
    for (int i = 0; i < n; i++) {
        s[i][0] = p[i][0] / p[i][3] - fx;
        s[i][1] = p[i][1] / p[i][3] - fy;
        s[i][2] = 1.0 / p[i][3];
    }
    for (int k = 1; k + 1 < n; k++) {
        const double *a = s[0], *b = s[k], *c = s[k + 1];
        const double area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        if (area == 0.0) {
            continue;
        }
        double mnx = fmin(a[0], fmin(b[0], c[0])), mxx = fmax(a[0], fmax(b[0], c[0]));
        double mny = fmin(a[1], fmin(b[1], c[1])), mxy = fmax(a[1], fmax(b[1], c[1]));
        int ix0 = (int)floor(mnx - 0.5), ix1 = (int)ceil(mxx - 0.5);
        int iy0 = (int)floor(mny - 0.5), iy1 = (int)ceil(mxy - 0.5);
        ix0 = ix0 < x0 ? x0 : ix0;
        iy0 = iy0 < y0 ? y0 : iy0;
        ix1 = ix1 > x1 ? x1 : ix1;
        iy1 = iy1 > y1 ? y1 : iy1;
        for (int y = iy0; y <= iy1; y++) {
            const double py = y + 0.5;
            for (int x = ix0; x <= ix1; x++) {
                const double px = x + 0.5;
                const double w0 = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
                const double w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
                    continue;
                }
                const float iw = (float)(w0 * a[2] + w1 * b[2] + w2 * c[2]);
                const int at = y * fw + x;
                if ((mask & 1) && iw > s_dAll[at]) {
                    s_dAll[at] = iw;
                }
                if ((mask & 2) && iw > s_dNew[at]) {
                    s_dNew[at] = iw;
                    s_idNew[at] = id;
                }
                if ((mask & 4) && iw > s_dNow[at]) {
                    s_dNow[at] = iw;
                }
                if ((mask & 8) && iw > s_dNei[at]) {
                    s_dNei[at] = iw;
                }
                if ((mask & 16) && iw > s_dS1[at]) {
                    s_dS1[at] = iw;
                }
            }
        }
    }
}

/* 1 when the nearest surface b is farther than a (or missing) */
static int farther(float a, float b)
{
    return a > 0.0f && b < a * (1.0f - 1e-5f);
}

static void eyeNoteWorst(EyeWorst *w, int pixels, double t, int part)
{
    const int n = EYE_WORST;
    int at = n;
    while (at > 0 && pixels > w[at - 1].pixels) {
        at--;
    }
    if (at >= n) {
        return;
    }
    memmove(&w[at + 1], &w[at], (size_t)(n - 1 - at) * sizeof(w[0]));
    w[at].pixels = pixels;
    w[at].t = t;
    w[at].step = s_eyeStep;
    w[at].model = part / 64; /* the part, model * 64 + part */
    w[at].part = part % 64;
    snprintf(w[at].label, sizeof(w[at].label), "%s", s_eyeLabel);
}

/* the picture at t (view vt, projection pt) between the ticks (vp, pp) and
   (vc, pc) */
static void eyePicture(const double *vp, const double *pp, const double *vc, const double *pc,
                       const double *vt, const double *pt, double t)
{
    double st[16], sp[16], sc[16], hp[3][4], hc[3][4];
    rdcb_mul4d(pt, vt, st);
    rdcb_mul4d(pp, vp, sp);
    rdcb_mul4d(pc, vc, sc);
    if (!s_flag) {
        s_flag = calloc((size_t)s_triCount, 1);
        s_h = calloc((size_t)s_triCount, sizeof(*s_h));
        if (!s_flag || !s_h) {
            CHECK(0, "eye: no memory");
            return;
        }
    }
    s_eye.pictures++;
    const int fw = gsFrame.w, fh = ScreenHeight;
    const double fx = 2048.0 - fw / 2, fy = 2048.0 - fh / 2;
    int any = 0, popTris = 0, tx0 = fw, ty0 = fh, tx1 = -1, ty1 = -1;
    for (int i = 0; i < s_triCount; i++) {
        triClip(st, (const float (*)[3])s_tri[i], s_h[i]);
        s_flag[i] = (unsigned char)eyeDrops((const double (*)[4])s_h[i]);
        if (!s_flag[i]) {
            continue;
        }
        any = 1;
        triClip(sp, (const float (*)[3])s_tri[i], hp);
        triClip(sc, (const float (*)[3])s_tri[i], hc);
        s_flag[i] |= (unsigned char)(eyeDrops((const double (*)[4])hp) << 1);
        s_flag[i] |= (unsigned char)(eyeDrops((const double (*)[4])hc) << 2);
        const int shown = (!(s_flag[i] & 2) && frameReach((const double (*)[4])hp, fw, fh)) ||
                          (!(s_flag[i] & 4) && frameReach((const double (*)[4])hc, fw, fh));
        s_flag[i] |= (unsigned char)(shown << 3);
        s_eye.dropT++;
        s_eye.dropBoth += s_flag[i] == 7;
        s_eye.dropNoTick += (s_flag[i] & 7) == 1;
        s_eye.dropUnseen += (s_flag[i] & 7) != 7 && !shown;
        if (s_flag[i] == 7) {
            continue;
        }
        popTris++;
        /* the tile: the dropped triangle's clipped extent in the frame */
        double p[12][4];
        memcpy(p, s_h[i], sizeof(s_h[i]));
        const int n = cutDepth(p, 3);
        for (int k = 0; k < n; k++) {
            if (!(p[k][3] > 0.0)) {
                tx0 = ty0 = 0;
                tx1 = fw - 1;
                ty1 = fh - 1;
                continue;
            }
            const double x = p[k][0] / p[k][3] - fx, y = p[k][1] / p[k][3] - fy;
            const int ix = x < -1.0 ? -1 : x > fw + 1.0 ? fw + 1 : (int)floor(x);
            const int iy = y < -1.0 ? -1 : y > fh + 1.0 ? fh + 1 : (int)floor(y);
            tx0 = ix < tx0 ? ix : tx0;
            ty0 = iy < ty0 ? iy : ty0;
            tx1 = ix + 1 > tx1 ? ix + 1 : tx1;
            ty1 = iy + 1 > ty1 ? iy + 1 : ty1;
        }
    }
    s_eye.withDrop += any;
    if (!popTris) {
        return;
    }
    tx0 = tx0 < 0 ? 0 : tx0;
    ty0 = ty0 < 0 ? 0 : ty0;
    tx1 = tx1 > fw - 1 ? fw - 1 : tx1;
    ty1 = ty1 > fh - 1 ? fh - 1 : ty1;
    if (tx1 < tx0 || ty1 < ty0) {
        return;
    }
    for (int y = ty0; y <= ty1; y++) {
        for (int x = tx0; x <= tx1; x++) {
            const int at = y * fw + x;
            s_dAll[at] = s_dNew[at] = s_dNow[at] = s_dNei[at] = s_dS1[at] = 0.0f;
            s_idNew[at] = -1;
        }
    }
    for (int i = 0; i < s_triCount; i++) {
        const double (*h)[4] = (const double (*)[4])s_h[i];
        if (h[0][3] > 0.0 && h[1][3] > 0.0 && h[2][3] > 0.0) {
            /* in front of the eye: off the tile by its corners alone */
            double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
            for (int k = 0; k < 3; k++) {
                for (int a = 0; a < 2; a++) {
                    const double q = h[k][a] / h[k][3] - (a ? fy : fx);
                    lo[a] = q < lo[a] ? q : lo[a];
                    hi[a] = q > hi[a] ? q : hi[a];
                }
            }
            if (hi[0] < tx0 || lo[0] > tx1 + 1 || hi[1] < ty0 || lo[1] > ty1 + 1) {
                continue;
            }
        }
        double p[12][4];
        memcpy(p, s_h[i], sizeof(s_h[i]));
        const int n = cutDepth(p, 3);
        if (n < 3) {
            continue;
        }
        const int f = s_flag[i] & 7;
        const int mask = 1 | (f == 7 ? 0 : 2) | (f & 1 ? 0 : 4) | (f == 7 || f == 1 ? 0 : 8) |
                         (!(f & 1) || (s_flag[i] & 8) ? 16 : 0);
        rasterPoly(p, n, fx, fy, fw, tx0, ty0, tx1, ty1, mask, i);
    }
    /* the part the most pop pixels show without the rule */
    static int byPart[3][STAGE_MODELS * 64];
    memset(byPart, 0, sizeof(byPart));
    int pops = 0, holes = 0, nei = 0, closed = 0, unseen = 0;
    for (int y = ty0; y <= ty1; y++) {
        for (int x = tx0; x <= tx1; x++) {
            const int at = y * fw + x;
            holes += farther(s_dAll[at], s_dNow[at]);
            const int id = s_idNew[at];
            const int part = id >= 0 ? s_triOf[id][0] * 64 + (s_triOf[id][1] & 63) : -1;
            if (farther(s_dNew[at], s_dNow[at])) {
                pops++;
                byPart[0][part >= 0 ? part : 0]++;
            }
            if (farther(s_dNew[at], s_dNei[at])) {
                nei++;
                byPart[1][part >= 0 ? part : 0]++;
            }
            closed += farther(s_dS1[at], s_dNow[at]);
            if (farther(s_dNew[at], s_dS1[at])) {
                unseen++;
                byPart[2][part >= 0 ? part : 0]++;
            }
        }
    }
    int best[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        for (int j = 1; j < STAGE_MODELS * 64; j++) {
            best[k] = byPart[k][j] > byPart[k][best[k]] ? j : best[k];
        }
    }
    s_eye.holes += holes;
    s_eye.pops += pops;
    s_eye.popsNeither += nei;
    if (pops > 0) {
        s_eye.withPop++;
        eyeNoteWorst(s_eye.worst, pops, t, best[0]);
    }
    if (nei > 0) {
        s_eye.withNeither++;
        eyeNoteWorst(s_eye.worstNeither, nei, t, best[1]);
    }
    s_eye.closedS1 += closed;
    s_eye.unseen += unseen;
    if (unseen > 0) {
        s_eye.withUnseen++;
        eyeNoteWorst(s_eye.worstUnseen, unseen, t, best[2]);
    }
    if (pops > 0) {
        printf("  eye: W %d step %d (%s) t %.4f: pop %d pixels (%s part %d), neither tick %d\n", fw,
               s_eyeStep, s_eyeLabel, t, pops, s_stage[best[0] / 64].name, best[0] % 64, nei);
    }
}

/* the ticks c0, c1, c2 (the game's camera at each) and the pictures between
   c1 and c2: every part a picture shows registered at c2, or at c1 and
   passed by the gate */
static void sweepPair(const char *what, const Cam *c0, const Cam *c1, const Cam *c2, float z1,
                      float z2, int oneCamera, SweepStats *st)
{
    const int np = stagePartCount();
    unsigned char keptPrev[512], keptCur[512], keptOne[512];
    float vPrev[16], vCur[16];
    if (np > 512) {
        CHECK(0, "stage: %d parts", np);
        return;
    }
    double scPrev[16];
    setCameraZoom(c0, z1);
    tick();
    setCameraZoom(c1, z1);
    registerStage(keptPrev, oneCamera);
    memcpy(vPrev, matrixptr + 0x80, sizeof(vPrev));
    readProjections();
    memcpy(scPrev, s_screen, sizeof(scPrev));
    tick();
    setCameraZoom(c2, z2);
    readProjections();
    st->regCur += registerStage(keptCur, oneCamera);
    const int views = gsb_HostCullViewsUsed();
    memcpy(vCur, matrixptr + 0x80, sizeof(vCur));
    st->pairs++;
    if (rdcb_camera_jump(vPrev, vCur)) {
        st->snapped++; /* the presenter snaps: no picture between */
        return;
    }
    s_eyeStep++;
    st->keepAll += views == 0;
    st->threeCams += views == 3;
    st->regOne += registerStage(keptOne, 1); /* the PS2's one camera, for the cost */
    double vp[16], vc[16], sc[16];
    for (int i = 0; i < 16; i++) {
        vp[i] = vPrev[i];
        vc[i] = vCur[i];
    }
    rdcb_mul4d(s_screen, vc, sc);
    for (int k = 1; k < 16; k++) {
        const double t = k / 16.0;
        double vt[16], pt[16];
        if (!rdcb_blend_view(vp, vc, t, vt)) {
            CHECK(0, "%s: no blended view at t %g", what, t);
            continue;
        }
        /* the projection lerped (camSetup's Pt for the easing zoom) */
        rdcb_blend_proj(scPrev, s_screen, t, pt);
        const double tx = (double)gsFrame.wExact * 0.5 / pt[0];
        const double ty = halfH() / pt[5];
        st->pictures++;
        if (s_eyeOn) {
            eyePicture(vp, scPrev, vc, s_screen, vt, pt, t);
        }
        int at = 0;
        for (int m = 0; m < s_stageCount; m++) {
            for (int j = 0; j < s_stage[m].parts; j++, at++) {
                const StagePart *sp = &s_stage[m].part[j];
                if (keptCur[at]) {
                    continue;
                }
                int shown = 0;
                for (int q = 0; q < sp->tris && !shown; q++) {
                    shown = triInPicture(vt, (const float (*)[3])sp->tri[q], tx, ty);
                }
                if (!shown) {
                    continue;
                }
                if (keptPrev[at] && gatePasses(sc, sp->reach, gsFrame.w, ScreenHeight, 2.0)) {
                    st->hidden++; /* drawn as the tick before's */
                    continue;
                }
                st->gaps++;
                st->worstT = t;
                if (st->gaps <= 8) {
                    printf("  %s: %s part %d shown at t %.4f, not registered (%d cameras; "
                           "the tick before %s it)\n",
                           what, s_stage[m].name, j, t, views,
                           keptPrev[at] ? "registered" : "dropped");
                }
            }
        }
    }
}

/* a unit vector */
static void unit3(double *u)
{
    const double n = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (int i = 0; i < 3; i++) {
        u[i] /= n;
    }
}

/* the pairs of ticks about the dump's camera: it as this tick (the ticks
   before it on the same path) and as the tick before (the next tick on
   it); oneCamera culls with the one camera (the control) */
static void sweepAbout(const char *what, const Cam *prevPrev, const Cam *prev, const Cam *cur,
                       const Cam *next, float zPrev, int oneCamera, SweepStats *st)
{
    sweepPair(what, prevPrev, prev, cur, zPrev, I76_ZOOM, oneCamera, st);
    sweepPair(what, prev, cur, next, I76_ZOOM, I76_ZOOM, oneCamera, st);
}

/* the dump's camera and the camera steps around it: orbits about points
   in front of it (about the world's up, the view's right, both, and the
   view's axis) with the eye 50 to 400 units on in a tick, the eye moved
   alone, the view turned alone about the eye (2 to 29 degrees) and an
   orbit with the zoom easing */
static void sweepStage(const char *what, int oneCamera, SweepStats *st)
{
    const Cam cur = camOfView(kI76View);
    double r[3][3];
    rotOf(cur.q, r);
    const double right[3] = {r[0][0], r[1][0], r[2][0]};
    const double down[3] = {r[0][1], r[1][1], r[2][1]};
    const double fwd[3] = {r[0][2], r[1][2], r[2][2]};
    static const double kDist[] = {150.0, 300.0, 500.0, 800.0};
    static const double kChord[] = {50.0, 100.0, 149.0, 200.0, 300.0, 400.0};
    static const double kTurn[] = {2.0, 5.0, 10.0, 14.0, 20.0, 29.0};
    for (size_t di = 0; di < sizeof(kDist) / sizeof(kDist[0]); di++) {
        double p[3];
        for (int i = 0; i < 3; i++) {
            p[i] = cur.e[i] + fwd[i] * kDist[di];
        }
        for (int ax = 0; ax < 5; ax++) {
            double u[3];
            for (int i = 0; i < 3; i++) {
                const double up = i == 1 ? 1.0 : 0.0;
                u[i] = ax == 0   ? up
                       : ax == 1 ? right[i]
                       : ax == 2 ? up + right[i]
                       : ax == 3 ? up - right[i]
                                 : fwd[i];
            }
            unit3(u);
            /* the eye's distance from the axis */
            double d[3], along = 0.0, rad = 0.0;
            for (int i = 0; i < 3; i++) {
                d[i] = cur.e[i] - p[i];
                along += d[i] * u[i];
            }
            for (int i = 0; i < 3; i++) {
                const double e = d[i] - along * u[i];
                rad += e * e;
            }
            rad = sqrt(rad);
            for (size_t ci = 0; ci < sizeof(kChord) / sizeof(kChord[0]); ci++) {
                double ang;
                if (ax == 4) {
                    ang = kChord[ci] / 400.0 * 8.0 * DEG; /* a roll: 1 to 8 degrees */
                } else {
                    if (kChord[ci] >= 2.0 * rad) {
                        continue;
                    }
                    ang = 2.0 * asin(kChord[ci] / (2.0 * rad));
                }
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    const Cam c0 = orbit(&cur, u, p, 2.0 * sgn * ang);
                    const Cam c1 = orbit(&cur, u, p, sgn * ang);
                    const Cam n1 = orbit(&cur, u, p, -sgn * ang);
                    snprintf(s_eyeLabel, sizeof(s_eyeLabel), "orbit %g out, axis %d, %+g units",
                             kDist[di], ax, sgn * kChord[ci]);
                    sweepAbout(what, &c0, &c1, &cur, &n1, I76_ZOOM, oneCamera, st);
                    if (ax == 0 && ci == 1) {
                        /* the zoom easing 10 % in the tick */
                        sweepAbout(what, &c0, &c1, &cur, &n1, I76_ZOOM * 0.9f, oneCamera, st);
                        sweepAbout(what, &c0, &c1, &cur, &n1, I76_ZOOM * 1.1f, oneCamera, st);
                    }
                }
            }
        }
    }
    /* the eye alone */
    for (int dir = 0; dir < 7; dir++) {
        double u[3];
        for (int i = 0; i < 3; i++) {
            u[i] = ((dir + 1) & 1 ? right[i] : 0.0) + ((dir + 1) & 2 ? down[i] : 0.0) +
                   ((dir + 1) & 4 ? fwd[i] : 0.0);
        }
        unit3(u);
        for (size_t ci = 0; ci < sizeof(kChord) / sizeof(kChord[0]); ci++) {
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                Cam c0 = cur, c1 = cur, n1 = cur;
                for (int i = 0; i < 3; i++) {
                    c1.e[i] -= sgn * kChord[ci] * u[i];
                    c0.e[i] -= 2.0 * sgn * kChord[ci] * u[i];
                    n1.e[i] += sgn * kChord[ci] * u[i];
                }
                snprintf(s_eyeLabel, sizeof(s_eyeLabel), "eye alone, direction %d, %+g units", dir,
                         sgn * kChord[ci]);
                sweepAbout(what, &c0, &c1, &cur, &n1, I76_ZOOM, oneCamera, st);
            }
        }
    }
    /* the view alone, about the eye */
    for (int ax = 0; ax < 5; ax++) {
        double u[3];
        for (int i = 0; i < 3; i++) {
            const double up = i == 1 ? 1.0 : 0.0;
            u[i] = ax == 0   ? up
                   : ax == 1 ? right[i]
                   : ax == 2 ? up + right[i]
                   : ax == 3 ? up - right[i]
                             : fwd[i];
        }
        unit3(u);
        for (size_t ti = 0; ti < sizeof(kTurn) / sizeof(kTurn[0]); ti++) {
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                const double ang = sgn * kTurn[ti] * DEG;
                const Cam c0 = orbit(&cur, u, cur.e, 2.0 * ang);
                const Cam c1 = orbit(&cur, u, cur.e, ang);
                const Cam n1 = orbit(&cur, u, cur.e, -ang);
                snprintf(s_eyeLabel, sizeof(s_eyeLabel), "turn alone, axis %d, %+g degrees", ax,
                         sgn * kTurn[ti]);
                sweepAbout(what, &c0, &c1, &cur, &n1, I76_ZOOM, oneCamera, st);
            }
        }
    }
}

static void checkStageSweep(const char *what, int winW, int winH, int wantW)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, winW, winH);
    CHECK(gsFrame.w == wantW, "stage %s: the native width %d, want %d", what, gsFrame.w, wantW);
    SweepStats st, one;
    memset(&st, 0, sizeof(st));
    memset(&one, 0, sizeof(one));
    memset(&s_eye, 0, sizeof(s_eye));
    s_eyeStep = 0;
    s_eyeOn = s_eyeWanted;
    sweepStage(what, 0, &st);
    s_eyeOn = 0;
    if (s_eyeWanted) {
        printf("gsb_cull_test: stage %s, behind the eye: %ld pictures, %ld with a triangle the "
               "in-between camera drops (%ld triangles: %ld dropped at both ticks too, %ld at "
               "neither); holes %ld pixels; pops (holes the both-ticks rule does not leave) %ld "
               "pixels in %ld pictures, of triangles dropped at neither tick %ld pixels in %ld "
               "pictures\n",
               what, s_eye.pictures, s_eye.withDrop, s_eye.dropT, s_eye.dropBoth, s_eye.dropNoTick,
               s_eye.holes, s_eye.pops, s_eye.withPop, s_eye.popsNeither, s_eye.withNeither);
        printf("gsb_cull_test: stage %s, shown at a tick (S1): %ld triangles not dropped at a "
               "tick but off its frame there; the shown-tick rule closes %ld hole pixels (the "
               "either-tick rule %ld); the either-tick rule added %ld pixels of triangles neither "
               "tick showed, in %ld pictures\n",
               what, s_eye.dropUnseen, s_eye.closedS1, s_eye.pops, s_eye.unseen, s_eye.withUnseen);
        for (int list = 0; list < 3; list++) {
            for (int k = 0; k < EYE_WORST; k++) {
                const EyeWorst *w = list == 2   ? &s_eye.worstUnseen[k]
                                    : list == 1 ? &s_eye.worstNeither[k]
                                                : &s_eye.worst[k];
                if (w->pixels > 0) {
                    printf("  %s %s: %d pixels at step %d (%s), t %.4f, %s part %d\n", what,
                           list == 2   ? "neither tick showed"
                           : list == 1 ? "neither tick"
                                       : "pop",
                           w->pixels, w->step, w->label, w->t, s_stage[w->model].name, w->part);
                }
            }
        }
    }
    printf("gsb_cull_test: stage %s (W %d): %d camera pairs (%d snapped, %d keeping every "
           "part, %d on three cameras), %d pictures between; parts registered %d (the PS2's one "
           "camera %d); parts a picture shows drawn as the tick before's %d, not drawn %d\n",
           what, gsFrame.w, st.pairs, st.snapped, st.keepAll, st.threeCams, st.pictures, st.regCur,
           st.regOne, st.hidden, st.gaps);
    CHECK(st.threeCams > 0 && st.pictures > 0, "stage %s: nothing tested", what);
    /* this tick's cull alone holds every picture (the gate is not needed) */
    CHECK(st.gaps == 0 && st.hidden == 0,
          "stage %s: %d parts a picture between the ticks shows not registered (%d of them not "
          "drawn)",
          what, st.gaps + st.hidden, st.gaps);
    /* the control: the same steps culled with the one camera leave parts
       the pictures show to the tick before's draws (the sweep sees them) */
    sweepStage("control", 1, &one);
    printf("gsb_cull_test: stage %s, culled with one camera: parts a picture shows drawn as the "
           "tick before's %d, not drawn %d\n",
           what, one.hidden, one.gaps);
    CHECK(one.hidden + one.gaps > 0,
          "stage %s: the one camera's cull leaves nothing the pictures show out", what);
}

/* the dump's own tick with the camera standing still (one camera, the
   margin): the parts dropped are the ones its log names (frame 5553:
   st18a_p1 3, 10, 13 to 20; st18a_p2 1, 2, 3, 5, 6; st18a_p3 2, 3, 7, 10
   to 13, 17 to 21) and st18a_p1 8, which the game's three cameras kept;
   this checks the boxes, the placement and the camera against the game */
static void checkStageDump(void)
{
    static const char *const kWant[3] = {"3 8 10 13 14 15 16 17 18 19 20", "1 2 3 5 6",
                                         "2 3 7 10 11 12 13 17 18 19 20 21"};
    unsigned char kept[512];
    setOptions(ICO_FRAMERATE_UNCAPPED, 1920, 1080);
    for (int k = 0; k < 2; k++) {
        gsb_SetVSMatrix(ScreenWidth, ScreenHeight, distanceFor(I76_ZOOM));
        memcpy(matrixptr + 0x80, kI76View, sizeof(kI76View));
        gsb_MakeCommonMatrix();
        if (k == 0) {
            tick();
        }
    }
    registerStage(kept, 0);
    {
        /* the eye rule at the dump's own camera, as the PS2 drew it there */
        double v[16], p[16], sv[16], h[3][4];
        int behind = 0, dropped = 0;
        for (int i = 0; i < 16; i++) {
            v[i] = kI76View[i];
            p[i] = ((const float *)(matrixptr + 0xC0))[i];
        }
        rdcb_mul4d(p, v, sv);
        for (int i = 0; i < s_triCount; i++) {
            triClip(sv, (const float (*)[3])s_tri[i], h);
            behind += h[0][3] <= 0.0 || h[1][3] <= 0.0 || h[2][3] <= 0.0;
            dropped += eyeDrops((const double (*)[4])h);
        }
        printf("gsb_cull_test: stage at the dump's camera: %d triangles with a vertex behind the "
               "eye, %d of them reaching the picture (not drawn there)\n",
               behind, dropped);
    }
    CHECK(gsb_HostCullViewsUsed() == 1, "stage: the still camera culls with %d cameras",
          gsb_HostCullViewsUsed());
    CHECK(fabsf(((const float *)(matrixptr + 0xC0))[0] - I76_ZOOM) < 1e-3f,
          "stage: the zoom %g, want the dump's %g", (double)((const float *)(matrixptr + 0xC0))[0],
          (double)I76_ZOOM);
    int at = 0;
    for (int m = 0; m < s_stageCount && m < 3; m++) {
        char got[256];
        size_t n = 0;
        got[0] = 0;
        for (int j = 0; j < s_stage[m].parts; j++, at++) {
            if (!kept[at] && n + 8 < sizeof(got)) {
                n += (size_t)snprintf(got + n, sizeof(got) - n, n ? " %d" : "%d", j);
            }
        }
        CHECK(strcmp(got, kWant[m]) == 0, "stage: %s drops %s at the dump's camera, want %s",
              s_stage[m].name, got, kWant[m]);
    }
}

/* A capture at the same place (user-slit-frame-20261012-
   162136: frame 22117, 3840 x 2160 at 16:9, Full pixel, framerate 60, the
   log's cull line without "/5"): its view and zoom from the dump's header.
   The presented picture is the tick's (the camera stood still: the
   mirrored present and the tick's frame differ by under half a grey level),
   so the cull ran with one camera for that reason, not for the framerate.
   At that tick: one camera, why the camera standing still; the parts
   dropped are the ones the log names; and st18a rasterised at the native
   frame's pixels (688 x 448, the nearest surface) has every pixel covered,
   none of them by a dropped part: the dark strips between the wall slabs
   in the picture are the stage's own faces, not a part the cull left out */
static const float kCaptureView[16] = {-0.336689532f, 0.561595857f, -0.755716801f, 0.0f,
                                       0.0f,          0.802602112f, 0.596437752f,  0.0f,
                                       0.941583395f,  0.200814337f, -0.2702277f,   0.0f,
                                       3036.56055f,   -478.052185f, 752.265686f,   1.0f};

#define CAPTURE_ZOOM 382.074646f

static void checkStageCapture(void)
{
    static const char *const kWant[3] = {"3 8 10 13 14 15 16 17 18 19 20", "2 3 5 6",
                                         "0 1 2 3 4 5 6 7 8 11 12 16 17 18 19 20 21"};
    unsigned char kept[512];
    int partAt[STAGE_MODELS + 1];
    setOptions(60, 3840, 2160);
    CHECK(gsFrame.w == 688, "capture: the native width %d, want 688", gsFrame.w);
    for (int k = 0; k < 2; k++) {
        gsb_SetVSMatrix(ScreenWidth, ScreenHeight, distanceFor(CAPTURE_ZOOM));
        memcpy(matrixptr + 0x80, kCaptureView, sizeof(kCaptureView));
        gsb_MakeCommonMatrix();
        if (k == 0) {
            tick();
        }
    }
    registerStage(kept, 0);
    CHECK(gsb_HostCullViewsUsed() == 1 && gsb_HostCullOneWhy() == GSB_CULL_ONE_STILL,
          "capture: one camera, the camera standing still (%d, why %d)", gsb_HostCullViewsUsed(),
          gsb_HostCullOneWhy());
    partAt[0] = 0;
    for (int m = 0; m < s_stageCount; m++) {
        partAt[m + 1] = partAt[m] + s_stage[m].parts;
        if (m >= 3) {
            continue;
        }
        char got[256];
        size_t n = 0;
        got[0] = 0;
        for (int j = 0; j < s_stage[m].parts; j++) {
            if (!kept[partAt[m] + j] && n + 8 < sizeof(got)) {
                n += (size_t)snprintf(got + n, sizeof(got) - n, n ? " %d" : "%d", j);
            }
        }
        CHECK(strcmp(got, kWant[m]) == 0, "capture: %s drops %s, want the log's %s",
              s_stage[m].name, got, kWant[m]);
    }
    /* the nearest surface of every triangle (s_dAll) and of the registered
       parts' (s_dNow), at the frame's pixels */
    double v[16], pr[16], sv[16];
    for (int i = 0; i < 16; i++) {
        v[i] = kCaptureView[i];
        pr[i] = ((const float *)(matrixptr + 0xC0))[i];
    }
    rdcb_mul4d(pr, v, sv);
    const int fw = gsFrame.w, fh = ScreenHeight;
    const double fx = 2048.0 - fw / 2, fy = 2048.0 - fh / 2;
    for (int i = 0; i < fw * fh; i++) {
        s_dAll[i] = s_dNow[i] = 0.0f;
    }
    for (int i = 0; i < s_triCount; i++) {
        double h[12][4];
        triClip(sv, (const float (*)[3])s_tri[i], h);
        const int n = cutDepth(h, 3);
        if (n >= 3) {
            const int reg = kept[partAt[s_triOf[i][0]] + s_triOf[i][1]];
            rasterPoly(h, n, fx, fy, fw, 0, 0, fw - 1, fh - 1, reg ? 1 | 4 : 1, i);
        }
    }
    int empty = 0, holes = 0;
    for (int i = 0; i < fw * fh; i++) {
        empty += !(s_dAll[i] > 0.0f);
        holes += farther(s_dAll[i], s_dNow[i]);
    }
    printf("gsb_cull_test: capture frame 22117: %d of %d frame pixels without a st18a surface, %d "
           "whose nearest surface is a dropped part's\n",
           empty, fw * fh, holes);
    CHECK(empty == 0 && holes == 0,
          "capture: %d pixels without a surface, %d whose nearest surface the cull dropped", empty,
          holes);
}

/* The loader against the game's own meshes: the capture frame
   4219 (frame-20261012-170845-v8447) drew st18a_p1 parts 0, 1, 2, 4 to 7, 9
   to 13, whose VU meshes (rd_replay_tool --mesh st18a_p1) hold 43,146
   indices, 14,382 triangles: the triangles with an area of those parts.
   Walking the strips to a count of -1 gave 52,145 there (loadModel) */
static void checkStageLoader(void)
{
    static const unsigned char kDrawn[] = {0, 1, 2, 4, 5, 6, 7, 9, 10, 11, 12, 13};
    int n = 0;
    for (int i = 0; i < s_triCount; i++) {
        if (s_triOf[i][0] != 0) {
            continue;
        }
        for (size_t k = 0; k < sizeof(kDrawn); k++) {
            n += s_triOf[i][1] == kDrawn[k];
        }
    }
    printf("gsb_cull_test: stage loader: %d triangles with an area, %d of them in the parts of "
           "st18a_p1 frame 4219 drew\n",
           s_triCount, n);
    CHECK(strcmp(s_stage[0].name, "st18a_p1") == 0 && n == 14382,
          "stage loader: %s's drawn parts hold %d triangles, the frame's meshes 14382",
          s_stage[0].name, n);
}

/* Two captures at the slit place while the camera
   moved (frames 4219 and 4481: frame-20261012-170845-v8447 and -170855-
   v8971, 3840 x 2160 at 16:9, Full pixel, half height, framerate uncapped,
   mirror mode): their views from the dumps' headers, and the camera of the
   picture on the screen at the second F12 (its PNG is the DISPLAY of the
   last present, rd_dump_on_demand's rd_read_display: a picture between the
   ticks), fitted to that picture from the tick's replay (2,101 matched
   points, 0.2 GS pixel rms): the tick's view turned 3.0 degrees about the
   same eye.  The camera at this place stands at one eye (1859.4, -65.0,
   -2560.0: every st18a dump there, i76 frame 5553 and the frames 6856,
   22117, 4219 and 4481) and only turns.  Checked: at each tick the parts
   the log names dropped are dropped by this tick's camera alone (the
   three cameras keep more), and every frame pixel's nearest st18a surface
   is a part the frame drew, none behind the eye; at the picture between
   the ticks, every pixel's nearest surface is a part the tick drew; the
   cull over the turn between the two capture views (10.6 degrees, one
   tick each way, three cameras) registers every part a picture between
   shows; and no triangle has a vertex behind the blended eye that is in
   front of both ticks' eyes (a turn about one eye blends no eye: rule 12
   cannot drop a triangle between the ticks here).  The slits in these
   pictures are the stage's own faces, drawn at every tick and between. */
static const float kSlitView[3][16] = {
    {-0.140554786f, 0.477280736f, -0.867376029f, 0.0f, 0.0f, 0.876084864f, 0.48207283f, 0.0f,
     0.990059435f, 0.0677576363f, -0.123137914f, 0.0f, 2795.9397f, -657.116272f, 1329.01965f, 1.0f},
    {0.0344097987f, 0.532462537f, -0.845698893f, 0.0f, 0.0f, 0.846202791f, 0.532779813f, 0.0f,
     0.99940455f, -0.0183328446f, 0.0291176662f, 0.0f, 2494.50977f, -982.064819f, 1681.78357f,
     1.0f},
    {-0.015146943f, 0.540823402f, -0.840944483f, 0.0f, 0.000223666723f, 0.84104551f, 0.540884361f,
     0.0f, 0.999881891f, 0.00800695662f, -0.012863849f, 0.0f, 2588.2667f, -930.657541f, 1565.90775f,
     1.0f}};

/* the parts each capture's log names dropped (st18a_p1, p2, p3) */
static const char *const kSlitDrops[2][3] = {
    {"3 8 14 15 16 17 18 19 20", "2 3 5 6", "2 3 7 10 11 12 14 16 17 18 19 20 21"},
    {"3 8 10 13 14 15 16 17 18 19 20", "1 2 3 5 6", "0 1 2 3 5 6 7 10 11 12 13 17 18 19 20 21"}};

/* the camera v standing still (one camera) at the dump's zoom */
static void slitCamera(const float *v)
{
    for (int k = 0; k < 2; k++) {
        gsb_SetVSMatrix(ScreenWidth, ScreenHeight, distanceFor(CAPTURE_ZOOM));
        memcpy(matrixptr + 0x80, v, 16 * sizeof(float));
        gsb_MakeCommonMatrix();
        if (k == 0) {
            tick();
        }
    }
}

/* drawn[part] from a capture's drop lists: 1 for a part the frame drew */
static void slitDrawn(int cap, unsigned char *drawn, const int *partAt)
{
    for (int m = 0; m < s_stageCount; m++) {
        for (int j = 0; j < s_stage[m].parts; j++) {
            drawn[partAt[m] + j] = 1;
        }
        if (m >= 3) {
            continue;
        }
        const char *p = kSlitDrops[cap][m];
        while (*p) {
            char *end;
            const long j = strtol(p, &end, 10);
            if (end == p) {
                break;
            }
            if (j >= 0 && j < s_stage[m].parts) {
                drawn[partAt[m] + j] = 0;
            }
            p = end;
        }
    }
}

/* st18a at view v at the native frame's pixels: every triangle's nearest
   surface against the drawn parts' less the triangles the region rule drops
   there for a vertex behind the eye (eyeDrops); the pixels without a
   surface, those whose nearest surface the drawn set lacks, the drops */
static void slitPicture(const float *v, const unsigned char *drawn, const int *partAt, int *empty,
                        int *holes, int *eyeDropped)
{
    double vd[16], pr[16], sv[16];
    for (int i = 0; i < 16; i++) {
        vd[i] = v[i];
        pr[i] = ((const float *)(matrixptr + 0xC0))[i];
    }
    rdcb_mul4d(pr, vd, sv);
    const int fw = gsFrame.w, fh = ScreenHeight;
    const double fx = 2048.0 - fw / 2, fy = 2048.0 - fh / 2;
    for (int i = 0; i < fw * fh; i++) {
        s_dAll[i] = s_dNow[i] = 0.0f;
    }
    *empty = *holes = *eyeDropped = 0;
    for (int i = 0; i < s_triCount; i++) {
        double h[12][4];
        triClip(sv, (const float (*)[3])s_tri[i], h);
        const int dropped = eyeDrops((const double (*)[4])h);
        *eyeDropped += dropped;
        const int n = cutDepth(h, 3);
        if (n >= 3) {
            const int reg = drawn[partAt[s_triOf[i][0]] + s_triOf[i][1]] && !dropped;
            rasterPoly(h, n, fx, fy, fw, 0, 0, fw - 1, fh - 1, reg ? 1 | 4 : 1, i);
        }
    }
    for (int i = 0; i < fw * fh; i++) {
        *empty += !(s_dAll[i] > 0.0f);
        *holes += farther(s_dAll[i], s_dNow[i]);
    }
}

static void checkStageSlit(void)
{
    unsigned char kept[512], drawn[512];
    int partAt[STAGE_MODELS + 1];
    setOptions(ICO_FRAMERATE_UNCAPPED, 3840, 2160);
    CHECK(gsFrame.w == 688, "slit: the native width %d, want 688", gsFrame.w);
    partAt[0] = 0;
    for (int m = 0; m < s_stageCount; m++) {
        partAt[m + 1] = partAt[m] + s_stage[m].parts;
    }
    if (partAt[s_stageCount] > 512) {
        CHECK(0, "slit: %d parts", partAt[s_stageCount]);
        return;
    }
    Cam cams[3];
    for (int c = 0; c < 3; c++) {
        cams[c] = camOfView(kSlitView[c]);
        CHECK(fabs(cams[c].e[0] - 1859.4) < 1.0 && fabs(cams[c].e[1] + 65.0) < 1.0 &&
                  fabs(cams[c].e[2] + 2560.0) < 1.0,
              "slit: camera %d's eye (%g, %g, %g), want the place's (1859.4, -65, -2560)", c,
              cams[c].e[0], cams[c].e[1], cams[c].e[2]);
    }
    for (int cap = 0; cap < 2; cap++) {
        slitCamera(kSlitView[cap]);
        registerStage(kept, 0);
        slitDrawn(cap, drawn, partAt);
        int extra = 0;
        for (int i = 0; i < partAt[s_stageCount]; i++) {
            CHECK(drawn[i] || !kept[i],
                  "slit %d: part %d the log dropped is kept by this tick's "
                  "camera alone",
                  cap, i);
            extra += drawn[i] && !kept[i];
        }
        int empty, holes, eyeDropped;
        slitPicture(kSlitView[cap], drawn, partAt, &empty, &holes, &eyeDropped);
        printf("gsb_cull_test: slit capture %d (frame %s): this tick's camera alone drops %d parts "
               "more than the three cameras did; %d of %d frame pixels without a st18a surface, "
               "%d whose nearest surface the frame did not draw; %d triangles dropped behind the "
               "eye\n",
               cap, cap ? "4481" : "4219", extra, empty, gsFrame.w * ScreenHeight, holes,
               eyeDropped);
        CHECK(holes == 0 && eyeDropped == 0,
              "slit %d: %d pixels whose nearest surface the frame did not draw, %d triangles "
              "dropped behind the eye",
              cap, holes, eyeDropped);
    }
    /* the picture on the screen at the second F12, between the ticks:
       every pixel's nearest surface is one of the tick's parts */
    {
        int empty, holes, eyeDropped;
        slitDrawn(1, drawn, partAt);
        slitCamera(kSlitView[2]);
        slitPicture(kSlitView[2], drawn, partAt, &empty, &holes, &eyeDropped);
        printf("gsb_cull_test: slit, the picture between the ticks (frame 4481's DISPLAY): %d "
               "pixels without a st18a surface, %d whose nearest surface the tick did not draw, "
               "%d triangles dropped behind the eye\n",
               empty, holes, eyeDropped);
        CHECK(holes == 0 && eyeDropped == 0,
              "slit, between the ticks: %d pixels the tick's parts leave, %d triangles dropped "
              "behind the eye",
              holes, eyeDropped);
    }
    /* the turn between the two capture views as one tick, each way: the
       three cameras' cull holds every picture between, and no triangle in
       front of both ticks' eyes is behind the blended one */
    SweepStats st;
    memset(&st, 0, sizeof(st));
    sweepPair("slit 4219 to 4481", &cams[0], &cams[0], &cams[1], CAPTURE_ZOOM, CAPTURE_ZOOM, 0,
              &st);
    sweepPair("slit 4481 to 4219", &cams[1], &cams[1], &cams[0], CAPTURE_ZOOM, CAPTURE_ZOOM, 0,
              &st);
    long behindBetween = 0;
    double v0[16], v1[16];
    for (int i = 0; i < 16; i++) {
        v0[i] = kSlitView[0][i];
        v1[i] = kSlitView[1][i];
    }
    for (int k = 1; k < 16; k++) {
        double vt[16];
        if (!rdcb_blend_view(v0, v1, k / 16.0, vt)) {
            CHECK(0, "slit: no blended view at t %g", k / 16.0);
            continue;
        }
        for (int i = 0; i < s_triCount; i++) {
            for (int c = 0; c < 3; c++) {
                double p[3] = {s_tri[i][c][0], s_tri[i][c][1], s_tri[i][c][2]}, a[4], b[4], o[4];
                xform(v0, p, a);
                xform(v1, p, b);
                xform(vt, p, o);
                behindBetween += a[2] > 0.0 && b[2] > 0.0 && o[2] <= 0.0;
            }
        }
    }
    printf("gsb_cull_test: slit, the turn between the captures (%d pairs, %d on three cameras, "
           "%d pictures): parts a picture shows drawn as the tick before's %d, not drawn %d; "
           "vertices behind the blended eye in front of both ticks' %ld\n",
           st.pairs, st.threeCams, st.pictures, st.hidden, st.gaps, behindBetween);
    CHECK(st.pictures == 30 && st.threeCams == 2 && st.gaps == 0 && st.hidden == 0 &&
              behindBetween == 0,
          "slit: the turn between the captures (%d pictures, %d on three cameras): %d parts a "
          "picture shows not registered, %ld vertices behind the blended eye only",
          st.pictures, st.threeCams, st.gaps + st.hidden, behindBetween);
}

static int checkStage(const char *disc)
{
    if (!loadStage(disc)) {
        printf("gsb_cull_test: SKIP the stage cases (no disc image at %s)\n",
               disc && disc[0] ? disc : "(none)");
        return 77;
    }
    int parts = 0, tris = 0;
    for (int m = 0; m < s_stageCount; m++) {
        for (int j = 0; j < s_stage[m].parts; j++) {
            tris += s_stage[m].part[j].tris;
        }
        parts += s_stage[m].parts;
    }
    printf("gsb_cull_test: stage st18a: %d models, %d parts, %d triangles\n", s_stageCount, parts,
           tris);
    CHECK(s_stageCount >= 3 && tris > 1000, "stage: st18a from the disc");
    CHECK(flattenStage() > 1000, "stage: st18a's triangles");
    checkStageLoader();
    checkStageDump();
    checkStageCapture();
    checkStageSlit();
    if (s_eyeWanted) {
        /* the frame is the picture (the shown-tick rule): the eye sweep only */
        checkStageSweep("4:3", 640, 480, 512);
    }
    checkStageSweep("16:9", 1920, 1080, 688);
    checkStageSweep("48:9", 4800, 900, 2048);
    return 0;
}

/* ---------------------------------------------------------------- main */

static void boot(void)
{
    matrixptr = s_spr;
    memset(s_spr, 0, sizeof(s_spr));
    systemStatus[0] = 0; /* NTSC: 512 x 448 */
    systemStatus[1] = 2;
    GlobalStageSetting.viewScale = 100;
    game_pause = 1;
    gsb_InitGSSystem();
}

int main(int argc, char **argv)
{
    rd__set_not_implemented_fatal(true);
    if (!rd__init_record_only(512, 448)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    boot();
    CHECK(ScreenWidth == 512 && ScreenHeight == 448, "the NTSC frame (%d x %d)", ScreenWidth,
          ScreenHeight);

    if (argc > 2 && strcmp(argv[1], "--stage") == 0) {
        s_eyeWanted = argc > 3 && strcmp(argv[3], "--eye") == 0;
        /* the disc's stage (ctest gsb_cull_stage): 77 without the disc */
        const int r = checkStage(argv[2]);
        rd_shutdown();
        if (r == 77) {
            return 77;
        }
        if (failures) {
            printf("gsb_cull_test: %d failures\n", failures);
            return 1;
        }
        printf("gsb_cull_test: stage ok\n");
        return 0;
    }

    checkMoving("4:3", 640, 480, 512);
    checkMoving("32:9", 3200, 900, 1376);
    checkMoving("the widest", 8000, 900, 2560);
    checkCorner();
    checkCut();
    checkStill();
    checkFirst();
    checkOriginal();
    checkFramerates();
    checkLocked();

    checkDotWindow("4:3", 640, 480, 6400);
    checkDotWindow("32:9", 3200, 900, (688 + 144) * 16);
    checkDotWindow("the widest", 8000, 900, 22784);
    checkDotEarlier();
    /* blended pictures: W / 2 times 1 + GSB_CULL_MARGIN (1.12), so a
     * line's cut end lies past the frame's edge while frames blend */
    checkLines("4:3", ICO_FRAMERATE_UNCAPPED, 640, 480, 256.0f * 1.12f);
    checkLines("32:9", ICO_FRAMERATE_UNCAPPED, 3200, 900, 688.0f * 1.12f);
    checkLines("the widest", ICO_FRAMERATE_UNCAPPED, 8000, 900, 1280.0f * 1.12f);
    /* the Original framerate: W / 2 exactly, 256 at 4:3 (the PS2's) */
    checkLines("4:3 Original", ICO_FRAMERATE_ORIGINAL, 640, 480, 256.0f);
    checkLines("32:9 Original", ICO_FRAMERATE_ORIGINAL, 3200, 900, 688.0f);
    checkShadows("4:3", 640, 480, 512.0f);
    checkShadows("32:9", 3200, 900, 1376.0f);
    checkShadows("48:9", 4800, 900, 2046.0f);
    checkShadows("the widest", 8000, 900, 2046.0f);
    checkMargin();
    checkSpanNear();
    checkBankFill();

    rd_shutdown();
    if (failures) {
        printf("gsb_cull_test: %d failures\n", failures);
        return 1;
    }
    printf("gsb_cull_test: ok\n");
    return 0;
}
