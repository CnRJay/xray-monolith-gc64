#ifndef FROXEL_COMMON_HLSLI
#define FROXEL_COMMON_HLSLI

cbuffer FroxelFrameCB : register(b0)
{
    row_major float4x4 mInvViewProj;
    row_major float4x4 mViewProj;
    row_major float4x4 mPrevViewProj;
    row_major float4x4 mSunCascadeXform;

    float4 vCameraPos;        // xyz = world pos, w = z_near
    float4 vCameraDir;        // xyz = world dir, w = z_far (max froxel range)
    float4 vGridParams;       // x = gridW, y = gridH, z = gridD, w = depthPower
    float4 vInvGridParams;    // x = 1/W, y = 1/H, z = 1/D, w = temporalBlend

    float4 vSunDir;           // xyz = light travel dir (from sun toward scene), w = sunEnabled (0/1)
    float4 vSunColor;         // rgb = sun color, w = anisotropy g
    float4 vFogColor;         // rgb = fog color, w = globalDensityScale
    float4 vFogParams;        // x = envFogDensity, y = fogNear, z = fogFar, w = rainDensity

    float4 vAmbientColor;     // rgb = ambient, w = timeGlobal
    float4 vHemiColor;        // rgb = hemi sky color, w = windVelocity
    float4 vWindAndJitter;    // x = windDirX, y = windDirZ, z = jitterSliceOffset, w = frameIndexMod64
    float4 vCascadeAndLights; // x = cascadeZMin, y = cascadeZMax, z = numDynamicLights, w = numShadowSpots
    float4 vScreenParams;     // x = screenW, y = screenH, z = 1/screenW, w = 1/screenH
    float4 vSunCascadeUVClip; // x = 1/smapSize, y = depthBias, z = cascadeIndex, w = skyFactor
};

struct SFroxelLight
{
    float4 posRange;      // xyz = world pos, w = invRangeSq (1 / (R*R))
    float4 colorVol;      // rgb = color * intensity, w = range
    float4 dirCone;       // xyz = spot dir (world), w = outerCos (<= -2.0 for point light)
    float4 spotParams;    // x = invConeDelta (1 / max(innerCos - outerCos, 1e-4)), y = volumetricScale, z = isSpot (0/1), w = pad
};

struct SFroxelShadowSpot
{
    row_major float4x4 mWorldToShadow;
    float4 posRange;      // xyz = world pos, w = invRangeSq
    float4 colorVol;      // rgb = color * volIntensity, w = range
    float4 dirCone;       // xyz = spot dir, w = outerCos
    float4 atlasBounds;   // x = minU, y = minV, z = maxU, w = maxV
};

static const float FROXEL_PI = 3.14159265359f;

float SliceToLinearDepth(float sliceNorm)
{
    float zNear = vCameraPos.w;
    float zFar  = vCameraDir.w;
    float p     = vGridParams.w;
    return lerp(zNear, zFar, pow(saturate(sliceNorm), p));
}

float LinearDepthToSlice(float depth)
{
    float zNear = vCameraPos.w;
    float zFar  = vCameraDir.w;
    float invP  = 1.0f / vGridParams.w;
    float t = saturate((depth - zNear) / max(zFar - zNear, 1e-3f));
    return pow(t, invP);
}

float3 FroxelToWorldPos(uint3 id, float sliceJitter)
{
    float2 uv = (float2(id.xy) + 0.5f) * vInvGridParams.xy;
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 farClip = mul(float4(ndc, 1.0f, 1.0f), mInvViewProj);
    float3 farWorld = farClip.xyz / max(abs(farClip.w), 1e-6f);
    float3 rayDir = farWorld - vCameraPos.xyz;
    float rayLen = length(rayDir);
    float3 rayNorm = rayDir / max(rayLen, 1e-6f);

    // Project view-space linear depth onto this pixel ray
    float cosTheta = max(dot(rayNorm, vCameraDir.xyz), 0.1f);
    float sliceNorm = (float(id.z) + sliceJitter) * vInvGridParams.z;
    float linearZ = SliceToLinearDepth(sliceNorm);
    return vCameraPos.xyz + rayNorm * (linearZ / cosTheta);
}

// Interleaved gradient noise (Jimenez 2014) for alias-free per-froxel slice jitter
float FroxelIGN(uint3 id, float frameIdx)
{
    float2 p = float2(id.xy) + float2(id.z * 5.588238f, frameIdx * 17.31f);
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

// Henyey-Greenstein phase function
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0f + g2 - 2.0f * g * cosTheta, 1e-3f);
    return (1.0f - g2) / (4.0f * FROXEL_PI * pow(denom, 1.5f));
}

// Dual-lobe HG: combines sharp forward beam with soft atmospheric wrap
float PhaseDualHG(float cosTheta, float g)
{
    return lerp(PhaseHG(cosTheta, g), PhaseHG(cosTheta, -0.25f), 0.18f);
}

// Fast 3D hash value noise for drifting mist/fog banks
float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float Noise3D(float3 x)
{
    float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0f - 2.0f * f);

    float n000 = Hash31(i + float3(0, 0, 0));
    float n100 = Hash31(i + float3(1, 0, 0));
    float n010 = Hash31(i + float3(0, 1, 0));
    float n110 = Hash31(i + float3(1, 1, 0));
    float n001 = Hash31(i + float3(0, 0, 1));
    float n101 = Hash31(i + float3(1, 0, 1));
    float n011 = Hash31(i + float3(0, 1, 1));
    float n111 = Hash31(i + float3(1, 1, 1));

    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    float nxy0 = lerp(nx00, nx10, f.y);
    float nxy1 = lerp(nx01, nx11, f.y);
    return lerp(nxy0, nxy1, f.z);
}

float Fbm3D(float3 p)
{
    float v = 0.0f;
    v += 0.62f * Noise3D(p);
    v += 0.38f * Noise3D(p * 2.07f + float3(13.7f, 7.1f, 19.3f));
    return v;
}

#endif
