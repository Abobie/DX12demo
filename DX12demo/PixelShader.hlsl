Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

cbuffer MVP : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;

    float3 lightDir;
    float pad1;

    float3 lightColor;
    float pad2;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET
{
    // Normalize
    float3 N = normalize(input.normal);
    float3 L = normalize(-lightDir);

    // Lambert lighting
    float diffuse = max(dot(N, L), 0.0f);

    float3 texColor = tex0.Sample(samp0, input.uv).rgb;
    float3 finalColor = texColor * lightColor * diffuse;

    return float4(finalColor, 1.0f);
}
