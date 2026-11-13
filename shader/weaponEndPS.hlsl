#include "common.hlsl"

cbuffer WeaponEnd : register(b7)
{
    float4 EndColor;
    float4 EndParams;
    float4 BladeAxis;
};

Texture2D BaseTexture : register(t0);
SamplerState BaseSampler : register(s0);

float hashCell(float3 cell)
{
    cell = frac(cell * 0.3183099 + float3(0.13, 0.37, 0.59));
    cell *= 17.0;
    return frac(cell.x * cell.y * cell.z * (cell.x + cell.y + cell.z));
}

float noise(float3 samplePosition)
{
    float3 cell = floor(samplePosition);
    float3 f = frac(samplePosition);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(
        lerp(lerp(hashCell(cell), hashCell(cell + float3(1,0,0)), f.x),
             lerp(hashCell(cell + float3(0,1,0)), hashCell(cell + float3(1,1,0)), f.x), f.y),
        lerp(lerp(hashCell(cell + float3(0,0,1)), hashCell(cell + float3(1,0,1)), f.x),
             lerp(hashCell(cell + float3(0,1,1)), hashCell(cell + float3(1,1,1)), f.x), f.y), f.z);
}

float4 main(float4 position : SV_POSITION, float3 local : TEXCOORD0,
    float2 uv : TEXCOORD1, float4 vertexColor : COLOR0) : SV_TARGET
{
    if (EndParams.z > 0.5)
    {
        float2 p = uv * float2(1.0, 1.45);
        float radius = dot(p, p);
        float core = exp2(-radius * 10.0);
        float halo = exp2(-radius * 4.0) * saturate(1.0 - radius);
        return float4(lerp(EndColor.rgb, float3(1, 1, 1), core * 0.72), vertexColor.a * (core + halo * 0.4));
    }

    float grain = noise(local * 23.0);
    float broad = noise(local * 8.0 + 3.1);
    float distanceFromCenter = abs(dot(local, BladeAxis.xyz)) * 2.0;
    float field = distanceFromCenter * 0.74 + broad * 0.16 + grain * 0.10;
    float edge = field - lerp(-0.012, 1.025, EndParams.x);
    clip(edge);
    float4 base = Material.Diffuse;
    if (Material.TextureEnable)
        base *= BaseTexture.Sample(BaseSampler, uv);
    clip(base.a - Material.AlphaCutoff);
    float width = max(0.004, fwidth(edge));
    float brightLine = 1.0 - smoothstep(width, width + 0.012, edge);
    float glow = exp2(-edge * 29.0);
    float3 shaded = base.rgb * lerp(0.48, 0.82, grain) + EndColor.rgb * 0.06;
    float3 light = (EndColor.rgb * glow * 0.66
        + lerp(EndColor.rgb, float3(1, 1, 1), 0.72) * brightLine * 0.88) * EndParams.y;
    return float4(shaded + light, Material.AlphaBlend ? base.a : 1.0);
}
