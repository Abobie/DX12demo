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
    float3 color : COLOR;
};

float4 main(PSInput input) : SV_TARGET
{
    // Normalize
    float3 N = normalize(input.normal);
    float3 L = normalize(-lightDir);

    // Lambert lighting
    float diffuse = max(dot(N, L), 0.0f);

    float3 finalColor =
        input.color * lightColor * diffuse;

    return float4(finalColor, 1.0f);
}
