
#include "common.hlsl"
#include "foliageWind.hlsli"


void main(in VS_IN In, out PS_IN Out)
{

    float4 position = mul(In.Position, World);
    applyFoliageWind(In.Position.xyz, World, position.xyz);
    Out.Position = mul(mul(position, View), Projection);
    Out.WorldPosition = position;
	Out.TexCoord = In.TexCoord;
	Out.Diffuse = In.Diffuse * Material.Diffuse;
	
    float4 normal = In.Normal;
    normal.w = 0.0f;
    normal = mul(normal, World);
    normal = normalize(normal);
    Out.Normal = normal;
	
    float3 lightDir = Light.Direction.xyz;
    lightDir = normalize(lightDir);
	
    float light = -dot(normal.xyz, Light.Direction.xyz);
    light = saturate(light);
	
    Out.Diffuse.rgb *= light * Light.Diffuse.rgb + Light.Ambient.rgb;
	
}

