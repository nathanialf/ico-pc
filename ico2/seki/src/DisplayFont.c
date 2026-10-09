#include "DisplayFont.h"
#include "debug.h"
#include <string.h>
#include "GsBase.h"
#include "Matrix.h"
#include "GifPacket.h"
#include "Texture.h"

#ifdef ICO_RD

#include <string.h>
#include "GifHost.h"

#endif

/* each character's first and last column in its 20-pixel cell of the "font"
   texture, {0, 0} where the font has no glyph */
static signed char fontKerning[128][2] = {
    /* derived name */
    {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},
    {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},
    {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},  {0, 0},
    {0, 0},  {0, 0},  {0, 0},  {8, 11}, {7, 12}, {3, 16}, {4, 15}, {2, 17}, {3, 16}, {7, 12},
    {6, 12}, {7, 13}, {4, 15}, {3, 16}, {8, 11}, {7, 12}, {8, 11}, {5, 14}, {4, 14}, {7, 12},
    {5, 14}, {5, 14}, {5, 15}, {5, 14}, {5, 14}, {5, 14}, {4, 14}, {5, 14}, {8, 11}, {8, 11},
    {5, 14}, {6, 13}, {5, 14}, {5, 14}, {1, 18}, {3, 16}, {4, 15}, {4, 16}, {3, 15}, {4, 14},
    {4, 14}, {3, 15}, {4, 15}, {8, 11}, {6, 13}, {4, 16}, {5, 15}, {3, 16}, {4, 15}, {3, 16},
    {4, 14}, {3, 16}, {4, 15}, {4, 15}, {3, 16}, {4, 15}, {3, 16}, {2, 17}, {3, 16}, {3, 16},
    {4, 15}, {7, 13}, {3, 16}, {6, 12}, {5, 13}, {4, 14}, {0, 0},  {5, 15}, {4, 14}, {5, 14},
    {5, 15}, {5, 14}, {6, 13}, {4, 15}, {5, 14}, {8, 11}, {6, 12}, {5, 14}, {8, 11}, {2, 17},
    {5, 14}, {5, 14}, {5, 14}, {5, 14}, {6, 13}, {6, 14}, {6, 13}, {5, 14}, {4, 14}, {2, 17},
    {4, 15}, {4, 14}, {5, 14}, {6, 12}, {8, 11}, {7, 13}, {5, 15}, {2, 16},
};

/* The .sdata run: the alignment a "{L}", "{R}" or "{C}" escape selects and the
   colour a "{#rrggbbaa}" escape sets, then the texture name at its use. */
static int fontAlign = 0; /* derived name */

static int fontColorR = 128; /* derived name */

static int fontColorG = 128; /* derived name */

static int fontColorB = 128; /* derived name */

static int fontColorA = 128; /* derived name */

inline int font_GetWidth(void)
{
    return 20;
}

inline int font_GetHeight(void)
{
    return (12800 / ScreenWidth) / 2;
}

inline void font_Init(void)
{
    fontAlign = 0;
    fontColorA = 128;
    fontColorB = 128;
    fontColorG = 128;
    fontColorR = 128;
}

/* A hex digit's value; inlined at all eight call sites. */
static inline int font_HexDigit(char c) /* derived name */
{
    int r = -1;

    if ((unsigned char)(c - '0') < 10)
        r = c - '0';
    else if ((unsigned)(c - 'A') < 6 || (unsigned)(c - 'a') < 6)
        r = (c >= 'a') ? (c - ('a' - 10)) : (c - ('A' - 10));
    return r;
}

int font_CheckAlign(SprCol *col, unsigned char *str)
{
    unsigned char buf[256];
    unsigned char *p;
    int n;
    int c;

    n = 0;
    p = str;
    while ((c = *p++) != 0) {
        if (c == '{') {
            n = 1;
        } else if (c == '}') {
            buf[n - 1] = 0;
            n = 0;
            switch (buf[0]) {
            case 'L':
                fontAlign = 1;
                break;
            case 'R':
                fontAlign = 2;
                break;
            case 'C':
                fontAlign = 0;
                break;
            case '#':
                if (strlen((const char *)buf) == 9) {
                    fontColorR = font_HexDigit(buf[1]) * 16 + font_HexDigit(buf[2]);
                    fontColorG = font_HexDigit(buf[3]) * 16 + font_HexDigit(buf[4]);
                    fontColorB = font_HexDigit(buf[5]) * 16 + font_HexDigit(buf[6]);
                    fontColorA = font_HexDigit(buf[7]) * 16 + font_HexDigit(buf[8]);
                }
                break;
            }
        } else if (n != 0) {
            buf[n - 1] = c;
            n++;
        }
    }
    col->f[0] = fontColorR;
    col->f[1] = fontColorG;
    col->f[2] = fontColorB;
    col->f[3] = fontColorA;
    return fontAlign;
}

#ifdef ICO_RD
/* PC port (R2a; the names here are the port's): the glyph sprites of one font_Print, collected and drawn
   with one gif_HostScreenPrims (rd_screen_prims) call.  The corners are the
   XYZ2 words drawOne packs, unpacked the way the GS reads them. */
#define FONT_MAX_SPRITES 256 /* port */

static RdScreenVtx fontVerts[FONT_MAX_SPRITES * 2];

static int fontVertCount;

static unsigned char fontRgba[4];

static void fontCorner(RdScreenVtx *o, long long xyz, int u, int v) /* port */
{
    o->x = (int32_t)(xyz & 0xFFFF);
    o->y = (int32_t)((xyz >> 16) & 0xFFFF);
    o->z = (uint32_t)((unsigned long long)xyz >> 32);
    o->s = (float)u;
    o->t = (float)v;
    o->q = 1.0f;
    memcpy(o->rgba, fontRgba, 4);
}

#endif

static inline float drawOne(float px, float py, int u, int v, int cw, int dp1,
                            int fontw) /* derived name */
{
    float fcw = (float)cw, fh = (float)(fontw * 640 / ScreenWidth);
    int gsofs = 0x8000;
    int uv[4] = {u * 16, v * 16, (u + dp1 + 1) * 16, (v + 20) * 16}, iv[4];
    float pos[4] = {px, py, fcw, fh};
    int ofs[4] = {-ScreenWidth / 2 * 16, -ScreenHeight / 4 * 16, 0, 0};

    _FTOI4Vector(iv, pos);
#ifdef ICO_RD
    if (fontVertCount + 2 <= FONT_MAX_SPRITES * 2) {
        long long xy0 = (ofs[0] + (iv[0] + gsofs)) | ((long long)(ofs[1] + (iv[1] + gsofs)) << 16) |
                        ((long long)-1 << 32);
        long long xy1 = (ofs[0] + (iv[0] + gsofs) + iv[2]) |
                        ((long long)(ofs[1] + (iv[1] + gsofs) + iv[3]) << 16) |
                        ((long long)-1 << 32);

        fontCorner(&fontVerts[fontVertCount++], xy0, uv[0], uv[1]);
        fontCorner(&fontVerts[fontVertCount++], xy1, uv[2], uv[3]);
    }
    return px + fcw + 1.0f;
#endif
    gif_SetGsReg(0, 0x156);
    gif_SetGsReg(3, uv[0] | ((long long)uv[1] << 16));
    /* clang-format off */
    gif_SetGsReg(5, (ofs[0] + (iv[0] + gsofs)) | ((long long)(ofs[1] + (iv[1] + gsofs)) << 16) | ((long long)-1 << 32));
    gif_SetGsReg(3, uv[2] | ((long long)uv[3] << 16));
    gif_SetGsReg(5, (ofs[0] + (iv[0] + gsofs) + iv[2]) | ((long long)(ofs[1] + (iv[1] + gsofs) + iv[3]) << 16) | ((long long)-1 << 32));
    /* clang-format on */
    return px + fcw + 1.0f;
}

static inline int measure(unsigned char *s, unsigned char *d) /* derived name */
{
    int brace = 0;
    int width = 0;
    int ch;

    while ((ch = *s++) != 0) {
        if (ch == '{') {
            brace = 1;
        } else if (ch == '}') {
            brace = 0;
        } else if (brace == 0) {
            int ca = fontKerning[ch][0];
            int cb = fontKerning[ch][1];
            int w = cb - ca;

            int cw = w + 1;

            if (cw < 7) {
                cw = w + 3;
            }
            if (ca == 0 && cb == 0) {
                width += 8;
            } else {
                width += 1 + cw;
            }
            *d++ = ch;
        }
    }
    *d++ = 0;
    return width;
}

void font_Print(unsigned int color, unsigned char *str, float x, float y, int align, SprCol col)
{
    unsigned char buf[256];
    unsigned char *p;
    int r;
    int g;
    int b;
    int a;
    float cx;
    float cy;
    float fw;

    texturetranssize += tex_TransTexture(tex_GetTextureNo("font"), 12);
    gif_StartPacketPriPath1(12);

    cy = cx = 0.0f;

    r = (color >> 24) * col.f[0] / 255;
    g = ((color >> 16) & 0xFF) * col.f[1] / 255;
    b = ((color >> 8) & 0xFF) * col.f[2] / 255;
    a = ((color & 0xFF) * col.f[3]) >> 7;
#ifdef ICO_RD
    /* R2a: the same state on rd: Z write on, TEST 0x30000, ALPHA 0x44,
       TEX1 0x60; the PRIM write (0x156: sprite, TME, ABE, FST) goes through
       the decoder once so it binds the "font" TEX0 tex_TransTexture wrote
       (the texture seam: a placeholder until Texture.c is on rd) */
    gif_HostFlush();
    rd_z_write(1);
    rd_test_gs(0x30000);
    rd_blend_func(RD_BLEND_LERP_AS, 0);
    rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    gif_SetGsReg(0, 0x156);
    fontRgba[0] = (unsigned char)r;
    fontRgba[1] = (unsigned char)g;
    fontRgba[2] = (unsigned char)b;
    fontRgba[3] = (unsigned char)a;
    fontVertCount = 0;
#else
    gif_SetGsReg(0x4E, 0x300000C0);
    gif_SetGsReg(0x47, 0x30000);
    gif_SetGsReg(0x42, 0x44);
    gif_SetGsReg(0x14, 0x60);
    gif_SetGsReg(1, r | ((long long)g << 8) | ((long long)b << 16) | ((long long)a << 24) |
                        ((long long)0xFE00 << 46));
#endif

    fw = (float)measure(str, buf);
    p = buf;

    switch (align) {
    case 0:
        cy = y;
        cx = x * (float)ScreenWidth / 640.0f + (float)(ScreenWidth / 2) - fw * 0.5f;
        break;
    case 1:
        cy = y;
        cx = x * (float)ScreenWidth / 640.0f + 4.0f;
        break;
    case 2:
        cy = y;
        cx = x * (float)ScreenWidth / 640.0f + (float)ScreenWidth - fw - 4.0f;
        break;
    }

    for (;;) {
        int c = *p++;
        int ca;
        int cb;
        int w;
        int cw;
        int dp1;
        int fontw;
        int u;
        int v;

        /* clang-format off */
        if (c == 0) break;
        /* clang-format on */
        if (c == ' ') {
            cx += 8.0f;
            continue;
        }
        ca = fontKerning[c][0];
        cb = fontKerning[c][1];
        u = (c - ' ') % 12 * 20 + ca;
        v = (c - ' ') / 12 * 20;
        w = cb - ca;
        cw = w + 1;
        fontw = font_GetWidth();
        dp1 = cw;
        if (cw < 7) {
            cx += 1.0f;
            cw = w + 2;
        }
        if (ca == 0 && cb == 0) {
            cx += 8.0f;
        } else {
            cx = drawOne(cx, cy, u, v, cw, dp1, fontw);
        }
    }

#ifdef ICO_RD
    if (fontVertCount != 0) {
        /* R7d: keyed by the string, so a line that moves or fades blends
           between two ticks glyph for glyph (a changed string is another
           key and snaps) */
        gif_HostDrawKeyText((const char *)str, 0xDF);
        gif_HostScreenPrims(RD_PRIM_SPRITES, fontVerts, (unsigned int)fontVertCount, RD_SPACE_UI,
                            1);
        gif_HostDrawKey(0, 0, 0);
    }
#endif
    gif_EndPacketPath1();
}
