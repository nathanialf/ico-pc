#include "debug.h"
#include "GsBase.h"
#include <eekernel.h>
#include "typedef.h"
#include "Texture.h"
#include "ZFog.h"
#include "main.h"
#include "DmaPacket.h"
#include "DisplayList.h"

/* The fog CLUT upload packet: a VIF code quad (nop, nop, FLUSHA, DIRECT 65),
 * a GIFtag (EOP, NLOOP=64, FLG=IMAGE), the 256-entry 32-bit CLUT itself and a
 * trailing FLUSHA quad. */
typedef struct FogClutPacket { /* field names derived */
    unsigned int vif[4];       /* 0x000 */
    long long gif[2];          /* 0x010 */
    unsigned int clut[256];    /* 0x020 */
    unsigned int vifEnd[4];    /* 0x420 */
} FogClutPacket;               /* derived name */

static FogClutPacket fogClutPacket; /* derived name */

void fog_MakeFogClut(void)
{
    unsigned int buf[8][2][2][8];
    unsigned int *clut = fogClutPacket.clut;
    int i;
    int j;
    int k;
    int l;
    int near;
    int far;
    int r;
    int g;
    int b;
    int a;
    float v;

    near = GlobalStageSetting.fogNear;
    far = GlobalStageSetting.fogFar;

    r = GlobalStageSetting.fogColR;
    g = GlobalStageSetting.fogColG;
    b = GlobalStageSetting.fogColB;
    a = GlobalStageSetting.fogColA;

    fogClutPacket.vif[0] = 0;
    fogClutPacket.vif[1] = 0;
    fogClutPacket.vif[2] = 0x13000000;
    fogClutPacket.vif[3] = 0x50000041;
    fogClutPacket.gif[0] = 0x0800000000008040;
    fogClutPacket.gif[1] = 0;
    fogClutPacket.vifEnd[0] = 0x13000000;
    fogClutPacket.vifEnd[1] = 0;
    fogClutPacket.vifEnd[2] = 0;
    fogClutPacket.vifEnd[3] = 0;

    for (i = 0; i < 256; i++) {
        if (i <= near) {
            v = 0.0f;
        } else if (i > far) {
            v = a / 2;
        } else {
            v = a * (i - near) / (far - near) / 2;
        }
        clut[255 - i] = ((int)v << 24) | (b << 16) | (g << 8) | r;
    }

    /* GS 32-bit CLUT storage swizzle: the two middle 8-entry blocks of every
     * 32-entry run trade places. */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 2; k++) {
                for (l = 0; l < 8; l++) {
                    buf[i][k][j][l] = *clut++;
                }
            }
        }
    }

    for (i = 0; i < 256; i++) {
        fogClutPacket.clut[i] = ((unsigned int *)buf)[i];
    }

    FlushCache(0);
}

/* The packet writers fog_DrawFog uses, as macros: their bodies are
   gif_StartPacket's, gif_SetGsReg's and gif_EndPacket's own (GifPacket.c),
   the end one with its DMA kick. */
#define FOG_START_PACKET() /* derived name */                                                      \
    {                                                                                              \
        char *c = PacketBufferStruct.ptr.c;                                                        \
        PacketBufferStruct.gif.c = 0;                                                              \
        PacketBufferStruct.end.c = 0;                                                              \
        PacketBufferStruct.dma.c = c;                                                              \
        PacketBufferStruct.tail.c = c;                                                             \
        PacketBufferStruct.ptr.c = c + 8;                                                          \
        *(unsigned int *)(c + 8) = 0x11000000;                                                     \
        PacketBufferStruct.gif.c = c + 0xC;                                                        \
        PacketBufferStruct.end.c = c + 0x10;                                                       \
        PacketBufferStruct.ptr.c = c + 0x18;                                                       \
        ((GifPkWord *)(c + 0x18))->d = 0xE;                                                        \
        PacketBufferStruct.ptr.c = c + 0x20;                                                       \
    }
#define FOG_SET_GSREG(reg, val) /* derived name */                                                 \
    {                                                                                              \
        *PacketBufferStruct.ptr.d++ = (val);                                                       \
        *PacketBufferStruct.ptr.d++ = (reg);                                                       \
    }
#define FOG_END_PACKET() /* derived name */                                                        \
    {                                                                                              \
        char *p;                                                                                   \
        ((GifPkWord *)PacketBufferStruct.end.c)->d =                                               \
            (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.end.c) >> \
                            4) -                                                                   \
                           1) |                                                                    \
            0x1000000000008000LL;                                                                  \
        ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =                                            \
            ((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) |           \
            0x50000000;                                                                            \
        ((GifPkWord *)PacketBufferStruct.tail.c)->d =                                              \
            (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c -                             \
                                            PacketBufferStruct.tail.c) >>                          \
                             4) -                                                                  \
                            1) |                                                                   \
                           0x10000000);                                                            \
        p = PacketBufferStruct.ptr.c;                                                              \
        PacketBufferStruct.tail.c = p;                                                             \
        ((GifPkWord *)p)->d = 0x60000000;                                                          \
        PacketBufferStruct.ptr.c = p + 8;                                                          \
        ((GifPkWord *)(p + 8))->w[0] = 0;                                                          \
        PacketBufferStruct.ptr.c = p + 0xC;                                                        \
        ((GifPkWord *)(p + 8))->w[1] = 0;                                                          \
        PacketBufferStruct.ptr.c = p + 0x10;                                                       \
        dl_OpenDma(5, PacketBufferStruct.dma.c, 0);                                                \
        dl_CloseDma();                                                                             \
    }
/* FRAME_1, SCISSOR_1 and XYOFFSET_1 for a w by h buffer at base fbp, the
   window centred on the GS's 2048.0 origin and moved by ox, oy sixteenths:
   src/Shadow.c's setFrame. */
#define FOG_SET_FRAME(fbp, w, h, ox, oy) /* derived name */                                        \
    {                                                                                              \
        FOG_SET_GSREG(0x4C, (fbp) | ((long long)(((w) >> 6) & 0x3F) << 16));                       \
        FOG_SET_GSREG(0x40, ((long long)((w) - 1) << 16) | ((long long)((h) - 1) << 48));          \
        FOG_SET_GSREG(0x18, (((long long)(2048 - (w) / 2) << 4) + (ox)) |                          \
                                ((((long long)(2048 - (h) / 2) << 4) + (oy)) << 32));              \
    }
/* RGBAQ packed from a four-byte colour, and XYZ2 with and without the
   2048.0-pixel window origin folded in, as src/Shadow.c packs them */
#define GIF_RGBA(c) /* derived name */                                                             \
    ((long long)(c)[0] | ((long long)(c)[1] << 8) | ((long long)(c)[2] << 16) |                    \
     ((long long)(c)[3] << 24))
#define GIF_XY0(x, y, z) ((long long)(x) | ((long long)(y) << 16) | ((z) << 32)) /* derived name */
#define GIF_XY(x, y, z) /* derived name */                                                         \
    ((long long)((x) + 0x8000) | ((long long)((y) + 0x8000) << 16) | ((z) << 32))
/* The textured sprite, src/Shadow.c's spriteUV: PRIM, RGBAQ, then a UV and
   an XYZ2 pair for each corner, the far corner as x + fx with fx = w + 0x8000. */
#define FOG_SPRITE_UV(r, uv, col, prim, z) /* derived name */                                      \
    {                                                                                              \
        FOG_SET_GSREG(0x00, prim);                                                                 \
        FOG_SET_GSREG(0x01, GIF_RGBA(col));                                                        \
        FOG_SET_GSREG(0x03, (long long)(uv)[0] | ((long long)(uv)[1] << 16));                      \
        FOG_SET_GSREG(0x05, GIF_XY((r)[0], (r)[1], z));                                            \
        FOG_SET_GSREG(0x03,                                                                        \
                      (long long)((uv)[0] + (uv)[2]) | ((long long)((uv)[1] + (uv)[3]) << 16));    \
        {                                                                                          \
            int fx = (r)[2] + 0x8000;                                                              \
            int fy = (r)[3] + 0x8000;                                                              \
                                                                                                   \
            FOG_SET_GSREG(0x05, GIF_XY0((r)[0] + fx, (r)[1] + fy, z));                             \
        }                                                                                          \
    }
/* The untextured sprite, src/Shadow.c's spriteRect: PRIM, RGBAQ and the two
   XYZ2 corners, the far corner as spriteUV holds it. */
#define FOG_SPRITE_RECT(r, col, prim, z) /* derived name */                                        \
    {                                                                                              \
        FOG_SET_GSREG(0x00, prim);                                                                 \
        FOG_SET_GSREG(0x01, GIF_RGBA(col));                                                        \
        FOG_SET_GSREG(0x05, GIF_XY((r)[0], (r)[1], z));                                            \
        {                                                                                          \
            int fx = (r)[2] + 0x8000;                                                              \
            int fy = (r)[3] + 0x8000;                                                              \
                                                                                                   \
            FOG_SET_GSREG(0x05, GIF_XY0((r)[0] + fx, (r)[1] + fy, z));                             \
        }                                                                                          \
    }

/* The depth fog layer.  dbg is a local debug switch, off: it draws the layer
   opaque with TEXA set as src/GsBase.c's full-screen sprites set it, as in
   src/Shadow.c shadow_Draw. */
/* clang-format off */
void fog_DrawFog(void)
{
    int rc0[4] = {-ScreenWidth / 2 * 16, -ScreenHeight / 2 * 16, ScreenWidth * 16, ScreenHeight * 16};
    int vram;
    int i;
    int j;
    int dbg = 0; /* local debug switch, see the test after the loop below */
    int rc1[4] = {8, 8, ScreenWidth * 16, ScreenHeight * 16};
    unsigned char cl[4] = {0x80, 0x80, 0x80, GlobalStageSetting.fogStrength};

    if (debug_fullscreen_effect == 0) return;
    if (GlobalStageSetting.fogOn == 0) return;

    if (debug_font_flag & 1) debug_Printf(510, ScreenHeight / 2 - 8, 0xCCCCCC00, "Z");

    vram = tex_AllocVramAuto(1, 4);

    tex_ResetVramPri(4);

    FOG_START_PACKET();

    FOG_SET_GSREG(0x50, ((long long)vram << 32) |
                  0x0001000000000000LL);
    FOG_SET_GSREG(0x51, 0);
    FOG_SET_GSREG(0x52, 0x0000001000000010LL);
    FOG_SET_GSREG(0x53, 0);

    FOG_END_PACKET();

    dl_OpenDma(2, (void *)ICO_PHYS(ICO_ADDR(&fogClutPacket)), 67);
    dl_CloseDma();

    FOG_START_PACKET();

    FOG_SET_GSREG(63, 1);

    FOG_SET_GSREG(0x50, 0x1800 | ((long long)(ScreenWidth / 64) << 16) | ((long long)0x30 << 24) |
                  ((long long)0x2800 << 32) |
                  ((long long)(ScreenWidth / 64) << 48));
    FOG_SET_GSREG(0x51, 0);
    FOG_SET_GSREG(0x52, ScreenWidth | ((long long)ScreenHeight << 32));
    FOG_SET_GSREG(0x53, 2);

    for (i = 0; i < ScreenHeight / 32; i++) {
        for (j = 0; j < 4; j++) {
            FOG_SET_GSREG(0x50, ((i * (ScreenWidth / 2) + 0x2800) | 0x14020000) |
                          ((long long)(i * (ScreenWidth / 2) + 0x2800) << 32) |
                          0x1402000000000000LL);
            FOG_SET_GSREG(0x51, (long long)(16 + j * 32) | ((long long)(16 + j * 32 + 8) << 32));
            FOG_SET_GSREG(0x52, ((long long)(ScreenWidth / 2 * 4) << 32) | 8);
            FOG_SET_GSREG(0x53, 2);
        }
    }
    if (dbg) { cl[3] = 0x80; FOG_SET_GSREG(0x3B, 0x8000000080LL); }
    FOG_SET_FRAME(64, ScreenWidth, ScreenHeight, 0, 0);
    FOG_SET_GSREG(73, 0); FOG_SET_GSREG(66, ((long long)128 << 32) | 0x44);
    FOG_SET_GSREG(6, 0x2800 | ((long long)(ScreenWidth / 64) << 14) | ((long long)0x1B << 20) | ((long long)9 << 26) | ((long long)9 << 30) | ((long long)1 << 34) |
                  ((long long)vram << 37) | ((long long)1 << 61));
    FOG_SET_GSREG(78, ((long long)0x13000 << 16) | 0xC0);
    FOG_SET_GSREG(71, 0x50000);
    FOG_SET_GSREG(20, 0);
    FOG_SPRITE_UV(rc0, rc1, cl, 342, 0xFFFFFFLL);
    FOG_SET_GSREG(20, 96);

    if (GlobalStageSetting.fogOffsetA > 0) {
        unsigned char cl2[4] = {GlobalStageSetting.fogColR, GlobalStageSetting.fogColG, GlobalStageSetting.fogColB, GlobalStageSetting.fogOffsetA};

        FOG_SET_GSREG(71, 0x30000);
        FOG_SPRITE_RECT(rc0, cl2, 1094, 0xFFFFFFFFLL);
        FOG_SET_GSREG(71, 0x50000);
    }
    FOG_SET_GSREG(78, ((long long)0x3000 << 16) | 0xC0);

    FOG_SET_FRAME(64, ScreenWidth, ScreenHeight, screenOffsetX, screenOffsetY);
    FOG_END_PACKET();
}

/* clang-format on */

/* one row of the fog debug menu: a label, the int it edits and its range */
typedef struct FogToolItem { /* field names derived */
    char *name;              /* 0x0 */
    int *val;                /* 0x4 */
    int min;                 /* 0x8 */
    int max;                 /* 0xC */
} FogToolItem;               /* derived name */

/* the two labels the 0/1 row prints */
static char *fogOnOffText[] = {"Off", "On"}; /* derived name */

/* the fog tool's nine rows and their names.  Each row names the stage
   setting word it edits; the first row is the only 0/1 one, which is what the
   tool tests to decide between the text and the number format. */

static const FogToolItem fogToolItems[9] = {
    {" Fog On/Off   ", &GlobalStageSetting.fogOn, 0, 1},
    {" Fog Color R  ", &GlobalStageSetting.fogColR, 0, 255},
    {" Fog Color G  ", &GlobalStageSetting.fogColG, 0, 255},
    {" Fog Color B  ", &GlobalStageSetting.fogColB, 0, 255},
    {" Fog Color A  ", &GlobalStageSetting.fogColA, 0, 255},
    {" Fog Offset A ", &GlobalStageSetting.fogOffsetA, 0, 255},
    {" Fog Near     ", &GlobalStageSetting.fogNear, 0, 255},
    {" Fog Far      ", &GlobalStageSetting.fogFar, 0, 255},
    {" Fog Strength ", &GlobalStageSetting.fogStrength, 0, 255},
}; /* derived name */

/* the colour a row is drawn in: white when the cursor is elsewhere, black when
   it is on this row. */
static const unsigned int fogRowColor[] = {0xFFFFFF00, 0xFF000000}; /* derived name */

static int toolRow = 0; /* derived name */ /* highlighted row */

int fog_FogTool(void)
{
    int i;
    int v;
    int ret;

    ret = 0;
    debug_PrintfDummy(10, 50, 0xFF800000, "Fog Tool");

    for (i = 0; i < 9; i++) {
        if (fogToolItems[i].min == 0 && fogToolItems[i].max == 1) {
            debug_PrintfDummy(18, (i + 1) * 8 + 50, fogRowColor[(toolRow == i) ? 1 : 0], "%s : %s",
                              fogToolItems[i].name, fogOnOffText[*fogToolItems[i].val]);
        } else {
            debug_PrintfDummy(18, (i + 1) * 8 + 50, fogRowColor[(toolRow == i) ? 1 : 0], "%s : %d",
                              fogToolItems[i].name, *fogToolItems[i].val);
        }
    }

    if (pad[0].rep & 0x4000) {
        toolRow++;
        if (toolRow >= 9) {
            toolRow = 0;
        }
    }
    if (pad[0].rep & 0x1000) {
        toolRow--;
        if (toolRow < 0) {
            toolRow = 8;
        }
    }
    if (pad[0].rep & 0x2000) {
        v = ++*fogToolItems[toolRow].val;
        if (fogToolItems[toolRow].max < v) {
            *fogToolItems[toolRow].val = fogToolItems[toolRow].min;
        }
    }
    if (pad[0].rep & 0x8000) {
        v = --*fogToolItems[toolRow].val;
        if (v < fogToolItems[toolRow].min) {
            *fogToolItems[toolRow].val = fogToolItems[toolRow].max;
        }
    }
    if (pad[0].flags & 0x20) {
        for (i = 0; i < 9; i++) {
            if (fogToolItems[i].min == 0 && fogToolItems[i].max == 1) {
                debug_StdPrintfDummy("Fog %s => %s\n", fogToolItems[i].name,
                                     fogOnOffText[*fogToolItems[i].val]);
            } else {
                debug_StdPrintfDummy("Fog %s => %d\n", fogToolItems[i].name, *fogToolItems[i].val);
            }
        }
        ret = 1;
    }
    if (pad[0].flags & 0x40) {
        ret = -1;
    }
    if (ret != 0) {
        toolRow = 0;
    }
    fog_MakeFogClut();
    return ret;
}
