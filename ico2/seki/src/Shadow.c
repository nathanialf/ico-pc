#include "typedef.h"
#include "GsBase.h"
#include "debug.h"
#include "Shadow.h"
#include "DisplayP2O.h"
#include "Texture.h"
#include "geometryManager.h"
#include "main.h"
#include "Matrix.h"
#include "DmaPacket.h"
#include "DisplayList.h"
#include "Basic.h"
#include "gobj.h"

#ifdef ICO_RD

#include <string.h>
#include "rd.h"

/* ===================================================================== *
 * PC port (renderer wave 4, R4b; docs/port/RENDER_API.md "Shadows").
 *
 * The packets below are still built (the DMA bookkeeping and the heap use
 * stay as they are), but nothing on the host reads them: shadow_Reset's is
 * an UNPACK to VU1 memory and MSCAL 0 (SET_GSREGISTER), the volumes' and
 * shadow_Draw's are DIRECT (PATH2) GIF packets.  Each function records what
 * its packet does on rd instead, where it chains the packet:
 *   shadow_Reset   its register writes as rd state; the clear of FBP 0x142
 *                  is rd_ShadowReset on the frame's count target
 *                  (rd_ShadowCountTarget) with SCENE's depth-stencil; the
 *                  band it clears at FBP 0x140 first (16 lines of the pages
 *                  in front of 0x142, which no shadow pass reads) is not
 *                  drawn, and its register writes are the ones the 0x142
 *                  clear repeats
 *   shadow_RenderVolume, shadow_RenderVolumeMulti
 *                  every strip emitVolumeStrip writes, as the eight flat
 *                  triangles its ten positions kick, each counted +1
 *                  (RGBAQ 0x04) or -1 (0xFC) by its last position, with the
 *                  strips' PRIM 0x144; one rd_ShadowTris per object, as the
 *                  PS2 chains one packet per object
 *   shadow_Draw    rd_ShadowResolve (the count into the count target), then
 *                  the packet's register writes and sprites in order: the
 *                  256/128/64 chain into SHADOW0..2 and the three
 *                  composites into SCENE, so the state they leave leaks
 *                  into the rest of list 3 and the next lists as on the GS
 * The level-0 texture is the count target's RGBA view where the GS reads
 * PSMCT24 under TEXA 0x80 AEM: the resolve writes that expansion as alpha
 * (rd.h, rd_ShadowResolve).  The GS register decoder (GifPacket.c) never
 * sees these writes: like rd_Post's passes, they leave its per-list FRAME,
 * PRIM and TEX0 shadow behind (section 12). */

/* the triangles of one object's strips, recorded at the end of the object
   or when full */
#define SHADOW_HOST_TRIS 2048 /* port name */

static struct {
    RdScreenVtx v[SHADOW_HOST_TRIS * 3];
    signed char sign[SHADOW_HOST_TRIS];
    unsigned int n;
    RdScreenVtx last[2]; /* the strip's two positions before the current one */
    void *obj;
} shadowHost; /* port name */

static void shadowHostFlush(void)
{
    if (shadowHost.n == 0) {
        return;
    }
    rd_ABE(1); /* PRIM 0x144: strip, flat, ABE, no texture */
    rd_Gouraud(0);
    rd_TextureOff();
    rd_ShadowTris(shadowHost.v, (const int8_t *)shadowHost.sign, shadowHost.n,
                  RD_KEY(shadowHost.obj, 0, 0));
    shadowHost.n = 0;
}

static void shadowHostBegin(void *obj)
{
    shadowHost.n = 0;
    shadowHost.obj = obj;
}

/* position i (0..9) of a strip at XYZ2 v, its RGBAQ 0x04 (plus) or 0xFC */
static void shadowHostStripPos(int i, const int *v, int plus)
{
    RdScreenVtx c;

    memset(&c, 0, sizeof(c));
    c.x = v[0];
    c.y = v[1];
    c.z = (unsigned int)v[2];
    c.q = 1.0f;
    if (i >= 2) {
        unsigned int t;

        if (shadowHost.n == SHADOW_HOST_TRIS) {
            shadowHostFlush();
        }
        t = shadowHost.n++;
        shadowHost.v[t * 3 + 0] = shadowHost.last[0];
        shadowHost.v[t * 3 + 1] = shadowHost.last[1];
        shadowHost.v[t * 3 + 2] = c;
        shadowHost.sign[t] = plus ? 1 : -1;
    }
    shadowHost.last[0] = shadowHost.last[1];
    shadowHost.last[1] = c;
}

static void shadowHostReset(void)
{
    RdTarget cnt = rd_ShadowCountTarget((uint32_t)ScreenWidth, (uint32_t)ScreenHeight);

    /* setFrame(0x142), ZBUF 0xC0 with ZMSK, TEST 0x30000, the clear sprite
       (PRIM 0x406) */
    rd_SetTarget(cnt, rd_Target(RD_TARGET_SCENE), (uint32_t)ScreenWidth, (uint32_t)ScreenHeight, 0);
    rd_ZWrite(0);
    rd_TestGs(0x30000);
    rd_ABE(0);
    rd_Gouraud(0);
    rd_TextureOff();
    rd_ShadowReset();
    /* FBA, TEXA, TEST, ALPHA, COLCLAMP */
    rd_FBA(0);
    rd_TexA(RD_TEXA_80_80);
    rd_TestGs(0x50000);
    rd_BlendFunc(RD_BLEND_CS_FIX_ADD_CD, 0x80);
    rd_ColClamp(0);
}

/* the target of blur level i: the count (FBP 0x142), then SHADOW0..2 */
static RdTarget shadowHostLevel(int i)
{
    if (i == 0) {
        return rd_ShadowCountTarget((uint32_t)ScreenWidth, (uint32_t)ScreenHeight);
    }
    return rd_Target((RdTargetId)(RD_TARGET_SHADOW0 + i - 1));
}

/* TEX0 of level i (TCC RGBA, MODULATE), as the packet writes it */
static void shadowHostTex0(int i)
{
    rd_Texture(rd_TargetTexture(shadowHostLevel(i), RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
}

/* spriteUV: PRIM, RGBAQ, UV and XYZ2 of each corner, Z 0xFFFFFFFF */
static void shadowHostSprite(const int *r, const int *uv, const unsigned char *col, int abe)
{
    RdScreenVtx v[2];
    int i;

    rd_ABE(abe); /* PRIM 0x116 or 0x156: sprite, flat, TME, FST */
    rd_Gouraud(0);
    memset(v, 0, sizeof(v));
    v[0].x = (r[0] + 0x8000) & 0xFFFF;
    v[0].y = (r[1] + 0x8000) & 0xFFFF;
    v[0].s = (float)uv[0];
    v[0].t = (float)uv[1];
    v[1].x = (r[0] + r[2] + 0x8000) & 0xFFFF;
    v[1].y = (r[1] + r[3] + 0x8000) & 0xFFFF;
    v[1].s = (float)(uv[0] + uv[2]);
    v[1].t = (float)(uv[1] + uv[3]);
    for (i = 0; i < 2; i++) {
        v[i].z = 0xFFFFFFFFu;
        v[i].q = 1.0f;
        memcpy(v[i].rgba, col, 4);
    }
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

/* shadow_Draw up to its first FRAME: the count resolved where the GS has
   it in FBP 0x142, then TEST, ZBUF, COLCLAMP, FBA, TEXA and TEX1 */
static void shadowHostDrawBegin(void)
{
    rd_SetTarget(shadowHostLevel(0), rd_Target(RD_TARGET_SCENE), (uint32_t)ScreenWidth,
                 (uint32_t)ScreenHeight, 0);
    rd_ShadowResolve();
    rd_TestGs(0x30000);
    rd_ZWrite(0);
    rd_ColClamp(1);
    rd_FBA(0);
    rd_TexA(RD_TEXA_80_80_AEM);
    rd_SamplerFilter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
}

/* one chain step: level i into level i + 1 */
static void shadowHostChain(int i, const int *r, const int *uv, const unsigned char *col)
{
    uint32_t w = 512u >> (i + 1);

    rd_SetTarget(shadowHostLevel(i + 1), (RdTarget){0}, w, w, 0);
    shadowHostTex0(i);
    shadowHostSprite(r, uv, col, 0);
}

/* setFrame(0x40), ALPHA 0x44, TEST 0x3400D; SCENE takes RD_TARGET_OFFSET
   as GifPacket.c's FRAME decoding gives it (the field offset, zero on the
   host: both setFrame calls of shadow_Draw are centred) */
static void shadowHostCompositeBegin(void)
{
    RdTarget scene = rd_Target(RD_TARGET_SCENE);

    rd_SetTarget(scene, scene, (uint32_t)ScreenWidth, (uint32_t)ScreenHeight, RD_TARGET_OFFSET);
    rd_BlendFunc(RD_BLEND_LERP_AS, 0);
    rd_TestGs(0x3400D);
}

/* one composite: level i into SCENE */
static void shadowHostComposite(int i, const int *r, const int *uv, const unsigned char *col)
{
    shadowHostTex0(i);
    rd_SamplerFilter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    shadowHostSprite(r, uv, col, 1);
}

/* ZBUF write on, TEST 0x50000, setFrame(0x40) with screenOffsetX/Y */
static void shadowHostDrawEnd(void)
{
    RdTarget scene = rd_Target(RD_TARGET_SCENE);

    rd_ZWrite(1);
    rd_TestGs(0x50000);
    rd_SetTarget(scene, scene, (uint32_t)ScreenWidth, (uint32_t)ScreenHeight, RD_TARGET_OFFSET);
}

#endif /* ICO_RD */

/* One skinning matrix per cluster, 64 of 64 bytes, built by
 * shadow_EntryClusterShadow. */
static char clusterMatrix[4096]; /* derived name */

/* the shadow switch shadow_Reset applies and the request shadow_KillShadow
   leaves for it */
static int killShadow = 0; /* derived name */

static int killShadowRequest = 0; /* derived name */

/* The shadow display opens, fills and closes a PATH1 packet of its own
 * through PacketBufferStruct (DmaPacket.h), whose packet addresses are one
 * pointer union. */
/* the screen width and height in pixels */

/* The GS A+D writer, a macro as in Texture.c. */
#define setGsReg(reg, val) /* derived name */                                                      \
    {                                                                                              \
        *PacketBufferStruct.ptr.d++ = (val);                                                       \
        *PacketBufferStruct.ptr.d++ = (reg);                                                       \
    }
/* The PATH1 packet open, a macro with the statements of GifPacket.c's
 * gif_StartPacketPath1; the close, gif_EndPacketPath1's statements, is
 * written out at its site below. */
#define gifStartPacketPath1(c) /* derived name */                                                  \
    {                                                                                              \
        (c) = PacketBufferStruct.ptr.c;                                                            \
        PacketBufferStruct.gif.c = 0;                                                              \
        PacketBufferStruct.end.c = 0;                                                              \
        PacketBufferStruct.dma.c = (c);                                                            \
        PacketBufferStruct.tail.c = (c);                                                           \
        PacketBufferStruct.ptr.c = ((c) + 8);                                                      \
        *(unsigned int *)((c) + 8) = 0x11000000;                                                   \
        PacketBufferStruct.gif.c = ((c) + 0xC);                                                    \
        PacketBufferStruct.end.c = ((c) + 0x10);                                                   \
        PacketBufferStruct.ptr.c = ((c) + 0x18);                                                   \
        ((GifPkWord *)((c) + 0x18))->d = 0xE;                                                      \
        PacketBufferStruct.ptr.c = ((c) + 0x20);                                                   \
    }
/* FRAME_1, SCISSOR_1 and XYOFFSET_1 for a w by h buffer at base fbp, the
 * window centred on the GS's 2048.0 origin and moved by ox, oy sixteenths. */
#define setFrame(fbp, w, h, ox, oy) /* derived name */                                             \
    {                                                                                              \
        setGsReg(0x4C, (fbp) | ((long long)(((w) >> 6) & 0x3F) << 16));                            \
        setGsReg(0x40, ((long long)((w) - 1) << 16) | ((long long)((h) - 1) << 48));               \
        setGsReg(0x18, (((long long)(2048 - (w) / 2) << 4) + (ox)) |                               \
                           ((((long long)(2048 - (h) / 2) << 4) + (oy)) << 32));                   \
    }
/* RGBAQ packed from a four-byte colour, as src/GifPacket.c packs it */
#define GIF_RGBA(c) /* derived name */                                                             \
    ((long long)(c)[0] | ((long long)(c)[1] << 8) | ((long long)(c)[2] << 16) |                    \
     ((long long)(c)[3] << 24))
/* XYZ2 with the 2048.0-pixel window origin folded in, and without it */
#define GIF_XY0(x, y, z) ((long long)(x) | ((long long)(y) << 16) | ((z) << 32)) /* derived name */
#define GIF_XY(x, y, z) /* derived name */                                                         \
    ((long long)((x) + 0x8000) | ((long long)((y) + 0x8000) << 16) | ((z) << 32))
/* The untextured sprite: PRIM, RGBAQ and the two XYZ2 corners of the rect r
 * (x, y, w, h in sixteenths).  The far corner is x + fx with fx = w + 0x8000,
 * as GsBase.c's spriteRect holds it. */
#define spriteRect(r, col, prim) /* derived name */                                                \
    {                                                                                              \
        setGsReg(0x00, prim);                                                                      \
        setGsReg(0x01, GIF_RGBA(col));                                                             \
        setGsReg(0x05, GIF_XY((r)[0], (r)[1], 0xFFFFFFFFLL));                                      \
        {                                                                                          \
            int fx = (r)[2] + 0x8000;                                                              \
            int fy = (r)[3] + 0x8000;                                                              \
                                                                                                   \
            setGsReg(0x05, GIF_XY0((r)[0] + fx, (r)[1] + fy, 0xFFFFFFFFLL));                       \
        }                                                                                          \
    }

void shadow_Reset(void)
{
    char *c;
    char *p;
    char *q;

    if (killShadowRequest != killShadow) {
        killShadow = killShadowRequest;
    }
    tex_LockHeadTBP(0x3D80, 3);
    dl_SetDLPriority(3);
    gifStartPacketPath1(c);
    {
        int full[4] = {-ScreenWidth / 2 * 16, -ScreenHeight / 2 * 16, ScreenWidth * 16,
                       ScreenHeight * 16};
        int band[4] = {-ScreenWidth / 2 * 16, -ScreenHeight / 2 * 16, ScreenWidth * 16, 256};
        unsigned char col[4] = {0};

        setFrame(0x140, ScreenWidth, ScreenHeight, 0, 0);
        setGsReg(0x4E, 0xC0 | ((long long)0x30 << 24) | ((long long)1 << 32));
        setGsReg(0x47, 0x30000);
        spriteRect(band, col, 0x406);

        setFrame(0x142, ScreenWidth, ScreenHeight, 0, 0);
        setGsReg(0x4E, 0xC0 | ((long long)0x30 << 24) | ((long long)1 << 32));
        setGsReg(0x47, 0x30000);
        spriteRect(full, col, 0x406);

        setGsReg(0x4A, 0);
        setGsReg(0x3B, 0x80 | ((long long)0x80 << 32));
        setGsReg(0x47, 0x50000);
        setGsReg(0x42, 0x68 | ((long long)0x80 << 32));
        setGsReg(0x46, 0);
    }
    ((GifPkWord *)PacketBufferStruct.end.c)->d =
        (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.end.c) >> 4) -
                       1) |
        0x1000000000008000LL;
    ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
        (((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) << 16) |
        0x6C008000;
    p = PacketBufferStruct.ptr.c;
    ((GifPkWord *)p)->w[0] = 0x15000000;
    p += 4;
    PacketBufferStruct.ptr.c = p;
    ((GifPkWord *)p)->w[0] = 0;
    PacketBufferStruct.ptr.c = (p + 4);
    ((GifPkWord *)(p + 4))->w[0] = 0;
    PacketBufferStruct.ptr.c = (p + 8);
    ((GifPkWord *)(p + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = (p + 0xC);
    ((GifPkWord *)PacketBufferStruct.tail.c)->d =
        (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.tail.c) >>
                         4) -
                        1) |
                       0x10000000);
    q = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = q;
    ((GifPkWord *)q)->d = 0x60000000;
    PacketBufferStruct.ptr.c = (q + 8);
    ((GifPkWord *)(q + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = (q + 0xC);
    ((GifPkWord *)(q + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = (q + 0x10);
#ifdef ICO_RD
    shadowHostReset();
#endif
    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
}

/* The textured sprite: PRIM, RGBAQ, then a UV and an XYZ2 pair for each
 * corner, the far corner as spriteRect holds it. */
#define spriteUV(r, uv, col, prim) /* derived name */                                              \
    {                                                                                              \
        setGsReg(0x00, prim);                                                                      \
        setGsReg(0x01, GIF_RGBA(col));                                                             \
        setGsReg(0x03, (long long)(uv)[0] | ((long long)(uv)[1] << 16));                           \
        setGsReg(0x05, GIF_XY((r)[0], (r)[1], 0xFFFFFFFFLL));                                      \
        setGsReg(0x03, (long long)((uv)[0] + (uv)[2]) | ((long long)((uv)[1] + (uv)[3]) << 16));   \
        {                                                                                          \
            int fx = (r)[2] + 0x8000;                                                              \
            int fy = (r)[3] + 0x8000;                                                              \
                                                                                                   \
            setGsReg(0x05, GIF_XY0((r)[0] + fx, (r)[1] + fy, 0xFFFFFFFFLL));                       \
        }                                                                                          \
    }

/* the 12.4 window offsets XYOFFSET_1 is programmed with for the screen pass */
/* the debug flag word: bit 0 turns the on-screen labels on */
/* "S", the one character label this pass prints */

void shadow_Draw(void)
{
    if (debug_font_flag & 1) {
        debug_Printf(500, ScreenHeight / 2 - 8, 0xCCCCCC00u, "S");
    }
    {
        /* the four level tables, this block's statics */
        /* the frame buffer pointer of each shadow mipmap level */
        static const unsigned int levelFbp[4] = {0x142, 0x1C2, 0x1E2, 0x1EA}; /* derived name */
        /* the texture base pointer of each shadow mipmap level */
        static const unsigned int levelTbp[4] = {0x2840, 0x3840, 0x3C40, 0x3D40}; /* derived name */
        /* the sprite corner and size of each level in 12.4 screen units */
        static const int levelRect[4][4] = {{-4100, -4100, 8192, 8192},
                                            {-2052, -2052, 4096, 4096},
                                            {-1028, -1028, 2048, 2048},
                                            {-516, -516, 1024, 1024}}; /* derived name */
        /* the sprite texture rectangle of each level at the 512 pixel default */
        static const int levelUV[4][4] = {{4, 4, 8192, 8192},
                                          {4, 4, 4096, 4096},
                                          {4, 4, 2048, 2048},
                                          {4, 4, 1024, 1024}}; /* derived name */
        int rect[4][4] = {{4, 4, ScreenWidth * 16, ScreenHeight * 16},
                          {4, 4, ScreenWidth * 8, ScreenHeight * 8},
                          {4, 4, ScreenWidth * 4, ScreenHeight * 4},
                          {4, 4, ScreenWidth * 2, ScreenHeight * 2}};
        int off[4] = {-(ScreenWidth >> 1) * 16, -(ScreenHeight >> 1) * 16, ScreenWidth * 16,
                      ScreenHeight * 16};
        unsigned char col[4] = {128, 128, 128, GlobalStageSetting.shadowDepth};
        int dbg = 0; /* local debug switch, see the test in the upward loop */
        int i;
        char *c;
        char *q;

        dl_SetDLPriority(3);
        gifStartPacketPath1(c);
        setGsReg(0x47, 0x30000);
        setGsReg(0x4E, 0xC0 | ((long long)0x30 << 24) | ((long long)1 << 32));
        setGsReg(0x46, 1);
        setGsReg(0x4A, 0);
        setGsReg(0x3B, 0x8080 | ((long long)0x80 << 32));
        setGsReg(0x14, 0x60);
#ifdef ICO_RD
        shadowHostDrawBegin();
#endif

        for (i = 0; i < 3; i++) {
            setFrame(levelFbp[i + 1], 512 >> (i + 1), 512 >> (i + 1), 0, 0);
            if (i == 0) {
                *PacketBufferStruct.ptr.d++ = levelTbp[i] | ((long long)(512 >> i) / 64 << 14) |
                                              ((long long)1 << 20) | ((long long)(9 - i) << 26) |
                                              ((long long)(9 - i) << 30) |
                                              ((long long)0x8000 << 19);
                *PacketBufferStruct.ptr.d++ = 0x06;
            } else {
                *PacketBufferStruct.ptr.d++ =
                    levelTbp[i] | ((long long)(512 >> i) / 64 << 14) | ((long long)(9 - i) << 26) |
                    ((long long)(9 - i) << 30) | ((long long)0x8000 << 19);
                *PacketBufferStruct.ptr.d++ = 0x06;
            }
            spriteUV(levelRect[i + 1], levelUV[i], col, 0x116);
#ifdef ICO_RD
            shadowHostChain(i, levelRect[i + 1], levelUV[i], col);
#endif
        }

        setFrame(0x40, ScreenWidth, ScreenHeight, 0, 0);
        setGsReg(0x42, 0x44);
        setGsReg(0x47, 0x3400D);
#ifdef ICO_RD
        shadowHostCompositeBegin();
#endif

        for (i = 3; i > 0; i--) {
            unsigned char col2[4] = {GlobalStageSetting.shadowColR, GlobalStageSetting.shadowColG,
                                     GlobalStageSetting.shadowColB,
                                     GlobalStageSetting.shadowBlend[i]};

            /* a local debug switch, off: draw every level in mid grey */
            if (dbg) {
                col2[0] = col2[1] = col2[2] = col2[3] = 0x80;
            }
            *PacketBufferStruct.ptr.d++ = levelTbp[i] | ((long long)(512 >> i) / 64 << 14) |
                                          ((long long)(9 - i) << 26) | ((long long)(9 - i) << 30) |
                                          ((long long)0x8000 << 19);
            *PacketBufferStruct.ptr.d++ = 0x06;
            setGsReg(0x14, 0x60);
            spriteUV(off, rect[i], col2, 0x156);
#ifdef ICO_RD
            shadowHostComposite(i, off, rect[i], col2);
#endif
        }

        setGsReg(0x4E, 0x300000C0);
        setGsReg(0x47, 0x50000);
        setFrame(0x40, ScreenWidth, ScreenHeight, screenOffsetX, screenOffsetY);
#ifdef ICO_RD
        shadowHostDrawEnd();
#endif

        ((GifPkWord *)PacketBufferStruct.end.c)->d =
            (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.end.c) >>
                            4) -
                           1) |
            0x1000000000008000LL;
        ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
            ((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) | 0x50000000;
        ((GifPkWord *)PacketBufferStruct.tail.c)->d =
            (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.tail.c) >>
                             4) -
                            1) |
                           0x10000000);
        q = PacketBufferStruct.ptr.c;
        PacketBufferStruct.tail.c = q;
        ((GifPkWord *)q)->d = 0x60000000;
        PacketBufferStruct.ptr.c = (q + 8);
        ((GifPkWord *)(q + 8))->w[0] = 0;
        PacketBufferStruct.ptr.c = (q + 0xC);
        ((GifPkWord *)(q + 8))->w[1] = 0;
        PacketBufferStruct.ptr.c = (q + 0x10);
        dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
        dl_CloseDma();
    }
    tex_UnlockHeadTBP(3);
}

#ifdef DEBUG

static void shadow_getShadowVectorAverage(void *dir, Sub15C *o);

#endif

/* The object's shadow render retired to shadow_RenderVolume; this entry only
   reports that it was reached (p2o_DispShadowVolume still passes the object),
   and the DEBUG build adds the object's shadow direction through dir. */
void shadow_Render(Sub15C *o)
{
    float dir[4];

    debug_StdPrintfDummy("shadow_Render called\n");
#ifdef DEBUG
    shadow_getShadowVectorAverage(dir, o);
    printf("  shadow dir %f %f %f\n", dir[0], dir[1], dir[2]);
#endif
}

static void shadow_getShadowVectorAverage(void *dir, Sub15C *o)
{
    _CopyVector(dir, o->shadowDir);
    _SetCurrentMatrix(matrixptr + 0x80);
    _ClearTransCurrentMatrix();
    _ApplyCurrentMatrix(dir, dir);
    _NormalizeVector(dir, dir);
}

/* the same quadword copy type src/Primitive.c uses: the accumulator reset is
 * one lq/sq pair per vertex */
typedef ICO_QW Qw128; /* derived name */

#ifdef ICO_HOST

#include "ee_view.h"

/* PC port: the accumulator reset copies a zero VECTOR into each Qw128 slot;
   the sizes must agree (tools/template_audit.py) */
ICO_LAYOUT_SIZE(VECTOR, Qw128);

#endif

/* one weighted vertex of a cluster run: the vertex it moves and the weight it
 * moves it by */
typedef struct ClusterWeight { /* field names derived */
    int idx;
    float w;
    int _8;
    int _C;
} ClusterWeight; /* derived name */

/* one cluster of a shadow volume: the -1 terminated run of weighted vertices
 * and the matrix slot it is skinned through */
typedef struct ClusterPoly {         /* field names derived */
    ICO_EEWORD(ClusterWeight *) run; /* an EE word on the host (eeword.h) */
    int matrix;
    int _8;
    int _C;
} ClusterPoly; /* derived name */

/* Add src, transformed by the cluster matrix in vf4-vf7 and scaled by the
 * weight, into dst: one asm block, the weight passed through $8 by hand and
 * the vnop runs placed around the multiply and the accumulate. */
static inline void applyWeightedVtx(void *dst, void *src, float w) /* derived name */
{
#ifdef ICO_HOST
    /* dst.xyz += (current matrix applied to (src.xyz, 1)).xyz * w; dst.w kept */
    float v[4];
    float *d = dst;

    ico_apply_matrix_w1(v, (const float (*)[4])ico_current_matrix, (const float *)src);
    d[0] = d[0] + v[0] * w;
    d[1] = d[1] + v[1] * w;
    d[2] = d[2] + v[2] * w;
#else
    __asm__ __volatile__("lqc2 $vf8, 0(%1)\n\t"
                         "lqc2 $vf9, 0(%0)\n\t"
                         "mfc1 $8, %2\n\t"
                         "qmtc2.ni $8, $vf11\n\t"
                         "vmulax.xyzw ACC, $vf4, $vf8x\n\t"
                         "vmadday.xyzw ACC, $vf5, $vf8y\n\t"
                         "vmaddaz.xyzw ACC, $vf6, $vf8z\n\t"
                         "vmaddw.xyzw $vf10, $vf7, $vf0w\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vmulx.xyz $vf10, $vf10, $vf11x\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vadd.xyz $vf9, $vf9, $vf10\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "sqc2 $vf9, 0(%0)"
                         :
                         : "r"(dst), "r"(src), "f"(w)
                         : "$8");
#endif
}

static void shadow_EntryClusterShadow(Sub15C *o, float len)
{
    VECTOR zero = {0.0f, 0.0f, 0.0f, 1.0f};
    float v[4];
    float sa[4];
    float sb[4];
    PObjModel *x = o->shadow;
    PObjPart *p;
    int i;
    unsigned int k;

    _InitCurrentMatrix();
    shadow_getShadowVectorAverage(v, o);
    _ScaleVectorXYZ(sa, v, len);
    _ScaleVectorXYZ(sb, v, 4.0f);

    for (i = 0; i < o->nodeNum; i++) {
        _SetCurrentMatrix((char *)o->nodeMtx + i * 0x40);
        _MulCurrentMatrixR(o->clusterMtx + i * 0x40);
        _MulCurrentMatrixL(matrixptr + 0x80);
        _GetCurrentMatrix(clusterMatrix + i * 0x40);
    }

#ifdef ICO_HOST
#else
    __asm__ __volatile__("lq $8, 0(%0)" : : "r"(&zero) : "$8");
#endif
    for (i = 0, p = x->parts; i < x->partCount; i++, p++) {
        for (k = 0; k < p->vtxCount; k++) {
#ifdef ICO_HOST
            __builtin_memcpy((Qw128 *)p->vtxSave + k, &zero, 16);
#else
            __asm__ __volatile__("sq $8, 0(%0)" : : "r"((Qw128 *)p->vtxSave + k) : "$8");
#endif
        }
    }

    for (i = 0, p = x->parts; i < x->partCount; i++, p++) {
        for (k = 0; k < p->polyCount; k++) {
            ClusterWeight *e;
            VECTOR *dst;
            VECTOR *src;

            _SetCurrentMatrix(clusterMatrix + ((ClusterPoly *)p->polys)[k].matrix * 0x40);
            e = ICO_EEPTR(ClusterWeight *, ((ClusterPoly *)p->polys)[k].run);
            /* both bases are read once here, ahead of the loop */
            dst = (VECTOR *)p->vtxSave;
            src = (VECTOR *)p->vtx;
            do {
                applyWeightedVtx(dst + e->idx, src + e->idx, e->w);
            } while ((++e)->idx != -1);
        }
        for (k = 0; k < p->vtxCount; k++) {
            _AddVectorXYZ(p->nrmSave + k * 16, p->vtxSave + k * 16, sa);
            _AddVectorXYZ(p->vtxSave + k * 16, p->vtxSave + k * 16, sb);
        }
    }

    for (i = 0, p = x->parts; i < x->partCount; i++, p++) {
        VECTOR *va = (VECTOR *)p->vtxSave;
        VECTOR *vb = (VECTOR *)p->nrmSave;

        for (k = 0; k < p->vtxCount; k++) {
            if (1.0f <= va[k].z && 1.0f <= vb[k].z) {
            } else if (va[k].z < 1.0f && vb[k].z < 1.0f) {
                vb[k].w = -1.0f;
                va[k].w = -1.0f;
            } else if (vb[k].z < 1.0f) {
                _InterVectorXYZ(&vb[k], &va[k], &vb[k],
                                1.0f - (va[k].z - 1.0f) / (va[k].z - vb[k].z));
            } else if (va[k].z < 1.0f) {
                _InterVectorXYZ(&va[k], &vb[k], &va[k],
                                1.0f - (vb[k].z - 1.0f) / (vb[k].z - va[k].z));
            }
        }
    }
}

/* Transform a vertex by the current matrix through VU0: one asm block, the
 * two vnop runs placed by hand around the multiply, both addresses "r"
 * operands. */
static inline void applyCurrentMatrixV(void *dst, void *src) /* derived name */
{
#ifdef ICO_HOST
    ico_apply_matrix_w1((float *)dst, (const float (*)[4])ico_current_matrix, (const float *)src);
#else
    __asm__ __volatile__("lqc2 $vf8, 0(%1)\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vmulax.xyzw ACC, $vf4, $vf8x\n\t"
                         "vmadday.xyzw ACC, $vf5, $vf8y\n\t"
                         "vmaddaz.xyzw ACC, $vf6, $vf8z\n\t"
                         "vmaddw.xyzw $vf9, $vf7, $vf0w\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "sqc2 $vf9, 0(%0)"
                         :
                         : "r"(dst), "r"(src));
#endif
}

static void shadow_EntryNormalShadow(Sub15C *o, int idx, float len)
{
    float v[4];
    float sa[4];
    float sb[4];
    PObjModel *x = o->shadow;
    int i;
    int j;
    PObjPart *p;

    shadow_getShadowVectorAverage(v, o);
    _ScaleVectorXYZ(sa, v, len);
    _ScaleVectorXYZ(sb, v, 4.0f);
    _SetCurrentMatrix((char *)o->nodeMtx + idx * 0x40);
    _MulCurrentMatrixL(matrixptr + 0x80);

    p = x->parts;
    for (i = 0; i < x->partCount; i++, p++) {
        for (j = 0; j < p->vtxCount; j++) {
            applyCurrentMatrixV(p->vtxSave + j * 16, p->vtx + j * 16);
        }
        for (j = 0; j < p->vtxCount; j++) {
            _AddVectorXYZ(p->nrmSave + j * 16, p->vtxSave + j * 16, sa);
            _AddVectorXYZ(p->vtxSave + j * 16, p->vtxSave + j * 16, sb);
        }
    }

    p = x->parts;
    for (i = 0; i < x->partCount; i++, p++) {
        VECTOR *va = (VECTOR *)p->vtxSave;
        VECTOR *vb = (VECTOR *)p->nrmSave;

        for (j = 0; j < p->vtxCount; j++) {
            if (1.0f <= va[j].z && 1.0f <= vb[j].z) {
            } else if (va[j].z < 1.0f && vb[j].z < 1.0f) {
                vb[j].w = -1.0f;
                va[j].w = -1.0f;
            } else if (vb[j].z < 1.0f) {
                _InterVectorXYZ(&vb[j], &va[j], &vb[j],
                                1.0f - (va[j].z - 1.0f) / (va[j].z - vb[j].z));
            } else if (va[j].z < 1.0f) {
                _InterVectorXYZ(&va[j], &vb[j], &va[j],
                                1.0f - (vb[j].z - 1.0f) / (vb[j].z - va[j].z));
            }
        }
    }
}

/* The static helpers shadow_RenderVolume and shadow_RenderVolumeMulti share;
 * both inline every one of them. */

/* the screen-space origin every projected point is measured from: the centre
 * of the GS's 4096-unit primitive coordinate space */
static VECTOR screenOrigin = {2048.0f, 2048.0f, 0.0f, 0.0f}; /* derived name */

/* the face normal z of each strip position, read back by the position that
 * shares the face with the one that computed it */
static float stripFaceZ[10]; /* derived name */

#ifdef ICO_HOST

/* The VU0 registers the shadow volume helpers below share on the PS2,
 * kept as data: vf1 (the shadow direction), vf10-vf15 (the strip's last
 * three top and bottom vertices) and vf20-vf25 (their projections). The
 * PS2 values left over from before the first strip are unspecified; the
 * host starts from zero. */
static struct {
    float dir[4];
    float src[6][4];
    float proj[6][4];
} shadowVolumeRegs; /* derived name */

#endif

/* the projection matrix and the shadow direction into the VU0
 * register file, where the edge projector below leaves them for the whole
 * mesh walk */
static inline void loadVolumeMatrix(void *dir) /* derived name */
{
#ifdef ICO_HOST
    /* the projection matrix becomes the current matrix, as on the PS2 */
    _SetCurrentMatrix(matrixptr + 0xC0);
    __builtin_memcpy(shadowVolumeRegs.dir, dir, 16);
#else
    char *m = matrixptr + 0xC0;

    __asm__ __volatile__("lqc2 $vf4, 0x0(%0)\n\t"
                         "lqc2 $vf5, 0x10(%0)\n\t"
                         "lqc2 $vf6, 0x20(%0)\n\t"
                         "lqc2 $vf7, 0x30(%0)\n\t"
                         "lqc2 $vf1, 0x0(%1)"
                         :
                         : "r"(m), "r"(dir));
#endif
}

/* the six strip vertices out of the VU register file as integer
 * screen coordinates */
static inline void storeVolumeVerts(void *dst) /* derived name */
{
#ifdef ICO_HOST
    int v[6][4];
    int n;
    int k;

    for (n = 0; n < 6; n++) {
        for (k = 0; k < 4; k++) {
            v[n][k] = ps2_ftoi4(shadowVolumeRegs.proj[n][k]);
        }
    }
    __builtin_memcpy(dst, v, sizeof v);
#else
    __asm__ __volatile__("vftoi4.xyzw $vf26, $vf20\n\t"
                         "vftoi4.xyzw $vf27, $vf21\n\t"
                         "vftoi4.xyzw $vf28, $vf22\n\t"
                         "vftoi4.xyzw $vf29, $vf23\n\t"
                         "vftoi4.xyzw $vf30, $vf24\n\t"
                         "vftoi4.xyzw $vf31, $vf25\n\t"
                         "sqc2 $vf26, 0x0(%0)\n\t"
                         "sqc2 $vf27, 0x10(%0)\n\t"
                         "sqc2 $vf28, 0x20(%0)\n\t"
                         "sqc2 $vf29, 0x30(%0)\n\t"
                         "sqc2 $vf30, 0x40(%0)\n\t"
                         "sqc2 $vf31, 0x50(%0)"
                         :
                         : "r"(dst));
#endif
}

/* Project one silhouette edge and clip the projected segment
 * to the screen rectangle. Returns the facing dot times the winding sign, or
 * -1.0f when the edge is wholly off screen, which is also how the caller
 * learns that both ends were marked away.
 * The projection is one hand-ordered block: it rolls the six vertex
 * registers of the strip down by one, projects the edge's two ends, divides
 * both by w, takes the facing dot of the new triangle and leaves the two
 * screen points both in the VU registers the emitter reads and in oa/ob for
 * the clipper.  The GPR the dot product passes through is a named $7 (and the
 * slides' is $8), declared clobbered, as Matrix.c and BgAnimation.c write
 * theirs. */
static inline float clipVolumeEdge(VECTOR *pa, VECTOR *pb, float sgn) /* derived name */
{
    VECTOR oa;
    VECTOR ob;
    float rate[2];
    float dot;
    float fw, fh, t0, t1, t;

#ifdef ICO_HOST
    {
        /* roll the strip down one place, load the new edge, project both
           ends (w taken as 1, then all four fields times 1/w), take the
           facing of the new top triangle against the shadow direction, and
           make the ends relative to the screen origin */
        float (*v)[4] = shadowVolumeRegs.src;
        float (*pr)[4] = shadowVolumeRegs.proj;
        float e8[3];
        float e9[3];
        float n[3];
        float q;
        int k;

        __builtin_memcpy(v[0], v[1], 16);
        __builtin_memcpy(v[3], v[4], 16);
        __builtin_memcpy(pr[0], pr[1], 16);
        __builtin_memcpy(pr[3], pr[4], 16);
        __builtin_memcpy(v[1], v[2], 16);
        __builtin_memcpy(v[4], v[5], 16);
        __builtin_memcpy(pr[1], pr[2], 16);
        __builtin_memcpy(pr[4], pr[5], 16);
        __builtin_memcpy(v[2], pa, 16);
        __builtin_memcpy(v[5], pb, 16);
        for (k = 0; k < 3; k++) {
            e8[k] = v[0][k] - v[1][k];
            e9[k] = v[2][k] - v[1][k];
        }
        ico_apply_matrix_w1(pr[2], (const float (*)[4])ico_current_matrix, v[2]);
        q = ps2_div(1.0f, pr[2][3]);
        for (k = 0; k < 4; k++) {
            pr[2][k] = pr[2][k] * q;
        }
        n[0] = e8[1] * e9[2] - e9[1] * e8[2];
        n[1] = e8[2] * e9[0] - e9[2] * e8[0];
        n[2] = e8[0] * e9[1] - e9[0] * e8[1];
        ico_apply_matrix_w1(pr[5], (const float (*)[4])ico_current_matrix, v[5]);
        q = ps2_div(1.0f, pr[5][3]);
        for (k = 0; k < 4; k++) {
            pr[5][k] = pr[5][k] * q;
        }
        v[2][3] = 1.0f;
        v[5][3] = 1.0f;
        dot = shadowVolumeRegs.dir[0] * n[0] + shadowVolumeRegs.dir[1] * n[1] +
              shadowVolumeRegs.dir[2] * n[2];
        for (k = 0; k < 4; k++) {
            ((float *)&oa)[k] = pr[2][k] - ((float *)&screenOrigin)[k];
            ((float *)&ob)[k] = pr[5][k] - ((float *)&screenOrigin)[k];
        }
    }
#else
    __asm__ __volatile__("vmove.xyzw $vf10, $vf11\n\t"
                         "vmove.xyzw $vf13, $vf14\n\t"
                         "vmove.xyzw $vf20, $vf21\n\t"
                         "vmove.xyzw $vf23, $vf24\n\t"
                         "vmove.xyzw $vf11, $vf12\n\t"
                         "vmove.xyzw $vf14, $vf15\n\t"
                         "vmove.xyzw $vf21, $vf22\n\t"
                         "vmove.xyzw $vf24, $vf25\n\t"
                         "lqc2 $vf12, 0x0(%1)\n\t"
                         "lqc2 $vf15, 0x0(%2)\n\t"
                         "vnop\n\t"
                         "vsub.xyz $vf8, $vf10, $vf11\n\t"
                         "vsub.xyz $vf9, $vf12, $vf11\n\t"
                         "vmulax.xyzw ACC, $vf4, $vf12x\n\t"
                         "vmadday.xyzw ACC, $vf5, $vf12y\n\t"
                         "vmaddaz.xyzw ACC, $vf6, $vf12z\n\t"
                         "vmaddw.xyzw $vf22, $vf7, $vf0w\n\t"
                         "vdiv Q, $vf0w, $vf22w\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyzw $vf22, $vf22, Q\n\t"
                         "vopmula.xyz ACC, $vf8, $vf9\n\t"
                         "vopmsub.xyz $vf2, $vf9, $vf8\n\t"
                         "vaddw.x $vf3, $vf0, $vf0w\n\t"
                         "vmulax.xyzw ACC, $vf4, $vf15x\n\t"
                         "vmadday.xyzw ACC, $vf5, $vf15y\n\t"
                         "vmaddaz.xyzw ACC, $vf6, $vf15z\n\t"
                         "vmaddw.xyzw $vf25, $vf7, $vf0w\n\t"
                         "vdiv Q, $vf0w, $vf25w\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyzw $vf25, $vf25, Q\n\t"
                         "vmul.xyz $vf2, $vf1, $vf2\n\t"
                         "vmove.w $vf12, $vf0\n\t"
                         "vmove.w $vf15, $vf0\n\t"
                         "vnop\n\t"
                         "vaddax.x ACC, $vf0, $vf2x\n\t"
                         "vmadday.x ACC, $vf3, $vf2y\n\t"
                         "vmaddz.x $vf2, $vf3, $vf2z\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "qmfc2.ni $7, $vf2\n\t"
                         "mtc1 $7, %0\n\t"
                         "lqc2 $vf8, 0x0(%5)\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vsub.xyzw $vf18, $vf22, $vf8\n\t"
                         "vsub.xyzw $vf19, $vf25, $vf8\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "sqc2 $vf18, 0x0(%3)\n\t"
                         "sqc2 $vf19, 0x0(%4)"
                         : "=f"(dot)
                         : "r"(pa), "r"(pb), "r"(&oa), "r"(&ob), "r"(&screenOrigin)
                         : "$7");
#endif

    rate[0] = rate[1] = 1.0f;
    /* a wholly visible edge returns at once */
    if (-ScreenWidth < oa.x && oa.x < ScreenWidth && -ScreenWidth < ob.x && ob.x < ScreenWidth &&
        -ScreenHeight < oa.y && oa.y < ScreenHeight && -ScreenHeight < ob.y &&
        ob.y < ScreenHeight) {
        return dot * sgn;
    }
    if ((oa.x <= -ScreenWidth && ob.x <= -ScreenWidth) ||
        (ScreenWidth <= oa.x && ScreenWidth <= ob.x) ||
        (oa.y <= -ScreenHeight && ob.y <= -ScreenHeight) ||
        (ScreenHeight <= oa.y && ScreenHeight <= ob.y)) {
        pb->w = -1.0f;
        pa->w = -1.0f;
        return -1.0f;
    }
    fw = ScreenWidth;
    fh = ScreenHeight;
    t0 = t1 = 1.0f;
    if (fw < oa.x && ob.x <= fw) {
        t = (ob.x - fw) / (ob.x - oa.x);
        if (t < t0 && t < 1.0f && 0.0f < t) {
            t0 = t;
        }
    } else if (fw < ob.x && oa.x <= fw) {
        t = (oa.x - fw) / (oa.x - ob.x);
        if (t < t1 && t < 1.0f && 0.0f < t) {
            t1 = t;
        }
    }
    if (oa.x < -fw && -fw <= ob.x) {
        t = (ob.x + fw) / (ob.x - oa.x);
        if (t < t0 && t < 1.0f && 0.0f < t) {
            t0 = t;
        }
    } else if (ob.x < -fw && -fw <= oa.x) {
        t = (oa.x + fw) / (oa.x - ob.x);
        if (t < t1 && t < 1.0f && 0.0f < t) {
            t1 = t;
        }
    }
    if (fh < oa.y && ob.y <= fh) {
        t = (ob.y - fh) / (ob.y - oa.y);
        if (t < t0 && t < 1.0f && 0.0f < t) {
            t0 = t;
        }
    } else if (fh < ob.y && oa.y <= fh) {
        t = (oa.y - fh) / (oa.y - ob.y);
        if (t < t1 && t < 1.0f && 0.0f < t) {
            t1 = t;
        }
    }
    if (oa.y < -fh && -fh <= ob.y) {
        t = (ob.y + fh) / (ob.y - oa.y);
        if (t < t0 && t < 1.0f && 0.0f < t) {
            t0 = t;
        }
    } else if (ob.y < -fh && -fh <= oa.y) {
        t = (oa.y + fh) / (oa.y - ob.y);
        if (t < t1 && t < 1.0f && 0.0f < t) {
            t1 = t;
        }
    }
    rate[0] = t0;
    rate[1] = t1;
    t0 = 1.0f - t0;
    t1 = 1.0f - t1;
    if (1.0f <= t0 + t1 || t0 + t1 <= 0.0f) {
        pb->w = -1.0f;
        pa->w = -1.0f;
        return -1.0f;
    }
    /* slide each end of the projected edge to the clip parameter found for
     * it */
    if (0.0f < rate[0] && rate[0] < 1.0f) {
#ifdef ICO_HOST
        {
            /* the projected end moved to rate 0 along the edge, back in
               screen coordinates; w gains the origin's w */
            float r = rate[0];
            float u = 1.0f - r;
            float *o = shadowVolumeRegs.proj[2];
            const float *org = (const float *)&screenOrigin;
            int k;

            for (k = 0; k < 3; k++) {
                o[k] = ((float *)&ob)[k] * u + ((float *)&oa)[k] * r;
            }
            for (k = 0; k < 4; k++) {
                o[k] = o[k] + org[k];
            }
        }
#else
        __asm__ __volatile__("mfc1 $8, %0\n\t"
                             "qmtc2.ni $8, $vf8\n\t"
                             "vsubx.w $vf8, $vf0, $vf8x\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vmulaw.xyz ACC, $vf19, $vf8w\n\t"
                             "vmaddx.xyz $vf22, $vf18, $vf8x\n\t"
                             "lqc2 $vf8, 0x0(%1)\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vadd.xyzw $vf22, $vf22, $vf8"
                             :
                             : "f"(rate[0]), "r"(&screenOrigin)
                             : "$8");
#endif
    }
    if (0.0f < rate[1] && rate[1] < 1.0f) {
#ifdef ICO_HOST
        {
            /* the projected end moved to rate 1 along the edge, back in
               screen coordinates; w gains the origin's w */
            float r = rate[1];
            float u = 1.0f - r;
            float *o = shadowVolumeRegs.proj[5];
            const float *org = (const float *)&screenOrigin;
            int k;

            for (k = 0; k < 3; k++) {
                o[k] = ((float *)&oa)[k] * u + ((float *)&ob)[k] * r;
            }
            for (k = 0; k < 4; k++) {
                o[k] = o[k] + org[k];
            }
        }
#else
        __asm__ __volatile__("mfc1 $8, %0\n\t"
                             "qmtc2.ni $8, $vf8\n\t"
                             "vsubx.w $vf8, $vf0, $vf8x\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vmulaw.xyz ACC, $vf18, $vf8w\n\t"
                             "vmaddx.xyz $vf25, $vf19, $vf8x\n\t"
                             "lqc2 $vf8, 0x0(%1)\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vnop\n\t"
                             "vadd.xyzw $vf25, $vf25, $vf8"
                             :
                             : "f"(rate[1]), "r"(&screenOrigin)
                             : "$8");
#endif
    }
    return dot * sgn;
}

/* The first two edges of a strip, which open it. Returns the
 * number of strip positions still to skip because an opening vertex was
 * marked away. */
static inline int clipVolumeHead(VECTOR *ta, VECTOR *ba, VECTOR *tb, VECTOR *bb,
                                 float sgn) /* derived name */
{
    int state = 0;

    if (clipVolumeEdge(ta, ba, sgn) == -1.0f && ta->w == -1.0f) {
        state = 1;
    }
    if (clipVolumeEdge(tb, bb, -sgn) == -1.0f && tb->w == -1.0f) {
        state = 2;
    }
    return state;
}

#define VOLUME_EDGE(a, b, c) /* derived name */                                                    \
    VU0_REG("vsub.xy $vf8, $vf" #a ", $vf" #b "\n\t"                                               \
            "vsub.xy $vf9, $vf" #c ", $vf" #b)

/* the facing z of the strip position i. The three positions that share a face
 * with the position two before them read the facing that one measured back
 * out of stripFaceZ instead of taking the cross product again. The cross
 * product of the two edges left in vf8 and vf9 is written straight into the
 * table. */
static inline float volumeStripFaceZ(int i) /* derived name */
{
#ifdef ICO_HOST
    /* the z of (A - B) x (C - B) over x and y, with A, B and C the projected
       strip vertices VOLUME_EDGE names (vf20-vf25 = proj[0..5]) */
    const float (*p)[4] = shadowVolumeRegs.proj;
    const float *a = p[0];
    const float *b = p[0];
    const float *c = p[0];
    float e8x, e8y, e9x, e9y;

    switch (i) {
    case 0:
    case 1:
        return 1.0f;
    case 2:
        a = p[0], b = p[1], c = p[3];
        break;
    case 3:
        return -stripFaceZ[2];
    case 4:
        a = p[3], b = p[4], c = p[5];
        break;
    case 5:
        a = p[4], b = p[5], c = p[1];
        break;
    case 6:
        return -stripFaceZ[5];
    case 7:
        a = p[1], b = p[2], c = p[0];
        break;
    case 8:
        a = p[2], b = p[0], c = p[5];
        break;
    case 9:
        return -stripFaceZ[8];
    }
    e8x = a[0] - b[0];
    e8y = a[1] - b[1];
    e9x = c[0] - b[0];
    e9y = c[1] - b[1];
    stripFaceZ[i] = e8x * e9y - e9x * e8y;
    return stripFaceZ[i];
#else
    switch (i) {
    case 0:
    case 1:
        return 1.0f;
    case 2:
        VOLUME_EDGE(20, 21, 23);
        break;
    case 3:
        return -stripFaceZ[2];
    case 4:
        VOLUME_EDGE(23, 24, 25);
        break;
    case 5:
        VOLUME_EDGE(24, 25, 21);
        break;
    case 6:
        return -stripFaceZ[5];
    case 7:
        VOLUME_EDGE(21, 22, 20);
        break;
    case 8:
        VOLUME_EDGE(22, 20, 25);
        break;
    case 9:
        return -stripFaceZ[8];
    }
    __asm__ __volatile__("vopmula.xyz ACC, $vf8, $vf9\n\t"
                         "vopmsub.xyz $vf2, $vf9, $vf8\n\t"
                         "vaddz.x $vf2, $vf0, $vf2z\n\t"
                         "qmfc2.ni $7, $vf2\n\t"
                         "mtc1 $7, %0"
                         : "=f"(stripFaceZ[i])
                         :
                         : "$7");
    return stripFaceZ[i];
#endif
}

/* the strip is dropped whole if any of its six vertices left
 * the guard band or went behind the eye */
static inline int volumeVertsOutOfRange(int *vi) /* derived name */
{
    int n;
    int *v;

    /* the cursor is set in the for; the parameter itself is never stepped */
    for (n = 0, v = vi; n < 6; n++, v += 4) {
        if (v[0] < 0x11 || 0xFFEF < v[0] || v[1] < 0x11 || 0xFFEF < v[1] || v[2] < 0) {
            return 1;
        }
    }
    return 0;
}

/* One shadow volume strip. Ten positions of a triangle strip
 * over the six projected vertices, each position carrying the front or the
 * back stencil colour its facing picks. */
static inline unsigned long long *emitVolumeStrip(unsigned long long *p,
                                                  float sign) /* derived name */
{
    int order[20] = {0, 1, 3, 4, 5, 1, 2, 0, 5, 3, 3, 4, 0, 1, 2, 4, 5, 3, 2, 0};
    int vi[6][4];
    int i;
    int k;
    int *v;
    /* the running sign is a local copy, so the caller's winding sign is
     * never written */
    float sgn = sign;

    storeVolumeVerts(vi);
    if (volumeVertsOutOfRange(&vi[0][0])) {
        return p;
    }
    p[0] = 0x1000000000008001LL;
    p[1] = 0xE;
    p[2] = 0x144;
    p[3] = 0;
    p[4] = 0x240000000000800ALL;
    p[5] = 0x51;
    p += 6;
    /* the for steps the index, flips the sign and steps the vertex word's
     * pointer; the colour word steps its own pointer in each arm */
    for (i = 0; i < 10; i++, sgn = -sgn, p++) {
        k = order[i];
        if (volumeStripFaceZ(i) * sgn < 0.0f) {
            *p++ = 0x3F80000080040404LL;
        } else {
            *p++ = 0x3F80000080FCFCFCLL;
        }
        v = vi[k];
        *p = (long long)v[0] | ((long long)v[1] << 16) | ((long long)v[2] << 32);
#ifdef ICO_RD
        shadowHostStripPos(i, v, (unsigned char)p[-1] == 0x04);
#endif
    }
    return p;
}

static void __GetCameraPos(VECTOR *pos)
{
    _PushCurrentMatrix();
    _SetCurrentMatrix(matrixptr + 0x80);
    _ClearTransCurrentMatrix();
    _TransposeCurrentMatrix();
    _ApplyCurrentMatrix(pos, matrixptr + 0xB0);
    _ScaleVector(pos, pos, -1.0f);
    pos->w = 1.0f;
    _PopCurrentMatrix();
}

/* One sixteen-byte record of a part's silhouette strip list. A strip opens
 * with a record whose count is its vertex count (0 ends the list); each
 * vertex record that follows carries its facing flag in the same short and
 * its vertex index at +4. Only two-byte aligned, which is why
 * shadow_MakeObjectData copies it with ldl/ldr. */
typedef struct ShadowRun { /* field names derived */
    short count;
    short pad2;
    short vtx;
    char pad6[10];
} ShadowRun; /* derived name */

void shadow_RenderVolume(Sub15C *o)
{
    VECTOR pos;
    VECTOR cam;
    PObjModel *x = o->shadow;
    float len = o->model->shadowLength;
    unsigned long long *p;
    unsigned long long *start;
    char *c;
    char *q;
    PObjPart *part;
    ShadowRun *e;
    VECTOR *top;
    VECTOR *bot;
    int i;
    unsigned int j;
    int k;
    int n;
    int state;
    float sgn;
    float r;

    if (x->mode.bits & 0x04000000) {
        return;
    }
    if (len != x->shadowLength && 0.0f < x->shadowLength) {
        len = x->shadowLength;
    }
    __GetCameraPos(&cam);
    GetRootPositionByDObj(&pos, o);
    _GetLength(&pos, &cam);
    if (o->dispType == 1) {
        shadow_EntryClusterShadow(o, len);
    } else {
        shadow_EntryNormalShadow(o, 0, len);
    }
    dl_SetDLPriority(3);
#ifdef ICO_RD
    shadowHostBegin(o);
#endif
    shadow_getShadowVectorAverage(&pos, o);
    loadVolumeMatrix(&pos);
    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.dma.c = c;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.tail.c = c;
    PacketBufferStruct.ptr.c = (c + 8);
    ((GifPkWord *)(c + 8))->w[0] = 0x11000000;
    PacketBufferStruct.gif.c = c + 0xC;
    PacketBufferStruct.ptr.c = (c + 0x10);
    p = PacketBufferStruct.ptr.d;
    start = p;
    for (i = 0; i < x->partCount; i++) {
        part = &x->parts[i];
        top = (VECTOR *)part->vtxSave;
        bot = (VECTOR *)part->nrmSave;
        for (j = 0; j < part->stripCount; j++) {
            e = ((ShadowRun **)part->strips)[j];
            while ((n = (e++)->count) != 0) {
                sgn = e->count != 0 ? 1.0f : -1.0f;
                state = clipVolumeHead(&top[e[0].vtx], &bot[e[0].vtx], &top[e[1].vtx],
                                       &bot[e[1].vtx], sgn);
                e += 2;
                for (k = 2; k < n; k++) {
                    if (top[e->vtx].w == -1.0f) {
                        state = 3;
                    } else if (state) {
                        state--;
                    }
                    r = clipVolumeEdge(&top[e->vtx], &bot[e->vtx], sgn);
                    if (0.0f <= r) {
                        if (state == 0) {
                            p = emitVolumeStrip(p, sgn);
                        }
                    } else if (top[e->vtx].w == -1.0f) {
                        state = 3;
                    }
                    sgn = -sgn;
                    e++;
                }
            }
        }
    }
    PacketBufferStruct.ptr.d = p;
    ((GifPkWord *)PacketBufferStruct.tail.c)->d =
        (unsigned int)((((unsigned int)((char *)p - PacketBufferStruct.tail.c) >> 4) - 1) |
                       0x10000000);
    ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
        ((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) | 0x50000000;
    q = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = q;
    ((GifPkWord *)q)->d = 0x60000000;
    PacketBufferStruct.ptr.c = (q + 8);
    ((GifPkWord *)(q + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = (q + 0xC);
    ((GifPkWord *)(q + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = (q + 0x10);
    if (p - start > 0) {
        dl_SetDLPriority(dl_GetPri());
#ifdef ICO_RD
        shadowHostFlush();
#endif
        dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
        dl_CloseDma();
    }
    dl_SetDLPriority(0);
}

void shadow_RenderVolumeMulti(Sub15C *o, int idx)
{
    VECTOR pos;
    VECTOR cam;
    PObjModel *x = o->shadow;
    float len = o->model->shadowLength;
    unsigned long long *p;
    unsigned long long *start;
    char *c;
    char *q;
    PObjPart *part;
    ShadowRun *e;
    VECTOR *top;
    VECTOR *bot;
    int i;
    unsigned int j;
    int k;
    int n;
    int state;
    float sgn;
    float r;

    if (x->mode.bits & 0x04000000) {
        return;
    }
    if (len != x->shadowLength && 0.0f < x->shadowLength) {
        len = x->shadowLength;
    }
    __GetCameraPos(&cam);
    GetRootPositionByDObj(&pos, o);
    _GetLength(&pos, &cam);
    shadow_EntryNormalShadow(o, idx, len);
    dl_SetDLPriority(3);
#ifdef ICO_RD
    shadowHostBegin(o);
#endif
    shadow_getShadowVectorAverage(&pos, o);
    loadVolumeMatrix(&pos);
    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.dma.c = c;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.tail.c = c;
    PacketBufferStruct.ptr.c = (c + 8);
    ((GifPkWord *)(c + 8))->w[0] = 0x11000000;
    PacketBufferStruct.gif.c = c + 0xC;
    PacketBufferStruct.ptr.c = (c + 0x10);
    p = PacketBufferStruct.ptr.d;
    start = p;
    for (i = 0; i < x->partCount; i++) {
        part = &x->parts[i];
        top = (VECTOR *)part->vtxSave;
        bot = (VECTOR *)part->nrmSave;
        for (j = 0; j < part->stripCount; j++) {
            e = ((ShadowRun **)part->strips)[j];
            while ((n = (e++)->count) != 0) {
                sgn = e->count != 0 ? 1.0f : -1.0f;
                state = clipVolumeHead(&top[e[0].vtx], &bot[e[0].vtx], &top[e[1].vtx],
                                       &bot[e[1].vtx], sgn);
                e += 2;
                for (k = 2; k < n; k++) {
                    if (top[e->vtx].w == -1.0f) {
                        state = 3;
                    } else if (state) {
                        state--;
                    }
                    r = clipVolumeEdge(&top[e->vtx], &bot[e->vtx], sgn);
                    if (0.0f <= r) {
                        if (state == 0) {
                            p = emitVolumeStrip(p, sgn);
                        }
                    } else if (top[e->vtx].w == -1.0f) {
                        state = 3;
                    }
                    sgn = -sgn;
                    e++;
                }
            }
        }
    }
    PacketBufferStruct.ptr.d = p;
    ((GifPkWord *)PacketBufferStruct.tail.c)->d =
        (unsigned int)((((unsigned int)((char *)p - PacketBufferStruct.tail.c) >> 4) - 1) |
                       0x10000000);
    ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
        ((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) | 0x50000000;
    q = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = q;
    ((GifPkWord *)q)->d = 0x60000000;
    PacketBufferStruct.ptr.c = (q + 8);
    ((GifPkWord *)(q + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = (q + 0xC);
    ((GifPkWord *)(q + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = (q + 0x10);
    if (p - start > 0) {
        dl_SetDLPriority(dl_GetPri());
#ifdef ICO_RD
        shadowHostFlush();
#endif
        dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
        dl_CloseDma();
    }
    dl_SetDLPriority(0);
}

/* The three record shapes shadow_MakeObjectData copies out of the model into
 * its own heap.  The vertex and polygon records are eight-byte aligned; the
 * strip record starts with a short count and is two-byte aligned. */
typedef struct ShadowVtx { /* field names derived */
    int _0;
    int _4;
    int _8;
    int _C;
} __attribute__((aligned(16))) ShadowVtx; /* derived name */

typedef struct ShadowPoly {      /* field names derived */
    ICO_EEWORD(ShadowVtx *) pts; /* an EE word on the host (eeword.h) */
    int _4;
    int _8;
    int _C;
} __attribute__((aligned(16))) ShadowPoly; /* derived name */

#ifdef ICO_HOST

_Static_assert(sizeof(ShadowPoly) == 16, "ShadowPoly is the file's 0x10-byte entry");

_Static_assert(sizeof(ClusterPoly) == 16, "ClusterPoly is the file's 0x10-byte entry");

#endif

void shadow_MakeObjectData(PObjModel *mdl)
{
    int i;
    int j;
    int l;
    /* zeroed here and again after each polygon, so the scan starts from a
     * value carried round the loop */
    int m = 0;
    int n;
    int c;
    PObjPart *p;
    ShadowVtx *q;
    ShadowPoly *r;
    ShadowVtx *t;
    ShadowRun **s;
    ShadowRun *u;

    for (i = 0; i < mdl->partCount; i++) {
        p = &mdl->parts[i];
        if (mdl->disp != 0) {
            p->vtxSave = mallocseki(p->vtxCount * 16);
            p->nrmSave = mallocseki(p->vtxCount * 16);
            q = (ShadowVtx *)mallocseki(p->vtxCount * 16);
            for (j = 0; j < p->vtxCount; j++) {
                _CopyVector(&q[j], p->vtx + j * 16);
            }
            p->vtx = (char *)q;

            r = (ShadowPoly *)mallocseki(p->polyCount * 16);
            for (j = 0; j < p->polyCount; j++) {
                r[j] = ((ShadowPoly *)p->polys)[j];
                while (ICO_EEPTR(ShadowVtx *, r[j].pts)[m]._0 != -1) {
                    m++;
                }
#ifdef ICO_HOST
                t = (ShadowVtx *)mallocseki((m + 1) * 16);
                r[j].pts = ICO_EEW(t);
#else
                t = r[j].pts = (ShadowVtx *)mallocseki((m + 1) * 16);
#endif
                for (l = 0; l < m + 1; l++) {
                    *t++ = ICO_EEPTR(ShadowVtx *, ((ShadowPoly *)p->polys)[j].pts)[l];
                }
                m = 0;
            }
            p->polys = r;
        } else {
            p->vtxSave = mallocseki(p->vtxCount * 16);
            p->nrmSave = mallocseki(p->vtxCount * 16);
            q = (ShadowVtx *)mallocseki(p->vtxCount * 16);
            for (j = 0; j < p->vtxCount; j++) {
                _CopyVector(&q[j], p->vtx + j * 16);
            }
            p->vtx = (char *)q;
        }

        s = (ShadowRun **)mallocseki(p->stripCount * sizeof(ShadowRun *));
        /* the strip pass reuses the outer loop's own index, so the outer
         * loop steps on from where this one ended */
        for (i = 0; i < p->stripCount; i++) {
            u = ((ShadowRun **)p->strips)[i];
            n = 0;
            for (;;) {
                c = u->count;
                if (c == 0) {
                    break;
                }
                u += c + 1;
                n += c + 1;
            }
            n++;
            s[i] = (ShadowRun *)mallocseki(n * 16);
            for (j = 0; j < n; j++) {
                s[i][j] = ((ShadowRun **)p->strips)[i][j];
            }
        }
        p->strips = s;
    }
}

inline void shadow_KillShadow(int val)
{
    killShadowRequest = val;
}

inline void shadow_DispCancel(int id, int cancel)
{
    GObj *obj = isysGObjGetExist_begin();
    if (obj != 0) {
        long long bit = (long long)(cancel & 1) << 26;
        do {
            Sub15C *node = obj->dobj;
            if (node != 0) {
                PObjModel *dl = node->model;
                if (dl != 0) {
                    PObjModel *x = node->shadow;
                    if (x != 0) {
                        if (dl->mode.s.id == id) {
                            x->mode.bits = (x->mode.bits & ~0x04000000) | bit;
                        }
                    }
                }
            }
            obj = isysGObjGetExist_next(obj);
        } while (obj != 0);
    }
}

inline void shadow_SetLength(Sub15C *o, float len)
{
    if (0.0f < len) {
        o->shadow->shadowLength = len;
    } else {
        o->shadow->shadowLength = o->model->shadowLength;
    }
}

inline void shadow_Init(void)
{
    GObj *obj;
    killShadow = 0;
    killShadowRequest = 0;
    for (obj = isysGObjGetExist_begin(); obj != 0; obj = isysGObjGetExist_next(obj)) {
        Sub15C *node = GOBJ_SUB(obj);
        if (node != 0) {
            PObjModel *dl = node->model;
            if (dl != 0) {
                PObjModel *x = node->shadow;
                if (x != 0) {
                    x->mode.bits &= ~0x04000000;
                }
            }
        }
    }
}

/* one row of the shadow tool: the name it prints, the variable it edits and
 * the range it wraps that variable through */
typedef struct ShadowToolRow { /* field names derived */
    char *name;
    int *val;
    int min;
    int max;
} ShadowToolRow; /* derived name */

/* the tool's eight rows, each a word of the stage record: the shadow depth,
 * its colour and the four blend weights */
static const ShadowToolRow shadowToolRows[] = {
    /* derived name */
    {" Shadow Depth      ", &GlobalStageSetting.shadowDepth, 0, 128},
    {" Shadow Color R    ", &GlobalStageSetting.shadowColR, 0, 255},
    {" Shadow Color G    ", &GlobalStageSetting.shadowColG, 0, 255},
    {" Shadow Color B    ", &GlobalStageSetting.shadowColB, 0, 255},
    {" Shadow Blend 1/1  ", &GlobalStageSetting.shadowBlend[0], 0, 128},
    {" Shadow Blend 1/4  ", &GlobalStageSetting.shadowBlend[1], 0, 128},
    {" Shadow Blend 1/16 ", &GlobalStageSetting.shadowBlend[2], 0, 128},
    {" Shadow Blend 1/64 ", &GlobalStageSetting.shadowBlend[3], 0, 128},
};

/* the two menu colours, unselected then selected, as ZFog's fogRowColor */
static const unsigned int shadowRowColor[] = {0xFFFFFF00, 0xFF000000}; /* derived name */

/* an initialised word nothing reads */
static int shadowUnusedWord = -1; /* derived name */

/* the names a 0/1 row prints instead of its number, as ZFog's fogOnOffText */
static char *shadowOnOffText[] = {"Off", "On"}; /* derived name */

/* the row the tool has selected */
static int toolRow = 0; /* derived name */

int shadow_Tool(void)
{
    int ret = 0;
    int i;

    debug_PrintfDummy(10, 50, 0xFF800000u, "Shadow Tool");
    for (i = 0; i < 8; i++) {
        if (shadowToolRows[i].min == 0 && shadowToolRows[i].max == 1) {
            debug_PrintfDummy(18, (i + 1) * 8 + 50, shadowRowColor[toolRow == i], "%s : %s",
                              shadowToolRows[i].name, shadowOnOffText[*shadowToolRows[i].val]);
        } else {
            debug_PrintfDummy(18, (i + 1) * 8 + 50, shadowRowColor[toolRow == i], "%s : %d",
                              shadowToolRows[i].name, *shadowToolRows[i].val);
        }
    }
    if (pad[0].rep & 0x4000) {
        toolRow++;
        if (8 <= toolRow) {
            toolRow = 0;
        }
    }
    if (pad[0].rep & 0x1000) {
        toolRow--;
        if (toolRow < 0) {
            toolRow = 7;
        }
    }
    if (pad[0].rep & 0x2000) {
        if (++*shadowToolRows[toolRow].val > shadowToolRows[toolRow].max) {
            *shadowToolRows[toolRow].val = shadowToolRows[toolRow].min;
        }
    }
    if (pad[0].rep & 0x8000) {
        if (--*shadowToolRows[toolRow].val < shadowToolRows[toolRow].min) {
            *shadowToolRows[toolRow].val = shadowToolRows[toolRow].max;
        }
    }
    if (pad[0].flags & 0x20) {
        for (i = 0; i < 8; i++) {
            if (shadowToolRows[i].min == 0 && shadowToolRows[i].max == 1) {
                debug_StdPrintfDummy("Shadow %s => %s\n", shadowToolRows[i].name,
                                     shadowOnOffText[*shadowToolRows[i].val]);
            } else {
                debug_StdPrintfDummy("Shadow %s => %d\n", shadowToolRows[i].name,
                                     *shadowToolRows[i].val);
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
    return ret;
}
