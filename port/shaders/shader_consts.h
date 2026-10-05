/* shader_consts.h: the C side of the byte layouts common.hlsli declares.
 * rd_core fills these; the static asserts pin each offset to the HLSL
 * cbuffer packing (every member is a 16-byte register or a 64-byte matrix,
 * so there is no packing ambiguity). Change this file and common.hlsli
 * together, and docs/port/SHADERS.md. */
#ifndef PORT_SHADERS_SHADER_CONSTS_H
#define PORT_SHADERS_SHADER_CONSTS_H

#include <stddef.h>
#include <stdint.h>

/* FrameCB: group 0, slot 0 (register b0, space0). */
typedef struct IcoFrameCB {
    float view[16]; /* column-major, element [column * 4 + row] */
    float proj[16];
    float viewProj[16];
    float cameraPos[4]; /* xyz eye, w = 1 on a camera cut tick */
    float clip[4];      /* near, far, zoom, aspect */
    float target[4];    /* w, h of the bound target in GS pixels, 1/w, 1/h */
    float origin[4];    /* xy GS window coordinate of the target's top-left, zw added after */
    float space[2][4];  /* [0] WORLD, [1] UI: ndc = ndc * xy + zw */
    float z[4];         /* x = 1 / 2^24 (PSMZ24 scale) */
    float misc[4];      /* x frame counter, y preset */
} IcoFrameCB;

/* DrawCB: group 1, slot 1 (register b1, space1). */
typedef struct IcoDrawCB {
    uint32_t col[4];   /* RGBA 0..255 */
    uint32_t mode[4];  /* flags, texa | texfmt << 8, atst | ate << 8 | split << 16, aref */
    uint32_t blend[4]; /* ALPHA register, FIX, COLCLAMP, 0 */
    float uvRect[4];   /* u0, v0, u1, v1 in texels */
    float tex[4];      /* w, h of t1 in texels, 1/w, 1/h */
    float param[4];    /* kind specific */
} IcoDrawCB;

/* DrawCB.mode[0] flags (DF_* in common.hlsli). */
enum {
    ICO_DF_TEXTURED = 1,
    ICO_DF_DECAL = 2,
    ICO_DF_TCC_RGBA = 4,
    ICO_DF_FBA = 8,
    ICO_DF_PABE = 16,
    ICO_DF_FIX_FACTOR = 32,
    ICO_DF_PREMUL = 64
};

/* DrawCB.mode[1] bits 8..: TEXFMT_* in gs_math.hlsli. */
enum { ICO_TEXFMT_RGBA32 = 0, ICO_TEXFMT_RGB24 = 1, ICO_TEXFMT_RGBA16 = 2 };

#define ICO_SPACE_WORLD 0
#define ICO_SPACE_UI 1

/* Vertex of sprite.hlsl and font.hlsl: 20 bytes.
 *   loc 0 RHI_VTX_U16x2_UINT, loc 1 RHI_VTX_U32x1, loc 2 RHI_VTX_U8x4_UINT,
 *   loc 3 RHI_VTX_F32x2 */
typedef struct IcoSpriteVertex {
    uint16_t x, y;   /* GS 12.4 window coordinates */
    uint32_t z;      /* GS Z */
    uint8_t rgba[4]; /* alpha 0x80 = 1.0 */
    float u, v;      /* texels of t1 */
} IcoSpriteVertex;

_Static_assert(offsetof(IcoFrameCB, view) == 0, "view");
_Static_assert(offsetof(IcoFrameCB, proj) == 64, "proj");
_Static_assert(offsetof(IcoFrameCB, viewProj) == 128, "viewProj");
_Static_assert(offsetof(IcoFrameCB, cameraPos) == 192, "cameraPos");
_Static_assert(offsetof(IcoFrameCB, clip) == 208, "clip");
_Static_assert(offsetof(IcoFrameCB, target) == 224, "target");
_Static_assert(offsetof(IcoFrameCB, origin) == 240, "origin");
_Static_assert(offsetof(IcoFrameCB, space) == 256, "space");
_Static_assert(offsetof(IcoFrameCB, z) == 288, "z");
_Static_assert(offsetof(IcoFrameCB, misc) == 304, "misc");
_Static_assert(sizeof(IcoFrameCB) == 320, "FrameCB size");
_Static_assert(offsetof(IcoDrawCB, mode) == 16, "mode");
_Static_assert(offsetof(IcoDrawCB, blend) == 32, "blend");
_Static_assert(offsetof(IcoDrawCB, uvRect) == 48, "uvRect");
_Static_assert(offsetof(IcoDrawCB, tex) == 64, "tex");
_Static_assert(offsetof(IcoDrawCB, param) == 80, "param");
_Static_assert(sizeof(IcoDrawCB) == 96, "DrawCB size");
_Static_assert(offsetof(IcoSpriteVertex, z) == 4, "vertex z");
_Static_assert(offsetof(IcoSpriteVertex, rgba) == 8, "vertex rgba");
_Static_assert(offsetof(IcoSpriteVertex, u) == 12, "vertex uv");
_Static_assert(sizeof(IcoSpriteVertex) == 20, "vertex size");

#endif
