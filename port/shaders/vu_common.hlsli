// vu_common.hlsli: what the VU1 program shaders (vu_*.hlsl) share. The
// programs are ico2/vusrc/{normal_c,normal_l,cluster,mesh,particle}.vsm,
// described; port/render/vu1_ref/ holds the CPU
// references the shaders are tested against (port/shaders/test/vu1_test.c).
// Byte layouts are mirrored in shader_consts.h (IcoVuCB, IcoVuBoneCB).
//
// Bindings (group 1, next to DrawCB at b1):
//   t0  vu_stream  the vertex quadwords as Packet.c / Primitive.c built them
//                  for the VIF UNPACK (float4 each; the cluster weight qword
//                  keeps its two int VU addresses as raw bits); static meshes
//                  without the batches' GIF tags (rd_mesh.h)
//   b2  VuCB       the VU1 data memory 0..35 the program reads (see below)
//   b3  VuBoneCB   cluster only: VU memory 16..255, the bone matrices
//
// Drawing: the mesh programs are drawn as an indexed triangle list whose
// index value (SV_VertexID; vertexOffset / BaseVertexLocation must be 0) is
// kick * 4 + corner: kick = the vertex index k whose XYZ2 draws the
// triangle k-2, k-1, k on the GS, corner 0..2 the vertex k-2+corner. Each
// invocation evaluates all three vertices' positions so the per-triangle
// decisions (region test, scissor) come out the same in all three. The
// particle program is drawn non-indexed, six vertices per particle.
#ifndef ICO_VU_COMMON_HLSLI
#define ICO_VU_COMMON_HLSLI

#include "common.hlsli"

// VuCB.vu_mem: VU1 data memory 0..35 as the program reads it.
//   0..15   the common block (RdVuCommon): 0 =
//           (0,0,0,1), 1 = (4095,4095,0,16777215), 2 = UV offset in xy
//           (SET_UVOFFSET) and the cluster fade alpha in w, 3 = GIF tag,
//           4..7 world to GS screen, 8..11 viewport, 12..15 inverse view
//   16..19  normal_c/normal_l: model to GS screen (+0x140; also vf01..vf04).
//           mesh: the SET_MESH_MATRIX matrix (vf01..vf04). particle:
//           SET_PARTICLE_MATRIX's first matrix (vf01..vf04)
//   20..23  normal: model to clip (+0x200 x model), the scissor clip space.
//           particle: the screen matrix (vf05..vf08)
//   24..27  normal: model to view (+0x80 x model), NORMAL_REF
//   28..31  light matrix L1 (normal mem 28..31; mesh vf05..vf08; cluster
//           vf13..vf16)
//   32..35  light colour matrix L2 (normal mem 32..35; mesh vf09..vf12;
//           cluster vf17..vf20); the fourth column is the ambient term
// vu_draw  x = qword index in vu_stream of the draw's first batch,
//          y = qwords per vertex, z = VU_F_* flags,
//          w = vertices per batch (0: one batch, the stream holds vertices only)
// vu_batch x = header qwords before each batch's vertices (the GIF tag:
//          1; mesh: the tag and the batch colour, 2, or 3 with the VIF qword
//          of an unstripped Mesh3D buffer), y = qwords after each batch's
//          vertices (an unstripped Mesh3D buffer's MSCNT: 1), z = a clip
//          plane's code (0 none; vu_plane_distance), w = its offset's bits
// vu_strip x, y = the draw's vertices, the first and one past the last
//          (the region rule's strip test, vu_strip_of; y 0: no strip test,
//          the PS2's rule), zw reserved
// vu_ticks under VU_F_TICKS: the two ticks' cameras of a blended picture,
//          R = S_tick St^-1 (columns; 0..3 the tick before's, 4..7 this
//          tick's), a vertex's h through the blended camera St to the h the
//          tick's camera gave (vu_tick_vertex)
cbuffer VuCB : register(b2, space1)
{
    float4 vu_mem[36];
    uint4 vu_draw;
    uint4 vu_batch;
    uint4 vu_strip;
    float4 vu_ticks[8];
};

StructuredBuffer<float4> vu_stream : register(t0, space1);

// vu_draw.z
#define VU_CLIP_MASK 3u
#define VU_CLIP_REGION 0u  // the triangle loops' region test (codes 32, 38; cluster, mesh)
#define VU_CLIP_NONE 1u    // normal_c code 34: no test, X/Y wrap to 16 bits as on the GS
#define VU_CLIP_SCISSOR 2u // code 36: clip-space flags, trivial reject, clipping
#define VU_F_PROBE 16u     // tests: write the vertex's values instead of drawing
// Scissor draw order: SCISSOR_COMMON kicks its fans while the loop runs,
// before the batch's own packet, so a VU batch is two draws to keep the
// order: first with VU_F_CUT_ONLY (the triangles the VU clips), then with
// VU_F_KICK_ONLY (the triangles the strip kicks). Neither: both, in strip
// order.
#define VU_F_CUT_ONLY 32u
#define VU_F_KICK_ONLY 64u
// Photo mode's free camera (RdCamera.freeCamera): every triangle failing the
// region test, and every particle outside its x/y window, is drawn
// GPU-clipped over the whole picture, a vertex behind the eye or not, in
// either preset (vu_triangle_out, vu_particle_vs)
#define VU_F_BEHIND_EYE 256u
// The picture is not the PS2's out of the box (rd_replay.c doVuDraw: the
// Enhanced preset, a frame wider than 4:3 or a scene scale above 1): a
// triangle the region test drops is drawn, GPU-clipped, unless it belongs
// to a card (vu_region_rule)
#define VU_F_EXTENDED 128u
// The draw is seen through a camera blended between two ticks' (rd_interp.c
// camSetup; rd_replay.c sets it from RdFrame.tickCams): vu_ticks holds the
// ticks' cameras, and a triangle the region rule drops at the blended
// camera is drawn, clipped, when either tick showed it on the frame
// (vu_region_rule)
#define VU_F_TICKS 512u
// The index bit of a triangle that overlaps an earlier triangle of
// its mesh in the same plane (shader_consts.h ICO_VU_INDEX_LATER; static
// prelit and lit meshes, rd_mesh.c markLaterOverlaps); vu_later_out
#define VU_INDEX_LATER 0x40000000u
// The content table (port/render/rd_content_hide.c, rd_mesh.c
// markContentHidden): the index bit of a triangle of an artist trick made
// for the 4:3 picture, drawn nowhere (HIDE). Static prelit and lit meshes
// only; shader_consts.h ICO_VU_INDEX_HIDE
#define VU_INDEX_HIDE 0x20000000u
// The index bit of a triangle of a joined strip (rd_mesh.c
// markJoinedStrips; shader_consts.h ICO_VU_INDEX_JOINED): not a card
// (vu_region_rule)
#define VU_INDEX_JOINED 0x10000000u
#define VU_INDEX_MASK 0x0FFFFFFFu

// Whether the triangle of index value vid is left out of every draw (HIDE).
bool vu_content_hidden(uint vid)
{
    return (vid & VU_INDEX_HIDE) != 0u;
}

#define VU_PROBE_FIELDS 16u

// ---------------------------------------------------------- VU operations

// mulax / madday / maddaz / maddw: c0 * v.x + c1 * v.y + c2 * v.z + c3 * w,
// every product and sum rounded on its own, in this order (precise: no
// fused multiply-add, no reassociation).
float4 vu_mat(float4 c0, float4 c1, float4 c2, float4 c3, float4 v, float w)
{
    precise float4 a = c0 * v.x;
    a = a + c1 * v.y;
    a = a + c2 * v.z;
    a = a + c3 * w;
    return a;
}

float4 vu_matm(uint at, float4 v, float w)
{
    return vu_mat(vu_mem[at], vu_mem[at + 1u], vu_mem[at + 2u], vu_mem[at + 3u], v, w);
}

// ftoi0 / ftoi4: truncation, saturating at +-2^31 like the VU.
int vu_ftoi(float x)
{
    if (x >= 2147483648.0) {
        return 0x7FFFFFFF;
    }
    if (x <= -2147483648.0) {
        return (int)0x80000000u;
    }
    return (int)x;
}

int vu_ftoi4(float x)
{
    return vu_ftoi(x * 16.0);
}

// div q, vf00w, x: a zero (or denormal) divisor gives +-Fmax.
float vu_rcp(float x)
{
    if ((asuint(x) & 0x7F800000u) == 0u) {
        return asfloat((asuint(x) & 0x80000000u) | 0x7F7FFFFFu);
    }
    precise float q = 1.0 / x;
    return q;
}

// The RGBAQ the GS takes from an ftoi0 quadword: bits 0..7 of each word.
uint4 vu_rgba(float4 c)
{
    return uint4(vu_ftoi(c.x), vu_ftoi(c.y), vu_ftoi(c.z), vu_ftoi(c.w)) & 255u;
}

// clipw.xyz v, v.w: bit 0 +x (x > |w|), 1 -x, 2 +y, 3 -y, 4 +z, 5 -z.
uint vu_clipw(float4 v)
{
    float w = abs(v.w);
    uint f = 0u;
    f |= v.x > w ? 1u : 0u;
    f |= v.x < -w ? 2u : 0u;
    f |= v.y > w ? 4u : 0u;
    f |= v.y < -w ? 8u : 0u;
    f |= v.z > w ? 16u : 0u;
    f |= v.z < -w ? 32u : 0u;
    return f;
}

// The triangle loops' region test (sub.xyw and fmand 0xD0): lo < p < hi on
// x, y (after the divide) and w (the clip w).
bool vu_inside(float4 p, float4 lo, float4 hi)
{
    return lo.x < p.x && lo.y < p.y && lo.w < p.w && p.x < hi.x && p.y < hi.y && p.w < hi.w;
}

// The bounds normal_c, normal_l and mesh load (loi 0x457FF000 = 4094.0,
// loi 0x4B7FFFFE = 16777214.0); cluster reads mem[0] and mem[1] instead.
static const float4 VU_LO0 = float4(0.0, 0.0, 0.0, 0.0);
static const float4 VU_HI4094 = float4(4094.0, 4094.0, 0.0, 16777214.0);

// -------------------------------------------------------------- the stream

// The first qword of vertex v and of its batch.
uint vu_batch_qw(uint v)
{
    uint vpb = vu_draw.w;
    uint b = vpb != 0u ? v / vpb : 0u;
    return vu_draw.x + b * (vu_batch.x + vpb * vu_draw.y + vu_batch.y);
}

uint vu_vertex_qw(uint v)
{
    uint vpb = vu_draw.w;
    uint k = vpb != 0u ? v % vpb : v;
    return vu_batch_qw(v) + vu_batch.x + k * vu_draw.y;
}

// ------------------------------------------------------------- one vertex

// What one VU loop iteration computes for a vertex.
struct VuVtx {
    float4 h;    // the screen matrix times pos, before the divide (w: the clip w)
    float4 p;    // xyz divided by w (pixels; z = GS Z / 16), w = h.w
    int3 gs;     // ftoi4 of p.xyz: GS X, Y (12.4) and Z
    uint4 rgba;  // RGBAQ, 0..255
    float3 stq;  // PACKED ST
    bool inside; // the region test
    float4 clip; // scissor programs: model to clip space (mem[20..23]) times pos
};

VuVtx vu_vtx_init()
{
    VuVtx o;
    o.h = float4(0.0, 0.0, 0.0, 1.0);
    o.p = o.h;
    o.gs = int3(0, 0, 0);
    o.rgba = uint4(0u, 0u, 0u, 0u);
    o.stq = float3(0.0, 0.0, 0.0);
    o.inside = true;
    o.clip = o.h;
    return o;
}

// The divide and the conversions every loop shares: q = 1 / h.w,
// p.xyz = h.xyz * q, ftoi4.
float vu_divide(inout VuVtx o)
{
    float q = vu_rcp(o.h.w);
    precise float3 p = o.h.xyz * q;
    o.p = float4(p, o.h.w);
    o.gs = int3(vu_ftoi4(p.x), vu_ftoi4(p.y), vu_ftoi4(p.z));
    return q;
}

// ------------------------------------------------------------ the output

struct VuVSOut {
    float4 pos : SV_Position;
    VK_LOC(0) noperspective float4 col : COLOR0; // RGBA 0..255, the GS interpolates in screen space
    VK_LOC(1)
    noperspective float3 stq : TEXCOORD0; // S, T, Q linear in screen space, divided per pixel
    VK_LOC(2) noperspective float plane : TEXCOORD1; // vu_plane_distance; drawn where >= 0
    // The triangle's attribute planes (vu_attr_planes): col, stq and plane
    // at its first corner (a0: col, a1: stq, plane) and per texel across
    // (x0, x1) and down (y0, y1); r.xy the corner in texels from the
    // target's centre, r.z 1 when the planes hold (vu_pixel_attrs).
    VK_LOC(3) nointerpolation float4 a0 : TEXCOORD2;
    VK_LOC(4) nointerpolation float4 a1 : TEXCOORD3;
    VK_LOC(5) nointerpolation float4 x0 : TEXCOORD4;
    VK_LOC(6) nointerpolation float4 x1 : TEXCOORD5;
    VK_LOC(7) nointerpolation float4 y0 : TEXCOORD6;
    VK_LOC(8) nointerpolation float4 y1 : TEXCOORD7;
    VK_LOC(9) nointerpolation float4 r : TEXCOORD8;
};

// GS pixels (XYOFFSET-relative window coordinates / 16) to clip space;
// gs_xy_to_ndc for signed and wider-than-16-bit values (common.hlsli
// gs_px_to_ndc: exact on the 1/16 grid under the exact viewport).
float2 vu_ndc(float2 px)
{
    return gs_px_to_ndc(px);
}

// The vertex as the GS gets it: X, Y and Z as integers, w = 1, no
// perspective left. wrap: X and Y keep their low 16 bits (the XYZ2 field).
float4 vu_gs_position(int3 gs, bool wrap)
{
    int2 xy = wrap ? (gs.xy & 0xFFFF) : gs.xy;
    return float4(vu_ndc(float2(xy) * (1.0 / 16.0)), gs_depth(asuint(gs.z)), 1.0);
}

// The homogeneous form of the same mapping, for the vertices of a triangle
// the GPU has to clip that have no GS position (vu_cut_position: behind the
// eye, outside the Z range or saturated by ftoi4):
// x/w and y/w are the pixel positions above (unsnapped), z/w = Z * g_z.x
// for the unsaturated GS Z = 16 * h.z / h.w, linear (gs_z_to_depth without
// its top band and clamp: a piecewise map is not linear in clip space).
// The GPU's 0 <= z <= w clip is then GS Z 0 .. 1/g_z.x: 2^32 on D24S8, but
// 2^33 on a float depth buffer (GS_ZSCALE_32F), where a vertex in the top
// band (Z >= 0xFFFF0000) gets z/w near 0.5, not gs_z_to_depth's band.
// Clipping at 2^32 there needs a clip plane (SV_ClipDistance and the
// device's shaderClipDistance), which the pipelines do not use.
float4 vu_homogeneous_position(float4 h)
{
    precise float z = 16.0 * h.z * g_z.x; // gs_z_to_depth's z * scale
    if (g_vp.x != 0.0) {
        // the exact viewport's form (common.hlsli gs_px_to_ndc) times w:
        // about the target's centre, so the same at every frame width up to
        // the viewport's power-of-two factor
        precise float2 c = g_origin.xy + g_target.xy * 0.5;
        precise float x = ((h.x - c.x * h.w) * g_vp.z + (g_frame.w + g_vpo.x) * h.w) * g_vp.x;
        precise float y = -(((h.y - c.y * h.w) * g_vp.w + (g_frame.w + g_vpo.y) * h.w) * g_vp.y);
        return float4(x, y, z, h.w);
    }
    float2 o = g_origin.xy - g_origin.zw;
    // precise: common.hlsli gs_px_to_ndc
    precise float x = (h.x - o.x * h.w) * g_target.z * 2.0 - h.w;
    precise float y = h.w - (h.y - o.y * h.w) * g_target.w * 2.0;
    return float4(x, y, z, h.w);
}

// The vertex the GS gets, from a loop's vertex: the ftoi4 value, on the
// 12.4 grid at every scale, so a VU vertex lands where the
// GIF and CPU paths put the same point (sprite.hlsl) and two meshes'
// shared vertices the GS merges stay merged (a crack between them would
// show the clear, which the fog pass paints at full fog).  g_z.w set
// (rd: only the developer switch ICO_RD_VU_OFFGRID, Enhanced on a target
// finer than the GS grid) keeps X and Y the divided position unquantised:
// smoother slow motion (the grid is 1/16 GS pixel, a quarter output pixel
// at 4x), but seams open.
float4 vu_vtx_position(VuVtx v)
{
    if (g_z.w != 0.0) {
        return float4(vu_ndc(v.p.xy), gs_depth(asuint(v.gs.z)), 1.0);
    }
    return vu_gs_position(v.gs, false);
}

// Whether the GS position of a vertex is the one the GS would
// draw it at: in front of the eye (w > 0), GS Z not negative (no -z clip)
// and none of X, Y, Z saturated by ftoi4 (the values vu_ftoi writes).
bool vu_has_gs_position(VuVtx v)
{
    bool satX = v.gs.x == 0x7FFFFFFF || v.gs.x == (int)0x80000000u;
    bool satY = v.gs.y == 0x7FFFFFFF || v.gs.y == (int)0x80000000u;
    return v.h.w > 0.0 && v.gs.z >= 0 && v.gs.z != 0x7FFFFFFF && !satX && !satY;
}

// A corner of a triangle the GPU clips (code 36's SCISSOR_COMMON): the
// vertex's GS position when it has one, so the triangle's depth and edges
// are bit for bit those of the kicked triangles and of codes 32, 34 and 38
// over the same surface (w = 1, gs_depth of the ftoi4 Z), and only the
// corners without one take the homogeneous form.  Mixing the two is sound:
// a clip-space position is homogeneous of degree 1, so the GPU's clip
// points and the screen-space (noperspective) varyings are those of either
// form.  The homogeneous depth, Z * g_z.x linear, differs from gs_depth's
// value enough that a coplanar pass (the specular pass, 34/38) would win or
// lose GEQUAL per pixel.  (The near railing's coplanar triangles are a
// code-32 draw: vu_later_out.)
float4 vu_cut_position(VuVtx v)
{
    return vu_has_gs_position(v) ? vu_vtx_position(v) : vu_homogeneous_position(v.h);
}

static const float4 VU_CULLED = float4(2.0, 2.0, 2.0, 1.0); // outside x <= w: clipped away

// The camera's picture: the part of the drawing camera's view the PS2
// showed, about GS (2048, 2048) (gsb_SetVSMatrix's centre): g_frame.y GS
// pixels either side across, g_frame.z up and down (rd_replay.c
// rd__frame_group_ex). The frame's camera: 256 (the 512-pixel screen) by
// half the native height. A reflection camera's (puddle.c, pool.c, through
// RdCamera.pictureW/H and the VU draw's command): the part of its block
// the frame shows at 4:3. The projection is the same at every aspect, so
// this is the same part of the camera's view on every frame; a frame wider
// than 4:3 shows more of the view beside it. A FrameCB without it (0) is
// read as 256 across and no bound up and down.
float2 vu_picture_half()
{
    return float2(g_frame.y > 0.0 ? g_frame.y : 256.0, g_frame.z > 0.0 ? g_frame.z : 1e6);
}

// The picture's edges in clip-space positions (x / w, y / w as vu_ndc maps
// GS pixels): x left, y right, z top, w bottom (top > bottom: clip y grows
// upwards).
float4 vu_picture_ndc()
{
    float2 h = vu_picture_half();
    float2 a = vu_ndc(float2(2048.0, 2048.0) - h);
    float2 b = vu_ndc(float2(2048.0, 2048.0) + h);
    return float4(a.x, b.x, a.y, b.y);
}

// The frame the bound target shows, in vu_picture_ndc's form: the
// target's GS extent about its centre (a Full pixel target's: the frame's
// exact width, less the texels it leaves out each side, g_vpo.z). At 4:3
// the frame's camera's frame is its picture; on a wider frame it holds
// the picture and the bands beside it.
float4 vu_frame_ndc()
{
    precise float2 c = g_origin.xy + g_target.xy * 0.5;
    precise float2 h = g_target.xy * 0.5;
    if (g_vp.x != 0.0 && g_vp.z > 0.0) {
        h.x = h.x - g_vpo.z / g_vp.z;
    }
    float2 a = vu_ndc(c - h);
    float2 b = vu_ndc(c + h);
    return float4(a.x, b.x, a.y, b.y);
}

// Whether a clip-space x range lo .. hi meets the columns of the edges e
// (vu_picture_ndc's form). Conservative at its edges: a range that ends
// exactly on an edge covers no pixel centre inside, but counts as reaching
// it.
bool vu_reaches_columns(float lo, float hi, float4 e)
{
    return lo < e.y && hi > e.x;
}

// Whether a clip-space x range lo .. hi meets the camera's picture's
// columns (vu_reaches_columns).
bool vu_reaches_picture(float lo, float hi)
{
    return vu_reaches_columns(lo, hi, vu_picture_ndc());
}

// Whether a clip-space rectangle lo .. hi (a sprite's corners) meets the
// camera's picture, edges counted as above.
bool vu_rect_reaches_picture(float2 lo, float2 hi)
{
    float4 e = vu_picture_ndc();
    return lo.x < e.y && hi.x > e.x && lo.y < e.z && hi.y > e.w;
}

// Corners p[0..n) of a convex polygon in clip space cut to
// dot(plane, v) >= 0 (Sutherland-Hodgman). A triangle cut by four planes
// has at most seven corners.
//
// No corner array is indexed by a run-time value and none is copied,
// built or carried whole: every loop over the corners is written out as
// VU_CUT_CORNERS constant steps guarded by the count, a write at a run-time
// position (vu_corner_set) or a read at one (vu_corner) is a select over
// the corners, so the compiled shader holds the corners in registers (no
// array-typed value, no OpVariable in the Function storage class). The
// vertex shaders carrying the arrays as values (an NVIDIA GeForce 10-series
// driver, an Adreno 740 driver) were the first pipeline those drivers'
// compilers crashed on, while every shader without them built;
// tools/check_spirv.py (the spirv_check test) keeps every shader free of
// them (docs/port/SHADERS.md, Scalars, not arrays).
#define VU_CUT_CORNERS 7u

// p[i] for a run-time i (i < VU_CUT_CORNERS)
float4 vu_corner(float4 p[VU_CUT_CORNERS], uint i)
{
    float4 r = p[0];
    [unroll] for (uint j = 1u; j < VU_CUT_CORNERS; j++)
    {
        r = j == i ? p[j] : r;
    }
    return r;
}

// q[m] = v for a run-time m (m < VU_CUT_CORNERS)
void vu_corner_set(inout float4 q[VU_CUT_CORNERS], uint m, float4 v)
{
    [unroll] for (uint j = 0u; j < VU_CUT_CORNERS; j++)
    {
        q[j] = j == m ? v : q[j];
    }
}

// One corner of the cut, corner I against the plane (I a literal: the
// loop below is written out, since the compiler left an [unroll] loop with
// this body as a loop, indexing the corners at run time)
#define VU_CLIP_STEP(I)                                                              \
    if ((I) < nc) {                                                                  \
        float4 a = p[I];                                                             \
        float4 b = vu_corner(p, (I) + 1u < nc ? (I) + 1u : 0u);                      \
        float da = dot(plane, a);                                                    \
        float db = dot(plane, b);                                                    \
        if (da >= 0.0 && m < VU_CUT_CORNERS) {                                       \
            vu_corner_set(q, m, a);                                                  \
            m++;                                                                     \
        }                                                                            \
        if ((da >= 0.0) != (db >= 0.0) && m < VU_CUT_CORNERS) {                      \
            vu_corner_set(q, m, lerp(a, b, da / (da - db)));                         \
            m++;                                                                     \
        }                                                                            \
    }

void vu_clip_corners(inout float4 p[VU_CUT_CORNERS], inout uint n, float4 plane)
{
    float4 q[VU_CUT_CORNERS] = p;
    uint m = 0u;
    const uint nc = min(n, VU_CUT_CORNERS);
    VU_CLIP_STEP(0u)
    VU_CLIP_STEP(1u)
    VU_CLIP_STEP(2u)
    VU_CLIP_STEP(3u)
    VU_CLIP_STEP(4u)
    VU_CLIP_STEP(5u)
    VU_CLIP_STEP(6u)
#undef VU_CLIP_STEP
    p = q;
    n = m;
}

// Whether what the GPU draws of the triangle with clip-space corners a, b
// and c (vu_cut_position) reaches into the rectangle with the edges e
// (vu_picture_ndc's form): the triangle cut to 0 <= z <= w as the GPU
// clips it and to e's top and bottom, its x extent met with e's columns
// (vu_reaches_columns). Cut to the rows first: a triangle whose part
// within the picture's columns lies wholly above or below the picture
// (only sky over it) but that covers frame pixels beside the picture lies
// wholly beside it, and is drawn there. A triangle cut away whole reaches
// nothing. The corners keep w >= 0 after the z cut, so the row planes cut
// in homogeneous form.
bool vu_cut_reaches(float4 a, float4 b, float4 c, float4 e)
{
    float4 p[VU_CUT_CORNERS] = {a, b, c, c, c, c, c};
    uint n = 3u;
    vu_clip_corners(p, n, float4(0.0, 0.0, 1.0, 0.0));
    vu_clip_corners(p, n, float4(0.0, 0.0, -1.0, 1.0));
    vu_clip_corners(p, n, float4(0.0, -1.0, 0.0, e.z)); // y <= top * w
    vu_clip_corners(p, n, float4(0.0, 1.0, 0.0, -e.w)); // y >= bottom * w
    float lo = 1e30;
    float hi = -1e30;
    const uint nc = min(n, VU_CUT_CORNERS);
    // a corner left at w = 0 (z = 0 there too) lies at infinity on its x's side
#define VU_CUT_EXTENT(I)                                                              \
    if ((I) < nc) {                                                                   \
        float x = p[I].w > 0.0 ? p[I].x / p[I].w : (p[I].x >= 0.0 ? 1e30 : -1e30);    \
        lo = min(lo, x);                                                              \
        hi = max(hi, x);                                                              \
    }
    VU_CUT_EXTENT(0u)
    VU_CUT_EXTENT(1u)
    VU_CUT_EXTENT(2u)
    VU_CUT_EXTENT(3u)
    VU_CUT_EXTENT(4u)
    VU_CUT_EXTENT(5u)
    VU_CUT_EXTENT(6u)
#undef VU_CUT_EXTENT
    return n >= 3u && vu_reaches_columns(lo, hi, e);
}

// Whether it reaches into the camera's picture (vu_picture_ndc)
bool vu_cut_reaches_picture(float4 a, float4 b, float4 c)
{
    return vu_cut_reaches(a, b, c, vu_picture_ndc());
}

VuVSOut vu_out_init()
{
    VuVSOut o;
    o.pos = VU_CULLED;
    o.col = float4(0.0, 0.0, 0.0, 0.0);
    o.stq = float3(0.0, 0.0, 1.0);
    o.plane = 1.0;
    o.a0 = o.a1 = o.x0 = o.x1 = o.y0 = o.y1 = o.r = float4(0.0, 0.0, 0.0, 0.0);
    return o;
}

// ------------------------------------------------- the attribute planes
// The GPU interpolates a varying from the pixel's place in the target: its
// float rounding depends on how far the triangle sits from the target's
// corner, so at a frame width other than 512 the 4:3 columns' Gouraud
// colours and STs come out one step apart on some pixels (rd_native_test:
// up to 2000 pixels at 2x with the GPU's interpolation, the coverage the
// same).  A triangle drawn at its GS positions carries its own planes
// instead, made from its three corners in texels from the target's
// centre (the same numbers at every width: a frame-wide target's centre is
// GS 2048), and the pixel shader evaluates them at the pixel's offset from
// the first corner (exact: pixel centres are k + 0.5, corners on the 1/16
// grid at an integer scale): the same operations on the same values at
// every width.  The GS's own colours and STs run linearly in screen space
// (noperspective), which the planes are.  A triangle with a corner the GPU
// clips (vu_cut_position's homogeneous form: behind the eye, saturated,
// outside the Z range) keeps the GPU's interpolation (the clip changes the
// corners).

// GS pixels to texels from the target's centre (gs_px_to_ndc's q: exact
// on the 1/16 grid at an integer scale)
float2 vu_texel_offset_px(float2 px)
{
    return gs_texel_offset(px);
}

// A corner's position in texels from the target's centre, on the 12.4 grid
// (vu_vtx_position's: unquantised under ICO_RD_VU_OFFGRID)
float2 vu_texel_offset(VuVtx v)
{
    return vu_texel_offset_px(g_z.w != 0.0 ? v.p.xy : float2(v.gs.xy) * (1.0 / 16.0));
}

// The gradient basis of the triangle with corners pa, pb, pc: for values
// va, vb, vc the change per texel across is (vb - va) g.x + (vc - va) g.y,
// down (vb - va) g.z + (vc - va) g.w.  Zero for a triangle without area.
float4 vu_grad_basis(float2 pa, float2 pb, float2 pc)
{
    precise float2 d1 = pb - pa;
    precise float2 d2 = pc - pa;
    precise float det = d1.x * d2.y - d1.y * d2.x;
    if (det == 0.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    precise float4 g = float4(d2.y, -d1.y, -d2.x, d1.x) / det;
    return g;
}

// the planes of col and stq (o.r.z 1) of the triangle with corners pa, pb,
// pc (texels from the target's centre) and those values there; only under
// the exact viewport
VuVSOut vu_attr_planes_at(VuVSOut o, float2 pa, float2 pb, float2 pc, float4 ca, float4 cb,
                          float4 cc, float3 sa, float3 sb, float3 sc)
{
    if (g_vp.x == 0.0) {
        return o;
    }
    float4 g = vu_grad_basis(pa, pb, pc);
    precise float4 db = cb - ca;
    precise float4 dc = cc - ca;
    precise float3 eb = sb - sa;
    precise float3 ec = sc - sa;
    precise float4 cx = db * g.x + dc * g.y;
    precise float4 cy = db * g.z + dc * g.w;
    precise float3 sx = eb * g.x + ec * g.y;
    precise float3 sy = eb * g.z + ec * g.w;
    o.a0 = ca;
    o.a1 = float4(sa, 0.0);
    o.x0 = cx;
    o.y0 = cy;
    o.x1 = float4(sx, 0.0);
    o.y1 = float4(sy, 0.0);
    o.r = float4(pa, 1.0, 0.0);
    return o;
}

// The corner the planes are anchored at (vu_attr_planes_at's pa, whose
// values a0 and a1 are): 0, 1 or 2 corners on from the first.  The first
// when it has a GS position (vu_has_gs_position); else the other corner
// with one nearer the target's centre (the first again when neither has
// one).  The pixel's values are a + x d.x + y d.y at its offset d from
// the anchor.  Anchored at a corner without a GS position (a wall triangle
// of st18a_p1 above the courtyard's doorways whose top corner stands
// 60,000 GS pixels up with its Z saturated by ftoi4, code 36's x/y-only
// cut: kicked as it is, vu_triangle_out_pos) the terms would be thousands
// of times their sum, which float32 keeps to a couple of texels of the
// 128-texel brick, changing with the camera: the triangle's texture would
// slide against its neighbours' as the camera moves.
// Nearer the centre the terms are the triangle's own size.  The GS never
// had such a corner (the VU's scissor cut it into a fan, or ftoi4
// saturated it); a triangle whose first corner has a GS position keeps it,
// and its planes are unchanged.
uint vu_plane_anchor(VuVtx a, VuVtx b, VuVtx c)
{
    if (vu_has_gs_position(a)) {
        return 0u;
    }
    precise float2 cc = g_origin.xy + g_target.xy * 0.5;
    float db = vu_has_gs_position(b) ? length(float2(b.gs.xy) * (1.0 / 16.0) - cc) : 1e30;
    float dc = vu_has_gs_position(c) ? length(float2(c.gs.xy) * (1.0 / 16.0) - cc) : 1e30;
    if (db >= 1e30 && dc >= 1e30) {
        return 0u;
    }
    return db <= dc ? 1u : 2u;
}

// the planes of a triangle every corner of which is drawn at its GS
// position (exact), anchored at vu_plane_anchor's corner; the plane
// distance's are vu_plane_out's (vu_plane_planes)
VuVSOut vu_attr_planes(VuVSOut o, VuVtx a, VuVtx b, VuVtx c, bool exact)
{
    if (!exact) {
        return o;
    }
    uint k = vu_plane_anchor(a, b, c);
    if (k == 1u) {
        VuVtx t = a;
        a = b;
        b = c;
        c = t;
    } else if (k == 2u) {
        VuVtx t = c;
        c = b;
        b = a;
        a = t;
    }
    return vu_attr_planes_at(o, vu_texel_offset(a), vu_texel_offset(b), vu_texel_offset(c),
                             float4(a.rgba), float4(b.rgba), float4(c.rgba), a.stq, b.stq, c.stq);
}

// the plane distance's plane (da, db, dc at the corners), where vu_attr_planes made the others
VuVSOut vu_plane_planes(VuVSOut o, VuVtx a, VuVtx b, VuVtx c, float da, float db, float dc)
{
    if (o.r.z == 0.0) {
        return o;
    }
    uint k = vu_plane_anchor(a, b, c);
    if (k == 1u) {
        VuVtx t = a;
        a = b;
        b = c;
        c = t;
        float td = da;
        da = db;
        db = dc;
        dc = td;
    } else if (k == 2u) {
        VuVtx t = c;
        c = b;
        b = a;
        a = t;
        float td = dc;
        dc = db;
        db = da;
        da = td;
    }
    float4 g = vu_grad_basis(vu_texel_offset(a), vu_texel_offset(b), vu_texel_offset(c));
    precise float x = (db - da) * g.x + (dc - da) * g.y;
    precise float y = (db - da) * g.z + (dc - da) * g.w;
    o.a1.w = da;
    o.x1.w = x;
    o.y1.w = y;
    return o;
}

// The pixel's col, stq and plane from the planes where the triangle has
// them (else the GPU's): at the pixel's offset from the first corner in
// texels, pos.xy less the target's centre (exact: both on a fine grid).
VuVSOut vu_pixel_attrs(VuVSOut i)
{
    if (i.r.z == 0.0) {
        return i;
    }
    float2 d = gs_plane_offset(i.pos, i.r.xy);
    precise float4 col = i.a0 + i.x0 * d.x + i.y0 * d.y;
    precise float4 sp = i.a1 + i.x1 * d.x + i.y1 * d.y;
    i.col = col;
    i.stq = sp.xyz;
    i.plane = sp.w;
    return i;
}

// A clip plane in the model's space (port/render/rd_chain.h, Natural
// chains: a chain's link drawn at its size, the part still at the top not
// drawn): vu_batch.z = 0 for none, else the axis + 1 in bits 0..1 and bit 2
// for the minus side, vu_batch.w the offset's bits.  The distance of
// vertex v (its stream position, qword 0 of a prelit or lit vertex) is the
// coordinate on the axis, negated for the minus side, plus the offset; the
// pixels where it is negative are discarded (vu_pixel, DF_PLANE).  Like
// every VU varying it runs linearly in screen space, as the GS's colours
// and STs do; a link's triangles are a few pixels long.
float vu_plane_distance(uint v)
{
    float4 p = vu_stream[vu_vertex_qw(v)];
    uint axis = (vu_batch.z & 3u) - 1u;
    float c = axis == 0u ? p.x : (axis == 1u ? p.y : p.z);
    precise float d = ((vu_batch.z & 4u) != 0u ? -c : c) + asfloat(vu_batch.w);
    return d;
}

// The plane on a drawn triangle k-2, k-1, k (me: the corner output): its
// distance at the corner, and the triangle not drawn when all three are
// past the plane.
VuVSOut vu_plane_out(VuVSOut o, VuVtx a, VuVtx b, VuVtx c, uint kick, uint me)
{
    if (vu_batch.z == 0u) {
        return o;
    }
    float da = vu_plane_distance(kick - 2u), db = vu_plane_distance(kick - 1u);
    float dc = vu_plane_distance(kick);
    if (da < 0.0 && db < 0.0 && dc < 0.0) {
        o.pos = VU_CULLED;
    }
    o.plane = me == 0u ? da : (me == 1u ? db : dc);
    return vu_plane_planes(o, a, b, c, da, db, dc);
}

// What the port draws of a triangle of a program with the region test
// (vu_region_rule; vu1_ref.h vu1ref_region_draw is the CPU mirror).
#define VU_RULE_EXACT 0u // all three vertices inside: at their GS positions
#define VU_RULE_CLIP 1u  // GPU-clipped at the native frustum (vu_cut_position)
#define VU_RULE_DROP 2u  // not drawn
// The card test decides (the caller's): VU_RULE_DROP for a triangle of a
// card, a free-standing strip (no VU_INDEX_JOINED) none of whose
// triangles passes the region test (vu_*_strip_passes), else VU_RULE_CLIP
#define VU_RULE_STRIP 3u

// The region rule (docs/port/VU1_PROGRAMS.md, "The region rule").
// On the PS2 any vertex outside the region sets ADC on itself and the next
// two, and the triangle is not drawn. That test is the camera's own: the
// program's, on the position as computed (VuVtx.inside, the tests' probe
// field), and the projection is the same at every aspect. The rule, in
// this order:
// - all three vertices inside: drawn exactly (vu_vtx_position, on the
//   ftoi4 grid);
// - photo mode's free camera (VU_F_BEHIND_EYE): drawn, clipped by the GPU
//   at the native frustum (else a walkway low under the camera leaves a
//   hole);
// - a triangle whose GPU-clipped extent (0 <= z <= w, a vertex behind the
//   eye or not) lies wholly beside the camera's picture (GS 2048 -+ 256
//   across by the native height, the drawing camera's,
//   vu_cut_reaches_picture; seen only on a frame wider than 4:3): drawn,
//   clipped (there the PS2 showed nothing, and dropped triangles would
//   leave holes: a wall with one vertex past the window; at 48:9 a
//   patch of the sea at the far left of the old bridge whose only
//   triangles there have a vertex behind the eye);
// - one that reaches into the picture, out of the box (the Original preset
//   at 4:3 and 1x, VU_F_EXTENDED clear): not drawn, as the PS2 does not
//   draw it. At 4:3 the frame is the picture, so every failing triangle
//   that could show reaches it, and the picture is the PS2's;
// - one that reaches into the picture in any other state (VU_F_EXTENDED:
//   Enhanced, wider than 4:3, or scaled up), every vertex in front of the
//   eye: such a picture shows everything in the camera, as the game would
//   look had it been made for that aspect ratio and resolution. A game
//   made for that picture clips such a triangle, so it is drawn, clipped,
//   unless it
//   belongs to an artist's card parked at the picture's edge: a
//   free-standing strip (the run of triangles the strip flag ties together,
//   vu_strip_of: one strip of the model, pac_makeNormalStrip; sharing no
//   vertex position with another strip of its mesh but a double-sided
//   copy, no VU_INDEX_JOINED, rd_mesh.c markJoinedStrips) none of whose
//   triangles pass the test. The opening's tree card (st26a_near, a
//   free-standing 2-triangle strip of a 12-card draw, every vertex 160 to
//   540 units in front of the eye) and the waterfall's mist cards stay out
//   of the picture, as out of the box. A wall is drawn: its
//   strip passes (the water tower's lift wall, st20a_p2 part 28,
//   packet 4, strip 14, two of its six triangles pass) or is joined to the
//   rest of the surface (the old bridge's plank faces at 48:9,
//   st08a_p4). The unit is not the draw: the tree card's draw has 7
//   passing triangles in its other cards;
// - one with a vertex behind the eye that reaches into the picture: not
//   drawn in any state (the opening's cliff st26a_p1, filmed from inside):
//   a game made for a wide picture would not draw geometry behind the eye
//   either. None is cut at the picture's edge (the waterfall's mist).
// A drawn triangle with a vertex outside takes code 36's corner rule
// (vu_cut_position: the GS position where the vertex has one, the
// homogeneous form of h where it is behind the eye, saturated or below GS
// Z 0; every program computes h before the test). A HIDE triangle never
// reaches here (vu_content_hidden); the strip test counts every triangle
// of the strip, marked or not. A grid (vu_grid.hlsl, Mesh3D) has a batch
// a strip, each the rows i and i + 1 of one vertex grid
// (Primitive.c prim_UpdateMesh3D), so neighbouring strips share a row of
// positions: rd_replay.c marks every triangle of a grid of two strips or
// more VU_INDEX_JOINED; a grid of one strip is a card when none of its
// triangles pass.
// Two differences from a code-36 draw of the same triangle remain: code
// 36's clipped triangles go through their own earlier pass with ABE forced
// on (rd_replay.c draws them as the VU's fans, PRIM 0x5D), which a code-32
// draw does not, so a mesh whose clip code flips between 32 and 36 still
// changes the blending along its cut rim; and vu_later_out's bias needs
// all three GS positions, so a later-marked triangle with a corner
// saturated or below GS Z 0 is drawn without it.
uint vu_region_rule_at(VuVtx a, VuVtx b, VuVtx c)
{
    if (a.inside && b.inside && c.inside) {
        return VU_RULE_EXACT;
    }
    if ((vu_draw.z & VU_F_BEHIND_EYE) != 0u ||
        !vu_cut_reaches_picture(vu_cut_position(a), vu_cut_position(b), vu_cut_position(c))) {
        return VU_RULE_CLIP;
    }
    if ((vu_draw.z & VU_F_EXTENDED) != 0u && vu_strip.y != 0u &&
        min(a.h.w, min(b.h.w, c.h.w)) > 0.0) {
        return VU_RULE_STRIP;
    }
    return VU_RULE_DROP;
}

// The vertex as tick k's camera gave it (0 the tick before, 1 this tick):
// h_k = R_k h, R_k = S_k St^-1 (vu_ticks), then the program's divide and
// its region test with its bounds lo, hi (vu_inside).  h is the vertex
// through the blended camera at the object's blended pose (rd_interp.c
// blends a matched draw's model matrix with the camera), so this is the
// tick's camera looking at the blended pose: only the camera is moved
// back to the tick, never the object (an object moving between the ticks
// is tested where it is drawn).
VuVtx vu_tick_vertex(VuVtx v, uint k, float4 lo, float4 hi)
{
    VuVtx o = vu_vtx_init();
    uint b = 4u * k;
    precise float4 h = vu_ticks[b] * v.h.x + vu_ticks[b + 1u] * v.h.y + vu_ticks[b + 2u] * v.h.z +
                       vu_ticks[b + 3u] * v.h.w;
    o.h = h;
    vu_divide(o);
    o.inside = vu_inside(o.p, lo, hi);
    return o;
}

// The region rule of the triangle a, b, c (vu_region_rule_at), whose
// program's region test has the bounds lo, hi.  The PS2
// drew only the ticks' pictures, each with the rule at its own camera; the
// port also draws pictures between two ticks, through a camera blended
// between theirs (rd_interp.c camSetup: the eye on the straight line
// between the two eyes, the turn slerped).  A triangle beside the camera,
// close to it, in front of the eye at both ticks, can have a vertex behind
// the blended camera's eye: the rule at that camera drops it (the vertex
// behind the eye), and the wall it belongs to would open for the pictures
// between the ticks and close at the tick (st18a,
// gsb_cull_test --stage DISC --eye: on a yaw orbit 500 units out, 200 a
// tick, holes of 15636 of the 688 x 448 native pixels at 16:9, 35786 of
// 2048 x 448 at 48:9; the same rule drops some triangles that are beside
// the picture at a tick, drawn there, too).  So under VU_F_TICKS a
// triangle the rule drops at the blended camera is drawn, clipped by the
// GPU at the native frustum, when a tick SHOWED it: the rule draws it at
// that tick's camera (exactly or clipped) and what the GPU draws of it
// there reaches the frame (vu_frame_ndc, the bound target: at 4:3 the
// picture, so a triangle wholly beside the picture at a tick, drawn there
// off the frame, was never seen and does not count; on a wider frame the
// bands beside the picture count).  A triangle neither tick showed stays
// dropped: between the ticks the port shows nothing the PS2 hid at both.
// When the rule would go to the card test at a showing tick, the card
// test decides (at the blended camera, as when the blended camera's own
// verdict is the card test: VU_RULE_STRIP is returned as it is, so a card
// never enters the picture between the ticks either).  The PS2's rule is
// unchanged at every tick's own picture (no VU_F_TICKS: t = 0, the
// Original framerate, a still camera) and under the free camera.
// vu1_ref.h vu1ref_region_between is the CPU mirror.
uint vu_region_rule(VuVtx a, VuVtx b, VuVtx c, float4 lo, float4 hi)
{
    uint r = vu_region_rule_at(a, b, c);
    if (r != VU_RULE_DROP || (vu_draw.z & VU_F_TICKS) == 0u) {
        return r;
    }
    float4 frame = vu_frame_ndc();
    uint at = VU_RULE_DROP;
    [loop] for (uint k = 0u; k < 2u; k++)
    {
        VuVtx ak = vu_tick_vertex(a, k, lo, hi);
        VuVtx bk = vu_tick_vertex(b, k, lo, hi);
        VuVtx ck = vu_tick_vertex(c, k, lo, hi);
        uint rk = vu_region_rule_at(ak, bk, ck);
        if (rk == VU_RULE_DROP ||
            !vu_cut_reaches(vu_cut_position(ak), vu_cut_position(bk), vu_cut_position(ck), frame)) {
            continue; // not shown at tick k
        }
        if (rk != VU_RULE_STRIP) {
            return VU_RULE_CLIP;
        }
        at = VU_RULE_STRIP;
    }
    return at;
}

// The strip test's longest scan, in vertices each way: a strip of the
// game's models has at most 64 (Packet.c pac_checkDivide: 192 qwords a
// batch, a batch holds whole strips); a longer one (a replacement model's,
// modelpack.c stripify) counts as having a passing triangle (not a card).
#define VU_STRIP_SCAN 256u

// Whether vertex v starts a strip (ST.w < 1, the strip flag; ST is the
// second-last quadword of a normal_c, normal_l and cluster vertex,
// rd_mesh.c batchIndices). flagged: the program reads the flag (mesh does
// not: a batch is one strip).
bool vu_strip_start(uint v, bool flagged)
{
    return flagged && vu_stream[vu_vertex_qw(v) + vu_draw.y - 2u].w < 1.0;
}

// The strip of kick k, as the static kicks see it (vu1_ref.h
// vu1ref_static_kicks): x its first vertex, y one past its last, z 1 when
// it is longer than VU_STRIP_SCAN. Its triangles are the kicks x + 2 ..
// y - 1, every one drawn (no vertex after x starts a strip). Bounded by
// the draw's vertices (vu_strip.xy) and, in a stream with batches
// (vu_draw.w), by k's batch; a tagless mesh's batch starts a strip
// (pac_makeNormalStrip and pac_makeClusterStrip write ST.w 0 on every
// strip's first vertex, modelpack.c on every one it starts).
uint3 vu_strip_of(uint k, bool flagged)
{
    uint lo = vu_strip.x;
    uint hi = vu_strip.y;
    uint vpb = vu_draw.w;
    if (vpb != 0u) {
        uint b0 = (k / vpb) * vpb;
        lo = max(lo, b0);
        hi = min(hi, b0 + vpb);
    }
    if (!flagged) {
        return uint3(lo, hi, 0u);
    }
    // k and k - 1 start nothing (k is a kick): the start is at k - 2 or before
    uint s = k - 2u;
    uint n = 0u;
    [loop] while (s > lo && !vu_strip_start(s, true) && n < VU_STRIP_SCAN)
    {
        s--;
        n++;
    }
    uint e = k + 1u;
    uint m = 0u;
    [loop] while (e < hi && !vu_strip_start(e, true) && m < VU_STRIP_SCAN)
    {
        e++;
        m++;
    }
    return uint3(s, e, n >= VU_STRIP_SCAN || m >= VU_STRIP_SCAN ? 1u : 0u);
}

// The most vertices a strip test walks (vu_strip_of's x .. y): a flagged
// strip spans at most 2 * VU_STRIP_SCAN + 3 (the scans stop there), a
// grid's strip is a batch of Mesh3D.stripLen vertices (rd_mesh.h
// RdVuGridDraw), which VU1's 1024 data quadwords hold at two or three a
// vertex. The loops stop at vu_strip_end, so a bound the GPU computed
// wrong cannot run them past VU_STRIP_SPAN_MAX vertices (when x + the
// span wraps the end is below the start and nothing runs).
#define VU_STRIP_SPAN_MAX 1024u

uint vu_strip_end(uint3 r)
{
    return min(r.y, r.x + VU_STRIP_SPAN_MAX);
}

// The triangle k-2, k-1, k: what the GS draws of it (vu_triangle_out),
// without the planes; exact: every corner drawn at its GS position.
VuVSOut vu_triangle_out_pos(VuVtx a, VuVtx b, VuVtx c, VuVtx me, uint mode, uint rule,
                            out bool exact)
{
    VuVSOut o = vu_out_init();
    o.col = float4(me.rgba);
    o.stq = me.stq;
    // every corner at its GS position (vu_attr_planes): the corners the
    // GPU clips have none
    bool gsPos = vu_has_gs_position(a) && vu_has_gs_position(b) && vu_has_gs_position(c);
    exact = false;
    if (mode == VU_CLIP_NONE) {
        // vu_vtx_position, as codes 32, 36 and 38 draw (on the 12.4 grid,
        // or off it under ICO_RD_VU_OFFGRID): a triangle placed differently
        // from the same triangle drawn by them would sit up to 1/16 GS pixel
        // away, which on a sloped surface moves its depth one way over the
        // whole triangle, so a coplanar pass by another program (the
        // reflection pass, 38) would fail or pass GEQUAL wholesale as the
        // camera moves.  A vertex whose X or Y the 16-bit wrap would change
        // keeps the wrapped GS value.
        bool wrapped = any((me.gs.xy & 0xFFFF) != me.gs.xy);
        o.pos = wrapped ? vu_gs_position(me.gs, true) : vu_vtx_position(me);
        exact = !any((a.gs.xy & 0xFFFF) != a.gs.xy) && !any((b.gs.xy & 0xFFFF) != b.gs.xy) &&
                !any((c.gs.xy & 0xFFFF) != c.gs.xy);
        return o;
    }
    if (mode == VU_CLIP_REGION) {
        // the region rule (vu_region_rule)
        if (rule == VU_RULE_EXACT) {
            o.pos = vu_vtx_position(me);
            exact = true;
        } else if (rule == VU_RULE_CLIP) {
            o.pos = vu_cut_position(me);
            exact = gsPos;
        }
        return o;
    }
    // VU_CLIP_SCISSOR, the PS2's (RegistPacket.c also gives it the packets
    // whose own box fails)
    uint fa = vu_clipw(a.clip), fb = vu_clipw(b.clip), fc = vu_clipw(c.clip);
    bool cut = (fa | fb | fc) != 0u;
    if ((cut && (vu_draw.z & VU_F_KICK_ONLY) != 0u) ||
        (!cut && (vu_draw.z & VU_F_CUT_ONLY) != 0u)) {
        return o; // drawn by the other pass
    }
    if (!cut) {
        o.pos = vu_vtx_position(me); // kicked as it is
        exact = true;
    } else if ((fa & fb & fc) != 0u) {
        return o; // trivially rejected (the six fcor tests)
    } else if (((fa | fb | fc) & 0x30u) != 0u || min(a.h.w, min(b.h.w, c.h.w)) <= 0.0) {
        // SCISSOR_COMMON; the GPU clips (at 0 <= z <= w, not the VU's
        // z = -w: that would need SV_ClipDistance)
        o.pos = vu_cut_position(me);
        exact = gsPos;
    } else {
        // only x/y flags: the VU clips to its guard band (the 1500 unit
        // clip window, wider than any target), the GS scissor does the rest
        o.pos = vu_vtx_position(me);
        exact = true;
    }
    return o;
}

// The triangle k-2, k-1, k: what the GS draws of it, with its attribute
// planes (vu_attr_planes). me is the corner this invocation outputs.
// mode: VU_CLIP_*; the programs without a scissor or no-test variant pass
// VU_CLIP_REGION whatever vu_draw.z says. rule: VU_CLIP_REGION's decision,
// vu_region_rule with VU_RULE_STRIP resolved (the others ignore it).
VuVSOut vu_triangle_out(VuVtx a, VuVtx b, VuVtx c, VuVtx me, uint mode, uint rule)
{
    bool exact;
    VuVSOut o = vu_triangle_out_pos(a, b, c, me, mode, rule, exact);
    return vu_attr_planes(o, a, b, c, exact);
}

// A triangle marked VU_INDEX_LATER and the earlier one it
// overlaps are one plane in the model (the two faces of the railing's
// lattice, split along other diagonals), where an exact GS's GEQUAL would
// let the later pass over the earlier at every pixel.  ftoi4 rounds each
// vertex's X, Y and Z on its own, so two triangulations of a plane differ
// in depth by up to (|dZ/dX| + |dZ/dY|) / 8 + 2 GS units, either way, and
// which one wins would flip region by region with sub-pixel camera motion
// (a shimmer on the near railing).  The later triangle's depth rises by
// twice that bound (the gradient from its own GS vertices) plus 512 GS units (two
// steps of a 24-bit depth buffer) and Z / 2^20 (float steps), at most Z /
// 256 (or 1024 units), the same in every program and pass that draws it.
// Only when all three vertices have their GS position (not a triangle the
// GPU clips at the eye) and Z is below the UI's top band.
float vu_later_bias(VuVtx a, VuVtx b, VuVtx c)
{
    precise float3 pa = float3(float2(a.gs.xy) * (1.0 / 16.0), float(asuint(a.gs.z)));
    precise float3 pb = float3(float2(b.gs.xy) * (1.0 / 16.0), float(asuint(b.gs.z)));
    precise float3 pc = float3(float2(c.gs.xy) * (1.0 / 16.0), float(asuint(c.gs.z)));
    precise float3 e1 = pb - pa;
    precise float3 e2 = pc - pa;
    precise float nx = e1.y * e2.z - e1.z * e2.y;
    precise float ny = e1.z * e2.x - e1.x * e2.z;
    precise float nz = abs(e1.x * e2.y - e1.y * e2.x);
    precise float zmax = max(pa.z, max(pb.z, pc.z));
    precise float cap = max(zmax * (1.0 / 256.0), 1024.0);
    // (|dZ/dX| + |dZ/dY|) / 4 = (|nx| + |ny|) / (4 |nz|), or the cap for a
    // triangle seen edge on
    precise float slope = (abs(nx) + abs(ny)) * 0.25;
    precise float bias = slope < cap * nz ? slope / nz : cap;
    precise float r = min(bias + 512.0 + zmax * (1.0 / 1048576.0), cap);
    return r;
}

VuVSOut vu_later_out(VuVSOut o, VuVtx a, VuVtx b, VuVtx c)
{
    uint zmax = max(asuint(a.gs.z), max(asuint(b.gs.z), asuint(c.gs.z)));
    if (vu_has_gs_position(a) && vu_has_gs_position(b) && vu_has_gs_position(c) &&
        zmax < 0xF0000000u) {
        precise float z = o.pos.z + vu_later_bias(a, b, c) * g_z.x * o.pos.w;
        o.pos.z = z;
    }
    return o;
}

// The probe layout of the tests: SV_VertexID = (vertex * 16 + field) * 3 +
// corner draws a one-pixel triangle at (field, vertex) of a 16-wide
// RGBA8_UINT target carrying one 32-bit value (vu_probe_ps). The value rides
// in col.xy as two 16-bit integers (a constant interpolates back to itself
// within far less than 0.5), so the shipped pipelines need no extra varying.
uint vu_probe_vertex(uint vid)
{
    return (vid / 3u) / VU_PROBE_FIELDS;
}

VuVSOut vu_probe_out(uint vid, uint value)
{
    VuVSOut o = vu_out_init();
    uint cell = vid / 3u;
    uint corner = vid % 3u;
    float2 px = float2(float(cell % VU_PROBE_FIELDS), float(cell / VU_PROBE_FIELDS));
    px += corner == 1u ? float2(1.5, 0.0) : (corner == 2u ? float2(0.0, 1.5) : float2(0.0, 0.0));
    // texel px of the target (under the exact viewport the target is the
    // share g_misc.zw of it about its centre: common.hlsli fullscreen_triangle)
    o.pos = g_vp.x != 0.0
                ? float4((px.x + g_vpo.x) * g_vp.x - g_misc.z, g_misc.w - (px.y + g_vpo.y) * g_vp.y,
                         0.5, 1.0)
                : float4(px.x * g_target.z * 2.0 - 1.0, 1.0 - px.y * g_target.w * 2.0, 0.5, 1.0);
    o.col = float4(float(value & 0xFFFFu), float(value >> 16), 0.0, 0.0);
    return o;
}

// Fields 0..8 of a mesh vertex probe.
uint vu_probe_field(VuVtx v, uint field)
{
    uint r = 0u;
    if (field == 0u) {
        r = asuint(v.gs.x);
    } else if (field == 1u) {
        r = asuint(v.gs.y);
    } else if (field == 2u) {
        r = asuint(v.gs.z);
    } else if (field == 3u) {
        r = v.rgba.x | (v.rgba.y << 8) | (v.rgba.z << 16) | (v.rgba.w << 24);
    } else if (field == 4u) {
        r = asuint(v.stq.x);
    } else if (field == 5u) {
        r = asuint(v.stq.y);
    } else if (field == 6u) {
        r = asuint(v.stq.z);
    } else if (field == 7u) {
        r = v.inside ? 1u : 0u;
    } else if (field == 8u) {
        r = vu_clipw(v.clip);
    }
    return r;
}

// ------------------------------------------------------------ pixel side

Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
Texture2D<float> g_dateSnap : register(t2, space2);

// S/Q, T/Q: the texture's normalised UV.  On a Full pixel crop of the
// frame (g_scale.z set, rd_replay.c fillDrawCB) the STs, normalised to the
// target's GS size (g_tex.x), address its texture at its scale over its
// width (g_tex.z) less its left-out share.
float2 vu_uv(float3 stq)
{
    float2 uv = stq.xy / stq.z;
    if (g_scale.z != 0.0) {
        uv.x = uv.x * (g_tex.x * g_tex.z) - g_scale.z;
    }
    return uv;
}

// sprite_ps with STQ: the texel at S/Q, T/Q (the texture is padded to
// 2^TW x 2^TH, so STQ is normalised already; a texture with GS levels at
// the GS's level by Q, gs_sample), texture function, TEXA, alpha test,
// DATE; the entry (vu_prelit.hlsl) writes the result (GS_RETURN).
uint4 vu_pixel(VuVSOut i)
{
    i = vu_pixel_attrs(i);
    if ((g_mode.x & DF_PLANE) != 0u && i.plane < 0.0) {
        GS_DISCARD;
    }
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        float2 uv = vu_uv(i.stq);
        uint4 t = uint4(floor(gs_sample(g_texture, g_sampler, uv, i.stq.z) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    return col;
}

// vu_texa_ps: vu_pixel with TEXA per texel before the
// bilinear weights (gs_texa_texture), for the draws rd_replay.c's VU
// planner gives it (the texture formats and TEXA modes of sprite_texa_ps).
uint4 vu_pixel_texa(VuVSOut i)
{
    i = vu_pixel_attrs(i);
    if ((g_mode.x & DF_PLANE) != 0u && i.plane < 0.0) {
        GS_DISCARD;
    }
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            GS_DISCARD;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        float2 uv = vu_uv(i.stq);
        uint4 t = (g_mode.x & DF_GS_LOD) != 0u
                      ? gs_texa_texture_lod(g_texture, uv, i.stq.z, g_mode.x)
                      : gs_texa_texture(g_texture, g_sampler, uv, g_mode.x);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    return col;
}

#endif
