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
//   b3  VuBoneCB   cluster: VU memory 16..255, the bones; every mesh
//                  program: the card bits after them (vu_strip_card)
//
// Drawing: the mesh programs are drawn as an indexed triangle list whose
// index value (SV_VertexID; vertexOffset / BaseVertexLocation must be 0) is
// kick * 4 + corner, nothing else (the marks are bytes in the stream,
// vu_kick_marks): kick = the vertex index k whose XYZ2 draws the
// triangle k-2, k-1, k on the GS, corner 0..2 the vertex k-2+corner. Each
// invocation evaluates all three vertices so the per-triangle
// decisions (region test, scissor) come out the same in all three. The
// particle program is drawn non-indexed, six vertices per particle.
//
// The forms (docs/port/SHADERS.md, The envelope): the vertex shaders are
// written as v0.5.2's, the last set that built on every device the port
// has failed on: scalar comparisons joined with &&, || and ! (which
// HLSL 2021 turns into branches), ifs, and ?: between numbers; no vector
// comparison, no any() or all(), no select(), no choice between booleans
// and no choice of a whole vector (the optimiser makes a choice between
// two vectors computed before it into a select on a vector of booleans: a
// vector is chosen component by component, or computed in its branch);
// flags that are combined after they are computed are uints (1 or 0), as
// vu_clipw's.  No loop, no dot(), no logical-not-equal, no undefined value,
// no local array, no array-typed value.  tools/check_spirv.py (the ctest
// spirv_check) holds every module to it: no vector of booleans, no
// any()/all(), no select with a boolean result.
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
// vu_strip the region rule's card test (vu_strip_card): x the first kick the
//          card bits cover, y 1 when the rule has the card test (0: none,
//          the PS2's rule), z the kicks the bits cover (0: no bits, no
//          strip of the draw is a card), w the bits' first qword in VuBoneCB
// vu_ticks under VU_F_TICKS: the two ticks' cameras of a blended picture,
//          R = S_tick St^-1 (columns; 0..3 the tick before's, 4..7 this
//          tick's), a vertex's h through the blended camera St to the h the
//          tick's camera gave (vu_tick_vertex)
// vu_mark  the triangles' marks (vu_kick_marks): x the first qword of the
//          mark bytes after vu_draw.x, y the marks of every triangle of the
//          draw (a grid of two strips or more: VU_MARK_JOINED), z the mark
//          bytes' qwords (0: none)
cbuffer VuCB : register(b2, space1)
{
    float4 vu_mem[36];
    uint4 vu_draw;
    uint4 vu_batch;
    uint4 vu_strip;
    float4 vu_ticks[8];
    uint4 vu_mark;
};

// VU memory 16..255: the cluster program's bones (vu_skin.hlsl), and for
// every mesh program the card bits from qword vu_strip.w (vu_strip_card;
// rd_replay.c doVuDraw puts them after the bones a skinned mesh reads)
cbuffer VuBoneCB : register(b3, space1)
{
    float4 vu_bone[240];
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
// The marks of a triangle k-2, k-1, k (shader_consts.h ICO_VU_MARK_*,
// rd_mesh.c RdMeshRec.kickMark): a byte a kick after the mesh's vertices
// in the stream, where vu_mark says.
//   VU_MARK_JOINED  a triangle of a joined strip (rd_mesh.c
//                   markJoinedStrips; the draw's own on a grid of two
//                   strips or more): not a card (vu_rule_card)
//   VU_MARK_HIDE    the content table (port/render/rd_content_hide.c,
//                   rd_mesh.c markContentHidden): a triangle of an artist
//                   trick made for the 4:3 picture, drawn nowhere (static
//                   prelit and lit meshes)
//   VU_MARK_LATER   a triangle that overlaps an earlier triangle of its
//                   mesh in the same plane (static prelit and lit meshes,
//                   rd_mesh.c markLaterOverlaps; vu_later_out)
#define VU_MARK_JOINED 1u
#define VU_MARK_HIDE 2u
#define VU_MARK_LATER 4u

// The marks of the triangle of kick k: the draw's own, and its byte when
// the draw has mark bytes (sixteen kicks a qword, four a word; the qword
// clamped into the bytes the draw bound)
uint vu_kick_marks(uint kick)
{
    uint m = vu_mark.y;
    if (vu_mark.z != 0u) {
        uint4 q = asuint(vu_stream[vu_draw.x + vu_mark.x + min(kick >> 4, vu_mark.z - 1u)]);
        uint s = (kick >> 2) & 3u;
        uint word = q.x;
        word = s == 1u ? q.y : word;
        word = s == 2u ? q.z : word;
        word = s == 3u ? q.w : word;
        m = m | ((word >> ((kick & 3u) * 8u)) & 0xFFu);
    }
    return m;
}

// Whether a triangle of these marks is left out of every draw (HIDE).
bool vu_content_hidden(uint marks)
{
    return (marks & VU_MARK_HIDE) != 0u;
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

// ftoi0 / ftoi4: truncation, saturating at +-2^31 like the VU (?: between
// numbers: the conversion of a value out of range is never the one taken).
int vu_ftoi(float x)
{
    int r = (int)x;
    r = x >= 2147483648.0 ? 0x7FFFFFFF : r;
    r = x <= -2147483648.0 ? (int)0x80000000u : r;
    return r;
}

int vu_ftoi4(float x)
{
    return vu_ftoi(x * 16.0);
}

// div q, vf00w, x: a zero (or denormal) divisor gives +-Fmax.
float vu_rcp(float x)
{
    precise float q = 1.0 / x;
    float fmax = asfloat((asuint(x) & 0x80000000u) | 0x7F7FFFFFu);
    return (asuint(x) & 0x7F800000u) == 0u ? fmax : q;
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
// x, y (p, after the divide) and w (the clip w); 1 inside, else 0.
uint vu_inside(float2 p, float w, float4 lo, float4 hi)
{
    return lo.x < p.x && lo.y < p.y && lo.w < w && p.x < hi.x && p.y < hi.y && w < hi.w ? 1u : 0u;
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
    float2 p;    // x and y divided by w (pixels)
    int gx, gy, gz; // ftoi4 of p.xyz: GS X, Y (12.4) and Z (scalars: no vector built
                    // whole whose lane nothing reads)
    uint4 rgba;  // RGBAQ, 0..255
    float3 stq;  // PACKED ST
    uint inside; // the region test: 1 inside (or no test), 0 outside
    float4 clip; // scissor programs: model to clip space (mem[20..23]) times pos
    uint gsOk;   // vu_has_gs_position
};

VuVtx vu_vtx_init()
{
    VuVtx o = (VuVtx)0;
    o.h = float4(0.0, 0.0, 0.0, 1.0);
    o.inside = 1u;
    o.clip = o.h;
    o.gsOk = 1u;
    return o;
}

// Whether the GS position of a vertex (clip w, ftoi4 X, Y, Z gs) is the one
// the GS would draw it at: in front of the eye (w > 0), GS Z not negative
// (no -z clip) and none of X, Y, Z saturated by ftoi4 (the values vu_ftoi
// writes); 1 or 0 (VuVtx.gsOk, vu_has_gs_position).
uint vu_gs_ok(float w, int gx, int gy, int gz)
{
    return w > 0.0 && gz >= 0 && gz != 0x7FFFFFFF && gx != 0x7FFFFFFF &&
                   gx != (int)0x80000000u && gy != 0x7FFFFFFF && gy != (int)0x80000000u
               ? 1u
               : 0u;
}

// The divide and the conversions every loop shares: q = 1 / h.w,
// p.xyz = h.xyz * q, ftoi4 (X, Y and Z: Z is p.z's).
float vu_divide(inout VuVtx o)
{
    float q = vu_rcp(o.h.w);
    precise float3 p = o.h.xyz * q;
    o.p = p.xy;
    o.gx = vu_ftoi4(p.x);
    o.gy = vu_ftoi4(p.y);
    o.gz = vu_ftoi4(p.z);
    o.gsOk = vu_gs_ok(o.h.w, o.gx, o.gy, o.gz);
    return q;
}

// The corner a triangle's invocation outputs, a, b or c by corner 0, 1, 2:
// chosen value by value (a whole vector chosen after it is computed would
// be a select on a vector of booleans)
float vu_pick1(uint corner, float a, float b, float c)
{
    float r = a;
    r = corner == 1u ? b : r;
    r = corner == 2u ? c : r;
    return r;
}

uint vu_pick1u(uint corner, uint a, uint b, uint c)
{
    uint r = a;
    r = corner == 1u ? b : r;
    r = corner == 2u ? c : r;
    return r;
}

VuVtx vu_pick(VuVtx a, VuVtx b, VuVtx c, uint corner)
{
    // value by value into the initial vertex (no vector built whole whose
    // lane nothing reads: the optimiser leaves an undefined value there)
    VuVtx o = vu_vtx_init();
    o.h.x = vu_pick1(corner, a.h.x, b.h.x, c.h.x);
    o.h.y = vu_pick1(corner, a.h.y, b.h.y, c.h.y);
    o.h.z = vu_pick1(corner, a.h.z, b.h.z, c.h.z);
    o.h.w = vu_pick1(corner, a.h.w, b.h.w, c.h.w);
    o.p.x = vu_pick1(corner, a.p.x, b.p.x, c.p.x);
    o.p.y = vu_pick1(corner, a.p.y, b.p.y, c.p.y);
    o.gx = asint(vu_pick1u(corner, asuint(a.gx), asuint(b.gx), asuint(c.gx)));
    o.gy = asint(vu_pick1u(corner, asuint(a.gy), asuint(b.gy), asuint(c.gy)));
    o.gz = asint(vu_pick1u(corner, asuint(a.gz), asuint(b.gz), asuint(c.gz)));
    o.rgba.x = vu_pick1u(corner, a.rgba.x, b.rgba.x, c.rgba.x);
    o.rgba.y = vu_pick1u(corner, a.rgba.y, b.rgba.y, c.rgba.y);
    o.rgba.z = vu_pick1u(corner, a.rgba.z, b.rgba.z, c.rgba.z);
    o.rgba.w = vu_pick1u(corner, a.rgba.w, b.rgba.w, c.rgba.w);
    o.stq.x = vu_pick1(corner, a.stq.x, b.stq.x, c.stq.x);
    o.stq.y = vu_pick1(corner, a.stq.y, b.stq.y, c.stq.y);
    o.stq.z = vu_pick1(corner, a.stq.z, b.stq.z, c.stq.z);
    o.inside = vu_pick1u(corner, a.inside, b.inside, c.inside);
    o.gsOk = vu_pick1u(corner, a.gsOk, b.gsOk, c.gsOk);
    return o;
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
float4 vu_gs_position(int gx, int gy, int gz, bool wrap)
{
    int x = gx, y = gy;
    if (wrap) {
        x = gx & 0xFFFF;
        y = gy & 0xFFFF;
    }
    return float4(vu_ndc(float2(x, y) * (1.0 / 16.0)), gs_depth(asuint(gz)), 1.0);
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
    float px = g_z.w != 0.0 ? v.p.x : float(v.gx) * (1.0 / 16.0);
    float py = g_z.w != 0.0 ? v.p.y : float(v.gy) * (1.0 / 16.0);
    return float4(vu_ndc(float2(px, py)), gs_depth(asuint(v.gz)), 1.0);
}

// Whether the GS position of a vertex is the one the GS would draw it at
// (vu_gs_ok, computed by vu_divide); 1 or 0.
uint vu_has_gs_position(VuVtx v)
{
    return v.gsOk;
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
float4 vu_cut_position_of(VuVtx v, float4 vtx)
{
    if (vu_has_gs_position(v) != 0u) {
        return vtx;
    }
    return vu_homogeneous_position(v.h);
}

float4 vu_cut_position(VuVtx v)
{
    return vu_cut_position_of(v, vu_vtx_position(v));
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

// Whether a clip-space rectangle lo .. hi (a sprite's corners) meets the
// camera's picture, edges counted as reaching (a range that ends exactly
// on an edge covers no pixel centre inside, but counts).
bool vu_rect_reaches_picture(float2 lo, float2 hi)
{
    float4 e = vu_picture_ndc();
    return lo.x < e.y && hi.x > e.x && lo.y < e.z && hi.y > e.w;
}

// ---------------------------------------------------------------- the cut
// Whether what the GPU draws of the triangle with clip-space corners a, b
// and c (vu_cut_position) reaches into the rectangle with the edges e
// (vu_picture_ndc's form): the triangle cut to 0 <= z <= w as the GPU
// clips it and to e's top and bottom, its x extent met with e's columns
// (lo < e.y and hi > e.x: edges count as reaching).  Cut to the rows
// first: a triangle whose part within the picture's columns lies wholly
// above or below the picture (only sky over it) but that covers frame
// pixels beside the picture lies wholly beside it, and is drawn there. A
// triangle cut away whole reaches nothing. The corners keep w >= 0 after
// the z cut, so the row planes cut in homogeneous form.
//
// Sutherland-Hodgman over the four planes, plane by plane in this order:
// for each corner of the polygon in turn, with its successor (the first
// corner after the last), the corner when its distance d >= 0, then the
// point lerp(a, b, da / (da - db)) when the two distances' signs differ.
// The distance is the plane's precise dot product in the VU's order,
// ((p.x v.x + p.y v.y) + p.z v.z) + p.w v.w, with the plane's zero and unit
// products folded (each exact for a finite corner, so the value is the
// dot's): z for z >= 0, w - z for z <= w, top w - y under the top, y -
// bottom w over the bottom.  The extent: x / w of each corner of the
// last polygon (one left at w = 0, z = 0 there too, lies at infinity on
// its x's side); at least three corners.
//
// The forms (no loop, no array, no vector of booleans): a corner is four
// scalars (x, y, z, w; after the z planes x, y, w), a plane's corners
// registers p0.., the polygon the last n of them; a corner is appended by
// moving the registers down one and writing the last (VU_CUT_PUSH*), each
// a ?: between two numbers on the step's flag; the flags are uints (1 or
// 0), as vu_clipw's.  A plane's steps are written out for the most corners
// it can be given (3, 4, 5, 6: a convex polygon cut by a plane gains at
// most one corner; one that rounding has made gain more keeps its first
// ones, as the cut always kept its first VU_CUT_CORNERS).  The z planes
// (vu_cut_z) and the rows (vu_cut_rows) are apart, so a tick's picture and
// frame tests share its z cut.
#define VU_CUT_CORNERS 7u

#define VU_CUT_MOVE4(C, Q, S)                                                                      \
    Q##x = C ? S##x : Q##x;                                                                        \
    Q##y = C ? S##y : Q##y;                                                                        \
    Q##z = C ? S##z : Q##z;                                                                        \
    Q##w = C ? S##w : Q##w;
#define VU_CUT_MOVE3(C, Q, S)                                                                      \
    Q##x = C ? S##x : Q##x;                                                                        \
    Q##y = C ? S##y : Q##y;                                                                        \
    Q##w = C ? S##w : Q##w;
// the appends of the z >= 0, z <= w and top planes
#define VU_CUT_PUSH_Z0(C, V)                                                                       \
    VU_CUT_MOVE4(C, q0, q1) VU_CUT_MOVE4(C, q1, q2) VU_CUT_MOVE4(C, q2, q3) VU_CUT_MOVE4(C, q3, V)
#define VU_CUT_PUSH_Z1(C, V)                                                                       \
    VU_CUT_MOVE4(C, q0, q1)                                                                        \
    VU_CUT_MOVE4(C, q1, q2) VU_CUT_MOVE4(C, q2, q3) VU_CUT_MOVE4(C, q3, q4) VU_CUT_MOVE4(C, q4, V)
#define VU_CUT_PUSH_TOP(C, V)                                                                      \
    VU_CUT_MOVE3(C, q0, q1)                                                                        \
    VU_CUT_MOVE3(C, q1, q2)                                                                        \
    VU_CUT_MOVE3(C, q2, q3) VU_CUT_MOVE3(C, q3, q4) VU_CUT_MOVE3(C, q4, q5) VU_CUT_MOVE3(C, q5, V)
// the bottom plane's corners go into the extent
#define VU_CUT_EXTENT(C, V)                                                                        \
    {                                                                                              \
        float xd_ = V##x / V##w;                                                                   \
        float xs_ = V##x >= 0.0 ? 1e30 : -1e30;                                                    \
        float xe_ = V##w > 0.0 ? xd_ : xs_;                                                        \
        float lo_ = min(lo, xe_), hi_ = max(hi, xe_);                                              \
        lo = C ? lo_ : lo;                                                                         \
        hi = C ? hi_ : hi;                                                                         \
    }
#define VU_CUT_LERP4(R, A, B, T)                                                                   \
    float R##x = lerp(A##x, B##x, T), R##y = lerp(A##y, B##y, T), R##z = lerp(A##z, B##z, T),     \
          R##w = lerp(A##w, B##w, T);
#define VU_CUT_LERP3(R, A, B, T)                                                                   \
    float R##x = lerp(A##x, B##x, T), R##y = lerp(A##y, B##y, T), R##w = lerp(A##w, B##w, T);
// One step: corner A (distance DA) and its successor B (DB); LIVE 1 when A
// is one of the polygon's, CAP 1 while there is room; OUT(C, V) takes the
// corner V when C.
#define VU_CUT_STEP(OUT, LERP, LIVE, CAP, A, DA, B, DB)                                            \
    {                                                                                              \
        uint ia_ = (DA) >= 0.0 ? 1u : 0u;                                                          \
        uint ib_ = (DB) >= 0.0 ? 1u : 0u;                                                          \
        bool keep_ = ((LIVE) & ia_ & (CAP)) != 0u;                                                 \
        OUT(keep_, A)                                                                              \
        m += keep_ ? 1u : 0u;                                                                      \
        bool cross_ = ((LIVE) & (ia_ != ib_ ? 1u : 0u) & (CAP)) != 0u;                             \
        float t_ = (DA) / ((DA) - (DB));                                                           \
        LERP(pt_, A, B, t_)                                                                        \
        OUT(cross_, pt_)                                                                           \
        m += cross_ ? 1u : 0u;                                                                     \
    }

// A triangle's polygon after the z planes: the last n of p0..p4 (x, y, w)
struct VuCutZ {
    float p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w, p3x, p3y, p3w, p4x, p4y, p4w;
    uint n;
};

// The triangle a, b, c cut to 0 <= z <= w
VuCutZ vu_cut_z(float4 a, float4 b, float4 c)
{
    float ax = a.x, ay = a.y, az = a.z, aw = a.w, bx = b.x, by = b.y, bz = b.z, bw = b.w;
    float cx = c.x, cy = c.y, cz = c.z, cw = c.w;
    float q0x = ax, q0y = ay, q0z = az, q0w = aw, q1x = ax, q1y = ay, q1z = az, q1w = aw;
    float q2x = ax, q2y = ay, q2z = az, q2w = aw, q3x = ax, q3y = ay, q3z = az, q3w = aw;
    float q4x = ax, q4y = ay, q4z = az, q4w = aw;
    uint m = 0u;
    // z >= 0: the triangle, 3 corners in, at most 4 out
    {
        float d0 = az, d1 = bz, d2 = cz;
        VU_CUT_STEP(VU_CUT_PUSH_Z0, VU_CUT_LERP4, 1u, 1u, a, d0, b, d1)
        VU_CUT_STEP(VU_CUT_PUSH_Z0, VU_CUT_LERP4, 1u, 1u, b, d1, c, d2)
        VU_CUT_STEP(VU_CUT_PUSH_Z0, VU_CUT_LERP4, 1u, 1u, c, d2, a, d0)
    }
    // z <= w: the last n of p0..p3 in, at most 5 out
    uint n = m;
    float p0x = q0x, p0y = q0y, p0z = q0z, p0w = q0w, p1x = q1x, p1y = q1y, p1z = q1z, p1w = q1w;
    float p2x = q2x, p2y = q2y, p2z = q2z, p2w = q2w, p3x = q3x, p3y = q3y, p3z = q3z, p3w = q3w;
    m = 0u;
    {
        // the first corner, slot 4 - n
        bool f1 = n == 3u, f2 = n == 2u, f3 = n < 2u;
        float fx = p0x, fy = p0y, fz = p0z, fw = p0w;
        fx = f1 ? p1x : fx;
        fy = f1 ? p1y : fy;
        fz = f1 ? p1z : fz;
        fw = f1 ? p1w : fw;
        fx = f2 ? p2x : fx;
        fy = f2 ? p2y : fy;
        fz = f2 ? p2z : fz;
        fw = f2 ? p2w : fw;
        fx = f3 ? p3x : fx;
        fy = f3 ? p3y : fy;
        fz = f3 ? p3z : fz;
        fw = f3 ? p3w : fw;
        precise float d0 = p0w - p0z, d1 = p1w - p1z, d2 = p2w - p2z, d3 = p3w - p3z;
        precise float df = fw - fz;
        uint l0 = n >= 4u ? 1u : 0u, l1 = n >= 3u ? 1u : 0u, l2 = n >= 2u ? 1u : 0u;
        uint l3 = n >= 1u ? 1u : 0u;
        VU_CUT_STEP(VU_CUT_PUSH_Z1, VU_CUT_LERP4, l0, (m < 5u ? 1u : 0u), p0, d0, p1, d1)
        VU_CUT_STEP(VU_CUT_PUSH_Z1, VU_CUT_LERP4, l1, (m < 5u ? 1u : 0u), p1, d1, p2, d2)
        VU_CUT_STEP(VU_CUT_PUSH_Z1, VU_CUT_LERP4, l2, (m < 5u ? 1u : 0u), p2, d2, p3, d3)
        VU_CUT_STEP(VU_CUT_PUSH_Z1, VU_CUT_LERP4, l3, (m < 5u ? 1u : 0u), p3, d3, f, df)
    }
    VuCutZ o;
    o.p0x = q0x;
    o.p0y = q0y;
    o.p0w = q0w;
    o.p1x = q1x;
    o.p1y = q1y;
    o.p1w = q1w;
    o.p2x = q2x;
    o.p2y = q2y;
    o.p2w = q2w;
    o.p3x = q3x;
    o.p3y = q3y;
    o.p3w = q3w;
    o.p4x = q4x;
    o.p4y = q4y;
    o.p4w = q4w;
    o.n = m;
    return o;
}

// The polygon z cut to the rows of the edges e (top e.z w, bottom e.w w),
// its x extent met with e's columns: whether it reaches the rectangle
bool vu_cut_rows(VuCutZ z, float4 e)
{
    float top = e.z, bottom = e.w;
    float p0x = z.p0x, p0y = z.p0y, p0w = z.p0w, p1x = z.p1x, p1y = z.p1y, p1w = z.p1w;
    float p2x = z.p2x, p2y = z.p2y, p2w = z.p2w, p3x = z.p3x, p3y = z.p3y, p3w = z.p3w;
    float p4x = z.p4x, p4y = z.p4y, p4w = z.p4w;
    float q0x = p0x, q0y = p0y, q0w = p0w, q1x = p0x, q1y = p0y, q1w = p0w;
    float q2x = p0x, q2y = p0y, q2w = p0w, q3x = p0x, q3y = p0y, q3w = p0w;
    float q4x = p0x, q4y = p0y, q4w = p0w, q5x = p0x, q5y = p0y, q5w = p0w;
    uint n = z.n;
    uint m = 0u;
    // y <= top * w: the last n of p0..p4 in, at most 6 out
    {
        bool f1 = n == 4u, f2 = n == 3u, f3 = n == 2u, f4 = n < 2u;
        float fx = p0x, fy = p0y, fw = p0w;
        fx = f1 ? p1x : fx;
        fy = f1 ? p1y : fy;
        fw = f1 ? p1w : fw;
        fx = f2 ? p2x : fx;
        fy = f2 ? p2y : fy;
        fw = f2 ? p2w : fw;
        fx = f3 ? p3x : fx;
        fy = f3 ? p3y : fy;
        fw = f3 ? p3w : fw;
        fx = f4 ? p4x : fx;
        fy = f4 ? p4y : fy;
        fw = f4 ? p4w : fw;
        precise float t0 = top * p0w, t1 = top * p1w, t2 = top * p2w, t3 = top * p3w;
        precise float t4 = top * p4w, tf = top * fw;
        precise float d0 = t0 - p0y, d1 = t1 - p1y, d2 = t2 - p2y, d3 = t3 - p3y, d4 = t4 - p4y;
        precise float df = tf - fy;
        uint l0 = n >= 5u ? 1u : 0u, l1 = n >= 4u ? 1u : 0u, l2 = n >= 3u ? 1u : 0u;
        uint l3 = n >= 2u ? 1u : 0u, l4 = n >= 1u ? 1u : 0u;
        VU_CUT_STEP(VU_CUT_PUSH_TOP, VU_CUT_LERP3, l0, (m < 6u ? 1u : 0u), p0, d0, p1, d1)
        VU_CUT_STEP(VU_CUT_PUSH_TOP, VU_CUT_LERP3, l1, (m < 6u ? 1u : 0u), p1, d1, p2, d2)
        VU_CUT_STEP(VU_CUT_PUSH_TOP, VU_CUT_LERP3, l2, (m < 6u ? 1u : 0u), p2, d2, p3, d3)
        VU_CUT_STEP(VU_CUT_PUSH_TOP, VU_CUT_LERP3, l3, (m < 6u ? 1u : 0u), p3, d3, p4, d4)
        VU_CUT_STEP(VU_CUT_PUSH_TOP, VU_CUT_LERP3, l4, (m < 6u ? 1u : 0u), p4, d4, f, df)
    }
    // y >= bottom * w: the last n of p0..p5 in, their corners into the extent
    n = m;
    float p5x = q5x, p5y = q5y, p5w = q5w;
    p0x = q0x;
    p0y = q0y;
    p0w = q0w;
    p1x = q1x;
    p1y = q1y;
    p1w = q1w;
    p2x = q2x;
    p2y = q2y;
    p2w = q2w;
    p3x = q3x;
    p3y = q3y;
    p3w = q3w;
    p4x = q4x;
    p4y = q4y;
    p4w = q4w;
    m = 0u;
    float lo = 1e30;
    float hi = -1e30;
    {
        bool f1 = n == 5u, f2 = n == 4u, f3 = n == 3u, f4 = n == 2u, f5 = n < 2u;
        float fx = p0x, fy = p0y, fw = p0w;
        fx = f1 ? p1x : fx;
        fy = f1 ? p1y : fy;
        fw = f1 ? p1w : fw;
        fx = f2 ? p2x : fx;
        fy = f2 ? p2y : fy;
        fw = f2 ? p2w : fw;
        fx = f3 ? p3x : fx;
        fy = f3 ? p3y : fy;
        fw = f3 ? p3w : fw;
        fx = f4 ? p4x : fx;
        fy = f4 ? p4y : fy;
        fw = f4 ? p4w : fw;
        fx = f5 ? p5x : fx;
        fy = f5 ? p5y : fy;
        fw = f5 ? p5w : fw;
        precise float t0 = bottom * p0w, t1 = bottom * p1w, t2 = bottom * p2w, t3 = bottom * p3w;
        precise float t4 = bottom * p4w, t5 = bottom * p5w, tf = bottom * fw;
        precise float d0 = p0y - t0, d1 = p1y - t1, d2 = p2y - t2, d3 = p3y - t3, d4 = p4y - t4;
        precise float d5 = p5y - t5, df = fy - tf;
        uint l0 = n >= 6u ? 1u : 0u, l1 = n >= 5u ? 1u : 0u, l2 = n >= 4u ? 1u : 0u;
        uint l3 = n >= 3u ? 1u : 0u, l4 = n >= 2u ? 1u : 0u, l5 = n >= 1u ? 1u : 0u;
        VU_CUT_STEP(VU_CUT_EXTENT, VU_CUT_LERP3, l0, (m < VU_CUT_CORNERS ? 1u : 0u), p0, d0, p1, d1)
        VU_CUT_STEP(VU_CUT_EXTENT, VU_CUT_LERP3, l1, (m < VU_CUT_CORNERS ? 1u : 0u), p1, d1, p2, d2)
        VU_CUT_STEP(VU_CUT_EXTENT, VU_CUT_LERP3, l2, (m < VU_CUT_CORNERS ? 1u : 0u), p2, d2, p3, d3)
        VU_CUT_STEP(VU_CUT_EXTENT, VU_CUT_LERP3, l3, (m < VU_CUT_CORNERS ? 1u : 0u), p3, d3, p4, d4)
        VU_CUT_STEP(VU_CUT_EXTENT, VU_CUT_LERP3, l4, (m < VU_CUT_CORNERS ? 1u : 0u), p4, d4, p5, d5)
        VU_CUT_STEP(VU_CUT_EXTENT, VU_CUT_LERP3, l5, (m < VU_CUT_CORNERS ? 1u : 0u), p5, d5, f, df)
    }
    return m >= 3u && lo < e.y && hi > e.x;
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
    if (g_z.w != 0.0) {
        return vu_texel_offset_px(v.p);
    }
    return vu_texel_offset_px(float2(v.gx, v.gy) * (1.0 / 16.0));
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
// pc (texels from the target's centre) and those values there; the caller
// asks only under the exact viewport (g_vp.x set), before it computes them
VuVSOut vu_attr_planes_at(VuVSOut o, float2 pa, float2 pb, float2 pc, float4 ca, float4 cb,
                          float4 cc, float3 sa, float3 sb, float3 sc)
{
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
    if (vu_has_gs_position(a) != 0u) {
        return 0u;
    }
    precise float2 cc = g_origin.xy + g_target.xy * 0.5;
    float db = vu_has_gs_position(b) != 0u ? length(float2(b.gx, b.gy) * (1.0 / 16.0) - cc) : 1e30;
    float dc = vu_has_gs_position(c) != 0u ? length(float2(c.gx, c.gy) * (1.0 / 16.0) - cc) : 1e30;
    if (db >= 1e30 && dc >= 1e30) {
        return 0u;
    }
    return db <= dc ? 1u : 2u;
}

// value i (0, 1, 2) of a, b, c turned to the anchor k (vu_plane_anchor: 1
// b, c, a; 2 c, a, b), chosen value by value
float vu_turn(uint k, uint i, float a, float b, float c)
{
    uint j = (k + i) % 3u;
    float r = a;
    r = j == 1u ? b : r;
    r = j == 2u ? c : r;
    return r;
}

float2 vu_turn2(uint k, uint i, float2 a, float2 b, float2 c)
{
    return float2(vu_turn(k, i, a.x, b.x, c.x), vu_turn(k, i, a.y, b.y, c.y));
}

float3 vu_turn3(uint k, uint i, float3 a, float3 b, float3 c)
{
    return float3(vu_turn(k, i, a.x, b.x, c.x), vu_turn(k, i, a.y, b.y, c.y),
                  vu_turn(k, i, a.z, b.z, c.z));
}

float4 vu_turn4(uint k, uint i, float4 a, float4 b, float4 c)
{
    return float4(vu_turn(k, i, a.x, b.x, c.x), vu_turn(k, i, a.y, b.y, c.y),
                  vu_turn(k, i, a.z, b.z, c.z), vu_turn(k, i, a.w, b.w, c.w));
}

// A triangle's corners turned to its anchor (vu_plane_anchor), in texels
// from the target's centre, and their gradient basis: what the attribute
// planes and the plane distance's share
struct VuPlaneTri {
    uint k;
    float2 pa, pb, pc;
    float4 g;
};

VuPlaneTri vu_plane_tri(VuVtx a, VuVtx b, VuVtx c)
{
    VuPlaneTri t;
    t.k = vu_plane_anchor(a, b, c);
    float2 ta = vu_texel_offset(a), tb = vu_texel_offset(b), tc = vu_texel_offset(c);
    t.pa = vu_turn2(t.k, 0u, ta, tb, tc);
    t.pb = vu_turn2(t.k, 1u, ta, tb, tc);
    t.pc = vu_turn2(t.k, 2u, ta, tb, tc);
    t.g = vu_grad_basis(t.pa, t.pb, t.pc);
    return t;
}

// the planes of a triangle every corner of which is drawn at its GS
// position (exact 1), anchored at vu_plane_anchor's corner; the plane
// distance's are vu_plane_out's
VuVSOut vu_attr_planes(VuVSOut o, VuPlaneTri t, VuVtx a, VuVtx b, VuVtx c, uint exact)
{
    if (exact == 0u || g_vp.x == 0.0) {
        return o;
    }
    float4 ca = float4(a.rgba), cb = float4(b.rgba), cc = float4(c.rgba);
    float4 va = vu_turn4(t.k, 0u, ca, cb, cc), vb = vu_turn4(t.k, 1u, ca, cb, cc);
    float4 vc = vu_turn4(t.k, 2u, ca, cb, cc);
    float3 sa = vu_turn3(t.k, 0u, a.stq, b.stq, c.stq), sb = vu_turn3(t.k, 1u, a.stq, b.stq, c.stq);
    float3 sc = vu_turn3(t.k, 2u, a.stq, b.stq, c.stq);
    precise float4 db = vb - va;
    precise float4 dc = vc - va;
    precise float3 eb = sb - sa;
    precise float3 ec = sc - sa;
    precise float4 cx = db * t.g.x + dc * t.g.y;
    precise float4 cy = db * t.g.z + dc * t.g.w;
    precise float3 sx = eb * t.g.x + ec * t.g.y;
    precise float3 sy = eb * t.g.z + ec * t.g.w;
    o.a0 = va;
    o.a1 = float4(sa, 0.0);
    o.x0 = cx;
    o.y0 = cy;
    o.x1 = float4(sx, 0.0);
    o.y1 = float4(sy, 0.0);
    o.r = float4(t.pa, 1.0, 0.0);
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
// chains): vu_batch.z = 0 for none, else the axis + 1 in bits 0..1 and bit 2
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
// past the plane; its plane where vu_attr_planes made the others (the same
// anchor and basis, t)
VuVSOut vu_plane_out(VuVSOut o, VuPlaneTri t, uint kick, uint me)
{
    if (vu_batch.z == 0u) {
        return o;
    }
    float da = vu_plane_distance(kick - 2u), db = vu_plane_distance(kick - 1u);
    float dc = vu_plane_distance(kick);
    // culled (VU_CULLED) value by value: a whole vector chosen after it is
    // computed would be a select on a vector of booleans
    bool past = da < 0.0 && db < 0.0 && dc < 0.0;
    o.pos.x = past ? VU_CULLED.x : o.pos.x;
    o.pos.y = past ? VU_CULLED.y : o.pos.y;
    o.pos.z = past ? VU_CULLED.z : o.pos.z;
    o.pos.w = past ? VU_CULLED.w : o.pos.w;
    o.plane = me == 0u ? da : (me == 1u ? db : dc);
    if (o.r.z == 0.0) {
        return o;
    }
    float ra = vu_turn(t.k, 0u, da, db, dc), rb = vu_turn(t.k, 1u, da, db, dc);
    float rc = vu_turn(t.k, 2u, da, db, dc);
    precise float x = (rb - ra) * t.g.x + (rc - ra) * t.g.y;
    precise float y = (rb - ra) * t.g.z + (rc - ra) * t.g.w;
    o.a1.w = ra;
    o.x1.w = x;
    o.y1.w = y;
    return o;
}

// What the port draws of a triangle of a program with the region test
// (vu_region_rule; vu1_ref.h vu1ref_region_draw is the CPU mirror).
#define VU_RULE_EXACT 0u // all three vertices inside: at their GS positions
#define VU_RULE_CLIP 1u  // GPU-clipped at the native frustum (vu_cut_position)
#define VU_RULE_DROP 2u  // not drawn
// The card test decides (vu_rule_card): VU_RULE_DROP for a triangle of a
// card, a free-standing strip (no VU_MARK_JOINED) none of whose
// triangles passes the region test (vu_strip_card), else VU_RULE_CLIP
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
//   across by the native height, the drawing camera's: the cut,
//   vu_cut_rows; seen only on a frame wider than 4:3): drawn,
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
//   unless it belongs to an artist's card parked at the picture's edge: a
//   free-standing strip (the run of triangles the strip flag ties together:
//   one strip of the model, pac_makeNormalStrip; sharing no vertex
//   position with another strip of its mesh but a double-sided copy, no
//   VU_MARK_JOINED, rd_mesh.c markJoinedStrips) none of whose triangles
//   pass the test (vu_strip_card: the CPU decides it per strip and tick,
//   rd_mesh.c). The opening's tree card (st26a_near, a free-standing
//   2-triangle strip of a 12-card draw, every vertex 160 to 540 units in
//   front of the eye) and the waterfall's mist cards stay out of the
//   picture, as out of the box. A wall is drawn: its strip passes (the
//   water tower's lift wall, st20a_p2 part 28, packet 4, strip 14, two of
//   its six triangles pass) or is joined to the rest of the surface (the
//   old bridge's plank faces at 48:9, st08a_p4). The unit is not the
//   draw: the tree card's draw has 7 passing triangles in its other cards;
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
// more VU_MARK_JOINED; a grid of one strip is a card when none of its
// triangles pass.
// Two differences from a code-36 draw of the same triangle remain: code
// 36's clipped triangles go through their own earlier pass with ABE forced
// on (rd_replay.c draws them as the VU's fans, PRIM 0x5D), which a code-32
// draw does not, so a mesh whose clip code flips between 32 and 36 still
// changes the blending along its cut rim; and vu_later_out's bias needs
// all three GS positions, so a later-marked triangle with a corner
// saturated or below GS Z 0 is drawn without it.
// The rule's steps after the free camera's at one camera (a triangle not
// all inside): the cut against the picture pic (vu_picture_ndc), then the
// card test's conditions
uint vu_region_rule_cut(VuVtx a, VuVtx b, VuVtx c, VuCutZ z, float4 pic)
{
    if (!vu_cut_rows(z, pic)) {
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
    o.inside = vu_inside(o.p, o.h.w, lo, hi);
    return o;
}

// Whether tick k showed the triangle a, b, c (vu_region_rule): 0 not, 1
// through the card test (the rule there was VU_RULE_STRIP), 2 drawn there.
// The rule at the tick's camera (the free camera's step aside: the blended
// camera dropped the triangle, so it is not the free camera), then what
// the GPU draws of it there against the frame; the z cut is the picture
// test's and the frame test's.
uint vu_tick_shown(VuVtx a, VuVtx b, VuVtx c, uint k, float4 lo, float4 hi, float4 pic,
                   float4 frame)
{
    VuVtx ak = vu_tick_vertex(a, k, lo, hi);
    VuVtx bk = vu_tick_vertex(b, k, lo, hi);
    VuVtx ck = vu_tick_vertex(c, k, lo, hi);
    VuCutZ z = vu_cut_z(vu_cut_position(ak), vu_cut_position(bk), vu_cut_position(ck));
    uint rk = VU_RULE_EXACT;
    if ((ak.inside & bk.inside & ck.inside) == 0u) {
        rk = vu_region_rule_cut(ak, bk, ck, z, pic);
    }
    if (rk == VU_RULE_DROP || !vu_cut_rows(z, frame)) {
        return 0u; // not shown at tick k
    }
    return rk == VU_RULE_STRIP ? 1u : 2u;
}

// The region rule of the triangle a, b, c, whose program's region test
// has the bounds lo, hi.  The PS2
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
// test decides (VU_RULE_STRIP is returned as it is, as when the blended
// camera's own verdict is the card test, so a card never enters the
// picture between the ticks either).  The PS2's rule is unchanged at
// every tick's own picture (no VU_F_TICKS: t = 0, the Original framerate,
// a still camera) and under the free camera.  The two ticks are written
// out (k = 0, then 1, only on this path), each tick's z cut shared by its
// picture and frame tests (vu_tick_shown).  vu1_ref.h
// vu1ref_region_between is the CPU mirror.
uint vu_region_rule(VuVtx a, VuVtx b, VuVtx c, float4 lo, float4 hi)
{
    if ((a.inside & b.inside & c.inside) != 0u) {
        return VU_RULE_EXACT;
    }
    if ((vu_draw.z & VU_F_BEHIND_EYE) != 0u) {
        return VU_RULE_CLIP;
    }
    float4 pic = vu_picture_ndc();
    VuCutZ z = vu_cut_z(vu_cut_position(a), vu_cut_position(b), vu_cut_position(c));
    uint r = vu_region_rule_cut(a, b, c, z, pic);
    if (r != VU_RULE_DROP || (vu_draw.z & VU_F_TICKS) == 0u) {
        return r;
    }
    float4 frame = vu_frame_ndc();
    uint s0 = vu_tick_shown(a, b, c, 0u, lo, hi, pic, frame);
    if (s0 == 2u) {
        return VU_RULE_CLIP;
    }
    uint s1 = vu_tick_shown(a, b, c, 1u, lo, hi, pic, frame);
    if (s1 == 2u) {
        return VU_RULE_CLIP;
    }
    return (s0 | s1) != 0u ? VU_RULE_STRIP : VU_RULE_DROP;
}

// The card test (VU_RULE_STRIP) of the triangle kick: whether its strip
// is a card.  The CPU decides it once per tick for every free-standing
// strip of a draw in a state other than out of the box (rd_mesh.c,
// vu1_ref.h vu1ref_strip_card: none of the strip's triangles passes the
// region test at the tick's camera) and the replay hands the verdicts over
// as two bits per kick, x .. x + z of vu_strip, in VuBoneCB from qword
// vu_strip.w (sixteen kicks a word, four words a qword): bit 0 the tick
// before, bit 1 this tick (rd_interp.c sets the first from the tick
// before's frame).  A strip is a card when it was one at either tick; a
// kick the bits do not cover is none.
bool vu_strip_card(uint kick)
{
    uint i = kick - vu_strip.x;
    if (i >= vu_strip.z) {
        return false;
    }
    uint4 q = asuint(vu_bone[min(vu_strip.w + (i >> 6), 239u)]);
    uint s = (i >> 4) & 3u;
    uint word = q.x;
    word = s == 1u ? q.y : word;
    word = s == 2u ? q.z : word;
    word = s == 3u ? q.w : word;
    return ((word >> ((i & 15u) * 2u)) & 3u) != 0u;
}

// VU_RULE_STRIP resolved: a joined strip is not a card; a free-standing
// one is when the CPU said so
uint vu_rule_card(uint rule, uint marks, uint kick)
{
    if (rule != VU_RULE_STRIP) {
        return rule;
    }
    if ((marks & VU_MARK_JOINED) != 0u || !vu_strip_card(kick)) {
        return VU_RULE_CLIP;
    }
    return VU_RULE_DROP;
}

// The triangle k-2, k-1, k: what the GS draws of it (vu_triangle_out),
// without the planes; exact 1: every corner drawn at its GS position.
// Each position the corner can take is computed once (pme its
// vu_vtx_position, cme its vu_cut_position), the rules give a code (0
// culled, 1 pme, 2 cme, 3 the wrapped GS position), and the position is
// chosen value by value.
VuVSOut vu_triangle_out_pos(VuVtx a, VuVtx b, VuVtx c, VuVtx me, uint mode, uint rule,
                            out uint exact)
{
    VuVSOut o = vu_out_init();
    o.col = float4(me.rgba);
    o.stq = me.stq;
    // every corner at its GS position (vu_attr_planes): the corners the
    // GPU clips have none
    uint gsPos = vu_has_gs_position(a) & vu_has_gs_position(b) & vu_has_gs_position(c);
    float4 pme = vu_vtx_position(me);
    float4 cme = vu_cut_position_of(me, pme);
    float4 wme = pme;
    if (mode == VU_CLIP_NONE) {
        wme = vu_gs_position(me.gx, me.gy, me.gz, true);
    }
    uint at = 0u;
    exact = 0u;
    if (mode == VU_CLIP_NONE) {
        // vu_vtx_position, as codes 32, 36 and 38 draw (on the 12.4 grid,
        // or off it under ICO_RD_VU_OFFGRID): a triangle placed differently
        // from the same triangle drawn by them would sit up to 1/16 GS pixel
        // away, which on a sloped surface moves its depth one way over the
        // whole triangle, so a coplanar pass by another program (the
        // reflection pass, 38) would fail or pass GEQUAL wholesale as the
        // camera moves.  A vertex whose X or Y the 16-bit wrap would change
        // keeps the wrapped GS value.
        at = (me.gx & 0xFFFF) != me.gx || (me.gy & 0xFFFF) != me.gy ? 3u : 1u;
        exact = (a.gx & 0xFFFF) == a.gx && (a.gy & 0xFFFF) == a.gy && (b.gx & 0xFFFF) == b.gx &&
                        (b.gy & 0xFFFF) == b.gy && (c.gx & 0xFFFF) == c.gx &&
                        (c.gy & 0xFFFF) == c.gy
                    ? 1u
                    : 0u;
    } else if (mode == VU_CLIP_REGION) {
        // the region rule (vu_region_rule)
        if (rule == VU_RULE_EXACT) {
            at = 1u;
            exact = 1u;
        } else if (rule == VU_RULE_CLIP) {
            at = 2u;
            exact = gsPos;
        }
    } else {
        // VU_CLIP_SCISSOR, the PS2's (RegistPacket.c also gives it the
        // packets whose own box fails)
        uint fa = vu_clipw(a.clip), fb = vu_clipw(b.clip), fc = vu_clipw(c.clip);
        bool cut = (fa | fb | fc) != 0u;
        if ((cut && (vu_draw.z & VU_F_KICK_ONLY) != 0u) ||
            (!cut && (vu_draw.z & VU_F_CUT_ONLY) != 0u)) {
            // drawn by the other pass
        } else if (!cut) {
            at = 1u; // kicked as it is
            exact = 1u;
        } else if ((fa & fb & fc) != 0u) {
            // trivially rejected (the six fcor tests)
        } else if (((fa | fb | fc) & 0x30u) != 0u || min(a.h.w, min(b.h.w, c.h.w)) <= 0.0) {
            // SCISSOR_COMMON; the GPU clips (at 0 <= z <= w, not the VU's
            // z = -w: that would need SV_ClipDistance)
            at = 2u;
            exact = gsPos;
        } else {
            // only x/y flags: the VU clips to its guard band (the 1500 unit
            // clip window, wider than any target), the GS scissor does the
            // rest
            at = 1u;
            exact = 1u;
        }
    }
    o.pos.x = at == 1u ? pme.x : (at == 2u ? cme.x : (at == 3u ? wme.x : VU_CULLED.x));
    o.pos.y = at == 1u ? pme.y : (at == 2u ? cme.y : (at == 3u ? wme.y : VU_CULLED.y));
    o.pos.z = at == 1u ? pme.z : (at == 2u ? cme.z : (at == 3u ? wme.z : VU_CULLED.z));
    o.pos.w = at == 1u ? pme.w : (at == 2u ? cme.w : (at == 3u ? wme.w : VU_CULLED.w));
    return o;
}

// A triangle marked VU_MARK_LATER and the earlier one it
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
    precise float3 pa = float3(float2(a.gx, a.gy) * (1.0 / 16.0), float(asuint(a.gz)));
    precise float3 pb = float3(float2(b.gx, b.gy) * (1.0 / 16.0), float(asuint(b.gz)));
    precise float3 pc = float3(float2(c.gx, c.gy) * (1.0 / 16.0), float(asuint(c.gz)));
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
    uint zmax = max(asuint(a.gz), max(asuint(b.gz), asuint(c.gz)));
    if ((vu_has_gs_position(a) & vu_has_gs_position(b) & vu_has_gs_position(c)) != 0u &&
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
    px.x += corner == 1u ? 1.5 : 0.0;
    px.y += corner == 2u ? 1.5 : 0.0;
    // texel px of the target (under the exact viewport the target is the
    // share g_misc.zw of it about its centre: common.hlsli fullscreen_triangle)
    if (g_vp.x != 0.0) {
        o.pos = float4((px.x + g_vpo.x) * g_vp.x - g_misc.z, g_misc.w - (px.y + g_vpo.y) * g_vp.y,
                       0.5, 1.0);
    } else {
        o.pos = float4(px.x * g_target.z * 2.0 - 1.0, 1.0 - px.y * g_target.w * 2.0, 0.5, 1.0);
    }
    o.col = float4(float(value & 0xFFFFu), float(value >> 16), 0.0, 0.0);
    return o;
}

// Fields 0..8 of a mesh vertex probe.
uint vu_probe_field(VuVtx v, uint field)
{
    uint r = 0u;
    if (field == 0u) {
        r = asuint(v.gx);
    } else if (field == 1u) {
        r = asuint(v.gy);
    } else if (field == 2u) {
        r = asuint(v.gz);
    } else if (field == 3u) {
        r = v.rgba.x | (v.rgba.y << 8) | (v.rgba.z << 16) | (v.rgba.w << 24);
    } else if (field == 4u) {
        r = asuint(v.stq.x);
    } else if (field == 5u) {
        r = asuint(v.stq.y);
    } else if (field == 6u) {
        r = asuint(v.stq.z);
    } else if (field == 7u) {
        r = v.inside;
    } else if (field == 8u) {
        r = vu_clipw(v.clip);
    }
    return r;
}

// What a mesh program draws of the triangle kick-2, kick-1, kick (a, b, c)
// at its corner (0..2) under its marks (vu_kick_marks): mode VU_CLIP_*, the programs without
// a scissor or no-test variant passing VU_CLIP_REGION whatever vu_draw.z
// says; lo, hi the region test's bounds; chain (a literal): the program
// has the chain clip plane and the later mark (normal_c, normal_l).
VuVSOut vu_mesh_out(VuVtx a, VuVtx b, VuVtx c, uint kick, uint corner, uint marks, uint mode,
                    float4 lo, float4 hi, bool chain)
{
    VuVtx me = vu_pick(a, b, c, corner);
    uint rule = VU_RULE_EXACT;
    if (mode == VU_CLIP_REGION) {
        rule = vu_rule_card(vu_region_rule(a, b, c, lo, hi), marks, kick);
    }
    uint exact;
    VuVSOut o = vu_triangle_out_pos(a, b, c, me, mode, rule, exact);
    VuPlaneTri t = vu_plane_tri(a, b, c);
    o = vu_attr_planes(o, t, a, b, c, exact);
    if (chain) {
        o = vu_plane_out(o, t, kick, corner);
        if ((marks & VU_MARK_LATER) != 0u) {
            o = vu_later_out(o, a, b, c);
        }
    }
    return o;
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
