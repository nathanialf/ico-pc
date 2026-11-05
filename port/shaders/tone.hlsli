// tone.hlsli: the colour adjustment (Options > Picture: Gamma, Picture
// brightness, Contrast, Saturation; RdSettings.tone*, rd_present.c
// rd__tone_params). Included by blit.hlsl (blit_tone_ps, blit_depth_tone_ps:
// the present's box blit) and crt.hlsl (the CRT filter's source reads).
//
// PCSX2's shade boost arithmetic (bin/resources/shaders/dx11/shadeboost.fx
// ContrastSaturationBrightness), its parameters as PCSX2 scales them
// (pcsx2/GS/Renderers/Common/GSDevice.cpp GSDevice::ShadeBoost: each 1..100
// setting times 1 / 50, so 50 is 1.0, the identity): p.x brightness, p.y
// contrast, p.z saturation, p.w gamma. rgb and the result are 0..1 (the
// result is not clamped here: the callers saturate).
//
// The one deviation: conColor is clamped to 1e-6 before log2 (a contrast
// above 50 takes a dark channel below 0, where PCSX2's log2 is NaN; crt.hlsl's
// powp has the same guard): such a channel comes out as 1e-6 ^ (1 / gamma),
// under half a level (0.26 at gamma 100). rd_present_test's reference
// applies the same clamp.
// No pow: its possibly negative base is an FXC warning (X3571), which the
// Direct3D 11 warning gate fails.

#ifndef ICO_TONE_HLSLI
#define ICO_TONE_HLSLI

float3 tone_adjust(float3 rgb, float4 p)
{
    const float3 lumCoeff = float3(0.2125, 0.7154, 0.0721);
    float3 brtColor = rgb * p.x;
    float intensity = dot(brtColor, lumCoeff);
    float3 satColor = lerp(float3(intensity, intensity, intensity), brtColor, p.z);
    float3 conColor = lerp(float3(0.5, 0.5, 0.5), satColor, p.y);
    return exp2(log2(max(conColor, float3(1e-6, 1e-6, 1e-6))) * (1.0 / p.w));
}

#endif
