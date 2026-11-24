#include "common.hlsl"

Texture2D BaseTexture : register(t0);
SamplerState BaseSampler : register(s0);

float4 main(PS_IN input) : SV_TARGET
{
    // 髪などの透過部分は元の形を保ち、表示部分だけ真っ白にする。
    float alpha = Material.Diffuse.a;
    if (Material.TextureEnable)
        alpha *= BaseTexture.Sample(BaseSampler, input.TexCoord).a;
    clip(alpha - Material.AlphaCutoff);
    return float4(1, 1, 1, Material.AlphaBlend ? alpha : 1);
}
