#include "common.hlsl"

PS_IN main(VS_IN input, float4 row0 : INSTANCEWORLD0, float4 row1 : INSTANCEWORLD1,
    float4 row2 : INSTANCEWORLD2, float4 row3 : INSTANCEWORLD3)
{
    PS_IN output = (PS_IN)0;
    float4x4 model = float4x4(row0, row1, row2, row3);
    float4 position = mul(input.Position, model);
    output.Position = mul(mul(position, View), Projection);
    output.TexCoord = input.TexCoord;
    output.Diffuse = input.Diffuse * Material.Diffuse;

    float3 normal = normalize(mul(float4(input.Normal.xyz, 0.0f), model).xyz);
    float light = saturate(-dot(normal, Light.Direction.xyz));
    output.Diffuse.rgb *= light * Light.Diffuse.rgb + Light.Ambient.rgb;
    return output;
}
