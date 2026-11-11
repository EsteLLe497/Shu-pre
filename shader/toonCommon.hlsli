// =========================================================
// ファイルの情報[toonCommon.hlsli]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
// C++ 側の ToonConstants と同じ順序にそろえる。
cbuffer ToonBuffer : register(b6)
{
    float lightThreshold;
    float edgeSoftness;
    float shadowBrightness;
    float outlineWidth;
    float exposure;
    float rimStrength;
    float rimPower;
    int shadingMode;
    int lightBands;
    int useBaseTextures;
    float2 padding;
    float4 outlineColor;
    float4 shadowTint;
    float4 rimColor;
    float4 lightDirection;
}
