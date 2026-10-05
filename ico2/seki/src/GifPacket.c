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

/* one A+D register write, which gif_SetGsReg and most of this file inline */
static inline void setGsReg(long long reg, long long data) /* derived name */
{
    *PacketBufferStruct.ptr.d++ = data;
    *PacketBufferStruct.ptr.d++ = reg;
}

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
    setGsReg(0x00, ((long long)prim << 6) | 0xA);
    setGsReg(0x01, GIF_RGBA(col));
    setGsReg(0x05, GIF_XYZOFF(v0, z0));
    setGsReg(0x05, GIF_XYZOFF(v1, z1));
}

void gif_MakeSprite(int x, int y, int w, int h, long long z, GifRect *uv, GifColor *col, int prim)
{
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
    setGsReg(0x00, (prim << 6) | 0x116);
    setGsReg(0x01, GIF_COLOR(col));
    setGsReg(0x03, GIF_UV(uv->x, uv->y));
    setGsReg(0x05, GIF_XY0(GIF_OX + x, GIF_OY + y, z));
    setGsReg(0x03, GIF_UV(uv->x + uv->w, uv->y + uv->h));
    setGsReg(0x05, GIF_XY0(GIF_OX + x + w, GIF_OY + y + h, z));
}

void gif_MakeSpriteWithStrip(int *r, long long z, GifRect *uv, GifColor *col, int prim)
{
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
#ifdef ICO_HOST
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
#else
    __asm__ __volatile__(".set noreorder\n\t"
                         "lqc2 $vf8, 0x0(%1)\n\t"
                         "vmulax.xyzw ACC, $vf4, $vf8x\n\t"
                         "vmadday.xyzw ACC, $vf5, $vf8y\n\t"
                         "vmaddaz.xyzw ACC, $vf6, $vf8z\n\t"
                         "vmaddw.xyzw $vf10, $vf7, $vf8w\n\t"
                         "vdiv Q, $vf0w, $vf10w\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyz $vf10, $vf10, Q\n\t"
                         "vftoi4.xyz $vf11, $vf10\n\t"
                         "sqc2 $vf11, 0x0(%0)\n\t"
                         ".set reorder"
                         :
                         : "r"(dst), "r"(src)
                         : "memory");
#endif
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
}

/* DisplayList.h is not included: this file's uses of dl_OpenDma do not fit
 * its prototype there */

void gif_StartPacketPri(int pri)
{
    dl_SetDLPriority(pri);
    gif_StartPacket();
    packetOpen = 1;
}

void gif_StartPacketPriPath1(int pri)
{
    dl_SetDLPriority(pri);
    gif_StartPacketPath1();
    packetOpen = 1;
}

void gif_SetGsReg(long long reg, long long data)
{
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
    makePoint2D(v, z, col, prim);
}

void gif_MakePoint2DOffset(int *v, long long z, unsigned char *col, int prim)
{
    makePoint2DOffset(v, z, col, prim);
}

void gif_MakeLine2D(int *v0, int *v1, long long z0, long long z1, unsigned char *col, int prim)
{
    makeLine2D(v0, v1, z0, z1, col, prim);
}

void gif_MakeSpriteNoTexture(int x, int y, int w, int h, long long z, GifColor *col, int prim)
{
    makeSpriteNoTexture(x, y, w, h, z, col, prim);
}

void gif_MakeSpriteNoTextureOffset(int x, int y, int w, int h, long long z, GifColor *col, int prim)
{
    makeSpriteNoTextureOffset(x, y, w, h, z, col, prim);
}

void gif_Point(int *v, long long z, unsigned char *col, int prim)
{
    int p[2];

    p[0] = v[0] * ScreenWidth / 640;
    p[1] = v[1] * ScreenHeight / 224;
    makePoint2D(p, z, col, prim);
}

void gif_LineOffset(int *v0, int *v1, long long z0, long long z1, unsigned char *col, int prim)
{
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
}

void gif_MoveImage(long long sbp, long long sbw, long long psm, int *rect, long long dbp,
                   long long dbw, long long dsax, long long dsay)
{
    setGsReg(0x50, (psm << 56) | (dbw << 48) | (dbp << 32) | (psm << 24) | (sbw << 16) | sbp);
    setGsReg(0x51, (dsay << 48) | (rect[1] << 16) | (dsax << 32) | rect[0]);
    setGsReg(0x52, ((long long)rect[3] << 32) | rect[2]);
    setGsReg(0x53, 2);
}

void gif_SetZTest(int on)
{
    if (on)
        setGsReg(0x47, 0x50000);
    else
        setGsReg(0x47, 0x30000);
}

void gif_SetZWrite(int on)
{
    if (on)
        setGsReg(0x4E, 0x300000C0);
    else
        setGsReg(0x4E, 0x1300000C0LL);
}

void gif_SetHalfOffset(void)
{
    setGsReg(0x18, (long long)(((0x800 - ScreenWidth / 2) << 4) + screenOffsetX) |
                       ((long long)(((0x800 - ScreenHeight / 2) << 4) + screenOffsetY) << 32));
}

int _IsInScreen(volatile int *v)
{
    return isInScreen(v);
}
