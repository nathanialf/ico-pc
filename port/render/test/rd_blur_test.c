/* rd_blur_test.c: the full-screen effects of staticBlur.c on rd (renderer
 * wave 5, R5a).
 *
 * staticBlur.c with the 2D layer (GifPacket.c, DisplayList.c, DmaPacket.c)
 * and Matrix.c, compiled as the window build has them (ICO_HOST, ICO_RD);
 * the rest of the game is stubbed below (the sun fans draw nothing).  No
 * disc data.
 *
 * CPU only (r): every entry point is driven and its recording checked:
 *   - every sprite is an rd_Post of its effect's kind, nothing goes through
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
 *      reduced into DISPLAY by rd_Post(RD_POST_REDUCTION), tinted, the
 *      whole loop in the model since R-POST): FIX 0x40 on a static image,
 *      FIX 0x40 on noise, FIX 0x70 with cuts (the cases of blend exactness under feedback);
 *   a  600 frames of the aura feedback through FEED128, 200 each of modes
 *      1 (aura), 2 (mirage) and 3 (aura v2), blurCol alpha 0x20 then 0x40;
 *   c  mirage ticks at alpha 128, one of them a camera cut (GlobalTimer),
 *      each replayed as the interpolating presenter does, as a tick's first
 *      present and as a later one: both draw the same SCENE and leave the
 *      same FEED128 (the later present of the cut drew black before
 *      FEED_HELD).
 *   q  (package QUEEN) the mirage as the Queen's F12 dumps record it: NTSC,
 *      feedbackCol (64, 64, 64, 128), the sprites' rectangles, UVs, TEX0
 *      sizes, colours and modes checked against the dumps', list 8's shine
 *      material into AURA_WORK with SCENE's depth (TEST 0x5346D, Z write)
 *      behind a nearer band, twelve frames of feedback against the model;
 *      then Enhanced 4x and 4x with the full-height scene, SCENE at the GS
 *      pixel centres against the 1x model: exact where the mask is 0 within
 *      16 GS pixels, within 4 where it is one value within 16.
 *   p  (package QUEEN) the mirage frame of q with the shine a keyed world
 *      draw, presented as the presenter does (a tick's first present and a
 *      later one at dt 0.5, the shine moving between ticks): the face band's
 *      interior (mask 0) is untouched and FEED128's alpha as the paste
 *      reads it is the mask, not SCENE's alpha, in every present.
 * Tolerance 0 everywhere (but q's scaled runs).  Every pipeline created is enumerated; no
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
float center_X = 2048.0f, center_Y = 2048.0f;
int screenOffsetX, screenOffsetY;
int fbKeep;
void *ios_partition_common;
void *dmaVif;
char *matrixptr;
int debug_font_flag;
int debug_fullscreen_effect = 1;
/* video_options.c's effect switches (issue 11), set by the cases below:
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

/* photo_mode.c's state (issue 14): off, the aura's feedback as in play */
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
    "SCENE",     "DISPLAY",   "SHADOW0",  "SHADOW1",   "SHADOW2",  "WORK0",
    "WORK1",     "WORK2",     "WORK3",    "AA0",       "AA1",      "FEED128",
    "DATE_SNAP", "AURA_WORK", "AURA_TAP", "WORK2_PAD", "FEED_HELD"};

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
    if (c->type == RDC_POST_STUB && rd__IsBlurKind(c->b[0])) {
        if (s_nspr < MAX_SPR) {
            Spr *p = &s_spr[s_nspr++];
            p->list = list;
            p->kind = c->b[0];
            p->st = *s;
            memcpy(&p->r, f->payload + c->u[1], sizeof(p->r));
        }
    } else if (c->type == RDC_SCREEN && (list == 7 || list == 8)) {
        s_screens78++;
    } else if (!rd__CmdIsState(c->type) && c->type != RDC_CLEAR && c->type != RDC_NOP) {
        s_others++;
    }
}

static void collectFrame(const RdFrame *f)
{
    s_nspr = s_screens78 = s_others = 0;
    RdStateBlock st = f->startState;
    rd__Walk(f, 0, &st, collect, (void *)f);
}

static uint32_t texTarget(const RdStateBlock *s, uint8_t *view)
{
    if (!s->ds.texEnabled) {
        return 0;
    }
    const RdTexRec *t = rd__TexRec(s->tex);
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
        collectFrame(rd__LastFrame());
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
            if (c == RD_TARGET_WORK2 && p->st.depth == rd_Target(RD_TARGET_SCENE).id &&
                p->st.gsW == W && p->st.gsH == H) {
                w2scene++;
            }
            if (c == RD_TARGET_AURA_WORK && p->st.depth == rd_Target(RD_TARGET_SCENE).id) {
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
                          p->st.depth == rd_Target(RD_TARGET_SCENE).id,
                      "%s: DoF plane %d: LERP FIX %u (got %u), ABE %d, Z GEQUAL, ZMSK", what,
                      planes, fix, p->st.ds.blendFix, p->st.ds.abe);
                CHECK(texTarget(&p->st, NULL) == rd_Target(RD_TARGET_WORK0).id,
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
    collectFrame(rd__LastFrame());
    memset(kinds, 0, sizeof(int) * RD_POST_COUNT);
    for (int i = 0; i < s_nspr; i++) {
        kinds[s_spr[i].kind]++;
    }
}

/* issue 11: Glow off drops the flare (or bloom) half of every post mode,
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

/* issue 11: Motion blur off draws nothing, whatever the stage's alpha, and
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
    collectFrame(rd__LastFrame());
    CHECK(s_nspr == 0, "motion blur off: no sprite (%d)", s_nspr);
    s_fx[3] = 1;
    MotionBlur();
    dl_Swap();
    collectFrame(rd__LastFrame());
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
    collectFrame(rd__LastFrame());
    CHECK(s_nspr == 1, "motion blur: one sprite (%d)", s_nspr);
    if (s_nspr == 1) {
        const Spr *p = &s_spr[0];
        uint8_t view = 0;
        CHECK(p->list == 7 && p->kind == RD_POST_MOTION_BLUR, "motion blur: list 7, its kind");
        CHECK(texTarget(&p->st, &view) == rd_Target(RD_TARGET_DISPLAY).id &&
                  view == RD_VIEW_RGB24_TA0 && p->st.ds.texa == RD_TEXA_80_80,
              "motion blur: DISPLAY's RGB24 view, TEXA 80/80");
        CHECK(p->st.color == rd_Target(RD_TARGET_SCENE).id && p->st.gsH == (uint32_t)h &&
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
    const RdStateBlock *e = &rd__LastFrame()->endState;
    (void)e;
    ScreenHeight = H;
    systemStatus[0] = 1;
}

/* ================================================= the CPU model of the GS */

typedef struct CpuT {
    int w, h;
    uint8_t *c;
    uint32_t *z;
} CpuT;

static CpuT s_cpu[RD_TARGET_COUNT];

static uint8_t *s_old;

static CpuT *cpuT(uint32_t id)
{
    const int n = named(id);
    return n >= 0 && s_cpu[n].c ? &s_cpu[n] : NULL;
}

static void cpuInit(void)
{
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        const RdTargetRec *t = rd__TargetRec((uint32_t)i + 1);
        if (!t || t->format != RHI_FMT_RGBA8_UNORM) {
            continue;
        }
        s_cpu[i].w = (int)t->w;
        s_cpu[i].h = (int)t->h;
        s_cpu[i].c = calloc((size_t)t->w * t->h, 4);
        if (t->withDepth) {
            s_cpu[i].z = calloc((size_t)t->w * t->h, 4);
        }
    }
    s_old = malloc((size_t)W * H * 4);
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
    x = t->clampS ? (x < 0 ? 0 : (x >= t->lw ? t->lw - 1 : x)) : (x & (t->lw - 1));
    y = t->clampT ? (y < 0 ? 0 : (y >= t->lh ? t->lh - 1 : y)) : (y & (t->lh - 1));
    x = x < t->w ? x : t->w - 1;
    y = y < t->h ? y : t->h - 1;
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

static void cpuSprite(const RdStateBlock *s, const RdPostRec *r)
{
    CpuT *tc = cpuT(s->color);
    if (!tc) {
        s_unexpected++;
        return;
    }
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
    if (d->texEnabled) {
        const RdTexRec *tr = rd__TexRec(s->tex);
        if (tr && tr->kind == RD_TEXKIND_TARGET && tr->view != RD_VIEW_DEPTH) {
            CpuT *src = cpuT(tr->target);
            if (src) {
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
    t.lw = (int)r->scalar[0];
    t.lh = (int)r->scalar[1];
    t.clampS = d->wrap.s == RD_WRAP_CLAMP;
    t.clampT = d->wrap.t == RD_WRAP_CLAMP;
    t.texa = d->texa;
    uint32_t tfx = r->lines & 3;
    if (d->texFn == RD_TEXFN_DECAL && tfx == 0) {
        tfx = 1;
    }
    const int tcc = d->tcc == RD_TCC_RGBA;
    const int linear = d->magFilter == RD_FILTER_LINEAR;
    const uint32_t reg = rd__AlphaRegister(d->blend < RD_BLEND_COUNT ? d->blend : RD_BLEND_LERP_AS);
    const int fix = rd__BlurFeedbackFix(d->blend, d->blendFix, r->scalar[2]);
    const int x0 = (int)r->rect[0], y0 = (int)r->rect[1], x1 = (int)r->rect[2],
              y1 = (int)r->rect[3];
    const int u0 = (int)r->uv[0], v0 = (int)r->uv[1], u1 = (int)r->uv[2], v1 = (int)r->uv[3];
    const int ox = (2048 - (int)(s->gsW >> 1)) * 16;
    const int oy = (2048 - (int)(s->gsH >> 1)) * 16 + ((s->useOffset & RD_TARGET_HALF_Y) ? 8 : 0);
    int sx0 = s->scissor[0] < 0 ? 0 : s->scissor[0], sy0 = s->scissor[1] < 0 ? 0 : s->scissor[1];
    int sx1 = s->scissor[2] >= tc->w ? tc->w - 1 : s->scissor[2];
    int sy1 = s->scissor[3] >= tc->h ? tc->h - 1 : s->scissor[3];
    for (int py = sy0; py <= sy1; py++) {
        const int Y = oy + py * 16;
        if (Y < y0 || Y >= y1) {
            continue;
        }
        for (int px = sx0; px <= sx1; px++) {
            const int X = ox + px * 16;
            if (X < x0 || X >= x1) {
                continue;
            }
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
                uint32_t tx[4];
                if (linear) {
                    const int uu = u - 8, vv = v - 8;
                    const int ix = uu >> 4, iy = vv >> 4;
                    const uint32_t fu = (uint32_t)(uu & 15), fv = (uint32_t)(vv & 15);
                    uint32_t a[4], b[4], c[4], e[4];
                    texel(&t, ix, iy, a);
                    texel(&t, ix + 1, iy, b);
                    texel(&t, ix, iy + 1, c);
                    texel(&t, ix + 1, iy + 1, e);
                    for (int ch = 0; ch < 4; ch++) {
                        tx[ch] = (a[ch] * (16 - fu) * (16 - fv) + b[ch] * fu * (16 - fv) +
                                  c[ch] * (16 - fu) * fv + e[ch] * fu * fv) >>
                                 8;
                    }
                } else {
                    texel(&t, u >> 4, v >> 4, tx);
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

static void cpuCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    const RdFrame *f = user;
    (void)list, (void)index;
    if (rd__CmdIsState(c->type) || c->type == RDC_NOP) {
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
    if (c->type == RDC_POST_STUB && rd__IsBlurKind(c->b[0])) {
        RdPostRec r;
        memcpy(&r, f->payload + c->u[1], sizeof(r));
        cpuSprite(s, &r);
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
    rd__Walk(f, 0, &st, cpuCmd, (void *)f);
}

static uint8_t s_gpu[W * H * 4];

/* the targets the effects touch, GPU against the CPU model; returns the
 * largest difference */
static const int kCompared[] = {RD_TARGET_SCENE,     RD_TARGET_DISPLAY,   RD_TARGET_WORK0,
                                RD_TARGET_WORK1,     RD_TARGET_WORK2,     RD_TARGET_WORK3,
                                RD_TARGET_FEED128,   RD_TARGET_AURA_WORK, RD_TARGET_AURA_TAP,
                                RD_TARGET_WORK2_PAD, RD_TARGET_FEED_HELD};

static int compareAll(const char *what, int verbose)
{
    int worst = 0;
    for (size_t k = 0; k < sizeof(kCompared) / sizeof(kCompared[0]); k++) {
        const CpuT *c = &s_cpu[kCompared[k]];
        uint32_t w = 0, h = 0;
        rhi_WaitFrame(); /* a readback that changes the target's state takes a command list */
        if (!rd__ReadTarget(rd_Target((RdTargetId)kCompared[k]), s_gpu, sizeof(s_gpu), &w, &h) ||
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
    rd_SetTarget(t, depth, (uint32_t)gw, (uint32_t)gh, 0);
    rd_TestGs(0x30000);
    rd_ZWrite(zwrite);
    rd_ABE(0);
    rd_FBA(0);
    rd_PABE(0);
    rd_SamplerFilter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_SamplerWrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(s_imgTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_Gouraud(0);
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
    rd_Post(RD_POST_FLARE, &p);
}

/* the scene: the picture in five bands of GS Z (2^16 z for z = 50, 150,
 * 250, 450, 600, so the depth-of-field planes at z = 100..500 cut through
 * them), the top band left at the clear's Z 0 (the sky) */
static void putScene(void)
{
    static const uint32_t kZ[5] = {0, 150u << 16, 250u << 16, 450u << 16, 600u << 16};
    dl_SetDLPriority(0);
    for (int b = 0; b < 5; b++) {
        putImage(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H, b * H / 5,
                 (b + 1) * H / 5, b == 0 ? (50u << 16) : kZ[b], b != 0);
    }
}

/* the reduction that closes the motion blur loop: gsbHostReduction's
 * rd_Post(RD_POST_REDUCTION) in list 12 (the black clear of DISPLAY, then
 * SCENE bilinear at u = x + 0.75, v = 2y + 1, tinted, inside the border
 * crop), drawn through the GS sprite model since R-POST, so the CPU model
 * runs it as one more sprite and the loop is compared exactly */
static void putReduction(void)
{
    dl_SetDLPriority(12);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rgba[0] = 0x80;
    p.rgba[1] = 0x78;
    p.rgba[2] = 0x64;
    p.rgba[3] = 0x80;
    rd_Post(RD_POST_REDUCTION, &p);
}

/* every compared target cleared to 0 (and SCENE's Z) on both sides */
static void clearAll(void)
{
    static const uint8_t zero[4] = {0, 0, 0, 0};
    dl_SetDLPriority(0);
    for (size_t k = 0; k < sizeof(kCompared) / sizeof(kCompared[0]); k++) {
        rd_ClearTarget(rd_Target((RdTargetId)kCompared[k]), zero, kCompared[k] == RD_TARGET_SCENE,
                       0);
    }
}

static int runFrame(const char *what, int verbose)
{
    dl_Swap();
    rhi_WaitIdle();
    s_unexpected = 0;
    cpuFrame(rd__LastFrame());
    CHECK(s_unexpected == 0, "%s: %d commands the CPU model does not know", what, s_unexpected);
    return compareAll(what, verbose);
}

/* =================================================== (e) one frame each */

static void checkEffects(void)
{
    InitStaticBlur(0, (float[4]){0.3f, -0.2f, -1.0f, 0.0f});
    for (int post = 1; post <= 7; post++) {
        char what[48];
        snprintf(what, sizeof(what), "(e) post mode %d", post);
        setStage(post, 0, 64);
        makeImage((uint32_t)post, 0);
        rd_UpdateTexture(s_imgTex, s_img);
        if (post == 1) {
            clearAll();
        }
        putScene();
        s_fanDraws = 0;
        FullScreenEffectBefore();
        /* a shine object of list 7 into the flare mask (WORK2, left bound) */
        dl_SetDLPriority(7);
        putImage(rd_Target(RD_TARGET_WORK2), rd_Target(RD_TARGET_SCENE), W, H, 200, 260, 0, 0);
        FullScreenEffectAfter();
        const int d = runFrame(what, 1);
        printf("  %s: max difference %d over %d sprites\n", what, d,
               (collectFrame(rd__LastFrame()), s_nspr));
        CHECK(post == 2 || s_fanDraws == 2, "%s: the sun is on screen (fans %d)", what, s_fanDraws);
    }
    InitializeStaticBlur(); /* sun off */
}

static void checkDump(void)
{
    static uint8_t first[W * H * 4], again[W * H * 4];
    uint32_t w, h;
    const char *path = "rd_blur_test.rddump";
    if (!rd__ReadTarget(rd_Target(RD_TARGET_SCENE), first, sizeof(first), &w, &h)) {
        CHECK(0, "(d) read SCENE");
        return;
    }
    CHECK(rd__DumpFrame(rd__LastFrame(), path), "(d) dump the frame");
    RdFrame g;
    memset(&g, 0, sizeof(g));
    if (!rd__LoadFrame(path, &g)) {
        CHECK(0, "(d) load the frame");
        return;
    }
    /* the frame read what the frame before left in the work buffers and
     * SCENE's Z: replay it on the same inputs (the CPU model's) */
    CHECK(rd__ReplayFrame(&g, 0, false), "(d) replay the loaded frame");
    rhi_WaitIdle();
    if (rd__ReadTarget(rd_Target(RD_TARGET_SCENE), again, sizeof(again), &w, &h)) {
        int diff = 0;
        for (size_t i = 0; i < sizeof(again); i++) {
            diff += again[i] != first[i];
        }
        printf("  (d) dump -> load -> replay of post mode 7: %d bytes of SCENE differ\n", diff);
        CHECK(diff == 0, "(d) the replayed dump equals the recorded frame");
    }
    rd__FrameFree(&g);
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
        rd_UpdateTexture(s_imgTex, s_img);
        putScene();
        /* the flare's TEX1 0x60 and CLAMP 5 leak into the motion blur */
        dl_SetDLPriority(7);
        rd_SamplerFilter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
        rd_SamplerWrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
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
     * (4-bit weights 12, 4) and rows 2y, 2y + 1 (8, 8), the sum >> 8, then
     * MODULATE by the tint; black outside (the GPU equals the model, above) */
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
                        const uint32_t t = (12u * 8u * r0[0] + 4u * 8u * r0[4] + 12u * 8u * r1[0] +
                                            4u * 8u * r1[4]) >>
                                           8;
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
        rd_UpdateTexture(s_imgTex, s_img);
        putScene();
        FullScreenEffectBefore();
        /* the list-8 objects: into AURA_WORK, left bound by auraInspireBefore */
        dl_SetDLPriority(8);
        putImage(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), W, H,
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
 * and as a later one (rd__InterpFrame's firstOfTick 0).  Both must draw
 * the same picture and leave the same FEED128; before the presenter kept
 * FEED128's input (FEED_HELD), the later present of the cut pasted the
 * black reset over the whole screen. */

static uint8_t s_firstScene[W * H * 4], s_firstFeed[128 * 128 * 4];

static int presentTick(const char *what, int first, uint8_t *scene, uint8_t *feed)
{
    const RdFrame *f = rd__InterpFrame(rd__PrevFrame(), rd__LastFrame(), 1.0f, 1.0f, first, NULL);
    if (!f || !rd__ReplayFrame(f, 0, false)) {
        CHECK(0, "%s: build and replay the present", what);
        return 0;
    }
    rhi_WaitIdle();
    s_unexpected = 0;
    cpuFrame(f);
    CHECK(s_unexpected == 0, "%s: %d commands the CPU model does not know", what, s_unexpected);
    const int d = compareAll(what, failures < 10);
    uint32_t w, h;
    CHECK(rd__ReadTarget(rd_Target(RD_TARGET_SCENE), scene, W * H * 4, &w, &h) && w == W && h == H,
          "%s: read SCENE", what);
    CHECK(rd__ReadTarget(rd_Target(RD_TARGET_FEED128), feed, 128 * 128 * 4, &w, &h) && w == 128 &&
              h == 128,
          "%s: read FEED128", what);
    return d;
}

static void checkCutPresents(void)
{
    static uint8_t scene[W * H * 4], feed[128 * 128 * 4];
    const uint8_t interpolate = g_rd.settings.interpolate;
    g_rd.settings.interpolate = 1; /* rd_EndFrame leaves the replays to the presents */
    clearAll();
    int worst = 0, black = 0;
    for (int n = 0; n < 8; n++) {
        setStage(0, 2, 0x80);
        GlobalTimer = n == 5; /* the cut */
        makeImage((uint32_t)(40 + n), 0);
        rd_UpdateTexture(s_imgTex, s_img);
        putScene();
        FullScreenEffectBefore();
        dl_SetDLPriority(8);
        putImage(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), W, H, 120 + n * 10,
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

/* ====================== (q) the queen's mirage, as the F12 dumps record it
 *
 * Package QUEEN.  The model viewer's dumps of the Queen (NTSC, 512 x 448,
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
 * scale: the work buffers are). */
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
    (void)n;
    dl_SetDLPriority(8);
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), W, QH, 0);
    rd_TestGs(0x5346D);
    rd_ZWrite(1);
    rd_ABE(1);
    rd_BlendFunc(RD_BLEND_LERP_AS, 0x80);
    rd_FBA(0);
    rd_PABE(0);
    rd_SamplerFilter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_SamplerWrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(s_qShineTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_Gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    const int dx = 0;
    p.rect[0] = (float)(0x8000 - W / 2 * 16 + (100 + dx) * 16);
    p.rect[1] = (float)(0x8000 - QH / 2 * 16 + 150 * 16);
    p.rect[2] = (float)(0x8000 - W / 2 * 16 + (400 + dx) * 16);
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
    rd_Post(RD_POST_AURA, &p);
}

/* one frame: the scene (Z 0, the face band rows 180..260 nearer), the
 * mirage around the shine material */
static void qFrame(int n)
{
    dl_SetDLPriority(0);
    putImage(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, QH, 0, QH, 0, 1);
    putImage(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, QH, 180, 260, Q_Z_FACE, 1);
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
    collectFrame(rd__LastFrame());
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
                      texTarget(&p->st, NULL) == rd_Target(RD_TARGET_AURA_WORK).id;
        } else if (c == RD_TARGET_FEED128 && p->st.ds.texEnabled &&
                   texTarget(&p->st, NULL) == rd_Target(RD_TARGET_WORK0).id) {
            copyA += qSame(r, 31744, 31744, 33792, 33792, 0, 0, 4096, 2048, 256, 128, mask) &&
                     p->st.ds.blend == RD_BLEND_CS_AS_ADD_CD && p->st.ds.abe;
        } else if (c == RD_TARGET_SCENE) {
            pasted += qSame(r, 28672, 29184, 36864, 36352, 8, 8, 2056, 1800, 128, 128, paste) &&
                      p->st.ds.blend == RD_BLEND_LERP_AS_ALT && p->st.ds.abe &&
                      texTarget(&p->st, NULL) == rd_Target(RD_TARGET_FEED128).id;
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
    st.aspect = 4.0f / 3.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = scale;
    st.fullHeightScene = (uint8_t)fullHeight;
    if (!rd_Init(W, QH, &st, NULL)) {
        CHECK(0, "(q) rd_Init %s", what);
        return;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    makeImage(7, 0);
    s_imgTex = rd_CreateTexture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");
    static uint8_t shine[512 * 512 * 4];
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 512; x++) {
            uint8_t *q = &shine[((size_t)y * 512 + x) * 4];
            const int hole = x >= 120 && x < 180 && y >= 0 && y < 30;
            q[0] = q[1] = q[2] = 0xFF;
            q[3] = hole ? 0x40 : 0x80;
        }
    }
    s_qShineTex = rd_CreateTexture(512, 512, shine, RD_TEXA_80_80, "rd_blur_test shine");
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
    rd_Shutdown();
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
            rhi_WaitIdle();
            qCheckRecorded();
            s_unexpected = 0;
            cpuFrame(rd__LastFrame());
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
    rd_Shutdown();

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
        rhi_WaitIdle();
        const RdTargetRec *t = rd__TargetRec(rd_Target(RD_TARGET_SCENE).id);
        uint8_t *img = t ? malloc((size_t)t->tw * t->th * 4) : NULL;
        uint32_t w = 0, h = 0;
        if (!img ||
            !rd__ReadTarget(rd_Target(RD_TARGET_SCENE), img, (size_t)t->tw * t->th * 4, &w, &h)) {
            CHECK(0, "(q) %s: read SCENE", kModes[m].what);
            free(img);
            rd_Shutdown();
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
               "(max %d), inside it %d of %d by more than 4 (max %d)\n",
               kModes[m].what, farBad, nFar, farMax, inBad, nIn, inMax);
        CHECK(farBad == 0, "(q) %s: %d pixels away from the mirage's mask differ from 1x",
              kModes[m].what, farBad);
        CHECK(inBad == 0, "(q) %s: %d pixels inside the mask differ from 1x by more than 4",
              kModes[m].what, inBad);
        free(img);
        CHECK(rhi_vk_ValidationErrorCount() == 0, "(q) %s: %u validation errors", kModes[m].what,
              rhi_vk_ValidationErrorCount());
        rd_Shutdown();
    }
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

/* ===================== (p) the mirage in the presenter's presents (QUEEN)
 *
 * The model viewer's rc2 dumps: the GS model of their own commands leaves
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
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), W, QH, 0);
    rd_TestGs(0x5346D);
    rd_ZWrite(1);
    rd_ABE(1);
    rd_BlendFunc(RD_BLEND_LERP_AS, 0x80);
    rd_FBA(0);
    rd_PABE(0);
    rd_SamplerFilter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_SamplerWrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(s_qShineTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_Gouraud(1);
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
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, RD_KEY(&kQShine, 8, 0));
}

static void qPresTick(int dx)
{
    dl_SetDLPriority(0);
    putImage(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, QH, 0, QH, 0, 1);
    putImage(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, QH, 180, 260, Q_Z_FACE, 1);
    FullScreenEffectBefore();
    qPresShine(dx);
    FullScreenEffectAfter();
    dl_Swap();
}

/* the frame for a present; with probe, everything after the paste (the
 * band clear and the copy of SCENE into FEED128) made a NOP */
static const RdFrame *qPresBuild(float alpha, int first, int probe)
{
    const RdFrame *f = rd__InterpFrame(rd__PrevFrame(), rd__LastFrame(), alpha, 0.5f, first, NULL);
    if (!f || !probe) {
        return f;
    }
    RdCmdList *cl = (RdCmdList *)&f->lists[8];
    RdStateBlock st = f->startState;
    for (int l = 0; l < 8; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            rd__ApplyState(&st, &f->lists[l].cmds[i]);
        }
    }
    int pasted = 0;
    for (uint32_t i = 0; i < cl->count; i++) {
        RdCmd *c = &cl->cmds[i];
        if (rd__ApplyState(&st, c)) {
            continue;
        }
        if (pasted && c->type == RDC_POST_STUB && st.color == rd_Target(RD_TARGET_FEED128).id) {
            c->type = RDC_NOP;
        }
        pasted |= c->type == RDC_POST_STUB && st.color == rd_Target(RD_TARGET_SCENE).id;
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
    if (f && rd__ReplayFrame(f, 0, false)) {
        rhi_WaitIdle();
        if (rd__ReadTarget(rd_Target(RD_TARGET_FEED128), feed, sizeof(feed), &w, &h)) {
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
    if (!f || !rd__ReplayFrame(f, 0, false)) {
        CHECK(0, "(p) %s: replay", what);
        return;
    }
    rhi_WaitIdle();
    if (!rd__ReadTarget(rd_Target(RD_TARGET_SCENE), scene, sizeof(scene), &w, &h)) {
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
    printf("  (p) %d mirage presents (first and later, dt 0.5, the shine moving): %d face pixels "
           "changed, %d FEED128 texels over the face set at the paste, the shine's mask set in "
           "%d\n",
           s_qpPresents, s_qpFaceBad, s_qpFeedBad, s_qpShineSet);
    CHECK(s_qpShineSet == s_qpPresents, "(p) the shine's mask is set in every probe");
    CHECK(rhi_vk_ValidationErrorCount() == 0, "(p) %u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
    ScreenHeight = screenHeight;
    systemStatus[0] = status0;
}

static void checkPipelines(void)
{
    static RdPipeKeyInt keys[512];
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipelines %u", n);
    int fx = 0;
    for (uint32_t i = 0; i < rd__PipelineCount(); i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < 512; j++) {
            found |= rd__PipeKeyEqual(&keys[j], k);
        }
        fx += k->fs == RD_FS_FX_SPRITE;
        CHECK(found,
              "created pipeline %u (prog %u vs %u fs %u ztst %u zwrite %u) is not enumerated", i,
              k->gs.program, k->vs, k->fs, k->gs.ztst, k->gs.zwrite);
    }
    printf("  pipelines: %u created (%d fx), %u reachable\n", rd__PipelineCount(), fx, n);
}

int main(void)
{
    printf("rd_blur_test\n");
    setMatrices();
    systemStatus[0] = 1; /* PAL: 512 lines */

    /* (r) recording */
    if (!rd__InitRecordOnly(W, H)) {
        printf("FAIL rd__InitRecordOnly\n");
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
    collectFrame(rd__LastFrame());
    {
        int scene = 0, named_ = 1;
        for (int i = 0; i < s_nspr; i++) {
            scene += s_spr[i].st.color == rd_Target(RD_TARGET_SCENE).id;
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
    collectFrame(rd__LastFrame());
    {
        int eye = 0, date = 0;
        for (int i = 0; i < s_nspr; i++) {
            eye += s_spr[i].kind == RD_POST_EYE_BLUR;
            date += s_spr[i].st.ds.test.date == RD_DATE_DEST_ALPHA_0 &&
                    s_spr[i].st.color == rd_Target(RD_TARGET_SCENE).id;
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
    rd_Shutdown();
    printf("  (r) recording checks: %s\n", failures ? "FAILED" : "ok");
    if (failures) {
        printf("rd_blur_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(W, H, &st, NULL)) {
        printf("rd_blur_test: CPU checks ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    cpuInit();
    makeImage(1, 0);
    s_imgTex = rd_CreateTexture(W, H, s_img, RD_TEXA_80_80, "rd_blur_test image");

    checkEffects();
    checkDump();
    checkMotionBlur();
    checkAura();
    checkCutPresents();

    checkPipelines();
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    checkQueenMirage(); /* package QUEEN: re-initialises rd (NTSC, 1x, 4x) */
    checkQueenPresents();
    if (failures) {
        printf("rd_blur_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_blur_test: ok\n");
    return 0;
}
