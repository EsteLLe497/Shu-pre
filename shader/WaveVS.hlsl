#include "common.hlsl"

void gerstner(float2 origin, float2 direction, float frequency, float amplitude, float speed,
    inout float3 position, inout float3 tangentX, inout float3 tangentZ)
{
    float sine, cosine;
    sincos(frequency * dot(origin, direction) - Material.Emission.x * speed, sine, cosine);
    position += amplitude * float3(direction.x * cosine * 0.58, sine, direction.y * cosine * 0.58);
    float3 gradient = frequency * amplitude
        * float3(-direction.x * sine * 0.58, cosine, -direction.y * sine * 0.58);
    tangentX += gradient * direction.x;
    tangentZ += gradient * direction.y;
}

void main(in VS_IN input, uint vertex : SV_VertexID, out PS_IN output)
{
    output.WorldPosition = mul(input.Position, World);
    output.TexCoord = input.TexCoord;
    output.Normal = input.Normal;
    output.Diffuse = input.Diffuse;
    if (Material.Shininess > 0.0)
    {
        float2 corner = float2((vertex << 1) & 2, vertex & 2);
        output.Position = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
        return;
    }
    float2 origin = output.WorldPosition.xz;
    float strength = smoothstep(0.025, 0.70, input.Normal.x);
    if (!Material.TextureEnable)
    {
        float bank = abs(input.TexCoord.x) * 2.0 / Material.Emission.z;
        float fromEnd = min(input.TexCoord.y, Material.Emission.w - input.TexCoord.y);
        strength *= 0.66 * (1.0 - smoothstep(0.66, 1.0, bank)) * smoothstep(0.0, 0.90, fromEnd);
    }
    float3 displaced = output.WorldPosition.xyz;
    float3 tangentX = float3(1, 0, 0), tangentZ = float3(0, 0, 1);
    gerstner(origin, float2(0.91, 0.4146), 0.96, 0.100 * strength, 1.42, displaced, tangentX, tangentZ);
    gerstner(origin, float2(-0.55, 0.8352), 1.49, 0.050 * strength, 1.94, displaced, tangentX, tangentZ);
    gerstner(origin, float2(0.71, -0.7042), 2.24, 0.024 * strength, 2.39, displaced, tangentX, tangentZ);
    output.WorldPosition.xyz = displaced;
    output.Normal.xyz = normalize(cross(tangentZ, tangentX));
    output.Position = mul(mul(output.WorldPosition, View), Projection);
}
