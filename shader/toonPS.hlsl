// =========================================================
// ファイルの情報[toonPS.hlsl]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "common.hlsl"
#include "toonCommon.hlsli"
#include "planetariumLight.hlsli"
#include "LabLighting.hlsli"
#include "CharacterLighting.hlsli"

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

    color.a *= objectOpacity();

    if (outlineWidth > 0.0f)
        return float4(FogSettings.z > 0.5 ? labFog(srgbToLinear(outlineColor.rgb), pixel.WorldPosition.xyz) : outlineColor.rgb, color.a);

    if (FogSettings.z > 0.5 && CharacterMaterialMode > 0.5)
    {
        int profile = (int)Material.Emission.w;
        if (CharacterMaterialMode > 1.5)
            return float4(srgbToLinear(characterMaterialColor(profile)), color.a);
        float3 shaded = shadingMode == 2 ? srgbToLinear(color.rgb)
            : shadeCharacter(color.rgb, normalize(pixel.Normal.xyz), pixel.WorldPosition.xyz, pixel.TexCoord, profile);
        return float4(labFog(shaded, pixel.WorldPosition.xyz), color.a);
    }

    if (shadingMode != 2)
    {
        float3 normal = normalize(pixel.Normal.xyz);
        const float night = planetariumLightWeight(pixel.WorldPosition.xyz);
        const float3 albedo = color.rgb;
        const bool labLighting = FogSettings.z > 0.5;
        float incidence = dot(normal, labLighting ? SunDirection.xyz : lightDirection.xyz);
        float light = labLighting ? saturate(incidence * 0.76 + 0.24) : saturate(incidence);
        float lightBand = shadingMode == 0 ? getLightBand(light) : light;
        if (labLighting)
        {
            float3 base = srgbToLinear(albedo);
            float3 sky = labAmbient(normal);
            sky = lerp(sky, dot(sky, float3(0.2126, 0.7152, 0.0722)).xxx, 0.4);
            sky *= lerp(shadowTint.rgb * shadowBrightness, 1.0, lightBand);
            float3 direct = SunColor.rgb * SunColor.a * (lightBand * labShadow(pixel.WorldPosition.xyz) / 3.141593);
            color.rgb = base * (sky + direct);
        }
        else
        {
            color.rgb *= lerp(shadowTint.rgb * shadowBrightness, 1.0f, lightBand);
        }

        // 視線に対して浅い角度の面を明るくし、輪郭付近に光を足す。
        if (rimStrength > 0.0f)
        {
            float3 viewDirection = normalize(CameraPosition.xyz - pixel.WorldPosition.xyz);
            float rim = pow(1.0f - saturate(dot(normal, viewDirection)), rimPower);
            color.rgb += rimColor.rgb * (rim * rimStrength);
        }
        if (!labLighting)
            color.rgb = lerp(color.rgb, albedo * planetariumIrradiance(normal), night);
    }

    if (FogSettings.z > 0.5)
        return float4(labFog(shadingMode == 2 ? srgbToLinear(color.rgb) : color.rgb, pixel.WorldPosition.xyz), color.a);
    return float4(color.rgb * exposure, color.a);
}
