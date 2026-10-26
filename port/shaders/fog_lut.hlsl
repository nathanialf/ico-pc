// fog_lut.hlsl: the depth fog of fog_DrawFog (ico2/seki/src/ZFog.c),
// RD_POST_FOG.
//
// What the GS does: the Z buffer (PSMZ32) is copied pixel for pixel into a
// PSMCT32 buffer, a PSMT4 view of that buffer copies the third byte of every
// 32-bit word into the fourth, and the fog sprite reads the buffer as PSMT8H
// (the top byte of each PSMCT32 word) through the 256-entry fog CLUT, with
// MODULATE by (0x80, 0x80, 0x80, fogStrength), ALPHA 0x44 and a Z test
// GEQUAL at sprite Z 0xFFFFFF under ZMSK. So the CLUT index is bits 16..23
// of the 32-bit Z, and only pixels with Z <= 0xFFFFFF are fogged.
//
// fog_lut_ps runs behind sprite_ui_vs / sprite_world_vs (the fog sprite's
// two corners, expanded by rd_replay.c) and replaces the texture fetch:
//   t1  the Z source's depth target itself (D32F, sampled as depth;
//       rd_replay.c moves it to the depth-read state for the fog pass, which
//       binds no depth attachment), where the GS reads a copy of the Z buffer
//   t2  the 256x1 RGBA8 LUT in index order (ZFog.c's CLUT unswizzled from
//       its CSM1 storage order)
//   g_tex.xy   size of the depth source in GS pixels (the UV's texel units;
//              the native frame's width, rd_replay.c doFog)
//   g_scale    xy the depth's texels per GS pixel, z its Full pixel crop
//              (common.hlsli; the UV then normalised to its own width)
//   g_col.x    the sprite's GS Z
//   g_col.y    the Z test (RdZTest: ALWAYS 1, GEQUAL 2, GREATER 3, NEVER 0)
//   g_param.x  the depth source's GS Z scale (PSMZ32: 2^-33 on a float
//              depth buffer, 2^-32 on D24S8; rd__target_z_scale)
//   g_param.y  2^24 - 1 when the depth source is 24-bit UNORM (D24S8),
//              0 on a float one
//   g_param.z  1: the depth read is replaced by 0 (a test switch,
//              ICO_RD_FOG_SABOTAGE: the reads of a device that returns
//              nothing), else 0
//   g_param.w  1 above 1x (the colour or the depth target scaled,
//              rd_replay.c doFog), else 0
//   g_uvRect.xy with g_param.w, the bound target's centre texel (its
//              texture size halved, rounded down: the native frame's
//              centre, rd_core.c rd__target_scale_of), the dither's origin
// Above 1x the table is read between entries: n = Z bits 16..23 and the
// next eight bits f interpolate LUT[n] .. LUT[n + 1] (ZFog.c builds the
// table as a linear ramp sampled at 256 depths, and the scaled depth
// carries Z between them), the texture function applied in the same fixed
// point, and a one-unit 8x8 ordered dither (common.hlsli gs_fog_channel,
// gs_bayer8) carries the fraction the 8-bit alpha cannot hold. The dither
// is indexed from the target's centre texel, so the 4:3 columns of a wide
// frame carry the same pattern as the 4:3 frame. At scale 1 g_param.w is 0
// and the entry is read as the GS reads it.
// fog_lut_buffer_ps (FOG_BUFFER) reads t1 as an R32_UINT colour texture
// instead: the depth aspect copied out to a buffer and back into a colour
// texture (rd_fog_path.c), for a device whose depth reads fail.  The words
// are the depth's bits: a float on D32S8, and on D24S8 the 24-bit step in
// the low bits with the top byte undefined, so it is masked off.
// fog_lut_ms_ps (FOG_MS, the MSAA epoch with sample-rate shading): t1 is
// the multisampled scene depth (Texture2DMS) and the shader runs per sample
// (SV_SampleIndex), reading that sample's depth, so the fog of each sample
// is the fog of its own surface (rd_replay.c doFog).
// fog_lut_local_ps (FOG_LOCAL, Vulkan: VK_KHR_dynamic_rendering_local
// read): the fog drawn inside the scene pass, its depth the pass's own
// depth attachment read as an input attachment (SubpassLoad: the stored
// depth of the fragment's own texel, after rhi_cmd_local_read_barrier made
// the pass's depth writes visible), group 2 rd's layoutLocal (t1 the input
// attachment, t2 the LUT). It is taken only where the fog's depth is the
// colour target's own at the same size (rd_replay.c doFog), where the
// texel the sprite's UV addresses below is the fragment's own: the same
// stored word the copy path's Load returns, through the same arithmetic.
// The pipeline binds the depth with its test ALWAYS and no write (the test
// is done here, as on every path). Its DXIL is the plain entry's (never
// used: D3D12 has no local read).
// The Z test is done here because the depth buffer being read cannot also be
// the bound attachment; it compares the same depth values the pipeline
// would (gs_z_to_depth of the sprite Z against the stored depth).
#include "common.hlsli"

// The sprite vertex shaders' output (sprite.hlsl SpriteVSOut).
struct FogPSIn {
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;   // 0..255
    VK_LOC(1) float2 uv : TEXCOORD0; // normalised by g_tex.zw
#ifdef FOG_MS
    uint sampleIndex : SV_SampleIndex;
#endif
};

#if defined(FOG_LOCAL) && defined(__spirv__)
// the depth attachment: no InputAttachmentIndex (the depth's mapping
// without VkRenderingInputAttachmentIndexInfoKHR); binding 17 = t1
SubpassInput<float> g_fogDepthIn : ICO_REG(t1, 2);
#define FOG_LOCAL_READ 1
#elif defined(FOG_MS)
Texture2DMS<float> g_fogDepth : ICO_REG(t1, 2);
#elif defined(FOG_BUFFER)
Texture2D<uint> g_fogDepth : ICO_REG(t1, 2);
#else
Texture2D<float> g_fogDepth : ICO_REG(t1, 2);
#endif
Texture2D<float4> g_fogLut : ICO_REG(t2, 2);

#define FOG_ZTST_NEVER 0u
#define FOG_ZTST_ALWAYS 1u
#define FOG_ZTST_GEQUAL 2u
#define FOG_ZTST_GREATER 3u

// The GS Z a stored depth d stands for: the inverse of gs_z_to_depth, z =
// d / scale (a power of two: exact), and for PSMZ32 on a float depth buffer
// (GS_ZSCALE_32F) the top band [1 - 2^-8, 1) back to 0xFFFF0000 + its
// steps.  Z clamps to 2^32 - 256.
uint fog_gs_z(float d, float scale)
{
    if (scale < 1.5e-10 && d >= 1.0 - 1.0 / 256.0) {
        return 0xFFFF0000u + (uint)((d - (1.0 - 1.0 / 256.0)) * 16777216.0);
    }
    float zf = clamp(d / scale, 0.0, 4294967040.0);
    return (uint)zf;
}

GS_PS_ATTR GS_PS_OUT fog_lut_ps(FogPSIn i)
{
    // the texel the sprite's UV addresses, nearest (ZFog.c writes TEX1 0);
    // in the depth target's own texels (g_scale, 1 in Original)
#ifndef FOG_LOCAL_READ
    float2 sc = float2(g_scale.x > 0.0 ? g_scale.x : 1.0, g_scale.y > 0.0 ? g_scale.y : 1.0);
    float2 fsize = g_tex.xy * sc;
    if (g_scale.z != 0.0) {
        // a Full pixel crop of the frame (common.hlsli gs_uv_norm): the UV
        // is normalised to the texture's own width
        uint dw, dh;
#ifdef FOG_MS
        uint dn;
        g_fogDepth.GetDimensions(dw, dh, dn);
#else
        g_fogDepth.GetDimensions(dw, dh);
#endif
        fsize.x = float(dw);
    }
    int2 size = int2(round(fsize));
    int2 tc = clamp(int2(floor(i.uv * fsize)), int2(0, 0), size - 1);
#endif
#ifdef FOG_LOCAL_READ
    float d = g_fogDepthIn.SubpassLoad();
#elif defined(FOG_MS)
    float d = g_fogDepth.Load(tc, int(i.sampleIndex));
#elif defined(FOG_BUFFER)
    uint raw = g_fogDepth.Load(int3(tc, 0));
    // the step as the sampled UNORM depth gives it (raw / (2^24 - 1)), or
    // the float's own bits
    float d = g_param.y > 0.0 ? float(raw & 0xFFFFFFu) / 16777215.0 : asfloat(raw);
#else
    float d = g_fogDepth.Load(int3(tc, 0));
#endif
    if (g_param.z > 0.5) {
        d = 0.0;
    }
    uint ztst = g_col.y;
    uint z = fog_gs_z(d, g_param.x);
    if (ztst != FOG_ZTST_ALWAYS) {
        // GS GEQUAL (sprite Z >= buffer Z) is depth(sprite) >= depth(buffer)
        float ds = gs_z_to_depth(g_col.x, g_param.x);
        if (g_param.y > 0.0) {
            // a 24-bit depth buffer holds the step nearest each depth, so
            // the sprite's depth is compared as the step it would store, as
            // the GS compares integers: 0xFFFFFF at 2^-32 is step 65535.996,
            // and a pixel drawn at Z 0xFFFF80 .. 0xFFFFFF stores step 65536,
            // above the sprite's float depth, which failed GEQUAL
            ds = round(ds * g_param.y);
            d = round(d * g_param.y);
        }
        bool passed = ztst == FOG_ZTST_GEQUAL ? ds >= d : (ztst == FOG_ZTST_GREATER ? ds > d : false);
        if (!passed) {
            GS_DISCARD;
        }
        // a pixel that passed has a Z at most the sprite's (GEQUAL) or below
        // it (GREATER): where the depth step rounds Z up past it (0xFFFFFF
        // stores as 2^24), take the bound
        uint zs = ztst == FOG_ZTST_GEQUAL ? g_col.x : g_col.x - 1u;
        z = min(z, zs);
    }
    // PSMT8H after the PSMT4 byte copy: bits 16..23 of the GS Z
    uint idx = (z >> 16) & 0xFFu;
    uint4 t = uint4(floor(g_fogLut.Load(int3(int(idx), 0, 0)) * 255.0 + 0.5));
    uint4 col = uint4(floor(i.col + 0.5));
    if (g_param.w > 0.5) {
        // above 1x: between the entries, dithered (the header)
        const uint4 hi =
            uint4(floor(g_fogLut.Load(int3(int(min(idx + 1u, 255u)), 0, 0)) * 255.0 + 0.5));
        const uint f = (z >> 8) & 0xFFu;
        const int2 p = int2(floor(i.pos.xy)) - int2(g_uvRect.xy);
        const uint b = gs_bayer8(uint(p.x), uint(p.y));
        const bool decal = (g_mode.x & DF_DECAL) != 0u;
        uint4 o;
        o.r = gs_fog_channel(t.r, hi.r, f, decal ? 128u : col.r, b);
        o.g = gs_fog_channel(t.g, hi.g, f, decal ? 128u : col.g, b);
        o.b = gs_fog_channel(t.b, hi.b, f, decal ? 128u : col.b, b);
        o.a = (g_mode.x & DF_TCC_RGBA) != 0u ? gs_fog_channel(t.a, hi.a, f, decal ? 128u : col.a, b)
                                              : col.a;
        col = o;
    } else {
        col = gs_texture_function(t, col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        GS_DISCARD;
    }
    GS_RETURN(col, i.pos);
}
