
#include "common.hlsl"


void main(in VS_IN In, out PS_IN Out)
{
    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);

    Out.Position = mul(In.Position, wvp);
    Out.WorldPosition = mul(In.Position, World);
    Out.TexCoord = In.TexCoord;
    Out.Diffuse = In.Diffuse * Material.Diffuse;
	
    float4 normal = In.Normal;
    normal.w = 0.0f;
    normal = mul(normal, World);
    normal = normalize(normal);
	
    float3 lightDir = Light.Direction.xyz;
    lightDir = normalize(lightDir);
	
    float light = -dot(normal.xyz, lightDir);
    light = saturate(light);
	
    Out.Normal = float4(normal.xyz, light); // w ÇÕí∏ì_Ç≈åvéZÇµÇΩíãÇÃñæÇÈÇ≥ÅB
}

