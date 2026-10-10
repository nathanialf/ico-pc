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
//          vertices (an unstripped Mesh3D buffer's MSCNT: 1), zw reserved
cbuffer VuCB : register(b2, space1)
{
    float4 vu_mem[36];
    uint4 vu_draw;
    uint4 vu_batch;
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
// The parts of a wide target beside the 4:3 picture are drawn with
// VU_F_DROP_WIDE: a triangle marked VU_INDEX_WIDE (a stage closing plane,
// rd_mesh.c markWideHidden) is not drawn there (vu_wide_dropped)
#define VU_F_DROP_WIDE 128u
// Photo mode's free camera (RdCamera.freeCamera): every triangle failing the
// region test, and every particle outside its x/y window, is drawn
// GPU-clipped over the whole picture, a vertex behind the eye or not
// (vu_triangle_out, vu_particle_vs)
#define VU_F_BEHIND_EYE 256u
// Issue 25: the index bit of a triangle that overlaps an earlier triangle of
// its mesh in the same plane (shader_consts.h ICO_VU_INDEX_LATER; static
// prelit and lit meshes, rd_mesh.c markLaterOverlaps); vu_later_out
#define VU_INDEX_LATER 0x40000000u
#define VU_INDEX_WIDE 0x20000000u // shader_consts.h ICO_VU_INDEX_WIDE
#define VU_INDEX_MASK 0x1FFFFFFFu

// Whether the triangle of index value vid is left out of this draw: a
// closing plane in a draw of the parts beside the 4:3 picture.
bool vu_wide_dropped(uint vid)
{
    return (vid & VU_INDEX_WIDE) != 0u && (vu_draw.z & VU_F_DROP_WIDE) != 0u;
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

// The centre the wide projection squeezes x about: the scene's XYOFFSET is
// 2048 - w/2, so 2048 is the middle of the picture (ICO_VU_REGION_CX).
static const float VU_REGION_CX = 2048.0;

// The position the region test compares (vu_inside with VU_LO0 and
// VU_HI4094: normal_c code 32, normal_l 32, 34 and 38, the grid program).
// On a wide screen a scene draw is squeezed in x by g_space[SPACE_WORLD].x
// about the centre, so the picture reaches far past the 4:3 frame and a
// triangle near its sides can have a vertex beyond GS X 4094: the whole
// triangle was dropped although most of it was on screen (water near the
// edges popped in and out). The test takes x where the squeezed picture
// puts it instead, so the GS window keeps the same share of the picture as
// at 4:3. Only the test uses it; the drawn position is p unchanged (no
// 16-bit wrap in this mode, vu_triangle_out). A scale of exactly 1 (4:3,
// the Original preset, stretched draws, targets that are not wide) returns
// p untouched: (x - 2048) * 1 + 2048 is not exact for every float x (a tiny
// positive x rounds to 0 and would fail 0 < x). precise: the CPU reference
// (vu1_ref_internal.h vu_region_x) rounds the subtract, the multiply and
// the add one at a time.
float4 vu_region_pos(float4 p)
{
    float f = g_space[SPACE_WORLD].x;
    if (f != 1.0) {
        precise float x = (p.x - VU_REGION_CX) * f + VU_REGION_CX;
        p.x = x;
    }
    return p;
}

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
struct VuVtx
{
    float4 h;     // the screen matrix times pos, before the divide (w: the clip w)
    float4 p;     // xyz divided by w (pixels; z = GS Z / 16), w = h.w
    int3 gs;      // ftoi4 of p.xyz: GS X, Y (12.4) and Z
    uint4 rgba;   // RGBAQ, 0..255
    float3 stq;   // PACKED ST
    bool inside;  // the region test
    float4 clip;  // scissor programs: model to clip space (mem[20..23]) times pos
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

struct VuVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) noperspective float4 col : COLOR0;   // RGBA 0..255, the GS interpolates in screen space
    VK_LOC(1) noperspective float3 stq : TEXCOORD0; // S, T, Q linear in screen space, divided per pixel
    // 1: a triangle or sprite the PS2 drops that the port draws only beside
    // the 4:3 picture (vu_beside_discard); 0: drawn everywhere. The same at
    // every corner of a triangle, so flat.
    VK_LOC(2) nointerpolation uint beside : TEXCOORD1;
};

// GS pixels (XYOFFSET-relative window coordinates / 16) to clip space in
// WORLD space; gs_xy_to_ndc for signed and wider-than-16-bit values.
// precise: common.hlsli gs_xy_to_ndc.
float2 vu_ndc(float2 px)
{
    precise float2 q = px - g_origin.xy + g_origin.zw;
    precise float2 ndc = float2(q.x * g_target.z * 2.0 - 1.0, 1.0 - q.y * g_target.w * 2.0);
    precise float2 r = ndc * g_space[SPACE_WORLD].xy + g_space[SPACE_WORLD].zw;
    return r;
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
    float2 o = g_origin.xy - g_origin.zw;
    float2 s = g_space[SPACE_WORLD].xy;
    float2 t = g_space[SPACE_WORLD].zw;
    // precise: common.hlsli gs_xy_to_ndc
    precise float x = ((h.x - o.x * h.w) * g_target.z * 2.0 - h.w) * s.x + t.x * h.w;
    precise float y = (h.w - (h.y - o.y * h.w) * g_target.w * 2.0) * s.y + t.y * h.w;
    precise float z = 16.0 * h.z * g_z.x; // gs_z_to_depth's z * scale
    return float4(x, y, z, h.w);
}

// The vertex the GS gets, from a loop's vertex: the ftoi4 value, on the
// 12.4 grid at every scale, so a VU vertex lands where the
// GIF and CPU paths put the same point (sprite.hlsl) and two meshes'
// shared vertices the GS merges stay merged (issue 26: a crack between
// them showed the clear, which the fog pass paints at full fog).  g_z.w set
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

// Issue 25: whether the GS position of a vertex is the one the GS would
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
// form.  The homogeneous depth, Z * g_z.x linear, differed from gs_depth's
// value enough that a coplanar pass (the specular pass, 34/38) won or lost
// GEQUAL per pixel.  (Made for issue 25, but the near railing's shimmer the
// user's dumps showed is a code-32 draw: vu_later_out.)
float4 vu_cut_position(VuVtx v)
{
    return vu_has_gs_position(v) ? vu_vtx_position(v) : vu_homogeneous_position(v.h);
}

static const float4 VU_CULLED = float4(2.0, 2.0, 2.0, 1.0); // outside x <= w: clipped away

VuVSOut vu_out_init()
{
    VuVSOut o;
    o.pos = VU_CULLED;
    o.col = float4(0.0, 0.0, 0.0, 0.0);
    o.stq = float3(0.0, 0.0, 1.0);
    o.beside = 0u;
    return o;
}

// Whether the draw's target has room beside the 4:3 picture: a wide scene
// draw (x scale below 1). At 4:3, in the Original preset, for a stretched
// draw or a target that is not wide, the 4:3 picture is the whole target.
bool vu_has_beside()
{
    return g_space[SPACE_WORLD].x < 1.0;
}

// Whether framebuffer x (SV_Position.x, in the target's texels) lies in the
// 4:3 picture: the target's GS pixels 0..w before the wide x scale f, which
// land on texels (w/2)(1 - f) sx .. (w/2)(1 + f) sx, rounded outwards so a
// texel the 4:3 picture touches is in it. The same texels as rd_replay.c
// vuWideParts, computed in the same order.
bool vu_in_picture(float x)
{
    float f = g_space[SPACE_WORLD].x;
    float sx = g_z.y > 0.0 ? g_z.y : 1.0;
    float c = g_target.x * 0.5;
    precise float fc = f * c;
    precise float l = (c - fc) * sx;
    precise float r = (c + fc) * sx;
    float t = floor(x);
    return t >= floor(l) && t < ceil(r);
}

// The PS2 does not draw a triangle that fails the region test, nor a
// particle outside its x/y window. Inside the 4:3 picture the port keeps
// that rule exactly (scenery the camera sits inside, like the tree trunk
// over the opening cutscene, stays out of the picture); beside it, where
// the PS2 showed nothing, it draws such a triangle clipped by the GPU so
// the edges of a wide screen have no holes.
bool vu_beside_discard(VuVSOut i)
{
    return i.beside != 0u && vu_in_picture(i.pos.x);
}

// The triangle k-2, k-1, k: what the GS draws of it. me is the corner this invocation outputs.
// mode: VU_CLIP_*; the programs without a scissor or no-test variant pass
// VU_CLIP_REGION whatever vu_draw.z says.
VuVSOut vu_triangle_out(VuVtx a, VuVtx b, VuVtx c, VuVtx me, uint mode)
{
    VuVSOut o = vu_out_init();
    o.col = float4(me.rgba);
    o.stq = me.stq;
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
        return o;
    }
    if (mode == VU_CLIP_REGION) {
        // On the PS2 any vertex outside the region sets ADC on itself and
        // the next two, and the triangle is not drawn. Inside the 4:3
        // picture the port keeps that rule exactly: the game's cameras pass
        // through and beside scenery that relies on it (the opening after
        // the Sony sign films the castle from inside a cliff, st26a_p1, and
        // the forest shot after it from inside a tree card, st26a_near;
        // drawn there, they covered the picture with stretched rock and a
        // dark trunk). Beside the 4:3 picture of a wide target, where the
        // PS2 showed nothing, every such triangle is drawn and clipped by
        // the GPU at 0 <= z <= w, a vertex behind the eye (w <= 0) or not,
        // and its pixels inside the picture are discarded (VuVSOut.beside,
        // vu_beside_discard), so the picture itself is the PS2's. Beside it
        // the dropped triangles left holes: a wall with one vertex past the
        // window, and at 48:9 a patch of the sea at the far left of the old
        // bridge whose only triangles there have a vertex behind the eye.
        // Photo mode's free camera (VU_F_BEHIND_EYE) has no such placing,
        // so there every failing triangle is drawn, clipped, everywhere (a
        // walkway low under the camera left a hole). A triangle with all
        // three vertices inside is drawn exactly as before (vu_vtx_position,
        // on the ftoi4 grid); a drawn one with a vertex outside takes code
        // 36's corner rule (vu_cut_position: the GS position where the
        // vertex has one, the homogeneous form of h where it is behind the
        // eye, saturated or below GS Z 0; every program computes h before
        // the test). The region test itself still runs (VuVtx.inside, the
        // tests' probe field), as the PS2's.
        // Two differences from a code-36 draw of the same triangle remain:
        // code 36's clipped triangles go through their own earlier pass
        // with ABE forced on (rd_replay.c draws them as the VU's fans,
        // PRIM 0x5D), which a code-32 draw does not, so a mesh whose clip
        // code flips between 32 and 36 still changes the blending along its
        // cut rim; and vu_later_out's bias needs all three GS positions, so
        // a later-marked triangle with a corner saturated or below GS Z 0
        // is drawn without it.
        if (a.inside && b.inside && c.inside) {
            o.pos = vu_vtx_position(me);
        } else if ((vu_draw.z & VU_F_BEHIND_EYE) != 0u) {
            o.pos = vu_cut_position(me);
        } else if (vu_has_beside()) {
            o.pos = vu_cut_position(me);
            o.beside = 1u;
        }
        return o;
    }
    // VU_CLIP_SCISSOR
    uint fa = vu_clipw(a.clip), fb = vu_clipw(b.clip), fc = vu_clipw(c.clip);
    bool cut = (fa | fb | fc) != 0u;
    if ((cut && (vu_draw.z & VU_F_KICK_ONLY) != 0u) ||
        (!cut && (vu_draw.z & VU_F_CUT_ONLY) != 0u)) {
        return o; // drawn by the other pass
    }
    if (!cut) {
        o.pos = vu_vtx_position(me); // kicked as it is
    } else if ((fa & fb & fc) != 0u) {
        return o; // trivially rejected (the six fcor tests)
    } else if (((fa | fb | fc) & 0x30u) != 0u || min(a.h.w, min(b.h.w, c.h.w)) <= 0.0) {
        // SCISSOR_COMMON; the GPU clips (at 0 <= z <= w, not the VU's
        // z = -w: that would need SV_ClipDistance)
        o.pos = vu_cut_position(me);
    } else {
        // only x/y flags: the VU clips to its guard band (the 1500 unit
        // clip window, wider than any target), the GS scissor does the rest
        o.pos = vu_vtx_position(me);
    }
    return o;
}

// Issue 25: a triangle marked VU_INDEX_LATER and the earlier one it
// overlaps are one plane in the model (the two faces of the railing's
// lattice, split along other diagonals), where an exact GS's GEQUAL would
// let the later pass over the earlier at every pixel.  ftoi4 rounds each
// vertex's X, Y and Z on its own, so two triangulations of a plane differ
// in depth by up to (|dZ/dX| + |dZ/dY|) / 8 + 2 GS units, either way, and
// which one won flipped region by region with sub-pixel camera motion (the
// near railing's shimmer).  The later triangle's depth rises by twice that
// bound (the gradient from its own GS vertices) plus 512 GS units (two
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
    o.pos = float4(px.x * g_target.z * 2.0 - 1.0, 1.0 - px.y * g_target.w * 2.0, 0.5, 1.0);
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

// sprite_ps with STQ: the texel at S/Q, T/Q (the texture is padded to
// 2^TW x 2^TH, so STQ is normalised already; a texture with GS levels at
// the GS's level by Q, gs_sample), texture function, TEXA, alpha test,
// DATE, dual-source output.
DualOut vu_pixel(VuVSOut i)
{
    if (vu_beside_discard(i)) {
        discard;
    }
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        float2 uv = gs_block_uv(i.stq.xy / i.stq.z); // widescreen reflections: the pool's grids
        uint4 t = uint4(floor(gs_sample(g_texture, g_sampler, uv, i.stq.z) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

// vu_texa_ps: vu_pixel with TEXA per texel before the
// bilinear weights (gs_texa_texture), for the draws rd_replay.c's VU
// planner gives it (the texture formats and TEXA modes of sprite_texa_ps).
DualOut vu_pixel_texa(VuVSOut i)
{
    if (vu_beside_discard(i)) {
        discard;
    }
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        float2 uv = gs_block_uv(i.stq.xy / i.stq.z);
        uint4 t = (g_mode.x & DF_GS_LOD) != 0u
                      ? gs_texa_texture_lod(g_texture, uv, i.stq.z, g_mode.x)
                      : gs_texa_texture(g_texture, g_sampler, uv, g_mode.x);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    return gs_dual_out(col, g_mode.x, g_blend.y);
}

#endif
