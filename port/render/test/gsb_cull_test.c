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
 *   moving   at k = 1, 2.67 (32:9) and 5 (the widest: aspect "auto" in a
 *            window wider than 60:9, clamped to ICO_ASPECT_MAX), pairs of
 *            cameras a tick apart, the eye moved up to 300 units in any
 *            direction and the view turned up to 30 degrees (yaw and
 *            pitch), as far as the presenter blends (rd_camera_blend.h
 *            RD_INTERP_CAMERA_MOVE, RD_INTERP_CAMERA_TURN); boxes around
 *            points the pictures at t = 0.25, 0.5 and 0.75 show (the turn
 *            slerped and the eye lerped, as rd_interp.c camSetup, computed
 *            here on their own), many of them at the picture's edges and
 *            corners: none is culled (gsb_ClipBox 0).  The worst miss (how
 *            far inside the in-between picture the shown point of a culled
 *            box was, as a share of the half-width or half-height) is
 *            printed, 0 when there is none, with how many boxes only an
 *            in-between picture showed and how many lay outside the three
 *            cameras' own frustums (kept by the eye's sweep alone).  Boxes
 *            whose bounding sphere, grown by the eye's step, lies past one
 *            plane of each of the three cull frustums are culled; -1 (no
 *            per-triangle test) only for a box every corner of which is
 *            inside all three cull frustums.
 *   corner   the case three cameras alone miss: the eye moving 300 units
 *            along the view's +x and +y together, a box in the picture's
 *            -x, +y corner (left, and down on the screen) at t = 0.25, 300
 *            units out, outside the three cameras' frustums: kept by the
 *            eye's sweep.
 *   cut      a cut the presenter does not blend over: the eye moved 2000
 *            units and the view turned 90 degrees, or a hard cut the game
 *            signalled (ico_video_camera_cut) since the flip: one camera,
 *            every result the PS2's.  At the limits: a step of 299 units or
 *            a turn of 29 degrees three cameras, 301 units or 31 degrees
 *            one.
 *   still    the camera the same matrix bitwise as the tick before: one
 *            camera, every result the PS2's single-camera test.
 *   first    a stage's first tick (gsb_InitGSSystem) and after
 *            gsb_ResetGSSystem: one camera.
 *   original the Original framerate (nothing blended): one camera, while
 *            the camera moves.
 *   locked   a part locked to the camera (gsb_HostCullCameraLocked, node
 *            flag 2): one camera.
 *   dots     the water dots' window (gsb_HostDotVisible) at k = 1, 2.67,
 *            5: (256 k + 144) pixels across and (224 + 144) up and down
 *            about the centre, 400 across at 4:3 (the PS2's), its edges
 *            in, one 1/16 pixel past them out, the widest inside 16 bits;
 *            a dot only the tick before's camera saw, its GS position
 *            outside the window but inside 16 bits, kept while the
 *            camera moves, and not past 16 bits.
 *   lines    lineManager.c's x clip (gsb_HostLineHalfWidth): 256 k pixels,
 *            exactly 256 at 4:3.
 *   shadows  Shadow.c's edge clip (gsb_HostShadowClipX): 512 k pixels, at
 *            most 2047 (2560 at k = 5 would leave the 16-bit window).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eeregs.h>
#include <libvu0.h>
#include "rd_internal.h"
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

/* the picture's half-width and half-height in GS pixels */
static double halfW(void)
{
    return (double)(ScreenWidth / 2) * (double)ico_video_wide_x();
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

/* the PS2's test (gsb_ClipBox's own code) on the current matrix: the
   reference for the cases with one camera */
static int refClip(const Box *b)
{
    int all = 0x3F, any = 0, nearC = 0;
    for (int i = 0; i < 8; i++) {
        float r[4];
        ico_apply_matrix_w1(r, (const float (*)[4])ico_current_matrix, b->v[i]);
        const float w = fabsf(r[3]);
        int f = 0;
        f |= (r[0] > w) << 0;
        f |= (r[0] < -w) << 1;
        f |= (r[1] > w) << 2;
        f |= (r[1] < -w) << 3;
        f |= (r[2] > w) << 4;
        f |= (r[2] < -w) << 5;
        all &= f;
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
   through aspect "auto" and a window of winW x winH */
static void setOptions(int framerate, int winW, int winH)
{
    IcoVideoOptions o;
    ico_video_defaults(&o);
    o.framerate = framerate;
    o.aspect = ICO_ASPECT_AUTO;
    ico_video_set(&o);
    ico_video_set_window(winW, winH);
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

/* ------------------------------------------------------------ moving */

#define PAIRS 240
#define BOXES 120
#define OUTSIDE 40

static void checkMoving(const char *what, int winW, int winH)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, winW, winH);
    const float k = ico_video_wide_x();
    int tested = 0, missed = 0, between = 0, sweepNeeded = 0, outside = 0, outsideKept = 0;
    int minusBad = 0, threeViews = 0;
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
            outside++;
            outsideKept += clipBox(&bx) != 0;
        }
    }
    printf("gsb_cull_test: %s (k %.3f): %d boxes the in-between pictures show, %d of them "
           "only those, %d outside the three cameras' frustums (kept by the eye's sweep); "
           "culled %d, the worst %.4f of the half-width inside\n",
           what, (double)k, tested, between, sweepNeeded, missed, worst);
    CHECK(missed == 0, "moving %s: %d boxes an in-between picture shows culled (worst %.4f)", what,
          missed, worst);
    CHECK(tested > PAIRS * BOXES / 2 && between > 0,
          "moving %s: %d boxes tested, %d only in between", what, tested, between);
    CHECK(threeViews > tested / 2, "moving %s: three cameras for %d of %d boxes", what, threeViews,
          tested);
    CHECK(outside > 0 && outsideKept == 0,
          "moving %s: %d of %d boxes outside every cull frustum kept", what, outsideKept, outside);
    CHECK(minusBad == 0, "moving %s: %d boxes -1 with a corner outside a cull frustum", what,
          minusBad);
}

/* the eye moving 300 units along the view's +x and +y together, a box in
   the picture's -x, +y corner at t = 0.25, 300 units out: outside the three
   cameras' frustums (the tick before's misses it above or below, the
   half-way and this tick's at the side), inside the swept ones */
static void checkCorner(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 640, 480);
    Cam a, b;
    camTurn(&a, 0.0, 0.0);
    a.e[0] = a.e[1] = a.e[2] = 0.0;
    b = a;
    b.e[0] = b.e[1] = 300.0 / sqrt(2.0);
    const Cam c = camAt(&a, &b, 0.25), mid = camAt(&a, &b, 0.5);
    setCamera(&a);
    tick();
    setCamera(&b);
    readProjections();
    const double tdx = halfW() / s_screen[0], tdy = halfH() / s_screen[5];
    const double centre[3] = {c.e[0] - 0.98 * tdx * 300.0, c.e[1] + 0.98 * tdy * 300.0, 300.0};
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
       degrees */
    CHECK(viewsAcross(299.0, 0.0) == 3, "cut: a step of 299 units culls against 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(301.0, 0.0) == 1, "cut: a step of 301 units culls against 1 camera (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(0.0, 29.0) == 3, "cut: a turn of 29 degrees culls against 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(0.0, 31.0) == 1, "cut: a turn of 31 degrees culls against 1 camera (%d)",
          gsb_HostCullViewsUsed());
    CHECK(viewsAcross(299.0, 29.0) == 3, "cut: both just inside the limits: 3 cameras (%d)",
          gsb_HostCullViewsUsed());
    /* a hard cut the game signalled: one camera until the next flip */
    {
        Cam a, b;
        camTurn(&a, -0.3, 0.0);
        a.e[0] = a.e[1] = a.e[2] = 0.0;
        b = nextCam(&a, -0.3, 0.0, 0);
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
    const Cam b = nextCam(&a, yaw, pitch, 0);
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

static void checkLocked(void)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, 640, 480);
    Cam a = randomCam();
    camTurn(&a, -0.7, 0.05);
    const Cam b = nextCam(&a, -0.7, 0.05, 0);
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
    const int ref = refClip(&bx);
    gsb_HostCullCameraLocked(0);
    gsb_ClipBox(&bx.v[0][0]);
    CHECK(lockedViews == 1 && locked == ref,
          "locked: one camera, the PS2's result (%d cameras, %d, %d)", lockedViews, locked, ref);
    CHECK(gsb_HostCullViewsUsed() == 3, "locked: a world part again culls against 3 cameras (%d)",
          gsb_HostCullViewsUsed());
}

/* ------------------------------------------------------------- windows */

/* the dot window's half-width and half-height, 1/16 pixels */
static int dotHalfX(void)
{
    return (int)(((float)(ScreenWidth / 2) * ico_video_wide_x() + 144.0f) * 16.0f);
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

static void checkLines(const char *what, int winW, int winH, float want)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, winW, winH);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 512.0f);
    const float hw = gsb_HostLineHalfWidth();
    CHECK(fabsf(hw - want) <= 1e-3f * want && 2048.0f + hw <= 4095.0f,
          "lines %s: half-width %g, want %g", what, (double)hw, (double)want);
}

static void checkShadows(const char *what, int winW, int winH, float want)
{
    setOptions(ICO_FRAMERATE_UNCAPPED, winW, winH);
    const float x = gsb_HostShadowClipX();
    CHECK(fabsf(x - want) <= 1e-3f * want && 2048.0f + x < 4096.0f,
          "shadows %s: edge clip %g, want %g", what, (double)x, (double)want);
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

int main(void)
{
    rd__set_not_implemented_fatal(true);
    if (!rd__init_record_only(512, 448)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    boot();
    CHECK(ScreenWidth == 512 && ScreenHeight == 448, "the NTSC frame (%d x %d)", ScreenWidth,
          ScreenHeight);

    checkMoving("4:3", 640, 480);
    checkMoving("32:9", 3200, 900);
    checkMoving("the widest", 8000, 900);
    checkCorner();
    checkCut();
    checkStill();
    checkFirst();
    checkOriginal();
    checkLocked();

    checkDotWindow("4:3", 640, 480, 6400);
    checkDotWindow("32:9", 3200, 900, -1);
    checkDotWindow("the widest", 8000, 900, 22784);
    checkDotEarlier();
    checkLines("4:3", 640, 480, 256.0f);
    checkLines("32:9", 3200, 900, 256.0f * 32.0f / 9.0f * 0.75f);
    checkLines("the widest", 8000, 900, 1280.0f);
    checkShadows("4:3", 640, 480, 512.0f);
    checkShadows("32:9", 3200, 900, 512.0f * 32.0f / 9.0f * 0.75f);
    checkShadows("48:9", 4800, 900, 2047.0f);
    checkShadows("the widest", 8000, 900, 2047.0f);

    rd_shutdown();
    if (failures) {
        printf("gsb_cull_test: %d failures\n", failures);
        return 1;
    }
    printf("gsb_cull_test: ok\n");
    return 0;
}
