// 全画面を覆う三角形を頂点バッファなしで描く。
float4 main(uint vertexId : SV_VertexID) : SV_POSITION
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
