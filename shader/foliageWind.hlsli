cbuffer FoliageWind : register(b12)
{
    float WindTime;
    float3 WindPadding;
}

void applyFoliageWind(float3 localPosition, float4x4 model, inout float3 worldPosition)
{
    // Only materials classified as foliage provide a model height.
    float4 wind = Material.Specular;
    [branch]
    if (wind.w <= 0.0f) return;

    float height = saturate(localPosition.y / wind.w + 0.5);
    float bend = saturate((height - wind.z) / (1.0 - wind.z));
    bend *= bend;

    const float2 direction = float2(0.8, 0.6);
    const float2 across = float2(-0.6, 0.8);
    float2 anchor = model[3].xz;
    float phase = dot(anchor, direction) * 0.245 - WindTime * 1.15;
    float gust = 0.28 + 0.52 * sin(phase) + 0.20 * sin(phase * 1.8 + anchor.x * 0.11);
    float flutter = sin(dot(worldPosition.xz, float2(1.43, -1.12)) + worldPosition.y * 2.6 + WindTime * 5.4);
    float heightWorld = wind.w * length(model[1].xyz);
    worldPosition.xz += (direction * (wind.x * gust) + across * (wind.y * flutter)) * (heightWorld * bend);
}
