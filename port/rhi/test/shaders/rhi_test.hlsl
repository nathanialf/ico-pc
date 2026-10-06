// rhi_test.hlsl: the shaders of port/rhi/test/rhi_vk_test.c, compiled by
// gen_shaders.sh with DXC to SPIR-V (rhi_test_spv.h). Register spaces are
// RHI bind groups; gen_shaders.sh gives the DXC flags that map
// registers to Vulkan bindings.

struct VSIn {
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float4 col : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 pos : SV_Position;
    [[vk::location(0)]] float4 col : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

// group 0: the test's uniform block (the game's per-frame b0)
cbuffer TestCB : register(b0, space0) {
    float4 g_src1; // dual-source output 1
    uint4 g_uint;  // value written to the RGBA8_UINT target
};

// group 2: textures t1.. and samplers s1..
Texture2D<float4> g_tex : register(t1, space2);
SamplerState g_smp : register(s1, space2);

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 1.0);
    o.col = i.col;
    o.uv = i.uv;
    return o;
}

struct DualOut {
    [[vk::location(0), vk::index(0)]] float4 c0 : SV_Target0;
    [[vk::location(0), vk::index(1)]] float4 c1 : SV_Target1;
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
