// =========================================================
// ファイルの情報[LabLighting.hlsli]
//
// 制作者:ozawa souma        日付:2026/11/28
// =========================================================

// 検証シーンだけがこの定数を設定する。通常ゲームでは従来の照明を使用する。
cbuffer LabLighting : register(b12)
{
    float4 SunDirection;
    float4 SunColor;
    float4 AmbientColor;
    float4 FogColor;
    float4 FogSettings;
    float3 ShadowSettings;
    float CharacterMaterialMode;
    float4 PostSettings;
    float4x4 SunViewProjection;
    float4x4 LabInverseViewProjection;
    float4 CharacterDetail;
    float4 GroveLight;
    // 画面効果は検証シーンの後処理だけが参照する。既存の項目の配置は変えない。
    float4x4 LabViewProjection;
    float4 AoSettings;    // x: 強度, y: 半径(m), z: 消える距離(m), w: 有効
    float4 BloomSettings; // x: 強度, y: しきい値, z: ひざ幅, w: 有効
    float4 GradeSettings; // x: 明暗の色分け, y: 周辺減光, z: 緑のくすみ, w: 有効
    float4 HazeSettings;  // x: 高さによる薄まり(1/m), y: 太陽側の明るさ, z: 霞の明るさ, w: 有効
};

Texture2D<float> LabShadow : register(t10);
SamplerComparisonState LabShadowSampler : register(s4);

float3 srgbToLinear(float3 c)
{
    return lerp(c / 12.92, pow((max(c, 0.0) + 0.055) / 1.055, 2.4), step(0.04045, c));
}

// 霞の色。地平線の空と同じ色にし、太陽の方向ほど前方散乱で明るく暖かくする。
float3 hazeInscatter(float3 ray)
{
    const float toward = saturate(dot(ray, SunDirection.xyz));
    const float forward = pow(toward, 6.0) * 0.55 + pow(toward, 1.6) * 0.18;
    return FogColor.rgb * HazeSettings.z + SunColor.rgb * SunColor.a * 0.085 * forward * HazeSettings.y;
}

float3 skyGradient(float3 direction)
{
    float elevation = saturate(direction.y);
    if (HazeSettings.w < 0.5)
        return lerp(float3(0.29, 0.64, 1.0), float3(0.014, 0.105, 0.46), pow(elevation, 0.45));
    // 実際の空は地平線付近が白っぽく霞み、天頂へ向かって深い青になる。
    const float3 zenith = float3(0.07, 0.20, 0.60);
    return lerp(hazeInscatter(direction) * 1.12, zenith, pow(elevation, 0.52));
}

float labShadow(float3 position)
{
    if (ShadowSettings.x <= 0.0) return 1.0;
    float4 clipPosition = mul(float4(position, 1.0), SunViewProjection);
    float3 ndc = clipPosition.xyz / clipPosition.w;
    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    // 斜面ではフィルター先の深度も傾け、地面や幹にシャドウマップの縞が出るのを抑える。
    const float2 uvX = ddx(uv), uvY = ddy(uv);
    const float depthX = ddx(ndc.z), depthY = ddy(ndc.z);
    const float determinant = uvX.x*uvY.y-uvX.y*uvY.x;
    const float2 slope = abs(determinant) > 1e-12
        ? float2(depthX*uvY.y-depthY*uvX.y,uvX.x*depthY-uvY.x*depthX) / determinant : 0.0;
    if (any(uv <= 0.0) || any(uv >= 1.0) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    // 中央を強くした九点の比較で、葉の細かい影を残しつつ輪郭をならす。
    float result = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float2 offset = float2(x,y)*ShadowSettings.y;
            const float receiverDepth = ndc.z + clamp(dot(slope,offset),-0.00052,0.00052);
            result += (2-abs(x))*(2-abs(y))*LabShadow.SampleCmpLevelZero(LabShadowSampler,
                uv+offset, receiverDepth-ShadowSettings.z);
        }
    return lerp(1.0, result / 16.0, ShadowSettings.x);
}

float3 labAmbient(float3 normal)
{
    return AmbientColor.rgb * AmbientColor.a * lerp(0.48, 1.0, saturate(normal.y * 0.5 + 0.5));
}

float3 labFog(float3 rgb, float3 position)
{
    const float3 toPoint = position - CameraPosition.xyz;
    const float distance = length(toPoint);
    float distanceInFog = max(distance - FogSettings.x, 0.0);
    if (HazeSettings.w < 0.5)
        return lerp(rgb, FogColor.rgb, 1.0 - exp(-distanceInFog * FogSettings.y));

    // 空気の濃さを高さで指数的に減らし、視線上の平均をとる。谷は霞み、稜線の上は澄む。
    const float falloff = max(HazeSettings.x, 0.0001);
    const float startHeight = max(CameraPosition.y, 0.0);
    const float endHeight = max(position.y, 0.0);
    const float rise = (endHeight - startHeight) * falloff;
    const float averageDensity = abs(rise) > 0.0001
        ? (exp(-startHeight * falloff) - exp(-endHeight * falloff)) / rise
        : exp(-startHeight * falloff);
    const float depth = distanceInFog * FogSettings.y * averageDensity;
    // 青い光ほど散乱されやすく、遠景ほど青白く沈む。
    const float3 transmittance = exp(-depth * float3(0.82, 0.95, 1.12));
    const float3 ray = toPoint / max(distance, 0.0001);
    return rgb * transmittance + hazeInscatter(ray) * (1.0 - transmittance);
}

float labHash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float labNoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(labHash(i), labHash(i + float2(1, 0)), f.x),
        lerp(labHash(i + float2(0, 1)), labHash(i + 1), f.x), f.y);
}

float labCloudNoise(float2 p)
{
    float sum = 0.0;
    float weight = 0.55;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        sum += labNoise(p) * weight;
        p = p * 2.13 + 9.7;
        weight *= 0.48;
    }
    return sum;
}

float3 labSky(float3 ray)
{
    float3 rgb = skyGradient(ray);
    // 雲のシルエットと青い底面を、異なるスケールのノイズで作る。
    float2 dome = float2(atan2(ray.x, ray.z) * 5.9 + 2.7, ray.y * 11.0);
    float shape = labCloudNoise(dome + 0.6 * labCloudNoise(dome * 0.65));
    float cap = smoothstep(0.47, 0.57, shape) * smoothstep(0.005, 0.10, ray.y);
    float relief = saturate((shape - labCloudNoise(dome + float2(-0.08, 0.23))) * 4 + ray.y * 1.9);
    float3 cloud = lerp(float3(0.46, 0.66, 0.83), float3(1.65, 1.69, 1.72), relief);
    if (HazeSettings.w > 0.5)
    {
        // 雲の底は空の色を映して青灰色に、地平線に近い雲ほど霞に溶かす。
        cloud = lerp(float3(0.42, 0.52, 0.64), float3(1.55, 1.55, 1.50), relief);
        cloud = lerp(hazeInscatter(ray) * 1.12, cloud, smoothstep(0.0, 0.32, ray.y));
    }
    rgb = lerp(rgb, cloud, cap);
    float glow = pow(saturate(dot(ray, SunDirection.xyz)), 150.0);
    rgb += SunColor.rgb * SunColor.a * glow * 0.4;
    return rgb;
}
