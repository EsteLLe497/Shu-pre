// ドーム内で受ける天空光。地面側には弱い反射光を残す。
cbuffer PlanetariumLight : register(b9)
{
    float4 planetariumCenterRadius;
    float4 planetariumSky;
};

float planetariumLightWeight(float3 worldPosition)
{
    float distanceToCenter = length(worldPosition - planetariumCenterRadius.xyz);
    return planetariumSky.a * (1.0 - smoothstep(
        max(0.0, planetariumCenterRadius.w - 0.45), planetariumCenterRadius.w, distanceToCenter));
}

float3 planetariumIrradiance(float3 worldNormal)
{
    const float skyVisibility = saturate(worldNormal.y * 0.5 + 0.5);
    const float3 bounce = float3(0.14, 0.16, 0.21);
    return lerp(bounce, planetariumSky.rgb, skyVisibility * skyVisibility);
}
