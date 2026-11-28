#include "common.hlsl"

void main(uint vertexId : SV_VertexID, out float4 position : SV_POSITION, out float3 ray : TEXCOORD0)
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    float2 clip = corner * float2(2.0, -2.0) + float2(-1.0, 1.0);
    position = float4(clip, 1.0, 1.0);
    float3 viewRay = float3(clip / float2(Projection[0][0], Projection[1][1]), 1.0);
    ray = mul((float3x3)View, viewRay);
}
