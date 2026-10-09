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
 *
 * Exit 0, 1 on a mismatch, 77 when there is no device (after the CPU
 * checks passed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
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

int malloc_GetPartition(void)
{
    return 0;
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

/* a direct texture: 16, 24 or 32 bits a texel */
static Made makeDirect(const char *name, int bpp, int w, int h, const Tim2Ext *ico)
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
        uint32_t v = hash((uint32_t)n * 13 + (uint32_t)bpp);
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

/* ----------------------------------------------------------------- R8 */

static uint8_t r8At(int x, int y)
{
    return (uint8_t)hash((uint32_t)(y * 64 + x) + 991u);
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
    replacementChecks();
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
    r8Pixels();
    replacementPixels();
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
