// raw_wrap.hlsl (renderer wave 5, R5c):
// screen prims blended additively or subtractively under COLCLAMP 0, where
// the GS keeps the low 8 bits of every blend result (the result wraps).
// darkVolume.c's spheres add their colour on the faces in front of the scene
// and its two's complement on the faces behind, so every pixel of its count
// buffer ends at a sum modulo 256.
//
// The hardware blender clamps, so rd_replay.c (doScreenWrap) draws such a
// command in two steps:
//   wrap_acc_ps      into an RGBA16F accumulator cleared to 0, with the
//                    pipeline's ONE + ONE on colour and ONE / ZERO on alpha:
//                    each fragment adds the GS blend term (Cs * F) >> 7
//                    (F = FIX or As, negated for Cd - Cs * F) reduced to
//                    -128..127, the same value modulo 256, and writes alpha
//                    As + 1 (the GS stores As; the last fragment wins, as in
//                    primitive order). Sixteen layers of a pixel stay within
//                    +-2048, where every integer is exact in half precision.
//   wrap_resolve_ps  then, fullscreen, into the target: RGB = (Cd + acc)
//                    mod 256 with Cd read from a copy of the target taken just
//                    before, A = acc.a - 1 where a fragment was written, else
//                    Cd's alpha.
// The sprite vertex shaders feed wrap_acc_ps (SpriteVSOut's layout), blit_vs
// feeds wrap_resolve_ps.
//
// DrawCB: as sprite_ps (g_mode, g_blend.y = FIX), g_param.x < 0 for the
// subtractive equations (ALPHA modes 1 and 6).
#include "common.hlsli"

struct WrapAccIn
{
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;   // 0..255, interpolated
    VK_LOC(1) float2 uv : TEXCOORD0; // normalised
};

Texture2D<float4> g_texture : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
// wrap_resolve_ps: the destination as it was before the command
Texture2D<float4> g_dest : register(t2, space2);

float4 wrap_acc_ps(WrapAccIn i) : SV_Target0
{
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint4 t = uint4(floor(g_texture.Sample(g_sampler, gs_block_uv(i.uv)) * 255.0 + 0.5));
        t = gs_texa_expand(t, g_mode.y & 0xFFu, g_mode.y >> 8);
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    uint a = col.a;
    if ((g_mode.x & DF_FBA) != 0u) {
        a |= 0x80u;
    }
    uint f = (g_mode.x & DF_FIX_FACTOR) != 0u ? g_blend.y : a;
    int3 t3 = int3((col.rgb * f) >> 7);
    if (g_param.x < 0.0) {
        t3 = -t3;
    }
    int3 s = ((t3 + 128) & 255) - 128;
    return float4(float3(s), float(a) + 1.0);
}

struct WrapResolveIn
{
    float4 pos : SV_Position;
    VK_LOC(0) float2 uv : TEXCOORD0;
};

float4 wrap_resolve_ps(WrapResolveIn i) : SV_Target0
{
    int3 p = int3(int2(i.pos.xy), 0);
    float4 acc = g_texture.Load(p);
    float4 dst = g_dest.Load(p);
    int3 d = int3(floor(dst.rgb * 255.0 + 0.5));
    int3 r = (d + int3(round(acc.rgb))) & 255;
    float a = acc.a > 0.5 ? acc.a - 1.0 : floor(dst.a * 255.0 + 0.5);
    return float4(float3(r), a) * (1.0 / 255.0);
}
