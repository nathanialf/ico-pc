/* shader_consts.h: the C side of the byte layouts common.hlsli declares.
 * rd_core fills these; the static asserts pin each offset to the HLSL
 * cbuffer packing (every member is a 16-byte register or a 64-byte matrix,
 * so there is no packing ambiguity). Change this file and common.hlsli
 * together. The constants mirrored from an .hlsli (ICO_DF_*:
 * common.hlsli; ICO_TEXFMT_*: gs_math.hlsli; ICO_VU_*: vu_common.hlsli;
 * ICO_SHEET_*: sheet_text.hlsli) hold the same values as their HLSL twins;
 * ICO_SHEET_* and ICO_TEXFMT_* are static-asserted (port/render/test/
 * sheet_ref.c, port/shaders/test/gs_math_test.c). */
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

    /* The native frame (common.hlsli g_frame): frame[0] the region
     * scale, 512 / W on a target as wide as the native frame W, exactly 1
     * on every other target and at 4:3 (rd_replay.c rd__frame_group_ex; no
     * shader reads it).  frame[1], frame[2] the drawing camera's picture,
     * its half-width and half-height in GS pixels about (2048, 2048): 256 by
     * half the native height for the frame's camera, a reflection camera's
     * own (RdCamera.pictureW/H); the VU programs' rule for a triangle the
     * region test drops reads them (vu_common.hlsli vu_picture_half).
     * frame[3] the exact viewport's half texel (0.5, or 0 for the MSAA
     * epoch's expanded sprites).  No position is transformed.
     * vp: the exact viewport (common.hlsli g_vp, gs_px_to_ndc): 2 / its
     * width and height in texels (a power of two centred on the target),
     * the target's texels per GS pixel; all 0 when the viewport is the
     * target (the plain mapping). */
    float frame[4];
    float vp[4];

    /* x = 1 / 2^24 (PSMZ24 scale); yz the bound target's texels per GS
     * pixel (1 in Original); w = 1: the VU programs output unquantised X, Y
     * (only the developer switch ICO_RD_VU_OFFGRID; Enhanced, a scaled
     * target) */
    float z[4];
    float misc[4]; /* x frame counter, y preset, zw the target's share of the exact
                    * viewport (common.hlsli fullscreen_triangle; 0: all of it) */
    /* xy: the exact viewport's centre lies this far (0 or 0.5 texel) before
     * the target's: its origin is kept on a whole texel for an odd-sized
     * target (common.hlsli g_vpo); z the texels of the GS frame a Full
     * pixel target leaves out at its left, else 0; w reserved (0) */
    float vpo[4];
} IcoFrameCB;

/* DrawCB: group 1, slot 1 (register b1, space1). */
typedef struct IcoDrawCB {
    uint32_t col[4];   /* RGBA 0..255 */
    uint32_t mode[4];  /* flags, texa | texfmt << 8, atst | ate << 8 | split << 16, aref */
    uint32_t blend[4]; /* ALPHA register, FIX, COLCLAMP, TEX1's mipmap fields with
                        * ICO_DF_GS_LOD (rd_state.h rd_tex1_lod), else 0 */
    float uvRect[4];   /* u0, v0, u1, v1 in texels */
    float tex[4];      /* w, h of t1 in texels, 1/w, 1/h */
    float param[4];    /* kind specific */
    float scale[4];    /* xy t1 texels per GS texel (1, or a scaled target's
                        * scale); z with t1 a Full pixel crop of the frame:
                        * the left-out share of t1's width, ox / tw, in the
                        * draws and the blur sprite (rd_replay.c fillDrawCB,
                        * doBlurSpriteDraw; common.hlsli gs_uv_norm), the
                        * left-out texels, ox, in the shadow reduce blit
                        * (shadowReduce; blit.hlsl box_reduce_ps); else 0;
                        * w reserved (0) */
} IcoDrawCB;

/* crt.hlsl's CrtCB, group 1, slot 1 (register b1, space1),
 * bound in DrawCB's place through the draw layout (rd_replay.c
 * dynamicGroup, port/render/rd_crt.c); the two are the same size (the
 * static assert below).
 * All in pixels of the box or of the virtual source (the PS2 picture's
 * grid, 512 wide at 4:3) unless said. */
typedef struct IcoCrtCB {
    float src[4];  /* the virtual source: w, h, 1 / w, 1 / h */
    float box[4];  /* the box: w, h, x, y in output pixels */
    float beam[4]; /* scanline strength, beam width min, max (lines, FWHM), gap columns a
                    * source pixel */
    float mask[4]; /* type (RdCrtMask: 0 none, 1 grille, 2 slot, 3 dots), strength (1 - the
                    * leak), fade (rd__crt_mask_fade), halation */
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
    /* the sampler state sprite_texa_ps and vu_texa_ps filter
     * by (gs_texa_texture in common.hlsli); set only for those entries,
     * and the CLAMP bits for a nearest draw at the GS's level
     * (ICO_DF_NEAREST with ICO_DF_GS_LOD) */
    ICO_DF_TEXA_MAG_LINEAR = 1024,
    ICO_DF_TEXA_MIN_LINEAR = 2048,
    ICO_DF_TEXA_CLAMP_S = 4096,
    ICO_DF_TEXA_CLAMP_T = 8192,
    ICO_DF_TEXA_MIN_SAMPLED = 16384, /* minified pixels: the bound (Enhanced, mipmapped) sampler */
    /* the pipeline blends Cs + Cd * c1 (src ONE, dst SRC1: Cd*FIX + Cs): a
     * PABE pixel left unblended outputs c1 = 0 */
    ICO_DF_C1_DST = 32768,
    /* the two-pass blend without dual-source blending (the
     * *_nodual entries; rd_pipeline.c rd__expand_no_dual): the colour pass
     * writes the blend factor into c0.a, the alpha pass the stored alpha */
    ICO_DF_NODUAL_FACTOR = 65536,
    ICO_DF_NODUAL_ALPHA_PASS = 131072,
    /* the texture has its GS mipmap levels: sample at the GS's level from
     * Q and TEX1's fields in DrawCB.blend[3] (common.hlsli gs_lod) */
    ICO_DF_GS_LOD = 262144,
    /* bit 23 (past the write mask's 19..22): a VU draw with a clip plane
     * (VuCB vu_batch.zw, rd_chain.h) discards the pixels past it */
    ICO_DF_PLANE = 8388608,
    /* both of the draw's filters nearest: common.hlsli gs_sample reads the
     * texel the GS takes (gs_nearest_uv; with ICO_DF_GS_LOD
     * gs_sample_lod_nearest, wrapped by ICO_DF_TEXA_CLAMP_S/T) */
    ICO_DF_NEAREST = 16777216
};

/* DrawCB.mode[0] bits 19..22 on a destination-read pass (the *_dstrov and
 * *_dstatt entries, port/render/rd_dst_read.c): the channels the pass
 * writes, R G B A = bits 19..22 (FRAME.FBMSK by whole channels, the key's
 * colorMask).  The storage path writes the target itself, so the shader
 * applies the mask (common.hlsli gs_dst_out). */
#define ICO_DF_WMASK_SHIFT 19u

/* DrawCB.mode[0] bit 25 (common.hlsli DF_ABOVE1X): a bilinear draw of
 * sprite_ps, sprite_aa1_ps or sprite_stq_ps sampling a target whose scale is
 * not the bound target's (rd_replay.c fillDrawCB), resampled by the step
 * (sprite.hlsl sprite_above1x), the wrap in ICO_DF_TEXA_CLAMP_S/T. */
enum { ICO_DF_ABOVE1X = 33554432 };

/* DrawCB.mode[1] bits 8..: TEXFMT_* in gs_math.hlsli. */
enum { ICO_TEXFMT_RGBA32 = 0, ICO_TEXFMT_RGB24 = 1, ICO_TEXFMT_RGBA16 = 2 };

/* font_sheet_ps's constants, SHEET_* in
 * sheet_text.hlsli (the same values; port/render/test/sheet_ref.c
 * static-asserts each pair).  rd.h rd_create_texture_sheet says what they do.
 * The style reaches the shader in DrawCB.param: the rim's weight (0 off,
 * 64 full), rimLevel, fillLevel, dither (rd_replay.c, from RdTexRec.sheet). */
/* The values are the sheets' survey (the comment above kSheetInk in
 * port/ui/menu_font.c; ctest menu_look testSurvey).  The rim is
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
/* A strip rasterised at s strip texels a sheet texel across and down (the
 * scene's or the output's scale, port/ui/menu_font.c), 1..ICO_SHEET_SCALE_MAX:
 * the letters are the strip's texels, the rim is the 1x rim of the sheet
 * texels' mean coverage magnified s times (rd.h rd_sheet_rim computes it
 * into the texture's bottom half), the Bayer cell is s x s strip texels
 * (one sheet texel), the levels are the same.  The scale reaches the
 * shader in DrawCB.param.w above the dither bit: dither | (s - 1) << 1
 * (rd_replay.c sheetParam), so s = 1 leaves the dither word as it is. */
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

/* The vertex of sprite_aa1_*_vs, 24 bytes: IcoSpriteVertex and,
 * at loc 4 (RHI_VTX_F32x1), the coverage: 0..1 on the geometry rd_replay.c
 * adds along an antialiased edge (interpolated, it is the pixel's coverage),
 * ICO_AA1_INTERIOR on a triangle's own vertices. */
typedef struct IcoSpriteAa1Vertex {
    IcoSpriteVertex v;
    float cov;
} IcoSpriteAa1Vertex;

#define ICO_AA1_INTERIOR 2.0f

/* Perspective-correct STQ: the vertex of sprite_stq_*_vs, 24 bytes: IcoSpriteVertex and, at loc 4
 * (RHI_VTX_F32x1), the GS Q.  u and v are S and T in texels of t1 (S times
 * the texture size, the UV offset added times Q), not divided: the shader
 * interpolates (u, v, q) in screen space and sprite_stq_ps divides per
 * pixel, as the GS does. */
typedef struct IcoSpriteStqVertex {
    IcoSpriteVertex v;
    float q;
} IcoSpriteStqVertex;

/* The vertex of sprite_plane_*_vs and sprite_stq_plane_*_vs, 120 bytes
 * (docs/port/RENDER.md, "Screen planes"): a screen-prim triangle's vertex (q 1 for a prim
 * without STQ) and its triangle's planes, the same on its three vertices
 * (rd_screen_planes.c): the colour at the triangle's first corner and its
 * change per GS pixel across and down (loc 5..7), S, T and Q there (S and
 * T in texels of t1, times Q) and their changes (loc 8..10, xyz), the first
 * corner's GS x and y in s0[3] and sx[3], 1 in sy[3].  sprite.hlsl
 * sprite_plane_attrs evaluates them per pixel, the same at every frame
 * width. */
typedef struct IcoSpritePlaneVertex {
    IcoSpriteStqVertex s;
    float c0[4], cx[4], cy[4];
    float s0[4], sx[4], sy[4];
} IcoSpritePlaneVertex;

_Static_assert(sizeof(IcoSpritePlaneVertex) == 120, "IcoSpritePlaneVertex size");

_Static_assert(offsetof(IcoFrameCB, view) == 0, "view");

_Static_assert(offsetof(IcoFrameCB, proj) == 64, "proj");

_Static_assert(offsetof(IcoFrameCB, viewProj) == 128, "viewProj");

_Static_assert(offsetof(IcoFrameCB, cameraPos) == 192, "cameraPos");

_Static_assert(offsetof(IcoFrameCB, clip) == 208, "clip");

_Static_assert(offsetof(IcoFrameCB, target) == 224, "target");

_Static_assert(offsetof(IcoFrameCB, origin) == 240, "origin");

_Static_assert(offsetof(IcoFrameCB, frame) == 256, "frame");

_Static_assert(offsetof(IcoFrameCB, vp) == 272, "vp");

_Static_assert(offsetof(IcoFrameCB, z) == 288, "z");

_Static_assert(offsetof(IcoFrameCB, misc) == 304, "misc");

_Static_assert(offsetof(IcoFrameCB, vpo) == 320, "vpo");

_Static_assert(sizeof(IcoFrameCB) == 336, "FrameCB size");

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
 * The VU1 program shaders (vu_common.hlsli). Group 1: t0 = the vertex
 * stream (float4 quadwords as the VIF unpacked them), b2 = IcoVuCB,
 * b3 = IcoVuBoneCB (cluster only). */

/* VuCB: group 1, slot 2 (register b2, space1). 752 bytes.
 *   mem    VU1 data memory 0..35 as the program reads it: 0..15 the common
 *          block (RdVuCommon), 16..27 the per-object matrices, 28..35 the
 *          light matrices (vu_common.hlsli lists what each program keeps
 *          there; register-only uploads go to the same slots)
 *   draw   qword of the first batch in the stream, qwords per vertex,
 *          ICO_VU_* flags, vertices per batch (0 = one batch)
 *   batch  qwords before each batch's vertices (GIF tag 1; mesh: tag and
 *          colour 2, 3 with the VIF qword of a Mesh3D buffer), qwords after
 *          them (a Mesh3D buffer's MSCNT: 1), the clip plane's code and its
 *          offset's float bits (0, 0: none; rd_chain.h, static prelit and lit
 *          meshes)
 *   strip  the draw's first vertex and one past its last (the region
 *          rule's strip test, vu_common.hlsli vu_strip_of; both 0: no strip
 *          test), 0, 0
 *   ticks  under ICO_VU_TICKS: the two ticks' cameras of a picture blended
 *          between them, as the matrices R = S_tick St^-1 (columns; 0..3
 *          the tick before's, 4..7 this tick's) that take a vertex's
 *          position h = St M pos through the blended camera St to the one
 *          each tick's camera gave (rd_interp.c, RdFrame.tickCams;
 *          vu_common.hlsli vu_region_rule); unused without the flag */
typedef struct IcoVuCB {
    float mem[36][4];
    uint32_t draw[4];
    uint32_t batch[4];
    uint32_t strip[4];
    float ticks[8][4];
} IcoVuCB;

/* VuBoneCB: group 1, slot 3 (register b3, space1): VU memory 16..255, the
 * cluster bone matrices at VU address bone * 4 + 16. 3840 bytes. */
typedef struct IcoVuBoneCB {
    float bone[240][4];
} IcoVuBoneCB;

/* IcoVuCB.draw[2] (VU_* in vu_common.hlsli). */
enum {
    ICO_VU_CLIP_REGION = 0,  /* region test: a failing triangle whose clipped extent
                              * reaches the camera's picture (FrameCB frame[1], [2]) is dropped,
                              * one wholly beside it GPU-clipped at the native frustum; every
                              * one GPU-clipped under ICO_VU_BEHIND_EYE; under
                              * ICO_VU_EXTENDED one in front of the eye is GPU-clipped unless
                              * it belongs to a card (vu_common.hlsli vu_region_rule) */
    ICO_VU_CLIP_NONE = 1,    /* normal_c code 34: no test, X/Y wrap to 16 bits */
    ICO_VU_CLIP_SCISSOR = 2, /* code 36: clip-space flags, trivial reject, GPU clipping */
    ICO_VU_CLIP_MASK = 3,
    ICO_VU_PROBE = 16,       /* tests: one value per pixel of a 16-wide RGBA8_UINT target */
    ICO_VU_CUT_ONLY = 32,    /* scissor: only the triangles SCISSOR_COMMON draws (first draw) */
    ICO_VU_KICK_ONLY = 64,   /* scissor: only the triangles the strip kicks (second draw) */
    ICO_VU_EXTENDED = 128,   /* region test: the picture is not the PS2's out of the box (the
                             * Enhanced preset, wider than 4:3 or scaled up; rd_replay.c
                             * vuExtended) */
    ICO_VU_BEHIND_EYE = 256, /* region test and particle window: a failing triangle or
                               * sprite is GPU-clipped, not dropped, in either preset
                               * (photo mode's free camera, RdCamera.freeCamera) */
    ICO_VU_TICKS = 512       /* region test: the draw is seen through a camera blended
                              * between two ticks' (IcoVuCB.ticks): a triangle the rule
                              * drops is drawn, clipped, when the rule draws it at either
                              * tick's camera (vu_common.hlsli vu_region_rule) */
};

/* The native frame's constants (port/include/ico_frame.h ICO_FRAME_BASE_W,
 * ICO_FRAME_K_MAX, ICO_FRAME_W_MAX), for the shaders' side: the 4:3 width
 * the projection and the 2D layer are sized by, the widest frame as a
 * multiple of it, and its width in GS pixels.  rd_present_test
 * static-asserts they equal the header's. */
#define ICO_SH_FRAME_BASE_W 512
#define ICO_SH_FRAME_K_MAX 5
#define ICO_SH_FRAME_W_MAX 2560

/* The index value of an indexed VU draw: kick * 4 + corner (corner 0..2 =
 * vertex kick - 2 + corner); vertexOffset must be 0. */
#define ICO_VU_INDEX(kick, corner) ((uint32_t)(kick) * 4u + (uint32_t)(corner))
/* Set on the three indices of a triangle that overlaps an earlier
 * triangle of its mesh in the same plane (RdMeshRec.drawIndex; static
 * prelit and lit meshes only), which vu_later_out draws in front of it;
 * vu_common.hlsli's VU_INDEX_LATER and VU_INDEX_MASK.  The kick stays
 * below ICO_VU_INDEX_KICK_LIMIT for any mark (rd_mesh.c marks no larger
 * mesh) */
#define ICO_VU_INDEX_LATER 0x40000000u
/* The content table (port/render/rd_content_hide.c, rd_mesh.c
 * markContentHidden), set on the three indices of a triangle of a listed
 * artist trick: HIDE, drawn nowhere.  vu_common.hlsli's VU_INDEX_HIDE */
#define ICO_VU_INDEX_HIDE 0x20000000u
/* Set on the three indices of a triangle of a joined strip (one
 * that shares a vertex position with another strip of its mesh, not its
 * double-sided copy; rd_mesh.c markJoinedStrips; prelit, lit and skinned
 * meshes): not a card, so the region rule draws its failing triangles
 * clipped outside the out-of-the-box state.  vu_common.hlsli's
 * VU_INDEX_JOINED */
#define ICO_VU_INDEX_JOINED 0x10000000u
#define ICO_VU_INDEX_MASK 0x0FFFFFFFu
/* kick * 4 + corner below 2^28: the meshes rd_mesh.c marks have fewer
 * vertices */
#define ICO_VU_INDEX_KICK_LIMIT (1u << 26)
#define ICO_VU_PROBE_FIELDS 16

_Static_assert(offsetof(IcoVuCB, mem) == 0, "vu mem");

_Static_assert(offsetof(IcoVuCB, draw) == 576, "vu draw");

_Static_assert(offsetof(IcoVuCB, batch) == 592, "vu batch");
_Static_assert(offsetof(IcoVuCB, strip) == 608, "vu strip");

_Static_assert(offsetof(IcoVuCB, ticks) == 624, "vu ticks");
_Static_assert(sizeof(IcoVuCB) == 752, "VuCB size");

_Static_assert(sizeof(IcoVuBoneCB) == 3840, "VuBoneCB size");

#endif
