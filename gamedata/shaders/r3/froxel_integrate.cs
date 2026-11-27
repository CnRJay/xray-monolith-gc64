#include "froxel_common.hlsli"

Texture3D<float4> g_LightVolume : register(t0);
RWTexture3D<float4> g_IntegratedOut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint3 dims = uint3(vGridParams.xyz);
    if (id.x >= dims.x || id.y >= dims.y)
        return;

    float3 accumScatter = float3(0.0f, 0.0f, 0.0f);
    float accumTrans = 1.0f;

    float prevDepth = vCameraPos.w;
    uint depthSlices = dims.z;

    [loop]
    for (uint z = 0; z < depthSlices; ++z)
    {
        float nextDepth = SliceToLinearDepth(float(z + 1) * vInvGridParams.z);
        float sliceThickness = max(nextDepth - prevDepth, 1e-4f);
        prevDepth = nextDepth;

        float4 s = g_LightVolume[uint3(id.xy, z)];
        if (any(isnan(s)) || any(isinf(s)))
            s = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float3 inScatter = clamp(s.rgb, 0.0f, 64.0f);
        float extinction = clamp(s.a, 0.0f, 8.0f);

        // Energy-conserving analytical Beer-Lambert integration (Hillaire, Frostbite 2015)
        float sliceTrans = exp(-extinction * sliceThickness);
        float3 sliceInt = (inScatter - inScatter * sliceTrans) / max(extinction, 1e-5f);

        accumScatter = clamp(accumScatter + accumTrans * sliceInt, 0.0f, 64.0f);
        accumTrans = saturate(accumTrans * sliceTrans);

        g_IntegratedOut[uint3(id.xy, z)] = float4(accumScatter, accumTrans);
    }
}
