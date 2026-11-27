struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}
