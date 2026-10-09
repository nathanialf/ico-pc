/* rd_fog_test.c: the depth fog of fog_DrawFog.
 *
 * ZFog.c with the 2D layer (GifPacket.c, DisplayList.c, DmaPacket.c),
 * compiled as the window build has them (ICO_HOST, ICO_RD); the rest of the
 * game is stubbed below.  No disc data.
 *
 * CPU only:
 *   s  the Z byte: a small model of GS local memory (the PSMCT32, PSMZ32
 *      and PSMT4 page, block and column layouts of the GS User's Manual)
 *      runs fog_DrawFog's transfers (the Z buffer PSMZ32 -> PSMCT32 at
 *      0x2800, then the PSMT4 8-pixel copies x 16+32j -> 24+32j of every
 *      band) over a 512 x 512 Z buffer of arbitrary 32-bit values, and
 *      reads the copy as PSMT8H: every pixel's index equals bits 16..23 of
 *      its Z (without the PSMT4 copies it would be bits 24..31);
 *   c  the CLUT: the storage order fog_MakeFogClut writes, read through the
 *      CSM1 lookup (index n at storage n with bits 3 and 4 swapped), is the
 *      logical table clut[255 - i] = f(i), and ZFog.c's host path passes
 *      exactly that table to rd_Post;
 *   r  the recording, in list 4: fog_DrawFog's register writes as rd state in packet
 *      order, the RD_POST_FOG record (sprite corners, UVs, RGBAQ, Z, LUT),
 *      the fogOffsetA sprite and the restores.
 * On a Vulkan device (exit 77 without one, after the CPU checks):
 *   p  a 16 x 16 grid of quads at known GS Z (fog indices 0..255 spread
 *      over the cells, Z above 0xFFFFFF, the 0xFFFFFF tie, the clear at Z
 *      0) is fogged by fog_DrawFog; every pixel equals the CPU reference
 *      (index = Z >> 16 where Z <= 0xFFFFFF, LUT, MODULATE by the strength,
 *      the GS LERP with As, alpha As) within 1 LSB per blend; with
 *      fogOffsetA the flat quad on top within 2;
 *   d  the fog frame dumped, loaded and replayed again gives the same pixels.
 * Every pipeline created is enumerated; no validation errors; no stubbed
 * command replayed. */
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
#include "ZFog.h"
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
/* video_options.c's Fog switch (issue 11) */
static int s_fogSwitch = 1;

int ico_video_effect_fog(void)
{
    return s_fogSwitch;
}

int ico_video_effect_cinematic_bars(void)
{
    return 1;
}

StageSetting GlobalStageSetting;
PadState pad[16];

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

static int s_vramAllocs, s_vramResets;

int tex_AllocVramAuto(int kind, int size)
{
    (void)kind, (void)size;
    s_vramAllocs++;
    return 0x3F00;
}

/* Texture.c's resetVramPri selects the list it resets (dl_SetDLPriority,
   Texture.c:1804): that is what puts the fog into list 4 */
void tex_ResetVramPri(int pri)
{
    CHECK(pri == 4, "fog_DrawFog resets list 4's VRAM bookkeeping, got %d", pri);
    dl_SetDLPriority(pri);
    s_vramResets++;
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

#define W 512
#define H 512

/* ================================================== (s) the GS swizzle
 * GS local memory: 4 MB, 8 KB pages of 32 blocks of 256 bytes, each block
 * four 64-byte columns (GS User's Manual, "Local Memory": page, block and
 * column arrangements).  The tables are the documented arrangements: block
 * number at each block position of a page, and the word (PSMCT32/Z32) or
 * nibble (PSMT4) of a column at each pixel position of the column.  The
 * PSMT4 column (32 x 4 pixels, 128 nibbles) is given for the even column
 * (rows 0..3) and the odd column (rows 4..7); nibble n of a column is
 * nibble n & 7 (bits 4 (n & 7) .. +3) of the column's word n >> 3, and the
 * column's word k is the one the PSMCT32 column table numbers k.  Checked
 * against the swizzle visualiser of T. Krinkle (gist bd6c6e17...), whose
 * tables agree with the manual's figures. */
static uint32_t s_vram[1u << 20];

/* PSMCT32: 8 x 4 blocks of 8 x 8 pixels per 64 x 32 page */
static const uint8_t kPageCT32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21},
                                        {2, 3, 6, 7, 18, 19, 22, 23},
                                        {8, 9, 12, 13, 24, 25, 28, 29},
                                        {10, 11, 14, 15, 26, 27, 30, 31}};

/* PSMZ32: the same page shape, other block numbers */
static const uint8_t kPageZ32[4][8] = {{24, 25, 28, 29, 8, 9, 12, 13},
                                       {26, 27, 30, 31, 10, 11, 14, 15},
                                       {16, 17, 20, 21, 0, 1, 4, 5},
                                       {18, 19, 22, 23, 2, 3, 6, 7}};

/* PSMT4: 4 x 8 blocks of 32 x 16 pixels per 128 x 128 page (the PSMCT16
 * arrangement) */
static const uint8_t kPageT4[8][4] = {{0, 2, 8, 10},    {1, 3, 9, 11},    {4, 6, 12, 14},
                                      {5, 7, 13, 15},   {16, 18, 24, 26}, {17, 19, 25, 27},
                                      {20, 22, 28, 30}, {21, 23, 29, 31}};

/* PSMCT32 / PSMZ32 column: 8 x 2 pixels, the word of each */
static const uint8_t kColCT32[2][8] = {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15}};

/* PSMT4 column: 32 x 4 pixels, the nibble of each; rows 0..3 even columns,
 * 4..7 odd columns */
static const uint8_t kColT4[8][32] = {
    {0, 8,  32, 40, 64, 72, 96,  104, 2, 10, 34, 42, 66, 74, 98,  106,
     4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
     20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97,  105, 1, 9,  33, 41, 67, 75, 99,  107, 3, 11, 35, 43,
     69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
     85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63},
    {64, 72, 96,  104, 0, 8,  32, 40, 66, 74, 98,  106, 2, 10, 34, 42,
     68, 76, 100, 108, 4, 12, 36, 44, 70, 78, 102, 110, 6, 14, 38, 46},
    {80, 88, 112, 120, 16, 24, 48, 56, 82, 90, 114, 122, 18, 26, 50, 58,
     84, 92, 116, 124, 20, 28, 52, 60, 86, 94, 118, 126, 22, 30, 54, 62},
    {1, 9,  33, 41, 65, 73, 97,  105, 3, 11, 35, 43, 67, 75, 99,  107,
     5, 13, 37, 45, 69, 77, 101, 109, 7, 15, 39, 47, 71, 79, 103, 111},
    {17, 25, 49, 57, 81, 89, 113, 121, 19, 27, 51, 59, 83, 91, 115, 123,
     21, 29, 53, 61, 85, 93, 117, 125, 23, 31, 55, 63, 87, 95, 119, 127}};

enum { PSMCT32 = 0x00, PSMT8H = 0x1B, PSMT4 = 0x14, PSMZ32 = 0x30 };

/* word address of a 32-bit pixel; bp in blocks (64 words), bw in units of
 * 64 pixels (one PSMCT32 page across) */
static uint32_t addr32(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    const uint8_t (*page)[8] = psm == PSMZ32 ? kPageZ32 : kPageCT32;
    const uint32_t pg = (y >> 5) * bw + (x >> 6);
    const uint32_t blk = page[(y >> 3) & 3][(x >> 3) & 7];
    const uint32_t col = (y >> 1) & 3;
    return (bp + pg * 32 + blk) * 64 + col * 16 + kColCT32[y & 1][x & 7];
}

/* nibble address of a PSMT4 pixel (a PSMT4 page is 128 pixels across, two
 * units of bw) */
static uint32_t addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    const uint32_t pg = (y >> 7) * (bw >> 1) + (x >> 7);
    const uint32_t blk = kPageT4[(y >> 4) & 7][(x >> 5) & 3];
    const uint32_t col = (y >> 2) & 3;
    return ((bp + pg * 32 + blk) * 64 + col * 16) * 8 + kColT4[(col & 1) * 4 + (y & 3)][x & 31];
}

static uint32_t rd4(uint32_t a)
{
    return (s_vram[a >> 3] >> ((a & 7) * 4)) & 0xF;
}

static void wr4(uint32_t a, uint32_t v)
{
    const uint32_t sh = (a & 7) * 4;
    s_vram[a >> 3] = (s_vram[a >> 3] & ~(0xFu << sh)) | ((v & 0xF) << sh);
}

/* BITBLTBUF/TRXPOS/TRXREG/TRXDIR 2: a local-to-local transfer, pixel by
 * pixel in raster order */
static void transfer(int spsm, uint32_t sbp, uint32_t sbw, uint32_t sx, uint32_t sy, int dpsm,
                     uint32_t dbp, uint32_t dbw, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h)
{
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            if (spsm == PSMT4) {
                wr4(addr4(dbp, dbw, dx + x, dy + y), rd4(addr4(sbp, sbw, sx + x, sy + y)));
            } else {
                s_vram[addr32(dpsm, dbp, dbw, dx + x, dy + y)] =
                    s_vram[addr32(spsm, sbp, sbw, sx + x, sy + y)];
            }
        }
    }
}

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void checkSwizzle(void)
{
    static uint32_t z[H][W];
    memset(s_vram, 0, sizeof(s_vram));
    /* the scene's Z buffer: ZBUF ZBP 0xC0 (block 0x1800), PSMZ32, 512 wide */
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            z[y][x] = hash32(y * W + x + 1);
            s_vram[addr32(PSMZ32, 0x1800, W / 64, x, y)] = z[y][x];
        }
    }
    /* fog_DrawFog's second packet: the Z buffer into 0x2800 as PSMCT32 */
    transfer(PSMZ32, 0x1800, W / 64, 0, 0, PSMCT32, 0x2800, W / 64, 0, 0, W, H);
    /* the PSMCT32 copy read before the PSMT4 copies: index = bits 24..31 */
    int before = 0;
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            before += (s_vram[addr32(PSMCT32, 0x2800, W / 64, x, y)] >> 24) == (z[y][x] >> 24);
        }
    }
    CHECK(before == W * H, "the PSMCT32 copy holds Z pixel for pixel: %d of %d", before, W * H);
    /* per 32-line band i (ScreenHeight / 32 of them), four PSMT4 copies of an
     * 8-pixel column at x 16 + 32 j to x 24 + 32 j, buffer width 2 (128
     * PSMT4 pixels), base i * ScreenWidth / 2 + 0x2800, ScreenWidth / 2 * 4
     * lines */
    for (uint32_t i = 0; i < H / 32; i++) {
        for (uint32_t j = 0; j < 4; j++) {
            const uint32_t bp = i * (W / 2) + 0x2800;
            transfer(PSMT4, bp, 2, 16 + j * 32, 0, PSMT4, bp, 2, 16 + j * 32 + 8, 0, 8, W / 2 * 4);
        }
    }
    /* TEX0: TBP 0x2800, TBW ScreenWidth / 64, PSMT8H: the top byte of each
     * PSMCT32 word */
    int byte2 = 0, other = 0;
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            const uint32_t w = s_vram[addr32(PSMCT32, 0x2800, W / 64, x, y)];
            const uint32_t idx = w >> 24;
            byte2 += idx == ((z[y][x] >> 16) & 0xFF);
            /* the rest of the word: bytes 0..2 of Z unchanged */
            other += (w & 0xFFFFFF) == (z[y][x] & 0xFFFFFF);
        }
    }
    CHECK(byte2 == W * H, "PSMT8H index = Z bits 16..23 at %d of %d pixels", byte2, W * H);
    CHECK(other == W * H, "Z bytes 0..2 untouched at %d of %d pixels", other, W * H);
    static const uint32_t pts[][2] = {{0, 0},   {7, 1},   {8, 0},    {16, 2},   {24, 3},
                                      {63, 31}, {64, 32}, {200, 77}, {511, 511}};
    for (size_t k = 0; k < sizeof(pts) / sizeof(pts[0]); k++) {
        const uint32_t x = pts[k][0], y = pts[k][1];
        const uint32_t w = s_vram[addr32(PSMCT32, 0x2800, W / 64, x, y)];
        printf("  (s) pixel (%3u,%3u): Z %08X  PSMT8H index %02X  (Z >> 16) & 0xFF %02X\n", x, y,
               z[y][x], w >> 24, (z[y][x] >> 16) & 0xFF);
    }
    printf("  (s) index = Z bits 16..23 at %d of %d pixels (bits 24..31 before the PSMT4 copies)\n",
           byte2, W * H);
}

/* ===================================================== (c) the CLUT */
static const int kFog[2][9] = {
    /* on, R, G, B, A, offsetA, near, far, strength */
    {1, 200, 150, 100, 250, 0, 40, 200, 0x80},
    {1, 90, 120, 230, 128, 0x30, 10, 120, 0xFF},
};

static void setFog(int k)
{
    GlobalStageSetting.fogOn = kFog[k][0];
    GlobalStageSetting.fogColR = kFog[k][1];
    GlobalStageSetting.fogColG = kFog[k][2];
    GlobalStageSetting.fogColB = kFog[k][3];
    GlobalStageSetting.fogColA = kFog[k][4];
    GlobalStageSetting.fogOffsetA = kFog[k][5];
    GlobalStageSetting.fogNear = kFog[k][6];
    GlobalStageSetting.fogFar = kFog[k][7];
    GlobalStageSetting.fogStrength = kFog[k][8];
}

/* the logical table: entry 255 - i is f(i) (fog_MakeFogClut) */
static void logicalLut(int k, uint8_t lut[256 * 4])
{
    const int near = kFog[k][6], far = kFog[k][7], a = kFog[k][4];
    for (int i = 0; i < 256; i++) {
        float v;
        if (i <= near) {
            v = 0.0f;
        } else if (i > far) {
            v = (float)(a / 2);
        } else {
            v = (float)(a * (i - near) / (far - near) / 2);
        }
        uint8_t *e = &lut[(255 - i) * 4];
        e[0] = (uint8_t)kFog[k][1];
        e[1] = (uint8_t)kFog[k][2];
        e[2] = (uint8_t)kFog[k][3];
        e[3] = (uint8_t)(int)v;
    }
}

/* fogClutPacket is static in ZFog.c: the CLUT words are found in the packet
 * fog_DrawFog's first DMA would send; on the host the packet is not sent,
 * so the storage order is read back from the table ZFog.c hands rd_Post and
 * from a CSM1 lookup of an independently swizzled copy */
static void checkClut(int k, const uint8_t *got)
{
    uint8_t want[256 * 4];
    logicalLut(k, want);
    CHECK(memcmp(got, want, sizeof(want)) == 0,
          "case %d: the LUT ZFog.c passes is the logical "
          "table clut[255 - i] = f(i) in index order",
          k);
    /* CSM1: the storage order fog_MakeFogClut writes (the middle 8-entry
     * blocks of every 32 traded), looked up as the GS does (index n at
     * storage entry n with bits 3 and 4 swapped) */
    uint32_t stor[256];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 2; j++) {
            for (int kk = 0; kk < 2; kk++) {
                for (int l = 0; l < 8; l++) {
                    const uint8_t *e = &want[(i * 32 + j * 16 + kk * 8 + l) * 4];
                    stor[i * 32 + kk * 16 + j * 8 + l] = (uint32_t)e[0] | ((uint32_t)e[1] << 8) |
                                                         ((uint32_t)e[2] << 16) |
                                                         ((uint32_t)e[3] << 24);
                }
            }
        }
    }
    int same = 0;
    for (int n = 0; n < 256; n++) {
        const int x = (n & 7) | ((n & 0x10) >> 1), y = ((n >> 4) & 0xE) | ((n >> 3) & 1);
        const uint32_t w = stor[y * 16 + x];
        same += w == ((uint32_t)got[n * 4] | ((uint32_t)got[n * 4 + 1] << 8) |
                      ((uint32_t)got[n * 4 + 2] << 16) | ((uint32_t)got[n * 4 + 3] << 24));
    }
    CHECK(same == 256, "case %d: CSM1 lookup of the storage order = the LUT at %d of 256", k, same);
}

/* ================================================= (r) the recording */
static const RdCmd *nextCmd(const RdCmdList *cl, uint32_t *i, uint8_t type, const char *what)
{
    while (*i < cl->count && cl->cmds[*i].type == RDC_NOP) {
        (*i)++;
    }
    if (*i >= cl->count) {
        CHECK(0, "list ends before %s", what);
        return NULL;
    }
    const RdCmd *c = &cl->cmds[(*i)++];
    CHECK(c->type == type, "%s: command %u has type %u, expected %u", what, *i - 1, c->type, type);
    return c->type == type ? c : NULL;
}

static void expectTest(const RdCmdList *cl, uint32_t *i, uint64_t gs, const char *what)
{
    const RdCmd *c = nextCmd(cl, i, RDC_TEST, what);
    const RdTestState t = rd_TestFromGs(gs);
    CHECK(c && c->b[0] == t.ate && c->b[1] == t.atst && c->b[2] == t.aref && c->b[3] == t.afail &&
              c->b[4] == t.date && c->b[5] == t.zte && c->b[6] == t.ztst,
          "%s: TEST 0x%llx", what, (unsigned long long)gs);
}

static void expect1(const RdCmdList *cl, uint32_t *i, uint8_t type, int v, const char *what)
{
    const RdCmd *c = nextCmd(cl, i, type, what);
    CHECK(c && c->b[0] == (uint8_t)v, "%s: %d, got %d", what, v, c ? c->b[0] : -1);
}

static void expectScene(const RdCmdList *cl, uint32_t *i, const char *what)
{
    const RdCmd *c = nextCmd(cl, i, RDC_TARGET, what);
    const uint32_t s = rd_Target(RD_TARGET_SCENE).id;
    CHECK(c && c->u[0] == s && c->u[1] == s && c->u[2] == (W | (H << 16)) &&
              c->b[0] == RD_TARGET_OFFSET,
          "%s: SCENE with its depth, %ux%u", what, W, H);
}

static uint8_t s_lutSeen[256 * 4];

/* fog_DrawFog in list 4 of a frame of its own; the commands checked */
static void checkRecording(int k)
{
    setFog(k);
    fog_MakeFogClut();
    dl_SetDLPriority(3); /* where shadow_Draw leaves it */
    const int resets = s_vramResets;
    fog_DrawFog();
    CHECK(dl_GetPri() == 4, "the fog is recorded in list 4");
    CHECK(s_vramResets == resets + 1, "tex_ResetVramPri(4) kept on the host");
    dl_Swap();
    const RdFrame *f = rd__LastFrame();
    const RdCmdList *cl = &f->lists[4];
    uint32_t i = 0;
    /* skip the list defaults rd_BeginFrame records */
    while (i < cl->count && cl->cmds[i].type != RDC_TARGET) {
        i++;
    }
    expectScene(cl, &i, "FRAME 0x40");
    expect1(cl, &i, RDC_PABE, 0, "PABE 0");
    const RdCmd *c = nextCmd(cl, &i, RDC_ALPHA, "ALPHA");
    CHECK(c && c->b[0] == RD_BLEND_LERP_AS && c->b[1] == 128, "ALPHA 0x44 FIX 128");
    c = nextCmd(cl, &i, RDC_TEXTURE, "TEX0");
    const RdTexRec *tr = c ? rd__TexRec(c->u[0]) : NULL;
    CHECK(tr && tr->kind == RD_TEXKIND_TARGET && tr->target == rd_Target(RD_TARGET_SCENE).id &&
              tr->view == RD_VIEW_DEPTH && c->b[0] == RD_TEXFN_MODULATE && c->b[1] == RD_TCC_RGBA,
          "TEX0: SCENE's depth view, MODULATE, TCC RGBA");
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_OFF, "ZBUF ZMSK");
    expectTest(cl, &i, 0x50000, "TEST 0x50000");
    c = nextCmd(cl, &i, RDC_FILTER, "TEX1 0");
    CHECK(c && c->b[0] == RD_FILTER_NEAREST && c->b[1] == RD_FILTER_NEAREST, "TEX1 0: nearest");
    expect1(cl, &i, RDC_ABE, 1, "PRIM 0x156 ABE");
    expect1(cl, &i, RDC_SHADE, 0, "PRIM 0x156 flat");
    c = nextCmd(cl, &i, RDC_POST_STUB, "the fog sprite");
    if (c) {
        CHECK(c->b[0] == RD_POST_FOG, "RD_POST_FOG");
        RdPostRec r;
        memcpy(&r, f->payload + c->u[1], sizeof(r));
        CHECK(r.rgba[0] == 0x80 && r.rgba[1] == 0x80 && r.rgba[2] == 0x80 &&
                  r.rgba[3] == (uint8_t)kFog[k][8] && r.z == 0xFFFFFF,
              "RGBAQ (0x80, 0x80, 0x80, fogStrength), Z 0xFFFFFF");
        CHECK(r.rect[0] == (float)(0x8000 - W / 2 * 16) &&
                  r.rect[1] == (float)(0x8000 - H / 2 * 16) &&
                  r.rect[2] == (float)(0x8000 + W / 2 * 16) &&
                  r.rect[3] == (float)(0x8000 + H / 2 * 16),
              "corners (%g,%g)-(%g,%g)", r.rect[0], r.rect[1], r.rect[2], r.rect[3]);
        CHECK(r.uv[0] == 8.0f && r.uv[1] == 8.0f && r.uv[2] == (float)(8 + W * 16) &&
                  r.uv[3] == (float)(8 + H * 16),
              "UV 0.5 .. W + 0.5");
        CHECK(r.lutOffset != ~0u, "LUT in the payload");
        if (r.lutOffset != ~0u) {
            memcpy(s_lutSeen, f->payload + r.lutOffset, sizeof(s_lutSeen));
            checkClut(k, s_lutSeen);
        }
    }
    c = nextCmd(cl, &i, RDC_FILTER, "TEX1 0x60");
    CHECK(c && c->b[0] == RD_FILTER_LINEAR && c->b[1] == RD_FILTER_LINEAR, "TEX1 0x60: linear");
    if (kFog[k][5] > 0) {
        expectTest(cl, &i, 0x30000, "TEST 0x30000");
        expect1(cl, &i, RDC_ABE, 1, "PRIM 0x446 ABE");
        expect1(cl, &i, RDC_SHADE, 0, "PRIM 0x446 flat");
        nextCmd(cl, &i, RDC_TEXTURE_OFF, "PRIM 0x446 TME off");
        c = nextCmd(cl, &i, RDC_SCREEN, "the fogOffsetA sprite");
        if (c) {
            const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
            CHECK(c->b[0] == RD_PRIM_SPRITES && c->u[1] == 2 && v[1].z == 0xFFFFFFFFu &&
                      v[1].rgba[0] == kFog[k][1] && v[1].rgba[1] == kFog[k][2] &&
                      v[1].rgba[2] == kFog[k][3] && v[1].rgba[3] == kFog[k][5] &&
                      v[0].x == 0x8000 - W / 2 * 16 && v[1].x == 0x8000 + W / 2 * 16,
                  "fogOffsetA sprite: fog colour, alpha fogOffsetA, Z 0xFFFFFFFF, full scene");
        }
        expectTest(cl, &i, 0x50000, "TEST 0x50000 again");
    }
    expect1(cl, &i, RDC_ZWRITE, RD_ZWRITE_ON, "ZBUF write on");
    expectScene(cl, &i, "FRAME 0x40 with the field offset");
    /* fogOn 0: nothing */
    GlobalStageSetting.fogOn = 0;
    dl_SetDLPriority(3);
    const uint32_t b3 = rd__RecFrame() ? rd__RecFrame()->lists[3].count : 0;
    const uint32_t b4 = rd__RecFrame() ? rd__RecFrame()->lists[4].count : 0;
    fog_DrawFog();
    CHECK(rd__RecFrame() && rd__RecFrame()->lists[3].count == b3 &&
              rd__RecFrame()->lists[4].count == b4 && dl_GetPri() == 3,
          "fogOn 0 records nothing");
    dl_Swap();
    /* issue 11: Options > Effects > Fog off records nothing either, and no
       RD_POST_FOG */
    setFog(k);
    s_fogSwitch = 0;
    dl_SetDLPriority(3);
    const uint32_t o3 = rd__RecFrame() ? rd__RecFrame()->lists[3].count : 0;
    const uint32_t o4 = rd__RecFrame() ? rd__RecFrame()->lists[4].count : 0;
    fog_DrawFog();
    CHECK(rd__RecFrame() && rd__RecFrame()->lists[3].count == o3 &&
              rd__RecFrame()->lists[4].count == o4 && dl_GetPri() == 3,
          "fog off records nothing");
    dl_Swap();
    {
        const RdFrame *g = rd__LastFrame();
        int fogs = 0;
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            for (uint32_t j = 0; j < g->lists[l].count; j++) {
                fogs += g->lists[l].cmds[j].type == RDC_POST_STUB &&
                        g->lists[l].cmds[j].b[0] == RD_POST_FOG;
            }
        }
        CHECK(fogs == 0, "fog off: no RD_POST_FOG (%d)", fogs);
    }
    s_fogSwitch = 1;
}

/* ===================================================== (p) the pixels */
#define CELLS 16
#define CELL (W / CELLS)

static uint32_t s_cellZ[CELLS * CELLS];

static uint8_t s_cellCol[CELLS * CELLS][4];

static const uint8_t kClear[4] = {30, 60, 90, 0x80};

/* cell Z: indices 0..255 spread over most cells, the low 16 bits kept
 * 0x400 away from the index boundaries (the D32F depth carries Z to 256
 * near 2^24); some cells above 0xFFFFFF (not fogged), one at the 0xFFFFFF
 * tie (fogged: GEQUAL), a few left at the clear's Z 0 (index 0) */
static void buildCells(void)
{
    for (int k = 0; k < CELLS * CELLS; k++) {
        const uint32_t h = hash32((uint32_t)k + 77);
        uint32_t z;
        if (k % 17 == 5) {
            z = 0x01000400u + (h & 0x0FFFFF00u); /* above 0xFFFFFF */
        } else if (k == 200) {
            z = 0xFFFFFFu; /* the tie */
        } else if (k % 31 == 3) {
            z = 0; /* not drawn: the clear */
        } else {
            const uint32_t idx = (uint32_t)(k * 37 + 11) & 0xFF;
            z = (idx << 16) | (0x0400u + (h & 0xF7FFu));
        }
        s_cellZ[k] = z;
        s_cellCol[k][0] = (uint8_t)(h >> 8);
        s_cellCol[k][1] = (uint8_t)(h >> 16);
        s_cellCol[k][2] = (uint8_t)(h >> 24);
        s_cellCol[k][3] = 0x40;
    }
}

static RdScreenVtx sv(int px, int py, uint32_t z, const uint8_t *rgba)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - W / 2 + px) * 16;
    v.y = (2048 - H / 2 + py) * 16;
    v.z = z;
    v.q = 1.0f;
    memcpy(v.rgba, rgba, 4);
    return v;
}

static void recordScene(int k)
{
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), W, H, RD_TARGET_OFFSET);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), kClear, 1, 0);
    rd_TextureOff();
    rd_ABE(0);
    rd_TestGs(0x30000);
    rd_ZWrite(1);
    rd_FBA(0);
    for (int c = 0; c < CELLS * CELLS; c++) {
        if (s_cellZ[c] == 0) {
            continue;
        }
        const int x = (c % CELLS) * CELL, y = (c / CELLS) * CELL;
        RdScreenVtx v[2] = {sv(x, y, s_cellZ[c], s_cellCol[c]),
                            sv(x + CELL, y + CELL, s_cellZ[c], s_cellCol[c])};
        rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 0, 0);
    }
    setFog(k);
    fog_MakeFogClut();
    dl_SetDLPriority(3);
    fog_DrawFog();
    dl_Swap();
}

/* the GS LERP (Cs - Cd) * A >> 7 + Cd, arithmetic shift, clamped */
static int lerp(int cs, int cd, int a)
{
    int v = (((cs - cd) * a) >> 7) + cd;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static uint8_t s_px[W * H * 4];

static void checkPixels(int k, int tol, const char *what)
{
    uint8_t lut[256 * 4];
    logicalLut(k, lut);
    int maxd = 0, bad = 0, fogged = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const int c = (y / CELL) * CELLS + x / CELL;
            const uint32_t z = s_cellZ[c];
            int want[4];
            const uint8_t *src = z ? s_cellCol[c] : kClear;
            for (int ch = 0; ch < 4; ch++) {
                want[ch] = src[ch];
            }
            if (z <= 0xFFFFFFu) {
                /* PSMT8H index of the Z copy: Z bits 16..23 */
                const uint8_t *t = &lut[((z >> 16) & 0xFF) * 4];
                int as = (t[3] * kFog[k][8]) >> 7;
                as = as > 255 ? 255 : as;
                for (int ch = 0; ch < 3; ch++) {
                    want[ch] = lerp((t[ch] * 0x80) >> 7, want[ch], as > 0x80 ? 0x80 : as);
                }
                want[3] = as;
                fogged++;
            }
            if (kFog[k][5] > 0) {
                for (int ch = 0; ch < 3; ch++) {
                    want[ch] = lerp(kFog[k][1 + ch], want[ch], kFog[k][5]);
                }
                want[3] = kFog[k][5];
            }
            const uint8_t *g = &s_px[(y * W + x) * 4];
            int d = 0;
            for (int ch = 0; ch < 4; ch++) {
                const int e = abs((int)g[ch] - want[ch]);
                d = e > d ? e : d;
            }
            maxd = d > maxd ? d : maxd;
            if (d > tol) {
                if (bad < 5) {
                    printf("  pixel (%d,%d) Z %08X: got %u %u %u %u, want %d %d %d %d\n", x, y, z,
                           g[0], g[1], g[2], g[3], want[0], want[1], want[2], want[3]);
                }
                bad++;
            }
        }
    }
    printf("  (p) %s: %d fogged pixels, max difference %d LSB (tolerance %d)\n", what, fogged, maxd,
           tol);
    CHECK(bad == 0, "%s: %d pixels beyond %d LSB", what, bad, tol);
}

static bool readScene(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    bool ok = rd__ReadTarget(rd_Target(RD_TARGET_SCENE), dst, (size_t)W * H * 4, &w, &h) &&
              w == W && h == H;
    CHECK(ok, "SCENE readback");
    return ok;
}

static void checkDump(void)
{
    const char *path = "rd_fog_test.rddump";
    CHECK(rd__DumpFrame(rd__LastFrame(), path), "dump the fog frame");
    RdFrame g;
    memset(&g, 0, sizeof(g));
    if (!rd__LoadFrame(path, &g)) {
        CHECK(0, "load the fog frame");
        return;
    }
    static uint8_t again[W * H * 4];
    CHECK(rd__ReplayFrame(&g, 0, false), "replay the loaded frame");
    rhi_WaitIdle();
    if (readScene(again)) {
        int diff = 0;
        for (size_t i = 0; i < sizeof(again); i++) {
            diff += again[i] != s_px[i];
        }
        printf("  (d) dump -> load -> replay: %d bytes differ\n", diff);
        CHECK(diff == 0, "the replayed dump equals the recorded frame");
    }
    rd__FrameFree(&g);
    remove(path);
}

static void checkPipelines(void)
{
    static RdPipeKeyInt keys[512];
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipelines %u", n);
    int fog = 0;
    for (uint32_t i = 0; i < rd__PipelineCount(); i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < 512; j++) {
            found |= rd__PipeKeyEqual(&keys[j], k);
        }
        fog += k->fs == RD_FS_FOG;
        CHECK(found, "created pipeline %u (prog %u vs %u fs %u blend %u) is not enumerated", i,
              k->gs.program, k->vs, k->fs, k->gs.blend);
    }
    printf("  pipelines: %u created (%d fog), %u reachable\n", rd__PipelineCount(), fog, n);
    /* the fog's LERP takes a colour and an alpha pass in the
     * two-pass blend fallback (rd_fog_nodual) */
    const int wantFog = rd_NoDual() ? 2 : 1;
    CHECK(fog == wantFog, "%d fog pipeline(s), %d expected", fog, wantFog);
}

int main(void)
{
    rd__SetNotImplementedFatal(true); /* a stub command replayed stops the test */
    printf("rd_fog_test\n");
    checkSwizzle();

    /* (c), (r) recording */
    if (!rd__InitRecordOnly(W, H)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    dl_Init();
    for (int k = 0; k < 2; k++) {
        checkRecording(k);
    }
    rd_Shutdown();
    if (failures) {
        printf("rd_fog_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(W, H, &st, NULL)) {
        printf("rd_fog_test: CPU checks ok; SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();

    buildCells();
    recordScene(0);
    if (readScene(s_px)) {
        checkPixels(0, 1, "case 0 (strength 0x80)");
        checkDump();
    }
    recordScene(1);
    if (readScene(s_px)) {
        checkPixels(1, 2, "case 1 (strength 0xFF, fogOffsetA 0x30)");
    }

    checkPipelines();
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_Shutdown();
    if (failures) {
        printf("rd_fog_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_fog_test: ok\n");
    return 0;
}
