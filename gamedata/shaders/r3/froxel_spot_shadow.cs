#include "froxel_common.hlsli"

Texture2D<float> g_SpotAtlas : register(t0);
StructuredBuffer<SFroxelShadowSpot> g_ShadowSpots : register(t1);
SamplerComparisonState g_SmpShadowCmp : register(s0);
RWTexture3D<float4> g_SpotAccum : register(u0);

[numthreads(8, 8, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint3 dims = uint3(vGridParams.xyz);
    if (any(id >= dims))
        return;

    uint spotCount = uint(vCascadeAndLights.w);
    if (spotCount == 0)
        return;

    float jitter = FroxelIGN(id, vWindAndJitter.w);
    float3 worldPos = FroxelToWorldPos(id, jitter);
    float3 viewDir = normalize(worldPos - vCameraPos.xyz);
    float g = vSunColor.w;

    float3 accum = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for (uint i = 0; i < spotCount; ++i)
    {
        SFroxelShadowSpot L = g_ShadowSpots[i];
        float3 toFroxel = worldPos - L.posRange.xyz;
        float distSq = dot(toFroxel, toFroxel);
        float normDistSq = distSq * L.posRange.w;
        if (normDistSq >= 1.0f)
            continue;

        float dist = sqrt(max(distSq, 1e-4f));
        float3 lightToFroxel = toFroxel / dist;

        float spotCos = dot(lightToFroxel, L.dirCone.xyz);
        if (spotCos <= L.dirCone.w)
            continue;

        float4 shadowCoord = mul(float4(worldPos, 1.0f), L.mWorldToShadow);
        if (shadowCoord.w <= 1e-4f)
            continue;

        float3 tc = shadowCoord.xyz / shadowCoord.w;
        if (tc.x < L.atlasBounds.x || tc.x > L.atlasBounds.z || tc.y < L.atlasBounds.y || tc.y > L.atlasBounds.w || tc.z <= 0.0f || tc.z >= 1.0f)
            continue;

        float texel = vSunCascadeUVClip.x;
        float zCmp = saturate(tc.z - 0.0005f);
        float vis = 0.25f * (
            g_SpotAtlas.SampleCmpLevelZero(g_SmpShadowCmp, clamp(tc.xy + float2(-texel, -texel), L.atlasBounds.xy, L.atlasBounds.zw), zCmp) +
            g_SpotAtlas.SampleCmpLevelZero(g_SmpShadowCmp, clamp(tc.xy + float2( texel, -texel), L.atlasBounds.xy, L.atlasBounds.zw), zCmp) +
            g_SpotAtlas.SampleCmpLevelZero(g_SmpShadowCmp, clamp(tc.xy + float2(-texel,  texel), L.atlasBounds.xy, L.atlasBounds.zw), zCmp) +
            g_SpotAtlas.SampleCmpLevelZero(g_SmpShadowCmp, clamp(tc.xy + float2( texel,  texel), L.atlasBounds.xy, L.atlasBounds.zw), zCmp)
        );
        if (vis <= 0.001f)
            continue;

        float coneAtten = saturate((spotCos - L.dirCone.w) / max(1.0f - L.dirCone.w, 1e-3f));
        coneAtten = smoothstep(0.0f, 1.0f, coneAtten);

        // Smooth physical inverse-square falloff with range windowing
        float window = saturate(1.0f - normDistSq * normDistSq);
        float distAtten = (window * window) / (1.0f + distSq * 0.35f);

        // Forward scattering along beam direction
        float phase = PhaseDualHG(dot(viewDir, lightToFroxel), g);
        accum += L.colorVol.rgb * (vis * coneAtten * distAtten * phase * 2.2f);
    }

    if (any(accum > 0.0f))
    {
        float4 prev = g_SpotAccum[id];
        g_SpotAccum[id] = float4(prev.rgb + accum, 0.0f);
    }
}
