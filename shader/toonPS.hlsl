// =========================================================
// ファイルの情報[toonPS.hlsl]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "common.hlsl"
#include "toonCommon.hlsli"

Texture2D baseTexture : register(t0);
SamplerState baseSampler : register(s0);

// 閾値から明部までを段階に分け、それぞれの境界をぼかす。
float getLightBand(float light)
{
    float steps = lightBands - 1;
    float scale = steps / (1.0f - lightThreshold);
    float value = (light - lightThreshold) * scale;
    float boundary = floor(value + 0.5f);
    float softness = clamp(edgeSoftness * scale, 0.0001f, 0.49f);
    float blend = smoothstep(-softness, softness, value - boundary);
    return saturate((boundary + blend) / steps);
}

float4 main(PS_IN pixel) : SV_Target
{
    float4 color = pixel.Diffuse;
    if (Material.TextureEnable)
    {
        float4 texel = baseTexture.Sample(baseSampler, pixel.TexCoord);
        color.a *= texel.a;
        if (useBaseTextures != 0)
            color.rgb *= texel.rgb;
    }

    // 透明な画素が奥の肌や白目を遮らないようにする。
    clip(color.a - Material.AlphaCutoff);
    if (!Material.AlphaBlend)
        color.a = 1.0f;

    if (outlineWidth > 0.0f)
        return float4(outlineColor.rgb, color.a);

    if (shadingMode != 2)
    {
        float3 normal = normalize(pixel.Normal.xyz);
        float light = saturate(dot(normal, lightDirection.xyz));
        float lightBand = shadingMode == 0 ? getLightBand(light) : light;
        color.rgb *= lerp(shadowTint.rgb * shadowBrightness, 1.0f, lightBand);

        // 視線に対して浅い角度の面を明るくし、輪郭付近に光を足す。
        if (rimStrength > 0.0f)
        {
            float3 viewDirection = normalize(CameraPosition.xyz - pixel.WorldPosition.xyz);
            float rim = pow(1.0f - saturate(dot(normal, viewDirection)), rimPower);
            color.rgb += rimColor.rgb * (rim * rimStrength);
        }
    }

    return float4(color.rgb * exposure, color.a);
}
