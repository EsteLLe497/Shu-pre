// シーン深度をワールド座標へ戻し、球面を境に色と透過率を変える。
Texture2D<float> sceneDepth : register(t8);

cbuffer Dome : register(b8)
{
    matrix inverseViewProjection;
    float4 centerRadius;
    float4 tint;
    float4 settings;
};

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
    float3 color = lerp(interior, exterior, outside * (1.0 - edge * 0.6));
    float opacity = lerp(0.40 + edge * 0.35, 1.0, outside) * tint.a;
    return float4(color, opacity);
}
