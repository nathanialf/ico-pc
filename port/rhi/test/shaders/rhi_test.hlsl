// rhi_test.hlsl: the shaders of port/rhi/test/rhi_vk_test.c, compiled by
// gen_shaders.sh with DXC to SPIR-V (rhi_test_spv.h). Register spaces are
// RHI bind groups; gen_shaders.sh gives the DXC flags that map
// registers to Vulkan bindings. The Direct3D 11 backend's tests compile the
// same text at run time with FXC (shader model 5.0: rhi_test_hlsl.h, the file
// as a C string), which has no register spaces and no [[vk::...]]
// attributes: under ICO_D3D11 REG(r, s) drops the space (as
// port/shaders/reg.hlsli does for the game's shaders), and the Vulkan
// attributes are spelled only when DXC builds SPIR-V.
#ifdef ICO_D3D11
#define REG(r, s) register(r)
#else
#define REG_SPACE(s) space##s
#define REG(r, s) register(r, REG_SPACE(s))
#endif
#ifdef __spirv__
#define VK_LOC(n) [[vk::location(n)]]
#define VK_DUAL(n, i) [[vk::location(n), vk::index(i)]]
#else
#define VK_LOC(n)
#define VK_DUAL(n, i)
#endif

struct VSIn {
    VK_LOC(0) float3 pos : POSITION;
    VK_LOC(1) float4 col : COLOR0;
    VK_LOC(2) float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0;
    VK_LOC(1) float2 uv : TEXCOORD0;
};

// group 0: the test's uniform block (the game's per-frame b0)
cbuffer TestCB : REG(b0, 0) {
    float4 g_src1; // dual-source output 1
    uint4 g_uint;  // value written to the RGBA8_UINT target
};

// group 2: textures t1.. and samplers s1..
Texture2D<float4> g_tex : REG(t1, 2);
SamplerState g_smp : REG(s1, 2);

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 1.0);
    o.col = i.col;
    o.uv = i.uv;
    return o;
}

struct DualOut {
    VK_DUAL(0, 0) float4 c0 : SV_Target0;
    VK_DUAL(0, 1) float4 c1 : SV_Target1;
};

DualOut ps_dual(VSOut i) {
    DualOut o;
    o.c0 = i.col;
    o.c1 = g_src1;
    return o;
}

float4 ps_color(VSOut i) : SV_Target0 {
    return i.col;
}

float4 ps_tex(VSOut i) : SV_Target0 {
    return g_tex.Sample(g_smp, i.uv);
}

uint4 ps_uint(VSOut i) : SV_Target0 {
    return g_uint;
}

// multisampling: the depth of a multisampled depth-stencil (t1, read with
// Load at each sample) resolved to the largest sample's (the renderer's
// GEQUAL depth: the nearest), written as SV_Depth into a single-sampled
// depth target over the whole viewport (the vertices of a full quad)
Texture2DMS<float> g_msDepth : REG(t1, 2);

float ps_depth_resolve(VSOut i) : SV_Depth {
    uint w, h, n;
    g_msDepth.GetDimensions(w, h, n);
    int2 p = int2(i.pos.xy);
    float d = 0.0;
    for (uint s = 0; s < n; s++) {
        d = max(d, g_msDepth.Load(p, int(s)));
    }
    return d;
}

// The destination read (rhi.h RhiDstRead): the GS's LERP
// ((Cs - Cd) * As >> 7) + Cd of the vertex colour (As its alpha byte, 0x80 =
// 1.0, no clamp of the factor; the result clamped to 0..255) over the
// target's own pixel, alpha As, computed in the shader.
uint4 dst_lerp(uint4 s, uint4 d)
{
    const int3 v = (((int3(s.rgb) - int3(d.rgb)) * int(s.a)) >> 7) + int3(d.rgb);
    return uint4(uint3(clamp(v, int3(0, 0, 0), int3(255, 255, 255))), s.a);
}

// RHI_DST_READ_STORAGE: the target a rasterizer-ordered view (u0, group 3;
// Vulkan: a storage image under fragment shader interlock), no colour
// target, the tests early
#ifdef __spirv__
[[vk::image_format("rgba8")]]
#endif
globallycoherent RasterizerOrderedTexture2D<unorm float4> g_rov : REG(u0, 3);

[earlydepthstencil] void ps_rov(VSOut i)
{
    const uint2 p = uint2(i.pos.xy);
    const uint4 d = uint4(floor(g_rov[p] * 255.0 + 0.5));
    const uint4 s = uint4(floor(i.col * 255.0 + 0.5));
    g_rov[p] = float4(dst_lerp(s, d)) * (1.0 / 255.0);
}

// RHI_DST_READ_ATTACHMENT (Vulkan): colour attachment 0 read as an input
// attachment (group 3, the t-register binding 16); the DXIL build, never
// used (no attachment read on D3D12), writes the vertex colour
#ifdef __spirv__
[[vk::input_attachment_index(0)]] [[vk::binding(16, 3)]] SubpassInput<float4> g_dstIn;

float4 ps_subpass(VSOut i) : SV_Target0
{
    const uint4 d = uint4(floor(g_dstIn.SubpassLoad() * 255.0 + 0.5));
    const uint4 s = uint4(floor(i.col * 255.0 + 0.5));
    return float4(dst_lerp(s, d)) * (1.0 / 255.0);
}
#else
float4 ps_subpass(VSOut i) : SV_Target0
{
    return i.col;
}
#endif
