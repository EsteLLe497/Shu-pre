
#include "common.hlsl"
#include "planetariumLight.hlsli"


Texture2D		g_Texture : register(t0);
SamplerState	g_SamplerState : register(s0);


void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{

	if (Material.TextureEnable)
	{
		outDiffuse = g_Texture.Sample(g_SamplerState, In.TexCoord);
		outDiffuse *= In.Diffuse;
	}
	else
	{
		outDiffuse = In.Diffuse;
	}

    const float night = planetariumLightWeight(In.WorldPosition.xyz);
    const float3 lighting = lerp(In.Normal.www, planetariumIrradiance(normalize(In.Normal.xyz)), night);
    outDiffuse.rgb *= lighting;
	
}
