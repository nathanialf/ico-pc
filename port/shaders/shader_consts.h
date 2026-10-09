/* shader_consts.h: the C side of the byte layouts common.hlsli declares.
 * rd_core fills these; the static asserts pin each offset to the HLSL
 * cbuffer packing (every member is a 16-byte register or a 64-byte matrix,
 * so there is no packing ambiguity). Change this file and common.hlsli
 * together; a constant mirrored from an .hlsli (ICO_SHEET_*: sheet_text.hlsli)
 * holds the same value as its HLSL twin. */
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
    float z[4];         /* x = 1 / 2^24 (PSMZ24 scale); R7a: yz the bound target's
                         * texels per GS pixel (1 in Original); S2: w = 1, the VU
                         * programs output unquantised X, Y (only the developer
                         * switch ICO_RD_VU_OFFGRID; Enhanced, a scaled target) */
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
    float scale[4];    /* R7a: xy t1 texels per GS texel (1, or a scaled target's
                        * scale); zw a widened block's x addressing, u' = u z + w
                        * (0 0: none; common.hlsli gs_block_uv) */
} IcoDrawCB;

/* Package CRT: crt.hlsl's CrtCB, group 1, slot 1 (register b1, space1),
 * bound in DrawCB's place through the draw layout, whose dynamic group is
 * cached per block size (rd_replay.c dynamicGroup), so the two sizes need
 * not match (port/render/rd_crt.c).
 * All in pixels of the box or of the virtual source (the PS2 picture's
 * grid, 512 wide at 4:3) unless said. */
typedef struct IcoCrtCB {
    float src[4];  /* the virtual source: w, h, 1 / w, 1 / h */
    float box[4];  /* the box: w, h, x, y in output pixels */
    float beam[4]; /* scanline strength, beam width min, max (lines, FWHM), gap columns a
                    * source pixel */
    float mask[4]; /* type (RdCrtMask: 0 none, 1 grille, 2 slot, 3 dots), strength (1 - the
                    * leak), fade (rd__CrtMaskFade), halation */
    float glow[4]; /* bloom, curvature x, curvature y, corner radius (of the box height) */
    float tone[4]; /* vignette, gamma in, gamma out, strength */
    float pass[4]; /* x mirror, y unused (the slot's row gain is per pixel: crt.hlsl
                    * rowGainOf), zw 1 / the blurred target's size */
} IcoCrtCB;

/* DrawCB.mode[0] flags (DF_* in common.hlsli). */
enum {
    ICO_DF_TEXTURED = 1,
    ICO_DF_DECAL = 2,
    ICO_DF_TCC_RGBA = 4,
    ICO_DF_FBA = 8,
    ICO_DF_PABE = 16,
    ICO_DF_FIX_FACTOR = 32,
    ICO_DF_PREMUL = 64,
    ICO_DF_DATE = 128,     /* destination alpha test against t2 (the DATE snapshot) */
    ICO_DF_DATM = 256,     /* with DF_DATE: pass where the MSB is 1 (else where it is 0) */
    ICO_DF_AA1_FULL = 512, /* sprite_aa1_ps: PRIM.ABE 0, the coverage alpha replaces every alpha */
    /* package TEXA: the sampler state sprite_texa_ps and vu_texa_ps filter
     * by (gs_texa_texture in common.hlsli); set only for those entries */
    ICO_DF_TEXA_MAG_LINEAR = 1024,
    ICO_DF_TEXA_MIN_LINEAR = 2048,
    ICO_DF_TEXA_CLAMP_S = 4096,
    ICO_DF_TEXA_CLAMP_T = 8192,
    ICO_DF_TEXA_MIN_SAMPLED = 16384, /* minified pixels: the bound (Enhanced, mipmapped) sampler */
    /* the pipeline blends Cs + Cd * c1 (src ONE, dst SRC1: Cd*FIX + Cs): a
     * PABE pixel left unblended outputs c1 = 0 */
    ICO_DF_C1_DST = 32768,
    /* package AN-E, the two-pass blend without dual-source blending (the
     * *_nodual entries; rd_pipeline.c rd__ExpandNoDual): the colour pass
     * writes the blend factor into c0.a, the alpha pass the stored alpha */
    ICO_DF_NODUAL_FACTOR = 65536,
    ICO_DF_NODUAL_ALPHA_PASS = 131072
};

/* DrawCB.mode[1] bits 8..: TEXFMT_* in gs_math.hlsli. */
enum { ICO_TEXFMT_RGBA32 = 0, ICO_TEXFMT_RGB24 = 1, ICO_TEXFMT_RGBA16 = 2 };

#define ICO_SPACE_WORLD 0
#define ICO_SPACE_UI 1

/* v0.4.2 (package F-A): font_sheet_ps's constants, SHEET_* in
 * sheet_text.hlsli (the same values; port/render/test/sheet_ref.c
 * static-asserts each pair).  rd.h rd_CreateTextureSheet says what they do.
 * The style reaches the shader in DrawCB.param: the rim's weight (0 off,
 * 64 full), rimLevel, fillLevel, dither (rd_replay.c, from RdTexRec.sheet). */
/* The values are the sheets' survey (package F-C1, the comment above
 * kSheetInk in port/ui/menu_font.c; ctest menu_look testSurvey).  The rim is
 * a weighted dilation: the largest coverage within ICO_SHEET_RX texels across
 * and ICO_SHEET_RY down, each scaled by WX[|dx|] * WY[|dy|] (per mille), so
 * the dark halo fades out as the sheets' does instead of filling the gaps
 * between letters.  The falloff is the English sheets' mean rim alpha at
 * each distance outside the letters (across 1..6, down 1..4). */
#define ICO_SHEET_RX 6 /* rim: texels across */
#define ICO_SHEET_RY 4 /* rim: texels (field lines) down */
#define ICO_SHEET_WX_0 1000
#define ICO_SHEET_WX_1 760
#define ICO_SHEET_WX_2 610
#define ICO_SHEET_WX_3 540
#define ICO_SHEET_WX_4 500
#define ICO_SHEET_WX_5 390
#define ICO_SHEET_WX_6 330
#define ICO_SHEET_WY_0 1000
#define ICO_SHEET_WY_1 770
#define ICO_SHEET_WY_2 440
#define ICO_SHEET_WY_3 360
#define ICO_SHEET_WY_4 270
/* opacity and rim-to-fill levels: the sheets' white takes three steps
   between none and full (five levels), but once the strips' letters sat on
   the sheets' (ctest menu_look) eight gave the smaller difference on every
   language (the blurred difference's median 8.7 .. 9.4 where five give
   9.0 .. 9.5), a finer grain still dithered */
#define ICO_SHEET_LEVELS 8
/* the 4x4 Bayer matrix, row y in one constant, column x in nibble x */
#define ICO_SHEET_BAYER_ROW0 0xA280
#define ICO_SHEET_BAYER_ROW1 0x6E4C
#define ICO_SHEET_BAYER_ROW2 0x91B3
#define ICO_SHEET_BAYER_ROW3 0x5D7F
#define ICO_SHEET_T_OFF 16 /* the threshold (32nds) with dither off: rounding */
/* v0.4.2 (package F-G): a strip rasterised at s strip texels a sheet texel
 * across and down (the scene's or the output's scale, port/ui/menu_font.c),
 * 1..ICO_SHEET_SCALE_MAX: the letters are the strip's texels, the rim is
 * the 1x rim of the sheet texels' mean coverage magnified s times (v0.4.3,
 * package RIM; rd.h rd_SheetRim computes it into the texture's bottom
 * half), the Bayer cell is s x s strip texels (one sheet texel), the
 * levels are the same.  The
 * scale reaches the shader in DrawCB.param.w above the dither bit:
 * dither | (s - 1) << 1 (rd_replay.c sheetParam), so 1x is as before. */
#define ICO_SHEET_SCALE_MAX 4

/* Vertex of sprite.hlsl and font.hlsl: 20 bytes.
 *   loc 0 RHI_VTX_U16x2_UINT, loc 1 RHI_VTX_U32x1, loc 2 RHI_VTX_U8x4_UINT,
 *   loc 3 RHI_VTX_F32x2 */
typedef struct IcoSpriteVertex {
    uint16_t x, y;   /* GS 12.4 window coordinates */
    uint32_t z;      /* GS Z */
    uint8_t rgba[4]; /* alpha 0x80 = 1.0 */
    float u, v;      /* texels of t1 */
} IcoSpriteVertex;

/* Package AA1: the vertex of sprite_aa1_*_vs, 24 bytes: IcoSpriteVertex and,
 * at loc 4 (RHI_VTX_F32x1), the coverage: 0..1 on the geometry rd_replay.c
 * adds along an antialiased edge (interpolated, it is the pixel's coverage),
 * ICO_AA1_INTERIOR on a triangle's own vertices. */
typedef struct IcoSpriteAa1Vertex {
    IcoSpriteVertex v;
    float cov;
} IcoSpriteAa1Vertex;

#define ICO_AA1_INTERIOR 2.0f

/* Perspective-correct STQ: the vertex
 * of sprite_stq_*_vs, 24 bytes: IcoSpriteVertex and, at loc 4
 * (RHI_VTX_F32x1), the GS Q.  u and v are S and T in texels of t1 (S times
 * the texture size, the UV offset added times Q), not divided: the shader
 * interpolates (u, v, q) in screen space and sprite_stq_ps divides per
 * pixel, as the GS does. */
typedef struct IcoSpriteStqVertex {
    IcoSpriteVertex v;
    float q;
} IcoSpriteStqVertex;

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

_Static_assert(offsetof(IcoDrawCB, scale) == 96, "scale");

_Static_assert(sizeof(IcoDrawCB) == 112, "DrawCB size");

_Static_assert(sizeof(IcoCrtCB) == sizeof(IcoDrawCB), "CrtCB shares DrawCB's slot and size");

_Static_assert(offsetof(IcoCrtCB, pass) == 96, "CrtCB pass");

_Static_assert(offsetof(IcoSpriteVertex, z) == 4, "vertex z");

_Static_assert(offsetof(IcoSpriteVertex, rgba) == 8, "vertex rgba");

_Static_assert(offsetof(IcoSpriteVertex, u) == 12, "vertex uv");

_Static_assert(sizeof(IcoSpriteVertex) == 20, "vertex size");
_Static_assert(offsetof(IcoSpriteAa1Vertex, cov) == 20, "AA1 vertex coverage");
_Static_assert(sizeof(IcoSpriteAa1Vertex) == 24, "AA1 vertex size");
_Static_assert(offsetof(IcoSpriteStqVertex, q) == 20, "STQ vertex q");
_Static_assert(sizeof(IcoSpriteStqVertex) == 24, "STQ vertex size");

/* ---------------------------------------------------------------- VU1
 * Wave 3 (R3c): the VU1 program shaders (vu_common.hlsli). Group 1: t0 = the vertex stream (float4
 * quadwords as the VIF unpacked them), b2 = IcoVuCB, b3 = IcoVuBoneCB
 * (cluster only). */

/* VuCB: group 1, slot 2 (register b2, space1). 608 bytes.
 *   mem    VU1 data memory 0..35 as the program reads it: 0..15 the common
 *          block (RdVuCommon), 16..27 the per-object matrices, 28..35 the
 *          light matrices (vu_common.hlsli lists what each program keeps
 *          there; register-only uploads go to the same slots)
 *   draw   qword of the first batch in the stream, qwords per vertex,
 *          ICO_VU_* flags, vertices per batch (0 = one batch)
 *   batch  qwords before each batch's vertices (GIF tag 1; mesh: tag and
 *          colour 2, 3 with the VIF qword of a Mesh3D buffer), qwords after
 *          them (a Mesh3D buffer's MSCNT: 1), 0, 0 */
typedef struct IcoVuCB {
    float mem[36][4];
    uint32_t draw[4];
    uint32_t batch[4];
} IcoVuCB;

/* VuBoneCB: group 1, slot 3 (register b3, space1): VU memory 16..255, the
 * cluster bone matrices at VU address bone * 4 + 16. 3840 bytes. */
typedef struct IcoVuBoneCB {
    float bone[240][4];
} IcoVuBoneCB;

/* IcoVuCB.draw[2] (VU_* in vu_common.hlsli). */
enum {
    ICO_VU_CLIP_REGION = 0,  /* region test: a triangle with a vertex outside is not drawn */
    ICO_VU_CLIP_NONE = 1,    /* normal_c code 34: no test, X/Y wrap to 16 bits */
    ICO_VU_CLIP_SCISSOR = 2, /* code 36: clip-space flags, trivial reject, GPU clipping */
    ICO_VU_CLIP_MASK = 3,
    ICO_VU_PROBE = 16,    /* tests: one value per pixel of a 16-wide RGBA8_UINT target */
    ICO_VU_CUT_ONLY = 32, /* scissor: only the triangles SCISSOR_COMMON draws (first draw) */
    ICO_VU_KICK_ONLY = 64 /* scissor: only the triangles the strip kicks (second draw) */
};

/* The region test's centre on a wide screen (vu_common.hlsli VU_REGION_CX,
 * vu_region_pos): the test compares x squeezed by the frame's world x scale
 * about GS X 2048, the middle of the scene's GS window. */
#define ICO_VU_REGION_CX 2048.0f

/* The index value of an indexed VU draw: kick * 4 + corner (corner 0..2 =
 * vertex kick - 2 + corner); vertexOffset must be 0. */
#define ICO_VU_INDEX(kick, corner) ((uint32_t)(kick) * 4u + (uint32_t)(corner))
/* Issue 25: set on the three indices of a triangle that overlaps an earlier
 * triangle of its mesh in the same plane (RdMeshRec.drawIndex; static
 * prelit and lit meshes only), which vu_later_out draws in front of it;
 * vu_common.hlsli's VU_INDEX_LATER and VU_INDEX_MASK */
#define ICO_VU_INDEX_LATER 0x40000000u
#define ICO_VU_INDEX_MASK 0x3FFFFFFFu
#define ICO_VU_PROBE_FIELDS 16

_Static_assert(offsetof(IcoVuCB, mem) == 0, "vu mem");

_Static_assert(offsetof(IcoVuCB, draw) == 576, "vu draw");

_Static_assert(offsetof(IcoVuCB, batch) == 592, "vu batch");

_Static_assert(sizeof(IcoVuCB) == 608, "VuCB size");

_Static_assert(sizeof(IcoVuBoneCB) == 3840, "VuBoneCB size");

#endif
