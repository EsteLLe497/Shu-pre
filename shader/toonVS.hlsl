// =========================================================
// ファイルの情報[toonVS.hlsl]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "common.hlsl"
#include "toonCommon.hlsli"

void main(in VS_IN vertex, out PS_IN pixel)
{
    float4 worldPosition = mul(vertex.Position, World);
    float3 worldNormal = normalize(mul(float4(vertex.Normal.xyz, 0.0f), World).xyz);

    // 輪郭パスのときだけ、アニメーション後の法線方向に押し出す。
    worldPosition.xyz += worldNormal * outlineWidth;
    pixel.Position = mul(mul(worldPosition, View), Projection);
    pixel.WorldPosition = worldPosition;
    pixel.Normal = float4(worldNormal, 0.0f);
    pixel.Diffuse = vertex.Diffuse * Material.Diffuse;
    pixel.TexCoord = vertex.TexCoord;
}
