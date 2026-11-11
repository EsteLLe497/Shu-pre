
#include "common.hlsl"


void main(in VS_IN In, out PS_IN Out)
{
    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);

    Out.Position = mul(In.Position, wvp);
    Out.TexCoord = In.TexCoord;
    Out.Diffuse = In.Diffuse * Material.Diffuse;
	
    float4 normal = In.Normal;
    normal.w = 0.0f;
    normal = mul(normal, World);
    normal = normalize(normal);
	
    float3 lightDir = Light.Direction.xyz;
    lightDir = normalize(lightDir);
	
    float light = -dot(normal.xyz, Light.Direction.xyz);
    light = saturate(light);
	
    Out.Diffuse.rgb *= light;
}

