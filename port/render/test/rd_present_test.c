/* rd_present_test.c: the display options.
 *
 * GsBase.c, GifPacket.c, DisplayList.c and DmaPacket.c compiled as the
 * window build compiles them (as rd_gsbase_test does), the display options
 * module (port/game/video_options.c) with its config.
 *
 * Without a device:
 *   options   [video] keys from a config.toml, the defaults, the parsers,
 *             "auto", run-time set, save
 *   wide      gsb_SetVSMatrix / gsb_MakeCommonMatrix with aspect 16:9:
 *             +0x80, +0xC0 and +0x100 byte-identical to 4:3 (so
 *             IsPointIsInScreen and the screen tests are unchanged), +0x240
 *             x scale divided by 4/3 and +0x280 following it; the renderer's
 *             g_proj / g_viewProj x row compressed about 2048
 *   scales    rd__apply_display: the Original rows 1, the options under the
 *             Original flag, its zero-size fallback; 2x, WxH, the window's
 *             box, the 4K cap, the work buffers' scale, full height
 *   boxes     rd__present_box at 4:3 and 16:9 in 4:3, 16:9 and 5:4 outputs
 *   coverage  rdtex_keep_alpha_coverage keeps an alpha-tested texture's share;
 *             a lattice (wires alpha 0x80, black holes alpha 0)
 *             keeps its wire colour down the alpha-weighted chain and no
 *             level's alpha rises above the base's
 * On a Vulkan device (77 without one):
 *   original  rd_pixel_test's rich frame (scene, additive quad, fade,
 *             letterbox, brightness, reduction) in the Original preset:
 *             DISPLAY, SCENE and the 960 x 720 present hash to the values
 *             the renderer gave before the display options (llvmpipe only:
 *             another driver's bilinear may differ; logged elsewhere), and the
 *             Enhanced preset with every option neutral gives the same bytes
 *   scale2    the same frame at 2x: every 2 x 2 block of SCENE uniform and
 *             equal to the 1x pixel (nearest-only content, 0 LSB), DISPLAY's
 *             block averages within 2 LSB of 1x (bilinear reduction)
 *   wide169   16:9 at 1x: a UI sprite lands in the centred 4:3 box, the
 *             letterbox bars and a full-width fill stretch over the whole
 *             width (58 lines), the present fills a 16:9 output; a UI
 *             band one pixel short at each side (the menus' bars) stretches,
 *             one two pixels short stays boxed
 *   present info  with interpolation off, rd_last_present_info names the
 *             frame rd_end_frame presented
 *   full pixel  the reduction draws the whole frame: the box's edges show
 *             the picture (also on a scaled target), black around the box, no
 *             validation errors; a square is the same size on and off, a
 *             one-pixel line lands on the same output pixels, the last
 *             column does not wrap to the first; with the CRT filter the
 *             overlay's top and bottom lines are in the output
 *   mips      the trilinear filter: a mipmapped game texture, minified,
 *             samples its average; a lattice drawn as the
 *             railings are (TEST 0x5160D, ALPHA 0x44 with ABE) minified 8:1
 *             blends its wire colour over the background by the average
 *             alpha, unraised (no coverage kept for a texture blended by
 *             As), and an unblended alpha-tested texture gets its mips
 *             rebuilt with the coverage of its draw's AREF
 *   overlay   the presentation overlay (rd.h
 *             rd_set_present_overlay): the rich frame presented at 960 x 720
 *             and 1920 x 1080 with a callback drawing two rectangles (one
 *             in the box, one in the pillarbox) and "H" through port/ui's
 *             font in overlay mode: the rectangles exactly at their output
 *             pixels, the glyph's stems inside its quad (texel for pixel),
 *             nothing else changed against the present without the
 *             overlay; with the mirror on the all-UI box shows the same
 *             picture (within 1 LSB) and the overlay is not flipped;
 *             unregistered, the present hashes as it did without one
 *   overlay under the CRT filter: Trinitron at 1536 x
 *             1152 (k 3), the glow and curvature off: the callback sees the
 *             filter's grid (box 0, 0, 512 x 512, scale 512 / 448), and its
 *             red rectangle comes out of the filter as phosphors: in the
 *             rectangle each block's R column red, its G column dark
 *   overlay top  rd_set_present_overlay_top without the CRT
 *             filter at 960 x 720: both layers get the output's ctx and
 *             draw, the top layer under the main one where they overlap;
 *             rd__overlay_ring_bytes counts both layers' batches, and a
 *             registration and the present forget them
 *   capture   rd_capture_presented: the rich frame at 800 x
 *             600 with the overlay's rectangles registered, CRT off and
 *             Consumer TV: the PNG is 800 x 600 RGB and holds exactly the
 *             present without the overlay, under the CRT filter too (the
 *             shown picture has the overlay inside it; the
 *             capture takes a filter pass without it); rd_capture_result
 *             reports it once
 *   passes    the Enhanced preset at 960 x 720 with a deferred text item:
 *             the box blit, the deferred text and both overlay layers draw
 *             in one pass on the output, so the present records two passes
 *             (the line doubling's and the output's) with or without them;
 *             with a capture armed the output's pass is ended for the copy
 *             and opened again (one more), and the shown picture is the
 *             same byte for byte; each layer's rectangle is drawn
 *   blank     rd_present_blank: at 800 x 600 with nothing
 *             registered the output is all 0 (no scene, though a frame was
 *             just recorded and presented); with an overlay drawing a red
 *             rectangle it is called once with the output's 4:3 box, the
 *             rectangle is exactly red and every other byte 0;
 *             rd_get_present_overlay gives back the registration; no
 *             validation errors
 *
 * Usage: rd_present_test [dir]  (dir: where the scratch config goes)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_tex.h"
#include "shader_consts.h"
#include "vk/rhi_vk.h"

#ifndef RD_PRESENT_BASELINE
/* port/ui's font (the overlay check draws a glyph) */
#include "font.h"
#include "ui_internal.h"
#endif

#ifndef RD_PRESENT_BASELINE

#include <eeregs.h>
#include <libvu0.h>
/* the game's side */
#include "typedef.h"
#include "main.h"
#include "Basic.h"
#include "DisplayList.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "config.h"
#include "video_options.h"

#endif

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
#ifndef RD_PRESENT_BASELINE

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

#endif /* !RD_PRESENT_BASELINE */

/* ------------------------------------------------------------- helpers */

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static RdScreenVtx vtx(int32_t x, int32_t y, uint32_t z, const uint8_t c[4], float s, float t)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = x;
    v.y = y;
    v.z = z;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

/* a sprite from (x0, y0) to (x1, y1) in 12.4 relative to the top-left of a
 * 512 x 512 target whose XYOFFSET is (2048 - 256, 2048 - 256) */
static void sprite(RdSpace space, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   const uint8_t c[4], int32_t u0, int32_t v0, int32_t u1, int32_t v1)
{
    const int32_t o = (2048 - 256) * 16;
    RdScreenVtx v[2] = {vtx(o + x0, o + y0, 0, c, (float)u0, (float)v0),
                        vtx(o + x1, o + y1, 0, c, (float)u1, (float)v1)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, space, 1, 0);
}

static void opaque2D(void)
{
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
}

static uint8_t *readTarget(RdTargetId id, uint32_t *w, uint32_t *h)
{
    static uint8_t buf[2048 * 2048 * 4];
    if (!rd__read_target(rd_target(id), buf, sizeof(buf), w, h)) {
        CHECK(0, "readback of target %d", (int)id);
        return NULL;
    }
    return buf;
}

/* ------------------------------------------- rd_pixel_test's rich frame */

static uint8_t s_scene[512 * 512 * 4];

/* rd_pixel_test.c testReduction's scene: smooth gradients plus noise */
static void makeNoiseScene(void)
{
    for (int i = 0; i < 512 * 512; i++) {
        uint32_t hsh = hash((uint32_t)i * 3 + 1);
        s_scene[i * 4 + 0] = (uint8_t)((i % 512) / 2 + (hsh & 31));
        s_scene[i * 4 + 1] = (uint8_t)((i / 512) / 2 + ((hsh >> 8) & 63));
        s_scene[i * 4 + 2] = (uint8_t)hsh;
        s_scene[i * 4 + 3] = (uint8_t)((hsh >> 24) & 0x7F) + 0x40;
    }
}

/* smooth gradients (neighbours differ by at most 1), alpha 0x80 */
static void makeSmoothScene(void)
{
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 512; x++) {
            uint8_t *p = &s_scene[(y * 512 + x) * 4];
            p[0] = (uint8_t)(x / 2);
            p[1] = (uint8_t)(y / 2);
            p[2] = (uint8_t)((x + y) / 4);
            p[3] = 0x80;
        }
    }
}

/* checkEffectsDepth: the scene's depth cleared to
 * DEPTH_CLEAR_Z instead of 0, and a sprite written at DEPTH_NEAR_Z over
 * DEPTH_RECT (GS pixels); 0 (every other cell): the plain frame */
static int s_depthFrame;

#define DEPTH_CLEAR_Z 0x40000000u
#define DEPTH_NEAR_Z 0xC0000000u
static const int32_t DEPTH_RECT[4] = {128, 96, 352, 320};

static void drawScene(RdTex t)
{
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 1, s_depthFrame ? DEPTH_CLEAR_Z : 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(RD_SPACE_UI, 0, 0, 512 * 16, 512 * 16, grey, 8, 8, 512 * 16 + 8, 512 * 16 + 8);
    if (s_depthFrame) {
        static const uint8_t near[4] = {0x30, 0x60, 0x90, 0x80};
        const int32_t o = (2048 - 256) * 16;
        RdScreenVtx v[2] = {
            vtx(o + DEPTH_RECT[0] * 16, o + DEPTH_RECT[1] * 16, DEPTH_NEAR_Z, near, 0, 0),
            vtx(o + DEPTH_RECT[2] * 16, o + DEPTH_RECT[3] * 16, DEPTH_NEAR_Z, near, 0, 0)};
        rd_texture_off();
        rd_z_write(1);
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, 0);
        rd_z_write(0);
        rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    }
}

/* rd_pixel_test.c recordRichFrame, the frame of its dump */
static void recordRichFrame(RdTex t, int reduce)
{
    rd_begin_frame();
    drawScene(t);
    rd_select_list(2);
    static const uint8_t add[4] = {60, 30, 90, 0xFF};
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 1);
    rd_texture_off();
    sprite(RD_SPACE_UI, 100 * 16, 100 * 16, 300 * 16, 200 * 16, add, 0, 0, 0, 0);
    rd_select_list(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 0x40;
    pp.rgba[0] = 0x20;
    rd_post(RD_POST_FADE, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x60;
    rd_post(RD_POST_LETTERBOX, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 5;
    rd_post(RD_POST_BRIGHTNESS, &pp);
    if (reduce) {
        rd_select_list(12);
        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = 128;
        pp.rgba[1] = 120;
        pp.rgba[2] = 110;
        rd_post(RD_POST_REDUCTION, &pp);
    }
    rd_end_frame(0);
}

typedef struct FrameHashes {
    uint64_t display, scene, present;
} FrameHashes;

/* rd_init with s, the rich frame, the three hashes; false without a device */
static bool richFrame(const RdSettings *s, FrameHashes *h)
{
    if (!rd_init(512, 512, s, NULL)) {
        return false;
    }
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t w = 0, hh = 0;
    uint8_t *p = readTarget(RD_TARGET_DISPLAY, &w, &hh);
    h->display = p ? fnv(p, (size_t)w * hh * 4) : 0;
    p = readTarget(RD_TARGET_SCENE, &w, &hh);
    h->scene = p ? fnv(p, (size_t)w * hh * 4) : 0;
    static uint8_t out[960 * 720 * 4];
    h->present = rd__read_present(out, sizeof(out), &w, &hh) ? fnv(out, sizeof(out)) : 0;
    rd_destroy_texture(t);
    rd_shutdown();
    return true;
}

static RdSettings originalSettings(void)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.sceneScale = 1.0f; /* the host's Original rows */
    s.outputWidth = 960;
    s.outputHeight = 720;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    return s;
}

/* What the renderer produced for the rich frame on llvmpipe before the
 * display options: rd_replay_tool, built from that renderer, on
 * rd_pixel_test's dump of this frame, DISPLAY, SCENE and --present 960x720.
 * The reduction later moved onto the GS sprite model (the GS integer
 * bilinear instead of the hardware's, at most 1 LSB apart; rd_pixel_test
 * checks it exactly), so DISPLAY and the present are the values of a build
 * with that change (0xde837c63a5e63c88, 0xbc970416f934e0c1 without it) and
 * SCENE is still the value from before the display options. */
#define GOLD_DISPLAY 0x212c1c733f4ba8a1ull
#define GOLD_SCENE 0x8da6e2ca4577cdacull
#define GOLD_PRESENT 0xedb088b74a237351ull
#ifdef RD_PRESENT_BASELINE

/* built against the renderer from before the display options: prints the
 * hashes of this file's own frame (they must equal the constants above) */
int main(void)
{
    makeNoiseScene();
    RdSettings s = originalSettings();
    FrameHashes h;
    if (!richFrame(&s, &h)) {
        return 77;
    }
    printf("baseline: display %016llx scene %016llx present %016llx\n",
           (unsigned long long)h.display, (unsigned long long)h.scene,
           (unsigned long long)h.present);
    return 0;
}

#else

/* ------------------------------------------------------------- options */

static void writeFile(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
}

static char *readFile(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = 0;
    fclose(fp);
    return buf;
}

static int near(float a, float b)
{
    return fabsf(a - b) <= 1e-5f * (fabsf(b) > 1.0f ? fabsf(b) : 1.0f);
}

static void checkOptions(const char *dir)
{
    char toml[1024], ini[1024];
    snprintf(toml, sizeof(toml), "%s/rd_present_config.toml", dir);
    snprintf(ini, sizeof(ini), "%s/rd_present_none.ini", dir);
    remove(ini);

    /* the defaults: an empty file */
    writeFile(toml, "version = 1\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    IcoVideoOptions o;
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL && o.aspect == ICO_ASPECT_4_3 && o.resW == 0 &&
              o.resH == 0 && o.resScale == 1 && o.windowMode == ICO_WINDOW_WINDOWED &&
              o.vsync == 1 && o.filter == ICO_FILTER_ORIGINAL && o.fullHeight == 0,
          "options: defaults (the Original rows)");
    CHECK(ico_video_wide_x() == 1.0f && ico_video_aspect() == 4.0f / 3.0f, "options: default 4:3");

    /* every key: "enhanced" takes the rows as written; 1920x1440 is none of
       the two presets' */
    writeFile(toml, "version = 1\n[video]\npreset = \"enhanced\"\nresolution = \"1920x1440\"\n"
                    "aspect = \"16:9\"\nfullscreen = true\nvsync = false\n"
                    "texture_filter = \"anisotropic\"\nfull_height = true\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_CUSTOM && o.resW == 1920 && o.resH == 1440 &&
              o.aspect == ICO_ASPECT_16_9 && o.windowMode == ICO_WINDOW_FULLSCREEN &&
              o.vsync == 0 && o.filter == ICO_FILTER_ANISOTROPIC && o.fullHeight == 1,
          "options: every key read, Custom");
    CHECK(near(ico_video_wide_x(), 4.0f / 3.0f), "options: 16:9 widens by 4/3");

    /* window_mode, and the older fullscreen bool when it is absent */
    writeFile(toml, "version = 1\n[video]\nwindow_mode = \"borderless\"\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_BORDERLESS, "options: window_mode borderless");
    writeFile(toml, "version = 1\n[video]\nwindow_mode = \"BORDERLESS\"\nfullscreen = true\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_BORDERLESS, "options: both keys, window_mode wins");
    writeFile(toml, "version = 1\n[video]\nwindow_mode = \"sideways\"\nfullscreen = true\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_WINDOWED, "options: an invalid window_mode is Windowed");
    writeFile(toml, "version = 1\n[video]\nwindow_mode = \"borderless\"\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    o.windowMode = ICO_WINDOW_FULLSCREEN;
    ico_video_set(&o);
    ico_video_save();
    CHECK(strcmp(ico_config_get_string("video.window_mode", ""), "fullscreen") == 0 &&
              ico_config_get_bool("video.fullscreen", 0) == 1,
          "options: save writes window_mode and fullscreen");
    o.windowMode = ICO_WINDOW_BORDERLESS;
    ico_video_set(&o);
    ico_video_save();
    CHECK(strcmp(ico_config_get_string("video.window_mode", ""), "borderless") == 0 &&
              ico_config_get_bool("video.fullscreen", 1) == 0,
          "options: borderless saves fullscreen false");
    /* back to the keys the rest of this check reads */
    writeFile(toml, "version = 1\n[video]\npreset = \"enhanced\"\nresolution = \"1920x1440\"\n"
                    "aspect = \"16:9\"\nfullscreen = true\nvsync = false\n"
                    "texture_filter = \"anisotropic\"\nfull_height = true\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);

    /* the Original shortcut: the four rows at the PS2's, the rest kept */
    IcoVideoOptions p = o;
    ico_video_set_preset(&p, ICO_VIDEO_ORIGINAL);
    const unsigned serial = ico_video_serial();
    ico_video_set(&p);
    CHECK(ico_video_serial() != serial, "options: set bumps the serial");
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL && o.resScale == 1 && o.resW == 0 &&
              o.resH == 0 && o.aspect == ICO_ASPECT_4_3 && o.filter == ICO_FILTER_ORIGINAL &&
              o.fullHeight == 0 && o.windowMode == ICO_WINDOW_FULLSCREEN && o.vsync == 0,
          "options: the Original shortcut is 1x, 4:3, original, half, the rest kept");
    CHECK(ico_video_wide_x() == 1.0f, "options: Original is 4:3");
    /* the aspect applies whatever the preset */
    p.aspect = ICO_ASPECT_16_9;
    ico_video_set(&p);
    ico_video_get(&o);
    CHECK(near(ico_video_wide_x(), 4.0f / 3.0f) && ico_video_preset(&o) == ICO_VIDEO_CUSTOM,
          "options: 16:9 alone widens by 4/3 and reads Custom");
    /* the Enhanced shortcut */
    ico_video_set_preset(&p, ICO_VIDEO_ENHANCED);
    ico_video_set(&p);
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED && o.resScale == 0 && o.resW == 0 &&
              o.resH == 0 && o.aspect == ICO_ASPECT_AUTO && o.filter == ICO_FILTER_ANISOTROPIC &&
              o.fullHeight == 1,
          "options: the Enhanced shortcut is window, auto, anisotropic, full");
    /* Custom is no shortcut */
    p.filter = ICO_FILTER_TRILINEAR;
    o = p;
    ico_video_set_preset(&p, ICO_VIDEO_CUSTOM);
    CHECK(memcmp(&o, &p, sizeof(o)) == 0, "options: the Custom shortcut changes nothing");
    CHECK(strcmp(ico_video_preset_name(ICO_VIDEO_ORIGINAL), "original") == 0 &&
              strcmp(ico_video_preset_name(ICO_VIDEO_ENHANCED), "enhanced") == 0 &&
              strcmp(ico_video_preset_name(ICO_VIDEO_CUSTOM), "custom") == 0,
          "options: the preset names");
    p.aspect = ICO_ASPECT_16_10;
    ico_video_set(&p);
    CHECK(near(ico_video_wide_x(), 1.2f), "options: 16:10 widens by 1.2");
    p.aspect = ICO_ASPECT_21_9;
    ico_video_set(&p);
    CHECK(near(ico_video_aspect(), 64.0f / 27.0f) && near(ico_video_wide_x(), 16.0f / 9.0f),
          "options: 21:9 widens by 16/9");
    p.aspect = ICO_ASPECT_32_9;
    ico_video_set(&p);
    CHECK(near(ico_video_aspect(), 32.0f / 9.0f) && near(ico_video_wide_x(), 8.0f / 3.0f),
          "options: 32:9 widens by 8/3");
    p.aspect = ICO_ASPECT_48_9;
    ico_video_set(&p);
    CHECK(near(ico_video_aspect(), 48.0f / 9.0f) && near(ico_video_wide_x(), 4.0f),
          "options: 48:9 widens by 4");
    p.aspect = ICO_ASPECT_AUTO;
    ico_video_set(&p);
    ico_video_set_window(0, 0);
    CHECK(ico_video_wide_x() == 1.0f, "options: auto without a window is 4:3");
    ico_video_set_window(1920, 1080);
    CHECK(near(ico_video_aspect(), 16.0f / 9.0f), "options: auto in 1920x1080");
    ico_video_set_window(3440, 1440);
    CHECK(near(ico_video_aspect(), 3440.0f / 1440.0f), "options: auto passes 3440x1440 through");
    ico_video_set_window(5760, 1080);
    CHECK(near(ico_video_aspect(), 48.0f / 9.0f), "options: auto passes 5760x1080 (48:9) through");
    CHECK(near(ico_video_wide_x(), 4.0f), "options: auto at 48:9 widens by 4");
    ico_video_set_window(9600, 1080);
    CHECK(ico_video_aspect() == ICO_ASPECT_MAX && near(ico_video_wide_x(), ICO_WIDE_X_MAX),
          "options: auto clamps at the maximum (20:3), 9600x1080");
    /* the renderer's mirror of the maximum (rd_internal.h) */
    CHECK(RD_ASPECT_MAX == ICO_ASPECT_MAX, "options: RD_ASPECT_MAX equals ICO_ASPECT_MAX");
    ico_video_set_window(1280, 1024);
    CHECK(ico_video_aspect() == 4.0f / 3.0f, "options: auto clamps at 4:3");
    ico_video_set_window(2400, 1080);
    CHECK(near(ico_video_aspect(), 20.0f / 9.0f), "options: auto fills a 2400x1080 phone (20:9)");
    ico_video_set_window(2208, 1840);
    CHECK(ico_video_aspect() == 4.0f / 3.0f, "options: auto clamps an unfolded 2208x1840 to 4:3");
    ico_video_set_window(0, 0);

    /* earlier builds' files: "original" (or no key) is the PS2 picture
       whatever the rows say */
    writeFile(toml, "version = 1\n[video]\npreset = \"original\"\nresolution = \"2x\"\n"
                    "aspect = \"16:9\"\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL && o.resScale == 1 &&
              o.aspect == ICO_ASPECT_4_3 && ico_video_wide_x() == 1.0f,
          "options: an old \"original\" with rows is Original");
    writeFile(toml, "version = 1\n[video]\nresolution = \"2x\"\ntexture_filter = \"trilinear\"\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL && o.resScale == 1 &&
              o.filter == ICO_FILTER_ORIGINAL,
          "options: no preset key is Original");
    writeFile(toml, "version = 1\n[video]\npreset = \"orignal\"\nresolution = \"2x\"\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL && o.resScale == 1,
          "options: a misspelt preset is Original");
    writeFile(toml, "version = 1\n[video]\npreset = \"enhanced\"\nresolution = \"1080p\"\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == 0 && o.resW == 0 && o.resH == 0,
          "options: an unparseable resolution is window, as logged");
    /* an old "enhanced" without rows: window, 4:3, original, half, the
       picture it had */
    writeFile(toml, "version = 1\n[video]\npreset = \"enhanced\"\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_CUSTOM && o.resScale == 0 && o.resW == 0 &&
              o.aspect == ICO_ASPECT_4_3 && o.filter == ICO_FILTER_ORIGINAL && o.fullHeight == 0 &&
              ico_video_wide_x() == 1.0f,
          "options: an old \"enhanced\" alone is Custom (window, 4:3, original, half)");
    writeFile(toml, "version = 1\n[video]\npreset = \"enhanced\"\nresolution = \"window\"\n"
                    "aspect = \"auto\"\ntexture_filter = \"anisotropic\"\nfull_height = true\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED,
          "options: the full Enhanced rows are Enhanced");
    writeFile(toml, "version = 1\n[video]\npreset = \"custom\"\nresolution = \"3x\"\n"
                    "aspect = \"16:10\"\ntexture_filter = \"trilinear\"\nfull_height = true\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_CUSTOM && o.resScale == 3 &&
              o.aspect == ICO_ASPECT_16_10 && o.filter == ICO_FILTER_TRILINEAR &&
              o.fullHeight == 1 && near(ico_video_wide_x(), 1.2f),
          "options: \"custom\" takes the rows as written");

    /* parsers */
    IcoVideoOptions q;
    ico_video_defaults(&q);
    CHECK(ico_video_parse_resolution("2x", &q) == 0 && q.resScale == 2 && q.resW == 0,
          "options: 2x");
    CHECK(ico_video_parse_resolution("Window", &q) == 0 && q.resScale == 0 && q.resW == 0,
          "options: window");
    CHECK(ico_video_parse_resolution("3840x2160", &q) == 0 && q.resW == 3840 && q.resH == 2160,
          "options: WxH");
    CHECK(ico_video_parse_resolution("2xx", &q) != 0 && ico_video_parse_resolution("0x", &q) != 0 &&
              ico_video_parse_resolution("17x", &q) != 0 && q.resW == 3840,
          "options: bad resolutions rejected, value kept");
    CHECK(ico_video_parse_resolution("10x", &q) == 0 && q.resScale == 10 &&
              ico_video_parse_resolution("16x", &q) == 0 && q.resScale == 16,
          "options: 10x and 16x");
    ico_video_parse_resolution("3840x2160", &q);
    int a = -1;
    CHECK(ico_video_parse_aspect("16:10", &a) == 0 && a == ICO_ASPECT_16_10 &&
              ico_video_parse_aspect("21:9", &a) == 0 && a == ICO_ASPECT_21_9 &&
              ico_video_parse_aspect("32:9", &a) == 0 && a == ICO_ASPECT_32_9 &&
              ico_video_parse_aspect("48:9", &a) == 0 && a == ICO_ASPECT_48_9 &&
              ico_video_parse_aspect("21:10", &a) != 0,
          "options: aspect parser");
    char buf[32];
    CHECK(strcmp(ico_video_resolution_name(&q, buf, sizeof(buf)), "3840x2160") == 0,
          "options: resolution name");

    /* save: the [video] keys land in config.toml; Custom is written as
       "enhanced" with its rows (an older build shows the same picture) */
    ico_video_get(&p);
    p.aspect = ICO_ASPECT_16_9;
    p.resW = p.resH = 0;
    p.resScale = 2;
    ico_video_set(&p);
    CHECK(ico_video_save() == 0, "options: save");
    const char *text = readFile(toml);
    CHECK(text && strstr(text, "aspect = \"16:9\"") && strstr(text, "resolution = \"2x\"") &&
              strstr(text, "preset = \"enhanced\"") && strstr(text, "full_height = true"),
          "options: saved keys");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&q);
    CHECK(q.resScale == 2 && q.aspect == ICO_ASPECT_16_9 &&
              ico_video_preset(&q) == ICO_VIDEO_CUSTOM,
          "options: Custom read back");
    /* Original is written as "original" with its rows */
    ico_video_set_preset(&p, ICO_VIDEO_ORIGINAL);
    ico_video_set(&p);
    CHECK(ico_video_save() == 0, "options: save Original");
    text = readFile(toml);
    CHECK(text && strstr(text, "preset = \"original\"") && strstr(text, "resolution = \"1x\""),
          "options: Original saved keys");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&q);
    CHECK(ico_video_preset(&q) == ICO_VIDEO_ORIGINAL && q.resScale == 1,
          "options: Original read back");

    /* back to the defaults for the rest */
    ico_video_defaults(&q);
    ico_video_set(&q);
    remove(toml);
}

/* ---------------------------------------------------------------- wide */

static void boot(void)
{
    static __attribute__((aligned(16))) char spr[0x800];
    matrixptr = spr;
    memset(spr, 0, sizeof(spr));
    systemStatus[0] = 1; /* PAL: 512 x 512 */
    systemStatus[1] = 2;
    GlobalStageSetting.viewScale = 100;
    game_pause = 1;
    gsb_InitGSSystem();
}

static void viewMatrix(float *m)
{
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

typedef struct Mats {
    float m80[16], mC0[16], m100[16], m240[16], m280[16];
    RdCamera cam;
} Mats;

/* what the game computes for one camera with the options in force */
static void cameraMats(Mats *m)
{
    float view[16];
    viewMatrix(view);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, 512.0f);
    memcpy(matrixptr + 0x80, view, 64);
    gsb_MakeCommonMatrix();
    memcpy(m->m80, matrixptr + 0x80, 64);
    memcpy(m->mC0, matrixptr + 0xC0, 64);
    memcpy(m->m100, matrixptr + 0x100, 64);
    memcpy(m->m240, matrixptr + 0x240, 64);
    memcpy(m->m280, matrixptr + 0x280, 64);
    const RdFrame *f = rd__rec_frame();
    if (f && f->hasCamera) {
        m->cam = f->camera;
    }
}

static void checkWide(void)
{
    IcoVideoOptions o;
    ico_video_defaults(&o);
    ico_video_set(&o);
    rd_begin_frame();
    Mats a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    cameraMats(&a);
    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    cameraMats(&b);
    rd_discard_frame();
    const float k = 4.0f / 3.0f;
    CHECK(memcmp(a.m80, b.m80, 64) == 0, "wide: +0x80 (the view) byte-identical");
    CHECK(memcmp(a.mC0, b.mC0, 64) == 0, "wide: +0xC0 (the 4:3 screen matrix) byte-identical");
    CHECK(memcmp(a.m100, b.m100, 64) == 0, "wide: +0x100 (screen x view) byte-identical");
    CHECK(near(b.m240[0], a.m240[0] / k) && memcmp(&a.m240[1], &b.m240[1], 60) == 0,
          "wide: +0x240 x scale divided by 4/3, the rest identical (%g -> %g)", a.m240[0],
          b.m240[0]);
    int scaled = 0, bad = 0;
    for (int i = 0; i < 16; i++) {
        if (a.m280[i] == b.m280[i]) {
            continue;
        }
        if (fabsf(b.m280[i] * k - a.m280[i]) <= 1e-5f * fabsf(a.m280[i])) {
            scaled++;
        } else {
            bad++;
        }
    }
    CHECK(bad == 0 && scaled >= 3,
          "wide: +0x280 is +0x240 x +0x80 with the narrower x (%d "
          "scaled, %d other)",
          scaled, bad);
    CHECK(memcmp(&a.cam, &b.cam, sizeof(a.cam)) == 0,
          "wide: the frame camera (proj43 = +0xC0) unchanged");
    ico_video_defaults(&o);
    ico_video_set(&o);

    /* the renderer's projection: x row compressed about 2048 */
    IcoFrameCB n, w;
    const float saved = g_rd.wideX;
    g_rd.wideX = 1.0f;
    rd__fill_camera_cb(&n, &a.cam);
    g_rd.wideX = 0.75f;
    rd__fill_camera_cb(&w, &a.cam);
    g_rd.wideX = saved;
    int ok = 1;
    for (int c = 0; c < 4; c++) {
        const float want = 0.75f * n.proj[c * 4] + 0.25f * 2048.0f * n.proj[c * 4 + 3];
        ok &= near(w.proj[c * 4], want);
        for (int r = 1; r < 4; r++) {
            ok &= w.proj[c * 4 + r] == n.proj[c * 4 + r];
        }
    }
    CHECK(ok, "wide: g_proj's x row = 0.75 x + 0.25 * 2048 w, other rows unchanged");
    CHECK(memcmp(n.proj, a.cam.proj43, 64) == 0, "wide: g_proj = proj43 at 4:3");
    /* a point: GS X moves towards 2048 by the factor, Y stays */
    const float p[4] = {100.0f, -50.0f, 800.0f, 1.0f};
    float hn[4] = {0}, hw[4] = {0};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            hn[r] += n.viewProj[c * 4 + r] * p[c];
            hw[r] += w.viewProj[c * 4 + r] * p[c];
        }
    }
    const float xn = hn[0] / hn[3], xw = hw[0] / hw[3];
    CHECK(fabsf((xw - 2048.0f) - 0.75f * (xn - 2048.0f)) < 1e-2f &&
              fabsf(hw[1] / hw[3] - hn[1] / hn[3]) < 1e-3f,
          "wide: a point's GS X %.3f -> %.3f about 2048, Y kept", xn, xw);
    CHECK(near(w.clip[3], 16.0f / 9.0f), "wide: g_clip.w the aspect");
}

/* -------------------------------------------------------------- scales */

static void applyWith(const RdSettings *s)
{
    g_rd.settings = *s;
    rd__apply_display();
}

static void checkScales(void)
{
    const RdSettings saved = g_rd.settings;
    RdSettings s = originalSettings();
    applyWith(&s);
    CHECK(g_rd.sceneSx == 1.0f && g_rd.sceneSy == 1.0f && g_rd.workScale == 1.0f &&
              g_rd.wideX == 1.0f && g_rd.outAspect == 4.0f / 3.0f && !g_rd.fullHeight &&
              !g_rd.filterUpgrade,
          "scales: the Original rows are 1");
    /* the options apply under the Original flag too */
    s.sceneScale = 3.0f;
    s.aspect = 16.0f / 9.0f;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 4.0f && g_rd.sceneSy == 3.0f && g_rd.workScale == 2.0f &&
              near(g_rd.wideX, 0.75f) && near(g_rd.outAspect, 16.0f / 9.0f),
          "scales: 3x at 16:9 under the Original flag (%g, %g, work %g, wide %g)", g_rd.sceneSx,
          g_rd.sceneSy, g_rd.workScale, g_rd.wideX);
    {
        RhiRect b;
        rd__present_box(1920, 1080, g_rd.outAspect, &b);
        CHECK(b.x == 0 && b.y == 0 && b.w == 1920 && b.h == 1080,
              "scales: the Original flag's 16:9 box fills 1920x1080");
    }
    /* the fallback: scale 0 and no size is the GS size under the Original
       flag (a zeroed RdSettings), the output's box only under Enhanced */
    s.sceneScale = 0.0f;
    s.aspect = 4.0f / 3.0f;
    s.outputWidth = 1920;
    s.outputHeight = 1080;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 1.0f && g_rd.sceneSy == 1.0f && g_rd.workScale == 1.0f,
          "scales: the Original flag with scale 0 and no size is 1x");
    s.outputWidth = 960;
    s.outputHeight = 720;
    s.preset = RD_PRESET_ENHANCED;
    s.aspect = 4.0f / 3.0f;
    s.sceneScale = 2.0f;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 2.0f && g_rd.sceneSy == 2.0f && g_rd.workScale == 2.0f &&
              g_rd.wideX == 1.0f,
          "scales: 2x at 4:3 (%g, %g, work %g)", g_rd.sceneSx, g_rd.sceneSy, g_rd.workScale);
    s.aspect = 16.0f / 9.0f;
    applyWith(&s);
    CHECK(near(g_rd.sceneSx, 8.0f / 3.0f) && g_rd.sceneSy == 2.0f && near(g_rd.wideX, 0.75f),
          "scales: 2x at 16:9 widens the texture (%g)", g_rd.sceneSx);
    s.sceneScale = 0.0f;
    s.sceneWidth = 1920;
    s.sceneHeight = 1440;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 3.75f && g_rd.sceneSy == 2.8125f && g_rd.workScale == 2.0f,
          "scales: 1920x1440");
    s.sceneWidth = s.sceneHeight = 0;
    s.outputWidth = 1920;
    s.outputHeight = 1080;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 3.75f && near(g_rd.sceneSy, 1080.0f / 512.0f),
          "scales: the window's 16:9 box");
    s.sceneWidth = 7680;
    s.sceneHeight = 4320;
    applyWith(&s);
    CHECK(near(g_rd.sceneSx * 512.0f, 7680.0f) && near(g_rd.sceneSy * 512.0f, 4320.0f),
          "scales: 8K is no longer held to 4K");
    s.sceneWidth = 20000;
    s.sceneHeight = 15000;
    applyWith(&s);
    CHECK(near(g_rd.sceneSx * 512.0f, 16384.0f) && near(g_rd.sceneSy * 512.0f, 12288.0f),
          "scales: capped at the texture limit, shape kept (%g x %g)",
          (double)(g_rd.sceneSx * 512.0f), (double)(g_rd.sceneSy * 512.0f));
    /* 48:9 in a 5760x1080 window with Window resolution: the box itself */
    s.sceneWidth = s.sceneHeight = 0;
    s.outputWidth = 5760;
    s.outputHeight = 1080;
    s.aspect = 48.0f / 9.0f;
    applyWith(&s);
    CHECK(near(g_rd.sceneSx * 512.0f, 5760.0f) && near(g_rd.sceneSy * 512.0f, 1080.0f),
          "scales: 48:9 in 5760x1080 renders 5760x1080");
    s.aspect = 10.0f;
    applyWith(&s);
    CHECK(g_rd.outAspect == RD_ASPECT_MAX, "scales: the aspect is held to the maximum");
    s.sceneWidth = s.sceneHeight = 0;
    s.outputWidth = 320;
    s.outputHeight = 240;
    s.aspect = 4.0f / 3.0f;
    s.fullHeightScene = 1;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 1.0f && g_rd.sceneSy == 1.0f && g_rd.fullHeight,
          "scales: never below the GS size; full height");
    /* targets */
    RdTargetRec t;
    memset(&t, 0, sizeof(t));
    t.w = 512;
    t.h = 256;
    rd__target_scale_of(&t, RD_TARGET_DISPLAY);
    CHECK(t.tw == 512 && t.th == 512 && t.sy == 2.0f && !t.wide,
          "scales: full height doubles DISPLAY's texture (%ux%u)", t.tw, t.th);
    s.fullHeightScene = 0;
    s.sceneScale = 2.0f;
    applyWith(&s);
    t.w = t.h = 512;
    rd__target_scale_of(&t, -1);
    CHECK(t.tw == 1024 && t.th == 1024 && t.wide, "scales: a scene-sized temporary target");
    t.w = t.h = 256;
    rd__target_scale_of(&t, -1);
    CHECK(t.tw == 256 && t.sx == 1.0f && !t.wide, "scales: other temporary targets stay");
    t.w = t.h = 128;
    rd__target_scale_of(&t, RD_TARGET_FEED128);
    CHECK(t.tw == 256 && t.th == 256 && !t.wide, "scales: the work buffers by the work scale");
    g_rd.settings = saved;
    rd__apply_display();
}

static void checkBoxes(void)
{
    RhiRect b;
    rd__present_box(1920, 1080, 4.0f / 3.0f, &b);
    CHECK(b.x == 240 && b.y == 0 && b.w == 1440 && b.h == 1080, "boxes: 4:3 in 16:9");
    rd__present_box(1920, 1080, 16.0f / 9.0f, &b);
    CHECK(b.x == 0 && b.y == 0 && b.w == 1920 && b.h == 1080, "boxes: 16:9 in 16:9");
    rd__present_box(1280, 1024, 16.0f / 9.0f, &b);
    CHECK(b.x == 0 && b.y == 152 && b.w == 1280 && b.h == 720, "boxes: 16:9 in 5:4 letterboxed");
    rd__present_box(960, 720, 16.0f / 10.0f, &b);
    CHECK(b.w == 960 && b.h == 600 && b.y == 60, "boxes: 16:10 in 4:3");
    rd__present_box(960, 720, 4.0f / 3.0f, &b);
    CHECK(b.x == 0 && b.y == 0 && b.w == 960 && b.h == 720, "boxes: 4:3 in 4:3");
    /* phone shapes (the movie's box is always 4:3) */
    rd__present_box(2400, 1080, 4.0f / 3.0f, &b);
    CHECK(b.x == 480 && b.y == 0 && b.w == 1440 && b.h == 1080,
          "boxes: 4:3 in a 2400x1080 phone (%d,%d %ux%u)", b.x, b.y, b.w, b.h);
    rd__present_box(2400, 1080, 2400.0f / 1080.0f, &b);
    CHECK(b.x == 0 && b.y == 0 && b.w == 2400 && b.h == 1080,
          "boxes: Auto fills a 2400x1080 phone (%d,%d %ux%u)", b.x, b.y, b.w, b.h);
    rd__present_box(2316, 904, 2316.0f / 904.0f, &b);
    CHECK(b.x == 0 && b.y == 0 && b.w == 2316 && b.h == 904,
          "boxes: Auto fills a 2316x904 phone (%d,%d %ux%u)", b.x, b.y, b.w, b.h);
    rd__present_box(2208, 1840, 4.0f / 3.0f, &b);
    CHECK(b.x == 0 && b.y == 92 && b.w == 2208 && b.h == 1656,
          "boxes: 4:3 in an unfolded 2208x1840 screen (%d,%d %ux%u)", b.x, b.y, b.w, b.h);
    rd__present_box(1080, 2400, 4.0f / 3.0f, &b);
    CHECK(b.x == 0 && b.y == 795 && b.w == 1080 && b.h == 810,
          "boxes: 4:3 in a 1080x2400 portrait phone (%d,%d %ux%u)", b.x, b.y, b.w, b.h);
}

static void checkCoverage(void)
{
    /* 16 x 16, alpha 0x80 on a sparse grid (1 texel in 4 per row and
     * column: 1/16 coverage), 0 elsewhere; GS alpha test a > 64 */
    static uint8_t img[16 * 16 * 4], chain[16 * 16 * 4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint8_t *p = &img[(y * 16 + x) * 4];
            p[0] = p[1] = p[2] = 200;
            p[3] = (x % 4 == 0 && y % 4 == 0) ? 0x80 : 0;
        }
    }
    const uint32_t n = rdtex_build_mip_chain(img, 16, 16, chain, 1);
    CHECK(n == 4, "coverage: 4 levels below 16x16");
    /* level 1 (8x8) box filtered: 0x20 at the dots, under the test */
    CHECK(chain[3] == 0x20, "coverage: the plain box filter thins it out");
    rdtex_keep_alpha_coverage(img, 16, 16, chain, n, 64);
    int pass = 0;
    for (int i = 0; i < 64; i++) {
        pass += chain[i * 4 + 3] > 64;
    }
    CHECK(pass >= 4, "coverage: level 1 keeps 1/16 of its texels over the test (%d of 64)", pass);
    CHECK(chain[3] <= 0x80, "coverage: never above level 0's largest alpha");
    /* opaque or empty textures are left alone */
    for (int i = 0; i < 256; i++) {
        img[i * 4 + 3] = 0x80;
    }
    rdtex_build_mip_chain(img, 16, 16, chain, 1);
    const uint64_t h0 = fnv(chain, sizeof(chain));
    rdtex_keep_alpha_coverage(img, 16, 16, chain, n, 64);
    CHECK(fnv(chain, sizeof(chain)) == h0, "coverage: an opaque texture is untouched");

    /* the railing's lattice, wires (x % 4 == 0 or y % 4 == 0,
     * 7/16 of the texels) colour (200, 180, 160) alpha 0x80, holes black
     * alpha 0.  Alpha-weighted, every level keeps the wire colour exactly
     * (the box filter darkened it to 7/16 of itself) and the mean alpha
     * stays at the base's 56 */
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint8_t *p = &img[(y * 16 + x) * 4];
            const int wire = x % 4 == 0 || y % 4 == 0;
            p[0] = wire ? 200 : 0;
            p[1] = wire ? 180 : 0;
            p[2] = wire ? 160 : 0;
            p[3] = wire ? 0x80 : 0;
        }
    }
    uint32_t base = 0;
    for (int i = 0; i < 256; i++) {
        base += img[i * 4 + 3];
    }
    const uint32_t ln = rdtex_build_mip_chain(img, 16, 16, chain, 1);
    const uint8_t *lv = chain;
    int dark = 0, high = 0;
    for (uint32_t l = 1, lw = 8; l <= ln; l++, lw /= 2) {
        uint32_t sum = 0;
        for (uint32_t i = 0; i < lw * lw; i++) {
            const uint8_t *p = &lv[i * 4];
            sum += p[3];
            high += p[3] > 0x80;
            dark += p[3] && (abs(p[0] - 200) > 1 || abs(p[1] - 180) > 1 || abs(p[2] - 160) > 1);
        }
        CHECK(sum * 256 <= (base + 256) * lw * lw,
              "lattice: level %u mean alpha %.1f above the base's %.1f", l, (double)sum / (lw * lw),
              (double)base / 256.0);
        lv += (size_t)lw * lw * 4;
    }
    CHECK(dark == 0, "lattice: %d texels of the alpha-weighted chain lost the wire colour", dark);
    CHECK(high == 0, "lattice: %d texels above alpha 0x80", high);
    rdtex_build_mip_chain(img, 16, 16, chain, 0);
    /* level 1 texel (2, 1): one wire column over two hole rows */
    CHECK(chain[(1 * 8 + 2) * 4] == 100,
          "lattice: the plain box filter darkens the wires (%u), the weighted one does not",
          chain[(1 * 8 + 2) * 4]);
}

/* -------------------------------------------------------- device checks */

static bool s_llvmpipe;

static uint64_t s_presentOriginal; /* checkOriginal's present hash */

static void checkOriginal(void)
{
    makeNoiseScene();
    RdSettings s = originalSettings();
    FrameHashes h;
    if (!richFrame(&s, &h)) {
        CHECK(0, "original: rd_init");
        return;
    }
    s_presentOriginal = h.present;
    printf("  original: display %016llx scene %016llx present %016llx\n",
           (unsigned long long)h.display, (unsigned long long)h.scene,
           (unsigned long long)h.present);
    if (s_llvmpipe) {
        CHECK(h.display == GOLD_DISPLAY && h.scene == GOLD_SCENE && h.present == GOLD_PRESENT,
              "original: the recorded bytes (the PS2 picture as before the display options)");
    } else {
        printf("  original: not llvmpipe, the recorded hashes are not compared\n");
    }
    /* the Enhanced flag with the Original rows: the same bytes (the flag
       keeps only the deferred text, the UI scale and the zero-size
       fallback; none touches this frame) */
    RdSettings e = s;
    e.preset = RD_PRESET_ENHANCED;
    e.sceneScale = 1.0f;
    FrameHashes g;
    if (richFrame(&e, &g)) {
        CHECK(g.display == h.display && g.scene == h.scene && g.present == h.present,
              "original: the Enhanced flag at 1x, 4:3, no filter, half height is the same frame");
    }
}

static uint8_t s_scene1[512 * 512 * 4], s_disp1[512 * 256 * 4];

static void checkScale2(void)
{
    makeSmoothScene();
    /* 1x */
    RdSettings s = originalSettings();
    if (!rd_init(512, 512, &s, NULL)) {
        CHECK(0, "scale2: rd_init");
        return;
    }
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t w, h;
    uint8_t *p = readTarget(RD_TARGET_DISPLAY, &w, &h);
    if (p) {
        memcpy(s_disp1, p, sizeof(s_disp1));
    }
    /* SCENE before the reduction: the next frame without one */
    recordRichFrame(t, 0);
    p = readTarget(RD_TARGET_SCENE, &w, &h);
    if (p) {
        memcpy(s_scene1, p, sizeof(s_scene1));
    }
    rd_destroy_texture(t);
    rd_shutdown();

    /* 2x */
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 2.0f;
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    p = readTarget(RD_TARGET_DISPLAY, &w, &h);
    CHECK(p && w == 1024 && h == 512, "scale2: DISPLAY is 1024x512 (%ux%u)", w, h);
    int worstD = 0, wx = -1, wy = -1;
    if (p && w == 1024 && h == 512) {
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 512; x++) {
                for (int k = 0; k < 3; k++) {
                    const int sum = p[((2 * y) * 1024 + 2 * x) * 4 + k] +
                                    p[((2 * y) * 1024 + 2 * x + 1) * 4 + k] +
                                    p[((2 * y + 1) * 1024 + 2 * x) * 4 + k] +
                                    p[((2 * y + 1) * 1024 + 2 * x + 1) * 4 + k];
                    const int e = abs((sum + 2) / 4 - s_disp1[(y * 512 + x) * 4 + k]);
                    if (e > worstD) {
                        worstD = e;
                        wx = x;
                        wy = y;
                    }
                }
            }
        }
    }
    recordRichFrame(t, 0);
    p = readTarget(RD_TARGET_SCENE, &w, &h);
    CHECK(p && w == 1024 && h == 1024, "scale2: SCENE is 1024x1024");
    int worst = 0, uneven = 0;
    if (p && w == 1024 && h == 1024) {
        for (int y = 0; y < 512; y++) {
            for (int x = 0; x < 512; x++) {
                for (int k = 0; k < 4; k++) {
                    const int a = p[((2 * y) * 1024 + 2 * x) * 4 + k];
                    const int u = a != p[((2 * y) * 1024 + 2 * x + 1) * 4 + k] ||
                                  a != p[((2 * y + 1) * 1024 + 2 * x) * 4 + k] ||
                                  a != p[((2 * y + 1) * 1024 + 2 * x + 1) * 4 + k];
                    if (u && uneven < 12) {
                        printf("    uneven block (%d,%d) ch %d: %u %u / %u %u\n", x, y, k, a,
                               p[((2 * y) * 1024 + 2 * x + 1) * 4 + k],
                               p[((2 * y + 1) * 1024 + 2 * x) * 4 + k],
                               p[((2 * y + 1) * 1024 + 2 * x + 1) * 4 + k]);
                    }
                    uneven += u;
                    const int e = abs(a - s_scene1[(y * 512 + x) * 4 + k]);
                    worst = e > worst ? e : worst;
                }
            }
        }
    }
    printf("  scale2: SCENE blocks uneven %d, worst block-to-1x error %d LSB; DISPLAY block "
           "average worst %d LSB (at %d, %d)\n",
           uneven, worst, worstD, wx, wy);
    CHECK(uneven == 0, "scale2: every 2x2 block of SCENE uniform");
    CHECK(worst <= 1, "scale2: SCENE blocks equal the 1x pixels within 1 LSB");
    CHECK(worstD <= 2, "scale2: DISPLAY's block averages within 2 LSB of 1x");
    CHECK(rhi_vk_validation_error_count() == 0, "scale2: %u validation errors",
          rhi_vk_validation_error_count());
    rd_destroy_texture(t);
    rd_shutdown();
}

/* The scene cap follows the device's texture limit (4096 under
 * ICO_VK_FAKE_LIMITS=min), and a scale the device cannot allocate falls back
 * by halves.  The settings are applied without recreating the targets for
 * the cap (a 16384 texture is not worth allocating); the fallback test makes
 * texture creates above a size fail instead of allocating the big ones. */
static void checkSceneLimit(void)
{
    RdSettings s = originalSettings();
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    float lim = (float)rhi_limits()->maxRenderTargetSize;
    if (rhi_limits()->tiler && lim > 4096.0f) {
        lim = 4096.0f; /* a phone GPU's cap (rd__apply_display) */
    }
    float ew = 20000.0f, eh = 15000.0f;
    if (ew > lim) {
        eh *= lim / ew;
        ew = lim;
    }
    if (eh > lim) {
        ew *= lim / eh;
        eh = lim;
    }
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 0.0f; /* the size below, not a scale */
    s.sceneWidth = 20000;
    s.sceneHeight = 15000;
    s.outputWidth = s.outputHeight = 0;
    g_rd.settings = s;
    rd__apply_display();
    CHECK(near(g_rd.sceneSx * 512.0f, ew) && near(g_rd.sceneSy * 512.0f, eh),
          "limit: the scene is held to the device's %g, shape kept (%g x %g)", (double)lim,
          (double)(g_rd.sceneSx * 512.0f), (double)(g_rd.sceneSy * 512.0f));
    rd_shutdown();

    if (rhi_backend() != RHI_BACKEND_VULKAN) {
        /* the failure hook is the Vulkan backend's: the fallback checks below
           have nothing to drive on another backend */
        printf("rd_present: scene fallback checks skipped (backend is not Vulkan)\n");
        return;
    }

    /* 16x asked, the device holds nothing over 3 million texels: 16x, 8x
       and 4x fail, 2x is made.  Nothing big is ever allocated. */
    s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 16.0f;
    s.aspect = 4.0f / 3.0f;
    vkr_test_fail_texels_above(3000000);
    if (rd_init(512, 512, &s, NULL)) {
        const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
        CHECK(g_rd.sceneSx == 2.0f && g_rd.sceneSy == 2.0f && rd_scene_scale_lowered() == 2,
              "fallback: 16x lands at 2x (%g x %g, lowered %d)", (double)g_rd.sceneSx,
              (double)g_rd.sceneSy, rd_scene_scale_lowered());
        CHECK(t && t->color.id != 0 && t->tw == 1024 && t->th == 1024,
              "fallback: the scene target exists at 2x");
        /* the same options again do not recreate the targets or raise it */
        g_rd.settings = s;
        CHECK(!rd__apply_display() && g_rd.sceneSx == 2.0f && rd_scene_scale_lowered() == 2,
              "fallback: unchanged options keep the lowered scale");
        /* a new request is tried afresh */
        s.sceneScale = 1.0f;
        g_rd.settings = s;
        CHECK(rd__apply_display() && rd_scene_scale_lowered() == 0 && g_rd.sceneSx == 1.0f,
              "fallback: a new scale starts from what is asked");
        rd_shutdown();
    } else {
        CHECK(0, "fallback: rd_init");
    }
    vkr_test_fail_texels_above(0);

    /* a scene-sized texture that fails after the targets were made (4x fits;
       then the device holds nothing over 3 million texels, and the shadow
       count's target does not): the next frame halves the scale and makes
       the targets again */
    s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 4.0f;
    s.aspect = 4.0f / 3.0f;
    if (rd_init(512, 512, &s, NULL)) {
        CHECK(g_rd.sceneSx == 4.0f && rd_scene_scale_lowered() == 0, "pressure: 4x is made");
        vkr_test_fail_texels_above(3000000);
        const uint32_t id = rd__temp_target_alloc(512, 512, 0, 0);
        CHECK(g_rd.scenePressure, "pressure: a scene-sized allocation that fails is noted");
        rd__temp_target_free(id);
        rd_begin_frame();
        const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
        CHECK(!g_rd.scenePressure && g_rd.sceneSx == 2.0f && g_rd.sceneSy == 2.0f &&
                  rd_scene_scale_lowered() == 2,
              "pressure: the next frame halves 4x to 2x (%g, lowered %d)", (double)g_rd.sceneSx,
              rd_scene_scale_lowered());
        CHECK(t && t->color.id != 0 && t->tw == 1024, "pressure: the scene target is remade at 2x");
        rd_shutdown();
    } else {
        CHECK(0, "pressure: rd_init");
    }
    /* at 1x there is nothing to give back: no flag */
    vkr_test_fail_texels_above(100000);
    s.sceneScale = 1.0f;
    if (rd_init(512, 512, &s, NULL)) {
        const uint32_t id = rd__temp_target_alloc(512, 512, 0, 0);
        CHECK(!g_rd.scenePressure, "pressure: none at 1x");
        rd__temp_target_free(id);
        rd_shutdown();
    }
    vkr_test_fail_texels_above(0);
}

static void checkWide169(void)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.aspect = 16.0f / 9.0f;
    s.sceneScale = 1.0f;
    s.outputWidth = 960;
    s.outputHeight = 540;
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    static const uint8_t black[4] = {0, 0, 0, 0x80}, white[4] = {255, 255, 255, 0x80},
                         red[4] = {255, 0, 0, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_texture_off();
    /* a UI item: GS 128..384 x 100..200 */
    sprite(RD_SPACE_UI, 128 * 16, 100 * 16, 384 * 16, 200 * 16, white, 0, 0, 0, 0);
    /* a full-width fill the game tags WORLD: rows 300..310 */
    sprite(RD_SPACE_WORLD, 0, 300 * 16, 512 * 16, 310 * 16, red, 0, 0, 0, 0);
    /* the pause menu's band as the layout draws it, UI space, GS x
     * 0.25 .. 511.44 (pixels 1..511): rows 320..330 */
    sprite(RD_SPACE_UI, 4, 320 * 16, 511 * 16 + 7, 330 * 16, red, 0, 0, 0, 0);
    /* a UI band two pixels short at the left (pixels 2..511): rows 340..350,
     * stays in the 4:3 box */
    sprite(RD_SPACE_UI, 2 * 16, 340 * 16, 512 * 16, 350 * 16, red, 0, 0, 0, 0);
    /* the letterbox at full strength */
    rd_select_list(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x80;
    rd_post(RD_POST_LETTERBOX, &pp);
    rd_select_list(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    uint32_t w, h;
    uint8_t *p = readTarget(RD_TARGET_SCENE, &w, &h);
    CHECK(p && w == 683 && h == 512, "wide169: SCENE is 683x512 (%ux%u)", w, h);
    if (p && w == 683 && h == 512) {
#define PX(x, y) (&p[((y) * 683 + (x)) * 4])
        /* the 4:3 box is 512 wide (75%) from 85.3: GS 128..384 -> 213.3..469.6 */
        int ok = 1;
        for (int x = 0; x < 683; x++) {
            const int in = x >= 214 && x <= 468, out = x <= 212 || x >= 470;
            if (in) {
                ok &= PX(x, 150)[0] == 255 && PX(x, 150)[1] == 255;
            } else if (out) {
                ok &= PX(x, 150)[0] == 0;
            }
        }
        CHECK(ok, "wide169: the UI sprite in the centred 4:3 box (texels 214..468)");
        ok = 1;
        for (int x = 0; x < 683; x++) {
            ok &= PX(x, 305)[0] == 255 && PX(x, 305)[1] == 0;
        }
        CHECK(ok, "wide169: the full-width WORLD fill stretches over the 16:9 width");
        /* the band one pixel short at each side: stretched (GS pixels
         * 1..511 at 683/512 texels each: texels 2..681 at least) */
        ok = 1;
        for (int x = 2; x < 682; x++) {
            ok &= PX(x, 325)[0] == 255;
        }
        CHECK(ok, "wide169: the layout's screen band (pixels 1..511) spans the 16:9 width");
        /* the band two pixels short: boxed, GS 2..511 -> texels 87..597 */
        ok = PX(80, 345)[0] == 0 && PX(90, 345)[0] == 255 && PX(595, 345)[0] == 255 &&
             PX(600, 345)[0] == 0;
        CHECK(ok, "wide169: a UI band two pixels short stays in the 4:3 box");
        /* letterbox: 58 lines top and bottom over the whole width, black */
        ok = 1;
        for (int x = 0; x < 683; x += 1) {
            ok &= PX(x, 0)[0] == 0 && PX(x, 57)[0] == 0 && PX(x, 454)[0] == 0 && PX(x, 511)[0] == 0;
        }
        CHECK(ok, "wide169: the bars span the width");
        CHECK(PX(300, 150)[0] == 255 && PX(5, 305)[0] == 255 && PX(300, 58)[0] == 0,
              "wide169: below the top bar the scene shows");
#undef PX
    }
    /* the present: 16:9 fills the 960 x 540 output (no pillars) */
    static uint8_t out[960 * 540 * 4];
    uint32_t ow = 0, oh = 0;
    if (rd__read_present(out, sizeof(out), &ow, &oh) && ow == 960 && oh == 540) {
        /* 12 output pixels in: past the reduction's 2-pixel border crop */
        const uint8_t *q = &out[(305 * 540 / 512 * 960 + 12) * 4];
        CHECK(q[0] > 200, "wide169: the fill reaches the output's left edge (%u)", q[0]);
    } else {
        CHECK(0, "wide169: present readback");
    }
    CHECK(rhi_vk_validation_error_count() == 0, "wide169: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
}

/* Full pixel: SCENE red through the reduction pass (which leaves its border
 * black), presented into w x h.  The box's edge columns and rows are red
 * with the option on, the reduction drawing the whole frame, and, with it
 * off, the border shows.  scale above 1 runs the reduction on a scaled
 * target. */
static bool fullPixelRun(float aspect, uint32_t w, uint32_t h, int full, int fullHeight,
                         float scale, uint8_t *dst, uint32_t *ow, uint32_t *oh)
{
    RdSettings s = originalSettings();
    s.aspect = aspect;
    s.outputWidth = w;
    s.outputHeight = h;
    s.fullPixel = (uint8_t)full;
    s.fullHeightScene = (uint8_t)fullHeight;
    if (aspect > 4.0f / 3.0f + 0.01f || scale > 1.0f) {
        s.preset = RD_PRESET_ENHANCED;
    }
    if (scale > 1.0f) {
        s.sceneScale = scale; /* a scaled target: the reduction's hardware sprite */
    }
    if (!rd_init(512, 512, &s, NULL)) {
        return false;
    }
    static const uint8_t red[4] = {255, 0, 0, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), red, 1, 0);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    const bool ok = rd__read_present(dst, (size_t)w * h * 4, ow, oh) && *ow == w && *oh == h;
    CHECK(rhi_vk_validation_error_count() == 0, "full pixel: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return ok;
}

static void checkFullPixelCase(const char *name, float aspect, uint32_t w, uint32_t h,
                               int fullHeight, float scale)
{
    static uint8_t off[960 * 540 * 4], on[960 * 540 * 4];
    uint32_t ow = 0, oh = 0;
    if (!fullPixelRun(aspect, w, h, 0, fullHeight, scale, off, &ow, &oh) ||
        !fullPixelRun(aspect, w, h, 1, fullHeight, scale, on, &ow, &oh)) {
        CHECK(0, "%s: present readback", name);
        return;
    }
    RhiRect box;
    rd__present_box(w, h, aspect, &box);
    const uint32_t bw = box.w, bh = box.h;
    const uint32_t x0 = (uint32_t)box.x, x1 = x0 + bw - 1, y0 = (uint32_t)box.y, y1 = y0 + bh - 1;
#define PXO(b, x, y) (&(b)[((size_t)(y) * w + (x)) * 4])
#define RED(q) ((q)[0] > 200 && (q)[1] < 60 && (q)[2] < 60)
#define BLACK(q) ((q)[0] < 8 && (q)[1] < 8 && (q)[2] < 8)
    const uint32_t cx = w / 2, cy = h / 2;
    /* off: the reduction's border at the box edge (2 of 512 columns, 8 of
     * DISPLAY's 256 rows), red inside */
    CHECK(BLACK(PXO(off, x0, cy)) && BLACK(PXO(off, x0 + 1, cy)) && BLACK(PXO(off, x1, cy)) &&
              BLACK(PXO(off, x1 - 1, cy)),
          "%s: off: the side columns are black", name);
    CHECK(BLACK(PXO(off, cx, y0)) && BLACK(PXO(off, cx, y0 + 3)) && BLACK(PXO(off, cx, y1)) &&
              BLACK(PXO(off, cx, y1 - 3)),
          "%s: off: the top and bottom rows are black", name);
    CHECK(RED(PXO(off, cx, cy)) && RED(PXO(off, x0 + bw / 8, cy)) && RED(PXO(off, cx, y0 + bh / 8)),
          "%s: off: red inside", name);
    /* on: the picture reaches the box's edge */
    CHECK(RED(PXO(on, x0, cy)) && RED(PXO(on, x1, cy)), "%s: on: the edge columns are red", name);
    CHECK(RED(PXO(on, cx, y0)) && RED(PXO(on, cx, y1)), "%s: on: the edge rows are red", name);
    CHECK(RED(PXO(on, x0, y0)) && RED(PXO(on, x1, y1)), "%s: on: the corners are red", name);
    CHECK(memcmp(PXO(on, cx, cy), PXO(off, cx, cy), 4) == 0, "%s: the centre is unchanged", name);
    /* outside the box stays black */
    int outside = 1;
    if (x0 > 0) {
        for (uint32_t y = 0; y < h; y += 7) {
            outside &= BLACK(PXO(on, x0 - 1, y)) && BLACK(PXO(on, x1 + 1, y)) &&
                       BLACK(PXO(on, 0, y)) && BLACK(PXO(on, w - 1, y));
        }
    }
    if (y0 > 0) {
        for (uint32_t x = 0; x < w; x += 7) {
            outside &= BLACK(PXO(on, x, y0 - 1)) && BLACK(PXO(on, x, y1 + 1));
        }
    }
    CHECK(outside, "%s: on: nothing outside the box", name);
#undef PXO
#undef RED
#undef BLACK
}

/* a white square (GS 192..320 both ways) on black through the reduction at
 * 512 lines (50 Hz), presented at 960 x 720 with full pixel off or on; its
 * width and height in output pixels along the centre row and column */
static bool squareRun(int full, uint32_t *sw, uint32_t *sh)
{
    const uint32_t w = 960, h = 720;
    static uint8_t out[960 * 720 * 4];
    RdSettings s = originalSettings();
    s.fullPixel = (uint8_t)full;
    if (!rd_init(512, 512, &s, NULL)) {
        return false;
    }
    static const uint8_t black[4] = {0, 0, 0, 0x80}, white[4] = {255, 255, 255, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_texture_off();
    sprite(RD_SPACE_UI, 192 * 16, 192 * 16, 320 * 16, 320 * 16, white, 0, 0, 0, 0);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd__read_present(out, sizeof(out), &ow, &oh) && ow == w && oh == h;
    CHECK(rhi_vk_validation_error_count() == 0, "full pixel square: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (!ok) {
        return false;
    }
    *sw = *sh = 0;
    for (uint32_t x = 0; x < w; x++) {
        *sw += out[((size_t)(h / 2) * w + x) * 4] > 128;
    }
    for (uint32_t y = 0; y < h; y++) {
        *sh += out[((size_t)y * w + w / 2) * 4] > 128;
    }
    return true;
}

/* full pixel no longer enlarges the picture: at 512 lines the square is the
 * same size on and off, on both axes */
static void checkFullPixelSquare(void)
{
    uint32_t w0 = 0, h0 = 0, w1 = 0, h1 = 0;
    if (!squareRun(0, &w0, &h0) || !squareRun(1, &w1, &h1)) {
        CHECK(0, "full pixel square: present readback");
        return;
    }
    printf("  full pixel square: off %u x %u, on %u x %u\n", w0, h0, w1, h1);
    CHECK(w0 > 200 && h0 > 150, "full pixel square: drawn (%u x %u)", w0, h0);
    CHECK(w0 == w1 && h0 == h1, "full pixel square: the same size on and off (%u x %u, %u x %u)",
          w0, h0, w1, h1);
}

/* a one-pixel white column at GS x = 256 (and a one-pixel white row at GS
 * y = 256) on black through the reduction, presented into w x h */
static bool lineRun(int full, uint32_t w, uint32_t h, uint8_t *out)
{
    RdSettings s = originalSettings();
    s.fullPixel = (uint8_t)full;
    s.outputWidth = w;
    s.outputHeight = h;
    if (!rd_init(512, 512, &s, NULL)) {
        return false;
    }
    static const uint8_t black[4] = {0, 0, 0, 0x80}, white[4] = {255, 255, 255, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_texture_off();
    sprite(RD_SPACE_UI, 256 * 16, 0, 257 * 16, 512 * 16, white, 0, 0, 0, 0);
    sprite(RD_SPACE_UI, 0, 256 * 16, 512 * 16, 257 * 16, white, 0, 0, 0, 0);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd__read_present(out, (size_t)w * h * 4, &ow, &oh) && ow == w && oh == h;
    CHECK(rhi_vk_validation_error_count() == 0, "full pixel lines: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return ok;
}

/* the reduction is native with the option on: the picture inside the box
 * is the same pixels as with it off, not resampled, so a one-pixel line
 * lands on the same output pixels with the same values.  Only the interior
 * is compared (an eighth in from each side): the strip itself is the same
 * reduction draw either way, so it cannot show a resample. */
static void checkFullPixelSharp(uint32_t w, uint32_t h)
{
    static uint8_t off[1440 * 1080 * 4], on[1440 * 1080 * 4];
    if (!lineRun(0, w, h, off) || !lineRun(1, w, h, on)) {
        CHECK(0, "full pixel sharp %ux%u: present readback", w, h);
        return;
    }
    RhiRect box;
    rd__present_box(w, h, 4.0f / 3.0f, &box);
    /* inside the border's reach: an eighth in from each side */
    const uint32_t x0 = (uint32_t)box.x + box.w / 8, x1 = (uint32_t)box.x + box.w - box.w / 8;
    const uint32_t y0 = (uint32_t)box.y + box.h / 8, y1 = (uint32_t)box.y + box.h - box.h / 8;
    int same = 1, lit = 0;
    for (uint32_t y = y0; y < y1; y++) {
        same &= memcmp(&off[((size_t)y * w + x0) * 4], &on[((size_t)y * w + x0) * 4],
                       (size_t)(x1 - x0) * 4) == 0;
    }
    for (uint32_t x = x0; x < x1; x++) {
        lit += off[((size_t)(h / 2 - 40) * w + x) * 4] > 128;
    }
    CHECK(lit > 0, "full pixel sharp %ux%u: the line is drawn", w, h);
    CHECK(same, "full pixel sharp %ux%u: the same output pixels on and off", w, h);
}

/* SCENE (red, with the extras the case draws) through the reduction with
 * Full pixel on or off, DISPLAY read back; scale above 1 puts the reduction
 * on the scaled target (the hardware sprite, where the clamp on the rows
 * matters), fullHeight doubles the rows, mirror flips the UI sprite.
 * bandTest 0: a blue column 0 and a blue row 0 in the scene.  1: a black
 * band, GS 1..511 across and the middle half of the rows, as the pause
 * menu's bars are drawn. */
static uint8_t *clampRun(int full, float scale, int fullHeight, int mirror, int bandTest,
                         uint32_t *w, uint32_t *h)
{
    static const uint8_t red[4] = {255, 0, 0, 0x80}, blue[4] = {0, 0, 255, 0x80},
                         black[4] = {0, 0, 0, 0x80};
    RdSettings s = originalSettings();
    s.fullPixel = (uint8_t)full;
    s.fullHeightScene = (uint8_t)fullHeight;
    s.mirror = (uint8_t)mirror;
    if (scale > 1.0f) {
        s.preset = RD_PRESET_ENHANCED;
        s.sceneScale = scale;
    }
    if (!rd_init(512, 512, &s, NULL)) {
        return NULL;
    }
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), red, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_texture_off();
    if (bandTest) {
        sprite(RD_SPACE_UI, 4, 128 * 16, 511 * 16 + 7, 384 * 16, black, 0, 0, 0, 0);
    } else {
        sprite(RD_SPACE_WORLD, 0, 0, 16, 512 * 16, blue, 0, 0, 0, 0);
        sprite(RD_SPACE_WORLD, 0, 0, 512 * 16, 16, blue, 0, 0, 0, 0);
    }
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    uint8_t *p = readTarget(RD_TARGET_DISPLAY, w, h);
    CHECK(rhi_vk_validation_error_count() == 0, "full pixel clamp: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return p && *w >= 512 && *h >= 64 ? p : NULL;
}

/* the reduction's edges read their own colour, not the opposite edge's: SCENE
 * red with a blue column 0 and a blue row 0.  With the option on DISPLAY's
 * last column and last row hold no blue (a lost clamp wraps column 0 and row
 * 0 into them; on the scaled target and with full height the last row's
 * bilinear taps reach past the scene's rows, where only the clamp matters),
 * and with the mirror on the left column, which then samples past the other
 * edge, is red.  The plain 1x case also keeps the exact value of column 0. */
static void checkFullPixelClamp(void)
{
    static const struct {
        float scale;
        int fullHeight, mirror;
        const char *name;
    } cases[] = {
        {1.0f, 0, 0, "1x"},        {1.0f, 1, 0, "1x full height"},
        {2.0f, 0, 0, "2x"},        {2.0f, 1, 0, "2x full height"},
        {1.0f, 0, 1, "1x mirror"}, {2.0f, 0, 1, "2x mirror"},
    };

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        for (int full = 0; full < 2; full++) {
            const char *nm = cases[k].name;
            uint32_t w = 0, h = 0;
            uint8_t *p =
                clampRun(full, cases[k].scale, cases[k].fullHeight, cases[k].mirror, 0, &w, &h);
            if (!p) {
                CHECK(0, "full pixel clamp %s: DISPLAY readback (%ux%u)", nm, w, h);
                continue;
            }
#define PX(x, y) (&p[((size_t)(y) * w + (x)) * 4])
#define NOBLUE(q) ((q)[0] >= 250 && (q)[1] <= 2 && (q)[2] <= 2)
            const uint8_t *r = PX(w - 1, h / 2), *l = PX(0, h / 2);
            if (full) {
                const uint8_t *bot = PX(w / 2, h - 1), *corner = PX(w - 1, h - 1);
                CHECK(NOBLUE(bot) && NOBLUE(corner),
                      "full pixel clamp %s: the last row is red (%u %u %u, corner %u %u %u)", nm,
                      bot[0], bot[1], bot[2], corner[0], corner[1], corner[2]);
                if (cases[k].mirror) {
                    CHECK(NOBLUE(l), "full pixel clamp %s: the left column is red (%u %u %u)", nm,
                          l[0], l[1], l[2]);
                    continue; /* the right column is the flipped blue one */
                }
                CHECK(NOBLUE(r), "full pixel clamp %s: the last column is red (%u %u %u)", nm, r[0],
                      r[1], r[2]);
                if (cases[k].scale == 1.0f && !cases[k].fullHeight) {
                    /* the GS samples column 0 at u = 0.75: three quarters of
                       the blue texel and a quarter of the red one beside it */
                    CHECK(l[2] >= 180 && l[2] <= 200 && l[0] >= 55 && l[0] <= 75 && l[1] <= 2,
                          "full pixel clamp %s: column 0 is three quarters blue (%u %u %u)", nm,
                          l[0], l[1], l[2]);
                }
            } else {
                CHECK(r[0] < 8 && r[1] < 8 && r[2] < 8 && l[0] < 8 && l[1] < 8 && l[2] < 8,
                      "full pixel clamp %s: off, the border columns stay black", nm);
            }
#undef NOBLUE
#undef PX
        }
    }
}

/* the layout's full-width bands (the pause and End Game bars, GS pixels 1
 * to 511 of 512) reach the frame's edge with the option on: the strip it
 * shows beside the bar is the bar, not a column of the scene under it.
 * Rows outside the band keep the scene's red at the edge, and with the
 * option off the border stays black as it was. */
static void checkFullPixelBands(void)
{
    static const struct {
        float scale;
        int mirror;
        const char *name;
    } cases[] = {{1.0f, 0, "1x"}, {2.0f, 0, "2x"}, {1.0f, 1, "1x mirror"}, {2.0f, 1, "2x mirror"}};

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        for (int full = 0; full < 2; full++) {
            const char *nm = cases[k].name;
            uint32_t w = 0, h = 0;
            uint8_t *p = clampRun(full, cases[k].scale, 0, cases[k].mirror, 1, &w, &h);
            if (!p) {
                CHECK(0, "full pixel bands %s: DISPLAY readback (%ux%u)", nm, w, h);
                continue;
            }
#define PX(x, y) (&p[((size_t)(y) * w + (x)) * 4])
#define DARK(q) ((q)[0] < 8 && (q)[1] < 8 && (q)[2] < 8)
            const uint8_t *l = PX(0, h / 2), *r = PX(w - 1, h / 2), *m = PX(w / 2, h / 2);
            if (full) {
                const uint8_t *lo = PX(0, h / 8), *ro = PX(w - 1, h / 8);
                CHECK(DARK(l) && DARK(r) && DARK(m),
                      "full pixel bands %s: the band is black to both edges (%u %u %u, %u %u %u)",
                      nm, l[0], l[1], l[2], r[0], r[1], r[2]);
                CHECK(lo[0] >= 250 && lo[1] <= 2 && lo[2] <= 2 && ro[0] >= 250 && ro[1] <= 2 &&
                          ro[2] <= 2,
                      "full pixel bands %s: rows outside the band are red at the edge", nm);
            } else {
                CHECK(DARK(l) && DARK(r) && DARK(m),
                      "full pixel bands %s: off, the border columns stay black", nm);
            }
#undef DARK
#undef PX
        }
    }
}

/* with interpolation off rd_end_frame presents each frame once, and
 * rd_last_present_info (the window's start-up log) names that present */
static void checkPresentInfo(void)
{
    RdSettings s = originalSettings();
    s.interpolate = 0;
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    RdPresentInfo pi;
    CHECK(!rd_last_present_info(&pi), "present info: none before the first present");
    makeNoiseScene();
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    for (int k = 0; k < 2; k++) {
        recordRichFrame(t, 1);
        memset(&pi, 0, sizeof(pi));
        CHECK(rd_last_present_info(&pi) && pi.frame == rd_frame_number() && pi.firstOfTick &&
                  !pi.keep,
              "present info: frame %u's present noted (frame %u, first %u, keep %u)",
              rd_frame_number(), pi.frame, pi.firstOfTick, pi.keep);
    }
    rd_destroy_texture(t);
    rd_shutdown();
}

static void checkFullPixel(void)
{
    checkFullPixelCase("fullpixel 4:3 640x480", 4.0f / 3.0f, 640, 480, 0, 1.0f);
    checkFullPixelCase("fullpixel 4:3 full height", 4.0f / 3.0f, 640, 480, 1, 1.0f);
    checkFullPixelCase("fullpixel 4:3 in 16:9 (pillars)", 4.0f / 3.0f, 960, 540, 0, 1.0f);
    checkFullPixelCase("fullpixel pillars, full height", 4.0f / 3.0f, 960, 540, 1, 1.0f);
    checkFullPixelCase("fullpixel 16:9", 16.0f / 9.0f, 960, 540, 0, 1.0f);
    checkFullPixelCase("fullpixel 16:9 Enhanced 2x", 16.0f / 9.0f, 960, 540, 0, 2.0f);
}

static void checkMips(void)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f;
    s.filterUpgrade = RD_FILTER_UPGRADE_TRILINEAR;
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    if (!rhi_limits()->textureMips) {
        printf("  mips: the backend has no mipmapped textures; skipped\n");
        rd_shutdown();
        return;
    }
    /* 64 x 64 one-texel checker, white and black */
    static uint8_t img[64 * 64 * 4];
    for (int i = 0; i < 64 * 64; i++) {
        const int c = ((i % 64) + (i / 64)) & 1 ? 255 : 0;
        img[i * 4 + 0] = img[i * 4 + 1] = img[i * 4 + 2] = (uint8_t)c;
        img[i * 4 + 3] = 0x80;
    }
    RdTex t = rd_create_texture(64, 64, img, RD_TEXA_80_80, "checker");
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80}, black[4] = {0, 0, 0, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    /* minified 8:1 into 8 x 8 pixels */
    sprite(RD_SPACE_WORLD, 16 * 16, 16 * 16, 24 * 16, 24 * 16, grey, 0, 0, 64 * 16, 64 * 16);
    rd_end_frame(0);
    const RdTexRec *tr = rd__tex_rec(t.id);
    CHECK(tr && tr->mipLevels == 7, "mips: a 64x64 texture gets 7 levels (%u)",
          tr ? tr->mipLevels : 0);
    uint32_t w, h;
    uint8_t *p = readTarget(RD_TARGET_SCENE, &w, &h);
    int worst = 0;
    if (p) {
        for (int y = 17; y < 23; y++) {
            for (int x = 17; x < 23; x++) {
                const int e = abs((int)p[(y * w + x) * 4] - 128);
                worst = e > worst ? e : worst;
            }
        }
    }
    printf("  mips: minified checker within %d of mid grey\n", worst);
    CHECK(worst <= 8, "mips: the minified checker samples its average");
    rd_destroy_texture(t);
    CHECK(rhi_vk_validation_error_count() == 0, "mips: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
}

/* Issue 9's far railings.  A 64 x 64 lattice (wires every 4th
 * row and column, 7/16 of the texels, colour (200, 200, 200) alpha 0x80;
 * holes black alpha 0) drawn as the railings are, TEST 0x5160D and ALPHA
 * 0x44 with ABE, minified 8:1 under the trilinear filter: level 3 is
 * uniform, alpha 56 and the wire colour, so the pixel is the background
 * lerped towards 200 by 56/128.  The box filter with the coverage kept at
 * 64 gave colour 87 at alpha 65: a dark sheet over the background.  Run
 * with FBA off and on (the game's materials set it, Packet.c). */
static void checkLatticeMips(int fba)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f;
    s.filterUpgrade = RD_FILTER_UPGRADE_TRILINEAR;
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    if (!rhi_limits()->textureMips) {
        printf("  lattice mips: the backend has no mipmapped textures; skipped\n");
        rd_shutdown();
        return;
    }
    static uint8_t img[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t *p = &img[(y * 64 + x) * 4];
            const int wire = x % 4 == 0 || y % 4 == 0;
            p[0] = p[1] = p[2] = wire ? 200 : 0;
            p[3] = wire ? 0x80 : 0;
        }
    }
    RdTex t = rd_create_texture(64, 64, img, RD_TEXA_80_80, "lattice");
    /* dots: 1 texel in 16 alpha 0x80, drawn unblended with AFAIL KEEP and
     * ATST GREATER 0x30 */
    static uint8_t dots[64 * 64 * 4];
    for (int i = 0; i < 64 * 64; i++) {
        dots[i * 4 + 0] = dots[i * 4 + 1] = dots[i * 4 + 2] = 200;
        dots[i * 4 + 3] = (i % 4 == 0 && (i / 64) % 4 == 0) ? 0x80 : 0;
    }
    RdTex td = rd_create_texture(64, 64, dots, RD_TEXA_80_80, "dots");
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80}, bg[4] = {0, 160, 0, 0x80};
    for (int frame = 0; frame < 2; frame++) {
        rd_begin_frame();
        rd_select_list(0);
        rd_clear_target(rd_target(RD_TARGET_SCENE), bg, 1, 0);
        rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
        rd_select_list(1);
        rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
        rd_test_gs(0x5160D);
        rd_z_write(1);
        rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
        rd_pabe(0);
        rd_fba(fba);
        rd_sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
        rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        sprite(RD_SPACE_WORLD, 16 * 16, 16 * 16, 24 * 16, 24 * 16, grey, 0, 0, 64 * 16, 64 * 16);
        rd_test_gs(0x3030D); /* ATE GREATER 0x30, AFAIL KEEP, Z ALWAYS */
        rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
        rd_fba(0);
        rd_texture(td, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        sprite(RD_SPACE_WORLD, 48 * 16, 16 * 16, 56 * 16, 24 * 16, grey, 0, 0, 64 * 16, 64 * 16);
        rd_end_frame(0);
    }
    const RdTexRec *tr = rd__tex_rec(t.id), *trd = rd__tex_rec(td.id);
    CHECK(tr && tr->mipLevels == 7 && (tr->mipUse & RD_MIPUSE_BLEND_AS) && !tr->mipBuiltBoost,
          "lattice mips: blended by As, no coverage kept (use %u boost %u)", tr ? tr->mipUse : 0,
          tr ? tr->mipBuiltBoost : 0);
    CHECK(trd && trd->mipUse == RD_MIPUSE_TESTED && trd->mipBuiltBoost && trd->mipBuiltRef == 0x30,
          "lattice mips: the unblended alpha-tested texture keeps coverage at its AREF 0x30 "
          "(use %u boost %u ref %u)",
          trd ? trd->mipUse : 0, trd ? trd->mipBuiltBoost : 0, trd ? trd->mipBuiltRef : 0);
    uint32_t w, h;
    uint8_t *p = readTarget(RD_TARGET_SCENE, &w, &h);
    /* (Cs - Cd) * 56 / 128 + Cd: R 87, G 177, B 87 */
    const int want[3] = {(200 * 56) / 128, 160 + ((200 - 160) * 56) / 128, (200 * 56) / 128};
    int worst = 0;
    if (p) {
        for (int y = 17; y < 23; y++) {
            for (int x = 17; x < 23; x++) {
                for (int c = 0; c < 3; c++) {
                    const int e = abs((int)p[(y * w + x) * 4 + c] - want[c]);
                    worst = e > worst ? e : worst;
                }
            }
        }
    }
    printf("  lattice mips (FBA %d): minified railing within %d of the blend by its average "
           "alpha\n",
           fba, worst);
    CHECK(p && worst <= 6, "lattice mips (FBA %d): the minified lattice is off by %d", fba, worst);
    rd_destroy_texture(t);
    rd_destroy_texture(td);
    CHECK(rhi_vk_validation_error_count() == 0, "lattice mips: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
}

/* ----------------------------------------------------------- the overlay */

typedef struct OvTest {
    int rects;  /* draw the two rectangles and the glyph */
    RdRect box; /* the ctx the callback saw */
    float boxScale;
    int mirror;
    int calls;
    int32_t hx0, hy0, hx1, hy1; /* the glyph quad, output pixels */
    int hquads;
} OvTest;

static OvTest s_ovt;

/* the box rectangle and the pillarbox one, output pixels from the box */
#define OV_RX0 100
#define OV_RY0 50
#define OV_RX1 140
#define OV_RY1 71

static void ovRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t c[4])
{
    RdScreenVtx v[2] = {vtx(x0 * 16, y0 * 16, 0, c, 0.0f, 0.0f),
                        vtx(x1 * 16, y1 * 16, 0, c, 0.0f, 0.0f)};
    rd_overlay_prims(RD_PRIM_SPRITES, v, 2, (RdTex){0}, RD_BLEND_LERP_AS);
}

/* the glyph's quad on its way to rd (font.c's overlay sink) */
static void ovSink(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex, RdBlend blend)
{
    if (n == 2) {
        s_ovt.hx0 = v[0].x / 16;
        s_ovt.hy0 = v[0].y / 16;
        s_ovt.hx1 = v[1].x / 16;
        s_ovt.hy1 = v[1].y / 16;
        s_ovt.hquads++;
    }
    rd_overlay_prims(type, v, n, tex, blend);
}

static void ovCallback(const RdOverlayCtx *ctx, void *user)
{
    OvTest *t = user;
    t->calls++;
    t->box = ctx->box;
    t->boxScale = ctx->boxScale;
    t->mirror = ctx->mirror;
    if (!t->rects) {
        return;
    }
    static const uint8_t red[4] = {255, 0, 0, 0x80}, green[4] = {0, 255, 0, 0x80},
                         white[4] = {0x80, 0x80, 0x80, 0x80};
    ovRect(ctx->box.x + OV_RX0, ctx->box.y + OV_RY0, ctx->box.x + OV_RX1, ctx->box.y + OV_RY1, red);
    /* the whole output is the overlay's, the pillarbox too (1080p) */
    if (ctx->box.x >= 30) {
        ovRect(10, 20, 30, 40, green);
    }
    ui__set_overlay_sink(ovSink);
    ui_begin_overlay(ctx);
    ui_draw_text(320.0f, 300.0f, 40.0f, white, "H", UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    ui_end_overlay();
    ui__set_overlay_sink(NULL);
}

/* the rich frame presented at w x h, mirror on or off, with the overlay
 * (rects) or without one; the output into dst */
static bool ovPresent(uint32_t w, uint32_t h, int mirror, int rects, uint8_t *dst)
{
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.outputWidth = w;
    s.outputHeight = h;
    s.mirror = (uint8_t)mirror;
    if (!rd_init(512, 512, &s, NULL)) {
        return false;
    }
    memset(&s_ovt, 0, sizeof(s_ovt));
    s_ovt.rects = rects;
    if (rects) {
        rd_set_present_overlay(ovCallback, &s_ovt);
    }
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd_read_presented(dst, &ow, &oh) && ow == w && oh == h;
    CHECK(ok, "overlay: the presented %ux%u output", w, h);
    CHECK(!rects || s_ovt.calls == 1, "overlay: one callback a present (%d)", s_ovt.calls);
    rd_set_present_overlay(NULL, NULL);
    ui_font_shutdown();
    rd_destroy_texture(t);
    CHECK(rhi_vk_validation_error_count() == 0, "overlay: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return ok;
}

static int ovInside(int32_t x, int32_t y, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

static void checkOverlayAt(uint32_t w, uint32_t h)
{
    const size_t n = (size_t)w * h * 4;
    uint8_t *plain = malloc(n), *over = malloc(n), *plainM = malloc(n), *overM = malloc(n);
    if (!plain || !over || !plainM || !overM) {
        CHECK(0, "overlay: memory");
        free(plain);
        free(over);
        free(plainM);
        free(overM);
        return;
    }
    if (!ovPresent(w, h, 0, 0, plain) || !ovPresent(w, h, 0, 1, over)) {
        free(plain);
        free(over);
        free(plainM);
        free(overM);
        return;
    }
    const OvTest t = s_ovt;
    RhiRect box;
    rd__present_box(w, h, 4.0f / 3.0f, &box);
    CHECK(t.box.x == box.x && t.box.y == box.y && t.box.w == box.w && t.box.h == box.h &&
              fabsf(t.boxScale - (float)box.h / 448.0f) < 1e-6f && !t.mirror,
          "overlay %ux%u: the ctx (box %d,%d %ux%u scale %g)", w, h, t.box.x, t.box.y, t.box.w,
          t.box.h, (double)t.boxScale);
    const int32_t rx0 = box.x + OV_RX0, ry0 = box.y + OV_RY0;
    const int32_t rx1 = box.x + OV_RX1, ry1 = box.y + OV_RY1;
    const int pillar = box.x >= 30;
    /* the glyph: a px-pixel "H", its quad the bitmap's size */
    const int px = (int)lrintf(40.0f * (float)box.h / 448.0f);
    UiGlyph g;
    CHECK(t.hquads == 1 && ui_font_glyph('H', px, &g) && t.hx1 - t.hx0 == g.w &&
              t.hy1 - t.hy0 == g.h,
          "overlay %ux%u: one glyph quad of the %d px bitmap (%d x %d)", w, h, px, t.hx1 - t.hx0,
          t.hy1 - t.hy0);
    uint32_t badRect = 0, badOutside = 0, stem = 0, pillarBad = 0;
    for (int32_t y = 0; y < (int32_t)h; y++) {
        for (int32_t x = 0; x < (int32_t)w; x++) {
            const size_t i = ((size_t)y * w + (size_t)x) * 4;
            const uint8_t *o = &over[i];
            if (ovInside(x, y, rx0, ry0, rx1, ry1)) {
                badRect += !(o[0] == 255 && o[1] == 0 && o[2] == 0);
            } else if (pillar && ovInside(x, y, 10, 20, 30, 40)) {
                pillarBad += !(o[0] == 0 && o[1] == 255 && o[2] == 0);
            } else if (ovInside(x, y, t.hx0, t.hy0, t.hx1, t.hy1)) {
                stem += o[0] == 255 && o[1] == 255 && o[2] == 255;
            } else {
                badOutside += memcmp(o, &plain[i], 4) != 0;
            }
        }
    }
    CHECK(badRect == 0, "overlay %ux%u: %u pixels of the rectangle are not its red", w, h, badRect);
    CHECK(pillarBad == 0, "overlay %ux%u: %u pillarbox pixels are not its green", w, h, pillarBad);
    /* the stems: two bars about 0.1 em wide over the capitals' height */
    CHECK(stem > (uint32_t)(g.h * px / 8), "overlay %ux%u: %u solid white glyph pixels", w, h,
          stem);
    CHECK(badOutside == 0, "overlay %ux%u: %u pixels outside the overlay changed", w, h,
          badOutside);
    printf("  overlay %ux%u: box %d,%d %ux%u, H %d px at %d,%d (%u solid)\n", w, h, box.x, box.y,
           box.w, box.h, px, t.hx0, t.hy0, stem);

    /* the mirror flips the box blit, never the overlay */
    if (ovPresent(w, h, 1, 0, plainM) && ovPresent(w, h, 1, 1, overM)) {
        CHECK(s_ovt.mirror, "overlay %ux%u: ctx.mirror with the mirror on", w, h);
        /* the rich frame is all UI: the replay's flip and the present's
         * cancel, and the reduction samples the mirror image
         * too, so the box shows the same picture, exactly where the box's
         * horizontal scale gives mirrored bilinear weights equal ones, else within 1 LSB */
        int maxd = 0;
        for (size_t i = 0; i < n; i++) {
            const int d = abs((int)plain[i] - (int)plainM[i]);
            maxd = d > maxd ? d : maxd;
        }
        CHECK(maxd <= 1, "overlay %ux%u: the mirrored all-UI picture is the same (max %d LSB)", w,
              h, maxd);
        uint32_t same = 0, total = 0, outside = 0;
        for (int32_t y = 0; y < (int32_t)h; y++) {
            for (int32_t x = 0; x < (int32_t)w; x++) {
                const size_t i = ((size_t)y * w + (size_t)x) * 4;
                if (ovInside(x, y, rx0, ry0, rx1, ry1) ||
                    (pillar && ovInside(x, y, 10, 20, 30, 40))) {
                    total++;
                    same += memcmp(&over[i], &overM[i], 3) == 0;
                } else if (!ovInside(x, y, t.hx0, t.hy0, t.hx1, t.hy1)) {
                    outside += memcmp(&overM[i], &plainM[i], 4) != 0;
                }
            }
        }
        CHECK(same == total, "overlay %ux%u mirrored: %u of %u rectangle pixels where they were", w,
              h, same, total);
        CHECK(s_ovt.hx0 == t.hx0 && s_ovt.hy0 == t.hy0,
              "overlay %ux%u mirrored: the glyph where it was (%d,%d against %d,%d)", w, h,
              s_ovt.hx0, s_ovt.hy0, t.hx0, t.hy0);
        CHECK(outside == 0, "overlay %ux%u mirrored: %u pixels outside the overlay changed", w, h,
              outside);
    }
    free(plain);
    free(over);
    free(plainM);
    free(overM);
}

static void checkOverlay(uint64_t presentNoOverlay)
{
    checkOverlayAt(960, 720);
    checkOverlayAt(1920, 1080);
    /* registered and unregistered again: the Original present unchanged */
    rd_set_present_overlay(ovCallback, &s_ovt);
    rd_set_present_overlay(NULL, NULL);
    makeNoiseScene();
    RdSettings s = originalSettings();
    FrameHashes h;
    if (richFrame(&s, &h)) {
        CHECK(h.present == presentNoOverlay,
              "overlay: without a callback the present hashes %016llx, not %016llx",
              (unsigned long long)h.present, (unsigned long long)presentNoOverlay);
        CHECK(!s_llvmpipe || h.present == GOLD_PRESENT,
              "overlay: without a callback the present is the recorded one");
    }
}

/* the overlay's frame drawn in white along its ctx box's top and bottom
 * lines (4 grid lines each) */
static RdOverlayCtx s_edgeCtx;
static int s_edgeCalls;

static void edgeCallback(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    s_edgeCtx = *ctx;
    s_edgeCalls++;
    static const uint8_t white[4] = {255, 255, 255, 0x80};
    const int32_t x0 = ctx->box.x, x1 = ctx->box.x + (int32_t)ctx->box.w;
    const int32_t y0 = ctx->box.y, y1 = ctx->box.y + (int32_t)ctx->box.h;
    ovRect(x0, y0, x1, y0 + 4, white);
    ovRect(x0, y1 - 4, x1, y1, white);
}

/* the CRT filter at 512 lines (50 Hz), 960 x 720, full pixel on or off: the
 * filter draws the whole grid into the box, so the overlay is laid out on
 * the whole grid and its top and bottom lines show inside the output.
 * boxOut gets the overlay's box. */
static void crtOverlayRun(int full, RdRect *boxOut)
{
    const uint32_t w = 960, h = 720;
    static uint8_t out[960 * 720 * 4];
    RdSettings s = originalSettings();
    s.fullPixel = (uint8_t)full;
    rd_crt_settings(&s, RD_CRT_SCANLINES, 1.0f);
    s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
    s.crtScanlines = 0.0f; /* a flat beam: every output row of a lit line lit */
    if (!rd_init(512, 512, &s, NULL)) {
        return;
    }
    s_edgeCalls = 0;
    rd_set_present_overlay(edgeCallback, NULL);
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd__read_present(out, sizeof(out), &ow, &oh) && ow == w && oh == h;
    rd_set_present_overlay(NULL, NULL);
    CHECK(rhi_vk_validation_error_count() == 0, "crt overlay (full pixel %d): %u validation errors",
          full, rhi_vk_validation_error_count());
    rd_shutdown();
    if (!ok) {
        CHECK(0, "crt overlay (full pixel %d): present readback", full);
        return;
    }
    const RdRect b = s_edgeCtx.box;
    printf("  full pixel crt overlay: grid %ux%u, box %d,%d %ux%u\n", s_edgeCtx.outW,
           s_edgeCtx.outH, b.x, b.y, b.w, b.h);
    CHECK(s_edgeCalls == 1, "crt overlay (full pixel %d): one callback (%d)", full, s_edgeCalls);
    CHECK(s_edgeCtx.outH == 512 && b.x == 0 && b.y == 0 && b.w == s_edgeCtx.outW &&
              b.h == s_edgeCtx.outH,
          "crt overlay (full pixel %d): the box is the whole grid (%d,%d %ux%u of %ux%u)", full,
          b.x, b.y, b.w, b.h, s_edgeCtx.outW, s_edgeCtx.outH);
    /* the centre column: the top and bottom bands lit within the output's
     * first and last 12 rows (4 grid lines are about 5.6 output rows) */
    int top = 0, bottom = 0;
    for (uint32_t y = 0; y < 12; y++) {
        top |= out[((size_t)y * w + w / 2) * 4] > 128;
        bottom |= out[((size_t)(h - 1 - y) * w + w / 2) * 4] > 128;
    }
    CHECK(top && bottom, "crt overlay (full pixel %d): the overlay's top (%d) and bottom (%d) show",
          full, top, bottom);
    /* the middle row stays the black scene */
    CHECK(out[((size_t)(h / 2) * w + w / 2) * 4] < 16, "crt overlay (full pixel %d): black between",
          full);
    *boxOut = b;
}

/* the overlay's box does not depend on the option: it is the whole grid
 * either way */
static void checkFullPixelCrtOverlay(void)
{
    RdRect off = {0}, on = {0};
    crtOverlayRun(0, &off);
    crtOverlayRun(1, &on);
    CHECK(off.x == on.x && off.y == on.y && off.w == on.w && off.h == on.h,
          "crt overlay: the box is the same with full pixel off (%d,%d %ux%u) and on (%d,%d %ux%u)",
          off.x, off.y, off.w, off.h, on.x, on.y, on.w, on.h);
}

/* the overlay drawn into the CRT filter's grid */
static void checkOverlayCrt(void)
{
    const uint32_t w = 1536, h = 1152;
    uint8_t *px = malloc((size_t)w * h * 4);
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.outputWidth = w;
    s.outputHeight = h;
    rd_crt_settings(&s, RD_CRT_TRINITRON, 1.0f);
    s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
    s.crtMask = 1.0f; /* pure stripes: the R column passes red alone (in a highlight,
                       * the overlay's full red, the strength eases to half: the G
                       * column leaks half the R column's red) */
    if (!px || !rd_init(512, 512, &s, NULL)) {
        free(px);
        return;
    }
    memset(&s_ovt, 0, sizeof(s_ovt));
    s_ovt.rects = 1;
    rd_set_present_overlay(ovCallback, &s_ovt);
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd_read_presented(px, &ow, &oh) && ow == w && oh == h;
    rd_set_present_overlay(NULL, NULL);
    ui_font_shutdown();
    rd_destroy_texture(t);
    CHECK(rhi_vk_validation_error_count() == 0, "overlay crt: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    CHECK(ok && s_ovt.calls == 1, "overlay crt: one callback, the present read");
    CHECK(s_ovt.box.x == 0 && s_ovt.box.y == 0 && s_ovt.box.w == 512 && s_ovt.box.h == 512 &&
              fabsf(s_ovt.boxScale - 512.0f / 448.0f) < 1e-6f,
          "overlay crt: the callback's box is the grid (%d, %d, %u x %u, scale %.3f)", s_ovt.box.x,
          s_ovt.box.y, s_ovt.box.w, s_ovt.box.h, s_ovt.boxScale);
    if (ok) {
        /* the rectangle: grid x 100..140, frame lines 50..71 (DISPLAY lines
         * 25..35): output columns 300..420, rows about 112..160 */
        const uint32_t y = 136;
        const float leak = powf(0.5f, 1.0f / 2.2f);
        int phosphors = 1;
        for (uint32_t gx = 104; gx < 136; gx++) {
            const uint8_t *r = &px[((size_t)y * w + gx * 3) * 4];
            const uint8_t *g = &px[((size_t)y * w + gx * 3 + 1) * 4];
            phosphors &= r[0] > 100 && r[1] < 30 && r[2] < 30 && g[1] < 30 &&
                         fabsf((float)g[0] / (float)r[0] - leak) < 0.05f;
        }
        const uint8_t *r = &px[((size_t)y * w + 360) * 4];
        printf("  overlay crt: red rectangle at (360, %u): R column %u %u %u, G column %u %u %u\n",
               y, r[0], r[1], r[2], r[4], r[5], r[6]);
        CHECK(phosphors, "overlay crt: the overlay's red is the R phosphors of its blocks");
    }
    free(px);
}

/* the top layer (rd_set_present_overlay_top) without the CRT
 * filter: one rectangle a layer, in output pixels from the box */
typedef struct LayerTest {
    RdOverlayCtx ctx;
    int calls;
    int32_t x0, y0, x1, y1;
    uint8_t c[4];
} LayerTest;

static void layerRect(const RdOverlayCtx *ctx, void *user)
{
    LayerTest *t = user;
    t->ctx = *ctx;
    t->calls++;
    ovRect(ctx->box.x + t->x0, ctx->box.y + t->y0, ctx->box.x + t->x1, ctx->box.y + t->y1, t->c);
}

static void checkOverlayTop(void)
{
    const uint32_t w = 960, h = 720;
    uint8_t *px = malloc((size_t)w * h * 4);
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.outputWidth = w;
    s.outputHeight = h;
    if (!px || !rd_init(512, 512, &s, NULL)) {
        free(px);
        return;
    }
    /* the main layer's red, the top layer's blue across its lower right */
    LayerTest m = {.x0 = 100, .y0 = 50, .x1 = 140, .y1 = 71, .c = {255, 0, 0, 0x80}};
    LayerTest t = {.x0 = 120, .y0 = 60, .x1 = 180, .y1 = 90, .c = {0, 0, 255, 0x80}};
    /* the ring: both layers' batches counted, the counters reset by a
     * registration */
    const uint64_t align = rhi_limits()->uniformAlign;
    const uint64_t oneRect = sizeof(IcoDrawCB) + 2 * align + 16 + 2 * 6 * sizeof(IcoSpriteVertex);
    rd__overlay_collect(NULL, 0);
    CHECK(rd__overlay_ring_bytes() == 0, "overlay top: nothing registered, no ring");
    rd_set_present_overlay_top(layerRect, &t);
    rd__overlay_collect(NULL, 0);
    const uint64_t topOnly = rd__overlay_ring_bytes();
    rd_set_present_overlay_top(NULL, NULL);
    CHECK(rd__overlay_ring_bytes() == 0, "overlay top: a registration forgets the batches");
    rd_set_present_overlay(layerRect, &m);
    rd__overlay_collect(NULL, 0);
    const uint64_t mainOnly = rd__overlay_ring_bytes();
    rd_set_present_overlay_top(layerRect, &t);
    rd__overlay_collect(NULL, 0);
    const uint64_t both = rd__overlay_ring_bytes();
    CHECK(topOnly > 0 && topOnly == mainOnly && both == mainOnly + oneRect,
          "overlay top: the ring counts both layers (top %llu, main %llu, both %llu, a rect %llu)",
          (unsigned long long)topOnly, (unsigned long long)mainOnly, (unsigned long long)both,
          (unsigned long long)oneRect);
    /* the present */
    m.calls = t.calls = 0;
    RdTex tex = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(tex, 1);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd_read_presented(px, &ow, &oh) && ow == w && oh == h;
    CHECK(rd__overlay_ring_bytes() == 0 && !rd__overlay_grid_pending(),
          "overlay top: the batches forgotten after the present");
    rd_set_present_overlay(NULL, NULL);
    rd_set_present_overlay_top(NULL, NULL);
    rd_destroy_texture(tex);
    CHECK(rhi_vk_validation_error_count() == 0, "overlay top: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    CHECK(ok && m.calls == 1 && t.calls == 1, "overlay top: one call a layer (%d, %d)", m.calls,
          t.calls);
    CHECK(memcmp(&m.ctx, &t.ctx, sizeof(m.ctx)) == 0,
          "overlay top: without the filter both layers have the output's ctx");
    if (ok) {
        RhiRect box;
        rd__present_box(w, h, 4.0f / 3.0f, &box);
        uint32_t badMain = 0, badTop = 0, n = 0;
        for (int32_t y = box.y + m.y0; y < box.y + t.y1; y++) {
            for (int32_t x = box.x + m.x0; x < box.x + t.x1; x++) {
                const uint8_t *o = &px[((size_t)y * w + (size_t)x) * 4];
                const int inM =
                    ovInside(x, y, box.x + m.x0, box.y + m.y0, box.x + m.x1, box.y + m.y1);
                const int inT =
                    ovInside(x, y, box.x + t.x0, box.y + t.y0, box.x + t.x1, box.y + t.y1);
                if (inM) {
                    /* the main layer over the top one where they overlap */
                    badMain += !(o[0] == 255 && o[1] == 0 && o[2] == 0);
                    n += inT;
                } else if (inT) {
                    badTop += !(o[0] == 0 && o[1] == 0 && o[2] == 255);
                }
            }
        }
        CHECK(n == 20 * 11 && badMain == 0,
              "overlay top: %u pixels of the main red are not red (%u overlapping)", badMain, n);
        CHECK(badTop == 0, "overlay top: %u pixels of the top layer's blue are not blue", badTop);
    }
    free(px);
}

/* --------------------------------------------------------------- capture */

static uint32_t be32At(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* rd_png.c's PNG back to RGB (one IDAT, stored deflate blocks, filter 0) */
static uint8_t *readPng(const char *path, uint32_t *w, uint32_t *h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    const long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *f = n > 0 ? malloc((size_t)n) : NULL;
    const bool got = f && fread(f, 1, (size_t)n, fp) == (size_t)n;
    fclose(fp);
    if (!got || n < 33 || memcmp(f + 12, "IHDR", 4) != 0 || f[24] != 8 || f[25] != 2) {
        free(f);
        return NULL;
    }
    *w = be32At(f + 16);
    *h = be32At(f + 20);
    const size_t row = (size_t)*w * 3 + 1, raw = row * *h;
    uint8_t *scan = malloc(raw), *rgb = malloc((size_t)*w * *h * 3);
    size_t at = 33, have = 0;
    while (scan && rgb && at + 8 <= (size_t)n && have < raw) {
        const uint32_t len = be32At(f + at);
        if (memcmp(f + at + 4, "IDAT", 4) == 0) {
            size_t o = at + 8 + 2; /* the zlib header */
            while (have < raw && o + 5 <= at + 8 + len) {
                const size_t bl = (size_t)f[o + 1] | ((size_t)f[o + 2] << 8);
                memcpy(scan + have, f + o + 5, bl);
                have += bl;
                o += 5 + bl;
            }
        }
        at += 12 + len;
    }
    free(f);
    if (have != raw) {
        free(scan);
        free(rgb);
        return NULL;
    }
    for (uint32_t y = 0; y < *h; y++) {
        memcpy(rgb + (size_t)y * *w * 3, scan + y * row + 1, (size_t)*w * 3);
    }
    free(scan);
    return rgb;
}

static void checkCaptureAt(const char *dir, int crt)
{
    const uint32_t w = 800, h = 600;
    const size_t n = (size_t)w * h * 4;
    uint8_t *plain = malloc(n), *over = malloc(n);
    char png[1100];
    snprintf(png, sizeof(png), "%s/rd_present_capture_%d.png", dir, crt);
    remove(png);
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.outputWidth = w;
    s.outputHeight = h;
    if (crt) {
        rd_crt_settings(&s, RD_CRT_CONSUMER, 1.0f);
    }
    bool ok = plain && over;
    /* the present without the overlay */
    if (ok && rd_init(512, 512, &s, NULL)) {
        RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
        recordRichFrame(t, 1);
        uint32_t ow = 0, oh = 0;
        ok = rd_read_presented(plain, &ow, &oh) && ow == w && oh == h;
        rd_destroy_texture(t);
        rd_shutdown();
    } else {
        ok = false;
    }
    /* again with the overlay's rectangles and a capture armed */
    if (ok && rd_init(512, 512, &s, NULL)) {
        memset(&s_ovt, 0, sizeof(s_ovt));
        s_ovt.rects = 1;
        rd_set_present_overlay(ovCallback, &s_ovt);
        RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
        char done[1100];
        CHECK(rd_capture_result(done, sizeof(done)) == 0, "capture: nothing before");
        CHECK(rd_capture_presented(png), "capture: armed");
        recordRichFrame(t, 1);
        CHECK(rd_capture_result(done, sizeof(done)) == 1 && strcmp(done, png) == 0,
              "capture: written (%s)", png);
        CHECK(rd_capture_result(done, sizeof(done)) == 0, "capture: reported once");
        uint32_t ow = 0, oh = 0;
        ok = rd_read_presented(over, &ow, &oh);
        rd_set_present_overlay(NULL, NULL);
        ui_font_shutdown();
        rd_destroy_texture(t);
        CHECK(rhi_vk_validation_error_count() == 0, "capture: %u validation errors",
              rhi_vk_validation_error_count());
        rd_shutdown();
    } else {
        ok = false;
    }
    CHECK(ok, "capture (crt %d): the two presents", crt);
    uint32_t pw = 0, ph = 0;
    uint8_t *rgb = ok ? readPng(png, &pw, &ph) : NULL;
    CHECK(rgb && pw == w && ph == h, "capture (crt %d): a %ux%u PNG (%ux%u)", crt, w, h, pw, ph);
    if (rgb && pw == w && ph == h) {
        size_t diffPlain = 0, diffOver = 0;
        for (size_t i = 0; i < (size_t)w * h; i++) {
            diffPlain += memcmp(rgb + i * 3, plain + i * 4, 3) != 0;
            diffOver += memcmp(rgb + i * 3, over + i * 4, 3) != 0;
        }
        /* under the CRT filter too: the overlay is inside the filtered
         * picture shown, and the capture takes a pass of the
         * filter without it */
        CHECK(diffPlain == 0 && diffOver > 0,
              "capture (crt %d): the present without the overlay (%zu pixels differ; %zu "
              "from the one with it)",
              crt, diffPlain, diffOver);
    }
    free(rgb);
    free(plain);
    free(over);
}

static void checkCapture(const char *dir)
{
    checkCaptureAt(dir, 0);
    checkCaptureAt(dir, 1);
}

/* ------------------------------------------- the present's render passes */

/* the deferred text's renderer: one green rectangle a call, in output
 * pixels from the box (inside the item's region) */
static int s_textCalls;

static void textRect(const RdOverlayCtx *ctx, const RdTextItem *item, void *user)
{
    (void)item;
    (void)user;
    static const uint8_t green[4] = {0, 255, 0, 0x80};
    s_textCalls++;
    ovRect(ctx->box.x + 300, ctx->box.y + 150, ctx->box.x + 340, ctx->box.y + 170, green);
}

/* the scene, one deferred text item in list 11 and the reduction into
 * DISPLAY (list 12) */
static void recordTextFrame(RdTex t)
{
    rd_begin_frame();
    drawScene(t);
    rd_select_list(11);
    RdTextItem it;
    memset(&it, 0, sizeof(it));
    snprintf(it.utf8, sizeof(it.utf8), "Continue");
    it.x = 320.0f;
    it.y = 200.0f;
    it.size = 27.0f;
    it.rgba[0] = it.rgba[1] = it.rgba[2] = it.rgba[3] = 0x80;
    rd_deferred_text(&it, 0);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
}

/* the text frame at w x h (0 x 0: no output, no present), with the three
 * layers (the deferred text, the overlay and the top layer) or none, a
 * capture armed or not: the render passes the frame's replay recorded, the
 * output into dst (when there is one) */
static bool passRun(uint32_t w, uint32_t h, int layers, const char *png, uint8_t *dst,
                    uint32_t *passes)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.outputWidth = w;
    s.outputHeight = h;
    if (!rd_init(512, 512, &s, NULL)) {
        return false;
    }
    LayerTest m = {.x0 = 100, .y0 = 50, .x1 = 140, .y1 = 71, .c = {255, 0, 0, 0x80}};
    LayerTest t = {.x0 = 200, .y0 = 50, .x1 = 240, .y1 = 71, .c = {0, 0, 255, 0x80}};
    s_textCalls = 0;
    if (layers) {
        rd_set_deferred_text_fn(textRect, NULL);
        rd_set_present_overlay(layerRect, &m);
        rd_set_present_overlay_top(layerRect, &t);
    }
    RdTex tex = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    if (png) {
        CHECK(rd_capture_presented(png), "passes: capture armed");
    }
    RhiStats a, b;
    rhi_get_stats(&a);
    recordTextFrame(tex);
    rhi_get_stats(&b);
    *passes = (uint32_t)(b.renderPasses - a.renderPasses);
    bool ok = true;
    if (w && h) {
        uint32_t ow = 0, oh = 0;
        ok = rd_read_presented(dst, &ow, &oh) && ow == w && oh == h;
        CHECK(ok, "passes: the presented %ux%u output", w, h);
    }
    if (png) {
        char done[1100];
        CHECK(rd_capture_result(done, sizeof(done)) == 1, "passes: the capture written");
    }
    CHECK(!layers || (s_textCalls == 1 && m.calls == 1 && t.calls == 1),
          "passes: one call a layer (text %d, main %d, top %d)", s_textCalls, m.calls, t.calls);
    rd_set_deferred_text_fn(NULL, NULL);
    rd_set_present_overlay(NULL, NULL);
    rd_set_present_overlay_top(NULL, NULL);
    rd_destroy_texture(tex);
    CHECK(rhi_vk_validation_error_count() == 0, "passes: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return ok;
}

static void checkPresentPasses(const char *dir)
{
    const uint32_t w = 960, h = 720;
    const size_t n = (size_t)w * h * 4;
    uint8_t *plain = malloc(n), *over = malloc(n), *cap = malloc(n);
    char png[1100];
    snprintf(png, sizeof(png), "%s/rd_present_passes.png", dir);
    remove(png);
    makeNoiseScene();
    uint32_t none = 0, bare = 0, all = 0, captured = 0;
    const bool ok = plain && over && cap && passRun(0, 0, 0, NULL, NULL, &none) &&
                    passRun(w, h, 0, NULL, plain, &bare) && passRun(w, h, 1, NULL, over, &all) &&
                    passRun(w, h, 1, png, cap, &captured);
    CHECK(ok, "passes: the four runs");
    if (ok) {
        printf("  passes: replay %u, present %u bare, %u with the text and both overlay layers, "
               "%u with a capture\n",
               none, bare - none, all - none, captured - none);
        /* the line doubling's pass and the output's (five with a pass a
         * layer) */
        CHECK(bare == none + 2 && all == none + 2,
              "passes: the present records %u passes bare and %u with the three layers, not 2",
              bare - none, all - none);
        CHECK(captured == all + 1, "passes: a capture adds %u passes, not 1 (the reopened one)",
              captured - all);
        CHECK(memcmp(over, cap, n) == 0, "passes: the capture changed the shown picture");
        RhiRect box;
        rd__present_box(w, h, 4.0f / 3.0f, &box);

        static const struct {
            int32_t x, y;
            uint8_t c[3];
        } at[3] = {{120, 60, {255, 0, 0}}, {220, 60, {0, 0, 255}}, {320, 160, {0, 255, 0}}};

        for (int i = 0; i < 3; i++) {
            const size_t k = ((size_t)(box.y + at[i].y) * w + (size_t)(box.x + at[i].x)) * 4;
            CHECK(memcmp(&over[k], at[i].c, 3) == 0 && memcmp(&plain[k], at[i].c, 3) != 0,
                  "passes: layer %d's rectangle not drawn (%u %u %u)", i, over[k], over[k + 1],
                  over[k + 2]);
        }
    }
    remove(png);
    free(plain);
    free(over);
    free(cap);
}

/* --------------------------------------------------------- effects depth */

/* the depth frame presented at w x h with the effects depth on or off: the
 * present's bytes, its hash, SCENE's depth (sw x sh) and the effects depth
 * (w x h, only with it on) */
typedef struct DepthRun {
    uint64_t present, display, scene;
    uint32_t sw, sh;
    bool depthOk;
} DepthRun;

static float s_presDepth[1280 * 720], s_sceneDepth[512 * 512];

static bool depthRun(uint32_t w, uint32_t h, int on, DepthRun *r)
{
    static uint8_t out[1280 * 720 * 4];
    memset(r, 0, sizeof(*r));
    RdSettings s = originalSettings();
    s.outputWidth = w;
    s.outputHeight = h;
    s.effectsDepth = (uint8_t)on;
    if (!rd_init(512, 512, &s, NULL)) {
        return false;
    }
    makeNoiseScene();
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    s_depthFrame = 1;
    recordRichFrame(t, 1);
    s_depthFrame = 0;
    uint32_t ow = 0, oh = 0;
    uint8_t *p = readTarget(RD_TARGET_DISPLAY, &ow, &oh);
    r->display = p ? fnv(p, (size_t)ow * oh * 4) : 0;
    p = readTarget(RD_TARGET_SCENE, &ow, &oh);
    r->scene = p ? fnv(p, (size_t)ow * oh * 4) : 0;
    r->present = rd__read_present(out, (size_t)w * h * 4, &ow, &oh) && ow == w && oh == h
                     ? fnv(out, (size_t)w * h * 4)
                     : 0;
    CHECK(rd__read_target_depth(rd_target(RD_TARGET_SCENE), s_sceneDepth, sizeof(s_sceneDepth),
                                &r->sw, &r->sh),
          "effects depth: SCENE's depth readback");
    if (on) {
        uint32_t dw = 0, dh = 0;
        r->depthOk = rd__read_present_depth(s_presDepth, sizeof(s_presDepth), &dw, &dh) &&
                     dw == w && dh == h;
    } else {
        r->depthOk = !rd__read_present_depth(s_presDepth, sizeof(s_presDepth), NULL, NULL);
    }
    rd_destroy_texture(t);
    rd_shutdown();
    return true;
}

/* whether key k is in the reachable set of the given blend mode */
static bool reachable(const RdPipeKeyInt *k, bool noDual)
{
    static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
    const bool was = rd_no_dual();
    rd_set_no_dual(noDual);
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_CACHE_MAX);
    rd_set_no_dual(was);
    for (uint32_t i = 0; i < n; i++) {
        if (rd__pipe_key_equal(k, &keys[i])) {
            return true;
        }
    }
    return false;
}

/* setenv for the tests: the mingw C runtime has _putenv_s instead (an
   empty value removes the variable there; NULL removes it on both). */
static void testSetEnv(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

/*   effects depth (RdSettings.effectsDepth): the rich frame with
 *   its scene depth cleared to 0x40000000 and a sprite written at
 *   0xC0000000, presented at 1280 x 720 (4:3 box at x 160, w 960): the
 *   present, DISPLAY and SCENE hash the same with the pass on and off; the
 *   effects depth is 0.0 (far) in both bars and, inside the box, SCENE's depth at
 *   the box mapping (nearest: one of the 3 x 3 texels around it, the near
 *   and far values each where expected); under the validation layer with no
 *   error; the blit's keys for both output formats are reachable in both
 *   blend modes */
static void checkEffectsDepth(void)
{
    const char *prev = getenv("ICO_VK_VALIDATION");
    char saved[16] = "";
    if (prev) {
        snprintf(saved, sizeof(saved), "%s", prev);
    }
    testSetEnv("ICO_VK_VALIDATION", "1");
    const uint32_t W = 1280, H = 720;
    DepthRun off, on;
    /* the pass runs only with an effects program loaded: forced here */
    rd__force_effects_depth(true);
    const bool ran = depthRun(W, H, 0, &off) && depthRun(W, H, 1, &on);
    rd__force_effects_depth(false);
    const uint32_t verrors = rhi_vk_validation_error_count();
    testSetEnv("ICO_VK_VALIDATION", prev ? saved : NULL);
    CHECK(ran, "effects depth: rd_init");
    if (!ran) {
        return;
    }
    printf("  effects depth: present %016llx off, %016llx on; %u validation errors\n",
           (unsigned long long)off.present, (unsigned long long)on.present, verrors);
    CHECK(verrors == 0, "effects depth: %u validation errors", verrors);
    CHECK(off.present != 0 && off.present == on.present && off.display == on.display &&
              off.scene == on.scene,
          "effects depth: the picture is the same with the pass on");
    CHECK(off.depthOk, "effects depth: none without the option");
    CHECK(on.depthOk, "effects depth: the output-size depth readback");
    CHECK(on.sw == 512 && on.sh == 512, "effects depth: SCENE's depth is %ux%u", on.sw, on.sh);
    if (!on.depthOk || on.sw != 512 || on.sh != 512) {
        return;
    }
    RhiRect box;
    rd__present_box(W, H, 4.0f / 3.0f, &box);
    CHECK(box.x == 160 && box.w == 960 && box.y == 0 && box.h == 720, "effects depth: the box");
    const float scale = rd__target_z_scale(rd_target(RD_TARGET_SCENE).id);
    const float nearD = rd__gs_depth(DEPTH_NEAR_Z, scale),
                farD = rd__gs_depth(DEPTH_CLEAR_Z, scale);
    uint32_t barBad = 0, boxBad = 0, nNear = 0, nFar = 0;
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            const float d = s_presDepth[y * W + x];
            if ((int32_t)x < box.x || (int32_t)x >= box.x + (int32_t)box.w) {
                barBad += d != 0.0f; /* far: the depth grows with GS Z */
                continue;
            }
            const int sx = (int)(((float)x + 0.5f - (float)box.x) / (float)box.w * 512.0f);
            const int sy = (int)(((float)y + 0.5f - (float)box.y) / (float)box.h * 512.0f);
            int ok = 0;
            for (int dy = -1; dy <= 1 && !ok; dy++) {
                for (int dx = -1; dx <= 1 && !ok; dx++) {
                    const int tx = sx + dx, ty = sy + dy;
                    ok = tx >= 0 && ty >= 0 && tx < 512 && ty < 512 &&
                         s_sceneDepth[ty * 512 + tx] == d;
                }
            }
            boxBad += !ok;
            nNear += d == nearD;
            nFar += d == farD;
        }
    }
    printf("  effects depth: near %.6g far %.6g; %u near and %u far pixels in the box, %u off "
           "the mapping, %u bar pixels not 0.0\n",
           (double)nearD, (double)farD, nNear, nFar, boxBad, barBad);
    CHECK(barBad == 0, "effects depth: %u bar pixels are not 0.0 (far)", barBad);
    CHECK(boxBad == 0, "effects depth: %u box pixels are not SCENE's depth at the mapping", boxBad);
    CHECK(nearD > farD && nearD < 1.0f, "effects depth: near %g, far %g", (double)nearD,
          (double)farD);
    /* the sprite's rectangle in the box, a pixel in from each edge */
    const uint32_t nx0 = (uint32_t)box.x + (uint32_t)(DEPTH_RECT[0] * 960 / 512) + 2,
                   nx1 = (uint32_t)box.x + (uint32_t)(DEPTH_RECT[2] * 960 / 512) - 2,
                   ny0 = (uint32_t)(DEPTH_RECT[1] * 720 / 512) + 2,
                   ny1 = (uint32_t)(DEPTH_RECT[3] * 720 / 512) - 2;
    CHECK(s_presDepth[((ny0 + ny1) / 2) * W + (nx0 + nx1) / 2] == nearD &&
              s_presDepth[ny0 * W + nx0] == nearD && s_presDepth[ny1 * W + nx1] == nearD,
          "effects depth: the near sprite");
    /* mid-height, clear of the letterbox's bands (whose draws write depth
       of their own: the mapping check above covers them) */
    CHECK(s_presDepth[(H / 2) * W + (uint32_t)box.x + 10] == farD &&
              s_presDepth[(H / 2) * W + (uint32_t)box.x + box.w - 10] == farD,
          "effects depth: the far scene");
    CHECK(nNear > 0 && nFar > 0, "effects depth: the box holds the near and the far values");
    /* the pipelines precreated for it, both output formats, both modes */
    for (int m = 0; m < 2; m++) {
        for (int f = 0; f < 2; f++) {
            const RdPipeKeyInt k =
                rd__present_depth_key(f ? RHI_FMT_BGRA8_UNORM : RHI_FMT_RGBA8_UNORM);
            CHECK(k.depthFmt == RHI_FMT_D32F && k.gs.zwrite == RD_ZWRITE_ON &&
                      k.gs.ztst == RD_ZTST_ALWAYS && k.fs == RD_FS_BLIT_DEPTH,
                  "effects depth: the key");
            CHECK(reachable(&k, m != 0), "effects depth: the %s key is not reachable%s",
                  f ? "BGRA8" : "RGBA8", m ? " (two-pass blend)" : "");
        }
    }
}

/* ------------------------------------------------------------------ main */

/* ------------------------------------------------------ the blank present */

static void blankCallback(const RdOverlayCtx *ctx, void *user)
{
    OvTest *t = user;
    static const uint8_t red[4] = {255, 0, 0, 0x80};
    t->calls++;
    t->box = ctx->box;
    t->boxScale = ctx->boxScale;
    ovRect(ctx->box.x + OV_RX0, ctx->box.y + OV_RY0, ctx->box.x + OV_RX1, ctx->box.y + OV_RY1, red);
}

static void checkBlank(void)
{
    const uint32_t w = 800, h = 600;
    const size_t n = (size_t)w * h * 4;
    uint8_t *out = malloc(n);
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.outputWidth = w;
    s.outputHeight = h;
    if (!out || !rd_init(512, 512, &s, NULL)) {
        CHECK(0, "blank: rd_init or memory");
        free(out);
        return;
    }
    /* a frame first: its picture must not show through */
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    rd_set_present_overlay(NULL, NULL);
    CHECK(rd_present_blank(), "blank: rd_present_blank without an overlay");
    uint32_t ow = 0, oh = 0;
    uint32_t nonzero = 0;
    if (rd_read_presented(out, &ow, &oh) && ow == w && oh == h) {
        for (size_t i = 0; i < n; i++) {
            nonzero += out[i] != 0;
        }
        CHECK(nonzero == 0, "blank: %u bytes of the output are not 0", nonzero);
    } else {
        CHECK(0, "blank: the presented %ux%u output (%ux%u)", w, h, ow, oh);
    }
    memset(&s_ovt, 0, sizeof(s_ovt));
    rd_set_present_overlay(blankCallback, &s_ovt);
    void *user = NULL;
    CHECK(rd_get_present_overlay(&user) == blankCallback && user == &s_ovt,
          "blank: rd_get_present_overlay");
    CHECK(rd_present_blank(), "blank: rd_present_blank with an overlay");
    CHECK(s_ovt.calls == 1, "blank: %d overlay calls", s_ovt.calls);
    RhiRect box;
    rd__present_box(w, h, 4.0f / 3.0f, &box);
    CHECK(s_ovt.box.x == box.x && s_ovt.box.y == box.y && s_ovt.box.w == box.w &&
              s_ovt.box.h == box.h && fabsf(s_ovt.boxScale - (float)box.h / 448.0f) < 1e-6f,
          "blank: the ctx (box %d,%d %ux%u)", s_ovt.box.x, s_ovt.box.y, s_ovt.box.w, s_ovt.box.h);
    if (rd_read_presented(out, &ow, &oh) && ow == w && oh == h) {
        const int32_t rx0 = box.x + OV_RX0, ry0 = box.y + OV_RY0;
        const int32_t rx1 = box.x + OV_RX1, ry1 = box.y + OV_RY1;
        uint32_t badRect = 0, badOutside = 0;
        for (int32_t y = 0; y < (int32_t)h; y++) {
            for (int32_t x = 0; x < (int32_t)w; x++) {
                const uint8_t *o = &out[((size_t)y * w + (size_t)x) * 4];
                if (ovInside(x, y, rx0, ry0, rx1, ry1)) {
                    badRect += !(o[0] == 255 && o[1] == 0 && o[2] == 0);
                } else {
                    badOutside += (o[0] | o[1] | o[2] | o[3]) != 0;
                }
            }
        }
        CHECK(badRect == 0, "blank: %u pixels of the rectangle are not its red", badRect);
        CHECK(badOutside == 0, "blank: %u pixels outside the rectangle are not 0", badOutside);
        printf("  blank %ux%u: cleared, the overlay's rectangle at %d,%d\n", w, h, rx0, ry0);
    } else {
        CHECK(0, "blank: the presented output with the overlay");
    }
    rd_set_present_overlay(NULL, NULL);
    CHECK(rd_get_present_overlay(&user) == NULL && user == NULL, "blank: unregistered");
    rd_destroy_texture(t);
    CHECK(rhi_vk_validation_error_count() == 0, "blank: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    free(out);
}

int main(int argc, char **argv)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    const char *dir = argc > 1 ? argv[1] : ".";
    checkOptions(dir);
    checkBoxes();
    checkCoverage();
    RdSettings st = originalSettings();
    st.outputWidth = st.outputHeight = 0;
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    boot();
    checkWide();
    checkScales();
    rd_shutdown();
    if (!rd_init(512, 512, &st, NULL)) {
        if (failures) {
            printf("rd_present_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_present_test: recording ok; SKIP the pixel checks: no usable device\n");
        return 77;
    }
    s_llvmpipe = strstr(rhi_adapter_name(), "llvmpipe") != NULL;
    printf("rd_present_test: adapter %s\n", rhi_adapter_name());
    rd_shutdown();
    checkOriginal();
    checkScale2();
    checkSceneLimit();
    checkWide169();
    checkPresentInfo();
    checkFullPixel();
    checkFullPixelSquare();
    checkFullPixelSharp(960, 720);
    checkFullPixelSharp(1440, 1080);
    checkFullPixelClamp();
    checkFullPixelBands();
    checkFullPixelCrtOverlay();
    checkMips();
    checkLatticeMips(0);
    checkLatticeMips(1); /* as the game's materials draw it */
    checkOverlay(s_presentOriginal);
    checkOverlayCrt();
    checkOverlayTop();
    checkCapture(dir);
    checkPresentPasses(dir);
    checkEffectsDepth();
    checkBlank();
    if (failures) {
        printf("rd_present_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_present_test: ok\n");
    return 0;
}

#endif /* RD_PRESENT_BASELINE */
