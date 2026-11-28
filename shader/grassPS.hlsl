#include "common.hlsl"

Texture2D grassTexture : register(t0);
SamplerState grassSampler : register(s0);

float4 main(PS_IN input) : SV_Target
{
    float4 color = input.Diffuse;
    if (Material.TextureEnable)
        color *= grassTexture.Sample(grassSampler, input.TexCoord);

    // 透明な板の部分を深度に書くと、後から描く水面に四角い欠けが出る。
    clip(color.a - 1.0f / 255.0f);
    color.a *= objectOpacity();
    return color;
}
