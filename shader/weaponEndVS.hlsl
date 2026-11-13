#include "common.hlsl"

cbuffer WeaponEnd : register(b7)
{
    float4 EndColor;
    float4 EndParams;
    float4 BladeAxis;
};

struct WeaponPixel
{
    float4 Position : SV_POSITION;
    float3 Local : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float4 Color : COLOR0;
};

WeaponPixel main(VS_IN vertex)
{
    WeaponPixel pixel;
    float4 viewPosition = mul(mul(vertex.Position, World), View);
    if (EndParams.z > 0.5)
        viewPosition.xy += vertex.TexCoord * vertex.Normal.x;
    pixel.Position = mul(viewPosition, Projection);
    pixel.Local = vertex.Position.xyz;
    pixel.UV = vertex.TexCoord;
    pixel.Color = vertex.Diffuse;
    return pixel;
}
