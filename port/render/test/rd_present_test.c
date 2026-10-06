/* rd_present_test.c: the display options (renderer wave 7, R7a;
 * docs/port/RENDER_API.md "Presets and display options", docs/port/DISPLAY.md).
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
 *   scales    rd__ApplyDisplay: Original 1; Enhanced 2x, WxH, the window's
 *             box, the 4K cap, the work buffers' scale, full height
 *   boxes     rd__PresentBox at 4:3 and 16:9 in 4:3, 16:9 and 5:4 outputs
 *   coverage  rdtex_KeepAlphaCoverage keeps an alpha-tested texture's share
 * On a Vulkan device (77 without one):
 *   original  rd_pixel_test's rich frame (scene, additive quad, fade,
 *             letterbox, brightness, reduction) in the Original preset:
 *             DISPLAY, SCENE and the 960 x 720 present hash to the values
 *             the renderer before R7a produced (llvmpipe only: another
 *             driver's bilinear may differ; logged elsewhere), and the
 *             Enhanced preset with every option neutral gives the same bytes
 *   scale2    the same frame at 2x: every 2 x 2 block of SCENE uniform and
 *             equal to the 1x pixel (nearest-only content, 0 LSB), DISPLAY's
 *             block averages within 2 LSB of 1x (bilinear reduction)
 *   wide169   16:9 at 1x: a UI sprite lands in the centred 4:3 box, the
 *             letterbox bars and a full-width fill stretch over the whole
 *             width (58 lines), the present fills a 16:9 output; W3: a UI
 *             band one pixel short at each side (the menus' bars) stretches,
 *             one two pixels short stays boxed
 *   mips      the trilinear filter: a mipmapped game texture, minified,
 *             samples its average
 *   overlay   package OV, the presentation overlay (rd.h
 *             rd_SetPresentOverlay): the rich frame presented at 960 x 720
 *             and 1920 x 1080 with a callback drawing two rectangles (one
 *             in the box, one in the pillarbox) and "H" through port/ui's
 *             font in overlay mode: the rectangles exactly at their output
 *             pixels, the glyph's stems inside its quad (texel for pixel),
 *             nothing else changed against the present without the
 *             overlay; with the mirror on the all-UI box shows the same
 *             picture (within 1 LSB) and the overlay is not flipped;
 *             unregistered, the present hashes as before
 *   overlay under the CRT filter (package CRT2): Trinitron at 1536 x
 *             1152 (k 3), the glow and curvature off: the callback sees the
 *             filter's grid (box 0, 0, 512 x 512, scale 512 / 448), and its
 *             red rectangle comes out of the filter as phosphors: in the
 *             rectangle each block's R column red, its G column dark
 *   capture   package PHOTO, rd_CapturePresented: the rich frame at 800 x
 *             600 with the overlay's rectangles registered, CRT off and
 *             Consumer TV: the PNG is 800 x 600 RGB; CRT off it holds
 *             exactly the present without the overlay, under the CRT filter
 *             (package CRT2) exactly the present with it (the overlay is
 *             inside the filtered picture); rd_CaptureResult reports it once
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
/* port/ui's font (package OV: the overlay check draws a glyph) */
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
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, space, 1, 0);
}

static void opaque2D(void)
{
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_PABE(0);
    rd_FBA(0);
}

static uint8_t *readTarget(RdTargetId id, uint32_t *w, uint32_t *h)
{
    static uint8_t buf[2048 * 2048 * 4];
    if (!rd__ReadTarget(rd_Target(id), buf, sizeof(buf), w, h)) {
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

static void drawScene(RdTex t)
{
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), clr, 1, 0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(RD_SPACE_UI, 0, 0, 512 * 16, 512 * 16, grey, 8, 8, 512 * 16 + 8, 512 * 16 + 8);
}

/* rd_pixel_test.c recordRichFrame, the frame of its dump */
static void recordRichFrame(RdTex t, int reduce)
{
    rd_BeginFrame();
    drawScene(t);
    rd_SelectList(2);
    static const uint8_t add[4] = {60, 30, 90, 0xFF};
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_Blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 1);
    rd_TextureOff();
    sprite(RD_SPACE_UI, 100 * 16, 100 * 16, 300 * 16, 200 * 16, add, 0, 0, 0, 0);
    rd_SelectList(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 0x40;
    pp.rgba[0] = 0x20;
    rd_Post(RD_POST_FADE, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x60;
    rd_Post(RD_POST_LETTERBOX, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 5;
    rd_Post(RD_POST_BRIGHTNESS, &pp);
    if (reduce) {
        rd_SelectList(12);
        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = 128;
        pp.rgba[1] = 120;
        pp.rgba[2] = 110;
        rd_Post(RD_POST_REDUCTION, &pp);
    }
    rd_EndFrame(0);
}

typedef struct FrameHashes {
    uint64_t display, scene, present;
} FrameHashes;

/* rd_Init with s, the rich frame, the three hashes; false without a device */
static bool richFrame(const RdSettings *s, FrameHashes *h)
{
    if (!rd_Init(512, 512, s, NULL)) {
        return false;
    }
    RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t w = 0, hh = 0;
    uint8_t *p = readTarget(RD_TARGET_DISPLAY, &w, &hh);
    h->display = p ? fnv(p, (size_t)w * hh * 4) : 0;
    p = readTarget(RD_TARGET_SCENE, &w, &hh);
    h->scene = p ? fnv(p, (size_t)w * hh * 4) : 0;
    static uint8_t out[960 * 720 * 4];
    h->present = rd__ReadPresent(out, sizeof(out), &w, &hh) ? fnv(out, sizeof(out)) : 0;
    rd_DestroyTexture(t);
    rd_Shutdown();
    return true;
}

static RdSettings originalSettings(void)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = 960;
    s.outputHeight = 720;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    return s;
}

/* What the renderer before R7a produced for the rich frame on llvmpipe:
 * rd_replay_tool (pre-R7a build) on rd_pixel_test's dump of this frame,
 * DISPLAY, SCENE and --present 960x720 (RENDER_API.md "Presets and display
 * options").  R-POST moved the reduction onto the GS sprite model (the GS
 * integer bilinear instead of the hardware's, at most 1 LSB apart;
 * rd_pixel_test checks it exactly), so DISPLAY and the present are that
 * build's values for those two (0xde837c63a5e63c88, 0xbc970416f934e0c1
 * before) and SCENE is still the pre-R7a value. */
#define GOLD_DISPLAY 0x212c1c733f4ba8a1ull
#define GOLD_SCENE 0x8da6e2ca4577cdacull
#define GOLD_PRESENT 0xedb088b74a237351ull
#ifdef RD_PRESENT_BASELINE

/* built against the pre-R7a renderer: prints the hashes of this file's own
 * frame (they must equal the constants above) */
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
    CHECK(o.preset == ICO_VIDEO_ORIGINAL && o.aspect == ICO_ASPECT_4_3 && o.resW == 0 &&
              o.resH == 0 && o.resScale == 0 && o.fullscreen == 0 && o.vsync == 1 &&
              o.filter == ICO_FILTER_ORIGINAL && o.fullHeight == 0,
          "options: defaults");
    CHECK(ico_video_wide_x() == 1.0f && ico_video_aspect() == 4.0f / 3.0f, "options: default 4:3");

    /* every key */
    writeFile(toml, "version = 1\n[video]\npreset = \"enhanced\"\nresolution = \"1920x1440\"\n"
                    "aspect = \"16:9\"\nfullscreen = true\nvsync = false\n"
                    "texture_filter = \"anisotropic\"\nfull_height = true\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.preset == ICO_VIDEO_ENHANCED && o.resW == 1920 && o.resH == 1440 &&
              o.aspect == ICO_ASPECT_16_9 && o.fullscreen == 1 && o.vsync == 0 &&
              o.filter == ICO_FILTER_ANISOTROPIC && o.fullHeight == 1,
          "options: every key read");
    CHECK(near(ico_video_wide_x(), 4.0f / 3.0f), "options: 16:9 widens by 4/3");

    /* Original ignores the aspect */
    IcoVideoOptions p = o;
    p.preset = ICO_VIDEO_ORIGINAL;
    const unsigned serial = ico_video_serial();
    ico_video_set(&p);
    CHECK(ico_video_serial() != serial, "options: set bumps the serial");
    CHECK(ico_video_wide_x() == 1.0f, "options: Original is 4:3 whatever the aspect");
    p.preset = ICO_VIDEO_ENHANCED;
    p.aspect = ICO_ASPECT_16_10;
    ico_video_set(&p);
    CHECK(near(ico_video_wide_x(), 1.2f), "options: 16:10 widens by 1.2");
    p.aspect = ICO_ASPECT_AUTO;
    ico_video_set(&p);
    ico_video_set_window(0, 0);
    CHECK(ico_video_wide_x() == 1.0f, "options: auto without a window is 4:3");
    ico_video_set_window(1920, 1080);
    CHECK(near(ico_video_aspect(), 16.0f / 9.0f), "options: auto in 1920x1080");
    ico_video_set_window(3440, 1440);
    CHECK(near(ico_video_aspect(), 16.0f / 9.0f), "options: auto clamps at 16:9");
    ico_video_set_window(1280, 1024);
    CHECK(ico_video_aspect() == 4.0f / 3.0f, "options: auto clamps at 4:3");
    ico_video_set_window(0, 0);

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
              ico_video_parse_resolution("10x", &q) != 0 && q.resW == 3840,
          "options: bad resolutions rejected, value kept");
    int a = -1;
    CHECK(ico_video_parse_aspect("16:10", &a) == 0 && a == ICO_ASPECT_16_10 &&
              ico_video_parse_aspect("21:9", &a) != 0,
          "options: aspect parser");
    char buf[32];
    CHECK(strcmp(ico_video_resolution_name(&q, buf, sizeof(buf)), "3840x2160") == 0,
          "options: resolution name");

    /* save: the [video] keys land in config.toml */
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
    CHECK(q.resScale == 2 && q.aspect == ICO_ASPECT_16_9, "options: read back");

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
    const RdFrame *f = rd__RecFrame();
    if (f && f->hasCamera) {
        m->cam = f->camera;
    }
}

static void checkWide(void)
{
    IcoVideoOptions o;
    ico_video_defaults(&o);
    ico_video_set(&o);
    rd_BeginFrame();
    Mats a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    cameraMats(&a);
    o.preset = ICO_VIDEO_ENHANCED;
    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    cameraMats(&b);
    rd_DiscardFrame();
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
    rd__FillCameraCB(&n, &a.cam);
    g_rd.wideX = 0.75f;
    rd__FillCameraCB(&w, &a.cam);
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
    rd__ApplyDisplay();
}

static void checkScales(void)
{
    const RdSettings saved = g_rd.settings;
    RdSettings s = originalSettings();
    s.sceneScale = 3.0f; /* ignored in Original */
    s.aspect = 16.0f / 9.0f;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 1.0f && g_rd.sceneSy == 1.0f && g_rd.workScale == 1.0f &&
              g_rd.wideX == 1.0f && g_rd.outAspect == 4.0f / 3.0f && !g_rd.fullHeight &&
              !g_rd.filterUpgrade,
          "scales: Original is 1 whatever the options");
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
    CHECK(near(g_rd.sceneSx * 512.0f, 3840.0f) && near(g_rd.sceneSy * 512.0f, 2160.0f),
          "scales: capped at 4K");
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
    rd__TargetScaleOf(&t, RD_TARGET_DISPLAY);
    CHECK(t.tw == 512 && t.th == 512 && t.sy == 2.0f && !t.wide,
          "scales: full height doubles DISPLAY's texture (%ux%u)", t.tw, t.th);
    s.fullHeightScene = 0;
    s.sceneScale = 2.0f;
    applyWith(&s);
    t.w = t.h = 512;
    rd__TargetScaleOf(&t, -1);
    CHECK(t.tw == 1024 && t.th == 1024 && t.wide, "scales: a scene-sized temporary target");
    t.w = t.h = 256;
    rd__TargetScaleOf(&t, -1);
    CHECK(t.tw == 256 && t.sx == 1.0f && !t.wide, "scales: other temporary targets stay");
    t.w = t.h = 128;
    rd__TargetScaleOf(&t, RD_TARGET_FEED128);
    CHECK(t.tw == 256 && t.th == 256 && !t.wide, "scales: the work buffers by the work scale");
    g_rd.settings = saved;
    rd__ApplyDisplay();
}

static void checkBoxes(void)
{
    RhiRect b;
    rd__PresentBox(1920, 1080, 4.0f / 3.0f, &b);
    CHECK(b.x == 240 && b.y == 0 && b.w == 1440 && b.h == 1080, "boxes: 4:3 in 16:9");
    rd__PresentBox(1920, 1080, 16.0f / 9.0f, &b);
    CHECK(b.x == 0 && b.y == 0 && b.w == 1920 && b.h == 1080, "boxes: 16:9 in 16:9");
    rd__PresentBox(1280, 1024, 16.0f / 9.0f, &b);
    CHECK(b.x == 0 && b.y == 152 && b.w == 1280 && b.h == 720, "boxes: 16:9 in 5:4 letterboxed");
    rd__PresentBox(960, 720, 16.0f / 10.0f, &b);
    CHECK(b.w == 960 && b.h == 600 && b.y == 60, "boxes: 16:10 in 4:3");
    rd__PresentBox(960, 720, 4.0f / 3.0f, &b);
    CHECK(b.x == 0 && b.y == 0 && b.w == 960 && b.h == 720, "boxes: 4:3 in 4:3");
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
    const uint32_t n = rdtex_BuildMipChain(img, 16, 16, chain);
    CHECK(n == 4, "coverage: 4 levels below 16x16");
    /* level 1 (8x8) box filtered: 0x20 at the dots, under the test */
    CHECK(chain[3] == 0x20, "coverage: the plain box filter thins it out");
    rdtex_KeepAlphaCoverage(img, 16, 16, chain, n, 64);
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
    rdtex_BuildMipChain(img, 16, 16, chain);
    const uint64_t h0 = fnv(chain, sizeof(chain));
    rdtex_KeepAlphaCoverage(img, 16, 16, chain, n, 64);
    CHECK(fnv(chain, sizeof(chain)) == h0, "coverage: an opaque texture is untouched");
}

/* -------------------------------------------------------- device checks */

static bool s_llvmpipe;

static uint64_t s_presentOriginal; /* checkOriginal's present hash (package OV) */

static void checkOriginal(void)
{
    makeNoiseScene();
    RdSettings s = originalSettings();
    FrameHashes h;
    if (!richFrame(&s, &h)) {
        CHECK(0, "original: rd_Init");
        return;
    }
    s_presentOriginal = h.present;
    printf("  original: display %016llx scene %016llx present %016llx\n",
           (unsigned long long)h.display, (unsigned long long)h.scene,
           (unsigned long long)h.present);
    if (s_llvmpipe) {
        CHECK(h.display == GOLD_DISPLAY && h.scene == GOLD_SCENE && h.present == GOLD_PRESENT,
              "original: the bytes of the renderer before R7a (with R-POST's reduction)");
    } else {
        printf("  original: not llvmpipe, the recorded hashes are not compared\n");
    }
    /* Enhanced with every option neutral: the same bytes */
    RdSettings e = s;
    e.preset = RD_PRESET_ENHANCED;
    e.sceneScale = 1.0f;
    FrameHashes g;
    if (richFrame(&e, &g)) {
        CHECK(g.display == h.display && g.scene == h.scene && g.present == h.present,
              "original: Enhanced at 1x, 4:3, no filter, half height is the Original frame");
    }
}

static uint8_t s_scene1[512 * 512 * 4], s_disp1[512 * 256 * 4];

static void checkScale2(void)
{
    makeSmoothScene();
    /* 1x */
    RdSettings s = originalSettings();
    if (!rd_Init(512, 512, &s, NULL)) {
        CHECK(0, "scale2: rd_Init");
        return;
    }
    RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
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
    rd_DestroyTexture(t);
    rd_Shutdown();

    /* 2x */
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 2.0f;
    if (!rd_Init(512, 512, &s, NULL)) {
        return;
    }
    t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
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
    CHECK(rhi_vk_ValidationErrorCount() == 0, "scale2: %u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_DestroyTexture(t);
    rd_Shutdown();
}

static void checkWide169(void)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.aspect = 16.0f / 9.0f;
    s.sceneScale = 1.0f;
    s.outputWidth = 960;
    s.outputHeight = 540;
    if (!rd_Init(512, 512, &s, NULL)) {
        return;
    }
    static const uint8_t black[4] = {0, 0, 0, 0x80}, white[4] = {255, 255, 255, 0x80},
                         red[4] = {255, 0, 0, 0x80};
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), black, 1, 0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_TextureOff();
    /* a UI item: GS 128..384 x 100..200 */
    sprite(RD_SPACE_UI, 128 * 16, 100 * 16, 384 * 16, 200 * 16, white, 0, 0, 0, 0);
    /* a full-width fill the game tags WORLD: rows 300..310 */
    sprite(RD_SPACE_WORLD, 0, 300 * 16, 512 * 16, 310 * 16, red, 0, 0, 0, 0);
    /* W3: the pause menu's band as the layout draws it, UI space, GS x
     * 0.25 .. 511.44 (pixels 1..511): rows 320..330 */
    sprite(RD_SPACE_UI, 4, 320 * 16, 511 * 16 + 7, 330 * 16, red, 0, 0, 0, 0);
    /* a UI band two pixels short at the left (pixels 2..511): rows 340..350,
     * stays in the 4:3 box */
    sprite(RD_SPACE_UI, 2 * 16, 340 * 16, 512 * 16, 350 * 16, red, 0, 0, 0, 0);
    /* the letterbox at full strength */
    rd_SelectList(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x80;
    rd_Post(RD_POST_LETTERBOX, &pp);
    rd_SelectList(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(0);
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
    if (rd__ReadPresent(out, sizeof(out), &ow, &oh) && ow == 960 && oh == 540) {
        /* 12 output pixels in: past the reduction's 2-pixel border crop */
        const uint8_t *q = &out[(305 * 540 / 512 * 960 + 12) * 4];
        CHECK(q[0] > 200, "wide169: the fill reaches the output's left edge (%u)", q[0]);
    } else {
        CHECK(0, "wide169: present readback");
    }
    CHECK(rhi_vk_ValidationErrorCount() == 0, "wide169: %u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
}

static void checkMips(void)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f;
    s.filterUpgrade = RD_FILTER_UPGRADE_TRILINEAR;
    if (!rd_Init(512, 512, &s, NULL)) {
        return;
    }
    if (!rhi_Limits()->textureMips) {
        printf("  mips: the backend has no mipmapped textures; skipped\n");
        rd_Shutdown();
        return;
    }
    /* 64 x 64 one-texel checker, white and black */
    static uint8_t img[64 * 64 * 4];
    for (int i = 0; i < 64 * 64; i++) {
        const int c = ((i % 64) + (i / 64)) & 1 ? 255 : 0;
        img[i * 4 + 0] = img[i * 4 + 1] = img[i * 4 + 2] = (uint8_t)c;
        img[i * 4 + 3] = 0x80;
    }
    RdTex t = rd_CreateTexture(64, 64, img, RD_TEXA_80_80, "checker");
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80}, black[4] = {0, 0, 0, 0x80};
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), black, 1, 0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_Sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    /* minified 8:1 into 8 x 8 pixels */
    sprite(RD_SPACE_WORLD, 16 * 16, 16 * 16, 24 * 16, 24 * 16, grey, 0, 0, 64 * 16, 64 * 16);
    rd_EndFrame(0);
    const RdTexRec *tr = rd__TexRec(t.id);
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
    rd_DestroyTexture(t);
    CHECK(rhi_vk_ValidationErrorCount() == 0, "mips: %u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
}

/* ---------------------------------------------- the overlay (package OV) */

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
    rd_OverlayPrims(RD_PRIM_SPRITES, v, 2, (RdTex){0}, RD_BLEND_LERP_AS);
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
    rd_OverlayPrims(type, v, n, tex, blend);
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
    ui__SetOverlaySink(ovSink);
    ui_BeginOverlay(ctx);
    ui_DrawText(320.0f, 300.0f, 40.0f, white, "H", UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    ui_EndOverlay();
    ui__SetOverlaySink(NULL);
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
    if (!rd_Init(512, 512, &s, NULL)) {
        return false;
    }
    memset(&s_ovt, 0, sizeof(s_ovt));
    s_ovt.rects = rects;
    if (rects) {
        rd_SetPresentOverlay(ovCallback, &s_ovt);
    }
    RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd_ReadPresented(dst, &ow, &oh) && ow == w && oh == h;
    CHECK(ok, "overlay: the presented %ux%u output", w, h);
    CHECK(!rects || s_ovt.calls == 1, "overlay: one callback a present (%d)", s_ovt.calls);
    rd_SetPresentOverlay(NULL, NULL);
    ui_FontShutdown();
    rd_DestroyTexture(t);
    CHECK(rhi_vk_ValidationErrorCount() == 0, "overlay: %u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
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
    rd__PresentBox(w, h, 4.0f / 3.0f, &box);
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
    CHECK(t.hquads == 1 && ui_FontGlyph('H', px, &g) && t.hx1 - t.hx0 == g.w &&
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
         * cancel, and since R-POST the reduction samples the mirror image
         * too, so the box shows the same picture, exactly where the box's
         * horizontal scale gives mirrored bilinear weights equal ones
         * (RENDER_API.md "Mirror mode"), else within 1 LSB */
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
    /* registered and unregistered again: the Original present as before */
    rd_SetPresentOverlay(ovCallback, &s_ovt);
    rd_SetPresentOverlay(NULL, NULL);
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

/* package CRT2: the overlay drawn into the CRT filter's grid */
static void checkOverlayCrt(void)
{
    const uint32_t w = 1536, h = 1152;
    uint8_t *px = malloc((size_t)w * h * 4);
    makeNoiseScene();
    RdSettings s = originalSettings();
    s.outputWidth = w;
    s.outputHeight = h;
    rd_CrtSettings(&s, RD_CRT_TRINITRON, 1.0f);
    s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
    if (!px || !rd_Init(512, 512, &s, NULL)) {
        free(px);
        return;
    }
    memset(&s_ovt, 0, sizeof(s_ovt));
    s_ovt.rects = 1;
    rd_SetPresentOverlay(ovCallback, &s_ovt);
    RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t, 1);
    uint32_t ow = 0, oh = 0;
    const bool ok = rd_ReadPresented(px, &ow, &oh) && ow == w && oh == h;
    rd_SetPresentOverlay(NULL, NULL);
    ui_FontShutdown();
    rd_DestroyTexture(t);
    CHECK(rhi_vk_ValidationErrorCount() == 0, "overlay crt: %u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
    CHECK(ok && s_ovt.calls == 1, "overlay crt: one callback, the present read");
    CHECK(s_ovt.box.x == 0 && s_ovt.box.y == 0 && s_ovt.box.w == 512 && s_ovt.box.h == 512 &&
              fabsf(s_ovt.boxScale - 512.0f / 448.0f) < 1e-6f,
          "overlay crt: the callback's box is the grid (%d, %d, %u x %u, scale %.3f)", s_ovt.box.x,
          s_ovt.box.y, s_ovt.box.w, s_ovt.box.h, s_ovt.boxScale);
    if (ok) {
        /* the rectangle: grid x 100..140, frame lines 50..71 (DISPLAY lines
         * 25..35): output columns 300..420, rows about 112..160 */
        const uint32_t y = 136;
        int phosphors = 1;
        for (uint32_t gx = 104; gx < 136; gx++) {
            const uint8_t *r = &px[((size_t)y * w + gx * 3) * 4];
            const uint8_t *g = &px[((size_t)y * w + gx * 3 + 1) * 4];
            phosphors &= r[0] > 100 && r[1] < 30 && r[2] < 30 && g[0] < 30 && g[1] < 30;
        }
        const uint8_t *r = &px[((size_t)y * w + 360) * 4];
        printf("  overlay crt: red rectangle at (360, %u): R column %u %u %u, G column %u %u %u\n",
               y, r[0], r[1], r[2], r[4], r[5], r[6]);
        CHECK(phosphors, "overlay crt: the overlay's red is the R phosphors of its blocks");
    }
    free(px);
}

/* ------------------------------------------------- capture (package PHOTO) */

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
        rd_CrtSettings(&s, RD_CRT_CONSUMER, 1.0f);
    }
    bool ok = plain && over;
    /* the present without the overlay */
    if (ok && rd_Init(512, 512, &s, NULL)) {
        RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
        recordRichFrame(t, 1);
        uint32_t ow = 0, oh = 0;
        ok = rd_ReadPresented(plain, &ow, &oh) && ow == w && oh == h;
        rd_DestroyTexture(t);
        rd_Shutdown();
    } else {
        ok = false;
    }
    /* again with the overlay's rectangles and a capture armed */
    if (ok && rd_Init(512, 512, &s, NULL)) {
        memset(&s_ovt, 0, sizeof(s_ovt));
        s_ovt.rects = 1;
        rd_SetPresentOverlay(ovCallback, &s_ovt);
        RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
        char done[1100];
        CHECK(rd_CaptureResult(done, sizeof(done)) == 0, "capture: nothing before");
        CHECK(rd_CapturePresented(png), "capture: armed");
        recordRichFrame(t, 1);
        CHECK(rd_CaptureResult(done, sizeof(done)) == 1 && strcmp(done, png) == 0,
              "capture: written (%s)", png);
        CHECK(rd_CaptureResult(done, sizeof(done)) == 0, "capture: reported once");
        uint32_t ow = 0, oh = 0;
        ok = rd_ReadPresented(over, &ow, &oh);
        rd_SetPresentOverlay(NULL, NULL);
        ui_FontShutdown();
        rd_DestroyTexture(t);
        CHECK(rhi_vk_ValidationErrorCount() == 0, "capture: %u validation errors",
              rhi_vk_ValidationErrorCount());
        rd_Shutdown();
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
        if (crt) {
            /* package CRT2: the overlay is inside the filtered picture */
            CHECK(diffOver == 0 && diffPlain > 0,
                  "capture (crt): the present with the overlay (%zu pixels differ; %zu from the "
                  "one without it)",
                  diffOver, diffPlain);
        } else {
            CHECK(diffPlain == 0 && diffOver > 0,
                  "capture (crt %d): the present without the overlay (%zu pixels differ; %zu "
                  "from the one with it)",
                  crt, diffPlain, diffOver);
        }
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

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    checkOptions(dir);
    checkBoxes();
    checkCoverage();
    RdSettings st = originalSettings();
    st.outputWidth = st.outputHeight = 0;
    if (!rd__InitRecordOnly(512, 512)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    boot();
    checkWide();
    checkScales();
    rd_Shutdown();
    if (!rd_Init(512, 512, &st, NULL)) {
        if (failures) {
            printf("rd_present_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_present_test: recording ok; SKIP the pixel checks: no usable device\n");
        return 77;
    }
    s_llvmpipe = strstr(rhi_AdapterName(), "llvmpipe") != NULL;
    printf("rd_present_test: adapter %s\n", rhi_AdapterName());
    rd_Shutdown();
    checkOriginal();
    checkScale2();
    checkWide169();
    checkMips();
    checkOverlay(s_presentOriginal);
    checkOverlayCrt();
    checkCapture(dir);
    if (failures) {
        printf("rd_present_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_present_test: ok\n");
    return 0;
}

#endif /* RD_PRESENT_BASELINE */
