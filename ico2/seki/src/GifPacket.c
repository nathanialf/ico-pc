#include "typedef.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "DmaPacket.h"
#include "DisplayList.h"

typedef struct { /* field names derived */
    int a, b, c, d;
} GsAlphaEnt; /* derived name */

/* The display-list packet builder state.  `ptr` is the write cursor; `dma`,
   `tail`, `gif` and `end` are the back-pointers into the packet that
   gif_EndPacket patches once the packet's size is known (DMA tag, source
   chain tail, VIF DIRECT code and GIF tag respectively). */

/* One 64-bit slot of a DMA/GIF packet: written either as the whole qword
   (DMA tag, GIF tag, A+D data) or as one of its two 32-bit halves. */

#ifdef ICO_RD

enum { GIF_SP_AUTO = 0, GIF_SP_UI = 1, GIF_SP_WORLD = 2 }; /* port: GIF_ENTER's spaces */

/* PC port, windowed build (renderer wave 2, R2a): the GS register decoder at
   the end of this file turns every register write into rd state and
   primitives (GifHost.h).  setGsReg feeds it instead of the packet, so the
   gif_* helpers below keep their bodies; GIF_ENTER marks each entry point
   with the space its vertices are in (RdSpace: UI for the 640 x 224 layout
   helpers, WORLD for the CPU-projected strips, else by list) and first
   decodes whatever raw A+D writes callers put in the open packet since the
   last entry, so the two stay in order. */
#include <stdio.h>
#include <string.h>
#include "GifHost.h"

static void gsWrite(unsigned long long reg, unsigned long long data);
static void gsPacketStart(void);
static void gsPacketEnd(void);
static int gifEnter(int space);
static void gifLeave(int *scope);

#define GIF_ENTER(sp) int gifScope_ __attribute__((cleanup(gifLeave), unused)) = gifEnter(sp)

static inline void setGsReg(long long reg, long long data)
{
    gsWrite((unsigned long long)reg, (unsigned long long)data);
}

#else
/* a block-scope declaration, so it emits nothing and may precede the
   function's own declarations under the period compiler */
#define GIF_ENTER(sp) extern int gifNoScope_

/* one A+D register write, which gif_SetGsReg and most of this file inline */
static inline void setGsReg(long long reg, long long data) /* derived name */
{
    *PacketBufferStruct.ptr.d++ = data;
    *PacketBufferStruct.ptr.d++ = reg;
}

#endif
/* The two GS register payloads this file packs over and over: RGBAQ from a
   4-byte colour, and XYZ2 from a 2D screen point plus a 64-bit Z.  The GS
   window origin is 2048.0 pixels, i.e. 0x8000 in 1/16-pixel units. */
#define GIF_RGBA(c) /* derived name */                                                             \
    ((long long)(c)[0] | ((long long)(c)[1] << 8) | ((long long)(c)[2] << 16) |                    \
     ((long long)(c)[3] << 24))
/* the same packed XYZ2 word with the window origin already folded into the
   coordinates (the sprite family offsets its size once, then adds the corner) */
/* the same RGBAQ payload from a GifColor */
#define GIF_COLOR(c) /* derived name */                                                            \
    ((long long)(c)->r | ((long long)(c)->g << 8) | ((long long)(c)->b << 16) |                    \
     ((long long)(c)->a << 24))
#define GIF_XY0(x, y, z) ((long long)(x) | ((long long)(y) << 16) | ((z) << 32)) /* derived name */
/* the ST/UV pair the textured-sprite family packs into the UV register */
#define GIF_UV(u, v) ((long long)(u) | ((long long)(v) << 16)) /* derived name */
#define GIF_XY(x, y, z)                                        /* derived name */                  \
    ((long long)((x) + 0x8000) | ((long long)((y) + 0x8000) << 16) | ((z) << 32))
#define GIF_XYZ(v, z) GIF_XY((v)[0], (v)[1], z) /* derived name */
/* The "Offset" family adds the float draw origin (in 1/16-pixel units) instead
   of the fixed 2048.0-pixel window origin. */
#define GIF_OX ((int)center_X * 16)                                   /* derived name */
#define GIF_OY ((int)center_Y * 16)                                   /* derived name */
#define GIF_XYZOFF(v, z) GIF_XY0(GIF_OX + (v)[0], GIF_OY + (v)[1], z) /* derived name */

static void gif_StartPacket(void)
{
    char *c;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.dma.c = c;
    PacketBufferStruct.tail.c = c;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0x11000000;
    PacketBufferStruct.gif.c = c + 0xC;
    PacketBufferStruct.end.c = c + 0x10;
    PacketBufferStruct.ptr.c = c + 0x18;
    ((GifPkWord *)(c + 0x18))->d = 0xE;
    PacketBufferStruct.ptr.c = c + 0x20;
}

/* the open-packet flag gif_EndPacket clears, then the strip drawers' last two on-screen
   flags and the index of the older one. */
static int packetOpen = 0; /* derived name */

void gif_EndPacket(void)
{
    char *p;

#ifdef ICO_RD
    gsPacketEnd();
#endif
    ((GifPkWord *)PacketBufferStruct.end.c)->d =
        (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.end.c) >> 4) -
                       1) |
        0x1000000000008000LL;
    ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
        ((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) | 0x50000000;
    ((GifPkWord *)PacketBufferStruct.tail.c)->d =
        (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.tail.c) >>
                         4) -
                        1) |
                       0x10000000);
    p = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = p;
    ((GifPkWord *)p)->d = 0x60000000;
    PacketBufferStruct.ptr.c = p + 8;
    ((GifPkWord *)(p + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 0xC;
    ((GifPkWord *)(p + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = p + 0x10;
    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
    packetOpen = 0;
}

static void gif_StartPacketPath1(void)
{
    char *c;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.dma.c = c;
    PacketBufferStruct.tail.c = c;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0x11000000;
    PacketBufferStruct.gif.c = c + 0xC;
    PacketBufferStruct.end.c = c + 0x10;
    PacketBufferStruct.ptr.c = c + 0x18;
    ((GifPkWord *)(c + 0x18))->d = 0xE;
    PacketBufferStruct.ptr.c = c + 0x20;
}

void gif_EndPacketPath1(void)
{
    char *p;
    char *q;

#ifdef ICO_RD
    gsPacketEnd();
#endif
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
    PacketBufferStruct.ptr.d = (unsigned long long *)p;
    ((GifPkWord *)p)->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 4;
    ((GifPkWord *)(p + 4))->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 8;
    ((GifPkWord *)(p + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 0xC;
    ((GifPkWord *)PacketBufferStruct.tail.c)->d =
        (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.tail.c) >>
                         4) -
                        1) |
                       0x10000000);
    q = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = q;
    ((GifPkWord *)q)->d = 0x60000000;
    PacketBufferStruct.ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = q + 0xC;
    ((GifPkWord *)(q + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = q + 0x10;
    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
    packetOpen = 0;
}

void gif_MakeLine2DOffset(int *v0, int *v1, long long z0, long long z1, unsigned char *col,
                          int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    setGsReg(0x00, ((long long)prim << 6) | 0xA);
    setGsReg(0x01, GIF_RGBA(col));
    setGsReg(0x05, GIF_XYZOFF(v0, z0));
    setGsReg(0x05, GIF_XYZOFF(v1, z1));
}

void gif_MakeSprite(int x, int y, int w, int h, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    int fx = w + 0x8000;
    int fy = h + 0x8000;

    setGsReg(0x00, (prim << 6) | 0x116);
    setGsReg(0x01, GIF_COLOR(col));
    setGsReg(0x03, GIF_UV(uv->x, uv->y));
    setGsReg(0x05, GIF_XY(x, y, z));
    setGsReg(0x03, GIF_UV(uv->x + uv->w, uv->y + uv->h));
    setGsReg(0x05, GIF_XY0(x + fx, y + fy, z));
}

void gif_MakeSpriteOffset(int x, int y, int w, int h, long long z, GifRect *uv, GifColor *col,
                          int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    setGsReg(0x00, (prim << 6) | 0x116);
    setGsReg(0x01, GIF_COLOR(col));
    setGsReg(0x03, GIF_UV(uv->x, uv->y));
    setGsReg(0x05, GIF_XY0(GIF_OX + x, GIF_OY + y, z));
    setGsReg(0x03, GIF_UV(uv->x + uv->w, uv->y + uv->h));
    setGsReg(0x05, GIF_XY0(GIF_OX + x + w, GIF_OY + y + h, z));
}

void gif_MakeSpriteWithStrip(int *r, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    setGsReg(0x00, (prim << 6) | 0x114);
    setGsReg(0x01, GIF_COLOR(col));
    setGsReg(0x03, GIF_UV(uv->x, uv->y));
    setGsReg(0x0D, GIF_XY(r[0], r[1], z));
    setGsReg(0x03, GIF_UV(uv->x, uv->y + uv->h));
    setGsReg(0x0D, GIF_XY(r[2], r[3], z));
    setGsReg(0x03, GIF_UV(uv->x + uv->w, uv->y));
    setGsReg(0x05, GIF_XY(r[4], r[5], z));
    setGsReg(0x03, GIF_UV(uv->x + uv->w, uv->y + uv->h));
    setGsReg(0x05, GIF_XY(r[6], r[7], z));
}

/* a point at an offset position, which gif_MakePoint2DOffset and
   gif_PointOffset inline */
static inline void makePoint2DOffset(int *v, long long z, unsigned char *col,
                                     int prim) /* derived name */
{
    setGsReg(0x00, 0x100 | ((long long)prim << 6));
    setGsReg(0x01, GIF_RGBA(col));
    setGsReg(0x05, GIF_XYZOFF(v, z));
}

void gif_PointOffset(int *v, long long z, unsigned char *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int p[4];

    p[0] = v[0] * ScreenWidth / 640;
    p[1] = v[1] * ScreenHeight / 224;
    makePoint2DOffset(p, z, col, prim);
}

/* a two-point line, which gif_MakeLine2D and gif_Line inline */
static inline void makeLine2D(int *v0, int *v1, long long z0, long long z1, unsigned char *col,
                              int prim) /* derived name */
{
    setGsReg(0x00, ((long long)prim << 6) | 0xA);
    setGsReg(0x01, GIF_RGBA(col));
    setGsReg(0x05, GIF_XYZ(v0, z0));
    setGsReg(0x05, GIF_XYZ(v1, z1));
}

void gif_Line(int *v0, int *v1, long long z0, long long z1, unsigned char *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int p0[4];
    int p1[4];

    p0[0] = v0[0] * ScreenWidth / 640 * 16;
    p0[1] = v0[1] * ScreenHeight / 224 * 16;
    p1[0] = v1[0] * ScreenWidth / 640 * 16;
    p1[1] = v1[1] * ScreenHeight / 224 * 16;
    makeLine2D(p0, p1, z0, z1, col, prim);
}

/* an untextured sprite, which gif_MakeSpriteNoTexture and the Sprite
   wrappers inline; gif_MakeSprite stays a call */
static inline void makeSpriteNoTexture(int x, int y, int w, int h, long long z, GifColor *col,
                                       int prim) /* derived name */
{
    int fx = w + 0x8000;
    int fy = h + 0x8000;

    setGsReg(0x00, (prim << 6) | 0x406);
    setGsReg(0x01, GIF_COLOR(col));
    setGsReg(0x05, GIF_XY(x, y, z));
    setGsReg(0x05, GIF_XY0(x + fx, y + fy, z));
}

/* an untextured sprite at an offset position, which
   gif_MakeSpriteNoTextureOffset and the offset Sprite wrappers inline */
static inline void makeSpriteNoTextureOffset(int x, int y, int w, int h, long long z, GifColor *col,
                                             int prim) /* derived name */
{
    setGsReg(0x00, (prim << 6) | 0x406);
    setGsReg(0x01, GIF_COLOR(col));
    setGsReg(0x05, GIF_XY0(GIF_OX + x, GIF_OY + y, z));
    setGsReg(0x05, GIF_XY0(GIF_OX + x + w, GIF_OY + y + h, z));
}

void gif_Sprite(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int x = r->x * ScreenWidth / 640 * 16;
    int y = r->y * ScreenHeight / 224 * 16;
    int w = r->w * ScreenWidth / 640 * 16;
    int h = r->h * ScreenHeight / 224 * 16;

    if (uv == 0) {
        makeSpriteNoTexture(x, y, w, h, z, col, prim);
    } else {
        GifRect t = *uv;

        t.x *= 16;
        t.y *= 16;
        t.w *= 16;
        t.h *= 16;
        gif_MakeSprite(x, y, w, h, z, &t, col, prim);
    }
}

void gif_SpriteSensitive(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int x = r->x * ScreenWidth / 640;
    int y = r->y * ScreenHeight / 224;
    int w = r->w * ScreenWidth / 640;
    int h = r->h * ScreenHeight / 224;

    if (uv) {
        gif_MakeSprite(x, y, w, h, z, uv, col, prim);
    } else {
        makeSpriteNoTexture(x, y, w, h, z, col, prim);
    }
}

void gif_SpriteOffset(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int x = r->x * ScreenWidth / 640 * 16;
    int y = r->y * ScreenHeight / 224 * 16;
    int w = r->w * ScreenWidth / 640 * 16;
    int h = r->h * ScreenHeight / 224 * 16;

    if (uv == 0) {
        makeSpriteNoTextureOffset(x, y, w, h, z, col, prim);
    } else {
        GifRect t = *uv;

        t.x *= 16;
        t.y *= 16;
        t.w *= 16;
        t.h *= 16;
        gif_MakeSpriteOffset(x, y, w, h, z, &t, col, prim);
    }
}

void gif_SpriteSensitiveOffset(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int x = r->x * ScreenWidth / 640;
    int y = r->y * ScreenHeight / 224;
    int w = r->w * ScreenWidth / 640;
    int h = r->h * ScreenHeight / 224;

    if (uv) {
        gif_MakeSpriteOffset(x, y, w, h, z, uv, col, prim);
    } else {
        makeSpriteNoTextureOffset(x, y, w, h, z, col, prim);
    }
}

void gif_SpriteOrg(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    int x = r->x * 16;
    int y = r->y * 16;
    int w = r->w * 16;
    int h = r->h * 16;

    if (uv == 0) {
        makeSpriteNoTexture(x, y, w, h, z, col, prim);
    } else {
        GifRect t = *uv;

        t.x *= 16;
        t.y *= 16;
        t.w *= 16;
        t.h *= 16;
        gif_MakeSprite(x, y, w, h, z, &t, col, prim);
    }
}

void gif_SpriteSensitiveOrg(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    int x = r->x;
    int y = r->y;
    int w = r->w;
    int h = r->h;

    if (uv) {
        gif_MakeSprite(x, y, w, h, z, uv, col, prim);
    } else {
        makeSpriteNoTexture(x, y, w, h, z, col, prim);
    }
}

void gif_SetDrawEnviroment(unsigned long long fbp, unsigned long long psm, unsigned int w,
                           unsigned int h, int useoffset, int clear)
{
    GIF_ENTER(GIF_SP_AUTO);
    setGsReg(0x4C,
             (fbp >> 5) | ((unsigned long long)((w >> 6) & 0x3F) << 16) | ((psm & 0xF) << 24));
    setGsReg(0x40, ((unsigned long long)(w - 1) << 16) | ((unsigned long long)(h - 1) << 48));
    if (clear) {
        setGsReg(0x47, 0x30000);
        setGsReg(0x00, 6);
        setGsReg(0x01, 0xFE00LL << 46);
        setGsReg(0x05, 0);
        setGsReg(0x05, (unsigned long long)(w << 4) | ((unsigned long long)(h << 4) << 16));
        setGsReg(0x47, 0x50000);
    }
    if (useoffset) {
        setGsReg(0x18,
                 (unsigned long long)(unsigned int)(((0x800 - (w >> 1)) << 4) + screenOffsetX) |
                     ((unsigned long long)(((0x800 - (h >> 1)) << 4) + screenOffsetY) << 32));
    } else {
        setGsReg(0x18, (unsigned long long)(unsigned int)((0x800 - (w >> 1)) << 4) |
                           ((unsigned long long)((0x800 - (h >> 1)) << 4) << 32));
    }
}

static int stripVisible[2] = {0, 0}; /* derived name */

static int stripIndex = 0; /* derived name */

/* the on-screen test of a projected vertex, which the strip and polygon
   functions and _IsInScreen further down this file inline */
static inline int isInScreen(volatile int *p) /* derived name */
{
    if (p[2] < 0)
        return 0;
    if (p[2] > 0x0FFFFFF0)
        return 0;
    if (p[0] < 0)
        return 0;
    if (p[0] > 0xFFF0)
        return 0;
    if (p[1] < 0)
        return 0;
    return p[1] <= 65520;
}

/* One vertex through the VU0 macro-mode pipeline: transform by the current
   matrix in vf4..vf7, perspective-divide by w and convert to the GS's 12.4
   fixed-point screen coordinates, stored to dst, in one asm statement. */
static inline void rotTransPers(volatile int *dst, void *src) /* derived name */
{
    /* the current matrix applied to src (all four fields), xyz times 1/w,
       xyz to 12.4; dst[3] is left alone (the PS2 stored vf11's stale w,
       which no caller reads) */
    float v[4];
    float q;

    ico_apply_matrix(v, (const float (*)[4])ico_current_matrix, src);
    q = ps2_div(1.0f, v[3]);
    dst[0] = ps2_ftoi4(v[0] * q);
    dst[1] = ps2_ftoi4(v[1] * q);
    dst[2] = ps2_ftoi4(v[2] * q);
}

/* One vertex through the VU0 pipeline into a caller-supplied projected-vertex
   slot, answering whether the result is on screen.  The first vertex, at
   frame offset 0, is written out here. */
static inline int projectVertex(int *d, void *src) /* derived name */
{
    rotTransPers(d, src);
    return isInScreen(d);
}

void gif_DrawPolyF4(void *p0, void *p1, void *p2, void *p3, int r, int g, int b, int a, int prim)
{
    GIF_ENTER(GIF_SP_WORLD);
    int q[4][4];
    int i;

    setGsReg(0x00, ((long long)prim << 6) | 0x104);
    setGsReg(0x01, (long long)r | ((long long)g << 8) | ((long long)b << 16) |
                       ((long long)a << 24) | (0xFE00LL << 46));
    rotTransPers(q[0], p0);
    if (!isInScreen(q[0]))
        return;
    if (!projectVertex(q[1], p1))
        return;
    if (!projectVertex(q[2], p2))
        return;
    if (!projectVertex(q[3], p3))
        return;
    for (i = 0; i < 4; i++) {
        int *s = q[i];

        setGsReg(0x05, GIF_XY0(s[0], s[1], (long long)s[2]));
    }
}

void gif_DrawStripF(void *v, GifColor col, int n, int prim)
{
    GIF_ENTER(GIF_SP_WORLD);
    char *p = v;
    int i;

    stripVisible[0] = stripVisible[1] = 0;
    setGsReg(0x00, ((long long)prim << 6) | 0x104);
    stripIndex = 0;
    setGsReg(0x01, GIF_COLOR(&col) | (0xFE00LL << 46));
    for (i = 0; i < n; i++, p += 16) {
        volatile int q[4];
        int t;

        rotTransPers(q, p);
        t = isInScreen(q);
        setGsReg(t && stripVisible[0] && stripVisible[1] ? 0x05 : 0x0D,
                 GIF_XY0(q[0], q[1], (long long)q[2]));
        stripVisible[stripIndex++] = t;
        stripIndex &= 1;
    }
}

void gif_DrawStripFST(void *v, void *uv, GifColor col, int n, int prim)
{
    GIF_ENTER(GIF_SP_WORLD);
    char *p = v;
    int *s = uv;
    int i;

    stripVisible[0] = stripVisible[1] = 0;
    setGsReg(0x00, ((long long)prim << 6) | 0x94);
    stripIndex = 0;
    setGsReg(0x01, GIF_COLOR(&col) | (0xFE00LL << 46));
    for (i = 0; i < n; i++, p += 16, s += 4) {
        volatile int q[4];
        int t;

        rotTransPers(q, p);
        t = isInScreen(q);
        setGsReg(0x02, (long long)s[0] | ((long long)s[1] << 32));
        setGsReg(t && stripVisible[0] && stripVisible[1] ? 0x05 : 0x0D,
                 GIF_XY0(q[0], q[1], (long long)q[2]));
        stripVisible[stripIndex++] = t;
        stripIndex &= 1;
    }
}

void gif_DrawStripG(void *v, void *col, int n, int prim)
{
    GIF_ENTER(GIF_SP_WORLD);
    char *p = v;
    unsigned char *c = col;
    int i;

    stripVisible[0] = stripVisible[1] = 0;
    setGsReg(0x00, ((long long)prim << 6) | 0x10C);
    stripIndex = 0;
    for (i = 0; i < n; i++, c += 4, p += 16) {
        volatile int q[4];
        int t;

        rotTransPers(q, p);
        t = isInScreen(q);
        setGsReg(0x01, GIF_RGBA(c) | (0xFE00LL << 46));
        if (t && stripVisible[0] && stripVisible[1]) {
            setGsReg(0x05, GIF_XY0(q[0], q[1], (long long)q[2]));
        } else {
            setGsReg(0x0D, GIF_XY0(q[0], q[1], (long long)q[2]));
        }
        stripVisible[stripIndex++] = t;
        stripIndex &= 1;
    }
}

void gif_Draw2DStripG(int *v, GifColor *col, int n, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    int i;

    stripVisible[0] = stripVisible[1] = 0;
    setGsReg(0x00, ((long long)prim << 6) | 0x10C);
    stripIndex = 0;
    for (i = 0; i < n; i++, col++, v += 4) {
        int c;

        c = isInScreen(v);
        setGsReg(0x01, GIF_COLOR(col) | (0xFE00LL << 46));
        if (c && stripVisible[0] && stripVisible[1]) {
            setGsReg(0x05, GIF_XY0(v[0], v[1], (long long)v[2]));
        } else {
            setGsReg(0x0D, GIF_XY0(v[0], v[1], (long long)v[2]));
        }
        stripVisible[stripIndex++] = c;
        stripIndex &= 1;
    }
}

void gif_Draw2DUVStripG(int *v, int *uv, unsigned char *col, int n, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    int i;

    stripVisible[0] = stripVisible[1] = 0;
    setGsReg(0x00, ((long long)prim << 6) | 0x11C);
    stripIndex = 0;
    for (i = 0; i < n; i++, col += 4, v += 4, uv += 4) {
        int c;

        c = isInScreen(v);
        setGsReg(0x01, GIF_RGBA(col) | (0xFE00LL << 46));
        setGsReg(0x03, GIF_UV(uv[0], uv[1]));
        if (c && stripVisible[0] && stripVisible[1]) {
            setGsReg(0x05, GIF_XY0(v[0], v[1], (long long)v[2]));
        } else {
            setGsReg(0x0D, GIF_XY0(v[0], v[1], (long long)v[2]));
        }
        stripVisible[stripIndex++] = c;
        stripIndex &= 1;
    }
}

void gif_Init(void)
{
    packetOpen = 0;
#ifdef ICO_RD
    gif_HostFrameReset();
#endif
}

/* DisplayList.h is not included: this file's uses of dl_OpenDma do not fit
 * its prototype there */

void gif_StartPacketPri(int pri)
{
    dl_SetDLPriority(pri);
    gif_StartPacket();
    packetOpen = 1;
#ifdef ICO_RD
    gsPacketStart();
#endif
}

void gif_StartPacketPriPath1(int pri)
{
    dl_SetDLPriority(pri);
    gif_StartPacketPath1();
    packetOpen = 1;
#ifdef ICO_RD
    gsPacketStart();
#endif
}

void gif_SetGsReg(long long reg, long long data)
{
    GIF_ENTER(GIF_SP_AUTO);
    setGsReg(reg, data);
}

int gif_CheckOpen(void)
{
    return packetOpen;
}

/* a point, which gif_MakePoint2D, gif_Point and the rest of the point
   family inline */
static inline void makePoint2D(int *v, long long z, unsigned char *col,
                               long long prim) /* derived name */
{
    setGsReg(0x00, (prim << 6) | 0x100);
    setGsReg(0x01, GIF_RGBA(col));
    setGsReg(0x05, GIF_XYZ(v, z));
}

void gif_MakePoint2D(int *v, long long z, unsigned char *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    makePoint2D(v, z, col, prim);
}

void gif_MakePoint2DOffset(int *v, long long z, unsigned char *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    makePoint2DOffset(v, z, col, prim);
}

void gif_MakeLine2D(int *v0, int *v1, long long z0, long long z1, unsigned char *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    makeLine2D(v0, v1, z0, z1, col, prim);
}

void gif_MakeSpriteNoTexture(int x, int y, int w, int h, long long z, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    makeSpriteNoTexture(x, y, w, h, z, col, prim);
}

void gif_MakeSpriteNoTextureOffset(int x, int y, int w, int h, long long z, GifColor *col, int prim)
{
    GIF_ENTER(GIF_SP_AUTO);
    makeSpriteNoTextureOffset(x, y, w, h, z, col, prim);
}

void gif_Point(int *v, long long z, unsigned char *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int p[2];

    p[0] = v[0] * ScreenWidth / 640;
    p[1] = v[1] * ScreenHeight / 224;
    makePoint2D(p, z, col, prim);
}

void gif_LineOffset(int *v0, int *v1, long long z0, long long z1, unsigned char *col, int prim)
{
    GIF_ENTER(GIF_SP_UI);
    int p0[4];
    int p1[4];

    p0[0] = v0[0] * ScreenWidth / 640 * 16;
    p0[1] = v0[1] * ScreenHeight / 224 * 16;
    p1[0] = v1[0] * ScreenWidth / 640 * 16;
    p1[1] = v1[1] * ScreenHeight / 224 * 16;
    gif_MakeLine2DOffset(p0, p1, z0, z1, col, prim);
}

/* the 12 ALPHA_1/2 blend-parameter quadruples gif_SetAlpha packs into the GS
   ALPHA register */
static const GsAlphaEnt alphaTable[12] = {
    /* derived name */
    {0, 2, 2, 1}, {2, 0, 2, 1}, {0, 1, 2, 1}, {1, 2, 2, 0}, {0, 1, 0, 1}, {0, 2, 0, 1},
    {2, 0, 0, 1}, {0, 1, 0, 1}, {0, 2, 1, 1}, {2, 0, 1, 1}, {0, 1, 1, 1}, {1, 2, 0, 1},
};

void gif_SetAlpha(long long alpha, long long mode, long long fix)
{
#ifdef ICO_RD
    /* the same two writes, PABE then ALPHA_1, through the decoder */
    GIF_ENTER(GIF_SP_AUTO);
    const GsAlphaEnt *e = &alphaTable[(unsigned long long)mode < 12 ? (int)mode : 0];

    if ((unsigned long long)mode >= 12) {
        fprintf(stderr, "gif: gif_SetAlpha mode %lld out of range: mode 0 used\n", mode);
    }
    gsWrite(0x49, alpha == 0);
    gsWrite(0x42, (unsigned long long)e->a | ((unsigned long long)e->b << 2) |
                      ((unsigned long long)e->c << 4) | ((unsigned long long)e->d << 6) |
                      ((unsigned long long)fix << 32));
#else
    unsigned long long *p, *q;
    unsigned long long v;
    int idx;

    idx = (int)mode;
    p = PacketBufferStruct.ptr.d;
    *(volatile unsigned long long *)p = (alpha == 0);
    p++;
    *(unsigned long long *volatile *)&PacketBufferStruct.ptr.d = p;
    *(volatile unsigned long long *)p = 0x49;
    *(unsigned long long *volatile *)&PacketBufferStruct.ptr.d = p + 1;
    mode = 0x42;
    v = (unsigned long long)alphaTable[idx].a | ((unsigned long long)fix << 32);
    v |=
        ((unsigned long long)alphaTable[idx].c << 4) | ((unsigned long long)alphaTable[idx].b << 2);
    v |= (unsigned long long)alphaTable[idx].d << 6;
    *(volatile unsigned long long *)(p + 1) = v;
    *(unsigned long long *volatile *)&PacketBufferStruct.ptr.d = p + 2;
    q = p + 3;
    *(volatile unsigned long long *)(p + 2) = mode;
    PacketBufferStruct.ptr.d = q;
#endif
}

void gif_MoveImage(long long sbp, long long sbw, long long psm, int *rect, long long dbp,
                   long long dbw, long long dsax, long long dsay)
{
    GIF_ENTER(GIF_SP_AUTO);
    setGsReg(0x50, (psm << 56) | (dbw << 48) | (dbp << 32) | (psm << 24) | (sbw << 16) | sbp);
    setGsReg(0x51, (dsay << 48) | (rect[1] << 16) | (dsax << 32) | rect[0]);
    setGsReg(0x52, ((long long)rect[3] << 32) | rect[2]);
    setGsReg(0x53, 2);
}

void gif_SetZTest(int on)
{
    GIF_ENTER(GIF_SP_AUTO);
    if (on)
        setGsReg(0x47, 0x50000);
    else
        setGsReg(0x47, 0x30000);
}

void gif_SetZWrite(int on)
{
    GIF_ENTER(GIF_SP_AUTO);
    if (on)
        setGsReg(0x4E, 0x300000C0);
    else
        setGsReg(0x4E, 0x1300000C0LL);
}

void gif_SetHalfOffset(void)
{
    GIF_ENTER(GIF_SP_AUTO);
    setGsReg(0x18, (long long)(((0x800 - ScreenWidth / 2) << 4) + screenOffsetX) |
                       ((long long)(((0x800 - ScreenHeight / 2) << 4) + screenOffsetY) << 32));
}

int _IsInScreen(volatile int *v)
{
    return isInScreen(v);
}

#ifdef ICO_RD

/* ===================================================================== *
 * PC port (renderer wave 2, R2a): the GS register decoder.
 *
 * Every A+D write the 2D layer makes, through the gif_* helpers above, raw
 * gif_SetGsReg calls, or the setGsReg macros Texture.c, GsBase.c and others
 * write straight into the open packet, arrives here in packet order and
 * becomes rd state and screen primitives in the list dl_SetDLPriority
 * selected:
 *
 *   PRIM            ABE, TME (texture on/off), IIP; resets the vertex queue;
 *                   AA1 on a line or triangle type (rd_AA1, package AA1),
 *                   returned to 0 when the packet ends
 *   RGBAQ ST UV     the current vertex attributes
 *   XYZ2 XYZF2      a vertex with a drawing kick; XYZ3 XYZF3 without one
 *                   (the queue still advances, as the strip helpers rely on)
 *   TEX0_1          the texture seam (GifHost.h), bound when PRIM.TME is on
 *   TEX1_1 CLAMP_1  rd_SamplerFilter, rd_SamplerWrap
 *   ALPHA_1 TEST_1 ZBUF_1 FBA_1 PABE TEXA COLCLAMP
 *                   rd_BlendFunc, rd_TestGs, rd_ZWrite, rd_FBA, rd_PABE,
 *                   rd_TexA, rd_ColClamp
 *   FRAME_1 XYOFFSET_1 SCISSOR_1
 *                   rd_SetTarget (FBP to a named target, size from XYOFFSET)
 *                   and rd_Scissor, emitted before the next primitive or at
 *                   the end of the packet; FRAME.FBMSK to rd_ColorMask
 *   TEXFLUSH        nothing to do
 *
 * Anything else is counted per register and logged once
 * (gif_HostUndecodedCount): the image transfers (BITBLTBUF, TRXPOS,
 * TRXREG, TRXDIR) belong to the texture package, MIPTBP and TEX2 to the
 * texture cache, FOG/FOGCOL to the fog package.
 *
 * Consecutive primitives of one kind, space and UV mode are batched into one
 * rd_ScreenPrims call; any state change ends the batch first, so rd sees the
 * writes in the order the GS would have.  Strips and fans are expanded to
 * triangle lists here, which also gives XYZ3 its GS meaning.
 * ===================================================================== */

#define GS_BATCH_MAX 1020 /* a multiple of 2 and 3 */
#define GS_PLACEHOLDERS 64

typedef struct GsShim { /* port */
    /* GS registers as last written, per list (PRIM, TEX0, FRAME, XYOFFSET,
       SCISSOR): the lists are recorded in any order but replayed 0..12, so a
       list that relies on a register it wrote earlier (Texture.c skips the
       TEX0 write when the list's last texture is the same,
       vramPri[].lastTex) means the value its own list wrote, not the one
       recorded last.  RGBAQ, ST and UV are written before every vertex. */
    unsigned long long primL[RD_LIST_COUNT], tex0L[RD_LIST_COUNT];
    unsigned long long frameL[RD_LIST_COUNT], xyoffsetL[RD_LIST_COUNT], scissorL[RD_LIST_COUNT];
    unsigned char rgba[4];
    float q, s, t;
    unsigned int u, v;
    unsigned char haveFrameL[RD_LIST_COUNT], haveXyoffsetL[RD_LIST_COUNT],
        haveScissorL[RD_LIST_COUNT], haveTex0L[RD_LIST_COUNT];
    unsigned long long tex0Last; /* the last TEX0 of any list: a list that never
                                    wrote one inherits it */
    int frameDirty, scissorDirty;
    unsigned int curW, curH;
    /* the vertex queue since the last PRIM write */
    RdScreenVtx queue[3];
    int qn;
    /* what this packet has emitted to rd (emValid 0: nothing known) */
    int emValid, emTme, emAbe, emIip;
    /* package AA1: rd's AA1 bit as this packet left it.  0 outside packets
       (gsAa1Off at every end), so a list recorded in any order replays with
       AA1 off wherever its own packet did not set it */
    int emAa1;
    unsigned long long emTex0;
    /* the open batch */
    RdScreenVtx batch[GS_BATCH_MAX];
    unsigned int nb;
    int bType, bSpace, bFixed;
    /* renderer R7d: the RdKey the batch's primitives carry
       (gif_HostDrawKey) */
    RdKey key;
    /* GIF_ENTER scopes */
    int depth, space;
    /* raw A+D bytes callers put in the open packet: decoded from here */
    char *rawFrom;
    /* diagnostics */
    unsigned int undecoded[256];
    unsigned int undecodedTotal;
    unsigned int onceBits;
    /* the texture seam */
    GifTex0Resolver resolver;

    struct {
        unsigned int tbp;
        RdTex tex;
    } ph[GS_PLACEHOLDERS];

    int phCount;

    /* wave 3 (R3ab): render-to-texture targets.  A FRAME.FBP that names no
       fixed buffer (puddle, pool and queen barrier blocks from
       tex_AllocVramAuto) draws into an rd_TempTarget of the size XYOFFSET
       gives, with its own depth, for the rest of the frame; a TEX0 whose
       TBP is that block's (FBP * 32) samples it */
    struct {
        unsigned int fbp, w, h;
        RdTarget t;
    } alias[8];

    int aliasCount;
} GsShim;

static GsShim gs = {.q = 1.0f};

static int gsList(void)
{
    int l = rd_CurrentList();

    return l >= 0 && l < RD_LIST_COUNT ? l : 0;
}

static unsigned long long gsTex0(void)
{
    int l = gsList();

    return gs.haveTex0L[l] ? gs.tex0L[l] : gs.tex0Last;
}

enum {
    GS_ONCE_FBP,
    GS_ONCE_ALPHA,
    GS_ONCE_TEXA,
    GS_ONCE_TFX,
    GS_ONCE_CTXT,
    GS_ONCE_FGE,
    GS_ONCE_PRMODE,
    GS_ONCE_CLAMP,
    GS_ONCE_PRIM7,
    GS_ONCE_DTHE
};

static void gsOnce(int bit, const char *msg, unsigned long long v)
{
    if (gs.onceBits & (1u << bit)) {
        return;
    }
    gs.onceBits |= 1u << bit;
    fprintf(stderr, "gif: %s (0x%llx; reported once)\n", msg, v);
}

static const char *gsRegName(unsigned int r)
{
    switch (r) {
    case 0x0A:
        return "FOG";
    case 0x16:
        return "TEX2_1";
    case 0x1B:
        return "PRMODE";
    case 0x1C:
        return "TEXCLUT";
    case 0x22:
        return "SCANMSK";
    case 0x34:
        return "MIPTBP1_1";
    case 0x36:
        return "MIPTBP2_1";
    case 0x3D:
        return "FOGCOL";
    case 0x44:
        return "DIMX";
    case 0x50:
        return "BITBLTBUF";
    case 0x51:
        return "TRXPOS";
    case 0x52:
        return "TRXREG";
    case 0x53:
        return "TRXDIR";
    case 0x54:
        return "HWREG";
    case 0x60:
        return "SIGNAL";
    case 0x61:
        return "FINISH";
    case 0x62:
        return "LABEL";
    default:
        return (r >= 0x07 && r <= 0x3F && (r & 1)) ? "a context-2 register" : "?";
    }
}

static void gsUndecoded(unsigned int r)
{
    r &= 0xFF;
    if (gs.undecoded[r]++ == 0) {
        fprintf(stderr,
                "gif: GS register 0x%02x (%s) written through the packet layer is not decoded "
                "(first in list %d; counted from here)\n",
                r, gsRegName(r), rd_CurrentList());
    }
    gs.undecodedTotal++;
}

unsigned int gif_HostUndecodedCount(int reg)
{
    return reg >= 0 && reg < 256 ? gs.undecoded[reg] : 0;
}

unsigned int gif_HostUndecodedTotal(void)
{
    return gs.undecodedTotal;
}

void gif_HostSetTex0Resolver(GifTex0Resolver fn)
{
    gs.resolver = fn;
}

/* ------------------------------------------------------------ the batch */

static void gsFlushBatch(void)
{
    if (gs.nb != 0) {
        rd_ScreenPrims((RdPrim)gs.bType, gs.batch, gs.nb, (RdSpace)gs.bSpace, gs.bFixed, gs.key);
        gs.nb = 0;
    }
}

/* ------------------------------------------------------- targets, scissor */

/* wave 3 (R3ab): the temporary target standing in for a VRAM block that is
   no fixed buffer, one per FBP and size and frame. */
static RdTarget gsAliasTarget(unsigned int fbp, unsigned int w, unsigned int h)
{
    int i;

    for (i = 0; i < gs.aliasCount; i++) {
        if (gs.alias[i].fbp == fbp && gs.alias[i].w == w && gs.alias[i].h == h) {
            return gs.alias[i].t;
        }
    }
    if (gs.aliasCount == (int)(sizeof(gs.alias) / sizeof(gs.alias[0]))) {
        return rd_Target(RD_TARGET_SCENE);
    }
    gs.alias[gs.aliasCount].fbp = fbp;
    gs.alias[gs.aliasCount].w = w;
    gs.alias[gs.aliasCount].h = h;
    gs.alias[gs.aliasCount].t = rd_TempTarget(w, h, 1, 0);
    return gs.alias[gs.aliasCount++].t;
}

static int gsIsNamedFbp(unsigned int fbp)
{
    return fbp == 0 || fbp == 0x40 || fbp == 0x140 || fbp == 0x160 || fbp == 0x180 || fbp == 0x1F8;
}

/* FRAME.FBP (2048-word pages) to the named target standing in for it. */
static RdTargetId gsTargetOfFbp(unsigned int fbp, unsigned int w, unsigned int h)
{
    switch (fbp) {
    case 0x000:
        return RD_TARGET_DISPLAY;
    case 0x040:
        return RD_TARGET_SCENE;
    case 0x140:
        return h <= 128 ? RD_TARGET_WORK0 : RD_TARGET_AA0; /* TBP 0x2800 */
    case 0x160:
        return w <= 128 ? RD_TARGET_AA1 : RD_TARGET_WORK1; /* TBP 0x2C00 */
    case 0x180:
        return RD_TARGET_WORK2; /* TBP 0x3000 */
    case 0x1F8:
        return RD_TARGET_FEED128; /* TBP 0x3F00 */
    default:
        gsOnce(GS_ONCE_FBP, "FRAME.FBP with no named target: drawn into SCENE", fbp);
        return RD_TARGET_SCENE;
    }
}

/* A pending FRAME/XYOFFSET/SCISSOR change into rd, before the next
   primitive or at the end of the packet. */
static void gsSyncEnv(void)
{
    int justSet = 0;

    if (!gs.frameDirty && !gs.scissorDirty) {
        return;
    }
    gsFlushBatch();
    if (gs.frameDirty) {
        unsigned int fbp = (unsigned int)(gs.frameL[gsList()] & 0x1FF);
        unsigned int w = (unsigned int)((gs.frameL[gsList()] >> 16) & 0x3F) * 64;
        unsigned int h = 0;
        RdTargetId id;

        if (gs.haveXyoffsetL[gsList()]) {
            /* XYOFFSET = (2048 - w/2, 2048 - h/2) in 12.4, the field offset
               (screenOffsetX/Y, zero on the host) left out */
            int ox = (int)(gs.xyoffsetL[gsList()] & 0xFFFF) - screenOffsetX;
            int oy = (int)((gs.xyoffsetL[gsList()] >> 32) & 0xFFFF) - screenOffsetY;
            int xw = (0x800 - (ox >> 4)) * 2;
            int xh = (0x800 - (oy >> 4)) * 2;

            if (xw > 0 && xw <= 2048) {
                w = (unsigned int)xw;
            }
            if (xh > 0 && xh <= 2048) {
                h = (unsigned int)xh;
            }
        }
        if (w == 0) {
            w = 512;
        }
        if (h == 0) {
            h = w;
        }
        if (!gsIsNamedFbp(fbp)) {
            /* wave 3 (R3ab): a render-to-texture block */
            RdTarget t = gsAliasTarget(fbp, w, h);

            rd_SetTarget(t, t, w, h, 0);
        } else {
            id = gsTargetOfFbp(fbp, w, h);
            rd_SetTarget(rd_Target(id), id == RD_TARGET_SCENE ? rd_Target(id) : (RdTarget){0}, w, h,
                         id == RD_TARGET_SCENE);
        }
        gs.curW = w;
        gs.curH = h;
        gs.frameDirty = 0;
        gs.scissorDirty = gs.haveScissorL[gsList()]; /* rd_SetTarget reset it: re-apply */
        justSet = 1;
    }
    if (gs.scissorDirty) {
        int x0 = (int)(gs.scissorL[gsList()] & 0x7FF),
            x1 = (int)((gs.scissorL[gsList()] >> 16) & 0x7FF);
        int y0 = (int)((gs.scissorL[gsList()] >> 32) & 0x7FF),
            y1 = (int)((gs.scissorL[gsList()] >> 48) & 0x7FF);

        if (!justSet || !(x0 == 0 && y0 == 0 && x1 == (int)gs.curW - 1 && y1 == (int)gs.curH - 1)) {
            rd_Scissor(x0, y0, x1, y1);
        }
        gs.scissorDirty = 0;
    }
}

/* ------------------------------------------------------------- textures */

RdTex gif_HostPlaceholder(unsigned int tbp)
{
    unsigned char px[16 * 16 * 4];
    unsigned int h = tbp * 2654435761u;
    unsigned char c[3];
    int i, x, y, slot;

    for (i = 0; i < gs.phCount; i++) {
        if (gs.ph[i].tbp == tbp && gs.ph[i].tex.id != 0) {
            return gs.ph[i].tex;
        }
    }
    if (gs.phCount == GS_PLACEHOLDERS) {
        /* every slot taken: share one.  Destroying a slot's texture here
           would pull it from under the lists of this frame (and the frames
           in flight) that already bind it. */
        return gs.ph[h % GS_PLACEHOLDERS].tex;
    }
    /* a magenta-leaning hue per TBP, so distinct textures look distinct;
       4-texel cells alternate with transparent ones */
    c[0] = (unsigned char)(160 + ((h >> 8) & 0x5F));
    c[1] = (unsigned char)(32 + ((h >> 16) & 0x7F));
    c[2] = (unsigned char)(160 + ((h >> 24) & 0x5F));
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            unsigned char *p = &px[(y * 16 + x) * 4];
            int on = ((x >> 2) + (y >> 2)) & 1;

            p[0] = on ? c[0] : 0;
            p[1] = on ? c[1] : 0;
            p[2] = on ? c[2] : 0;
            p[3] = on ? 0x80 : 0x00;
        }
    }
    slot = gs.phCount++;
    gs.ph[slot].tbp = tbp;
    gs.ph[slot].tex = rd_CreateTexture(16, 16, px, RD_TEXA_80_80, "R2a placeholder");
    return gs.ph[slot].tex;
}

/* TEX0 to an rd texture: the resolver (R2b), then the named targets the
   post passes read, then the placeholder. */
static RdTex gsResolveTex0(unsigned long long tex0)
{
    unsigned int tbp = (unsigned int)(tex0 & 0x3FFF);
    unsigned int psm = (unsigned int)((tex0 >> 20) & 0x3F);
    unsigned int tw = (unsigned int)((tex0 >> 26) & 0xF);
    unsigned int th = (unsigned int)((tex0 >> 30) & 0xF);
    RdTargetId id;

    if (gs.resolver != 0) {
        RdTex t = gs.resolver(tex0, rd_CurrentList());

        if (t.id != 0) {
            return t;
        }
    }
    switch (tbp) {
    case 0x0000:
        id = RD_TARGET_DISPLAY;
        break;
    case 0x0800:
        id = RD_TARGET_SCENE;
        break;
    case 0x2800:
        id = th == 7 ? RD_TARGET_WORK0 : RD_TARGET_AA0;
        break;
    case 0x2840:
        id = RD_TARGET_SHADOW0;
        break;
    case 0x2C00:
        id = tw == 7 ? RD_TARGET_AA1 : RD_TARGET_WORK1;
        break;
    case 0x3000:
        id = RD_TARGET_WORK2;
        break;
    case 0x3F00:
        id = RD_TARGET_FEED128;
        break;
    default: {
        int i;

        /* wave 3 (R3ab): a render-to-texture block drawn this frame */
        for (i = 0; i < gs.aliasCount; i++) {
            if (gs.alias[i].fbp * 32 == tbp) {
                return rd_TargetTexture(gs.alias[i].t, RD_VIEW_RGBA);
            }
        }
        return gif_HostPlaceholder(tbp);
    }
    }
    return rd_TargetTexture(rd_Target(id), psm == 1 ? RD_VIEW_RGB24_TA0 : RD_VIEW_RGBA);
}

static void gsBindTexture(void)
{
    unsigned int tfx = (unsigned int)((gsTex0() >> 35) & 3);

    if (tfx > 1) {
        gsOnce(GS_ONCE_TFX, "TEX0.TFX HIGHLIGHT is not supported: MODULATE used", gsTex0());
        tfx = 0;
    }
    rd_Texture(gsResolveTex0(gsTex0()), tfx ? RD_TEXFN_DECAL : RD_TEXFN_MODULATE,
               ((gsTex0() >> 34) & 1) ? RD_TCC_RGBA : RD_TCC_RGB);
    gs.emTex0 = gsTex0();
}

/* --------------------------------------------------------------- PRIM */

/* package AA1: rd's AA1 bit back to 0 (the batch already flushed) */
static void gsAa1Off(void)
{
    if (gs.emAa1) {
        rd_AA1(0);
        gs.emAa1 = 0;
    }
}

static void gsApplyPrim(void)
{
    int tme = (int)((gs.primL[gsList()] >> 4) & 1);
    int abe = (int)((gs.primL[gsList()] >> 6) & 1);
    int iip = (int)((gs.primL[gsList()] >> 3) & 1);
    /* package AA1: PRIM.AA1 (bit 7) acts on lines and triangles (types 1 to
       5); points and sprites (the particles' 0xD6) draw as without it, so
       rd is told only where it matters */
    int type = (int)(gs.primL[gsList()] & 7);
    int aa1 = (int)((gs.primL[gsList()] >> 7) & 1) && type >= 1 && type <= 5;

    if ((gs.primL[gsList()] & 7) == 7) {
        gsOnce(GS_ONCE_PRIM7, "PRIM type 7 (reserved): ignored", gs.primL[gsList()]);
    }
    if (gs.primL[gsList()] & 0x200) {
        gsOnce(GS_ONCE_CTXT, "PRIM.CTXT 1 (context 2) is not decoded: context 1 used",
               gs.primL[gsList()]);
    }
    if (gs.primL[gsList()] & 0x20) {
        gsOnce(GS_ONCE_FGE, "PRIM.FGE (fog) is not decoded yet: no fog", gs.primL[gsList()]);
    }
    if (!gs.emValid || tme != gs.emTme || (tme && gs.emTex0 != gsTex0())) {
        gsFlushBatch();
        if (tme) {
            gsBindTexture();
        } else {
            rd_TextureOff();
        }
        gs.emTme = tme;
    }
    if (!gs.emValid || abe != gs.emAbe) {
        gsFlushBatch();
        rd_ABE(abe);
        gs.emAbe = abe;
    }
    if (!gs.emValid || iip != gs.emIip) {
        gsFlushBatch();
        rd_Gouraud(iip);
        gs.emIip = iip;
    }
    if (aa1 != gs.emAa1) {
        gsFlushBatch();
        rd_AA1(aa1);
        gs.emAa1 = aa1;
    }
    gs.emValid = 1;
}

/* ------------------------------------------------------------- vertices */

static int gsSpace(void)
{
    switch (gs.space) {
    case GIF_SP_UI:
        return RD_SPACE_UI;
    case GIF_SP_WORLD:
        return RD_SPACE_WORLD;
    default:
        /* raw writes and the raw-coordinate helpers: UI in list 12, WORLD
           elsewhere.  PC port (renderer wave 7, R7c): list 11 was UI too
           until R7c, but what draws raw there is world-projected
           (waterDot.c's drops, the insect net, the debug lines and
           volumes); list 12's raw writers are 2D (debug.c's font and bars,
           icoMisc.c's memory bar).  The layout, subtitles, staff roll and
           font come through the UI helpers above and gif_HostScreenPrims.
           UI and WORLD replay alike except under the mirror mode, which
           flips UI prims (RENDER_API.md "Mirror mode") */
        return rd_CurrentList() >= 12 ? RD_SPACE_UI : RD_SPACE_WORLD;
    }
}

static void gsEmit(RdPrim type, const RdScreenVtx *v, int n)
{
    int space;
    int fixed = (int)((gs.primL[gsList()] >> 8) & 1);

    gsSyncEnv();
    space = gsSpace();
    if (gs.nb != 0 && (gs.bType != (int)type || gs.bSpace != space || gs.bFixed != fixed ||
                       gs.nb + (unsigned int)n > GS_BATCH_MAX)) {
        gsFlushBatch();
    }
    gs.bType = (int)type;
    gs.bSpace = space;
    gs.bFixed = fixed;
    memcpy(&gs.batch[gs.nb], v, (size_t)n * sizeof(*v));
    gs.nb += (unsigned int)n;
}

static void gsVertex(unsigned long long data, int fog, int kick)
{
    RdScreenVtx v;
    RdScreenVtx *q = gs.queue;

    if (!gs.emValid || (((gs.primL[gsList()] >> 4) & 1) && gs.emTex0 != gsTex0())) {
        /* first vertex of the packet: PRIM's state as the GS holds it */
        gsApplyPrim();
    }
    v.x = (int32_t)(data & 0xFFFF);
    v.y = (int32_t)((data >> 16) & 0xFFFF);
    v.z = fog ? (uint32_t)((data >> 32) & 0xFFFFFF) : (uint32_t)(data >> 32);
    if (gs.primL[gsList()] & 0x100) { /* FST: UV in 12.4 texels */
        v.s = (float)gs.u;
        v.t = (float)gs.v;
        v.q = 1.0f;
    } else {
        v.s = gs.s;
        v.t = gs.t;
        v.q = gs.q;
    }
    memcpy(v.rgba, gs.rgba, 4);
    switch (gs.primL[gsList()] & 7) {
    case 0: /* points */
        if (kick) {
            gsEmit(RD_PRIM_POINTS, &v, 1);
        }
        break;
    case 1: /* lines */
        q[gs.qn++] = v;
        if (gs.qn == 2) {
            if (kick) {
                gsEmit(RD_PRIM_LINES, q, 2);
            }
            gs.qn = 0;
        }
        break;
    case 2: /* line strip */
        q[gs.qn++] = v;
        if (gs.qn == 2) {
            if (kick) {
                gsEmit(RD_PRIM_LINES, q, 2);
            }
            q[0] = q[1];
            gs.qn = 1;
        }
        break;
    case 3: /* triangles */
        q[gs.qn++] = v;
        if (gs.qn == 3) {
            if (kick) {
                gsEmit(RD_PRIM_TRIANGLES, q, 3);
            }
            gs.qn = 0;
        }
        break;
    case 4: /* triangle strip */
        q[gs.qn++] = v;
        if (gs.qn == 3) {
            if (kick) {
                gsEmit(RD_PRIM_TRIANGLES, q, 3);
            }
            q[0] = q[1];
            q[1] = q[2];
            gs.qn = 2;
        }
        break;
    case 5: /* triangle fan */
        q[gs.qn++] = v;
        if (gs.qn == 3) {
            if (kick) {
                gsEmit(RD_PRIM_TRIANGLES, q, 3);
            }
            q[1] = q[2];
            gs.qn = 2;
        }
        break;
    case 6: /* sprites */
        q[gs.qn++] = v;
        if (gs.qn == 2) {
            if (kick) {
                gsEmit(RD_PRIM_SPRITES, q, 2);
            }
            gs.qn = 0;
        }
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- the rest */

static const unsigned char gsAlphaRegs[RD_BLEND_COUNT] = {
    0x68, 0x62, 0x64, 0x29, 0x44, 0x48, 0x42, 0x44, 0x58, 0x52, 0x54, 0x49,
}; /* mode 3: 0x29 (alphaTable {1, 2, 2, 0}), R5c; R2a had 0x61 */

static void gsAlpha(unsigned long long data)
{
    unsigned int reg = (unsigned int)(data & 0xFF);
    int i;

    for (i = 0; i < RD_BLEND_COUNT; i++) {
        if (gsAlphaRegs[i] == reg) {
            rd_BlendFunc((RdBlend)i, (uint8_t)((data >> 32) & 0xFF));
            return;
        }
    }
    gsOnce(GS_ONCE_ALPHA, "ALPHA_1 equation outside the twelve modes: ignored", data);
}

static void gsTexa(unsigned long long data)
{
    unsigned int ta0 = (unsigned int)(data & 0xFF);
    unsigned int aem = (unsigned int)((data >> 15) & 1);
    unsigned int ta1 = (unsigned int)((data >> 32) & 0xFF);

    if (ta0 == 0x80 && ta1 == 0x80) {
        rd_TexA(aem ? RD_TEXA_80_80_AEM : RD_TEXA_80_80);
    } else if (ta0 == 0x7F && ta1 == 0x81 && aem) {
        rd_TexA(RD_TEXA_7F_81_AEM);
    } else {
        gsOnce(GS_ONCE_TEXA, "TEXA outside the three modes: nearest mode used", data);
        rd_TexA(aem ? RD_TEXA_7F_81_AEM : RD_TEXA_80_80);
    }
}

static void gsWrite(unsigned long long reg, unsigned long long data)
{
    unsigned int r = (unsigned int)(reg & 0xFF);

    switch (r) {
    case 0x00: /* PRIM */
        gs.primL[gsList()] = data;
        gs.qn = 0;
        gsApplyPrim();
        return;
    case 0x01: /* RGBAQ */
    {
        unsigned int qb = (unsigned int)(data >> 32);

        gs.rgba[0] = (unsigned char)data;
        gs.rgba[1] = (unsigned char)(data >> 8);
        gs.rgba[2] = (unsigned char)(data >> 16);
        gs.rgba[3] = (unsigned char)(data >> 24);
        memcpy(&gs.q, &qb, 4);
        return;
    }
    case 0x02: /* ST */
    {
        unsigned int sb = (unsigned int)data, tb = (unsigned int)(data >> 32);

        memcpy(&gs.s, &sb, 4);
        memcpy(&gs.t, &tb, 4);
        return;
    }
    case 0x03: /* UV */
        gs.u = (unsigned int)(data & 0x3FFF);
        gs.v = (unsigned int)((data >> 16) & 0x3FFF);
        return;
    case 0x04: /* XYZF2 */
        gsVertex(data, 1, 1);
        return;
    case 0x05: /* XYZ2 */
        gsVertex(data, 0, 1);
        return;
    case 0x0C: /* XYZF3 */
        gsVertex(data, 1, 0);
        return;
    case 0x0D: /* XYZ3 */
        gsVertex(data, 0, 0);
        return;
    case 0x3F: /* TEXFLUSH */
        return;
    case 0x06: /* TEX0_1 */
        gs.tex0L[gsList()] = data;
        gs.haveTex0L[gsList()] = 1;
        gs.tex0Last = data;
        if (gs.emValid && gs.emTme) {
            gsFlushBatch();
            gsBindTexture();
        }
        return;
    default:
        break;
    }
    /* state: ends the batch so rd sees it between the right primitives */
    gsFlushBatch();
    switch (r) {
    case 0x08: /* CLAMP_1 */
    {
        RdSamplerWrap w = rd_WrapFromGs(data);

        if ((data & 2) || (data & 8)) {
            gsOnce(GS_ONCE_CLAMP, "CLAMP region modes are not decoded: CLAMP used", data);
        }
        rd_SamplerWrap((RdWrap)w.s, (RdWrap)w.t);
        return;
    }
    case 0x14: /* TEX1_1 */
    {
        unsigned int mmin = (unsigned int)((data >> 6) & 7);

        rd_SamplerFilter((data >> 5) & 1 ? RD_FILTER_LINEAR : RD_FILTER_NEAREST,
                         mmin == 1 || mmin == 4 || mmin == 5 ? RD_FILTER_LINEAR
                                                             : RD_FILTER_NEAREST);
        return;
    }
    case 0x18: /* XYOFFSET_1 */
        gs.xyoffsetL[gsList()] = data;
        gs.haveXyoffsetL[gsList()] = 1;
        gs.frameDirty = gs.haveFrameL[gsList()];
        return;
    case 0x1A: /* PRMODECONT */
        if ((data & 1) == 0) {
            gsOnce(GS_ONCE_PRMODE, "PRMODECONT 0 (attributes from PRMODE) is not decoded", data);
        }
        return;
    case 0x3B: /* TEXA */
        gsTexa(data);
        return;
    case 0x40: /* SCISSOR_1 */
        gs.scissorL[gsList()] = data;
        gs.haveScissorL[gsList()] = 1;
        gs.scissorDirty = 1;
        return;
    case 0x42: /* ALPHA_1 */
        gsAlpha(data);
        return;
    case 0x45: /* DTHE */
        if (data & 1) {
            gsOnce(GS_ONCE_DTHE, "DTHE 1 (dithering) is not reproduced", data);
        }
        return;
    case 0x46: /* COLCLAMP */
        rd_ColClamp((int)(data & 1));
        return;
    case 0x47: /* TEST_1 */
        rd_TestGs(data);
        return;
    case 0x49: /* PABE */
        rd_PABE((int)(data & 1));
        return;
    case 0x4A: /* FBA_1 */
        rd_FBA((int)(data & 1));
        return;
    case 0x4C: /* FRAME_1 */
        gs.frameL[gsList()] = data;
        gs.haveFrameL[gsList()] = 1;
        gs.frameDirty = 1;
        rd_ColorMask((uint32_t)(data >> 32));
        return;
    case 0x4E: /* ZBUF_1 */
        rd_ZWrite(((data >> 32) & 1) == 0);
        return;
    default:
        gsUndecoded(r);
        return;
    }
}

/* ----------------------------------------------------- raw packet bytes */

/* The A+D pairs callers wrote straight into the open packet since the last
   entry point, decoded in order. */
static void gsRawFlush(void)
{
    char *p = gs.rawFrom;
    char *e = PacketBufferStruct.ptr.c;

    if (p == 0) {
        return;
    }
    if (e < p || e - p > 0x80000) {
        gs.rawFrom = e; /* the bank was swapped or the cursor reset */
        return;
    }
    while (e - p >= 16) {
        unsigned long long d, r;

        memcpy(&d, p, 8);
        memcpy(&r, p + 8, 8);
        gsWrite(r, d);
        p += 16;
    }
    gs.rawFrom = p;
}

static int gifEnter(int space)
{
    if (gs.depth++ == 0) {
        gsRawFlush();
        gs.space = space;
    }
    return 0;
}

static void gifLeave(int *scope)
{
    (void)scope;
    if (--gs.depth == 0) {
        gs.space = GIF_SP_AUTO;
    }
}

static void gsPacketStart(void)
{
    gs.emValid = 0;
    gs.qn = 0;
    gs.rawFrom = PacketBufferStruct.ptr.c;
}

static void gsPacketEnd(void)
{
    gsRawFlush();
    gsSyncEnv();
    gsFlushBatch();
    gsAa1Off();
    gs.emValid = 0;
    gs.rawFrom = 0;
}

void gif_HostFlush(void)
{
    gsRawFlush();
    gsSyncEnv();
    gsFlushBatch();
    gsAa1Off();
    gs.emValid = 0;
}

void gif_HostScreenPrims(RdPrim type, const RdScreenVtx *v, unsigned int n, RdSpace space,
                         int uvFixed)
{
    gsRawFlush();
    gsSyncEnv();
    gsFlushBatch();
    rd_ScreenPrims(type, v, n, space, uvFixed, gs.key);
}

void gif_HostDrawKey(const void *obj, int part, int ordinal)
{
    RdKey k = RD_KEY(obj, part, ordinal);

    if (k == gs.key) {
        return;
    }
    gsRawFlush();
    gsFlushBatch();
    gs.key = k;
}

void gif_HostDrawKeyText(const char *s, int part)
{
    unsigned long long h = 0xCBF29CE484222325ull; /* FNV-1a, 64 bits */

    while (s != 0 && *s != 0) {
        h = (h ^ (unsigned char)*s++) * 0x100000001B3ull;
    }
    /* RD_KEY shifts the object left by 16: the hash's high bits are lost,
       its low 48 kept */
    gif_HostDrawKey((const void *)(uintptr_t)(h ^ (h >> 48)), part, 0);
}

void gif_HostForgetTextures(void)
{
    gs.phCount = 0;
    memset(gs.ph, 0, sizeof(gs.ph));
}

/* wave 3 (R3ab): A+D writes that reach the GS through the VU (SET_GSREGISTER,
   the PRIM of a mesh batch's GIF tag), decoded in order with what the open
   packet holds; whatever the decoder still batches goes to rd first. */
void gif_HostWriteRegs(const unsigned long long *ad, unsigned int n)
{
    unsigned int i;

    gsRawFlush();
    for (i = 0; i < n; i++) {
        gsWrite(ad[2 * i + 1], ad[2 * i]);
    }
    gsSyncEnv();
    gsFlushBatch();
    gsAa1Off();
}

void gif_HostFrameReset(void)
{
    gs.aliasCount = 0; /* wave 3: the temporary targets die with the frame */
    gs.nb = 0;
    gs.qn = 0;
    gs.emValid = 0;
    gs.emAa1 = 0;
    gs.rawFrom = 0;
    gs.depth = 0;
    gs.space = GIF_SP_AUTO;
    gs.key = 0;
}

#endif /* ICO_RD */
