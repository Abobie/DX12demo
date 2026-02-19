cbuffer MVPBuffer : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;

    float3 ambientColor;
    float pad1;

    float3 directionalLightDir;
    float pad2;

    float3 directionalLightColor;
    float pad3;

    float3 pointLightPosition;
    float pointLightRange;

    float3 pointLightColor;
    float pad4;
};

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 worldPos : WORLDPOS;
};

PSInput main(VSInput input)
{
    PSInput output;

    float4 pos = float4(input.pos, 1.0f);

    pos = mul(pos, world);
    pos = mul(pos, view);
    pos = mul(pos, projection);

    output.pos = pos;

    output.normal = mul(input.normal, (float3x3) world);
    output.uv = input.uv;
    output.worldPos = mul(float4(input.pos, 1.0f), world);

    return output;
}