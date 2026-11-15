// シーン深度をワールド座標へ戻し、球面を境に色と透過率を変える。
Texture2D<float> sceneDepth : register(t8);
Texture2D<float4> starPanorama : register(t9);
SamplerState starSampler : register(s2);

cbuffer Dome : register(b8)
{
    matrix inverseViewProjection;
    float4 centerRadius;
    float4 tint;
    float4 settings;
};

cbuffer Constellation : register(b10)
{
    float4 starColor;
    float4 starState;
    float4 starBounds;
    float4 previewLine;
    float4 previewState;
    float4 stars[16];
    float4 links[16];
};

// 星は白い芯と属性色のにじみ、線は細い芯と外側の光で描く。
float4 constellationGlow(float2 screenPixel)
{
    if (starColor.a <= 0.0 || any(screenPixel < starBounds.xy) || any(screenPixel > starBounds.zw))
        return 0.0;

    float core = 0.0;
    float glow = 0.0;
    for (uint i = 0; i < (uint)starState.x; ++i)
    {
        if (stars[i].z == 0.0)
            continue;

        const float2 offset = abs(screenPixel - stars[i].xy);
        const float radius = length(offset);
        const float emphasis = saturate(stars[i].w);
        const float availability = stars[i].w < 0.0 ? 0.24 : 1.0;
        core = max(core, (1.0 - smoothstep(1.2, 3.8 + emphasis * 1.4, radius)) * availability);
        const float crossLight = exp2(-min(offset.x, offset.y) * 2.2 - max(offset.x, offset.y) * 0.28);
        glow += (exp2(-radius * 0.22) * 0.68 + crossLight * 0.35) * (1.0 + emphasis * 0.6) * availability;
    }

    for (uint j = 0; j < (uint)starState.y; ++j)
    {
        const float2 a = stars[(uint)links[j].x].xy;
        const float2 axis = stars[(uint)links[j].y].xy - a;
        const float along = saturate(dot(screenPixel - a, axis) / max(dot(axis, axis), 0.0001));
        const float distanceToLine = length(screenPixel - a - axis * along);
        const float strength = lerp(0.22, 0.85 + starState.z * 0.15, links[j].z);
        core = max(core, (1.0 - smoothstep(0.45, 1.9, distanceToLine)) * strength);
        glow += exp2(-distanceToLine * 0.85) * strength * 0.5;
    }

    float3 color = lerp(starColor.rgb, float3(0.96, 0.99, 1.0), saturate(core));
    if (previewState.x != 0.0)
    {
        const float2 axis = previewLine.zw - previewLine.xy;
        const float along = saturate(dot(screenPixel - previewLine.xy, axis) / max(dot(axis, axis), 0.0001));
        const float distanceToLine = length(screenPixel - previewLine.xy - axis * along);
        const float lineCoverage = (1.0 - smoothstep(0.7, 2.5, distanceToLine)) * (previewState.x > 1.0 ? 0.9 : 0.47);
        const float3 lineColor = previewState.x < 0.0 ? float3(1.0, 0.29, 0.26) : lerp(starColor.rgb, 1.0, 0.38);
        color = lerp(color, lineColor, saturate(lineCoverage / max(core + glow + lineCoverage, 0.001)));
        glow += lineCoverage;
    }
    return float4(color, saturate(core + glow) * starColor.a);
}

// 中央照準は四隅を開け、星に合うと内側へ締まる。
float4 constellationReticle(float2 screenPixel)
{
    if (starState.w == 0.0)
        return 0.0;

    const float2 delta = abs(screenPixel - 0.5 / settings.xy);
    const float size = starState.w > 1.0 ? 9.0 : 12.0;
    const float corner = min(delta.x, delta.y);
    const float edge = abs(max(delta.x, delta.y) - size);
    const float arms = (1.0 - smoothstep(0.45, 1.55, edge)) * smoothstep(size - 5.0, size - 3.5, corner);
    const float halo = exp2(-edge * 1.65) * smoothstep(size - 6.0, size - 2.0, corner);
    const float dotCore = 1.0 - smoothstep(0.45, 1.4, length(delta));
    const float3 color = starState.w < 0.0 ? float3(1.0, 0.29, 0.26)
        : lerp(starColor.rgb, float3(0.94, 0.99, 1.0), saturate(arms + dotCore));
    return float4(color,
        saturate(arms * 0.85 + halo * 0.27 + dotCore * 0.8));
}

float4 main(float4 screen : SV_POSITION) : SV_TARGET
{
    float depth = sceneDepth.Load(int3(int2(screen.xy), 0));
    float2 ndc = screen.xy * settings.xy * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 world = mul(float4(ndc, depth, 1.0), inverseViewProjection);
    float3 relative = world.xyz / world.w - centerRadius.xyz;
    float radius = centerRadius.w;
    float distanceToEdge = length(relative) - radius;
    float outside = depth >= 0.99999 ? 1.0 : smoothstep(-0.2, 0.35, distanceToEdge);
    float edge = 1.0 - smoothstep(0.0, 0.45, abs(distanceToEdge));
    edge *= 1.0 - step(0.99999, depth);
    float ring = edge * (1.0 - smoothstep(0.05, 0.65, abs(relative.y)));

    // 内側は地形を暗く染め、境界だけ細い光を重ねる。
    float3 interior = tint.rgb * 0.16 + tint.rgb * (edge * 0.37 + ring * 0.52);
    // 球面を横切る視線から、テクスチャなしでドームの色の深さを出す。
    float4 nearPoint = mul(float4(ndc, 0.0, 1.0), inverseViewProjection);
    float3 rayOrigin = nearPoint.xyz / nearPoint.w - centerRadius.xyz;
    float4 farPoint = mul(float4(ndc, 1.0, 1.0), inverseViewProjection);
    float3 ray = normalize(farPoint.xyz / farPoint.w - centerRadius.xyz - rayOrigin);
    float along = dot(rayOrigin, ray);
    float discriminant = along * along - dot(rayOrigin, rayOrigin) + radius * radius;
    float exitDistance = -along + sqrt(max(0.0, discriminant));
    float3 shellNormal = (rayOrigin + ray * exitDistance) / radius;
    float shell = step(0.0, discriminant) * step(0.0, exitDistance);
    float horizon = pow(saturate(1.0 - abs(shellNormal.y)), 3.0);
    float3 exterior = float3(0.009, 0.015, 0.038)
        + tint.rgb * (0.019 + shell * (0.04 + horizon * 0.17));
    // 全天パノラマを球面に貼り、銀河の帯が斜めに見えるように傾ける。
    if (settings.z > 0.5 && shell > 0.0 && outside > 0.0)
    {
        const float pi = 3.14159265;
        const float3 direction = float3(shellNormal.x * 0.819 + shellNormal.y * 0.574,
            shellNormal.y * 0.819 - shellNormal.x * 0.574, shellNormal.z);
        const float2 uv = float2(atan2(direction.z, direction.x) / (2.0 * pi) + 0.5,
            acos(clamp(direction.y, -1.0, 1.0)) / pi);
        const float3 sky = starPanorama.SampleLevel(starSampler, uv, 0).rgb;
        const float3 lifted = pow(max(sky - 0.004, 0.0), 0.72);
        exterior = saturate(exterior + lifted * (0.9 + tint.rgb * 1.55));
    }
    float3 color = lerp(interior, exterior, outside * (1.0 - edge * 0.6));
    float opacity = lerp(0.12 + edge * 0.5, 1.0, outside) * tint.a;
    const float4 overlay = constellationGlow(screen.xy);
    const float alpha = overlay.a + opacity * (1.0 - overlay.a);
    const float3 premultiplied = overlay.rgb * overlay.a + color * opacity * (1.0 - overlay.a);
    const float4 reticle = constellationReticle(screen.xy);
    const float finalAlpha = reticle.a + alpha * (1.0 - reticle.a);
    const float3 finalColor = reticle.rgb * reticle.a + premultiplied * (1.0 - reticle.a);
    return float4(finalColor / max(finalAlpha, 0.0001), finalAlpha);
}
