
#include "common.hlsl"
#include "planetariumLight.hlsli"


Texture2D		g_Texture : register(t0);
Texture2D SoilTexture : register(t1);
Texture2D GrassTexture : register(t2);
Texture2D<float2> TerrainPaint : register(t3);
SamplerState	g_SamplerState : register(s0);
SamplerState PaintSampler : register(s1);

float terrainVariation(float2 p)
{
    // Interpolated lattice noise for color changes that do not repeat with the detail textures.
    float2 cell = floor(p);
    float2 w = frac(p);
    w = w * w * (3.0 - 2.0 * w);
    float4 n = frac((cell.x + float4(0.0, 1.0, 0.0, 1.0)) * 0.1031
                  + (cell.y + float4(0.0, 0.0, 1.0, 1.0)) * 0.11369);
    n = frac(n * (n + 19.19) * (n + 43.17));
    return lerp(lerp(n.x, n.y, w.x), lerp(n.z, n.w, w.x), w.y);
}

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
            float2 ground = In.WorldPosition.xz;
            float broad = terrainVariation(ground * 0.095);
            float patch = terrainVariation(ground * 0.43);
            float variation = broad * 0.64 + patch * 0.36;
            float3 earthTint = lerp(float3(0.72, 0.71, 0.68), float3(1.19, 1.13, 1.04), variation);
            float3 grassTint = lerp(float3(0.69, 0.78, 0.74), float3(1.17, 1.12, 0.94), variation);

            // Break up the transition while preserving the coverage and fully painted areas.
            float grassShare = weights.y / max(total, 0.0001);
            grassShare += (variation - 0.5) * 1.2 * grassShare * (1.0 - grassShare);
            float3 surface = lerp(soil * earthTint, grass * grassTint, grassShare);
            outDiffuse.rgb = lerp(outDiffuse.rgb, surface, total);
        }
		outDiffuse *= In.Diffuse;
	}
	else
	{
		outDiffuse = In.Diffuse;
	}

    const float night = planetariumLightWeight(In.WorldPosition.xyz);
    const float3 daylight = Light.Ambient.rgb + In.Normal.w * Light.Diffuse.rgb;
    const float3 lighting = lerp(daylight, planetariumIrradiance(normalize(In.Normal.xyz)), night);
    outDiffuse.rgb *= lighting;
	
}
