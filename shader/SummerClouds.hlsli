#ifndef SUMMER_CLOUDS_HLSLI
#define SUMMER_CLOUDS_HLSLI

Texture3D<float4> SummerCloudDensity : register(t8);
SamplerState SummerCloudSampler : register(s4);

struct CloudVolume
{
    float3 center;
    float3 radius;
    int shape;
};

// These distant clouds share one wind direction. The red density channel
// contains the tall anvil cloud, green the rising towers, and blue smaller puffs.
static const CloudVolume cloudVolumes[] =
{
    { float3(-2.6, 4.4, -3.5), float3(1.52, 0.79, 1.39), 2 },
    { float3( 3.6, 4.5,  2.4), float3(1.76, 0.95, 1.44), 2 },
    { float3(-3.1, 2.9, -6.0), float3(1.30, 0.67, 1.14), 2 },
    { float3( 0.7, 3.4, -8.1), float3(3.35, 3.13, 2.12), 0 },
    { float3(-8.2, 3.2,  1.6), float3(2.30, 2.58, 3.17), 1 },
    { float3(-8.8, 2.4, -3.6), float3(0.96, 0.68, 1.48), 2 },
    { float3( 8.5, 2.8, -1.1), float3(2.06, 2.10, 2.78), 1 },
    { float3(-3.5, 2.5,  9.1), float3(3.12, 1.63, 1.91), 2 },
    { float3( 3.6, 2.7, 10.4), float3(3.03, 1.75, 2.13), 1 }
};

float3 inCloudSpace(float3 direction)
{
    return float3(
        direction.x * SkyCloudWind.x - direction.z * SkyCloudWind.y,
        direction.y,
        direction.x * SkyCloudWind.y + direction.z * SkyCloudWind.x);
}

float cloudDensity(float3 uv, int shape)
{
    return SummerCloudDensity.SampleLevel(SummerCloudSampler, uv, 0)[shape];
}

float4 summerClouds(float3 direction, bool reflection)
{
    float4 cloud = 0.0;
    const int volumeCount = direction.y > 0.03 && SkyCloudWind.z > 0.0 ? 9 : 0;

    float3 ray = inCloudSpace(direction);
    float3 inverseRay = (step(0.0, ray) * 2.0 - 1.0) / max(0.0001, abs(ray));
    float3 light = inCloudSpace(SkySun.y >= -0.025 ? SkySun.xyz : SkyMoon.xyz);
    float silver = pow(saturate(dot(direction, SkySun.xyz)), 8.0) * SkySun.w;
    float3 shade = lerp(float3(0.030, 0.043, 0.075), float3(0.43, 0.52, 0.65), SkyCycle.x);
    float3 lit = lerp(float3(0.09, 0.12, 0.18), float3(1.02, 1.01, 0.98), SkyCycle.x);
    lit = lerp(lit, float3(1.00, 0.58, 0.34), SkyCycle.y * 0.72);
    const int steps = reflection ? 7 : 18;

    [loop]
    for (int i = 0; i < volumeCount; ++i)
    {
        CloudVolume volume = cloudVolumes[i];
        float3 a = (volume.center - volume.radius) * inverseRay;
        float3 b = (volume.center + volume.radius) * inverseRay;
        float3 near = min(a, b), far = max(a, b);
        float entry = max(0.0, max(near.x, max(near.y, near.z)));
        float exit = min(far.x, min(far.y, far.z));
        [branch]
        if (exit <= entry) continue;

        float stepLength = (exit - entry) / steps;
        float3 worldToVolume = 0.5 / volume.radius;
        float3 uv = (ray * (entry + 0.5 * stepLength) - volume.center) * worldToVolume + 0.5;
        float3 delta = ray * worldToVolume * stepLength;
        float3 lightStep = light * worldToVolume * 0.52;
        [loop]
        for (int j = 0; j < steps; ++j)
        {
            float density = cloudDensity(uv, volume.shape);
            [branch]
            if (density > 0.004)
            {
                // Nearby density along the light ray estimates self-shadowing inside the cloud.
                float towardLight = cloudDensity(uv + lightStep, volume.shape);
                if (!reflection)
                    towardLight += cloudDensity(uv + 2.7 * lightStep, volume.shape) * 0.60;
                float visibility = exp2(-towardLight * (reflection ? 2.5 : 2.0));
                float top = smoothstep(0.27, 0.87, uv.y);
                float brightness = saturate(visibility * 0.87 + top * 0.26 + silver * (0.25 - 0.18 * density));
                float3 color = lerp(shade, lit, brightness);
                float opacity = 1.0 - exp2(-2.85 * density * stepLength);
                float contribution = (1.0 - cloud.a) * opacity;
                cloud.rgb += color * contribution;
                cloud.a += contribution;
                if (cloud.a > 0.992) break;
            }
            uv += delta;
        }
        if (cloud.a > 0.992) break;
    }
    return cloud;
}

#endif
