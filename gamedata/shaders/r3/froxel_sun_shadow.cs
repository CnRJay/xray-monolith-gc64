#include "froxel_common.hlsli"

Texture2D<float> g_SunSmap : register(t0);
SamplerComparisonState g_SmpShadowCmp : register(s0);
RWTexture3D<float> g_SunVisOut : register(u0);

[numthreads(8, 8, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint3 dims = uint3(vGridParams.xyz);
    if (any(id >= dims))
        return;

    float jitter = FroxelIGN(id, vWindAndJitter.w);
    float sliceNorm = (float(id.z) + jitter) * vInvGridParams.z;
    float linearZ = SliceToLinearDepth(sliceNorm);

    // Only touch slices that belong to this cascade's depth interval
    if (linearZ < vCascadeAndLights.x || linearZ > vCascadeAndLights.y)
        return;

    float3 worldPos = FroxelToWorldPos(id, jitter);
    float4 shadowCoord = mul(float4(worldPos, 1.0f), mSunCascadeXform);
    float invW = 1.0f / max(abs(shadowCoord.w), 1e-6f);
    float3 tc = shadowCoord.xyz * invW;

    if (tc.x <= 0.002f || tc.x >= 0.998f || tc.y <= 0.002f || tc.y >= 0.998f || tc.z <= 0.0f || tc.z >= 1.0f)
        return;

    // 4-tap rotated hardware PCF filter to keep volumetric god-rays crisp without aliasing
    float texel = vSunCascadeUVClip.x * 1.5f;
    float zCmp = saturate(tc.z - vSunCascadeUVClip.y);

    float vis = 0.0f;
    vis += g_SunSmap.SampleCmpLevelZero(g_SmpShadowCmp, tc.xy + float2(-texel, -texel * 0.5f), zCmp);
    vis += g_SunSmap.SampleCmpLevelZero(g_SmpShadowCmp, tc.xy + float2( texel,  texel * 0.5f), zCmp);
    vis += g_SunSmap.SampleCmpLevelZero(g_SmpShadowCmp, tc.xy + float2( texel * 0.5f, -texel), zCmp);
    vis += g_SunSmap.SampleCmpLevelZero(g_SmpShadowCmp, tc.xy + float2(-texel * 0.5f,  texel), zCmp);
    vis *= 0.25f;

    g_SunVisOut[id] = vis;
}
