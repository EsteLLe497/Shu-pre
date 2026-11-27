
#include "common.hlsl"
#include "planetariumLight.hlsli"


Texture2D		g_Texture : register(t0);
Texture2D SoilTexture : register(t1);
Texture2D GrassTexture : register(t2);
Texture2D<float2> TerrainPaint : register(t3);
SamplerState	g_SamplerState : register(s0);
SamplerState PaintSampler : register(s1);


void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{

	if (Material.TextureEnable)
	{
		outDiffuse = g_Texture.Sample(g_SamplerState, In.TexCoord * 20.0);
        float2 weights = TerrainPaint.SampleLevel(PaintSampler, In.TexCoord, 0);
        float total = saturate(weights.x + weights.y);
        float2 detailUV = In.TexCoord * 74.0;
        float2 gradX = ddx(detailUV), gradY = ddy(detailUV);
        [branch]
        if (total > 0.0)
        {
            float3 soil = SoilTexture.SampleGrad(g_SamplerState, detailUV, gradX, gradY).rgb;
            float3 grass = GrassTexture.SampleGrad(g_SamplerState, detailUV, gradX, gradY).rgb;
            outDiffuse.rgb = outDiffuse.rgb * (1.0 - total) + soil * weights.x + grass * weights.y;
        }
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
