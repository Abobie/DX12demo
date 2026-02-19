Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

cbuffer MVP : register(b0)
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

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 worldPos : WORLDPOS;
};

float4 main(PSInput input) : SV_TARGET
{
    // Normalize
    float3 N = normalize(input.normal);

    // Ambient
    float3 ambient = ambientColor;
    
    // Directional light
    float3 Ld = normalize(-directionalLightDir);
    float directionalDiffuse = max(dot(N, Ld), 0.0f);
    float3 directional = directionalLightColor * directionalDiffuse;
    
    // Point light
    float3 toLight = pointLightPosition - input.worldPos.xyz;
    float dist = length(toLight);
    float3 Lp = normalize(toLight);
    float diffPoint = max(dot(N, Lp), 0.0f);
    
    // Smooth attenuation
    float attenuation = saturate(1.0f - dist / pointLightRange);
    attenuation *= attenuation; // Quadratic falloff
    
    float3 pointLight = pointLightColor * diffPoint * attenuation;
    
    // Final light color
    float3 lightColor = ambient + directional + pointLight;

    float3 texColor = tex0.Sample(samp0, input.uv).rgb;
    float3 finalColor = texColor * lightColor;

    return float4(finalColor, 1.0f);
}
