/* rd_tex_test.c: the texture cache and Texture.c on rd.
 *
 * Texture.c, GifPacket.c, DisplayList.c and DmaPacket.c compiled as the
 * window build compiles them (ICO_HOST, ICO_RD), fed synthetic TIM2 files
 * built here (no disc data):
 *
 *   decode   PSMT4 and PSMT8 with 32-bit and 16-bit CLUTs, each with the
 *            CLUT in CSM1 order and in index order (TIM2 ClutType bit 7,
 *            which tex_convertClutCSM2ToCSM1 rearranges), PSMCT16 (alpha
 *            bit kept; the three TEXA modes applied on the CPU against a
 *            reference), PSMCT24, PSMCT32 with and without the ICO block, a
 *            size that is not a power of two (zero padding), and the rd_tex
 *            decoder's PSMT8H/PSMT4HL/PSMT4HH directly: every texel equals
 *            a reference written independently here;
 *   sampler  the ICO block's SMPMAG/SMPMIN reach the entry, the defaults
 *            (linear / GlobalStageSetting.texSampleMode) otherwise;
 *   scroll   a CLUT scroll (tex_ResetVram -> tex_textureAnimation) bumps
 *            the generation of that texture only and re-expands it into the
 *            same rd texture;
 *   cache    rd_tex keys: hit, generation miss, a baked TEXA variant as a
 *            separate entry, in-place update, a new size retired and
 *            destroyed two frame ticks later, drop;
 *   reload   (issue 174) tex_Init hands the next stage the old stage's
 *            slots: a load into a slot whose picture is cached gives the
 *            new picture an rd texture of its own, even at the same size
 *            and format, and the old texture keeps its texels for the
 *            frames that drew it until two frame ticks later; a CLUT
 *            scroll still re-expands in place (scroll above);
 *   resolve  tex_TransTexture's packet decodes to TEX1/TEST/TEX0 with the
 *            cached texture bound, and no register is left undecoded;
 *   pixels   (Vulkan, lavapipe in the container) one PSMT8 sprite and one
 *            PSMCT16 sprite under TEXA 7F/81+AEM, drawn through
 *            tex_TransTexture + gif_SpriteSensitiveOrg, give the exact
 *            texels in SCENE;
 *   r8       rd_create_texture_r8 keeps w * h bytes and the
 *            format; rd_update_texture_rect writes the rectangle alone,
 *            clipped, counts only updates that change texels and keeps the
 *            union of the rectangles as the dirty one; an RGBA8 texture
 *            takes rectangles too.  On the device the texture reads back
 *            as created, and after two rectangle updates the GPU copy
 *            changed inside their union only (texels changed in the CPU
 *            copy outside it without an update stay as uploaded before).
 *            rd_create_texture_sheet keeps w * h bytes, the
 *            SHEET format (R8 on the device) and its style; rectangles as
 *            R8; rd_set_texture_sheet_style changes a sheet's style alone.
 *   packs    (texture packs) rdtex_create_replacement moves the image into
 *            the texture's pending upload with the box chain for an RGBA8
 *            image without mips, rdtex_replacement_mips on a size that is
 *            not a power of two; a replacement's chain is the
 *            alpha-weighted box chain with no alpha coverage kept (a
 *            lattice's level 1 alpha is the plain average, its colour the
 *            wires'); BC refused without a device;
 *            rdtex_replace installs it on the entry (a stale generation
 *            refused), a new store, rdtex_revert_replacements and
 *            rdtex_drop each give it up through the release hook, the old
 *            textures are destroyed two ticks later, rd_update_texture
 *            leaves a replacement alone.  On the device a 2x RGBA8
 *            replacement of the PSMT8 sprite's texture (uvW/uvH the GS
 *            16x16) and a BC3 one of the PSMCT16 sprite's are drawn
 *            through the same packets: the sprites show the replacements'
 *            texels at the same place; a 1024x1024 replacement (over the
 *            ring's 4 MB, its own upload buffer) reads back exactly.
 *   thread   (not OpenGL) the frame replayed on the renderer's own thread
 *            while this thread records and updates (threadPixels: the
 *            same SCENE); in sync mode a picture is waited for and not
 *            counted as handed over, with the thread on it is until the
 *            drain (threadSync).  The slot ahead (threadSlotAhead): with
 *            the render thread held in its wait for the next picture's
 *            frame slot, this thread's rd_update_texture and rdtex_find
 *            do not wait; the next picture takes that slot (no wait inside
 *            its reads), SCENE the same; as the device has it, with the
 *            render thread submitting, and with ICO_RD_SLOT_AHEAD=0 (every
 *            picture inside).  Fairness (threadFair): a call that waits
 *            for picture 1's reads (its walk held open) runs before
 *            picture 2's reads begin, one wait of one picture's reads,
 *            counted in the walk.  The display's wait ahead
 *            (threadRoomAhead, the render thread submitting, the backend's
 *            wait for room in the present queue replaced by a seam): a
 *            picture handed over that presented ends by making the next
 *            picture's wait; held there, this thread's rd_update_texture
 *            and rdtex_find do not wait; the next picture's acquire takes
 *            it (none inside its reads); a picture after one whose wait
 *            was not made waits inside its reads (counted); SCENE the
 *            same; with ICO_RD_ACQUIRE_AHEAD=0 the seam is never asked.
 *            Frame rate Original (threadOriginal, the window's gate set):
 *            the frame rd_end_frame closes is handed over to the render
 *            thread (rd_submit_async, rd_last_present_info at once), the
 *            gate runs on this thread, SCENE and DISPLAY byte for byte as
 *            without the thread, the presents on the submit thread; the
 *            gate leaving out every second frame gives 15 pictures of 30;
 *            ICO_RD_ORIGINAL_ASYNC=0 replays each inside the step.
 *
 * Exit 0, 1 on a mismatch, 77 when there is no device (after the CPU
 * checks passed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ico_thread.h"
#include "rd_internal.h"
#include "rd_submit.h"
#include "rd_submit_thread.h"
#include "rd_tex.h"
#include "texpack.h"
#include "vk/rhi_vk.h"
/* the game's side */
#include "typedef.h"
#include "DisplayList.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "Texture.h"
#include "Tim2.h"
#include "photo_mode.h"

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

/* -------------------------------------------- what the four files import */
int ScreenWidth = 512, ScreenHeight = 512;

float center_X = 2048.0f, center_Y = 2048.0f;

int screenOffsetX, screenOffsetY;

int fbKeep;

void *ios_partition_common;

void *dmaVif;

int systemStatus[12];

StageSetting GlobalStageSetting;

PadState pad[16];

int textures, texregs;

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

void *mallocseki(int size)
{
    return calloc(1, (size_t)size + 16);
}

void freeseki(void *p)
{
    free(p);
}

void malloc_MemCpy(void *dst, void *src, int size)
{
    memcpy(dst, src, (size_t)size);
}

/* the partition a load records (TexExt.partition): 0 resident, which
   tex_Init keeps; a stage's textures are in another, whose slots tex_Init
   hands the next stage (slotReloadChecks) */
static int s_partition;

int malloc_GetPartition(void)
{
    return s_partition;
}

int file_LoadFile(void **adr, char *name, int area)
{
    (void)adr;
    (void)name;
    (void)area;
    return -1;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_PrintfDummy(int x, int y, unsigned int col, const char *fmt, ...)
{
    (void)x;
    (void)y;
    (void)col;
    (void)fmt;
}

void debug_Assert(char *fmt, ...)
{
    printf("debug_Assert %s\n", fmt);
    abort();
}

void debug_DispQW(void *p, int size)
{
    (void)p;
    (void)size;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("debug_assertMessage %s:%d %s\n", file, line, mes);
    abort();
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

/* tex_TransTexture's UV offset packet to the VU state
   (MicroCode.c); the mesh path's test covers it (rd_mesh_test) */
void mc_HostDma(int id, const void *addr, int qwc)
{
    (void)id, (void)addr, (void)qwc;
}

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

/* --------------------------------------------------------------- helpers */

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* GS CSM1 position of CLUT index i (256 entries): bits 3 and 4 swapped,
   written independently of rd_tex's helper */
static uint32_t csm1(uint32_t i)
{
    return (i & ~0x18u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1);
}

/* a 16-bit texel to the RGBA8 the cache holds (A bit as 0/1) */
static void exp16(uint16_t v, uint8_t *o)
{
    o[0] = (uint8_t)((v & 31) << 3);
    o[1] = (uint8_t)(((v >> 5) & 31) << 3);
    o[2] = (uint8_t)(((v >> 10) & 31) << 3);
    o[3] = (uint8_t)(v >> 15);
}

/* TIM2 image types (Texture.c psmTable) and CLUT types */
enum { T2_CT16 = 1, T2_CT24 = 2, T2_CT32 = 3, T2_T4 = 4, T2_T8 = 5 };

enum { C2_16 = 1, C2_32 = 3, C2_LINEAR = 0x80 };

typedef struct Tim2Spec {
    int imageType, clutType;
    int w, h;
    const void *image; /* w x h texels in the format */
    size_t imageSize;
    const void *clut; /* as stored in the file */
    size_t clutSize;
    int colors;
    const Tim2Ext *ico; /* the ICO block, or null */
} Tim2Spec;

/* a TIM2 file in a new buffer: file header, picture header, [ICO block],
   image, CLUT; the buffer has room behind it */
static void *makeTim2(const Tim2Spec *s)
{
    size_t hdr = 0x30 + (s->ico ? 0x40 : 0);
    size_t total = 16 + hdr + s->imageSize + s->clutSize;
    unsigned char *f = calloc(1, total + 64);
    Tim2Picture pic;

    memcpy(f, "TIM2", 4);
    f[4] = 4;
    f[6] = 1;
    memset(&pic, 0, sizeof(pic));
    pic.totalSize = (unsigned int)(hdr + s->imageSize + s->clutSize);
    pic.clutSize = (unsigned int)s->clutSize;
    pic.imageSize = (unsigned int)s->imageSize;
    pic.headerSize = (unsigned short)hdr;
    pic.clutColors = (unsigned short)s->colors;
    pic.mipMapTextures = 1;
    pic.clutType = (unsigned char)s->clutType;
    pic.imageType = (unsigned char)s->imageType;
    pic.imageWidth = (unsigned short)s->w;
    pic.imageHeight = (unsigned short)s->h;
    memcpy(f + 16, &pic, sizeof(pic));
    if (s->ico) {
        memcpy(f + 16 + 0x30, s->ico, 0x40);
    }
    memcpy(f + 16 + hdr, s->image, s->imageSize);
    if (s->clutSize) {
        memcpy(f + 16 + hdr + s->imageSize, s->clut, s->clutSize);
    }
    return f;
}

static const RdTexRec *texRec(int id)
{
    return rd__tex_rec(tex_HostTextureId(id));
}

/* every texel of texture id against ref (w x h RGBA8), the padding zero */
static void checkTexels(const char *what, int id, const uint8_t *ref, int w, int h, int padW,
                        int padH, RdTexSrc src)
{
    const RdTexRec *t = texRec(id);
    int bad = 0;

    CHECK(t != NULL, "%s: no rd texture", what);
    if (!t) {
        return;
    }
    CHECK((int)t->w == padW && (int)t->h == padH, "%s: size %ux%u, expected %dx%d", what, t->w,
          t->h, padW, padH);
    CHECK(t->src == src, "%s: source format %u, expected %d", what, t->src, src);
    if ((int)t->w != padW || (int)t->h != padH) {
        return;
    }
    for (int y = 0; y < padH; y++) {
        for (int x = 0; x < padW; x++) {
            const uint8_t *g = &t->pixels[(y * padW + x) * 4];
            uint8_t z[4] = {0, 0, 0, 0};
            const uint8_t *e = (x < w && y < h) ? &ref[(y * w + x) * 4] : z;

            if (memcmp(g, e, 4) != 0) {
                if (bad++ < 4) {
                    printf("  %s texel %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u\n", what, x, y, g[0],
                           g[1], g[2], g[3], e[0], e[1], e[2], e[3]);
                }
            }
        }
    }
    CHECK(bad == 0, "%s: %d texels differ", what, bad);
}

/* ---------------------------------------------------------- the textures */

/* the logical 256-entry palette (RGBA, alpha 0..0x80 and a few above) */
static uint8_t pal32[256][4];

static uint16_t pal16[256];

static void makePalettes(void)
{
    for (int i = 0; i < 256; i++) {
        uint32_t h = hash((uint32_t)i * 3 + 1);
        pal32[i][0] = (uint8_t)h;
        pal32[i][1] = (uint8_t)(h >> 8);
        pal32[i][2] = (uint8_t)(h >> 16);
        pal32[i][3] = (uint8_t)((h >> 24) % 0x90);
        pal16[i] = (uint16_t)(hash((uint32_t)i + 999) & 0xFFFF);
    }
}

typedef struct Made {
    int id;
    int w, h, padW, padH;
    uint8_t *ref;
    RdTexSrc src;
} Made;

/* an indexed texture: bits 4 or 8, CLUT 32 or 16 bit, CSM1 or linear */
static Made makeIndexed(const char *name, int bits, int clut16, int linear, int w, int h,
                        const Tim2Ext *ico)
{
    Made m;
    int colors = bits == 4 ? 16 : 256;
    int esz = clut16 ? 2 : 4;
    size_t isz = bits == 4 ? (size_t)w * h / 2 : (size_t)w * h;
    uint8_t *img = calloc(1, isz);
    uint8_t *clut = calloc((size_t)colors, (size_t)esz);
    Tim2Spec s;

    m.w = w;
    m.h = h;
    m.padW = w;
    m.padH = h;
    m.ref = malloc((size_t)w * h * 4);
    m.src = clut16 ? RD_TEXSRC_RGBA16 : RD_TEXSRC_RGBA32;
    for (int i = 0; i < colors; i++) {
        /* the file position of logical entry i */
        int pos = (colors == 256 && !linear) ? (int)csm1((uint32_t)i) : i;
        if (clut16) {
            memcpy(clut + pos * 2, &pal16[i], 2);
        } else {
            memcpy(clut + pos * 4, pal32[i], 4);
        }
    }
    for (int n = 0; n < w * h; n++) {
        int idx = (int)(hash((uint32_t)n * 7 + (uint32_t)bits + (uint32_t)w) % (uint32_t)colors);
        if (bits == 4) {
            img[n / 2] |= (uint8_t)(idx << ((n & 1) * 4));
        } else {
            img[n] = (uint8_t)idx;
        }
        if (clut16) {
            exp16(pal16[idx], &m.ref[n * 4]);
        } else {
            memcpy(&m.ref[n * 4], pal32[idx], 4);
        }
    }
    memset(&s, 0, sizeof(s));
    s.imageType = bits == 4 ? T2_T4 : T2_T8;
    s.clutType = (clut16 ? C2_16 : C2_32) | (linear ? C2_LINEAR : 0);
    s.w = w;
    s.h = h;
    s.image = img;
    s.imageSize = isz;
    s.clut = clut;
    s.clutSize = (size_t)colors * esz;
    s.colors = colors;
    s.ico = ico;
    m.id = tex_InitTexture((char *)name, makeTim2(&s));
    free(img);
    free(clut);
    return m;
}

/* a direct texture: 16, 24 or 32 bits a texel; salt 0 the pattern the
   checks above know, another salt other texels of the same size */
static Made makeDirectSalt(const char *name, int bpp, int w, int h, const Tim2Ext *ico,
                           uint32_t salt)
{
    Made m;
    int bytes = bpp / 8;
    uint8_t *img = calloc((size_t)w * h, (size_t)bytes);
    Tim2Spec s;
    int tw = 1, th = 1;

    while (tw < w) {
        tw <<= 1;
    }
    while (th < h) {
        th <<= 1;
    }
    m.w = w;
    m.h = h;
    m.padW = tw;
    m.padH = th;
    m.ref = malloc((size_t)w * h * 4);
    m.src = bpp == 16 ? RD_TEXSRC_RGBA16 : bpp == 24 ? RD_TEXSRC_RGB24 : RD_TEXSRC_RGBA32;
    for (int n = 0; n < w * h; n++) {
        uint32_t v = hash((uint32_t)n * 13 + (uint32_t)bpp + salt * 7919u);
        uint8_t *o = &m.ref[n * 4];

        if ((n % 7) == 3) {
            v &= 0xFF000000u; /* black texels, for AEM */
            if (bpp == 16) {
                v = (n & 8) ? 0x8000u : 0;
            }
        }
        if (bpp == 16) {
            uint16_t t = (uint16_t)v;
            memcpy(img + n * 2, &t, 2);
            exp16(t, o);
        } else if (bpp == 24) {
            memcpy(img + n * 3, &v, 3);
            o[0] = (uint8_t)v;
            o[1] = (uint8_t)(v >> 8);
            o[2] = (uint8_t)(v >> 16);
            o[3] = 0;
        } else {
            memcpy(img + n * 4, &v, 4);
            memcpy(o, &v, 4);
        }
    }
    memset(&s, 0, sizeof(s));
    s.imageType = bpp == 16 ? T2_CT16 : bpp == 24 ? T2_CT24 : T2_CT32;
    s.w = w;
    s.h = h;
    s.image = img;
    s.imageSize = (size_t)w * h * bytes;
    s.ico = ico;
    m.id = tex_InitTexture((char *)name, makeTim2(&s));
    free(img);
    return m;
}

static Made makeDirect(const char *name, int bpp, int w, int h, const Tim2Ext *ico)
{
    return makeDirectSalt(name, bpp, w, h, ico, 0);
}

static Tim2Ext icoBlock(int smpMag, int smpMin)
{
    Tim2Ext e;

    memset(&e, 0, sizeof(e));
    memcpy(e.magic, "ICO", 4);
    e.smpMag = smpMag;
    e.smpMin = smpMin;
    e.mipmapK = -165;
    return e;
}

/* GS TEXA on one RGBA16/RGB24 texel, written from the GS manual's rule */
static uint8_t texaRef(const uint8_t *t, RdTexSrc src, RdTexA mode)
{
    uint8_t ta0 = mode == RD_TEXA_7F_81_AEM ? 0x7F : 0x80;
    uint8_t ta1 = mode == RD_TEXA_7F_81_AEM ? 0x81 : 0x80;
    int aem = mode != RD_TEXA_80_80;

    if (aem && t[0] == 0 && t[1] == 0 && t[2] == 0) {
        return 0;
    }
    return (src == RD_TEXSRC_RGBA16 && t[3]) ? ta1 : ta0;
}

static Made s_t4, s_t4lin, s_t8, s_t8lin, s_t8c16, s_t8c16lin, s_t4c16, s_c16, s_c24, s_c32,
    s_c32ico, s_npot, s_scroll;

static void decodeChecks(void)
{
    Tim2Ext nearest = icoBlock(0, 0);
    Tim2Ext scroll = icoBlock(0, 0);

    s_t4 = makeIndexed("t4", 4, 0, 0, 16, 16, NULL);
    s_t4lin = makeIndexed("t4lin", 4, 0, 1, 32, 8, NULL);
    s_t4c16 = makeIndexed("t4c16", 4, 1, 1, 16, 8, NULL);
    s_t8 = makeIndexed("t8", 8, 0, 0, 16, 16, &nearest);
    s_t8lin = makeIndexed("t8lin", 8, 0, 1, 32, 16, NULL);
    s_t8c16 = makeIndexed("t8c16", 8, 1, 0, 16, 16, NULL);
    s_t8c16lin = makeIndexed("t8c16lin", 8, 1, 1, 16, 32, NULL);
    s_c16 = makeDirect("c16", 16, 16, 16, NULL);
    s_c24 = makeDirect("c24", 24, 16, 8, NULL);
    s_c32 = makeDirect("c32", 32, 8, 8, NULL);
    s_c32ico = makeDirect("c32ico", 32, 16, 16, &nearest);
    s_npot = makeDirect("npot", 32, 24, 10, NULL);
    scroll.csBgn = 0;
    scroll.csEnd = 15;
    scroll.csSpd = 1;
    scroll.csStp = 1;
    s_scroll = makeIndexed("scroll", 8, 0, 0, 16, 16, &scroll);

    const Made *all[] = {&s_t4,  &s_t4lin, &s_t4c16, &s_t8,     &s_t8lin, &s_t8c16, &s_t8c16lin,
                         &s_c16, &s_c24,   &s_c32,   &s_c32ico, &s_npot,  &s_scroll};
    const char *names[] = {
        "PSMT4 CSM1",        "PSMT4 index order", "PSMT4 16-bit CLUT",  "PSMT8 CSM1",
        "PSMT8 index order", "PSMT8 16-bit CSM1", "PSMT8 16-bit index", "PSMCT16",
        "PSMCT24",           "PSMCT32",           "PSMCT32 ICO",        "PSMCT32 24x10",
        "PSMT8 scroll"};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        CHECK(all[i]->id >= 0, "%s: tex_InitTexture", names[i]);
        checkTexels(names[i], all[i]->id, all[i]->ref, all[i]->w, all[i]->h, all[i]->padW,
                    all[i]->padH, all[i]->src);
    }

    /* PSMCT16 under the three TEXA modes, on the CPU */
    {
        const RdTexRec *t = texRec(s_c16.id);
        for (int mode = 0; t && mode < RD_TEXA_COUNT; mode++) {
            uint8_t *px = malloc((size_t)t->w * t->h * 4);
            int bad = 0;

            memcpy(px, t->pixels, (size_t)t->w * t->h * 4);
            rdtex_apply_texa(px, (size_t)t->w * t->h, RD_TEXSRC_RGBA16, (RdTexA)mode);
            for (int n = 0; n < s_c16.w * s_c16.h; n++) {
                bad += px[n * 4 + 3] != texaRef(&s_c16.ref[n * 4], RD_TEXSRC_RGBA16, (RdTexA)mode);
            }
            CHECK(bad == 0, "PSMCT16 TEXA mode %d: %d alphas differ", mode, bad);
            free(px);
        }
        t = texRec(s_c24.id);
        for (int mode = 0; t && mode < RD_TEXA_COUNT; mode++) {
            uint8_t *px = malloc((size_t)t->w * t->h * 4);
            int bad = 0;

            memcpy(px, t->pixels, (size_t)t->w * t->h * 4);
            rdtex_apply_texa(px, (size_t)t->w * t->h, RD_TEXSRC_RGB24, (RdTexA)mode);
            for (int n = 0; n < s_c24.w * s_c24.h; n++) {
                bad += px[n * 4 + 3] != texaRef(&s_c24.ref[n * 4], RD_TEXSRC_RGB24, (RdTexA)mode);
            }
            CHECK(bad == 0, "PSMCT24 TEXA mode %d: %d alphas differ", mode, bad);
            free(px);
        }
    }

    /* the rd_tex decoder alone: PSMT8H, PSMT4HL, PSMT4HH read the index from
       the top byte of a 32-bit texel */
    {
        static const uint32_t psms[3] = {RDTEX_PSMT8H, RDTEX_PSMT4HL, RDTEX_PSMT4HH};
        uint8_t img[8 * 4 * 4], out[8 * 4 * 4], clut[256 * 4];

        for (int i = 0; i < (int)sizeof(img); i++) {
            img[i] = (uint8_t)hash((uint32_t)i + 5);
        }
        for (int k = 0; k < 3; k++) {
            RdTexImage im;
            RdTexSrc src;
            int bad = 0;

            /* 256 entries in CSM1 order; 16 entries straight */
            for (int i = 0; i < 256; i++) {
                memcpy(&clut[(k == 0 ? csm1((uint32_t)i) : (uint32_t)i) * 4], pal32[i], 4);
            }
            memset(&im, 0, sizeof(im));
            im.w = 8;
            im.h = 4;
            im.psm = psms[k];
            im.cpsm = RDTEX_PSMCT32;
            im.clutColors = k == 0 ? 256 : 16;
            im.pixels = img;
            im.clut = clut;
            CHECK(rdtex_decode(&im, out, &src) == 0, "decode psm %u", psms[k]);
            for (int n = 0; n < 32; n++) {
                uint8_t b = img[n * 4 + 3];
                int idx = k == 0 ? b : k == 1 ? (b & 15) : (b >> 4);
                bad += memcmp(&out[n * 4], pal32[idx], 4) != 0;
            }
            CHECK(bad == 0, "psm %u: %d texels differ", psms[k], bad);
        }
    }
}

static void samplerChecks(void)
{
    const RdTexSampler *a = rdtex_sampler((uint32_t)s_c32ico.id, RDTEX_TEXA_REPLAY);
    const RdTexSampler *b = rdtex_sampler((uint32_t)s_c32.id, RDTEX_TEXA_REPLAY);

    CHECK(a && a->mag == RD_FILTER_NEAREST && a->min == RD_FILTER_NEAREST,
          "ICO block SMPMAG 0 SMPMIN 0: nearest");
    /* no ICO block: MMAG 1, MMIN GlobalStageSetting.texSampleMode (1) */
    CHECK(b && b->mag == RD_FILTER_LINEAR && b->min == RD_FILTER_LINEAR, "defaults: linear");
}

static void scrollChecks(void)
{
    const RdTexCacheStats *st = rdtex_stats();
    uint32_t before = tex_HostTextureId(s_scroll.id);
    uint32_t other = tex_HostTextureId(s_t8.id);
    uint32_t decodes = st->decodes, creates = st->creates, updates = st->updates;
    uint8_t *ref = malloc((size_t)16 * 16 * 4);

    /* one frame: tex_ResetVram runs the animation (systemStatus[5] == 0) */
    tex_ResetVram();
    CHECK(tex_HostTextureId(s_scroll.id) == before, "scroll: the same rd texture");
    CHECK(tex_HostTextureId(s_t8.id) == other, "scroll: the other texture is untouched");
    CHECK(st->decodes == decodes + 1 && st->updates == updates + 1 && st->creates == creates,
          "scroll: one re-expansion in place (decodes +%u, updates +%u, creates +%u)",
          st->decodes - decodes, st->updates - updates, st->creates - creates);
    /* CS-STP 1 over entries 0..15: entry i takes entry i+1's colour */
    for (int n = 0; n < 16 * 16; n++) {
        int idx = -1;
        for (int i = 0; i < 256; i++) {
            if (memcmp(&s_scroll.ref[n * 4], pal32[i], 4) == 0) {
                idx = i;
                break;
            }
        }
        if (idx >= 0 && idx < 16) {
            idx = (idx + 1) & 15;
        }
        memcpy(&ref[n * 4], idx >= 0 ? pal32[idx] : &s_scroll.ref[n * 4], 4);
    }
    checkTexels("PSMT8 after one CLUT scroll", s_scroll.id, ref, 16, 16, 16, 16, RD_TEXSRC_RGBA32);
    /* a frame with no change of content decodes nothing */
    decodes = st->decodes;
    tex_HostTextureId(s_t8.id);
    tex_HostTextureId(s_c16.id);
    CHECK(st->decodes == decodes, "hits decode nothing");
    free(ref);
}

static void cacheChecks(void)
{
    uint8_t img[4 * 4 * 2], img2[8 * 8 * 2];
    RdTexImage im;
    RdTex a, b, c, d;

    for (int i = 0; i < (int)sizeof(img); i++) {
        img[i] = (uint8_t)hash((uint32_t)i);
    }
    for (int i = 0; i < (int)sizeof(img2); i++) {
        img2[i] = (uint8_t)hash((uint32_t)i + 50);
    }
    memset(&im, 0, sizeof(im));
    im.w = im.h = 4;
    im.psm = RDTEX_PSMCT16;
    im.pixels = img;

    a = rdtex_store(1000, 1, RDTEX_TEXA_REPLAY, &im, NULL, "key");
    CHECK(a.id != 0, "store");
    CHECK(rdtex_find(1000, 1, RDTEX_TEXA_REPLAY).id == a.id, "hit on the same key");
    CHECK(rdtex_find(1000, 2, RDTEX_TEXA_REPLAY).id == 0, "miss on another generation");
    CHECK(rdtex_find(1000, 1, RD_TEXA_80_80_AEM).id == 0, "miss on another TEXA mode");
    b = rdtex_store(1000, 1, RD_TEXA_80_80_AEM, &im, NULL, "key baked");
    CHECK(b.id != 0 && b.id != a.id, "a baked TEXA variant is its own entry");
    {
        const RdTexRec *rb = rd__tex_rec(b.id), *ra = rd__tex_rec(a.id);
        int bad = 0;
        for (int n = 0; ra && rb && n < 16; n++) {
            bad += rb->pixels[n * 4 + 3] !=
                   texaRef(&ra->pixels[n * 4], RD_TEXSRC_RGBA16, RD_TEXA_80_80_AEM);
        }
        CHECK(ra && rb && rb->src == RD_TEXSRC_RGBA32 && bad == 0, "baked TEXA alphas (%d)", bad);
    }
    img[0] ^= 0xFF;
    c = rdtex_store(1000, 2, RDTEX_TEXA_REPLAY, &im, NULL, "key");
    CHECK(c.id == a.id, "a new generation of the same shape updates in place");
    CHECK(rdtex_find(1000, 1, RDTEX_TEXA_REPLAY).id == 0 &&
              rdtex_find(1000, 2, RDTEX_TEXA_REPLAY).id == a.id,
          "the entry holds the new generation");
    im.w = im.h = 8;
    im.pixels = img2;
    d = rdtex_store(1000, 3, RDTEX_TEXA_REPLAY, &im, NULL, "key");
    CHECK(d.id != 0 && d.id != a.id, "a new size is a new texture");
    CHECK(rd__tex_rec(a.id) != NULL, "the old texture lives until two frame ticks");
    rdtex_frame_tick();
    CHECK(rd__tex_rec(a.id) != NULL, "still after one");
    rdtex_frame_tick();
    CHECK(rd__tex_rec(a.id) == NULL, "destroyed after two");
    rdtex_drop(1000);
    CHECK(rdtex_find(1000, 3, RDTEX_TEXA_REPLAY).id == 0 &&
              rdtex_find(1000, 1, RD_TEXA_80_80_AEM).id == 0,
          "dropped");
    rdtex_frame_tick();
    rdtex_frame_tick();
    CHECK(rd__tex_rec(d.id) == NULL && rd__tex_rec(b.id) == NULL, "dropped textures destroyed");
}

/* ------------------------------------------------------- texture packs */

static int s_released;
static RdTex s_lastReleased;

static void onRelease(RdTex t)
{
    s_released++;
    s_lastReleased = t;
}

/* the 2x replacement's picture: texel (x, y) of a 16x16 pattern */
static void repPattern(uint32_t x, uint32_t y, uint8_t *o)
{
    const uint32_t h = hash(y * 16 + x + 4242u);
    o[0] = (uint8_t)h;
    o[1] = (uint8_t)(h >> 8);
    o[2] = (uint8_t)(h >> 16);
    o[3] = 0x80;
}

/* an RGBA8 TexpackImage of w x h, one level, each 2x2 (scale) block one
   pattern texel when scale is 2, else hashed texels */
static TexpackImage repImage(uint32_t w, uint32_t h, uint32_t scale)
{
    TexpackImage img;
    memset(&img, 0, sizeof(img));
    uint8_t *p = malloc((size_t)w * h * 4);
    for (uint32_t y = 0; p && y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *o = p + ((size_t)y * w + x) * 4;
            if (scale) {
                repPattern(x / scale, y / scale, o);
            } else {
                const uint32_t v = hash(y * w + x + 77u);
                memcpy(o, &v, 4);
            }
        }
    }
    img.fmt = RD_TEXEL_RGBA8;
    img.w = w;
    img.h = h;
    img.levels = 1;
    img.lv[0].data = p;
    img.lv[0].w = w;
    img.lv[0].h = h;
    img.lv[0].pitch = w * 4;
    img.lv[0].size = (size_t)w * h * 4;
    img.blob = p;
    img.bytes = img.lv[0].size;
    return img;
}

static void replacementChecks(void)
{
    uint8_t px[8 * 8 * 4];
    RdTexImage im;
    for (int i = 0; i < (int)sizeof(px); i++) {
        px[i] = (uint8_t)hash((uint32_t)i + 300);
    }
    memset(&im, 0, sizeof(im));
    im.w = im.h = 8;
    im.psm = RDTEX_PSMCT32;
    im.pixels = px;
    rdtex_set_release_hook(onRelease);
    s_released = 0;

    const RdTex a = rdtex_store(2000, 1, RDTEX_TEXA_REPLAY, &im, NULL, "orig");
    TexpackImage img = repImage(16, 16, 2);
    const RdTex r = rdtex_create_replacement(&img, 8, 8, "rep");
    const RdTexRec *rr = rd__tex_rec(r.id);
    CHECK(r.id && rr && img.blob == NULL && img.levels == 0, "the image moved into the texture");
    CHECK(rr && rr->replacement && rr->kind == RD_TEXKIND_IMAGE && rr->format == RD_TEXEL_RGBA8 &&
              rr->src == RD_TEXSRC_RGBA32 && rr->pixels == NULL && rr->w == 16 && rr->h == 16 &&
              rr->uvW == 8 && rr->uvH == 8 && rr->dirty && rr->pending &&
              rr->pending->levels == 5 && rr->mipLevels == 5,
          "replacement record (levels %u)", rr && rr->pending ? rr->pending->levels : 0);
    if (rr && rr->pending && rr->pending->levels == 5) {
        /* level 1 of a 2x2-block picture is the picture */
        const TexpackImageLevel *l1 = &rr->pending->lv[1];
        uint8_t want[4];
        repPattern(5, 3, want);
        CHECK(l1->w == 8 && l1->h == 8 && l1->pitch == 32 &&
                  memcmp(l1->data + (3 * 8 + 5) * 4, want, 4) == 0,
              "the box chain's level 1");
    }
    /* a stale generation is refused, the right one installs */
    CHECK(rdtex_replace(2000, 2, RDTEX_TEXA_REPLAY, r) == -1, "stale generation refused");
    CHECK(rdtex_replace(2001, 1, RDTEX_TEXA_REPLAY, r) == -1, "unknown id refused");
    CHECK(rdtex_replace(2000, 1, RDTEX_TEXA_REPLAY, r) == 0, "rdtex_replace");
    CHECK(rdtex_find(2000, 1, RDTEX_TEXA_REPLAY).id == r.id, "the entry binds the replacement");
    CHECK(rdtex_stats()->replaced >= 1 && s_released == 0, "replaced, nothing released yet");
    rd_update_texture(r, px); /* no CPU texels: ignored */
    CHECK(rd__tex_rec(r.id) && rd__tex_rec(r.id)->pixels == NULL, "rd_update_texture leaves it");
    rdtex_frame_tick();
    rdtex_frame_tick();
    CHECK(rd__tex_rec(a.id) == NULL, "the game's texture destroyed two ticks later");

    /* a new generation (a CLUT scroll) of the same shape: a new texture of
       the game's own, the replacement released */
    const RdTex b = rdtex_store(2000, 2, RDTEX_TEXA_REPLAY, &im, NULL, "orig");
    CHECK(b.id && b.id != r.id && s_released == 1 && s_lastReleased.id == r.id,
          "a new store gives the replacement up (%d)", s_released);
    CHECK(rd__tex_rec(b.id) && rd__tex_rec(b.id)->pixels &&
              memcmp(rd__tex_rec(b.id)->pixels, px, 16) == 0,
          "the game's texels again");
    rdtex_frame_tick();
    rdtex_frame_tick();
    CHECK(rd__tex_rec(r.id) == NULL, "the replacement destroyed (pending freed with it)");

    /* revert: the entry forgotten, the next bind misses */
    img = repImage(16, 16, 2);
    const RdTex r2 = rdtex_create_replacement(&img, 8, 8, "rep2");
    CHECK(rdtex_replace(2000, 2, RDTEX_TEXA_REPLAY, r2) == 0, "second replacement");
    rdtex_revert_replacements();
    CHECK(s_released == 2 && s_lastReleased.id == r2.id, "revert releases it");
    CHECK(rdtex_find(2000, 2, RDTEX_TEXA_REPLAY).id == 0, "revert: the next bind decodes again");
    const RdTex c = rdtex_store(2000, 2, RDTEX_TEXA_REPLAY, &im, NULL, "orig");
    CHECK(c.id && rdtex_find(2000, 2, RDTEX_TEXA_REPLAY).id == c.id, "decoded again");

    /* drop */
    img = repImage(16, 16, 2);
    const RdTex r3 = rdtex_create_replacement(&img, 8, 8, "rep3");
    CHECK(rdtex_replace(2000, 2, RDTEX_TEXA_REPLAY, r3) == 0, "third replacement");
    rdtex_drop(2000);
    CHECK(s_released == 3 && s_lastReleased.id == r3.id, "drop releases it");
    for (int i = 0; i < 3; i++) {
        rdtex_frame_tick();
    }
    CHECK(rd__tex_rec(r2.id) == NULL && rd__tex_rec(r3.id) == NULL && rd__tex_rec(b.id) == NULL &&
              rd__tex_rec(c.id) == NULL,
          "everything retired is destroyed");

    /* BC without a device: refused, the image untouched */
    TexpackImage bc;
    memset(&bc, 0, sizeof(bc));
    uint8_t blocks[16];
    memset(blocks, 0, sizeof(blocks));
    bc.fmt = RD_TEXEL_BC1;
    bc.w = bc.h = 4;
    bc.levels = 1;
    bc.lv[0] = (TexpackImageLevel){blocks, 4, 4, 8, 8};
    bc.blob = blocks;
    CHECK(rdtex_create_replacement(&bc, 4, 4, "bc").id == 0 && bc.blob == blocks,
          "BC refused without a device, image untouched");

    /* the chain of a size that is not a power of two: 13x5, 6x2, 3x1, 1x1 */
    img = repImage(13, 5, 0);
    CHECK(rdtex_replacement_mips(&img) == 0 && img.levels == 4 && img.lv[1].w == 6 &&
              img.lv[1].h == 2 && img.lv[2].w == 3 && img.lv[2].h == 1 && img.lv[3].w == 1 &&
              img.lv[3].h == 1 && img.bytes == rdtex_mip_chain_bytes(13, 5) + 13 * 5 * 4,
          "13x5 chain");
    CHECK(rdtex_replacement_mips(&img) == -1, "a second chain refused");
    texpack_free_image(&img);
    /* a lattice (wires every 4th row and column, (200, 180,
     * 160) alpha 0x80; holes black alpha 0): level 1 is the alpha-weighted
     * box chain exactly, with no coverage boost (the boost would raise the
     * 0x20 texels over 64) */
    img = repImage(16, 16, 0);
    {
        uint8_t *p = (uint8_t *)img.lv[0].data;
        for (int i = 0; i < 256; i++) {
            const int wire = (i % 16) % 4 == 0 || (i / 16) % 4 == 0;
            p[i * 4 + 0] = wire ? 200 : 0;
            p[i * 4 + 1] = wire ? 180 : 0;
            p[i * 4 + 2] = wire ? 160 : 0;
            p[i * 4 + 3] = wire ? 0x80 : 0;
        }
        static uint8_t want[16 * 16 * 4];
        const uint32_t wn = rdtex_build_mip_chain(p, 16, 16, want, 1);
        CHECK(rdtex_replacement_mips(&img) == 0 && img.levels == wn + 1 &&
                  memcmp(img.lv[1].data, want, rdtex_mip_chain_bytes(16, 16)) == 0,
              "a replacement's chain is the alpha-weighted box chain, no coverage kept");
        const uint8_t *l1 = img.lv[1].data;
        int over = 0, top = 0, dark = 0;
        for (int i = 0; i < 64; i++) {
            over += l1[i * 4 + 3] > 0x60;
            top += l1[i * 4 + 3] == 0x60;
            dark += l1[i * 4 + 3] && l1[i * 4] != 200;
        }
        /* level 1: 0x60 where both coordinates are even (three wire texels
         * of four), 0x40 where one is, 0 where neither; a coverage boost
         * at 64 would raise them (a quarter over 64, the base 7/16) */
        CHECK(over == 0 && top == 16 && dark == 0,
              "replacement lattice level 1: %d texels raised over 0x60, %d at 0x60 (16 "
              "expected), %d off the wire colour",
              over, top, dark);
    }
    texpack_free_image(&img);
    /* a replacement destroyed before its upload frees its levels */
    img = repImage(8, 8, 0);
    const RdTex r4 = rdtex_create_replacement(&img, 8, 8, "rep4");
    CHECK(r4.id && rd__tex_rec(r4.id)->pending, "pending");
    rd_destroy_texture(r4);
    rdtex_set_release_hook(NULL);
}

static void recordFrame(void);

/* the entry key (table index, generation) Texture.c stores a texture
   under: the generation is serial * 8 + level, found by asking */
static uint32_t entryGen(int idx)
{
    const uint32_t want = tex_HostTextureId(idx);
    for (uint32_t gen = 1; gen < 8u * 4096u; gen++) {
        if (rdtex_find((uint32_t)idx, gen, RDTEX_TEXA_REPLAY).id == want) {
            return gen;
        }
    }
    return 0;
}

static void replacementPixels(void)
{
    /* the PSMT8 sprite: a 32x32 RGBA8 replacement of its 16x16 texture */
    const uint32_t g8 = entryGen(s_t8.id);
    TexpackImage img = repImage(32, 32, 2);
    const RdTex r = rdtex_create_replacement(&img, 16, 16, "rep t8");
    CHECK(g8 && r.id && rdtex_replace((uint32_t)s_t8.id, g8, RDTEX_TEXA_REPLAY, r) == 0,
          "replace the PSMT8 texture (gen %u)", g8);
    /* the PSMCT16 sprite: a BC3 16x16, one colour, alpha 0x80 */
    const uint32_t g16 = entryGen(s_c16.id);
    TexpackImage bc;
    memset(&bc, 0, sizeof(bc));
    uint8_t *blocks = malloc(16 * 16);
    for (int i = 0; i < 16; i++) {
        uint8_t *o = blocks + i * 16;
        memset(o, 0, 16);
        o[0] = o[1] = 0x80;  /* alpha0 = alpha1 = 0x80, indices 0 */
        o[8] = o[10] = 0x1F; /* colour0 = colour1 = 0x001F: blue */
        o[9] = o[11] = 0x00;
    }
    bc.fmt = RD_TEXEL_BC3;
    bc.w = bc.h = 16;
    bc.levels = 1;
    bc.lv[0] = (TexpackImageLevel){blocks, 16, 16, 64, 256};
    bc.blob = blocks;
    bc.bytes = 256;
    const RdTex rb = rdtex_create_replacement(&bc, 16, 16, "rep c16");
    CHECK(g16 && rb.id && rdtex_replace((uint32_t)s_c16.id, g16, RDTEX_TEXA_REPLAY, rb) == 0,
          "replace the PSMCT16 texture with BC3");
    if (failures) {
        return;
    }
    gif_HostFrameReset();
    dl_Clear();
    recordFrame();
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4);
    if (!px || !rd__read_target(rd_target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h) ||
        w != 512) {
        CHECK(0, "SCENE readback");
        free(px);
        return;
    }
    int bad = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const uint8_t *g = &px[((256 + y) * 512 + 256 + x) * 4];
            uint8_t e[4];
            repPattern((uint32_t)x, (uint32_t)y, e);
            if (memcmp(g, e, 4) != 0 && bad++ < 4) {
                printf("  replaced PSMT8 pixel %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u\n", x, y,
                       g[0], g[1], g[2], g[3], e[0], e[1], e[2], e[3]);
            }
        }
    }
    CHECK(bad == 0, "the 2x replacement at the GS UVs: %d pixels differ", bad);
    bad = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const uint8_t *g = &px[((256 + y) * 512 + 288 + x) * 4];
            const uint8_t e[4] = {0, 0, 255, 0x80};
            if (memcmp(g, e, 4) != 0 && bad++ < 4) {
                printf("  replaced PSMCT16 pixel %d,%d: %u,%u,%u,%u expected 0,0,255,128\n", x, y,
                       g[0], g[1], g[2], g[3]);
            }
        }
    }
    CHECK(bad == 0, "the BC3 replacement (no TEXA on pack texels): %d pixels differ", bad);
    free(px);

    /* over the ring's 4 MB: its own upload buffer */
    img = repImage(1024, 1024, 0);
    uint8_t *ref = malloc(img.lv[0].size), *got = malloc(img.lv[0].size);
    memcpy(ref, img.lv[0].data, img.lv[0].size);
    const RdTex big = rdtex_create_replacement(&img, 64, 64, "rep big");
    rd_begin_frame();
    rd_end_frame(0);
    const RdTexRec *br = rd__tex_rec(big.id);
    CHECK(br && br->pending == NULL && br->mipLevels == 11, "the big replacement uploaded");
    CHECK(rd__read_texture(big, got, 1024 * 1024 * 4, &w, &h) && w == 1024 &&
              memcmp(got, ref, 1024 * 1024 * 4) == 0,
          "the big replacement reads back");
    rd_destroy_texture(big);
    free(ref);
    free(got);
}

/* --------------------------------------------- the decoder and the frame */

typedef struct Walk {
    int screens;
    RdStateBlock st[8];
} Walk;

static void collect(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Walk *w = user;
    (void)index;
    if (list == 11 && c->type == RDC_SCREEN && w->screens < 8) {
        w->st[w->screens++] = *s;
    }
}

/* a sprite of w x h texels, 1:1 at pixel (256 + px, 256 + py) of SCENE */
static void sprite(int px, int py, int w, int h)
{
    GifRect r = {px * 16, py * 16, w * 16, h * 16};
    GifRect uv = {8, 8, w * 16, h * 16};
    GifColor col = {128, 128, 128, 128};

    gif_SpriteSensitiveOrg(&r, 0, &uv, &col, 0);
}

static void recordFrame(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0};

    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    /* PSMT8, ICO block nearest */
    tex_TransTexture(s_t8.id, 11);
    gif_StartPacketPri(11);
    sprite(0, 0, 16, 16);
    gif_EndPacket();
    /* PSMCT16, no ICO block (linear), TEXA 7F/81 + AEM */
    tex_TransTexture(s_c16.id, 11);
    gif_StartPacketPri(11);
    gif_SetGsReg(0x3B, 0x810000807FLL);
    sprite(32, 0, 16, 16);
    gif_EndPacket();
    dl_Swap();
}

static void checkRecording(const RdFrame *f)
{
    Walk w;
    memset(&w, 0, sizeof(w));
    RdStateBlock s = f->startState;
    rd__walk(f, 0, &s, collect, &w);
    CHECK(w.screens == 2, "list 11 holds %d screen batches, expected 2", w.screens);
    if (w.screens != 2) {
        return;
    }
    CHECK(w.st[0].ds.texEnabled && w.st[0].tex == tex_HostTextureId(s_t8.id),
          "sprite 1 binds the cached PSMT8 texture (%u, expected %u)", w.st[0].tex,
          tex_HostTextureId(s_t8.id));
    CHECK(w.st[0].ds.magFilter == RD_FILTER_NEAREST && w.st[0].ds.minFilter == RD_FILTER_NEAREST,
          "sprite 1: TEX1 from the ICO block (nearest)");
    /* TEST from the record: ATE GREATER AREF 96 AFAIL FB_ONLY, Z GEQUAL */
    CHECK(w.st[0].ds.test.ate == 1 && w.st[0].ds.test.atst == 6 && w.st[0].ds.test.aref == 96 &&
              w.st[0].ds.test.afail == 1 && w.st[0].ds.test.ztst == RD_ZTST_GEQUAL,
          "sprite 1: the record's TEST (ate %d atst %d aref %d afail %d ztst %d)",
          w.st[0].ds.test.ate, w.st[0].ds.test.atst, w.st[0].ds.test.aref, w.st[0].ds.test.afail,
          w.st[0].ds.test.ztst);
    CHECK(w.st[1].tex == tex_HostTextureId(s_c16.id), "sprite 2 binds the cached PSMCT16 texture");
    CHECK(w.st[1].ds.magFilter == RD_FILTER_LINEAR, "sprite 2: TEX1 default (linear)");
    CHECK(w.st[1].ds.texa == RD_TEXA_7F_81_AEM, "sprite 2: TEXA 7F/81 AEM");
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

static void checkPixels(void)
{
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4);
    int bad = 0;

    if (!px || !rd__read_target(rd_target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h) ||
        w != 512) {
        CHECK(0, "SCENE readback");
        free(px);
        return;
    }
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const uint8_t *g = &px[((256 + y) * 512 + 256 + x) * 4];
            const uint8_t *e = &s_t8.ref[(y * 16 + x) * 4];
            if (memcmp(g, e, 4) != 0 && bad++ < 4) {
                printf("  PSMT8 pixel %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u\n", x, y, g[0], g[1],
                       g[2], g[3], e[0], e[1], e[2], e[3]);
            }
        }
    }
    CHECK(bad == 0, "PSMT8 sprite: %d pixels differ", bad);
    bad = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const uint8_t *g = &px[((256 + y) * 512 + 288 + x) * 4];
            const uint8_t *t = &s_c16.ref[(y * 16 + x) * 4];
            uint8_t e[4] = {t[0], t[1], t[2], texaRef(t, RD_TEXSRC_RGBA16, RD_TEXA_7F_81_AEM)};
            if (memcmp(g, e, 4) != 0 && bad++ < 4) {
                printf("  PSMCT16 pixel %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u\n", x, y, g[0],
                       g[1], g[2], g[3], e[0], e[1], e[2], e[3]);
            }
        }
    }
    CHECK(bad == 0, "PSMCT16 sprite under TEXA 7F/81+AEM: %d pixels differ", bad);
    /* outside: the clear */
    CHECK(px[(250 * 512 + 250) * 4] == 0, "outside the sprites");
    free(px);
}

/* The render thread (rd_thread.h): the same frame replayed on the
 * renderer's own thread, each replay handed over without waiting
 * (rd__replay_frame_async), while this thread, the game's side, records the
 * next frame through the texture cache and updates a texture of its own
 * between them (rd__texlock_wait holds each change until the replay's reads
 * end).  The picture is the one replayed here, the pictures and the waits
 * counted.  Under the tsan build (docs/port/TESTING_PROGRAMS.md, rd_tex) a
 * report is a failure. */
static void threadPixels(void)
{
    if (rhi_backend() == RHI_BACKEND_OPENGL) {
        printf("render thread: not with OpenGL here (its context stays on the test's thread)\n");
        return;
    }
    if (!rd_thread_local_start(RD_THREAD_ON)) {
        CHECK(0, "render thread: the renderer's own thread could not start");
        return;
    }
    RdThreadBlock b;
    rd_thread_block_take(&b); /* the block from here */
    uint8_t *pat = malloc(64 * 64 * 4);
    RdTex u = rd_create_texture(64, 64, NULL, RD_TEXA_80_80, "updater");
    const int frames = 30;
    int handed = 0;
    for (int k = 0; k < frames && pat; k++) {
        gif_HostFrameReset();
        dl_Clear();
        recordFrame();
        const RdFrame *f = rd__last_frame();
        handed += f != NULL && rd__replay_frame_async(f, 0, false);
        /* the game's side goes on while the replay runs */
        for (int i = 0; i < 64 * 64 * 4; i++) {
            pat[i] = (uint8_t)(hash((uint32_t)(k * 64 * 64 * 4 + i)) & 0xFF);
        }
        rd_update_texture(u, pat);
    }
    (void)rd_thread_drain();
    rd_thread_block_take(&b);
    CHECK(handed == frames && b.pictures >= (unsigned)frames,
          "render thread: %d of %d replays handed over, %u pictures run there", handed, frames,
          b.pictures);
    printf("render thread: %u pictures, %u waits of the game's side for their reads (%.2f ms, "
           "longest %.2f ms), %u textures retired\n",
           b.pictures, b.readWaits, b.readWaitMs, b.readWaitMaxMs, b.retired);
    checkPixels();         /* the last replay's SCENE: as on this thread */
    rd_destroy_texture(u); /* to the retire list: destroyed at the stop below */
    free(pat);
    rd_thread_local_stop();
}

/* An environment variable set (v) or removed (NULL) */
static void setEnv(const char *name, const char *v)
{
#ifdef _WIN32
    _putenv_s(name, v ? v : "");
#else
    if (v) {
        setenv(name, v, 1);
    } else {
        unsetenv(name);
    }
#endif
}

/* The render thread's hook (rd_thread.h rd__thread_test_hook): mode 1
 * holds the slot ahead (at RD_READ_PHASES) until this thread releases it,
 * as a GPU fence that does not pass; mode 2 holds the first picture's walk
 * open for holdMs (its reads go on) and numbers each picture's start
 * (RD_READ_INTERP) in one sequence with this thread's calls */
static struct {
    IcoMutex *m;
    IcoCond *c;
    int mode;
    int entered, release, held;
    unsigned seq, begun[4], nBegun;
    double holdMs;
} s_hk;

static void hookWait(int *flag)
{
    ico_mutex_lock(s_hk.m);
    while (!*flag) {
        ico_cond_wait(s_hk.c, s_hk.m);
    }
    ico_mutex_unlock(s_hk.m);
}

static void hookSet(int *flag)
{
    ico_mutex_lock(s_hk.m);
    *flag = 1;
    ico_cond_broadcast(s_hk.c);
    ico_mutex_unlock(s_hk.m);
}

static unsigned hookSeq(void)
{
    ico_mutex_lock(s_hk.m);
    const unsigned n = ++s_hk.seq;
    ico_mutex_unlock(s_hk.m);
    return n;
}

static void testHook(int at)
{
    if (s_hk.mode == 1 && at == RD_READ_PHASES) {
        hookSet(&s_hk.entered);
        hookWait(&s_hk.release);
    } else if (s_hk.mode == 2 && at == RD_READ_INTERP) {
        const unsigned n = hookSeq();
        if (s_hk.nBegun < 4) {
            s_hk.begun[s_hk.nBegun++] = n;
        }
    } else if (s_hk.mode == 2 && at == RD_READ_WALK && !s_hk.held) {
        s_hk.held = 1;
        hookSet(&s_hk.entered);
        const double t0 = ico_thread_now_ms();
        while (ico_thread_now_ms() - t0 < s_hk.holdMs) {
            /* the picture's reads held open */
        }
    }
}

static bool hookStart(int mode)
{
    memset(&s_hk, 0, sizeof(s_hk));
    s_hk.m = ico_mutex_create();
    s_hk.c = ico_cond_create();
    s_hk.mode = mode;
    s_hk.holdMs = 300.0;
    if (!s_hk.m || !s_hk.c) {
        CHECK(0, "render thread: the hook's lock");
        return false;
    }
    rd__thread_test_hook(testHook);
    return true;
}

static void hookStop(void)
{
    rd__thread_test_hook(NULL);
    ico_cond_destroy(s_hk.c);
    ico_mutex_destroy(s_hk.m);
    memset(&s_hk, 0, sizeof(s_hk));
}

/* The slot ahead (rd_thread.h rd__slot_ahead_take): a picture handed over
 * ends by waiting for the next picture's frame slot outside its reads.
 * With that wait held (the hook, as a fence that does not pass), this
 * thread's calls that change the renderer's state (rd_update_texture,
 * rdtex_find: rd__texlock_wait) return at once: no read wait while the
 * render thread still waits; the next picture then takes the slot waited
 * for (no wait inside its reads; the same frame again, as a frame closed
 * between would be replayed by rd_end_frame under the Original preset and
 * take that slot first) and its SCENE is the same.  Under
 * ICO_RD_SLOT_AHEAD=0 every picture waits inside its reads, none ahead.
 * Run as the device has it (the submit thread with the frame slots apart
 * or one at a time, where nothing is waited for ahead) and with
 * ICO_RD_SUBMIT_THREAD=0 (the render thread submits: always ahead). */
static void threadSlotAhead(void)
{
    if (rhi_backend() == RHI_BACKEND_OPENGL) {
        return;
    }
    for (int run = 0; run < 3; run++) {
        setEnv("ICO_RD_SUBMIT_THREAD", run == 1 ? "0" : NULL);
        setEnv("ICO_RD_SLOT_AHEAD", run == 2 ? "0" : NULL);
        const char *name = run == 0   ? "as the device has it"
                           : run == 1 ? "ICO_RD_SUBMIT_THREAD=0"
                                      : "ICO_RD_SLOT_AHEAD=0";
        if (!rd_thread_local_start(RD_THREAD_ON)) {
            CHECK(0, "slot ahead, %s: the renderer's own thread could not start", name);
            break;
        }
        /* nothing waited for ahead with the submit thread on and the frame
           slots one at a time, or with the switch off */
        const bool ahead = run != 2 && !(rd_thread_submit_on() && !rd_thread_submit_slots_apart());
        RdThreadBlock b, mid;
        uint8_t *pat = malloc(64 * 64 * 4);
        RdTex u = rd_create_texture(64, 64, NULL, RD_TEXA_80_80, "slot ahead");
        rd_thread_block_take(&b); /* the block from here */
        memset(&mid, 0, sizeof(mid));
        if (!pat || !hookStart(ahead ? 1 : 0)) {
            free(pat);
            rd_thread_local_stop();
            break;
        }
        gif_HostFrameReset();
        dl_Clear();
        recordFrame();
        const RdFrame *f = rd__last_frame();
        const bool posted = f != NULL && rd__replay_frame_async(f, 0, false);
        CHECK(posted, "slot ahead, %s: picture 1 handed over", name);
        if (posted && ahead) {
            hookWait(&s_hk.entered); /* the render thread in the slot ahead, held */
            for (int i = 0; i < 64 * 64 * 4; i++) {
                pat[i] = (uint8_t)(hash((uint32_t)(i + 77)) & 0xFF);
            }
            const double t0 = ico_thread_now_ms();
            rd_update_texture(u, pat);
            (void)rdtex_find((uint32_t)s_t8.id, 1u, RDTEX_TEXA_REPLAY);
            const double ms = ico_thread_now_ms() - t0;
            rd_thread_block_take(&mid);
            CHECK(mid.readWaits == 0 && !s_hk.release,
                  "slot ahead, %s: with the slot's wait held, the game side's calls waited %u "
                  "times (%.2f ms)",
                  name, mid.readWaits, ms);
            hookSet(&s_hk.release);
        }
        (void)rd_thread_drain();
        rd_thread_block_take(&b);
        if (ahead) {
            /* picture 1, the first: its own slot inside its reads */
            CHECK(mid.fenceInside + b.fenceInside == 1 && mid.aheads + b.aheads == 1 &&
                      mid.readWaits + b.readWaits == 0,
                  "slot ahead, %s: picture 1 waited for its own slot inside its reads (%u, the "
                  "first), then for the next slot ahead (%u), no read wait (%u)",
                  name, mid.fenceInside + b.fenceInside, mid.aheads + b.aheads,
                  mid.readWaits + b.readWaits);
        }
        s_hk.mode = 0;
        /* picture 2: the slot waited for ahead, no wait inside its reads.
           The same closed frame again, nothing recorded between: under the
           test's Original preset (interpolation off) rd_end_frame replays
           each frame it closes on the render thread (rd__on_host, a call
           the game side waits for), and that replay would take the slot
           waited for ahead first, as any replay after it does */
        CHECK(f != NULL && rd__replay_frame_async(f, 0, false),
              "slot ahead, %s: picture 2 handed over", name);
        (void)rd_thread_drain();
        rd_thread_block_take(&b);
        if (ahead) {
            CHECK(b.pictures == 1 && b.fenceInside == 0 && b.aheads == 1,
                  "slot ahead, %s: picture 2 took the slot waited for (%u inside its reads, %u "
                  "ahead after it, %u pictures)",
                  name, b.fenceInside, b.aheads, b.pictures);
        } else {
            CHECK(b.aheads == 0 && b.fenceInside == b.pictures,
                  "slot ahead, %s: nothing waited for ahead (%u), each picture's slot inside its "
                  "reads (%u of %u)",
                  name, b.aheads, b.fenceInside, b.pictures);
        }
        {
            char text[640];
            rd_thread_read_waits_text(&b, text, sizeof(text));
            CHECK(strstr(text, "; the next picture's frame slot waited for at the end of the one "
                               "before") != NULL,
                  "slot ahead, %s: the 10 s line's clause: %s", name, text);
            printf("slot ahead, %s: %s\n", name, text);
        }
        hookStop();
        checkPixels(); /* the last replay's SCENE: as on this thread */
        rd_destroy_texture(u);
        free(pat);
        rd_thread_local_stop();
    }
    setEnv("ICO_RD_SUBMIT_THREAD", NULL);
    setEnv("ICO_RD_SLOT_AHEAD", NULL);
}

/* One picture's reads at most: a call of this thread's that waits for
 * picture 1's reads (held open in its walk) runs before picture 2's reads
 * begin (the hand-over is this thread's, at its post, after the call), so
 * the wait is that one picture's, counted by what the render thread was
 * doing (the walk); and the split's text names each part */
static void threadFair(void)
{
    if (rhi_backend() == RHI_BACKEND_OPENGL) {
        return;
    }
    if (!rd_thread_local_start(RD_THREAD_ON)) {
        CHECK(0, "fairness: the renderer's own thread could not start");
        return;
    }
    RdThreadBlock b;
    uint8_t *pat = malloc(64 * 64 * 4);
    RdTex u = rd_create_texture(64, 64, NULL, RD_TEXA_80_80, "fair");
    rd_thread_block_take(&b);
    if (!pat || !hookStart(2)) {
        free(pat);
        rd_thread_local_stop();
        return;
    }
    gif_HostFrameReset();
    dl_Clear();
    recordFrame();
    const RdFrame *f = rd__last_frame();
    const bool p1 = f != NULL && rd__replay_frame_async(f, 0, false);
    unsigned callDone = 0;
    if (p1) {
        hookWait(&s_hk.entered); /* picture 1 in its walk, held */
        for (int i = 0; i < 64 * 64 * 4; i++) {
            pat[i] = (uint8_t)(hash((uint32_t)(i + 991)) & 0xFF);
        }
        rd_update_texture(u, pat); /* waits for picture 1's reads */
        callDone = hookSeq();
    }
    gif_HostFrameReset();
    dl_Clear();
    recordFrame();
    f = rd__last_frame();
    const bool p2 = f != NULL && rd__replay_frame_async(f, 0, false);
    (void)rd_thread_drain();
    rd_thread_block_take(&b);
    CHECK(p1 && p2 && s_hk.nBegun == 2, "fairness: two pictures handed over (%u begun)",
          s_hk.nBegun);
    CHECK(s_hk.nBegun < 2 || (s_hk.begun[0] < callDone && callDone < s_hk.begun[1]),
          "fairness: the waiting call ran (%u) after picture 1 began (%u) and before picture 2's "
          "reads began (%u)",
          callDone, s_hk.begun[0], s_hk.nBegun == 2 ? s_hk.begun[1] : 0u);
    CHECK(b.readWaits <= 1, "fairness: %u read waits, one picture's at most", b.readWaits);
    if (b.readWaits == 1) {
        CHECK(b.readWaitBy[RD_READ_WALK] == 1 && b.readWaitMaxMs < 2.0 * s_hk.holdMs,
              "fairness: the wait began in the walk (%u) and lasted one picture's reads (%.1f "
              "ms, the walk held %.0f ms)",
              b.readWaitBy[RD_READ_WALK], b.readWaitMaxMs, s_hk.holdMs);
        char text[640];
        rd_thread_read_waits_text(&b, text, sizeof(text));
        CHECK(strstr(text, "; the render thread as each began: the hand-over (the picture not yet "
                           "begun there) 0 (0.0 ms), the interpolation 0") != NULL &&
                  strstr(text, ", the walk 1 (") != NULL,
              "fairness: the split's text: %s", text);
        printf("fairness: read waits 1 (%.1f ms)%s\n", b.readWaitMs, text);
    } else {
        printf("fairness: the call came after picture 1's reads (no wait): the order still "
               "checked\n");
    }
    hookStop();
    rd_destroy_texture(u);
    free(pat);
    rd_thread_local_stop();
}

/* The display's wait ahead's seam (rd_thread.h rd__thread_room_hook), in
 * place of the backend's rhi_wait_present_room: mode 1 holds the next wait
 * until this thread releases it (a display that keeps the queue full),
 * mode 2 answers false once (no wait made: as a present that queued
 * nothing), else true at once; every call counted */
static int s_roomMode;
static unsigned s_roomCalls;

static bool roomHook(void)
{
    ico_mutex_lock(s_hk.m);
    s_roomCalls++;
    const int mode = s_roomMode;
    s_roomMode = 0;
    ico_mutex_unlock(s_hk.m);
    if (mode == 1) {
        hookSet(&s_hk.entered);
        hookWait(&s_hk.release);
    }
    return mode != 2;
}

static void roomSet(int mode)
{
    ico_mutex_lock(s_hk.m);
    s_roomMode = mode;
    ico_mutex_unlock(s_hk.m);
}

static unsigned roomCalls(void)
{
    ico_mutex_lock(s_hk.m);
    const unsigned n = s_roomCalls;
    ico_mutex_unlock(s_hk.m);
    return n;
}

/* The display's wait ahead (rd_thread.h rd__room_ahead_take): with the
 * render thread submitting (ICO_RD_SUBMIT_THREAD=0, as Direct3D 11 and
 * OpenGL run), a picture handed over that presented ends, after its
 * present, by making the next picture's wait for room in the present
 * queue (the seam above in place of the backend's).  Held there, this
 * thread's calls that change the renderer's state (rd_update_texture,
 * rdtex_find: rd__texlock_wait) return without a read wait; the next
 * picture's acquire takes the wait made ahead (none inside its reads); a
 * picture whose predecessor made none (the seam's false) waits inside its
 * reads and is counted; the 10 s line's clause says so; SCENE after
 * picture 3 byte for byte as after picture 0 (the same frame; the cell
 * runs after replacementPixels, so not checkPixels' references).  With
 * ICO_RD_ACQUIRE_AHEAD=0 the seam is never asked.  The output is given a
 * size so the pictures present (rd_tex_test's settings have none): run
 * after the other device cells */
static void threadRoomAhead(void)
{
    if (rhi_backend() == RHI_BACKEND_OPENGL) {
        return;
    }
    rd_resize_output(256, 256);
    for (int run = 0; run < 2; run++) {
        setEnv("ICO_RD_SUBMIT_THREAD", "0");
        setEnv("ICO_RD_ACQUIRE_AHEAD", run == 1 ? "0" : NULL);
        const char *name = run == 0 ? "ICO_RD_SUBMIT_THREAD=0" : "ICO_RD_ACQUIRE_AHEAD=0";
        if (!rd_thread_local_start(RD_THREAD_ON)) {
            CHECK(0, "display's wait ahead, %s: the renderer's own thread could not start", name);
            break;
        }
        RdThreadBlock b, mid;
        uint8_t *pat = malloc(64 * 64 * 4);
        uint8_t *scene0 = malloc(512 * 512 * 4), *scene3 = malloc(512 * 512 * 4);
        uint32_t w0 = 0, h0 = 0, w3 = 0, h3 = 0;
        RdTex u = rd_create_texture(64, 64, NULL, RD_TEXA_80_80, "room ahead");
        memset(&mid, 0, sizeof(mid));
        if (!pat || !scene0 || !scene3 || !hookStart(0)) {
            free(pat);
            free(scene0);
            free(scene3);
            rd_thread_local_stop();
            break;
        }
        s_roomMode = 0;
        s_roomCalls = 0;
        rd__thread_room_hook(roomHook);
        gif_HostFrameReset();
        dl_Clear();
        recordFrame(); /* replayed and presented by rd_end_frame: a call, not handed over */
        const RdFrame *f = rd__last_frame();
        (void)rd_thread_drain();
        rd_thread_block_take(&b); /* the block from here */
        /* picture 0: presents, makes the wait ahead at once */
        const bool p0 = f != NULL && rd__replay_frame_async(f, 0, true);
        (void)rd_thread_drain();
        rd_thread_block_take(&b);
        if (run == 1) {
            CHECK(p0 && b.roomAheads == 0 && b.roomInside == 0 && roomCalls() == 0,
                  "display's wait ahead, %s: never made (%u, the seam asked %u times), none "
                  "counted inside (%u)",
                  name, b.roomAheads, roomCalls(), b.roomInside);
        } else if (!(p0 && b.roomAheads == 1 && b.pictures == 1) ||
                   !rd__read_target(rd_target(RD_TARGET_SCENE), scene0, 512 * 512 * 4, &w0, &h0)) {
            CHECK(0,
                  "display's wait ahead, %s: picture 0 handed over (%d) and presented, its job "
                  "ending with the wait ahead (%u, %u pictures; none: the picture did not "
                  "present, the cell cannot go on)",
                  name, (int)p0, b.roomAheads, b.pictures);
        } else {
            /* picture 1: takes picture 0's wait, then its own held */
            roomSet(1);
            const bool p1 = rd__replay_frame_async(f, 0, true);
            CHECK(p1, "display's wait ahead, %s: picture 1 handed over", name);
            if (p1) {
                hookWait(&s_hk.entered); /* the render thread in the wait ahead, held */
                for (int i = 0; i < 64 * 64 * 4; i++) {
                    pat[i] = (uint8_t)(hash((uint32_t)(i + 313)) & 0xFF);
                }
                const double t0 = ico_thread_now_ms();
                rd_update_texture(u, pat);
                (void)rdtex_find((uint32_t)s_t8.id, 1u, RDTEX_TEXA_REPLAY);
                const double ms = ico_thread_now_ms() - t0;
                rd_thread_block_take(&mid);
                CHECK(mid.readWaits == 0 && !s_hk.release,
                      "display's wait ahead, %s: with the display's wait held, the game side's "
                      "calls waited %u times (%.2f ms)",
                      name, mid.readWaits, ms);
                hookSet(&s_hk.release);
            }
            (void)rd_thread_drain();
            rd_thread_block_take(&b);
            CHECK(mid.roomAheads + b.roomAheads == 1 && mid.roomInside + b.roomInside == 0 &&
                      mid.readWaits + b.readWaits == 0,
                  "display's wait ahead, %s: picture 1 took the wait made ahead (%u inside its "
                  "reads), made the next one (%u), no read wait (%u)",
                  name, mid.roomInside + b.roomInside, mid.roomAheads + b.roomAheads,
                  mid.readWaits + b.readWaits);
            /* picture 2 makes none (the seam's false); picture 3 then
               waits inside its reads, and makes its own */
            roomSet(2);
            const bool p2 = rd__replay_frame_async(f, 0, true);
            const bool p3 = p2 && rd__replay_frame_async(f, 0, true);
            (void)rd_thread_drain();
            rd_thread_block_take(&b);
            CHECK(p2 && p3 && b.pictures == 2 && b.roomAheads == 1 && b.roomInside == 1,
                  "display's wait ahead, %s: after a picture that made none, the next waited "
                  "inside its reads (%u) and made its own (%u; %u pictures)",
                  name, b.roomInside, b.roomAheads, b.pictures);
            CHECK(rd__read_target(rd_target(RD_TARGET_SCENE), scene3, 512 * 512 * 4, &w3, &h3) &&
                      w3 == w0 && h3 == h0 && memcmp(scene0, scene3, (size_t)w0 * h0 * 4) == 0,
                  "display's wait ahead, %s: SCENE after picture 3 as after picture 0 (%ux%u, "
                  "%ux%u)",
                  name, w0, h0, w3, h3);
            char text[1024];
            rd_thread_read_waits_text(&b, text, sizeof(text));
            CHECK(strstr(text, "; the display's wait for room in the present queue made at the "
                               "end of the picture before 1 times (") != NULL &&
                      strstr(text, "1 pictures waited for it inside their reads") != NULL,
                  "display's wait ahead, %s: the 10 s line's clause: %s", name, text);
            printf("display's wait ahead, %s: %s\n", name, text);
        }
        rd__thread_room_hook(NULL);
        hookStop();
        rd_destroy_texture(u);
        free(pat);
        free(scene0);
        free(scene3);
        rd_thread_local_stop();
    }
    setEnv("ICO_RD_SUBMIT_THREAD", NULL);
    setEnv("ICO_RD_ACQUIRE_AHEAD", NULL);
}

/* [dev] render_thread = sync (rd_thread.h RdThreadHost.sync): a picture is
 * posted as one that waits and is not counted as handed over
 * (rd_submit_async false after it, as with the thread off, so the window
 * paces it by the present's own blocked time); with the thread on it is,
 * until the drain */
static void threadSync(void)
{
    if (rhi_backend() == RHI_BACKEND_OPENGL) {
        return;
    }
    for (int on = 0; on < 2; on++) {
        if (!rd_thread_local_start(on ? RD_THREAD_ON : RD_THREAD_SYNC)) {
            CHECK(0, "render thread %s: the renderer's own thread could not start",
                  on ? "on" : "sync");
            return;
        }
        gif_HostFrameReset();
        dl_Clear();
        recordFrame();
        const RdFrame *f = rd__last_frame();
        const bool posted = f != NULL && rd__replay_frame_async(f, 0, false);
        const bool handed = rd_submit_async();
        (void)rd_thread_drain();
        CHECK(posted && handed == (on != 0) && !rd_submit_async(),
              "render thread %s: the picture %s handed over (posted %d)", on ? "on" : "sync",
              handed ? "counted as" : "not counted as", posted);
        rd_thread_local_stop();
    }
}

/* ------------------------------------------------- Frame rate Original */

/* The window's gate as the test's (rd.h RdReplayGate): its calls, the ones
 * made off this thread (it runs on the game thread, never on the render
 * thread), the frames it presented while told busy; mode 1 leaves out
 * every second frame (its odd calls) */
static _Thread_local int t_testThread;

static struct {
    unsigned calls, other, busy;
    int mode;
} s_og;

static int origGate(int keep, bool busy)
{
    const unsigned n = s_og.calls++;
    /* mode 2: window_orig_gate's busy arm (pace_original_gate): a frame
       that is not a keep frame is left out while told busy */
    const int answer = s_og.mode == 2 && busy && !keep ? RD_GATE_BUSY
                       : s_og.mode == 1 && (n & 1u)    ? RD_GATE_LATE
                                                       : RD_GATE_PRESENT;
    s_og.other += !t_testThread || rd_thread_on_render();
    s_og.busy += busy && answer == RD_GATE_PRESENT ? 1u : 0u;
    return answer;
}

/* The submit thread's hook (rd_submit_thread.h rd__submit_test_hook): the
 * first present held, as a driver's present call that blocks, until this
 * thread releases it (or 10 s pass: a test that would wait for it fails
 * instead of hanging) */
static int s_shEntered, s_shRelease, s_shHeld;

static bool spinUntil(const int *flag, double ms)
{
    const double t0 = ico_thread_now_ms();
    while (!__atomic_load_n(flag, __ATOMIC_ACQUIRE)) {
        if (ico_thread_now_ms() - t0 > ms) {
            return false;
        }
    }
    return true;
}

static void submitHold(void)
{
    if (__atomic_exchange_n(&s_shHeld, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    __atomic_store_n(&s_shEntered, 1, __ATOMIC_RELEASE);
    (void)spinUntil(&s_shRelease, 10000.0);
}

/* the presents' records popped since the last call: how many, and how many
 * had their present where `want` says (RdPerfRecord.presentThread; the
 * first RHI_FRAMES_IN_FLIGHT records of a pass are the pass before's last
 * replays, whose GPU times came in this one) */
static void presentWhere(int want, unsigned *n, unsigned *match)
{
    RdPerfRecord r;
    *n = *match = 0;
    while (rd_perf_pop(&r)) {
        if (!r.presented) {
            continue;
        }
        (*n)++;
        *match += r.presentThread == want;
    }
}

/* SCENE into scene, and DISPLAY into display where it reads back (*dw x
 * *dh; 0 x 0 where it does not) */
static bool readScene(uint8_t *scene, uint8_t *display, uint32_t *dw, uint32_t *dh)
{
    uint32_t w = 0, h = 0;
    *dw = *dh = 0;
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), scene, 512 * 512 * 4, &w, &h) || w != 512) {
        return false;
    }
    if (!rd__read_target(rd_target(RD_TARGET_DISPLAY), display, 512 * 512 * 4, dw, dh)) {
        *dw = *dh = 0;
    }
    return true;
}

/* Frame rate Original with the render thread (rd.h RdReplayGate, rd_thread.h
 * rd__replay_frame_post): with the gate set, the frame rd_end_frame closes
 * is handed over (rd_submit_async true right after it; rd_last_present_info
 * names it at once), the gate runs on this thread every time, and after the
 * drain SCENE and DISPLAY are byte for byte the renderer's without the
 * thread (RD_THREAD_OFF, the replay inside the step); the presents ran on
 * the submit thread (inside the step without the thread).  A frame
 * presented while the render thread was busy waited for the picture before
 * (counted, as many as the gate was told busy).  The gate leaving out every
 * second frame: 15 pictures of 30, the same SCENE.  Under
 * ICO_RD_ORIGINAL_ASYNC=0 ([dev] original_async = false) every frame is
 * replayed inside the step (rd_submit_async false), the same SCENE.  Run
 * after threadRoomAhead: the output has a size, so the pictures present. */
static void threadOriginal(void)
{
    if (rhi_backend() == RHI_BACKEND_OPENGL) {
        return;
    }
    const int frames = 30;
    uint8_t *refScene = malloc(512 * 512 * 4), *refDisplay = malloc(512 * 512 * 4);
    uint8_t *scene = malloc(512 * 512 * 4), *display = malloc(512 * 512 * 4);
    uint8_t *pat = malloc(64 * 64 * 4);
    uint32_t refW = 0, refH = 0, dw = 0, dh = 0;
    RdFramePost fp;
    RdFramePostLast last;
    RdThreadBlock b;
    unsigned n, match;
    t_testThread = 1;
    if (!refScene || !refDisplay || !scene || !display || !pat) {
        CHECK(0, "Original: memory");
        goto out;
    }
    rd_set_replay_gate(origGate);
    /* pass 0: no render thread, the replay inside the step */
    memset(&s_og, 0, sizeof(s_og));
    rd_frame_post_take(&fp);
    presentWhere(RD_PRESENT_IN_STEP, &n, &match);
    for (int k = 0; k < frames; k++) {
        gif_HostFrameReset();
        dl_Clear();
        recordFrame();
    }
    rd_frame_post_take(&fp);
    CHECK(s_og.calls == (unsigned)frames && s_og.other == 0 && s_og.busy == 0 &&
              fp.inlines == (unsigned)frames && fp.posts == 0,
          "Original, no render thread: the gate %u times (%u off this thread, %u busy), %u "
          "replayed inside the step, %u handed over",
          s_og.calls, s_og.other, s_og.busy, fp.inlines, fp.posts);
    CHECK(rd_frame_post_last_take(&last) && last.how == RD_POST_INLINE &&
              last.frame == rd__last_frame()->number && !rd_frame_post_last_take(&last),
          "Original, no render thread: the last picture's record");
    presentWhere(RD_PRESENT_IN_STEP, &n, &match);
    CHECK(match > 0 && match + RHI_FRAMES_IN_FLIGHT >= n,
          "Original, no render thread: %u of %u presents inside the step", match, n);
    if (!readScene(refScene, refDisplay, &refW, &refH)) {
        CHECK(0, "Original: SCENE readback");
        goto out;
    }
    printf("Original: the reference SCENE%s\n", refW ? " and DISPLAY" : " (no DISPLAY)");
    for (int run = 0; run < 3; run++) {
        /* 1: handed over; 2: every second frame left out; 3: the escape */
        setEnv("ICO_RD_ORIGINAL_ASYNC", run == 2 ? "0" : NULL);
        const char *name = run == 0   ? "handed over"
                           : run == 1 ? "every second frame left out"
                                      : "ICO_RD_ORIGINAL_ASYNC=0";
        if (!rd_thread_local_start(RD_THREAD_ON)) {
            CHECK(0, "Original, %s: the renderer's own thread could not start", name);
            break;
        }
        RdTex u = rd_create_texture(64, 64, NULL, RD_TEXA_80_80, "original");
        memset(&s_og, 0, sizeof(s_og));
        s_og.mode = run == 1 ? 1 : 0;
        rd_thread_block_take(&b);
        rd_frame_post_take(&fp);
        presentWhere(RD_PRESENT_ON_SUBMIT, &n, &match);
        int async = 0, infoOk = 0;
        for (int k = 0; k < frames; k++) {
            gif_HostFrameReset();
            dl_Clear();
            recordFrame();
            RdPresentInfo pi;
            const RdFrame *f = rd__last_frame();
            const bool shown = run != 1 || (k & 1) == 0;
            async += shown && rd_submit_async(); /* true until the next drain */
            infoOk += !shown || (f != NULL && rd_last_present_info(&pi) && pi.frame == f->number);
            /* the game's side goes on while the picture runs */
            for (int i = 0; i < 64 * 64 * 4; i++) {
                pat[i] = (uint8_t)(hash((uint32_t)(k * 64 * 64 * 4 + i + 7)) & 0xFF);
            }
            rd_update_texture(u, pat);
        }
        (void)rd_thread_drain();
        rd_thread_block_take(&b);
        rd_frame_post_take(&fp);
        const unsigned want = run == 1 ? (unsigned)frames / 2u : (unsigned)frames;
        CHECK(s_og.calls == (unsigned)frames && s_og.other == 0,
              "Original, %s: the gate %u times, %u of them off this thread", name, s_og.calls,
              s_og.other);
        CHECK(infoOk == frames, "Original, %s: rd_last_present_info named the frame %d of %d times",
              name, infoOk, frames);
        if (run == 2) {
            CHECK(async == 0 && fp.inlines == want && fp.posts == 0 && s_og.busy == 0,
                  "Original, %s: %d handed over, %u replayed inside the step, %u handed over "
                  "counted, the gate told busy %u times",
                  name, async, fp.inlines, fp.posts, s_og.busy);
        } else {
            CHECK(async == (int)want && fp.posts == want && fp.inlines == 0 && b.pictures == want &&
                      fp.postWaits == s_og.busy && fp.lateSkips == (unsigned)frames - want,
                  "Original, %s: %d handed over (%u counted, %u inside the step, %u pictures on "
                  "the render thread, want %u), %u waited for the picture before (the gate told "
                  "busy %u times), %u left out",
                  name, async, fp.posts, fp.inlines, b.pictures, want, fp.postWaits, s_og.busy,
                  fp.lateSkips);
        }
        {
            char text[320];
            rd_frame_post_text(&fp, text, sizeof(text));
            CHECK(strstr(text, "; its picture handed over in ") != NULL &&
                      strstr(text, "); presents waited inside the step: ") != NULL,
                  "Original, %s: the 10 s clause: %s", name, text);
            printf("Original, %s:%s\n", name, text);
        }
        presentWhere(run == 2                ? RD_PRESENT_IN_STEP
                     : rd_thread_submit_on() ? RD_PRESENT_ON_SUBMIT
                                             : RD_PRESENT_ELSEWHERE,
                     &n, &match);
        CHECK(match > 0 && match + RHI_FRAMES_IN_FLIGHT >= n,
              "Original, %s: %u of %u presents where they ran (%s)", name, match, n,
              run == 2 ? "inside the step" : "the submit thread");
        CHECK(readScene(scene, display, &dw, &dh) &&
                  memcmp(scene, refScene, (size_t)512 * 512 * 4) == 0,
              "Original, %s: SCENE as without the render thread", name);
        CHECK(dw == refW && dh == refH &&
                  (refW == 0 || memcmp(display, refDisplay, (size_t)refW * refH * 4) == 0),
              "Original, %s: DISPLAY as without the render thread (%ux%u, %ux%u)", name, dw, dh,
              refW, refH);
        rd_destroy_texture(u);
        rd_thread_local_stop();
    }
    setEnv("ICO_RD_ORIGINAL_ASYNC", NULL);
    /* the busy skip, made certain through the submit side: the first
       picture's present held on the submit thread (its render job over, so
       the render thread's part reads idle), the gate answering as
       window_orig_gate does; the next frames are left out (not replayed,
       counted as busy) and SCENE stays the first one's */
    if (rd_thread_local_start(RD_THREAD_ON)) {
        if (!rd_thread_submit_on()) {
            printf("Original, busy: no submit thread on this device; the held present is not "
                   "driven\n");
        } else {
            const unsigned skipFrames = 4;
            memset(&s_og, 0, sizeof(s_og));
            s_og.mode = 2;
            __atomic_store_n(&s_shEntered, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&s_shRelease, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&s_shHeld, 0, __ATOMIC_RELEASE);
            rd__submit_test_hook(submitHold);
            rd_thread_block_take(&b);
            rd_frame_post_take(&fp);
            gif_HostFrameReset();
            dl_Clear();
            recordFrame(); /* both idle: handed over */
            const bool entered = spinUntil(&s_shEntered, 5000.0);
            bool renderIdle = false;
            {
                const double t0 = ico_thread_now_ms();
                while (!(renderIdle = !rd__thread_picture_busy_render()) &&
                       ico_thread_now_ms() - t0 < 5000.0) {
                    /* the render job's end, after its post to the submit thread */
                }
            }
            const bool busyHeld = rd__thread_picture_busy();
            CHECK(entered && renderIdle && rd_thread_submit_unfinished() && busyHeld,
                  "Original, busy: the present held on the submit thread (%d), the render "
                  "thread's part idle (%d): busy %d (the submit thread's part of it)",
                  entered, renderIdle, busyHeld);
            for (unsigned k = 0; k < skipFrames; k++) {
                gif_HostFrameReset();
                dl_Clear();
                recordFrame(); /* told busy: left out */
            }
            __atomic_store_n(&s_shRelease, 1, __ATOMIC_RELEASE);
            (void)rd_thread_drain();
            rd__submit_test_hook(NULL);
            rd_thread_block_take(&b);
            rd_frame_post_take(&fp);
            CHECK(s_og.calls == skipFrames + 1u && fp.busySkips == skipFrames &&
                      fp.posts + fp.busySkips == s_og.calls && fp.lateSkips == 0 &&
                      fp.inlines == 0 && b.pictures == fp.posts && fp.posts == 1u,
                  "Original, busy: the gate %u times, %u left out as busy, %u handed over, %u "
                  "inside the step, %u late, %u pictures on the render thread",
                  s_og.calls, fp.busySkips, fp.posts, fp.inlines, fp.lateSkips, b.pictures);
            {
                char text[320];
                rd_frame_post_text(&fp, text, sizeof(text));
                CHECK(strstr(text, "skipped: render thread busy") != NULL,
                      "Original, busy: the 10 s clause: %s", text);
                printf("Original, busy:%s\n", text);
            }
            CHECK(readScene(scene, display, &dw, &dh) &&
                      memcmp(scene, refScene, (size_t)512 * 512 * 4) == 0,
                  "Original, busy: SCENE the handed-over frame's, the frames left out not "
                  "replayed");
        }
        rd_thread_local_stop();
    } else {
        CHECK(0, "Original, busy: the renderer's own thread could not start");
    }
out:
    rd_set_replay_gate(NULL);
    (void)rd_frame_post_last_take(&last);
    free(refScene);
    free(refDisplay);
    free(scene);
    free(display);
    free(pat);
}

/* ----------------------------------------------------------------- R8 */

static uint8_t r8At(int x, int y)
{
    return (uint8_t)hash((uint32_t)(y * 64 + x) + 991u);
}

/* rdtex_text_upload_failed: one log line a session, for the text's
   coverage formats only (rd_upload.c rd__upload_dirty_textures calls it for
   every texture it could not upload; the game's own textures are not text) */
static void textUploadLogChecks(void)
{
    CHECK(rdtex_text_upload_failed("rgba", 4, 4, RD_TEXEL_RGBA8, "test") == 0,
          "upload log: an RGBA8 texture is not text");
    CHECK(rdtex_text_upload_failed("bc1", 4, 4, RD_TEXEL_BC1, "test") == 0,
          "upload log: a BC1 texture is not text");
    CHECK(rdtex_text_upload_failed("ui menu text light p0", 256, 128, RD_TEXEL_SHEET, "test") == 1,
          "upload log: a menu text page is named");
    CHECK(rdtex_text_upload_failed("ui font 16px p0", 256, 256, RD_TEXEL_R8, "test") == 0,
          "upload log: once a session");
}

static void r8Checks(void)
{
    enum { W = 37, H = 23 };

    static uint8_t cov[W * H];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            cov[y * W + x] = r8At(x, y);
        }
    }
    RdTex t = rd_create_texture_r8(W, H, cov, "r8");
    RdTexRec *r = rd__tex_rec(t.id);
    CHECK(r && r->kind == RD_TEXKIND_IMAGE && r->format == RD_TEXEL_R8 && r->w == W && r->h == H,
          "R8 record");
    if (!r) {
        return;
    }
    CHECK(memcmp(r->pixels, cov, sizeof(cov)) == 0, "R8 texels kept as given");
    CHECK(r->dirty && r->dirtyX0 == 0 && r->dirtyY0 == 0 && r->dirtyX1 == W && r->dirtyY1 == H,
          "a new texture is dirty whole");
    /* as if uploaded: the rectangles below start a new union */
    r->dirty = 0;
    r->dirtyX0 = r->dirtyY0 = r->dirtyX1 = r->dirtyY1 = 0;
    g_rd.texDirtyCount--;
    const uint32_t rects0 = g_rd.texRectUpdates, full0 = g_rd.texFullUpdates;
    uint8_t a[4 * 3], b[5 * 5];
    memset(a, 0xA5, sizeof(a));
    memset(b, 0x3C, sizeof(b));
    rd_update_texture_rect(t, 2, 3, 4, 3, a);
    CHECK(r->dirty && r->dirtyX0 == 2 && r->dirtyY0 == 3 && r->dirtyX1 == 6 && r->dirtyY1 == 6,
          "dirty rectangle %u,%u-%u,%u", r->dirtyX0, r->dirtyY0, r->dirtyX1, r->dirtyY1);
    /* past the right and bottom edges: clipped to 3 x 2 at (34, 21) */
    rd_update_texture_rect(t, 34, 21, 5, 5, b);
    CHECK(r->dirtyX0 == 2 && r->dirtyY0 == 3 && r->dirtyX1 == W && r->dirtyY1 == H,
          "union %u,%u-%u,%u", r->dirtyX0, r->dirtyY0, r->dirtyX1, r->dirtyY1);
    int bad = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t want = r8At(x, y);
            if (x >= 2 && x < 6 && y >= 3 && y < 6) {
                want = 0xA5;
            } else if (x >= 34 && y >= 21) {
                want = 0x3C;
            }
            bad += r->pixels[y * W + x] != want;
        }
    }
    CHECK(bad == 0, "R8 rectangles: %d texels wrong", bad);
    /* the same texels again change nothing and count nothing */
    rd_update_texture_rect(t, 2, 3, 4, 3, a);
    rd_update_texture_rect(t, W, 0, 1, 1, a); /* outside: ignored */
    CHECK(g_rd.texRectUpdates - rects0 == 2 && g_rd.texFullUpdates == full0,
          "rectangle updates counted %u (full %u)", g_rd.texRectUpdates - rects0,
          g_rd.texFullUpdates - full0);
    rd_update_texture(t, cov);
    CHECK(g_rd.texFullUpdates - full0 == 1 && memcmp(r->pixels, cov, sizeof(cov)) == 0 &&
              r->dirtyX0 == 0 && r->dirtyX1 == W,
          "rd_update_texture on R8: w * h bytes, dirty whole");
    rd_destroy_texture(t);

    /* RGBA8 takes rectangles in its own format */
    static uint8_t rgba[8 * 8 * 4];
    memset(rgba, 0, sizeof(rgba));
    RdTex u = rd_create_texture(8, 8, rgba, RD_TEXA_80_80, "rect rgba");
    const uint8_t px[2 * 4] = {1, 2, 3, 4, 5, 6, 7, 8};
    rd_update_texture_rect(u, 6, 7, 2, 1, px);
    const RdTexRec *ur = rd__tex_rec(u.id);
    CHECK(ur && ur->format == RD_TEXEL_RGBA8 && memcmp(ur->pixels + (7 * 8 + 6) * 4, px, 8) == 0 &&
              ur->pixels[(7 * 8 + 5) * 4] == 0,
          "RGBA8 rectangle");
    rd_destroy_texture(u);

    /* a sheet texture is one byte a texel, keeps its style
     * (the rim as its weight, 64 full, and dither as 0 or 1) and takes R8's
     * rectangles */
    const RdSheetStyle fr = {7, 62, 0xF0, 3, 0, 1};
    RdTex sh = rd_create_texture_sheet(W, H, cov, &fr, "sheet");
    RdTexRec *sr = rd__tex_rec(sh.id);
    CHECK(sr && sr->kind == RD_TEXKIND_IMAGE && sr->format == RD_TEXEL_SHEET && sr->w == W &&
              sr->h == H && memcmp(sr->pixels, cov, sizeof(cov)) == 0 &&
              rd__texel_bytes(sr->format) == 1 && strcmp(sr->name, "sheet") == 0,
          "sheet record");
    CHECK(sr && sr->sheet[0] == 64 && sr->sheet[1] == 62 && sr->sheet[2] == 0xF0 &&
              sr->sheet[3] == 1,
          "sheet style kept");
    CHECK(rd__texel_rhi_format(RD_TEXEL_SHEET) == RHI_FMT_R8_UNORM &&
              rd__texel_is_coverage(RD_TEXEL_SHEET) && !rd__texel_is_block(RD_TEXEL_SHEET),
          "sheet texels: R8, coverage");
    if (sr) {
        rd_update_texture_rect(sh, 2, 3, 4, 3, a);
        rd_update_texture_rect(sh, 34, 21, 5, 5, b);
        bad = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint8_t want = r8At(x, y);
                if (x >= 2 && x < 6 && y >= 3 && y < 6) {
                    want = 0xA5;
                } else if (x >= 34 && y >= 21) {
                    want = 0x3C;
                }
                bad += sr->pixels[y * W + x] != want;
            }
        }
        CHECK(bad == 0 && sr->dirty && sr->dirtyX0 == 0 && sr->dirtyX1 == W,
              "sheet rectangles: %d texels wrong (dirty whole since the create)", bad);
        const RdSheetStyle en = {1, 0, 0xFF, 0, 0, 1};
        rd_set_texture_sheet_style(sh, &en);
        CHECK(sr->sheet[0] == 64 && sr->sheet[1] == 0 && sr->sheet[2] == 0xFF && sr->sheet[3] == 0,
              "rd_set_texture_sheet_style");
        const RdSheetStyle faint = {1, 0, 0xFF, 0, 21, 1};
        rd_set_texture_sheet_style(sh, &faint);
        CHECK(sr->sheet[0] == 21, "a faint rim's weight kept (%u)", sr->sheet[0]);
        rd_set_texture_sheet_style(sh, NULL);
        CHECK(sr->sheet[0] == 64 && sr->sheet[1] == 0 && sr->sheet[2] == 0xFF && sr->sheet[3] == 1,
              "the default sheet style");
    }
    rd_destroy_texture(sh);
    RdTex r8 = rd_create_texture_r8(4, 4, NULL, "not a sheet");
    const RdSheetStyle grey = {1, 62, 0xFF, 1, 0, 1};
    rd_set_texture_sheet_style(r8, &grey);
    const RdTexRec *r8r = rd__tex_rec(r8.id);
    CHECK(r8r && r8r->format == RD_TEXEL_R8 && r8r->sheet[1] == 0,
          "rd_set_texture_sheet_style ignores an R8 texture");
    rd_destroy_texture(r8);
}

static void r8Pixels(void)
{
    enum { W = 37, H = 23 };

    static uint8_t cov[W * H], got[W * H];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            cov[y * W + x] = r8At(x, y);
        }
    }
    RdTex t = rd_create_texture_r8(W, H, cov, "r8 gpu");
    rd_begin_frame();
    rd_end_frame(0); /* the replay uploads it */
    uint32_t w = 0, h = 0;
    CHECK(rd__read_texture(t, got, sizeof(got), &w, &h) && w == W && h == H &&
              memcmp(got, cov, sizeof(cov)) == 0,
          "R8 texture reads back as created");
    RdTexRec *r = rd__tex_rec(t.id);
    if (!r) {
        return;
    }
    /* outside the coming union, behind rd's back: must not reach the GPU */
    r->pixels[0] ^= 0xFF;
    r->pixels[(H - 1) * W + W - 1] ^= 0xFF;
    const uint8_t a[3 * 2] = {10, 20, 30, 40, 50, 60}, b[2 * 2] = {7, 8, 9, 11};
    rd_update_texture_rect(t, 4, 5, 3, 2, a);
    rd_update_texture_rect(t, 9, 9, 2, 2, b);
    rd_begin_frame();
    rd_end_frame(0);
    CHECK(rd__read_texture(t, got, sizeof(got), &w, &h), "R8 readback after the rectangles");
    int bad = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const int in = x >= 4 && x < 11 && y >= 5 && y < 11;
            const uint8_t want = in ? r->pixels[y * W + x] : cov[y * W + x];
            bad += got[y * W + x] != want;
        }
    }
    CHECK(bad == 0, "R8 rectangle upload: %d texels differ (the union 4,5-11,11 uploaded alone)",
          bad);
    CHECK(got[(5 * W) + 4] == 10 && got[(10 * W) + 10] == 11, "the rectangles' texels on the GPU");
    rd_destroy_texture(t);
}

/* Photo mode: the game is paused (systemStatus[5]) and every
 * texture scroll with it, but the film grain's texture (sandstorm_spr)
 * keeps scrolling so a capture holds a fresh pattern */
static void photoGrainChecks(void)
{
    Tim2Ext scroll = icoBlock(0, 0);
    scroll.scrlU = 0.25f;
    const Made grain = makeIndexed("sandstorm_spr", 8, 0, 0, 16, 16, &scroll);
    const Made other = makeIndexed("grain_other", 8, 0, 0, 16, 16, &scroll);
    CHECK(grain.id >= 0 && other.id >= 0, "grain: textures made");
    TexExt *g = tex_GetTexExtData(grain.id);
    TexExt *o = tex_GetTexExtData(other.id);

    systemStatus[5] = 1;
    tex_ResetVram();
    CHECK(g->frame == 0 && o->frame == 0, "grain: paused, photo mode off: nothing scrolls (%u %u)",
          g->frame, o->frame);
    ico_photo_enter();
    tex_ResetVram();
    CHECK(g->frame == 1, "grain: paused in photo mode: the grain texture scrolls (%u)", g->frame);
    CHECK(o->frame == 0, "grain: paused in photo mode: another texture stays (%u)", o->frame);
    tex_ResetVram();
    CHECK(g->frame == 2, "grain: a second photo frame scrolls again (%u)", g->frame);
    ico_photo_exit();
    systemStatus[5] = 0;
    tex_ResetVram();
    CHECK(g->frame == 3 && o->frame == 1, "grain: unpaused: both scroll once (%u %u)", g->frame,
          o->frame);
}

/* Issue 174.  tex_Init gives the next stage the slots of the stage before
 * (TexExt.partition not 0; nothing frees them), and a load into such a slot
 * whose picture is in the cache used to re-expand the old rd texture in place
 * when the size and format matched: a frame recorded before the load and
 * replayed after it (a later present of the tick, a picture for a bug report)
 * drew the old stage with the new stage's texels.  Now the new picture gets a
 * texture of its own and the old one keeps its texels until two frame ticks
 * later, as after tex_FreeTexture. */
static int texelsAre(uint32_t texId, const uint8_t *ref, int w, int h)
{
    const RdTexRec *t = rd__tex_rec(texId);

    return t != NULL && t->pixels != NULL && (int)t->w == w && (int)t->h == h &&
           memcmp(t->pixels, ref, (size_t)w * h * 4) == 0;
}

static void slotReloadChecks(void)
{
    const RdTexCacheStats *st = rdtex_stats();

    s_partition = 1; /* the stage's textures */
    Made a = makeDirectSalt("reload_stage_a", 32, 16, 16, NULL, 1);
    s_partition = 0;
    CHECK(a.id >= 0, "reload: the first stage's texture loaded");
    rdtex_settle();
    const uint32_t ta = tex_HostTextureId(a.id);
    CHECK(ta != 0 && texelsAre(ta, a.ref, 16, 16), "reload: the first picture decoded");

    /* the next stage: the slot again, a picture of the same size and format */
    tex_Init();
    const uint32_t creates = st->creates, updates = st->updates;
    s_partition = 1;
    Made b = makeDirectSalt("reload_stage_b", 32, 16, 16, NULL, 2);
    s_partition = 0;
    CHECK(b.id == a.id, "reload: the next stage's texture takes the same slot (%d, %d)", b.id,
          a.id);
    CHECK(memcmp(a.ref, b.ref, (size_t)16 * 16 * 4) != 0, "reload: the two pictures differ");
    rdtex_settle();
    const uint32_t tb = tex_HostTextureId(b.id);
    CHECK(tb != 0 && tb != ta, "reload: the new picture has a texture of its own (%u, old %u)", tb,
          ta);
    CHECK(st->creates == creates + 1 && st->updates == updates,
          "reload: one texture made, none re-expanded in place (creates +%u, updates +%u)",
          st->creates - creates, st->updates - updates);
    CHECK(texelsAre(tb, b.ref, 16, 16), "reload: the new texture holds the new picture");
    CHECK(texelsAre(ta, a.ref, 16, 16),
          "reload: the old texture keeps the old picture for the frames that drew it");
    CHECK(tex_HostTextureId(b.id) == tb, "reload: the slot's entry is the new picture's");
    rdtex_frame_tick();
    CHECK(texelsAre(ta, a.ref, 16, 16), "reload: the old texture still lives after one frame tick");
    rdtex_frame_tick();
    CHECK(rd__tex_rec(ta) == NULL, "reload: the old texture destroyed after two frame ticks");
    CHECK(texelsAre(tb, b.ref, 16, 16), "reload: the new texture lives on");
    /* a frame's tex_ResetVram writes the reload's log line once and counts afresh */
    tex_ResetVram();
    free(a.ref);
    free(b.ref);
}

int main(void)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    systemStatus[1] = 1;
    GlobalStageSetting.texSampleMode = 1;
    makePalettes();
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    dl_Init();
    tex_Init();
    decodeChecks();
    samplerChecks();
    scrollChecks();
    cacheChecks();
    r8Checks();
    textUploadLogChecks();
    replacementChecks();
    photoGrainChecks();
    slotReloadChecks();
    dl_Clear();
    recordFrame();
    {
        const RdFrame *f = rd__last_frame();
        CHECK(f != NULL, "a closed frame");
        if (f) {
            checkRecording(f);
        }
    }
    rd_shutdown();
    rdtex_reset();
    if (failures) {
        printf("rd_tex_test: %d failures\n", failures);
        return 1;
    }

    /* the same frame on a device */
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(512, 512, &st, NULL)) {
        printf("rd_tex_test: CPU checks ok; SKIP the pixel check: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    recordFrame();
    checkPixels();
    threadPixels();
    threadSync();
    threadSlotAhead();
    threadFair();
    r8Pixels();
    replacementPixels();
    threadRoomAhead(); /* it gives the output a size */
    threadOriginal();  /* after it: its pictures present */
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    rdtex_reset();
    if (failures) {
        printf("rd_tex_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_tex_test: ok\n");
    return 0;
}
