#include "Collision.h"
#include <algorithm>

static float Clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

bool Collision::SphereVsSphere(
    const Vector3& centerA,
    float radiusA,
    const Vector3& centerB,
    float radiusB)
{
    Vector3 dir = centerB - centerA;
    float radius = radiusA + radiusB;

    return VectorMag(dir) < radius;
}

bool Collision::SphereVsAABB(
    const Vector3& sphereCenter,
    float sphereRadius,
    const Vector3& boxCenter,
    const Vector3& boxHalfSize)
{
    Vector3 closest(
        Clamp(sphereCenter.x, boxCenter.x - boxHalfSize.x, boxCenter.x + boxHalfSize.x),
        Clamp(sphereCenter.y, boxCenter.y - boxHalfSize.y, boxCenter.y + boxHalfSize.y),
        Clamp(sphereCenter.z, boxCenter.z - boxHalfSize.z, boxCenter.z + boxHalfSize.z)
    );

    Vector3 dir = sphereCenter - closest;

    return VectorMag(dir) < sphereRadius;
}