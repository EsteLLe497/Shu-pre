#ifndef SKY_FRAME_HLSLI
#define SKY_FRAME_HLSLI

cbuffer SkyFrame : register(b13)
{
    float4 SkySun;    // xyz: toward the sun, w: direct sunlight strength
    float4 SkyMoon;   // xyz: toward the moon, w: visible moonlight
    float4 SkyCycle;  // daylight, twilight, cloud travel time, active
    float4 SkyCloudWind; // inverse horizontal wind rotation, cosine and sine
};

float3 skySceneTint()
{
    float3 tint = lerp(float3(0.34, 0.41, 0.56), 1.0, SkyCycle.x);
    tint += SkyCycle.y * float3(0.18, 0.085, 0.015);
    return lerp(1.0, tint, SkyCycle.w);
}

#endif
