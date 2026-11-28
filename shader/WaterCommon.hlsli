#ifndef WATER_COMMON_HLSLI
#define WATER_COMMON_HLSLI

Texture2D<float> TerrainHeight : register(t5);

cbuffer WaterFrame : register(b7)
{
    matrix InverseViewProjection;
    matrix ViewProjection;
    float ReflectionStrength;
    float HasSceneDepth;
    float HasSceneColor;
    float WaterFramePadding;
    float4 TerrainGrid;
    float4 WaterVolume;
    float4 WaterChannel;
};

float terrainAt(float2 world, out int2 cell, out bool lowerTriangle)
{
    uint width = 2, rows = 2;
    TerrainHeight.GetDimensions(width, rows);
    float2 limit = float2(width, rows) - 1.0;
    float2 raw = (world - TerrainGrid.xy) / TerrainGrid.zw;
    bool inside = all(raw >= 0.0) && all(raw <= limit);
    float2 grid = clamp(raw, 0.0, limit);
    cell = int2(min(grid, limit - 1.0));
    float2 f = grid - cell;
    float right = TerrainHeight.Load(int3(cell + int2(1, 0), 0));
    float bottom = TerrainHeight.Load(int3(cell + int2(0, 1), 0));
    lowerTriangle = f.x + f.y <= 1.0;
    float corner = TerrainHeight.Load(int3(cell + (lowerTriangle ? int2(0, 0) : int2(1, 1)), 0));
    float ground = lowerTriangle ? corner + dot(f, float2(right, bottom) - corner)
        : corner + dot(1.0 - f, float2(bottom, right) - corner);
    return inside ? ground : 1000000.0;
}

#endif
