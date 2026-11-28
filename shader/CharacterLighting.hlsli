// =========================================================
// ファイルの情報[CharacterLighting.hlsli]
//
// 制作者:ozawa souma        日付:2026/11/28
// =========================================================

// FBX の材質名を AnimationModel 側で分類し、面ごとの照明処理を選ぶ。
static const int SkinMaterial = 1;
static const int HairMaterial = 2;
static const int FaceDetailMaterial = 3;
static const int ClothMaterial = 4;
static const int EyeIrisMaterial = 5;
static const int EyeWhiteMaterial = 6;
static const int EyeHighlightMaterial = 7;
static const float3 characterLuma = float3(0.2126, 0.7152, 0.0722);

float3 characterMaterialColor(int profile)
{
    if (profile == SkinMaterial) return float3(0.88, 0.31, 0.12);
    if (profile == HairMaterial) return float3(0.25, 0.46, 0.95);
    if (profile == FaceDetailMaterial) return float3(0.8, 0.25, 0.66);
    if (profile == ClothMaterial) return float3(0.19, 0.77, 0.28);
    if (profile == EyeIrisMaterial) return float3(0.14, 0.77, 0.87);
    if (profile == EyeWhiteMaterial) return float3(0.76, 0.81, 0.92);
    if (profile == EyeHighlightMaterial) return float3(1.0, 0.92, 0.23);
    return float3(0.47, 0.47, 0.47);
}

float3 shadeEyes(float3 base, float3 normal, float3 view, float2 uv, float3 sunlight, float fill, int profile)
{
    const float strength = CharacterDetail.z;
    const float luma = dot(base, characterLuma);
    const float light = saturate(dot(normal, SunDirection.xyz) * 0.36 + 0.64);
    const float daylight = dot(sunlight, characterLuma);
    const float illumination = fill + daylight * light;

    // 白目・虹彩・描き込まれた光点は別の明るさに保つ。瞳孔は暗いまま残す。
    float3 pigment = max(0.0, lerp(luma.xxx, base, 1.0 + 0.30 * strength));
    float irisMask = smoothstep(0.012, 0.14, luma);
    float3 halfVector = normalize(view + SunDirection.xyz + float3(0, 0.0001, 0));
    float reflected = pow(saturate(dot(halfVector, normal)), 22.0);
    float3 glint = sunlight * (0.12 * reflected * irisMask * strength);
    float irisContrast = lerp(1.0, 0.46 + 0.49 * irisMask, saturate(strength));

    float2 span = Material.Specular.zw - Material.Specular.xy;
    if (all(span > 0.0001))
    {
        float2 iris = (uv - Material.Specular.xy) / span;
        float upperIris = 1.0 - smoothstep(0.18, 0.78, iris.y);
        float3 ink = lerp(float3(0.71, 0.78, 1.0), float3(0.44, 0.56, 0.87), upperIris);
        pigment *= lerp(1.0, ink, saturate(strength * 0.65));
        float2 glintCenter = float2(0.34, 0.31) + float2(SunDirection.x, -SunDirection.y) * 0.09;
        float radius = length((iris - glintCenter) * float2(1.0, 1.15));
        float catchlight = 1.0 - smoothstep(0.035, 0.12, radius);
        glint += lerp(float3(0.72, 0.82, 1.0) * fill, sunlight, 0.66) * (catchlight * strength * irisMask * 0.85);
    }
    float3 result = pigment * (illumination * irisContrast) + glint;
    if (profile == EyeHighlightMaterial)
        result = base * (illumination + 0.75 * strength * saturate(illumination));
    else if (profile == EyeWhiteMaterial)
        result = base * (illumination * lerp(1.0, 0.67, saturate(strength)));
    return result;
}

float3 clothResponse(float3 base, float3 normal, float3 view, float2 uv,
    float3 sky, float3 sunlight, float band, float shadow)
{
    const float strength = CharacterDetail.x;
    // 織り目は遠方や斜めから見ると消し、細かなちらつきを抑える。
    float2 weaveUv = uv * 142.0;
    float footprint = max(fwidth(weaveUv.x), fwidth(weaveUv.y));
    float visibility = 1.0 - smoothstep(0.17, 0.52, footprint);
    float weave = sin(weaveUv.x * 6.283185) * sin(weaveUv.y * 6.283185) * visibility;
    float grazing = pow(1.0 - saturate(dot(normal, view)), 2.4);
    float upperLight = saturate(dot(normal, SunDirection.xyz) * 0.55 + 0.45);
    float luma = dot(base, characterLuma);
    float darkFabric = 1.0 - smoothstep(0.08, 0.65, luma);
    float3 fiberTint = lerp(base, float3(0.17, 0.19, 0.23), darkFabric * 0.77);
    float3 broadLight = sky * (0.035 + 0.25 * grazing)
        + sunlight * ((0.035 + 0.20 * grazing) * upperLight * shadow);
    float3 foldLight = sqrt(base) * sunlight * ((0.025 + 0.02 * weave) * band * shadow);
    return (fiberTint * broadLight * (0.86 + 0.14 * weave) + foldLight) * strength;
}

float3 shadeCharacter(float3 albedo, float3 normal, float3 worldPosition, float2 uv, int profile)
{
    float3 base = srgbToLinear(albedo);
    float3 view = normalize(CameraPosition.xyz - worldPosition);
    float incidence = dot(normal, SunDirection.xyz);
    float light = saturate(incidence * 0.62 + 0.38);
    float3 sunlight = SunColor.rgb * SunColor.a / 3.141593;
    float3 sky = labAmbient(normal);
    float skyLuma = dot(sky, characterLuma);

    float shadow = labShadow(worldPosition);
    float threshold = lightThreshold;
    // 影の境界には画面上の変化幅も使い、引きの視点でも階調を安定させる。
    float edge = clamp(max(edgeSoftness, fwidth(light) * 0.50), 0.04, 0.22);
    float3 shadowColor = float3(0.81, 0.85, 1.0);
    bool skin = profile == SkinMaterial;
    if (skin || profile == FaceDetailMaterial)
    {
        light = saturate(incidence * 0.42 + 0.58);
        threshold -= 0.06;
        edge = max(edge, 0.13);
        sky = lerp(sky, skyLuma.xxx, 0.81);
        shadowColor = float3(1.0, 0.87, 0.87);
        // 睫毛や前髪の影が顔全体の色をつぶさないようにする。
        shadow = lerp(shadow, 1.0, skin ? 0.52 : 0.72);
    }

    float band = shadingMode == 0 ? smoothstep(threshold - edge, threshold + edge, light) : light;
    if (skin)
    {
        // 肌アトラスに同居する黒い靴には、肌向けの赤みが乗らないようにする。
        float skinWeight = smoothstep(0.16, 0.48, dot(base, characterLuma)) * CharacterDetail.y;
        float terminator = 1.0 - abs(2.0 * band - 1.0);
        float3 warmSkin = base * float3(0.92, 0.78, 0.72);
        base = lerp(base, warmSkin, saturate(skinWeight * 0.75));
        sky *= lerp(1.0, float3(1.12, 0.94, 0.94), saturate(skinWeight * (0.42 + 0.44 * terminator)));
        sunlight *= 1.0 - saturate(skinWeight) * 0.16;
    }

    float3 rgb = base * (sky * lerp(shadowColor * shadowBrightness, 1.0, band) + sunlight * band * shadow);
    float fresnel = pow(1.0 - saturate(dot(view, normal)), rimPower);
    float rimMask = (profile == HairMaterial) ? 1.0 : (skin ? 0.24 : 0.45);
    if (profile != FaceDetailMaterial)
        rgb += rimColor.rgb * (fresnel * rimStrength * rimMask) * sqrt(base) * (skyLuma + 0.20 * sunlight);

    if (profile == ClothMaterial)
        rgb += clothResponse(base, normal, view, uv, sky, sunlight, band, shadow);
    if (profile == HairMaterial)
    {
        // 広く弱い艶を重ね、テクスチャ由来の毛束の明暗を残す。
        float3 halfway = normalize(view + SunDirection.xyz + float3(0.0, 0.0001, 0.0));
        float sheen = smoothstep(0.68, 0.98, saturate(dot(halfway, normal)));
        rgb += sqrt(base) * sunlight * (0.12 * sheen * band * shadow);
    }
    if (skin)
    {
        // 肌の明部を整えつつ、影の境目に柔らかい桃色を残す。
        float front = saturate(dot(view, normal) * 0.67 + 0.33);
        float skinMask = smoothstep(0.14, 0.55, dot(base, characterLuma));
        const float lightLevel = saturate(skyLuma + dot(sunlight, characterLuma) * 0.52);
        float3 faceFill = base * lerp(float3(0.54, 0.29, 0.40), float3(0.92, 0.76, 0.81), front) * lightLevel;
        rgb = lerp(rgb, rgb * 0.50 + faceFill, CharacterDetail.w * skinMask);
    }

    if (profile == EyeIrisMaterial || profile == EyeWhiteMaterial || profile == EyeHighlightMaterial)
    {
        const float skyFill = dot(sunlight, characterLuma) * (profile == EyeWhiteMaterial ? 0.055 : 0.14);
        float eyeFill = skyLuma + skyFill * CharacterDetail.z;
        rgb = shadeEyes(base, normal, view, uv, sunlight, eyeFill, profile);
    }
    return rgb;
}
