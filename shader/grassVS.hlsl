#include "common.hlsl"

// 草のワールド座標・頂点変換を修正 担当:ozawa souma        日付:2026/11/28
void main(in VS_IN input, out PS_IN output)
{
    // ワールド変換の結果を引き継ぎ、配置座標とビルボードの向きを反映する。
    output.WorldPosition = mul(input.Position, World);
    output.Position = mul(mul(output.WorldPosition, View), Projection);
    output.Normal = float4(mul(input.Normal.xyz, (float3x3)World), 0.0f);
    output.TexCoord = input.TexCoord;
    output.Diffuse = input.Diffuse * Material.Diffuse;
}
