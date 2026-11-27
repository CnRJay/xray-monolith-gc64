#include "froxel_common.hlsli"

Texture3D<float4> g_Media : register(t0);
Texture3D<float>  g_SunVis : register(t1);
Texture3D<float4> g_SpotAccum : register(t2);
Texture3D<float4> g_History : register(t3);
StructuredBuffer<SFroxelLight> g_Lights : register(t4);

SamplerState g_SmpLinear : register(s0);
RWTexture3D<float4> g_LightOut : register(u0);

[numthreads(8, 8, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint3 dims = uint3(vGridParams.xyz);
    if (any(id >= dims))
        return;

    float4 media = g_Media[id];
    float3 albedo = media.rgb;
    float extinction = media.a;

    float jitter = FroxelIGN(id, vWindAndJitter.w);
    float3 worldPos = FroxelToWorldPos(id, jitter);
    float3 viewDir = normalize(worldPos - vCameraPos.xyz);
    float g = vSunColor.w;

    float3 totalLight = float3(0.0f, 0.0f, 0.0f);

    // 1. Multi-cascade shadowed Sun scattering (god-rays)
    if (vSunDir.w > 0.5f)
    {
        float sunVis = g_SunVis[id];
        // vSunDir.xyz points from sun toward scene (same direction as photon travel),
        // so forward scatter peaks when viewDir aligns with -vSunDir (looking toward sun)
        float cosTheta = dot(viewDir, -vSunDir.xyz);
        float phase = PhaseDualHG(cosTheta, g);
        totalLight += vSunColor.rgb * (sunVis * phase * 2.8f);
    }

    // 2. Shadowed spotlights accumulated from the live DX11 shadow atlas
    totalLight += g_SpotAccum[id].rgb;

    // 3. Dynamic point lights and unshadowed spotlights (campfires, anomalies, lamps, flashes)
    uint numLights = uint(vCascadeAndLights.z);
    [loop]
    for (uint i = 0; i < numLights; ++i)
    {
        SFroxelLight L = g_Lights[i];
        float3 toFroxel = worldPos - L.posRange.xyz;
        float distSq = dot(toFroxel, toFroxel);
        float normDistSq = distSq * L.posRange.w;
        if (normDistSq >= 1.0f)
            continue;

        float dist = sqrt(max(distSq, 1e-4f));
        float3 lightToFroxel = toFroxel / dist;

        float spotAtten = 1.0f;
        if (L.spotParams.z > 0.5f)
        {
            float c = dot(lightToFroxel, L.dirCone.xyz);
            if (c <= L.dirCone.w)
                continue;
            spotAtten = saturate((c - L.dirCone.w) * L.spotParams.x);
            spotAtten *= spotAtten;
        }

        float window = saturate(1.0f - normDistSq * normDistSq);
        float distAtten = (window * window) / (1.0f + distSq * 0.45f);
        float phase = (L.spotParams.z > 0.5f)
            ? PhaseDualHG(dot(viewDir, lightToFroxel), g * 0.8f)
            : (1.0f / (4.0f * FROXEL_PI));

        totalLight += L.colorVol.rgb * (spotAtten * distAtten * phase * L.spotParams.y);
    }

    // 4. Hemisphere / sky ambient scattering (keeps shadowed mist atmospheric without crushing blacks)
    float3 ambientScatter = (vHemiColor.rgb * 0.14f + vAmbientColor.rgb * 0.10f + vFogColor.rgb * 0.08f) * (1.0f / (4.0f * FROXEL_PI));
    totalLight += ambientScatter;

    float3 inScatter = totalLight * albedo * extinction;
    float4 currentVal = float4(inScatter, extinction);

    // 5. Temporal reprojection using unjittered froxel center to stabilize jittered shadow & noise samples
    float temporalBlend = vInvGridParams.w;
    if (temporalBlend > 0.001f)
    {
        float3 unjitteredWorld = FroxelToWorldPos(id, 0.5f);
        float4 prevClip = mul(float4(unjitteredWorld, 1.0f), mPrevViewProj);
        if (prevClip.w > 0.05f)
        {
            float2 prevNdc = prevClip.xy / prevClip.w;
            float2 prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
            float prevSlice = LinearDepthToSlice(prevClip.w);

            if (all(prevUv > 0.0f) && all(prevUv < 1.0f) && prevSlice > 0.0f && prevSlice < 1.0f)
            {
                float4 history = g_History.SampleLevel(g_SmpLinear, float3(prevUv, prevSlice), 0.0f);
                // Reduce history weight slightly near edges and on sharp brightness changes to avoid ghosting
                float2 edgeDist = min(prevUv, 1.0f - prevUv);
                float edgeWeight = saturate(min(edgeDist.x, edgeDist.y) * 16.0f);
                float blend = temporalBlend * edgeWeight;
                currentVal = lerp(currentVal, history, blend);
            }
        }
    }

    g_LightOut[id] = max(currentVal, 0.0f);
}
