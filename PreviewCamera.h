// =========================================================
// ファイルの情報[PreviewCamera.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <DirectXMath.h>

class PreviewCamera
{
private:
    enum class Drag { none, orbit, pan, dolly };

    Drag m_drag = Drag::none;
    int m_dragButton = -1;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    float m_panZ = 0.0f;

    void zoom(float amount);

public:
    static constexpr float minDistance = 0.6f;
    static constexpr float maxDistance = 12.0f;

    float yaw = 0.0f;
    float pitch = 0.12f;
    float distance = 4.2f;
    float lookHeight = 0.62f;

    static bool mouseOverViewport();
    void handleMouse();
    bool dragging() const { return m_drag != Drag::none; }
    void reset();
    static DirectX::XMMATRIX projection();
    DirectX::XMMATRIX bind(const DirectX::XMFLOAT3& targetOffset) const;
};
