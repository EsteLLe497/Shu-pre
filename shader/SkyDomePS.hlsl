#include "SkyColor.hlsli"

float starField(float3 ray)
{
    // A different-sized star is placed in each occupied angular cell.
    float2 angular = float2(atan2(ray.z, ray.x) * 0.15915494 + 0.5,
        asin(clamp(ray.y, -1.0, 1.0)) * 0.31830989 + 0.5) * float2(720.0, 360.0);
    float2 cell = floor(angular);
    float seed = skyHash(cell);
    float2 offset = float2(skyHash(cell + 37.1), skyHash(cell - 19.3));
    float radius = lerp(0.059, 0.18, seed * seed);
    float pixel = clamp(length(fwidth(angular)) * 0.41, 0.025, 0.24);
    float spark = 1.0 - smoothstep(radius - pixel, radius + pixel, length(frac(angular) - lerp(0.23, 0.77, offset)));
    return spark * (seed > 0.988 ? lerp(0.28, 0.89, offset.x) : 0.0)
        * smoothstep(0.03, 0.21, ray.y);
}

float4 main(float4 position : SV_POSITION, float3 viewRay : TEXCOORD0) : SV_Target
{
    float3 ray = normalize(viewRay);
    float aa = max(length(fwidth(ray)), 0.0005);
    float3 color = clearSky(ray);

    float sunAngle = length(ray - SkySun.xyz);
    float disc = 1.0 - smoothstep(0.010 - aa, 0.010 + aa, sunAngle);
    float horizon = smoothstep(-0.012, 0.012, ray.y);
    float3 sun = lerp(float3(1.10, 0.50, 0.24), float3(1.16, 1.07, 0.83), smoothstep(0.0, 0.32, SkySun.y));
    color += sun * disc * horizon * smoothstep(-0.050, -0.003, SkySun.y);
    color += sun * exp2(-sunAngle * 35.0) * 0.28 * smoothstep(-0.09, 0.09, SkySun.y);

    float moonAngle = length(ray - SkyMoon.xyz);
    float moonDisc = 1.0 - smoothstep(0.012 - aa, 0.012 + aa, moonAngle);
    float3 moonSide = normalize(float3(SkyMoon.z, 0.0, -SkyMoon.x));
    float phase = smoothstep(-aa, aa, dot(ray, moonSide) + 0.007);
    color += float3(0.66, 0.74, 0.89) * moonDisc * phase * horizon * SkyMoon.w;
    [branch]
    if (SkyCycle.x < 0.25 && ray.y > 0.015)
        color += starField(ray) * (1.0 - smoothstep(0.0, 0.25, SkyCycle.x));

    const float4 cloud = summerClouds(ray, false);
    color = color * (1.0 - cloud.a) + cloud.rgb;
    return float4(saturate(color), 1.0);
}
