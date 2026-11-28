#include "common.hlsl"
#include "WaterCommon.hlsli"
#include "LabLighting.hlsli"
#include "SkyColor.hlsli"

Texture2D<float> PondMask : register(t1);
Texture2D<float> SceneDepth : register(t2);
Texture2D<float4> SceneColor : register(t4);
Texture2D<float2> BasinMask : register(t6);
Texture2DArray<float4> WaterDetail : register(t7);
SamplerState PondSampler : register(s1);
SamplerState DetailSampler : register(s3);

// Graphics Lab の空・フォグを水面に反映 担当:ozawa souma        日付:2026/11/28
float3 waterSkyTint()
{
    return FogSettings.z > 0.5 ? 1.0.xxx : skySceneTint();
}

float3 waterSkyReflection(float3 direction)
{
    if (FogSettings.z > 0.5)
        return FogSettings.w > 0.5 ? labSky(direction) : float3(0.16, 0.33, 0.58);
    return reflectedSkyColor(direction);
}

float hash(float2 cell)
{
    return frac(sin(dot(cell, float2(127.1, 311.7))) * 43758.5453);
}

float noise(float2 position)
{
    float2 cell = floor(position);
    float2 f = frac(position);
    float2 blend = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash(cell), hash(cell + float2(1, 0)), blend.x),
        lerp(hash(cell + float2(0, 1)), hash(cell + 1), blend.x), blend.y);
}

void groundAt(float2 world, out float height, out float filled)
{
    int2 cell = 0;
    bool lower = false;
    height = terrainAt(world, cell, lower);
    float2 basin = BasinMask.Load(int3(cell, 0));
    filled = lower ? basin.x : basin.y;
}

float pondCoverage(float2 world, float filled)
{
    float2 uv = (world - WaterVolume.z) / WaterVolume.w;
    float mask = PondMask.SampleLevel(PondSampler, uv, 0);
    float painted = all(uv >= 0.0) && all(uv <= 1.0) ? smoothstep(0.32, 0.71, mask) : 0.0;
    return max(filled, painted);
}

float riverCoverage(float2 local, float width, float length)
{
    float fromBank = width * 0.5 - abs(local.x);
    float bankFade = min(width * 0.10, 0.12);
    float endFade = min(length * 0.2, 0.18);
    return smoothstep(0.0, bankFade, fromBank) * smoothstep(0.0, endFade, min(local.y, length - local.y));
}

float3 scenePosition(float2 pixel, float depth, float2 size)
{
    float2 ndc = pixel / size * float2(2, -2) + float2(-1, 1);
    float4 world = mul(float4(ndc, depth, 1), InverseViewProjection);
    return world.xyz / max(world.w, 0.000001);
}

float3 waterDetail(float2 position, float time, float2 dx, float2 dy)
{
    const float2x2 rotation = float2x2(0.62, 0.7846, -0.7846, 0.62);
    float2 uv = position * 0.33 + time * float2(0.049, -0.025);
    float3 a = WaterDetail.SampleGrad(DetailSampler, float3(uv, 0), dx * 0.33, dy * 0.33).rgb;
    uv = mul(rotation, position) * 0.57 + time * float2(-0.029, -0.063);
    float3 b = WaterDetail.SampleGrad(DetailSampler, float3(uv, 1), mul(rotation, dx) * 0.57,
        mul(rotation, dy) * 0.57).rgb;
    return float3((a.rg - 0.5) * 0.29 + mul(b.rg - 0.5, rotation) * 0.20,
        a.b * 0.65 + b.b * 0.35);
}

float causticLight(float2 world, float time, float depth)
{
    float2 projection = Light.Direction.xz * depth / max(0.25, -Light.Direction.y);
    float2 xy = (world - projection) * 3.75;
    float2 warp = float2(sin(xy.y * 1.13 + time * 0.94), sin(xy.x * 0.87 - time * 0.61));
    float a = sin(xy.x + warp.x * 1.48 - time * 0.51) + sin(xy.y + warp.y * 1.21);
    float b = sin(dot(xy, float2(0.63, 0.58)) + warp.y - time * 0.72);
    return pow(saturate(1.0 - abs(a + b * 0.62)), 4.0)
        * (0.32 + 0.68 * noise(world * 0.85 + time * 0.10));
}

float3 refractedBackground(PS_IN input, float2 offset, bool underwater, float3 originalWorld, out float3 background)
{
    uint width = 1, height = 1;
    SceneColor.GetDimensions(width, height);
    float2 bounds = float2(width, height);
    float2 shifted = clamp(input.Position.xy + offset, 0.5, bounds - 0.5);
    float3 original = SceneColor.Load(int3(int2(input.Position.xy), 0)).rgb;
    float3 shiftedColor = SceneColor.SampleLevel(PondSampler, shifted / bounds, 0).rgb;
    background = originalWorld;
    float confidence = 1.0;
    if (HasSceneDepth != 0.0)
    {
        float2 samplePixel = floor(shifted);
        float depth = SceneDepth.Load(int3(int2(samplePixel), 0));
        float3 candidate = scenePosition(samplePixel + 0.5, depth, bounds);
        // Judge the depth on the shifted ray, since SV_Position.z belongs to the original ray.
        float clearance = (candidate.y - Material.Specular.x) * (underwater ? 1.0 : -1.0);
        confidence = depth >= 1.0 ? float(underwater) : smoothstep(0.0, 0.10, clearance);
        confidence *= 1.0 - smoothstep(0.8, 3.0, abs(distance(candidate, CameraPosition.xyz)
            - distance(originalWorld, CameraPosition.xyz)));
        background = lerp(originalWorld, candidate, confidence);
    }
    return lerp(original, shiftedColor, confidence);
}

bool reflectionProbe(float3 world, float2 size, out float2 uv, out float projectedDepth, out float sampledDepth)
{
    float4 projected = mul(float4(world, 1), ViewProjection);
    uv = projected.xy / max(projected.w, 0.0001) * float2(0.5, -0.5) + 0.5;
    projectedDepth = projected.z / max(projected.w, 0.0001);
    float2 pixel = clamp(uv * size, 0.5, size - 0.5);
    sampledDepth = SceneDepth.Load(int3(int2(pixel), 0));
    return projected.w > 0.0 && all(uv > 0.0) && all(uv < 1.0)
        && projectedDepth > 0.0 && projectedDepth < 1.0;
}

float3 reflectedScene(float3 surface, float3 normal, float3 ray, float3 sky, float2 size)
{
    float3 origin = surface + normal * 0.035;
    float previous = 0.2, distance = 0.38;
    bool previousInFront = false;
    [loop]
    for (int i = 0; i < 15; ++i)
    {
        float2 uv = 0.0;
        float rayDepth = 0.0, sceneDepth = 1.0;
        if (!reflectionProbe(origin + ray * distance, size, uv, rayDepth, sceneDepth)) break;
        if (previousInFront && sceneDepth < 1.0 && rayDepth >= sceneDepth)
        {
            float low = previous, high = distance;
            [unroll]
            for (int step = 0; step < 4; ++step)
            {
                float middle = (low + high) * 0.5;
                reflectionProbe(origin + ray * middle, size, uv, rayDepth, sceneDepth);
                if (rayDepth < sceneDepth) low = middle;
                else high = middle;
            }
            float3 hit = origin + ray * high;
            reflectionProbe(hit, size, uv, rayDepth, sceneDepth);
            float3 visible = scenePosition(uv * size, sceneDepth, size);
            float miss = length(hit - visible);
            if (sceneDepth < 1.0 && visible.y > Material.Specular.x + 0.035 && miss < 0.30 + high * 0.045)
            {
                float margin = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
                float weight = smoothstep(0.0, 0.10, margin) * (1.0 - smoothstep(16.0, 31.0, high));
                return lerp(sky, SceneColor.SampleLevel(PondSampler, uv, 0).rgb, weight);
            }
        }
        previousInFront = rayDepth < sceneDepth;
        previous = distance;
        distance = distance * 1.25 + 0.03;
    }
    return sky;
}

bool inWaterVolume(float2 position)
{
    float bed = 0.0, filled = 0.0;
    groundAt(position, bed, filled);
    float2 uv = (position - WaterChannel.xy) * WaterChannel.zw;
    float river = all(uv >= 0.0) && all(uv <= 1.0) ? PondMask.SampleLevel(PondSampler, uv, 0) : 0.0;
    float coverage = WaterVolume.y != 0.0 ? pondCoverage(position, filled) : river;
    return bed < WaterVolume.x && coverage > 0.02;
}

float3 throughWater(float3 source, float distance)
{
    float3 transmission = exp2(-distance * float3(0.72, 0.29, 0.16));
    return source * transmission + float3(0.017, 0.19, 0.23) * waterSkyTint() * (1.0 - transmission);
}

float causticStrength()
{
    return Light.Enable ? lerp(1.0, SkySun.w, SkyCycle.w) : 0.0;
}

float4 underwaterView(float2 pixel)
{
    uint width = 1, height = 1;
    SceneDepth.GetDimensions(width, height);
    float2 size = float2(width, height);
    float sceneDepth = SceneDepth.Load(int3(int2(pixel), 0));
    float3 eye = CameraPosition.xyz;
    float3 scene = scenePosition(pixel, sceneDepth, size);
    float3 segment = scene - eye;
    float sceneDistance = max(0.0001, length(segment));
    float3 ray = segment / sceneDistance;
    float lengthInWater = min(sceneDistance, 30.0);
    if (ray.y > 0.0001)
        lengthInWater = min(lengthInWater, (WaterVolume.x - eye.y) / ray.y);

    // Painted pools and river strips can have open sides. Stop tinting at a dry bank.
    float stepLength = lengthInWater * 0.2;
    [loop]
    for (int i = 1; i <= 5; ++i)
    {
        float sampleDistance = stepLength * i;
        if (inWaterVolume(eye.xz + ray.xz * sampleDistance)) continue;
        float start = sampleDistance - stepLength;
        float end = sampleDistance;
        [unroll]
        for (int refine = 0; refine < 3; ++refine)
        {
            float middle = (start + end) * 0.5;
            if (inWaterVolume(eye.xz + ray.xz * middle)) start = middle;
            else end = middle;
        }
        lengthInWater = (start + end) * 0.5;
        break;
    }

    float3 original = SceneColor.Load(int3(int2(pixel), 0)).rgb;
    float3 color = original;
    float depth = WaterVolume.x - scene.y;
    float caustics = causticStrength();
    if (caustics > 0.0 && sceneDepth < 1.0 && depth > 0.0 && lengthInWater >= sceneDistance - 0.08)
    {
        float light = causticLight(scene.xz, Material.Emission.x, depth) * exp2(-depth * 0.68);
        color += light * caustics * 0.53 * float3(0.56, 0.91, 0.96);
    }
    float fade = smoothstep(0.0, 0.24, WaterVolume.x - eye.y) * Material.Diffuse.a;
    return float4(saturate(lerp(original, throughWater(color, lengthInWater), fade)), 1.0);
}

float4 main(PS_IN input) : SV_Target
{
    [branch]
    if (Material.Shininess > 0.0) return underwaterView(input.Position.xy);

    float elapsed = Material.Emission.x;
    float2 world = input.WorldPosition.xz;
    float2 worldDx = ddx(world), worldDy = ddy(world);
    float bed = 0.0, filled = 0.0;
    groundAt(world, bed, filled);
    float depth = Material.Specular.x - bed;
    float coastWidth = fwidth(depth);
    clip(depth - 0.00001);
    float coast = saturate(depth / max(0.0001, coastWidth));
    float shoreline = exp2(-max(depth, 0.0) * 9.0)
        * smoothstep(0.010, 0.085, coastWidth);
    float coverage = Material.TextureEnable ? pondCoverage(world, filled)
        : riverCoverage(input.TexCoord, Material.Emission.z, Material.Emission.w);
    clip(coverage - (Material.AlphaBlend ? 0.002 : 0.45));
    float2 ripple;
    float foamNoise;
    float bankFoam = 0.0;

    if (Material.TextureEnable)
    {
        float3 detail = waterDetail(world, elapsed, worldDx, worldDy);
        ripple = detail.xy;
        foamNoise = detail.z;
    }
    else
    {
        float across = input.TexCoord.x;
        float along = input.TexCoord.y;
        float bank = saturate(abs(across) * 2.0 / Material.Emission.z);

        // Blend two short advection cycles so the shear at the banks does not
        // accumulate indefinitely. The first coordinate is the channel width.
        float localSpeed = Material.Emission.y * lerp(1.0, 0.24, smoothstep(0.25, 0.97, bank));
        const float cycle = 4.0;
        float phase = frac(elapsed / cycle);
        float second = frac(phase + 0.5);
        float weight = 1.0 - abs(phase * 2.0 - 1.0);
        float2 flowA = float2(across, along - localSpeed * phase * cycle);
        float2 flowB = float2(across, along - localSpeed * second * cycle);
        float2 stretch = float2(1.28, 0.48);
        float2 localDx = ddx(input.TexCoord) * stretch, localDy = ddy(input.TexCoord) * stretch;
        float3 detail = lerp(waterDetail(flowB * stretch, 0.0, localDx, localDy),
            waterDetail(flowA * stretch, 0.0, localDx, localDy), weight);
        float2 local = detail.xy * stretch;
        local *= lerp(1.0, 0.60, bank);
        foamNoise = detail.z;
        bankFoam = smoothstep(0.67, 0.90, bank) * (1.0 - smoothstep(0.91, 1.0, bank))
            * exp2(-depth * 0.9) * saturate(Material.Emission.y * 0.35);

        // Rotate the channel-space ripple into world coordinates before reflecting.
        float2 tangent = normalize(input.Diffuse.xz);
        ripple = float2(-tangent.y, tangent.x) * local.x + tangent * local.y;
    }

    ripple *= lerp(0.28, 1.0, smoothstep(0.02, 0.85, depth));
    float3 toEye = normalize(CameraPosition.xyz - input.WorldPosition.xyz);
    float3 normal = normalize(input.Normal.xyz + float3(ripple.x, 0.0, ripple.y));
    bool belowSurface = toEye.y < 0.0;
    if (belowSurface) normal = -normal;
    float incidence = saturate(dot(normal, toEye));
    float fresnel = 0.022 + 0.978 * pow(1.0 - incidence, 5.0);
    float innerReflection = 1.0 - smoothstep(0.64, 0.74, incidence);
    float3 reflectionDirection = reflect(-toEye, normal);
    float opacity = Material.AlphaBlend ? Material.Diffuse.a : 1.0;
    float reflected = saturate(ReflectionStrength * fresnel) * opacity * (2.0 - opacity);
    float3 reflection = 0.0;
    [branch]
    if (reflected > 0.001)
        reflection = waterSkyReflection(reflectionDirection);
    uint width = 1, height = 1;
    float contact = 0.0;
    float3 opaqueWorld = input.WorldPosition.xyz - float3(0.0, depth, 0.0);
    float gap = 100.0;
    float path = min(10.0, depth / max(0.28, abs(toEye.y)));
    if (HasSceneDepth != 0.0)
    {
        SceneDepth.GetDimensions(width, height);
        float2 size = float2(width, height);
        float opaqueDepth = SceneDepth.Load(int3(int2(input.Position.xy), 0));
        opaqueWorld = scenePosition(input.Position.xy, opaqueDepth, size);
        gap = opaqueDepth < 1.0 ? length(opaqueWorld - input.WorldPosition.xyz) : 100.0;
        path = min(path, gap);
        contact = (1.0 - smoothstep(0.06, 0.34, gap)) * smoothstep(0.015, 0.095, fwidth(gap));
        [branch]
        if (!belowSurface && HasSceneColor != 0.0 && reflected > 0.065 && reflectionDirection.y > 0.015)
            reflection = reflectedScene(input.WorldPosition.xyz, normal, reflectionDirection, reflection, size);
    }
    float3 towardLight = normalize(-Light.Direction.xyz);
    float3 halfVector = normalize(toEye + towardLight);
    float glintAngle = saturate(dot(normal, halfVector));
    float sunlight = Light.Enable ? (0.76 * pow(glintAngle, 136.0) + 0.16 * pow(glintAngle, 38.0)) : 0.0;

    float3 subsurface = lerp(float3(0.10, 0.37, 0.38), float3(0.013, 0.14, 0.23), 1.0 - exp2(-path * 0.85));
    subsurface *= waterSkyTint();
    float foam = max(max(0.80 * shoreline, 0.86 * contact), 0.32 * bankFoam)
        * smoothstep(0.48, 0.72, foamNoise);
    float alpha = 1.0;
    if (Material.AlphaBlend)
    {
        alpha = coverage * coast;
        if (HasSceneColor != 0.0)
        {
            float offsetScale = belowSurface ? 33.0 : 36.0 * sqrt(saturate(path)) * sqrt(opacity);
            offsetScale *= smoothstep(0.015, 0.24, depth) * smoothstep(0.035, 0.30, gap);
            float2 distortion = (input.Normal.xz * 0.65 + ripple) * float2(1.0, -1.0);
            float3 background;
            float3 transmitted = refractedBackground(input, clamp(distortion * offsetScale, -5.0, 5.0),
                belowSurface, opaqueWorld, background);
            if (belowSurface)
            {
                float3 inside = lerp(float3(0.07, 0.26, 0.32), float3(0.014, 0.12, 0.17), innerReflection);
                return float4(lerp(transmitted, inside * waterSkyTint(), opacity * lerp(0.035, 0.97, innerReflection)), alpha);
            }
            float backgroundDepth = clamp(Material.Specular.x - background.y, 0.0, depth + 0.20);
            float caustics = causticStrength();
            float light = caustics > 0.0 ? causticLight(background.xz, elapsed, backgroundDepth) * caustics : 0.0;
            light *= smoothstep(0.02, 0.20, backgroundDepth) * exp2(-backgroundDepth * 0.55);
            subsurface = throughWater(transmitted + light * 0.22 * float3(0.48, 0.89, 0.94),
                min(path, length(input.WorldPosition.xyz - background)) * opacity);
        }
        else alpha *= opacity;
    }
    float3 color = lerp(subsurface, reflection, reflected);
    float3 sunTint = FogSettings.z > 0.5 ? SunColor.rgb * SunColor.a / 3.9 : Light.Diffuse.rgb;
    color += sunTint * sunlight * ReflectionStrength * opacity;
    color = lerp(color, float3(0.81, 0.91, 0.93) * waterSkyTint(), foam * opacity);
    return float4(FogSettings.z > 0.5 ? labFog(max(color, 0.0), input.WorldPosition.xyz) : saturate(color), alpha);
}
