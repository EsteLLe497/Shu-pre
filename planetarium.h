// =========================================================
// 星座ドームの展開・カメラ・描画 [planetarium.h]
// =========================================================
#pragma once
#include <DirectXMath.h>
#include "Vector3.h"

namespace planetarium
{
    void init();
    void uninit();
    void reset();
    float updatePlayer();
    bool updateWorld();
    void frameCamera(const Vector3& playerPosition, const Vector3& orbitRotation, float orbitDistance,
        Vector3& eye, Vector3& target);
    void draw(DirectX::FXMMATRIX view, DirectX::CXMMATRIX projection);
}
