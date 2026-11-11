#pragma once
#include "Vector3.h"

class Collision
{
public:
    static bool SphereVsSphere(
        const Vector3& centerA,
        float radiusA,
        const Vector3& centerB,
        float radiusB);

    static bool SphereVsAABB(
        const Vector3& sphereCenter,
        float sphereRadius,
        const Vector3& boxCenter,
        const Vector3& boxHalfSize);
};
