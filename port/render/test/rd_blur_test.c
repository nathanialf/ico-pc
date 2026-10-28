/* rd_blur_test.c: the full-screen effects of staticBlur.c on rd.
 *
 * staticBlur.c with the 2D layer (GifPacket.c, DisplayList.c, DmaPacket.c)
 * and Matrix.c, compiled as the window build has them (ICO_HOST, ICO_RD);
 * the rest of the game is stubbed below (the sun fans draw nothing).  No
 * disc data.
 *
 * CPU only (r): every entry point is driven and its recording checked:
 *   - every sprite is an rd_post of its effect's kind, nothing goes through
 *     the register decoder (no RDC_SCREEN in lists 7 and 8, nothing
 *     undecoded), and every work buffer a sprite samples was drawn earlier
 *     in the frame (FEED128, SCENE and DISPLAY excepted);
 *   - the buffers: WORK0/WORK1 ping-pong in the flare blur and the depth of
 *     field, WORK1 256 x 256 in copyToWork, WORK2 and AURA_WORK
 *     scene-sized with SCENE's depth, AURA_TAP 128 x 128, FEED128;
 *   - TFX HIGHLIGHT on the blur passes, TEXA and DISPLAY's RGB24 view in
 *     the motion blur, the H/2 -> H stretch for 448 and 512 lines, the
 *     depth of field's four planes (FIX 32, 64, 96 with ABE, 128 without,
 *     Z GEQUAL), and the state left behind.
 * On a Vulkan device (exit 77 without one, after the CPU checks), every
 * frame is also run through a CPU model of the GS arithmetic (the same
 * rules as fx_sprite_ps: coverage, the 12.4 UV step, nearest or the 4-bit
 * bilinear with TEXA before filtering, TFX, alpha test, DATE, the integer
 * blend, the Z test) over the recorded commands, and every target the
 * effects touch is compared with the GPU's:
 *   e  one frame per post mode 1..7 (flare, glow, backlight, depth of
 *      field, combinations; the sun on for the eye blur's ghosts and the
 *      DATE pass), and a dump -> load -> replay of one of them;
 *   m  600 frames of motion blur feedback (DISPLAY -> SCENE, then SCENE
 *      reduced into DISPLAY by rd_post(RD_POST_REDUCTION), tinted, the
 *      whole loop in the model): FIX 0x40 on a static image,
 *      FIX 0x40 on noise, FIX 0x70 with cuts (the cases of blend exactness under feedback);
 *   a  600 frames of the aura feedback through FEED128, 200 each of modes
 *      1 (aura), 2 (mirage) and 3 (aura v2), blurCol alpha 0x20 then 0x40;
 *   c  mirage ticks at alpha 128, one of them a camera cut (GlobalTimer),
 *      each replayed as the interpolating presenter does, as a tick's first
 *      present and as a later one: both draw the same SCENE and leave the
 *      same FEED128 (the later present of the cut drew black before
 *      FEED_HELD).
 *   f  a sprite at ALPHA LERP FIX 128 (Cs exactly), which the replay draws
 *      without ABE and without its snapshot: each frame replayed with the
 *      skip on and off, SCENE the model's both ways, one copy fewer only
 *      where the skip applies (not under DATE or AFAIL RGB_ONLY, nor FIX 127).
 *   n  the motion blur with the aura's glow and a tint
 *      of 0x98 over a still picture, each tick presented 1, 2, 4 and 10
 *      times (30 Hz ticks at 30, 60, 120, 300 presents a second): every
 *      tick's DISPLAY is the bytes of one replay a tick (Original), and the
 *      presents as drawn without the held DISPLAY copy (each feeding back
 *      the present before, FIX 128 (FIX / 128)^(1 / N)) run brighter, to
 *      white at 10; then on
 *      the GPU at Enhanced 2x with the full-height scene, DISPLAY_HELD is
 *      DISPLAY's size and 10 presents a tick give one present's DISPLAY.
 *   q  the mirage as the Queen's F12 dumps record it: NTSC,
 *      feedbackCol (64, 64, 64, 128), the sprites' rectangles, UVs, TEX0
 *      sizes, colours and modes checked against the dumps', list 8's shine
 *      material into AURA_WORK with SCENE's depth (TEST 0x5346D, Z write)
 *      behind a nearer band, twelve frames of feedback against the model;
 *      then Enhanced 4x and 4x with the full-height scene, SCENE at the GS
 *      pixel centres against the 1x model: exact where the mask is 0 within
 *      16 GS pixels, within 4 where it is one value within 16, with the
 *      above-1x kernels drawing the chain's decimations and magnifications.
 *   p  the mirage frame of q with the shine a keyed world
 *      draw, presented as the presenter does (a tick's first present at
 *      alpha 0.5 and a later one at 1, the shine moving between ticks): the
 *      face band's interior (mask 0) is untouched and FEED128's alpha as the
 *      paste reads it is the mask, not SCENE's alpha, in every present.
 *   w  the native frame: CPU only, every effect recorded at
 *      W = 2048 (48:9) draws each buffer at its own GS width (the work
 *      buffers W / 2, W / 4 across, the frame-sized ones W), reads each
 *      with TEX0's TW of the source's width, keeps every rect and UV on
 *      its buffer, and covers as many buffers whole as at 4:3 (no 4:3
 *      extent left in a wider buffer); on the device the aura's feedback
 *      chain (AURA_WORK -> WORK0 -> FEED -> SCENE and back) in modes 2
 *      (the Queen's mirage), 1 and 3 at W = 512 and W = 2048, every target
 *      against the CPU model exactly, the 4:3 columns of the wide SCENE the
 *      512 run's byte for byte (mode 2 all of them; modes 1 and 3, whose
 *      taps spread one feedback texel a frame, away from the 4:3 edges by 4
 *      pixels a frame), and FEED's alpha as the paste reads it at most
 *      0x80 (the mask's, never extrapolating the paste); the motion blur's
 *      loop at W = 512, 688, 1376 and 2560 on the device, SCENE's and
 *      DISPLAY's 4:3 columns the 512 run's byte for byte (frames + 2
 *      pixels in from their edges); and the glow's left edge at W = 512,
 *      848 and 2048 (glow_edge_wide): the replay's strip over the columns
 *      the blur's first pass leaves in the model, SCENE the same over
 *      magenta and green work buffers (rd__set_work_clear) with it, and
 *      different at the left edge without it.
 *   x  above 1x: the aura's modes 1, 2 and 3 at Enhanced 4x (NTSC: SCENE
 *      4x, the work buffers at their GS sizes, as at every scale), every
 *      target against the CPU model at its own scale exactly, the
 *      decimations (SCENE and AURA_WORK into FEED128 and WORK0) the area
 *      box of above1x_ref.c and the magnifications (WORK0, FEED128 and
 *      AURA_TAP over the frame) the GS's own bilinear at each SCENE
 *      texel's GS position, never the B-spline; the replay flags as many
 *      sprites as the model resamples.
 *   g  the glow above 1x: post modes 1..7 as e runs them at Enhanced 2x
 *      (PAL), every target against the CPU model exactly: the flare's and
 *      the bloom's paste WORK3 -> SCENE and the depth of field's WORK0 ->
 *      SCENE the GS's bilinear at the finer grid, no B-spline.  Every
 *      comparison of a run at scale 1 (all the others) finds no sprite
 *      flagged.
 *   u  the RGBA8_UINT views: the work and scene-class targets made again
 *      with them (RdProfile.uintViewWork, uintViewScene), post modes 1..7
 *      as e runs them, every target against the CPU model exactly (so the
 *      bytes fx_sprite_ps writes), the sprites reading targets drawn
 *      through fx_sprite_u_ps (RdPerfRecord.uintDraws) and every pipeline
 *      created enumerated; then the targets made again without the views
 *      (skipped where the device has no view).
 *   b  the blur pairs folded (RdProfile.blurFold, fx_blur2_ps:
 *      rd_replay_tiler.c rd__replay_blur_fold): post modes 1..7 as e runs them with the
 *      fold on, every target against the CPU model of the two passes
 *      exactly (WORK0 and WORK1 too: blur(n), n = 10..19, nine pairs a
 *      chain folded and the last drawn as two passes), the folds counted
 *      (RdPerfRecord.blurFolds) and every pipeline created enumerated;
 *      then (blur_fold_wide) the flare at W 512, 688 (16:9), 2048 (48:9)
 *      and 2560 (60:9): 9, 9, 8 and 8 pairs folded, every target the
 *      model's (the glow's edge strip in it), the same frame replayed with
 *      the fold off the model's again (the folded and unfolded chains'
 *      bytes the same at every width), and with the strip off every
 *      target the model's (nine folds at 4:3).
 *   l  the frame-size post sprites' plan (rd_fx_plan.c, RdProfile.fxPlan, on
 *      in every other case): post modes 1..7 as e runs them and 40 motion
 *      blur frames as m runs them, every target against the CPU model
 *      exactly, the constant fold (cleanUpFB, the WORK0 fill) and the
 *      premultiplied paste counted (RdPerfRecord.fxConstant, fxAdditive);
 *      then, where the device has local read, the same with the scene
 *      passes readable (RdProfile.dateStencil) and fxDstLocal on: the
 *      motion blur's destination by local read (fxLocal), every target the
 *      model's, rd__fx_local_selftest passing, every pipeline created
 *      enumerated.  f holds the plan off: it counts the FIX 128 skip's
 *      snapshots alone.
 *   s  Motion blur: Smooth (rd_blur_smooth.c; rd_blur_smooth_test.inc says
 *      what): a still picture within 1 of Original's at 1, 4 and 10
 *      presents a tick, a moving bar's trail moved on at each present and
 *      its ghost after a tick within 2 of Original's, the restart after a
 *      tick without the motion blur holding no ghost; then (s2) at Enhanced
 *      2x with the full-height scene the accumulator at SCENE's size.
 * Tolerance 0 everywhere (but q's scaled runs and s).  Every pipeline created is enumerated; no
 * validation errors; no stubbed command replayed. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "shader_consts.h"
#include "vk/rhi_vk.h"
/* the game's side */
#include "typedef.h"
#include "DisplayList.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "Matrix.h"
#include "Primitive.h"
#include "staticBlur.h"
#include "debug.h"
#include "main.h"
#include "../../test/ico_shard.h"
#include "above1x_ref.h"

_Static_assert(RD_FX_BOX_MIN32 == A1X_BOX_MIN32 && RD_FX_BSPLINE_MAX32 == A1X_BSPLINE_MAX32 &&
                   RD_FX_STEP_MAX == A1X_STEP_MAX,
               "the replay's above-1x thresholds are the reference's");

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
int ScreenWidth = 512, ScreenHeight = 512;
/* GsBase.c's native frame (gsFrame, latched at the flip): 4:3 unless a case
   sets another width with setFrameWidth */
IcoFrame gsFrame = {512, 512, 8, 512.0f, 4.0f / 3.0f};
float center_X = 2048.0f, center_Y = 2048.0f;
int screenOffsetX, screenOffsetY;
int fbKeep;
void *ios_partition_common;
void *dmaVif;
char *matrixptr;
int debug_font_flag;
int debug_fullscreen_effect = 1;
/* video_options.c's effect switches, set by the cases below:
   glow, depth of field, softening, motion blur, fog */
static int s_fx[5] = {1, 1, 1, 1, 1};

int ico_video_effect_glow(void)
{
    return s_fx[0];
}

int ico_video_effect_depth_of_field(void)
{
    return s_fx[1];
}

int ico_video_effect_softening(void)
{
    return s_fx[2];
}

int ico_video_effect_motion_blur(void)
{
    return s_fx[3];
}

int ico_video_effect_fog(void)
{
    return s_fx[4];
}

int ico_video_effect_cinematic_bars(void)
{
    return 1;
}

/* photo_mode.c's state: off, the aura's feedback as in play */
int ico_photo_active(void)
{
    return 0;
}

int ico_photo_left(void)
{
    return 0;
}

StageSetting GlobalStageSetting;
PadState pad[16];
int systemStatus[12];
int GlobalTimer;
int currentScreenWidth;
/* main.c's; only the 2001 builds' MotionBlur twins read it */
int NonLinearCameraMove;
float ZeroPoint[4];

static unsigned char s_heap[8u << 20] __attribute__((aligned(16)));

static size_t s_heapAt;

static void *zalloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (s_heapAt + n > sizeof(s_heap)) {
        printf("test heap exhausted\n");
        abort();
    }
    void *p = s_heap + s_heapAt;
    s_heapAt += n ? n : 16;
    memset(p, 0, n);
    return p;
}

void *iosMallocDebug(void *part, int size, const char *file, int line)
{
    (void)part, (void)file, (void)line;
    return zalloc((size_t)size);
}

void iosFree(void *p)
{
    (void)p;
}

void *mallocseki(int size)
{
    return zalloc((size_t)size);
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...)
{
    (void)a, (void)b, (void)c, (void)fmt;
}

void debug_PrintfDummy(int x, int y, unsigned int col, const char *fmt, ...)
{
    (void)x, (void)y, (void)col, (void)fmt;
}

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

void mc_Reset(void) {}

float GetTableSin(short angle)
{
    return sinf((float)angle * (3.14159265f / 32768.0f));
}

float GetTableCos(short angle)
{
    return cosf((float)angle * (3.14159265f / 32768.0f));
}

void FlushCache(int op)
{
    (void)op;
}

void sceDmaSend(void *ch, void *addr)
{
    (void)ch, (void)addr;
}

/* Texture.c's resetVramPri selects the list it resets (dl_SetDLPriority,
   Texture.c:1804) */
static int s_lockBase;

void tex_LockHeadTBP(int tbp, int pri)
{
    s_lockBase = tbp;
    dl_SetDLPriority(pri);
}

void tex_UnlockHeadTBP(int pri)
{
    dl_SetDLPriority(pri);
}

void CopyVector(float *d, float *s)
{
    memcpy(d, s, 16);
}

/* the sun fans: Primitive.c is not linked; they draw nothing here */
static Fan2D s_fan;

static int s_fanDraws;

Fan2D *prim_InitFan2D(int n, float r, float *pos, unsigned int cc, unsigned int rc)
{
    (void)n, (void)r, (void)pos, (void)cc, (void)rc;
    return &s_fan;
}

void prim_SetFan2D(Fan2D *f, float r, float *pos, unsigned int cc, unsigned int rc)
{
    (void)f, (void)r, (void)pos, (void)cc, (void)rc;
}

void prim_DispFan2D(Fan2D *f, int mode)
{
    (void)f, (void)mode;
    s_fanDraws++;
}

/* the EE word arena (eeword.h) */
static unsigned char s_arena[1 << 12] __attribute__((aligned(16)));

unsigned char *ico_arena_cached_base = s_arena;

unsigned char *ico_arena_base(void)
{
    return s_arena;
}

int ico_arena_contains(const void *p, __SIZE_TYPE__ n)
{
    const unsigned char *c = p;
    return c >= s_arena && c + n <= s_arena + sizeof(s_arena);
}

/* the scratchpad matrices staticBlur.c reads: +0x80 view (the sun's
 * direction), +0xC0 screen (the depth of field's plane Z: GS Z = 2^16 z),
 * +0x100 world to screen (the sun on screen at (40, -30) from the centre) */
static float s_mtx[256] __attribute__((aligned(16)));

static void setMatrices(void)
{
    memset(s_mtx, 0, sizeof(s_mtx));
    matrixptr = (char *)s_mtx;
    float *view = s_mtx + 32, *screen = s_mtx + 48, *ws = s_mtx + 64;
    view[0] = view[5] = 1.0f;
    view[10] = -1.0f;
    view[15] = 1.0f;
    screen[10] = 4096.0f; /* v[2] * 16 = 65536 z */
    screen[15] = 1.0f;
    ws[12] = 2048.0f + 40.0f;
    ws[13] = 2048.0f - 30.0f;
    ws[15] = 1.0f;
}

#define W 512
#define H 512

/* ============================================================ helpers */

static const char *kindName(uint32_t k)
{
    switch (k) {
    case RD_POST_MOTION_BLUR:
        return "MOTION_BLUR";
    case RD_POST_DOF:
        return "DOF";
    case RD_POST_FLARE:
        return "FLARE";
    case RD_POST_BLOOM:
        return "BLOOM";
    case RD_POST_AURA:
        return "AURA";
    case RD_POST_EYE_BLUR:
        return "EYE_BLUR";
    case RD_POST_REDUCTION:
        return "REDUCTION";
    default:
        return "?";
    }
}

static int named(uint32_t id)
{
    return id != 0 && (id >> 16) == 0 && (id & 0xFFFF) <= RD_TARGET_COUNT ? (int)id - 1 : -1;
}

static const char *const kTargetNames[RD_TARGET_COUNT] = {
    "SCENE",    "DISPLAY",   "SHADOW0",   "SHADOW1",      "SHADOW2",   "WORK0",     "WORK1",
    "WORK2",    "WORK3",     "AA0",       "AA1",          "FEED128",   "DATE_SNAP", "AURA_WORK",
    "AURA_TAP", "WORK2_PAD", "FEED_HELD", "DISPLAY_HELD", "WORK3_HELD"};

static const char *tname(uint32_t id)
{
    int n = named(id);
    return n >= 0 ? kTargetNames[n] : "temp";
}

/* one sprite of a recorded frame, with the state at it */
typedef struct Spr {
    int list;
    uint32_t kind;
    RdStateBlock st;
    RdPostRec r;
} Spr;

#define MAX_SPR 512

static Spr s_spr[MAX_SPR];

static int s_nspr, s_screens78, s_others;

static void collect(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    const RdFrame *f = user;
    (void)index;
    if (c->type == RDC_POST_STUB && rd__is_blur_kind(c->b[0])) {
        if (s_nspr < MAX_SPR) {
            Spr *p = &s_spr[s_nspr++];
            p->list = list;
            p->kind = c->b[0];
            p->st = *s;
            memcpy(&p->r, f->payload + c->u[1], sizeof(p->r));
        }
    } else if (c->type == RDC_SCREEN && (list == 7 || list == 8)) {
        s_screens78++;
    } else if (!rd__cmd_is_state(c->type) && c->type != RDC_CLEAR && c->type != RDC_NOP) {
        s_others++;
    }
}

static void collectFrame(const RdFrame *f)
{
    s_nspr = s_screens78 = s_others = 0;
    RdStateBlock st = f->startState;
    rd__walk(f, 0, &st, collect, (void *)f);
}

static uint32_t texTarget(const RdStateBlock *s, uint8_t *view)
{
    if (!s->ds.texEnabled) {
        return 0;
    }
    const RdTexRec *t = rd__tex_rec(s->tex);
    if (!t || t->kind != RD_TEXKIND_TARGET) {
        return 0;
    }
    if (view) {
        *view = t->view;
    }
    return t->target;
}

/* ===================================================== (r) the recording */

static void setStage(int post, int feed, int alpha)
{
    GlobalStageSetting.postEffect = post;
    GlobalStageSetting.feedbackEffect = feed;
    GlobalStageSetting.feedbackCol[0] = 100;
    GlobalStageSetting.feedbackCol[1] = 110;
    GlobalStageSetting.feedbackCol[2] = 120;
    GlobalStageSetting.feedbackCol[3] = alpha;
    GlobalStageSetting.depthFieldStart = 100;
    GlobalStageSetting.depthFieldWidth = 400;
}

/* every sampled work buffer was drawn earlier in the frame */
static void checkReadAfterWrite(const char *what)
{
    int drawn[RD_TARGET_COUNT];
    memset(drawn, 0, sizeof(drawn));
    for (int i = 0; i < s_nspr; i++) {
        const Spr *p = &s_spr[i];
        const uint32_t t = texTarget(&p->st, NULL);
        const int n = named(t);
        if (t && n != RD_TARGET_SCENE && n != RD_TARGET_DISPLAY && n != RD_TARGET_FEED128) {
            CHECK(n >= 0 && drawn[n], "%s: sprite %d (%s) samples %s before anything drew it", what,
                  i, kindName(p->kind), tname(t));
        }
        const int d = named(p->st.color);
        CHECK(d >= 0, "%s: sprite %d draws into a temporary target", what, i);
        if (d >= 0) {
            drawn[d] = 1;
        }
    }
}

static void checkRecordingModes(void)
{
    static const struct {
        int post, feed;
    } kCases[] = {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {0, 1}, {0, 2}, {0, 3}};

    for (size_t k = 0; k < sizeof(kCases) / sizeof(kCases[0]); k++) {
        char what[64];
        snprintf(what, sizeof(what), "post %d feed %d", kCases[k].post, kCases[k].feed);
        setStage(kCases[k].post, kCases[k].feed, 64);
        const unsigned undec = gif_HostUndecodedTotal();
        FullScreenEffectBefore();
        FullScreenEffectAfter();
        CHECK(gif_HostUndecodedTotal() == undec, "%s: undecoded registers", what);
        dl_Swap();
        collectFrame(rd__last_frame());
        CHECK(s_screens78 == 0, "%s: %d screen prims in lists 7/8 (decoder path)", what,
              s_screens78);
        CHECK(s_others == 0, "%s: %d other actions", what, s_others);
        checkReadAfterWrite(what);
        int kinds[RD_POST_COUNT];
        memset(kinds, 0, sizeof(kinds));
        int hl = 0, w1big = 0, w2scene = 0, auraScene = 0, tap = 0, feedW = 0, planes = 0;
        for (int i = 0; i < s_nspr; i++) {
            const Spr *p = &s_spr[i];
            kinds[p->kind]++;
            const int c = named(p->st.color);
            if (p->r.lines == 2 && texTarget(&p->st, NULL)) {
                hl++;
            }
            if (c == RD_TARGET_WORK1 && p->st.gsH == 256) {
                w1big++;
            }
            if (c == RD_TARGET_WORK2 && p->st.depth == rd_target(RD_TARGET_SCENE).id &&
                p->st.gsW == W && p->st.gsH == H) {
                w2scene++;
            }
            if (c == RD_TARGET_AURA_WORK && p->st.depth == rd_target(RD_TARGET_SCENE).id) {
                auraScene++;
            }
            if (c == RD_TARGET_AURA_TAP && p->st.gsW == 128) {
                tap++;
            }
            if (c == RD_TARGET_FEED128) {
                feedW++;
            }
            if (p->kind == RD_POST_DOF && c == RD_TARGET_SCENE) {
                /* pasteToFB: FIX rate * 128, ABE unless rate 1, TEST 0x50000 */
                const uint8_t fix = (uint8_t)(32 * (planes + 1));
                CHECK(p->st.ds.blend == RD_BLEND_LERP_FIX && p->st.ds.blendFix == fix &&
                          p->st.ds.abe == (planes < 3) && p->st.ds.test.ztst == RD_ZTST_GEQUAL &&
                          p->st.ds.test.zte && p->st.ds.zwrite == RD_ZWRITE_OFF &&
                          p->st.depth == rd_target(RD_TARGET_SCENE).id,
                      "%s: DoF plane %d: LERP FIX %u (got %u), ABE %d, Z GEQUAL, ZMSK", what,
                      planes, fix, p->st.ds.blendFix, p->st.ds.abe);
                CHECK(texTarget(&p->st, NULL) == rd_target(RD_TARGET_WORK0).id,
                      "%s: DoF plane samples WORK0", what);
                if (planes > 0) {
                    CHECK(p->r.z > s_spr[i - 1].r.z, "%s: DoF planes at increasing Z", what);
                }
                planes++;
            }
        }
        const int post = kCases[k].post;
        const int flare = post == 1 || post >= 3;
        if (flare) {
            const int want = post == 4 || post == 5 ? RD_POST_BLOOM : RD_POST_FLARE;
            CHECK(kinds[want] > 20 && kinds[RD_POST_EYE_BLUR] >= 1, "%s: flare sprites as %s (%d)",
                  what, kindName((uint32_t)want), kinds[want]);
            CHECK(hl == 20 + (post == 3 || post == 5 || post == 7 ? 7 : 0),
                  "%s: HIGHLIGHT on the 20 blur passes (and DoF copyToWork2 and its 6 passes): %d",
                  what, hl);
            CHECK(w2scene >= 2, "%s: WORK2 scene-sized with SCENE's Z", what);
        }
        if (post == 2 || post == 3 || post == 5 || post == 7) {
            CHECK(kinds[RD_POST_DOF] == 2 + 6 + 4, "%s: DoF sprites %d", what, kinds[RD_POST_DOF]);
            CHECK(planes == 4, "%s: four DoF planes (%d)", what, planes);
            CHECK(w1big == 1, "%s: copyToWork into WORK1 as 256 x 256", what);
        }
        if (kCases[k].feed) {
            CHECK(kinds[RD_POST_AURA] > 0 && auraScene >= 1 && feedW >= 1,
                  "%s: aura in AURA_WORK with SCENE's Z, FEED128 written", what);
            if (kCases[k].feed != 2) {
                CHECK(tap == 5, "%s: AURA_TAP 128 x 128: clear and four taps (%d)", what, tap);
            }
        }
        /* staticBlur.c: 0x3A00 at 50 Hz (systemStatus[0] 1), 0x3800 at 60 Hz.
           The Japanese disc's source locks 0x3800 alone, which is the
           60 Hz arm: its systemStatus[0] is 0 from the mount (region.h
           fixed_60hz), so the PAL test of the word covers both discs and
           no region field is needed. */
        CHECK(s_lockBase == 0x3A00, "%s: PAL texture lock base 0x3A00", what);
    }
}

/* one frame of post mode post and feed mode feed under the switches in
   s_fx: the sprites of each kind into kinds */
static void recordSwitched(int post, int feed, int kinds[RD_POST_COUNT])
{
    setStage(post, feed, 64);
    FullScreenEffectBefore();
    FullScreenEffectAfter();
    dl_Swap();
    collectFrame(rd__last_frame());
    memset(kinds, 0, sizeof(int) * RD_POST_COUNT);
    for (int i = 0; i < s_nspr; i++) {
        kinds[s_spr[i].kind]++;
    }
}

/* Options > Effects: Glow off drops the flare (or bloom) half of every post mode,
   Depth of field off the depth half, each leaving the other as it was; the
   aura is neither */
static void checkRecordingSwitches(void)
{
    int kinds[RD_POST_COUNT], on[RD_POST_COUNT];

    for (int post = 1; post <= 8; post++) {
        const int flare = post != 2 && post != 8;
        const int dof = post == 2 || post == 3 || post == 5 || post == 7;
        const int want = post == 4 || post == 5 ? RD_POST_BLOOM : RD_POST_FLARE;
        char what[64];

        /* glow off */
        s_fx[0] = 0;
        s_fx[1] = 1;
        snprintf(what, sizeof(what), "post %d, glow off", post);
        recordSwitched(post, 0, kinds);
        CHECK(kinds[RD_POST_FLARE] == 0 && kinds[RD_POST_BLOOM] == 0 &&
                  kinds[RD_POST_EYE_BLUR] == 0,
              "%s: no flare, bloom or eye blur (%d, %d, %d)", what, kinds[RD_POST_FLARE],
              kinds[RD_POST_BLOOM], kinds[RD_POST_EYE_BLUR]);
        CHECK(kinds[RD_POST_DOF] == (dof ? 2 + 6 + 4 : 0), "%s: DoF sprites %d", what,
              kinds[RD_POST_DOF]);
        checkReadAfterWrite(what);
        /* depth of field off */
        s_fx[0] = 1;
        s_fx[1] = 0;
        snprintf(what, sizeof(what), "post %d, depth of field off", post);
        recordSwitched(post, 0, kinds);
        CHECK(kinds[RD_POST_DOF] == 0, "%s: no DoF sprites (%d)", what, kinds[RD_POST_DOF]);
        if (flare) {
            CHECK(kinds[want] > 20 && kinds[RD_POST_EYE_BLUR] >= 1, "%s: flare sprites as %s (%d)",
                  what, kindName((uint32_t)want), kinds[want]);
        } else {
            CHECK(kinds[RD_POST_FLARE] == 0 && kinds[RD_POST_BLOOM] == 0, "%s: no flare", what);
        }
        checkReadAfterWrite(what);
        /* both off: nothing */
        s_fx[0] = s_fx[1] = 0;
        snprintf(what, sizeof(what), "post %d, both off", post);
        recordSwitched(post, 0, kinds);
        CHECK(s_nspr == 0, "%s: no sprites (%d)", what, s_nspr);
    }
    /* the aura (feedback) the same with every switch off as on */
    for (int feed = 1; feed <= 3; feed++) {
        char what[64];

        snprintf(what, sizeof(what), "post 7 feed %d", feed);
        s_fx[0] = s_fx[1] = s_fx[2] = s_fx[3] = s_fx[4] = 1;
        recordSwitched(7, feed, on);
        s_fx[0] = s_fx[1] = s_fx[2] = s_fx[3] = s_fx[4] = 0;
        recordSwitched(7, feed, kinds);
        CHECK(on[RD_POST_AURA] > 0 && kinds[RD_POST_AURA] == on[RD_POST_AURA],
              "%s: aura sprites with the switches off %d, on %d", what, kinds[RD_POST_AURA],
              on[RD_POST_AURA]);
    }
    s_fx[0] = s_fx[1] = s_fx[2] = s_fx[3] = s_fx[4] = 1;
}

/* Options > Effects: Motion blur off draws nothing, whatever the stage's alpha, and
   leaves nothing for the presenter to scale */
static void checkRecordingMotionBlurOff(void)
{
    ScreenHeight = 448;
    systemStatus[0] = 0;
    currentScreenWidth = 0;
    SetMotionBlur(0x40);
    s_fx[3] = 0;
    dl_SetDLPriority(0);
    MotionBlur();
    dl_Swap();
    collectFrame(rd__last_frame());
    CHECK(s_nspr == 0, "motion blur off: no sprite (%d)", s_nspr);
    s_fx[3] = 1;
    MotionBlur();
    dl_Swap();
    collectFrame(rd__last_frame());
    CHECK(s_nspr == 1 && s_spr[0].kind == RD_POST_MOTION_BLUR, "motion blur back on: one sprite");
    ScreenHeight = H;
    systemStatus[0] = 1;
}

/* the motion blur: DISPLAY (PSMCT24) H/2 lines stretched over H */
static void checkRecordingMotionBlur(int h)
{
    ScreenHeight = h;
    systemStatus[0] = h == 512;
    currentScreenWidth = 0;
    SetMotionBlur(0x40);
    dl_SetDLPriority(0);
    MotionBlur();
    dl_Swap();
    collectFrame(rd__last_frame());
    CHECK(s_nspr == 1, "motion blur: one sprite (%d)", s_nspr);
    if (s_nspr == 1) {
        const Spr *p = &s_spr[0];
        uint8_t view = 0;
        CHECK(p->list == 7 && p->kind == RD_POST_MOTION_BLUR, "motion blur: list 7, its kind");
        CHECK(texTarget(&p->st, &view) == rd_target(RD_TARGET_DISPLAY).id &&
                  view == RD_VIEW_RGB24_TA0 && p->st.ds.texa == RD_TEXA_80_80,
              "motion blur: DISPLAY's RGB24 view, TEXA 80/80");
        CHECK(p->st.color == rd_target(RD_TARGET_SCENE).id && p->st.gsH == (uint32_t)h &&
                  p->st.ds.blend == RD_BLEND_LERP_FIX && p->st.ds.blendFix == 0x40 && p->st.ds.abe,
              "motion blur: into SCENE, LERP FIX 0x40");
        CHECK(p->r.rect[3] - p->r.rect[1] == (float)(h * 16) &&
                  p->r.uv[3] - p->r.uv[1] == (float)(h / 2 * 16) && p->r.uv[1] == 4.0f &&
                  p->r.z == 0xFFFFFFFFu,
              "motion blur %d lines: H/2 texels over H lines (%g over %g)", h,
              p->r.uv[3] - p->r.uv[1], p->r.rect[3] - p->r.rect[1]);
        CHECK(p->st.ds.test.zte && p->st.ds.test.ztst == RD_ZTST_ALWAYS &&
                  p->st.ds.zwrite == RD_ZWRITE_OFF,
              "motion blur: TEST 0x3000C, ZMSK");
    }
    /* the state left behind: ZBUF on, TEST 0x5000D */
    const RdStateBlock *e = &rd__last_frame()->endState;
    (void)e;
    ScreenHeight = H;
    systemStatus[0] = 1;
}

/* ===================================== (w) the native frame, recorded */

/* a native frame fw wide, as GsBase.c's latch leaves it: gsFrame and the GIF
   decoder's width (gif_HostSetFrameWidth); 512 is the 4:3 frame */
static void setFrameWidth(int fw)
{
    gsFrame.w = fw;
    gsFrame.fbw = (fw + 63) / 64;
    gsFrame.wExact = (float)fw;
    gsFrame.aspect = fw == W ? 4.0f / 3.0f : 4.0f / 3.0f * (float)fw / (float)W;
    gif_HostSetFrameWidth(fw);
}

static const struct {
    int post, feed;
} kWideCases[] = {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {0, 1}, {0, 2}, {0, 3}};

#define N_WIDE_CASES ((int)(sizeof(kWideCases) / sizeof(kWideCases[0])))

/* every case recorded at the frame's width fw (the rd targets made for
 * it): each sprite drawn with its buffer's own GS width (a 4:3 width left
 * in a wider buffer would put the pass in its left part), each read with
 * TEX0's TW of its source's width (the GS model's texture size: a 4:3 TW
 * on a wider buffer clamps or wraps the reads), every rect on its buffer
 * (within a pixel) and every UV range on its source (within the 4 texels
 * reduceWork2ToWork0's UV, 64 .. W * 16 + 64, runs past it on the PS2 too);
 * whole[k] the sprites
 * of case k that cover their buffer across */
static void recordWide(int fw, int whole[N_WIDE_CASES])
{
    for (int k = 0; k < N_WIDE_CASES; k++) {
        char what[64];
        snprintf(what, sizeof(what), "(w) W %d post %d feed %d", fw, kWideCases[k].post,
                 kWideCases[k].feed);
        setStage(kWideCases[k].post, kWideCases[k].feed, 64);
        FullScreenEffectBefore();
        FullScreenEffectAfter();
        dl_Swap();
        collectFrame(rd__last_frame());
        whole[k] = 0;
        int env = 0, tw = 0, offT = 0, offS = 0;
        for (int i = 0; i < s_nspr; i++) {
            const Spr *p = &s_spr[i];
            const RdTargetRec *t = rd__target_rec(p->st.color);
            if (!t) {
                env++;
                continue;
            }
            env += p->st.gsW != t->w || p->st.gsH > t->h;
            const float ox = (float)(0x8000 - (int32_t)(p->st.gsW / 2) * 16);
            const float x0 = p->r.rect[0], x1 = p->r.rect[2];
            offT += x0 < ox - 16.0f || x1 > ox + (float)(t->w * 16 + 16);
            /* across: within 1/32 of the buffer (4 pixels of a 128-wide
               one; the glow's and the depth of field's shrinks, at most
               2% of the PS2's 256, scale with the buffer at the PS2's
               slope: blurPass, depthFieldPass) */
            whole[k] += (x1 - x0) * 32.0f >= (float)t->w * 16.0f * 31.0f;
            const uint32_t src = texTarget(&p->st, NULL);
            const RdTargetRec *s = src ? rd__target_rec(src) : NULL;
            if (s) {
                tw += p->r.scalar[0] != (float)(1u << ico_frame_tw((int)s->w));
                const float u0 = p->r.uv[0] < p->r.uv[2] ? p->r.uv[0] : p->r.uv[2];
                const float u1 = p->r.uv[0] < p->r.uv[2] ? p->r.uv[2] : p->r.uv[0];
                offS += u0 < -16.0f || u1 > (float)(s->w * 16 + 64);
            }
        }
        CHECK(s_nspr > 0 && env == 0, "%s: %d of %d sprites not at their buffer's GS width", what,
              env, s_nspr);
        CHECK(tw == 0, "%s: %d sprites read a buffer with a TEX0 TW not its width's", what, tw);
        CHECK(offT == 0 && offS == 0,
              "%s: %d rects off their buffer, %d UV ranges off their source", what, offT, offS);
    }
}

/* (w) the glow's and the depth of field's shrink passes (staticBlur.c
 * blurPass, depthFieldPass): a work buffer into its twin, the rect or the
 * UV range shrunk.  For each such sprite (x slope not 1) the map from the
 * buffer's column x (1/16 px from its left edge) to the texel u */
typedef struct ShrinkMap {
    float bx0, u0, slope;
} ShrinkMap;

#define MAX_SHRINK 64

/* the shrink sprites of the glow (post 4: blur ten times, two passes
   each) and the depth of field (post 2: six passes), recorded at the
   current frame width; the count */
static int recordShrinks(ShrinkMap *m)
{
    static const int kPost[2] = {4, 2};
    int n = 0;
    for (int c = 0; c < 2; c++) {
        setStage(kPost[c], 0, 64);
        FullScreenEffectBefore();
        FullScreenEffectAfter();
        dl_Swap();
        collectFrame(rd__last_frame());
        for (int i = 0; i < s_nspr; i++) {
            const Spr *p = &s_spr[i];
            const int d = named(p->st.color), sv = named(texTarget(&p->st, NULL));
            if ((p->kind != RD_POST_BLOOM && p->kind != RD_POST_FLARE && p->kind != RD_POST_DOF) ||
                (d != RD_TARGET_WORK0 && d != RD_TARGET_WORK1) ||
                (sv != RD_TARGET_WORK0 && sv != RD_TARGET_WORK1) || d == sv) {
                continue;
            }
            const float ox = (float)(0x8000 - (int32_t)(p->st.gsW / 2) * 16);
            const float w = p->r.rect[2] - p->r.rect[0];
            const float slope = w > 0.0f ? (p->r.uv[2] - p->r.uv[0]) / w : 0.0f;
            if (slope == 1.0f || n >= MAX_SHRINK) {
                continue;
            }
            m[n].bx0 = p->r.rect[0] - ox;
            m[n].u0 = p->r.uv[0];
            m[n].slope = slope;
            n++;
        }
    }
    return n;
}

/* (w) the shrink passes at fw keep the PS2's sub-pixel pattern on the 4:3
 * columns: buffer column a + x (a = fw / 4 - 128, the 4:3 part's left edge
 * in the half-width buffer) samples texel a + the 4:3 run's texel for x,
 * for x across the 4:3 part (within 2/16 px: the anchor's rounding to the
 * GS's 1/16 grid); at 4:3 every shrink starts at the buffer's left edge
 * (the border 16 for the depth of field), the PS2's rects and UVs */
static void checkShrinksWide(int fw, const ShrinkMap *m43, int n43, const ShrinkMap *mw, int nw)
{
    CHECK(n43 >= 26 && nw == n43,
          "(w) shrinks: %d sprites at 4:3 (want 20 glow + 6 depth of field), %d at W %d", n43, nw,
          fw);
    int ps2 = 0, bad = 0;
    float worst = 0.0f;
    const float a16 = (float)((fw / 4 - 128) * 16);
    for (int i = 0; i < n43 && i < nw; i++) {
        ps2 += !((m43[i].bx0 == 0.0f && m43[i].u0 == 8.0f) ||
                 (m43[i].bx0 == 16.0f && m43[i].u0 == 24.0f));
        for (int x = 0; x <= 4096; x += 1024) {
            const float u43 = m43[i].u0 + ((float)x - m43[i].bx0) * m43[i].slope;
            const float uw = mw[i].u0 + (a16 + (float)x - mw[i].bx0) * mw[i].slope - a16;
            const float e = fabsf(uw - u43);
            worst = e > worst ? e : worst;
            bad += e > 2.0f;
        }
    }
    CHECK(ps2 == 0, "(w) shrinks: %d of the 4:3 run's start off the PS2's (0, 8) or (16, 24)", ps2);
    CHECK(bad == 0,
          "(w) shrinks at W %d: %d samples of the 4:3 columns read off the 4:3 run's texel (worst "
          "%.2f / 16 px)",
          fw, bad, (double)worst);
    printf("  (w) %d shrink passes at W %d: the 4:3 columns' texels the 4:3 run's (worst %.2f / 16 "
           "px)\n",
           nw, fw, (double)worst);
}

/* (w) the screen softening's passes (rd_post.c AA_DOWNSAMPLE and
 * AA_COMPOSITE, as gsb_antiAlias records them) at the frame's width fw:
 * SCENE (fw x H) into AA0 (fw / 2 x 256), AA0 into AA1 (fw / 4 x 128),
 * both composited over SCENE (fw x 512), each sprite across its whole
 * target and each UV across its whole source (the PS2's at 512) */
static void checkAaRecordAt(int fw)
{
    RdPostParams pp;
    dl_SetDLPriority(10);
    memset(&pp, 0, sizeof(pp));
    pp.lines = 2;
    rd_post(RD_POST_AA_DOWNSAMPLE, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = 64;
    rd_post(RD_POST_AA_COMPOSITE, &pp);
    dl_Swap();
    const RdFrame *f = rd__last_frame();
    const RdCmdList *l = f ? &f->lists[10] : NULL;
    uint32_t tgt[8], nt = 0;
    RdScreenVtx v[8];
    uint32_t nv = 0;
    for (uint32_t i = 0; l && i < l->count; i++) {
        const RdCmd *c = &l->cmds[i];
        if (c->type == RDC_TARGET && nt < 8) {
            tgt[nt++] = c->u[2];
        } else if (c->type == RDC_SCREEN && c->u[1] == 2 && nv < 8) {
            memcpy(&v[nv], f->payload + c->u[0], 2 * sizeof(RdScreenVtx));
            nv += 2;
        }
    }
    const uint32_t hw = (uint32_t)fw / 2, qw = (uint32_t)fw / 4;
    CHECK(nt == 4 && tgt[0] == (hw | 256u << 16) && tgt[1] == (qw | 128u << 16) &&
              tgt[2] == ((uint32_t)fw | 512u << 16) && tgt[3] == ((uint32_t)fw | (uint32_t)H << 16),
          "(w) W %d: the softening's targets AA0 %ux256, AA1 %ux128, SCENE %dx512 (%u records)", fw,
          hw, qw, fw, nt);
    /* AA0 from SCENE, AA1 from AA0, AA1 and AA0 over SCENE: corners at
       -0.25 px of each target's left edge, UV 4 .. the source's width */
    const int32_t dstW[4] = {(int32_t)hw, (int32_t)qw, fw, fw};
    const int32_t srcW[4] = {fw, (int32_t)hw, (int32_t)qw, (int32_t)hw};
    int bad = nv != 8;
    for (uint32_t s = 0; !bad && s < 4; s++) {
        const RdScreenVtx *a = &v[2 * s];
        bad += a[0].x != 0x8000 - dstW[s] / 2 * 16 - 4 || a[1].x != a[0].x + dstW[s] * 16 ||
               a[0].s != 4.0f || a[1].s != (float)(4 + srcW[s] * 16);
    }
    CHECK(!bad, "(w) W %d: the softening's sprites span their targets and sources", fw);
}

/* (b) the reduction's open border (rd_blur.c rd__blur_border_strips, CPU):
 * the reduction's textured sprite as rd_post.c records it on a frame W
 * wide (corners a quarter pixel before pixels 0 and W, U 8 .. 16 W + 8),
 * the two strips: the left one covers pixels 0 and 1 alone, the right one
 * W - 2 and W - 1, each pixel of a strip gets the U the sprite gives pixel
 * 2 (or W - 3) by fx_sprite_ps's formula, with the mirror's half texel too
 * (rd__blur_uv_rect moves both ends of a strip as it moves the sprite's);
 * the hardware sprite's pairs (rd__blur_border_verts) the same S at those
 * pixels' coordinates, plain and mirrored; a sprite short of the frame's
 * width, corners off the 12.4 grid or a frame under 6 wide get none */
static int32_t borderModelU(int32_t X, int32_t x0, int32_t x1, int32_t u0, int32_t u1)
{
    return u0 + (int32_t)(((int64_t)X - x0) * ((int64_t)u1 - u0) / ((int64_t)x1 - x0));
}

static void checkBorderStrips(void)
{
    static const int32_t widths[] = {512, 688, 832, 2048, 2560};
    int bad = 0;
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        const int32_t Wf = widths[i], o = 16 * (2048 - Wf / 2);
        RdPostRec p;
        memset(&p, 0, sizeof(p));
        p.rect[0] = (float)(o - 4);
        p.rect[1] = (float)(16 * (2048 - 128) - 4);
        p.rect[2] = (float)(o - 4 + 16 * Wf);
        p.rect[3] = p.rect[1] + 16.0f * 256.0f;
        p.uv[0] = 8.0f;
        p.uv[1] = 8.0f;
        p.uv[2] = (float)(16 * Wf + 8);
        p.uv[3] = (float)(16 * 512 + 8);
        p.lutOffset = ~0u;
        for (int mirror = 0; mirror < 2; mirror++) {
            float uv[4];
            memcpy(uv, p.uv, sizeof(uv));
            if (mirror) {
                uv[0] -= 8.0f; /* rd__blur_uv_rect with the mirror on */
                uv[2] -= 8.0f;
            }
            RdPostRec out[2];
            if (!rd__blur_border_strips(&p, p.uv, Wf, out)) {
                bad++;
                printf("  (b) W %d: no strips\n", Wf);
                continue;
            }
            const int32_t want[2] = {borderModelU(o + 32, (int32_t)p.rect[0], (int32_t)p.rect[2],
                                                  (int32_t)uv[0], (int32_t)uv[2]),
                                     borderModelU(o + 16 * (Wf - 3), (int32_t)p.rect[0],
                                                  (int32_t)p.rect[2], (int32_t)uv[0],
                                                  (int32_t)uv[2])};
            for (int k = 0; k < 2; k++) {
                const RdPostRec *q = &out[k];
                /* the strip's UVs as the replay draws them (the mirror's
                   shift on both ends) */
                const int32_t su0 = (int32_t)q->uv[0] - (mirror ? 8 : 0);
                const int32_t su1 = (int32_t)q->uv[2] - (mirror ? 8 : 0);
                for (int32_t x = 0; x < Wf; x++) {
                    const int32_t X = o + 16 * x;
                    const int cov = X >= (int32_t)q->rect[0] && X < (int32_t)q->rect[2];
                    const int wantCov = k == 0 ? x < 2 : x >= Wf - 2;
                    bad += cov != wantCov;
                    if (cov) {
                        bad += borderModelU(X, (int32_t)q->rect[0], (int32_t)q->rect[2], su0,
                                            su1) != want[k];
                    }
                }
                bad += q->rect[1] != p.rect[1] || q->rect[3] != p.rect[3] || q->uv[1] != p.uv[1] ||
                       q->uv[3] != p.uv[3];
            }
            /* the PS2's U at pixel 2: 16 * 2 + 12 (texels 2 and 3 at 12 / 16
               and 4 / 16), mirrored 16 * 2 + 4 */
            bad += want[0] != (mirror ? 36 : 44) || want[1] != 16 * (Wf - 3) + (mirror ? 4 : 12);
        }
        /* the hardware sprite's pairs: plain, then mirrored (rd_post.c) */
        RdScreenVtx v[4], hv[8];
        memset(v, 0, sizeof(v));
        for (int m = 0; m < 2; m++) {
            v[2 * m].x = o - 4;
            v[2 * m + 1].x = o - 4 + 16 * Wf;
            v[2 * m].s = (float)(8 - 8 * m);
            v[2 * m + 1].s = (float)(16 * Wf + 8 - 8 * m);
        }
        rd__blur_border_verts(v, Wf, hv);
        const float ws[4] = {44.0f, 36.0f, (float)(16 * (Wf - 3) + 12), (float)(16 * (Wf - 3) + 4)};
        for (int k = 0; k < 2; k++) {
            for (int m = 0; m < 2; m++) {
                const RdScreenVtx *w = &hv[4 * k + 2 * m];
                bad += w[0].s != ws[2 * k + m] || w[1].s != ws[2 * k + m];
                bad += k == 0 ? (w[0].x != o - 4 || w[1].x != o + 32)
                              : (w[0].x != o + 16 * (Wf - 2) - 4 || w[1].x != o - 4 + 16 * Wf);
            }
        }
    }
    CHECK(bad == 0, "(b) the border's strips repeat columns 2 and W - 3 (%d differ)", bad);
    /* none: a sprite one pixel short of the width, a corner off the grid, a
       frame under 6 wide */
    RdPostRec p, out[2];
    memset(&p, 0, sizeof(p));
    const int32_t o = 16 * (2048 - 256);
    p.rect[0] = (float)(o - 4);
    p.rect[2] = (float)(o - 4 + 16 * 511);
    p.uv[2] = 16.0f * 512.0f;
    CHECK(!rd__blur_border_strips(&p, p.uv, 512, out), "(b) a sprite short of the width: none");
    p.rect[2] = (float)(o - 4 + 16 * 512) + 0.5f;
    CHECK(!rd__blur_border_strips(&p, p.uv, 512, out), "(b) a corner off the 12.4 grid: none");
    p.rect[0] = (float)(16 * (2048 - 2) - 4);
    p.rect[2] = p.rect[0] + 16.0f * 4.0f;
    CHECK(!rd__blur_border_strips(&p, p.uv, 4, out), "(b) a frame 4 wide: none");
}

/* (w) recording: the effects at 4:3 and on a 2048-wide frame (48:9, the
 * work buffers four times as wide), the sun on (the eye blur's ghosts) */
static void checkRecordingWide(void)
{
    const int fw = 2048;
    int at43[N_WIDE_CASES], wide[N_WIDE_CASES];

    static ShrinkMap sh43[MAX_SHRINK], shw[MAX_SHRINK];
    int nsh43, nshw;

    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    recordWide(W, at43);
    nsh43 = recordShrinks(sh43);
    checkAaRecordAt(W);
    rd_shutdown();
    setFrameWidth(fw);
    if (!rd__init_record_only((uint32_t)fw, H)) {
        CHECK(0, "(w) rd__init_record_only at W %d", fw);
        setFrameWidth(W);
        rd__init_record_only(W, H);
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();

    static const struct {
        int id;
        uint32_t w, h;
    } kSizes[] = {{RD_TARGET_SCENE, 2048, H},       {RD_TARGET_DISPLAY, 2048, H / 2},
                  {RD_TARGET_SHADOW0, 1024, 256},   {RD_TARGET_SHADOW1, 512, 128},
                  {RD_TARGET_SHADOW2, 256, 64},     {RD_TARGET_WORK0, 1024, 128},
                  {RD_TARGET_WORK1, 1024, 256},     {RD_TARGET_WORK2, 2048, H},
                  {RD_TARGET_WORK3, 1024, 128},     {RD_TARGET_AA0, 1024, 256},
                  {RD_TARGET_AA1, 512, 128},        {RD_TARGET_FEED128, 512, 128},
                  {RD_TARGET_AURA_WORK, 2048, H},   {RD_TARGET_AURA_TAP, 512, 128},
                  {RD_TARGET_WORK2_PAD, 1024, 64},  {RD_TARGET_FEED_HELD, 512, 128},
                  {RD_TARGET_WORK3_HELD, 1024, 128}};

    for (size_t i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++) {
        const RdTargetRec *t = rd__target_rec(rd_target((RdTargetId)kSizes[i].id).id);
        CHECK(t && t->w == kSizes[i].w && t->h == kSizes[i].h,
              "(w) W %d: %s is %ux%u (want %ux%u: the PS2's per 512 columns)", fw,
              kTargetNames[kSizes[i].id], t ? t->w : 0, t ? t->h : 0, kSizes[i].w, kSizes[i].h);
    }
    recordWide(fw, wide);
    nshw = recordShrinks(shw);
    checkShrinksWide(fw, sh43, nsh43, shw, nshw);
    checkAaRecordAt(fw);
    for (int k = 0; k < N_WIDE_CASES; k++) {
        CHECK(wide[k] == at43[k],
              "(w) post %d feed %d: %d sprites cover their buffer across at W %d, %d at 4:3",
              kWideCases[k].post, kWideCases[k].feed, wide[k], fw, at43[k]);
    }
    printf("  (w) %d cases recorded at W %d and 512: buffers at their widths, TEX0 TW, rects "
           "and UVs on their buffers%s\n",
           N_WIDE_CASES, fw, failures ? ": FAILED" : "");
    InitializeStaticBlur(); /* sun off */
    rd_shutdown();
    setFrameWidth(W);
    rd__init_record_only(W, H);
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
}

/* ================================================= the CPU model of the GS */

typedef struct CpuT {
    int w, h;
    uint8_t *c;
    uint32_t *z;
} CpuT;

/* The GS's bilinear blend with its 4-bit fractions (common.hlsli
 * gs_bilerp): across first, then down, each step truncated (arithmetic
 * shifts), as PCSX2's software renderer filters */
static uint32_t gsBilerp(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t fu, uint32_t fv)
{
    const int top = (int)a + ((((int)b - (int)a) * (int)fu) >> 4);
    const int bot = (int)c + ((((int)d - (int)c) * (int)fu) >> 4);
    return (uint32_t)(top + (((bot - top) * (int)fv) >> 4));
}

static CpuT s_cpu[RD_TARGET_COUNT];

static uint8_t *s_old;

static CpuT *cpuT(uint32_t id)
{
    const int n = named(id);
    return n >= 0 && s_cpu[n].c ? &s_cpu[n] : NULL;
}

/* the largest named RGBA8 target's bytes (SCENE's, W x H at 1x) */
static size_t cpuMaxBytes(void)
{
    size_t m = (size_t)W * H * 4;
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        const RdTargetRec *t = rd__target_rec((uint32_t)i + 1);
        if (t && (size_t)t->tw * t->th * 4 > m) {
            m = (size_t)t->tw * t->th * 4;
        }
    }
    return m;
}

static void cpuInit(void)
{
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        const RdTargetRec *t = rd__target_rec((uint32_t)i + 1);
        if (!t || t->format != RHI_FMT_RGBA8_UNORM) {
            continue;
        }
        s_cpu[i].w = (int)t->tw;
        s_cpu[i].h = (int)t->th;
        s_cpu[i].c = calloc((size_t)t->tw * t->th, 4);
        if (t->withDepth) {
            s_cpu[i].z = calloc((size_t)t->tw * t->th, 4);
        }
    }
    s_old = malloc(cpuMaxBytes());
}

static int s_unexpected;

static uint32_t texaAlpha(const uint8_t *t, uint32_t mode, uint32_t fmt)
{
    if (fmt == RD_TEXSRC_RGBA32) {
        return t[3];
    }
    uint32_t ta0 = 0x80, ta1 = 0x80, aem = 0;
    if (mode == RD_TEXA_7F_81_AEM) {
        ta0 = 0x7F;
        ta1 = 0x81;
        aem = 1;
    } else if (mode == RD_TEXA_80_80_AEM) {
        aem = 1;
    }
    uint32_t a = fmt == RD_TEXSRC_RGBA16 && t[3] ? ta1 : ta0;
    if (aem && (t[0] | t[1] | t[2]) == 0) {
        a = 0;
    }
    return a;
}

typedef struct Tex {
    const uint8_t *px;
    int w, h, lw, lh, clampS, clampT;
    uint32_t texa, fmt;
} Tex;

static void texel(const Tex *t, int x, int y, uint32_t o[4])
{
    /* REPEAT as fx_texel: a modulo (c & (n - 1) on a power of two) */
    x = t->clampS ? (x < 0 ? 0 : (x >= t->lw ? t->lw - 1 : x)) : ((x % t->lw) + t->lw) % t->lw;
    y = t->clampT ? (y < 0 ? 0 : (y >= t->lh ? t->lh - 1 : y)) : ((y % t->lh) + t->lh) % t->lh;
    x = x < 0 ? 0 : (x < t->w ? x : t->w - 1);
    y = y < 0 ? 0 : (y < t->h ? y : t->h - 1);
    const uint8_t *p = &t->px[((size_t)y * t->w + x) * 4];
    o[0] = p[0];
    o[1] = p[1];
    o[2] = p[2];
    o[3] = texaAlpha(p, t->texa, t->fmt);
}

static int term(uint32_t sel, int cs, int cd)
{
    return sel == 0 ? cs : (sel == 1 ? cd : 0);
}

static int blendCh(uint32_t reg, int cs, int cd, int as, int ad, int fix, int clampMode)
{
    const int a = term(reg & 3, cs, cd), b = term((reg >> 2) & 3, cs, cd);
    const uint32_t cs_ = (reg >> 4) & 3;
    const int c = cs_ == 0 ? as : (cs_ == 1 ? ad : fix);
    const int d = term((reg >> 6) & 3, cs, cd);
    const int v = (((a - b) * c) >> 7) + d;
    return clampMode ? (v < 0 ? 0 : (v > 255 ? 255 : v)) : (v & 255);
}

static int alphaPass(uint32_t atst, uint32_t aref, uint32_t a)
{
    switch (atst) {
    case RD_ATST_NEVER:
        return 0;
    case RD_ATST_LESS:
        return a < aref;
    case RD_ATST_LEQUAL:
        return a <= aref;
    case RD_ATST_EQUAL:
        return a == aref;
    case RD_ATST_GEQUAL:
        return a >= aref;
    case RD_ATST_GREATER:
        return a > aref;
    case RD_ATST_NOTEQUAL:
        return a != aref;
    default:
        return 1;
    }
}

/* (above1x) the sprites the model drew with the above-1x kernels, and
 * their axes' kernels (A1X_GS, A1X_BOX, A1X_BSPLINE); the sprites that read
 * a work buffer onto a scaled target with the GS's own filter */
static int s_a1xSprites, s_a1xKinds[3], s_a1xWorkGs;

/* the work buffers, at their GS sizes at every scale (rd_core.c
 * rd__work_class): a sprite reading one onto a scaled target takes the
 * GS's own filter at that target's finer grid, never a kernel
 * (rd_internal.h rd__work_class) */
static int workBuffer(int n)
{
    return n == RD_TARGET_WORK0 || n == RD_TARGET_WORK1 || n == RD_TARGET_WORK3 ||
           n == RD_TARGET_AA0 || n == RD_TARGET_AA1 || n == RD_TARGET_FEED128 ||
           n == RD_TARGET_AURA_TAP || n == RD_TARGET_WORK2_PAD || n == RD_TARGET_FEED_HELD ||
           n == RD_TARGET_WORK3_HELD;
}

/* (above1x) the distinct resampled passes: source, destination, the axes'
 * kernels and steps */
#define A1X_PASSES 16

static struct {
    int src, dst, kx, ky;
    uint32_t sx, sy;
} s_a1xPass[A1X_PASSES];

static int s_a1xNPass;

static void a1xNote(int src, int dst, int kx, int ky, uint32_t sx, uint32_t sy)
{
    for (int i = 0; i < s_a1xNPass; i++) {
        if (s_a1xPass[i].src == src && s_a1xPass[i].dst == dst && s_a1xPass[i].sx == sx &&
            s_a1xPass[i].sy == sy) {
            return;
        }
    }
    if (s_a1xNPass < A1X_PASSES) {
        s_a1xPass[s_a1xNPass].src = src;
        s_a1xPass[s_a1xNPass].dst = dst;
        s_a1xPass[s_a1xNPass].kx = kx;
        s_a1xPass[s_a1xNPass].ky = ky;
        s_a1xPass[s_a1xNPass].sx = sx;
        s_a1xPass[s_a1xNPass].sy = sy;
        s_a1xNPass++;
    }
}

/* the model's texel fetch for the above-1x kernels (fx_texel) */
static void cpuFetch(void *user, int x, int y, uint32_t out[4])
{
    texel((const Tex *)user, x, y, out);
}

/* fx_sprite_ps on the CPU (the 1x model of the cases above, and x's at
 * scale).  On a scaled target (sx, sy texels a GS pixel)
 * each texel stands for the GS position of its centre, (p + 0.5) / s - 0.5
 * in 12.4 (rounded), and is covered when its GS pixel (floor(p / s)) is;
 * a target texture is read at its own scale (U times its scale, 12.4), and
 * with a scaled target involved and an axis past the GS's filter
 * (a1x_kind) the texel is above1x_ref's resampling, but for a work buffer
 * (workBuffer), which is always read with the GS's own bilinear or nearest
 * at each texel's 12.4 GS position.  At scale 1 every one of these is the
 * GS's own formula. */
static void cpuSprite(const RdStateBlock *s, const RdPostRec *r)
{
    CpuT *tc = cpuT(s->color);
    const RdTargetRec *tcr = rd__target_rec(s->color);
    if (!tc || !tcr || tcr->ox != 0.0f) {
        s_unexpected++;
        return;
    }
    const float tsx = tcr->sx, tsy = tcr->sy;
    const RdDrawState *d = &s->ds;
    CpuT *td = cpuT(s->depth);
    if (td && (!td->z || td->w != tc->w || td->h != tc->h)) {
        td = NULL;
    }
    const uint8_t ztst = d->test.zte ? d->test.ztst : RD_ZTST_ALWAYS;
    const int useDepth = td && (ztst != RD_ZTST_ALWAYS || d->zwrite == RD_ZWRITE_ON);
    memcpy(s_old, tc->c, (size_t)tc->w * tc->h * 4);

    Tex t;
    memset(&t, 0, sizeof(t));
    int textured = 0;
    float ssx = 1.0f, ssy = 1.0f; /* the texture's texels a GS texel */
    int a1x = 0;
    uint32_t stepX = 0, stepY = 0;
    if (d->texEnabled) {
        const RdTexRec *tr = rd__tex_rec(s->tex);
        if (tr && tr->kind == RD_TEXKIND_TARGET && tr->view != RD_VIEW_DEPTH) {
            CpuT *src = cpuT(tr->target);
            const RdTargetRec *sr = rd__target_rec(tr->target);
            if (src && sr) {
                if (sr->ox != 0.0f) {
                    s_unexpected++;
                }
                ssx = sr->sx;
                ssy = sr->sy;
                if (workBuffer(named(tr->target))) {
                    /* the GS's bilinear or nearest below, at each texel's
                     * GS position (X, Y) */
                    s_a1xWorkGs += tsx != 1.0f || tsy != 1.0f;
                } else if (tsx != 1.0f || tsy != 1.0f || ssx != 1.0f || ssy != 1.0f) {
                    stepX = a1x_step32(r->uv[2] - r->uv[0], r->rect[2] - r->rect[0], ssx, tsx);
                    stepY = a1x_step32(r->uv[3] - r->uv[1], r->rect[3] - r->rect[1], ssy, tsy);
                    const int lin = d->magFilter == RD_FILTER_LINEAR;
                    const int kx = a1x_kind(stepX, lin), ky = a1x_kind(stepY, lin);
                    a1x = kx != A1X_GS || ky != A1X_GS;
                    if (a1x) {
                        s_a1xSprites++;
                        s_a1xKinds[kx]++;
                        s_a1xKinds[ky]++;
                        a1xNote(named(tr->target), named(s->color), kx, ky, stepX, stepY);
                    }
                }
                t.px = src == tc ? s_old : src->c;
                t.w = src->w;
                t.h = src->h;
                t.fmt = tr->src;
                textured = 1;
            }
        } else if (tr && tr->kind == RD_TEXKIND_IMAGE) {
            t.px = tr->pixels;
            t.w = (int)tr->w;
            t.h = (int)tr->h;
            t.fmt = tr->src;
            textured = 1;
        }
    }
    t.lw = (int)rintf(r->scalar[0] * ssx);
    t.lh = (int)rintf(r->scalar[1] * ssy);
    t.lw = t.lw < 1 ? 1 : t.lw;
    t.lh = t.lh < 1 ? 1 : t.lh;
    t.clampS = d->wrap.s == RD_WRAP_CLAMP;
    t.clampT = d->wrap.t == RD_WRAP_CLAMP;
    t.texa = d->texa;
    uint32_t tfx = r->lines & 3;
    if (d->texFn == RD_TEXFN_DECAL && tfx == 0) {
        tfx = 1;
    }
    const int tcc = d->tcc == RD_TCC_RGBA;
    const int linear = d->magFilter == RD_FILTER_LINEAR;
    const uint32_t reg =
        rd__alpha_register(d->blend < RD_BLEND_COUNT ? d->blend : RD_BLEND_LERP_AS);
    const int fix = d->blendFix;
    const int x0 = (int)r->rect[0], y0 = (int)r->rect[1], x1 = (int)r->rect[2],
              y1 = (int)r->rect[3];
    const int u0 = (int)r->uv[0], v0 = (int)r->uv[1], u1 = (int)r->uv[2], v1 = (int)r->uv[3];
    const int ox = (2048 - (int)(s->gsW >> 1)) * 16;
    const int oy = (2048 - (int)(s->gsH >> 1)) * 16 + ((s->useOffset & RD_TARGET_HALF_Y) ? 8 : 0);
    /* the scissor in GS pixels, then in texels (rd_replay.c gsRectToTexels) */
    int sx0 = s->scissor[0] < 0 ? 0 : s->scissor[0], sy0 = s->scissor[1] < 0 ? 0 : s->scissor[1];
    int sx1 = s->scissor[2] >= (int)tcr->w ? (int)tcr->w - 1 : s->scissor[2];
    int sy1 = s->scissor[3] >= (int)tcr->h ? (int)tcr->h - 1 : s->scissor[3];
    if (tsx != 1.0f || tsy != 1.0f) {
        const int a = (int)((float)sx0 * tsx), b = (int)((float)sy0 * tsy);
        const int c = (int)((float)(sx1 + 1) * tsx + 0.999f) - 1;
        const int e = (int)((float)(sy1 + 1) * tsy + 0.999f) - 1;
        sx0 = a;
        sy0 = b;
        sx1 = c >= tc->w ? tc->w - 1 : c;
        sy1 = e >= tc->h ? tc->h - 1 : e;
    }
    for (int py = sy0; py <= sy1; py++) {
        const float fy = (float)py + 0.5f;
        if (oy + (int)floorf((fy - 0.5f) / tsy) * 16 < y0 ||
            oy + (int)floorf((fy - 0.5f) / tsy) * 16 >= y1) {
            continue;
        }
        const int Y = oy + (int)floorf((fy / tsy - 0.5f) * 16.0f + 0.5f);
        for (int px = sx0; px <= sx1; px++) {
            const float fx = (float)px + 0.5f;
            const int gbx = ox + (int)floorf((fx - 0.5f) / tsx) * 16;
            if (gbx < x0 || gbx >= x1) {
                continue;
            }
            const int X = ox + (int)floorf((fx / tsx - 0.5f) * 16.0f + 0.5f);
            const size_t i = (size_t)py * tc->w + px;
            if (useDepth) {
                const uint32_t zb = td->z[i];
                const int pass = ztst == RD_ZTST_GEQUAL    ? r->z >= zb
                                 : ztst == RD_ZTST_GREATER ? r->z > zb
                                 : ztst == RD_ZTST_NEVER   ? 0
                                                           : 1;
                if (!pass) {
                    continue;
                }
            }
            uint32_t col[4] = {r->rgba[0], r->rgba[1], r->rgba[2], r->rgba[3]};
            if (textured) {
                const int u = u0 + ((X - x0) * (u1 - u0)) / (x1 - x0);
                const int v = v0 + ((Y - y0) * (v1 - v0)) / (y1 - y0);
                /* in the texture's own 12.4 texels */
                const int su = (int)rintf((float)u * ssx), sv = (int)rintf((float)v * ssy);
                uint32_t tx[4];
                if (a1x) {
                    a1x_resample(cpuFetch, &t, su, sv, stepX, stepY, linear, tx);
                } else if (linear) {
                    const int uu = su - 8, vv = sv - 8;
                    const int ix = uu >> 4, iy = vv >> 4;
                    const uint32_t fu = (uint32_t)(uu & 15), fv = (uint32_t)(vv & 15);
                    uint32_t a[4], b[4], c[4], e[4];
                    texel(&t, ix, iy, a);
                    texel(&t, ix + 1, iy, b);
                    texel(&t, ix, iy + 1, c);
                    texel(&t, ix + 1, iy + 1, e);
                    for (int ch = 0; ch < 4; ch++) {
                        tx[ch] = gsBilerp(a[ch], b[ch], c[ch], e[ch], fu, fv);
                    }
                } else {
                    texel(&t, su >> 4, sv >> 4, tx);
                }
                uint32_t o[4];
                if (tfx == 1) {
                    o[0] = tx[0];
                    o[1] = tx[1];
                    o[2] = tx[2];
                    o[3] = tcc ? tx[3] : col[3];
                } else {
                    for (int ch = 0; ch < 3; ch++) {
                        uint32_t m = (tx[ch] * col[ch]) >> 7;
                        m = m > 255 ? 255 : m;
                        if (tfx >= 2) {
                            m += col[3];
                            m = m > 255 ? 255 : m;
                        }
                        o[ch] = m;
                    }
                    if (!tcc) {
                        o[3] = col[3];
                    } else if (tfx == 0) {
                        o[3] = (tx[3] * col[3]) >> 7;
                        o[3] = o[3] > 255 ? 255 : o[3];
                    } else if (tfx == 2) {
                        o[3] = tx[3] + col[3];
                        o[3] = o[3] > 255 ? 255 : o[3];
                    } else {
                        o[3] = tx[3];
                    }
                }
                memcpy(col, o, sizeof(col));
            }
            int keepA = 0;
            if (d->test.ate && !alphaPass(d->test.atst, d->test.aref, col[3])) {
                if (d->test.afail == RD_AFAIL_KEEP || d->test.afail == RD_AFAIL_ZB_ONLY) {
                    continue;
                }
                keepA = d->test.afail == RD_AFAIL_RGB_ONLY;
            }
            const uint8_t *dp = &s_old[i * 4];
            if (d->test.date != RD_DATE_OFF) {
                const int msb = (dp[3] & 0x80) != 0;
                if (msb != (d->test.date == RD_DATE_DEST_ALPHA_1)) {
                    continue;
                }
            }
            uint32_t res[4] = {col[0], col[1], col[2], col[3]};
            if (d->abe && !(d->pabe && (col[3] & 0x80) == 0)) {
                for (int ch = 0; ch < 3; ch++) {
                    res[ch] = (uint32_t)blendCh(reg, (int)col[ch], dp[ch], (int)col[3], dp[3], fix,
                                                d->colclamp);
                }
            }
            res[3] = col[3] | (d->fba ? 0x80u : 0u);
            if (keepA) {
                res[3] = dp[3];
            }
            uint8_t *op = &tc->c[i * 4];
            for (int ch = 0; ch < 4; ch++) {
                if (d->colorMask & (1u << ch)) {
                    op[ch] = (uint8_t)res[ch];
                }
            }
            if (useDepth && d->zwrite == RD_ZWRITE_ON) {
                td->z[i] = r->z;
            }
        }
    }
}

/* (w) the feedback area's alpha as the mirage's paste reads it (the CPU
   model's FEED128 just before the paste: equal to the GPU's, which every
   frame compares): its largest value and the texels the mask set */
static int s_feedProbe, s_feedMaxA, s_feedSetA;

static void feedProbe(void)
{
    const CpuT *t = &s_cpu[RD_TARGET_FEED128];
    for (int i = 0; t->c && i < t->w * t->h; i++) {
        const int a = t->c[i * 4 + 3];
        s_feedMaxA = a > s_feedMaxA ? a : s_feedMaxA;
        s_feedSetA += a != 0;
    }
}

/* the model draws the glow's left edge as the replay does (rd__set_glow_edge_fill) */
static int s_modelGlowEdge = 1;

static void cpuCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    const RdFrame *f = user;
    (void)list, (void)index;
    if (rd__cmd_is_state(c->type) || c->type == RDC_NOP) {
        return;
    }
    if (c->type == RDC_CLEAR) {
        CpuT *t = cpuT(c->u[0]);
        if (!t) {
            return;
        }
        for (int i = 0; i < t->w * t->h; i++) {
            memcpy(&t->c[i * 4], c->b, 4);
            if (t->z && c->b[4]) {
                t->z[i] = c->u[1];
            }
        }
        return;
    }
    if (c->type == RDC_POST_STUB && rd__is_blur_kind(c->b[0])) {
        RdPostRec r;
        memcpy(&r, f->payload + c->u[1], sizeof(r));
        if (s_feedProbe && s->color == rd_target(RD_TARGET_SCENE).id &&
            texTarget(s, NULL) == rd_target(RD_TARGET_FEED128).id) {
            feedProbe();
        }
        cpuSprite(s, &r);
        /* the replay's strip over the columns pass 1 of the glow's blur
         * leaves on a wide frame (rd_blur_border.c glowEdge) */
        const RdTexRec *tr = s->ds.texEnabled ? rd__tex_rec(s->tex) : NULL;
        RdPostRec e;
        if (s_modelGlowEdge && tr && tr->kind == RD_TEXKIND_TARGET && tr->view != RD_VIEW_DEPTH &&
            rd__glow_edge_strip(c->b[0], &r, (int32_t)s->gsW, rd__target_rec(s->color),
                                rd__target_rec(tr->target), &e) > 0) {
            cpuSprite(s, &e);
        }
        return;
    }
    if (c->type == RDC_COPY) {
        /* the presenter's FEED128 <-> FEED_HELD copy (rd_interp.c
         * feedback): targets of one size and scale */
        CpuT *src = cpuT(c->u[0]), *dst = cpuT(c->u[1]);
        RdCopyRec r;
        memcpy(&r, f->payload + c->u[2], sizeof(r));
        if (!src || !dst || src == dst || r.srcX < 0 || r.srcY < 0 || r.dstX < 0 || r.dstY < 0) {
            s_unexpected++;
            return;
        }
        for (int y = 0; y < (int)r.h; y++) {
            const int sy = r.srcY + y, dy = r.dstY + y;
            if (sy >= src->h || dy >= dst->h) {
                break;
            }
            for (int x = 0; x < (int)r.w; x++) {
                const int sx = r.srcX + x, dx = r.dstX + x;
                if (sx >= src->w || dx >= dst->w) {
                    break;
                }
                memcpy(&dst->c[((size_t)dy * dst->w + dx) * 4],
                       &src->c[((size_t)sy * src->w + sx) * 4], 4);
            }
        }
        return;
    }
    s_unexpected++;
}

static void cpuFrame(const RdFrame *f)
{
    RdStateBlock st = f->startState;
    rd__walk(f, 0, &st, cpuCmd, (void *)f);
}

static uint8_t *s_gpu;

static size_t s_gpuSize;

/* the targets the effects touch, GPU against the CPU model; returns the
 * largest difference */
static const int kCompared[] = {RD_TARGET_SCENE,     RD_TARGET_DISPLAY,   RD_TARGET_WORK0,
                                RD_TARGET_WORK1,     RD_TARGET_WORK2,     RD_TARGET_WORK3,
                                RD_TARGET_FEED128,   RD_TARGET_AURA_WORK, RD_TARGET_AURA_TAP,
                                RD_TARGET_WORK2_PAD, RD_TARGET_FEED_HELD, RD_TARGET_DISPLAY_HELD};

/* set while a case compares a scaled run (above1x): every other comparison
 * is of a run whose targets are all at scale 1, where the replay never
 * takes the above-1x kernels */
static int s_scaledRun;

static int compareAll(const char *what, int verbose)
{
    int worst = 0;
    CHECK(s_scaledRun || g_rd.above1xDraws == 0, "%s: %u sprites took the above-1x kernels at 1x",
          what, g_rd.above1xDraws);
    for (size_t k = 0; k < sizeof(kCompared) / sizeof(kCompared[0]); k++) {
        const CpuT *c = &s_cpu[kCompared[k]];
        uint32_t w = 0, h = 0;
        rhi_wait_frame(); /* a readback that changes the target's state takes a command list */
        if (s_gpuSize < cpuMaxBytes()) {
            free(s_gpu);
            s_gpuSize = cpuMaxBytes();
            s_gpu = malloc(s_gpuSize);
        }
        if (!rd__read_target(rd_target((RdTargetId)kCompared[k]), s_gpu, s_gpuSize, &w, &h) ||
            (int)w != c->w || (int)h != c->h) {
            CHECK(0, "%s: read %s", what, kTargetNames[kCompared[k]]);
            continue;
        }
        int maxd = 0, bad = 0;
        for (int i = 0; i < c->w * c->h * 4; i++) {
            const int e = abs((int)s_gpu[i] - (int)c->c[i]);
            if (e > 0) {
                if (bad < 3 && verbose) {
                    printf("  %s %s pixel (%d,%d) ch %d: gpu %u cpu %u\n", what,
                           kTargetNames[kCompared[k]], (i / 4) % c->w, (i / 4) / c->w, i & 3,
                           s_gpu[i], c->c[i]);
                }
                bad++;
            }
            maxd = e > maxd ? e : maxd;
        }
        worst = maxd > worst ? maxd : worst;
        CHECK(bad == 0, "%s: %s: %d channels differ (max %d)", what, kTargetNames[kCompared[k]],
              bad, maxd);
    }
    return worst;
}

/* ============================================================ content */

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static uint8_t s_img[W * H * 4];

/* a picture with edges and gradients (seed), or noise */
static void makeImage(uint32_t seed, int noise)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = &s_img[(y * W + x) * 4];
            const uint32_t h = hash32((uint32_t)(y * W + x) ^ (seed * 0x9E3779B9u));
            if (noise) {
                p[0] = (uint8_t)h;
                p[1] = (uint8_t)(h >> 8);
                p[2] = (uint8_t)(h >> 16);
                p[3] = (uint8_t)(h >> 24);
                continue;
            }
            const int cx = (int)(seed * 37 % 300) + 100, cy = (int)(seed * 53 % 300) + 100;
            const int dx = x - cx, dy = y - cy;
            const int inDisc = dx * dx + dy * dy < 90 * 90;
            p[0] = (uint8_t)(inDisc ? 240 : (x * 255 / W));
            p[1] = (uint8_t)(inDisc ? 200 : (y * 255 / H));
            p[2] = (uint8_t)((((x >> 5) + (y >> 5) + (int)seed) & 1) ? 180 : 40);
            p[3] = (uint8_t)(inDisc ? 0x40 : ((x + y) & 0x100 ? 0x7F : 0x30));
        }
    }
}

static RdTex s_imgTex;

/* a 1:1 nearest sprite of s_imgTex over the bound target, Z ALWAYS */
static void putImage(RdTarget t, RdTarget depth, int gw, int gh, int py0, int py1, uint32_t z,
                     int zwrite)
{
    rd_set_target(t, depth, (uint32_t)gw, (uint32_t)gh, 0);
    rd_test_gs(0x30000);
    rd_z_write(zwrite);
    rd_abe(0);
    rd_fba(0);
    rd_pabe(0);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_imgTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)(0x8000 - gw / 2 * 16);
    p.rect[1] = (float)(0x8000 - gh / 2 * 16 + py0 * 16);
    p.rect[2] = (float)(0x8000 + gw / 2 * 16);
    p.rect[3] = (float)(0x8000 - gh / 2 * 16 + py1 * 16);
    p.uv[0] = 0.0f;
    p.uv[1] = (float)(py0 * 16);
    p.uv[2] = (float)(gw * 16);
    p.uv[3] = (float)(py1 * 16);
    p.rgba[0] = p.rgba[1] = p.rgba[2] = p.rgba[3] = 0x80;
    p.z = z;
    p.scalar[0] = p.scalar[1] = 512.0f;
    p.scalar[2] = 1.0f;
    rd_post(RD_POST_FLARE, &p);
}

/* the scene: the picture in five bands of GS Z (2^16 z for z = 50, 150,
 * 250, 450, 600, so the depth-of-field planes at z = 100..500 cut through
 * them), the top band left at the clear's Z 0 (the sky) */
static void putScene(void)
{
    static const uint32_t kZ[5] = {0, 150u << 16, 250u << 16, 450u << 16, 600u << 16};
    dl_SetDLPriority(0);
    for (int b = 0; b < 5; b++) {
        putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, H, b * H / 5,
                 (b + 1) * H / 5, b == 0 ? (50u << 16) : kZ[b], b != 0);
    }
}

/* the reduction that closes the motion blur loop: gsbHostReduction's
 * rd_post(RD_POST_REDUCTION) in list 12 (the black clear of DISPLAY, then
 * SCENE bilinear at u = x + 0.75, v = 2y + 1, tinted, inside the border
 * crop), drawn through the GS sprite model, so the CPU model
 * runs it as one more sprite and the loop is compared exactly */
static void putReductionTint(uint8_t r, uint8_t g, uint8_t b)
{
    dl_SetDLPriority(12);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rgba[0] = r;
    p.rgba[1] = g;
    p.rgba[2] = b;
    p.rgba[3] = 0x80;
    rd_post(RD_POST_REDUCTION, &p);
}

static void putReduction(void)
{
    putReductionTint(0x80, 0x78, 0x64);
}

/* every compared target cleared to 0 (and SCENE's Z) on both sides */
static void clearAll(void)
{
    static const uint8_t zero[4] = {0, 0, 0, 0};
    dl_SetDLPriority(0);
    for (size_t k = 0; k < sizeof(kCompared) / sizeof(kCompared[0]); k++) {
        rd_clear_target(rd_target((RdTargetId)kCompared[k]), zero, kCompared[k] == RD_TARGET_SCENE,
                        0);
    }
}

static int runFrame(const char *what, int verbose)
{
    dl_Swap();
    rhi_wait_idle();
    s_unexpected = 0;
    cpuFrame(rd__last_frame());
    CHECK(s_unexpected == 0, "%s: %d commands the CPU model does not know", what, s_unexpected);
    return compareAll(what, verbose);
}

/* ================================================ (p) the GS bilinear */

/* Eight pixels of the opening's flare blur (forest, PAL frame_count 551,
 * PCSX2 2.8.2 software renderer, per-draw dump: the WORK1 -> WORK0 step,
 * HIGHLIGHT, vertex colour 139, 136, 132, UV 0.5 .. 255.5 over 256
 * pixels): the four texels around each sample point, the GS's 4-bit
 * fractions and the value PCSX2 wrote.  The GS blends across then down,
 * each step truncated (gsBilerp): one weighted sum >> 8 is 1 or 2 above
 * at every one of these (the last column), and over the 20 steps of the
 * chain such pixels (about 5 % of each step) added up to ~15 % more glow
 * than PCSX2.  Each pixel is drawn as a one-pixel sprite of the blur's
 * kind from a 16 x 2 texture holding its texels; the GPU must equal both
 * the CPU model and PCSX2's value. */
static void checkBilerpPcsx2(void)
{
    static const struct {
        uint8_t a, b, c, d, fu, fv, want[3], sum[3];
    } kV[8] = {
        {3, 71, 67, 119, 14, 15, {117, 114, 111}, {118, 115, 112}},
        {71, 179, 119, 0, 14, 15, {24, 24, 23}, {26, 25, 24}},
        {63, 43, 0, 19, 10, 15, {14, 13, 13}, {15, 14, 14}},
        {43, 0, 131, 0, 10, 15, {49, 48, 47}, {51, 49, 48}},
        {79, 0, 0, 23, 9, 15, {14, 13, 13}, {15, 14, 14}},
        {215, 183, 71, 191, 9, 15, {153, 149, 145}, {154, 150, 146}},
        {183, 39, 191, 215, 9, 15, {213, 209, 203}, {215, 210, 204}},
        {39, 23, 215, 207, 9, 15, {215, 210, 204}, {216, 211, 205}},
    };

    static const uint32_t kCv[3] = {139, 136, 132};
    uint8_t tex[16 * 2 * 4];
    for (int k = 0; k < 8; k++) {
        const uint8_t q[4] = {kV[k].a, kV[k].b, kV[k].c, kV[k].d};
        for (int j = 0; j < 4; j++) {
            uint8_t *p = &tex[((j >> 1) * 16 + 2 * k + (j & 1)) * 4];
            p[0] = p[1] = p[2] = q[j];
            p[3] = 0x80;
        }
        /* the formula first: PCSX2's value, and not the one-sum's */
        for (int ch = 0; ch < 3; ch++) {
            const uint32_t t = gsBilerp(kV[k].a, kV[k].b, kV[k].c, kV[k].d, kV[k].fu, kV[k].fv);
            const uint32_t one = ((uint32_t)kV[k].a * (16u - kV[k].fu) * (16u - kV[k].fv) +
                                  (uint32_t)kV[k].b * kV[k].fu * (16u - kV[k].fv) +
                                  (uint32_t)kV[k].c * (16u - kV[k].fu) * kV[k].fv +
                                  (uint32_t)kV[k].d * kV[k].fu * kV[k].fv) >>
                                 8;
            const uint32_t m = (t * kCv[ch]) >> 7, m1 = (one * kCv[ch]) >> 7;
            CHECK(m == kV[k].want[ch] && m1 == kV[k].sum[ch],
                  "(p) vector %d ch %d: two-step %u (PCSX2 %u), one sum %u (%u)", k, ch, m,
                  kV[k].want[ch], m1, kV[k].sum[ch]);
        }
    }
    RdTex t = rd_create_texture(16, 2, tex, RD_TEXA_80_80, "rd_blur_test bilerp");
    clearAll();
    dl_SetDLPriority(7);
    const int gw = 256, gh = 128;
    for (int k = 0; k < 8; k++) {
        rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, (uint32_t)gw, (uint32_t)gh, 0);
        rd_test_gs(0x30000);
        rd_z_write(0);
        rd_abe(0);
        rd_fba(0);
        rd_pabe(0);
        rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
        rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_gouraud(0);
        RdPostParams p;
        memset(&p, 0, sizeof(p));
        /* pixel (8 k, 0); U - 8 = 32 k + fu: texel 2 k and fraction fu */
        p.rect[0] = (float)(0x8000 - gw / 2 * 16 + 8 * k * 16);
        p.rect[1] = (float)(0x8000 - gh / 2 * 16);
        p.rect[2] = p.rect[0] + 16.0f;
        p.rect[3] = p.rect[1] + 16.0f;
        p.uv[0] = (float)(32 * k + 8 + kV[k].fu);
        p.uv[1] = (float)(8 + kV[k].fv);
        p.uv[2] = p.uv[0] + 16.0f;
        p.uv[3] = p.uv[1] + 16.0f;
        p.rgba[0] = (uint8_t)kCv[0];
        p.rgba[1] = (uint8_t)kCv[1];
        p.rgba[2] = (uint8_t)kCv[2];
        p.rgba[3] = 0;
        p.scalar[0] = 16.0f;
        p.scalar[1] = 2.0f;
        p.scalar[2] = 1.0f;
        rd_post(RD_POST_FLARE, &p);
    }
    runFrame("(p) PCSX2 bilinear", 1);
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc((size_t)gw * gh * 4 * 4);
    rhi_wait_frame();
    if (!px || !rd__read_target(rd_target(RD_TARGET_WORK0), px, (size_t)gw * gh * 4 * 4, &w, &h)) {
        CHECK(0, "(p) read WORK0");
    } else {
        int bad = 0;
        for (int k = 0; k < 8; k++) {
            const uint8_t *q = px + (size_t)(8 * k) * 4;
            for (int ch = 0; ch < 3; ch++) {
                if (q[ch] != kV[k].want[ch]) {
                    printf("  (p) vector %d ch %d: GPU %u, PCSX2 %u\n", k, ch, q[ch],
                           kV[k].want[ch]);
                    bad++;
                }
            }
        }
        CHECK(bad == 0, "(p) %d channels of the eight pixels are not PCSX2's", bad);
        printf("  (p) the GS bilinear: eight PCSX2 pixels %s\n", bad ? "differ" : "equal");
    }
    free(px);
    rd_destroy_texture(t);
}

/* =================================================== (e) one frame each */

/* one frame of post mode `post` (the sun on, InitStaticBlur called), run
 * against the CPU model; returns the max difference */
static int effectsFrame(int post, int clear)
{
    char what[48];
    snprintf(what, sizeof(what), "(e) post mode %d", post);
    setStage(post, 0, 64);
    makeImage((uint32_t)post, 0);
    rd_update_texture(s_imgTex, s_img);
    if (clear) {
        clearAll();
    }
    putScene();
    s_fanDraws = 0;
    FullScreenEffectBefore();
    /* a shine object of list 7 into the flare mask (WORK2, left bound) */
    dl_SetDLPriority(7);
    putImage(rd_target(RD_TARGET_WORK2), rd_target(RD_TARGET_SCENE), W, H, 200, 260, 0, 0);
    FullScreenEffectAfter();
    const int d = runFrame(what, 1);
    printf("  %s: max difference %d over %d sprites\n", what, d,
           (collectFrame(rd__last_frame()), s_nspr));
    CHECK(post == 2 || s_fanDraws == 2, "%s: the sun is on screen (fans %d)", what, s_fanDraws);
    return d;
}

static void checkEffects(void)
{
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    for (int post = 1; post <= 7; post++) {
        (void)effectsFrame(post, post == 1);
    }
    InitializeStaticBlur(); /* sun off */
}

static void checkDump(void)
{
    static uint8_t first[W * H * 4], again[W * H * 4];
    uint32_t w, h;
    const char *path = "rd_blur_test.rddump";
    /* its own frame (post mode 7), so the case does not rely on the case
     * before it in the shard */
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    (void)effectsFrame(7, 1);
    InitializeStaticBlur(); /* sun off */
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), first, sizeof(first), &w, &h)) {
        CHECK(0, "(d) read SCENE");
        return;
    }
    CHECK(rd__dump_frame(rd__last_frame(), path), "(d) dump the frame");
    RdFrame g;
    memset(&g, 0, sizeof(g));
    if (!rd__load_frame(path, &g)) {
        CHECK(0, "(d) load the frame");
        return;
    }
    /* the frame read what the frame before left in the work buffers and
     * SCENE's Z: replay it on the same inputs (the CPU model's) */
    CHECK(rd__replay_frame(&g, 0, false), "(d) replay the loaded frame");
    rhi_wait_idle();
    if (rd__read_target(rd_target(RD_TARGET_SCENE), again, sizeof(again), &w, &h)) {
        int diff = 0;
        for (size_t i = 0; i < sizeof(again); i++) {
            diff += again[i] != first[i];
        }
        printf("  (d) dump -> load -> replay of post mode 7: %d bytes of SCENE differ\n", diff);
        CHECK(diff == 0, "(d) the replayed dump equals the recorded frame");
    }
    rd__frame_free(&g);
    remove(path);
}

/* ============================================== (m) motion blur feedback */

static void checkMotionBlur(void)
{
    setStage(0, 0, 0);
    currentScreenWidth = 0;
    int worst = 0;
    clearAll();
    for (int n = 0; n < 600; n++) {
        const int seg = n / 200;
        const int fix = seg == 2 ? 0x70 : 0x40;
        if (seg == 0) {
            makeImage(3, 0); /* static */
        } else if (seg == 1) {
            makeImage((uint32_t)n, 1); /* noise */
        } else {
            makeImage((uint32_t)(n / 30), 0); /* a cut every 30 frames */
        }
        rd_update_texture(s_imgTex, s_img);
        putScene();
        /* the flare's TEX1 0x60 and CLAMP 5 leak into the motion blur */
        dl_SetDLPriority(7);
        rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
        rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        SetMotionBlur(fix);
        MotionBlur();
        putReduction();
        char what[48];
        snprintf(what, sizeof(what), "(m) frame %d FIX 0x%x", n, fix);
        const int d = runFrame(what, failures < 10);
        worst = d > worst ? d : worst;
        if (failures > 20) {
            break;
        }
    }
    /* the effect is there: the last frame's SCENE is not the picture drawn into it */
    int blurred = 0;
    for (int i = 0; i < W * H * 4; i++) {
        blurred += (i & 3) != 3 && s_cpu[RD_TARGET_SCENE].c[i] != s_img[i];
    }
    CHECK(blurred > W * H, "(m) the motion blur changed %d channels of the last frame", blurred);
    /* the model's reduction against the GS formula written out: DISPLAY
     * (x, y) inside the crop is the bilinear of SCENE texels x, x + 1
     * (4-bit fraction 4) and rows 2y, 2y + 1 (fraction 8), across then
     * down, each step truncated (gsBilerp), then MODULATE by the tint;
     * black outside (the GPU equals the model, above) */
    {
        static const uint32_t kTint[4] = {0x80, 0x78, 0x64, 0x80};
        const CpuT *sc = &s_cpu[RD_TARGET_SCENE], *dp = &s_cpu[RD_TARGET_DISPLAY];
        int bad = 0;
        for (int py = 0; py < dp->h; py++) {
            for (int px = 0; px < dp->w; px++) {
                const int inside = px >= 2 && px <= W - 3 && py >= 8 && py <= H / 2 - 9;
                for (int k = 0; k < 4; k++) {
                    uint32_t want = 0;
                    if (inside) {
                        const uint8_t *r0 = &sc->c[((size_t)(2 * py) * W + px) * 4 + k];
                        const uint8_t *r1 = r0 + (size_t)W * 4;
                        const uint32_t t = gsBilerp(r0[0], r0[4], r1[0], r1[4], 4u, 8u);
                        want = (t * kTint[k]) >> 7;
                        want = want > 255 ? 255 : want;
                    }
                    bad += dp->c[((size_t)py * dp->w + px) * 4 + k] != want;
                }
            }
        }
        CHECK(bad == 0, "(m) the reduction is the GS formula: %d channels differ", bad);
    }
    printf("  (m) 600 motion blur frames (FIX 0x40 static, 0x40 noise, 0x70 cuts): max difference "
           "%d LSB (%d of the last frame's RGB channels blurred)\n",
           worst, blurred);
}

/* ===================================================== (a) aura feedback */

static void checkAura(void)
{
    int worst = 0;
    clearAll();
    for (int n = 0; n < 600; n++) {
        const int mode = 1 + n / 200;
        setStage(0, mode, (n / 100) & 1 ? 0x40 : 0x20);
        GlobalTimer = 0;
        makeImage((uint32_t)(n / 20), 0);
        rd_update_texture(s_imgTex, s_img);
        putScene();
        FullScreenEffectBefore();
        /* the list-8 objects: into AURA_WORK, left bound by auraInspireBefore */
        dl_SetDLPriority(8);
        putImage(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), W, H,
                 100 + (n * 3) % 200, 160 + (n * 3) % 200, 0, 0);
        FullScreenEffectAfter();
        char what[48];
        snprintf(what, sizeof(what), "(a) frame %d mode %d", n, mode);
        const int d = runFrame(what, failures < 10);
        worst = d > worst ? d : worst;
        if (failures > 20) {
            break;
        }
    }
    int fed = 0;
    for (int i = 0; i < 128 * 128 * 4; i++) {
        fed += (i & 3) != 3 && s_cpu[RD_TARGET_FEED128].c[i] != 0;
    }
    CHECK(fed > 1000, "(a) FEED128 holds the feedback (%d non-zero channels)", fed);
    printf("  (a) 600 aura frames (modes 1, 2, 3; alpha 0x20/0x40): max difference %d LSB\n",
           worst);
}

/* ================================ (c) the presents of a camera cut's tick
 *
 * With interpolation every present of a tick replays the whole frame.  A
 * mirage (feedback mode 2) frame on a camera cut (GlobalTimer 1) ends by
 * filling FEED128 with black at alpha 128 (auraInspireAfter's reset), and
 * pastes FEED128 over the screen before that at blurCol's alpha (128 here,
 * as in most stages).  Each tick is presented twice: as its first present
 * and as a later one (rd__interp_frame's firstOfTick 0).  Both must draw
 * the same picture and leave the same FEED128; before the presenter kept
 * FEED128's input (FEED_HELD), the later present of the cut pasted the
 * black reset over the whole screen. */

static uint8_t s_firstScene[W * H * 4], s_firstFeed[128 * 128 * 4];

static int presentTick(const char *what, int first, uint8_t *scene, uint8_t *feed)
{
    const RdFrame *f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 1.0f, first, NULL);
    if (!f || !rd__replay_frame(f, 0, false)) {
        CHECK(0, "%s: build and replay the present", what);
        return 0;
    }
    rhi_wait_idle();
    s_unexpected = 0;
    cpuFrame(f);
    CHECK(s_unexpected == 0, "%s: %d commands the CPU model does not know", what, s_unexpected);
    const int d = compareAll(what, failures < 10);
    uint32_t w, h;
    CHECK(rd__read_target(rd_target(RD_TARGET_SCENE), scene, W * H * 4, &w, &h) && w == W && h == H,
          "%s: read SCENE", what);
    CHECK(rd__read_target(rd_target(RD_TARGET_FEED128), feed, 128 * 128 * 4, &w, &h) && w == 128 &&
              h == 128,
          "%s: read FEED128", what);
    return d;
}

static void checkCutPresents(void)
{
    static uint8_t scene[W * H * 4], feed[128 * 128 * 4];
    const uint8_t interpolate = g_rd.settings.interpolate;
    g_rd.settings.interpolate = 1; /* rd_end_frame leaves the replays to the presents */
    clearAll();
    int worst = 0, black = 0;
    for (int n = 0; n < 8; n++) {
        setStage(0, 2, 0x80);
        GlobalTimer = n == 5; /* the cut */
        makeImage((uint32_t)(40 + n), 0);
        rd_update_texture(s_imgTex, s_img);
        putScene();
        FullScreenEffectBefore();
        dl_SetDLPriority(8);
        putImage(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), W, H, 120 + n * 10,
                 220 + n * 10, 0, 0);
        FullScreenEffectAfter();
        dl_Swap();
        char what[64];
        snprintf(what, sizeof(what), "(c) tick %d%s, first present", n, n == 5 ? " (cut)" : "");
        int d = presentTick(what, 1, s_firstScene, s_firstFeed);
        worst = d > worst ? d : worst;
        snprintf(what, sizeof(what), "(c) tick %d%s, later present", n, n == 5 ? " (cut)" : "");
        d = presentTick(what, 0, scene, feed);
        worst = d > worst ? d : worst;
        int differ = 0, dark = 0;
        for (int i = 0; i < W * H; i++) {
            differ += memcmp(&scene[i * 4], &s_firstScene[i * 4], 3) != 0;
            dark += (scene[i * 4] | scene[i * 4 + 1] | scene[i * 4 + 2]) == 0;
        }
        CHECK(differ == 0,
              "(c) tick %d%s: the later present's SCENE equals the first's (%d pixels "
              "differ, %d black)",
              n, n == 5 ? " (cut)" : "", differ, dark);
        CHECK(memcmp(feed, s_firstFeed, sizeof(feed)) == 0,
              "(c) tick %d: both presents leave the same FEED128", n);
        black = dark > black ? dark : black;
    }
    GlobalTimer = 0;
    g_rd.settings.interpolate = interpolate;
    CHECK(black < W * H / 2, "(c) no present is black (%d black pixels at most)", black);
    printf("  (c) 8 mirage ticks (a cut at the sixth), two presents each: max difference %d LSB, "
           "at most %d black pixels\n",
           worst, black);
}

/* =================================== (f) FIX 128 without its snapshot */

/* rd_replay.c draws a staticBlur sprite whose blend is ALPHA LERP FIX at
 * FIX 128 without ABE, since ((Cs - Cd) * 128 >> 7) + Cd is Cs, and so takes
 * no snapshot of its target (an untextured one over the whole target
 * becomes a clear, blurSpriteClears). Each case draws a noise picture into
 * SCENE and one sprite over it, then replays the frame with the skip on and
 * off (rd__set_blur_fix128_skip): SCENE is the CPU model's both ways, so the
 * same bytes, and the replay records one copy fewer with the skip on where
 * it applies (FIX 128, textured or not) and the same copies where it must
 * not (FIX 128 under DATE or AFAIL RGB_ONLY, which read the destination,
 * and FIX 127). */
static RdTex s_fixTex;

static int s_fixTexMade;

static void fixSprite(int textured, uint8_t fix, uint64_t test)
{
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, H, 0);
    rd_test_gs(test);
    rd_z_write(0);
    rd_color_mask(0);
    rd_abe(1);
    rd_pabe(0);
    rd_fba(0);
    rd_blend_func(RD_BLEND_LERP_FIX, fix);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    if (textured) {
        rd_texture(s_fixTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    } else {
        rd_texture_off();
    }
    rd_gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)(0x8000 - W / 2 * 16);
    p.rect[1] = (float)(0x8000 - H / 2 * 16);
    p.rect[2] = (float)(0x8000 + W / 2 * 16);
    p.rect[3] = (float)(0x8000 + H / 2 * 16);
    p.uv[2] = (float)(W * 16);
    p.uv[3] = (float)(H * 16);
    p.rgba[0] = textured ? 0x80 : 0x30;
    p.rgba[1] = textured ? 0x80 : 0xC8;
    p.rgba[2] = textured ? 0x80 : 0x71;
    p.rgba[3] = textured ? 0x80 : 0x50;
    p.scalar[0] = p.scalar[1] = 512.0f;
    p.scalar[2] = 1.0f;
    rd_post(RD_POST_FLARE, &p);
}

/* the frame replayed once with the skip on or off: SCENE into out, and the
 * copies the replay recorded */
static uint64_t fixReplay(const char *what, const RdFrame *f, int skip, uint8_t *out)
{
    RhiStats a, b;
    uint32_t w = 0, h = 0;
    rd__set_blur_fix128_skip(skip != 0);
    rhi_wait_idle();
    rhi_get_stats(&a);
    const uint64_t snaps = rd__blur_snapshot_count();
    CHECK(rd__replay_frame(f, 0, false), "%s: replay with the skip %s", what, skip ? "on" : "off");
    rhi_wait_idle();
    rhi_get_stats(&b);
    CHECK(rd__read_target(rd_target(RD_TARGET_SCENE), out, (size_t)W * H * 4, &w, &h) && w == W &&
              h == H,
          "%s: read SCENE", what);
    (void)a;
    (void)b;
    /* the snapshots the replay took, a copy or a swap alike (a sprite that
       covers its target swaps instead of copying, so rhi copies no longer
       count them) */
    return rd__blur_snapshot_count() - snaps;
}

static void checkFix128Skip(void)
{
    static uint8_t on[W * H * 4], off[W * H * 4];
    /* the skip's snapshots alone: the fx plan would draw the untextured
       LERP FIX 128 without the skip as a constant fold, no snapshot */
    const bool plan = g_rd.profile.fxPlan;
    g_rd.profile.fxPlan = false;
    /* ZTE, ZTST ALWAYS; DATE (DATM 0); ATE, ATST GEQUAL, AREF 0x40, AFAIL RGB_ONLY */
    const uint64_t z = 0x30000, date = 1u << 14, rgbOnly = 1 | (5 << 1) | (0x40 << 4) | (3 << 12);

    static const struct {
        const char *name;
        int textured, skip;
        uint8_t fix;
        uint64_t test;
    } kC[] = {{"FIX 128", 1, 1, 128, 0},
              {"FIX 128 untextured (a clear)", 0, 1, 128, 0},
              {"FIX 128 under DATE", 1, 0, 128, 1},
              {"FIX 128 with AFAIL RGB_ONLY", 1, 0, 128, 2},
              {"FIX 127", 1, 0, 127, 0}};

    makeImage(7, 1);
    rd_update_texture(s_imgTex, s_img);
    makeImage(8, 1);
    if (!s_fixTexMade) {
        s_fixTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test fix128");
        s_fixTexMade = 1;
    } else {
        rd_update_texture(s_fixTex, s_img);
    }
    for (size_t k = 0; k < sizeof(kC) / sizeof(kC[0]); k++) {
        char what[80];
        snprintf(what, sizeof(what), "(f) %s", kC[k].name);
        clearAll();
        putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, H, 0, H, 0, 0);
        fixSprite(kC[k].textured, kC[k].fix,
                  z | (kC[k].test == 1 ? date : 0) | (kC[k].test == 2 ? rgbOnly : 0));
        runFrame(what, 1);
        const RdFrame *f = rd__last_frame();
        const uint64_t cOn = fixReplay(what, f, 1, on);
        const uint64_t cOff = fixReplay(what, f, 0, off);
        const uint8_t *model = s_cpu[RD_TARGET_SCENE].c;
        int dOn = 0, dOff = 0, dBoth = 0;
        for (size_t i = 0; i < sizeof(on); i++) {
            dOn += on[i] != model[i];
            dOff += off[i] != model[i];
            dBoth += on[i] != off[i];
        }
        printf("  %s: %llu snapshots with the skip, %llu without; SCENE bytes differing: %d on, %d "
               "off from the model, %d between\n",
               what, (unsigned long long)cOn, (unsigned long long)cOff, dOn, dOff, dBoth);
        CHECK(dOn == 0 && dOff == 0 && dBoth == 0, "%s: SCENE the model's both ways", what);
        CHECK(kC[k].skip ? cOff == cOn + 1 : cOff == cOn, "%s: %s (snapshots %llu on, %llu off)",
              what, kC[k].skip ? "the skip takes no snapshot" : "the snapshot is kept",
              (unsigned long long)cOn, (unsigned long long)cOff);
    }
    rd__set_blur_fix128_skip(true);
    g_rd.profile.fxPlan = plan;
}

/* ========== (n) the motion blur presented many times a tick
 *
 * The motion blur at FIX 32 (the old frame keeps a quarter a tick)
 * with the aura of mode 1 (its glow added onto SCENE in list 8, after the
 * blur) and a reduction tint of 0x98 (a gain of 1.1875 on the way back to
 * DISPLAY), on a still picture: N_TICKS ticks, each presented N = 1, 2, 4
 * and 10 times (alpha k / N, the first present the tick's first), as the
 * presenter does at 30 Hz ticks with 30, 60, 120 and 300 presents a
 * second.  Every tick's last present is compared with the CPU model, and
 * its DISPLAY must be the bytes the same frames give replayed once a tick
 * without interpolation (the Original frame rate, the PS2's arithmetic):
 * the old frame keeps FIX / 128 a tick and the loop through the aura and
 * the tint runs once a tick, whatever N.  The presents as drawn without the
 * held DISPLAY copy are replayed too (the frames recorded with FIX' = 128
 * (32 / 128)^(1 / N) and every present reading the DISPLAY the present
 * before left): there
 * the tint and the aura went round N times a tick, 1.1875 x 111 / 128 > 1
 * at N = 10, so the picture ran to white; the test shows how far. */
#define N_TICKS 8
#define N_DW W
#define N_DH (H / 2)

static uint8_t s_nRef[N_TICKS][N_DW * N_DH * 4];

/* one tick of the still picture, recorded (closed by the caller) */
static void nRecord(int fix)
{
    setStage(0, 1, 0x40);
    GlobalTimer = 0;
    currentScreenWidth = 0;
    putScene();
    FullScreenEffectBefore();
    /* the aura's objects: into AURA_WORK, left bound by auraInspireBefore */
    dl_SetDLPriority(8);
    putImage(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), W, H, 150, 250, 0, 0);
    FullScreenEffectAfter();
    SetMotionBlur(fix);
    MotionBlur();
    putReductionTint(0x98, 0x98, 0x98);
}

/* DISPLAY's mean RGB and its saturated RGB channels */
static double nMean(const uint8_t *d, int *sat)
{
    double sum = 0.0;
    int n = 0;
    *sat = 0;
    for (int i = 0; i < N_DW * N_DH * 4; i++) {
        if ((i & 3) != 3) {
            sum += d[i];
            *sat += d[i] == 255;
            n++;
        }
    }
    return sum / n;
}

/* N_TICKS ticks presented n times each; legacy: as presented without the
 * held DISPLAY copy (each present reading the DISPLAY the present before
 * left).
 * Returns the bytes of the ticks' DISPLAYs that differ from s_nRef's; the
 * last tick's mean and saturated channels in *mean, *sat. */
static int nRun(int n, int legacy, double *mean, int *sat)
{
    const uint32_t disp = rd_target(RD_TARGET_DISPLAY).id;
    const uint32_t dispHeld = rd_target(RD_TARGET_DISPLAY_HELD).id;
    const int fix = legacy ? (int)(128.0 * pow(32.0 / 128.0, 1.0 / n) + 0.5) : 32;
    const CpuT *dp = &s_cpu[RD_TARGET_DISPLAY];
    int differ = 0, firstLast = 0;
    clearAll();
    for (int t = 0; t < N_TICKS; t++) {
        nRecord(fix);
        dl_Swap(); /* closed, not replayed: interpolate is on */
        static uint8_t firstDisp[N_DW * N_DH * 4];
        for (int k = 0; k < n; k++) {
            const int first = k == 0;
            const RdFrame *f = rd__interp_frame(rd__prev_frame(), rd__last_frame(),
                                                (float)(k + 1) / n, first, NULL);
            if (f && legacy && !first) {
                /* legacy: no DISPLAY_HELD, the present before's DISPLAY read */
                RdCmdList *cl = (RdCmdList *)&f->lists[0];
                for (uint32_t i = 0; i < cl->count && i < 2; i++) {
                    if (cl->cmds[i].type == RDC_COPY && cl->cmds[i].u[0] == dispHeld &&
                        cl->cmds[i].u[1] == disp) {
                        cl->cmds[i].type = RDC_NOP;
                    }
                }
            }
            if (!f || !rd__replay_frame(f, 0, false)) {
                CHECK(0, "(n) N %d%s tick %d present %d: build and replay", n,
                      legacy ? " (v0.4.2)" : "", t, k);
                return -1;
            }
            rhi_wait_idle();
            s_unexpected = 0;
            cpuFrame(f);
            CHECK(s_unexpected == 0, "(n) %d commands the CPU model does not know", s_unexpected);
            if (first) {
                memcpy(firstDisp, dp->c, sizeof(firstDisp));
            }
        }
        char what[64];
        snprintf(what, sizeof(what), "(n) N %d%s tick %d", n, legacy ? " (v0.4.2)" : "", t);
        compareAll(what, failures < 10);
        firstLast += memcmp(firstDisp, dp->c, sizeof(firstDisp)) != 0;
        for (int i = 0; i < N_DW * N_DH * 4; i++) {
            differ += dp->c[i] != s_nRef[t][i];
        }
    }
    if (!legacy) {
        CHECK(firstLast == 0, "(n) N %d: a tick's presents draw one DISPLAY (%d ticks differ)", n,
              firstLast);
    }
    *mean = nMean(dp->c, sat);
    return differ;
}

static void checkMotionBlurPresents(void)
{
    const CpuT *dp = &s_cpu[RD_TARGET_DISPLAY];
    if (dp->w != N_DW || dp->h != N_DH) {
        CHECK(0, "(n) DISPLAY is %d x %d", dp->w, dp->h);
        return;
    }
    makeImage(5, 0);
    rd_update_texture(s_imgTex, s_img);
    /* the reference: each tick replayed once as it closes (Original) */
    clearAll();
    for (int t = 0; t < N_TICKS; t++) {
        nRecord(32);
        char what[48];
        snprintf(what, sizeof(what), "(n) reference tick %d", t);
        runFrame(what, failures < 10);
        memcpy(s_nRef[t], dp->c, sizeof(s_nRef[t]));
    }
    int refSat = 0;
    const double refMean = nMean(s_nRef[N_TICKS - 1], &refSat);
    /* the blur and the aura are there: the first tick (no old frame yet)
     * against the last */
    int moved = 0;
    for (int i = 0; i < N_DW * N_DH * 4; i++) {
        moved += s_nRef[0][i] != s_nRef[N_TICKS - 1][i];
    }
    CHECK(moved > N_DW * N_DH, "(n) the feedback changed %d channels from the first tick", moved);
    const uint8_t interpolate = g_rd.settings.interpolate;
    g_rd.settings.interpolate = 1; /* rd_end_frame leaves the replays to the presents */
    static const int kN[] = {1, 2, 4, 10};
    double mean[4], legacyMean[4];
    int sat[4], legacySat[4], legacyDiff[4];
    for (int i = 0; i < 4; i++) {
        const int d = nRun(kN[i], 0, &mean[i], &sat[i]);
        CHECK(d == 0,
              "(n) %d presents a tick: %d bytes of the ticks' DISPLAYs differ from one "
              "replay a tick",
              kN[i], d);
        legacyDiff[i] = nRun(kN[i], 1, &legacyMean[i], &legacySat[i]);
    }
    g_rd.settings.interpolate = interpolate;
    /* legacy at N = 1 is the PS2's too; at N = 2 already brighter; at
     * N = 10 its loop gain is over 1: white inside the crop (the black
     * border is about 7 % of DISPLAY) */
    CHECK(legacyDiff[0] == 0, "(n) v0.4.2 at one present a tick: %d bytes differ", legacyDiff[0]);
    CHECK(legacyMean[1] > refMean + 5.0, "(n) v0.4.2 at 2 presents a tick: mean %.1f (%.1f)",
          legacyMean[1], refMean);
    CHECK(legacyMean[3] > refMean + 40.0 && legacySat[3] > N_DW * N_DH * 3 / 10 * 8,
          "(n) v0.4.2 at 10 presents a tick: mean %.1f (one replay a tick %.1f), %d of %d "
          "channels at 255",
          legacyMean[3], refMean, legacySat[3], N_DW * N_DH * 3);
    printf("  (n) %d ticks of motion blur (FIX 32), aura and tint 0x98, one replay a tick: mean "
           "%.1f, %d of %d channels at 255\n",
           N_TICKS, refMean, refSat, N_DW * N_DH * 3);
    for (int i = 0; i < 4; i++) {
        printf("  (n) N = %2d: mean %.1f, %d at 255 (as v0.4.2 presented them, FIX %d: mean %.1f, "
               "%d at 255, %d bytes of the ticks differ)\n",
               kN[i], mean[i], sat[i], (int)(128.0 * pow(0.25, 1.0 / kN[i]) + 0.5), legacyMean[i],
               legacySat[i], legacyDiff[i]);
    }
}

/* ====================== (q) the queen's mirage, as the F12 dumps record it
 *
 * The model viewer's dumps of the Queen (NTSC, 512 x 448,
 * feedback mode 2, feedbackCol (64, 64, 64, 128)) record the mirage's
 * sprites with these parameters (rd_replay_tool --list --no-device): the
 * reduction AURA_WORK -> WORK0 rect (30720,31736)-(34816,33528) (256 x 112
 * at (-128, -64.5)) UV (16,16)-(8208,7184) TEX0 512 x 512, colour
 * (0,0,0,0x80), ALPHA mode 5; the alpha copy WORK0 -> FEED128 rect
 * (31744,31744)-(33792,33792) UV (0,0)-(4096,2048) TEX0 256 x 128, same
 * colour and mode; the paste FEED128 -> SCENE rect (28672,29184)-
 * (36864,36352) UV (8,8)-(2056,1800) TEX0 128 x 128, colour (64,64,64,128),
 * mode 7; the band clear of FEED128 rows 112..128; the copy SCENE ->
 * FEED128 with PABE 1, mode 2, ABE 0.  The game's own staticBlur.c records
 * them here (checked against those numbers), list 8's shine material is a
 * sprite into AURA_WORK with SCENE's depth under the dumps' state (TEST
 * 0x5346D, ALPHA 0x44, ABE, Z write) behind a nearer band of the scene, and
 * every target is compared with the CPU model exactly, frame after frame of
 * feedback.  Then the same frames in Enhanced 4x (the resolution window)
 * and with the full-height scene: SCENE at every GS pixel centre against
 * the 1x model's, exactly where the mask is 0 within 16 GS pixels and
 * within 4 LSB where it is one value within 16 (its edges are finer at a
 * scale: AURA_WORK's mask is at the scene's scale). */
#define QH 448
#define Q_FRAMES 12
#define Q_Z_FACE (0x00C00000u)
#define Q_Z_SHINE (0x00400000u)

static uint8_t s_qScene1x[W * QH * 4], s_qMask1x[W * QH];

static RdTex s_qShineTex;

/* the shine material: a sprite into AURA_WORK, rows 150..300, x 100..400,
 * behind the face band; its texture white at alpha 0x80 but for a hole of
 * alpha 0x40 (failing the alpha test: RGB only) */
static void qShine(int n)
{
    /* on a frame wider than 4:3 (w) the same place in its 4:3 columns */
    const int fw = gsFrame.w;
    (void)n;
    dl_SetDLPriority(8);
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), (uint32_t)fw, QH, 0);
    rd_test_gs(0x5346D);
    rd_z_write(1);
    rd_abe(1);
    rd_blend_func(RD_BLEND_LERP_AS, 0x80);
    rd_fba(0);
    rd_pabe(0);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_qShineTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    const int dx = (fw - W) / 2;
    p.rect[0] = (float)(0x8000 - fw / 2 * 16 + (100 + dx) * 16);
    p.rect[1] = (float)(0x8000 - QH / 2 * 16 + 150 * 16);
    p.rect[2] = (float)(0x8000 - fw / 2 * 16 + (400 + dx) * 16);
    p.rect[3] = (float)(0x8000 - QH / 2 * 16 + 300 * 16);
    p.uv[0] = 0.0f;
    p.uv[1] = 0.0f;
    p.uv[2] = 300.0f * 16.0f;
    p.uv[3] = 150.0f * 16.0f;
    p.rgba[0] = p.rgba[1] = p.rgba[2] = 0x80;
    p.rgba[3] = 0x7F; /* the queen's vertex alpha */
    p.z = Q_Z_SHINE;
    p.scalar[0] = p.scalar[1] = 512.0f;
    p.scalar[2] = 1.0f;
    p.exactInt = 1;
    rd_post(RD_POST_AURA, &p);
}

/* one frame: the scene (Z 0, the face band rows 180..260 nearer), the
 * mirage around the shine material */
static void qFrame(int n)
{
    dl_SetDLPriority(0);
    putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 0, QH, 0, 1);
    putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 180, 260, Q_Z_FACE, 1);
    FullScreenEffectBefore();
    qShine(n);
    FullScreenEffectAfter();
}

static int qSame(const RdPostRec *r, float x0, float y0, float x1, float y1, float u0, float v0,
                 float u1, float v1, float s0, float s1, const uint8_t c[4])
{
    return r->rect[0] == x0 && r->rect[1] == y0 && r->rect[2] == x1 && r->rect[3] == y1 &&
           r->uv[0] == u0 && r->uv[1] == v0 && r->uv[2] == u1 && r->uv[3] == v1 &&
           r->scalar[0] == s0 && r->scalar[1] == s1 && memcmp(r->rgba, c, 4) == 0;
}

/* the recorded sprites are the dumps' */
static void qCheckRecorded(void)
{
    static const uint8_t mask[4] = {0, 0, 0, 0x80}, paste[4] = {64, 64, 64, 128},
                         zero[4] = {0, 0, 0, 0}, white[4] = {128, 128, 128, 128};
    collectFrame(rd__last_frame());
    int reduce = 0, copyA = 0, pasted = 0, band = 0, copyF = 0;
    for (int i = 0; i < s_nspr; i++) {
        const Spr *p = &s_spr[i];
        if (p->kind != RD_POST_AURA) {
            continue;
        }
        const int c = named(p->st.color);
        const RdPostRec *r = &p->r;
        if (c == RD_TARGET_WORK0) {
            reduce += qSame(r, 30720, 31736, 34816, 33528, 16, 16, 8208, 7184, 512, 512, mask) &&
                      p->st.ds.blend == RD_BLEND_CS_AS_ADD_CD && p->st.ds.abe &&
                      texTarget(&p->st, NULL) == rd_target(RD_TARGET_AURA_WORK).id;
        } else if (c == RD_TARGET_FEED128 && p->st.ds.texEnabled &&
                   texTarget(&p->st, NULL) == rd_target(RD_TARGET_WORK0).id) {
            copyA += qSame(r, 31744, 31744, 33792, 33792, 0, 0, 4096, 2048, 256, 128, mask) &&
                     p->st.ds.blend == RD_BLEND_CS_AS_ADD_CD && p->st.ds.abe;
        } else if (c == RD_TARGET_SCENE) {
            pasted += qSame(r, 28672, 29184, 36864, 36352, 8, 8, 2056, 1800, 128, 128, paste) &&
                      p->st.ds.blend == RD_BLEND_LERP_AS_ALT && p->st.ds.abe &&
                      texTarget(&p->st, NULL) == rd_target(RD_TARGET_FEED128).id;
        } else if (c == RD_TARGET_FEED128 && !p->st.ds.texEnabled) {
            band += r->rect[0] == 31744 && r->rect[1] == 33536 && r->rect[2] == 33792 &&
                    r->rect[3] == 33792 && memcmp(r->rgba, zero, 4) == 0;
        } else if (c == RD_TARGET_FEED128) {
            copyF += qSame(r, 31744, 31744, 33792, 33536, 8, 8, 8200, 7176, 512, 512, white) &&
                     p->st.ds.pabe && !p->st.ds.abe && p->st.ds.blend == RD_BLEND_LERP_FIX;
        }
    }
    CHECK(reduce == 1 && copyA == 1 && pasted == 1 && band == 1 && copyF == 1,
          "(q) the mirage's sprites as the dumps record them: reduce %d, alpha copy %d, paste %d, "
          "band clear %d, copy %d (want 1 each)",
          reduce, copyA, pasted, band, copyF);
}

static void qInit(const char *what, int preset, float scale, int fullHeight)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = (uint8_t)preset;
    st.aspect = gsFrame.aspect; /* 4:3 but in (w) */
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = scale;
    st.fullHeightScene = (uint8_t)fullHeight;
    if (!rd_init((uint32_t)gsFrame.w, QH, &st, NULL)) {
        CHECK(0, "(q) rd_init %s", what);
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    makeImage(7, 0);
    s_imgTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");
    static uint8_t shine[512 * 512 * 4];
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 512; x++) {
            uint8_t *q = &shine[((size_t)y * 512 + x) * 4];
            const int hole = x >= 120 && x < 180 && y >= 0 && y < 30;
            q[0] = q[1] = q[2] = 0xFF;
            q[3] = hole ? 0x40 : 0x80;
        }
    }
    s_qShineTex = rd_create_texture(512, 512, shine, RD_TEXA_80_80, "rd_blur_test shine");
}

/* ============================================= (x) above 1x
 *
 * Enhanced 4x (NTSC, SCENE 4x, the work buffers at their GS sizes): the
 * aura's three feedback modes over a picture with a nearer band, every
 * target against the CPU model at its own scale (cpuSprite), 0 LSB: the
 * GS's sprites where no target is scaled or no axis leaves the GS's filter,
 * above1x_ref's box where the replay sets FXF_ABOVE1X (SCENE into FEED128
 * and AURA_WORK into WORK0, decimations), and the GS's own bilinear at each
 * SCENE texel's GS position where a work buffer is drawn back over the
 * frame (WORK0, FEED128 and AURA_TAP, magnifications).  The replay flagged
 * as many sprites as the model resampled; the box ran, the B-spline never. */
#define X_FRAMES 3

static void checkAbove1x(void)
{
    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    ScreenHeight = QH;
    systemStatus[0] = 0;
    rd_shutdown();
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        free(s_cpu[i].c);
        free(s_cpu[i].z);
    }
    memset(s_cpu, 0, sizeof(s_cpu));
    free(s_old);
    s_old = NULL;
    qInit("Enhanced 4x", RD_PRESET_ENHANCED, 4.0f, 0);
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const RdTargetRec *fb = rd__target_rec(rd_target(RD_TARGET_FEED128).id);
    CHECK(sc && fb && sc->sx == 4.0f && fb->sx == 1.0f, "(x) SCENE at 4x, FEED128 at its GS size");
    cpuInit();
    s_scaledRun = 1;
    clearAll();
    s_a1xSprites = 0;
    s_a1xWorkGs = 0;
    s_a1xNPass = 0;
    memset(s_a1xKinds, 0, sizeof(s_a1xKinds));
    int worst = 0;
    for (int n = 0; n < 3 * X_FRAMES; n++) {
        const int mode = 1 + n / X_FRAMES;
        setStage(0, mode, (n & 1) ? 0x40 : 0x20);
        GlobalTimer = 0;
        makeImage((uint32_t)(n + 3), 0);
        rd_update_texture(s_imgTex, s_img);
        dl_SetDLPriority(0);
        putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 0, QH, 0, 1);
        putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 180, 260, Q_Z_FACE,
                 1);
        FullScreenEffectBefore();
        dl_SetDLPriority(8);
        putImage(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), W, QH, 100 + n * 20,
                 200 + n * 20, 0, 0);
        FullScreenEffectAfter();
        char what[48];
        snprintf(what, sizeof(what), "(x) 4x frame %d mode %d", n, mode);
        const int d = runFrame(what, failures < 10);
        worst = d > worst ? d : worst;
        if (failures > 20) {
            break;
        }
    }
    rhi_wait_idle();
    printf("  (x) %d aura frames at Enhanced 4x: max difference %d LSB; %d sprites resampled "
           "(the replay flagged %u), axes: %d GS, %d box, %d B-spline; %d work buffer reads "
           "onto the frame with the GS's filter\n",
           3 * X_FRAMES, worst, s_a1xSprites, g_rd.above1xDraws, s_a1xKinds[A1X_GS],
           s_a1xKinds[A1X_BOX], s_a1xKinds[A1X_BSPLINE], s_a1xWorkGs);
    static const char *const kKind[3] = {"GS", "box", "B-spline"};
    for (int i = 0; i < s_a1xNPass; i++) {
        printf("      %s -> %s: across %s at %.3f texels, down %s at %.3f\n",
               kTargetNames[s_a1xPass[i].src], kTargetNames[s_a1xPass[i].dst],
               kKind[s_a1xPass[i].kx], s_a1xPass[i].sx / 32.0, kKind[s_a1xPass[i].ky],
               s_a1xPass[i].sy / 32.0);
    }
    CHECK(s_a1xSprites > 0 && (uint32_t)s_a1xSprites == g_rd.above1xDraws,
          "(x) the replay flagged %u sprites, the model resampled %d", g_rd.above1xDraws,
          s_a1xSprites);
    CHECK(s_a1xKinds[A1X_BOX] > 0 && s_a1xKinds[A1X_BSPLINE] == 0 && s_a1xWorkGs > 0,
          "(x) the box ran (%d), the B-spline did not (%d), the work buffers went back over "
          "the frame through the GS's filter (%d)",
          s_a1xKinds[A1X_BOX], s_a1xKinds[A1X_BSPLINE], s_a1xWorkGs);
    CHECK(rhi_vk_validation_error_count() == 0, "(x) %u validation errors",
          rhi_vk_validation_error_count());
    s_scaledRun = 0;
    rd_shutdown();
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* ======================================== (g) the glow above 1x
 *
 * Enhanced 2x (PAL, SCENE 2x, the work buffers at their GS sizes): one
 * frame of each post mode 1..7 as (e) runs them (the flare's and the
 * bloom's chains and their paste WORK3 -> SCENE, the depth of field's four
 * pastes WORK0 -> SCENE, the eye blur with the sun), every target against
 * the CPU model at its own scale, 0 LSB: each paste the GS's bilinear at
 * every SCENE texel's 12.4 GS position (cpuSprite, workBuffer), the
 * decimations from SCENE and WORK2 into the work buffers the box.  The
 * B-spline never ran; the replay flagged as many sprites as the model
 * resampled. */
static void checkEffectsScaled(void)
{
    rd_shutdown();
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        free(s_cpu[i].c);
        free(s_cpu[i].z);
    }
    memset(s_cpu, 0, sizeof(s_cpu));
    free(s_old);
    s_old = NULL;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = gsFrame.aspect;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = 2.0f;
    if (!rd_init(W, H, &st, NULL)) {
        CHECK(0, "(g) rd_init at Enhanced 2x");
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    makeImage(1, 0);
    s_imgTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");
    const RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const RdTargetRec *w3 = rd__target_rec(rd_target(RD_TARGET_WORK3).id);
    CHECK(sc && w3 && sc->sx == 2.0f && w3->sx == 1.0f && w3->tw == 256 && w3->th == 128,
          "(g) SCENE at 2x, WORK3 256 x 128");
    cpuInit();
    s_scaledRun = 1;
    s_a1xSprites = 0;
    s_a1xWorkGs = 0;
    s_a1xNPass = 0;
    memset(s_a1xKinds, 0, sizeof(s_a1xKinds));
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    int worst = 0;
    for (int post = 1; post <= 7 && failures <= 20; post++) {
        const int d = effectsFrame(post, post == 1);
        worst = d > worst ? d : worst;
    }
    InitializeStaticBlur(); /* sun off */
    rhi_wait_idle();
    printf("  (g) post modes 1..7 at Enhanced 2x: max difference %d LSB; %d work buffer reads "
           "onto SCENE with the GS's filter; %d sprites resampled (the replay flagged %u), "
           "axes: %d GS, %d box, %d B-spline\n",
           worst, s_a1xWorkGs, s_a1xSprites, g_rd.above1xDraws, s_a1xKinds[A1X_GS],
           s_a1xKinds[A1X_BOX], s_a1xKinds[A1X_BSPLINE]);
    CHECK(s_a1xWorkGs > 0 && s_a1xKinds[A1X_BSPLINE] == 0,
          "(g) the pastes through the GS's filter (%d), no B-spline (%d)", s_a1xWorkGs,
          s_a1xKinds[A1X_BSPLINE]);
    CHECK((uint32_t)s_a1xSprites == g_rd.above1xDraws,
          "(g) the replay flagged %u sprites, the model resampled %d", g_rd.above1xDraws,
          s_a1xSprites);
    CHECK(rhi_vk_validation_error_count() == 0, "(g) %u validation errors",
          rhi_vk_validation_error_count());
    s_scaledRun = 0;
    rd_shutdown();
}

static void checkQueenMirage(void)
{
    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    ScreenHeight = QH;
    systemStatus[0] = 0; /* NTSC: 448 lines, the dumps' */
    setStage(0, 2, 128);
    GlobalStageSetting.feedbackCol[0] = GlobalStageSetting.feedbackCol[1] =
        GlobalStageSetting.feedbackCol[2] = 64;
    GlobalTimer = 0;

    /* 1x: the CPU model, every target */
    rd_shutdown();
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        free(s_cpu[i].c);
        free(s_cpu[i].z);
    }
    memset(s_cpu, 0, sizeof(s_cpu));
    free(s_old);
    qInit("Original 1x", RD_PRESET_ORIGINAL, 0.0f, 0);
    cpuInit();
    clearAll();
    int worst = 0;
    for (int n = 0; n < Q_FRAMES; n++) {
        qFrame(n);
        char what[48];
        snprintf(what, sizeof(what), "(q) 1x frame %d", n);
        if (n == 0) {
            dl_Swap();
            rhi_wait_idle();
            qCheckRecorded();
            s_unexpected = 0;
            cpuFrame(rd__last_frame());
            worst = compareAll(what, 1);
        } else {
            const int d = runFrame(what, failures < 10);
            worst = d > worst ? d : worst;
        }
    }
    memcpy(s_qScene1x, s_cpu[RD_TARGET_SCENE].c, sizeof(s_qScene1x));
    for (int i = 0; i < W * QH; i++) {
        s_qMask1x[i] = s_cpu[RD_TARGET_AURA_WORK].c[i * 4 + 3];
    }
    int masked = 0;
    for (int y = 0; y < QH; y++) {
        for (int x = 0; x < W; x++) {
            const uint8_t *a = &s_qScene1x[((size_t)y * W + x) * 4];
            masked += a[0] < 40 && y >= 150 && y < 300; /* the mirage darkened it */
        }
    }
    printf("  (q) %d mirage frames at 1x (NTSC, the dumps' sprites): max difference %d LSB, %d "
           "darkened pixels in the shine's rows\n",
           Q_FRAMES, worst, masked);
    rd_shutdown();

    /* 4x and full height: SCENE against the 1x model */
    static const struct {
        const char *what;
        float scale;
        int fullHeight;
    } kModes[2] = {{"Enhanced 4x", 4.0f, 0}, {"Enhanced 4x, full height", 4.0f, 1}};

    for (int m = 0; m < 2; m++) {
        qInit(kModes[m].what, RD_PRESET_ENHANCED, kModes[m].scale, kModes[m].fullHeight);
        clearAll();
        for (int n = 0; n < Q_FRAMES; n++) {
            qFrame(n);
            dl_Swap();
        }
        rhi_wait_idle();
        const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
        uint8_t *img = t ? malloc((size_t)t->tw * t->th * 4) : NULL;
        uint32_t w = 0, h = 0;
        if (!img ||
            !rd__read_target(rd_target(RD_TARGET_SCENE), img, (size_t)t->tw * t->th * 4, &w, &h)) {
            CHECK(0, "(q) %s: read SCENE", kModes[m].what);
            free(img);
            rd_shutdown();
            continue;
        }
        const float sx = (float)w / (float)W, sy = (float)h / (float)QH;
        int farBad = 0, farMax = 0, inMax = 0, inBad = 0, nFar = 0, nIn = 0;
        for (int y = 0; y < QH; y++) {
            for (int x = 0; x < W; x++) {
                const uint8_t *g =
                    &img[((size_t)(((float)y + 0.5f) * sy) * w + (size_t)(((float)x + 0.5f) * sx)) *
                         4];
                const uint8_t *c = &s_qScene1x[((size_t)y * W + x) * 4];
                /* away from the mask: AURA_WORK's alpha 0 within 16 GS
                   pixels; inside it: one value within 16 */
                int lo = 255, hi = 0;
                for (int yy = y - 16; yy <= y + 16; yy++) {
                    for (int xx = x - 16; xx <= x + 16; xx++) {
                        const int a = yy < 0 || yy >= QH || xx < 0 || xx >= W
                                          ? 0
                                          : s_qMask1x[(size_t)yy * W + xx];
                        lo = a < lo ? a : lo;
                        hi = a > hi ? a : hi;
                    }
                }
                const int far = hi == 0, in = lo == hi && lo != 0;
                int d = 0;
                for (int k = 0; k < 3; k++) {
                    const int e = abs((int)g[k] - (int)c[k]);
                    d = e > d ? e : d;
                }
                if (far) {
                    nFar++;
                    farBad += d != 0;
                    farMax = d > farMax ? d : farMax;
                } else if (in) {
                    nIn++;
                    inBad += d > 4;
                    inMax = d > inMax ? d : inMax;
                }
            }
        }
        printf("  (q) %s: SCENE against the 1x model: away from the mask %d of %d pixels differ "
               "(max %d), inside it %d of %d by more than 4 (max %d); %u sprites took the "
               "above-1x kernels\n",
               kModes[m].what, farBad, nFar, farMax, inBad, nIn, inMax, g_rd.above1xDraws);
        CHECK(g_rd.above1xDraws > 0, "(q) %s: no sprite took the above-1x kernels", kModes[m].what);
        CHECK(farBad == 0, "(q) %s: %d pixels away from the mirage's mask differ from 1x",
              kModes[m].what, farBad);
        CHECK(inBad == 0, "(q) %s: %d pixels inside the mask differ from 1x by more than 4",
              kModes[m].what, inBad);
        free(img);
        CHECK(rhi_vk_validation_error_count() == 0, "(q) %s: %u validation errors", kModes[m].what,
              rhi_vk_validation_error_count());
        rd_shutdown();
    }
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* ============================= (p) the mirage in the presenter's presents
 *
 * The model viewer's dumps: the GS model of their own commands leaves
 * the Queen's face outside the mirage's mask in every frame, yet the
 * presented picture blackens it in some.  Here the mirage frame of (q) with
 * the shine material a keyed world draw (as her list-8 meshes are) behind
 * a nearer face band, presented as the presenter does: a tick's first
 * present and a later one (dt 0.5), and between two ticks whose shine
 * moves.  In every present:
 *   - the face band's interior (rows 200..240: mask 0, clear of the
 *     bilinear bleed) is the picture drawn into it, byte for byte;
 *   - FEED128's alpha as the paste reads it (the frame replayed up to the
 *     paste) is the WORK0 mask: 0 over the face's rows, set over the
 *     shine's, never SCENE's alpha (which the copy for the next tick
 *     writes into FEED128). */
static const char kQShine;

static void qPresShine(int dx)
{
    static const uint8_t c[4] = {0x80, 0x80, 0x80, 0x7F};
    dl_SetDLPriority(8);
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), W, QH, 0);
    rd_test_gs(0x5346D);
    rd_z_write(1);
    rd_abe(1);
    rd_blend_func(RD_BLEND_LERP_AS, 0x80);
    rd_fba(0);
    rd_pabe(0);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_qShineTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_gouraud(1);
    const int32_t ox = (2048 - W / 2) * 16, oy = (2048 - QH / 2) * 16;
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = ox + (100 + dx) * 16;
    v[0].y = oy + 150 * 16;
    v[1].x = ox + (400 + dx) * 16;
    v[1].y = oy + 300 * 16;
    v[0].z = v[1].z = Q_Z_SHINE;
    v[1].s = 300.0f * 16.0f;
    v[1].t = 150.0f * 16.0f;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, c, 4);
    memcpy(v[1].rgba, c, 4);
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, RD_KEY(&kQShine, 8, 0));
}

static void qPresTick(int dx)
{
    dl_SetDLPriority(0);
    putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 0, QH, 0, 1);
    putImage(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, QH, 180, 260, Q_Z_FACE, 1);
    FullScreenEffectBefore();
    qPresShine(dx);
    FullScreenEffectAfter();
    dl_Swap();
}

/* the frame for a present; with probe, everything after the paste (the
 * band clear and the copy of SCENE into FEED128) made a NOP */
static const RdFrame *qPresBuild(float alpha, int first, int probe)
{
    const RdFrame *f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), alpha, first, NULL);
    if (!f || !probe) {
        return f;
    }
    RdCmdList *cl = (RdCmdList *)&f->lists[8];
    RdStateBlock st = f->startState;
    for (int l = 0; l < 8; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            rd__apply_state(&st, &f->lists[l].cmds[i]);
        }
    }
    int pasted = 0;
    for (uint32_t i = 0; i < cl->count; i++) {
        RdCmd *c = &cl->cmds[i];
        if (rd__apply_state(&st, c)) {
            continue;
        }
        if (pasted && c->type == RDC_POST_STUB && st.color == rd_target(RD_TARGET_FEED128).id) {
            c->type = RDC_NOP;
        }
        pasted |= c->type == RDC_POST_STUB && st.color == rd_target(RD_TARGET_SCENE).id;
    }
    return f;
}

static int s_qpFaceBad, s_qpFeedBad, s_qpPresents, s_qpShineSet;

static void qPresCheck(const char *what, float alpha, int first)
{
    static uint8_t scene[W * QH * 4], feed[128 * 128 * 4];
    uint32_t w = 0, h = 0;
    /* the probe: FEED128 as the paste read it */
    const RdFrame *f = qPresBuild(alpha, 0, 1);
    if (f && rd__replay_frame(f, 0, false)) {
        rhi_wait_idle();
        if (rd__read_target(rd_target(RD_TARGET_FEED128), feed, sizeof(feed), &w, &h)) {
            int bad = 0, set = 0;
            for (int x = 8; x < 120; x++) {
                bad += feed[(55 * 128 + x) * 4 + 3] != 0;  /* rows 220 / 4 of the face */
                set += feed[(40 * 128 + 50) * 4 + 3] != 0; /* the shine above it */
            }
            s_qpFeedBad += bad;
            s_qpShineSet += set != 0;
            CHECK(bad == 0,
                  "(p) %s: FEED128's alpha the paste reads over the face's rows: %d of "
                  "112 texels set (SCENE's alpha?)",
                  what, bad);
        }
    }
    f = qPresBuild(alpha, first, 0);
    if (!f || !rd__replay_frame(f, 0, false)) {
        CHECK(0, "(p) %s: replay", what);
        return;
    }
    rhi_wait_idle();
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), scene, sizeof(scene), &w, &h)) {
        CHECK(0, "(p) %s: read SCENE", what);
        return;
    }
    int bad = 0;
    for (int y = 200; y < 240; y++) {
        for (int x = 140; x < 360; x++) {
            bad += memcmp(&scene[((size_t)y * W + x) * 4], &s_img[((size_t)y * W + x) * 4], 3) != 0;
        }
    }
    s_qpFaceBad += bad;
    s_qpPresents++;
    CHECK(bad == 0, "(p) %s: %d of 8800 face pixels (mask 0) changed by the mirage", what, bad);
}

static void checkQueenPresents(void)
{
    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    ScreenHeight = QH;
    systemStatus[0] = 0;
    setStage(0, 2, 128);
    GlobalStageSetting.feedbackCol[0] = GlobalStageSetting.feedbackCol[1] =
        GlobalStageSetting.feedbackCol[2] = 64;
    GlobalTimer = 0;
    qInit("presents", RD_PRESET_ORIGINAL, 0.0f, 0);
    const uint8_t interpolate = g_rd.settings.interpolate;
    g_rd.settings.interpolate = 1; /* the replays are the presents' */
    clearAll();
    for (int n = 0; n < 6; n++) {
        qPresTick((n % 3) * 8); /* the shine moves */
        char what[64];
        snprintf(what, sizeof(what), "tick %d, first present at 0.5", n);
        qPresCheck(what, 0.5f, 1);
        snprintf(what, sizeof(what), "tick %d, later present at 1", n);
        qPresCheck(what, 1.0f, 0);
    }
    g_rd.settings.interpolate = interpolate;
    printf("  (p) %d mirage presents (first and later, the shine moving): %d face pixels "
           "changed, %d FEED128 texels over the face set at the paste, the shine's mask set in "
           "%d\n",
           s_qpPresents, s_qpFaceBad, s_qpFeedBad, s_qpShineSet);
    CHECK(s_qpShineSet == s_qpPresents, "(p) the shine's mask is set in every probe");
    CHECK(rhi_vk_validation_error_count() == 0, "(p) %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* (n) at a scale, the GPU alone: Enhanced 2x with the full-height scene
 * (DISPLAY 2 x 4 times its GS size), DISPLAY_HELD has DISPLAY's texels and
 * every tick presented 10 times leaves the DISPLAY one present a tick does
 * (FNV-1a of the readback).  Re-initialises rd, after the queen's cases. */
static uint64_t fnv64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

static void checkMotionBlurPresentsScaled(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 4.0f / 3.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = 2.0f;
    st.fullHeightScene = 1;
    st.interpolate = 1; /* rd_end_frame leaves the replays to the presents */
    if (!rd_init(W, H, &st, NULL)) {
        CHECK(0, "(n) rd_init at 2x");
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    makeImage(5, 0);
    s_imgTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");
    const RdTargetRec *d = rd__target_rec(rd_target(RD_TARGET_DISPLAY).id);
    const RdTargetRec *h = rd__target_rec(rd_target(RD_TARGET_DISPLAY_HELD).id);
    CHECK(d && h && d->tw == h->tw && d->th == h->th && d->sx == h->sx && d->sy == h->sy &&
              d->tw == 2 * N_DW && d->th == 4 * N_DH,
          "(n) 2x full height: DISPLAY %u x %u, DISPLAY_HELD %u x %u", d ? d->tw : 0, d ? d->th : 0,
          h ? h->tw : 0, h ? h->th : 0);
    const size_t size = d ? (size_t)d->tw * d->th * 4 : 0;
    uint8_t *px = size ? malloc(size) : NULL;
    static const int kN[2] = {1, 10};
    uint64_t hash[2][N_TICKS];
    memset(hash, 0, sizeof(hash));
    for (int r = 0; px && r < 2; r++) {
        clearAll();
        for (int t = 0; t < N_TICKS; t++) {
            nRecord(32);
            dl_Swap();
            for (int k = 0; k < kN[r]; k++) {
                const RdFrame *f = rd__interp_frame(rd__prev_frame(), rd__last_frame(),
                                                    (float)(k + 1) / kN[r], k == 0, NULL);
                CHECK(f && rd__replay_frame(f, 0, false), "(n) 2x N %d tick %d present %d", kN[r],
                      t, k);
            }
            rhi_wait_idle();
            uint32_t w = 0, hh = 0;
            CHECK(rd__read_target(rd_target(RD_TARGET_DISPLAY), px, size, &w, &hh) &&
                      (size_t)w * hh * 4 == size,
                  "(n) 2x: read DISPLAY");
            hash[r][t] = fnv64(px, size);
        }
    }
    int differ = 0;
    for (int t = 0; t < N_TICKS; t++) {
        differ += hash[0][t] != hash[1][t];
    }
    CHECK(px && differ == 0, "(n) 2x full height: %d of %d ticks differ between 1 and 10 presents",
          differ, N_TICKS);
    printf("  (n) Enhanced 2x, full height (DISPLAY %u x %u): 10 presents a tick against one, %d "
           "of %d ticks' DISPLAYs differ\n",
           d ? d->tw : 0, d ? d->th : 0, differ, N_TICKS);
    free(px);
    CHECK(rhi_vk_validation_error_count() == 0, "(n) 2x: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
}

#include "rd_blur_smooth_test.inc"

/* ================== (w) the aura's feedback chain on a wide frame
 *
 * The Queen's mirage (feedback mode 2: AURA_WORK's alpha reduced into
 * WORK0, copied into FEED128's alpha, FEED128 pasted over SCENE through
 * that alpha, SCENE copied back into FEED128) and the auras of modes 1 and
 * 3 (FEED128 tapped into AURA_TAP, blended into AURA_WORK and SCENE, then
 * reduced back into FEED128), run the same frames at W = 512 and W = 2048:
 * the scene the 512-wide picture in the frame's 4:3 columns (repeated
 * beside them), the shine at the same place in them.  Every target against
 * the CPU model exactly at both widths; the 4:3 columns of the wide SCENE
 * are the 4:3 run's byte for byte (mode 2 every column: its mask is zero
 * near the 4:3 edges, so what lies beside them never reaches the paste;
 * modes 1 and 3 their taps reach a feedback texel (4 pixels) and more a
 * frame, compared 8 pixels a frame in from the edges); in mode 2 the
 * feedback area's alpha as the paste reads it is at most 0x80 and set
 * somewhere (the mask). */
static uint8_t s_wCentre[2][W * QH * 4];

/* the image across a target fw wide, its 4:3 columns the image's columns
 * 0 .. 511 (REPEAT beside them), nearest, Z ALWAYS */
static void putImageAt(RdTarget t, RdTarget depth, int fw, int gh, int py0, int py1, uint32_t z,
                       int zwrite)
{
    const int side = (fw - W) / 2;
    const int u0 = ((W - side % W) % W) * 16;
    rd_set_target(t, depth, (uint32_t)fw, (uint32_t)gh, 0);
    rd_test_gs(0x30000);
    rd_z_write(zwrite);
    rd_abe(0);
    rd_fba(0);
    rd_pabe(0);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_sampler_wrap(RD_WRAP_REPEAT, RD_WRAP_CLAMP);
    rd_texture(s_imgTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)(0x8000 - fw / 2 * 16);
    p.rect[1] = (float)(0x8000 - gh / 2 * 16 + py0 * 16);
    p.rect[2] = (float)(0x8000 + fw / 2 * 16);
    p.rect[3] = (float)(0x8000 - gh / 2 * 16 + py1 * 16);
    p.uv[0] = (float)u0;
    p.uv[1] = (float)(py0 * 16);
    p.uv[2] = (float)(u0 + fw * 16);
    p.uv[3] = (float)(py1 * 16);
    p.rgba[0] = p.rgba[1] = p.rgba[2] = p.rgba[3] = 0x80;
    p.z = z;
    p.scalar[0] = p.scalar[1] = 512.0f;
    p.scalar[2] = 1.0f;
    rd_post(RD_POST_FLARE, &p);
}

static void cpuFree(void)
{
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        free(s_cpu[i].c);
        free(s_cpu[i].z);
    }
    memset(s_cpu, 0, sizeof(s_cpu));
    free(s_old);
    s_old = NULL;
}

static void checkFeedbackWide(void)
{
    static const int kW[2] = {W, 2048}, kModes[3] = {2, 1, 3};
    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    ScreenHeight = QH;
    systemStatus[0] = 0; /* NTSC, the Queen's dumps' */
    GlobalTimer = 0;
    rd_shutdown();
    cpuFree();
    for (int m = 0; m < 3; m++) {
        const int mode = kModes[m];
        int worst = 0;
        for (int r = 0; r < 2; r++) {
            const int fw = kW[r];
            char what[64];
            setFrameWidth(fw);
            setStage(0, mode, mode == 2 ? 128 : 0x40);
            if (mode == 2) { /* the Queen's feedbackCol */
                GlobalStageSetting.feedbackCol[0] = GlobalStageSetting.feedbackCol[1] =
                    GlobalStageSetting.feedbackCol[2] = 64;
            }
            snprintf(what, sizeof(what), "(w) mode %d W %d", mode, fw);
            qInit(what, RD_PRESET_ORIGINAL, 0.0f, 0);
            cpuInit();
            clearAll();
            s_feedMaxA = s_feedSetA = 0;
            s_feedProbe = mode == 2;
            for (int n = 0; n < Q_FRAMES; n++) {
                dl_SetDLPriority(0);
                putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 0, QH, 0,
                           1);
                putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 180, 260,
                           Q_Z_FACE, 1);
                FullScreenEffectBefore();
                qShine(n);
                FullScreenEffectAfter();
                snprintf(what, sizeof(what), "(w) mode %d W %d frame %d", mode, fw, n);
                const int d = runFrame(what, failures < 10);
                worst = d > worst ? d : worst;
            }
            s_feedProbe = 0;
            const CpuT *sc = &s_cpu[RD_TARGET_SCENE];
            const int c0 = (fw - W) / 2;
            for (int y = 0; sc->c && sc->w == fw && y < QH; y++) {
                memcpy(&s_wCentre[r][(size_t)y * W * 4], &sc->c[((size_t)y * fw + c0) * 4],
                       (size_t)W * 4);
            }
            CHECK(sc->c && sc->w == fw && sc->h == QH, "(w) mode %d W %d: SCENE %dx%d", mode, fw,
                  sc->w, sc->h);
            if (mode == 2) {
                printf("  (w) mirage at W %d: FEED128's alpha at the paste at most 0x%02x, %d "
                       "texel reads set\n",
                       fw, s_feedMaxA, s_feedSetA);
                CHECK(s_feedSetA > 0 && s_feedMaxA <= 0x80,
                      "(w) mode 2 W %d: the paste reads FEED128's alpha up to 0x%x (want the "
                      "mask's, <= 0x80: above it the paste extrapolates)",
                      fw, s_feedMaxA);
            }
            CHECK(rhi_vk_validation_error_count() == 0, "(w) mode %d W %d: %u validation errors",
                  mode, fw, rhi_vk_validation_error_count());
            rd_shutdown();
            cpuFree();
        }
        const int margin = mode == 2 ? 0 : 8 * (Q_FRAMES + 2);
        int bad = 0, n = 0, darkened = 0;
        for (int y = 0; y < QH; y++) {
            for (int x = margin; x < W - margin; x++) {
                const size_t i = ((size_t)y * W + x) * 4;
                bad += memcmp(&s_wCentre[0][i], &s_wCentre[1][i], 4) != 0;
                darkened += memcmp(&s_wCentre[0][i], &s_img[((size_t)y * W + x) * 4], 3) != 0;
                n++;
            }
        }
        printf("  (w) mode %d, %d frames at W 512 and 2048 (max difference against the model %d "
               "LSB): %d of %d pixels of the 4:3 columns (%d in from their edges) differ, %d "
               "changed by the effect\n",
               mode, Q_FRAMES, worst, bad, n, margin, darkened);
        CHECK(bad == 0,
              "(w) mode %d: the wide frame's 4:3 columns differ from the 4:3 run's in %d "
              "pixels",
              mode, bad);
        CHECK(darkened > 1000, "(w) mode %d: the effect changed the picture (%d pixels)", mode,
              darkened);
    }
    setFrameWidth(W);
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* (w) the glow (post 4) and the depth of field (post 2) on the device at
 * W 512 and 1536 (a = 256 buffer pixels: the glow's shrink anchors,
 * a * d / 256, and spans, 3 * 4096 * d / 4096, land on the GS's 1/16 grid
 * exactly, so the glow's 4:3 columns are the 4:3 run's byte for byte, 32
 * pixels in from their edges, where the kernel reaches what lies beside
 * them); the depth of field's anchors, a * cur / 254, round to that grid,
 * so its 4:3 columns are the 4:3 run's within the bilinear weights' 1/16
 * steps (no channel more than 24 off, at most 1% of the pixels more than
 * 4).  Before the anchor (a scale about the buffer's left edge) the
 * texel phase drifted by a * d / 4096 pixels across the 4:3 columns. */
static void checkShrinksWideDevice(void)
{
    static const int kW[2] = {W, 1536}, kPost[2] = {4, 2};
    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    const int margin = 32;
    ScreenHeight = QH;
    systemStatus[0] = 0;
    GlobalTimer = 0;
    rd_shutdown();
    cpuFree();
    for (int m = 0; m < 2; m++) {
        const int post = kPost[m];
        for (int r = 0; r < 2; r++) {
            const int fw = kW[r];
            char what[64];
            setFrameWidth(fw);
            setStage(post, 0, 64);
            snprintf(what, sizeof(what), "(w) post %d W %d", post, fw);
            qInit(what, RD_PRESET_ORIGINAL, 0.0f, 0);
            cpuInit();
            clearAll();
            for (int n = 0; n < 2; n++) {
                dl_SetDLPriority(0);
                putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 0, QH, 0,
                           1);
                putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 180, 260,
                           Q_Z_FACE, 1);
                FullScreenEffectBefore();
                FullScreenEffectAfter();
                snprintf(what, sizeof(what), "(w) post %d W %d frame %d", post, fw, n);
                runFrame(what, failures < 10);
            }
            const CpuT *sc = &s_cpu[RD_TARGET_SCENE];
            const int c0 = (fw - W) / 2;
            for (int y = 0; sc->c && sc->w == fw && y < QH; y++) {
                memcpy(&s_wCentre[r][(size_t)y * W * 4], &sc->c[((size_t)y * fw + c0) * 4],
                       (size_t)W * 4);
            }
            CHECK(sc->c && sc->w == fw && sc->h == QH, "(w) post %d W %d: SCENE %dx%d", post, fw,
                  sc->w, sc->h);
            CHECK(rhi_vk_validation_error_count() == 0, "(w) post %d W %d: %u validation errors",
                  post, fw, rhi_vk_validation_error_count());
            rd_shutdown();
            cpuFree();
        }
        int over4 = 0, worst = 0, n = 0, changed = 0;
        for (int y = 0; y < QH; y++) {
            for (int x = margin; x < W - margin; x++) {
                const size_t i = ((size_t)y * W + x) * 4;
                int d = 0;
                for (int c = 0; c < 4; c++) {
                    const int e = abs((int)s_wCentre[0][i + c] - (int)s_wCentre[1][i + c]);
                    d = e > d ? e : d;
                }
                worst = d > worst ? d : worst;
                over4 += d > 4;
                changed += memcmp(&s_wCentre[0][i], &s_img[((size_t)y * W + x) * 4], 3) != 0;
                n++;
            }
        }
        printf("  (w) post %d at W 512 and 1536: the 4:3 columns (%d in from their edges) at most "
               "%d LSB off the 4:3 run's, %d of %d pixels more than 4, %d changed by the effect\n",
               post, margin, worst, over4, n, changed);
        if (post == 4) {
            CHECK(worst == 0,
                  "(w) the glow: the wide frame's 4:3 columns differ from the 4:3 "
                  "run's by up to %d LSB",
                  worst);
        } else {
            CHECK(worst <= 24 && over4 * 100 <= n,
                  "(w) the depth of field: the wide frame's 4:3 columns differ from the 4:3 run's "
                  "by up to %d LSB, %d pixels more than 4",
                  worst, over4);
        }
        CHECK(changed > 1000, "(w) post %d: the effect changed the picture (%d pixels)", post,
              changed);
    }
    setFrameWidth(W);
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* (w) the motion blur on the device at W 512, 688, 1376 and 2560: the
 * loop of checkMotionBlur (DISPLAY blended over SCENE at FIX 0x40 with the
 * flare's linear filter leaked in, SCENE reduced into DISPLAY) over the
 * picture putImageAt lays across the frame (the 4:3 columns the image's,
 * whatever is beside them), every target against the CPU model exactly.
 * The game's sprite (staticBlur.c MotionBlur) spans the frame W * 16 wide
 * with a UV W * 16 wide, both from the frame's left edge: one texel a pixel
 * at the texel centre at every width, so the 4:3 columns of the wide
 * SCENE and DISPLAY are the 4:3 run's byte for byte, but for the pixels
 * that what lies beside them can reach: the reduction's bilinear reads one
 * pixel to the right a frame, so frames + 2 pixels in from the edges. */
static uint8_t s_mbCentre[2][2][W * QH * 4]; /* [run][SCENE, DISPLAY] */

static void checkMotionBlurWide(void)
{
    static const int kW[4] = {W, 688, 1376, 2560};

    enum { FRAMES = 6 };

    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    const int margin = FRAMES + 2;
    ScreenHeight = QH;
    systemStatus[0] = 0;
    GlobalTimer = 0;
    currentScreenWidth = 0;
    rd_shutdown();
    cpuFree();
    makeImage(5, 0);
    for (int r = 0; r < 4; r++) {
        const int fw = kW[r];
        char what[64];
        setFrameWidth(fw);
        setStage(0, 0, 0);
        snprintf(what, sizeof(what), "(w) motion blur W %d", fw);
        qInit(what, RD_PRESET_ORIGINAL, 0.0f, 0);
        cpuInit();
        clearAll();
        rd_update_texture(s_imgTex, s_img);
        for (int n = 0; n < FRAMES; n++) {
            dl_SetDLPriority(0);
            putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 0, QH, 0, 1);
            dl_SetDLPriority(7);
            rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
            rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
            SetMotionBlur(0x40);
            MotionBlur();
            putReduction();
            snprintf(what, sizeof(what), "(w) motion blur W %d frame %d", fw, n);
            runFrame(what, failures < 10);
        }
        const int c0 = (fw - W) / 2;
        static const RdTargetId kT[2] = {RD_TARGET_SCENE, RD_TARGET_DISPLAY};
        for (int t = 0; t < 2; t++) {
            const CpuT *ct = &s_cpu[kT[t]];
            CHECK(ct->c && ct->w == fw, "(w) motion blur W %d: %s %dx%d", fw, tname(kT[t] + 1u),
                  ct->w, ct->h);
            for (int y = 0; ct->c && ct->w == fw && y < ct->h && y < QH; y++) {
                memcpy(&s_mbCentre[r ? 1 : 0][t][(size_t)y * W * 4],
                       &ct->c[((size_t)y * fw + c0) * 4], (size_t)W * 4);
            }
            if (r == 0 || !ct->c || ct->w != fw) {
                continue;
            }
            int bad = 0, n = 0, blurred = 0;
            for (int y = 0; y < ct->h && y < QH; y++) {
                for (int x = margin; x < W - margin; x++) {
                    const size_t i = ((size_t)y * W + x) * 4;
                    bad += memcmp(&s_mbCentre[0][t][i], &s_mbCentre[1][t][i], 4) != 0;
                    blurred += t == 0 && memcmp(&s_mbCentre[1][t][i], &s_img[i], 3) != 0;
                    n++;
                }
            }
            printf("  (w) motion blur W %d: %s's 4:3 columns (%d in from their edges): %d of %d "
                   "pixels differ from the 4:3 run's\n",
                   fw, tname(kT[t] + 1u), margin, bad, n);
            CHECK(bad == 0,
                  "(w) motion blur W %d: %s's 4:3 columns differ from the 4:3 run's in %d pixels",
                  fw, tname(kT[t] + 1u), bad);
            if (t == 0) {
                CHECK(blurred > 1000, "(w) motion blur W %d: the effect changed SCENE (%d pixels)",
                      fw, blurred);
            }
        }
        CHECK(rhi_vk_validation_error_count() == 0, "(w) motion blur W %d: %u validation errors",
              fw, rhi_vk_validation_error_count());
        rd_shutdown();
        cpuFree();
    }
    setFrameWidth(W);
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* (w) the glow's left edge on a wide frame (issue 129, rd_blur_border.c
 * glowEdge): the flare (post 1) at W 848 (the 2364 x 1080 phone's frame:
 * WORK1 424 wide, pass 1 of blur(n) 5..8 sixteenths right of WORK1's
 * pixel 0, column 0 left) and 2048 (48:9: 24..37 sixteenths, columns 0..2
 * left), and W 512 (4:3, no strip).  One frame against the CPU model (the
 * strip in it), the strips counted (ten a frame with the pairs the blur
 * fold draws as one pass, which draw none; none at 4:3) and WORK1's
 * columns 0 .. n - 1 the bytes of column n (n from the last pair's
 * shrink, 25 sixteenths).  Then a frame of the same picture with nothing
 * cleared in it, replayed with every work buffer first cleared to magenta
 * and then to green (rd__set_work_clear: rd_replay_tool --work-clear): its
 * SCENE (the glow's output, what the reduction presents) the same bytes
 * both ways, so the frame reads no texel it did not draw; with the strip
 * off the two differ at a wide frame, and only in the left eighth of the
 * frame (the band the phone showed). */
static uint8_t s_geA[2048 * QH * 4], s_geB[2048 * QH * 4], s_geW1[1024 * 256 * 4];

static void geReplay(const char *what, const RdFrame *f, uint32_t rgb, uint8_t *out, int fw)
{
    uint32_t w = 0, h = 0;
    rd__set_work_clear(true, rgb);
    rhi_wait_idle();
    CHECK(rd__replay_frame(f, 0, false), "%s: replay over %06X", what, (unsigned)rgb);
    rhi_wait_idle();
    rd__set_work_clear(false, 0);
    CHECK(rd__read_target(rd_target(RD_TARGET_SCENE), out, (size_t)fw * QH * 4, &w, &h) &&
              (int)w == fw && h == QH,
          "%s: read SCENE (%ux%u)", what, w, h);
}

/* the largest column where a and b differ, -1 where they do not */
static int geDiffColumn(const uint8_t *a, const uint8_t *b, int fw)
{
    int last = -1;
    for (int y = 0; y < QH; y++) {
        for (int x = fw - 1; x > last; x--) {
            if (memcmp(&a[((size_t)y * fw + x) * 4], &b[((size_t)y * fw + x) * 4], 4) != 0) {
                last = x;
                break;
            }
        }
    }
    return last;
}

static void checkGlowEdgeWide(void)
{
    static const int kW[3] = {W, 848, 2048};
    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    ScreenHeight = QH;
    systemStatus[0] = 0;
    GlobalTimer = 0;
    rd_shutdown();
    cpuFree();
    for (int r = 0; r < 3; r++) {
        const int fw = kW[r], ww = fw / 2;
        char what[64];
        setFrameWidth(fw);
        setStage(1, 0, 64);
        snprintf(what, sizeof(what), "(w) glow edge W %d", fw);
        qInit(what, RD_PRESET_ORIGINAL, 0.0f, 0);
        cpuInit();
        clearAll();
        rd__set_glow_edge_fill(true);
        s_modelGlowEdge = 1;
        const uint64_t e0 = rd__glow_edge_count();
        dl_SetDLPriority(0);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 0, QH, 0, 1);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 180, 260,
                   Q_Z_FACE, 1);
        FullScreenEffectBefore();
        FullScreenEffectAfter();
        runFrame(what, failures < 10);
        const uint64_t strips = rd__glow_edge_count() - e0;
        /* a pair the blur fold draws as one pass draws no pass 1 and so no
           strip (the profile folds on every class but Mali) */
        const uint32_t folds = g_rdPerf.blurFolds;
        CHECK(fw == W ? strips == 0 : (strips > 0 && strips + folds == 10),
              "%s: %llu strips drawn, %u pairs folded (ten together a replay, no strip at 4:3)",
              what, (unsigned long long)strips, folds);
        /* the last pass 1 (blur(19), shrink 25): columns 0 .. n - 1 left */
        const int a = ww / 2 - 128, sh = (a * 16 * 25 + 2048) / 4096, n = (sh + 15) / 16;
        uint32_t w1w = 0, w1h = 0;
        CHECK(rd__read_target(rd_target(RD_TARGET_WORK1), s_geW1, sizeof(s_geW1), &w1w, &w1h) &&
                  (int)w1w == ww && w1h == 256,
              "%s: read WORK1 (%ux%u)", what, w1w, w1h);
        int same = 1;
        for (int y = 0; y < 127 && (int)w1w == ww; y++) {
            for (int x = 0; x < n; x++) {
                same &= memcmp(&s_geW1[((size_t)y * ww + x) * 4], &s_geW1[((size_t)y * ww + n) * 4],
                               4) == 0;
            }
        }
        CHECK(same, "%s: WORK1's columns 0..%d are not column %d's bytes", what, n - 1, n);

        /* the same picture again, nothing cleared in the frame */
        dl_SetDLPriority(0);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 0, QH, 0, 1);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 180, 260,
                   Q_Z_FACE, 1);
        FullScreenEffectBefore();
        FullScreenEffectAfter();
        dl_Swap();
        rhi_wait_idle();
        const RdFrame *f = rd__last_frame();
        geReplay(what, f, 0xFF00FFu, s_geA, fw);
        geReplay(what, f, 0x00FF00u, s_geB, fw);
        const int on = geDiffColumn(s_geA, s_geB, fw);
        rd__set_glow_edge_fill(false);
        geReplay(what, f, 0xFF00FFu, s_geA, fw);
        geReplay(what, f, 0x00FF00u, s_geB, fw);
        rd__set_glow_edge_fill(true);
        const int off = geDiffColumn(s_geA, s_geB, fw);
        printf("  (w) glow edge at W %d: %llu strips (%u pairs folded), WORK1 columns 0..%d drawn "
               "as column %d; SCENE over magenta and green work buffers differs up to column %d "
               "with the strip, %d without it\n",
               fw, (unsigned long long)strips, folds, n - 1, n, on, off);
        CHECK(on < 0,
              "%s: SCENE depends on what the work buffers held before the frame (up to "
              "column %d)",
              what, on);
        if (fw == W) {
            CHECK(off < 0,
                  "%s: at 4:3 SCENE depends on the work buffers' old texels without the "
                  "strip (column %d)",
                  what, off);
        } else {
            CHECK(off >= 0 && off < fw / 8,
                  "%s: without the strip SCENE should differ in the left eighth alone (column %d)",
                  what, off);
        }
        CHECK(rhi_vk_validation_error_count() == 0, "%s: %u validation errors", what,
              rhi_vk_validation_error_count());
        rd_shutdown();
        cpuFree();
    }
    setFrameWidth(W);
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

static void checkPipelines(void);

/* (b) the blur fold on the native frame (rd_replay_tiler.c, "the blur pair
 * fold"): the flare (post 1, blur(n) n = 10..19) at W 512 (4:3), 688
 * (16:9), 2048 (48:9) and ICO_FRAME_W_MAX (2560, 60:9) with the fold on,
 * its pairs counted (9, 9, 8 and 8 a chain: the geometry in that comment),
 * every target against the CPU model of the two passes and the glow's edge
 * strip exactly; the same recorded frame replayed with the fold off, every
 * target the model's again, so the folded and the unfolded chains write the
 * same bytes at every width; then with the strip off (rd__set_glow_edge_fill,
 * the model without it), where the fold may not stand in for the strip:
 * every target still the model's (nine folds at 4:3, where the strip never
 * applies; on a wide frame whatever the rules leave, printed). */
static void checkBlurFoldWide(void)
{
    static const struct {
        int fw;
        uint32_t folds;
    } kF[4] = {{W, 9}, {688, 9}, {2048, 8}, {ICO_FRAME_W_MAX, 8}};

    const int screenHeight = ScreenHeight, status0 = systemStatus[0];
    ScreenHeight = QH;
    systemStatus[0] = 0;
    GlobalTimer = 0;
    rd_shutdown();
    cpuFree();
    for (int r = 0; r < 4; r++) {
        const int fw = kF[r].fw;
        char what[64], again[112];
        setFrameWidth(fw);
        setStage(1, 0, 64);
        snprintf(what, sizeof(what), "(b) blur fold W %d", fw);
        qInit(what, RD_PRESET_ORIGINAL, 0.0f, 0);
        cpuInit();
        if (!g_rd.fs[RD_FS_FX_BLUR2].id) {
            printf("  %s: skipped (no fx_blur2_ps)\n", what);
            rd_shutdown();
            cpuFree();
            continue;
        }
        const bool was = g_rd.profile.blurFold;
        g_rd.profile.blurFold = true;
        rd__set_glow_edge_fill(true);
        s_modelGlowEdge = 1;
        clearAll(); /* in the frame: each replay of it starts from the same targets */
        dl_SetDLPriority(0);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 0, QH, 0, 1);
        putImageAt(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), fw, QH, 180, 260,
                   Q_Z_FACE, 1);
        FullScreenEffectBefore();
        FullScreenEffectAfter();
        const int dOn = runFrame(what, failures < 10);
        const uint32_t on = g_rdPerf.blurFolds;
        CHECK(on == kF[r].folds, "%s: %u pairs folded, %u expected", what, on, kF[r].folds);
        const RdFrame *f = rd__last_frame();

        /* the same frame, the fold off */
        g_rd.profile.blurFold = false;
        snprintf(again, sizeof(again), "%s, the fold off", what);
        rhi_wait_idle();
        CHECK(rd__replay_frame(f, 0, false), "%s: replay", again);
        rhi_wait_idle();
        const uint32_t off = g_rdPerf.blurFolds;
        const int dOff = compareAll(again, failures < 10);
        CHECK(off == 0, "%s: %u pairs folded", again, off);

        /* the strip off: the true pairs are refused on a wide frame */
        g_rd.profile.blurFold = true;
        rd__set_glow_edge_fill(false);
        s_modelGlowEdge = 0;
        snprintf(again, sizeof(again), "%s, the glow's edge strip off", what);
        s_unexpected = 0;
        cpuFrame(f);
        CHECK(s_unexpected == 0, "%s: %d commands the CPU model does not know", again,
              s_unexpected);
        rhi_wait_idle();
        CHECK(rd__replay_frame(f, 0, false), "%s: replay", again);
        rhi_wait_idle();
        const uint32_t bare = g_rdPerf.blurFolds;
        const int dBare = compareAll(again, failures < 10);
        CHECK(fw != W || bare == kF[r].folds, "%s: %u pairs folded, %u expected", again, bare,
              kF[r].folds);
        rd__set_glow_edge_fill(true);
        s_modelGlowEdge = 1;
        printf("  %s: %u pairs folded (max difference %d), %u with the fold off (%d), %u without "
               "the glow's edge strip (%d)\n",
               what, on, dOn, off, dOff, bare, dBare);
        checkPipelines(); /* the fold's key enumerated while the profile folds */
        CHECK(rhi_vk_validation_error_count() == 0, "%s: %u validation errors", what,
              rhi_vk_validation_error_count());
        g_rd.profile.blurFold = was;
        rd_shutdown();
        cpuFree();
    }
    setFrameWidth(W);
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* ============================================== (u) the UINT views */

static void checkUintView(void)
{
    if (!rhi_limits()->uintView || !g_rd.fs[RD_FS_FX_SPRITE_UINT].id) {
        printf("  (u) the UINT views: skipped (no RGBA8_UINT view)\n");
        return;
    }
    g_rd.profile.uintViewWork = g_rd.profile.uintViewScene = true;
    rd_reset_scene(W, H);
    /* every compared target is a named RGBA8 one of the work or the scene
       class, so each has the view */
    int views = 0, rgba8 = 0;
    for (size_t k = 0; k < sizeof(kCompared) / sizeof(kCompared[0]); k++) {
        const RdTargetRec *t = rd__target_rec(rd_target((RdTargetId)kCompared[k]).id);
        rgba8 += t && t->format == RHI_FMT_RGBA8_UNORM;
        views += t && t->uintView;
    }
    CHECK(views > 0 && views == rgba8, "(u) %d of the %d RGBA8 compared targets have the UINT view",
          views, rgba8);
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    uint32_t sprites = 0;
    int worst = 0;
    for (int post = 1; post <= 7; post++) {
        const int d = effectsFrame(post, post == 1);
        worst = d > worst ? d : worst;
        sprites += g_rdPerf.uintDraws; /* the frame's replay (runFrame) */
    }
    InitializeStaticBlur(); /* sun off */
    CHECK(sprites > 0, "(u) no sprite drew through fx_sprite_u_ps");
    printf("  (u) the UINT views: %u sprites through fx_sprite_u_ps, max difference %d\n", sprites,
           worst);
    checkPipelines(); /* the twins enumerated while the views are on */
    CHECK(rhi_vk_validation_error_count() == 0, "(u) %u validation errors",
          rhi_vk_validation_error_count());
    g_rd.profile.uintViewWork = g_rd.profile.uintViewScene = false;
    rd_reset_scene(W, H);
    rhi_wait_idle();
    rd__pipeline_cache_clear(); /* the twins are outside the reachable set again */
}

/* ============================================== (b) the blur pair fold */

static void checkBlurFold(void)
{
    if (!g_rd.fs[RD_FS_FX_BLUR2].id) {
        printf("  (b) the blur fold: skipped (no fx_blur2_ps)\n");
        return;
    }
    const bool was = g_rd.profile.blurFold;
    g_rd.profile.blurFold = true;
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    uint32_t folds = 0, frames = 0;
    int worst = 0;
    for (int post = 1; post <= 7; post++) {
        const int d = effectsFrame(post, post == 1);
        worst = d > worst ? d : worst;
        const uint32_t f = g_rdPerf.blurFolds; /* the frame's replay (runFrame) */
        folds += f;
        frames += f > 0;
        /* a chain of ten pairs folds nine, or eight where a pass 1 covers a
           column fewer than the pair before (the wide-frame fold, D2); a
           frame holds one or two chains */
        CHECK(f == 0 || f == 8 || f == 9 || (f >= 16 && f <= 18),
              "(b) post mode %d: %u pairs folded, 8 or 9 a chain expected", post, f);
    }
    InitializeStaticBlur(); /* sun off */
    CHECK(folds > 0, "(b) no blur pair folded");
    printf("  (b) the blur fold: %u pairs folded in %u frames, max difference %d\n", folds, frames,
           worst);
    checkPipelines(); /* the fold's key enumerated while the profile folds */
    CHECK(rhi_vk_validation_error_count() == 0, "(b) %u validation errors",
          rhi_vk_validation_error_count());
    g_rd.profile.blurFold = was;
    rhi_wait_idle();
    rd__pipeline_cache_clear(); /* the fold's key is outside the reachable set again */
}

/* ============================================== (l) the fx plan */

/* post modes 1..7 and 40 motion blur frames, the plan's counters summed */
static void fxPlanFrames(const char *what, uint32_t sum[3], int *worst)
{
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    for (int post = 1; post <= 7; post++) {
        const int d = effectsFrame(post, post == 1);
        *worst = d > *worst ? d : *worst;
        sum[0] += g_rdPerf.fxConstant; /* the frame's replay (runFrame) */
        sum[1] += g_rdPerf.fxAdditive;
        sum[2] += g_rdPerf.fxLocal;
    }
    InitializeStaticBlur(); /* sun off */
    setStage(0, 0, 0);
    currentScreenWidth = 0;
    clearAll();
    for (int n = 0; n < 40; n++) {
        makeImage((uint32_t)n, 1); /* noise */
        rd_update_texture(s_imgTex, s_img);
        putScene();
        dl_SetDLPriority(7);
        rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
        rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        SetMotionBlur(0x40);
        MotionBlur();
        putReduction();
        char f[64];
        snprintf(f, sizeof(f), "%s motion blur frame %d", what, n);
        const int d = runFrame(f, failures < 10);
        *worst = d > *worst ? d : *worst;
        sum[0] += g_rdPerf.fxConstant;
        sum[1] += g_rdPerf.fxAdditive;
        sum[2] += g_rdPerf.fxLocal;
    }
}

static void checkFxPlan(void)
{
    const RdProfile was = g_rd.profile;
    g_rd.profile.fxPlan = true;
    g_rd.profile.fxDstLocal = false;
    uint32_t sum[3] = {0, 0, 0};
    int worst = 0;
    fxPlanFrames("(l)", sum, &worst);
    CHECK(sum[0] > 0 && sum[1] > 0 && sum[2] == 0,
          "(l) the plan drew %u constant, %u additive, %u local", sum[0], sum[1], sum[2]);
    printf("  (l) the fx plan: %u constant, %u additive sprites, max difference %d\n", sum[0],
           sum[1], worst);
    checkPipelines();
    if (!rhi_limits()->localRead || !g_rd.fs[RD_FS_FX_LOCAL].id ||
        !g_rd.fs[RD_FS_DATE_STENCIL].id || !g_rd.layoutLocal.id) {
        printf("  (l) the local read: skipped (no local read on this device)\n");
    } else {
        /* the scene passes readable (the scene targets made again with the
           input attachment) and the destination by local read */
        g_rd.profile.dateStencil = true;
        g_rd.profile.fxDstLocal = true;
        rd_reset_scene(W, H);
        uint32_t sl[3] = {0, 0, 0};
        int wl = 0;
        fxPlanFrames("(l) local", sl, &wl);
        CHECK(sl[2] > 0, "(l) no sprite took the local read");
        printf("  (l) the local read: %u sprites by local read (%u constant, %u additive), max "
               "difference %d\n",
               sl[2], sl[0], sl[1], wl);
        checkPipelines(); /* fx_local_ps's keys enumerated while the profile has them */
        CHECK(rd__fx_local_selftest() && g_rd.profile.fxDstLocal, "(l) the local read's self-test");
    }
    CHECK(rhi_vk_validation_error_count() == 0, "(l) %u validation errors",
          rhi_vk_validation_error_count());
    g_rd.profile = was;
    rd_reset_scene(W, H);
    rhi_wait_idle();
    rd__pipeline_cache_clear(); /* the local keys are outside the reachable set again */
}

static void checkPipelines(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_REACHABLE_MAX];
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipelines %u", n);
    int fx = 0;
    for (uint32_t i = 0; i < rd__pipeline_count(); i++) {
        const RdPipeKeyInt *k = rd__pipeline_key_at(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < RD_PIPELINE_REACHABLE_MAX; j++) {
            found |= rd__pipe_key_equal(&keys[j], k);
        }
        fx += k->fs == RD_FS_FX_SPRITE;
        CHECK(found,
              "created pipeline %u (prog %u vs %u fs %u ztst %u zwrite %u) is not enumerated", i,
              k->gs.program, k->vs, k->fs, k->gs.ztst, k->gs.zwrite);
    }
    printf("  pipelines: %u created (%d fx), %u reachable\n", rd__pipeline_count(), fx, n);
}

/* the weights are each case's seconds in a whole run (255 s at -j2 on 4 cores); the
   aura case (600 frames, each fed by the one before) is the shortest a shard can be */
static const IcoShardCase k_cases[] = {
    {"bilerp_pcsx2", 1},
    {"effects", 2},
    {"dump", 1},
    {"motion_blur", 46},
    {"aura", 81},
    {"cut_presents", 1},
    {"motion_blur_presents", 40},
    {"fix128_skip", 1},
    {"motion_blur_smooth", 6},
    {"uint_view", 2},
    {"blur_fold", 2},
    {"fx_plan", 8},
    {"queen_mirage", 10},
    {"queen_presents", 1},
    {"motion_blur_presents_scaled", 41},
    {"feedback_wide", 25},
    {"shrinks_wide_device", 3},
    {"glow_edge_wide", 2},
    {"blur_fold_wide", 4},
    {"motion_blur_wide", 4},
    {"above1x", 38},
    {"effects_scaled", 4},
    {"motion_blur_smooth_scaled", 2},
};

int main(int argc, char **argv)
{
    if (ico_shard_start(argc, argv, k_cases, ICO_SHARD_COUNT(k_cases))) {
        return ico_shard_exit();
    }
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    printf("rd_blur_test\n");
    setMatrices();
    systemStatus[0] = 1; /* PAL: 512 lines */

    /* (r) recording */
    if (!rd__init_record_only(W, H)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    dl_Init();
    /* the first FullScreenEffectAfter of a run comes before any
       FullScreenEffectBefore (gsb_PostEffect, then gsb_UpdateGSSystem):
       postMode's initial 1 and feedMode's initial 2 with workBase's initial
       0x2800, 0x2C00, 0x3000, 0x3400; the buffers are still the work
       buffers, and only the flare's and the mirage's pastes draw into
       SCENE */
    FullScreenEffectAfter();
    dl_Swap();
    collectFrame(rd__last_frame());
    {
        int scene = 0, named_ = 1;
        for (int i = 0; i < s_nspr; i++) {
            scene += s_spr[i].st.color == rd_target(RD_TARGET_SCENE).id;
            named_ &= named(s_spr[i].st.color) >= 0;
        }
        CHECK(s_nspr > 20 && scene == 2 && named_,
              "first After before any Before: %d sprites, %d into SCENE (want 2)", s_nspr, scene);
    }
    checkRecordingModes();
    checkRecordingSwitches();
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    setStage(7, 0, 64);
    FullScreenEffectBefore();
    FullScreenEffectAfter();
    dl_Swap();
    collectFrame(rd__last_frame());
    {
        int eye = 0, date = 0;
        for (int i = 0; i < s_nspr; i++) {
            eye += s_spr[i].kind == RD_POST_EYE_BLUR;
            date += s_spr[i].st.ds.test.date == RD_DATE_DEST_ALPHA_0 &&
                    s_spr[i].st.color == rd_target(RD_TARGET_SCENE).id;
        }
        CHECK(eye == 1 + 4 + 1 + 1 + 1,
              "eye blur with the sun: base, 4 ghosts, shrink, tint, "
              "subtract: %d",
              eye);
        CHECK(date == 1, "the backlight subtract into SCENE with DATE (DATM 0): %d", date);
    }
    InitializeStaticBlur();
    checkRecordingMotionBlur(448);
    checkRecordingMotionBlur(512);
    checkRecordingMotionBlurOff();
    checkRecordingWide();
    checkBorderStrips();
    rd_shutdown();
    printf("  (r) recording checks: %s\n", failures ? "FAILED" : "ok");
    if (failures) {
        printf("rd_blur_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(W, H, &st, NULL)) {
        printf("rd_blur_test: CPU checks ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    cpuInit();
    makeImage(1, 0);
    s_imgTex = rd_create_texture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");

    /* the cases, in the order they run (ico_shard.h): the first seven share
       one device, the rest each start their own */
    if (ico_shard_take("bilerp_pcsx2")) {
        checkBilerpPcsx2();
    }
    if (ico_shard_take("effects")) {
        checkEffects();
    }
    if (ico_shard_take("dump")) {
        checkDump();
    }
    if (ico_shard_take("motion_blur")) {
        checkMotionBlur();
    }
    if (ico_shard_take("aura")) {
        checkAura();
    }
    if (ico_shard_take("cut_presents")) {
        checkCutPresents();
    }
    if (ico_shard_take("motion_blur_presents")) {
        checkMotionBlurPresents();
    }
    if (ico_shard_take("fix128_skip")) {
        checkFix128Skip();
    }
    if (ico_shard_take("motion_blur_smooth")) {
        checkMotionBlurSmooth(); /* Motion blur: Smooth (rd_blur_smooth_test.inc) */
    }

    /* the pipelines the cases above created (this shard's) are all in the
       reachable set; every shard runs these two */
    checkPipelines();
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    if (ico_shard_take("uint_view")) {
        checkUintView(); /* makes the targets again, with the views and without */
    }
    if (ico_shard_take("blur_fold")) {
        checkBlurFold();
    }
    if (ico_shard_take("fx_plan")) {
        checkFxPlan(); /* makes the targets again with the scene passes readable, then without */
    }
    if (ico_shard_take("queen_mirage")) {
        checkQueenMirage(); /* re-initialises rd (NTSC, 1x, 4x) */
    }
    if (ico_shard_take("queen_presents")) {
        checkQueenPresents();
    }
    if (ico_shard_take("motion_blur_presents_scaled")) {
        checkMotionBlurPresentsScaled(); /* re-initialises rd (2x) */
    }
    if (ico_shard_take("feedback_wide")) {
        checkFeedbackWide(); /* re-initialises rd (W 512, 2048) */
    }
    if (ico_shard_take("shrinks_wide_device")) {
        checkShrinksWideDevice(); /* the shrink anchor: re-initialises rd (W 512, 1536) */
    }
    if (ico_shard_take("glow_edge_wide")) {
        checkGlowEdgeWide(); /* the glow's left edge: re-initialises rd (W 512, 848, 2048) */
    }
    if (ico_shard_take("blur_fold_wide")) {
        checkBlurFoldWide(); /* the blur fold: re-initialises rd (W 512, 688, 2048, 2560) */
    }
    if (ico_shard_take("motion_blur_wide")) {
        checkMotionBlurWide(); /* the motion blur's 4:3 columns: re-initialises rd */
    }
    if (ico_shard_take("above1x")) {
        checkAbove1x(); /* re-initialises rd (NTSC, Enhanced 4x) */
    }
    if (ico_shard_take("effects_scaled")) {
        checkEffectsScaled(); /* re-initialises rd (PAL, Enhanced 2x) */
    }
    if (ico_shard_take("motion_blur_smooth_scaled")) {
        checkMotionBlurSmoothScaled(); /* re-initialises rd (Enhanced 2x, full height) */
    }
    if (ico_shard_finish() != 0) {
        return 1;
    }
    if (failures) {
        printf("rd_blur_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_blur_test: ok\n");
    return 0;
}
