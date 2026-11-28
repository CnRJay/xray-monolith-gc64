#include "froxel_common.hlsli"

RWTexture3D<float4> g_MediaOut : register(u0);

[numthreads(8, 8, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint3 dims = uint3(vGridParams.xyz);
    if (any(id >= dims))
        return;

    float jitter = FroxelIGN(id, vWindAndJitter.w);
    float3 worldPos = FroxelToWorldPos(id, jitter);

    // Base weather fog density scaled into physically balanced extinction units (1/m)
    float envFog = clamp(vFogParams.x, 0.0f, 2.0f);
    float rainBoost = saturate(vFogParams.w) * 0.55f;
    float baseExtinction = (0.0022f + envFog * 0.0065f + rainBoost * 0.0045f) * vFogColor.w;

    // Gentle height-fog stratification relative to camera ground elevation
    float relHeight = worldPos.y - (vCameraPos.y - 1.6f);
    float heightFalloff = exp(-max(relHeight, -6.0f) * 0.035f);
    heightFalloff = clamp(heightFalloff, 0.18f, 1.85f);

    // Animated 3D wind-driven atmospheric mist pockets
    float windSpeed = clamp(vHemiColor.w * 0.08f, 0.15f, 1.5f);
    float3 windDrift = float3(vWindAndJitter.x, 0.08f, vWindAndJitter.y) * (vAmbientColor.w * windSpeed);
    float n = Fbm3D(worldPos * 0.045f + windDrift);
    float noiseMod = lerp(0.68f, 1.42f, n);

    // Soft near-camera fade so fog never muddies weapon HUD / immediate foreground
    float camDist = length(worldPos - vCameraPos.xyz);
    float nearFade = smoothstep(0.6f, 4.5f, camDist);

    float extinction = baseExtinction * heightFalloff * noiseMod * nearFade;

    // Scattering albedo tinted subtly toward weather fog color
    float3 albedo = lerp(float3(0.88f, 0.90f, 0.92f), saturate(vFogColor.rgb * 1.15f + 0.15f), 0.45f);

    g_MediaOut[id] = float4(albedo, extinction);
}
