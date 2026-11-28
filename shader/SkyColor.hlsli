#ifndef SKY_COLOR_HLSLI
#define SKY_COLOR_HLSLI

#include "SkyFrame.hlsli"

float skyHash(float2 coordinate)
{
    float3 p = frac(coordinate.xyx * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float3 clearSky(float3 direction)
{
    float height = max(0.0, direction.y);
    float horizon = exp2(-height * 7.2);
    float3 day = lerp(float3(0.045, 0.28, 0.66), float3(0.63, 0.81, 0.91), horizon);
    float3 night = lerp(float3(0.012, 0.024, 0.065), float3(0.053, 0.081, 0.14), horizon);
    float sunFacing = saturate(0.48 + 0.52 * dot(direction, SkySun.xyz));
    float glow = pow(sunFacing, 4.0);
    float3 color = lerp(night, day, SkyCycle.x);
    float3 dusk = horizon * lerp(float3(0.26, 0.21, 0.29), float3(0.99, 0.42, 0.17), glow);
    color = lerp(color, dusk + float3(0.055, 0.07, 0.095), SkyCycle.y * horizon * 0.82);
    color += float3(0.30, 0.23, 0.14) * pow(sunFacing, 11.0) * SkySun.w;
    float moonHalo = pow(saturate(dot(direction, SkyMoon.xyz)), 48.0);
    color += float3(0.105, 0.138, 0.21) * moonHalo * SkyMoon.w;
    float below = smoothstep(-0.48, 0.0, direction.y);
    return lerp(lerp(float3(0.016, 0.024, 0.033), float3(0.31, 0.40, 0.45), SkyCycle.x), color, below);
}

#include "SummerClouds.hlsli"

float3 reflectedSkyColor(float3 direction)
{
    float3 color = lerp(float3(0.41, 0.48, 0.51), float3(0.10, 0.26, 0.49), saturate(direction.y));
    [branch]
    if (SkyCycle.w > 0.0)
    {
        const float4 cloud = summerClouds(direction, true);
        color = clearSky(direction) * (1.0 - cloud.a) + cloud.rgb;
    }
    return color;
}

#endif
