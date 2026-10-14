/* rd_present_test.c: the display options.
 *
 * GsBase.c, GifPacket.c, DisplayList.c and DmaPacket.c compiled as the
 * window build compiles them (as rd_gsbase_test does), the display options
 * module (port/game/video_options.c) with its config.
 *
 * Without a device:
 *   options   [video] keys from a config.toml, the defaults, the parsers,
 *             "auto", run-time set, save
 *   wide      gsb_SetVSMatrix / gsb_MakeCommonMatrix with aspect 16:9 (the
 *             native frame 688 wide): +0x80, +0xC0 and +0x100 byte-identical
 *             to 4:3 (so IsPointIsInScreen and the screen tests are
 *             unchanged), +0x240 x scale the native one (688 / 512 narrower)
 *             and +0x280 following it, the frame's camera the 4:3 one but
 *             for its aspect; the renderer's g_proj / g_viewProj the
 *             camera's at 16:9 too (byte-identical to 4:3: nothing
 *             compressed), g_clip.w the camera's aspect; the present's
 *             exact width (512 on a 512 frame, 384 aspect held to the
 *             frame's width on a wider one); shader_consts.h's frame
 *             constants are ico_frame.h's
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
 *   wide169   16:9 at 1x on the native frame (688 x 512, square texels): a
 *             UI sprite of the 4:3 columns at its GS X in the centre, a
 *             world sprite there not squeezed or stretched, a
 *             full-screen-tagged one the same, a frame-wide fill and the
 *             letterbox bars over all 688 columns; the present fills a
 *             16:9 output with the frame's exact width, the 2.67 columns a
 *             side past it cropped (the reduction's border with them)
 *   exact window  Full pixel under the Enhanced window resolution (1920 x
 *             1080, 2560 x 1440, 3840 x 2160 at 16:9, 3840 x 720 at 48:9,
 *             an odd 2341 x 1080): SCENE and DISPLAY the box's exact size,
 *             the present DISPLAY byte for byte, the frame's 4:3 columns
 *             the 4:3 picture at the box's height byte for byte (SCENE;
 *             DISPLAY but the reduction's edge columns); Full pixel off (the
 *             game's border mask) on a cropped frame SCENE and DISPLAY the
 *             on run's byte for byte and the present the on present under
 *             the mask, at 48:9 (the frame its exact width) the GS's own
 *             border, no mask; the present pinned with it on (hashes), off
 *             at every pinned cropped case the pinned picture under the
 *             mask, and off at 4:3 and 48:9 pinned to v0.7.4's hashes
 *   border    Full pixel off at 448 and 512 lines on a cropped frame (16:9
 *             1080p, 1440p and 4K, odd sizes): the mask's bands 2 GS
 *             columns at the picture's scale and the reduction's rows,
 *             within a pixel, black; the on picture inside them; DISPLAY
 *             the on one's.  At 4:3 and 48:9 (the frame its exact width):
 *             no mask, the GS's own border as v0.7.4 had it
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
 *   aa off    without anti-aliasing, 4x into 640 x 480 (3.2 texels a pixel)
 *             is DISPLAY box-reduced byte for byte (cpuReduce), one pass
 *             more than at 1.28 a pixel; 1x into 320 x 240 keeps the plain
 *             box blit (the passes of 1x into 960 x 720)
 *   full height 1x  the reduction (DISPLAY sy 2, one SCENE row a DISPLAY
 *             row) takes no above-1x kernel, byte for byte the frame with
 *             it flagged (rd__set_a1x_gs_keep off), which is counted
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
    /* the RdBlendExact the frame was drawn with (rd_dst_read.c): the
     * recorded hashes below are the fixed-function blend's (0) */
    int exact;
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
    h->exact = (int)rd_blend_exact();
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
 * SCENE is still the value from before the display options.  The GS
 * bilinear then became PCSX2's two-step lerp (common.hlsli gs_bilerp; the
 * one weighted sum gave 0x212c1c733f4ba8a1, 0xedb088b74a237351). */
#define GOLD_DISPLAY 0xe6242f6ac3b7ad9cull
#define GOLD_SCENE 0x8da6e2ca4577cdacull
#define GOLD_PRESENT 0x9089548e05b4f69eull
/* The same frame under the exact GS blend's attachment read (rd_dst_read.c;
 * llvmpipe, LLVM 19.1.7, Mesa 25.0.7): the fade, the letterbox and the
 * brightness are LERPs into DISPLAY computed in the shader.  The values
 * above stay the fixed-function blend's (rd_present_fixed). */
#define GOLD_DISPLAY_EXACT 0x7174f748fabd9253ull
#define GOLD_SCENE_EXACT 0x0f982728fcb9118cull
#define GOLD_PRESENT_EXACT 0xea894ace64f1d717ull
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

/* the native frame's width for the options in force (ico_video_frame) */
static int frameW(void)
{
    IcoFrame f;
    ico_video_frame(&f, 448);
    return f.w;
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
    CHECK(frameW() == 512 && ico_video_aspect() == 4.0f / 3.0f, "options: default 4:3");

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
    CHECK(frameW() == 688, "options: 16:9 is 688 wide");

    /* window_mode, and the fullscreen bool when it is absent */
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
    CHECK(frameW() == 512, "options: Original is 4:3");
    /* the aspect applies whatever the preset */
    p.aspect = ICO_ASPECT_16_9;
    ico_video_set(&p);
    ico_video_get(&o);
    CHECK(frameW() == 688 && ico_video_preset(&o) == ICO_VIDEO_CUSTOM,
          "options: 16:9 alone is 688 wide and reads Custom");
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
    CHECK(frameW() == 624, "options: 16:10 is 624 wide");
    p.aspect = ICO_ASPECT_21_9;
    ico_video_set(&p);
    CHECK(near(ico_video_aspect(), 64.0f / 27.0f) && frameW() == 912, "options: 21:9 is 912 wide");
    p.aspect = ICO_ASPECT_32_9;
    ico_video_set(&p);
    CHECK(near(ico_video_aspect(), 32.0f / 9.0f) && frameW() == 1376, "options: 32:9 is 1376 wide");
    p.aspect = ICO_ASPECT_48_9;
    ico_video_set(&p);
    CHECK(near(ico_video_aspect(), 48.0f / 9.0f) && frameW() == 2048, "options: 48:9 is 2048 wide");
    p.aspect = ICO_ASPECT_AUTO;
    ico_video_set(&p);
    ico_video_set_window(0, 0);
    CHECK(frameW() == 512, "options: auto without a window is 4:3");
    ico_video_set_window(1920, 1080);
    CHECK(near(ico_video_aspect(), 16.0f / 9.0f), "options: auto in 1920x1080");
    ico_video_set_window(3440, 1440);
    CHECK(near(ico_video_aspect(), 3440.0f / 1440.0f), "options: auto passes 3440x1440 through");
    ico_video_set_window(5760, 1080);
    CHECK(near(ico_video_aspect(), 48.0f / 9.0f), "options: auto passes 5760x1080 (48:9) through");
    CHECK(frameW() == 2048, "options: auto at 48:9 is 2048 wide");
    ico_video_set_window(9600, 1080);
    CHECK(ico_video_aspect() == ICO_ASPECT_MAX && frameW() == ICO_FRAME_W_MAX,
          "options: auto clamps at the maximum (20:3), 9600x1080");
    /* the options' maximum is the native frame's */
    CHECK(ICO_FRAME_ASPECT_MAX == ICO_ASPECT_MAX, "options: ICO_ASPECT_MAX is the frame's");
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
              o.aspect == ICO_ASPECT_4_3 && frameW() == 512,
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
              frameW() == 512,
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
              o.fullHeight == 1 && frameW() == 624,
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
    ico_video_frame(&gsFrame, ScreenHeight); /* what the flip latches */
    cameraMats(&a);
    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    ico_video_frame(&gsFrame, ScreenHeight);
    CHECK(gsFrame.w == 688, "wide: the native frame 688 wide (%d)", gsFrame.w);
    cameraMats(&b);
    rd_discard_frame();
    const float k = 688.0f / 512.0f;
    CHECK(memcmp(a.m80, b.m80, 64) == 0, "wide: +0x80 (the view) byte-identical");
    CHECK(memcmp(a.mC0, b.mC0, 64) == 0, "wide: +0xC0 (the 4:3 screen matrix) byte-identical");
    CHECK(memcmp(a.m100, b.m100, 64) == 0, "wide: +0x100 (screen x view) byte-identical");
    CHECK(near(b.m240[0], a.m240[0] / k) && memcmp(&a.m240[1], &b.m240[1], 60) == 0,
          "wide: +0x240 x scale 688 / 512 narrower, the rest identical (%g -> %g)", a.m240[0],
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
    RdCamera bAs43 = b.cam;
    bAs43.aspect = 4.0f / 3.0f;
    CHECK(memcmp(&a.cam, &bAs43, sizeof(a.cam)) == 0 && a.cam.aspect == 4.0f / 3.0f &&
              b.cam.aspect == 16.0f / 9.0f,
          "wide: the frame camera (proj = +0xC0) unchanged but for the aspect (%g, %g)",
          (double)a.cam.aspect, (double)b.cam.aspect);
    ico_video_defaults(&o);
    ico_video_set(&o);
    ico_video_frame(&gsFrame, ScreenHeight);

    /* the renderer's projection: the camera's own at every aspect (the
     * native frame: nothing compressed), so a world point's GS X is the
     * same at 4:3 and 16:9 */
    IcoFrameCB n, w;
    rd__fill_camera_cb(&n, &a.cam);
    rd__fill_camera_cb(&w, &b.cam);
    CHECK(memcmp(n.proj, a.cam.proj, 64) == 0 && memcmp(w.proj, b.cam.proj, 64) == 0,
          "wide: g_proj = the camera's proj at 4:3 and 16:9");
    CHECK(memcmp(n.proj, w.proj, 64) == 0 && memcmp(n.viewProj, w.viewProj, 64) == 0,
          "wide: g_proj and g_viewProj byte-identical at 16:9 (no x compression)");
    const float p[4] = {100.0f, -50.0f, 800.0f, 1.0f};
    float hn[4] = {0}, hw[4] = {0};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            hn[r] += n.viewProj[c * 4 + r] * p[c];
            hw[r] += w.viewProj[c * 4 + r] * p[c];
        }
    }
    const float xn = hn[0] / hn[3], xw = hw[0] / hw[3];
    CHECK(xn == xw && hn[1] / hn[3] == hw[1] / hw[3],
          "wide: a point's GS X %.3f at 4:3 and %.3f at 16:9", xn, xw);
    IcoFrameCB bw;
    rd__fill_camera_cb(&bw, &b.cam);
    CHECK(n.clip[3] == 4.0f / 3.0f && bw.clip[3] == 16.0f / 9.0f,
          "wide: g_clip.w the camera's aspect (%g, %g)", (double)n.clip[3], (double)bw.clip[3]);
    /* the present's exact width: 512 exactly on a 512-wide frame whatever
     * the aspect (the integer 4:3 box, no crop), the replayed frame's own or
     * else 384 times the frame camera's aspect on a wider one (the box 16:9, (688 - 682.67) / 2
     * columns cropped a side), never past the frame */
    {
        const uint32_t keepW = g_rd.gsW;
        const float keepA = g_rd.frameAspect, keepE = g_rd.frameWExact;
        g_rd.frameWExact = 0.0f; /* no frame head replayed: the camera's aspect */
        g_rd.gsW = 512;
        g_rd.frameAspect = 16.0f / 9.0f;
        CHECK(rd__frame_wexact() == 512.0f, "wide: a 512 frame shows 512 columns (%g)",
              (double)rd__frame_wexact());
        g_rd.gsW = 688;
        CHECK(near(rd__frame_wexact(), 384.0f * 16.0f / 9.0f),
              "wide: a 688 frame at 16:9 shows 682.67 columns (%g)", (double)rd__frame_wexact());
        g_rd.frameAspect = 48.0f / 9.0f;
        CHECK(rd__frame_wexact() == 688.0f, "wide: never past the frame's width (%g)",
              (double)rd__frame_wexact());
        /* a replayed frame's own exact width (RdFrame.wExact) wins */
        g_rd.frameWExact = 683.0f;
        CHECK(rd__frame_wexact() == 683.0f, "wide: the frame head's exact width (%g)",
              (double)rd__frame_wexact());
        g_rd.gsW = 512;
        CHECK(rd__frame_wexact() == 512.0f, "wide: still 512 on a 512 frame (%g)",
              (double)rd__frame_wexact());
        g_rd.gsW = keepW;
        g_rd.frameAspect = keepA;
        g_rd.frameWExact = keepE;
    }
    /* the shaders' copy of the frame constants */
    _Static_assert(ICO_SH_FRAME_BASE_W == ICO_FRAME_BASE_W, "shader_consts.h ICO_SH_FRAME_BASE_W");
    _Static_assert(ICO_SH_FRAME_K_MAX == ICO_FRAME_K_MAX, "shader_consts.h ICO_SH_FRAME_K_MAX");
    _Static_assert(ICO_SH_FRAME_W_MAX == ICO_FRAME_W_MAX, "shader_consts.h ICO_SH_FRAME_W_MAX");
}

/* -------------------------------------------------------------- scales */

static void applyWith(const RdSettings *s)
{
    g_rd.settings = *s;
    rd__apply_display();
}

/* the native frame for an aspect, as the flip latches it (rd_reset_scene
   sets the scene size; the targets are not remade here) */
static void frameFor(float aspect)
{
    g_rd.gsW = (uint32_t)ico_frame_width_for(aspect);
}

static void checkScales(void)
{
    const RdSettings saved = g_rd.settings;
    const uint32_t savedW = g_rd.gsW;
    RdSettings s = originalSettings();
    frameFor(4.0f / 3.0f);
    applyWith(&s);
    CHECK(g_rd.sceneSx == 1.0f && g_rd.sceneSy == 1.0f && g_rd.workScale == 1.0f &&
              g_rd.outAspect == 4.0f / 3.0f && !g_rd.fullHeight && !g_rd.filterUpgrade,
          "scales: the Original rows are 1");
    /* the options apply under the Original flag too; at 16:9 the frame is
     * 688 GS pixels wide, and the scale is the resolution's alone, the same
     * both ways */
    s.sceneScale = 3.0f;
    s.aspect = 16.0f / 9.0f;
    frameFor(s.aspect);
    applyWith(&s);
    CHECK(g_rd.gsW == 688 && g_rd.sceneSx == 3.0f && g_rd.sceneSy == 3.0f &&
              g_rd.workScale == 2.0f && near(g_rd.outAspect, 16.0f / 9.0f),
          "scales: 3x at 16:9 under the Original flag (%g, %g, work %g; frame %u)", g_rd.sceneSx,
          g_rd.sceneSy, g_rd.workScale, g_rd.gsW);
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
    frameFor(s.aspect);
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
    CHECK(g_rd.sceneSx == 2.0f && g_rd.sceneSy == 2.0f && g_rd.workScale == 2.0f,
          "scales: 2x at 4:3 (%g, %g, work %g)", g_rd.sceneSx, g_rd.sceneSy, g_rd.workScale);
    s.aspect = 16.0f / 9.0f;
    frameFor(s.aspect);
    applyWith(&s);
    CHECK(g_rd.sceneSx == 2.0f && g_rd.sceneSy == 2.0f,
          "scales: 2x at 16:9 is 2x both ways: the frame is wider, not its pixels (%g, %g)",
          g_rd.sceneSx, g_rd.sceneSy);
    s.aspect = 4.0f / 3.0f;
    frameFor(s.aspect);
    s.sceneScale = 0.0f;
    s.sceneWidth = 1920;
    s.sceneHeight = 1440;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 3.75f && g_rd.sceneSy == 2.8125f && g_rd.workScale == 2.0f,
          "scales: 1920x1440");
    /* the window's box at 16:9: the box's exact width (682.67 of the 688
     * columns) over 1920, one texel an output pixel, the PS2's pixel shape
     * kept, with Full pixel off (the game's: the border mask over the same
     * picture) and on */
    s.sceneWidth = s.sceneHeight = 0;
    s.outputWidth = 1920;
    s.outputHeight = 1080;
    s.aspect = 16.0f / 9.0f;
    frameFor(s.aspect);
    s.borderMask = 1;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 2.8125f && near(g_rd.sceneSy, 1080.0f / 512.0f),
          "scales: the window's 16:9 box with Full pixel off (%g, %g)", g_rd.sceneSx, g_rd.sceneSy);
    s.borderMask = 0;
    s.fullPixel = 1;
    applyWith(&s);
    CHECK(g_rd.sceneSx == 2.8125f && near(g_rd.sceneSy, 1080.0f / 512.0f),
          "scales: the window's 16:9 box with Full pixel on (%g, %g)", g_rd.sceneSx, g_rd.sceneSy);
    s.fullPixel = 0;
    s.aspect = 4.0f / 3.0f;
    frameFor(s.aspect);
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
    /* 48:9 in a 5760x1080 window with Window resolution: the box itself over
     * the 2048-wide frame */
    s.sceneWidth = s.sceneHeight = 0;
    s.outputWidth = 5760;
    s.outputHeight = 1080;
    s.aspect = 48.0f / 9.0f;
    frameFor(s.aspect);
    applyWith(&s);
    CHECK(g_rd.gsW == 2048 && near(g_rd.sceneSx * 2048.0f, 5760.0f) &&
              near(g_rd.sceneSy * 512.0f, 1080.0f),
          "scales: 48:9 in 5760x1080 renders 5760x1080 (frame %u)", g_rd.gsW);
    /* supersampling only as far as the picture lacks it: 8x at 48:9 into a
     * 3840x720 box already has 4.27 texels an output pixel across (5.69
     * down at full height), so SSAA2 leaves the scale at 8x; 1x there has
     * 0.53 across, so SSAA2 applies whole (ceil(2 / 0.53) = 4, at most 2) */
    {
        const RdSettings before = s;
        float lim = g_rd.hasDevice ? (float)rhi_limits()->maxRenderTargetSize : 16384.0f;
        if (g_rd.hasDevice && rhi_limits()->tiler && lim > 4096.0f) {
            lim = 4096.0f;
        }
        s.outputWidth = 3840;
        s.outputHeight = 720;
        s.fullHeightScene = 1;
        s.antiAlias = RD_AA_SSAA2;
        s.sceneScale = 8.0f;
        applyWith(&s);
        const float want8 = 2048.0f * 8.0f <= lim ? 8.0f : lim / 2048.0f;
        CHECK(g_rd.ssaa == 1.0f && near(g_rd.sceneSx, want8) && near(g_rd.sceneSy, want8),
              "scales: 8x + SSAA2 at 48:9 in a 3840x720 box stays %gx (%g, %g, ssaa %g)",
              (double)want8, g_rd.sceneSx, g_rd.sceneSy, (double)g_rd.ssaa);
        s.sceneScale = 1.0f;
        applyWith(&s);
        CHECK(g_rd.ssaa == 2.0f && g_rd.sceneSx == 2.0f && g_rd.sceneSy == 2.0f,
              "scales: 1x + SSAA2 at 48:9 in a 3840x720 box is 2x (%g, %g, ssaa %g)", g_rd.sceneSx,
              g_rd.sceneSy, (double)g_rd.ssaa);
        /* 4x there has 2.13 across: SSAA4 applies as 2 (ceil(4 / 2.13)) */
        s.sceneScale = 4.0f;
        s.antiAlias = RD_AA_SSAA4;
        applyWith(&s);
        const float want4 = 2048.0f * 8.0f <= lim ? 8.0f : lim / 2048.0f;
        CHECK(g_rd.ssaa == 2.0f && near(g_rd.sceneSx, want4),
              "scales: 4x + SSAA4 at 48:9 in a 3840x720 box is 8x (%g, ssaa %g)", g_rd.sceneSx,
              (double)g_rd.ssaa);
        s = before;
        applyWith(&s);
    }
    s.aspect = 10.0f;
    applyWith(&s);
    CHECK(g_rd.outAspect == ICO_FRAME_ASPECT_MAX, "scales: the aspect is held to the maximum");
    s.sceneWidth = s.sceneHeight = 0;
    s.outputWidth = 320;
    s.outputHeight = 240;
    s.aspect = 4.0f / 3.0f;
    frameFor(s.aspect);
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
    CHECK(t.tw == 512 && t.th == 512 && t.sy == 2.0f,
          "scales: full height doubles DISPLAY's texture (%ux%u)", t.tw, t.th);
    s.fullHeightScene = 0;
    s.sceneScale = 2.0f;
    applyWith(&s);
    t.w = t.h = 512;
    rd__target_scale_of(&t, -1);
    CHECK(t.tw == 1024 && t.th == 1024, "scales: a scene-sized temporary target");
    t.w = t.h = 256;
    rd__target_scale_of(&t, -1);
    CHECK(t.tw == 256 && t.sx == 1.0f, "scales: other temporary targets stay");
    t.w = t.h = 128;
    rd__target_scale_of(&t, RD_TARGET_FEED128);
    CHECK(t.tw == 256 && t.th == 256, "scales: the work buffers by the work scale");
    /* at 16:9 2x: a block the game sizes as the frame (688 x 512) is
     * scene-class, 2x both ways; a 256 x 256 reflection block is drawn at
     * its own size, whatever the aspect (no widening) */
    s.aspect = 16.0f / 9.0f;
    frameFor(s.aspect);
    applyWith(&s);
    t.w = 688;
    t.h = 512;
    t.withDepth = 1;
    rd__target_scale_of(&t, -1);
    CHECK(t.tw == 1376 && t.th == 1024 && t.sx == t.sy,
          "scales: a frame-sized block at 16:9 2x is %ux%u", t.tw, t.th);
    t.w = t.h = 256;
    rd__target_scale_of(&t, -1);
    CHECK(t.tw == 256 && t.th == 256 && t.sx == 1.0f && t.sy == 1.0f,
          "scales: a 256 x 256 block at 16:9 stays 256 texels across (%u)", t.tw);
    g_rd.gsW = savedW;
    g_rd.settings = saved;
    rd__apply_display();
}

/* rd_ui_box_of: the 2D layer's centred 4:3 box inside the presentation box
 * (the pointer's mapping, the deferred text's box, the movie's box) */
static void checkUiBox(void)
{
    static const struct {
        RdRect in, want;
        const char *name;
    } cases[] = {
        {{0, 0, 960, 720}, {0, 0, 960, 720}, "4:3 in 4:3"},
        {{240, 0, 1440, 1080}, {240, 0, 1440, 1080}, "the 4:3 box of 16:9 output is its own"},
        {{0, 0, 1920, 1080}, {240, 0, 1440, 1080}, "16:9: the pillar"},
        {{0, 0, 3840, 720}, {1440, 0, 960, 720}, "48:9: the pillar"},
        {{100, 50, 1600, 900}, {300, 50, 1200, 900}, "an offset 16:9 box"},
        {{0, 152, 1280, 720}, {160, 152, 960, 720}, "16:9 letterboxed in 5:4"},
        {{0, 0, 500, 500}, {0, 0, 500, 500}, "a box narrower than 4:3 is kept whole"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const RdRect u = rd_ui_box_of(&cases[i].in);
        CHECK(u.x == cases[i].want.x && u.y == cases[i].want.y && u.w == cases[i].want.w &&
                  u.h == cases[i].want.h,
              "ui box %s: %d,%d %ux%u, want %d,%d %ux%u", cases[i].name, u.x, u.y, u.w, u.h,
              cases[i].want.x, cases[i].want.y, cases[i].want.w, cases[i].want.h);
    }
    /* the movie's box is the same rectangle for an output: 4:3 pillar of 16:9 */
    RhiRect b;
    rd__ui_box(1920, 1080, &b);
    CHECK(b.x == 240 && b.y == 0 && b.w == 1440 && b.h == 1080, "ui box: rd__ui_box 1920x1080");
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
    if (s_llvmpipe && h.exact == RD_BLEND_EXACT_ATTACHMENT) {
        CHECK(h.display == GOLD_DISPLAY_EXACT && h.scene == GOLD_SCENE_EXACT &&
                  h.present == GOLD_PRESENT_EXACT,
              "original: the recorded bytes of the exact blend (attachment read)");
    } else if (s_llvmpipe && h.exact) {
        /* the storage path is not recorded (llvmpipe has no interlock) */
        printf("  original: the exact blend (%s): no recorded hashes\n",
               rd__blend_exact_name(h.exact));
    } else if (s_llvmpipe) {
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

/* Supersampling (RdSettings.antiAlias): the CPU's model of aa_reduce_ps,
 * the mean over each box pixel's footprint on a 1/16-texel grid rounded half
 * up, (sum + area / 2) / area, over src (sw x sh RGBA8) into dst (bw x bh) */
static void cpuReduce(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t bw,
                      uint32_t bh)
{
    const uint32_t ax = sw * 16u / bw, bx = sw * 16u % bw;
    const uint32_t ay = sh * 16u / bh, by = sh * 16u % bh;
    for (uint32_t py = 0; py < bh; py++) {
        const uint32_t ly = py * ay + py * by / bh, hy = (py + 1) * ay + (py + 1) * by / bh;
        for (uint32_t px = 0; px < bw; px++) {
            const uint32_t lx = px * ax + px * bx / bw, hx = (px + 1) * ax + (px + 1) * bx / bw;
            uint32_t sum[4] = {0, 0, 0, 0};
            for (uint32_t y = ly >> 4; y < ((hy + 15) >> 4) && y < sh; y++) {
                const uint32_t wy =
                    (hy < (y + 1) * 16 ? hy : (y + 1) * 16) - (ly > y * 16 ? ly : y * 16);
                uint32_t row[4] = {0, 0, 0, 0};
                for (uint32_t x = lx >> 4; x < ((hx + 15) >> 4) && x < sw; x++) {
                    const uint32_t wx =
                        (hx < (x + 1) * 16 ? hx : (x + 1) * 16) - (lx > x * 16 ? lx : x * 16);
                    for (int k = 0; k < 4; k++) {
                        row[k] += src[((size_t)y * sw + x) * 4 + k] * wx;
                    }
                }
                for (int k = 0; k < 4; k++) {
                    sum[k] += row[k] * wy;
                }
            }
            const uint32_t area = (hx - lx) * (hy - ly);
            for (int k = 0; k < 4; k++) {
                dst[((size_t)py * bw + px) * 4 + k] = (uint8_t)((sum[k] + area / 2) / area);
            }
        }
    }
}

static uint8_t s_ssaaDisp[2048 * 2048 * 4], s_ssaaRef[1280 * 960 * 4], s_ssaaOut[1600 * 1200 * 4];

/* the render passes the last ssaaRun's frame recorded, and its draws
 * above 1x (g_rd.above1xDraws) */
static uint32_t s_ssaaPasses, s_ssaaA1x;

/* the rich frame at Enhanced, full height, 4:3 into outW x outH with the
 * anti-aliasing aa and scene scale scale: DISPLAY (into s_ssaaDisp when
 * disp), the present (into s_ssaaOut) and its box; its passes in
 * s_ssaaPasses */
static bool ssaaRun(int aa, float scale, uint32_t outW, uint32_t outH, int disp, uint32_t *dw,
                    uint32_t *dh, RhiRect *box)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = scale;
    s.fullHeightScene = 1;
    s.outputWidth = outW;
    s.outputHeight = outH;
    s.antiAlias = (uint8_t)aa;
    if (!rd_init(512, 512, &s, NULL)) {
        return false;
    }
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    RhiStats st0, st1;
    const uint32_t a1x0 = g_rd.above1xDraws;
    rhi_get_stats(&st0);
    recordRichFrame(t, 1);
    rhi_get_stats(&st1);
    s_ssaaPasses = (uint32_t)(st1.renderPasses - st0.renderPasses);
    s_ssaaA1x = g_rd.above1xDraws - a1x0;
    bool ok = true;
    uint32_t w = 0, h = 0;
    uint8_t *p = readTarget(RD_TARGET_DISPLAY, &w, &h);
    ok = p != NULL && (size_t)w * h * 4 <= sizeof(s_ssaaDisp);
    if (ok && disp) {
        memcpy(s_ssaaDisp, p, (size_t)w * h * 4);
    }
    *dw = w;
    *dh = h;
    uint32_t ow = 0, oh = 0, bw = 0, bh = 0;
    ok = ok && rd__read_present(s_ssaaOut, sizeof(s_ssaaOut), &ow, &oh) && ow == outW &&
         oh == outH && rd__last_present_box(&bw, &bh, box);
    rd_destroy_texture(t);
    rd_shutdown();
    return ok;
}

/* SSAA2 (and SSAA4) at 4:3 Enhanced: the scene is the 2x (4x) render (DISPLAY
 * byte for byte the run at that scale without anti-aliasing), and the
 * present inside the box is DISPLAY box-reduced on the CPU, byte for byte
 * (the rounding: half up of the exact mean on the 1/16-texel grid).  And
 * under the Original preset the setting is off: the PS2 frame unchanged. */
static void checkSsaa(void)
{
    makeNoiseScene();

    static const struct {
        int aa;
        float scale;
        uint32_t outW, outH;
    } cases[] = {
        {RD_AA_SSAA2, 2.0f, 640, 480},  /* 1024 texels into 640: 1.6 a pixel */
        {RD_AA_SSAA2, 2.0f, 512, 384},  /* exactly 2 across */
        {RD_AA_SSAA4, 4.0f, 1280, 960}, /* 2048 into 1280 */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t w1 = 0, h1 = 0, w2 = 0, h2 = 0;
        RhiRect box, box2;
        /* the scale without anti-aliasing: its DISPLAY */
        if (!ssaaRun(RD_AA_OFF, cases[i].scale, cases[i].outW, cases[i].outH, 1, &w1, &h1, &box2)) {
            CHECK(0, "ssaa %zu: the %gx run", i, (double)cases[i].scale);
            continue;
        }
        /* 1x with supersampling */
        if (!ssaaRun(cases[i].aa, 1.0f, cases[i].outW, cases[i].outH, 0, &w2, &h2, &box)) {
            CHECK(0, "ssaa %zu: the supersampled run", i);
            continue;
        }
        CHECK(w1 == w2 && h1 == h2 && w2 == 512u * (uint32_t)cases[i].scale,
              "ssaa %zu: DISPLAY %ux%u, the %gx run's %ux%u", i, w2, h2, (double)cases[i].scale, w1,
              h1);
        CHECK(box.w == box2.w && box.h == box2.h && box.w > 0 && box.h > 0,
              "ssaa %zu: the same box", i);
        if (w1 != w2 || h1 != h2 || box.w == 0 || box.h == 0 ||
            box.w * 4u * box.h > sizeof(s_ssaaRef)) {
            continue;
        }
        cpuReduce(s_ssaaDisp, w1, h1, s_ssaaRef, box.w, box.h);
        int bad = 0, worst = 0;
        for (uint32_t y = 0; y < box.h; y++) {
            for (uint32_t x = 0; x < box.w; x++) {
                const uint8_t *g = &s_ssaaOut[(((size_t)(box.y + (int32_t)y)) * cases[i].outW +
                                               (size_t)box.x + x) *
                                              4];
                const uint8_t *c = &s_ssaaRef[((size_t)y * box.w + x) * 4];
                for (int k = 0; k < 3; k++) {
                    const int e = abs((int)g[k] - (int)c[k]);
                    worst = e > worst ? e : worst;
                    bad += e != 0;
                }
            }
        }
        printf("  ssaa %zu: %ux%u -> box %ux%u, %d bytes differ, worst %d\n", i, w1, h1, box.w,
               box.h, bad, worst);
        CHECK(bad == 0, "ssaa %zu: the present is DISPLAY box-reduced, byte for byte", i);
    }
    /* the Original preset keeps it off */
    RdSettings s = originalSettings();
    s.antiAlias = RD_AA_SSAA2;
    FrameHashes h;
    if (s_presentOriginal && richFrame(&s, &h)) {
        CHECK(h.present == s_presentOriginal, "ssaa: under the Original preset nothing changes");
    }
    CHECK(rhi_vk_validation_error_count() == 0, "ssaa: %u validation errors",
          rhi_vk_validation_error_count());
}

/* Without anti-aliasing: a scaled scene with more than two texels a pixel
 * (4x, 2048 texels into a 640 x 480 box: 3.2 a pixel) is box-reduced as
 * the supersampling is, the present inside the box byte for byte DISPLAY
 * reduced on the CPU, one pass more than the same scale at 1.28 a pixel
 * (1600 x 1200: the box blit's one bilinear tap); at scale 1 into a small
 * window (512 into 320: 1.6 a pixel) the plain box blit stays, the same
 * passes as into a window larger than the frame (960 x 720). */
static void checkAaOffReduce(void)
{
    makeNoiseScene();
    uint32_t w = 0, h = 0;
    RhiRect box;
    if (!ssaaRun(RD_AA_OFF, 4.0f, 1600, 1200, 0, &w, &h, &box)) {
        CHECK(0, "aa off: the 4x run into 1600 x 1200");
        return;
    }
    const uint32_t passesLow = s_ssaaPasses;
    if (!ssaaRun(RD_AA_OFF, 4.0f, 640, 480, 1, &w, &h, &box)) {
        CHECK(0, "aa off: the 4x run into 640 x 480");
        return;
    }
    const uint32_t passesHigh = s_ssaaPasses;
    CHECK(passesHigh == passesLow + 1u,
          "aa off: 3.2 texels a pixel adds the reduce's pass (%u, %u)", passesHigh, passesLow);
    CHECK(w == 2048 && box.w > 0 && box.h > 0 && box.w * 4u * box.h <= sizeof(s_ssaaRef),
          "aa off: DISPLAY %ux%u, box %ux%u", w, h, box.w, box.h);
    if (w == 2048 && box.w > 0 && box.h > 0 && box.w * 4u * box.h <= sizeof(s_ssaaRef)) {
        cpuReduce(s_ssaaDisp, w, h, s_ssaaRef, box.w, box.h);
        int bad = 0, worst = 0;
        for (uint32_t y = 0; y < box.h; y++) {
            for (uint32_t x = 0; x < box.w; x++) {
                const uint8_t *g =
                    &s_ssaaOut[(((size_t)(box.y + (int32_t)y)) * 640u + (size_t)box.x + x) * 4];
                const uint8_t *c = &s_ssaaRef[((size_t)y * box.w + x) * 4];
                for (int k = 0; k < 3; k++) {
                    const int e = abs((int)g[k] - (int)c[k]);
                    worst = e > worst ? e : worst;
                    bad += e != 0;
                }
            }
        }
        printf("  aa off 4x: %ux%u -> box %ux%u, %d bytes differ, worst %d\n", w, h, box.w, box.h,
               bad, worst);
        CHECK(bad == 0, "aa off 4x: the present is DISPLAY box-reduced, byte for byte");
    }
    if (!ssaaRun(RD_AA_OFF, 1.0f, 960, 720, 0, &w, &h, &box)) {
        CHECK(0, "aa off: the 1x run into 960 x 720");
        return;
    }
    const uint32_t passesBig = s_ssaaPasses;
    if (!ssaaRun(RD_AA_OFF, 1.0f, 320, 240, 0, &w, &h, &box)) {
        CHECK(0, "aa off: the 1x run into 320 x 240");
        return;
    }
    CHECK(s_ssaaPasses == passesBig, "aa off 1x: a small window keeps the plain blit (%u, %u)",
          s_ssaaPasses, passesBig);
    CHECK(rhi_vk_validation_error_count() == 0, "aa off: %u validation errors",
          rhi_vk_validation_error_count());
}

/* Full height at Resolution 1x: DISPLAY has twice the GS's rows (sy 2),
 * so the reduction is drawn as its fallback sprite, one SCENE row a
 * DISPLAY row.  The GS's filter on both axes there: no draw takes the
 * above-1x kernels, and DISPLAY and the present are byte for byte the
 * frame with the reduction flagged above 1x (rd__set_a1x_gs_keep off),
 * whose shader takes the GS's path at every pixel. */
static uint8_t s_fh1Disp[512 * 1024 * 4], s_fh1Out[960 * 720 * 4];

static void checkFullHeight1x(void)
{
    makeNoiseScene();
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    RhiRect box;
    rd__set_a1x_gs_keep(false);
    const bool okFlag = ssaaRun(RD_AA_OFF, 1.0f, 960, 720, 1, &w, &h, &box);
    rd__set_a1x_gs_keep(true);
    const uint32_t a1xFlag = s_ssaaA1x;
    CHECK(okFlag && (size_t)w * h * 4 <= sizeof(s_fh1Disp), "full height 1x: the flagged run");
    if (!okFlag || (size_t)w * h * 4 > sizeof(s_fh1Disp)) {
        return;
    }
    memcpy(s_fh1Disp, s_ssaaDisp, (size_t)w * h * 4);
    memcpy(s_fh1Out, s_ssaaOut, sizeof(s_fh1Out));
    if (!ssaaRun(RD_AA_OFF, 1.0f, 960, 720, 1, &w2, &h2, &box)) {
        CHECK(0, "full height 1x: the run");
        return;
    }
    printf("  full height 1x: DISPLAY %ux%u, %u draws above 1x flagged, %u kept at the GS's "
           "filter\n",
           w, h, a1xFlag, s_ssaaA1x);
    CHECK(a1xFlag > 0, "full height 1x: the reduction's fallback sprite flagged when asked");
    CHECK(s_ssaaA1x == 0, "full height 1x: %u draws above 1x at scale 1", s_ssaaA1x);
    CHECK(w == w2 && h == h2 && memcmp(s_fh1Disp, s_ssaaDisp, (size_t)w * h * 4) == 0,
          "full height 1x: DISPLAY byte for byte the flagged frame's");
    CHECK(memcmp(s_fh1Out, s_ssaaOut, sizeof(s_fh1Out)) == 0,
          "full height 1x: the present byte for byte the flagged frame's");
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
    /* the native frame at 16:9: 688 GS pixels across, the origin 2048 - 344,
     * the 4:3 columns 88..599 */
    const uint32_t FW = (uint32_t)ico_frame_width_for(16.0f / 9.0f);
    CHECK(FW == 688, "wide169: the frame is 688 wide (%u)", FW);
    if (!rd_init(FW, 512, &s, NULL)) {
        return;
    }
    static const uint8_t black[4] = {0, 0, 0, 0x80}, white[4] = {255, 255, 255, 0x80},
                         red[4] = {255, 0, 0, 0x80}, green[4] = {0, 255, 0, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), FW, 512, 1);
    opaque2D();
    rd_texture_off();
    /* a UI item in the 640 grid's 4:3 columns: GS 128..384 of them x 100..200 */
    sprite(RD_SPACE_UI, 128 * 16, 100 * 16, 384 * 16, 200 * 16, white, 0, 0, 0, 0);
    /* a WORLD sprite over the 4:3 columns 0..512: rows 280..290 */
    sprite(RD_SPACE_WORLD, 0, 280 * 16, 512 * 16, 290 * 16, green, 0, 0, 0, 0);
    /* a full-screen-tagged sprite over the 4:3 columns: rows 340..350 */
    sprite(RD_SPACE_FULLFRAME, 0, 340 * 16, 512 * 16, 350 * 16, green, 0, 0, 0, 0);
    /* a fill the game draws at the frame's width (GS 1704..2392) under the
     * full-frame tag (gif_FullFrameBegin's scope): rows 300..310 */
    {
        const int32_t x0 = (2048 - (int32_t)FW / 2) * 16, x1 = (2048 + (int32_t)FW / 2) * 16;
        const int32_t o = (2048 - 256) * 16;
        RdScreenVtx v[2] = {vtx(x0, o + 300 * 16, 0, red, 0.0f, 0.0f),
                            vtx(x1, o + 310 * 16, 0, red, 0.0f, 0.0f)};
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_FULLFRAME, 1, 0);
    }
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
    CHECK(p && w == FW && h == 512, "wide169: SCENE is 688x512, square texels (%ux%u)", w, h);
    {
        /* rd_ui_box: the 4:3 pillar of the 960 x 540 output's 16:9 box */
        const RdRect u = rd_ui_box();
        CHECK(u.x == 120 && u.y == 0 && u.w == 720 && u.h == 540,
              "wide169: rd_ui_box is the 720 x 540 pillar (%d,%d %ux%u)", u.x, u.y, u.w, u.h);
    }
    if (p && w == FW && h == 512) {
#define PX(x, y) (&p[((y) * (int)FW + (x)) * 4])
        /* the UI sprite at its GS X: 4:3 columns 128..383 are texels 216..471 */
        int ok = 1;
        for (int x = 0; x < (int)FW; x++) {
            const int in = x >= 216 && x <= 471;
            ok &= in ? PX(x, 150)[0] == 255 && PX(x, 150)[1] == 255 : PX(x, 150)[0] == 0;
        }
        CHECK(ok, "wide169: the UI sprite at its GS X in the centre (texels 216..471)");
        /* the world sprite where the game put it: not squeezed, not stretched */
        ok = 1;
        for (int x = 0; x < (int)FW; x++) {
            const int in = x >= 88 && x <= 599;
            ok &= in ? PX(x, 285)[1] == 255 : PX(x, 285)[1] == 0;
        }
        CHECK(ok, "wide169: a world sprite over the 4:3 columns covers texels 88..599 only");
        /* the full-frame tag changes no transform: a 512-wide sprite under it
           covers the 4:3 columns, as the untagged world one does */
        ok = 1;
        for (int x = 0; x < (int)FW; x++) {
            ok &= PX(x, 345)[1] == PX(x, 285)[1];
        }
        CHECK(ok, "wide169: a full-frame-tagged 512-wide sprite is drawn as the world one");
        ok = 1;
        for (int x = 0; x < (int)FW; x++) {
            ok &= PX(x, 305)[0] == 255 && PX(x, 305)[1] == 0;
        }
        CHECK(ok, "wide169: a full-frame-tagged frame-wide fill covers all 688 columns");
        /* letterbox: 58 lines top and bottom over the whole width, black */
        ok = 1;
        for (int x = 0; x < (int)FW; x += 1) {
            ok &= PX(x, 0)[0] == 0 && PX(x, 57)[0] == 0 && PX(x, 454)[0] == 0 && PX(x, 511)[0] == 0;
        }
        CHECK(ok, "wide169: the bars span the frame's width");
        CHECK(PX(300, 150)[0] == 255 && PX(5, 305)[0] == 255 && PX(300, 58)[0] == 0,
              "wide169: below the top bar the scene shows");
#undef PX
    }
    /* the present: the frame's exact width (682.67 of 688 columns) fills the
     * 960 x 540 output; the (688 - 682.67) / 2 columns cropped at each side
     * take the reduction's 2-column black border with them, so the fill
     * reaches the output's first and last columns (the GS's DISPLAY: no
     * border mask) */
    static uint8_t out[960 * 540 * 4];
    uint32_t ow = 0, oh = 0;
    if (rd__read_present(out, sizeof(out), &ow, &oh) && ow == 960 && oh == 540) {
        const uint8_t *q0 = &out[(305 * 540 / 512 * 960 + 0) * 4];
        const uint8_t *q1 = &out[(305 * 540 / 512 * 960 + 959) * 4];
        CHECK(q0[0] > 200 && q1[0] > 200,
              "wide169: the fill reaches the output's edges, the border cropped (%u, %u)", q0[0],
              q1[0]);
    } else {
        CHECK(0, "wide169: present readback");
    }
    CHECK(rhi_vk_validation_error_count() == 0, "wide169: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
}

/* A scene whose texel density is the box's is presented one texel
 * an output pixel.  A window-sized scene (Enhanced, scale 0) at aspect
 * `aspect` in outW x outH; GS column c of the frame is one flat colour of its
 * own, in rows 160..352 (the centre row's neighbourhood).  The presented
 * picture's centre row must equal the DISPLAY row's texels srcX .. srcX + outW
 * byte for byte (no half-texel blend: u0 = floor(crop + 1/2), snapWindow);
 * wantScene is the scene's width (the box's exact width at the box's scale
 * and RD_FULL_PIXEL_GUARD texels a side where the frame is cropped: 1922 at
 * 1920 across 16:9, Full pixel off as on) and srcX the first texel shown
 * (the guard). */
static void checkOneToOne(const char *name, float aspect, uint32_t outW, uint32_t outH,
                          uint32_t wantScene, uint32_t srcX)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.aspect = aspect;
    s.sceneScale = 0.0f;
    s.outputWidth = outW;
    s.outputHeight = outH;
    const uint32_t FW = (uint32_t)ico_frame_width_for(aspect);
    if (!rd_init(FW, 512, &s, NULL)) {
        return;
    }
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), FW, 512, 1);
    opaque2D();
    rd_texture_off();
    {
        RdScreenVtx *v = (RdScreenVtx *)malloc(sizeof(RdScreenVtx) * 2u * FW);
        const int32_t o = (2048 - 256) * 16;
        for (uint32_t c = 0; v && c < FW; c++) {
            const uint32_t hh = hash(c * 2654435761u + 12345u);
            const uint8_t col[4] = {(uint8_t)hh, (uint8_t)(hh >> 8), (uint8_t)(hh >> 16), 0x80};
            const int32_t x0 = (2048 - (int32_t)FW / 2 + (int32_t)c) * 16;
            v[2 * c] = vtx(x0, o + 160 * 16, 0, col, 0.0f, 0.0f);
            v[2 * c + 1] = vtx(x0 + 16, o + 352 * 16, 0, col, 0.0f, 0.0f);
        }
        if (v) {
            rd_screen_prims(RD_PRIM_SPRITES, v, 2u * FW, RD_SPACE_FULLFRAME, 1, 0);
        }
        /* the reduction pass writes SCENE into DISPLAY, which is what the
           presenter shows (as checkWide169 gets content there) */
        rd_select_list(12);
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
        rd_post(RD_POST_REDUCTION, &pp);
        rd_end_frame(0);
        free(v);
    }
    uint32_t sw = 0, sh = 0, dw = 0, dh = 0, ow = 0, oh = 0;
    const size_t cap = (size_t)8192 * 4096 * 4;
    uint8_t *scene = (uint8_t *)malloc(cap);
    uint8_t *disp = (uint8_t *)malloc(cap);
    uint8_t *out = (uint8_t *)malloc((size_t)outW * outH * 4);
    if (scene && disp && out && rd__read_target(rd_target(RD_TARGET_SCENE), scene, cap, &sw, &sh) &&
        rd__read_target(rd_target(RD_TARGET_DISPLAY), disp, cap, &dw, &dh) &&
        rd__read_present(out, (size_t)outW * outH * 4, &ow, &oh) && ow == outW && oh == outH) {
        CHECK(sw == wantScene && dw == wantScene, "%s: SCENE and DISPLAY are %u wide (%u, %u)",
              name, wantScene, sw, dw);
        RhiRect bx;
        rd__present_box(outW, outH, aspect, &bx);
        /* the box's centre row; DISPLAY is the half-height or the full-height
           picture of the box (the bands are flat down their rows, so either
           maps the centre to dh / 2) */
        const uint32_t y = (uint32_t)bx.y + bx.h / 2;
        const uint32_t dy = dh / 2;
        uint32_t bad = 0, first = 0;
        for (uint32_t x = 0; x < outW && srcX + x < dw; x++) {
            const uint8_t *a = &out[((size_t)y * outW + x) * 4];
            const uint8_t *b = &disp[((size_t)dy * dw + srcX + x) * 4];
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
                if (!bad++) {
                    first = x;
                }
            }
        }
        CHECK(bad == 0,
              "%s: the presented centre row is DISPLAY texels %u..%u byte for byte (%u columns "
              "differ, the first %u)",
              name, srcX, srcX + outW, bad, first);
    } else {
        CHECK(0, "%s: readback", name);
    }
    free(disp);
    free(scene);
    free(out);
    rd_shutdown();
}

static void checkPresent1to1(void)
{
    /* the box's exact width (682.67 of 688 columns): a scene of 1920 and
     * its guard texels across the 1920 box, shown from the guard */
    checkOneToOne("1to1 1920x1080 16:9", 16.0f / 9.0f, 1920, 1080, 1920 + 2 * RD_FULL_PIXEL_GUARD,
                  RD_FULL_PIXEL_GUARD);
    /* the same at 4K */
    checkOneToOne("1to1 3840x2160 16:9", 16.0f / 9.0f, 3840, 2160, 3840 + 2 * RD_FULL_PIXEL_GUARD,
                  RD_FULL_PIXEL_GUARD);
    /* 48:9: the native 2048 is the exact width, no crop */
    checkOneToOne("1to1 3840x2160 48:9", 48.0f / 9.0f, 3840, 2160, 3840, 0);
}

/* Full pixel: the window's exact pixels.  One frame drawn by GS
 * X (so the same picture at every frame width): a band of one-column
 * sprites, colour hash(X), a Gouraud triangle wider than any frame (cut by
 * the exact viewport), and a sprite sampling SCENE itself
 * (its snapshot through the UV crop, nearest), then the reduction into
 * DISPLAY; full height, so DISPLAY has every row.  The Enhanced window
 * resolution at aspect `aspect` in outW x outH.  Returns SCENE, DISPLAY and
 * the present (malloc'd; NULL on a failed readback) and the box.  full: 0
 * Full pixel off with the GS's own DISPLAY (a zeroed RdSettings), 1 on, 2
 * off as the game has it (RdSettings.borderMask). */
typedef struct {
    uint8_t *scene, *disp, *out;
    uint32_t sw, sh, dw, dh;
    RhiRect box;
} ExactRun;

/* The last run's border mask (exactRun, fullPixelRun, checkBorderOff),
 * noted before rd_shutdown: the present's box, the mask's rect in it
 * (rd__border_mask; on 0 without the mask), the frame's exact width and
 * lines */
typedef struct {
    RhiRect box, inner;
    int on;
    float we;
    uint32_t gsH;
} MaskInfo;

static MaskInfo s_mask;

static void noteMask(void)
{
    memset(&s_mask, 0, sizeof(s_mask));
    uint32_t w = 0, h = 0;
    if (!rd__last_present_box(&w, &h, &s_mask.box)) {
        return;
    }
    s_mask.we = rd__frame_wexact();
    s_mask.gsH = g_rd.gsH;
    s_mask.on = rd__border_mask(&s_mask.box, s_mask.we, s_mask.gsH, &s_mask.inner);
}

/* Full pixel off in the game against on, both outW x outH presents of the
 * same frame: the mask's rect m->inner is the on picture byte for byte,
 * the rest of the box (the bands) black, outside the box the on present;
 * and the bands are the game's border at the picture's own scale within a
 * pixel: 2 GS columns of box.w / we output pixels at each side, and the
 * reduction's crop rows of DISPLAY's gsH / 2 (2 of 224 at 448 lines, 8 of
 * 256 at 512) of the box's height at the top and bottom.  Nothing of the
 * picture moves: the mask only hides its edge. */
static void checkMaskAgainst(const char *name, const uint8_t *off, const uint8_t *on, uint32_t outW,
                             uint32_t outH, const MaskInfo *m)
{
    CHECK(m->on, "%s: the border mask is drawn", name);
    if (!m->on) {
        return;
    }
    const RhiRect *b = &m->box, *in = &m->inner;
    const int32_t l = in->x - b->x, t = in->y - b->y;
    const int32_t r = (b->x + (int32_t)b->w) - (in->x + (int32_t)in->w);
    const int32_t bo = (b->y + (int32_t)b->h) - (in->y + (int32_t)in->h);
    const float crop = m->gsH >= 512u ? 8.0f : 2.0f;
    const float side = 2.0f * (float)b->w / m->we;
    const float rows = (float)b->h * crop / ((float)m->gsH * 0.5f);
    printf("%s: border mask: box %d,%d %ux%u, bands %d left, %d right, %d top, %d bottom (the "
           "border %.3f a side, %.3f top and bottom)\n",
           name, b->x, b->y, b->w, b->h, l, r, t, bo, (double)side, (double)rows);
    CHECK(fabsf((float)l - side) <= 1.0f && fabsf((float)r - side) <= 1.0f,
          "%s: the side bands are 2 GS columns at the picture's scale (%d, %d; %.3f)", name, l, r,
          (double)side);
    CHECK(fabsf((float)t - rows) <= 1.0f && fabsf((float)bo - rows) <= 1.0f,
          "%s: the top and bottom bands are %g of %u lines (%d, %d; %.3f)", name, (double)crop,
          m->gsH / 2u, t, bo, (double)rows);
    uint32_t inside = 0, band = 0, outside = 0;
    for (uint32_t y = 0; y < outH; y++) {
        for (uint32_t x = 0; x < outW; x++) {
            const size_t o = ((size_t)y * outW + x) * 4;
            const int32_t X = (int32_t)x, Y = (int32_t)y;
            const int inBox =
                X >= b->x && X < b->x + (int32_t)b->w && Y >= b->y && Y < b->y + (int32_t)b->h;
            const int inMask = X >= in->x && X < in->x + (int32_t)in->w && Y >= in->y &&
                               Y < in->y + (int32_t)in->h;
            if (inMask) {
                inside += memcmp(&off[o], &on[o], 4) != 0;
            } else if (inBox) {
                band += (off[o] | off[o + 1] | off[o + 2]) != 0;
            } else {
                outside += memcmp(&off[o], &on[o], 4) != 0;
            }
        }
    }
    CHECK(inside == 0 && band == 0 && outside == 0,
          "%s: inside the mask the on picture byte for byte, the bands black (%u pixels differ "
          "inside, %u lit in the bands, %u differ outside the box)",
          name, inside, band, outside);
}

/* exactRun without its Gouraud triangle: every remaining pixel is a flat
 * colour or a whole-texel copy, the same on every conformant device (the
 * hash pins below) */
static int s_exactNoTri;

static void exactFree(ExactRun *e)
{
    free(e->scene);
    free(e->disp);
    free(e->out);
    memset(e, 0, sizeof(*e));
}

static bool exactRun(float aspect, uint32_t outW, uint32_t outH, int full, ExactRun *e)
{
    memset(e, 0, sizeof(*e));
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.aspect = aspect;
    s.sceneScale = 0.0f;
    s.outputWidth = outW;
    s.outputHeight = outH;
    s.fullPixel = (uint8_t)(full == 1);
    s.borderMask = (uint8_t)(full == 2);
    s.fullHeightScene = 1;
    const uint32_t FW = (uint32_t)ico_frame_width_for(aspect);
    if (!rd_init(FW, 512, &s, NULL)) {
        return false;
    }
    static const uint8_t black[4] = {0, 0, 0, 0x80}, grey[4] = {0x80, 0x80, 0x80, 0x80};
    const int32_t left = 2048 - (int32_t)FW / 2, top = 2048 - 256;
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), FW, 512, 1);
    opaque2D();
    rd_texture_off();
    RdScreenVtx *v = (RdScreenVtx *)malloc(sizeof(RdScreenVtx) * 2u * FW);
    for (uint32_t c = 0; v && c < FW; c++) {
        const int32_t X = left + (int32_t)c;
        const uint32_t hh = hash((uint32_t)X * 2654435761u + 777u);
        const uint8_t col[4] = {(uint8_t)hh, (uint8_t)(hh >> 8), (uint8_t)(hh >> 16), 0x80};
        v[2 * c] = vtx(X * 16, (top + 96) * 16, 0, col, 0.0f, 0.0f);
        v[2 * c + 1] = vtx((X + 1) * 16, (top + 240) * 16, 0, col, 0.0f, 0.0f);
    }
    if (v) {
        rd_screen_prims(RD_PRIM_SPRITES, v, 2u * FW, RD_SPACE_WORLD, 1, 0);
    }
    free(v);
    rd_gouraud(1);
    if (!s_exactNoTri) {
        static const uint8_t ca[4] = {250, 10, 40, 0x80}, cb[4] = {20, 240, 60, 0x80},
                             cc[4] = {30, 70, 230, 0x80};
        /* GS 1000 .. 3100 across (wider than 48:9's 2048), 12.4 off the grid */
        const RdScreenVtx t[3] = {vtx(1000 * 16 + 5, (top + 250) * 16 + 3, 0, ca, 0.0f, 0.0f),
                                  vtx(3100 * 16 + 11, (top + 262) * 16 + 7, 0, cb, 0.0f, 0.0f),
                                  vtx(2049 * 16 + 9, (top + 330) * 16 + 1, 0, cc, 0.0f, 0.0f)};
        rd_screen_prims(RD_PRIM_TRIANGLES, t, 3, RD_SPACE_WORLD, 1, 0);
    }
    /* SCENE's rows 96..136 (the band) again at rows 360..400, sampled from
       SCENE itself: u = the column's GS X less the frame's left, in 1/16 */
    rd_texture(rd_target_texture(rd_target(RD_TARGET_SCENE), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
               RD_TCC_RGBA);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    {
        const RdScreenVtx q[2] = {
            vtx(left * 16, (top + 360) * 16, 0, grey, 8.0f, 96.0f * 16.0f + 8.0f),
            vtx((left + (int32_t)FW) * 16, (top + 400) * 16, 0, grey, (float)FW * 16.0f + 8.0f,
                136.0f * 16.0f + 8.0f)};
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_texture_off();
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    const size_t cap = (size_t)4096 * 2304 * 4;
    e->scene = (uint8_t *)malloc(cap);
    e->disp = (uint8_t *)malloc(cap);
    e->out = (uint8_t *)malloc((size_t)outW * outH * 4);
    uint32_t ow = 0, oh = 0;
    rd__present_box(outW, outH, aspect, &e->box);
    const bool ok = e->scene && e->disp && e->out &&
                    rd__read_target(rd_target(RD_TARGET_SCENE), e->scene, cap, &e->sw, &e->sh) &&
                    rd__read_target(rd_target(RD_TARGET_DISPLAY), e->disp, cap, &e->dw, &e->dh) &&
                    rd__read_present(e->out, (size_t)outW * outH * 4, &ow, &oh) && ow == outW &&
                    oh == outH;
    CHECK(rhi_vk_validation_error_count() == 0, "exact window: %u validation errors",
          rhi_vk_validation_error_count());
    noteMask();
    rd_shutdown();
    if (!ok) {
        exactFree(e);
    }
    return ok;
}

/* the columns of a and b (rows 0..h) that differ: a's x0 + i against b's
 * y0 + i for i < n */
static uint32_t colsDiffer(const uint8_t *a, uint32_t aw, uint32_t x0, const uint8_t *b,
                           uint32_t bw, uint32_t y0, uint32_t n, uint32_t h, uint32_t *first)
{
    uint32_t bad = 0;
    *first = ~0u;
    for (uint32_t i = 0; i < n; i++) {
        int d = 0;
        for (uint32_t y = 0; y < h && !d; y++) {
            d = memcmp(&a[((size_t)y * aw + x0 + i) * 4], &b[((size_t)y * bw + y0 + i) * 4], 3);
        }
        if (d) {
            bad++;
            *first = *first == ~0u ? i : *first;
        }
    }
    return bad;
}

/* The present's edge columns with Full pixel on: DISPLAY is
 * SCENE through the reduction, which samples a whole texel off its pixel
 * (round(s / 4), rd__blur_snap_verts); the shift d is found on the band's
 * inner columns (rows of one colour a column), and every presented column
 * of the band's middle row, the edge ones too, must be SCENE's column
 * x + G + d (G the guard texels the present leaves out).  Without the
 * guard the last column's sample lay past SCENE and clamped: it repeated
 * its neighbour's texel. */
static void checkExactEdges(const char *name, const ExactRun *a, uint32_t outW, uint32_t G)
{
    const uint32_t W = a->box.w;
    const uint32_t y = 168u * a->sh / 512u; /* the band's middle row (GS 96 .. 240) */
    const uint8_t *srow = &a->scene[(size_t)y * a->sw * 4];
    const uint8_t *orow = &a->out[(((size_t)a->box.y + y) * outW + (uint32_t)a->box.x) * 4];
    int d = -99;
    for (int k = -2; k <= 2 && d == -99; k++) {
        uint32_t bad = 0;
        for (uint32_t x = W / 4; x < 3u * W / 4u; x++) {
            const int64_t sx = (int64_t)x + G + k;
            for (int c = 0; c < 3; c++) {
                bad += abs((int)orow[x * 4 + c] - (int)srow[sx * 4 + c]) > 1;
            }
        }
        d = bad == 0 ? k : d;
    }
    CHECK(d != -99, "%s: the present's middle row is SCENE's, shifted (no shift fits)", name);
    if (d == -99) {
        return;
    }
    uint32_t past = 0, bad = 0, first = ~0u;
    for (uint32_t x = 0; x < W; x++) {
        const int64_t sx = (int64_t)x + G + d;
        if (sx < 0 || sx >= (int64_t)a->sw) {
            past++;
            first = first == ~0u ? x : first;
            continue;
        }
        int e = 0;
        for (int c = 0; c < 3; c++) {
            e |= abs((int)orow[x * 4 + c] - (int)srow[sx * 4 + c]) > 1;
        }
        bad += (uint32_t)e;
        first = e && first == ~0u ? x : first;
    }
    CHECK(past == 0 && bad == 0,
          "%s: every presented column is SCENE's column + %d, the edges too (%u read past "
          "SCENE's %u texels, %u differ, the first %u)",
          name, (int)G + d, past, a->sw, bad, first);
    printf("%s: the present is SCENE shifted %d texel(s), edge columns included (%u differ)\n",
           name, (int)G + d, past + bad);
}

/* With Full pixel on: SCENE and DISPLAY are the box's exact size and
 * RD_FULL_PIXEL_GUARD texels more at each side, the present is DISPLAY one
 * texel a pixel without the guard (the whole box, nothing else cropped),
 * and the frame's 4:3 columns are byte for byte the 4:3 picture at the
 * same height (the Enhanced window at 4:3 in a box as high: "4:3 is a
 * camera"): SCENE whole, DISPLAY but the reduction's few columns at the
 * 4:3 picture's own edges (it reads past them there).  With it off (the
 * game's: the border mask) on a cropped frame SCENE and DISPLAY are the on
 * run's byte for byte and the present is the on present under the mask
 * (checkMaskAgainst); where the frame is its exact width (48:9) off is
 * the GS's own border, no mask, the zeroed RdSettings' run byte for
 * byte. */
static void checkExactWindow(const char *name, float aspect, uint32_t outW, uint32_t outH)
{
    ExactRun a, b, c;
    if (!exactRun(aspect, outW, outH, 1, &a)) {
        CHECK(0, "%s: full pixel readback", name);
        return;
    }
    /* the guard where the frame is cropped (its exact width below the
       native width: not 48:9, whose 2048 is exact) */
    IcoFrame fr;
    ico_frame_make(&fr, aspect, 448);
    const uint32_t G = fr.wExact < (float)fr.w - 0.01f ? RD_FULL_PIXEL_GUARD : 0u;
    CHECK(a.sw == a.box.w + 2u * G && a.sh == a.box.h,
          "%s: SCENE is the box and %u guard texels a side, %ux%u (%ux%u)", name, G,
          a.box.w + 2u * G, a.box.h, a.sw, a.sh);
    CHECK(a.dw == a.sw && a.dh == a.sh, "%s: DISPLAY is SCENE's size (%ux%u)", name, a.dw, a.dh);
    if (a.dw == a.box.w + 2u * G && a.dh == a.box.h) {
        uint32_t bad = 0;
        for (uint32_t y = 0; y < a.dh; y++) {
            bad += memcmp(&a.out[(((size_t)a.box.y + y) * outW + (uint32_t)a.box.x) * 4],
                          &a.disp[((size_t)y * a.dw + G) * 4], (size_t)a.box.w * 4) != 0;
        }
        CHECK(bad == 0,
              "%s: the present is DISPLAY byte for byte, its guard left out (%u rows differ)", name,
              bad);
    }
    if (a.sh == a.box.h && a.sw >= a.box.w) {
        checkExactEdges(name, &a, outW, G);
    }
    /* the 4:3 picture as high as the box */
    const uint32_t w43 = (uint32_t)(((uint64_t)a.box.h * 4u + 1u) / 3u);
    if (!exactRun(4.0f / 3.0f, w43, a.box.h, 1, &b)) {
        CHECK(0, "%s: 4:3 readback", name);
        exactFree(&a);
        return;
    }
    if (b.sw == w43 && b.sh == a.sh && a.sw >= w43) {
        /* the frame's centre, GS 2048, is texel floor(w / 2) of each */
        const uint32_t off = a.sw / 2u - b.sw / 2u;
        uint32_t first;
        uint32_t bad = colsDiffer(a.scene, a.sw, off, b.scene, b.sw, 0, b.sw, b.sh, &first);
        CHECK(bad == 0,
              "%s: SCENE's 4:3 columns (%u..%u) are the 4:3 picture's byte for byte (%u differ, "
              "the first %u)",
              name, off, off + b.sw - 1, bad, first);
        const uint32_t edge = 8;
        bad =
            colsDiffer(a.disp, a.dw, off + edge, b.disp, b.dw, edge, b.dw - 2 * edge, b.dh, &first);
        CHECK(bad == 0, "%s: DISPLAY's 4:3 columns are the 4:3 picture's (%u differ, the first %u)",
              name, bad, first);
    } else {
        CHECK(0, "%s: the 4:3 picture is %ux%u (%ux%u wanted)", name, b.sw, b.sh, w43, a.sh);
    }
    exactFree(&b);
    if (!G) {
        /* off where the frame is its exact width (4:3, 48:9): nothing is
           cropped, so the game's border is the GS's own, as in v0.7.4: the
           replay keeps the reduction's scissor and no mask is drawn
           (rd__frame_cropped); the game's off (2) is the zeroed
           RdSettings' (0) byte for byte, SCENE the box */
        ExactRun d;
        const bool okC = exactRun(aspect, outW, outH, 2, &c);
        const MaskInfo m = s_mask;
        const bool okD = okC && exactRun(aspect, outW, outH, 0, &d);
        if (okD) {
            CHECK(!m.on, "%s: full pixel off: no border mask where the frame is its exact width",
                  name);
            CHECK(c.sw == a.box.w && c.sh == a.box.h,
                  "%s: full pixel off: SCENE is the box, %ux%u (%ux%u)", name, a.box.w, a.box.h,
                  c.sw, c.sh);
            const bool same = c.sw == d.sw && c.sh == d.sh && c.dw == d.dw && c.dh == d.dh &&
                              memcmp(c.scene, d.scene, (size_t)c.sw * c.sh * 4) == 0 &&
                              memcmp(c.disp, d.disp, (size_t)c.dw * c.dh * 4) == 0 &&
                              memcmp(c.out, d.out, (size_t)outW * outH * 4) == 0;
            CHECK(same,
                  "%s: full pixel off: SCENE, DISPLAY and the present are the GS's own border's "
                  "byte for byte (borderMask changes nothing)",
                  name);
            exactFree(&d);
        } else {
            CHECK(0, "%s: full pixel off readback", name);
        }
        if (okC) {
            exactFree(&c);
        }
    } else if (exactRun(aspect, outW, outH, 2, &c)) {
        /* off on a cropped frame: the same frame, the same picture, the
           border masked over it at present (rd__border_mask) */
        const MaskInfo m = s_mask;
        CHECK(c.sw == a.sw && c.sh == a.sh && c.dw == a.dw && c.dh == a.dh,
              "%s: full pixel off: SCENE and DISPLAY are the on run's size (%ux%u, %ux%u)", name,
              c.sw, c.sh, c.dw, c.dh);
        if (c.sw == a.sw && c.sh == a.sh && c.dw == a.dw && c.dh == a.dh) {
            CHECK(memcmp(c.scene, a.scene, (size_t)a.sw * a.sh * 4) == 0 &&
                      memcmp(c.disp, a.disp, (size_t)a.dw * a.dh * 4) == 0,
                  "%s: full pixel off: SCENE and DISPLAY are the on run's byte for byte (the "
                  "mask is not in them)",
                  name);
            char nm[160];
            snprintf(nm, sizeof(nm), "%s off", name);
            checkMaskAgainst(nm, c.out, a.out, outW, outH, &m);
        }
        exactFree(&c);
    } else {
        CHECK(0, "%s: full pixel off readback", name);
    }
    exactFree(&a);
}

/* Full pixel on, pinned: the present of the Enhanced window (exactRun
 * without its triangle) and of a fixed resolution (fullPixelRun's red
 * frame) hash to what they were measured at before v0.7.4's Full pixel
 * off box changed (the same values since: the border mask changes off
 * alone), so no Full pixel off change can have touched the on
 * picture.  0 prints the hash (to measure a new case).  Off at each pinned
 * cropped case (16:9, the odd size) is the on present under the border
 * mask (checkMaskPin); off at 4:3 and 48:9, where the frame is its exact
 * width, is pinned to v0.7.4's hashes with the border mask asked for as
 * the host asks (full 2) and without it (0): the Original preset's
 * picture is the PS2's, byte for byte as in v0.7.4. */
static bool fullPixelRun(float aspect, uint32_t w, uint32_t h, int full, int fullHeight,
                         float scale, uint8_t *dst, uint32_t *ow, uint32_t *oh);

static void checkFullPixelOnPin(const char *name, float aspect, uint32_t outW, uint32_t outH,
                                int full, uint64_t want)
{
    ExactRun a;
    s_exactNoTri = 1;
    const bool ok = exactRun(aspect, outW, outH, full, &a);
    s_exactNoTri = 0;
    if (!ok) {
        CHECK(0, "%s: readback", name);
        return;
    }
    const uint64_t got = fnv(a.out, (size_t)outW * outH * 4);
    printf("%s: present %016llx\n", name, (unsigned long long)got);
    CHECK(!want || got == want, "%s: the present is pinned (%016llx, %016llx wanted)", name,
          (unsigned long long)got, (unsigned long long)want);
    exactFree(&a);
}

static void checkFullPixelOnPinFixed(const char *name, float aspect, float scale, int full,
                                     uint64_t want)
{
    static uint8_t out[960 * 540 * 4];
    uint32_t ow = 0, oh = 0;
    if (!fullPixelRun(aspect, 960, 540, full, 0, scale, out, &ow, &oh)) {
        CHECK(0, "%s: readback", name);
        return;
    }
    const uint64_t got = fnv(out, sizeof(out));
    printf("%s: present %016llx\n", name, (unsigned long long)got);
    CHECK(!want || got == want, "%s: the present is pinned (%016llx, %016llx wanted)", name,
          (unsigned long long)got, (unsigned long long)want);
}

/* Full pixel off at a pinned case: the on present (the pinned picture,
 * without the triangle) under the border mask, byte for byte inside it */
static void checkMaskPin(const char *name, float aspect, uint32_t outW, uint32_t outH)
{
    ExactRun a, c;
    s_exactNoTri = 1;
    const bool okA = exactRun(aspect, outW, outH, 1, &a);
    const bool okC = okA && exactRun(aspect, outW, outH, 2, &c);
    const MaskInfo m = s_mask;
    s_exactNoTri = 0;
    if (!okC) {
        CHECK(0, "%s: readback", name);
        if (okA) {
            exactFree(&a);
        }
        return;
    }
    checkMaskAgainst(name, c.out, a.out, outW, outH, &m);
    exactFree(&a);
    exactFree(&c);
}

static void checkMaskPinFixed(const char *name, float aspect, float scale)
{
    static uint8_t on[960 * 540 * 4], off[960 * 540 * 4];
    uint32_t ow = 0, oh = 0;
    if (!fullPixelRun(aspect, 960, 540, 1, 0, scale, on, &ow, &oh) ||
        !fullPixelRun(aspect, 960, 540, 2, 0, scale, off, &ow, &oh)) {
        CHECK(0, "%s: readback", name);
        return;
    }
    const MaskInfo m = s_mask;
    checkMaskAgainst(name, off, on, 960, 540, &m);
}

static void checkFullPixelOnPins(void)
{
    checkFullPixelOnPin("on pin 1920x1080 16:9", 16.0f / 9.0f, 1920, 1080, 1,
                        0x5a6253d370f93fedull);
    checkFullPixelOnPin("on pin 2560x1440 16:9", 16.0f / 9.0f, 2560, 1440, 1,
                        0xb9a521ebc29ea8e1ull);
    checkFullPixelOnPin("on pin 3840x2160 16:9", 16.0f / 9.0f, 3840, 2160, 1,
                        0x3d86fa050d74b667ull);
    checkFullPixelOnPin("on pin 3840x720 48:9", 48.0f / 9.0f, 3840, 720, 1, 0x7923b695ae9fbb52ull);
    checkFullPixelOnPin("on pin 2341x1080 odd", 2341.0f / 1080.0f, 2341, 1080, 1,
                        0xc70ed63a947fee65ull);
    checkFullPixelOnPin("on pin 1440x1080 4:3", 4.0f / 3.0f, 1440, 1080, 1, 0xaec60101729fd8a5ull);
    checkFullPixelOnPinFixed("on pin 16:9 1x", 16.0f / 9.0f, 1.0f, 1, 0xf3a2cee658fa9b25ull);
    checkFullPixelOnPinFixed("on pin 16:9 2x", 16.0f / 9.0f, 2.0f, 1, 0xf3a2cee658fa9b25ull);
    checkFullPixelOnPinFixed("on pin 4:3 1x", 4.0f / 3.0f, 1.0f, 1, 0x61c70a2a8bc63125ull);
    /* off (the game's, the border mask) at each pinned cropped case: the
     * on picture under the mask */
    checkMaskPin("mask pin 1920x1080 16:9", 16.0f / 9.0f, 1920, 1080);
    checkMaskPin("mask pin 2560x1440 16:9", 16.0f / 9.0f, 2560, 1440);
    checkMaskPin("mask pin 3840x2160 16:9", 16.0f / 9.0f, 3840, 2160);
    checkMaskPin("mask pin 2341x1080 odd", 2341.0f / 1080.0f, 2341, 1080);
    checkMaskPinFixed("mask pin 16:9 1x", 16.0f / 9.0f, 1.0f);
    checkMaskPinFixed("mask pin 16:9 2x", 16.0f / 9.0f, 2.0f);
    /* off at 4:3 and 48:9, where the frame is its exact width: v0.7.4's
     * pins, the game's off (2, the host's borderMask) and the GS's own (0) */
    checkFullPixelOnPin("off pin 1440x1080 4:3", 4.0f / 3.0f, 1440, 1080, 2, 0xdb7d8b80a8a90725ull);
    checkFullPixelOnPinFixed("off pin 4:3 1x", 4.0f / 3.0f, 1.0f, 2, 0xd3d0e6a8c395dc0dull);
    checkFullPixelOnPin("off pin 3840x720 48:9", 48.0f / 9.0f, 3840, 720, 2, 0x7cfce9bed9c31c45ull);
    checkFullPixelOnPin("off pin 1440x1080 4:3 (GS)", 4.0f / 3.0f, 1440, 1080, 0,
                        0xdb7d8b80a8a90725ull);
    checkFullPixelOnPinFixed("off pin 4:3 1x (GS)", 4.0f / 3.0f, 1.0f, 0, 0xd3d0e6a8c395dc0dull);
    checkFullPixelOnPin("off pin 3840x720 48:9 (GS)", 48.0f / 9.0f, 3840, 720, 0,
                        0x7cfce9bed9c31c45ull);
}

static void checkExactWindows(void)
{
    checkExactWindow("exact 1920x1080 16:9", 16.0f / 9.0f, 1920, 1080);
    checkExactWindow("exact 2560x1440 16:9", 16.0f / 9.0f, 2560, 1440);
    checkExactWindow("exact 3840x2160 16:9", 16.0f / 9.0f, 3840, 2160);
    checkExactWindow("exact 3840x720 48:9", 48.0f / 9.0f, 3840, 720);
    checkExactWindow("exact 2341x1080 odd", 2341.0f / 1080.0f, 2341, 1080);
}

/* Full pixel: SCENE red through the reduction pass (which leaves its border
 * black), presented into w x h.  The box's edge columns and rows are red
 * with the option on, the reduction drawing the whole frame, and, with it
 * off, the border shows.  scale above 1 runs the reduction on a scaled
 * target.  full: as exactRun's (2: off as the game has it, the border
 * mask) */
static bool fullPixelRun(float aspect, uint32_t w, uint32_t h, int full, int fullHeight,
                         float scale, uint8_t *dst, uint32_t *ow, uint32_t *oh)
{
    RdSettings s = originalSettings();
    s.aspect = aspect;
    s.outputWidth = w;
    s.outputHeight = h;
    s.fullPixel = (uint8_t)(full == 1);
    s.borderMask = (uint8_t)(full == 2);
    s.fullHeightScene = (uint8_t)fullHeight;
    if (aspect > 4.0f / 3.0f + 0.01f || scale > 1.0f) {
        s.preset = RD_PRESET_ENHANCED;
    }
    if (scale > 1.0f) {
        s.sceneScale = scale; /* a scaled target: the reduction's hardware sprite */
    }
    /* the native frame for the aspect (688 at 16:9) */
    if (!rd_init((uint32_t)ico_frame_width_for(aspect), 512, &s, NULL)) {
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
    noteMask();
    rd_shutdown();
    return ok;
}

static void checkFullPixelCase(const char *name, float aspect, uint32_t w, uint32_t h,
                               int fullHeight, float scale)
{
    static uint8_t off[960 * 540 * 4], on[960 * 540 * 4], gs[960 * 540 * 4];
    uint32_t ow = 0, oh = 0;
    /* off as the game has it: on a cropped frame (16:9) the border mask
     * over the on picture; at 4:3, where the frame is its exact width, the
     * GS's own border, the zeroed RdSettings' present byte for byte */
    if (!fullPixelRun(aspect, w, h, 2, fullHeight, scale, off, &ow, &oh)) {
        CHECK(0, "%s: present readback", name);
        return;
    }
    const MaskInfo m = s_mask;
    if (!fullPixelRun(aspect, w, h, 1, fullHeight, scale, on, &ow, &oh)) {
        CHECK(0, "%s: present readback", name);
        return;
    }
    IcoFrame fr;
    ico_frame_make(&fr, aspect, 512);
    if (fr.wExact < (float)fr.w - 0.01f) {
        checkMaskAgainst(name, off, on, w, h, &m);
    } else {
        if (!fullPixelRun(aspect, w, h, 0, fullHeight, scale, gs, &ow, &oh)) {
            CHECK(0, "%s: present readback", name);
            return;
        }
        CHECK(!m.on, "%s: off: no border mask where the frame is its exact width", name);
        CHECK(memcmp(off, gs, (size_t)w * h * 4) == 0,
              "%s: off: the GS's own border, the present byte for byte without borderMask", name);
    }
    RhiRect box;
    rd__present_box(w, h, aspect, &box);
    const uint32_t bw = box.w, bh = box.h;
    const uint32_t x0 = (uint32_t)box.x, x1 = x0 + bw - 1, y0 = (uint32_t)box.y, y1 = y0 + bh - 1;
#define PXO(b, x, y) (&(b)[((size_t)(y) * w + (x)) * 4])
#define RED(q) ((q)[0] > 200 && (q)[1] < 60 && (q)[2] < 60)
#define BLACK(q) ((q)[0] < 8 && (q)[1] < 8 && (q)[2] < 8)
    const uint32_t cx = w / 2, cy = h / 2;
    /* off: the game's border at the box edge (2 of the frame's columns at
     * the picture's scale, 8 of DISPLAY's 256 rows), red inside, at every
     * aspect: the box is on's, the mask hides its edge on a cropped frame,
     * the reduction's own border at 4:3 */
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

/* full pixel does not enlarge the picture: at 512 lines the square is the
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
 * menu's bars are drawn (full-frame tagged: the game draws them in
 * gif_FullFrameBegin's scope).  2: the same band UI-tagged.  full: as
 * exactRun's (2: off as the game has it, the border mask) */
static uint8_t *clampRun(int full, float scale, int fullHeight, int mirror, int bandTest,
                         uint32_t *w, uint32_t *h)
{
    static const uint8_t red[4] = {255, 0, 0, 0x80}, blue[4] = {0, 0, 255, 0x80},
                         black[4] = {0, 0, 0, 0x80};
    RdSettings s = originalSettings();
    s.fullPixel = (uint8_t)(full == 1);
    s.borderMask = (uint8_t)(full == 2);
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
        sprite(bandTest == 2 ? RD_SPACE_UI : RD_SPACE_FULLFRAME, 4, 128 * 16, 511 * 16 + 7,
               384 * 16, black, 0, 0, 0, 0);
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

/* Reduction snap.  SCENE is alternating 0 / 255 GS columns
 * (symmetric about the frame's centre), the reduction runs at a scale, and
 * every DISPLAY texel inside the crop is exactly 0 or 255: the hardware
 * sprite's sample lands on a texel centre and no neighbouring columns are
 * blended (a sample at x + s / 4 gives a 30 / 70 mix at 2.8125, 41 / 59
 * at 5.625).  The mirrored run is the mirror image of the plain one. */
static uint8_t *snapRun(float scale, int mirror, uint32_t *w, uint32_t *h)
{
    static uint8_t keep[2][2880 * 1440 * 4];
    static int which;
    static const uint8_t black[4] = {0, 0, 0, 0x80}, white[4] = {255, 255, 255, 0x80};
    RdSettings s = originalSettings();
    s.mirror = (uint8_t)mirror;
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = scale;
    if (!rd_init(512, 512, &s, NULL)) {
        return NULL;
    }
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_texture_off();
    for (int i = 0; i < 512; i++) {
        const int m = i < 256 ? i : 511 - i;
        if (m & 1) {
            sprite(RD_SPACE_FULLFRAME, i * 16, 0, (i + 1) * 16, 512 * 16, white, 0, 0, 0, 0);
        }
    }
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    uint8_t *p = readTarget(RD_TARGET_DISPLAY, w, h);
    CHECK(rhi_vk_validation_error_count() == 0, "reduction snap: %u validation errors",
          rhi_vk_validation_error_count());
    if (p && (size_t)*w * *h * 4 <= sizeof(keep[0])) {
        which ^= 1;
        memcpy(keep[which], p, (size_t)*w * *h * 4);
        p = keep[which];
    } else {
        p = NULL;
    }
    rd_shutdown();
    return p;
}

static void checkReductionSnapAt(float scale)
{
    const int whole = scale == floorf(scale);
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    const uint8_t *a = snapRun(scale, 0, &w, &h);
    if (!a) {
        CHECK(0, "reduction snap %g: readback", (double)scale);
        return;
    }
    static uint8_t plain[2880 * 1440 * 4];
    memcpy(plain, a, (size_t)w * h * 4);
    const uint8_t *m = snapRun(scale, 1, &w2, &h2);
    if (!m || w2 != w || h2 != h) {
        CHECK(0, "reduction snap %g: mirrored readback", (double)scale);
        return;
    }
    const uint32_t pad = (uint32_t)(6.0f * scale) + 1;
    /* the plain sample is x + b, the mirrored one x - b, so the mirrored
     * texel x is the plain texel x - 2b (b = s / 4, a whole texel unless s
     * is whole: 2x keeps the PS2 blend, b = 0.5) */
    const uint32_t shift =
        whole ? (uint32_t)(scale / 2.0f + 0.5f) : 2u * (uint32_t)(scale / 4.0f + 0.5f);
    uint32_t mixed = 0, mixedM = 0, lit = 0, differ = 0;
    for (uint32_t y = h / 4; y < h / 4 + 8; y++) {
        for (uint32_t x = pad; x + pad < w; x++) {
            const uint8_t v = plain[((size_t)y * w + x) * 4];
            const uint8_t vm = m[((size_t)y * w + x) * 4];
            mixed += v != 0 && v != 255;
            mixedM += vm != 0 && vm != 255;
            lit += v == 255;
            differ += vm != plain[((size_t)y * w + (x - shift)) * 4];
        }
    }
    CHECK(lit > 0, "reduction snap %g: the columns are drawn", (double)scale);
    if (!whole || scale >= 4.0f) {
        CHECK(mixed == 0, "reduction snap %g: %u blended texels (plain)", (double)scale, mixed);
        CHECK(mixedM == 0, "reduction snap %g: %u blended texels (mirrored)", (double)scale,
              mixedM);
    }
    CHECK(differ == 0,
          "reduction snap %g: the mirrored run is not the plain one shifted (%u texels)",
          (double)scale, differ);
}

static void checkReductionSnap(void)
{
    static const float scales[] = {2.8125f, 3.75f, 2.0f, 4.0f, 5.625f};
    for (size_t i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
        checkReductionSnapAt(scales[i]);
    }
}

/* A draw that samples a render target clamps at its edges with the option
 * on: SCENE blue with red from column 8 on, drawn into WORK0 (256 x 128)
 * through a sprite whose u starts at -8, so WORK0's columns 0..7 read
 * SCENE's wrapped right side (red) with the option off and the clamped blue
 * column 0 with it on.  The game leaves such screen-space lookups on REPEAT
 * and the PS2's border hid where they wrapped. */
static void checkFullPixelTargetWrap(void)
{
    static const uint8_t red[4] = {255, 0, 0, 0x80}, blue[4] = {0, 0, 255, 0x80},
                         white[4] = {0x80, 0x80, 0x80, 0x80};
    for (int full = 0; full < 2; full++) {
        RdSettings s = originalSettings();
        s.fullPixel = (uint8_t)full;
        if (!rd_init(512, 512, &s, NULL)) {
            return;
        }
        rd_begin_frame();
        rd_select_list(0);
        rd_clear_target(rd_target(RD_TARGET_SCENE), blue, 1, 0);
        rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
        opaque2D();
        rd_texture_off();
        sprite(RD_SPACE_WORLD, 8 * 16, 0, 512 * 16, 512 * 16, red, 0, 0, 0, 0);
        /* WORK0's pixel 0 sits at GS 1920, 1984: 128 and 192 past the
           helper's 512-target origin; u = -8 .. 248 texels, in sixteenths, over
           its 256 columns */
        rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
        rd_texture(rd_target_texture(rd_target(RD_TARGET_SCENE), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
                   RD_TCC_RGBA);
        rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
        sprite(RD_SPACE_WORLD, 128 * 16, 192 * 16, (128 + 256) * 16, (192 + 128) * 16, white,
               -8 * 16, 0, 248 * 16, 128 * 16);
        rd_end_frame(0);
        uint32_t w = 0, h = 0;
        uint8_t *p = readTarget(RD_TARGET_WORK0, &w, &h);
        if (p && w >= 256 && h >= 128) {
            const uint8_t *c = &p[((size_t)(h / 2) * w + 2) * 4];
            const uint8_t *in = &p[((size_t)(h / 2) * w + 64) * 4];
            CHECK(in[0] >= 250 && in[2] <= 2, "target wrap %s: the inside reads red (%u %u %u)",
                  full ? "on" : "off", in[0], in[1], in[2]);
            if (full) {
                CHECK(c[2] >= 250 && c[0] <= 2,
                      "target wrap on: the edge clamps to SCENE's own column (%u %u %u)", c[0],
                      c[1], c[2]);
            } else {
                CHECK(c[0] >= 250 && c[2] <= 2,
                      "target wrap off: the edge wraps to the far side as the GS does (%u %u %u)",
                      c[0], c[1], c[2]);
            }
        } else {
            CHECK(0, "target wrap: WORK0 readback (%ux%u)", w, h);
        }
        CHECK(rhi_vk_validation_error_count() == 0, "target wrap: %u validation errors",
              rhi_vk_validation_error_count());
        rd_shutdown();
    }
}

/* the reduction's edges read their own colour, not the opposite edge's: SCENE
 * red with a blue column 0 and a blue row 0.  With the option on DISPLAY's
 * last column and last row hold no blue (a lost clamp wraps column 0 and row
 * 0 into them; on the scaled target and with full height the last row's
 * bilinear taps reach past the scene's rows, where only the clamp matters),
 * and with the mirror on the left column, which then samples past the other
 * edge, is red.  The plain 1x case also keeps the exact value of column 0.
 * Off as the game has it (the border mask) on this 512-wide frame, which is
 * its exact width (nothing cropped: rd_present.c rd__frame_cropped), DISPLAY
 * is the zeroed RdSettings' byte for byte: the GS's own border, as v0.7.4
 * and the PS2 have it. */
static bool sameAs(const uint8_t *p, uint32_t w, uint32_t h, const uint8_t *ref, uint32_t rw,
                   uint32_t rh)
{
    return ref && w == rw && h == rh && memcmp(p, ref, (size_t)w * h * 4) == 0;
}

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
        uint8_t *ref = NULL;
        uint32_t rw = 0, rh = 0;
        for (int full = 0; full < 3; full++) {
            const char *nm = cases[k].name;
            uint32_t w = 0, h = 0;
            uint8_t *p =
                clampRun(full, cases[k].scale, cases[k].fullHeight, cases[k].mirror, 0, &w, &h);
            if (!p) {
                CHECK(0, "full pixel clamp %s: DISPLAY readback (%ux%u)", nm, w, h);
                continue;
            }
            if (full == 2) {
                CHECK(
                    sameAs(p, w, h, ref, rw, rh),
                    "full pixel clamp %s: off (border mask): DISPLAY is the GS's own border's byte "
                    "for byte",
                    nm);
                continue;
            }
            if (full == 0) {
                free(ref);
                ref = (uint8_t *)malloc((size_t)w * h * 4);
                if (ref) {
                    memcpy(ref, p, (size_t)w * h * 4);
                    rw = w;
                    rh = h;
                }
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
                    /* the mirror flips at the present, not in DISPLAY: the
                       mirrored reduction samples column 0 at u = 0.25, which
                       the clamp keeps on its own blue texel (the wrap would
                       blend the red column 511 in) */
                    CHECK(l[2] >= 250 && l[0] <= 2,
                          "full pixel clamp %s: the left column is its own blue (%u %u %u)", nm,
                          l[0], l[1], l[2]);
                    CHECK(NOBLUE(r), "full pixel clamp %s: the last column is red (%u %u %u)", nm,
                          r[0], r[1], r[2]);
                    continue;
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
        free(ref);
    }
}

/* the layout's full-width bands (the pause and End Game bars, GS pixels 1
 * to 511 of 512, full-frame tagged) reach the frame's edge with the option
 * on: the strip it shows beside the bar is the bar, not a column of the
 * scene under it.  Rows outside the band keep the scene's red at the edge,
 * and with the option off the border stays black as it was.  The same band
 * UI-tagged is not extended: the tag is the rule, nothing is detected from
 * the pixels.  Off as the game has it (the border mask) on this 512-wide
 * frame, DISPLAY is the zeroed RdSettings' byte for byte (sameAs). */
static void checkFullPixelBands(void)
{
    static const struct {
        float scale;
        int mirror;
        const char *name;
    } cases[] = {{1.0f, 0, "1x"}, {2.0f, 0, "2x"}, {1.0f, 1, "1x mirror"}, {2.0f, 1, "2x mirror"}};

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        uint8_t *ref = NULL;
        uint32_t rw = 0, rh = 0;
        for (int full = 0; full < 3; full++) {
            const char *nm = cases[k].name;
            uint32_t w = 0, h = 0;
            uint8_t *p = clampRun(full, cases[k].scale, 0, cases[k].mirror, 1, &w, &h);
            if (!p) {
                CHECK(0, "full pixel bands %s: DISPLAY readback (%ux%u)", nm, w, h);
                continue;
            }
            if (full == 2) {
                CHECK(
                    sameAs(p, w, h, ref, rw, rh),
                    "full pixel bands %s: off (border mask): DISPLAY is the GS's own border's byte "
                    "for byte",
                    nm);
                continue;
            }
            if (full == 0) {
                free(ref);
                ref = (uint8_t *)malloc((size_t)w * h * 4);
                if (ref) {
                    memcpy(ref, p, (size_t)w * h * 4);
                    rw = w;
                    rh = h;
                }
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
        free(ref);
    }
    uint32_t w = 0, h = 0;
    uint8_t *p = clampRun(1, 1.0f, 0, 0, 2, &w, &h);
    if (!p) {
        CHECK(0, "full pixel bands UI-tagged: DISPLAY readback (%ux%u)", w, h);
        return;
    }
    const uint8_t *l = &p[((size_t)(h / 2) * w) * 4], *m = &p[((size_t)(h / 2) * w + w / 2) * 4];
    CHECK(m[0] < 8 && !(l[0] < 8 && l[1] < 8 && l[2] < 8),
          "full pixel bands: a UI-tagged band is not extended (edge %u %u %u)", l[0], l[1], l[2]);
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

/* Full pixel off (the game's: RdSettings.borderMask) on a cropped frame
 * (16:9 and the odd sizes; checkBorderOwn at 4:3 and 48:9): Full pixel on's
 * picture, the same box, scale and crop, with the game's border masked
 * over its edge at the picture's own scale, where the PS2's reduction
 * scissor held it black (v0.7.4 shrank the whole frame into a box of its
 * own shape instead).  A red frame through the reduction at
 * gsH lines (448: 60 Hz, 2 of DISPLAY's 224 rows a side; 512: 50 Hz, 8 of
 * 256), the Enhanced window resolution, presented into outW x outH off
 * and on.  The black run from each side of the output at the centre row
 * and column must be the box's margin plus the border, 2 columns of the
 * frame's exact width at the box's scale and the crop's rows of the box's
 * height, within a pixel (measured on the picture: on is red to the box's
 * edges); inside the mask the present is on's byte for byte, the bands
 * black (checkMaskAgainst); and DISPLAY, the picture the F12 dump saves,
 * is on's byte for byte: the mask is the presenter's alone. */
static bool borderRun(float aspect, uint32_t outW, uint32_t outH, uint32_t gsH, int full,
                      uint8_t *out, uint8_t *disp, size_t cap, uint32_t *dw, uint32_t *dh)
{
    RdSettings s = originalSettings();
    s.preset = RD_PRESET_ENHANCED;
    s.aspect = aspect;
    s.sceneScale = 0.0f;
    s.outputWidth = outW;
    s.outputHeight = outH;
    s.fullPixel = (uint8_t)(full == 1);
    s.borderMask = (uint8_t)(full == 2);
    const uint32_t FW = (uint32_t)ico_frame_width_for(aspect);
    if (!rd_init(FW, gsH, &s, NULL)) {
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
    uint32_t ow = 0, oh = 0;
    const bool ok = rd__read_present(out, (size_t)outW * outH * 4, &ow, &oh) && ow == outW &&
                    oh == outH && rd__read_target(rd_target(RD_TARGET_DISPLAY), disp, cap, dw, dh);
    CHECK(rhi_vk_validation_error_count() == 0, "border: %u validation errors",
          rhi_vk_validation_error_count());
    noteMask();
    rd_shutdown();
    return ok;
}

static void checkBorderOff(const char *name, float aspect, uint32_t outW, uint32_t outH,
                           uint32_t gsH)
{
    const size_t cap = (size_t)4096 * 2304 * 4;
    uint8_t *out = (uint8_t *)malloc((size_t)outW * outH * 4);
    uint8_t *on = (uint8_t *)malloc((size_t)outW * outH * 4);
    uint8_t *disp = (uint8_t *)malloc(cap);
    uint8_t *dispOn = (uint8_t *)malloc(cap);
    uint32_t dw = 0, dh = 0, dwOn = 0, dhOn = 0;
    bool ok = out && on && disp && dispOn &&
              borderRun(aspect, outW, outH, gsH, 2, out, disp, cap, &dw, &dh);
    const MaskInfo m = s_mask;
    ok = ok && borderRun(aspect, outW, outH, gsH, 1, on, dispOn, cap, &dwOn, &dhOn);
    if (!ok) {
        CHECK(0, "%s: readback", name);
        free(out);
        free(on);
        free(disp);
        free(dispOn);
        return;
    }
    /* the box: on's, the frame's exact width (the 4:3 integer box at 512) */
    const RhiRect b = m.box;
#define PXB(x, y) (&out[((size_t)(y) * outW + (x)) * 4])
#define DARK(q) ((q)[0] < 128)
    const uint32_t cx = outW / 2, cy = outH / 2;
    uint32_t l = 0, r = 0, t = 0, bo = 0;
    while (l < outW && DARK(PXB(l, cy))) {
        l++;
    }
    while (r < outW && DARK(PXB(outW - 1 - r, cy))) {
        r++;
    }
    while (t < outH && DARK(PXB(cx, t))) {
        t++;
    }
    while (bo < outH && DARK(PXB(cx, outH - 1 - bo))) {
        bo++;
    }
    const float crop = gsH >= 512u ? 8.0f : 2.0f;
    const float side = (float)b.x + 2.0f * (float)b.w / m.we;
    const float top = (float)b.y + (float)b.h * crop / ((float)gsH * 0.5f);
    printf("%s: off: box %d,%d %ux%u; black %u left, %u right, %u top, %u bottom (the border "
           "%.2f a side, %.2f top and bottom)\n",
           name, b.x, b.y, b.w, b.h, l, r, t, bo, (double)side, (double)top);
    CHECK(fabsf((float)l - side) <= 1.0f && fabsf((float)r - side) <= 1.0f,
          "%s: off: the side borders are the box's margin and 2 of %.2f columns (%u, %u; %.2f)",
          name, (double)m.we, l, r, (double)side);
    CHECK(fabsf((float)t - top) <= 1.0f && fabsf((float)bo - top) <= 1.0f,
          "%s: off: the top and bottom borders are the box's margin and %g of %u rows (%u, %u; "
          "%.2f)",
          name, (double)crop, gsH / 2u, t, bo, (double)top);
#undef PXB
#undef DARK
    checkMaskAgainst(name, out, on, outW, outH, &m);
    CHECK(dw == dwOn && dh == dhOn && memcmp(disp, dispOn, (size_t)dw * dh * 4) == 0,
          "%s: off: DISPLAY is on's byte for byte, no border in it (%ux%u, %ux%u)", name, dw, dh,
          dwOn, dhOn);
    free(out);
    free(on);
    free(disp);
    free(dispOn);
}

/* Full pixel off where the frame is its exact width (4:3 at 512, 48:9 at
 * 2048): nothing is cropped, so the game's own border is in the picture and
 * the game's off (the host's borderMask) is v0.7.4's and the PS2's: the
 * reduction keeps its scissor, no mask is drawn (rd_present.c
 * rd__frame_cropped), the present is the zeroed RdSettings' byte for byte.
 * v0.7.4's checks: the black run from each side of the output at the centre
 * row and column is the box's margin plus the reduction's 2 columns of the
 * frame's W and 2 rows of 224 (in DISPLAY's whole rows, doubled), within a
 * pixel; and the box is DISPLAY one texel an output pixel, line-doubled. */
static void checkBorderOwn(const char *name, float aspect, uint32_t outW, uint32_t outH)
{
    const size_t cap = (size_t)4096 * 2304 * 4;
    uint8_t *out = (uint8_t *)malloc((size_t)outW * outH * 4);
    uint8_t *gs = (uint8_t *)malloc((size_t)outW * outH * 4);
    uint8_t *disp = (uint8_t *)malloc(cap);
    uint8_t *dispGs = (uint8_t *)malloc(cap);
    uint32_t dw = 0, dh = 0, dwGs = 0, dhGs = 0;
    bool ok = out && gs && disp && dispGs &&
              borderRun(aspect, outW, outH, 448, 2, out, disp, cap, &dw, &dh);
    const MaskInfo m = s_mask;
    ok = ok && borderRun(aspect, outW, outH, 448, 0, gs, dispGs, cap, &dwGs, &dhGs);
    if (!ok) {
        CHECK(0, "%s: readback", name);
        free(out);
        free(gs);
        free(disp);
        free(dispGs);
        return;
    }
    CHECK(!m.on, "%s: off: no border mask where the frame is its exact width", name);
    CHECK(dw == dwGs && dh == dhGs && memcmp(disp, dispGs, (size_t)dw * dh * 4) == 0 &&
              memcmp(out, gs, (size_t)outW * outH * 4) == 0,
          "%s: off: DISPLAY and the present are the GS's own border's byte for byte", name);
    const uint32_t FW = (uint32_t)ico_frame_width_for(aspect);
    /* the box: the whole frame, FW / 384 (the 4:3 integer box at 512) */
    RhiRect b;
    rd__present_box(outW, outH, FW == 512u ? 4.0f / 3.0f : (float)FW / 384.0f, &b);
#define PXB(x, y) (&out[((size_t)(y) * outW + (x)) * 4])
#define DARK(q) ((q)[0] < 128)
    const uint32_t cx = outW / 2, cy = outH / 2;
    uint32_t l = 0, r = 0, t = 0, bo = 0;
    while (l < outW && DARK(PXB(l, cy))) {
        l++;
    }
    while (r < outW && DARK(PXB(outW - 1 - r, cy))) {
        r++;
    }
    while (t < outH && DARK(PXB(cx, t))) {
        t++;
    }
    while (bo < outH && DARK(PXB(cx, outH - 1 - bo))) {
        bo++;
    }
    const float side = (float)b.x + 2.0f * (float)b.w / (float)FW;
    /* the scissor's 2 rows of 224 in DISPLAY's whole rows (b.h / 2 of
       them), each presented twice */
    const float top = (float)b.y + 2.0f * floorf(2.0f * (float)(b.h / 2u) / 224.0f);
    printf("%s: off: box %d,%d %ux%u; black %u left, %u right, %u top, %u bottom (the picture "
           "%u..%u x %u..%u; the border %.2f a side, %.2f top and bottom)\n",
           name, b.x, b.y, b.w, b.h, l, r, t, bo, l, outW - 1 - r, t, outH - 1 - bo, (double)side,
           (double)top);
    CHECK(l > (uint32_t)b.x && r > (uint32_t)b.x && t > (uint32_t)b.y && bo > (uint32_t)b.y,
          "%s: off: a black border inside the box on all four sides (%u %u %u %u)", name, l, r, t,
          bo);
    CHECK(fabsf((float)l - side) <= 1.0f && fabsf((float)r - side) <= 1.0f,
          "%s: off: the side borders are the box's margin and 2 of %u columns (%u, %u; %.2f)", name,
          FW, l, r, (double)side);
    CHECK(fabsf((float)t - top) <= 1.0f && fabsf((float)bo - top) <= 1.0f,
          "%s: off: the top and bottom borders are the box's margin and 2 of 224 rows (%u, %u; "
          "%.2f)",
          name, t, bo, (double)top);
    /* the box is DISPLAY: one texel an output pixel, each row twice */
    uint32_t bad = 0;
    if (dw == b.w && dh * 2u == b.h) {
        for (uint32_t y = 0; y < b.h; y++) {
            bad += memcmp(PXB((uint32_t)b.x, (uint32_t)b.y + y), &disp[(size_t)(y / 2u) * dw * 4],
                          (size_t)b.w * 4) != 0;
        }
    }
    CHECK(dw == b.w && dh * 2u == b.h && bad == 0,
          "%s: off: the box is DISPLAY (%ux%u, the box %ux%u) byte for byte, nothing cropped (%u "
          "rows differ)",
          name, dw, dh, b.w, b.h, bad);
#undef PXB
#undef DARK
    free(out);
    free(gs);
    free(disp);
    free(dispGs);
}

static void checkBordersOff(void)
{
    /* the reporter's 1920 x 1080 at 16:9: on's 1920 x 1080 box, 2 columns
     * of 682.67 at 2.8125 a column are 5.6 pixels, 2 rows of 224 are 9.6 */
    checkBorderOff("border 1920x1080 16:9", 16.0f / 9.0f, 1920, 1080, 448);
    checkBorderOff("border 2560x1440 16:9", 16.0f / 9.0f, 2560, 1440, 448);
    checkBorderOff("border 3840x2160 16:9", 16.0f / 9.0f, 3840, 2160, 448);
    checkBorderOff("border 2341x1080 odd", 2341.0f / 1080.0f, 2341, 1080, 448);
    checkBorderOff("border 1366x768 odd", 1366.0f / 768.0f, 1366, 768, 448);
    /* 50 Hz: 8 of 256 rows, 33.75 pixels at 1080 */
    checkBorderOff("border 1920x1080 16:9 50 Hz", 16.0f / 9.0f, 1920, 1080, 512);
    checkBorderOff("border 3840x2160 16:9 50 Hz", 16.0f / 9.0f, 3840, 2160, 512);
    /* the frame its exact width: the GS's own border, v0.7.4's picture */
    checkBorderOwn("border 1440x1080 4:3", 4.0f / 3.0f, 1440, 1080);
    checkBorderOwn("border 1920x1080 4:3 (pillars)", 4.0f / 3.0f, 1920, 1080);
    checkBorderOwn("border 3840x720 48:9", 48.0f / 9.0f, 3840, 720);
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

/* The far railings.  A 64 x 64 lattice (wires every 4th
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
        CHECK(!s_llvmpipe ||
                  (h.exact == RD_BLEND_EXACT_ATTACHMENT ? h.present == GOLD_PRESENT_EXACT
                                                        : h.exact || h.present == GOLD_PRESENT),
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

/* issue 87: a ring that cannot grow (a full upload heap) keeps the old
   one, so the next frame that fits still draws; the frame that needed more
   is refused; a growth to the doubled size that is refused tries the size
   the frame needs */
static void checkRingGrowth(void)
{
    RdSettings s = originalSettings();
    if (!rd_init(512, 512, &s, NULL)) {
        CHECK(0, "ring: rd_init");
        return;
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        CHECK(rd__begin_own_frame(1u << 20), "ring: a 1 MB frame on slot %d", i);
    }
    const uint32_t slot = g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT;
    const RhiBuffer old = g_rd.ring[slot];
    const uint64_t c = g_rd.ringCap[slot];
    CHECK(old.id && c >= (1u << 20) && c % (1u << 20) == 0, "ring: slot %u has %llu bytes", slot,
          (unsigned long long)c);
    g_rdRingFailOver = c;
    CHECK(!rd__begin_own_frame(c + 1), "ring: a frame needing more than a full heap allows");
    CHECK(g_rd.ring[slot].id == old.id && g_rd.ringCap[slot] == c && g_rd.ringMap[slot] != NULL,
          "ring: slot %u kept its %llu bytes after a refused growth (now %llu)", slot,
          (unsigned long long)c, (unsigned long long)g_rd.ringCap[slot]);
    CHECK(rd__begin_own_frame(1u << 20), "ring: the other slot");
    CHECK(rd__begin_own_frame(c), "ring: a frame that fits the kept ring draws");
    CHECK(g_rd.ring[slot].id == old.id, "ring: the kept ring is the one in use");
    /* doubling twice (4c) refused, the frame's own size (2c + 1 MB) made */
    g_rdRingFailOver = 3 * c;
    CHECK(rd__begin_own_frame(1u << 20), "ring: the other slot again");
    CHECK(rd__begin_own_frame(2 * c + (1u << 20)), "ring: the frame's own size");
    CHECK(g_rd.ringCap[slot] == 2 * c + (1u << 20) && g_rd.ring[slot].id != old.id,
          "ring: slot %u grew to %llu bytes (want %llu)", slot,
          (unsigned long long)g_rd.ringCap[slot], (unsigned long long)(2 * c + (1u << 20)));
    g_rdRingFailOver = 0;
    CHECK(rd__begin_own_frame(1u << 20), "ring: the other slot, last");
    CHECK(rd__begin_own_frame(4 * c), "ring: growth with nothing refused");
    CHECK(g_rd.ringCap[slot] >= 4 * c, "ring: slot %u grew to %llu bytes", slot,
          (unsigned long long)g_rd.ringCap[slot]);
    rhi_wait_idle();
    rd_shutdown();
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
    checkUiBox();
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
    checkSsaa();
    checkAaOffReduce();
    checkFullHeight1x();
    checkSceneLimit();
    checkWide169();
    checkPresent1to1();
    checkExactWindows();
    checkFullPixelOnPins();
    checkBordersOff();
    checkPresentInfo();
    checkFullPixel();
    checkFullPixelSquare();
    checkFullPixelSharp(960, 720);
    checkFullPixelSharp(1440, 1080);
    checkFullPixelClamp();
    checkFullPixelTargetWrap();
    checkReductionSnap();
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
    checkRingGrowth();
    if (failures) {
        printf("rd_present_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_present_test: ok\n");
    return 0;
}

#endif /* RD_PRESENT_BASELINE */
