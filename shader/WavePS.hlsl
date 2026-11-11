
#include "common.hlsl"


Texture2D		g_Texture : register(t0);
SamplerState	g_SamplerState : register(s0);

static float PI = 3.141592653589;

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    float3 eyev = In.WorldPosition.xyz - CameraPosition.xyz;
    eyev = normalize(eyev);
	
    float3 normal = normalize(In.Normal.xyz);
    float3 refv = reflect(eyev, normal);
	
	//環境マッピングテクスチャ座標（パノラマ）
    float2 envTexCoord;
    envTexCoord.x = atan2(refv.x, refv.z) / (PI * 2) + 0.5f;
    envTexCoord.y = acos(refv.y) / PI;
	
	if (Material.TextureEnable)
	{
        outDiffuse = g_Texture.Sample(g_SamplerState, envTexCoord);
		//outDiffuse *= In.Diffuse;
	}
	else
	{
		outDiffuse = In.Diffuse;
	}
	
}
